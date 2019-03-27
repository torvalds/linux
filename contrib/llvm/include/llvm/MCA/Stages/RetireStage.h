//===---------------------- RetireStage.h -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the retire stage of a default instruction pipeline.
/// The RetireStage represents the process logic that interacts with the
/// simulated RetireControlUnit hardware.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_RETIRE_STAGE_H
#define LLVM_MCA_RETIRE_STAGE_H

#include "llvm/MCA/HardwareUnits/RegisterFile.h"
#include "llvm/MCA/HardwareUnits/RetireControlUnit.h"
#include "llvm/MCA/Stages/Stage.h"

namespace llvm {
namespace mca {

class RetireStage final : public Stage {
  // Owner will go away when we move listeners/eventing to the stages.
  RetireControlUnit &RCU;
  RegisterFile &PRF;

  RetireStage(const RetireStage &Other) = delete;
  RetireStage &operator=(const RetireStage &Other) = delete;

public:
  RetireStage(RetireControlUnit &R, RegisterFile &F)
      : Stage(), RCU(R), PRF(F) {}

  bool hasWorkToComplete() const override { return !RCU.isEmpty(); }
  Error cycleStart() override;
  Error execute(InstRef &IR) override;
  void notifyInstructionRetired(const InstRef &IR) const;
};

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_RETIRE_STAGE_H
