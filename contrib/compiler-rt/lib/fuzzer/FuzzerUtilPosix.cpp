//===- FuzzerUtilPosix.cpp - Misc utils for Posix. ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Misc utils implementation using Posix API.
//===----------------------------------------------------------------------===//
#include "FuzzerDefs.h"
#if LIBFUZZER_POSIX
#include "FuzzerIO.h"
#include "FuzzerInternal.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <errno.h>
#include <iomanip>
#include <signal.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

namespace fuzzer {

static void AlarmHandler(int, siginfo_t *, void *) {
  Fuzzer::StaticAlarmCallback();
}

static void CrashHandler(int, siginfo_t *, void *) {
  Fuzzer::StaticCrashSignalCallback();
}

static void InterruptHandler(int, siginfo_t *, void *) {
  Fuzzer::StaticInterruptCallback();
}

static void GracefulExitHandler(int, siginfo_t *, void *) {
  Fuzzer::StaticGracefulExitCallback();
}

static void FileSizeExceedHandler(int, siginfo_t *, void *) {
  Fuzzer::StaticFileSizeExceedCallback();
}

static void SetSigaction(int signum,
                         void (*callback)(int, siginfo_t *, void *)) {
  struct sigaction sigact = {};
  if (sigaction(signum, nullptr, &sigact)) {
    Printf("libFuzzer: sigaction failed with %d\n", errno);
    exit(1);
  }
  if (sigact.sa_flags & SA_SIGINFO) {
    if (sigact.sa_sigaction)
      return;
  } else {
    if (sigact.sa_handler != SIG_DFL && sigact.sa_handler != SIG_IGN &&
        sigact.sa_handler != SIG_ERR)
      return;
  }

  sigact = {};
  sigact.sa_sigaction = callback;
  if (sigaction(signum, &sigact, 0)) {
    Printf("libFuzzer: sigaction failed with %d\n", errno);
    exit(1);
  }
}

void SetTimer(int Seconds) {
  struct itimerval T {
    {Seconds, 0}, { Seconds, 0 }
  };
  if (setitimer(ITIMER_REAL, &T, nullptr)) {
    Printf("libFuzzer: setitimer failed with %d\n", errno);
    exit(1);
  }
  SetSigaction(SIGALRM, AlarmHandler);
}

void SetSignalHandler(const FuzzingOptions& Options) {
  if (Options.UnitTimeoutSec > 0)
    SetTimer(Options.UnitTimeoutSec / 2 + 1);
  if (Options.HandleInt)
    SetSigaction(SIGINT, InterruptHandler);
  if (Options.HandleTerm)
    SetSigaction(SIGTERM, InterruptHandler);
  if (Options.HandleSegv)
    SetSigaction(SIGSEGV, CrashHandler);
  if (Options.HandleBus)
    SetSigaction(SIGBUS, CrashHandler);
  if (Options.HandleAbrt)
    SetSigaction(SIGABRT, CrashHandler);
  if (Options.HandleIll)
    SetSigaction(SIGILL, CrashHandler);
  if (Options.HandleFpe)
    SetSigaction(SIGFPE, CrashHandler);
  if (Options.HandleXfsz)
    SetSigaction(SIGXFSZ, FileSizeExceedHandler);
  if (Options.HandleUsr1)
    SetSigaction(SIGUSR1, GracefulExitHandler);
  if (Options.HandleUsr2)
    SetSigaction(SIGUSR2, GracefulExitHandler);
}

void SleepSeconds(int Seconds) {
  sleep(Seconds); // Use C API to avoid coverage from instrumented libc++.
}

unsigned long GetPid() { return (unsigned long)getpid(); }

size_t GetPeakRSSMb() {
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage))
    return 0;
  if (LIBFUZZER_LINUX || LIBFUZZER_FREEBSD || LIBFUZZER_NETBSD ||
      LIBFUZZER_OPENBSD) {
    // ru_maxrss is in KiB
    return usage.ru_maxrss >> 10;
  } else if (LIBFUZZER_APPLE) {
    // ru_maxrss is in bytes
    return usage.ru_maxrss >> 20;
  }
  assert(0 && "GetPeakRSSMb() is not implemented for your platform");
  return 0;
}

FILE *OpenProcessPipe(const char *Command, const char *Mode) {
  return popen(Command, Mode);
}

const void *SearchMemory(const void *Data, size_t DataLen, const void *Patt,
                         size_t PattLen) {
  return memmem(Data, DataLen, Patt, PattLen);
}

std::string DisassembleCmd(const std::string &FileName) {
  return "objdump -d " + FileName;
}

std::string SearchRegexCmd(const std::string &Regex) {
  return "grep '" + Regex + "'";
}

}  // namespace fuzzer

#endif // LIBFUZZER_POSIX
