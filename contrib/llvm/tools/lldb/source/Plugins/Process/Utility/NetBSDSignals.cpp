//===-- NetBSDSignals.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "NetBSDSignals.h"

using namespace lldb_private;

NetBSDSignals::NetBSDSignals() : UnixSignals() { Reset(); }

void NetBSDSignals::Reset() {
  UnixSignals::Reset();
  //        SIGNO  NAME          SUPPRESS STOP   NOTIFY DESCRIPTION
  //        ====== ============  ======== ====== ======
  //        ===================================================
  AddSignal(32, "SIGPWR", false, true, true,
            "power fail/restart (not reset when caught)");
  AddSignal(33, "SIGRTMIN", false, false, false, "real time signal 0");
  AddSignal(34, "SIGRTMIN+1", false, false, false, "real time signal 1");
  AddSignal(35, "SIGRTMIN+2", false, false, false, "real time signal 2");
  AddSignal(36, "SIGRTMIN+3", false, false, false, "real time signal 3");
  AddSignal(37, "SIGRTMIN+4", false, false, false, "real time signal 4");
  AddSignal(38, "SIGRTMIN+5", false, false, false, "real time signal 5");
  AddSignal(39, "SIGRTMIN+6", false, false, false, "real time signal 6");
  AddSignal(40, "SIGRTMIN+7", false, false, false, "real time signal 7");
  AddSignal(41, "SIGRTMIN+8", false, false, false, "real time signal 8");
  AddSignal(42, "SIGRTMIN+9", false, false, false, "real time signal 9");
  AddSignal(43, "SIGRTMIN+10", false, false, false, "real time signal 10");
  AddSignal(44, "SIGRTMIN+11", false, false, false, "real time signal 11");
  AddSignal(45, "SIGRTMIN+12", false, false, false, "real time signal 12");
  AddSignal(46, "SIGRTMIN+13", false, false, false, "real time signal 13");
  AddSignal(47, "SIGRTMIN+14", false, false, false, "real time signal 14");
  AddSignal(48, "SIGRTMIN+15", false, false, false, "real time signal 15");
  AddSignal(49, "SIGRTMIN-14", false, false, false, "real time signal 16");
  AddSignal(50, "SIGRTMAX-13", false, false, false, "real time signal 17");
  AddSignal(51, "SIGRTMAX-12", false, false, false, "real time signal 18");
  AddSignal(52, "SIGRTMAX-11", false, false, false, "real time signal 19");
  AddSignal(53, "SIGRTMAX-10", false, false, false, "real time signal 20");
  AddSignal(54, "SIGRTMAX-9", false, false, false, "real time signal 21");
  AddSignal(55, "SIGRTMAX-8", false, false, false, "real time signal 22");
  AddSignal(56, "SIGRTMAX-7", false, false, false, "real time signal 23");
  AddSignal(57, "SIGRTMAX-6", false, false, false, "real time signal 24");
  AddSignal(58, "SIGRTMAX-5", false, false, false, "real time signal 25");
  AddSignal(59, "SIGRTMAX-4", false, false, false, "real time signal 26");
  AddSignal(60, "SIGRTMAX-3", false, false, false, "real time signal 27");
  AddSignal(61, "SIGRTMAX-2", false, false, false, "real time signal 28");
  AddSignal(62, "SIGRTMAX-1", false, false, false, "real time signal 29");
  AddSignal(63, "SIGRTMAX", false, false, false, "real time signal 30");
}
