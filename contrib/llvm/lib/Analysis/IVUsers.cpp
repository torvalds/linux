//===- IVUsers.cpp - Induction Variable Users -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements bookkeeping for "interesting" users of expressions
// computed from induction variables.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/IVUsers.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
using namespace llvm;

#define DEBUG_TYPE "iv-users"

AnalysisKey IVUsersAnalysis::Key;

IVUsers IVUsersAnalysis::run(Loop &L, LoopAnalysisManager &AM,
                             LoopStandardAnalysisResults &AR) {
  return IVUsers(&L, &AR.AC, &AR.LI, &AR.DT, &AR.SE);
}

char IVUsersWrapperPass::ID = 0;
INITIALIZE_PASS_BEGIN(IVUsersWrapperPass, "iv-users",
                      "Induction Variable Users", false, true)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_END(IVUsersWrapperPass, "iv-users", "Induction Variable Users",
                    false, true)

Pass *llvm::createIVUsersPass() { return new IVUsersWrapperPass(); }

/// isInteresting - Test whether the given expression is "interesting" when
/// used by the given expression, within the context of analyzing the
/// given loop.
static bool isInteresting(const SCEV *S, const Instruction *I, const Loop *L,
                          ScalarEvolution *SE, LoopInfo *LI) {
  // An addrec is interesting if it's affine or if it has an interesting start.
  if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(S)) {
    // Keep things simple. Don't touch loop-variant strides unless they're
    // only used outside the loop and we can simplify them.
    if (AR->getLoop() == L)
      return AR->isAffine() ||
             (!L->contains(I) &&
              SE->getSCEVAtScope(AR, LI->getLoopFor(I->getParent())) != AR);
    // Otherwise recurse to see if the start value is interesting, and that
    // the step value is not interesting, since we don't yet know how to
    // do effective SCEV expansions for addrecs with interesting steps.
    return isInteresting(AR->getStart(), I, L, SE, LI) &&
          !isInteresting(AR->getStepRecurrence(*SE), I, L, SE, LI);
  }

  // An add is interesting if exactly one of its operands is interesting.
  if (const SCEVAddExpr *Add = dyn_cast<SCEVAddExpr>(S)) {
    bool AnyInterestingYet = false;
    for (const auto *Op : Add->operands())
      if (isInteresting(Op, I, L, SE, LI)) {
        if (AnyInterestingYet)
          return false;
        AnyInterestingYet = true;
      }
    return AnyInterestingYet;
  }

  // Nothing else is interesting here.
  return false;
}

/// Return true if all loop headers that dominate this block are in simplified
/// form.
static bool isSimplifiedLoopNest(BasicBlock *BB, const DominatorTree *DT,
                                 const LoopInfo *LI,
                                 SmallPtrSetImpl<Loop*> &SimpleLoopNests) {
  Loop *NearestLoop = nullptr;
  for (DomTreeNode *Rung = DT->getNode(BB);
       Rung; Rung = Rung->getIDom()) {
    BasicBlock *DomBB = Rung->getBlock();
    Loop *DomLoop = LI->getLoopFor(DomBB);
    if (DomLoop && DomLoop->getHeader() == DomBB) {
      // If the domtree walk reaches a loop with no preheader, return false.
      if (!DomLoop->isLoopSimplifyForm())
        return false;
      // If we have already checked this loop nest, stop checking.
      if (SimpleLoopNests.count(DomLoop))
        break;
      // If we have not already checked this loop nest, remember the loop
      // header nearest to BB. The nearest loop may not contain BB.
      if (!NearestLoop)
        NearestLoop = DomLoop;
    }
  }
  if (NearestLoop)
    SimpleLoopNests.insert(NearestLoop);
  return true;
}

/// IVUseShouldUsePostIncValue - We have discovered a "User" of an IV expression
/// and now we need to decide whether the user should use the preinc or post-inc
/// value.  If this user should use the post-inc version of the IV, return true.
///
/// Choosing wrong here can break dominance properties (if we choose to use the
/// post-inc value when we cannot) or it can end up adding extra live-ranges to
/// the loop, resulting in reg-reg copies (if we use the pre-inc value when we
/// should use the post-inc value).
static bool IVUseShouldUsePostIncValue(Instruction *User, Value *Operand,
                                       const Loop *L, DominatorTree *DT) {
  // If the user is in the loop, use the preinc value.
  if (L->contains(User))
    return false;

  BasicBlock *LatchBlock = L->getLoopLatch();
  if (!LatchBlock)
    return false;

  // Ok, the user is outside of the loop.  If it is dominated by the latch
  // block, use the post-inc value.
  if (DT->dominates(LatchBlock, User->getParent()))
    return true;

  // There is one case we have to be careful of: PHI nodes.  These little guys
  // can live in blocks that are not dominated by the latch block, but (since
  // their uses occur in the predecessor block, not the block the PHI lives in)
  // should still use the post-inc value.  Check for this case now.
  PHINode *PN = dyn_cast<PHINode>(User);
  if (!PN || !Operand)
    return false; // not a phi, not dominated by latch block.

  // Look at all of the uses of Operand by the PHI node.  If any use corresponds
  // to a block that is not dominated by the latch block, give up and use the
  // preincremented value.
  for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i)
    if (PN->getIncomingValue(i) == Operand &&
        !DT->dominates(LatchBlock, PN->getIncomingBlock(i)))
      return false;

  // Okay, all uses of Operand by PN are in predecessor blocks that really are
  // dominated by the latch block.  Use the post-incremented value.
  return true;
}

/// AddUsersImpl - Inspect the specified instruction.  If it is a
/// reducible SCEV, recursively add its users to the IVUsesByStride set and
/// return true.  Otherwise, return false.
bool IVUsers::AddUsersImpl(Instruction *I,
                           SmallPtrSetImpl<Loop*> &SimpleLoopNests) {
  const DataLayout &DL = I->getModule()->getDataLayout();

  // Add this IV user to the Processed set before returning false to ensure that
  // all IV users are members of the set. See IVUsers::isIVUserOrOperand.
  if (!Processed.insert(I).second)
    return true;    // Instruction already handled.

  if (!SE->isSCEVable(I->getType()))
    return false;   // Void and FP expressions cannot be reduced.

  // IVUsers is used by LSR which assumes that all SCEV expressions are safe to
  // pass to SCEVExpander. Expressions are not safe to expand if they represent
  // operations that are not safe to speculate, namely integer division.
  if (!isa<PHINode>(I) && !isSafeToSpeculativelyExecute(I))
    return false;

  // LSR is not APInt clean, do not touch integers bigger than 64-bits.
  // Also avoid creating IVs of non-native types. For example, we don't want a
  // 64-bit IV in 32-bit code just because the loop has one 64-bit cast.
  uint64_t Width = SE->getTypeSizeInBits(I->getType());
  if (Width > 64 || !DL.isLegalInteger(Width))
    return false;

  // Don't attempt to promote ephemeral values to indvars. They will be removed
  // later anyway.
  if (EphValues.count(I))
    return false;

  // Get the symbolic expression for this instruction.
  const SCEV *ISE = SE->getSCEV(I);

  // If we've come to an uninteresting expression, stop the traversal and
  // call this a user.
  if (!isInteresting(ISE, I, L, SE, LI))
    return false;

  SmallPtrSet<Instruction *, 4> UniqueUsers;
  for (Use &U : I->uses()) {
    Instruction *User = cast<Instruction>(U.getUser());
    if (!UniqueUsers.insert(User).second)
      continue;

    // Do not infinitely recurse on PHI nodes.
    if (isa<PHINode>(User) && Processed.count(User))
      continue;

    // Only consider IVUsers that are dominated by simplified loop
    // headers. Otherwise, SCEVExpander will crash.
    BasicBlock *UseBB = User->getParent();
    // A phi's use is live out of its predecessor block.
    if (PHINode *PHI = dyn_cast<PHINode>(User)) {
      unsigned OperandNo = U.getOperandNo();
      unsigned ValNo = PHINode::getIncomingValueNumForOperand(OperandNo);
      UseBB = PHI->getIncomingBlock(ValNo);
    }
    if (!isSimplifiedLoopNest(UseBB, DT, LI, SimpleLoopNests))
      return false;

    // Descend recursively, but not into PHI nodes outside the current loop.
    // It's important to see the entire expression outside the loop to get
    // choices that depend on addressing mode use right, although we won't
    // consider references outside the loop in all cases.
    // If User is already in Processed, we don't want to recurse into it again,
    // but do want to record a second reference in the same instruction.
    bool AddUserToIVUsers = false;
    if (LI->getLoopFor(User->getParent()) != L) {
      if (isa<PHINode>(User) || Processed.count(User) ||
          !AddUsersImpl(User, SimpleLoopNests)) {
        LLVM_DEBUG(dbgs() << "FOUND USER in other loop: " << *User << '\n'
                          << "   OF SCEV: " << *ISE << '\n');
        AddUserToIVUsers = true;
      }
    } else if (Processed.count(User) || !AddUsersImpl(User, SimpleLoopNests)) {
      LLVM_DEBUG(dbgs() << "FOUND USER: " << *User << '\n'
                        << "   OF SCEV: " << *ISE << '\n');
      AddUserToIVUsers = true;
    }

    if (AddUserToIVUsers) {
      // Okay, we found a user that we cannot reduce.
      IVStrideUse &NewUse = AddUser(User, I);
      // Autodetect the post-inc loop set, populating NewUse.PostIncLoops.
      // The regular return value here is discarded; instead of recording
      // it, we just recompute it when we need it.
      const SCEV *OriginalISE = ISE;

      auto NormalizePred = [&](const SCEVAddRecExpr *AR) {
        auto *L = AR->getLoop();
        bool Result = IVUseShouldUsePostIncValue(User, I, L, DT);
        if (Result)
          NewUse.PostIncLoops.insert(L);
        return Result;
      };

      ISE = normalizeForPostIncUseIf(ISE, NormalizePred, *SE);

      // PostIncNormalization effectively simplifies the expression under
      // pre-increment assumptions. Those assumptions (no wrapping) might not
      // hold for the post-inc value. Catch such cases by making sure the
      // transformation is invertible.
      if (OriginalISE != ISE) {
        const SCEV *DenormalizedISE =
            denormalizeForPostIncUse(ISE, NewUse.PostIncLoops, *SE);

        // If we normalized the expression, but denormalization doesn't give the
        // original one, discard this user.
        if (OriginalISE != DenormalizedISE) {
          LLVM_DEBUG(dbgs()
                     << "   DISCARDING (NORMALIZATION ISN'T INVERTIBLE): "
                     << *ISE << '\n');
          IVUses.pop_back();
          return false;
        }
      }
      LLVM_DEBUG(if (SE->getSCEV(I) != ISE) dbgs()
                 << "   NORMALIZED TO: " << *ISE << '\n');
    }
  }
  return true;
}

bool IVUsers::AddUsersIfInteresting(Instruction *I) {
  // SCEVExpander can only handle users that are dominated by simplified loop
  // entries. Keep track of all loops that are only dominated by other simple
  // loops so we don't traverse the domtree for each user.
  SmallPtrSet<Loop*,16> SimpleLoopNests;

  return AddUsersImpl(I, SimpleLoopNests);
}

IVStrideUse &IVUsers::AddUser(Instruction *User, Value *Operand) {
  IVUses.push_back(new IVStrideUse(this, User, Operand));
  return IVUses.back();
}

IVUsers::IVUsers(Loop *L, AssumptionCache *AC, LoopInfo *LI, DominatorTree *DT,
                 ScalarEvolution *SE)
    : L(L), AC(AC), LI(LI), DT(DT), SE(SE), IVUses() {
  // Collect ephemeral values so that AddUsersIfInteresting skips them.
  EphValues.clear();
  CodeMetrics::collectEphemeralValues(L, AC, EphValues);

  // Find all uses of induction variables in this loop, and categorize
  // them by stride.  Start by finding all of the PHI nodes in the header for
  // this loop.  If they are induction variables, inspect their uses.
  for (BasicBlock::iterator I = L->getHeader()->begin(); isa<PHINode>(I); ++I)
    (void)AddUsersIfInteresting(&*I);
}

void IVUsers::print(raw_ostream &OS, const Module *M) const {
  OS << "IV Users for loop ";
  L->getHeader()->printAsOperand(OS, false);
  if (SE->hasLoopInvariantBackedgeTakenCount(L)) {
    OS << " with backedge-taken count " << *SE->getBackedgeTakenCount(L);
  }
  OS << ":\n";

  for (const IVStrideUse &IVUse : IVUses) {
    OS << "  ";
    IVUse.getOperandValToReplace()->printAsOperand(OS, false);
    OS << " = " << *getReplacementExpr(IVUse);
    for (auto PostIncLoop : IVUse.PostIncLoops) {
      OS << " (post-inc with loop ";
      PostIncLoop->getHeader()->printAsOperand(OS, false);
      OS << ")";
    }
    OS << " in  ";
    if (IVUse.getUser())
      IVUse.getUser()->print(OS);
    else
      OS << "Printing <null> User";
    OS << '\n';
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void IVUsers::dump() const { print(dbgs()); }
#endif

void IVUsers::releaseMemory() {
  Processed.clear();
  IVUses.clear();
}

IVUsersWrapperPass::IVUsersWrapperPass() : LoopPass(ID) {
  initializeIVUsersWrapperPassPass(*PassRegistry::getPassRegistry());
}

void IVUsersWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<AssumptionCacheTracker>();
  AU.addRequired<LoopInfoWrapperPass>();
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addRequired<ScalarEvolutionWrapperPass>();
  AU.setPreservesAll();
}

bool IVUsersWrapperPass::runOnLoop(Loop *L, LPPassManager &LPM) {
  auto *AC = &getAnalysis<AssumptionCacheTracker>().getAssumptionCache(
      *L->getHeader()->getParent());
  auto *LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  auto *DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  auto *SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();

  IU.reset(new IVUsers(L, AC, LI, DT, SE));
  return false;
}

void IVUsersWrapperPass::print(raw_ostream &OS, const Module *M) const {
  IU->print(OS, M);
}

void IVUsersWrapperPass::releaseMemory() { IU->releaseMemory(); }

/// getReplacementExpr - Return a SCEV expression which computes the
/// value of the OperandValToReplace.
const SCEV *IVUsers::getReplacementExpr(const IVStrideUse &IU) const {
  return SE->getSCEV(IU.getOperandValToReplace());
}

/// getExpr - Return the expression for the use.
const SCEV *IVUsers::getExpr(const IVStrideUse &IU) const {
  return normalizeForPostIncUse(getReplacementExpr(IU), IU.getPostIncLoops(),
                                *SE);
}

static const SCEVAddRecExpr *findAddRecForLoop(const SCEV *S, const Loop *L) {
  if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(S)) {
    if (AR->getLoop() == L)
      return AR;
    return findAddRecForLoop(AR->getStart(), L);
  }

  if (const SCEVAddExpr *Add = dyn_cast<SCEVAddExpr>(S)) {
    for (const auto *Op : Add->operands())
      if (const SCEVAddRecExpr *AR = findAddRecForLoop(Op, L))
        return AR;
    return nullptr;
  }

  return nullptr;
}

const SCEV *IVUsers::getStride(const IVStrideUse &IU, const Loop *L) const {
  if (const SCEVAddRecExpr *AR = findAddRecForLoop(getExpr(IU), L))
    return AR->getStepRecurrence(*SE);
  return nullptr;
}

void IVStrideUse::transformToPostInc(const Loop *L) {
  PostIncLoops.insert(L);
}

void IVStrideUse::deleted() {
  // Remove this user from the list.
  Parent->Processed.erase(this->getUser());
  Parent->IVUses.erase(this);
  // this now dangles!
}
