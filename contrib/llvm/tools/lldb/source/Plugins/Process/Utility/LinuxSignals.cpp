//===-- LinuxSignals.cpp ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "LinuxSignals.h"

using namespace lldb_private;

LinuxSignals::LinuxSignals() : UnixSignals() { Reset(); }

void LinuxSignals::Reset() {
  m_signals.clear();
  //        SIGNO  NAME             SUPPRESS STOP   NOTIFY DESCRIPTION ALIAS
  //        =====  ===========      ======== =====  ======
  //        ======================================   ======
  AddSignal(1, "SIGHUP", false, true, true, "hangup");
  AddSignal(2, "SIGINT", true, true, true, "interrupt");
  AddSignal(3, "SIGQUIT", false, true, true, "quit");
  AddSignal(4, "SIGILL", false, true, true, "illegal instruction");
  AddSignal(5, "SIGTRAP", true, true, true,
            "trace trap (not reset when caught)");
  AddSignal(6, "SIGABRT", false, true, true, "abort()/IOT trap", "SIGIOT");
  AddSignal(7, "SIGBUS", false, true, true, "bus error");
  AddSignal(8, "SIGFPE", false, true, true, "floating point exception");
  AddSignal(9, "SIGKILL", false, true, true, "kill");
  AddSignal(10, "SIGUSR1", false, true, true, "user defined signal 1");
  AddSignal(11, "SIGSEGV", false, true, true, "segmentation violation");
  AddSignal(12, "SIGUSR2", false, true, true, "user defined signal 2");
  AddSignal(13, "SIGPIPE", false, true, true,
            "write to pipe with reading end closed");
  AddSignal(14, "SIGALRM", false, false, false, "alarm");
  AddSignal(15, "SIGTERM", false, true, true, "termination requested");
  AddSignal(16, "SIGSTKFLT", false, true, true, "stack fault");
  AddSignal(17, "SIGCHLD", false, false, true, "child status has changed",
            "SIGCLD");
  AddSignal(18, "SIGCONT", false, true, true, "process continue");
  AddSignal(19, "SIGSTOP", true, true, true, "process stop");
  AddSignal(20, "SIGTSTP", false, true, true, "tty stop");
  AddSignal(21, "SIGTTIN", false, true, true, "background tty read");
  AddSignal(22, "SIGTTOU", false, true, true, "background tty write");
  AddSignal(23, "SIGURG", false, true, true, "urgent data on socket");
  AddSignal(24, "SIGXCPU", false, true, true, "CPU resource exceeded");
  AddSignal(25, "SIGXFSZ", false, true, true, "file size limit exceeded");
  AddSignal(26, "SIGVTALRM", false, true, true, "virtual time alarm");
  AddSignal(27, "SIGPROF", false, false, false, "profiling time alarm");
  AddSignal(28, "SIGWINCH", false, true, true, "window size changes");
  AddSignal(29, "SIGIO", false, true, true, "input/output ready/Pollable event",
            "SIGPOLL");
  AddSignal(30, "SIGPWR", false, true, true, "power failure");
  AddSignal(31, "SIGSYS", false, true, true, "invalid system call");
  AddSignal(32, "SIG32", false, false, false,
            "threading library internal signal 1");
  AddSignal(33, "SIG33", false, false, false,
            "threading library internal signal 2");
  AddSignal(34, "SIGRTMIN", false, false, false, "real time signal 0");
  AddSignal(35, "SIGRTMIN+1", false, false, false, "real time signal 1");
  AddSignal(36, "SIGRTMIN+2", false, false, false, "real time signal 2");
  AddSignal(37, "SIGRTMIN+3", false, false, false, "real time signal 3");
  AddSignal(38, "SIGRTMIN+4", false, false, false, "real time signal 4");
  AddSignal(39, "SIGRTMIN+5", false, false, false, "real time signal 5");
  AddSignal(40, "SIGRTMIN+6", false, false, false, "real time signal 6");
  AddSignal(41, "SIGRTMIN+7", false, false, false, "real time signal 7");
  AddSignal(42, "SIGRTMIN+8", false, false, false, "real time signal 8");
  AddSignal(43, "SIGRTMIN+9", false, false, false, "real time signal 9");
  AddSignal(44, "SIGRTMIN+10", false, false, false, "real time signal 10");
  AddSignal(45, "SIGRTMIN+11", false, false, false, "real time signal 11");
  AddSignal(46, "SIGRTMIN+12", false, false, false, "real time signal 12");
  AddSignal(47, "SIGRTMIN+13", false, false, false, "real time signal 13");
  AddSignal(48, "SIGRTMIN+14", false, false, false, "real time signal 14");
  AddSignal(49, "SIGRTMIN+15", false, false, false, "real time signal 15");
  AddSignal(50, "SIGRTMAX-14", false, false, false,
            "real time signal 16"); // switching to SIGRTMAX-xxx to match "kill
                                    // -l" output
  AddSignal(51, "SIGRTMAX-13", false, false, false, "real time signal 17");
  AddSignal(52, "SIGRTMAX-12", false, false, false, "real time signal 18");
  AddSignal(53, "SIGRTMAX-11", false, false, false, "real time signal 19");
  AddSignal(54, "SIGRTMAX-10", false, false, false, "real time signal 20");
  AddSignal(55, "SIGRTMAX-9", false, false, false, "real time signal 21");
  AddSignal(56, "SIGRTMAX-8", false, false, false, "real time signal 22");
  AddSignal(57, "SIGRTMAX-7", false, false, false, "real time signal 23");
  AddSignal(58, "SIGRTMAX-6", false, false, false, "real time signal 24");
  AddSignal(59, "SIGRTMAX-5", false, false, false, "real time signal 25");
  AddSignal(60, "SIGRTMAX-4", false, false, false, "real time signal 26");
  AddSignal(61, "SIGRTMAX-3", false, false, false, "real time signal 27");
  AddSignal(62, "SIGRTMAX-2", false, false, false, "real time signal 28");
  AddSignal(63, "SIGRTMAX-1", false, false, false, "real time signal 29");
  AddSignal(64, "SIGRTMAX", false, false, false, "real time signal 30");
}
