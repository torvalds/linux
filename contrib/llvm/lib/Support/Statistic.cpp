//===-- Statistic.cpp - Easy way to expose stats information --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the 'Statistic' class, which is designed to be an easy
// way to expose various success metrics from passes.  These statistics are
// printed at the end of a run, when the -stats command line option is enabled
// on the command line.
//
// This is useful for reporting information like the number of instructions
// simplified, optimized or removed by various transformations, like this:
//
// static Statistic NumInstEliminated("GCSE", "Number of instructions killed");
//
// Later, in the code: ++NumInstEliminated;
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstring>
using namespace llvm;

/// -stats - Command line option to cause transformations to emit stats about
/// what they did.
///
static cl::opt<bool> Stats(
    "stats",
    cl::desc("Enable statistics output from program (available with Asserts)"),
    cl::Hidden);

static cl::opt<bool> StatsAsJSON("stats-json",
                                 cl::desc("Display statistics as json data"),
                                 cl::Hidden);

static bool Enabled;
static bool PrintOnExit;

namespace {
/// This class is used in a ManagedStatic so that it is created on demand (when
/// the first statistic is bumped) and destroyed only when llvm_shutdown is
/// called. We print statistics from the destructor.
/// This class is also used to look up statistic values from applications that
/// use LLVM.
class StatisticInfo {
  std::vector<Statistic*> Stats;

  friend void llvm::PrintStatistics();
  friend void llvm::PrintStatistics(raw_ostream &OS);
  friend void llvm::PrintStatisticsJSON(raw_ostream &OS);

  /// Sort statistics by debugtype,name,description.
  void sort();
public:
  using const_iterator = std::vector<Statistic *>::const_iterator;

  StatisticInfo();
  ~StatisticInfo();

  void addStatistic(Statistic *S) {
    Stats.push_back(S);
  }

  const_iterator begin() const { return Stats.begin(); }
  const_iterator end() const { return Stats.end(); }
  iterator_range<const_iterator> statistics() const {
    return {begin(), end()};
  }

  void reset();
};
} // end anonymous namespace

static ManagedStatic<StatisticInfo> StatInfo;
static ManagedStatic<sys::SmartMutex<true> > StatLock;

/// RegisterStatistic - The first time a statistic is bumped, this method is
/// called.
void Statistic::RegisterStatistic() {
  // If stats are enabled, inform StatInfo that this statistic should be
  // printed.
  // llvm_shutdown calls destructors while holding the ManagedStatic mutex.
  // These destructors end up calling PrintStatistics, which takes StatLock.
  // Since dereferencing StatInfo and StatLock can require taking the
  // ManagedStatic mutex, doing so with StatLock held would lead to a lock
  // order inversion. To avoid that, we dereference the ManagedStatics first,
  // and only take StatLock afterwards.
  if (!Initialized.load(std::memory_order_relaxed)) {
    sys::SmartMutex<true> &Lock = *StatLock;
    StatisticInfo &SI = *StatInfo;
    sys::SmartScopedLock<true> Writer(Lock);
    // Check Initialized again after acquiring the lock.
    if (Initialized.load(std::memory_order_relaxed))
      return;
    if (Stats || Enabled)
      SI.addStatistic(this);

    // Remember we have been registered.
    Initialized.store(true, std::memory_order_release);
  }
}

StatisticInfo::StatisticInfo() {
  // Ensure timergroup lists are created first so they are destructed after us.
  TimerGroup::ConstructTimerLists();
}

// Print information when destroyed, iff command line option is specified.
StatisticInfo::~StatisticInfo() {
  if (::Stats || PrintOnExit)
    llvm::PrintStatistics();
}

void llvm::EnableStatistics(bool PrintOnExit) {
  Enabled = true;
  ::PrintOnExit = PrintOnExit;
}

bool llvm::AreStatisticsEnabled() {
  return Enabled || Stats;
}

void StatisticInfo::sort() {
  std::stable_sort(Stats.begin(), Stats.end(),
                   [](const Statistic *LHS, const Statistic *RHS) {
    if (int Cmp = std::strcmp(LHS->getDebugType(), RHS->getDebugType()))
      return Cmp < 0;

    if (int Cmp = std::strcmp(LHS->getName(), RHS->getName()))
      return Cmp < 0;

    return std::strcmp(LHS->getDesc(), RHS->getDesc()) < 0;
  });
}

void StatisticInfo::reset() {
  sys::SmartScopedLock<true> Writer(*StatLock);

  // Tell each statistic that it isn't registered so it has to register
  // again. We're holding the lock so it won't be able to do so until we're
  // finished. Once we've forced it to re-register (after we return), then zero
  // the value.
  for (auto *Stat : Stats) {
    // Value updates to a statistic that complete before this statement in the
    // iteration for that statistic will be lost as intended.
    Stat->Initialized = false;
    Stat->Value = 0;
  }

  // Clear the registration list and release the lock once we're done. Any
  // pending updates from other threads will safely take effect after we return.
  // That might not be what the user wants if they're measuring a compilation
  // but it's their responsibility to prevent concurrent compilations to make
  // a single compilation measurable.
  Stats.clear();
}

void llvm::PrintStatistics(raw_ostream &OS) {
  StatisticInfo &Stats = *StatInfo;

  // Figure out how long the biggest Value and Name fields are.
  unsigned MaxDebugTypeLen = 0, MaxValLen = 0;
  for (size_t i = 0, e = Stats.Stats.size(); i != e; ++i) {
    MaxValLen = std::max(MaxValLen,
                         (unsigned)utostr(Stats.Stats[i]->getValue()).size());
    MaxDebugTypeLen = std::max(MaxDebugTypeLen,
                         (unsigned)std::strlen(Stats.Stats[i]->getDebugType()));
  }

  Stats.sort();

  // Print out the statistics header...
  OS << "===" << std::string(73, '-') << "===\n"
     << "                          ... Statistics Collected ...\n"
     << "===" << std::string(73, '-') << "===\n\n";

  // Print all of the statistics.
  for (size_t i = 0, e = Stats.Stats.size(); i != e; ++i)
    OS << format("%*u %-*s - %s\n",
                 MaxValLen, Stats.Stats[i]->getValue(),
                 MaxDebugTypeLen, Stats.Stats[i]->getDebugType(),
                 Stats.Stats[i]->getDesc());

  OS << '\n';  // Flush the output stream.
  OS.flush();
}

void llvm::PrintStatisticsJSON(raw_ostream &OS) {
  sys::SmartScopedLock<true> Reader(*StatLock);
  StatisticInfo &Stats = *StatInfo;

  Stats.sort();

  // Print all of the statistics.
  OS << "{\n";
  const char *delim = "";
  for (const Statistic *Stat : Stats.Stats) {
    OS << delim;
    assert(yaml::needsQuotes(Stat->getDebugType()) == yaml::QuotingType::None &&
           "Statistic group/type name is simple.");
    assert(yaml::needsQuotes(Stat->getName()) == yaml::QuotingType::None &&
           "Statistic name is simple");
    OS << "\t\"" << Stat->getDebugType() << '.' << Stat->getName() << "\": "
       << Stat->getValue();
    delim = ",\n";
  }
  // Print timers.
  TimerGroup::printAllJSONValues(OS, delim);

  OS << "\n}\n";
  OS.flush();
}

void llvm::PrintStatistics() {
#if LLVM_ENABLE_STATS
  sys::SmartScopedLock<true> Reader(*StatLock);
  StatisticInfo &Stats = *StatInfo;

  // Statistics not enabled?
  if (Stats.Stats.empty()) return;

  // Get the stream to write to.
  std::unique_ptr<raw_ostream> OutStream = CreateInfoOutputFile();
  if (StatsAsJSON)
    PrintStatisticsJSON(*OutStream);
  else
    PrintStatistics(*OutStream);

#else
  // Check if the -stats option is set instead of checking
  // !Stats.Stats.empty().  In release builds, Statistics operators
  // do nothing, so stats are never Registered.
  if (Stats) {
    // Get the stream to write to.
    std::unique_ptr<raw_ostream> OutStream = CreateInfoOutputFile();
    (*OutStream) << "Statistics are disabled.  "
                 << "Build with asserts or with -DLLVM_ENABLE_STATS\n";
  }
#endif
}

const std::vector<std::pair<StringRef, unsigned>> llvm::GetStatistics() {
  sys::SmartScopedLock<true> Reader(*StatLock);
  std::vector<std::pair<StringRef, unsigned>> ReturnStats;

  for (const auto &Stat : StatInfo->statistics())
    ReturnStats.emplace_back(Stat->getName(), Stat->getValue());
  return ReturnStats;
}

void llvm::ResetStatistics() {
  StatInfo->reset();
}
