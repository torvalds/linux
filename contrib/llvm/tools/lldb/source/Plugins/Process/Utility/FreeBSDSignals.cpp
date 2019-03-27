//===-- FreeBSDSignals.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "FreeBSDSignals.h"

using namespace lldb_private;

FreeBSDSignals::FreeBSDSignals() : UnixSignals() { Reset(); }

void FreeBSDSignals::Reset() {
  UnixSignals::Reset();

  //        SIGNO   NAME            SUPPRESS STOP   NOTIFY DESCRIPTION
  //        ======  ============    ======== ====== ======
  //        ===================================================
  AddSignal(32, "SIGTHR", false, false, false, "thread interrupt");
  AddSignal(33, "SIGLIBRT", false, false, false,
            "reserved by real-time library");
  AddSignal(65, "SIGRTMIN", false, false, false, "real time signal 0");
  AddSignal(66, "SIGRTMIN+1", false, false, false, "real time signal 1");
  AddSignal(67, "SIGRTMIN+2", false, false, false, "real time signal 2");
  AddSignal(68, "SIGRTMIN+3", false, false, false, "real time signal 3");
  AddSignal(69, "SIGRTMIN+4", false, false, false, "real time signal 4");
  AddSignal(70, "SIGRTMIN+5", false, false, false, "real time signal 5");
  AddSignal(71, "SIGRTMIN+6", false, false, false, "real time signal 6");
  AddSignal(72, "SIGRTMIN+7", false, false, false, "real time signal 7");
  AddSignal(73, "SIGRTMIN+8", false, false, false, "real time signal 8");
  AddSignal(74, "SIGRTMIN+9", false, false, false, "real time signal 9");
  AddSignal(75, "SIGRTMIN+10", false, false, false, "real time signal 10");
  AddSignal(76, "SIGRTMIN+11", false, false, false, "real time signal 11");
  AddSignal(77, "SIGRTMIN+12", false, false, false, "real time signal 12");
  AddSignal(78, "SIGRTMIN+13", false, false, false, "real time signal 13");
  AddSignal(79, "SIGRTMIN+14", false, false, false, "real time signal 14");
  AddSignal(80, "SIGRTMIN+15", false, false, false, "real time signal 15");
  AddSignal(81, "SIGRTMIN+16", false, false, false, "real time signal 16");
  AddSignal(82, "SIGRTMIN+17", false, false, false, "real time signal 17");
  AddSignal(83, "SIGRTMIN+18", false, false, false, "real time signal 18");
  AddSignal(84, "SIGRTMIN+19", false, false, false, "real time signal 19");
  AddSignal(85, "SIGRTMIN+20", false, false, false, "real time signal 20");
  AddSignal(86, "SIGRTMIN+21", false, false, false, "real time signal 21");
  AddSignal(87, "SIGRTMIN+22", false, false, false, "real time signal 22");
  AddSignal(88, "SIGRTMIN+23", false, false, false, "real time signal 23");
  AddSignal(89, "SIGRTMIN+24", false, false, false, "real time signal 24");
  AddSignal(90, "SIGRTMIN+25", false, false, false, "real time signal 25");
  AddSignal(91, "SIGRTMIN+26", false, false, false, "real time signal 26");
  AddSignal(92, "SIGRTMIN+27", false, false, false, "real time signal 27");
  AddSignal(93, "SIGRTMIN+28", false, false, false, "real time signal 28");
  AddSignal(94, "SIGRTMIN+29", false, false, false, "real time signal 29");
  AddSignal(95, "SIGRTMIN+30", false, false, false, "real time signal 30");
  AddSignal(96, "SIGRTMAX-30", false, false, false, "real time signal 31");
  AddSignal(97, "SIGRTMAX-29", false, false, false, "real time signal 32");
  AddSignal(98, "SIGRTMAX-28", false, false, false, "real time signal 33");
  AddSignal(99, "SIGRTMAX-27", false, false, false, "real time signal 34");
  AddSignal(100, "SIGRTMAX-26", false, false, false, "real time signal 35");
  AddSignal(101, "SIGRTMAX-25", false, false, false, "real time signal 36");
  AddSignal(102, "SIGRTMAX-24", false, false, false, "real time signal 37");
  AddSignal(103, "SIGRTMAX-23", false, false, false, "real time signal 38");
  AddSignal(104, "SIGRTMAX-22", false, false, false, "real time signal 39");
  AddSignal(105, "SIGRTMAX-21", false, false, false, "real time signal 40");
  AddSignal(106, "SIGRTMAX-20", false, false, false, "real time signal 41");
  AddSignal(107, "SIGRTMAX-19", false, false, false, "real time signal 42");
  AddSignal(108, "SIGRTMAX-18", false, false, false, "real time signal 43");
  AddSignal(109, "SIGRTMAX-17", false, false, false, "real time signal 44");
  AddSignal(110, "SIGRTMAX-16", false, false, false, "real time signal 45");
  AddSignal(111, "SIGRTMAX-15", false, false, false, "real time signal 46");
  AddSignal(112, "SIGRTMAX-14", false, false, false, "real time signal 47");
  AddSignal(113, "SIGRTMAX-13", false, false, false, "real time signal 48");
  AddSignal(114, "SIGRTMAX-12", false, false, false, "real time signal 49");
  AddSignal(115, "SIGRTMAX-11", false, false, false, "real time signal 50");
  AddSignal(116, "SIGRTMAX-10", false, false, false, "real time signal 51");
  AddSignal(117, "SIGRTMAX-9", false, false, false, "real time signal 52");
  AddSignal(118, "SIGRTMAX-8", false, false, false, "real time signal 53");
  AddSignal(119, "SIGRTMAX-7", false, false, false, "real time signal 54");
  AddSignal(120, "SIGRTMAX-6", false, false, false, "real time signal 55");
  AddSignal(121, "SIGRTMAX-5", false, false, false, "real time signal 56");
  AddSignal(122, "SIGRTMAX-4", false, false, false, "real time signal 57");
  AddSignal(123, "SIGRTMAX-3", false, false, false, "real time signal 58");
  AddSignal(124, "SIGRTMAX-2", false, false, false, "real time signal 59");
  AddSignal(125, "SIGRTMAX-1", false, false, false, "real time signal 60");
  AddSignal(126, "SIGRTMAX", false, false, false, "real time signal 61");
}
