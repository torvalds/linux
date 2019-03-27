//===---------------------- ExecuteStage.h ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the execution stage of a default instruction pipeline.
///
/// The ExecuteStage is responsible for managing the hardware scheduler
/// and issuing notifications that an instruction has been executed.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_EXECUTE_STAGE_H
#define LLVM_MCA_EXECUTE_STAGE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/MCA/HardwareUnits/Scheduler.h"
#include "llvm/MCA/Instruction.h"
#include "llvm/MCA/Stages/Stage.h"

namespace llvm {
namespace mca {

class ExecuteStage final : public Stage {
  Scheduler &HWS;

  Error issueInstruction(InstRef &IR);

  // Called at the beginning of each cycle to issue already dispatched
  // instructions to the underlying pipelines.
  Error issueReadyInstructions();

  // Used to notify instructions eliminated at register renaming stage.
  Error handleInstructionEliminated(InstRef &IR);

  ExecuteStage(const ExecuteStage &Other) = delete;
  ExecuteStage &operator=(const ExecuteStage &Other) = delete;

public:
  ExecuteStage(Scheduler &S) : Stage(), HWS(S) {}

  // This stage works under the assumption that the Pipeline will eventually
  // execute a retire stage. We don't need to check if pipelines and/or
  // schedulers have instructions to process, because those instructions are
  // also tracked by the retire control unit. That means,
  // RetireControlUnit::hasWorkToComplete() is responsible for checking if there
  // are still instructions in-flight in the out-of-order backend.
  bool hasWorkToComplete() const override { return false; }
  bool isAvailable(const InstRef &IR) const override;

  // Notifies the scheduler that a new cycle just started.
  //
  // This method notifies the scheduler that a new cycle started.
  // This method is also responsible for notifying listeners about instructions
  // state changes, and processor resources freed by the scheduler.
  // Instructions that transitioned to the 'Executed' state are automatically
  // moved to the next stage (i.e. RetireStage).
  Error cycleStart() override;
  Error execute(InstRef &IR) override;

  void notifyInstructionIssued(
      const InstRef &IR,
      MutableArrayRef<std::pair<ResourceRef, ResourceCycles>> Used) const;
  void notifyInstructionExecuted(const InstRef &IR) const;
  void notifyInstructionReady(const InstRef &IR) const;
  void notifyResourceAvailable(const ResourceRef &RR) const;

  // Notify listeners that buffered resources have been consumed or freed.
  void notifyReservedOrReleasedBuffers(const InstRef &IR, bool Reserved) const;
};

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_EXECUTE_STAGE_H
