//===- FuzzerLoop.cpp - Fuzzer's main loop --------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Fuzzer's main loop.
//===----------------------------------------------------------------------===//

#include "FuzzerCorpus.h"
#include "FuzzerIO.h"
#include "FuzzerInternal.h"
#include "FuzzerMutate.h"
#include "FuzzerRandom.h"
#include "FuzzerShmem.h"
#include "FuzzerTracePC.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>

#if defined(__has_include)
#if __has_include(<sanitizer / lsan_interface.h>)
#include <sanitizer/lsan_interface.h>
#endif
#endif

#define NO_SANITIZE_MEMORY
#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
#undef NO_SANITIZE_MEMORY
#define NO_SANITIZE_MEMORY __attribute__((no_sanitize_memory))
#endif
#endif

namespace fuzzer {
static const size_t kMaxUnitSizeToPrint = 256;

thread_local bool Fuzzer::IsMyThread;

SharedMemoryRegion SMR;

bool RunningUserCallback = false;

// Only one Fuzzer per process.
static Fuzzer *F;

// Leak detection is expensive, so we first check if there were more mallocs
// than frees (using the sanitizer malloc hooks) and only then try to call lsan.
struct MallocFreeTracer {
  void Start(int TraceLevel) {
    this->TraceLevel = TraceLevel;
    if (TraceLevel)
      Printf("MallocFreeTracer: START\n");
    Mallocs = 0;
    Frees = 0;
  }
  // Returns true if there were more mallocs than frees.
  bool Stop() {
    if (TraceLevel)
      Printf("MallocFreeTracer: STOP %zd %zd (%s)\n", Mallocs.load(),
             Frees.load(), Mallocs == Frees ? "same" : "DIFFERENT");
    bool Result = Mallocs > Frees;
    Mallocs = 0;
    Frees = 0;
    TraceLevel = 0;
    return Result;
  }
  std::atomic<size_t> Mallocs;
  std::atomic<size_t> Frees;
  int TraceLevel = 0;

  std::recursive_mutex TraceMutex;
  bool TraceDisabled = false;
};

static MallocFreeTracer AllocTracer;

// Locks printing and avoids nested hooks triggered from mallocs/frees in
// sanitizer.
class TraceLock {
public:
  TraceLock() : Lock(AllocTracer.TraceMutex) {
    AllocTracer.TraceDisabled = !AllocTracer.TraceDisabled;
  }
  ~TraceLock() { AllocTracer.TraceDisabled = !AllocTracer.TraceDisabled; }

  bool IsDisabled() const {
    // This is already inverted value.
    return !AllocTracer.TraceDisabled;
  }

private:
  std::lock_guard<std::recursive_mutex> Lock;
};

ATTRIBUTE_NO_SANITIZE_MEMORY
void MallocHook(const volatile void *ptr, size_t size) {
  size_t N = AllocTracer.Mallocs++;
  F->HandleMalloc(size);
  if (int TraceLevel = AllocTracer.TraceLevel) {
    TraceLock Lock;
    if (Lock.IsDisabled())
      return;
    Printf("MALLOC[%zd] %p %zd\n", N, ptr, size);
    if (TraceLevel >= 2 && EF)
      PrintStackTrace();
  }
}

ATTRIBUTE_NO_SANITIZE_MEMORY
void FreeHook(const volatile void *ptr) {
  size_t N = AllocTracer.Frees++;
  if (int TraceLevel = AllocTracer.TraceLevel) {
    TraceLock Lock;
    if (Lock.IsDisabled())
      return;
    Printf("FREE[%zd]   %p\n", N, ptr);
    if (TraceLevel >= 2 && EF)
      PrintStackTrace();
  }
}

// Crash on a single malloc that exceeds the rss limit.
void Fuzzer::HandleMalloc(size_t Size) {
  if (!Options.MallocLimitMb || (Size >> 20) < (size_t)Options.MallocLimitMb)
    return;
  Printf("==%d== ERROR: libFuzzer: out-of-memory (malloc(%zd))\n", GetPid(),
         Size);
  Printf("   To change the out-of-memory limit use -rss_limit_mb=<N>\n\n");
  PrintStackTrace();
  DumpCurrentUnit("oom-");
  Printf("SUMMARY: libFuzzer: out-of-memory\n");
  PrintFinalStats();
  _Exit(Options.ErrorExitCode); // Stop right now.
}

Fuzzer::Fuzzer(UserCallback CB, InputCorpus &Corpus, MutationDispatcher &MD,
               FuzzingOptions Options)
    : CB(CB), Corpus(Corpus), MD(MD), Options(Options) {
  if (EF->__sanitizer_set_death_callback)
    EF->__sanitizer_set_death_callback(StaticDeathCallback);
  assert(!F);
  F = this;
  TPC.ResetMaps();
  IsMyThread = true;
  if (Options.DetectLeaks && EF->__sanitizer_install_malloc_and_free_hooks)
    EF->__sanitizer_install_malloc_and_free_hooks(MallocHook, FreeHook);
  TPC.SetUseCounters(Options.UseCounters);
  TPC.SetUseValueProfileMask(Options.UseValueProfile);

  if (Options.Verbosity)
    TPC.PrintModuleInfo();
  if (!Options.OutputCorpus.empty() && Options.ReloadIntervalSec)
    EpochOfLastReadOfOutputCorpus = GetEpoch(Options.OutputCorpus);
  MaxInputLen = MaxMutationLen = Options.MaxLen;
  TmpMaxMutationLen = Max(size_t(4), Corpus.MaxInputSize());
  AllocateCurrentUnitData();
  CurrentUnitSize = 0;
  memset(BaseSha1, 0, sizeof(BaseSha1));
  TPC.SetFocusFunction(Options.FocusFunction);
  DFT.Init(Options.DataFlowTrace, Options.FocusFunction);
}

Fuzzer::~Fuzzer() {}

void Fuzzer::AllocateCurrentUnitData() {
  if (CurrentUnitData || MaxInputLen == 0)
    return;
  CurrentUnitData = new uint8_t[MaxInputLen];
}

void Fuzzer::StaticDeathCallback() {
  assert(F);
  F->DeathCallback();
}

void Fuzzer::DumpCurrentUnit(const char *Prefix) {
  if (!CurrentUnitData)
    return; // Happens when running individual inputs.
  ScopedDisableMsanInterceptorChecks S;
  MD.PrintMutationSequence();
  Printf("; base unit: %s\n", Sha1ToString(BaseSha1).c_str());
  size_t UnitSize = CurrentUnitSize;
  if (UnitSize <= kMaxUnitSizeToPrint) {
    PrintHexArray(CurrentUnitData, UnitSize, "\n");
    PrintASCII(CurrentUnitData, UnitSize, "\n");
  }
  WriteUnitToFileWithPrefix({CurrentUnitData, CurrentUnitData + UnitSize},
                            Prefix);
}

NO_SANITIZE_MEMORY
void Fuzzer::DeathCallback() {
  DumpCurrentUnit("crash-");
  PrintFinalStats();
}

void Fuzzer::StaticAlarmCallback() {
  assert(F);
  F->AlarmCallback();
}

void Fuzzer::StaticCrashSignalCallback() {
  assert(F);
  F->CrashCallback();
}

void Fuzzer::StaticExitCallback() {
  assert(F);
  F->ExitCallback();
}

void Fuzzer::StaticInterruptCallback() {
  assert(F);
  F->InterruptCallback();
}

void Fuzzer::StaticGracefulExitCallback() {
  assert(F);
  F->GracefulExitRequested = true;
  Printf("INFO: signal received, trying to exit gracefully\n");
}

void Fuzzer::StaticFileSizeExceedCallback() {
  Printf("==%lu== ERROR: libFuzzer: file size exceeded\n", GetPid());
  exit(1);
}

void Fuzzer::CrashCallback() {
  if (EF->__sanitizer_acquire_crash_state)
    EF->__sanitizer_acquire_crash_state();
  Printf("==%lu== ERROR: libFuzzer: deadly signal\n", GetPid());
  PrintStackTrace();
  Printf("NOTE: libFuzzer has rudimentary signal handlers.\n"
         "      Combine libFuzzer with AddressSanitizer or similar for better "
         "crash reports.\n");
  Printf("SUMMARY: libFuzzer: deadly signal\n");
  DumpCurrentUnit("crash-");
  PrintFinalStats();
  _Exit(Options.ErrorExitCode); // Stop right now.
}

void Fuzzer::ExitCallback() {
  if (!RunningUserCallback)
    return; // This exit did not come from the user callback
  if (EF->__sanitizer_acquire_crash_state &&
      !EF->__sanitizer_acquire_crash_state())
    return;
  Printf("==%lu== ERROR: libFuzzer: fuzz target exited\n", GetPid());
  PrintStackTrace();
  Printf("SUMMARY: libFuzzer: fuzz target exited\n");
  DumpCurrentUnit("crash-");
  PrintFinalStats();
  _Exit(Options.ErrorExitCode);
}

void Fuzzer::MaybeExitGracefully() {
  if (!GracefulExitRequested) return;
  Printf("==%lu== INFO: libFuzzer: exiting as requested\n", GetPid());
  PrintFinalStats();
  _Exit(0);
}

void Fuzzer::InterruptCallback() {
  Printf("==%lu== libFuzzer: run interrupted; exiting\n", GetPid());
  PrintFinalStats();
  _Exit(0); // Stop right now, don't perform any at-exit actions.
}

NO_SANITIZE_MEMORY
void Fuzzer::AlarmCallback() {
  assert(Options.UnitTimeoutSec > 0);
  // In Windows Alarm callback is executed by a different thread.
  // NetBSD's current behavior needs this change too.
#if !LIBFUZZER_WINDOWS && !LIBFUZZER_NETBSD
  if (!InFuzzingThread())
    return;
#endif
  if (!RunningUserCallback)
    return; // We have not started running units yet.
  size_t Seconds =
      duration_cast<seconds>(system_clock::now() - UnitStartTime).count();
  if (Seconds == 0)
    return;
  if (Options.Verbosity >= 2)
    Printf("AlarmCallback %zd\n", Seconds);
  if (Seconds >= (size_t)Options.UnitTimeoutSec) {
    if (EF->__sanitizer_acquire_crash_state &&
        !EF->__sanitizer_acquire_crash_state())
      return;
    Printf("ALARM: working on the last Unit for %zd seconds\n", Seconds);
    Printf("       and the timeout value is %d (use -timeout=N to change)\n",
           Options.UnitTimeoutSec);
    DumpCurrentUnit("timeout-");
    Printf("==%lu== ERROR: libFuzzer: timeout after %d seconds\n", GetPid(),
           Seconds);
    PrintStackTrace();
    Printf("SUMMARY: libFuzzer: timeout\n");
    PrintFinalStats();
    _Exit(Options.TimeoutExitCode); // Stop right now.
  }
}

void Fuzzer::RssLimitCallback() {
  if (EF->__sanitizer_acquire_crash_state &&
      !EF->__sanitizer_acquire_crash_state())
    return;
  Printf(
      "==%lu== ERROR: libFuzzer: out-of-memory (used: %zdMb; limit: %zdMb)\n",
      GetPid(), GetPeakRSSMb(), Options.RssLimitMb);
  Printf("   To change the out-of-memory limit use -rss_limit_mb=<N>\n\n");
  PrintMemoryProfile();
  DumpCurrentUnit("oom-");
  Printf("SUMMARY: libFuzzer: out-of-memory\n");
  PrintFinalStats();
  _Exit(Options.ErrorExitCode); // Stop right now.
}

void Fuzzer::PrintStats(const char *Where, const char *End, size_t Units) {
  size_t ExecPerSec = execPerSec();
  if (!Options.Verbosity)
    return;
  Printf("#%zd\t%s", TotalNumberOfRuns, Where);
  if (size_t N = TPC.GetTotalPCCoverage())
    Printf(" cov: %zd", N);
  if (size_t N = Corpus.NumFeatures())
    Printf(" ft: %zd", N);
  if (!Corpus.empty()) {
    Printf(" corp: %zd", Corpus.NumActiveUnits());
    if (size_t N = Corpus.SizeInBytes()) {
      if (N < (1 << 14))
        Printf("/%zdb", N);
      else if (N < (1 << 24))
        Printf("/%zdKb", N >> 10);
      else
        Printf("/%zdMb", N >> 20);
    }
    if (size_t FF = Corpus.NumInputsThatTouchFocusFunction())
      Printf(" focus: %zd", FF);
  }
  if (TmpMaxMutationLen)
    Printf(" lim: %zd", TmpMaxMutationLen);
  if (Units)
    Printf(" units: %zd", Units);

  Printf(" exec/s: %zd", ExecPerSec);
  Printf(" rss: %zdMb", GetPeakRSSMb());
  Printf("%s", End);
}

void Fuzzer::PrintFinalStats() {
  if (Options.PrintCoverage)
    TPC.PrintCoverage();
  if (Options.DumpCoverage)
    TPC.DumpCoverage();
  if (Options.PrintCorpusStats)
    Corpus.PrintStats();
  if (!Options.PrintFinalStats)
    return;
  size_t ExecPerSec = execPerSec();
  Printf("stat::number_of_executed_units: %zd\n", TotalNumberOfRuns);
  Printf("stat::average_exec_per_sec:     %zd\n", ExecPerSec);
  Printf("stat::new_units_added:          %zd\n", NumberOfNewUnitsAdded);
  Printf("stat::slowest_unit_time_sec:    %zd\n", TimeOfLongestUnitInSeconds);
  Printf("stat::peak_rss_mb:              %zd\n", GetPeakRSSMb());
}

void Fuzzer::SetMaxInputLen(size_t MaxInputLen) {
  assert(this->MaxInputLen == 0); // Can only reset MaxInputLen from 0 to non-0.
  assert(MaxInputLen);
  this->MaxInputLen = MaxInputLen;
  this->MaxMutationLen = MaxInputLen;
  AllocateCurrentUnitData();
  Printf("INFO: -max_len is not provided; "
         "libFuzzer will not generate inputs larger than %zd bytes\n",
         MaxInputLen);
}

void Fuzzer::SetMaxMutationLen(size_t MaxMutationLen) {
  assert(MaxMutationLen && MaxMutationLen <= MaxInputLen);
  this->MaxMutationLen = MaxMutationLen;
}

void Fuzzer::CheckExitOnSrcPosOrItem() {
  if (!Options.ExitOnSrcPos.empty()) {
    static auto *PCsSet = new Set<uintptr_t>;
    auto HandlePC = [&](uintptr_t PC) {
      if (!PCsSet->insert(PC).second)
        return;
      std::string Descr = DescribePC("%F %L", PC + 1);
      if (Descr.find(Options.ExitOnSrcPos) != std::string::npos) {
        Printf("INFO: found line matching '%s', exiting.\n",
               Options.ExitOnSrcPos.c_str());
        _Exit(0);
      }
    };
    TPC.ForEachObservedPC(HandlePC);
  }
  if (!Options.ExitOnItem.empty()) {
    if (Corpus.HasUnit(Options.ExitOnItem)) {
      Printf("INFO: found item with checksum '%s', exiting.\n",
             Options.ExitOnItem.c_str());
      _Exit(0);
    }
  }
}

void Fuzzer::RereadOutputCorpus(size_t MaxSize) {
  if (Options.OutputCorpus.empty() || !Options.ReloadIntervalSec)
    return;
  Vector<Unit> AdditionalCorpus;
  ReadDirToVectorOfUnits(Options.OutputCorpus.c_str(), &AdditionalCorpus,
                         &EpochOfLastReadOfOutputCorpus, MaxSize,
                         /*ExitOnError*/ false);
  if (Options.Verbosity >= 2)
    Printf("Reload: read %zd new units.\n", AdditionalCorpus.size());
  bool Reloaded = false;
  for (auto &U : AdditionalCorpus) {
    if (U.size() > MaxSize)
      U.resize(MaxSize);
    if (!Corpus.HasUnit(U)) {
      if (RunOne(U.data(), U.size())) {
        CheckExitOnSrcPosOrItem();
        Reloaded = true;
      }
    }
  }
  if (Reloaded)
    PrintStats("RELOAD");
}

void Fuzzer::PrintPulseAndReportSlowInput(const uint8_t *Data, size_t Size) {
  auto TimeOfUnit =
      duration_cast<seconds>(UnitStopTime - UnitStartTime).count();
  if (!(TotalNumberOfRuns & (TotalNumberOfRuns - 1)) &&
      secondsSinceProcessStartUp() >= 2)
    PrintStats("pulse ");
  if (TimeOfUnit > TimeOfLongestUnitInSeconds * 1.1 &&
      TimeOfUnit >= Options.ReportSlowUnits) {
    TimeOfLongestUnitInSeconds = TimeOfUnit;
    Printf("Slowest unit: %zd s:\n", TimeOfLongestUnitInSeconds);
    WriteUnitToFileWithPrefix({Data, Data + Size}, "slow-unit-");
  }
}

bool Fuzzer::RunOne(const uint8_t *Data, size_t Size, bool MayDeleteFile,
                    InputInfo *II, bool *FoundUniqFeatures) {
  if (!Size)
    return false;

  ExecuteCallback(Data, Size);

  UniqFeatureSetTmp.clear();
  size_t FoundUniqFeaturesOfII = 0;
  size_t NumUpdatesBefore = Corpus.NumFeatureUpdates();
  TPC.CollectFeatures([&](size_t Feature) {
    if (Corpus.AddFeature(Feature, Size, Options.Shrink))
      UniqFeatureSetTmp.push_back(Feature);
    if (Options.ReduceInputs && II)
      if (std::binary_search(II->UniqFeatureSet.begin(),
                             II->UniqFeatureSet.end(), Feature))
        FoundUniqFeaturesOfII++;
  });
  if (FoundUniqFeatures)
    *FoundUniqFeatures = FoundUniqFeaturesOfII;
  PrintPulseAndReportSlowInput(Data, Size);
  size_t NumNewFeatures = Corpus.NumFeatureUpdates() - NumUpdatesBefore;
  if (NumNewFeatures) {
    TPC.UpdateObservedPCs();
    Corpus.AddToCorpus({Data, Data + Size}, NumNewFeatures, MayDeleteFile,
                       TPC.ObservedFocusFunction(), UniqFeatureSetTmp, DFT, II);
    return true;
  }
  if (II && FoundUniqFeaturesOfII &&
      II->DataFlowTraceForFocusFunction.empty() &&
      FoundUniqFeaturesOfII == II->UniqFeatureSet.size() &&
      II->U.size() > Size) {
    Corpus.Replace(II, {Data, Data + Size});
    return true;
  }
  return false;
}

size_t Fuzzer::GetCurrentUnitInFuzzingThead(const uint8_t **Data) const {
  assert(InFuzzingThread());
  *Data = CurrentUnitData;
  return CurrentUnitSize;
}

void Fuzzer::CrashOnOverwrittenData() {
  Printf("==%d== ERROR: libFuzzer: fuzz target overwrites it's const input\n",
         GetPid());
  DumpCurrentUnit("crash-");
  Printf("SUMMARY: libFuzzer: out-of-memory\n");
  _Exit(Options.ErrorExitCode); // Stop right now.
}

// Compare two arrays, but not all bytes if the arrays are large.
static bool LooseMemeq(const uint8_t *A, const uint8_t *B, size_t Size) {
  const size_t Limit = 64;
  if (Size <= 64)
    return !memcmp(A, B, Size);
  // Compare first and last Limit/2 bytes.
  return !memcmp(A, B, Limit / 2) &&
         !memcmp(A + Size - Limit / 2, B + Size - Limit / 2, Limit / 2);
}

void Fuzzer::ExecuteCallback(const uint8_t *Data, size_t Size) {
  TPC.RecordInitialStack();
  TotalNumberOfRuns++;
  assert(InFuzzingThread());
  if (SMR.IsClient())
    SMR.WriteByteArray(Data, Size);
  // We copy the contents of Unit into a separate heap buffer
  // so that we reliably find buffer overflows in it.
  uint8_t *DataCopy = new uint8_t[Size];
  memcpy(DataCopy, Data, Size);
  if (EF->__msan_unpoison)
    EF->__msan_unpoison(DataCopy, Size);
  if (CurrentUnitData && CurrentUnitData != Data)
    memcpy(CurrentUnitData, Data, Size);
  CurrentUnitSize = Size;
  {
    ScopedEnableMsanInterceptorChecks S;
    AllocTracer.Start(Options.TraceMalloc);
    UnitStartTime = system_clock::now();
    TPC.ResetMaps();
    RunningUserCallback = true;
    int Res = CB(DataCopy, Size);
    RunningUserCallback = false;
    UnitStopTime = system_clock::now();
    (void)Res;
    assert(Res == 0);
    HasMoreMallocsThanFrees = AllocTracer.Stop();
  }
  if (!LooseMemeq(DataCopy, Data, Size))
    CrashOnOverwrittenData();
  CurrentUnitSize = 0;
  delete[] DataCopy;
}

void Fuzzer::WriteToOutputCorpus(const Unit &U) {
  if (Options.OnlyASCII)
    assert(IsASCII(U));
  if (Options.OutputCorpus.empty())
    return;
  std::string Path = DirPlusFile(Options.OutputCorpus, Hash(U));
  WriteToFile(U, Path);
  if (Options.Verbosity >= 2)
    Printf("Written %zd bytes to %s\n", U.size(), Path.c_str());
}

void Fuzzer::WriteUnitToFileWithPrefix(const Unit &U, const char *Prefix) {
  if (!Options.SaveArtifacts)
    return;
  std::string Path = Options.ArtifactPrefix + Prefix + Hash(U);
  if (!Options.ExactArtifactPath.empty())
    Path = Options.ExactArtifactPath; // Overrides ArtifactPrefix.
  WriteToFile(U, Path);
  Printf("artifact_prefix='%s'; Test unit written to %s\n",
         Options.ArtifactPrefix.c_str(), Path.c_str());
  if (U.size() <= kMaxUnitSizeToPrint)
    Printf("Base64: %s\n", Base64(U).c_str());
}

void Fuzzer::PrintStatusForNewUnit(const Unit &U, const char *Text) {
  if (!Options.PrintNEW)
    return;
  PrintStats(Text, "");
  if (Options.Verbosity) {
    Printf(" L: %zd/%zd ", U.size(), Corpus.MaxInputSize());
    MD.PrintMutationSequence();
    Printf("\n");
  }
}

void Fuzzer::ReportNewCoverage(InputInfo *II, const Unit &U) {
  II->NumSuccessfullMutations++;
  MD.RecordSuccessfulMutationSequence();
  PrintStatusForNewUnit(U, II->Reduced ? "REDUCE" : "NEW   ");
  WriteToOutputCorpus(U);
  NumberOfNewUnitsAdded++;
  CheckExitOnSrcPosOrItem(); // Check only after the unit is saved to corpus.
  LastCorpusUpdateRun = TotalNumberOfRuns;
}

// Tries detecting a memory leak on the particular input that we have just
// executed before calling this function.
void Fuzzer::TryDetectingAMemoryLeak(const uint8_t *Data, size_t Size,
                                     bool DuringInitialCorpusExecution) {
  if (!HasMoreMallocsThanFrees)
    return; // mallocs==frees, a leak is unlikely.
  if (!Options.DetectLeaks)
    return;
  if (!DuringInitialCorpusExecution &&
      TotalNumberOfRuns >= Options.MaxNumberOfRuns)
    return;
  if (!&(EF->__lsan_enable) || !&(EF->__lsan_disable) ||
      !(EF->__lsan_do_recoverable_leak_check))
    return; // No lsan.
  // Run the target once again, but with lsan disabled so that if there is
  // a real leak we do not report it twice.
  EF->__lsan_disable();
  ExecuteCallback(Data, Size);
  EF->__lsan_enable();
  if (!HasMoreMallocsThanFrees)
    return; // a leak is unlikely.
  if (NumberOfLeakDetectionAttempts++ > 1000) {
    Options.DetectLeaks = false;
    Printf("INFO: libFuzzer disabled leak detection after every mutation.\n"
           "      Most likely the target function accumulates allocated\n"
           "      memory in a global state w/o actually leaking it.\n"
           "      You may try running this binary with -trace_malloc=[12]"
           "      to get a trace of mallocs and frees.\n"
           "      If LeakSanitizer is enabled in this process it will still\n"
           "      run on the process shutdown.\n");
    return;
  }
  // Now perform the actual lsan pass. This is expensive and we must ensure
  // we don't call it too often.
  if (EF->__lsan_do_recoverable_leak_check()) { // Leak is found, report it.
    if (DuringInitialCorpusExecution)
      Printf("\nINFO: a leak has been found in the initial corpus.\n\n");
    Printf("INFO: to ignore leaks on libFuzzer side use -detect_leaks=0.\n\n");
    CurrentUnitSize = Size;
    DumpCurrentUnit("leak-");
    PrintFinalStats();
    _Exit(Options.ErrorExitCode); // not exit() to disable lsan further on.
  }
}

void Fuzzer::MutateAndTestOne() {
  MD.StartMutationSequence();

  auto &II = Corpus.ChooseUnitToMutate(MD.GetRand());
  const auto &U = II.U;
  memcpy(BaseSha1, II.Sha1, sizeof(BaseSha1));
  assert(CurrentUnitData);
  size_t Size = U.size();
  assert(Size <= MaxInputLen && "Oversized Unit");
  memcpy(CurrentUnitData, U.data(), Size);

  assert(MaxMutationLen > 0);

  size_t CurrentMaxMutationLen =
      Min(MaxMutationLen, Max(U.size(), TmpMaxMutationLen));
  assert(CurrentMaxMutationLen > 0);

  for (int i = 0; i < Options.MutateDepth; i++) {
    if (TotalNumberOfRuns >= Options.MaxNumberOfRuns)
      break;
    MaybeExitGracefully();
    size_t NewSize = 0;
    if (II.HasFocusFunction && !II.DataFlowTraceForFocusFunction.empty() &&
        Size <= CurrentMaxMutationLen)
      NewSize = MD.MutateWithMask(CurrentUnitData, Size, Size,
                                  II.DataFlowTraceForFocusFunction);
    else
      NewSize = MD.Mutate(CurrentUnitData, Size, CurrentMaxMutationLen);
    assert(NewSize > 0 && "Mutator returned empty unit");
    assert(NewSize <= CurrentMaxMutationLen && "Mutator return oversized unit");
    Size = NewSize;
    II.NumExecutedMutations++;

    bool FoundUniqFeatures = false;
    bool NewCov = RunOne(CurrentUnitData, Size, /*MayDeleteFile=*/true, &II,
                         &FoundUniqFeatures);
    TryDetectingAMemoryLeak(CurrentUnitData, Size,
                            /*DuringInitialCorpusExecution*/ false);
    if (NewCov) {
      ReportNewCoverage(&II, {CurrentUnitData, CurrentUnitData + Size});
      break;  // We will mutate this input more in the next rounds.
    }
    if (Options.ReduceDepth && !FoundUniqFeatures)
        break;
  }
}

void Fuzzer::PurgeAllocator() {
  if (Options.PurgeAllocatorIntervalSec < 0 || !EF->__sanitizer_purge_allocator)
    return;
  if (duration_cast<seconds>(system_clock::now() -
                             LastAllocatorPurgeAttemptTime)
          .count() < Options.PurgeAllocatorIntervalSec)
    return;

  if (Options.RssLimitMb <= 0 ||
      GetPeakRSSMb() > static_cast<size_t>(Options.RssLimitMb) / 2)
    EF->__sanitizer_purge_allocator();

  LastAllocatorPurgeAttemptTime = system_clock::now();
}

void Fuzzer::ReadAndExecuteSeedCorpora(const Vector<std::string> &CorpusDirs) {
  const size_t kMaxSaneLen = 1 << 20;
  const size_t kMinDefaultLen = 4096;
  Vector<SizedFile> SizedFiles;
  size_t MaxSize = 0;
  size_t MinSize = -1;
  size_t TotalSize = 0;
  size_t LastNumFiles = 0;
  for (auto &Dir : CorpusDirs) {
    GetSizedFilesFromDir(Dir, &SizedFiles);
    Printf("INFO: % 8zd files found in %s\n", SizedFiles.size() - LastNumFiles,
           Dir.c_str());
    LastNumFiles = SizedFiles.size();
  }
  for (auto &File : SizedFiles) {
    MaxSize = Max(File.Size, MaxSize);
    MinSize = Min(File.Size, MinSize);
    TotalSize += File.Size;
  }
  if (Options.MaxLen == 0)
    SetMaxInputLen(std::min(std::max(kMinDefaultLen, MaxSize), kMaxSaneLen));
  assert(MaxInputLen > 0);

  // Test the callback with empty input and never try it again.
  uint8_t dummy = 0;
  ExecuteCallback(&dummy, 0);

  if (SizedFiles.empty()) {
    Printf("INFO: A corpus is not provided, starting from an empty corpus\n");
    Unit U({'\n'}); // Valid ASCII input.
    RunOne(U.data(), U.size());
  } else {
    Printf("INFO: seed corpus: files: %zd min: %zdb max: %zdb total: %zdb"
           " rss: %zdMb\n",
           SizedFiles.size(), MinSize, MaxSize, TotalSize, GetPeakRSSMb());
    if (Options.ShuffleAtStartUp)
      std::shuffle(SizedFiles.begin(), SizedFiles.end(), MD.GetRand());

    if (Options.PreferSmall) {
      std::stable_sort(SizedFiles.begin(), SizedFiles.end());
      assert(SizedFiles.front().Size <= SizedFiles.back().Size);
    }

    // Load and execute inputs one by one.
    for (auto &SF : SizedFiles) {
      auto U = FileToVector(SF.File, MaxInputLen, /*ExitOnError=*/false);
      assert(U.size() <= MaxInputLen);
      RunOne(U.data(), U.size());
      CheckExitOnSrcPosOrItem();
      TryDetectingAMemoryLeak(U.data(), U.size(),
                              /*DuringInitialCorpusExecution*/ true);
    }
  }

  PrintStats("INITED");
  if (!Options.FocusFunction.empty())
    Printf("INFO: %zd/%zd inputs touch the focus function\n",
           Corpus.NumInputsThatTouchFocusFunction(), Corpus.size());
  if (!Options.DataFlowTrace.empty())
    Printf("INFO: %zd/%zd inputs have the Data Flow Trace\n",
           Corpus.NumInputsWithDataFlowTrace(), Corpus.size());

  if (Corpus.empty() && Options.MaxNumberOfRuns) {
    Printf("ERROR: no interesting inputs were found. "
           "Is the code instrumented for coverage? Exiting.\n");
    exit(1);
  }
}

void Fuzzer::Loop(const Vector<std::string> &CorpusDirs) {
  ReadAndExecuteSeedCorpora(CorpusDirs);
  DFT.Clear();  // No need for DFT any more.
  TPC.SetPrintNewPCs(Options.PrintNewCovPcs);
  TPC.SetPrintNewFuncs(Options.PrintNewCovFuncs);
  system_clock::time_point LastCorpusReload = system_clock::now();
  if (Options.DoCrossOver)
    MD.SetCorpus(&Corpus);
  while (true) {
    auto Now = system_clock::now();
    if (duration_cast<seconds>(Now - LastCorpusReload).count() >=
        Options.ReloadIntervalSec) {
      RereadOutputCorpus(MaxInputLen);
      LastCorpusReload = system_clock::now();
    }
    if (TotalNumberOfRuns >= Options.MaxNumberOfRuns)
      break;
    if (TimedOut())
      break;

    // Update TmpMaxMutationLen
    if (Options.LenControl) {
      if (TmpMaxMutationLen < MaxMutationLen &&
          TotalNumberOfRuns - LastCorpusUpdateRun >
              Options.LenControl * Log(TmpMaxMutationLen)) {
        TmpMaxMutationLen =
            Min(MaxMutationLen, TmpMaxMutationLen + Log(TmpMaxMutationLen));
        LastCorpusUpdateRun = TotalNumberOfRuns;
      }
    } else {
      TmpMaxMutationLen = MaxMutationLen;
    }

    // Perform several mutations and runs.
    MutateAndTestOne();

    PurgeAllocator();
  }

  PrintStats("DONE  ", "\n");
  MD.PrintRecommendedDictionary();
}

void Fuzzer::MinimizeCrashLoop(const Unit &U) {
  if (U.size() <= 1)
    return;
  while (!TimedOut() && TotalNumberOfRuns < Options.MaxNumberOfRuns) {
    MD.StartMutationSequence();
    memcpy(CurrentUnitData, U.data(), U.size());
    for (int i = 0; i < Options.MutateDepth; i++) {
      size_t NewSize = MD.Mutate(CurrentUnitData, U.size(), MaxMutationLen);
      assert(NewSize > 0 && NewSize <= MaxMutationLen);
      ExecuteCallback(CurrentUnitData, NewSize);
      PrintPulseAndReportSlowInput(CurrentUnitData, NewSize);
      TryDetectingAMemoryLeak(CurrentUnitData, NewSize,
                              /*DuringInitialCorpusExecution*/ false);
    }
  }
}

void Fuzzer::AnnounceOutput(const uint8_t *Data, size_t Size) {
  if (SMR.IsServer()) {
    SMR.WriteByteArray(Data, Size);
  } else if (SMR.IsClient()) {
    SMR.PostClient();
    SMR.WaitServer();
    size_t OtherSize = SMR.ReadByteArraySize();
    uint8_t *OtherData = SMR.GetByteArray();
    if (Size != OtherSize || memcmp(Data, OtherData, Size) != 0) {
      size_t i = 0;
      for (i = 0; i < Min(Size, OtherSize); i++)
        if (Data[i] != OtherData[i])
          break;
      Printf("==%lu== ERROR: libFuzzer: equivalence-mismatch. Sizes: %zd %zd; "
             "offset %zd\n",
             GetPid(), Size, OtherSize, i);
      DumpCurrentUnit("mismatch-");
      Printf("SUMMARY: libFuzzer: equivalence-mismatch\n");
      PrintFinalStats();
      _Exit(Options.ErrorExitCode);
    }
  }
}

} // namespace fuzzer

extern "C" {

__attribute__((visibility("default"))) size_t
LLVMFuzzerMutate(uint8_t *Data, size_t Size, size_t MaxSize) {
  assert(fuzzer::F);
  return fuzzer::F->GetMD().DefaultMutate(Data, Size, MaxSize);
}

// Experimental
__attribute__((visibility("default"))) void
LLVMFuzzerAnnounceOutput(const uint8_t *Data, size_t Size) {
  assert(fuzzer::F);
  fuzzer::F->AnnounceOutput(Data, Size);
}
} // extern "C"
