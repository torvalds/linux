//===- Local.cpp - Functions to perform local transformations -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This family of functions perform various local transformations to the
// program.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/Local.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/EHPersonalities.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LazyValueInfo.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DomTreeUpdater.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdint>
#include <iterator>
#include <map>
#include <utility>

using namespace llvm;
using namespace llvm::PatternMatch;

#define DEBUG_TYPE "local"

STATISTIC(NumRemoved, "Number of unreachable basic blocks removed");

//===----------------------------------------------------------------------===//
//  Local constant propagation.
//

/// ConstantFoldTerminator - If a terminator instruction is predicated on a
/// constant value, convert it into an unconditional branch to the constant
/// destination.  This is a nontrivial operation because the successors of this
/// basic block must have their PHI nodes updated.
/// Also calls RecursivelyDeleteTriviallyDeadInstructions() on any branch/switch
/// conditions and indirectbr addresses this might make dead if
/// DeleteDeadConditions is true.
bool llvm::ConstantFoldTerminator(BasicBlock *BB, bool DeleteDeadConditions,
                                  const TargetLibraryInfo *TLI,
                                  DomTreeUpdater *DTU) {
  Instruction *T = BB->getTerminator();
  IRBuilder<> Builder(T);

  // Branch - See if we are conditional jumping on constant
  if (auto *BI = dyn_cast<BranchInst>(T)) {
    if (BI->isUnconditional()) return false;  // Can't optimize uncond branch
    BasicBlock *Dest1 = BI->getSuccessor(0);
    BasicBlock *Dest2 = BI->getSuccessor(1);

    if (auto *Cond = dyn_cast<ConstantInt>(BI->getCondition())) {
      // Are we branching on constant?
      // YES.  Change to unconditional branch...
      BasicBlock *Destination = Cond->getZExtValue() ? Dest1 : Dest2;
      BasicBlock *OldDest     = Cond->getZExtValue() ? Dest2 : Dest1;

      // Let the basic block know that we are letting go of it.  Based on this,
      // it will adjust it's PHI nodes.
      OldDest->removePredecessor(BB);

      // Replace the conditional branch with an unconditional one.
      Builder.CreateBr(Destination);
      BI->eraseFromParent();
      if (DTU)
        DTU->deleteEdgeRelaxed(BB, OldDest);
      return true;
    }

    if (Dest2 == Dest1) {       // Conditional branch to same location?
      // This branch matches something like this:
      //     br bool %cond, label %Dest, label %Dest
      // and changes it into:  br label %Dest

      // Let the basic block know that we are letting go of one copy of it.
      assert(BI->getParent() && "Terminator not inserted in block!");
      Dest1->removePredecessor(BI->getParent());

      // Replace the conditional branch with an unconditional one.
      Builder.CreateBr(Dest1);
      Value *Cond = BI->getCondition();
      BI->eraseFromParent();
      if (DeleteDeadConditions)
        RecursivelyDeleteTriviallyDeadInstructions(Cond, TLI);
      return true;
    }
    return false;
  }

  if (auto *SI = dyn_cast<SwitchInst>(T)) {
    // If we are switching on a constant, we can convert the switch to an
    // unconditional branch.
    auto *CI = dyn_cast<ConstantInt>(SI->getCondition());
    BasicBlock *DefaultDest = SI->getDefaultDest();
    BasicBlock *TheOnlyDest = DefaultDest;

    // If the default is unreachable, ignore it when searching for TheOnlyDest.
    if (isa<UnreachableInst>(DefaultDest->getFirstNonPHIOrDbg()) &&
        SI->getNumCases() > 0) {
      TheOnlyDest = SI->case_begin()->getCaseSuccessor();
    }

    // Figure out which case it goes to.
    for (auto i = SI->case_begin(), e = SI->case_end(); i != e;) {
      // Found case matching a constant operand?
      if (i->getCaseValue() == CI) {
        TheOnlyDest = i->getCaseSuccessor();
        break;
      }

      // Check to see if this branch is going to the same place as the default
      // dest.  If so, eliminate it as an explicit compare.
      if (i->getCaseSuccessor() == DefaultDest) {
        MDNode *MD = SI->getMetadata(LLVMContext::MD_prof);
        unsigned NCases = SI->getNumCases();
        // Fold the case metadata into the default if there will be any branches
        // left, unless the metadata doesn't match the switch.
        if (NCases > 1 && MD && MD->getNumOperands() == 2 + NCases) {
          // Collect branch weights into a vector.
          SmallVector<uint32_t, 8> Weights;
          for (unsigned MD_i = 1, MD_e = MD->getNumOperands(); MD_i < MD_e;
               ++MD_i) {
            auto *CI = mdconst::extract<ConstantInt>(MD->getOperand(MD_i));
            Weights.push_back(CI->getValue().getZExtValue());
          }
          // Merge weight of this case to the default weight.
          unsigned idx = i->getCaseIndex();
          Weights[0] += Weights[idx+1];
          // Remove weight for this case.
          std::swap(Weights[idx+1], Weights.back());
          Weights.pop_back();
          SI->setMetadata(LLVMContext::MD_prof,
                          MDBuilder(BB->getContext()).
                          createBranchWeights(Weights));
        }
        // Remove this entry.
        BasicBlock *ParentBB = SI->getParent();
        DefaultDest->removePredecessor(ParentBB);
        i = SI->removeCase(i);
        e = SI->case_end();
        if (DTU)
          DTU->deleteEdgeRelaxed(ParentBB, DefaultDest);
        continue;
      }

      // Otherwise, check to see if the switch only branches to one destination.
      // We do this by reseting "TheOnlyDest" to null when we find two non-equal
      // destinations.
      if (i->getCaseSuccessor() != TheOnlyDest)
        TheOnlyDest = nullptr;

      // Increment this iterator as we haven't removed the case.
      ++i;
    }

    if (CI && !TheOnlyDest) {
      // Branching on a constant, but not any of the cases, go to the default
      // successor.
      TheOnlyDest = SI->getDefaultDest();
    }

    // If we found a single destination that we can fold the switch into, do so
    // now.
    if (TheOnlyDest) {
      // Insert the new branch.
      Builder.CreateBr(TheOnlyDest);
      BasicBlock *BB = SI->getParent();
      std::vector <DominatorTree::UpdateType> Updates;
      if (DTU)
        Updates.reserve(SI->getNumSuccessors() - 1);

      // Remove entries from PHI nodes which we no longer branch to...
      for (BasicBlock *Succ : successors(SI)) {
        // Found case matching a constant operand?
        if (Succ == TheOnlyDest) {
          TheOnlyDest = nullptr; // Don't modify the first branch to TheOnlyDest
        } else {
          Succ->removePredecessor(BB);
          if (DTU)
            Updates.push_back({DominatorTree::Delete, BB, Succ});
        }
      }

      // Delete the old switch.
      Value *Cond = SI->getCondition();
      SI->eraseFromParent();
      if (DeleteDeadConditions)
        RecursivelyDeleteTriviallyDeadInstructions(Cond, TLI);
      if (DTU)
        DTU->applyUpdates(Updates, /*ForceRemoveDuplicates*/ true);
      return true;
    }

    if (SI->getNumCases() == 1) {
      // Otherwise, we can fold this switch into a conditional branch
      // instruction if it has only one non-default destination.
      auto FirstCase = *SI->case_begin();
      Value *Cond = Builder.CreateICmpEQ(SI->getCondition(),
          FirstCase.getCaseValue(), "cond");

      // Insert the new branch.
      BranchInst *NewBr = Builder.CreateCondBr(Cond,
                                               FirstCase.getCaseSuccessor(),
                                               SI->getDefaultDest());
      MDNode *MD = SI->getMetadata(LLVMContext::MD_prof);
      if (MD && MD->getNumOperands() == 3) {
        ConstantInt *SICase =
            mdconst::dyn_extract<ConstantInt>(MD->getOperand(2));
        ConstantInt *SIDef =
            mdconst::dyn_extract<ConstantInt>(MD->getOperand(1));
        assert(SICase && SIDef);
        // The TrueWeight should be the weight for the single case of SI.
        NewBr->setMetadata(LLVMContext::MD_prof,
                        MDBuilder(BB->getContext()).
                        createBranchWeights(SICase->getValue().getZExtValue(),
                                            SIDef->getValue().getZExtValue()));
      }

      // Update make.implicit metadata to the newly-created conditional branch.
      MDNode *MakeImplicitMD = SI->getMetadata(LLVMContext::MD_make_implicit);
      if (MakeImplicitMD)
        NewBr->setMetadata(LLVMContext::MD_make_implicit, MakeImplicitMD);

      // Delete the old switch.
      SI->eraseFromParent();
      return true;
    }
    return false;
  }

  if (auto *IBI = dyn_cast<IndirectBrInst>(T)) {
    // indirectbr blockaddress(@F, @BB) -> br label @BB
    if (auto *BA =
          dyn_cast<BlockAddress>(IBI->getAddress()->stripPointerCasts())) {
      BasicBlock *TheOnlyDest = BA->getBasicBlock();
      std::vector <DominatorTree::UpdateType> Updates;
      if (DTU)
        Updates.reserve(IBI->getNumDestinations() - 1);

      // Insert the new branch.
      Builder.CreateBr(TheOnlyDest);

      for (unsigned i = 0, e = IBI->getNumDestinations(); i != e; ++i) {
        if (IBI->getDestination(i) == TheOnlyDest) {
          TheOnlyDest = nullptr;
        } else {
          BasicBlock *ParentBB = IBI->getParent();
          BasicBlock *DestBB = IBI->getDestination(i);
          DestBB->removePredecessor(ParentBB);
          if (DTU)
            Updates.push_back({DominatorTree::Delete, ParentBB, DestBB});
        }
      }
      Value *Address = IBI->getAddress();
      IBI->eraseFromParent();
      if (DeleteDeadConditions)
        RecursivelyDeleteTriviallyDeadInstructions(Address, TLI);

      // If we didn't find our destination in the IBI successor list, then we
      // have undefined behavior.  Replace the unconditional branch with an
      // 'unreachable' instruction.
      if (TheOnlyDest) {
        BB->getTerminator()->eraseFromParent();
        new UnreachableInst(BB->getContext(), BB);
      }

      if (DTU)
        DTU->applyUpdates(Updates, /*ForceRemoveDuplicates*/ true);
      return true;
    }
  }

  return false;
}

//===----------------------------------------------------------------------===//
//  Local dead code elimination.
//

/// isInstructionTriviallyDead - Return true if the result produced by the
/// instruction is not used, and the instruction has no side effects.
///
bool llvm::isInstructionTriviallyDead(Instruction *I,
                                      const TargetLibraryInfo *TLI) {
  if (!I->use_empty())
    return false;
  return wouldInstructionBeTriviallyDead(I, TLI);
}

bool llvm::wouldInstructionBeTriviallyDead(Instruction *I,
                                           const TargetLibraryInfo *TLI) {
  if (I->isTerminator())
    return false;

  // We don't want the landingpad-like instructions removed by anything this
  // general.
  if (I->isEHPad())
    return false;

  // We don't want debug info removed by anything this general, unless
  // debug info is empty.
  if (DbgDeclareInst *DDI = dyn_cast<DbgDeclareInst>(I)) {
    if (DDI->getAddress())
      return false;
    return true;
  }
  if (DbgValueInst *DVI = dyn_cast<DbgValueInst>(I)) {
    if (DVI->getValue())
      return false;
    return true;
  }
  if (DbgLabelInst *DLI = dyn_cast<DbgLabelInst>(I)) {
    if (DLI->getLabel())
      return false;
    return true;
  }

  if (!I->mayHaveSideEffects())
    return true;

  // Special case intrinsics that "may have side effects" but can be deleted
  // when dead.
  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
    // Safe to delete llvm.stacksave and launder.invariant.group if dead.
    if (II->getIntrinsicID() == Intrinsic::stacksave ||
        II->getIntrinsicID() == Intrinsic::launder_invariant_group)
      return true;

    // Lifetime intrinsics are dead when their right-hand is undef.
    if (II->isLifetimeStartOrEnd())
      return isa<UndefValue>(II->getArgOperand(1));

    // Assumptions are dead if their condition is trivially true.  Guards on
    // true are operationally no-ops.  In the future we can consider more
    // sophisticated tradeoffs for guards considering potential for check
    // widening, but for now we keep things simple.
    if (II->getIntrinsicID() == Intrinsic::assume ||
        II->getIntrinsicID() == Intrinsic::experimental_guard) {
      if (ConstantInt *Cond = dyn_cast<ConstantInt>(II->getArgOperand(0)))
        return !Cond->isZero();

      return false;
    }
  }

  if (isAllocLikeFn(I, TLI))
    return true;

  if (CallInst *CI = isFreeCall(I, TLI))
    if (Constant *C = dyn_cast<Constant>(CI->getArgOperand(0)))
      return C->isNullValue() || isa<UndefValue>(C);

  if (CallSite CS = CallSite(I))
    if (isMathLibCallNoop(CS, TLI))
      return true;

  return false;
}

/// RecursivelyDeleteTriviallyDeadInstructions - If the specified value is a
/// trivially dead instruction, delete it.  If that makes any of its operands
/// trivially dead, delete them too, recursively.  Return true if any
/// instructions were deleted.
bool llvm::RecursivelyDeleteTriviallyDeadInstructions(
    Value *V, const TargetLibraryInfo *TLI, MemorySSAUpdater *MSSAU) {
  Instruction *I = dyn_cast<Instruction>(V);
  if (!I || !I->use_empty() || !isInstructionTriviallyDead(I, TLI))
    return false;

  SmallVector<Instruction*, 16> DeadInsts;
  DeadInsts.push_back(I);
  RecursivelyDeleteTriviallyDeadInstructions(DeadInsts, TLI, MSSAU);

  return true;
}

void llvm::RecursivelyDeleteTriviallyDeadInstructions(
    SmallVectorImpl<Instruction *> &DeadInsts, const TargetLibraryInfo *TLI,
    MemorySSAUpdater *MSSAU) {
  // Process the dead instruction list until empty.
  while (!DeadInsts.empty()) {
    Instruction &I = *DeadInsts.pop_back_val();
    assert(I.use_empty() && "Instructions with uses are not dead.");
    assert(isInstructionTriviallyDead(&I, TLI) &&
           "Live instruction found in dead worklist!");

    // Don't lose the debug info while deleting the instructions.
    salvageDebugInfo(I);

    // Null out all of the instruction's operands to see if any operand becomes
    // dead as we go.
    for (Use &OpU : I.operands()) {
      Value *OpV = OpU.get();
      OpU.set(nullptr);

      if (!OpV->use_empty())
        continue;

      // If the operand is an instruction that became dead as we nulled out the
      // operand, and if it is 'trivially' dead, delete it in a future loop
      // iteration.
      if (Instruction *OpI = dyn_cast<Instruction>(OpV))
        if (isInstructionTriviallyDead(OpI, TLI))
          DeadInsts.push_back(OpI);
    }
    if (MSSAU)
      MSSAU->removeMemoryAccess(&I);

    I.eraseFromParent();
  }
}

bool llvm::replaceDbgUsesWithUndef(Instruction *I) {
  SmallVector<DbgVariableIntrinsic *, 1> DbgUsers;
  findDbgUsers(DbgUsers, I);
  for (auto *DII : DbgUsers) {
    Value *Undef = UndefValue::get(I->getType());
    DII->setOperand(0, MetadataAsValue::get(DII->getContext(),
                                            ValueAsMetadata::get(Undef)));
  }
  return !DbgUsers.empty();
}

/// areAllUsesEqual - Check whether the uses of a value are all the same.
/// This is similar to Instruction::hasOneUse() except this will also return
/// true when there are no uses or multiple uses that all refer to the same
/// value.
static bool areAllUsesEqual(Instruction *I) {
  Value::user_iterator UI = I->user_begin();
  Value::user_iterator UE = I->user_end();
  if (UI == UE)
    return true;

  User *TheUse = *UI;
  for (++UI; UI != UE; ++UI) {
    if (*UI != TheUse)
      return false;
  }
  return true;
}

/// RecursivelyDeleteDeadPHINode - If the specified value is an effectively
/// dead PHI node, due to being a def-use chain of single-use nodes that
/// either forms a cycle or is terminated by a trivially dead instruction,
/// delete it.  If that makes any of its operands trivially dead, delete them
/// too, recursively.  Return true if a change was made.
bool llvm::RecursivelyDeleteDeadPHINode(PHINode *PN,
                                        const TargetLibraryInfo *TLI) {
  SmallPtrSet<Instruction*, 4> Visited;
  for (Instruction *I = PN; areAllUsesEqual(I) && !I->mayHaveSideEffects();
       I = cast<Instruction>(*I->user_begin())) {
    if (I->use_empty())
      return RecursivelyDeleteTriviallyDeadInstructions(I, TLI);

    // If we find an instruction more than once, we're on a cycle that
    // won't prove fruitful.
    if (!Visited.insert(I).second) {
      // Break the cycle and delete the instruction and its operands.
      I->replaceAllUsesWith(UndefValue::get(I->getType()));
      (void)RecursivelyDeleteTriviallyDeadInstructions(I, TLI);
      return true;
    }
  }
  return false;
}

static bool
simplifyAndDCEInstruction(Instruction *I,
                          SmallSetVector<Instruction *, 16> &WorkList,
                          const DataLayout &DL,
                          const TargetLibraryInfo *TLI) {
  if (isInstructionTriviallyDead(I, TLI)) {
    salvageDebugInfo(*I);

    // Null out all of the instruction's operands to see if any operand becomes
    // dead as we go.
    for (unsigned i = 0, e = I->getNumOperands(); i != e; ++i) {
      Value *OpV = I->getOperand(i);
      I->setOperand(i, nullptr);

      if (!OpV->use_empty() || I == OpV)
        continue;

      // If the operand is an instruction that became dead as we nulled out the
      // operand, and if it is 'trivially' dead, delete it in a future loop
      // iteration.
      if (Instruction *OpI = dyn_cast<Instruction>(OpV))
        if (isInstructionTriviallyDead(OpI, TLI))
          WorkList.insert(OpI);
    }

    I->eraseFromParent();

    return true;
  }

  if (Value *SimpleV = SimplifyInstruction(I, DL)) {
    // Add the users to the worklist. CAREFUL: an instruction can use itself,
    // in the case of a phi node.
    for (User *U : I->users()) {
      if (U != I) {
        WorkList.insert(cast<Instruction>(U));
      }
    }

    // Replace the instruction with its simplified value.
    bool Changed = false;
    if (!I->use_empty()) {
      I->replaceAllUsesWith(SimpleV);
      Changed = true;
    }
    if (isInstructionTriviallyDead(I, TLI)) {
      I->eraseFromParent();
      Changed = true;
    }
    return Changed;
  }
  return false;
}

/// SimplifyInstructionsInBlock - Scan the specified basic block and try to
/// simplify any instructions in it and recursively delete dead instructions.
///
/// This returns true if it changed the code, note that it can delete
/// instructions in other blocks as well in this block.
bool llvm::SimplifyInstructionsInBlock(BasicBlock *BB,
                                       const TargetLibraryInfo *TLI) {
  bool MadeChange = false;
  const DataLayout &DL = BB->getModule()->getDataLayout();

#ifndef NDEBUG
  // In debug builds, ensure that the terminator of the block is never replaced
  // or deleted by these simplifications. The idea of simplification is that it
  // cannot introduce new instructions, and there is no way to replace the
  // terminator of a block without introducing a new instruction.
  AssertingVH<Instruction> TerminatorVH(&BB->back());
#endif

  SmallSetVector<Instruction *, 16> WorkList;
  // Iterate over the original function, only adding insts to the worklist
  // if they actually need to be revisited. This avoids having to pre-init
  // the worklist with the entire function's worth of instructions.
  for (BasicBlock::iterator BI = BB->begin(), E = std::prev(BB->end());
       BI != E;) {
    assert(!BI->isTerminator());
    Instruction *I = &*BI;
    ++BI;

    // We're visiting this instruction now, so make sure it's not in the
    // worklist from an earlier visit.
    if (!WorkList.count(I))
      MadeChange |= simplifyAndDCEInstruction(I, WorkList, DL, TLI);
  }

  while (!WorkList.empty()) {
    Instruction *I = WorkList.pop_back_val();
    MadeChange |= simplifyAndDCEInstruction(I, WorkList, DL, TLI);
  }
  return MadeChange;
}

//===----------------------------------------------------------------------===//
//  Control Flow Graph Restructuring.
//

/// RemovePredecessorAndSimplify - Like BasicBlock::removePredecessor, this
/// method is called when we're about to delete Pred as a predecessor of BB.  If
/// BB contains any PHI nodes, this drops the entries in the PHI nodes for Pred.
///
/// Unlike the removePredecessor method, this attempts to simplify uses of PHI
/// nodes that collapse into identity values.  For example, if we have:
///   x = phi(1, 0, 0, 0)
///   y = and x, z
///
/// .. and delete the predecessor corresponding to the '1', this will attempt to
/// recursively fold the and to 0.
void llvm::RemovePredecessorAndSimplify(BasicBlock *BB, BasicBlock *Pred,
                                        DomTreeUpdater *DTU) {
  // This only adjusts blocks with PHI nodes.
  if (!isa<PHINode>(BB->begin()))
    return;

  // Remove the entries for Pred from the PHI nodes in BB, but do not simplify
  // them down.  This will leave us with single entry phi nodes and other phis
  // that can be removed.
  BB->removePredecessor(Pred, true);

  WeakTrackingVH PhiIt = &BB->front();
  while (PHINode *PN = dyn_cast<PHINode>(PhiIt)) {
    PhiIt = &*++BasicBlock::iterator(cast<Instruction>(PhiIt));
    Value *OldPhiIt = PhiIt;

    if (!recursivelySimplifyInstruction(PN))
      continue;

    // If recursive simplification ended up deleting the next PHI node we would
    // iterate to, then our iterator is invalid, restart scanning from the top
    // of the block.
    if (PhiIt != OldPhiIt) PhiIt = &BB->front();
  }
  if (DTU)
    DTU->deleteEdgeRelaxed(Pred, BB);
}

/// MergeBasicBlockIntoOnlyPred - DestBB is a block with one predecessor and its
/// predecessor is known to have one successor (DestBB!). Eliminate the edge
/// between them, moving the instructions in the predecessor into DestBB and
/// deleting the predecessor block.
void llvm::MergeBasicBlockIntoOnlyPred(BasicBlock *DestBB,
                                       DomTreeUpdater *DTU) {

  // If BB has single-entry PHI nodes, fold them.
  while (PHINode *PN = dyn_cast<PHINode>(DestBB->begin())) {
    Value *NewVal = PN->getIncomingValue(0);
    // Replace self referencing PHI with undef, it must be dead.
    if (NewVal == PN) NewVal = UndefValue::get(PN->getType());
    PN->replaceAllUsesWith(NewVal);
    PN->eraseFromParent();
  }

  BasicBlock *PredBB = DestBB->getSinglePredecessor();
  assert(PredBB && "Block doesn't have a single predecessor!");

  bool ReplaceEntryBB = false;
  if (PredBB == &DestBB->getParent()->getEntryBlock())
    ReplaceEntryBB = true;

  // DTU updates: Collect all the edges that enter
  // PredBB. These dominator edges will be redirected to DestBB.
  SmallVector<DominatorTree::UpdateType, 32> Updates;

  if (DTU) {
    Updates.push_back({DominatorTree::Delete, PredBB, DestBB});
    for (auto I = pred_begin(PredBB), E = pred_end(PredBB); I != E; ++I) {
      Updates.push_back({DominatorTree::Delete, *I, PredBB});
      // This predecessor of PredBB may already have DestBB as a successor.
      if (llvm::find(successors(*I), DestBB) == succ_end(*I))
        Updates.push_back({DominatorTree::Insert, *I, DestBB});
    }
  }

  // Zap anything that took the address of DestBB.  Not doing this will give the
  // address an invalid value.
  if (DestBB->hasAddressTaken()) {
    BlockAddress *BA = BlockAddress::get(DestBB);
    Constant *Replacement =
      ConstantInt::get(Type::getInt32Ty(BA->getContext()), 1);
    BA->replaceAllUsesWith(ConstantExpr::getIntToPtr(Replacement,
                                                     BA->getType()));
    BA->destroyConstant();
  }

  // Anything that branched to PredBB now branches to DestBB.
  PredBB->replaceAllUsesWith(DestBB);

  // Splice all the instructions from PredBB to DestBB.
  PredBB->getTerminator()->eraseFromParent();
  DestBB->getInstList().splice(DestBB->begin(), PredBB->getInstList());
  new UnreachableInst(PredBB->getContext(), PredBB);

  // If the PredBB is the entry block of the function, move DestBB up to
  // become the entry block after we erase PredBB.
  if (ReplaceEntryBB)
    DestBB->moveAfter(PredBB);

  if (DTU) {
    assert(PredBB->getInstList().size() == 1 &&
           isa<UnreachableInst>(PredBB->getTerminator()) &&
           "The successor list of PredBB isn't empty before "
           "applying corresponding DTU updates.");
    DTU->applyUpdates(Updates, /*ForceRemoveDuplicates*/ true);
    DTU->deleteBB(PredBB);
    // Recalculation of DomTree is needed when updating a forward DomTree and
    // the Entry BB is replaced.
    if (ReplaceEntryBB && DTU->hasDomTree()) {
      // The entry block was removed and there is no external interface for
      // the dominator tree to be notified of this change. In this corner-case
      // we recalculate the entire tree.
      DTU->recalculate(*(DestBB->getParent()));
    }
  }

  else {
    PredBB->eraseFromParent(); // Nuke BB if DTU is nullptr.
  }
}

/// CanMergeValues - Return true if we can choose one of these values to use
/// in place of the other. Note that we will always choose the non-undef
/// value to keep.
static bool CanMergeValues(Value *First, Value *Second) {
  return First == Second || isa<UndefValue>(First) || isa<UndefValue>(Second);
}

/// CanPropagatePredecessorsForPHIs - Return true if we can fold BB, an
/// almost-empty BB ending in an unconditional branch to Succ, into Succ.
///
/// Assumption: Succ is the single successor for BB.
static bool CanPropagatePredecessorsForPHIs(BasicBlock *BB, BasicBlock *Succ) {
  assert(*succ_begin(BB) == Succ && "Succ is not successor of BB!");

  LLVM_DEBUG(dbgs() << "Looking to fold " << BB->getName() << " into "
                    << Succ->getName() << "\n");
  // Shortcut, if there is only a single predecessor it must be BB and merging
  // is always safe
  if (Succ->getSinglePredecessor()) return true;

  // Make a list of the predecessors of BB
  SmallPtrSet<BasicBlock*, 16> BBPreds(pred_begin(BB), pred_end(BB));

  // Look at all the phi nodes in Succ, to see if they present a conflict when
  // merging these blocks
  for (BasicBlock::iterator I = Succ->begin(); isa<PHINode>(I); ++I) {
    PHINode *PN = cast<PHINode>(I);

    // If the incoming value from BB is again a PHINode in
    // BB which has the same incoming value for *PI as PN does, we can
    // merge the phi nodes and then the blocks can still be merged
    PHINode *BBPN = dyn_cast<PHINode>(PN->getIncomingValueForBlock(BB));
    if (BBPN && BBPN->getParent() == BB) {
      for (unsigned PI = 0, PE = PN->getNumIncomingValues(); PI != PE; ++PI) {
        BasicBlock *IBB = PN->getIncomingBlock(PI);
        if (BBPreds.count(IBB) &&
            !CanMergeValues(BBPN->getIncomingValueForBlock(IBB),
                            PN->getIncomingValue(PI))) {
          LLVM_DEBUG(dbgs()
                     << "Can't fold, phi node " << PN->getName() << " in "
                     << Succ->getName() << " is conflicting with "
                     << BBPN->getName() << " with regard to common predecessor "
                     << IBB->getName() << "\n");
          return false;
        }
      }
    } else {
      Value* Val = PN->getIncomingValueForBlock(BB);
      for (unsigned PI = 0, PE = PN->getNumIncomingValues(); PI != PE; ++PI) {
        // See if the incoming value for the common predecessor is equal to the
        // one for BB, in which case this phi node will not prevent the merging
        // of the block.
        BasicBlock *IBB = PN->getIncomingBlock(PI);
        if (BBPreds.count(IBB) &&
            !CanMergeValues(Val, PN->getIncomingValue(PI))) {
          LLVM_DEBUG(dbgs() << "Can't fold, phi node " << PN->getName()
                            << " in " << Succ->getName()
                            << " is conflicting with regard to common "
                            << "predecessor " << IBB->getName() << "\n");
          return false;
        }
      }
    }
  }

  return true;
}

using PredBlockVector = SmallVector<BasicBlock *, 16>;
using IncomingValueMap = DenseMap<BasicBlock *, Value *>;

/// Determines the value to use as the phi node input for a block.
///
/// Select between \p OldVal any value that we know flows from \p BB
/// to a particular phi on the basis of which one (if either) is not
/// undef. Update IncomingValues based on the selected value.
///
/// \param OldVal The value we are considering selecting.
/// \param BB The block that the value flows in from.
/// \param IncomingValues A map from block-to-value for other phi inputs
/// that we have examined.
///
/// \returns the selected value.
static Value *selectIncomingValueForBlock(Value *OldVal, BasicBlock *BB,
                                          IncomingValueMap &IncomingValues) {
  if (!isa<UndefValue>(OldVal)) {
    assert((!IncomingValues.count(BB) ||
            IncomingValues.find(BB)->second == OldVal) &&
           "Expected OldVal to match incoming value from BB!");

    IncomingValues.insert(std::make_pair(BB, OldVal));
    return OldVal;
  }

  IncomingValueMap::const_iterator It = IncomingValues.find(BB);
  if (It != IncomingValues.end()) return It->second;

  return OldVal;
}

/// Create a map from block to value for the operands of a
/// given phi.
///
/// Create a map from block to value for each non-undef value flowing
/// into \p PN.
///
/// \param PN The phi we are collecting the map for.
/// \param IncomingValues [out] The map from block to value for this phi.
static void gatherIncomingValuesToPhi(PHINode *PN,
                                      IncomingValueMap &IncomingValues) {
  for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
    BasicBlock *BB = PN->getIncomingBlock(i);
    Value *V = PN->getIncomingValue(i);

    if (!isa<UndefValue>(V))
      IncomingValues.insert(std::make_pair(BB, V));
  }
}

/// Replace the incoming undef values to a phi with the values
/// from a block-to-value map.
///
/// \param PN The phi we are replacing the undefs in.
/// \param IncomingValues A map from block to value.
static void replaceUndefValuesInPhi(PHINode *PN,
                                    const IncomingValueMap &IncomingValues) {
  for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
    Value *V = PN->getIncomingValue(i);

    if (!isa<UndefValue>(V)) continue;

    BasicBlock *BB = PN->getIncomingBlock(i);
    IncomingValueMap::const_iterator It = IncomingValues.find(BB);
    if (It == IncomingValues.end()) continue;

    PN->setIncomingValue(i, It->second);
  }
}

/// Replace a value flowing from a block to a phi with
/// potentially multiple instances of that value flowing from the
/// block's predecessors to the phi.
///
/// \param BB The block with the value flowing into the phi.
/// \param BBPreds The predecessors of BB.
/// \param PN The phi that we are updating.
static void redirectValuesFromPredecessorsToPhi(BasicBlock *BB,
                                                const PredBlockVector &BBPreds,
                                                PHINode *PN) {
  Value *OldVal = PN->removeIncomingValue(BB, false);
  assert(OldVal && "No entry in PHI for Pred BB!");

  IncomingValueMap IncomingValues;

  // We are merging two blocks - BB, and the block containing PN - and
  // as a result we need to redirect edges from the predecessors of BB
  // to go to the block containing PN, and update PN
  // accordingly. Since we allow merging blocks in the case where the
  // predecessor and successor blocks both share some predecessors,
  // and where some of those common predecessors might have undef
  // values flowing into PN, we want to rewrite those values to be
  // consistent with the non-undef values.

  gatherIncomingValuesToPhi(PN, IncomingValues);

  // If this incoming value is one of the PHI nodes in BB, the new entries
  // in the PHI node are the entries from the old PHI.
  if (isa<PHINode>(OldVal) && cast<PHINode>(OldVal)->getParent() == BB) {
    PHINode *OldValPN = cast<PHINode>(OldVal);
    for (unsigned i = 0, e = OldValPN->getNumIncomingValues(); i != e; ++i) {
      // Note that, since we are merging phi nodes and BB and Succ might
      // have common predecessors, we could end up with a phi node with
      // identical incoming branches. This will be cleaned up later (and
      // will trigger asserts if we try to clean it up now, without also
      // simplifying the corresponding conditional branch).
      BasicBlock *PredBB = OldValPN->getIncomingBlock(i);
      Value *PredVal = OldValPN->getIncomingValue(i);
      Value *Selected = selectIncomingValueForBlock(PredVal, PredBB,
                                                    IncomingValues);

      // And add a new incoming value for this predecessor for the
      // newly retargeted branch.
      PN->addIncoming(Selected, PredBB);
    }
  } else {
    for (unsigned i = 0, e = BBPreds.size(); i != e; ++i) {
      // Update existing incoming values in PN for this
      // predecessor of BB.
      BasicBlock *PredBB = BBPreds[i];
      Value *Selected = selectIncomingValueForBlock(OldVal, PredBB,
                                                    IncomingValues);

      // And add a new incoming value for this predecessor for the
      // newly retargeted branch.
      PN->addIncoming(Selected, PredBB);
    }
  }

  replaceUndefValuesInPhi(PN, IncomingValues);
}

/// TryToSimplifyUncondBranchFromEmptyBlock - BB is known to contain an
/// unconditional branch, and contains no instructions other than PHI nodes,
/// potential side-effect free intrinsics and the branch.  If possible,
/// eliminate BB by rewriting all the predecessors to branch to the successor
/// block and return true.  If we can't transform, return false.
bool llvm::TryToSimplifyUncondBranchFromEmptyBlock(BasicBlock *BB,
                                                   DomTreeUpdater *DTU) {
  assert(BB != &BB->getParent()->getEntryBlock() &&
         "TryToSimplifyUncondBranchFromEmptyBlock called on entry block!");

  // We can't eliminate infinite loops.
  BasicBlock *Succ = cast<BranchInst>(BB->getTerminator())->getSuccessor(0);
  if (BB == Succ) return false;

  // Check to see if merging these blocks would cause conflicts for any of the
  // phi nodes in BB or Succ. If not, we can safely merge.
  if (!CanPropagatePredecessorsForPHIs(BB, Succ)) return false;

  // Check for cases where Succ has multiple predecessors and a PHI node in BB
  // has uses which will not disappear when the PHI nodes are merged.  It is
  // possible to handle such cases, but difficult: it requires checking whether
  // BB dominates Succ, which is non-trivial to calculate in the case where
  // Succ has multiple predecessors.  Also, it requires checking whether
  // constructing the necessary self-referential PHI node doesn't introduce any
  // conflicts; this isn't too difficult, but the previous code for doing this
  // was incorrect.
  //
  // Note that if this check finds a live use, BB dominates Succ, so BB is
  // something like a loop pre-header (or rarely, a part of an irreducible CFG);
  // folding the branch isn't profitable in that case anyway.
  if (!Succ->getSinglePredecessor()) {
    BasicBlock::iterator BBI = BB->begin();
    while (isa<PHINode>(*BBI)) {
      for (Use &U : BBI->uses()) {
        if (PHINode* PN = dyn_cast<PHINode>(U.getUser())) {
          if (PN->getIncomingBlock(U) != BB)
            return false;
        } else {
          return false;
        }
      }
      ++BBI;
    }
  }

  LLVM_DEBUG(dbgs() << "Killing Trivial BB: \n" << *BB);

  SmallVector<DominatorTree::UpdateType, 32> Updates;
  if (DTU) {
    Updates.push_back({DominatorTree::Delete, BB, Succ});
    // All predecessors of BB will be moved to Succ.
    for (auto I = pred_begin(BB), E = pred_end(BB); I != E; ++I) {
      Updates.push_back({DominatorTree::Delete, *I, BB});
      // This predecessor of BB may already have Succ as a successor.
      if (llvm::find(successors(*I), Succ) == succ_end(*I))
        Updates.push_back({DominatorTree::Insert, *I, Succ});
    }
  }

  if (isa<PHINode>(Succ->begin())) {
    // If there is more than one pred of succ, and there are PHI nodes in
    // the successor, then we need to add incoming edges for the PHI nodes
    //
    const PredBlockVector BBPreds(pred_begin(BB), pred_end(BB));

    // Loop over all of the PHI nodes in the successor of BB.
    for (BasicBlock::iterator I = Succ->begin(); isa<PHINode>(I); ++I) {
      PHINode *PN = cast<PHINode>(I);

      redirectValuesFromPredecessorsToPhi(BB, BBPreds, PN);
    }
  }

  if (Succ->getSinglePredecessor()) {
    // BB is the only predecessor of Succ, so Succ will end up with exactly
    // the same predecessors BB had.

    // Copy over any phi, debug or lifetime instruction.
    BB->getTerminator()->eraseFromParent();
    Succ->getInstList().splice(Succ->getFirstNonPHI()->getIterator(),
                               BB->getInstList());
  } else {
    while (PHINode *PN = dyn_cast<PHINode>(&BB->front())) {
      // We explicitly check for such uses in CanPropagatePredecessorsForPHIs.
      assert(PN->use_empty() && "There shouldn't be any uses here!");
      PN->eraseFromParent();
    }
  }

  // If the unconditional branch we replaced contains llvm.loop metadata, we
  // add the metadata to the branch instructions in the predecessors.
  unsigned LoopMDKind = BB->getContext().getMDKindID("llvm.loop");
  Instruction *TI = BB->getTerminator();
  if (TI)
    if (MDNode *LoopMD = TI->getMetadata(LoopMDKind))
      for (pred_iterator PI = pred_begin(BB), E = pred_end(BB); PI != E; ++PI) {
        BasicBlock *Pred = *PI;
        Pred->getTerminator()->setMetadata(LoopMDKind, LoopMD);
      }

  // Everything that jumped to BB now goes to Succ.
  BB->replaceAllUsesWith(Succ);
  if (!Succ->hasName()) Succ->takeName(BB);

  // Clear the successor list of BB to match updates applying to DTU later.
  if (BB->getTerminator())
    BB->getInstList().pop_back();
  new UnreachableInst(BB->getContext(), BB);
  assert(succ_empty(BB) && "The successor list of BB isn't empty before "
                           "applying corresponding DTU updates.");

  if (DTU) {
    DTU->applyUpdates(Updates, /*ForceRemoveDuplicates*/ true);
    DTU->deleteBB(BB);
  } else {
    BB->eraseFromParent(); // Delete the old basic block.
  }
  return true;
}

/// EliminateDuplicatePHINodes - Check for and eliminate duplicate PHI
/// nodes in this block. This doesn't try to be clever about PHI nodes
/// which differ only in the order of the incoming values, but instcombine
/// orders them so it usually won't matter.
bool llvm::EliminateDuplicatePHINodes(BasicBlock *BB) {
  // This implementation doesn't currently consider undef operands
  // specially. Theoretically, two phis which are identical except for
  // one having an undef where the other doesn't could be collapsed.

  struct PHIDenseMapInfo {
    static PHINode *getEmptyKey() {
      return DenseMapInfo<PHINode *>::getEmptyKey();
    }

    static PHINode *getTombstoneKey() {
      return DenseMapInfo<PHINode *>::getTombstoneKey();
    }

    static unsigned getHashValue(PHINode *PN) {
      // Compute a hash value on the operands. Instcombine will likely have
      // sorted them, which helps expose duplicates, but we have to check all
      // the operands to be safe in case instcombine hasn't run.
      return static_cast<unsigned>(hash_combine(
          hash_combine_range(PN->value_op_begin(), PN->value_op_end()),
          hash_combine_range(PN->block_begin(), PN->block_end())));
    }

    static bool isEqual(PHINode *LHS, PHINode *RHS) {
      if (LHS == getEmptyKey() || LHS == getTombstoneKey() ||
          RHS == getEmptyKey() || RHS == getTombstoneKey())
        return LHS == RHS;
      return LHS->isIdenticalTo(RHS);
    }
  };

  // Set of unique PHINodes.
  DenseSet<PHINode *, PHIDenseMapInfo> PHISet;

  // Examine each PHI.
  bool Changed = false;
  for (auto I = BB->begin(); PHINode *PN = dyn_cast<PHINode>(I++);) {
    auto Inserted = PHISet.insert(PN);
    if (!Inserted.second) {
      // A duplicate. Replace this PHI with its duplicate.
      PN->replaceAllUsesWith(*Inserted.first);
      PN->eraseFromParent();
      Changed = true;

      // The RAUW can change PHIs that we already visited. Start over from the
      // beginning.
      PHISet.clear();
      I = BB->begin();
    }
  }

  return Changed;
}

/// enforceKnownAlignment - If the specified pointer points to an object that
/// we control, modify the object's alignment to PrefAlign. This isn't
/// often possible though. If alignment is important, a more reliable approach
/// is to simply align all global variables and allocation instructions to
/// their preferred alignment from the beginning.
static unsigned enforceKnownAlignment(Value *V, unsigned Align,
                                      unsigned PrefAlign,
                                      const DataLayout &DL) {
  assert(PrefAlign > Align);

  V = V->stripPointerCasts();

  if (AllocaInst *AI = dyn_cast<AllocaInst>(V)) {
    // TODO: ideally, computeKnownBits ought to have used
    // AllocaInst::getAlignment() in its computation already, making
    // the below max redundant. But, as it turns out,
    // stripPointerCasts recurses through infinite layers of bitcasts,
    // while computeKnownBits is not allowed to traverse more than 6
    // levels.
    Align = std::max(AI->getAlignment(), Align);
    if (PrefAlign <= Align)
      return Align;

    // If the preferred alignment is greater than the natural stack alignment
    // then don't round up. This avoids dynamic stack realignment.
    if (DL.exceedsNaturalStackAlignment(PrefAlign))
      return Align;
    AI->setAlignment(PrefAlign);
    return PrefAlign;
  }

  if (auto *GO = dyn_cast<GlobalObject>(V)) {
    // TODO: as above, this shouldn't be necessary.
    Align = std::max(GO->getAlignment(), Align);
    if (PrefAlign <= Align)
      return Align;

    // If there is a large requested alignment and we can, bump up the alignment
    // of the global.  If the memory we set aside for the global may not be the
    // memory used by the final program then it is impossible for us to reliably
    // enforce the preferred alignment.
    if (!GO->canIncreaseAlignment())
      return Align;

    GO->setAlignment(PrefAlign);
    return PrefAlign;
  }

  return Align;
}

unsigned llvm::getOrEnforceKnownAlignment(Value *V, unsigned PrefAlign,
                                          const DataLayout &DL,
                                          const Instruction *CxtI,
                                          AssumptionCache *AC,
                                          const DominatorTree *DT) {
  assert(V->getType()->isPointerTy() &&
         "getOrEnforceKnownAlignment expects a pointer!");

  KnownBits Known = computeKnownBits(V, DL, 0, AC, CxtI, DT);
  unsigned TrailZ = Known.countMinTrailingZeros();

  // Avoid trouble with ridiculously large TrailZ values, such as
  // those computed from a null pointer.
  TrailZ = std::min(TrailZ, unsigned(sizeof(unsigned) * CHAR_BIT - 1));

  unsigned Align = 1u << std::min(Known.getBitWidth() - 1, TrailZ);

  // LLVM doesn't support alignments larger than this currently.
  Align = std::min(Align, +Value::MaximumAlignment);

  if (PrefAlign > Align)
    Align = enforceKnownAlignment(V, Align, PrefAlign, DL);

  // We don't need to make any adjustment.
  return Align;
}

///===---------------------------------------------------------------------===//
///  Dbg Intrinsic utilities
///

/// See if there is a dbg.value intrinsic for DIVar before I.
static bool LdStHasDebugValue(DILocalVariable *DIVar, DIExpression *DIExpr,
                              Instruction *I) {
  // Since we can't guarantee that the original dbg.declare instrinsic
  // is removed by LowerDbgDeclare(), we need to make sure that we are
  // not inserting the same dbg.value intrinsic over and over.
  BasicBlock::InstListType::iterator PrevI(I);
  if (PrevI != I->getParent()->getInstList().begin()) {
    --PrevI;
    if (DbgValueInst *DVI = dyn_cast<DbgValueInst>(PrevI))
      if (DVI->getValue() == I->getOperand(0) &&
          DVI->getVariable() == DIVar &&
          DVI->getExpression() == DIExpr)
        return true;
  }
  return false;
}

/// See if there is a dbg.value intrinsic for DIVar for the PHI node.
static bool PhiHasDebugValue(DILocalVariable *DIVar,
                             DIExpression *DIExpr,
                             PHINode *APN) {
  // Since we can't guarantee that the original dbg.declare instrinsic
  // is removed by LowerDbgDeclare(), we need to make sure that we are
  // not inserting the same dbg.value intrinsic over and over.
  SmallVector<DbgValueInst *, 1> DbgValues;
  findDbgValues(DbgValues, APN);
  for (auto *DVI : DbgValues) {
    assert(DVI->getValue() == APN);
    if ((DVI->getVariable() == DIVar) && (DVI->getExpression() == DIExpr))
      return true;
  }
  return false;
}

/// Check if the alloc size of \p ValTy is large enough to cover the variable
/// (or fragment of the variable) described by \p DII.
///
/// This is primarily intended as a helper for the different
/// ConvertDebugDeclareToDebugValue functions. The dbg.declare/dbg.addr that is
/// converted describes an alloca'd variable, so we need to use the
/// alloc size of the value when doing the comparison. E.g. an i1 value will be
/// identified as covering an n-bit fragment, if the store size of i1 is at
/// least n bits.
static bool valueCoversEntireFragment(Type *ValTy, DbgVariableIntrinsic *DII) {
  const DataLayout &DL = DII->getModule()->getDataLayout();
  uint64_t ValueSize = DL.getTypeAllocSizeInBits(ValTy);
  if (auto FragmentSize = DII->getFragmentSizeInBits())
    return ValueSize >= *FragmentSize;
  // We can't always calculate the size of the DI variable (e.g. if it is a
  // VLA). Try to use the size of the alloca that the dbg intrinsic describes
  // intead.
  if (DII->isAddressOfVariable())
    if (auto *AI = dyn_cast_or_null<AllocaInst>(DII->getVariableLocation()))
      if (auto FragmentSize = AI->getAllocationSizeInBits(DL))
        return ValueSize >= *FragmentSize;
  // Could not determine size of variable. Conservatively return false.
  return false;
}

/// Inserts a llvm.dbg.value intrinsic before a store to an alloca'd value
/// that has an associated llvm.dbg.declare or llvm.dbg.addr intrinsic.
void llvm::ConvertDebugDeclareToDebugValue(DbgVariableIntrinsic *DII,
                                           StoreInst *SI, DIBuilder &Builder) {
  assert(DII->isAddressOfVariable());
  auto *DIVar = DII->getVariable();
  assert(DIVar && "Missing variable");
  auto *DIExpr = DII->getExpression();
  Value *DV = SI->getOperand(0);

  if (!valueCoversEntireFragment(SI->getValueOperand()->getType(), DII)) {
    // FIXME: If storing to a part of the variable described by the dbg.declare,
    // then we want to insert a dbg.value for the corresponding fragment.
    LLVM_DEBUG(dbgs() << "Failed to convert dbg.declare to dbg.value: "
                      << *DII << '\n');
    // For now, when there is a store to parts of the variable (but we do not
    // know which part) we insert an dbg.value instrinsic to indicate that we
    // know nothing about the variable's content.
    DV = UndefValue::get(DV->getType());
    if (!LdStHasDebugValue(DIVar, DIExpr, SI))
      Builder.insertDbgValueIntrinsic(DV, DIVar, DIExpr, DII->getDebugLoc(),
                                      SI);
    return;
  }

  if (!LdStHasDebugValue(DIVar, DIExpr, SI))
    Builder.insertDbgValueIntrinsic(DV, DIVar, DIExpr, DII->getDebugLoc(),
                                    SI);
}

/// Inserts a llvm.dbg.value intrinsic before a load of an alloca'd value
/// that has an associated llvm.dbg.declare or llvm.dbg.addr intrinsic.
void llvm::ConvertDebugDeclareToDebugValue(DbgVariableIntrinsic *DII,
                                           LoadInst *LI, DIBuilder &Builder) {
  auto *DIVar = DII->getVariable();
  auto *DIExpr = DII->getExpression();
  assert(DIVar && "Missing variable");

  if (LdStHasDebugValue(DIVar, DIExpr, LI))
    return;

  if (!valueCoversEntireFragment(LI->getType(), DII)) {
    // FIXME: If only referring to a part of the variable described by the
    // dbg.declare, then we want to insert a dbg.value for the corresponding
    // fragment.
    LLVM_DEBUG(dbgs() << "Failed to convert dbg.declare to dbg.value: "
                      << *DII << '\n');
    return;
  }

  // We are now tracking the loaded value instead of the address. In the
  // future if multi-location support is added to the IR, it might be
  // preferable to keep tracking both the loaded value and the original
  // address in case the alloca can not be elided.
  Instruction *DbgValue = Builder.insertDbgValueIntrinsic(
      LI, DIVar, DIExpr, DII->getDebugLoc(), (Instruction *)nullptr);
  DbgValue->insertAfter(LI);
}

/// Inserts a llvm.dbg.value intrinsic after a phi that has an associated
/// llvm.dbg.declare or llvm.dbg.addr intrinsic.
void llvm::ConvertDebugDeclareToDebugValue(DbgVariableIntrinsic *DII,
                                           PHINode *APN, DIBuilder &Builder) {
  auto *DIVar = DII->getVariable();
  auto *DIExpr = DII->getExpression();
  assert(DIVar && "Missing variable");

  if (PhiHasDebugValue(DIVar, DIExpr, APN))
    return;

  if (!valueCoversEntireFragment(APN->getType(), DII)) {
    // FIXME: If only referring to a part of the variable described by the
    // dbg.declare, then we want to insert a dbg.value for the corresponding
    // fragment.
    LLVM_DEBUG(dbgs() << "Failed to convert dbg.declare to dbg.value: "
                      << *DII << '\n');
    return;
  }

  BasicBlock *BB = APN->getParent();
  auto InsertionPt = BB->getFirstInsertionPt();

  // The block may be a catchswitch block, which does not have a valid
  // insertion point.
  // FIXME: Insert dbg.value markers in the successors when appropriate.
  if (InsertionPt != BB->end())
    Builder.insertDbgValueIntrinsic(APN, DIVar, DIExpr, DII->getDebugLoc(),
                                    &*InsertionPt);
}

/// Determine whether this alloca is either a VLA or an array.
static bool isArray(AllocaInst *AI) {
  return AI->isArrayAllocation() ||
    AI->getType()->getElementType()->isArrayTy();
}

/// LowerDbgDeclare - Lowers llvm.dbg.declare intrinsics into appropriate set
/// of llvm.dbg.value intrinsics.
bool llvm::LowerDbgDeclare(Function &F) {
  DIBuilder DIB(*F.getParent(), /*AllowUnresolved*/ false);
  SmallVector<DbgDeclareInst *, 4> Dbgs;
  for (auto &FI : F)
    for (Instruction &BI : FI)
      if (auto DDI = dyn_cast<DbgDeclareInst>(&BI))
        Dbgs.push_back(DDI);

  if (Dbgs.empty())
    return false;

  for (auto &I : Dbgs) {
    DbgDeclareInst *DDI = I;
    AllocaInst *AI = dyn_cast_or_null<AllocaInst>(DDI->getAddress());
    // If this is an alloca for a scalar variable, insert a dbg.value
    // at each load and store to the alloca and erase the dbg.declare.
    // The dbg.values allow tracking a variable even if it is not
    // stored on the stack, while the dbg.declare can only describe
    // the stack slot (and at a lexical-scope granularity). Later
    // passes will attempt to elide the stack slot.
    if (!AI || isArray(AI))
      continue;

    // A volatile load/store means that the alloca can't be elided anyway.
    if (llvm::any_of(AI->users(), [](User *U) -> bool {
          if (LoadInst *LI = dyn_cast<LoadInst>(U))
            return LI->isVolatile();
          if (StoreInst *SI = dyn_cast<StoreInst>(U))
            return SI->isVolatile();
          return false;
        }))
      continue;

    for (auto &AIUse : AI->uses()) {
      User *U = AIUse.getUser();
      if (StoreInst *SI = dyn_cast<StoreInst>(U)) {
        if (AIUse.getOperandNo() == 1)
          ConvertDebugDeclareToDebugValue(DDI, SI, DIB);
      } else if (LoadInst *LI = dyn_cast<LoadInst>(U)) {
        ConvertDebugDeclareToDebugValue(DDI, LI, DIB);
      } else if (CallInst *CI = dyn_cast<CallInst>(U)) {
        // This is a call by-value or some other instruction that takes a
        // pointer to the variable. Insert a *value* intrinsic that describes
        // the variable by dereferencing the alloca.
        auto *DerefExpr =
            DIExpression::append(DDI->getExpression(), dwarf::DW_OP_deref);
        DIB.insertDbgValueIntrinsic(AI, DDI->getVariable(), DerefExpr,
                                    DDI->getDebugLoc(), CI);
      }
    }
    DDI->eraseFromParent();
  }
  return true;
}

/// Propagate dbg.value intrinsics through the newly inserted PHIs.
void llvm::insertDebugValuesForPHIs(BasicBlock *BB,
                                    SmallVectorImpl<PHINode *> &InsertedPHIs) {
  assert(BB && "No BasicBlock to clone dbg.value(s) from.");
  if (InsertedPHIs.size() == 0)
    return;

  // Map existing PHI nodes to their dbg.values.
  ValueToValueMapTy DbgValueMap;
  for (auto &I : *BB) {
    if (auto DbgII = dyn_cast<DbgVariableIntrinsic>(&I)) {
      if (auto *Loc = dyn_cast_or_null<PHINode>(DbgII->getVariableLocation()))
        DbgValueMap.insert({Loc, DbgII});
    }
  }
  if (DbgValueMap.size() == 0)
    return;

  // Then iterate through the new PHIs and look to see if they use one of the
  // previously mapped PHIs. If so, insert a new dbg.value intrinsic that will
  // propagate the info through the new PHI.
  LLVMContext &C = BB->getContext();
  for (auto PHI : InsertedPHIs) {
    BasicBlock *Parent = PHI->getParent();
    // Avoid inserting an intrinsic into an EH block.
    if (Parent->getFirstNonPHI()->isEHPad())
      continue;
    auto PhiMAV = MetadataAsValue::get(C, ValueAsMetadata::get(PHI));
    for (auto VI : PHI->operand_values()) {
      auto V = DbgValueMap.find(VI);
      if (V != DbgValueMap.end()) {
        auto *DbgII = cast<DbgVariableIntrinsic>(V->second);
        Instruction *NewDbgII = DbgII->clone();
        NewDbgII->setOperand(0, PhiMAV);
        auto InsertionPt = Parent->getFirstInsertionPt();
        assert(InsertionPt != Parent->end() && "Ill-formed basic block");
        NewDbgII->insertBefore(&*InsertionPt);
      }
    }
  }
}

/// Finds all intrinsics declaring local variables as living in the memory that
/// 'V' points to. This may include a mix of dbg.declare and
/// dbg.addr intrinsics.
TinyPtrVector<DbgVariableIntrinsic *> llvm::FindDbgAddrUses(Value *V) {
  // This function is hot. Check whether the value has any metadata to avoid a
  // DenseMap lookup.
  if (!V->isUsedByMetadata())
    return {};
  auto *L = LocalAsMetadata::getIfExists(V);
  if (!L)
    return {};
  auto *MDV = MetadataAsValue::getIfExists(V->getContext(), L);
  if (!MDV)
    return {};

  TinyPtrVector<DbgVariableIntrinsic *> Declares;
  for (User *U : MDV->users()) {
    if (auto *DII = dyn_cast<DbgVariableIntrinsic>(U))
      if (DII->isAddressOfVariable())
        Declares.push_back(DII);
  }

  return Declares;
}

void llvm::findDbgValues(SmallVectorImpl<DbgValueInst *> &DbgValues, Value *V) {
  // This function is hot. Check whether the value has any metadata to avoid a
  // DenseMap lookup.
  if (!V->isUsedByMetadata())
    return;
  if (auto *L = LocalAsMetadata::getIfExists(V))
    if (auto *MDV = MetadataAsValue::getIfExists(V->getContext(), L))
      for (User *U : MDV->users())
        if (DbgValueInst *DVI = dyn_cast<DbgValueInst>(U))
          DbgValues.push_back(DVI);
}

void llvm::findDbgUsers(SmallVectorImpl<DbgVariableIntrinsic *> &DbgUsers,
                        Value *V) {
  // This function is hot. Check whether the value has any metadata to avoid a
  // DenseMap lookup.
  if (!V->isUsedByMetadata())
    return;
  if (auto *L = LocalAsMetadata::getIfExists(V))
    if (auto *MDV = MetadataAsValue::getIfExists(V->getContext(), L))
      for (User *U : MDV->users())
        if (DbgVariableIntrinsic *DII = dyn_cast<DbgVariableIntrinsic>(U))
          DbgUsers.push_back(DII);
}

bool llvm::replaceDbgDeclare(Value *Address, Value *NewAddress,
                             Instruction *InsertBefore, DIBuilder &Builder,
                             bool DerefBefore, int Offset, bool DerefAfter) {
  auto DbgAddrs = FindDbgAddrUses(Address);
  for (DbgVariableIntrinsic *DII : DbgAddrs) {
    DebugLoc Loc = DII->getDebugLoc();
    auto *DIVar = DII->getVariable();
    auto *DIExpr = DII->getExpression();
    assert(DIVar && "Missing variable");
    DIExpr = DIExpression::prepend(DIExpr, DerefBefore, Offset, DerefAfter);
    // Insert llvm.dbg.declare immediately before InsertBefore, and remove old
    // llvm.dbg.declare.
    Builder.insertDeclare(NewAddress, DIVar, DIExpr, Loc, InsertBefore);
    if (DII == InsertBefore)
      InsertBefore = InsertBefore->getNextNode();
    DII->eraseFromParent();
  }
  return !DbgAddrs.empty();
}

bool llvm::replaceDbgDeclareForAlloca(AllocaInst *AI, Value *NewAllocaAddress,
                                      DIBuilder &Builder, bool DerefBefore,
                                      int Offset, bool DerefAfter) {
  return replaceDbgDeclare(AI, NewAllocaAddress, AI->getNextNode(), Builder,
                           DerefBefore, Offset, DerefAfter);
}

static void replaceOneDbgValueForAlloca(DbgValueInst *DVI, Value *NewAddress,
                                        DIBuilder &Builder, int Offset) {
  DebugLoc Loc = DVI->getDebugLoc();
  auto *DIVar = DVI->getVariable();
  auto *DIExpr = DVI->getExpression();
  assert(DIVar && "Missing variable");

  // This is an alloca-based llvm.dbg.value. The first thing it should do with
  // the alloca pointer is dereference it. Otherwise we don't know how to handle
  // it and give up.
  if (!DIExpr || DIExpr->getNumElements() < 1 ||
      DIExpr->getElement(0) != dwarf::DW_OP_deref)
    return;

  // Insert the offset immediately after the first deref.
  // We could just change the offset argument of dbg.value, but it's unsigned...
  if (Offset) {
    SmallVector<uint64_t, 4> Ops;
    Ops.push_back(dwarf::DW_OP_deref);
    DIExpression::appendOffset(Ops, Offset);
    Ops.append(DIExpr->elements_begin() + 1, DIExpr->elements_end());
    DIExpr = Builder.createExpression(Ops);
  }

  Builder.insertDbgValueIntrinsic(NewAddress, DIVar, DIExpr, Loc, DVI);
  DVI->eraseFromParent();
}

void llvm::replaceDbgValueForAlloca(AllocaInst *AI, Value *NewAllocaAddress,
                                    DIBuilder &Builder, int Offset) {
  if (auto *L = LocalAsMetadata::getIfExists(AI))
    if (auto *MDV = MetadataAsValue::getIfExists(AI->getContext(), L))
      for (auto UI = MDV->use_begin(), UE = MDV->use_end(); UI != UE;) {
        Use &U = *UI++;
        if (auto *DVI = dyn_cast<DbgValueInst>(U.getUser()))
          replaceOneDbgValueForAlloca(DVI, NewAllocaAddress, Builder, Offset);
      }
}

/// Wrap \p V in a ValueAsMetadata instance.
static MetadataAsValue *wrapValueInMetadata(LLVMContext &C, Value *V) {
  return MetadataAsValue::get(C, ValueAsMetadata::get(V));
}

bool llvm::salvageDebugInfo(Instruction &I) {
  SmallVector<DbgVariableIntrinsic *, 1> DbgUsers;
  findDbgUsers(DbgUsers, &I);
  if (DbgUsers.empty())
    return false;

  auto &M = *I.getModule();
  auto &DL = M.getDataLayout();
  auto &Ctx = I.getContext();
  auto wrapMD = [&](Value *V) { return wrapValueInMetadata(Ctx, V); };

  auto doSalvage = [&](DbgVariableIntrinsic *DII, SmallVectorImpl<uint64_t> &Ops) {
    auto *DIExpr = DII->getExpression();
    if (!Ops.empty()) {
      // Do not add DW_OP_stack_value for DbgDeclare and DbgAddr, because they
      // are implicitly pointing out the value as a DWARF memory location
      // description.
      bool WithStackValue = isa<DbgValueInst>(DII);
      DIExpr = DIExpression::prependOpcodes(DIExpr, Ops, WithStackValue);
    }
    DII->setOperand(0, wrapMD(I.getOperand(0)));
    DII->setOperand(2, MetadataAsValue::get(Ctx, DIExpr));
    LLVM_DEBUG(dbgs() << "SALVAGE: " << *DII << '\n');
  };

  auto applyOffset = [&](DbgVariableIntrinsic *DII, uint64_t Offset) {
    SmallVector<uint64_t, 8> Ops;
    DIExpression::appendOffset(Ops, Offset);
    doSalvage(DII, Ops);
  };

  auto applyOps = [&](DbgVariableIntrinsic *DII,
                      std::initializer_list<uint64_t> Opcodes) {
    SmallVector<uint64_t, 8> Ops(Opcodes);
    doSalvage(DII, Ops);
  };

  if (auto *CI = dyn_cast<CastInst>(&I)) {
    if (!CI->isNoopCast(DL))
      return false;

    // No-op casts are irrelevant for debug info.
    MetadataAsValue *CastSrc = wrapMD(I.getOperand(0));
    for (auto *DII : DbgUsers) {
      DII->setOperand(0, CastSrc);
      LLVM_DEBUG(dbgs() << "SALVAGE: " << *DII << '\n');
    }
    return true;
  } else if (auto *GEP = dyn_cast<GetElementPtrInst>(&I)) {
    unsigned BitWidth =
        M.getDataLayout().getIndexSizeInBits(GEP->getPointerAddressSpace());
    // Rewrite a constant GEP into a DIExpression.  Since we are performing
    // arithmetic to compute the variable's *value* in the DIExpression, we
    // need to mark the expression with a DW_OP_stack_value.
    APInt Offset(BitWidth, 0);
    if (GEP->accumulateConstantOffset(M.getDataLayout(), Offset))
      for (auto *DII : DbgUsers)
        applyOffset(DII, Offset.getSExtValue());
    return true;
  } else if (auto *BI = dyn_cast<BinaryOperator>(&I)) {
    // Rewrite binary operations with constant integer operands.
    auto *ConstInt = dyn_cast<ConstantInt>(I.getOperand(1));
    if (!ConstInt || ConstInt->getBitWidth() > 64)
      return false;

    uint64_t Val = ConstInt->getSExtValue();
    for (auto *DII : DbgUsers) {
      switch (BI->getOpcode()) {
      case Instruction::Add:
        applyOffset(DII, Val);
        break;
      case Instruction::Sub:
        applyOffset(DII, -int64_t(Val));
        break;
      case Instruction::Mul:
        applyOps(DII, {dwarf::DW_OP_constu, Val, dwarf::DW_OP_mul});
        break;
      case Instruction::SDiv:
        applyOps(DII, {dwarf::DW_OP_constu, Val, dwarf::DW_OP_div});
        break;
      case Instruction::SRem:
        applyOps(DII, {dwarf::DW_OP_constu, Val, dwarf::DW_OP_mod});
        break;
      case Instruction::Or:
        applyOps(DII, {dwarf::DW_OP_constu, Val, dwarf::DW_OP_or});
        break;
      case Instruction::And:
        applyOps(DII, {dwarf::DW_OP_constu, Val, dwarf::DW_OP_and});
        break;
      case Instruction::Xor:
        applyOps(DII, {dwarf::DW_OP_constu, Val, dwarf::DW_OP_xor});
        break;
      case Instruction::Shl:
        applyOps(DII, {dwarf::DW_OP_constu, Val, dwarf::DW_OP_shl});
        break;
      case Instruction::LShr:
        applyOps(DII, {dwarf::DW_OP_constu, Val, dwarf::DW_OP_shr});
        break;
      case Instruction::AShr:
        applyOps(DII, {dwarf::DW_OP_constu, Val, dwarf::DW_OP_shra});
        break;
      default:
        // TODO: Salvage constants from each kind of binop we know about.
        return false;
      }
    }
    return true;
  } else if (isa<LoadInst>(&I)) {
    MetadataAsValue *AddrMD = wrapMD(I.getOperand(0));
    for (auto *DII : DbgUsers) {
      // Rewrite the load into DW_OP_deref.
      auto *DIExpr = DII->getExpression();
      DIExpr = DIExpression::prepend(DIExpr, DIExpression::WithDeref);
      DII->setOperand(0, AddrMD);
      DII->setOperand(2, MetadataAsValue::get(Ctx, DIExpr));
      LLVM_DEBUG(dbgs() << "SALVAGE:  " << *DII << '\n');
    }
    return true;
  }
  return false;
}

/// A replacement for a dbg.value expression.
using DbgValReplacement = Optional<DIExpression *>;

/// Point debug users of \p From to \p To using exprs given by \p RewriteExpr,
/// possibly moving/deleting users to prevent use-before-def. Returns true if
/// changes are made.
static bool rewriteDebugUsers(
    Instruction &From, Value &To, Instruction &DomPoint, DominatorTree &DT,
    function_ref<DbgValReplacement(DbgVariableIntrinsic &DII)> RewriteExpr) {
  // Find debug users of From.
  SmallVector<DbgVariableIntrinsic *, 1> Users;
  findDbgUsers(Users, &From);
  if (Users.empty())
    return false;

  // Prevent use-before-def of To.
  bool Changed = false;
  SmallPtrSet<DbgVariableIntrinsic *, 1> DeleteOrSalvage;
  if (isa<Instruction>(&To)) {
    bool DomPointAfterFrom = From.getNextNonDebugInstruction() == &DomPoint;

    for (auto *DII : Users) {
      // It's common to see a debug user between From and DomPoint. Move it
      // after DomPoint to preserve the variable update without any reordering.
      if (DomPointAfterFrom && DII->getNextNonDebugInstruction() == &DomPoint) {
        LLVM_DEBUG(dbgs() << "MOVE:  " << *DII << '\n');
        DII->moveAfter(&DomPoint);
        Changed = true;

      // Users which otherwise aren't dominated by the replacement value must
      // be salvaged or deleted.
      } else if (!DT.dominates(&DomPoint, DII)) {
        DeleteOrSalvage.insert(DII);
      }
    }
  }

  // Update debug users without use-before-def risk.
  for (auto *DII : Users) {
    if (DeleteOrSalvage.count(DII))
      continue;

    LLVMContext &Ctx = DII->getContext();
    DbgValReplacement DVR = RewriteExpr(*DII);
    if (!DVR)
      continue;

    DII->setOperand(0, wrapValueInMetadata(Ctx, &To));
    DII->setOperand(2, MetadataAsValue::get(Ctx, *DVR));
    LLVM_DEBUG(dbgs() << "REWRITE:  " << *DII << '\n');
    Changed = true;
  }

  if (!DeleteOrSalvage.empty()) {
    // Try to salvage the remaining debug users.
    Changed |= salvageDebugInfo(From);

    // Delete the debug users which weren't salvaged.
    for (auto *DII : DeleteOrSalvage) {
      if (DII->getVariableLocation() == &From) {
        LLVM_DEBUG(dbgs() << "Erased UseBeforeDef:  " << *DII << '\n');
        DII->eraseFromParent();
        Changed = true;
      }
    }
  }

  return Changed;
}

/// Check if a bitcast between a value of type \p FromTy to type \p ToTy would
/// losslessly preserve the bits and semantics of the value. This predicate is
/// symmetric, i.e swapping \p FromTy and \p ToTy should give the same result.
///
/// Note that Type::canLosslesslyBitCastTo is not suitable here because it
/// allows semantically unequivalent bitcasts, such as <2 x i64> -> <4 x i32>,
/// and also does not allow lossless pointer <-> integer conversions.
static bool isBitCastSemanticsPreserving(const DataLayout &DL, Type *FromTy,
                                         Type *ToTy) {
  // Trivially compatible types.
  if (FromTy == ToTy)
    return true;

  // Handle compatible pointer <-> integer conversions.
  if (FromTy->isIntOrPtrTy() && ToTy->isIntOrPtrTy()) {
    bool SameSize = DL.getTypeSizeInBits(FromTy) == DL.getTypeSizeInBits(ToTy);
    bool LosslessConversion = !DL.isNonIntegralPointerType(FromTy) &&
                              !DL.isNonIntegralPointerType(ToTy);
    return SameSize && LosslessConversion;
  }

  // TODO: This is not exhaustive.
  return false;
}

bool llvm::replaceAllDbgUsesWith(Instruction &From, Value &To,
                                 Instruction &DomPoint, DominatorTree &DT) {
  // Exit early if From has no debug users.
  if (!From.isUsedByMetadata())
    return false;

  assert(&From != &To && "Can't replace something with itself");

  Type *FromTy = From.getType();
  Type *ToTy = To.getType();

  auto Identity = [&](DbgVariableIntrinsic &DII) -> DbgValReplacement {
    return DII.getExpression();
  };

  // Handle no-op conversions.
  Module &M = *From.getModule();
  const DataLayout &DL = M.getDataLayout();
  if (isBitCastSemanticsPreserving(DL, FromTy, ToTy))
    return rewriteDebugUsers(From, To, DomPoint, DT, Identity);

  // Handle integer-to-integer widening and narrowing.
  // FIXME: Use DW_OP_convert when it's available everywhere.
  if (FromTy->isIntegerTy() && ToTy->isIntegerTy()) {
    uint64_t FromBits = FromTy->getPrimitiveSizeInBits();
    uint64_t ToBits = ToTy->getPrimitiveSizeInBits();
    assert(FromBits != ToBits && "Unexpected no-op conversion");

    // When the width of the result grows, assume that a debugger will only
    // access the low `FromBits` bits when inspecting the source variable.
    if (FromBits < ToBits)
      return rewriteDebugUsers(From, To, DomPoint, DT, Identity);

    // The width of the result has shrunk. Use sign/zero extension to describe
    // the source variable's high bits.
    auto SignOrZeroExt = [&](DbgVariableIntrinsic &DII) -> DbgValReplacement {
      DILocalVariable *Var = DII.getVariable();

      // Without knowing signedness, sign/zero extension isn't possible.
      auto Signedness = Var->getSignedness();
      if (!Signedness)
        return None;

      bool Signed = *Signedness == DIBasicType::Signedness::Signed;

      if (!Signed) {
        // In the unsigned case, assume that a debugger will initialize the
        // high bits to 0 and do a no-op conversion.
        return Identity(DII);
      } else {
        // In the signed case, the high bits are given by sign extension, i.e:
        //   (To >> (ToBits - 1)) * ((2 ^ FromBits) - 1)
        // Calculate the high bits and OR them together with the low bits.
        SmallVector<uint64_t, 8> Ops({dwarf::DW_OP_dup, dwarf::DW_OP_constu,
                                      (ToBits - 1), dwarf::DW_OP_shr,
                                      dwarf::DW_OP_lit0, dwarf::DW_OP_not,
                                      dwarf::DW_OP_mul, dwarf::DW_OP_or});
        return DIExpression::appendToStack(DII.getExpression(), Ops);
      }
    };
    return rewriteDebugUsers(From, To, DomPoint, DT, SignOrZeroExt);
  }

  // TODO: Floating-point conversions, vectors.
  return false;
}

unsigned llvm::removeAllNonTerminatorAndEHPadInstructions(BasicBlock *BB) {
  unsigned NumDeadInst = 0;
  // Delete the instructions backwards, as it has a reduced likelihood of
  // having to update as many def-use and use-def chains.
  Instruction *EndInst = BB->getTerminator(); // Last not to be deleted.
  while (EndInst != &BB->front()) {
    // Delete the next to last instruction.
    Instruction *Inst = &*--EndInst->getIterator();
    if (!Inst->use_empty() && !Inst->getType()->isTokenTy())
      Inst->replaceAllUsesWith(UndefValue::get(Inst->getType()));
    if (Inst->isEHPad() || Inst->getType()->isTokenTy()) {
      EndInst = Inst;
      continue;
    }
    if (!isa<DbgInfoIntrinsic>(Inst))
      ++NumDeadInst;
    Inst->eraseFromParent();
  }
  return NumDeadInst;
}

unsigned llvm::changeToUnreachable(Instruction *I, bool UseLLVMTrap,
                                   bool PreserveLCSSA, DomTreeUpdater *DTU) {
  BasicBlock *BB = I->getParent();
  std::vector <DominatorTree::UpdateType> Updates;

  // Loop over all of the successors, removing BB's entry from any PHI
  // nodes.
  if (DTU)
    Updates.reserve(BB->getTerminator()->getNumSuccessors());
  for (BasicBlock *Successor : successors(BB)) {
    Successor->removePredecessor(BB, PreserveLCSSA);
    if (DTU)
      Updates.push_back({DominatorTree::Delete, BB, Successor});
  }
  // Insert a call to llvm.trap right before this.  This turns the undefined
  // behavior into a hard fail instead of falling through into random code.
  if (UseLLVMTrap) {
    Function *TrapFn =
      Intrinsic::getDeclaration(BB->getParent()->getParent(), Intrinsic::trap);
    CallInst *CallTrap = CallInst::Create(TrapFn, "", I);
    CallTrap->setDebugLoc(I->getDebugLoc());
  }
  auto *UI = new UnreachableInst(I->getContext(), I);
  UI->setDebugLoc(I->getDebugLoc());

  // All instructions after this are dead.
  unsigned NumInstrsRemoved = 0;
  BasicBlock::iterator BBI = I->getIterator(), BBE = BB->end();
  while (BBI != BBE) {
    if (!BBI->use_empty())
      BBI->replaceAllUsesWith(UndefValue::get(BBI->getType()));
    BB->getInstList().erase(BBI++);
    ++NumInstrsRemoved;
  }
  if (DTU)
    DTU->applyUpdates(Updates, /*ForceRemoveDuplicates*/ true);
  return NumInstrsRemoved;
}

/// changeToCall - Convert the specified invoke into a normal call.
static void changeToCall(InvokeInst *II, DomTreeUpdater *DTU = nullptr) {
  SmallVector<Value*, 8> Args(II->arg_begin(), II->arg_end());
  SmallVector<OperandBundleDef, 1> OpBundles;
  II->getOperandBundlesAsDefs(OpBundles);
  CallInst *NewCall = CallInst::Create(II->getCalledValue(), Args, OpBundles,
                                       "", II);
  NewCall->takeName(II);
  NewCall->setCallingConv(II->getCallingConv());
  NewCall->setAttributes(II->getAttributes());
  NewCall->setDebugLoc(II->getDebugLoc());
  NewCall->copyMetadata(*II);
  II->replaceAllUsesWith(NewCall);

  // Follow the call by a branch to the normal destination.
  BasicBlock *NormalDestBB = II->getNormalDest();
  BranchInst::Create(NormalDestBB, II);

  // Update PHI nodes in the unwind destination
  BasicBlock *BB = II->getParent();
  BasicBlock *UnwindDestBB = II->getUnwindDest();
  UnwindDestBB->removePredecessor(BB);
  II->eraseFromParent();
  if (DTU)
    DTU->deleteEdgeRelaxed(BB, UnwindDestBB);
}

BasicBlock *llvm::changeToInvokeAndSplitBasicBlock(CallInst *CI,
                                                   BasicBlock *UnwindEdge) {
  BasicBlock *BB = CI->getParent();

  // Convert this function call into an invoke instruction.  First, split the
  // basic block.
  BasicBlock *Split =
      BB->splitBasicBlock(CI->getIterator(), CI->getName() + ".noexc");

  // Delete the unconditional branch inserted by splitBasicBlock
  BB->getInstList().pop_back();

  // Create the new invoke instruction.
  SmallVector<Value *, 8> InvokeArgs(CI->arg_begin(), CI->arg_end());
  SmallVector<OperandBundleDef, 1> OpBundles;

  CI->getOperandBundlesAsDefs(OpBundles);

  // Note: we're round tripping operand bundles through memory here, and that
  // can potentially be avoided with a cleverer API design that we do not have
  // as of this time.

  InvokeInst *II = InvokeInst::Create(CI->getCalledValue(), Split, UnwindEdge,
                                      InvokeArgs, OpBundles, CI->getName(), BB);
  II->setDebugLoc(CI->getDebugLoc());
  II->setCallingConv(CI->getCallingConv());
  II->setAttributes(CI->getAttributes());

  // Make sure that anything using the call now uses the invoke!  This also
  // updates the CallGraph if present, because it uses a WeakTrackingVH.
  CI->replaceAllUsesWith(II);

  // Delete the original call
  Split->getInstList().pop_front();
  return Split;
}

static bool markAliveBlocks(Function &F,
                            SmallPtrSetImpl<BasicBlock *> &Reachable,
                            DomTreeUpdater *DTU = nullptr) {
  SmallVector<BasicBlock*, 128> Worklist;
  BasicBlock *BB = &F.front();
  Worklist.push_back(BB);
  Reachable.insert(BB);
  bool Changed = false;
  do {
    BB = Worklist.pop_back_val();

    // Do a quick scan of the basic block, turning any obviously unreachable
    // instructions into LLVM unreachable insts.  The instruction combining pass
    // canonicalizes unreachable insts into stores to null or undef.
    for (Instruction &I : *BB) {
      if (auto *CI = dyn_cast<CallInst>(&I)) {
        Value *Callee = CI->getCalledValue();
        // Handle intrinsic calls.
        if (Function *F = dyn_cast<Function>(Callee)) {
          auto IntrinsicID = F->getIntrinsicID();
          // Assumptions that are known to be false are equivalent to
          // unreachable. Also, if the condition is undefined, then we make the
          // choice most beneficial to the optimizer, and choose that to also be
          // unreachable.
          if (IntrinsicID == Intrinsic::assume) {
            if (match(CI->getArgOperand(0), m_CombineOr(m_Zero(), m_Undef()))) {
              // Don't insert a call to llvm.trap right before the unreachable.
              changeToUnreachable(CI, false, false, DTU);
              Changed = true;
              break;
            }
          } else if (IntrinsicID == Intrinsic::experimental_guard) {
            // A call to the guard intrinsic bails out of the current
            // compilation unit if the predicate passed to it is false. If the
            // predicate is a constant false, then we know the guard will bail
            // out of the current compile unconditionally, so all code following
            // it is dead.
            //
            // Note: unlike in llvm.assume, it is not "obviously profitable" for
            // guards to treat `undef` as `false` since a guard on `undef` can
            // still be useful for widening.
            if (match(CI->getArgOperand(0), m_Zero()))
              if (!isa<UnreachableInst>(CI->getNextNode())) {
                changeToUnreachable(CI->getNextNode(), /*UseLLVMTrap=*/false,
                                    false, DTU);
                Changed = true;
                break;
              }
          }
        } else if ((isa<ConstantPointerNull>(Callee) &&
                    !NullPointerIsDefined(CI->getFunction())) ||
                   isa<UndefValue>(Callee)) {
          changeToUnreachable(CI, /*UseLLVMTrap=*/false, false, DTU);
          Changed = true;
          break;
        }
        if (CI->doesNotReturn()) {
          // If we found a call to a no-return function, insert an unreachable
          // instruction after it.  Make sure there isn't *already* one there
          // though.
          if (!isa<UnreachableInst>(CI->getNextNode())) {
            // Don't insert a call to llvm.trap right before the unreachable.
            changeToUnreachable(CI->getNextNode(), false, false, DTU);
            Changed = true;
          }
          break;
        }
      } else if (auto *SI = dyn_cast<StoreInst>(&I)) {
        // Store to undef and store to null are undefined and used to signal
        // that they should be changed to unreachable by passes that can't
        // modify the CFG.

        // Don't touch volatile stores.
        if (SI->isVolatile()) continue;

        Value *Ptr = SI->getOperand(1);

        if (isa<UndefValue>(Ptr) ||
            (isa<ConstantPointerNull>(Ptr) &&
             !NullPointerIsDefined(SI->getFunction(),
                                   SI->getPointerAddressSpace()))) {
          changeToUnreachable(SI, true, false, DTU);
          Changed = true;
          break;
        }
      }
    }

    Instruction *Terminator = BB->getTerminator();
    if (auto *II = dyn_cast<InvokeInst>(Terminator)) {
      // Turn invokes that call 'nounwind' functions into ordinary calls.
      Value *Callee = II->getCalledValue();
      if ((isa<ConstantPointerNull>(Callee) &&
           !NullPointerIsDefined(BB->getParent())) ||
          isa<UndefValue>(Callee)) {
        changeToUnreachable(II, true, false, DTU);
        Changed = true;
      } else if (II->doesNotThrow() && canSimplifyInvokeNoUnwind(&F)) {
        if (II->use_empty() && II->onlyReadsMemory()) {
          // jump to the normal destination branch.
          BasicBlock *NormalDestBB = II->getNormalDest();
          BasicBlock *UnwindDestBB = II->getUnwindDest();
          BranchInst::Create(NormalDestBB, II);
          UnwindDestBB->removePredecessor(II->getParent());
          II->eraseFromParent();
          if (DTU)
            DTU->deleteEdgeRelaxed(BB, UnwindDestBB);
        } else
          changeToCall(II, DTU);
        Changed = true;
      }
    } else if (auto *CatchSwitch = dyn_cast<CatchSwitchInst>(Terminator)) {
      // Remove catchpads which cannot be reached.
      struct CatchPadDenseMapInfo {
        static CatchPadInst *getEmptyKey() {
          return DenseMapInfo<CatchPadInst *>::getEmptyKey();
        }

        static CatchPadInst *getTombstoneKey() {
          return DenseMapInfo<CatchPadInst *>::getTombstoneKey();
        }

        static unsigned getHashValue(CatchPadInst *CatchPad) {
          return static_cast<unsigned>(hash_combine_range(
              CatchPad->value_op_begin(), CatchPad->value_op_end()));
        }

        static bool isEqual(CatchPadInst *LHS, CatchPadInst *RHS) {
          if (LHS == getEmptyKey() || LHS == getTombstoneKey() ||
              RHS == getEmptyKey() || RHS == getTombstoneKey())
            return LHS == RHS;
          return LHS->isIdenticalTo(RHS);
        }
      };

      // Set of unique CatchPads.
      SmallDenseMap<CatchPadInst *, detail::DenseSetEmpty, 4,
                    CatchPadDenseMapInfo, detail::DenseSetPair<CatchPadInst *>>
          HandlerSet;
      detail::DenseSetEmpty Empty;
      for (CatchSwitchInst::handler_iterator I = CatchSwitch->handler_begin(),
                                             E = CatchSwitch->handler_end();
           I != E; ++I) {
        BasicBlock *HandlerBB = *I;
        auto *CatchPad = cast<CatchPadInst>(HandlerBB->getFirstNonPHI());
        if (!HandlerSet.insert({CatchPad, Empty}).second) {
          CatchSwitch->removeHandler(I);
          --I;
          --E;
          Changed = true;
        }
      }
    }

    Changed |= ConstantFoldTerminator(BB, true, nullptr, DTU);
    for (BasicBlock *Successor : successors(BB))
      if (Reachable.insert(Successor).second)
        Worklist.push_back(Successor);
  } while (!Worklist.empty());
  return Changed;
}

void llvm::removeUnwindEdge(BasicBlock *BB, DomTreeUpdater *DTU) {
  Instruction *TI = BB->getTerminator();

  if (auto *II = dyn_cast<InvokeInst>(TI)) {
    changeToCall(II, DTU);
    return;
  }

  Instruction *NewTI;
  BasicBlock *UnwindDest;

  if (auto *CRI = dyn_cast<CleanupReturnInst>(TI)) {
    NewTI = CleanupReturnInst::Create(CRI->getCleanupPad(), nullptr, CRI);
    UnwindDest = CRI->getUnwindDest();
  } else if (auto *CatchSwitch = dyn_cast<CatchSwitchInst>(TI)) {
    auto *NewCatchSwitch = CatchSwitchInst::Create(
        CatchSwitch->getParentPad(), nullptr, CatchSwitch->getNumHandlers(),
        CatchSwitch->getName(), CatchSwitch);
    for (BasicBlock *PadBB : CatchSwitch->handlers())
      NewCatchSwitch->addHandler(PadBB);

    NewTI = NewCatchSwitch;
    UnwindDest = CatchSwitch->getUnwindDest();
  } else {
    llvm_unreachable("Could not find unwind successor");
  }

  NewTI->takeName(TI);
  NewTI->setDebugLoc(TI->getDebugLoc());
  UnwindDest->removePredecessor(BB);
  TI->replaceAllUsesWith(NewTI);
  TI->eraseFromParent();
  if (DTU)
    DTU->deleteEdgeRelaxed(BB, UnwindDest);
}

/// removeUnreachableBlocks - Remove blocks that are not reachable, even
/// if they are in a dead cycle.  Return true if a change was made, false
/// otherwise. If `LVI` is passed, this function preserves LazyValueInfo
/// after modifying the CFG.
bool llvm::removeUnreachableBlocks(Function &F, LazyValueInfo *LVI,
                                   DomTreeUpdater *DTU,
                                   MemorySSAUpdater *MSSAU) {
  SmallPtrSet<BasicBlock*, 16> Reachable;
  bool Changed = markAliveBlocks(F, Reachable, DTU);

  // If there are unreachable blocks in the CFG...
  if (Reachable.size() == F.size())
    return Changed;

  assert(Reachable.size() < F.size());
  NumRemoved += F.size()-Reachable.size();

  SmallPtrSet<BasicBlock *, 16> DeadBlockSet;
  for (Function::iterator I = ++F.begin(), E = F.end(); I != E; ++I) {
    auto *BB = &*I;
    if (Reachable.count(BB))
      continue;
    DeadBlockSet.insert(BB);
  }

  if (MSSAU)
    MSSAU->removeBlocks(DeadBlockSet);

  // Loop over all of the basic blocks that are not reachable, dropping all of
  // their internal references. Update DTU and LVI if available.
  std::vector<DominatorTree::UpdateType> Updates;
  for (auto *BB : DeadBlockSet) {
    for (BasicBlock *Successor : successors(BB)) {
      if (!DeadBlockSet.count(Successor))
        Successor->removePredecessor(BB);
      if (DTU)
        Updates.push_back({DominatorTree::Delete, BB, Successor});
    }
    if (LVI)
      LVI->eraseBlock(BB);
    BB->dropAllReferences();
  }
  for (Function::iterator I = ++F.begin(); I != F.end();) {
    auto *BB = &*I;
    if (Reachable.count(BB)) {
      ++I;
      continue;
    }
    if (DTU) {
      // Remove the terminator of BB to clear the successor list of BB.
      if (BB->getTerminator())
        BB->getInstList().pop_back();
      new UnreachableInst(BB->getContext(), BB);
      assert(succ_empty(BB) && "The successor list of BB isn't empty before "
                               "applying corresponding DTU updates.");
      ++I;
    } else {
      I = F.getBasicBlockList().erase(I);
    }
  }

  if (DTU) {
    DTU->applyUpdates(Updates, /*ForceRemoveDuplicates*/ true);
    bool Deleted = false;
    for (auto *BB : DeadBlockSet) {
      if (DTU->isBBPendingDeletion(BB))
        --NumRemoved;
      else
        Deleted = true;
      DTU->deleteBB(BB);
    }
    if (!Deleted)
      return false;
  }
  return true;
}

void llvm::combineMetadata(Instruction *K, const Instruction *J,
                           ArrayRef<unsigned> KnownIDs, bool DoesKMove) {
  SmallVector<std::pair<unsigned, MDNode *>, 4> Metadata;
  K->dropUnknownNonDebugMetadata(KnownIDs);
  K->getAllMetadataOtherThanDebugLoc(Metadata);
  for (const auto &MD : Metadata) {
    unsigned Kind = MD.first;
    MDNode *JMD = J->getMetadata(Kind);
    MDNode *KMD = MD.second;

    switch (Kind) {
      default:
        K->setMetadata(Kind, nullptr); // Remove unknown metadata
        break;
      case LLVMContext::MD_dbg:
        llvm_unreachable("getAllMetadataOtherThanDebugLoc returned a MD_dbg");
      case LLVMContext::MD_tbaa:
        K->setMetadata(Kind, MDNode::getMostGenericTBAA(JMD, KMD));
        break;
      case LLVMContext::MD_alias_scope:
        K->setMetadata(Kind, MDNode::getMostGenericAliasScope(JMD, KMD));
        break;
      case LLVMContext::MD_noalias:
      case LLVMContext::MD_mem_parallel_loop_access:
        K->setMetadata(Kind, MDNode::intersect(JMD, KMD));
        break;
      case LLVMContext::MD_access_group:
        K->setMetadata(LLVMContext::MD_access_group,
                       intersectAccessGroups(K, J));
        break;
      case LLVMContext::MD_range:

        // If K does move, use most generic range. Otherwise keep the range of
        // K.
        if (DoesKMove)
          // FIXME: If K does move, we should drop the range info and nonnull.
          //        Currently this function is used with DoesKMove in passes
          //        doing hoisting/sinking and the current behavior of using the
          //        most generic range is correct in those cases.
          K->setMetadata(Kind, MDNode::getMostGenericRange(JMD, KMD));
        break;
      case LLVMContext::MD_fpmath:
        K->setMetadata(Kind, MDNode::getMostGenericFPMath(JMD, KMD));
        break;
      case LLVMContext::MD_invariant_load:
        // Only set the !invariant.load if it is present in both instructions.
        K->setMetadata(Kind, JMD);
        break;
      case LLVMContext::MD_nonnull:
        // If K does move, keep nonull if it is present in both instructions.
        if (DoesKMove)
          K->setMetadata(Kind, JMD);
        break;
      case LLVMContext::MD_invariant_group:
        // Preserve !invariant.group in K.
        break;
      case LLVMContext::MD_align:
        K->setMetadata(Kind,
          MDNode::getMostGenericAlignmentOrDereferenceable(JMD, KMD));
        break;
      case LLVMContext::MD_dereferenceable:
      case LLVMContext::MD_dereferenceable_or_null:
        K->setMetadata(Kind,
          MDNode::getMostGenericAlignmentOrDereferenceable(JMD, KMD));
        break;
    }
  }
  // Set !invariant.group from J if J has it. If both instructions have it
  // then we will just pick it from J - even when they are different.
  // Also make sure that K is load or store - f.e. combining bitcast with load
  // could produce bitcast with invariant.group metadata, which is invalid.
  // FIXME: we should try to preserve both invariant.group md if they are
  // different, but right now instruction can only have one invariant.group.
  if (auto *JMD = J->getMetadata(LLVMContext::MD_invariant_group))
    if (isa<LoadInst>(K) || isa<StoreInst>(K))
      K->setMetadata(LLVMContext::MD_invariant_group, JMD);
}

void llvm::combineMetadataForCSE(Instruction *K, const Instruction *J,
                                 bool KDominatesJ) {
  unsigned KnownIDs[] = {
      LLVMContext::MD_tbaa,            LLVMContext::MD_alias_scope,
      LLVMContext::MD_noalias,         LLVMContext::MD_range,
      LLVMContext::MD_invariant_load,  LLVMContext::MD_nonnull,
      LLVMContext::MD_invariant_group, LLVMContext::MD_align,
      LLVMContext::MD_dereferenceable,
      LLVMContext::MD_dereferenceable_or_null,
      LLVMContext::MD_access_group};
  combineMetadata(K, J, KnownIDs, KDominatesJ);
}

void llvm::patchReplacementInstruction(Instruction *I, Value *Repl) {
  auto *ReplInst = dyn_cast<Instruction>(Repl);
  if (!ReplInst)
    return;

  // Patch the replacement so that it is not more restrictive than the value
  // being replaced.
  // Note that if 'I' is a load being replaced by some operation,
  // for example, by an arithmetic operation, then andIRFlags()
  // would just erase all math flags from the original arithmetic
  // operation, which is clearly not wanted and not needed.
  if (!isa<LoadInst>(I))
    ReplInst->andIRFlags(I);

  // FIXME: If both the original and replacement value are part of the
  // same control-flow region (meaning that the execution of one
  // guarantees the execution of the other), then we can combine the
  // noalias scopes here and do better than the general conservative
  // answer used in combineMetadata().

  // In general, GVN unifies expressions over different control-flow
  // regions, and so we need a conservative combination of the noalias
  // scopes.
  static const unsigned KnownIDs[] = {
      LLVMContext::MD_tbaa,            LLVMContext::MD_alias_scope,
      LLVMContext::MD_noalias,         LLVMContext::MD_range,
      LLVMContext::MD_fpmath,          LLVMContext::MD_invariant_load,
      LLVMContext::MD_invariant_group, LLVMContext::MD_nonnull,
      LLVMContext::MD_access_group};
  combineMetadata(ReplInst, I, KnownIDs, false);
}

template <typename RootType, typename DominatesFn>
static unsigned replaceDominatedUsesWith(Value *From, Value *To,
                                         const RootType &Root,
                                         const DominatesFn &Dominates) {
  assert(From->getType() == To->getType());

  unsigned Count = 0;
  for (Value::use_iterator UI = From->use_begin(), UE = From->use_end();
       UI != UE;) {
    Use &U = *UI++;
    if (!Dominates(Root, U))
      continue;
    U.set(To);
    LLVM_DEBUG(dbgs() << "Replace dominated use of '" << From->getName()
                      << "' as " << *To << " in " << *U << "\n");
    ++Count;
  }
  return Count;
}

unsigned llvm::replaceNonLocalUsesWith(Instruction *From, Value *To) {
   assert(From->getType() == To->getType());
   auto *BB = From->getParent();
   unsigned Count = 0;

  for (Value::use_iterator UI = From->use_begin(), UE = From->use_end();
       UI != UE;) {
    Use &U = *UI++;
    auto *I = cast<Instruction>(U.getUser());
    if (I->getParent() == BB)
      continue;
    U.set(To);
    ++Count;
  }
  return Count;
}

unsigned llvm::replaceDominatedUsesWith(Value *From, Value *To,
                                        DominatorTree &DT,
                                        const BasicBlockEdge &Root) {
  auto Dominates = [&DT](const BasicBlockEdge &Root, const Use &U) {
    return DT.dominates(Root, U);
  };
  return ::replaceDominatedUsesWith(From, To, Root, Dominates);
}

unsigned llvm::replaceDominatedUsesWith(Value *From, Value *To,
                                        DominatorTree &DT,
                                        const BasicBlock *BB) {
  auto ProperlyDominates = [&DT](const BasicBlock *BB, const Use &U) {
    auto *I = cast<Instruction>(U.getUser())->getParent();
    return DT.properlyDominates(BB, I);
  };
  return ::replaceDominatedUsesWith(From, To, BB, ProperlyDominates);
}

bool llvm::callsGCLeafFunction(ImmutableCallSite CS,
                               const TargetLibraryInfo &TLI) {
  // Check if the function is specifically marked as a gc leaf function.
  if (CS.hasFnAttr("gc-leaf-function"))
    return true;
  if (const Function *F = CS.getCalledFunction()) {
    if (F->hasFnAttribute("gc-leaf-function"))
      return true;

    if (auto IID = F->getIntrinsicID())
      // Most LLVM intrinsics do not take safepoints.
      return IID != Intrinsic::experimental_gc_statepoint &&
             IID != Intrinsic::experimental_deoptimize;
  }

  // Lib calls can be materialized by some passes, and won't be
  // marked as 'gc-leaf-function.' All available Libcalls are
  // GC-leaf.
  LibFunc LF;
  if (TLI.getLibFunc(CS, LF)) {
    return TLI.has(LF);
  }

  return false;
}

void llvm::copyNonnullMetadata(const LoadInst &OldLI, MDNode *N,
                               LoadInst &NewLI) {
  auto *NewTy = NewLI.getType();

  // This only directly applies if the new type is also a pointer.
  if (NewTy->isPointerTy()) {
    NewLI.setMetadata(LLVMContext::MD_nonnull, N);
    return;
  }

  // The only other translation we can do is to integral loads with !range
  // metadata.
  if (!NewTy->isIntegerTy())
    return;

  MDBuilder MDB(NewLI.getContext());
  const Value *Ptr = OldLI.getPointerOperand();
  auto *ITy = cast<IntegerType>(NewTy);
  auto *NullInt = ConstantExpr::getPtrToInt(
      ConstantPointerNull::get(cast<PointerType>(Ptr->getType())), ITy);
  auto *NonNullInt = ConstantExpr::getAdd(NullInt, ConstantInt::get(ITy, 1));
  NewLI.setMetadata(LLVMContext::MD_range,
                    MDB.createRange(NonNullInt, NullInt));
}

void llvm::copyRangeMetadata(const DataLayout &DL, const LoadInst &OldLI,
                             MDNode *N, LoadInst &NewLI) {
  auto *NewTy = NewLI.getType();

  // Give up unless it is converted to a pointer where there is a single very
  // valuable mapping we can do reliably.
  // FIXME: It would be nice to propagate this in more ways, but the type
  // conversions make it hard.
  if (!NewTy->isPointerTy())
    return;

  unsigned BitWidth = DL.getIndexTypeSizeInBits(NewTy);
  if (!getConstantRangeFromMetadata(*N).contains(APInt(BitWidth, 0))) {
    MDNode *NN = MDNode::get(OldLI.getContext(), None);
    NewLI.setMetadata(LLVMContext::MD_nonnull, NN);
  }
}

void llvm::dropDebugUsers(Instruction &I) {
  SmallVector<DbgVariableIntrinsic *, 1> DbgUsers;
  findDbgUsers(DbgUsers, &I);
  for (auto *DII : DbgUsers)
    DII->eraseFromParent();
}

void llvm::hoistAllInstructionsInto(BasicBlock *DomBlock, Instruction *InsertPt,
                                    BasicBlock *BB) {
  // Since we are moving the instructions out of its basic block, we do not
  // retain their original debug locations (DILocations) and debug intrinsic
  // instructions (dbg.values).
  //
  // Doing so would degrade the debugging experience and adversely affect the
  // accuracy of profiling information.
  //
  // Currently, when hoisting the instructions, we take the following actions:
  // - Remove their dbg.values.
  // - Set their debug locations to the values from the insertion point.
  //
  // As per PR39141 (comment #8), the more fundamental reason why the dbg.values
  // need to be deleted, is because there will not be any instructions with a
  // DILocation in either branch left after performing the transformation. We
  // can only insert a dbg.value after the two branches are joined again.
  //
  // See PR38762, PR39243 for more details.
  //
  // TODO: Extend llvm.dbg.value to take more than one SSA Value (PR39141) to
  // encode predicated DIExpressions that yield different results on different
  // code paths.
  for (BasicBlock::iterator II = BB->begin(), IE = BB->end(); II != IE;) {
    Instruction *I = &*II;
    I->dropUnknownNonDebugMetadata();
    if (I->isUsedByMetadata())
      dropDebugUsers(*I);
    if (isa<DbgVariableIntrinsic>(I)) {
      // Remove DbgInfo Intrinsics.
      II = I->eraseFromParent();
      continue;
    }
    I->setDebugLoc(InsertPt->getDebugLoc());
    ++II;
  }
  DomBlock->getInstList().splice(InsertPt->getIterator(), BB->getInstList(),
                                 BB->begin(),
                                 BB->getTerminator()->getIterator());
}

namespace {

/// A potential constituent of a bitreverse or bswap expression. See
/// collectBitParts for a fuller explanation.
struct BitPart {
  BitPart(Value *P, unsigned BW) : Provider(P) {
    Provenance.resize(BW);
  }

  /// The Value that this is a bitreverse/bswap of.
  Value *Provider;

  /// The "provenance" of each bit. Provenance[A] = B means that bit A
  /// in Provider becomes bit B in the result of this expression.
  SmallVector<int8_t, 32> Provenance; // int8_t means max size is i128.

  enum { Unset = -1 };
};

} // end anonymous namespace

/// Analyze the specified subexpression and see if it is capable of providing
/// pieces of a bswap or bitreverse. The subexpression provides a potential
/// piece of a bswap or bitreverse if it can be proven that each non-zero bit in
/// the output of the expression came from a corresponding bit in some other
/// value. This function is recursive, and the end result is a mapping of
/// bitnumber to bitnumber. It is the caller's responsibility to validate that
/// the bitnumber to bitnumber mapping is correct for a bswap or bitreverse.
///
/// For example, if the current subexpression if "(shl i32 %X, 24)" then we know
/// that the expression deposits the low byte of %X into the high byte of the
/// result and that all other bits are zero. This expression is accepted and a
/// BitPart is returned with Provider set to %X and Provenance[24-31] set to
/// [0-7].
///
/// To avoid revisiting values, the BitPart results are memoized into the
/// provided map. To avoid unnecessary copying of BitParts, BitParts are
/// constructed in-place in the \c BPS map. Because of this \c BPS needs to
/// store BitParts objects, not pointers. As we need the concept of a nullptr
/// BitParts (Value has been analyzed and the analysis failed), we an Optional
/// type instead to provide the same functionality.
///
/// Because we pass around references into \c BPS, we must use a container that
/// does not invalidate internal references (std::map instead of DenseMap).
static const Optional<BitPart> &
collectBitParts(Value *V, bool MatchBSwaps, bool MatchBitReversals,
                std::map<Value *, Optional<BitPart>> &BPS) {
  auto I = BPS.find(V);
  if (I != BPS.end())
    return I->second;

  auto &Result = BPS[V] = None;
  auto BitWidth = cast<IntegerType>(V->getType())->getBitWidth();

  if (Instruction *I = dyn_cast<Instruction>(V)) {
    // If this is an or instruction, it may be an inner node of the bswap.
    if (I->getOpcode() == Instruction::Or) {
      auto &A = collectBitParts(I->getOperand(0), MatchBSwaps,
                                MatchBitReversals, BPS);
      auto &B = collectBitParts(I->getOperand(1), MatchBSwaps,
                                MatchBitReversals, BPS);
      if (!A || !B)
        return Result;

      // Try and merge the two together.
      if (!A->Provider || A->Provider != B->Provider)
        return Result;

      Result = BitPart(A->Provider, BitWidth);
      for (unsigned i = 0; i < A->Provenance.size(); ++i) {
        if (A->Provenance[i] != BitPart::Unset &&
            B->Provenance[i] != BitPart::Unset &&
            A->Provenance[i] != B->Provenance[i])
          return Result = None;

        if (A->Provenance[i] == BitPart::Unset)
          Result->Provenance[i] = B->Provenance[i];
        else
          Result->Provenance[i] = A->Provenance[i];
      }

      return Result;
    }

    // If this is a logical shift by a constant, recurse then shift the result.
    if (I->isLogicalShift() && isa<ConstantInt>(I->getOperand(1))) {
      unsigned BitShift =
          cast<ConstantInt>(I->getOperand(1))->getLimitedValue(~0U);
      // Ensure the shift amount is defined.
      if (BitShift > BitWidth)
        return Result;

      auto &Res = collectBitParts(I->getOperand(0), MatchBSwaps,
                                  MatchBitReversals, BPS);
      if (!Res)
        return Result;
      Result = Res;

      // Perform the "shift" on BitProvenance.
      auto &P = Result->Provenance;
      if (I->getOpcode() == Instruction::Shl) {
        P.erase(std::prev(P.end(), BitShift), P.end());
        P.insert(P.begin(), BitShift, BitPart::Unset);
      } else {
        P.erase(P.begin(), std::next(P.begin(), BitShift));
        P.insert(P.end(), BitShift, BitPart::Unset);
      }

      return Result;
    }

    // If this is a logical 'and' with a mask that clears bits, recurse then
    // unset the appropriate bits.
    if (I->getOpcode() == Instruction::And &&
        isa<ConstantInt>(I->getOperand(1))) {
      APInt Bit(I->getType()->getPrimitiveSizeInBits(), 1);
      const APInt &AndMask = cast<ConstantInt>(I->getOperand(1))->getValue();

      // Check that the mask allows a multiple of 8 bits for a bswap, for an
      // early exit.
      unsigned NumMaskedBits = AndMask.countPopulation();
      if (!MatchBitReversals && NumMaskedBits % 8 != 0)
        return Result;

      auto &Res = collectBitParts(I->getOperand(0), MatchBSwaps,
                                  MatchBitReversals, BPS);
      if (!Res)
        return Result;
      Result = Res;

      for (unsigned i = 0; i < BitWidth; ++i, Bit <<= 1)
        // If the AndMask is zero for this bit, clear the bit.
        if ((AndMask & Bit) == 0)
          Result->Provenance[i] = BitPart::Unset;
      return Result;
    }

    // If this is a zext instruction zero extend the result.
    if (I->getOpcode() == Instruction::ZExt) {
      auto &Res = collectBitParts(I->getOperand(0), MatchBSwaps,
                                  MatchBitReversals, BPS);
      if (!Res)
        return Result;

      Result = BitPart(Res->Provider, BitWidth);
      auto NarrowBitWidth =
          cast<IntegerType>(cast<ZExtInst>(I)->getSrcTy())->getBitWidth();
      for (unsigned i = 0; i < NarrowBitWidth; ++i)
        Result->Provenance[i] = Res->Provenance[i];
      for (unsigned i = NarrowBitWidth; i < BitWidth; ++i)
        Result->Provenance[i] = BitPart::Unset;
      return Result;
    }
  }

  // Okay, we got to something that isn't a shift, 'or' or 'and'.  This must be
  // the input value to the bswap/bitreverse.
  Result = BitPart(V, BitWidth);
  for (unsigned i = 0; i < BitWidth; ++i)
    Result->Provenance[i] = i;
  return Result;
}

static bool bitTransformIsCorrectForBSwap(unsigned From, unsigned To,
                                          unsigned BitWidth) {
  if (From % 8 != To % 8)
    return false;
  // Convert from bit indices to byte indices and check for a byte reversal.
  From >>= 3;
  To >>= 3;
  BitWidth >>= 3;
  return From == BitWidth - To - 1;
}

static bool bitTransformIsCorrectForBitReverse(unsigned From, unsigned To,
                                               unsigned BitWidth) {
  return From == BitWidth - To - 1;
}

bool llvm::recognizeBSwapOrBitReverseIdiom(
    Instruction *I, bool MatchBSwaps, bool MatchBitReversals,
    SmallVectorImpl<Instruction *> &InsertedInsts) {
  if (Operator::getOpcode(I) != Instruction::Or)
    return false;
  if (!MatchBSwaps && !MatchBitReversals)
    return false;
  IntegerType *ITy = dyn_cast<IntegerType>(I->getType());
  if (!ITy || ITy->getBitWidth() > 128)
    return false;   // Can't do vectors or integers > 128 bits.
  unsigned BW = ITy->getBitWidth();

  unsigned DemandedBW = BW;
  IntegerType *DemandedTy = ITy;
  if (I->hasOneUse()) {
    if (TruncInst *Trunc = dyn_cast<TruncInst>(I->user_back())) {
      DemandedTy = cast<IntegerType>(Trunc->getType());
      DemandedBW = DemandedTy->getBitWidth();
    }
  }

  // Try to find all the pieces corresponding to the bswap.
  std::map<Value *, Optional<BitPart>> BPS;
  auto Res = collectBitParts(I, MatchBSwaps, MatchBitReversals, BPS);
  if (!Res)
    return false;
  auto &BitProvenance = Res->Provenance;

  // Now, is the bit permutation correct for a bswap or a bitreverse? We can
  // only byteswap values with an even number of bytes.
  bool OKForBSwap = DemandedBW % 16 == 0, OKForBitReverse = true;
  for (unsigned i = 0; i < DemandedBW; ++i) {
    OKForBSwap &=
        bitTransformIsCorrectForBSwap(BitProvenance[i], i, DemandedBW);
    OKForBitReverse &=
        bitTransformIsCorrectForBitReverse(BitProvenance[i], i, DemandedBW);
  }

  Intrinsic::ID Intrin;
  if (OKForBSwap && MatchBSwaps)
    Intrin = Intrinsic::bswap;
  else if (OKForBitReverse && MatchBitReversals)
    Intrin = Intrinsic::bitreverse;
  else
    return false;

  if (ITy != DemandedTy) {
    Function *F = Intrinsic::getDeclaration(I->getModule(), Intrin, DemandedTy);
    Value *Provider = Res->Provider;
    IntegerType *ProviderTy = cast<IntegerType>(Provider->getType());
    // We may need to truncate the provider.
    if (DemandedTy != ProviderTy) {
      auto *Trunc = CastInst::Create(Instruction::Trunc, Provider, DemandedTy,
                                     "trunc", I);
      InsertedInsts.push_back(Trunc);
      Provider = Trunc;
    }
    auto *CI = CallInst::Create(F, Provider, "rev", I);
    InsertedInsts.push_back(CI);
    auto *ExtInst = CastInst::Create(Instruction::ZExt, CI, ITy, "zext", I);
    InsertedInsts.push_back(ExtInst);
    return true;
  }

  Function *F = Intrinsic::getDeclaration(I->getModule(), Intrin, ITy);
  InsertedInsts.push_back(CallInst::Create(F, Res->Provider, "rev", I));
  return true;
}

// CodeGen has special handling for some string functions that may replace
// them with target-specific intrinsics.  Since that'd skip our interceptors
// in ASan/MSan/TSan/DFSan, and thus make us miss some memory accesses,
// we mark affected calls as NoBuiltin, which will disable optimization
// in CodeGen.
void llvm::maybeMarkSanitizerLibraryCallNoBuiltin(
    CallInst *CI, const TargetLibraryInfo *TLI) {
  Function *F = CI->getCalledFunction();
  LibFunc Func;
  if (F && !F->hasLocalLinkage() && F->hasName() &&
      TLI->getLibFunc(F->getName(), Func) && TLI->hasOptimizedCodeGen(Func) &&
      !F->doesNotAccessMemory())
    CI->addAttribute(AttributeList::FunctionIndex, Attribute::NoBuiltin);
}

bool llvm::canReplaceOperandWithVariable(const Instruction *I, unsigned OpIdx) {
  // We can't have a PHI with a metadata type.
  if (I->getOperand(OpIdx)->getType()->isMetadataTy())
    return false;

  // Early exit.
  if (!isa<Constant>(I->getOperand(OpIdx)))
    return true;

  switch (I->getOpcode()) {
  default:
    return true;
  case Instruction::Call:
  case Instruction::Invoke:
    // Can't handle inline asm. Skip it.
    if (isa<InlineAsm>(ImmutableCallSite(I).getCalledValue()))
      return false;
    // Many arithmetic intrinsics have no issue taking a
    // variable, however it's hard to distingish these from
    // specials such as @llvm.frameaddress that require a constant.
    if (isa<IntrinsicInst>(I))
      return false;

    // Constant bundle operands may need to retain their constant-ness for
    // correctness.
    if (ImmutableCallSite(I).isBundleOperand(OpIdx))
      return false;
    return true;
  case Instruction::ShuffleVector:
    // Shufflevector masks are constant.
    return OpIdx != 2;
  case Instruction::Switch:
  case Instruction::ExtractValue:
    // All operands apart from the first are constant.
    return OpIdx == 0;
  case Instruction::InsertValue:
    // All operands apart from the first and the second are constant.
    return OpIdx < 2;
  case Instruction::Alloca:
    // Static allocas (constant size in the entry block) are handled by
    // prologue/epilogue insertion so they're free anyway. We definitely don't
    // want to make them non-constant.
    return !cast<AllocaInst>(I)->isStaticAlloca();
  case Instruction::GetElementPtr:
    if (OpIdx == 0)
      return true;
    gep_type_iterator It = gep_type_begin(I);
    for (auto E = std::next(It, OpIdx); It != E; ++It)
      if (It.isStruct())
        return false;
    return true;
  }
}
