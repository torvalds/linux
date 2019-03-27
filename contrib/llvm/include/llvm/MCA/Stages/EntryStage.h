//===---------------------- EntryStage.h ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the Entry stage of an instruction pipeline.  Its sole
/// purpose in life is to pick instructions in sequence and move them to the
/// next pipeline stage.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_ENTRY_STAGE_H
#define LLVM_MCA_ENTRY_STAGE_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/MCA/SourceMgr.h"
#include "llvm/MCA/Stages/Stage.h"

namespace llvm {
namespace mca {

class EntryStage final : public Stage {
  InstRef CurrentInstruction;
  SmallVector<std::unique_ptr<Instruction>, 16> Instructions;
  SourceMgr &SM;
  unsigned NumRetired;

  // Updates the program counter, and sets 'CurrentInstruction'.
  void getNextInstruction();

  EntryStage(const EntryStage &Other) = delete;
  EntryStage &operator=(const EntryStage &Other) = delete;

public:
  EntryStage(SourceMgr &SM) : CurrentInstruction(), SM(SM), NumRetired(0) { }

  bool isAvailable(const InstRef &IR) const override;
  bool hasWorkToComplete() const override;
  Error execute(InstRef &IR) override;
  Error cycleStart() override;
  Error cycleEnd() override;
};

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_FETCH_STAGE_H
