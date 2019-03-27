//===--- Windows/Watchdog.inc - Windows Watchdog Implementation -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides the generic Windows implementation of the Watchdog class.
//
//===----------------------------------------------------------------------===//

// TODO: implement.
// Currently this is only used by PrettyStackTrace which is also unimplemented
// on Windows. Roughly, a Windows implementation would use CreateWaitableTimer
// and a second thread to run the TimerAPCProc.

namespace llvm {
  namespace sys {
    Watchdog::Watchdog(unsigned int seconds) {}
    Watchdog::~Watchdog() {}
  }
}
