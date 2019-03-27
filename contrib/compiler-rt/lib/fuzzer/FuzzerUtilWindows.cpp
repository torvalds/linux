//===- FuzzerUtilWindows.cpp - Misc utils for Windows. --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Misc utils implementation for Windows.
//===----------------------------------------------------------------------===//
#include "FuzzerDefs.h"
#if LIBFUZZER_WINDOWS
#include "FuzzerCommand.h"
#include "FuzzerIO.h"
#include "FuzzerInternal.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <errno.h>
#include <iomanip>
#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <windows.h>

// This must be included after windows.h.
#include <psapi.h>

namespace fuzzer {

static const FuzzingOptions* HandlerOpt = nullptr;

static LONG CALLBACK ExceptionHandler(PEXCEPTION_POINTERS ExceptionInfo) {
  switch (ExceptionInfo->ExceptionRecord->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_STACK_OVERFLOW:
      if (HandlerOpt->HandleSegv)
        Fuzzer::StaticCrashSignalCallback();
      break;
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_IN_PAGE_ERROR:
      if (HandlerOpt->HandleBus)
        Fuzzer::StaticCrashSignalCallback();
      break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
      if (HandlerOpt->HandleIll)
        Fuzzer::StaticCrashSignalCallback();
      break;
    case EXCEPTION_FLT_DENORMAL_OPERAND:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INEXACT_RESULT:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_STACK_CHECK:
    case EXCEPTION_FLT_UNDERFLOW:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
      if (HandlerOpt->HandleFpe)
        Fuzzer::StaticCrashSignalCallback();
      break;
    // TODO: handle (Options.HandleXfsz)
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

BOOL WINAPI CtrlHandler(DWORD dwCtrlType) {
  switch (dwCtrlType) {
    case CTRL_C_EVENT:
      if (HandlerOpt->HandleInt)
        Fuzzer::StaticInterruptCallback();
      return TRUE;
    case CTRL_BREAK_EVENT:
      if (HandlerOpt->HandleTerm)
        Fuzzer::StaticInterruptCallback();
      return TRUE;
  }
  return FALSE;
}

void CALLBACK AlarmHandler(PVOID, BOOLEAN) {
  Fuzzer::StaticAlarmCallback();
}

class TimerQ {
  HANDLE TimerQueue;
 public:
  TimerQ() : TimerQueue(NULL) {};
  ~TimerQ() {
    if (TimerQueue)
      DeleteTimerQueueEx(TimerQueue, NULL);
  };
  void SetTimer(int Seconds) {
    if (!TimerQueue) {
      TimerQueue = CreateTimerQueue();
      if (!TimerQueue) {
        Printf("libFuzzer: CreateTimerQueue failed.\n");
        exit(1);
      }
    }
    HANDLE Timer;
    if (!CreateTimerQueueTimer(&Timer, TimerQueue, AlarmHandler, NULL,
        Seconds*1000, Seconds*1000, 0)) {
      Printf("libFuzzer: CreateTimerQueueTimer failed.\n");
      exit(1);
    }
  };
};

static TimerQ Timer;

static void CrashHandler(int) { Fuzzer::StaticCrashSignalCallback(); }

void SetSignalHandler(const FuzzingOptions& Options) {
  HandlerOpt = &Options;

  if (Options.UnitTimeoutSec > 0)
    Timer.SetTimer(Options.UnitTimeoutSec / 2 + 1);

  if (Options.HandleInt || Options.HandleTerm)
    if (!SetConsoleCtrlHandler(CtrlHandler, TRUE)) {
      DWORD LastError = GetLastError();
      Printf("libFuzzer: SetConsoleCtrlHandler failed (Error code: %lu).\n",
        LastError);
      exit(1);
    }

  if (Options.HandleSegv || Options.HandleBus || Options.HandleIll ||
      Options.HandleFpe)
    SetUnhandledExceptionFilter(ExceptionHandler);

  if (Options.HandleAbrt)
    if (SIG_ERR == signal(SIGABRT, CrashHandler)) {
      Printf("libFuzzer: signal failed with %d\n", errno);
      exit(1);
    }
}

void SleepSeconds(int Seconds) { Sleep(Seconds * 1000); }

unsigned long GetPid() { return GetCurrentProcessId(); }

size_t GetPeakRSSMb() {
  PROCESS_MEMORY_COUNTERS info;
  if (!GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info)))
    return 0;
  return info.PeakWorkingSetSize >> 20;
}

FILE *OpenProcessPipe(const char *Command, const char *Mode) {
  return _popen(Command, Mode);
}

int ExecuteCommand(const Command &Cmd) {
  std::string CmdLine = Cmd.toString();
  return system(CmdLine.c_str());
}

const void *SearchMemory(const void *Data, size_t DataLen, const void *Patt,
                         size_t PattLen) {
  // TODO: make this implementation more efficient.
  const char *Cdata = (const char *)Data;
  const char *Cpatt = (const char *)Patt;

  if (!Data || !Patt || DataLen == 0 || PattLen == 0 || DataLen < PattLen)
    return NULL;

  if (PattLen == 1)
    return memchr(Data, *Cpatt, DataLen);

  const char *End = Cdata + DataLen - PattLen + 1;

  for (const char *It = Cdata; It < End; ++It)
    if (It[0] == Cpatt[0] && memcmp(It, Cpatt, PattLen) == 0)
      return It;

  return NULL;
}

std::string DisassembleCmd(const std::string &FileName) {
  Vector<std::string> command_vector;
  command_vector.push_back("dumpbin /summary > nul");
  if (ExecuteCommand(Command(command_vector)) == 0)
    return "dumpbin /disasm " + FileName;
  Printf("libFuzzer: couldn't find tool to disassemble (dumpbin)\n");
  exit(1);
}

std::string SearchRegexCmd(const std::string &Regex) {
  return "findstr /r \"" + Regex + "\"";
}

} // namespace fuzzer

#endif // LIBFUZZER_WINDOWS
