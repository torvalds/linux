//===---------------------- EntryStage.cpp ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the Fetch stage of an instruction pipeline.  Its sole
/// purpose in life is to produce instructions for the rest of the pipeline.
///
//===----------------------------------------------------------------------===//

#include "llvm/MCA/Stages/EntryStage.h"
#include "llvm/MCA/Instruction.h"

namespace llvm {
namespace mca {

bool EntryStage::hasWorkToComplete() const { return CurrentInstruction; }

bool EntryStage::isAvailable(const InstRef & /* unused */) const {
  if (CurrentInstruction)
    return checkNextStage(CurrentInstruction);
  return false;
}

void EntryStage::getNextInstruction() {
  assert(!CurrentInstruction && "There is already an instruction to process!");
  if (!SM.hasNext())
    return;
  SourceRef SR = SM.peekNext();
  std::unique_ptr<Instruction> Inst = llvm::make_unique<Instruction>(SR.second);
  CurrentInstruction = InstRef(SR.first, Inst.get());
  Instructions.emplace_back(std::move(Inst));
  SM.updateNext();
}

llvm::Error EntryStage::execute(InstRef & /*unused */) {
  assert(CurrentInstruction && "There is no instruction to process!");
  if (llvm::Error Val = moveToTheNextStage(CurrentInstruction))
    return Val;

  // Move the program counter.
  CurrentInstruction.invalidate();
  getNextInstruction();
  return llvm::ErrorSuccess();
}

llvm::Error EntryStage::cycleStart() {
  if (!CurrentInstruction)
    getNextInstruction();
  return llvm::ErrorSuccess();
}

llvm::Error EntryStage::cycleEnd() {
  // Find the first instruction which hasn't been retired.
  auto Range = make_range(&Instructions[NumRetired], Instructions.end());
  auto It = find_if(Range, [](const std::unique_ptr<Instruction> &I) {
    return !I->isRetired();
  });

  NumRetired = std::distance(Instructions.begin(), It);
  // Erase instructions up to the first that hasn't been retired.
  if ((NumRetired * 2) >= Instructions.size()) {
    Instructions.erase(Instructions.begin(), It);
    NumRetired = 0;
  }

  return llvm::ErrorSuccess();
}

} // namespace mca
} // namespace llvm
