//===- PassTimingInfo.h - pass execution timing -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This header defines classes/functions to handle pass execution timing
/// information with interfaces for both pass managers.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_PASSTIMINGINFO_H
#define LLVM_IR_PASSTIMINGINFO_H

#include "llvm/ADT/Any.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/TypeName.h"
#include <memory>
namespace llvm {

class Pass;
class PassInstrumentationCallbacks;

/// If -time-passes has been specified, report the timings immediately and then
/// reset the timers to zero.
void reportAndResetTimings();

/// Request the timer for this legacy-pass-manager's pass instance.
Timer *getPassTimer(Pass *);

/// If the user specifies the -time-passes argument on an LLVM tool command line
/// then the value of this boolean will be true, otherwise false.
/// This is the storage for the -time-passes option.
extern bool TimePassesIsEnabled;

/// This class implements -time-passes functionality for new pass manager.
/// It provides the pass-instrumentation callbacks that measure the pass
/// execution time. They collect timing info into individual timers as
/// passes are being run. At the end of its life-time it prints the resulting
/// timing report.
class TimePassesHandler {
  /// Value of this type is capable of uniquely identifying pass invocations.
  /// It is a pair of string Pass-Identifier (which for now is common
  /// to all the instance of a given pass) + sequential invocation counter.
  using PassInvocationID = std::pair<StringRef, unsigned>;

  /// A group of all pass-timing timers.
  TimerGroup TG;

  /// Map of timers for pass invocations
  DenseMap<PassInvocationID, std::unique_ptr<Timer>> TimingData;

  /// Map that counts invocations of passes, for use in UniqPassID construction.
  StringMap<unsigned> PassIDCountMap;

  /// Stack of currently active timers.
  SmallVector<Timer *, 8> TimerStack;

  bool Enabled;

public:
  TimePassesHandler(bool Enabled = TimePassesIsEnabled);

  /// Destructor handles the print action if it has not been handled before.
  ~TimePassesHandler() {
    // First destroying the timers from TimingData, which deploys all their
    // collected data into the TG time group member, which later prints itself
    // when being destroyed.
    TimingData.clear();
  }

  /// Prints out timing information and then resets the timers.
  void print();

  // We intend this to be unique per-compilation, thus no copies.
  TimePassesHandler(const TimePassesHandler &) = delete;
  void operator=(const TimePassesHandler &) = delete;

  void registerCallbacks(PassInstrumentationCallbacks &PIC);

private:
  /// Dumps information for running/triggered timers, useful for debugging
  LLVM_DUMP_METHOD void dump() const;

  /// Returns the new timer for each new run of the pass.
  Timer &getPassTimer(StringRef PassID);

  /// Returns the incremented counter for the next invocation of \p PassID.
  unsigned nextPassID(StringRef PassID) { return ++PassIDCountMap[PassID]; }

  void startTimer(StringRef PassID);
  void stopTimer(StringRef PassID);

  // Implementation of pass instrumentation callbacks.
  bool runBeforePass(StringRef PassID);
  void runAfterPass(StringRef PassID);
};

} // namespace llvm

#endif
