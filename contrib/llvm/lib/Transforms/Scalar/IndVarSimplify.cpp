//===- IndVarSimplify.cpp - Induction Variable Elimination ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This transformation analyzes and transforms the induction variables (and
// computations derived from them) into simpler forms suitable for subsequent
// analysis and transformation.
//
// If the trip count of a loop is computable, this pass also makes the following
// changes:
//   1. The exit condition for the loop is canonicalized to compare the
//      induction value against the exit value.  This turns loops like:
//        'for (i = 7; i*i < 1000; ++i)' into 'for (i = 0; i != 25; ++i)'
//   2. Any use outside of the loop of an expression derived from the indvar
//      is changed to compute the derived value outside of the loop, eliminating
//      the dependence on the exit value of the induction variable.  If the only
//      purpose of the loop is to compute the exit value of some derived
//      expression, this transformation will make the loop dead.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/IndVarSimplify.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/SimplifyIndVar.h"
#include <cassert>
#include <cstdint>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "indvars"

STATISTIC(NumWidened     , "Number of indvars widened");
STATISTIC(NumReplaced    , "Number of exit values replaced");
STATISTIC(NumLFTR        , "Number of loop exit tests replaced");
STATISTIC(NumElimExt     , "Number of IV sign/zero extends eliminated");
STATISTIC(NumElimIV      , "Number of congruent IVs eliminated");

// Trip count verification can be enabled by default under NDEBUG if we
// implement a strong expression equivalence checker in SCEV. Until then, we
// use the verify-indvars flag, which may assert in some cases.
static cl::opt<bool> VerifyIndvars(
  "verify-indvars", cl::Hidden,
  cl::desc("Verify the ScalarEvolution result after running indvars"));

enum ReplaceExitVal { NeverRepl, OnlyCheapRepl, AlwaysRepl };

static cl::opt<ReplaceExitVal> ReplaceExitValue(
    "replexitval", cl::Hidden, cl::init(OnlyCheapRepl),
    cl::desc("Choose the strategy to replace exit value in IndVarSimplify"),
    cl::values(clEnumValN(NeverRepl, "never", "never replace exit value"),
               clEnumValN(OnlyCheapRepl, "cheap",
                          "only replace exit value when the cost is cheap"),
               clEnumValN(AlwaysRepl, "always",
                          "always replace exit value whenever possible")));

static cl::opt<bool> UsePostIncrementRanges(
  "indvars-post-increment-ranges", cl::Hidden,
  cl::desc("Use post increment control-dependent ranges in IndVarSimplify"),
  cl::init(true));

static cl::opt<bool>
DisableLFTR("disable-lftr", cl::Hidden, cl::init(false),
            cl::desc("Disable Linear Function Test Replace optimization"));

namespace {

struct RewritePhi;

class IndVarSimplify {
  LoopInfo *LI;
  ScalarEvolution *SE;
  DominatorTree *DT;
  const DataLayout &DL;
  TargetLibraryInfo *TLI;
  const TargetTransformInfo *TTI;

  SmallVector<WeakTrackingVH, 16> DeadInsts;

  bool isValidRewrite(Value *FromVal, Value *ToVal);

  bool handleFloatingPointIV(Loop *L, PHINode *PH);
  bool rewriteNonIntegerIVs(Loop *L);

  bool simplifyAndExtend(Loop *L, SCEVExpander &Rewriter, LoopInfo *LI);

  bool canLoopBeDeleted(Loop *L, SmallVector<RewritePhi, 8> &RewritePhiSet);
  bool rewriteLoopExitValues(Loop *L, SCEVExpander &Rewriter);
  bool rewriteFirstIterationLoopExitValues(Loop *L);
  bool hasHardUserWithinLoop(const Loop *L, const Instruction *I) const;

  bool linearFunctionTestReplace(Loop *L, const SCEV *BackedgeTakenCount,
                                 PHINode *IndVar, SCEVExpander &Rewriter);

  bool sinkUnusedInvariants(Loop *L);

public:
  IndVarSimplify(LoopInfo *LI, ScalarEvolution *SE, DominatorTree *DT,
                 const DataLayout &DL, TargetLibraryInfo *TLI,
                 TargetTransformInfo *TTI)
      : LI(LI), SE(SE), DT(DT), DL(DL), TLI(TLI), TTI(TTI) {}

  bool run(Loop *L);
};

} // end anonymous namespace

/// Return true if the SCEV expansion generated by the rewriter can replace the
/// original value. SCEV guarantees that it produces the same value, but the way
/// it is produced may be illegal IR.  Ideally, this function will only be
/// called for verification.
bool IndVarSimplify::isValidRewrite(Value *FromVal, Value *ToVal) {
  // If an SCEV expression subsumed multiple pointers, its expansion could
  // reassociate the GEP changing the base pointer. This is illegal because the
  // final address produced by a GEP chain must be inbounds relative to its
  // underlying object. Otherwise basic alias analysis, among other things,
  // could fail in a dangerous way. Ultimately, SCEV will be improved to avoid
  // producing an expression involving multiple pointers. Until then, we must
  // bail out here.
  //
  // Retrieve the pointer operand of the GEP. Don't use GetUnderlyingObject
  // because it understands lcssa phis while SCEV does not.
  Value *FromPtr = FromVal;
  Value *ToPtr = ToVal;
  if (auto *GEP = dyn_cast<GEPOperator>(FromVal)) {
    FromPtr = GEP->getPointerOperand();
  }
  if (auto *GEP = dyn_cast<GEPOperator>(ToVal)) {
    ToPtr = GEP->getPointerOperand();
  }
  if (FromPtr != FromVal || ToPtr != ToVal) {
    // Quickly check the common case
    if (FromPtr == ToPtr)
      return true;

    // SCEV may have rewritten an expression that produces the GEP's pointer
    // operand. That's ok as long as the pointer operand has the same base
    // pointer. Unlike GetUnderlyingObject(), getPointerBase() will find the
    // base of a recurrence. This handles the case in which SCEV expansion
    // converts a pointer type recurrence into a nonrecurrent pointer base
    // indexed by an integer recurrence.

    // If the GEP base pointer is a vector of pointers, abort.
    if (!FromPtr->getType()->isPointerTy() || !ToPtr->getType()->isPointerTy())
      return false;

    const SCEV *FromBase = SE->getPointerBase(SE->getSCEV(FromPtr));
    const SCEV *ToBase = SE->getPointerBase(SE->getSCEV(ToPtr));
    if (FromBase == ToBase)
      return true;

    LLVM_DEBUG(dbgs() << "INDVARS: GEP rewrite bail out " << *FromBase
                      << " != " << *ToBase << "\n");

    return false;
  }
  return true;
}

/// Determine the insertion point for this user. By default, insert immediately
/// before the user. SCEVExpander or LICM will hoist loop invariants out of the
/// loop. For PHI nodes, there may be multiple uses, so compute the nearest
/// common dominator for the incoming blocks.
static Instruction *getInsertPointForUses(Instruction *User, Value *Def,
                                          DominatorTree *DT, LoopInfo *LI) {
  PHINode *PHI = dyn_cast<PHINode>(User);
  if (!PHI)
    return User;

  Instruction *InsertPt = nullptr;
  for (unsigned i = 0, e = PHI->getNumIncomingValues(); i != e; ++i) {
    if (PHI->getIncomingValue(i) != Def)
      continue;

    BasicBlock *InsertBB = PHI->getIncomingBlock(i);
    if (!InsertPt) {
      InsertPt = InsertBB->getTerminator();
      continue;
    }
    InsertBB = DT->findNearestCommonDominator(InsertPt->getParent(), InsertBB);
    InsertPt = InsertBB->getTerminator();
  }
  assert(InsertPt && "Missing phi operand");

  auto *DefI = dyn_cast<Instruction>(Def);
  if (!DefI)
    return InsertPt;

  assert(DT->dominates(DefI, InsertPt) && "def does not dominate all uses");

  auto *L = LI->getLoopFor(DefI->getParent());
  assert(!L || L->contains(LI->getLoopFor(InsertPt->getParent())));

  for (auto *DTN = (*DT)[InsertPt->getParent()]; DTN; DTN = DTN->getIDom())
    if (LI->getLoopFor(DTN->getBlock()) == L)
      return DTN->getBlock()->getTerminator();

  llvm_unreachable("DefI dominates InsertPt!");
}

//===----------------------------------------------------------------------===//
// rewriteNonIntegerIVs and helpers. Prefer integer IVs.
//===----------------------------------------------------------------------===//

/// Convert APF to an integer, if possible.
static bool ConvertToSInt(const APFloat &APF, int64_t &IntVal) {
  bool isExact = false;
  // See if we can convert this to an int64_t
  uint64_t UIntVal;
  if (APF.convertToInteger(makeMutableArrayRef(UIntVal), 64, true,
                           APFloat::rmTowardZero, &isExact) != APFloat::opOK ||
      !isExact)
    return false;
  IntVal = UIntVal;
  return true;
}

/// If the loop has floating induction variable then insert corresponding
/// integer induction variable if possible.
/// For example,
/// for(double i = 0; i < 10000; ++i)
///   bar(i)
/// is converted into
/// for(int i = 0; i < 10000; ++i)
///   bar((double)i);
bool IndVarSimplify::handleFloatingPointIV(Loop *L, PHINode *PN) {
  unsigned IncomingEdge = L->contains(PN->getIncomingBlock(0));
  unsigned BackEdge     = IncomingEdge^1;

  // Check incoming value.
  auto *InitValueVal = dyn_cast<ConstantFP>(PN->getIncomingValue(IncomingEdge));

  int64_t InitValue;
  if (!InitValueVal || !ConvertToSInt(InitValueVal->getValueAPF(), InitValue))
    return false;

  // Check IV increment. Reject this PN if increment operation is not
  // an add or increment value can not be represented by an integer.
  auto *Incr = dyn_cast<BinaryOperator>(PN->getIncomingValue(BackEdge));
  if (Incr == nullptr || Incr->getOpcode() != Instruction::FAdd) return false;

  // If this is not an add of the PHI with a constantfp, or if the constant fp
  // is not an integer, bail out.
  ConstantFP *IncValueVal = dyn_cast<ConstantFP>(Incr->getOperand(1));
  int64_t IncValue;
  if (IncValueVal == nullptr || Incr->getOperand(0) != PN ||
      !ConvertToSInt(IncValueVal->getValueAPF(), IncValue))
    return false;

  // Check Incr uses. One user is PN and the other user is an exit condition
  // used by the conditional terminator.
  Value::user_iterator IncrUse = Incr->user_begin();
  Instruction *U1 = cast<Instruction>(*IncrUse++);
  if (IncrUse == Incr->user_end()) return false;
  Instruction *U2 = cast<Instruction>(*IncrUse++);
  if (IncrUse != Incr->user_end()) return false;

  // Find exit condition, which is an fcmp.  If it doesn't exist, or if it isn't
  // only used by a branch, we can't transform it.
  FCmpInst *Compare = dyn_cast<FCmpInst>(U1);
  if (!Compare)
    Compare = dyn_cast<FCmpInst>(U2);
  if (!Compare || !Compare->hasOneUse() ||
      !isa<BranchInst>(Compare->user_back()))
    return false;

  BranchInst *TheBr = cast<BranchInst>(Compare->user_back());

  // We need to verify that the branch actually controls the iteration count
  // of the loop.  If not, the new IV can overflow and no one will notice.
  // The branch block must be in the loop and one of the successors must be out
  // of the loop.
  assert(TheBr->isConditional() && "Can't use fcmp if not conditional");
  if (!L->contains(TheBr->getParent()) ||
      (L->contains(TheBr->getSuccessor(0)) &&
       L->contains(TheBr->getSuccessor(1))))
    return false;

  // If it isn't a comparison with an integer-as-fp (the exit value), we can't
  // transform it.
  ConstantFP *ExitValueVal = dyn_cast<ConstantFP>(Compare->getOperand(1));
  int64_t ExitValue;
  if (ExitValueVal == nullptr ||
      !ConvertToSInt(ExitValueVal->getValueAPF(), ExitValue))
    return false;

  // Find new predicate for integer comparison.
  CmpInst::Predicate NewPred = CmpInst::BAD_ICMP_PREDICATE;
  switch (Compare->getPredicate()) {
  default: return false;  // Unknown comparison.
  case CmpInst::FCMP_OEQ:
  case CmpInst::FCMP_UEQ: NewPred = CmpInst::ICMP_EQ; break;
  case CmpInst::FCMP_ONE:
  case CmpInst::FCMP_UNE: NewPred = CmpInst::ICMP_NE; break;
  case CmpInst::FCMP_OGT:
  case CmpInst::FCMP_UGT: NewPred = CmpInst::ICMP_SGT; break;
  case CmpInst::FCMP_OGE:
  case CmpInst::FCMP_UGE: NewPred = CmpInst::ICMP_SGE; break;
  case CmpInst::FCMP_OLT:
  case CmpInst::FCMP_ULT: NewPred = CmpInst::ICMP_SLT; break;
  case CmpInst::FCMP_OLE:
  case CmpInst::FCMP_ULE: NewPred = CmpInst::ICMP_SLE; break;
  }

  // We convert the floating point induction variable to a signed i32 value if
  // we can.  This is only safe if the comparison will not overflow in a way
  // that won't be trapped by the integer equivalent operations.  Check for this
  // now.
  // TODO: We could use i64 if it is native and the range requires it.

  // The start/stride/exit values must all fit in signed i32.
  if (!isInt<32>(InitValue) || !isInt<32>(IncValue) || !isInt<32>(ExitValue))
    return false;

  // If not actually striding (add x, 0.0), avoid touching the code.
  if (IncValue == 0)
    return false;

  // Positive and negative strides have different safety conditions.
  if (IncValue > 0) {
    // If we have a positive stride, we require the init to be less than the
    // exit value.
    if (InitValue >= ExitValue)
      return false;

    uint32_t Range = uint32_t(ExitValue-InitValue);
    // Check for infinite loop, either:
    // while (i <= Exit) or until (i > Exit)
    if (NewPred == CmpInst::ICMP_SLE || NewPred == CmpInst::ICMP_SGT) {
      if (++Range == 0) return false;  // Range overflows.
    }

    unsigned Leftover = Range % uint32_t(IncValue);

    // If this is an equality comparison, we require that the strided value
    // exactly land on the exit value, otherwise the IV condition will wrap
    // around and do things the fp IV wouldn't.
    if ((NewPred == CmpInst::ICMP_EQ || NewPred == CmpInst::ICMP_NE) &&
        Leftover != 0)
      return false;

    // If the stride would wrap around the i32 before exiting, we can't
    // transform the IV.
    if (Leftover != 0 && int32_t(ExitValue+IncValue) < ExitValue)
      return false;
  } else {
    // If we have a negative stride, we require the init to be greater than the
    // exit value.
    if (InitValue <= ExitValue)
      return false;

    uint32_t Range = uint32_t(InitValue-ExitValue);
    // Check for infinite loop, either:
    // while (i >= Exit) or until (i < Exit)
    if (NewPred == CmpInst::ICMP_SGE || NewPred == CmpInst::ICMP_SLT) {
      if (++Range == 0) return false;  // Range overflows.
    }

    unsigned Leftover = Range % uint32_t(-IncValue);

    // If this is an equality comparison, we require that the strided value
    // exactly land on the exit value, otherwise the IV condition will wrap
    // around and do things the fp IV wouldn't.
    if ((NewPred == CmpInst::ICMP_EQ || NewPred == CmpInst::ICMP_NE) &&
        Leftover != 0)
      return false;

    // If the stride would wrap around the i32 before exiting, we can't
    // transform the IV.
    if (Leftover != 0 && int32_t(ExitValue+IncValue) > ExitValue)
      return false;
  }

  IntegerType *Int32Ty = Type::getInt32Ty(PN->getContext());

  // Insert new integer induction variable.
  PHINode *NewPHI = PHINode::Create(Int32Ty, 2, PN->getName()+".int", PN);
  NewPHI->addIncoming(ConstantInt::get(Int32Ty, InitValue),
                      PN->getIncomingBlock(IncomingEdge));

  Value *NewAdd =
    BinaryOperator::CreateAdd(NewPHI, ConstantInt::get(Int32Ty, IncValue),
                              Incr->getName()+".int", Incr);
  NewPHI->addIncoming(NewAdd, PN->getIncomingBlock(BackEdge));

  ICmpInst *NewCompare = new ICmpInst(TheBr, NewPred, NewAdd,
                                      ConstantInt::get(Int32Ty, ExitValue),
                                      Compare->getName());

  // In the following deletions, PN may become dead and may be deleted.
  // Use a WeakTrackingVH to observe whether this happens.
  WeakTrackingVH WeakPH = PN;

  // Delete the old floating point exit comparison.  The branch starts using the
  // new comparison.
  NewCompare->takeName(Compare);
  Compare->replaceAllUsesWith(NewCompare);
  RecursivelyDeleteTriviallyDeadInstructions(Compare, TLI);

  // Delete the old floating point increment.
  Incr->replaceAllUsesWith(UndefValue::get(Incr->getType()));
  RecursivelyDeleteTriviallyDeadInstructions(Incr, TLI);

  // If the FP induction variable still has uses, this is because something else
  // in the loop uses its value.  In order to canonicalize the induction
  // variable, we chose to eliminate the IV and rewrite it in terms of an
  // int->fp cast.
  //
  // We give preference to sitofp over uitofp because it is faster on most
  // platforms.
  if (WeakPH) {
    Value *Conv = new SIToFPInst(NewPHI, PN->getType(), "indvar.conv",
                                 &*PN->getParent()->getFirstInsertionPt());
    PN->replaceAllUsesWith(Conv);
    RecursivelyDeleteTriviallyDeadInstructions(PN, TLI);
  }
  return true;
}

bool IndVarSimplify::rewriteNonIntegerIVs(Loop *L) {
  // First step.  Check to see if there are any floating-point recurrences.
  // If there are, change them into integer recurrences, permitting analysis by
  // the SCEV routines.
  BasicBlock *Header = L->getHeader();

  SmallVector<WeakTrackingVH, 8> PHIs;
  for (PHINode &PN : Header->phis())
    PHIs.push_back(&PN);

  bool Changed = false;
  for (unsigned i = 0, e = PHIs.size(); i != e; ++i)
    if (PHINode *PN = dyn_cast_or_null<PHINode>(&*PHIs[i]))
      Changed |= handleFloatingPointIV(L, PN);

  // If the loop previously had floating-point IV, ScalarEvolution
  // may not have been able to compute a trip count. Now that we've done some
  // re-writing, the trip count may be computable.
  if (Changed)
    SE->forgetLoop(L);
  return Changed;
}

namespace {

// Collect information about PHI nodes which can be transformed in
// rewriteLoopExitValues.
struct RewritePhi {
  PHINode *PN;

  // Ith incoming value.
  unsigned Ith;

  // Exit value after expansion.
  Value *Val;

  // High Cost when expansion.
  bool HighCost;

  RewritePhi(PHINode *P, unsigned I, Value *V, bool H)
      : PN(P), Ith(I), Val(V), HighCost(H) {}
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// rewriteLoopExitValues - Optimize IV users outside the loop.
// As a side effect, reduces the amount of IV processing within the loop.
//===----------------------------------------------------------------------===//

bool IndVarSimplify::hasHardUserWithinLoop(const Loop *L, const Instruction *I) const {
  SmallPtrSet<const Instruction *, 8> Visited;
  SmallVector<const Instruction *, 8> WorkList;
  Visited.insert(I);
  WorkList.push_back(I);
  while (!WorkList.empty()) {
    const Instruction *Curr = WorkList.pop_back_val();
    // This use is outside the loop, nothing to do.
    if (!L->contains(Curr))
      continue;
    // Do we assume it is a "hard" use which will not be eliminated easily?
    if (Curr->mayHaveSideEffects())
      return true;
    // Otherwise, add all its users to worklist.
    for (auto U : Curr->users()) {
      auto *UI = cast<Instruction>(U);
      if (Visited.insert(UI).second)
        WorkList.push_back(UI);
    }
  }
  return false;
}

/// Check to see if this loop has a computable loop-invariant execution count.
/// If so, this means that we can compute the final value of any expressions
/// that are recurrent in the loop, and substitute the exit values from the loop
/// into any instructions outside of the loop that use the final values of the
/// current expressions.
///
/// This is mostly redundant with the regular IndVarSimplify activities that
/// happen later, except that it's more powerful in some cases, because it's
/// able to brute-force evaluate arbitrary instructions as long as they have
/// constant operands at the beginning of the loop.
bool IndVarSimplify::rewriteLoopExitValues(Loop *L, SCEVExpander &Rewriter) {
  // Check a pre-condition.
  assert(L->isRecursivelyLCSSAForm(*DT, *LI) &&
         "Indvars did not preserve LCSSA!");

  SmallVector<BasicBlock*, 8> ExitBlocks;
  L->getUniqueExitBlocks(ExitBlocks);

  SmallVector<RewritePhi, 8> RewritePhiSet;
  // Find all values that are computed inside the loop, but used outside of it.
  // Because of LCSSA, these values will only occur in LCSSA PHI Nodes.  Scan
  // the exit blocks of the loop to find them.
  for (BasicBlock *ExitBB : ExitBlocks) {
    // If there are no PHI nodes in this exit block, then no values defined
    // inside the loop are used on this path, skip it.
    PHINode *PN = dyn_cast<PHINode>(ExitBB->begin());
    if (!PN) continue;

    unsigned NumPreds = PN->getNumIncomingValues();

    // Iterate over all of the PHI nodes.
    BasicBlock::iterator BBI = ExitBB->begin();
    while ((PN = dyn_cast<PHINode>(BBI++))) {
      if (PN->use_empty())
        continue; // dead use, don't replace it

      if (!SE->isSCEVable(PN->getType()))
        continue;

      // It's necessary to tell ScalarEvolution about this explicitly so that
      // it can walk the def-use list and forget all SCEVs, as it may not be
      // watching the PHI itself. Once the new exit value is in place, there
      // may not be a def-use connection between the loop and every instruction
      // which got a SCEVAddRecExpr for that loop.
      SE->forgetValue(PN);

      // Iterate over all of the values in all the PHI nodes.
      for (unsigned i = 0; i != NumPreds; ++i) {
        // If the value being merged in is not integer or is not defined
        // in the loop, skip it.
        Value *InVal = PN->getIncomingValue(i);
        if (!isa<Instruction>(InVal))
          continue;

        // If this pred is for a subloop, not L itself, skip it.
        if (LI->getLoopFor(PN->getIncomingBlock(i)) != L)
          continue; // The Block is in a subloop, skip it.

        // Check that InVal is defined in the loop.
        Instruction *Inst = cast<Instruction>(InVal);
        if (!L->contains(Inst))
          continue;

        // Okay, this instruction has a user outside of the current loop
        // and varies predictably *inside* the loop.  Evaluate the value it
        // contains when the loop exits, if possible.
        const SCEV *ExitValue = SE->getSCEVAtScope(Inst, L->getParentLoop());
        if (!SE->isLoopInvariant(ExitValue, L) ||
            !isSafeToExpand(ExitValue, *SE))
          continue;

        // Computing the value outside of the loop brings no benefit if it is
        // definitely used inside the loop in a way which can not be optimized
        // away.
        if (!isa<SCEVConstant>(ExitValue) && hasHardUserWithinLoop(L, Inst))
          continue;

        bool HighCost = Rewriter.isHighCostExpansion(ExitValue, L, Inst);
        Value *ExitVal = Rewriter.expandCodeFor(ExitValue, PN->getType(), Inst);

        LLVM_DEBUG(dbgs() << "INDVARS: RLEV: AfterLoopVal = " << *ExitVal
                          << '\n'
                          << "  LoopVal = " << *Inst << "\n");

        if (!isValidRewrite(Inst, ExitVal)) {
          DeadInsts.push_back(ExitVal);
          continue;
        }

#ifndef NDEBUG
        // If we reuse an instruction from a loop which is neither L nor one of
        // its containing loops, we end up breaking LCSSA form for this loop by
        // creating a new use of its instruction.
        if (auto *ExitInsn = dyn_cast<Instruction>(ExitVal))
          if (auto *EVL = LI->getLoopFor(ExitInsn->getParent()))
            if (EVL != L)
              assert(EVL->contains(L) && "LCSSA breach detected!");
#endif

        // Collect all the candidate PHINodes to be rewritten.
        RewritePhiSet.emplace_back(PN, i, ExitVal, HighCost);
      }
    }
  }

  bool LoopCanBeDel = canLoopBeDeleted(L, RewritePhiSet);

  bool Changed = false;
  // Transformation.
  for (const RewritePhi &Phi : RewritePhiSet) {
    PHINode *PN = Phi.PN;
    Value *ExitVal = Phi.Val;

    // Only do the rewrite when the ExitValue can be expanded cheaply.
    // If LoopCanBeDel is true, rewrite exit value aggressively.
    if (ReplaceExitValue == OnlyCheapRepl && !LoopCanBeDel && Phi.HighCost) {
      DeadInsts.push_back(ExitVal);
      continue;
    }

    Changed = true;
    ++NumReplaced;
    Instruction *Inst = cast<Instruction>(PN->getIncomingValue(Phi.Ith));
    PN->setIncomingValue(Phi.Ith, ExitVal);

    // If this instruction is dead now, delete it. Don't do it now to avoid
    // invalidating iterators.
    if (isInstructionTriviallyDead(Inst, TLI))
      DeadInsts.push_back(Inst);

    // Replace PN with ExitVal if that is legal and does not break LCSSA.
    if (PN->getNumIncomingValues() == 1 &&
        LI->replacementPreservesLCSSAForm(PN, ExitVal)) {
      PN->replaceAllUsesWith(ExitVal);
      PN->eraseFromParent();
    }
  }

  // The insertion point instruction may have been deleted; clear it out
  // so that the rewriter doesn't trip over it later.
  Rewriter.clearInsertPoint();
  return Changed;
}

//===---------------------------------------------------------------------===//
// rewriteFirstIterationLoopExitValues: Rewrite loop exit values if we know
// they will exit at the first iteration.
//===---------------------------------------------------------------------===//

/// Check to see if this loop has loop invariant conditions which lead to loop
/// exits. If so, we know that if the exit path is taken, it is at the first
/// loop iteration. This lets us predict exit values of PHI nodes that live in
/// loop header.
bool IndVarSimplify::rewriteFirstIterationLoopExitValues(Loop *L) {
  // Verify the input to the pass is already in LCSSA form.
  assert(L->isLCSSAForm(*DT));

  SmallVector<BasicBlock *, 8> ExitBlocks;
  L->getUniqueExitBlocks(ExitBlocks);
  auto *LoopHeader = L->getHeader();
  assert(LoopHeader && "Invalid loop");

  bool MadeAnyChanges = false;
  for (auto *ExitBB : ExitBlocks) {
    // If there are no more PHI nodes in this exit block, then no more
    // values defined inside the loop are used on this path.
    for (PHINode &PN : ExitBB->phis()) {
      for (unsigned IncomingValIdx = 0, E = PN.getNumIncomingValues();
           IncomingValIdx != E; ++IncomingValIdx) {
        auto *IncomingBB = PN.getIncomingBlock(IncomingValIdx);

        // We currently only support loop exits from loop header. If the
        // incoming block is not loop header, we need to recursively check
        // all conditions starting from loop header are loop invariants.
        // Additional support might be added in the future.
        if (IncomingBB != LoopHeader)
          continue;

        // Get condition that leads to the exit path.
        auto *TermInst = IncomingBB->getTerminator();

        Value *Cond = nullptr;
        if (auto *BI = dyn_cast<BranchInst>(TermInst)) {
          // Must be a conditional branch, otherwise the block
          // should not be in the loop.
          Cond = BI->getCondition();
        } else if (auto *SI = dyn_cast<SwitchInst>(TermInst))
          Cond = SI->getCondition();
        else
          continue;

        if (!L->isLoopInvariant(Cond))
          continue;

        auto *ExitVal = dyn_cast<PHINode>(PN.getIncomingValue(IncomingValIdx));

        // Only deal with PHIs.
        if (!ExitVal)
          continue;

        // If ExitVal is a PHI on the loop header, then we know its
        // value along this exit because the exit can only be taken
        // on the first iteration.
        auto *LoopPreheader = L->getLoopPreheader();
        assert(LoopPreheader && "Invalid loop");
        int PreheaderIdx = ExitVal->getBasicBlockIndex(LoopPreheader);
        if (PreheaderIdx != -1) {
          assert(ExitVal->getParent() == LoopHeader &&
                 "ExitVal must be in loop header");
          MadeAnyChanges = true;
          PN.setIncomingValue(IncomingValIdx,
                              ExitVal->getIncomingValue(PreheaderIdx));
        }
      }
    }
  }
  return MadeAnyChanges;
}

/// Check whether it is possible to delete the loop after rewriting exit
/// value. If it is possible, ignore ReplaceExitValue and do rewriting
/// aggressively.
bool IndVarSimplify::canLoopBeDeleted(
    Loop *L, SmallVector<RewritePhi, 8> &RewritePhiSet) {
  BasicBlock *Preheader = L->getLoopPreheader();
  // If there is no preheader, the loop will not be deleted.
  if (!Preheader)
    return false;

  // In LoopDeletion pass Loop can be deleted when ExitingBlocks.size() > 1.
  // We obviate multiple ExitingBlocks case for simplicity.
  // TODO: If we see testcase with multiple ExitingBlocks can be deleted
  // after exit value rewriting, we can enhance the logic here.
  SmallVector<BasicBlock *, 4> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);
  SmallVector<BasicBlock *, 8> ExitBlocks;
  L->getUniqueExitBlocks(ExitBlocks);
  if (ExitBlocks.size() > 1 || ExitingBlocks.size() > 1)
    return false;

  BasicBlock *ExitBlock = ExitBlocks[0];
  BasicBlock::iterator BI = ExitBlock->begin();
  while (PHINode *P = dyn_cast<PHINode>(BI)) {
    Value *Incoming = P->getIncomingValueForBlock(ExitingBlocks[0]);

    // If the Incoming value of P is found in RewritePhiSet, we know it
    // could be rewritten to use a loop invariant value in transformation
    // phase later. Skip it in the loop invariant check below.
    bool found = false;
    for (const RewritePhi &Phi : RewritePhiSet) {
      unsigned i = Phi.Ith;
      if (Phi.PN == P && (Phi.PN)->getIncomingValue(i) == Incoming) {
        found = true;
        break;
      }
    }

    Instruction *I;
    if (!found && (I = dyn_cast<Instruction>(Incoming)))
      if (!L->hasLoopInvariantOperands(I))
        return false;

    ++BI;
  }

  for (auto *BB : L->blocks())
    if (llvm::any_of(*BB, [](Instruction &I) {
          return I.mayHaveSideEffects();
        }))
      return false;

  return true;
}

//===----------------------------------------------------------------------===//
//  IV Widening - Extend the width of an IV to cover its widest uses.
//===----------------------------------------------------------------------===//

namespace {

// Collect information about induction variables that are used by sign/zero
// extend operations. This information is recorded by CollectExtend and provides
// the input to WidenIV.
struct WideIVInfo {
  PHINode *NarrowIV = nullptr;

  // Widest integer type created [sz]ext
  Type *WidestNativeType = nullptr;

  // Was a sext user seen before a zext?
  bool IsSigned = false;
};

} // end anonymous namespace

/// Update information about the induction variable that is extended by this
/// sign or zero extend operation. This is used to determine the final width of
/// the IV before actually widening it.
static void visitIVCast(CastInst *Cast, WideIVInfo &WI, ScalarEvolution *SE,
                        const TargetTransformInfo *TTI) {
  bool IsSigned = Cast->getOpcode() == Instruction::SExt;
  if (!IsSigned && Cast->getOpcode() != Instruction::ZExt)
    return;

  Type *Ty = Cast->getType();
  uint64_t Width = SE->getTypeSizeInBits(Ty);
  if (!Cast->getModule()->getDataLayout().isLegalInteger(Width))
    return;

  // Check that `Cast` actually extends the induction variable (we rely on this
  // later).  This takes care of cases where `Cast` is extending a truncation of
  // the narrow induction variable, and thus can end up being narrower than the
  // "narrow" induction variable.
  uint64_t NarrowIVWidth = SE->getTypeSizeInBits(WI.NarrowIV->getType());
  if (NarrowIVWidth >= Width)
    return;

  // Cast is either an sext or zext up to this point.
  // We should not widen an indvar if arithmetics on the wider indvar are more
  // expensive than those on the narrower indvar. We check only the cost of ADD
  // because at least an ADD is required to increment the induction variable. We
  // could compute more comprehensively the cost of all instructions on the
  // induction variable when necessary.
  if (TTI &&
      TTI->getArithmeticInstrCost(Instruction::Add, Ty) >
          TTI->getArithmeticInstrCost(Instruction::Add,
                                      Cast->getOperand(0)->getType())) {
    return;
  }

  if (!WI.WidestNativeType) {
    WI.WidestNativeType = SE->getEffectiveSCEVType(Ty);
    WI.IsSigned = IsSigned;
    return;
  }

  // We extend the IV to satisfy the sign of its first user, arbitrarily.
  if (WI.IsSigned != IsSigned)
    return;

  if (Width > SE->getTypeSizeInBits(WI.WidestNativeType))
    WI.WidestNativeType = SE->getEffectiveSCEVType(Ty);
}

namespace {

/// Record a link in the Narrow IV def-use chain along with the WideIV that
/// computes the same value as the Narrow IV def.  This avoids caching Use*
/// pointers.
struct NarrowIVDefUse {
  Instruction *NarrowDef = nullptr;
  Instruction *NarrowUse = nullptr;
  Instruction *WideDef = nullptr;

  // True if the narrow def is never negative.  Tracking this information lets
  // us use a sign extension instead of a zero extension or vice versa, when
  // profitable and legal.
  bool NeverNegative = false;

  NarrowIVDefUse(Instruction *ND, Instruction *NU, Instruction *WD,
                 bool NeverNegative)
      : NarrowDef(ND), NarrowUse(NU), WideDef(WD),
        NeverNegative(NeverNegative) {}
};

/// The goal of this transform is to remove sign and zero extends without
/// creating any new induction variables. To do this, it creates a new phi of
/// the wider type and redirects all users, either removing extends or inserting
/// truncs whenever we stop propagating the type.
class WidenIV {
  // Parameters
  PHINode *OrigPhi;
  Type *WideType;

  // Context
  LoopInfo        *LI;
  Loop            *L;
  ScalarEvolution *SE;
  DominatorTree   *DT;

  // Does the module have any calls to the llvm.experimental.guard intrinsic
  // at all? If not we can avoid scanning instructions looking for guards.
  bool HasGuards;

  // Result
  PHINode *WidePhi = nullptr;
  Instruction *WideInc = nullptr;
  const SCEV *WideIncExpr = nullptr;
  SmallVectorImpl<WeakTrackingVH> &DeadInsts;

  SmallPtrSet<Instruction *,16> Widened;
  SmallVector<NarrowIVDefUse, 8> NarrowIVUsers;

  enum ExtendKind { ZeroExtended, SignExtended, Unknown };

  // A map tracking the kind of extension used to widen each narrow IV
  // and narrow IV user.
  // Key: pointer to a narrow IV or IV user.
  // Value: the kind of extension used to widen this Instruction.
  DenseMap<AssertingVH<Instruction>, ExtendKind> ExtendKindMap;

  using DefUserPair = std::pair<AssertingVH<Value>, AssertingVH<Instruction>>;

  // A map with control-dependent ranges for post increment IV uses. The key is
  // a pair of IV def and a use of this def denoting the context. The value is
  // a ConstantRange representing possible values of the def at the given
  // context.
  DenseMap<DefUserPair, ConstantRange> PostIncRangeInfos;

  Optional<ConstantRange> getPostIncRangeInfo(Value *Def,
                                              Instruction *UseI) {
    DefUserPair Key(Def, UseI);
    auto It = PostIncRangeInfos.find(Key);
    return It == PostIncRangeInfos.end()
               ? Optional<ConstantRange>(None)
               : Optional<ConstantRange>(It->second);
  }

  void calculatePostIncRanges(PHINode *OrigPhi);
  void calculatePostIncRange(Instruction *NarrowDef, Instruction *NarrowUser);

  void updatePostIncRangeInfo(Value *Def, Instruction *UseI, ConstantRange R) {
    DefUserPair Key(Def, UseI);
    auto It = PostIncRangeInfos.find(Key);
    if (It == PostIncRangeInfos.end())
      PostIncRangeInfos.insert({Key, R});
    else
      It->second = R.intersectWith(It->second);
  }

public:
  WidenIV(const WideIVInfo &WI, LoopInfo *LInfo, ScalarEvolution *SEv,
          DominatorTree *DTree, SmallVectorImpl<WeakTrackingVH> &DI,
          bool HasGuards)
      : OrigPhi(WI.NarrowIV), WideType(WI.WidestNativeType), LI(LInfo),
        L(LI->getLoopFor(OrigPhi->getParent())), SE(SEv), DT(DTree),
        HasGuards(HasGuards), DeadInsts(DI) {
    assert(L->getHeader() == OrigPhi->getParent() && "Phi must be an IV");
    ExtendKindMap[OrigPhi] = WI.IsSigned ? SignExtended : ZeroExtended;
  }

  PHINode *createWideIV(SCEVExpander &Rewriter);

protected:
  Value *createExtendInst(Value *NarrowOper, Type *WideType, bool IsSigned,
                          Instruction *Use);

  Instruction *cloneIVUser(NarrowIVDefUse DU, const SCEVAddRecExpr *WideAR);
  Instruction *cloneArithmeticIVUser(NarrowIVDefUse DU,
                                     const SCEVAddRecExpr *WideAR);
  Instruction *cloneBitwiseIVUser(NarrowIVDefUse DU);

  ExtendKind getExtendKind(Instruction *I);

  using WidenedRecTy = std::pair<const SCEVAddRecExpr *, ExtendKind>;

  WidenedRecTy getWideRecurrence(NarrowIVDefUse DU);

  WidenedRecTy getExtendedOperandRecurrence(NarrowIVDefUse DU);

  const SCEV *getSCEVByOpCode(const SCEV *LHS, const SCEV *RHS,
                              unsigned OpCode) const;

  Instruction *widenIVUse(NarrowIVDefUse DU, SCEVExpander &Rewriter);

  bool widenLoopCompare(NarrowIVDefUse DU);
  bool widenWithVariantLoadUse(NarrowIVDefUse DU);
  void widenWithVariantLoadUseCodegen(NarrowIVDefUse DU);

  void pushNarrowIVUsers(Instruction *NarrowDef, Instruction *WideDef);
};

} // end anonymous namespace

/// Perform a quick domtree based check for loop invariance assuming that V is
/// used within the loop. LoopInfo::isLoopInvariant() seems gratuitous for this
/// purpose.
static bool isLoopInvariant(Value *V, const Loop *L, const DominatorTree *DT) {
  Instruction *Inst = dyn_cast<Instruction>(V);
  if (!Inst)
    return true;

  return DT->properlyDominates(Inst->getParent(), L->getHeader());
}

Value *WidenIV::createExtendInst(Value *NarrowOper, Type *WideType,
                                 bool IsSigned, Instruction *Use) {
  // Set the debug location and conservative insertion point.
  IRBuilder<> Builder(Use);
  // Hoist the insertion point into loop preheaders as far as possible.
  for (const Loop *L = LI->getLoopFor(Use->getParent());
       L && L->getLoopPreheader() && isLoopInvariant(NarrowOper, L, DT);
       L = L->getParentLoop())
    Builder.SetInsertPoint(L->getLoopPreheader()->getTerminator());

  return IsSigned ? Builder.CreateSExt(NarrowOper, WideType) :
                    Builder.CreateZExt(NarrowOper, WideType);
}

/// Instantiate a wide operation to replace a narrow operation. This only needs
/// to handle operations that can evaluation to SCEVAddRec. It can safely return
/// 0 for any operation we decide not to clone.
Instruction *WidenIV::cloneIVUser(NarrowIVDefUse DU,
                                  const SCEVAddRecExpr *WideAR) {
  unsigned Opcode = DU.NarrowUse->getOpcode();
  switch (Opcode) {
  default:
    return nullptr;
  case Instruction::Add:
  case Instruction::Mul:
  case Instruction::UDiv:
  case Instruction::Sub:
    return cloneArithmeticIVUser(DU, WideAR);

  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
    return cloneBitwiseIVUser(DU);
  }
}

Instruction *WidenIV::cloneBitwiseIVUser(NarrowIVDefUse DU) {
  Instruction *NarrowUse = DU.NarrowUse;
  Instruction *NarrowDef = DU.NarrowDef;
  Instruction *WideDef = DU.WideDef;

  LLVM_DEBUG(dbgs() << "Cloning bitwise IVUser: " << *NarrowUse << "\n");

  // Replace NarrowDef operands with WideDef. Otherwise, we don't know anything
  // about the narrow operand yet so must insert a [sz]ext. It is probably loop
  // invariant and will be folded or hoisted. If it actually comes from a
  // widened IV, it should be removed during a future call to widenIVUse.
  bool IsSigned = getExtendKind(NarrowDef) == SignExtended;
  Value *LHS = (NarrowUse->getOperand(0) == NarrowDef)
                   ? WideDef
                   : createExtendInst(NarrowUse->getOperand(0), WideType,
                                      IsSigned, NarrowUse);
  Value *RHS = (NarrowUse->getOperand(1) == NarrowDef)
                   ? WideDef
                   : createExtendInst(NarrowUse->getOperand(1), WideType,
                                      IsSigned, NarrowUse);

  auto *NarrowBO = cast<BinaryOperator>(NarrowUse);
  auto *WideBO = BinaryOperator::Create(NarrowBO->getOpcode(), LHS, RHS,
                                        NarrowBO->getName());
  IRBuilder<> Builder(NarrowUse);
  Builder.Insert(WideBO);
  WideBO->copyIRFlags(NarrowBO);
  return WideBO;
}

Instruction *WidenIV::cloneArithmeticIVUser(NarrowIVDefUse DU,
                                            const SCEVAddRecExpr *WideAR) {
  Instruction *NarrowUse = DU.NarrowUse;
  Instruction *NarrowDef = DU.NarrowDef;
  Instruction *WideDef = DU.WideDef;

  LLVM_DEBUG(dbgs() << "Cloning arithmetic IVUser: " << *NarrowUse << "\n");

  unsigned IVOpIdx = (NarrowUse->getOperand(0) == NarrowDef) ? 0 : 1;

  // We're trying to find X such that
  //
  //  Widen(NarrowDef `op` NonIVNarrowDef) == WideAR == WideDef `op.wide` X
  //
  // We guess two solutions to X, sext(NonIVNarrowDef) and zext(NonIVNarrowDef),
  // and check using SCEV if any of them are correct.

  // Returns true if extending NonIVNarrowDef according to `SignExt` is a
  // correct solution to X.
  auto GuessNonIVOperand = [&](bool SignExt) {
    const SCEV *WideLHS;
    const SCEV *WideRHS;

    auto GetExtend = [this, SignExt](const SCEV *S, Type *Ty) {
      if (SignExt)
        return SE->getSignExtendExpr(S, Ty);
      return SE->getZeroExtendExpr(S, Ty);
    };

    if (IVOpIdx == 0) {
      WideLHS = SE->getSCEV(WideDef);
      const SCEV *NarrowRHS = SE->getSCEV(NarrowUse->getOperand(1));
      WideRHS = GetExtend(NarrowRHS, WideType);
    } else {
      const SCEV *NarrowLHS = SE->getSCEV(NarrowUse->getOperand(0));
      WideLHS = GetExtend(NarrowLHS, WideType);
      WideRHS = SE->getSCEV(WideDef);
    }

    // WideUse is "WideDef `op.wide` X" as described in the comment.
    const SCEV *WideUse = nullptr;

    switch (NarrowUse->getOpcode()) {
    default:
      llvm_unreachable("No other possibility!");

    case Instruction::Add:
      WideUse = SE->getAddExpr(WideLHS, WideRHS);
      break;

    case Instruction::Mul:
      WideUse = SE->getMulExpr(WideLHS, WideRHS);
      break;

    case Instruction::UDiv:
      WideUse = SE->getUDivExpr(WideLHS, WideRHS);
      break;

    case Instruction::Sub:
      WideUse = SE->getMinusSCEV(WideLHS, WideRHS);
      break;
    }

    return WideUse == WideAR;
  };

  bool SignExtend = getExtendKind(NarrowDef) == SignExtended;
  if (!GuessNonIVOperand(SignExtend)) {
    SignExtend = !SignExtend;
    if (!GuessNonIVOperand(SignExtend))
      return nullptr;
  }

  Value *LHS = (NarrowUse->getOperand(0) == NarrowDef)
                   ? WideDef
                   : createExtendInst(NarrowUse->getOperand(0), WideType,
                                      SignExtend, NarrowUse);
  Value *RHS = (NarrowUse->getOperand(1) == NarrowDef)
                   ? WideDef
                   : createExtendInst(NarrowUse->getOperand(1), WideType,
                                      SignExtend, NarrowUse);

  auto *NarrowBO = cast<BinaryOperator>(NarrowUse);
  auto *WideBO = BinaryOperator::Create(NarrowBO->getOpcode(), LHS, RHS,
                                        NarrowBO->getName());

  IRBuilder<> Builder(NarrowUse);
  Builder.Insert(WideBO);
  WideBO->copyIRFlags(NarrowBO);
  return WideBO;
}

WidenIV::ExtendKind WidenIV::getExtendKind(Instruction *I) {
  auto It = ExtendKindMap.find(I);
  assert(It != ExtendKindMap.end() && "Instruction not yet extended!");
  return It->second;
}

const SCEV *WidenIV::getSCEVByOpCode(const SCEV *LHS, const SCEV *RHS,
                                     unsigned OpCode) const {
  if (OpCode == Instruction::Add)
    return SE->getAddExpr(LHS, RHS);
  if (OpCode == Instruction::Sub)
    return SE->getMinusSCEV(LHS, RHS);
  if (OpCode == Instruction::Mul)
    return SE->getMulExpr(LHS, RHS);

  llvm_unreachable("Unsupported opcode.");
}

/// No-wrap operations can transfer sign extension of their result to their
/// operands. Generate the SCEV value for the widened operation without
/// actually modifying the IR yet. If the expression after extending the
/// operands is an AddRec for this loop, return the AddRec and the kind of
/// extension used.
WidenIV::WidenedRecTy WidenIV::getExtendedOperandRecurrence(NarrowIVDefUse DU) {
  // Handle the common case of add<nsw/nuw>
  const unsigned OpCode = DU.NarrowUse->getOpcode();
  // Only Add/Sub/Mul instructions supported yet.
  if (OpCode != Instruction::Add && OpCode != Instruction::Sub &&
      OpCode != Instruction::Mul)
    return {nullptr, Unknown};

  // One operand (NarrowDef) has already been extended to WideDef. Now determine
  // if extending the other will lead to a recurrence.
  const unsigned ExtendOperIdx =
      DU.NarrowUse->getOperand(0) == DU.NarrowDef ? 1 : 0;
  assert(DU.NarrowUse->getOperand(1-ExtendOperIdx) == DU.NarrowDef && "bad DU");

  const SCEV *ExtendOperExpr = nullptr;
  const OverflowingBinaryOperator *OBO =
    cast<OverflowingBinaryOperator>(DU.NarrowUse);
  ExtendKind ExtKind = getExtendKind(DU.NarrowDef);
  if (ExtKind == SignExtended && OBO->hasNoSignedWrap())
    ExtendOperExpr = SE->getSignExtendExpr(
      SE->getSCEV(DU.NarrowUse->getOperand(ExtendOperIdx)), WideType);
  else if(ExtKind == ZeroExtended && OBO->hasNoUnsignedWrap())
    ExtendOperExpr = SE->getZeroExtendExpr(
      SE->getSCEV(DU.NarrowUse->getOperand(ExtendOperIdx)), WideType);
  else
    return {nullptr, Unknown};

  // When creating this SCEV expr, don't apply the current operations NSW or NUW
  // flags. This instruction may be guarded by control flow that the no-wrap
  // behavior depends on. Non-control-equivalent instructions can be mapped to
  // the same SCEV expression, and it would be incorrect to transfer NSW/NUW
  // semantics to those operations.
  const SCEV *lhs = SE->getSCEV(DU.WideDef);
  const SCEV *rhs = ExtendOperExpr;

  // Let's swap operands to the initial order for the case of non-commutative
  // operations, like SUB. See PR21014.
  if (ExtendOperIdx == 0)
    std::swap(lhs, rhs);
  const SCEVAddRecExpr *AddRec =
      dyn_cast<SCEVAddRecExpr>(getSCEVByOpCode(lhs, rhs, OpCode));

  if (!AddRec || AddRec->getLoop() != L)
    return {nullptr, Unknown};

  return {AddRec, ExtKind};
}

/// Is this instruction potentially interesting for further simplification after
/// widening it's type? In other words, can the extend be safely hoisted out of
/// the loop with SCEV reducing the value to a recurrence on the same loop. If
/// so, return the extended recurrence and the kind of extension used. Otherwise
/// return {nullptr, Unknown}.
WidenIV::WidenedRecTy WidenIV::getWideRecurrence(NarrowIVDefUse DU) {
  if (!SE->isSCEVable(DU.NarrowUse->getType()))
    return {nullptr, Unknown};

  const SCEV *NarrowExpr = SE->getSCEV(DU.NarrowUse);
  if (SE->getTypeSizeInBits(NarrowExpr->getType()) >=
      SE->getTypeSizeInBits(WideType)) {
    // NarrowUse implicitly widens its operand. e.g. a gep with a narrow
    // index. So don't follow this use.
    return {nullptr, Unknown};
  }

  const SCEV *WideExpr;
  ExtendKind ExtKind;
  if (DU.NeverNegative) {
    WideExpr = SE->getSignExtendExpr(NarrowExpr, WideType);
    if (isa<SCEVAddRecExpr>(WideExpr))
      ExtKind = SignExtended;
    else {
      WideExpr = SE->getZeroExtendExpr(NarrowExpr, WideType);
      ExtKind = ZeroExtended;
    }
  } else if (getExtendKind(DU.NarrowDef) == SignExtended) {
    WideExpr = SE->getSignExtendExpr(NarrowExpr, WideType);
    ExtKind = SignExtended;
  } else {
    WideExpr = SE->getZeroExtendExpr(NarrowExpr, WideType);
    ExtKind = ZeroExtended;
  }
  const SCEVAddRecExpr *AddRec = dyn_cast<SCEVAddRecExpr>(WideExpr);
  if (!AddRec || AddRec->getLoop() != L)
    return {nullptr, Unknown};
  return {AddRec, ExtKind};
}

/// This IV user cannot be widen. Replace this use of the original narrow IV
/// with a truncation of the new wide IV to isolate and eliminate the narrow IV.
static void truncateIVUse(NarrowIVDefUse DU, DominatorTree *DT, LoopInfo *LI) {
  LLVM_DEBUG(dbgs() << "INDVARS: Truncate IV " << *DU.WideDef << " for user "
                    << *DU.NarrowUse << "\n");
  IRBuilder<> Builder(
      getInsertPointForUses(DU.NarrowUse, DU.NarrowDef, DT, LI));
  Value *Trunc = Builder.CreateTrunc(DU.WideDef, DU.NarrowDef->getType());
  DU.NarrowUse->replaceUsesOfWith(DU.NarrowDef, Trunc);
}

/// If the narrow use is a compare instruction, then widen the compare
//  (and possibly the other operand).  The extend operation is hoisted into the
// loop preheader as far as possible.
bool WidenIV::widenLoopCompare(NarrowIVDefUse DU) {
  ICmpInst *Cmp = dyn_cast<ICmpInst>(DU.NarrowUse);
  if (!Cmp)
    return false;

  // We can legally widen the comparison in the following two cases:
  //
  //  - The signedness of the IV extension and comparison match
  //
  //  - The narrow IV is always positive (and thus its sign extension is equal
  //    to its zero extension).  For instance, let's say we're zero extending
  //    %narrow for the following use
  //
  //      icmp slt i32 %narrow, %val   ... (A)
  //
  //    and %narrow is always positive.  Then
  //
  //      (A) == icmp slt i32 sext(%narrow), sext(%val)
  //          == icmp slt i32 zext(%narrow), sext(%val)
  bool IsSigned = getExtendKind(DU.NarrowDef) == SignExtended;
  if (!(DU.NeverNegative || IsSigned == Cmp->isSigned()))
    return false;

  Value *Op = Cmp->getOperand(Cmp->getOperand(0) == DU.NarrowDef ? 1 : 0);
  unsigned CastWidth = SE->getTypeSizeInBits(Op->getType());
  unsigned IVWidth = SE->getTypeSizeInBits(WideType);
  assert(CastWidth <= IVWidth && "Unexpected width while widening compare.");

  // Widen the compare instruction.
  IRBuilder<> Builder(
      getInsertPointForUses(DU.NarrowUse, DU.NarrowDef, DT, LI));
  DU.NarrowUse->replaceUsesOfWith(DU.NarrowDef, DU.WideDef);

  // Widen the other operand of the compare, if necessary.
  if (CastWidth < IVWidth) {
    Value *ExtOp = createExtendInst(Op, WideType, Cmp->isSigned(), Cmp);
    DU.NarrowUse->replaceUsesOfWith(Op, ExtOp);
  }
  return true;
}

/// If the narrow use is an instruction whose two operands are the defining
/// instruction of DU and a load instruction, then we have the following:
/// if the load is hoisted outside the loop, then we do not reach this function
/// as scalar evolution analysis works fine in widenIVUse with variables
/// hoisted outside the loop and efficient code is subsequently generated by
/// not emitting truncate instructions. But when the load is not hoisted
/// (whether due to limitation in alias analysis or due to a true legality),
/// then scalar evolution can not proceed with loop variant values and
/// inefficient code is generated. This function handles the non-hoisted load
/// special case by making the optimization generate the same type of code for
/// hoisted and non-hoisted load (widen use and eliminate sign extend
/// instruction). This special case is important especially when the induction
/// variables are affecting addressing mode in code generation.
bool WidenIV::widenWithVariantLoadUse(NarrowIVDefUse DU) {
  Instruction *NarrowUse = DU.NarrowUse;
  Instruction *NarrowDef = DU.NarrowDef;
  Instruction *WideDef = DU.WideDef;

  // Handle the common case of add<nsw/nuw>
  const unsigned OpCode = NarrowUse->getOpcode();
  // Only Add/Sub/Mul instructions are supported.
  if (OpCode != Instruction::Add && OpCode != Instruction::Sub &&
      OpCode != Instruction::Mul)
    return false;

  // The operand that is not defined by NarrowDef of DU. Let's call it the
  // other operand.
  unsigned ExtendOperIdx = DU.NarrowUse->getOperand(0) == NarrowDef ? 1 : 0;
  assert(DU.NarrowUse->getOperand(1 - ExtendOperIdx) == DU.NarrowDef &&
         "bad DU");

  const SCEV *ExtendOperExpr = nullptr;
  const OverflowingBinaryOperator *OBO =
    cast<OverflowingBinaryOperator>(NarrowUse);
  ExtendKind ExtKind = getExtendKind(NarrowDef);
  if (ExtKind == SignExtended && OBO->hasNoSignedWrap())
    ExtendOperExpr = SE->getSignExtendExpr(
      SE->getSCEV(NarrowUse->getOperand(ExtendOperIdx)), WideType);
  else if (ExtKind == ZeroExtended && OBO->hasNoUnsignedWrap())
    ExtendOperExpr = SE->getZeroExtendExpr(
      SE->getSCEV(NarrowUse->getOperand(ExtendOperIdx)), WideType);
  else
    return false;

  // We are interested in the other operand being a load instruction.
  // But, we should look into relaxing this restriction later on.
  auto *I = dyn_cast<Instruction>(NarrowUse->getOperand(ExtendOperIdx));
  if (I && I->getOpcode() != Instruction::Load)
    return false;

  // Verifying that Defining operand is an AddRec
  const SCEV *Op1 = SE->getSCEV(WideDef);
  const SCEVAddRecExpr *AddRecOp1 = dyn_cast<SCEVAddRecExpr>(Op1);
  if (!AddRecOp1 || AddRecOp1->getLoop() != L)
    return false;
  // Verifying that other operand is an Extend.
  if (ExtKind == SignExtended) {
    if (!isa<SCEVSignExtendExpr>(ExtendOperExpr))
      return false;
  } else {
    if (!isa<SCEVZeroExtendExpr>(ExtendOperExpr))
      return false;
  }

  if (ExtKind == SignExtended) {
    for (Use &U : NarrowUse->uses()) {
      SExtInst *User = dyn_cast<SExtInst>(U.getUser());
      if (!User || User->getType() != WideType)
        return false;
    }
  } else { // ExtKind == ZeroExtended
    for (Use &U : NarrowUse->uses()) {
      ZExtInst *User = dyn_cast<ZExtInst>(U.getUser());
      if (!User || User->getType() != WideType)
        return false;
    }
  }

  return true;
}

/// Special Case for widening with variant Loads (see
/// WidenIV::widenWithVariantLoadUse). This is the code generation part.
void WidenIV::widenWithVariantLoadUseCodegen(NarrowIVDefUse DU) {
  Instruction *NarrowUse = DU.NarrowUse;
  Instruction *NarrowDef = DU.NarrowDef;
  Instruction *WideDef = DU.WideDef;

  ExtendKind ExtKind = getExtendKind(NarrowDef);

  LLVM_DEBUG(dbgs() << "Cloning arithmetic IVUser: " << *NarrowUse << "\n");

  // Generating a widening use instruction.
  Value *LHS = (NarrowUse->getOperand(0) == NarrowDef)
                   ? WideDef
                   : createExtendInst(NarrowUse->getOperand(0), WideType,
                                      ExtKind, NarrowUse);
  Value *RHS = (NarrowUse->getOperand(1) == NarrowDef)
                   ? WideDef
                   : createExtendInst(NarrowUse->getOperand(1), WideType,
                                      ExtKind, NarrowUse);

  auto *NarrowBO = cast<BinaryOperator>(NarrowUse);
  auto *WideBO = BinaryOperator::Create(NarrowBO->getOpcode(), LHS, RHS,
                                        NarrowBO->getName());
  IRBuilder<> Builder(NarrowUse);
  Builder.Insert(WideBO);
  WideBO->copyIRFlags(NarrowBO);

  if (ExtKind == SignExtended)
    ExtendKindMap[NarrowUse] = SignExtended;
  else
    ExtendKindMap[NarrowUse] = ZeroExtended;

  // Update the Use.
  if (ExtKind == SignExtended) {
    for (Use &U : NarrowUse->uses()) {
      SExtInst *User = dyn_cast<SExtInst>(U.getUser());
      if (User && User->getType() == WideType) {
        LLVM_DEBUG(dbgs() << "INDVARS: eliminating " << *User << " replaced by "
                          << *WideBO << "\n");
        ++NumElimExt;
        User->replaceAllUsesWith(WideBO);
        DeadInsts.emplace_back(User);
      }
    }
  } else { // ExtKind == ZeroExtended
    for (Use &U : NarrowUse->uses()) {
      ZExtInst *User = dyn_cast<ZExtInst>(U.getUser());
      if (User && User->getType() == WideType) {
        LLVM_DEBUG(dbgs() << "INDVARS: eliminating " << *User << " replaced by "
                          << *WideBO << "\n");
        ++NumElimExt;
        User->replaceAllUsesWith(WideBO);
        DeadInsts.emplace_back(User);
      }
    }
  }
}

/// Determine whether an individual user of the narrow IV can be widened. If so,
/// return the wide clone of the user.
Instruction *WidenIV::widenIVUse(NarrowIVDefUse DU, SCEVExpander &Rewriter) {
  assert(ExtendKindMap.count(DU.NarrowDef) &&
         "Should already know the kind of extension used to widen NarrowDef");

  // Stop traversing the def-use chain at inner-loop phis or post-loop phis.
  if (PHINode *UsePhi = dyn_cast<PHINode>(DU.NarrowUse)) {
    if (LI->getLoopFor(UsePhi->getParent()) != L) {
      // For LCSSA phis, sink the truncate outside the loop.
      // After SimplifyCFG most loop exit targets have a single predecessor.
      // Otherwise fall back to a truncate within the loop.
      if (UsePhi->getNumOperands() != 1)
        truncateIVUse(DU, DT, LI);
      else {
        // Widening the PHI requires us to insert a trunc.  The logical place
        // for this trunc is in the same BB as the PHI.  This is not possible if
        // the BB is terminated by a catchswitch.
        if (isa<CatchSwitchInst>(UsePhi->getParent()->getTerminator()))
          return nullptr;

        PHINode *WidePhi =
          PHINode::Create(DU.WideDef->getType(), 1, UsePhi->getName() + ".wide",
                          UsePhi);
        WidePhi->addIncoming(DU.WideDef, UsePhi->getIncomingBlock(0));
        IRBuilder<> Builder(&*WidePhi->getParent()->getFirstInsertionPt());
        Value *Trunc = Builder.CreateTrunc(WidePhi, DU.NarrowDef->getType());
        UsePhi->replaceAllUsesWith(Trunc);
        DeadInsts.emplace_back(UsePhi);
        LLVM_DEBUG(dbgs() << "INDVARS: Widen lcssa phi " << *UsePhi << " to "
                          << *WidePhi << "\n");
      }
      return nullptr;
    }
  }

  // This narrow use can be widened by a sext if it's non-negative or its narrow
  // def was widended by a sext. Same for zext.
  auto canWidenBySExt = [&]() {
    return DU.NeverNegative || getExtendKind(DU.NarrowDef) == SignExtended;
  };
  auto canWidenByZExt = [&]() {
    return DU.NeverNegative || getExtendKind(DU.NarrowDef) == ZeroExtended;
  };

  // Our raison d'etre! Eliminate sign and zero extension.
  if ((isa<SExtInst>(DU.NarrowUse) && canWidenBySExt()) ||
      (isa<ZExtInst>(DU.NarrowUse) && canWidenByZExt())) {
    Value *NewDef = DU.WideDef;
    if (DU.NarrowUse->getType() != WideType) {
      unsigned CastWidth = SE->getTypeSizeInBits(DU.NarrowUse->getType());
      unsigned IVWidth = SE->getTypeSizeInBits(WideType);
      if (CastWidth < IVWidth) {
        // The cast isn't as wide as the IV, so insert a Trunc.
        IRBuilder<> Builder(DU.NarrowUse);
        NewDef = Builder.CreateTrunc(DU.WideDef, DU.NarrowUse->getType());
      }
      else {
        // A wider extend was hidden behind a narrower one. This may induce
        // another round of IV widening in which the intermediate IV becomes
        // dead. It should be very rare.
        LLVM_DEBUG(dbgs() << "INDVARS: New IV " << *WidePhi
                          << " not wide enough to subsume " << *DU.NarrowUse
                          << "\n");
        DU.NarrowUse->replaceUsesOfWith(DU.NarrowDef, DU.WideDef);
        NewDef = DU.NarrowUse;
      }
    }
    if (NewDef != DU.NarrowUse) {
      LLVM_DEBUG(dbgs() << "INDVARS: eliminating " << *DU.NarrowUse
                        << " replaced by " << *DU.WideDef << "\n");
      ++NumElimExt;
      DU.NarrowUse->replaceAllUsesWith(NewDef);
      DeadInsts.emplace_back(DU.NarrowUse);
    }
    // Now that the extend is gone, we want to expose it's uses for potential
    // further simplification. We don't need to directly inform SimplifyIVUsers
    // of the new users, because their parent IV will be processed later as a
    // new loop phi. If we preserved IVUsers analysis, we would also want to
    // push the uses of WideDef here.

    // No further widening is needed. The deceased [sz]ext had done it for us.
    return nullptr;
  }

  // Does this user itself evaluate to a recurrence after widening?
  WidenedRecTy WideAddRec = getExtendedOperandRecurrence(DU);
  if (!WideAddRec.first)
    WideAddRec = getWideRecurrence(DU);

  assert((WideAddRec.first == nullptr) == (WideAddRec.second == Unknown));
  if (!WideAddRec.first) {
    // If use is a loop condition, try to promote the condition instead of
    // truncating the IV first.
    if (widenLoopCompare(DU))
      return nullptr;

    // We are here about to generate a truncate instruction that may hurt
    // performance because the scalar evolution expression computed earlier
    // in WideAddRec.first does not indicate a polynomial induction expression.
    // In that case, look at the operands of the use instruction to determine
    // if we can still widen the use instead of truncating its operand.
    if (widenWithVariantLoadUse(DU)) {
      widenWithVariantLoadUseCodegen(DU);
      return nullptr;
    }

    // This user does not evaluate to a recurrence after widening, so don't
    // follow it. Instead insert a Trunc to kill off the original use,
    // eventually isolating the original narrow IV so it can be removed.
    truncateIVUse(DU, DT, LI);
    return nullptr;
  }
  // Assume block terminators cannot evaluate to a recurrence. We can't to
  // insert a Trunc after a terminator if there happens to be a critical edge.
  assert(DU.NarrowUse != DU.NarrowUse->getParent()->getTerminator() &&
         "SCEV is not expected to evaluate a block terminator");

  // Reuse the IV increment that SCEVExpander created as long as it dominates
  // NarrowUse.
  Instruction *WideUse = nullptr;
  if (WideAddRec.first == WideIncExpr &&
      Rewriter.hoistIVInc(WideInc, DU.NarrowUse))
    WideUse = WideInc;
  else {
    WideUse = cloneIVUser(DU, WideAddRec.first);
    if (!WideUse)
      return nullptr;
  }
  // Evaluation of WideAddRec ensured that the narrow expression could be
  // extended outside the loop without overflow. This suggests that the wide use
  // evaluates to the same expression as the extended narrow use, but doesn't
  // absolutely guarantee it. Hence the following failsafe check. In rare cases
  // where it fails, we simply throw away the newly created wide use.
  if (WideAddRec.first != SE->getSCEV(WideUse)) {
    LLVM_DEBUG(dbgs() << "Wide use expression mismatch: " << *WideUse << ": "
                      << *SE->getSCEV(WideUse) << " != " << *WideAddRec.first
                      << "\n");
    DeadInsts.emplace_back(WideUse);
    return nullptr;
  }

  ExtendKindMap[DU.NarrowUse] = WideAddRec.second;
  // Returning WideUse pushes it on the worklist.
  return WideUse;
}

/// Add eligible users of NarrowDef to NarrowIVUsers.
void WidenIV::pushNarrowIVUsers(Instruction *NarrowDef, Instruction *WideDef) {
  const SCEV *NarrowSCEV = SE->getSCEV(NarrowDef);
  bool NonNegativeDef =
      SE->isKnownPredicate(ICmpInst::ICMP_SGE, NarrowSCEV,
                           SE->getConstant(NarrowSCEV->getType(), 0));
  for (User *U : NarrowDef->users()) {
    Instruction *NarrowUser = cast<Instruction>(U);

    // Handle data flow merges and bizarre phi cycles.
    if (!Widened.insert(NarrowUser).second)
      continue;

    bool NonNegativeUse = false;
    if (!NonNegativeDef) {
      // We might have a control-dependent range information for this context.
      if (auto RangeInfo = getPostIncRangeInfo(NarrowDef, NarrowUser))
        NonNegativeUse = RangeInfo->getSignedMin().isNonNegative();
    }

    NarrowIVUsers.emplace_back(NarrowDef, NarrowUser, WideDef,
                               NonNegativeDef || NonNegativeUse);
  }
}

/// Process a single induction variable. First use the SCEVExpander to create a
/// wide induction variable that evaluates to the same recurrence as the
/// original narrow IV. Then use a worklist to forward traverse the narrow IV's
/// def-use chain. After widenIVUse has processed all interesting IV users, the
/// narrow IV will be isolated for removal by DeleteDeadPHIs.
///
/// It would be simpler to delete uses as they are processed, but we must avoid
/// invalidating SCEV expressions.
PHINode *WidenIV::createWideIV(SCEVExpander &Rewriter) {
  // Is this phi an induction variable?
  const SCEVAddRecExpr *AddRec = dyn_cast<SCEVAddRecExpr>(SE->getSCEV(OrigPhi));
  if (!AddRec)
    return nullptr;

  // Widen the induction variable expression.
  const SCEV *WideIVExpr = getExtendKind(OrigPhi) == SignExtended
                               ? SE->getSignExtendExpr(AddRec, WideType)
                               : SE->getZeroExtendExpr(AddRec, WideType);

  assert(SE->getEffectiveSCEVType(WideIVExpr->getType()) == WideType &&
         "Expect the new IV expression to preserve its type");

  // Can the IV be extended outside the loop without overflow?
  AddRec = dyn_cast<SCEVAddRecExpr>(WideIVExpr);
  if (!AddRec || AddRec->getLoop() != L)
    return nullptr;

  // An AddRec must have loop-invariant operands. Since this AddRec is
  // materialized by a loop header phi, the expression cannot have any post-loop
  // operands, so they must dominate the loop header.
  assert(
      SE->properlyDominates(AddRec->getStart(), L->getHeader()) &&
      SE->properlyDominates(AddRec->getStepRecurrence(*SE), L->getHeader()) &&
      "Loop header phi recurrence inputs do not dominate the loop");

  // Iterate over IV uses (including transitive ones) looking for IV increments
  // of the form 'add nsw %iv, <const>'. For each increment and each use of
  // the increment calculate control-dependent range information basing on
  // dominating conditions inside of the loop (e.g. a range check inside of the
  // loop). Calculated ranges are stored in PostIncRangeInfos map.
  //
  // Control-dependent range information is later used to prove that a narrow
  // definition is not negative (see pushNarrowIVUsers). It's difficult to do
  // this on demand because when pushNarrowIVUsers needs this information some
  // of the dominating conditions might be already widened.
  if (UsePostIncrementRanges)
    calculatePostIncRanges(OrigPhi);

  // The rewriter provides a value for the desired IV expression. This may
  // either find an existing phi or materialize a new one. Either way, we
  // expect a well-formed cyclic phi-with-increments. i.e. any operand not part
  // of the phi-SCC dominates the loop entry.
  Instruction *InsertPt = &L->getHeader()->front();
  WidePhi = cast<PHINode>(Rewriter.expandCodeFor(AddRec, WideType, InsertPt));

  // Remembering the WideIV increment generated by SCEVExpander allows
  // widenIVUse to reuse it when widening the narrow IV's increment. We don't
  // employ a general reuse mechanism because the call above is the only call to
  // SCEVExpander. Henceforth, we produce 1-to-1 narrow to wide uses.
  if (BasicBlock *LatchBlock = L->getLoopLatch()) {
    WideInc =
      cast<Instruction>(WidePhi->getIncomingValueForBlock(LatchBlock));
    WideIncExpr = SE->getSCEV(WideInc);
    // Propagate the debug location associated with the original loop increment
    // to the new (widened) increment.
    auto *OrigInc =
      cast<Instruction>(OrigPhi->getIncomingValueForBlock(LatchBlock));
    WideInc->setDebugLoc(OrigInc->getDebugLoc());
  }

  LLVM_DEBUG(dbgs() << "Wide IV: " << *WidePhi << "\n");
  ++NumWidened;

  // Traverse the def-use chain using a worklist starting at the original IV.
  assert(Widened.empty() && NarrowIVUsers.empty() && "expect initial state" );

  Widened.insert(OrigPhi);
  pushNarrowIVUsers(OrigPhi, WidePhi);

  while (!NarrowIVUsers.empty()) {
    NarrowIVDefUse DU = NarrowIVUsers.pop_back_val();

    // Process a def-use edge. This may replace the use, so don't hold a
    // use_iterator across it.
    Instruction *WideUse = widenIVUse(DU, Rewriter);

    // Follow all def-use edges from the previous narrow use.
    if (WideUse)
      pushNarrowIVUsers(DU.NarrowUse, WideUse);

    // widenIVUse may have removed the def-use edge.
    if (DU.NarrowDef->use_empty())
      DeadInsts.emplace_back(DU.NarrowDef);
  }

  // Attach any debug information to the new PHI. Since OrigPhi and WidePHI
  // evaluate the same recurrence, we can just copy the debug info over.
  SmallVector<DbgValueInst *, 1> DbgValues;
  llvm::findDbgValues(DbgValues, OrigPhi);
  auto *MDPhi = MetadataAsValue::get(WidePhi->getContext(),
                                     ValueAsMetadata::get(WidePhi));
  for (auto &DbgValue : DbgValues)
    DbgValue->setOperand(0, MDPhi);
  return WidePhi;
}

/// Calculates control-dependent range for the given def at the given context
/// by looking at dominating conditions inside of the loop
void WidenIV::calculatePostIncRange(Instruction *NarrowDef,
                                    Instruction *NarrowUser) {
  using namespace llvm::PatternMatch;

  Value *NarrowDefLHS;
  const APInt *NarrowDefRHS;
  if (!match(NarrowDef, m_NSWAdd(m_Value(NarrowDefLHS),
                                 m_APInt(NarrowDefRHS))) ||
      !NarrowDefRHS->isNonNegative())
    return;

  auto UpdateRangeFromCondition = [&] (Value *Condition,
                                       bool TrueDest) {
    CmpInst::Predicate Pred;
    Value *CmpRHS;
    if (!match(Condition, m_ICmp(Pred, m_Specific(NarrowDefLHS),
                                 m_Value(CmpRHS))))
      return;

    CmpInst::Predicate P =
            TrueDest ? Pred : CmpInst::getInversePredicate(Pred);

    auto CmpRHSRange = SE->getSignedRange(SE->getSCEV(CmpRHS));
    auto CmpConstrainedLHSRange =
            ConstantRange::makeAllowedICmpRegion(P, CmpRHSRange);
    auto NarrowDefRange =
            CmpConstrainedLHSRange.addWithNoSignedWrap(*NarrowDefRHS);

    updatePostIncRangeInfo(NarrowDef, NarrowUser, NarrowDefRange);
  };

  auto UpdateRangeFromGuards = [&](Instruction *Ctx) {
    if (!HasGuards)
      return;

    for (Instruction &I : make_range(Ctx->getIterator().getReverse(),
                                     Ctx->getParent()->rend())) {
      Value *C = nullptr;
      if (match(&I, m_Intrinsic<Intrinsic::experimental_guard>(m_Value(C))))
        UpdateRangeFromCondition(C, /*TrueDest=*/true);
    }
  };

  UpdateRangeFromGuards(NarrowUser);

  BasicBlock *NarrowUserBB = NarrowUser->getParent();
  // If NarrowUserBB is statically unreachable asking dominator queries may
  // yield surprising results. (e.g. the block may not have a dom tree node)
  if (!DT->isReachableFromEntry(NarrowUserBB))
    return;

  for (auto *DTB = (*DT)[NarrowUserBB]->getIDom();
       L->contains(DTB->getBlock());
       DTB = DTB->getIDom()) {
    auto *BB = DTB->getBlock();
    auto *TI = BB->getTerminator();
    UpdateRangeFromGuards(TI);

    auto *BI = dyn_cast<BranchInst>(TI);
    if (!BI || !BI->isConditional())
      continue;

    auto *TrueSuccessor = BI->getSuccessor(0);
    auto *FalseSuccessor = BI->getSuccessor(1);

    auto DominatesNarrowUser = [this, NarrowUser] (BasicBlockEdge BBE) {
      return BBE.isSingleEdge() &&
             DT->dominates(BBE, NarrowUser->getParent());
    };

    if (DominatesNarrowUser(BasicBlockEdge(BB, TrueSuccessor)))
      UpdateRangeFromCondition(BI->getCondition(), /*TrueDest=*/true);

    if (DominatesNarrowUser(BasicBlockEdge(BB, FalseSuccessor)))
      UpdateRangeFromCondition(BI->getCondition(), /*TrueDest=*/false);
  }
}

/// Calculates PostIncRangeInfos map for the given IV
void WidenIV::calculatePostIncRanges(PHINode *OrigPhi) {
  SmallPtrSet<Instruction *, 16> Visited;
  SmallVector<Instruction *, 6> Worklist;
  Worklist.push_back(OrigPhi);
  Visited.insert(OrigPhi);

  while (!Worklist.empty()) {
    Instruction *NarrowDef = Worklist.pop_back_val();

    for (Use &U : NarrowDef->uses()) {
      auto *NarrowUser = cast<Instruction>(U.getUser());

      // Don't go looking outside the current loop.
      auto *NarrowUserLoop = (*LI)[NarrowUser->getParent()];
      if (!NarrowUserLoop || !L->contains(NarrowUserLoop))
        continue;

      if (!Visited.insert(NarrowUser).second)
        continue;

      Worklist.push_back(NarrowUser);

      calculatePostIncRange(NarrowDef, NarrowUser);
    }
  }
}

//===----------------------------------------------------------------------===//
//  Live IV Reduction - Minimize IVs live across the loop.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Simplification of IV users based on SCEV evaluation.
//===----------------------------------------------------------------------===//

namespace {

class IndVarSimplifyVisitor : public IVVisitor {
  ScalarEvolution *SE;
  const TargetTransformInfo *TTI;
  PHINode *IVPhi;

public:
  WideIVInfo WI;

  IndVarSimplifyVisitor(PHINode *IV, ScalarEvolution *SCEV,
                        const TargetTransformInfo *TTI,
                        const DominatorTree *DTree)
    : SE(SCEV), TTI(TTI), IVPhi(IV) {
    DT = DTree;
    WI.NarrowIV = IVPhi;
  }

  // Implement the interface used by simplifyUsersOfIV.
  void visitCast(CastInst *Cast) override { visitIVCast(Cast, WI, SE, TTI); }
};

} // end anonymous namespace

/// Iteratively perform simplification on a worklist of IV users. Each
/// successive simplification may push more users which may themselves be
/// candidates for simplification.
///
/// Sign/Zero extend elimination is interleaved with IV simplification.
bool IndVarSimplify::simplifyAndExtend(Loop *L,
                                       SCEVExpander &Rewriter,
                                       LoopInfo *LI) {
  SmallVector<WideIVInfo, 8> WideIVs;

  auto *GuardDecl = L->getBlocks()[0]->getModule()->getFunction(
          Intrinsic::getName(Intrinsic::experimental_guard));
  bool HasGuards = GuardDecl && !GuardDecl->use_empty();

  SmallVector<PHINode*, 8> LoopPhis;
  for (BasicBlock::iterator I = L->getHeader()->begin(); isa<PHINode>(I); ++I) {
    LoopPhis.push_back(cast<PHINode>(I));
  }
  // Each round of simplification iterates through the SimplifyIVUsers worklist
  // for all current phis, then determines whether any IVs can be
  // widened. Widening adds new phis to LoopPhis, inducing another round of
  // simplification on the wide IVs.
  bool Changed = false;
  while (!LoopPhis.empty()) {
    // Evaluate as many IV expressions as possible before widening any IVs. This
    // forces SCEV to set no-wrap flags before evaluating sign/zero
    // extension. The first time SCEV attempts to normalize sign/zero extension,
    // the result becomes final. So for the most predictable results, we delay
    // evaluation of sign/zero extend evaluation until needed, and avoid running
    // other SCEV based analysis prior to simplifyAndExtend.
    do {
      PHINode *CurrIV = LoopPhis.pop_back_val();

      // Information about sign/zero extensions of CurrIV.
      IndVarSimplifyVisitor Visitor(CurrIV, SE, TTI, DT);

      Changed |=
          simplifyUsersOfIV(CurrIV, SE, DT, LI, DeadInsts, Rewriter, &Visitor);

      if (Visitor.WI.WidestNativeType) {
        WideIVs.push_back(Visitor.WI);
      }
    } while(!LoopPhis.empty());

    for (; !WideIVs.empty(); WideIVs.pop_back()) {
      WidenIV Widener(WideIVs.back(), LI, SE, DT, DeadInsts, HasGuards);
      if (PHINode *WidePhi = Widener.createWideIV(Rewriter)) {
        Changed = true;
        LoopPhis.push_back(WidePhi);
      }
    }
  }
  return Changed;
}

//===----------------------------------------------------------------------===//
//  linearFunctionTestReplace and its kin. Rewrite the loop exit condition.
//===----------------------------------------------------------------------===//

/// Return true if this loop's backedge taken count expression can be safely and
/// cheaply expanded into an instruction sequence that can be used by
/// linearFunctionTestReplace.
///
/// TODO: This fails for pointer-type loop counters with greater than one byte
/// strides, consequently preventing LFTR from running. For the purpose of LFTR
/// we could skip this check in the case that the LFTR loop counter (chosen by
/// FindLoopCounter) is also pointer type. Instead, we could directly convert
/// the loop test to an inequality test by checking the target data's alignment
/// of element types (given that the initial pointer value originates from or is
/// used by ABI constrained operation, as opposed to inttoptr/ptrtoint).
/// However, we don't yet have a strong motivation for converting loop tests
/// into inequality tests.
static bool canExpandBackedgeTakenCount(Loop *L, ScalarEvolution *SE,
                                        SCEVExpander &Rewriter) {
  const SCEV *BackedgeTakenCount = SE->getBackedgeTakenCount(L);
  if (isa<SCEVCouldNotCompute>(BackedgeTakenCount) ||
      BackedgeTakenCount->isZero())
    return false;

  if (!L->getExitingBlock())
    return false;

  // Can't rewrite non-branch yet.
  if (!isa<BranchInst>(L->getExitingBlock()->getTerminator()))
    return false;

  if (Rewriter.isHighCostExpansion(BackedgeTakenCount, L))
    return false;

  return true;
}

/// Return the loop header phi IFF IncV adds a loop invariant value to the phi.
static PHINode *getLoopPhiForCounter(Value *IncV, Loop *L, DominatorTree *DT) {
  Instruction *IncI = dyn_cast<Instruction>(IncV);
  if (!IncI)
    return nullptr;

  switch (IncI->getOpcode()) {
  case Instruction::Add:
  case Instruction::Sub:
    break;
  case Instruction::GetElementPtr:
    // An IV counter must preserve its type.
    if (IncI->getNumOperands() == 2)
      break;
    LLVM_FALLTHROUGH;
  default:
    return nullptr;
  }

  PHINode *Phi = dyn_cast<PHINode>(IncI->getOperand(0));
  if (Phi && Phi->getParent() == L->getHeader()) {
    if (isLoopInvariant(IncI->getOperand(1), L, DT))
      return Phi;
    return nullptr;
  }
  if (IncI->getOpcode() == Instruction::GetElementPtr)
    return nullptr;

  // Allow add/sub to be commuted.
  Phi = dyn_cast<PHINode>(IncI->getOperand(1));
  if (Phi && Phi->getParent() == L->getHeader()) {
    if (isLoopInvariant(IncI->getOperand(0), L, DT))
      return Phi;
  }
  return nullptr;
}

/// Return the compare guarding the loop latch, or NULL for unrecognized tests.
static ICmpInst *getLoopTest(Loop *L) {
  assert(L->getExitingBlock() && "expected loop exit");

  BasicBlock *LatchBlock = L->getLoopLatch();
  // Don't bother with LFTR if the loop is not properly simplified.
  if (!LatchBlock)
    return nullptr;

  BranchInst *BI = dyn_cast<BranchInst>(L->getExitingBlock()->getTerminator());
  assert(BI && "expected exit branch");

  return dyn_cast<ICmpInst>(BI->getCondition());
}

/// linearFunctionTestReplace policy. Return true unless we can show that the
/// current exit test is already sufficiently canonical.
static bool needsLFTR(Loop *L, DominatorTree *DT) {
  // Do LFTR to simplify the exit condition to an ICMP.
  ICmpInst *Cond = getLoopTest(L);
  if (!Cond)
    return true;

  // Do LFTR to simplify the exit ICMP to EQ/NE
  ICmpInst::Predicate Pred = Cond->getPredicate();
  if (Pred != ICmpInst::ICMP_NE && Pred != ICmpInst::ICMP_EQ)
    return true;

  // Look for a loop invariant RHS
  Value *LHS = Cond->getOperand(0);
  Value *RHS = Cond->getOperand(1);
  if (!isLoopInvariant(RHS, L, DT)) {
    if (!isLoopInvariant(LHS, L, DT))
      return true;
    std::swap(LHS, RHS);
  }
  // Look for a simple IV counter LHS
  PHINode *Phi = dyn_cast<PHINode>(LHS);
  if (!Phi)
    Phi = getLoopPhiForCounter(LHS, L, DT);

  if (!Phi)
    return true;

  // Do LFTR if PHI node is defined in the loop, but is *not* a counter.
  int Idx = Phi->getBasicBlockIndex(L->getLoopLatch());
  if (Idx < 0)
    return true;

  // Do LFTR if the exit condition's IV is *not* a simple counter.
  Value *IncV = Phi->getIncomingValue(Idx);
  return Phi != getLoopPhiForCounter(IncV, L, DT);
}

/// Recursive helper for hasConcreteDef(). Unfortunately, this currently boils
/// down to checking that all operands are constant and listing instructions
/// that may hide undef.
static bool hasConcreteDefImpl(Value *V, SmallPtrSetImpl<Value*> &Visited,
                               unsigned Depth) {
  if (isa<Constant>(V))
    return !isa<UndefValue>(V);

  if (Depth >= 6)
    return false;

  // Conservatively handle non-constant non-instructions. For example, Arguments
  // may be undef.
  Instruction *I = dyn_cast<Instruction>(V);
  if (!I)
    return false;

  // Load and return values may be undef.
  if(I->mayReadFromMemory() || isa<CallInst>(I) || isa<InvokeInst>(I))
    return false;

  // Optimistically handle other instructions.
  for (Value *Op : I->operands()) {
    if (!Visited.insert(Op).second)
      continue;
    if (!hasConcreteDefImpl(Op, Visited, Depth+1))
      return false;
  }
  return true;
}

/// Return true if the given value is concrete. We must prove that undef can
/// never reach it.
///
/// TODO: If we decide that this is a good approach to checking for undef, we
/// may factor it into a common location.
static bool hasConcreteDef(Value *V) {
  SmallPtrSet<Value*, 8> Visited;
  Visited.insert(V);
  return hasConcreteDefImpl(V, Visited, 0);
}

/// Return true if this IV has any uses other than the (soon to be rewritten)
/// loop exit test.
static bool AlmostDeadIV(PHINode *Phi, BasicBlock *LatchBlock, Value *Cond) {
  int LatchIdx = Phi->getBasicBlockIndex(LatchBlock);
  Value *IncV = Phi->getIncomingValue(LatchIdx);

  for (User *U : Phi->users())
    if (U != Cond && U != IncV) return false;

  for (User *U : IncV->users())
    if (U != Cond && U != Phi) return false;
  return true;
}

/// Find an affine IV in canonical form.
///
/// BECount may be an i8* pointer type. The pointer difference is already
/// valid count without scaling the address stride, so it remains a pointer
/// expression as far as SCEV is concerned.
///
/// Currently only valid for LFTR. See the comments on hasConcreteDef below.
///
/// FIXME: Accept -1 stride and set IVLimit = IVInit - BECount
///
/// FIXME: Accept non-unit stride as long as SCEV can reduce BECount * Stride.
/// This is difficult in general for SCEV because of potential overflow. But we
/// could at least handle constant BECounts.
static PHINode *FindLoopCounter(Loop *L, const SCEV *BECount,
                                ScalarEvolution *SE, DominatorTree *DT) {
  uint64_t BCWidth = SE->getTypeSizeInBits(BECount->getType());

  Value *Cond =
    cast<BranchInst>(L->getExitingBlock()->getTerminator())->getCondition();

  // Loop over all of the PHI nodes, looking for a simple counter.
  PHINode *BestPhi = nullptr;
  const SCEV *BestInit = nullptr;
  BasicBlock *LatchBlock = L->getLoopLatch();
  assert(LatchBlock && "needsLFTR should guarantee a loop latch");
  const DataLayout &DL = L->getHeader()->getModule()->getDataLayout();

  for (BasicBlock::iterator I = L->getHeader()->begin(); isa<PHINode>(I); ++I) {
    PHINode *Phi = cast<PHINode>(I);
    if (!SE->isSCEVable(Phi->getType()))
      continue;

    // Avoid comparing an integer IV against a pointer Limit.
    if (BECount->getType()->isPointerTy() && !Phi->getType()->isPointerTy())
      continue;

    const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(SE->getSCEV(Phi));
    if (!AR || AR->getLoop() != L || !AR->isAffine())
      continue;

    // AR may be a pointer type, while BECount is an integer type.
    // AR may be wider than BECount. With eq/ne tests overflow is immaterial.
    // AR may not be a narrower type, or we may never exit.
    uint64_t PhiWidth = SE->getTypeSizeInBits(AR->getType());
    if (PhiWidth < BCWidth || !DL.isLegalInteger(PhiWidth))
      continue;

    const SCEV *Step = dyn_cast<SCEVConstant>(AR->getStepRecurrence(*SE));
    if (!Step || !Step->isOne())
      continue;

    int LatchIdx = Phi->getBasicBlockIndex(LatchBlock);
    Value *IncV = Phi->getIncomingValue(LatchIdx);
    if (getLoopPhiForCounter(IncV, L, DT) != Phi)
      continue;

    // Avoid reusing a potentially undef value to compute other values that may
    // have originally had a concrete definition.
    if (!hasConcreteDef(Phi)) {
      // We explicitly allow unknown phis as long as they are already used by
      // the loop test. In this case we assume that performing LFTR could not
      // increase the number of undef users.
      if (ICmpInst *Cond = getLoopTest(L)) {
        if (Phi != getLoopPhiForCounter(Cond->getOperand(0), L, DT) &&
            Phi != getLoopPhiForCounter(Cond->getOperand(1), L, DT)) {
          continue;
        }
      }
    }
    const SCEV *Init = AR->getStart();

    if (BestPhi && !AlmostDeadIV(BestPhi, LatchBlock, Cond)) {
      // Don't force a live loop counter if another IV can be used.
      if (AlmostDeadIV(Phi, LatchBlock, Cond))
        continue;

      // Prefer to count-from-zero. This is a more "canonical" counter form. It
      // also prefers integer to pointer IVs.
      if (BestInit->isZero() != Init->isZero()) {
        if (BestInit->isZero())
          continue;
      }
      // If two IVs both count from zero or both count from nonzero then the
      // narrower is likely a dead phi that has been widened. Use the wider phi
      // to allow the other to be eliminated.
      else if (PhiWidth <= SE->getTypeSizeInBits(BestPhi->getType()))
        continue;
    }
    BestPhi = Phi;
    BestInit = Init;
  }
  return BestPhi;
}

/// Help linearFunctionTestReplace by generating a value that holds the RHS of
/// the new loop test.
static Value *genLoopLimit(PHINode *IndVar, const SCEV *IVCount, Loop *L,
                           SCEVExpander &Rewriter, ScalarEvolution *SE) {
  const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(SE->getSCEV(IndVar));
  assert(AR && AR->getLoop() == L && AR->isAffine() && "bad loop counter");
  const SCEV *IVInit = AR->getStart();

  // IVInit may be a pointer while IVCount is an integer when FindLoopCounter
  // finds a valid pointer IV. Sign extend BECount in order to materialize a
  // GEP. Avoid running SCEVExpander on a new pointer value, instead reusing
  // the existing GEPs whenever possible.
  if (IndVar->getType()->isPointerTy() && !IVCount->getType()->isPointerTy()) {
    // IVOffset will be the new GEP offset that is interpreted by GEP as a
    // signed value. IVCount on the other hand represents the loop trip count,
    // which is an unsigned value. FindLoopCounter only allows induction
    // variables that have a positive unit stride of one. This means we don't
    // have to handle the case of negative offsets (yet) and just need to zero
    // extend IVCount.
    Type *OfsTy = SE->getEffectiveSCEVType(IVInit->getType());
    const SCEV *IVOffset = SE->getTruncateOrZeroExtend(IVCount, OfsTy);

    // Expand the code for the iteration count.
    assert(SE->isLoopInvariant(IVOffset, L) &&
           "Computed iteration count is not loop invariant!");
    BranchInst *BI = cast<BranchInst>(L->getExitingBlock()->getTerminator());
    Value *GEPOffset = Rewriter.expandCodeFor(IVOffset, OfsTy, BI);

    Value *GEPBase = IndVar->getIncomingValueForBlock(L->getLoopPreheader());
    assert(AR->getStart() == SE->getSCEV(GEPBase) && "bad loop counter");
    // We could handle pointer IVs other than i8*, but we need to compensate for
    // gep index scaling. See canExpandBackedgeTakenCount comments.
    assert(SE->getSizeOfExpr(IntegerType::getInt64Ty(IndVar->getContext()),
                             cast<PointerType>(GEPBase->getType())
                                 ->getElementType())->isOne() &&
           "unit stride pointer IV must be i8*");

    IRBuilder<> Builder(L->getLoopPreheader()->getTerminator());
    return Builder.CreateGEP(nullptr, GEPBase, GEPOffset, "lftr.limit");
  } else {
    // In any other case, convert both IVInit and IVCount to integers before
    // comparing. This may result in SCEV expansion of pointers, but in practice
    // SCEV will fold the pointer arithmetic away as such:
    // BECount = (IVEnd - IVInit - 1) => IVLimit = IVInit (postinc).
    //
    // Valid Cases: (1) both integers is most common; (2) both may be pointers
    // for simple memset-style loops.
    //
    // IVInit integer and IVCount pointer would only occur if a canonical IV
    // were generated on top of case #2, which is not expected.

    const SCEV *IVLimit = nullptr;
    // For unit stride, IVCount = Start + BECount with 2's complement overflow.
    // For non-zero Start, compute IVCount here.
    if (AR->getStart()->isZero())
      IVLimit = IVCount;
    else {
      assert(AR->getStepRecurrence(*SE)->isOne() && "only handles unit stride");
      const SCEV *IVInit = AR->getStart();

      // For integer IVs, truncate the IV before computing IVInit + BECount.
      if (SE->getTypeSizeInBits(IVInit->getType())
          > SE->getTypeSizeInBits(IVCount->getType()))
        IVInit = SE->getTruncateExpr(IVInit, IVCount->getType());

      IVLimit = SE->getAddExpr(IVInit, IVCount);
    }
    // Expand the code for the iteration count.
    BranchInst *BI = cast<BranchInst>(L->getExitingBlock()->getTerminator());
    IRBuilder<> Builder(BI);
    assert(SE->isLoopInvariant(IVLimit, L) &&
           "Computed iteration count is not loop invariant!");
    // Ensure that we generate the same type as IndVar, or a smaller integer
    // type. In the presence of null pointer values, we have an integer type
    // SCEV expression (IVInit) for a pointer type IV value (IndVar).
    Type *LimitTy = IVCount->getType()->isPointerTy() ?
      IndVar->getType() : IVCount->getType();
    return Rewriter.expandCodeFor(IVLimit, LimitTy, BI);
  }
}

/// This method rewrites the exit condition of the loop to be a canonical !=
/// comparison against the incremented loop induction variable.  This pass is
/// able to rewrite the exit tests of any loop where the SCEV analysis can
/// determine a loop-invariant trip count of the loop, which is actually a much
/// broader range than just linear tests.
bool IndVarSimplify::
linearFunctionTestReplace(Loop *L, const SCEV *BackedgeTakenCount,
                          PHINode *IndVar, SCEVExpander &Rewriter) {
  assert(canExpandBackedgeTakenCount(L, SE, Rewriter) && "precondition");

  // Initialize CmpIndVar and IVCount to their preincremented values.
  Value *CmpIndVar = IndVar;
  const SCEV *IVCount = BackedgeTakenCount;

  assert(L->getLoopLatch() && "Loop no longer in simplified form?");

  // If the exiting block is the same as the backedge block, we prefer to
  // compare against the post-incremented value, otherwise we must compare
  // against the preincremented value.
  if (L->getExitingBlock() == L->getLoopLatch()) {
    // Add one to the "backedge-taken" count to get the trip count.
    // This addition may overflow, which is valid as long as the comparison is
    // truncated to BackedgeTakenCount->getType().
    IVCount = SE->getAddExpr(BackedgeTakenCount,
                             SE->getOne(BackedgeTakenCount->getType()));
    // The BackedgeTaken expression contains the number of times that the
    // backedge branches to the loop header.  This is one less than the
    // number of times the loop executes, so use the incremented indvar.
    CmpIndVar = IndVar->getIncomingValueForBlock(L->getExitingBlock());
  }

  Value *ExitCnt = genLoopLimit(IndVar, IVCount, L, Rewriter, SE);
  assert(ExitCnt->getType()->isPointerTy() ==
             IndVar->getType()->isPointerTy() &&
         "genLoopLimit missed a cast");

  // Insert a new icmp_ne or icmp_eq instruction before the branch.
  BranchInst *BI = cast<BranchInst>(L->getExitingBlock()->getTerminator());
  ICmpInst::Predicate P;
  if (L->contains(BI->getSuccessor(0)))
    P = ICmpInst::ICMP_NE;
  else
    P = ICmpInst::ICMP_EQ;

  LLVM_DEBUG(dbgs() << "INDVARS: Rewriting loop exit condition to:\n"
                    << "      LHS:" << *CmpIndVar << '\n'
                    << "       op:\t" << (P == ICmpInst::ICMP_NE ? "!=" : "==")
                    << "\n"
                    << "      RHS:\t" << *ExitCnt << "\n"
                    << "  IVCount:\t" << *IVCount << "\n");

  IRBuilder<> Builder(BI);

  // The new loop exit condition should reuse the debug location of the
  // original loop exit condition.
  if (auto *Cond = dyn_cast<Instruction>(BI->getCondition()))
    Builder.SetCurrentDebugLocation(Cond->getDebugLoc());

  // LFTR can ignore IV overflow and truncate to the width of
  // BECount. This avoids materializing the add(zext(add)) expression.
  unsigned CmpIndVarSize = SE->getTypeSizeInBits(CmpIndVar->getType());
  unsigned ExitCntSize = SE->getTypeSizeInBits(ExitCnt->getType());
  if (CmpIndVarSize > ExitCntSize) {
    const SCEVAddRecExpr *AR = cast<SCEVAddRecExpr>(SE->getSCEV(IndVar));
    const SCEV *ARStart = AR->getStart();
    const SCEV *ARStep = AR->getStepRecurrence(*SE);
    // For constant IVCount, avoid truncation.
    if (isa<SCEVConstant>(ARStart) && isa<SCEVConstant>(IVCount)) {
      const APInt &Start = cast<SCEVConstant>(ARStart)->getAPInt();
      APInt Count = cast<SCEVConstant>(IVCount)->getAPInt();
      // Note that the post-inc value of BackedgeTakenCount may have overflowed
      // above such that IVCount is now zero.
      if (IVCount != BackedgeTakenCount && Count == 0) {
        Count = APInt::getMaxValue(Count.getBitWidth()).zext(CmpIndVarSize);
        ++Count;
      }
      else
        Count = Count.zext(CmpIndVarSize);
      APInt NewLimit;
      if (cast<SCEVConstant>(ARStep)->getValue()->isNegative())
        NewLimit = Start - Count;
      else
        NewLimit = Start + Count;
      ExitCnt = ConstantInt::get(CmpIndVar->getType(), NewLimit);

      LLVM_DEBUG(dbgs() << "  Widen RHS:\t" << *ExitCnt << "\n");
    } else {
      // We try to extend trip count first. If that doesn't work we truncate IV.
      // Zext(trunc(IV)) == IV implies equivalence of the following two:
      // Trunc(IV) == ExitCnt and IV == zext(ExitCnt). Similarly for sext. If
      // one of the two holds, extend the trip count, otherwise we truncate IV.
      bool Extended = false;
      const SCEV *IV = SE->getSCEV(CmpIndVar);
      const SCEV *ZExtTrunc =
           SE->getZeroExtendExpr(SE->getTruncateExpr(SE->getSCEV(CmpIndVar),
                                                     ExitCnt->getType()),
                                 CmpIndVar->getType());

      if (ZExtTrunc == IV) {
        Extended = true;
        ExitCnt = Builder.CreateZExt(ExitCnt, IndVar->getType(),
                                     "wide.trip.count");
      } else {
        const SCEV *SExtTrunc =
          SE->getSignExtendExpr(SE->getTruncateExpr(SE->getSCEV(CmpIndVar),
                                                    ExitCnt->getType()),
                                CmpIndVar->getType());
        if (SExtTrunc == IV) {
          Extended = true;
          ExitCnt = Builder.CreateSExt(ExitCnt, IndVar->getType(),
                                       "wide.trip.count");
        }
      }

      if (!Extended)
        CmpIndVar = Builder.CreateTrunc(CmpIndVar, ExitCnt->getType(),
                                        "lftr.wideiv");
    }
  }
  Value *Cond = Builder.CreateICmp(P, CmpIndVar, ExitCnt, "exitcond");
  Value *OrigCond = BI->getCondition();
  // It's tempting to use replaceAllUsesWith here to fully replace the old
  // comparison, but that's not immediately safe, since users of the old
  // comparison may not be dominated by the new comparison. Instead, just
  // update the branch to use the new comparison; in the common case this
  // will make old comparison dead.
  BI->setCondition(Cond);
  DeadInsts.push_back(OrigCond);

  ++NumLFTR;
  return true;
}

//===----------------------------------------------------------------------===//
//  sinkUnusedInvariants. A late subpass to cleanup loop preheaders.
//===----------------------------------------------------------------------===//

/// If there's a single exit block, sink any loop-invariant values that
/// were defined in the preheader but not used inside the loop into the
/// exit block to reduce register pressure in the loop.
bool IndVarSimplify::sinkUnusedInvariants(Loop *L) {
  BasicBlock *ExitBlock = L->getExitBlock();
  if (!ExitBlock) return false;

  BasicBlock *Preheader = L->getLoopPreheader();
  if (!Preheader) return false;

  bool MadeAnyChanges = false;
  BasicBlock::iterator InsertPt = ExitBlock->getFirstInsertionPt();
  BasicBlock::iterator I(Preheader->getTerminator());
  while (I != Preheader->begin()) {
    --I;
    // New instructions were inserted at the end of the preheader.
    if (isa<PHINode>(I))
      break;

    // Don't move instructions which might have side effects, since the side
    // effects need to complete before instructions inside the loop.  Also don't
    // move instructions which might read memory, since the loop may modify
    // memory. Note that it's okay if the instruction might have undefined
    // behavior: LoopSimplify guarantees that the preheader dominates the exit
    // block.
    if (I->mayHaveSideEffects() || I->mayReadFromMemory())
      continue;

    // Skip debug info intrinsics.
    if (isa<DbgInfoIntrinsic>(I))
      continue;

    // Skip eh pad instructions.
    if (I->isEHPad())
      continue;

    // Don't sink alloca: we never want to sink static alloca's out of the
    // entry block, and correctly sinking dynamic alloca's requires
    // checks for stacksave/stackrestore intrinsics.
    // FIXME: Refactor this check somehow?
    if (isa<AllocaInst>(I))
      continue;

    // Determine if there is a use in or before the loop (direct or
    // otherwise).
    bool UsedInLoop = false;
    for (Use &U : I->uses()) {
      Instruction *User = cast<Instruction>(U.getUser());
      BasicBlock *UseBB = User->getParent();
      if (PHINode *P = dyn_cast<PHINode>(User)) {
        unsigned i =
          PHINode::getIncomingValueNumForOperand(U.getOperandNo());
        UseBB = P->getIncomingBlock(i);
      }
      if (UseBB == Preheader || L->contains(UseBB)) {
        UsedInLoop = true;
        break;
      }
    }

    // If there is, the def must remain in the preheader.
    if (UsedInLoop)
      continue;

    // Otherwise, sink it to the exit block.
    Instruction *ToMove = &*I;
    bool Done = false;

    if (I != Preheader->begin()) {
      // Skip debug info intrinsics.
      do {
        --I;
      } while (isa<DbgInfoIntrinsic>(I) && I != Preheader->begin());

      if (isa<DbgInfoIntrinsic>(I) && I == Preheader->begin())
        Done = true;
    } else {
      Done = true;
    }

    MadeAnyChanges = true;
    ToMove->moveBefore(*ExitBlock, InsertPt);
    if (Done) break;
    InsertPt = ToMove->getIterator();
  }

  return MadeAnyChanges;
}

//===----------------------------------------------------------------------===//
//  IndVarSimplify driver. Manage several subpasses of IV simplification.
//===----------------------------------------------------------------------===//

bool IndVarSimplify::run(Loop *L) {
  // We need (and expect!) the incoming loop to be in LCSSA.
  assert(L->isRecursivelyLCSSAForm(*DT, *LI) &&
         "LCSSA required to run indvars!");
  bool Changed = false;

  // If LoopSimplify form is not available, stay out of trouble. Some notes:
  //  - LSR currently only supports LoopSimplify-form loops. Indvars'
  //    canonicalization can be a pessimization without LSR to "clean up"
  //    afterwards.
  //  - We depend on having a preheader; in particular,
  //    Loop::getCanonicalInductionVariable only supports loops with preheaders,
  //    and we're in trouble if we can't find the induction variable even when
  //    we've manually inserted one.
  //  - LFTR relies on having a single backedge.
  if (!L->isLoopSimplifyForm())
    return false;

  // If there are any floating-point recurrences, attempt to
  // transform them to use integer recurrences.
  Changed |= rewriteNonIntegerIVs(L);

  const SCEV *BackedgeTakenCount = SE->getBackedgeTakenCount(L);

  // Create a rewriter object which we'll use to transform the code with.
  SCEVExpander Rewriter(*SE, DL, "indvars");
#ifndef NDEBUG
  Rewriter.setDebugType(DEBUG_TYPE);
#endif

  // Eliminate redundant IV users.
  //
  // Simplification works best when run before other consumers of SCEV. We
  // attempt to avoid evaluating SCEVs for sign/zero extend operations until
  // other expressions involving loop IVs have been evaluated. This helps SCEV
  // set no-wrap flags before normalizing sign/zero extension.
  Rewriter.disableCanonicalMode();
  Changed |= simplifyAndExtend(L, Rewriter, LI);

  // Check to see if this loop has a computable loop-invariant execution count.
  // If so, this means that we can compute the final value of any expressions
  // that are recurrent in the loop, and substitute the exit values from the
  // loop into any instructions outside of the loop that use the final values of
  // the current expressions.
  //
  if (ReplaceExitValue != NeverRepl &&
      !isa<SCEVCouldNotCompute>(BackedgeTakenCount))
    Changed |= rewriteLoopExitValues(L, Rewriter);

  // Eliminate redundant IV cycles.
  NumElimIV += Rewriter.replaceCongruentIVs(L, DT, DeadInsts);

  // If we have a trip count expression, rewrite the loop's exit condition
  // using it.  We can currently only handle loops with a single exit.
  if (!DisableLFTR && canExpandBackedgeTakenCount(L, SE, Rewriter) &&
      needsLFTR(L, DT)) {
    PHINode *IndVar = FindLoopCounter(L, BackedgeTakenCount, SE, DT);
    if (IndVar) {
      // Check preconditions for proper SCEVExpander operation. SCEV does not
      // express SCEVExpander's dependencies, such as LoopSimplify. Instead any
      // pass that uses the SCEVExpander must do it. This does not work well for
      // loop passes because SCEVExpander makes assumptions about all loops,
      // while LoopPassManager only forces the current loop to be simplified.
      //
      // FIXME: SCEV expansion has no way to bail out, so the caller must
      // explicitly check any assumptions made by SCEV. Brittle.
      const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(BackedgeTakenCount);
      if (!AR || AR->getLoop()->getLoopPreheader())
        Changed |= linearFunctionTestReplace(L, BackedgeTakenCount, IndVar,
                                             Rewriter);
    }
  }
  // Clear the rewriter cache, because values that are in the rewriter's cache
  // can be deleted in the loop below, causing the AssertingVH in the cache to
  // trigger.
  Rewriter.clear();

  // Now that we're done iterating through lists, clean up any instructions
  // which are now dead.
  while (!DeadInsts.empty())
    if (Instruction *Inst =
            dyn_cast_or_null<Instruction>(DeadInsts.pop_back_val()))
      Changed |= RecursivelyDeleteTriviallyDeadInstructions(Inst, TLI);

  // The Rewriter may not be used from this point on.

  // Loop-invariant instructions in the preheader that aren't used in the
  // loop may be sunk below the loop to reduce register pressure.
  Changed |= sinkUnusedInvariants(L);

  // rewriteFirstIterationLoopExitValues does not rely on the computation of
  // trip count and therefore can further simplify exit values in addition to
  // rewriteLoopExitValues.
  Changed |= rewriteFirstIterationLoopExitValues(L);

  // Clean up dead instructions.
  Changed |= DeleteDeadPHIs(L->getHeader(), TLI);

  // Check a post-condition.
  assert(L->isRecursivelyLCSSAForm(*DT, *LI) &&
         "Indvars did not preserve LCSSA!");

  // Verify that LFTR, and any other change have not interfered with SCEV's
  // ability to compute trip count.
#ifndef NDEBUG
  if (VerifyIndvars && !isa<SCEVCouldNotCompute>(BackedgeTakenCount)) {
    SE->forgetLoop(L);
    const SCEV *NewBECount = SE->getBackedgeTakenCount(L);
    if (SE->getTypeSizeInBits(BackedgeTakenCount->getType()) <
        SE->getTypeSizeInBits(NewBECount->getType()))
      NewBECount = SE->getTruncateOrNoop(NewBECount,
                                         BackedgeTakenCount->getType());
    else
      BackedgeTakenCount = SE->getTruncateOrNoop(BackedgeTakenCount,
                                                 NewBECount->getType());
    assert(BackedgeTakenCount == NewBECount && "indvars must preserve SCEV");
  }
#endif

  return Changed;
}

PreservedAnalyses IndVarSimplifyPass::run(Loop &L, LoopAnalysisManager &AM,
                                          LoopStandardAnalysisResults &AR,
                                          LPMUpdater &) {
  Function *F = L.getHeader()->getParent();
  const DataLayout &DL = F->getParent()->getDataLayout();

  IndVarSimplify IVS(&AR.LI, &AR.SE, &AR.DT, DL, &AR.TLI, &AR.TTI);
  if (!IVS.run(&L))
    return PreservedAnalyses::all();

  auto PA = getLoopPassPreservedAnalyses();
  PA.preserveSet<CFGAnalyses>();
  return PA;
}

namespace {

struct IndVarSimplifyLegacyPass : public LoopPass {
  static char ID; // Pass identification, replacement for typeid

  IndVarSimplifyLegacyPass() : LoopPass(ID) {
    initializeIndVarSimplifyLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnLoop(Loop *L, LPPassManager &LPM) override {
    if (skipLoop(L))
      return false;

    auto *LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    auto *SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
    auto *DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    auto *TLIP = getAnalysisIfAvailable<TargetLibraryInfoWrapperPass>();
    auto *TLI = TLIP ? &TLIP->getTLI() : nullptr;
    auto *TTIP = getAnalysisIfAvailable<TargetTransformInfoWrapperPass>();
    auto *TTI = TTIP ? &TTIP->getTTI(*L->getHeader()->getParent()) : nullptr;
    const DataLayout &DL = L->getHeader()->getModule()->getDataLayout();

    IndVarSimplify IVS(LI, SE, DT, DL, TLI, TTI);
    return IVS.run(L);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    getLoopAnalysisUsage(AU);
  }
};

} // end anonymous namespace

char IndVarSimplifyLegacyPass::ID = 0;

INITIALIZE_PASS_BEGIN(IndVarSimplifyLegacyPass, "indvars",
                      "Induction Variable Simplification", false, false)
INITIALIZE_PASS_DEPENDENCY(LoopPass)
INITIALIZE_PASS_END(IndVarSimplifyLegacyPass, "indvars",
                    "Induction Variable Simplification", false, false)

Pass *llvm::createIndVarSimplifyPass() {
  return new IndVarSimplifyLegacyPass();
}
