//===-- LoopUnrollAndJam.cpp - Loop unrolling utilities -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements loop unroll and jam as a routine, much like
// LoopUnroll.cpp implements loop unroll.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/Analysis/Utils/Local.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/SimplifyIndVar.h"
#include "llvm/Transforms/Utils/UnrollLoop.h"
using namespace llvm;

#define DEBUG_TYPE "loop-unroll-and-jam"

STATISTIC(NumUnrolledAndJammed, "Number of loops unroll and jammed");
STATISTIC(NumCompletelyUnrolledAndJammed, "Number of loops unroll and jammed");

typedef SmallPtrSet<BasicBlock *, 4> BasicBlockSet;

// Partition blocks in an outer/inner loop pair into blocks before and after
// the loop
static bool partitionOuterLoopBlocks(Loop *L, Loop *SubLoop,
                                     BasicBlockSet &ForeBlocks,
                                     BasicBlockSet &SubLoopBlocks,
                                     BasicBlockSet &AftBlocks,
                                     DominatorTree *DT) {
  BasicBlock *SubLoopLatch = SubLoop->getLoopLatch();
  SubLoopBlocks.insert(SubLoop->block_begin(), SubLoop->block_end());

  for (BasicBlock *BB : L->blocks()) {
    if (!SubLoop->contains(BB)) {
      if (DT->dominates(SubLoopLatch, BB))
        AftBlocks.insert(BB);
      else
        ForeBlocks.insert(BB);
    }
  }

  // Check that all blocks in ForeBlocks together dominate the subloop
  // TODO: This might ideally be done better with a dominator/postdominators.
  BasicBlock *SubLoopPreHeader = SubLoop->getLoopPreheader();
  for (BasicBlock *BB : ForeBlocks) {
    if (BB == SubLoopPreHeader)
      continue;
    Instruction *TI = BB->getTerminator();
    for (unsigned i = 0, e = TI->getNumSuccessors(); i != e; ++i)
      if (!ForeBlocks.count(TI->getSuccessor(i)))
        return false;
  }

  return true;
}

// Looks at the phi nodes in Header for values coming from Latch. For these
// instructions and all their operands calls Visit on them, keeping going for
// all the operands in AftBlocks. Returns false if Visit returns false,
// otherwise returns true. This is used to process the instructions in the
// Aft blocks that need to be moved before the subloop. It is used in two
// places. One to check that the required set of instructions can be moved
// before the loop. Then to collect the instructions to actually move in
// moveHeaderPhiOperandsToForeBlocks.
template <typename T>
static bool processHeaderPhiOperands(BasicBlock *Header, BasicBlock *Latch,
                                     BasicBlockSet &AftBlocks, T Visit) {
  SmallVector<Instruction *, 8> Worklist;
  for (auto &Phi : Header->phis()) {
    Value *V = Phi.getIncomingValueForBlock(Latch);
    if (Instruction *I = dyn_cast<Instruction>(V))
      Worklist.push_back(I);
  }

  while (!Worklist.empty()) {
    Instruction *I = Worklist.back();
    Worklist.pop_back();
    if (!Visit(I))
      return false;

    if (AftBlocks.count(I->getParent()))
      for (auto &U : I->operands())
        if (Instruction *II = dyn_cast<Instruction>(U))
          Worklist.push_back(II);
  }

  return true;
}

// Move the phi operands of Header from Latch out of AftBlocks to InsertLoc.
static void moveHeaderPhiOperandsToForeBlocks(BasicBlock *Header,
                                              BasicBlock *Latch,
                                              Instruction *InsertLoc,
                                              BasicBlockSet &AftBlocks) {
  // We need to ensure we move the instructions in the correct order,
  // starting with the earliest required instruction and moving forward.
  std::vector<Instruction *> Visited;
  processHeaderPhiOperands(Header, Latch, AftBlocks,
                           [&Visited, &AftBlocks](Instruction *I) {
                             if (AftBlocks.count(I->getParent()))
                               Visited.push_back(I);
                             return true;
                           });

  // Move all instructions in program order to before the InsertLoc
  BasicBlock *InsertLocBB = InsertLoc->getParent();
  for (Instruction *I : reverse(Visited)) {
    if (I->getParent() != InsertLocBB)
      I->moveBefore(InsertLoc);
  }
}

/*
  This method performs Unroll and Jam. For a simple loop like:
  for (i = ..)
    Fore(i)
    for (j = ..)
      SubLoop(i, j)
    Aft(i)

  Instead of doing normal inner or outer unrolling, we do:
  for (i = .., i+=2)
    Fore(i)
    Fore(i+1)
    for (j = ..)
      SubLoop(i, j)
      SubLoop(i+1, j)
    Aft(i)
    Aft(i+1)

  So the outer loop is essetially unrolled and then the inner loops are fused
  ("jammed") together into a single loop. This can increase speed when there
  are loads in SubLoop that are invariant to i, as they become shared between
  the now jammed inner loops.

  We do this by spliting the blocks in the loop into Fore, Subloop and Aft.
  Fore blocks are those before the inner loop, Aft are those after. Normal
  Unroll code is used to copy each of these sets of blocks and the results are
  combined together into the final form above.

  isSafeToUnrollAndJam should be used prior to calling this to make sure the
  unrolling will be valid. Checking profitablility is also advisable.

  If EpilogueLoop is non-null, it receives the epilogue loop (if it was
  necessary to create one and not fully unrolled).
*/
LoopUnrollResult llvm::UnrollAndJamLoop(
    Loop *L, unsigned Count, unsigned TripCount, unsigned TripMultiple,
    bool UnrollRemainder, LoopInfo *LI, ScalarEvolution *SE, DominatorTree *DT,
    AssumptionCache *AC, OptimizationRemarkEmitter *ORE, Loop **EpilogueLoop) {

  // When we enter here we should have already checked that it is safe
  BasicBlock *Header = L->getHeader();
  assert(L->getSubLoops().size() == 1);
  Loop *SubLoop = *L->begin();

  // Don't enter the unroll code if there is nothing to do.
  if (TripCount == 0 && Count < 2) {
    LLVM_DEBUG(dbgs() << "Won't unroll-and-jam; almost nothing to do\n");
    return LoopUnrollResult::Unmodified;
  }

  assert(Count > 0);
  assert(TripMultiple > 0);
  assert(TripCount == 0 || TripCount % TripMultiple == 0);

  // Are we eliminating the loop control altogether?
  bool CompletelyUnroll = (Count == TripCount);

  // We use the runtime remainder in cases where we don't know trip multiple
  if (TripMultiple == 1 || TripMultiple % Count != 0) {
    if (!UnrollRuntimeLoopRemainder(L, Count, /*AllowExpensiveTripCount*/ false,
                                    /*UseEpilogRemainder*/ true,
                                    UnrollRemainder, LI, SE, DT, AC, true,
                                    EpilogueLoop)) {
      LLVM_DEBUG(dbgs() << "Won't unroll-and-jam; remainder loop could not be "
                           "generated when assuming runtime trip count\n");
      return LoopUnrollResult::Unmodified;
    }
  }

  // Notify ScalarEvolution that the loop will be substantially changed,
  // if not outright eliminated.
  if (SE) {
    SE->forgetLoop(L);
    SE->forgetLoop(SubLoop);
  }

  using namespace ore;
  // Report the unrolling decision.
  if (CompletelyUnroll) {
    LLVM_DEBUG(dbgs() << "COMPLETELY UNROLL AND JAMMING loop %"
                      << Header->getName() << " with trip count " << TripCount
                      << "!\n");
    ORE->emit(OptimizationRemark(DEBUG_TYPE, "FullyUnrolled", L->getStartLoc(),
                                 L->getHeader())
              << "completely unroll and jammed loop with "
              << NV("UnrollCount", TripCount) << " iterations");
  } else {
    auto DiagBuilder = [&]() {
      OptimizationRemark Diag(DEBUG_TYPE, "PartialUnrolled", L->getStartLoc(),
                              L->getHeader());
      return Diag << "unroll and jammed loop by a factor of "
                  << NV("UnrollCount", Count);
    };

    LLVM_DEBUG(dbgs() << "UNROLL AND JAMMING loop %" << Header->getName()
                      << " by " << Count);
    if (TripMultiple != 1) {
      LLVM_DEBUG(dbgs() << " with " << TripMultiple << " trips per branch");
      ORE->emit([&]() {
        return DiagBuilder() << " with " << NV("TripMultiple", TripMultiple)
                             << " trips per branch";
      });
    } else {
      LLVM_DEBUG(dbgs() << " with run-time trip count");
      ORE->emit([&]() { return DiagBuilder() << " with run-time trip count"; });
    }
    LLVM_DEBUG(dbgs() << "!\n");
  }

  BasicBlock *Preheader = L->getLoopPreheader();
  BasicBlock *LatchBlock = L->getLoopLatch();
  BranchInst *BI = dyn_cast<BranchInst>(LatchBlock->getTerminator());
  assert(Preheader && LatchBlock && Header);
  assert(BI && !BI->isUnconditional());
  bool ContinueOnTrue = L->contains(BI->getSuccessor(0));
  BasicBlock *LoopExit = BI->getSuccessor(ContinueOnTrue);
  bool SubLoopContinueOnTrue = SubLoop->contains(
      SubLoop->getLoopLatch()->getTerminator()->getSuccessor(0));

  // Partition blocks in an outer/inner loop pair into blocks before and after
  // the loop
  BasicBlockSet SubLoopBlocks;
  BasicBlockSet ForeBlocks;
  BasicBlockSet AftBlocks;
  partitionOuterLoopBlocks(L, SubLoop, ForeBlocks, SubLoopBlocks, AftBlocks,
                           DT);

  // We keep track of the entering/first and exiting/last block of each of
  // Fore/SubLoop/Aft in each iteration. This helps make the stapling up of
  // blocks easier.
  std::vector<BasicBlock *> ForeBlocksFirst;
  std::vector<BasicBlock *> ForeBlocksLast;
  std::vector<BasicBlock *> SubLoopBlocksFirst;
  std::vector<BasicBlock *> SubLoopBlocksLast;
  std::vector<BasicBlock *> AftBlocksFirst;
  std::vector<BasicBlock *> AftBlocksLast;
  ForeBlocksFirst.push_back(Header);
  ForeBlocksLast.push_back(SubLoop->getLoopPreheader());
  SubLoopBlocksFirst.push_back(SubLoop->getHeader());
  SubLoopBlocksLast.push_back(SubLoop->getExitingBlock());
  AftBlocksFirst.push_back(SubLoop->getExitBlock());
  AftBlocksLast.push_back(L->getExitingBlock());
  // Maps Blocks[0] -> Blocks[It]
  ValueToValueMapTy LastValueMap;

  // Move any instructions from fore phi operands from AftBlocks into Fore.
  moveHeaderPhiOperandsToForeBlocks(
      Header, LatchBlock, SubLoop->getLoopPreheader()->getTerminator(),
      AftBlocks);

  // The current on-the-fly SSA update requires blocks to be processed in
  // reverse postorder so that LastValueMap contains the correct value at each
  // exit.
  LoopBlocksDFS DFS(L);
  DFS.perform(LI);
  // Stash the DFS iterators before adding blocks to the loop.
  LoopBlocksDFS::RPOIterator BlockBegin = DFS.beginRPO();
  LoopBlocksDFS::RPOIterator BlockEnd = DFS.endRPO();

  if (Header->getParent()->isDebugInfoForProfiling())
    for (BasicBlock *BB : L->getBlocks())
      for (Instruction &I : *BB)
        if (!isa<DbgInfoIntrinsic>(&I))
          if (const DILocation *DIL = I.getDebugLoc()) {
            auto NewDIL = DIL->cloneWithDuplicationFactor(Count);
            if (NewDIL)
              I.setDebugLoc(NewDIL.getValue());
            else
              LLVM_DEBUG(dbgs()
                         << "Failed to create new discriminator: "
                         << DIL->getFilename() << " Line: " << DIL->getLine());
          }

  // Copy all blocks
  for (unsigned It = 1; It != Count; ++It) {
    std::vector<BasicBlock *> NewBlocks;
    // Maps Blocks[It] -> Blocks[It-1]
    DenseMap<Value *, Value *> PrevItValueMap;

    for (LoopBlocksDFS::RPOIterator BB = BlockBegin; BB != BlockEnd; ++BB) {
      ValueToValueMapTy VMap;
      BasicBlock *New = CloneBasicBlock(*BB, VMap, "." + Twine(It));
      Header->getParent()->getBasicBlockList().push_back(New);

      if (ForeBlocks.count(*BB)) {
        L->addBasicBlockToLoop(New, *LI);

        if (*BB == ForeBlocksFirst[0])
          ForeBlocksFirst.push_back(New);
        if (*BB == ForeBlocksLast[0])
          ForeBlocksLast.push_back(New);
      } else if (SubLoopBlocks.count(*BB)) {
        SubLoop->addBasicBlockToLoop(New, *LI);

        if (*BB == SubLoopBlocksFirst[0])
          SubLoopBlocksFirst.push_back(New);
        if (*BB == SubLoopBlocksLast[0])
          SubLoopBlocksLast.push_back(New);
      } else if (AftBlocks.count(*BB)) {
        L->addBasicBlockToLoop(New, *LI);

        if (*BB == AftBlocksFirst[0])
          AftBlocksFirst.push_back(New);
        if (*BB == AftBlocksLast[0])
          AftBlocksLast.push_back(New);
      } else {
        llvm_unreachable("BB being cloned should be in Fore/Sub/Aft");
      }

      // Update our running maps of newest clones
      PrevItValueMap[New] = (It == 1 ? *BB : LastValueMap[*BB]);
      LastValueMap[*BB] = New;
      for (ValueToValueMapTy::iterator VI = VMap.begin(), VE = VMap.end();
           VI != VE; ++VI) {
        PrevItValueMap[VI->second] =
            const_cast<Value *>(It == 1 ? VI->first : LastValueMap[VI->first]);
        LastValueMap[VI->first] = VI->second;
      }

      NewBlocks.push_back(New);

      // Update DomTree:
      if (*BB == ForeBlocksFirst[0])
        DT->addNewBlock(New, ForeBlocksLast[It - 1]);
      else if (*BB == SubLoopBlocksFirst[0])
        DT->addNewBlock(New, SubLoopBlocksLast[It - 1]);
      else if (*BB == AftBlocksFirst[0])
        DT->addNewBlock(New, AftBlocksLast[It - 1]);
      else {
        // Each set of blocks (Fore/Sub/Aft) will have the same internal domtree
        // structure.
        auto BBDomNode = DT->getNode(*BB);
        auto BBIDom = BBDomNode->getIDom();
        BasicBlock *OriginalBBIDom = BBIDom->getBlock();
        assert(OriginalBBIDom);
        assert(LastValueMap[cast<Value>(OriginalBBIDom)]);
        DT->addNewBlock(
            New, cast<BasicBlock>(LastValueMap[cast<Value>(OriginalBBIDom)]));
      }
    }

    // Remap all instructions in the most recent iteration
    for (BasicBlock *NewBlock : NewBlocks) {
      for (Instruction &I : *NewBlock) {
        ::remapInstruction(&I, LastValueMap);
        if (auto *II = dyn_cast<IntrinsicInst>(&I))
          if (II->getIntrinsicID() == Intrinsic::assume)
            AC->registerAssumption(II);
      }
    }

    // Alter the ForeBlocks phi's, pointing them at the latest version of the
    // value from the previous iteration's phis
    for (PHINode &Phi : ForeBlocksFirst[It]->phis()) {
      Value *OldValue = Phi.getIncomingValueForBlock(AftBlocksLast[It]);
      assert(OldValue && "should have incoming edge from Aft[It]");
      Value *NewValue = OldValue;
      if (Value *PrevValue = PrevItValueMap[OldValue])
        NewValue = PrevValue;

      assert(Phi.getNumOperands() == 2);
      Phi.setIncomingBlock(0, ForeBlocksLast[It - 1]);
      Phi.setIncomingValue(0, NewValue);
      Phi.removeIncomingValue(1);
    }
  }

  // Now that all the basic blocks for the unrolled iterations are in place,
  // finish up connecting the blocks and phi nodes. At this point LastValueMap
  // is the last unrolled iterations values.

  // Update Phis in BB from OldBB to point to NewBB
  auto updatePHIBlocks = [](BasicBlock *BB, BasicBlock *OldBB,
                            BasicBlock *NewBB) {
    for (PHINode &Phi : BB->phis()) {
      int I = Phi.getBasicBlockIndex(OldBB);
      Phi.setIncomingBlock(I, NewBB);
    }
  };
  // Update Phis in BB from OldBB to point to NewBB and use the latest value
  // from LastValueMap
  auto updatePHIBlocksAndValues = [](BasicBlock *BB, BasicBlock *OldBB,
                                     BasicBlock *NewBB,
                                     ValueToValueMapTy &LastValueMap) {
    for (PHINode &Phi : BB->phis()) {
      for (unsigned b = 0; b < Phi.getNumIncomingValues(); ++b) {
        if (Phi.getIncomingBlock(b) == OldBB) {
          Value *OldValue = Phi.getIncomingValue(b);
          if (Value *LastValue = LastValueMap[OldValue])
            Phi.setIncomingValue(b, LastValue);
          Phi.setIncomingBlock(b, NewBB);
          break;
        }
      }
    }
  };
  // Move all the phis from Src into Dest
  auto movePHIs = [](BasicBlock *Src, BasicBlock *Dest) {
    Instruction *insertPoint = Dest->getFirstNonPHI();
    while (PHINode *Phi = dyn_cast<PHINode>(Src->begin()))
      Phi->moveBefore(insertPoint);
  };

  // Update the PHI values outside the loop to point to the last block
  updatePHIBlocksAndValues(LoopExit, AftBlocksLast[0], AftBlocksLast.back(),
                           LastValueMap);

  // Update ForeBlocks successors and phi nodes
  BranchInst *ForeTerm =
      cast<BranchInst>(ForeBlocksLast.back()->getTerminator());
  BasicBlock *Dest = SubLoopBlocksFirst[0];
  ForeTerm->setSuccessor(0, Dest);

  if (CompletelyUnroll) {
    while (PHINode *Phi = dyn_cast<PHINode>(ForeBlocksFirst[0]->begin())) {
      Phi->replaceAllUsesWith(Phi->getIncomingValueForBlock(Preheader));
      Phi->getParent()->getInstList().erase(Phi);
    }
  } else {
    // Update the PHI values to point to the last aft block
    updatePHIBlocksAndValues(ForeBlocksFirst[0], AftBlocksLast[0],
                             AftBlocksLast.back(), LastValueMap);
  }

  for (unsigned It = 1; It != Count; It++) {
    // Remap ForeBlock successors from previous iteration to this
    BranchInst *ForeTerm =
        cast<BranchInst>(ForeBlocksLast[It - 1]->getTerminator());
    BasicBlock *Dest = ForeBlocksFirst[It];
    ForeTerm->setSuccessor(0, Dest);
  }

  // Subloop successors and phis
  BranchInst *SubTerm =
      cast<BranchInst>(SubLoopBlocksLast.back()->getTerminator());
  SubTerm->setSuccessor(!SubLoopContinueOnTrue, SubLoopBlocksFirst[0]);
  SubTerm->setSuccessor(SubLoopContinueOnTrue, AftBlocksFirst[0]);
  updatePHIBlocks(SubLoopBlocksFirst[0], ForeBlocksLast[0],
                  ForeBlocksLast.back());
  updatePHIBlocks(SubLoopBlocksFirst[0], SubLoopBlocksLast[0],
                  SubLoopBlocksLast.back());

  for (unsigned It = 1; It != Count; It++) {
    // Replace the conditional branch of the previous iteration subloop with an
    // unconditional one to this one
    BranchInst *SubTerm =
        cast<BranchInst>(SubLoopBlocksLast[It - 1]->getTerminator());
    BranchInst::Create(SubLoopBlocksFirst[It], SubTerm);
    SubTerm->eraseFromParent();

    updatePHIBlocks(SubLoopBlocksFirst[It], ForeBlocksLast[It],
                    ForeBlocksLast.back());
    updatePHIBlocks(SubLoopBlocksFirst[It], SubLoopBlocksLast[It],
                    SubLoopBlocksLast.back());
    movePHIs(SubLoopBlocksFirst[It], SubLoopBlocksFirst[0]);
  }

  // Aft blocks successors and phis
  BranchInst *Term = cast<BranchInst>(AftBlocksLast.back()->getTerminator());
  if (CompletelyUnroll) {
    BranchInst::Create(LoopExit, Term);
    Term->eraseFromParent();
  } else {
    Term->setSuccessor(!ContinueOnTrue, ForeBlocksFirst[0]);
  }
  updatePHIBlocks(AftBlocksFirst[0], SubLoopBlocksLast[0],
                  SubLoopBlocksLast.back());

  for (unsigned It = 1; It != Count; It++) {
    // Replace the conditional branch of the previous iteration subloop with an
    // unconditional one to this one
    BranchInst *AftTerm =
        cast<BranchInst>(AftBlocksLast[It - 1]->getTerminator());
    BranchInst::Create(AftBlocksFirst[It], AftTerm);
    AftTerm->eraseFromParent();

    updatePHIBlocks(AftBlocksFirst[It], SubLoopBlocksLast[It],
                    SubLoopBlocksLast.back());
    movePHIs(AftBlocksFirst[It], AftBlocksFirst[0]);
  }

  // Dominator Tree. Remove the old links between Fore, Sub and Aft, adding the
  // new ones required.
  if (Count != 1) {
    SmallVector<DominatorTree::UpdateType, 4> DTUpdates;
    DTUpdates.emplace_back(DominatorTree::UpdateKind::Delete, ForeBlocksLast[0],
                           SubLoopBlocksFirst[0]);
    DTUpdates.emplace_back(DominatorTree::UpdateKind::Delete,
                           SubLoopBlocksLast[0], AftBlocksFirst[0]);

    DTUpdates.emplace_back(DominatorTree::UpdateKind::Insert,
                           ForeBlocksLast.back(), SubLoopBlocksFirst[0]);
    DTUpdates.emplace_back(DominatorTree::UpdateKind::Insert,
                           SubLoopBlocksLast.back(), AftBlocksFirst[0]);
    DT->applyUpdates(DTUpdates);
  }

  // Merge adjacent basic blocks, if possible.
  SmallPtrSet<BasicBlock *, 16> MergeBlocks;
  MergeBlocks.insert(ForeBlocksLast.begin(), ForeBlocksLast.end());
  MergeBlocks.insert(SubLoopBlocksLast.begin(), SubLoopBlocksLast.end());
  MergeBlocks.insert(AftBlocksLast.begin(), AftBlocksLast.end());
  while (!MergeBlocks.empty()) {
    BasicBlock *BB = *MergeBlocks.begin();
    BranchInst *Term = dyn_cast<BranchInst>(BB->getTerminator());
    if (Term && Term->isUnconditional() && L->contains(Term->getSuccessor(0))) {
      BasicBlock *Dest = Term->getSuccessor(0);
      if (BasicBlock *Fold = foldBlockIntoPredecessor(Dest, LI, SE, DT)) {
        // Don't remove BB and add Fold as they are the same BB
        assert(Fold == BB);
        (void)Fold;
        MergeBlocks.erase(Dest);
      } else
        MergeBlocks.erase(BB);
    } else
      MergeBlocks.erase(BB);
  }

  // At this point, the code is well formed.  We now do a quick sweep over the
  // inserted code, doing constant propagation and dead code elimination as we
  // go.
  simplifyLoopAfterUnroll(SubLoop, true, LI, SE, DT, AC);
  simplifyLoopAfterUnroll(L, !CompletelyUnroll && Count > 1, LI, SE, DT, AC);

  NumCompletelyUnrolledAndJammed += CompletelyUnroll;
  ++NumUnrolledAndJammed;

#ifndef NDEBUG
  // We shouldn't have done anything to break loop simplify form or LCSSA.
  Loop *OuterL = L->getParentLoop();
  Loop *OutestLoop = OuterL ? OuterL : (!CompletelyUnroll ? L : SubLoop);
  assert(OutestLoop->isRecursivelyLCSSAForm(*DT, *LI));
  if (!CompletelyUnroll)
    assert(L->isLoopSimplifyForm());
  assert(SubLoop->isLoopSimplifyForm());
  assert(DT->verify());
#endif

  // Update LoopInfo if the loop is completely removed.
  if (CompletelyUnroll)
    LI->erase(L);

  return CompletelyUnroll ? LoopUnrollResult::FullyUnrolled
                          : LoopUnrollResult::PartiallyUnrolled;
}

static bool getLoadsAndStores(BasicBlockSet &Blocks,
                              SmallVector<Value *, 4> &MemInstr) {
  // Scan the BBs and collect legal loads and stores.
  // Returns false if non-simple loads/stores are found.
  for (BasicBlock *BB : Blocks) {
    for (Instruction &I : *BB) {
      if (auto *Ld = dyn_cast<LoadInst>(&I)) {
        if (!Ld->isSimple())
          return false;
        MemInstr.push_back(&I);
      } else if (auto *St = dyn_cast<StoreInst>(&I)) {
        if (!St->isSimple())
          return false;
        MemInstr.push_back(&I);
      } else if (I.mayReadOrWriteMemory()) {
        return false;
      }
    }
  }
  return true;
}

static bool checkDependencies(SmallVector<Value *, 4> &Earlier,
                              SmallVector<Value *, 4> &Later,
                              unsigned LoopDepth, bool InnerLoop,
                              DependenceInfo &DI) {
  // Use DA to check for dependencies between loads and stores that make unroll
  // and jam invalid
  for (Value *I : Earlier) {
    for (Value *J : Later) {
      Instruction *Src = cast<Instruction>(I);
      Instruction *Dst = cast<Instruction>(J);
      if (Src == Dst)
        continue;
      // Ignore Input dependencies.
      if (isa<LoadInst>(Src) && isa<LoadInst>(Dst))
        continue;

      // Track dependencies, and if we find them take a conservative approach
      // by allowing only = or < (not >), altough some > would be safe
      // (depending upon unroll width).
      // For the inner loop, we need to disallow any (> <) dependencies
      // FIXME: Allow > so long as distance is less than unroll width
      if (auto D = DI.depends(Src, Dst, true)) {
        assert(D->isOrdered() && "Expected an output, flow or anti dep.");

        if (D->isConfused()) {
          LLVM_DEBUG(dbgs() << "  Confused dependency between:\n"
                            << "  " << *Src << "\n"
                            << "  " << *Dst << "\n");
          return false;
        }
        if (!InnerLoop) {
          if (D->getDirection(LoopDepth) & Dependence::DVEntry::GT) {
            LLVM_DEBUG(dbgs() << "  > dependency between:\n"
                              << "  " << *Src << "\n"
                              << "  " << *Dst << "\n");
            return false;
          }
        } else {
          assert(LoopDepth + 1 <= D->getLevels());
          if (D->getDirection(LoopDepth) & Dependence::DVEntry::GT &&
              D->getDirection(LoopDepth + 1) & Dependence::DVEntry::LT) {
            LLVM_DEBUG(dbgs() << "  < > dependency between:\n"
                              << "  " << *Src << "\n"
                              << "  " << *Dst << "\n");
            return false;
          }
        }
      }
    }
  }
  return true;
}

static bool checkDependencies(Loop *L, BasicBlockSet &ForeBlocks,
                              BasicBlockSet &SubLoopBlocks,
                              BasicBlockSet &AftBlocks, DependenceInfo &DI) {
  // Get all loads/store pairs for each blocks
  SmallVector<Value *, 4> ForeMemInstr;
  SmallVector<Value *, 4> SubLoopMemInstr;
  SmallVector<Value *, 4> AftMemInstr;
  if (!getLoadsAndStores(ForeBlocks, ForeMemInstr) ||
      !getLoadsAndStores(SubLoopBlocks, SubLoopMemInstr) ||
      !getLoadsAndStores(AftBlocks, AftMemInstr))
    return false;

  // Check for dependencies between any blocks that may change order
  unsigned LoopDepth = L->getLoopDepth();
  return checkDependencies(ForeMemInstr, SubLoopMemInstr, LoopDepth, false,
                           DI) &&
         checkDependencies(ForeMemInstr, AftMemInstr, LoopDepth, false, DI) &&
         checkDependencies(SubLoopMemInstr, AftMemInstr, LoopDepth, false,
                           DI) &&
         checkDependencies(SubLoopMemInstr, SubLoopMemInstr, LoopDepth, true,
                           DI);
}

bool llvm::isSafeToUnrollAndJam(Loop *L, ScalarEvolution &SE, DominatorTree &DT,
                                DependenceInfo &DI) {
  /* We currently handle outer loops like this:
        |
    ForeFirst    <----\    }
     Blocks           |    } ForeBlocks
    ForeLast          |    }
        |             |
    SubLoopFirst  <\  |    }
     Blocks        |  |    } SubLoopBlocks
    SubLoopLast   -/  |    }
        |             |
    AftFirst          |    }
     Blocks           |    } AftBlocks
    AftLast     ------/    }
        |

    There are (theoretically) any number of blocks in ForeBlocks, SubLoopBlocks
    and AftBlocks, providing that there is one edge from Fores to SubLoops,
    one edge from SubLoops to Afts and a single outer loop exit (from Afts).
    In practice we currently limit Aft blocks to a single block, and limit
    things further in the profitablility checks of the unroll and jam pass.

    Because of the way we rearrange basic blocks, we also require that
    the Fore blocks on all unrolled iterations are safe to move before the
    SubLoop blocks of all iterations. So we require that the phi node looping
    operands of ForeHeader can be moved to at least the end of ForeEnd, so that
    we can arrange cloned Fore Blocks before the subloop and match up Phi's
    correctly.

    i.e. The old order of blocks used to be F1 S1_1 S1_2 A1 F2 S2_1 S2_2 A2.
    It needs to be safe to tranform this to F1 F2 S1_1 S2_1 S1_2 S2_2 A1 A2.

    There are then a number of checks along the lines of no calls, no
    exceptions, inner loop IV is consistent, etc. Note that for loops requiring
    runtime unrolling, UnrollRuntimeLoopRemainder can also fail in
    UnrollAndJamLoop if the trip count cannot be easily calculated.
  */

  if (!L->isLoopSimplifyForm() || L->getSubLoops().size() != 1)
    return false;
  Loop *SubLoop = L->getSubLoops()[0];
  if (!SubLoop->isLoopSimplifyForm())
    return false;

  BasicBlock *Header = L->getHeader();
  BasicBlock *Latch = L->getLoopLatch();
  BasicBlock *Exit = L->getExitingBlock();
  BasicBlock *SubLoopHeader = SubLoop->getHeader();
  BasicBlock *SubLoopLatch = SubLoop->getLoopLatch();
  BasicBlock *SubLoopExit = SubLoop->getExitingBlock();

  if (Latch != Exit)
    return false;
  if (SubLoopLatch != SubLoopExit)
    return false;

  if (Header->hasAddressTaken() || SubLoopHeader->hasAddressTaken()) {
    LLVM_DEBUG(dbgs() << "Won't unroll-and-jam; Address taken\n");
    return false;
  }

  // Split blocks into Fore/SubLoop/Aft based on dominators
  BasicBlockSet SubLoopBlocks;
  BasicBlockSet ForeBlocks;
  BasicBlockSet AftBlocks;
  if (!partitionOuterLoopBlocks(L, SubLoop, ForeBlocks, SubLoopBlocks,
                                AftBlocks, &DT)) {
    LLVM_DEBUG(dbgs() << "Won't unroll-and-jam; Incompatible loop layout\n");
    return false;
  }

  // Aft blocks may need to move instructions to fore blocks, which becomes more
  // difficult if there are multiple (potentially conditionally executed)
  // blocks. For now we just exclude loops with multiple aft blocks.
  if (AftBlocks.size() != 1) {
    LLVM_DEBUG(dbgs() << "Won't unroll-and-jam; Can't currently handle "
                         "multiple blocks after the loop\n");
    return false;
  }

  // Check inner loop backedge count is consistent on all iterations of the
  // outer loop
  if (!hasIterationCountInvariantInParent(SubLoop, SE)) {
    LLVM_DEBUG(dbgs() << "Won't unroll-and-jam; Inner loop iteration count is "
                         "not consistent on each iteration\n");
    return false;
  }

  // Check the loop safety info for exceptions.
  SimpleLoopSafetyInfo LSI;
  LSI.computeLoopSafetyInfo(L);
  if (LSI.anyBlockMayThrow()) {
    LLVM_DEBUG(dbgs() << "Won't unroll-and-jam; Something may throw\n");
    return false;
  }

  // We've ruled out the easy stuff and now need to check that there are no
  // interdependencies which may prevent us from moving the:
  //  ForeBlocks before Subloop and AftBlocks.
  //  Subloop before AftBlocks.
  //  ForeBlock phi operands before the subloop

  // Make sure we can move all instructions we need to before the subloop
  if (!processHeaderPhiOperands(
          Header, Latch, AftBlocks, [&AftBlocks, &SubLoop](Instruction *I) {
            if (SubLoop->contains(I->getParent()))
              return false;
            if (AftBlocks.count(I->getParent())) {
              // If we hit a phi node in afts we know we are done (probably
              // LCSSA)
              if (isa<PHINode>(I))
                return false;
              // Can't move instructions with side effects or memory
              // reads/writes
              if (I->mayHaveSideEffects() || I->mayReadOrWriteMemory())
                return false;
            }
            // Keep going
            return true;
          })) {
    LLVM_DEBUG(dbgs() << "Won't unroll-and-jam; can't move required "
                         "instructions after subloop to before it\n");
    return false;
  }

  // Check for memory dependencies which prohibit the unrolling we are doing.
  // Because of the way we are unrolling Fore/Sub/Aft blocks, we need to check
  // there are no dependencies between Fore-Sub, Fore-Aft, Sub-Aft and Sub-Sub.
  if (!checkDependencies(L, ForeBlocks, SubLoopBlocks, AftBlocks, DI)) {
    LLVM_DEBUG(dbgs() << "Won't unroll-and-jam; failed dependency check\n");
    return false;
  }

  return true;
}
