//===-- MIUtilDebug.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

#define MI_USE_DEBUG_TRACE_FN // Undefine to compile out fn trace code

// In-house headers:
#include "MIUtilString.h"

// Declarations:
class CMICmnLog;

//++
//============================================================================
// Details: MI debugging aid utility class.
//--
class CMIUtilDebug {
  // Statics:
public:
  static void WaitForDbgAttachInfinteLoop();

  // Methods:
public:
  /* ctor */ CMIUtilDebug();

  // Overrideable:
public:
  // From CMICmnBase
  /* dtor */ virtual ~CMIUtilDebug();
};

//++
//============================================================================
// Details: MI debug utility class. Used to indicate the current function
//          depth in the call stack. It uses the CMIlCmnLog logger to output
//          the current fn trace information.
//          Use macro MI_TRACEFN( "Some fn name" ) and implement the scope of
//          the functions you wish to build up a trace off.
//          Use preprocessor definition MI_USE_DEBUG_TRACE_FN to turn off or on
//          tracing code.
//--
class CMIUtilDebugFnTrace {
  // Methods:
public:
  /* ctor */ CMIUtilDebugFnTrace(const CMIUtilString &vFnName);

  // Overrideable:
public:
  // From CMICmnBase
  /* dtor */ virtual ~CMIUtilDebugFnTrace();

  // Attributes:
private:
  const CMIUtilString m_strFnName;

  static CMICmnLog &ms_rLog;
  static MIuint ms_fnDepthCnt; // Increment count as fn depth increases,
                               // decrement count as fn stack pops off
};

//++
//============================================================================
// Details: Take the given text and send it to the server's Logger to output to
// the
//          trace file.
// Type:    Compile preprocess.
// Args:    x   - (R) Message (may be seen by user).
//--
#ifdef MI_USE_DEBUG_TRACE_FN
#define MI_TRACEFN(x) CMIUtilDebugFnTrace __MITrace(x)
#else
#define MI_TRACEFN(x)
#endif // MI_USE_DEBUG_TRACE_FN
