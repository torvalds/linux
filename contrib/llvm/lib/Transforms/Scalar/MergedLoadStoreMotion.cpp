//===- MergedLoadStoreMotion.cpp - merge and hoist/sink load/stores -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//! \file
//! This pass performs merges of loads and stores on both sides of a
//  diamond (hammock). It hoists the loads and sinks the stores.
//
// The algorithm iteratively hoists two loads to the same address out of a
// diamond (hammock) and merges them into a single load in the header. Similar
// it sinks and merges two stores to the tail block (footer). The algorithm
// iterates over the instructions of one side of the diamond and attempts to
// find a matching load/store on the other side. It hoists / sinks when it
// thinks it safe to do so.  This optimization helps with eg. hiding load
// latencies, triggering if-conversion, and reducing static code size.
//
// NOTE: This code no longer performs load hoisting, it is subsumed by GVNHoist.
//
//===----------------------------------------------------------------------===//
//
//
// Example:
// Diamond shaped code before merge:
//
//            header:
//                     br %cond, label %if.then, label %if.else
//                        +                    +
//                       +                      +
//                      +                        +
//            if.then:                         if.else:
//               %lt = load %addr_l               %le = load %addr_l
//               <use %lt>                        <use %le>
//               <...>                            <...>
//               store %st, %addr_s               store %se, %addr_s
//               br label %if.end                 br label %if.end
//                     +                         +
//                      +                       +
//                       +                     +
//            if.end ("footer"):
//                     <...>
//
// Diamond shaped code after merge:
//
//            header:
//                     %l = load %addr_l
//                     br %cond, label %if.then, label %if.else
//                        +                    +
//                       +                      +
//                      +                        +
//            if.then:                         if.else:
//               <use %l>                         <use %l>
//               <...>                            <...>
//               br label %if.end                 br label %if.end
//                      +                        +
//                       +                      +
//                        +                    +
//            if.end ("footer"):
//                     %s.sink = phi [%st, if.then], [%se, if.else]
//                     <...>
//                     store %s.sink, %addr_s
//                     <...>
//
//
//===----------------------- TODO -----------------------------------------===//
//
// 1) Generalize to regions other than diamonds
// 2) Be more aggressive merging memory operations
// Note that both changes require register pressure control
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/MergedLoadStoreMotion.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

#define DEBUG_TYPE "mldst-motion"

namespace {
//===----------------------------------------------------------------------===//
//                         MergedLoadStoreMotion Pass
//===----------------------------------------------------------------------===//
class MergedLoadStoreMotion {
  AliasAnalysis *AA = nullptr;

  // The mergeLoad/Store algorithms could have Size0 * Size1 complexity,
  // where Size0 and Size1 are the #instructions on the two sides of
  // the diamond. The constant chosen here is arbitrary. Compiler Time
  // Control is enforced by the check Size0 * Size1 < MagicCompileTimeControl.
  const int MagicCompileTimeControl = 250;

public:
  bool run(Function &F, AliasAnalysis &AA);

private:
  BasicBlock *getDiamondTail(BasicBlock *BB);
  bool isDiamondHead(BasicBlock *BB);
  // Routines for sinking stores
  StoreInst *canSinkFromBlock(BasicBlock *BB, StoreInst *SI);
  PHINode *getPHIOperand(BasicBlock *BB, StoreInst *S0, StoreInst *S1);
  bool isStoreSinkBarrierInRange(const Instruction &Start,
                                 const Instruction &End, MemoryLocation Loc);
  bool sinkStore(BasicBlock *BB, StoreInst *SinkCand, StoreInst *ElseInst);
  bool mergeStores(BasicBlock *BB);
};
} // end anonymous namespace

///
/// Return tail block of a diamond.
///
BasicBlock *MergedLoadStoreMotion::getDiamondTail(BasicBlock *BB) {
  assert(isDiamondHead(BB) && "Basic block is not head of a diamond");
  return BB->getTerminator()->getSuccessor(0)->getSingleSuccessor();
}

///
/// True when BB is the head of a diamond (hammock)
///
bool MergedLoadStoreMotion::isDiamondHead(BasicBlock *BB) {
  if (!BB)
    return false;
  auto *BI = dyn_cast<BranchInst>(BB->getTerminator());
  if (!BI || !BI->isConditional())
    return false;

  BasicBlock *Succ0 = BI->getSuccessor(0);
  BasicBlock *Succ1 = BI->getSuccessor(1);

  if (!Succ0->getSinglePredecessor())
    return false;
  if (!Succ1->getSinglePredecessor())
    return false;

  BasicBlock *Succ0Succ = Succ0->getSingleSuccessor();
  BasicBlock *Succ1Succ = Succ1->getSingleSuccessor();
  // Ignore triangles.
  if (!Succ0Succ || !Succ1Succ || Succ0Succ != Succ1Succ)
    return false;
  return true;
}


///
/// True when instruction is a sink barrier for a store
/// located in Loc
///
/// Whenever an instruction could possibly read or modify the
/// value being stored or protect against the store from
/// happening it is considered a sink barrier.
///
bool MergedLoadStoreMotion::isStoreSinkBarrierInRange(const Instruction &Start,
                                                      const Instruction &End,
                                                      MemoryLocation Loc) {
  for (const Instruction &Inst :
       make_range(Start.getIterator(), End.getIterator()))
    if (Inst.mayThrow())
      return true;
  return AA->canInstructionRangeModRef(Start, End, Loc, ModRefInfo::ModRef);
}

///
/// Check if \p BB contains a store to the same address as \p SI
///
/// \return The store in \p  when it is safe to sink. Otherwise return Null.
///
StoreInst *MergedLoadStoreMotion::canSinkFromBlock(BasicBlock *BB1,
                                                   StoreInst *Store0) {
  LLVM_DEBUG(dbgs() << "can Sink? : "; Store0->dump(); dbgs() << "\n");
  BasicBlock *BB0 = Store0->getParent();
  for (Instruction &Inst : reverse(*BB1)) {
    auto *Store1 = dyn_cast<StoreInst>(&Inst);
    if (!Store1)
      continue;

    MemoryLocation Loc0 = MemoryLocation::get(Store0);
    MemoryLocation Loc1 = MemoryLocation::get(Store1);
    if (AA->isMustAlias(Loc0, Loc1) && Store0->isSameOperationAs(Store1) &&
        !isStoreSinkBarrierInRange(*Store1->getNextNode(), BB1->back(), Loc1) &&
        !isStoreSinkBarrierInRange(*Store0->getNextNode(), BB0->back(), Loc0)) {
      return Store1;
    }
  }
  return nullptr;
}

///
/// Create a PHI node in BB for the operands of S0 and S1
///
PHINode *MergedLoadStoreMotion::getPHIOperand(BasicBlock *BB, StoreInst *S0,
                                              StoreInst *S1) {
  // Create a phi if the values mismatch.
  Value *Opd1 = S0->getValueOperand();
  Value *Opd2 = S1->getValueOperand();
  if (Opd1 == Opd2)
    return nullptr;

  auto *NewPN = PHINode::Create(Opd1->getType(), 2, Opd2->getName() + ".sink",
                                &BB->front());
  NewPN->applyMergedLocation(S0->getDebugLoc(), S1->getDebugLoc());
  NewPN->addIncoming(Opd1, S0->getParent());
  NewPN->addIncoming(Opd2, S1->getParent());
  return NewPN;
}

///
/// Merge two stores to same address and sink into \p BB
///
/// Also sinks GEP instruction computing the store address
///
bool MergedLoadStoreMotion::sinkStore(BasicBlock *BB, StoreInst *S0,
                                      StoreInst *S1) {
  // Only one definition?
  auto *A0 = dyn_cast<Instruction>(S0->getPointerOperand());
  auto *A1 = dyn_cast<Instruction>(S1->getPointerOperand());
  if (A0 && A1 && A0->isIdenticalTo(A1) && A0->hasOneUse() &&
      (A0->getParent() == S0->getParent()) && A1->hasOneUse() &&
      (A1->getParent() == S1->getParent()) && isa<GetElementPtrInst>(A0)) {
    LLVM_DEBUG(dbgs() << "Sink Instruction into BB \n"; BB->dump();
               dbgs() << "Instruction Left\n"; S0->dump(); dbgs() << "\n";
               dbgs() << "Instruction Right\n"; S1->dump(); dbgs() << "\n");
    // Hoist the instruction.
    BasicBlock::iterator InsertPt = BB->getFirstInsertionPt();
    // Intersect optional metadata.
    S0->andIRFlags(S1);
    S0->dropUnknownNonDebugMetadata();

    // Create the new store to be inserted at the join point.
    StoreInst *SNew = cast<StoreInst>(S0->clone());
    Instruction *ANew = A0->clone();
    SNew->insertBefore(&*InsertPt);
    ANew->insertBefore(SNew);

    assert(S0->getParent() == A0->getParent());
    assert(S1->getParent() == A1->getParent());

    // New PHI operand? Use it.
    if (PHINode *NewPN = getPHIOperand(BB, S0, S1))
      SNew->setOperand(0, NewPN);
    S0->eraseFromParent();
    S1->eraseFromParent();
    A0->replaceAllUsesWith(ANew);
    A0->eraseFromParent();
    A1->replaceAllUsesWith(ANew);
    A1->eraseFromParent();
    return true;
  }
  return false;
}

///
/// True when two stores are equivalent and can sink into the footer
///
/// Starting from a diamond tail block, iterate over the instructions in one
/// predecessor block and try to match a store in the second predecessor.
///
bool MergedLoadStoreMotion::mergeStores(BasicBlock *T) {

  bool MergedStores = false;
  assert(T && "Footer of a diamond cannot be empty");

  pred_iterator PI = pred_begin(T), E = pred_end(T);
  assert(PI != E);
  BasicBlock *Pred0 = *PI;
  ++PI;
  BasicBlock *Pred1 = *PI;
  ++PI;
  // tail block  of a diamond/hammock?
  if (Pred0 == Pred1)
    return false; // No.
  if (PI != E)
    return false; // No. More than 2 predecessors.

  // #Instructions in Succ1 for Compile Time Control
  auto InstsNoDbg = Pred1->instructionsWithoutDebug();
  int Size1 = std::distance(InstsNoDbg.begin(), InstsNoDbg.end());
  int NStores = 0;

  for (BasicBlock::reverse_iterator RBI = Pred0->rbegin(), RBE = Pred0->rend();
       RBI != RBE;) {

    Instruction *I = &*RBI;
    ++RBI;

    // Don't sink non-simple (atomic, volatile) stores.
    auto *S0 = dyn_cast<StoreInst>(I);
    if (!S0 || !S0->isSimple())
      continue;

    ++NStores;
    if (NStores * Size1 >= MagicCompileTimeControl)
      break;
    if (StoreInst *S1 = canSinkFromBlock(Pred1, S0)) {
      bool Res = sinkStore(T, S0, S1);
      MergedStores |= Res;
      // Don't attempt to sink below stores that had to stick around
      // But after removal of a store and some of its feeding
      // instruction search again from the beginning since the iterator
      // is likely stale at this point.
      if (!Res)
        break;
      RBI = Pred0->rbegin();
      RBE = Pred0->rend();
      LLVM_DEBUG(dbgs() << "Search again\n"; Instruction *I = &*RBI; I->dump());
    }
  }
  return MergedStores;
}

bool MergedLoadStoreMotion::run(Function &F, AliasAnalysis &AA) {
  this->AA = &AA;

  bool Changed = false;
  LLVM_DEBUG(dbgs() << "Instruction Merger\n");

  // Merge unconditional branches, allowing PRE to catch more
  // optimization opportunities.
  for (Function::iterator FI = F.begin(), FE = F.end(); FI != FE;) {
    BasicBlock *BB = &*FI++;

    // Hoist equivalent loads and sink stores
    // outside diamonds when possible
    if (isDiamondHead(BB)) {
      Changed |= mergeStores(getDiamondTail(BB));
    }
  }
  return Changed;
}

namespace {
class MergedLoadStoreMotionLegacyPass : public FunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  MergedLoadStoreMotionLegacyPass() : FunctionPass(ID) {
    initializeMergedLoadStoreMotionLegacyPassPass(
        *PassRegistry::getPassRegistry());
  }

  ///
  /// Run the transformation for each function
  ///
  bool runOnFunction(Function &F) override {
    if (skipFunction(F))
      return false;
    MergedLoadStoreMotion Impl;
    return Impl.run(F, getAnalysis<AAResultsWrapperPass>().getAAResults());
  }

private:
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<AAResultsWrapperPass>();
    AU.addPreserved<GlobalsAAWrapperPass>();
  }
};

char MergedLoadStoreMotionLegacyPass::ID = 0;
} // anonymous namespace

///
/// createMergedLoadStoreMotionPass - The public interface to this file.
///
FunctionPass *llvm::createMergedLoadStoreMotionPass() {
  return new MergedLoadStoreMotionLegacyPass();
}

INITIALIZE_PASS_BEGIN(MergedLoadStoreMotionLegacyPass, "mldst-motion",
                      "MergedLoadStoreMotion", false, false)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(MergedLoadStoreMotionLegacyPass, "mldst-motion",
                    "MergedLoadStoreMotion", false, false)

PreservedAnalyses
MergedLoadStoreMotionPass::run(Function &F, FunctionAnalysisManager &AM) {
  MergedLoadStoreMotion Impl;
  auto &AA = AM.getResult<AAManager>(F);
  if (!Impl.run(F, AA))
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  PA.preserve<GlobalsAA>();
  return PA;
}
