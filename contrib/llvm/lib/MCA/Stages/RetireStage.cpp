//===---------------------- RetireStage.cpp ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the retire stage of an instruction pipeline.
/// The RetireStage represents the process logic that interacts with the
/// simulated RetireControlUnit hardware.
///
//===----------------------------------------------------------------------===//

#include "llvm/MCA/Stages/RetireStage.h"
#include "llvm/MCA/HWEventListener.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "llvm-mca"

namespace llvm {
namespace mca {

llvm::Error RetireStage::cycleStart() {
  if (RCU.isEmpty())
    return llvm::ErrorSuccess();

  const unsigned MaxRetirePerCycle = RCU.getMaxRetirePerCycle();
  unsigned NumRetired = 0;
  while (!RCU.isEmpty()) {
    if (MaxRetirePerCycle != 0 && NumRetired == MaxRetirePerCycle)
      break;
    const RetireControlUnit::RUToken &Current = RCU.peekCurrentToken();
    if (!Current.Executed)
      break;
    RCU.consumeCurrentToken();
    notifyInstructionRetired(Current.IR);
    NumRetired++;
  }

  return llvm::ErrorSuccess();
}

llvm::Error RetireStage::execute(InstRef &IR) {
  RCU.onInstructionExecuted(IR.getInstruction()->getRCUTokenID());
  return llvm::ErrorSuccess();
}

void RetireStage::notifyInstructionRetired(const InstRef &IR) const {
  LLVM_DEBUG(llvm::dbgs() << "[E] Instruction Retired: #" << IR << '\n');
  llvm::SmallVector<unsigned, 4> FreedRegs(PRF.getNumRegisterFiles());
  const Instruction &Inst = *IR.getInstruction();

  for (const WriteState &WS : Inst.getDefs())
    PRF.removeRegisterWrite(WS, FreedRegs);
  notifyEvent<HWInstructionEvent>(HWInstructionRetiredEvent(IR, FreedRegs));
}

} // namespace mca
} // namespace llvm
