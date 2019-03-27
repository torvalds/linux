//===- IndirectBrExpandPass.cpp - Expand indirectbr to switch -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Implements an expansion pass to turn `indirectbr` instructions in the IR
/// into `switch` instructions. This works by enumerating the basic blocks in
/// a dense range of integers, replacing each `blockaddr` constant with the
/// corresponding integer constant, and then building a switch that maps from
/// the integers to the actual blocks. All of the indirectbr instructions in the
/// function are redirected to this common switch.
///
/// While this is generically useful if a target is unable to codegen
/// `indirectbr` natively, it is primarily useful when there is some desire to
/// get the builtin non-jump-table lowering of a switch even when the input
/// source contained an explicit indirect branch construct.
///
/// Note that it doesn't make any sense to enable this pass unless a target also
/// disables jump-table lowering of switches. Doing that is likely to pessimize
/// the code.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "indirectbr-expand"

namespace {

class IndirectBrExpandPass : public FunctionPass {
  const TargetLowering *TLI = nullptr;

public:
  static char ID; // Pass identification, replacement for typeid

  IndirectBrExpandPass() : FunctionPass(ID) {
    initializeIndirectBrExpandPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override;
};

} // end anonymous namespace

char IndirectBrExpandPass::ID = 0;

INITIALIZE_PASS(IndirectBrExpandPass, DEBUG_TYPE,
                "Expand indirectbr instructions", false, false)

FunctionPass *llvm::createIndirectBrExpandPass() {
  return new IndirectBrExpandPass();
}

bool IndirectBrExpandPass::runOnFunction(Function &F) {
  auto &DL = F.getParent()->getDataLayout();
  auto *TPC = getAnalysisIfAvailable<TargetPassConfig>();
  if (!TPC)
    return false;

  auto &TM = TPC->getTM<TargetMachine>();
  auto &STI = *TM.getSubtargetImpl(F);
  if (!STI.enableIndirectBrExpand())
    return false;
  TLI = STI.getTargetLowering();

  SmallVector<IndirectBrInst *, 1> IndirectBrs;

  // Set of all potential successors for indirectbr instructions.
  SmallPtrSet<BasicBlock *, 4> IndirectBrSuccs;

  // Build a list of indirectbrs that we want to rewrite.
  for (BasicBlock &BB : F)
    if (auto *IBr = dyn_cast<IndirectBrInst>(BB.getTerminator())) {
      // Handle the degenerate case of no successors by replacing the indirectbr
      // with unreachable as there is no successor available.
      if (IBr->getNumSuccessors() == 0) {
        (void)new UnreachableInst(F.getContext(), IBr);
        IBr->eraseFromParent();
        continue;
      }

      IndirectBrs.push_back(IBr);
      for (BasicBlock *SuccBB : IBr->successors())
        IndirectBrSuccs.insert(SuccBB);
    }

  if (IndirectBrs.empty())
    return false;

  // If we need to replace any indirectbrs we need to establish integer
  // constants that will correspond to each of the basic blocks in the function
  // whose address escapes. We do that here and rewrite all the blockaddress
  // constants to just be those integer constants cast to a pointer type.
  SmallVector<BasicBlock *, 4> BBs;

  for (BasicBlock &BB : F) {
    // Skip blocks that aren't successors to an indirectbr we're going to
    // rewrite.
    if (!IndirectBrSuccs.count(&BB))
      continue;

    auto IsBlockAddressUse = [&](const Use &U) {
      return isa<BlockAddress>(U.getUser());
    };
    auto BlockAddressUseIt = llvm::find_if(BB.uses(), IsBlockAddressUse);
    if (BlockAddressUseIt == BB.use_end())
      continue;

    assert(std::find_if(std::next(BlockAddressUseIt), BB.use_end(),
                        IsBlockAddressUse) == BB.use_end() &&
           "There should only ever be a single blockaddress use because it is "
           "a constant and should be uniqued.");

    auto *BA = cast<BlockAddress>(BlockAddressUseIt->getUser());

    // Skip if the constant was formed but ended up not being used (due to DCE
    // or whatever).
    if (!BA->isConstantUsed())
      continue;

    // Compute the index we want to use for this basic block. We can't use zero
    // because null can be compared with block addresses.
    int BBIndex = BBs.size() + 1;
    BBs.push_back(&BB);

    auto *ITy = cast<IntegerType>(DL.getIntPtrType(BA->getType()));
    ConstantInt *BBIndexC = ConstantInt::get(ITy, BBIndex);

    // Now rewrite the blockaddress to an integer constant based on the index.
    // FIXME: We could potentially preserve the uses as arguments to inline asm.
    // This would allow some uses such as diagnostic information in crashes to
    // have higher quality even when this transform is enabled, but would break
    // users that round-trip blockaddresses through inline assembly and then
    // back into an indirectbr.
    BA->replaceAllUsesWith(ConstantExpr::getIntToPtr(BBIndexC, BA->getType()));
  }

  if (BBs.empty()) {
    // There are no blocks whose address is taken, so any indirectbr instruction
    // cannot get a valid input and we can replace all of them with unreachable.
    for (auto *IBr : IndirectBrs) {
      (void)new UnreachableInst(F.getContext(), IBr);
      IBr->eraseFromParent();
    }
    return true;
  }

  BasicBlock *SwitchBB;
  Value *SwitchValue;

  // Compute a common integer type across all the indirectbr instructions.
  IntegerType *CommonITy = nullptr;
  for (auto *IBr : IndirectBrs) {
    auto *ITy =
        cast<IntegerType>(DL.getIntPtrType(IBr->getAddress()->getType()));
    if (!CommonITy || ITy->getBitWidth() > CommonITy->getBitWidth())
      CommonITy = ITy;
  }

  auto GetSwitchValue = [DL, CommonITy](IndirectBrInst *IBr) {
    return CastInst::CreatePointerCast(
        IBr->getAddress(), CommonITy,
        Twine(IBr->getAddress()->getName()) + ".switch_cast", IBr);
  };

  if (IndirectBrs.size() == 1) {
    // If we only have one indirectbr, we can just directly replace it within
    // its block.
    SwitchBB = IndirectBrs[0]->getParent();
    SwitchValue = GetSwitchValue(IndirectBrs[0]);
    IndirectBrs[0]->eraseFromParent();
  } else {
    // Otherwise we need to create a new block to hold the switch across BBs,
    // jump to that block instead of each indirectbr, and phi together the
    // values for the switch.
    SwitchBB = BasicBlock::Create(F.getContext(), "switch_bb", &F);
    auto *SwitchPN = PHINode::Create(CommonITy, IndirectBrs.size(),
                                     "switch_value_phi", SwitchBB);
    SwitchValue = SwitchPN;

    // Now replace the indirectbr instructions with direct branches to the
    // switch block and fill out the PHI operands.
    for (auto *IBr : IndirectBrs) {
      SwitchPN->addIncoming(GetSwitchValue(IBr), IBr->getParent());
      BranchInst::Create(SwitchBB, IBr);
      IBr->eraseFromParent();
    }
  }

  // Now build the switch in the block. The block will have no terminator
  // already.
  auto *SI = SwitchInst::Create(SwitchValue, BBs[0], BBs.size(), SwitchBB);

  // Add a case for each block.
  for (int i : llvm::seq<int>(1, BBs.size()))
    SI->addCase(ConstantInt::get(CommonITy, i + 1), BBs[i]);

  return true;
}
