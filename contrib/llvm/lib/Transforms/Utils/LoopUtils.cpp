//===-- LoopUtils.cpp - Loop Utility functions -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines common loop utility functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/MustExecute.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionAliasAnalysis.h"
#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DomTreeUpdater.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;
using namespace llvm::PatternMatch;

#define DEBUG_TYPE "loop-utils"

static const char *LLVMLoopDisableNonforced = "llvm.loop.disable_nonforced";

bool llvm::formDedicatedExitBlocks(Loop *L, DominatorTree *DT, LoopInfo *LI,
                                   bool PreserveLCSSA) {
  bool Changed = false;

  // We re-use a vector for the in-loop predecesosrs.
  SmallVector<BasicBlock *, 4> InLoopPredecessors;

  auto RewriteExit = [&](BasicBlock *BB) {
    assert(InLoopPredecessors.empty() &&
           "Must start with an empty predecessors list!");
    auto Cleanup = make_scope_exit([&] { InLoopPredecessors.clear(); });

    // See if there are any non-loop predecessors of this exit block and
    // keep track of the in-loop predecessors.
    bool IsDedicatedExit = true;
    for (auto *PredBB : predecessors(BB))
      if (L->contains(PredBB)) {
        if (isa<IndirectBrInst>(PredBB->getTerminator()))
          // We cannot rewrite exiting edges from an indirectbr.
          return false;

        InLoopPredecessors.push_back(PredBB);
      } else {
        IsDedicatedExit = false;
      }

    assert(!InLoopPredecessors.empty() && "Must have *some* loop predecessor!");

    // Nothing to do if this is already a dedicated exit.
    if (IsDedicatedExit)
      return false;

    auto *NewExitBB = SplitBlockPredecessors(
        BB, InLoopPredecessors, ".loopexit", DT, LI, nullptr, PreserveLCSSA);

    if (!NewExitBB)
      LLVM_DEBUG(
          dbgs() << "WARNING: Can't create a dedicated exit block for loop: "
                 << *L << "\n");
    else
      LLVM_DEBUG(dbgs() << "LoopSimplify: Creating dedicated exit block "
                        << NewExitBB->getName() << "\n");
    return true;
  };

  // Walk the exit blocks directly rather than building up a data structure for
  // them, but only visit each one once.
  SmallPtrSet<BasicBlock *, 4> Visited;
  for (auto *BB : L->blocks())
    for (auto *SuccBB : successors(BB)) {
      // We're looking for exit blocks so skip in-loop successors.
      if (L->contains(SuccBB))
        continue;

      // Visit each exit block exactly once.
      if (!Visited.insert(SuccBB).second)
        continue;

      Changed |= RewriteExit(SuccBB);
    }

  return Changed;
}

/// Returns the instructions that use values defined in the loop.
SmallVector<Instruction *, 8> llvm::findDefsUsedOutsideOfLoop(Loop *L) {
  SmallVector<Instruction *, 8> UsedOutside;

  for (auto *Block : L->getBlocks())
    // FIXME: I believe that this could use copy_if if the Inst reference could
    // be adapted into a pointer.
    for (auto &Inst : *Block) {
      auto Users = Inst.users();
      if (any_of(Users, [&](User *U) {
            auto *Use = cast<Instruction>(U);
            return !L->contains(Use->getParent());
          }))
        UsedOutside.push_back(&Inst);
    }

  return UsedOutside;
}

void llvm::getLoopAnalysisUsage(AnalysisUsage &AU) {
  // By definition, all loop passes need the LoopInfo analysis and the
  // Dominator tree it depends on. Because they all participate in the loop
  // pass manager, they must also preserve these.
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addPreserved<DominatorTreeWrapperPass>();
  AU.addRequired<LoopInfoWrapperPass>();
  AU.addPreserved<LoopInfoWrapperPass>();

  // We must also preserve LoopSimplify and LCSSA. We locally access their IDs
  // here because users shouldn't directly get them from this header.
  extern char &LoopSimplifyID;
  extern char &LCSSAID;
  AU.addRequiredID(LoopSimplifyID);
  AU.addPreservedID(LoopSimplifyID);
  AU.addRequiredID(LCSSAID);
  AU.addPreservedID(LCSSAID);
  // This is used in the LPPassManager to perform LCSSA verification on passes
  // which preserve lcssa form
  AU.addRequired<LCSSAVerificationPass>();
  AU.addPreserved<LCSSAVerificationPass>();

  // Loop passes are designed to run inside of a loop pass manager which means
  // that any function analyses they require must be required by the first loop
  // pass in the manager (so that it is computed before the loop pass manager
  // runs) and preserved by all loop pasess in the manager. To make this
  // reasonably robust, the set needed for most loop passes is maintained here.
  // If your loop pass requires an analysis not listed here, you will need to
  // carefully audit the loop pass manager nesting structure that results.
  AU.addRequired<AAResultsWrapperPass>();
  AU.addPreserved<AAResultsWrapperPass>();
  AU.addPreserved<BasicAAWrapperPass>();
  AU.addPreserved<GlobalsAAWrapperPass>();
  AU.addPreserved<SCEVAAWrapperPass>();
  AU.addRequired<ScalarEvolutionWrapperPass>();
  AU.addPreserved<ScalarEvolutionWrapperPass>();
}

/// Manually defined generic "LoopPass" dependency initialization. This is used
/// to initialize the exact set of passes from above in \c
/// getLoopAnalysisUsage. It can be used within a loop pass's initialization
/// with:
///
///   INITIALIZE_PASS_DEPENDENCY(LoopPass)
///
/// As-if "LoopPass" were a pass.
void llvm::initializeLoopPassPass(PassRegistry &Registry) {
  INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
  INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
  INITIALIZE_PASS_DEPENDENCY(LoopSimplify)
  INITIALIZE_PASS_DEPENDENCY(LCSSAWrapperPass)
  INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
  INITIALIZE_PASS_DEPENDENCY(BasicAAWrapperPass)
  INITIALIZE_PASS_DEPENDENCY(GlobalsAAWrapperPass)
  INITIALIZE_PASS_DEPENDENCY(SCEVAAWrapperPass)
  INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
}

/// Find string metadata for loop
///
/// If it has a value (e.g. {"llvm.distribute", 1} return the value as an
/// operand or null otherwise.  If the string metadata is not found return
/// Optional's not-a-value.
Optional<const MDOperand *> llvm::findStringMetadataForLoop(const Loop *TheLoop,
                                                            StringRef Name) {
  MDNode *MD = findOptionMDForLoop(TheLoop, Name);
  if (!MD)
    return None;
  switch (MD->getNumOperands()) {
  case 1:
    return nullptr;
  case 2:
    return &MD->getOperand(1);
  default:
    llvm_unreachable("loop metadata has 0 or 1 operand");
  }
}

static Optional<bool> getOptionalBoolLoopAttribute(const Loop *TheLoop,
                                                   StringRef Name) {
  MDNode *MD = findOptionMDForLoop(TheLoop, Name);
  if (!MD)
    return None;
  switch (MD->getNumOperands()) {
  case 1:
    // When the value is absent it is interpreted as 'attribute set'.
    return true;
  case 2:
    if (ConstantInt *IntMD =
            mdconst::extract_or_null<ConstantInt>(MD->getOperand(1).get()))
      return IntMD->getZExtValue();
    return true;
  }
  llvm_unreachable("unexpected number of options");
}

static bool getBooleanLoopAttribute(const Loop *TheLoop, StringRef Name) {
  return getOptionalBoolLoopAttribute(TheLoop, Name).getValueOr(false);
}

llvm::Optional<int> llvm::getOptionalIntLoopAttribute(Loop *TheLoop,
                                                      StringRef Name) {
  const MDOperand *AttrMD =
      findStringMetadataForLoop(TheLoop, Name).getValueOr(nullptr);
  if (!AttrMD)
    return None;

  ConstantInt *IntMD = mdconst::extract_or_null<ConstantInt>(AttrMD->get());
  if (!IntMD)
    return None;

  return IntMD->getSExtValue();
}

Optional<MDNode *> llvm::makeFollowupLoopID(
    MDNode *OrigLoopID, ArrayRef<StringRef> FollowupOptions,
    const char *InheritOptionsExceptPrefix, bool AlwaysNew) {
  if (!OrigLoopID) {
    if (AlwaysNew)
      return nullptr;
    return None;
  }

  assert(OrigLoopID->getOperand(0) == OrigLoopID);

  bool InheritAllAttrs = !InheritOptionsExceptPrefix;
  bool InheritSomeAttrs =
      InheritOptionsExceptPrefix && InheritOptionsExceptPrefix[0] != '\0';
  SmallVector<Metadata *, 8> MDs;
  MDs.push_back(nullptr);

  bool Changed = false;
  if (InheritAllAttrs || InheritSomeAttrs) {
    for (const MDOperand &Existing : drop_begin(OrigLoopID->operands(), 1)) {
      MDNode *Op = cast<MDNode>(Existing.get());

      auto InheritThisAttribute = [InheritSomeAttrs,
                                   InheritOptionsExceptPrefix](MDNode *Op) {
        if (!InheritSomeAttrs)
          return false;

        // Skip malformatted attribute metadata nodes.
        if (Op->getNumOperands() == 0)
          return true;
        Metadata *NameMD = Op->getOperand(0).get();
        if (!isa<MDString>(NameMD))
          return true;
        StringRef AttrName = cast<MDString>(NameMD)->getString();

        // Do not inherit excluded attributes.
        return !AttrName.startswith(InheritOptionsExceptPrefix);
      };

      if (InheritThisAttribute(Op))
        MDs.push_back(Op);
      else
        Changed = true;
    }
  } else {
    // Modified if we dropped at least one attribute.
    Changed = OrigLoopID->getNumOperands() > 1;
  }

  bool HasAnyFollowup = false;
  for (StringRef OptionName : FollowupOptions) {
    MDNode *FollowupNode = findOptionMDForLoopID(OrigLoopID, OptionName);
    if (!FollowupNode)
      continue;

    HasAnyFollowup = true;
    for (const MDOperand &Option : drop_begin(FollowupNode->operands(), 1)) {
      MDs.push_back(Option.get());
      Changed = true;
    }
  }

  // Attributes of the followup loop not specified explicity, so signal to the
  // transformation pass to add suitable attributes.
  if (!AlwaysNew && !HasAnyFollowup)
    return None;

  // If no attributes were added or remove, the previous loop Id can be reused.
  if (!AlwaysNew && !Changed)
    return OrigLoopID;

  // No attributes is equivalent to having no !llvm.loop metadata at all.
  if (MDs.size() == 1)
    return nullptr;

  // Build the new loop ID.
  MDTuple *FollowupLoopID = MDNode::get(OrigLoopID->getContext(), MDs);
  FollowupLoopID->replaceOperandWith(0, FollowupLoopID);
  return FollowupLoopID;
}

bool llvm::hasDisableAllTransformsHint(const Loop *L) {
  return getBooleanLoopAttribute(L, LLVMLoopDisableNonforced);
}

TransformationMode llvm::hasUnrollTransformation(Loop *L) {
  if (getBooleanLoopAttribute(L, "llvm.loop.unroll.disable"))
    return TM_SuppressedByUser;

  Optional<int> Count =
      getOptionalIntLoopAttribute(L, "llvm.loop.unroll.count");
  if (Count.hasValue())
    return Count.getValue() == 1 ? TM_SuppressedByUser : TM_ForcedByUser;

  if (getBooleanLoopAttribute(L, "llvm.loop.unroll.enable"))
    return TM_ForcedByUser;

  if (getBooleanLoopAttribute(L, "llvm.loop.unroll.full"))
    return TM_ForcedByUser;

  if (hasDisableAllTransformsHint(L))
    return TM_Disable;

  return TM_Unspecified;
}

TransformationMode llvm::hasUnrollAndJamTransformation(Loop *L) {
  if (getBooleanLoopAttribute(L, "llvm.loop.unroll_and_jam.disable"))
    return TM_SuppressedByUser;

  Optional<int> Count =
      getOptionalIntLoopAttribute(L, "llvm.loop.unroll_and_jam.count");
  if (Count.hasValue())
    return Count.getValue() == 1 ? TM_SuppressedByUser : TM_ForcedByUser;

  if (getBooleanLoopAttribute(L, "llvm.loop.unroll_and_jam.enable"))
    return TM_ForcedByUser;

  if (hasDisableAllTransformsHint(L))
    return TM_Disable;

  return TM_Unspecified;
}

TransformationMode llvm::hasVectorizeTransformation(Loop *L) {
  Optional<bool> Enable =
      getOptionalBoolLoopAttribute(L, "llvm.loop.vectorize.enable");

  if (Enable == false)
    return TM_SuppressedByUser;

  Optional<int> VectorizeWidth =
      getOptionalIntLoopAttribute(L, "llvm.loop.vectorize.width");
  Optional<int> InterleaveCount =
      getOptionalIntLoopAttribute(L, "llvm.loop.interleave.count");

  // 'Forcing' vector width and interleave count to one effectively disables
  // this tranformation.
  if (Enable == true && VectorizeWidth == 1 && InterleaveCount == 1)
    return TM_SuppressedByUser;

  if (getBooleanLoopAttribute(L, "llvm.loop.isvectorized"))
    return TM_Disable;

  if (Enable == true)
    return TM_ForcedByUser;

  if (VectorizeWidth == 1 && InterleaveCount == 1)
    return TM_Disable;

  if (VectorizeWidth > 1 || InterleaveCount > 1)
    return TM_Enable;

  if (hasDisableAllTransformsHint(L))
    return TM_Disable;

  return TM_Unspecified;
}

TransformationMode llvm::hasDistributeTransformation(Loop *L) {
  if (getBooleanLoopAttribute(L, "llvm.loop.distribute.enable"))
    return TM_ForcedByUser;

  if (hasDisableAllTransformsHint(L))
    return TM_Disable;

  return TM_Unspecified;
}

TransformationMode llvm::hasLICMVersioningTransformation(Loop *L) {
  if (getBooleanLoopAttribute(L, "llvm.loop.licm_versioning.disable"))
    return TM_SuppressedByUser;

  if (hasDisableAllTransformsHint(L))
    return TM_Disable;

  return TM_Unspecified;
}

/// Does a BFS from a given node to all of its children inside a given loop.
/// The returned vector of nodes includes the starting point.
SmallVector<DomTreeNode *, 16>
llvm::collectChildrenInLoop(DomTreeNode *N, const Loop *CurLoop) {
  SmallVector<DomTreeNode *, 16> Worklist;
  auto AddRegionToWorklist = [&](DomTreeNode *DTN) {
    // Only include subregions in the top level loop.
    BasicBlock *BB = DTN->getBlock();
    if (CurLoop->contains(BB))
      Worklist.push_back(DTN);
  };

  AddRegionToWorklist(N);

  for (size_t I = 0; I < Worklist.size(); I++)
    for (DomTreeNode *Child : Worklist[I]->getChildren())
      AddRegionToWorklist(Child);

  return Worklist;
}

void llvm::deleteDeadLoop(Loop *L, DominatorTree *DT = nullptr,
                          ScalarEvolution *SE = nullptr,
                          LoopInfo *LI = nullptr) {
  assert((!DT || L->isLCSSAForm(*DT)) && "Expected LCSSA!");
  auto *Preheader = L->getLoopPreheader();
  assert(Preheader && "Preheader should exist!");

  // Now that we know the removal is safe, remove the loop by changing the
  // branch from the preheader to go to the single exit block.
  //
  // Because we're deleting a large chunk of code at once, the sequence in which
  // we remove things is very important to avoid invalidation issues.

  // Tell ScalarEvolution that the loop is deleted. Do this before
  // deleting the loop so that ScalarEvolution can look at the loop
  // to determine what it needs to clean up.
  if (SE)
    SE->forgetLoop(L);

  auto *ExitBlock = L->getUniqueExitBlock();
  assert(ExitBlock && "Should have a unique exit block!");
  assert(L->hasDedicatedExits() && "Loop should have dedicated exits!");

  auto *OldBr = dyn_cast<BranchInst>(Preheader->getTerminator());
  assert(OldBr && "Preheader must end with a branch");
  assert(OldBr->isUnconditional() && "Preheader must have a single successor");
  // Connect the preheader to the exit block. Keep the old edge to the header
  // around to perform the dominator tree update in two separate steps
  // -- #1 insertion of the edge preheader -> exit and #2 deletion of the edge
  // preheader -> header.
  //
  //
  // 0.  Preheader          1.  Preheader           2.  Preheader
  //        |                    |   |                   |
  //        V                    |   V                   |
  //      Header <--\            | Header <--\           | Header <--\
  //       |  |     |            |  |  |     |           |  |  |     |
  //       |  V     |            |  |  V     |           |  |  V     |
  //       | Body --/            |  | Body --/           |  | Body --/
  //       V                     V  V                    V  V
  //      Exit                   Exit                    Exit
  //
  // By doing this is two separate steps we can perform the dominator tree
  // update without using the batch update API.
  //
  // Even when the loop is never executed, we cannot remove the edge from the
  // source block to the exit block. Consider the case where the unexecuted loop
  // branches back to an outer loop. If we deleted the loop and removed the edge
  // coming to this inner loop, this will break the outer loop structure (by
  // deleting the backedge of the outer loop). If the outer loop is indeed a
  // non-loop, it will be deleted in a future iteration of loop deletion pass.
  IRBuilder<> Builder(OldBr);
  Builder.CreateCondBr(Builder.getFalse(), L->getHeader(), ExitBlock);
  // Remove the old branch. The conditional branch becomes a new terminator.
  OldBr->eraseFromParent();

  // Rewrite phis in the exit block to get their inputs from the Preheader
  // instead of the exiting block.
  for (PHINode &P : ExitBlock->phis()) {
    // Set the zero'th element of Phi to be from the preheader and remove all
    // other incoming values. Given the loop has dedicated exits, all other
    // incoming values must be from the exiting blocks.
    int PredIndex = 0;
    P.setIncomingBlock(PredIndex, Preheader);
    // Removes all incoming values from all other exiting blocks (including
    // duplicate values from an exiting block).
    // Nuke all entries except the zero'th entry which is the preheader entry.
    // NOTE! We need to remove Incoming Values in the reverse order as done
    // below, to keep the indices valid for deletion (removeIncomingValues
    // updates getNumIncomingValues and shifts all values down into the operand
    // being deleted).
    for (unsigned i = 0, e = P.getNumIncomingValues() - 1; i != e; ++i)
      P.removeIncomingValue(e - i, false);

    assert((P.getNumIncomingValues() == 1 &&
            P.getIncomingBlock(PredIndex) == Preheader) &&
           "Should have exactly one value and that's from the preheader!");
  }

  // Disconnect the loop body by branching directly to its exit.
  Builder.SetInsertPoint(Preheader->getTerminator());
  Builder.CreateBr(ExitBlock);
  // Remove the old branch.
  Preheader->getTerminator()->eraseFromParent();

  DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Eager);
  if (DT) {
    // Update the dominator tree by informing it about the new edge from the
    // preheader to the exit.
    DTU.insertEdge(Preheader, ExitBlock);
    // Inform the dominator tree about the removed edge.
    DTU.deleteEdge(Preheader, L->getHeader());
  }

  // Use a map to unique and a vector to guarantee deterministic ordering.
  llvm::SmallDenseSet<std::pair<DIVariable *, DIExpression *>, 4> DeadDebugSet;
  llvm::SmallVector<DbgVariableIntrinsic *, 4> DeadDebugInst;

  // Given LCSSA form is satisfied, we should not have users of instructions
  // within the dead loop outside of the loop. However, LCSSA doesn't take
  // unreachable uses into account. We handle them here.
  // We could do it after drop all references (in this case all users in the
  // loop will be already eliminated and we have less work to do but according
  // to API doc of User::dropAllReferences only valid operation after dropping
  // references, is deletion. So let's substitute all usages of
  // instruction from the loop with undef value of corresponding type first.
  for (auto *Block : L->blocks())
    for (Instruction &I : *Block) {
      auto *Undef = UndefValue::get(I.getType());
      for (Value::use_iterator UI = I.use_begin(), E = I.use_end(); UI != E;) {
        Use &U = *UI;
        ++UI;
        if (auto *Usr = dyn_cast<Instruction>(U.getUser()))
          if (L->contains(Usr->getParent()))
            continue;
        // If we have a DT then we can check that uses outside a loop only in
        // unreachable block.
        if (DT)
          assert(!DT->isReachableFromEntry(U) &&
                 "Unexpected user in reachable block");
        U.set(Undef);
      }
      auto *DVI = dyn_cast<DbgVariableIntrinsic>(&I);
      if (!DVI)
        continue;
      auto Key = DeadDebugSet.find({DVI->getVariable(), DVI->getExpression()});
      if (Key != DeadDebugSet.end())
        continue;
      DeadDebugSet.insert({DVI->getVariable(), DVI->getExpression()});
      DeadDebugInst.push_back(DVI);
    }

  // After the loop has been deleted all the values defined and modified
  // inside the loop are going to be unavailable.
  // Since debug values in the loop have been deleted, inserting an undef
  // dbg.value truncates the range of any dbg.value before the loop where the
  // loop used to be. This is particularly important for constant values.
  DIBuilder DIB(*ExitBlock->getModule());
  for (auto *DVI : DeadDebugInst)
    DIB.insertDbgValueIntrinsic(
        UndefValue::get(Builder.getInt32Ty()), DVI->getVariable(),
        DVI->getExpression(), DVI->getDebugLoc(), ExitBlock->getFirstNonPHI());

  // Remove the block from the reference counting scheme, so that we can
  // delete it freely later.
  for (auto *Block : L->blocks())
    Block->dropAllReferences();

  if (LI) {
    // Erase the instructions and the blocks without having to worry
    // about ordering because we already dropped the references.
    // NOTE: This iteration is safe because erasing the block does not remove
    // its entry from the loop's block list.  We do that in the next section.
    for (Loop::block_iterator LpI = L->block_begin(), LpE = L->block_end();
         LpI != LpE; ++LpI)
      (*LpI)->eraseFromParent();

    // Finally, the blocks from loopinfo.  This has to happen late because
    // otherwise our loop iterators won't work.

    SmallPtrSet<BasicBlock *, 8> blocks;
    blocks.insert(L->block_begin(), L->block_end());
    for (BasicBlock *BB : blocks)
      LI->removeBlock(BB);

    // The last step is to update LoopInfo now that we've eliminated this loop.
    LI->erase(L);
  }
}

Optional<unsigned> llvm::getLoopEstimatedTripCount(Loop *L) {
  // Only support loops with a unique exiting block, and a latch.
  if (!L->getExitingBlock())
    return None;

  // Get the branch weights for the loop's backedge.
  BranchInst *LatchBR =
      dyn_cast<BranchInst>(L->getLoopLatch()->getTerminator());
  if (!LatchBR || LatchBR->getNumSuccessors() != 2)
    return None;

  assert((LatchBR->getSuccessor(0) == L->getHeader() ||
          LatchBR->getSuccessor(1) == L->getHeader()) &&
         "At least one edge out of the latch must go to the header");

  // To estimate the number of times the loop body was executed, we want to
  // know the number of times the backedge was taken, vs. the number of times
  // we exited the loop.
  uint64_t TrueVal, FalseVal;
  if (!LatchBR->extractProfMetadata(TrueVal, FalseVal))
    return None;

  if (!TrueVal || !FalseVal)
    return 0;

  // Divide the count of the backedge by the count of the edge exiting the loop,
  // rounding to nearest.
  if (LatchBR->getSuccessor(0) == L->getHeader())
    return (TrueVal + (FalseVal / 2)) / FalseVal;
  else
    return (FalseVal + (TrueVal / 2)) / TrueVal;
}

bool llvm::hasIterationCountInvariantInParent(Loop *InnerLoop,
                                              ScalarEvolution &SE) {
  Loop *OuterL = InnerLoop->getParentLoop();
  if (!OuterL)
    return true;

  // Get the backedge taken count for the inner loop
  BasicBlock *InnerLoopLatch = InnerLoop->getLoopLatch();
  const SCEV *InnerLoopBECountSC = SE.getExitCount(InnerLoop, InnerLoopLatch);
  if (isa<SCEVCouldNotCompute>(InnerLoopBECountSC) ||
      !InnerLoopBECountSC->getType()->isIntegerTy())
    return false;

  // Get whether count is invariant to the outer loop
  ScalarEvolution::LoopDisposition LD =
      SE.getLoopDisposition(InnerLoopBECountSC, OuterL);
  if (LD != ScalarEvolution::LoopInvariant)
    return false;

  return true;
}

/// Adds a 'fast' flag to floating point operations.
static Value *addFastMathFlag(Value *V) {
  if (isa<FPMathOperator>(V)) {
    FastMathFlags Flags;
    Flags.setFast();
    cast<Instruction>(V)->setFastMathFlags(Flags);
  }
  return V;
}

Value *llvm::createMinMaxOp(IRBuilder<> &Builder,
                            RecurrenceDescriptor::MinMaxRecurrenceKind RK,
                            Value *Left, Value *Right) {
  CmpInst::Predicate P = CmpInst::ICMP_NE;
  switch (RK) {
  default:
    llvm_unreachable("Unknown min/max recurrence kind");
  case RecurrenceDescriptor::MRK_UIntMin:
    P = CmpInst::ICMP_ULT;
    break;
  case RecurrenceDescriptor::MRK_UIntMax:
    P = CmpInst::ICMP_UGT;
    break;
  case RecurrenceDescriptor::MRK_SIntMin:
    P = CmpInst::ICMP_SLT;
    break;
  case RecurrenceDescriptor::MRK_SIntMax:
    P = CmpInst::ICMP_SGT;
    break;
  case RecurrenceDescriptor::MRK_FloatMin:
    P = CmpInst::FCMP_OLT;
    break;
  case RecurrenceDescriptor::MRK_FloatMax:
    P = CmpInst::FCMP_OGT;
    break;
  }

  // We only match FP sequences that are 'fast', so we can unconditionally
  // set it on any generated instructions.
  IRBuilder<>::FastMathFlagGuard FMFG(Builder);
  FastMathFlags FMF;
  FMF.setFast();
  Builder.setFastMathFlags(FMF);

  Value *Cmp;
  if (RK == RecurrenceDescriptor::MRK_FloatMin ||
      RK == RecurrenceDescriptor::MRK_FloatMax)
    Cmp = Builder.CreateFCmp(P, Left, Right, "rdx.minmax.cmp");
  else
    Cmp = Builder.CreateICmp(P, Left, Right, "rdx.minmax.cmp");

  Value *Select = Builder.CreateSelect(Cmp, Left, Right, "rdx.minmax.select");
  return Select;
}

// Helper to generate an ordered reduction.
Value *
llvm::getOrderedReduction(IRBuilder<> &Builder, Value *Acc, Value *Src,
                          unsigned Op,
                          RecurrenceDescriptor::MinMaxRecurrenceKind MinMaxKind,
                          ArrayRef<Value *> RedOps) {
  unsigned VF = Src->getType()->getVectorNumElements();

  // Extract and apply reduction ops in ascending order:
  // e.g. ((((Acc + Scl[0]) + Scl[1]) + Scl[2]) + ) ... + Scl[VF-1]
  Value *Result = Acc;
  for (unsigned ExtractIdx = 0; ExtractIdx != VF; ++ExtractIdx) {
    Value *Ext =
        Builder.CreateExtractElement(Src, Builder.getInt32(ExtractIdx));

    if (Op != Instruction::ICmp && Op != Instruction::FCmp) {
      Result = Builder.CreateBinOp((Instruction::BinaryOps)Op, Result, Ext,
                                   "bin.rdx");
    } else {
      assert(MinMaxKind != RecurrenceDescriptor::MRK_Invalid &&
             "Invalid min/max");
      Result = createMinMaxOp(Builder, MinMaxKind, Result, Ext);
    }

    if (!RedOps.empty())
      propagateIRFlags(Result, RedOps);
  }

  return Result;
}

// Helper to generate a log2 shuffle reduction.
Value *
llvm::getShuffleReduction(IRBuilder<> &Builder, Value *Src, unsigned Op,
                          RecurrenceDescriptor::MinMaxRecurrenceKind MinMaxKind,
                          ArrayRef<Value *> RedOps) {
  unsigned VF = Src->getType()->getVectorNumElements();
  // VF is a power of 2 so we can emit the reduction using log2(VF) shuffles
  // and vector ops, reducing the set of values being computed by half each
  // round.
  assert(isPowerOf2_32(VF) &&
         "Reduction emission only supported for pow2 vectors!");
  Value *TmpVec = Src;
  SmallVector<Constant *, 32> ShuffleMask(VF, nullptr);
  for (unsigned i = VF; i != 1; i >>= 1) {
    // Move the upper half of the vector to the lower half.
    for (unsigned j = 0; j != i / 2; ++j)
      ShuffleMask[j] = Builder.getInt32(i / 2 + j);

    // Fill the rest of the mask with undef.
    std::fill(&ShuffleMask[i / 2], ShuffleMask.end(),
              UndefValue::get(Builder.getInt32Ty()));

    Value *Shuf = Builder.CreateShuffleVector(
        TmpVec, UndefValue::get(TmpVec->getType()),
        ConstantVector::get(ShuffleMask), "rdx.shuf");

    if (Op != Instruction::ICmp && Op != Instruction::FCmp) {
      // Floating point operations had to be 'fast' to enable the reduction.
      TmpVec = addFastMathFlag(Builder.CreateBinOp((Instruction::BinaryOps)Op,
                                                   TmpVec, Shuf, "bin.rdx"));
    } else {
      assert(MinMaxKind != RecurrenceDescriptor::MRK_Invalid &&
             "Invalid min/max");
      TmpVec = createMinMaxOp(Builder, MinMaxKind, TmpVec, Shuf);
    }
    if (!RedOps.empty())
      propagateIRFlags(TmpVec, RedOps);
  }
  // The result is in the first element of the vector.
  return Builder.CreateExtractElement(TmpVec, Builder.getInt32(0));
}

/// Create a simple vector reduction specified by an opcode and some
/// flags (if generating min/max reductions).
Value *llvm::createSimpleTargetReduction(
    IRBuilder<> &Builder, const TargetTransformInfo *TTI, unsigned Opcode,
    Value *Src, TargetTransformInfo::ReductionFlags Flags,
    ArrayRef<Value *> RedOps) {
  assert(isa<VectorType>(Src->getType()) && "Type must be a vector");

  Value *ScalarUdf = UndefValue::get(Src->getType()->getVectorElementType());
  std::function<Value *()> BuildFunc;
  using RD = RecurrenceDescriptor;
  RD::MinMaxRecurrenceKind MinMaxKind = RD::MRK_Invalid;
  // TODO: Support creating ordered reductions.
  FastMathFlags FMFFast;
  FMFFast.setFast();

  switch (Opcode) {
  case Instruction::Add:
    BuildFunc = [&]() { return Builder.CreateAddReduce(Src); };
    break;
  case Instruction::Mul:
    BuildFunc = [&]() { return Builder.CreateMulReduce(Src); };
    break;
  case Instruction::And:
    BuildFunc = [&]() { return Builder.CreateAndReduce(Src); };
    break;
  case Instruction::Or:
    BuildFunc = [&]() { return Builder.CreateOrReduce(Src); };
    break;
  case Instruction::Xor:
    BuildFunc = [&]() { return Builder.CreateXorReduce(Src); };
    break;
  case Instruction::FAdd:
    BuildFunc = [&]() {
      auto Rdx = Builder.CreateFAddReduce(ScalarUdf, Src);
      cast<CallInst>(Rdx)->setFastMathFlags(FMFFast);
      return Rdx;
    };
    break;
  case Instruction::FMul:
    BuildFunc = [&]() {
      auto Rdx = Builder.CreateFMulReduce(ScalarUdf, Src);
      cast<CallInst>(Rdx)->setFastMathFlags(FMFFast);
      return Rdx;
    };
    break;
  case Instruction::ICmp:
    if (Flags.IsMaxOp) {
      MinMaxKind = Flags.IsSigned ? RD::MRK_SIntMax : RD::MRK_UIntMax;
      BuildFunc = [&]() {
        return Builder.CreateIntMaxReduce(Src, Flags.IsSigned);
      };
    } else {
      MinMaxKind = Flags.IsSigned ? RD::MRK_SIntMin : RD::MRK_UIntMin;
      BuildFunc = [&]() {
        return Builder.CreateIntMinReduce(Src, Flags.IsSigned);
      };
    }
    break;
  case Instruction::FCmp:
    if (Flags.IsMaxOp) {
      MinMaxKind = RD::MRK_FloatMax;
      BuildFunc = [&]() { return Builder.CreateFPMaxReduce(Src, Flags.NoNaN); };
    } else {
      MinMaxKind = RD::MRK_FloatMin;
      BuildFunc = [&]() { return Builder.CreateFPMinReduce(Src, Flags.NoNaN); };
    }
    break;
  default:
    llvm_unreachable("Unhandled opcode");
    break;
  }
  if (TTI->useReductionIntrinsic(Opcode, Src->getType(), Flags))
    return BuildFunc();
  return getShuffleReduction(Builder, Src, Opcode, MinMaxKind, RedOps);
}

/// Create a vector reduction using a given recurrence descriptor.
Value *llvm::createTargetReduction(IRBuilder<> &B,
                                   const TargetTransformInfo *TTI,
                                   RecurrenceDescriptor &Desc, Value *Src,
                                   bool NoNaN) {
  // TODO: Support in-order reductions based on the recurrence descriptor.
  using RD = RecurrenceDescriptor;
  RD::RecurrenceKind RecKind = Desc.getRecurrenceKind();
  TargetTransformInfo::ReductionFlags Flags;
  Flags.NoNaN = NoNaN;
  switch (RecKind) {
  case RD::RK_FloatAdd:
    return createSimpleTargetReduction(B, TTI, Instruction::FAdd, Src, Flags);
  case RD::RK_FloatMult:
    return createSimpleTargetReduction(B, TTI, Instruction::FMul, Src, Flags);
  case RD::RK_IntegerAdd:
    return createSimpleTargetReduction(B, TTI, Instruction::Add, Src, Flags);
  case RD::RK_IntegerMult:
    return createSimpleTargetReduction(B, TTI, Instruction::Mul, Src, Flags);
  case RD::RK_IntegerAnd:
    return createSimpleTargetReduction(B, TTI, Instruction::And, Src, Flags);
  case RD::RK_IntegerOr:
    return createSimpleTargetReduction(B, TTI, Instruction::Or, Src, Flags);
  case RD::RK_IntegerXor:
    return createSimpleTargetReduction(B, TTI, Instruction::Xor, Src, Flags);
  case RD::RK_IntegerMinMax: {
    RD::MinMaxRecurrenceKind MMKind = Desc.getMinMaxRecurrenceKind();
    Flags.IsMaxOp = (MMKind == RD::MRK_SIntMax || MMKind == RD::MRK_UIntMax);
    Flags.IsSigned = (MMKind == RD::MRK_SIntMax || MMKind == RD::MRK_SIntMin);
    return createSimpleTargetReduction(B, TTI, Instruction::ICmp, Src, Flags);
  }
  case RD::RK_FloatMinMax: {
    Flags.IsMaxOp = Desc.getMinMaxRecurrenceKind() == RD::MRK_FloatMax;
    return createSimpleTargetReduction(B, TTI, Instruction::FCmp, Src, Flags);
  }
  default:
    llvm_unreachable("Unhandled RecKind");
  }
}

void llvm::propagateIRFlags(Value *I, ArrayRef<Value *> VL, Value *OpValue) {
  auto *VecOp = dyn_cast<Instruction>(I);
  if (!VecOp)
    return;
  auto *Intersection = (OpValue == nullptr) ? dyn_cast<Instruction>(VL[0])
                                            : dyn_cast<Instruction>(OpValue);
  if (!Intersection)
    return;
  const unsigned Opcode = Intersection->getOpcode();
  VecOp->copyIRFlags(Intersection);
  for (auto *V : VL) {
    auto *Instr = dyn_cast<Instruction>(V);
    if (!Instr)
      continue;
    if (OpValue == nullptr || Opcode == Instr->getOpcode())
      VecOp->andIRFlags(V);
  }
}

bool llvm::isKnownNegativeInLoop(const SCEV *S, const Loop *L,
                                 ScalarEvolution &SE) {
  const SCEV *Zero = SE.getZero(S->getType());
  return SE.isAvailableAtLoopEntry(S, L) &&
         SE.isLoopEntryGuardedByCond(L, ICmpInst::ICMP_SLT, S, Zero);
}

bool llvm::isKnownNonNegativeInLoop(const SCEV *S, const Loop *L,
                                    ScalarEvolution &SE) {
  const SCEV *Zero = SE.getZero(S->getType());
  return SE.isAvailableAtLoopEntry(S, L) &&
         SE.isLoopEntryGuardedByCond(L, ICmpInst::ICMP_SGE, S, Zero);
}

bool llvm::cannotBeMinInLoop(const SCEV *S, const Loop *L, ScalarEvolution &SE,
                             bool Signed) {
  unsigned BitWidth = cast<IntegerType>(S->getType())->getBitWidth();
  APInt Min = Signed ? APInt::getSignedMinValue(BitWidth) :
    APInt::getMinValue(BitWidth);
  auto Predicate = Signed ? ICmpInst::ICMP_SGT : ICmpInst::ICMP_UGT;
  return SE.isAvailableAtLoopEntry(S, L) &&
         SE.isLoopEntryGuardedByCond(L, Predicate, S,
                                     SE.getConstant(Min));
}

bool llvm::cannotBeMaxInLoop(const SCEV *S, const Loop *L, ScalarEvolution &SE,
                             bool Signed) {
  unsigned BitWidth = cast<IntegerType>(S->getType())->getBitWidth();
  APInt Max = Signed ? APInt::getSignedMaxValue(BitWidth) :
    APInt::getMaxValue(BitWidth);
  auto Predicate = Signed ? ICmpInst::ICMP_SLT : ICmpInst::ICMP_ULT;
  return SE.isAvailableAtLoopEntry(S, L) &&
         SE.isLoopEntryGuardedByCond(L, Predicate, S,
                                     SE.getConstant(Max));
}
