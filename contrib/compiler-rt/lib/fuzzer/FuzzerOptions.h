//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// fuzzer::FuzzingOptions
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_OPTIONS_H
#define LLVM_FUZZER_OPTIONS_H

#include "FuzzerDefs.h"

namespace fuzzer {

struct FuzzingOptions {
  int Verbosity = 1;
  size_t MaxLen = 0;
  size_t LenControl = 1000;
  int UnitTimeoutSec = 300;
  int TimeoutExitCode = 77;
  int ErrorExitCode = 77;
  int MaxTotalTimeSec = 0;
  int RssLimitMb = 0;
  int MallocLimitMb = 0;
  bool DoCrossOver = true;
  int MutateDepth = 5;
  bool ReduceDepth = false;
  bool UseCounters = false;
  bool UseMemmem = true;
  bool UseCmp = false;
  int UseValueProfile = false;
  bool Shrink = false;
  bool ReduceInputs = false;
  int ReloadIntervalSec = 1;
  bool ShuffleAtStartUp = true;
  bool PreferSmall = true;
  size_t MaxNumberOfRuns = -1L;
  int ReportSlowUnits = 10;
  bool OnlyASCII = false;
  std::string OutputCorpus;
  std::string ArtifactPrefix = "./";
  std::string ExactArtifactPath;
  std::string ExitOnSrcPos;
  std::string ExitOnItem;
  std::string FocusFunction;
  std::string DataFlowTrace;
  bool SaveArtifacts = true;
  bool PrintNEW = true; // Print a status line when new units are found;
  bool PrintNewCovPcs = false;
  int PrintNewCovFuncs = 0;
  bool PrintFinalStats = false;
  bool PrintCorpusStats = false;
  bool PrintCoverage = false;
  bool DumpCoverage = false;
  bool DetectLeaks = true;
  int PurgeAllocatorIntervalSec = 1;
  int  TraceMalloc = 0;
  bool HandleAbrt = false;
  bool HandleBus = false;
  bool HandleFpe = false;
  bool HandleIll = false;
  bool HandleInt = false;
  bool HandleSegv = false;
  bool HandleTerm = false;
  bool HandleXfsz = false;
  bool HandleUsr1 = false;
  bool HandleUsr2 = false;
};

}  // namespace fuzzer

#endif  // LLVM_FUZZER_OPTIONS_H
