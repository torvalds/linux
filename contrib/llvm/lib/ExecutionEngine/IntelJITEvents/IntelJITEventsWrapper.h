//===-- IntelJITEventsWrapper.h - Intel JIT Events API Wrapper --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a wrapper for the Intel JIT Events API. It allows for the
// implementation of the jitprofiling library to be swapped with an alternative
// implementation (for testing). To include this file, you must have the
// jitprofiling.h header available; it is available in Intel(R) VTune(TM)
// Amplifier XE 2011.
//
//===----------------------------------------------------------------------===//

#ifndef INTEL_JIT_EVENTS_WRAPPER_H
#define INTEL_JIT_EVENTS_WRAPPER_H

#include "jitprofiling.h"

namespace llvm {

class IntelJITEventsWrapper {
  // Function pointer types for testing implementation of Intel jitprofiling
  // library
  typedef int (*NotifyEventPtr)(iJIT_JVM_EVENT, void*);
  typedef void (*RegisterCallbackExPtr)(void *, iJIT_ModeChangedEx );
  typedef iJIT_IsProfilingActiveFlags (*IsProfilingActivePtr)(void);
  typedef void (*FinalizeThreadPtr)(void);
  typedef void (*FinalizeProcessPtr)(void);
  typedef unsigned int (*GetNewMethodIDPtr)(void);

  NotifyEventPtr NotifyEventFunc;
  RegisterCallbackExPtr RegisterCallbackExFunc;
  IsProfilingActivePtr IsProfilingActiveFunc;
  GetNewMethodIDPtr GetNewMethodIDFunc;

public:
  bool isAmplifierRunning() {
    return iJIT_IsProfilingActive() == iJIT_SAMPLING_ON;
  }

  IntelJITEventsWrapper()
  : NotifyEventFunc(::iJIT_NotifyEvent),
    RegisterCallbackExFunc(::iJIT_RegisterCallbackEx),
    IsProfilingActiveFunc(::iJIT_IsProfilingActive),
    GetNewMethodIDFunc(::iJIT_GetNewMethodID) {
  }

  IntelJITEventsWrapper(NotifyEventPtr NotifyEventImpl,
                   RegisterCallbackExPtr RegisterCallbackExImpl,
                   IsProfilingActivePtr IsProfilingActiveImpl,
                   FinalizeThreadPtr FinalizeThreadImpl,
                   FinalizeProcessPtr FinalizeProcessImpl,
                   GetNewMethodIDPtr GetNewMethodIDImpl)
  : NotifyEventFunc(NotifyEventImpl),
    RegisterCallbackExFunc(RegisterCallbackExImpl),
    IsProfilingActiveFunc(IsProfilingActiveImpl),
    GetNewMethodIDFunc(GetNewMethodIDImpl) {
  }

  // Sends an event announcing that a function has been emitted
  //   return values are event-specific.  See Intel documentation for details.
  int  iJIT_NotifyEvent(iJIT_JVM_EVENT EventType, void *EventSpecificData) {
    if (!NotifyEventFunc)
      return -1;
    return NotifyEventFunc(EventType, EventSpecificData);
  }

  // Registers a callback function to receive notice of profiling state changes
  void iJIT_RegisterCallbackEx(void *UserData,
                               iJIT_ModeChangedEx NewModeCallBackFuncEx) {
    if (RegisterCallbackExFunc)
      RegisterCallbackExFunc(UserData, NewModeCallBackFuncEx);
  }

  // Returns the current profiler mode
  iJIT_IsProfilingActiveFlags iJIT_IsProfilingActive(void) {
    if (!IsProfilingActiveFunc)
      return iJIT_NOTHING_RUNNING;
    return IsProfilingActiveFunc();
  }

  // Generates a locally unique method ID for use in code registration
  unsigned int iJIT_GetNewMethodID(void) {
    if (!GetNewMethodIDFunc)
      return -1;
    return GetNewMethodIDFunc();
  }
};

} //namespace llvm

#endif //INTEL_JIT_EVENTS_WRAPPER_H
