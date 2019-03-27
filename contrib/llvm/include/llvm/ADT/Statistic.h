//===-- llvm/ADT/Statistic.h - Easy way to expose stats ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the 'Statistic' class, which is designed to be an easy way
// to expose various metrics from passes.  These statistics are printed at the
// end of a run (from llvm_shutdown), when the -stats command line option is
// passed on the command line.
//
// This is useful for reporting information like the number of instructions
// simplified, optimized or removed by various transformations, like this:
//
// static Statistic NumInstsKilled("gcse", "Number of instructions killed");
//
// Later, in the code: ++NumInstsKilled;
//
// NOTE: Statistics *must* be declared as global variables.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_STATISTIC_H
#define LLVM_ADT_STATISTIC_H

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Compiler.h"
#include <atomic>
#include <memory>
#include <vector>

// Determine whether statistics should be enabled. We must do it here rather
// than in CMake because multi-config generators cannot determine this at
// configure time.
#if !defined(NDEBUG) || LLVM_FORCE_ENABLE_STATS
#define LLVM_ENABLE_STATS 1
#endif

namespace llvm {

class raw_ostream;
class raw_fd_ostream;
class StringRef;

class Statistic {
public:
  const char *DebugType;
  const char *Name;
  const char *Desc;
  std::atomic<unsigned> Value;
  std::atomic<bool> Initialized;

  unsigned getValue() const { return Value.load(std::memory_order_relaxed); }
  const char *getDebugType() const { return DebugType; }
  const char *getName() const { return Name; }
  const char *getDesc() const { return Desc; }

  /// construct - This should only be called for non-global statistics.
  void construct(const char *debugtype, const char *name, const char *desc) {
    DebugType = debugtype;
    Name = name;
    Desc = desc;
    Value = 0;
    Initialized = false;
  }

  // Allow use of this class as the value itself.
  operator unsigned() const { return getValue(); }

#if LLVM_ENABLE_STATS
   const Statistic &operator=(unsigned Val) {
    Value.store(Val, std::memory_order_relaxed);
    return init();
  }

  const Statistic &operator++() {
    Value.fetch_add(1, std::memory_order_relaxed);
    return init();
  }

  unsigned operator++(int) {
    init();
    return Value.fetch_add(1, std::memory_order_relaxed);
  }

  const Statistic &operator--() {
    Value.fetch_sub(1, std::memory_order_relaxed);
    return init();
  }

  unsigned operator--(int) {
    init();
    return Value.fetch_sub(1, std::memory_order_relaxed);
  }

  const Statistic &operator+=(unsigned V) {
    if (V == 0)
      return *this;
    Value.fetch_add(V, std::memory_order_relaxed);
    return init();
  }

  const Statistic &operator-=(unsigned V) {
    if (V == 0)
      return *this;
    Value.fetch_sub(V, std::memory_order_relaxed);
    return init();
  }

  void updateMax(unsigned V) {
    unsigned PrevMax = Value.load(std::memory_order_relaxed);
    // Keep trying to update max until we succeed or another thread produces
    // a bigger max than us.
    while (V > PrevMax && !Value.compare_exchange_weak(
                              PrevMax, V, std::memory_order_relaxed)) {
    }
    init();
  }

#else  // Statistics are disabled in release builds.

  const Statistic &operator=(unsigned Val) {
    return *this;
  }

  const Statistic &operator++() {
    return *this;
  }

  unsigned operator++(int) {
    return 0;
  }

  const Statistic &operator--() {
    return *this;
  }

  unsigned operator--(int) {
    return 0;
  }

  const Statistic &operator+=(const unsigned &V) {
    return *this;
  }

  const Statistic &operator-=(const unsigned &V) {
    return *this;
  }

  void updateMax(unsigned V) {}

#endif  // LLVM_ENABLE_STATS

protected:
  Statistic &init() {
    if (!Initialized.load(std::memory_order_acquire))
      RegisterStatistic();
    return *this;
  }

  void RegisterStatistic();
};

// STATISTIC - A macro to make definition of statistics really simple.  This
// automatically passes the DEBUG_TYPE of the file into the statistic.
#define STATISTIC(VARNAME, DESC)                                               \
  static llvm::Statistic VARNAME = {DEBUG_TYPE, #VARNAME, DESC, {0}, {false}}

/// Enable the collection and printing of statistics.
void EnableStatistics(bool PrintOnExit = true);

/// Check if statistics are enabled.
bool AreStatisticsEnabled();

/// Return a file stream to print our output on.
std::unique_ptr<raw_fd_ostream> CreateInfoOutputFile();

/// Print statistics to the file returned by CreateInfoOutputFile().
void PrintStatistics();

/// Print statistics to the given output stream.
void PrintStatistics(raw_ostream &OS);

/// Print statistics in JSON format. This does include all global timers (\see
/// Timer, TimerGroup). Note that the timers are cleared after printing and will
/// not be printed in human readable form or in a second call of
/// PrintStatisticsJSON().
void PrintStatisticsJSON(raw_ostream &OS);

/// Get the statistics. This can be used to look up the value of
/// statistics without needing to parse JSON.
///
/// This function does not prevent statistics being updated by other threads
/// during it's execution. It will return the value at the point that it is
/// read. However, it will prevent new statistics from registering until it
/// completes.
const std::vector<std::pair<StringRef, unsigned>> GetStatistics();

/// Reset the statistics. This can be used to zero and de-register the
/// statistics in order to measure a compilation.
///
/// When this function begins to call destructors prior to returning, all
/// statistics will be zero and unregistered. However, that might not remain the
/// case by the time this function finishes returning. Whether update from other
/// threads are lost or merely deferred until during the function return is
/// timing sensitive.
///
/// Callers who intend to use this to measure statistics for a single
/// compilation should ensure that no compilations are in progress at the point
/// this function is called and that only one compilation executes until calling
/// GetStatistics().
void ResetStatistics();

} // end namespace llvm

#endif // LLVM_ADT_STATISTIC_H
