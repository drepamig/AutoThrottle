﻿// This Source Code Form is subject to the terms of the Mozilla Public
// License, v.2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.


#ifdef IBM

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
) {
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

#endif // IBM

#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>

#include "XPLM/XPLMDataAccess.h"
#include "XPLM/XPLMDisplay.h"
#include "XPLM/XPLMGraphics.h"
#include "XPLM/XPLMMenus.h"
#include "XPLM/XPLMPlugin.h"
#include "XPLM/XPLMProcessing.h"
#include "XPLM/XPLMUtilities.h"

#include "Widgets/XPWidgets.h"
#include "Widgets/XPStandardWidgets.h"

#if LIN
#include <GL/gl.h>
#elif __GNUC__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "cereal/archives/json.hpp"

#include "ap_core.h"

// Window handle
static XPLMWindowID window;

// Callbacks
void draw_hello_world(XPLMWindowID in_window_id, void* in_refcon);
int dummy_mouse_handler(XPLMWindowID in_window_id, int x, int y, int is_down, void* in_refcon) { return 0; }
XPLMCursorStatus dummy_cursor_status_handler(XPLMWindowID in_window_id, int x, int y, void* in_refcon) { return xplm_CursorDefault; }
int dummy_wheel_handler(XPLMWindowID in_window_id, int x, int y, int wheel, int clicks, void* in_refcon) { return 0; }
void dummy_key_handler(XPLMWindowID in_window_id, char key, XPLMKeyFlags flags, char virtual_key, void* in_refcon, int losing_focus) {}



void MenuHandler(void* inMenuRef, void* inItemRef);

#ifdef _DEBUG
XPLMFlightLoopID debug_beforeFlightLoop;
float debug_beforeFlightLoopCallback(float inTimeSinceLastCall, float inTimeSinceLastFlightLoop, int inCounter, void* inRefcon);
XPLMFlightLoopID debug_afterFlightLoop;
float debug_afterFlightLoopCallback(float inTimeSinceLastCall, float inTimeSinceLastFlightLoop, int inCounter, void* inRefcon);
#endif // _DEBUG


void setupWidgets();
XPWidgetID settingsWidget;
XPWidgetID settingsSubWidgets[7];



PLUGIN_API int XPluginStart(char* outName, char* outSig, char* outDesc) {
	strcpy_s(outName, 19, "AutoThrottlePlugin");
	strcpy_s(outSig, 32, "smarky55.tbm.autothrottleplugin");
	strcpy_s(outDesc, 57, "An autothrottle implementation for the Hotstart TBM 900.");

	XPLMEnableFeature("XPLM_USE_NATIVE_WIDGET_WINDOWS", true);
	
	XPLMDebugString("[AutoThrottle] Init\n");
	
// Test window
#ifdef _DEBUG

	XPLMCreateWindow_t params;
	params.structSize = sizeof(params);
	params.visible = 1;
	params.drawWindowFunc = draw_hello_world;
	params.handleMouseWheelFunc = dummy_wheel_handler;
	params.handleMouseClickFunc = dummy_mouse_handler;
	params.handleRightClickFunc = dummy_mouse_handler;
	params.handleCursorFunc = dummy_cursor_status_handler;
	params.handleKeyFunc = dummy_key_handler;
	params.refcon = NULL;
	params.layer = xplm_WindowLayerFloatingWindows;
	params.decorateAsFloatingWindow = xplm_WindowDecorationRoundRectangle;

	int left, bottom, right, top;
	XPLMGetScreenBoundsGlobal(&left, &top, &right, &bottom);
	params.left = left + 50;
	params.bottom = bottom + 150;
	params.right = params.left + 200;
	params.top = params.bottom + 200;

	window = XPLMCreateWindowEx(&params);
	XPLMSetWindowPositioningMode(window, xplm_WindowPositionFree, -1);
	XPLMSetWindowResizingLimits(window, 200, 200, 300, 300);
	XPLMSetWindowTitle(window, "Test window");

#endif // DEBUG

#ifdef _DEBUG
	Performance p;
	p.test();

	std::stringstream ss;

	ss << p.tables["TST"].getValue(2.5, 35) << std::endl;

	cereal::JSONOutputArchive output(ss);
	output(cereal::make_nvp("testperf", p));
	ss << std::endl;

	//XPLMDebugString(ss.str().c_str());

#endif // _DEBUG

	char sysPath[512];
	XPLMGetSystemPath(sysPath);
	std::string perfPath = sysPath;
	perfPath.append("Resources\\plugins\\AutoThrottle\\tbm9.perf");
	XPLMDebugString(perfPath.c_str());

	std::ifstream perfFile(perfPath);


	try {
		cereal::JSONInputArchive perfArchive(perfFile);
		perfArchive(perf);
	}
	catch (const std::exception& e) {
		XPLMDebugString(e.what());
	}

	perfFile.close();
	
	timeDref = XPLMFindDataRef("sim/time/total_running_time_sec");
	throttleDref = XPLMFindDataRef("sim/cockpit2/engine/actuators/throttle_ratio_all");
	trqDref = XPLMFindDataRef("sim/flightmodel/engine/ENGN_TRQ");

	dJoyAxisAssignments = XPLMFindDataRef("sim/joystick/joystick_axis_assignments");

	XPLMMenuID myMenu;
	int myMenuSubItem;

	myMenuSubItem = XPLMAppendMenuItem(XPLMFindPluginsMenu(), "AutoThrottle", NULL, 0);
	myMenu = XPLMCreateMenu("AutoThrottle", XPLMFindPluginsMenu(), myMenuSubItem, MenuHandler, NULL);
	XPLMAppendMenuItem(myMenu, "Test", reinterpret_cast<void*>(1), 0);
	XPLMAppendMenuItem(myMenu, "Settings", reinterpret_cast<void*>(2), 0);
	XPLMAppendMenuItem(myMenu, "Debug flight loops", reinterpret_cast<void*>(3), 0);
	
	setupFlightLoop();

#ifdef _DEBUG
	XPLMCreateFlightLoop_t createFlightLoop;
	createFlightLoop.structSize = sizeof(XPLMCreateFlightLoop_t);
	createFlightLoop.phase = xplm_FlightLoop_Phase_BeforeFlightModel;
	createFlightLoop.callbackFunc = debug_beforeFlightLoopCallback;
	debug_beforeFlightLoop = XPLMCreateFlightLoop(&createFlightLoop);

	createFlightLoop.phase = xplm_FlightLoop_Phase_AfterFlightModel;
	createFlightLoop.callbackFunc = debug_afterFlightLoopCallback;
	debug_afterFlightLoop = XPLMCreateFlightLoop(&createFlightLoop);
#endif // _DEBUG

	trqPid.setGains(1.0f, 0.0f, 0.0f);

	setupWidgets();

	return window != NULL;
}

PLUGIN_API void XPluginStop(void) {

	XPDestroyWidget(settingsWidget, 1);
	XPLMDestroyFlightLoop(flightLoop);

#ifdef _DEBUG

	XPLMDestroyFlightLoop(debug_beforeFlightLoop);
	XPLMDestroyFlightLoop(debug_afterFlightLoop);

	XPLMDestroyWindow(window);
	window = NULL;

#endif // _DEBUG
}

PLUGIN_API int XPluginEnable(void) { return 1; }
PLUGIN_API void XPluginDisable(void) {}
PLUGIN_API void XPluginReceiveMessage(XPLMPluginID inFrom, int inMsg, void* inParam) {}

#ifdef _DEBUG
float debug_pidspeed;
float debug_trq;
float debug_throttle;

bool debug_flightloopTest = false;
bool debug_flightLoopEnabled = false;
#endif // _DEBUG


void draw_hello_world(XPLMWindowID inID, void* inRefcon) {
	XPLMSetGraphicsState(0, 0, 0, 0, 1, 1, 0);

	int l, t, r, b;
	XPLMGetWindowGeometry(inID, &l, &t, &r, &b);
	float col_white[] = { 1.0, 1.0, 1.0 };
	XPLMDrawString(col_white, l + 10, t - 20, const_cast<char*>("Testing!"), NULL, xplmFont_Proportional);

#ifdef _DEBUG
	std::stringstream ss;
	ss << "Pidspeed: " << debug_pidspeed;
	XPLMDrawString(col_white, l + 10, t - 40, const_cast<char*>(ss.str().c_str()), NULL, xplmFont_Proportional);
	ss.str("");
	ss << "Torque: " << debug_trq;
	XPLMDrawString(col_white, l + 10, t - 60, const_cast<char*>(ss.str().c_str()), NULL, xplmFont_Proportional);
	ss.str("");
	ss << "Throttle: " << debug_throttle;
	XPLMDrawString(col_white, l + 10, t - 80, const_cast<char*>(ss.str().c_str()), NULL, xplmFont_Proportional);
#endif // _DEBUG

}

void MenuHandler(void* inMenuRef, void* inItemRef) {
	switch (reinterpret_cast<intptr_t>(inItemRef)) {
	case 1: // Testing
		if (bAutoThrottleEnabled) {
			deactivateAutoThrottle();
		}
		else {
			trqPid.setTarget(1500.0f);
			activateAutoThrottle();
		}
		break;
	case 2:
		if (XPIsWidgetVisible(settingsWidget)) {
			XPHideWidget(settingsWidget);
		}
		else {
			XPShowWidget(settingsWidget);
		}
		break;
	case 3:
#ifdef _DEBUG
		if (debug_flightLoopEnabled) {
			XPLMScheduleFlightLoop(debug_beforeFlightLoop, 0.0f, false);
			XPLMScheduleFlightLoop(debug_afterFlightLoop, 0.0f, false);
			debug_flightLoopEnabled = false;
			debug_flightloopTest = false;
		}
		else {
			XPLMScheduleFlightLoop(debug_beforeFlightLoop, -1.0f, false);
			XPLMScheduleFlightLoop(debug_afterFlightLoop, -1.0f, false);
			debug_flightLoopEnabled = true;
		}
#endif // _DEBUG
		break;
	default:
		break;
	}
}


#ifdef _DEBUG
float debug_beforeFlightLoopCallback(float inTimeSinceLastCall, float inTimeSinceLastFlightLoop, int inCounter, void* inRefcon) {
	
	if (debug_flightloopTest && XPLMGetDataf(throttleDref) != 0.2f) {
		XPLMDebugString("[Autothrottle] Before flight loop.\n");
	}
	XPLMSetDataf(throttleDref, 0.2f);
	debug_flightloopTest = true;

	return -1.0;
}

float debug_afterFlightLoopCallback(float inTimeSinceLastCall, float inTimeSinceLastFlightLoop, int inCounter, void* inRefcon) {

	if (debug_flightloopTest && XPLMGetDataf(throttleDref) != 0.2f) {
		XPLMDebugString("[Autothrottle] After flight loop.\n");
	}
	XPLMSetDataf(throttleDref, 0.2f);
	debug_flightloopTest = true;

	return -1.0;
}
#endif // _DEBUG


int settingsWidgetFunc(XPWidgetMessage inMessage, XPWidgetID inWidget, intptr_t param1, intptr_t param2) {
	if (inWidget == settingsWidget) {
		if (inMessage == xpMessage_CloseButtonPushed) {
			XPHideWidget(inWidget);
			return 1;
		}
	}
	else if (inWidget == settingsSubWidgets[6]) {
		if (inMessage == xpMsg_PushButtonPressed) {
			char buffer[64];
			std::string num;
			float kP, kI, kD;
			try {

				XPGetWidgetDescriptor(settingsSubWidgets[1], buffer, 64);
				num = buffer;
				kP = std::stof(num);
				XPGetWidgetDescriptor(settingsSubWidgets[3], buffer, 64);
				num = buffer;
				kI = std::stof(num);
				XPGetWidgetDescriptor(settingsSubWidgets[5], buffer, 64);
				num = buffer;
				kD = std::stof(num);

				trqPid.setGains(kP, kI, kD);
#ifdef _DEBUG
				std::stringstream ss;
				trqPid.getGains(&kP, &kI, &kD);
				ss << "Pid gains: " << kP << " " << kI << " " << kD << std::endl;
				XPLMDebugString(ss.str().c_str());
#endif // _DEBUG
			}
			catch (const std::exception& e) {
				XPLMDebugString(e.what());
				XPLMDebugString("\n");
				XPLMDebugString(num.c_str());
				XPLMDebugString("\n");
			}
			XPHideWidget(settingsWidget);
			return 1;
		}
	}
	return 0;
}

void setupWidgets() {
	int screenLeft, screenRight, screenTop, screenBottom;
	XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);

	int settingsLeft = 50 + screenLeft, settingsBottom = 400 + screenBottom, settingsWidth = 300, settingsHeight = 100;
	settingsWidget = XPCreateWidget( settingsLeft,
				settingsBottom + settingsHeight,
				settingsLeft + settingsWidth,
				settingsBottom,
				0, "Autothrottle Settings", 1, NULL, xpWidgetClass_MainWindow);
	XPSetWidgetProperty(settingsWidget, xpProperty_MainWindowHasCloseBoxes, 1);
	XPAddWidgetCallback(settingsWidget, settingsWidgetFunc);

	float kP, kI, kD;
	trqPid.getGains(&kP, &kI, &kD);

	settingsSubWidgets[0] = XPCreateWidget(settingsLeft + 10, settingsBottom + 50, settingsLeft + 30, settingsBottom + 40, 1, "kP", 0, settingsWidget, xpWidgetClass_Caption);
	settingsSubWidgets[1] = XPCreateWidget(settingsLeft + 30, settingsBottom + 50, settingsLeft + 70, settingsBottom + 35, 1, std::to_string(kP).c_str(), 0, settingsWidget, xpWidgetClass_TextField);
	settingsSubWidgets[2] = XPCreateWidget(settingsLeft + 80, settingsBottom + 50, settingsLeft + 100, settingsBottom + 40, 1, "kI", 0, settingsWidget, xpWidgetClass_Caption);
	settingsSubWidgets[3] = XPCreateWidget(settingsLeft + 100, settingsBottom + 50, settingsLeft + 140, settingsBottom + 35, 1, std::to_string(kI).c_str(), 0, settingsWidget, xpWidgetClass_TextField);
	settingsSubWidgets[4] = XPCreateWidget(settingsLeft + 150, settingsBottom + 50, settingsLeft + 170, settingsBottom + 40, 1, "kD", 0, settingsWidget, xpWidgetClass_Caption);
	settingsSubWidgets[5] = XPCreateWidget(settingsLeft + 170, settingsBottom + 50, settingsLeft + 210, settingsBottom + 35, 1, std::to_string(kD).c_str(), 0, settingsWidget, xpWidgetClass_TextField);
	settingsSubWidgets[6] = XPCreateWidget(settingsLeft + settingsWidth - 55, settingsBottom + 25, settingsLeft + settingsWidth - 5, settingsBottom + 5, 1, "Save", 0, settingsWidget, xpWidgetClass_Button);
	XPAddWidgetCallback(settingsSubWidgets[6], settingsWidgetFunc);

}