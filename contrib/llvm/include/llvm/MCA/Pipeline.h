//===--------------------- Pipeline.h ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements an ordered container of stages that simulate the
/// pipeline of a hardware backend.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_PIPELINE_H
#define LLVM_MCA_PIPELINE_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/MCA/HardwareUnits/Scheduler.h"
#include "llvm/MCA/Stages/Stage.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace mca {

class HWEventListener;

/// A pipeline for a specific subtarget.
///
/// It emulates an out-of-order execution of instructions. Instructions are
/// fetched from a MCInst sequence managed by an initial 'Fetch' stage.
/// Instructions are firstly fetched, then dispatched to the schedulers, and
/// then executed.
///
/// This class tracks the lifetime of an instruction from the moment where
/// it gets dispatched to the schedulers, to the moment where it finishes
/// executing and register writes are architecturally committed.
/// In particular, it monitors changes in the state of every instruction
/// in flight.
///
/// Instructions are executed in a loop of iterations. The number of iterations
/// is defined by the SourceMgr object, which is managed by the initial stage
/// of the instruction pipeline.
///
/// The Pipeline entry point is method 'run()' which executes cycles in a loop
/// until there are new instructions to dispatch, and not every instruction
/// has been retired.
///
/// Internally, the Pipeline collects statistical information in the form of
/// histograms. For example, it tracks how the dispatch group size changes
/// over time.
class Pipeline {
  Pipeline(const Pipeline &P) = delete;
  Pipeline &operator=(const Pipeline &P) = delete;

  /// An ordered list of stages that define this instruction pipeline.
  SmallVector<std::unique_ptr<Stage>, 8> Stages;
  std::set<HWEventListener *> Listeners;
  unsigned Cycles;

  Error runCycle();
  bool hasWorkToProcess();
  void notifyCycleBegin();
  void notifyCycleEnd();

public:
  Pipeline() : Cycles(0) {}
  void appendStage(std::unique_ptr<Stage> S);

  /// Returns the total number of simulated cycles.
  Expected<unsigned> run();

  void addEventListener(HWEventListener *Listener);
};
} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_PIPELINE_H
