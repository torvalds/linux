//===-- MIUtilDebug.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Third party headers:
#ifdef _WIN32
#include <windows.h>
#endif

// In-house headers:
#include "MICmnLog.h"
#include "MIDriver.h"
#include "MIUtilDebug.h"

//++
//------------------------------------------------------------------------------------
// Details: CMIUtilDebug constructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMIUtilDebug::CMIUtilDebug() {}

//++
//------------------------------------------------------------------------------------
// Details: CMIUtilDebug destructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMIUtilDebug::~CMIUtilDebug() {}

//++
//------------------------------------------------------------------------------------
// Details: Temporarily stall the process/application to give the programmer the
//          opportunity to attach a debugger. How to use: Put a break in the
//          programmer
//          where you want to visit, run the application then attach your
//          debugger to the
//          application. Hit the debugger's pause button and the debugger should
//          should
//          show this loop. Change the i variable value to break out of the loop
//          and
//          visit your break point.
// Type:    Static method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
void CMIUtilDebug::WaitForDbgAttachInfinteLoop() {
  MIuint i = 0;
  while (i == 0) {
    const std::chrono::milliseconds time(100);
    std::this_thread::sleep_for(time);
  }
}

//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------

// Instantiations:
CMICmnLog &CMIUtilDebugFnTrace::ms_rLog = CMICmnLog::Instance();
MIuint CMIUtilDebugFnTrace::ms_fnDepthCnt = 0;

//++
//------------------------------------------------------------------------------------
// Details: CMIUtilDebugFnTrace constructor.
// Type:    Method.
// Args:    vFnName - (R) The text to insert into the log.
// Return:  None.
// Throws:  None.
//--
CMIUtilDebugFnTrace::CMIUtilDebugFnTrace(const CMIUtilString &vFnName)
    : m_strFnName(vFnName) {
  const CMIUtilString txt(
      CMIUtilString::Format("%d>%s", ++ms_fnDepthCnt, m_strFnName.c_str()));
  ms_rLog.Write(txt, CMICmnLog::eLogVerbosity_FnTrace);
}

//++
//------------------------------------------------------------------------------------
// Details: CMIUtilDebugFnTrace destructor.
// Type:    Method.
// Args:    None.
// Return:  None.
// Throws:  None.
//--
CMIUtilDebugFnTrace::~CMIUtilDebugFnTrace() {
  const CMIUtilString txt(
      CMIUtilString::Format("%d<%s", ms_fnDepthCnt--, m_strFnName.c_str()));
  ms_rLog.Write(txt, CMICmnLog::eLogVerbosity_FnTrace);
}
