//===- SimplifyCFG.cpp - Code to perform CFG simplification ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Peephole optimize the CFG.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/EHPersonalities.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
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
#include "llvm/IR/NoFolder.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "simplifycfg"

// Chosen as 2 so as to be cheap, but still to have enough power to fold
// a select, so the "clamp" idiom (of a min followed by a max) will be caught.
// To catch this, we need to fold a compare and a select, hence '2' being the
// minimum reasonable default.
static cl::opt<unsigned> PHINodeFoldingThreshold(
    "phi-node-folding-threshold", cl::Hidden, cl::init(2),
    cl::desc(
        "Control the amount of phi node folding to perform (default = 2)"));

static cl::opt<bool> DupRet(
    "simplifycfg-dup-ret", cl::Hidden, cl::init(false),
    cl::desc("Duplicate return instructions into unconditional branches"));

static cl::opt<bool>
    SinkCommon("simplifycfg-sink-common", cl::Hidden, cl::init(true),
               cl::desc("Sink common instructions down to the end block"));

static cl::opt<bool> HoistCondStores(
    "simplifycfg-hoist-cond-stores", cl::Hidden, cl::init(true),
    cl::desc("Hoist conditional stores if an unconditional store precedes"));

static cl::opt<bool> MergeCondStores(
    "simplifycfg-merge-cond-stores", cl::Hidden, cl::init(true),
    cl::desc("Hoist conditional stores even if an unconditional store does not "
             "precede - hoist multiple conditional stores into a single "
             "predicated store"));

static cl::opt<bool> MergeCondStoresAggressively(
    "simplifycfg-merge-cond-stores-aggressively", cl::Hidden, cl::init(false),
    cl::desc("When merging conditional stores, do so even if the resultant "
             "basic blocks are unlikely to be if-converted as a result"));

static cl::opt<bool> SpeculateOneExpensiveInst(
    "speculate-one-expensive-inst", cl::Hidden, cl::init(true),
    cl::desc("Allow exactly one expensive instruction to be speculatively "
             "executed"));

static cl::opt<unsigned> MaxSpeculationDepth(
    "max-speculation-depth", cl::Hidden, cl::init(10),
    cl::desc("Limit maximum recursion depth when calculating costs of "
             "speculatively executed instructions"));

STATISTIC(NumBitMaps, "Number of switch instructions turned into bitmaps");
STATISTIC(NumLinearMaps,
          "Number of switch instructions turned into linear mapping");
STATISTIC(NumLookupTables,
          "Number of switch instructions turned into lookup tables");
STATISTIC(
    NumLookupTablesHoles,
    "Number of switch instructions turned into lookup tables (holes checked)");
STATISTIC(NumTableCmpReuses, "Number of reused switch table lookup compares");
STATISTIC(NumSinkCommons,
          "Number of common instructions sunk down to the end block");
STATISTIC(NumSpeculations, "Number of speculative executed instructions");

namespace {

// The first field contains the value that the switch produces when a certain
// case group is selected, and the second field is a vector containing the
// cases composing the case group.
using SwitchCaseResultVectorTy =
    SmallVector<std::pair<Constant *, SmallVector<ConstantInt *, 4>>, 2>;

// The first field contains the phi node that generates a result of the switch
// and the second field contains the value generated for a certain case in the
// switch for that PHI.
using SwitchCaseResultsTy = SmallVector<std::pair<PHINode *, Constant *>, 4>;

/// ValueEqualityComparisonCase - Represents a case of a switch.
struct ValueEqualityComparisonCase {
  ConstantInt *Value;
  BasicBlock *Dest;

  ValueEqualityComparisonCase(ConstantInt *Value, BasicBlock *Dest)
      : Value(Value), Dest(Dest) {}

  bool operator<(ValueEqualityComparisonCase RHS) const {
    // Comparing pointers is ok as we only rely on the order for uniquing.
    return Value < RHS.Value;
  }

  bool operator==(BasicBlock *RHSDest) const { return Dest == RHSDest; }
};

class SimplifyCFGOpt {
  const TargetTransformInfo &TTI;
  const DataLayout &DL;
  SmallPtrSetImpl<BasicBlock *> *LoopHeaders;
  const SimplifyCFGOptions &Options;
  bool Resimplify;

  Value *isValueEqualityComparison(Instruction *TI);
  BasicBlock *GetValueEqualityComparisonCases(
      Instruction *TI, std::vector<ValueEqualityComparisonCase> &Cases);
  bool SimplifyEqualityComparisonWithOnlyPredecessor(Instruction *TI,
                                                     BasicBlock *Pred,
                                                     IRBuilder<> &Builder);
  bool FoldValueComparisonIntoPredecessors(Instruction *TI,
                                           IRBuilder<> &Builder);

  bool SimplifyReturn(ReturnInst *RI, IRBuilder<> &Builder);
  bool SimplifyResume(ResumeInst *RI, IRBuilder<> &Builder);
  bool SimplifySingleResume(ResumeInst *RI);
  bool SimplifyCommonResume(ResumeInst *RI);
  bool SimplifyCleanupReturn(CleanupReturnInst *RI);
  bool SimplifyUnreachable(UnreachableInst *UI);
  bool SimplifySwitch(SwitchInst *SI, IRBuilder<> &Builder);
  bool SimplifyIndirectBr(IndirectBrInst *IBI);
  bool SimplifyUncondBranch(BranchInst *BI, IRBuilder<> &Builder);
  bool SimplifyCondBranch(BranchInst *BI, IRBuilder<> &Builder);

  bool tryToSimplifyUncondBranchWithICmpInIt(ICmpInst *ICI,
                                             IRBuilder<> &Builder);

public:
  SimplifyCFGOpt(const TargetTransformInfo &TTI, const DataLayout &DL,
                 SmallPtrSetImpl<BasicBlock *> *LoopHeaders,
                 const SimplifyCFGOptions &Opts)
      : TTI(TTI), DL(DL), LoopHeaders(LoopHeaders), Options(Opts) {}

  bool run(BasicBlock *BB);
  bool simplifyOnce(BasicBlock *BB);

  // Helper to set Resimplify and return change indication.
  bool requestResimplify() {
    Resimplify = true;
    return true;
  }
};

} // end anonymous namespace

/// Return true if it is safe to merge these two
/// terminator instructions together.
static bool
SafeToMergeTerminators(Instruction *SI1, Instruction *SI2,
                       SmallSetVector<BasicBlock *, 4> *FailBlocks = nullptr) {
  if (SI1 == SI2)
    return false; // Can't merge with self!

  // It is not safe to merge these two switch instructions if they have a common
  // successor, and if that successor has a PHI node, and if *that* PHI node has
  // conflicting incoming values from the two switch blocks.
  BasicBlock *SI1BB = SI1->getParent();
  BasicBlock *SI2BB = SI2->getParent();

  SmallPtrSet<BasicBlock *, 16> SI1Succs(succ_begin(SI1BB), succ_end(SI1BB));
  bool Fail = false;
  for (BasicBlock *Succ : successors(SI2BB))
    if (SI1Succs.count(Succ))
      for (BasicBlock::iterator BBI = Succ->begin(); isa<PHINode>(BBI); ++BBI) {
        PHINode *PN = cast<PHINode>(BBI);
        if (PN->getIncomingValueForBlock(SI1BB) !=
            PN->getIncomingValueForBlock(SI2BB)) {
          if (FailBlocks)
            FailBlocks->insert(Succ);
          Fail = true;
        }
      }

  return !Fail;
}

/// Return true if it is safe and profitable to merge these two terminator
/// instructions together, where SI1 is an unconditional branch. PhiNodes will
/// store all PHI nodes in common successors.
static bool
isProfitableToFoldUnconditional(BranchInst *SI1, BranchInst *SI2,
                                Instruction *Cond,
                                SmallVectorImpl<PHINode *> &PhiNodes) {
  if (SI1 == SI2)
    return false; // Can't merge with self!
  assert(SI1->isUnconditional() && SI2->isConditional());

  // We fold the unconditional branch if we can easily update all PHI nodes in
  // common successors:
  // 1> We have a constant incoming value for the conditional branch;
  // 2> We have "Cond" as the incoming value for the unconditional branch;
  // 3> SI2->getCondition() and Cond have same operands.
  CmpInst *Ci2 = dyn_cast<CmpInst>(SI2->getCondition());
  if (!Ci2)
    return false;
  if (!(Cond->getOperand(0) == Ci2->getOperand(0) &&
        Cond->getOperand(1) == Ci2->getOperand(1)) &&
      !(Cond->getOperand(0) == Ci2->getOperand(1) &&
        Cond->getOperand(1) == Ci2->getOperand(0)))
    return false;

  BasicBlock *SI1BB = SI1->getParent();
  BasicBlock *SI2BB = SI2->getParent();
  SmallPtrSet<BasicBlock *, 16> SI1Succs(succ_begin(SI1BB), succ_end(SI1BB));
  for (BasicBlock *Succ : successors(SI2BB))
    if (SI1Succs.count(Succ))
      for (BasicBlock::iterator BBI = Succ->begin(); isa<PHINode>(BBI); ++BBI) {
        PHINode *PN = cast<PHINode>(BBI);
        if (PN->getIncomingValueForBlock(SI1BB) != Cond ||
            !isa<ConstantInt>(PN->getIncomingValueForBlock(SI2BB)))
          return false;
        PhiNodes.push_back(PN);
      }
  return true;
}

/// Update PHI nodes in Succ to indicate that there will now be entries in it
/// from the 'NewPred' block. The values that will be flowing into the PHI nodes
/// will be the same as those coming in from ExistPred, an existing predecessor
/// of Succ.
static void AddPredecessorToBlock(BasicBlock *Succ, BasicBlock *NewPred,
                                  BasicBlock *ExistPred) {
  for (PHINode &PN : Succ->phis())
    PN.addIncoming(PN.getIncomingValueForBlock(ExistPred), NewPred);
}

/// Compute an abstract "cost" of speculating the given instruction,
/// which is assumed to be safe to speculate. TCC_Free means cheap,
/// TCC_Basic means less cheap, and TCC_Expensive means prohibitively
/// expensive.
static unsigned ComputeSpeculationCost(const User *I,
                                       const TargetTransformInfo &TTI) {
  assert(isSafeToSpeculativelyExecute(I) &&
         "Instruction is not safe to speculatively execute!");
  return TTI.getUserCost(I);
}

/// If we have a merge point of an "if condition" as accepted above,
/// return true if the specified value dominates the block.  We
/// don't handle the true generality of domination here, just a special case
/// which works well enough for us.
///
/// If AggressiveInsts is non-null, and if V does not dominate BB, we check to
/// see if V (which must be an instruction) and its recursive operands
/// that do not dominate BB have a combined cost lower than CostRemaining and
/// are non-trapping.  If both are true, the instruction is inserted into the
/// set and true is returned.
///
/// The cost for most non-trapping instructions is defined as 1 except for
/// Select whose cost is 2.
///
/// After this function returns, CostRemaining is decreased by the cost of
/// V plus its non-dominating operands.  If that cost is greater than
/// CostRemaining, false is returned and CostRemaining is undefined.
static bool DominatesMergePoint(Value *V, BasicBlock *BB,
                                SmallPtrSetImpl<Instruction *> &AggressiveInsts,
                                unsigned &CostRemaining,
                                const TargetTransformInfo &TTI,
                                unsigned Depth = 0) {
  // It is possible to hit a zero-cost cycle (phi/gep instructions for example),
  // so limit the recursion depth.
  // TODO: While this recursion limit does prevent pathological behavior, it
  // would be better to track visited instructions to avoid cycles.
  if (Depth == MaxSpeculationDepth)
    return false;

  Instruction *I = dyn_cast<Instruction>(V);
  if (!I) {
    // Non-instructions all dominate instructions, but not all constantexprs
    // can be executed unconditionally.
    if (ConstantExpr *C = dyn_cast<ConstantExpr>(V))
      if (C->canTrap())
        return false;
    return true;
  }
  BasicBlock *PBB = I->getParent();

  // We don't want to allow weird loops that might have the "if condition" in
  // the bottom of this block.
  if (PBB == BB)
    return false;

  // If this instruction is defined in a block that contains an unconditional
  // branch to BB, then it must be in the 'conditional' part of the "if
  // statement".  If not, it definitely dominates the region.
  BranchInst *BI = dyn_cast<BranchInst>(PBB->getTerminator());
  if (!BI || BI->isConditional() || BI->getSuccessor(0) != BB)
    return true;

  // If we have seen this instruction before, don't count it again.
  if (AggressiveInsts.count(I))
    return true;

  // Okay, it looks like the instruction IS in the "condition".  Check to
  // see if it's a cheap instruction to unconditionally compute, and if it
  // only uses stuff defined outside of the condition.  If so, hoist it out.
  if (!isSafeToSpeculativelyExecute(I))
    return false;

  unsigned Cost = ComputeSpeculationCost(I, TTI);

  // Allow exactly one instruction to be speculated regardless of its cost
  // (as long as it is safe to do so).
  // This is intended to flatten the CFG even if the instruction is a division
  // or other expensive operation. The speculation of an expensive instruction
  // is expected to be undone in CodeGenPrepare if the speculation has not
  // enabled further IR optimizations.
  if (Cost > CostRemaining &&
      (!SpeculateOneExpensiveInst || !AggressiveInsts.empty() || Depth > 0))
    return false;

  // Avoid unsigned wrap.
  CostRemaining = (Cost > CostRemaining) ? 0 : CostRemaining - Cost;

  // Okay, we can only really hoist these out if their operands do
  // not take us over the cost threshold.
  for (User::op_iterator i = I->op_begin(), e = I->op_end(); i != e; ++i)
    if (!DominatesMergePoint(*i, BB, AggressiveInsts, CostRemaining, TTI,
                             Depth + 1))
      return false;
  // Okay, it's safe to do this!  Remember this instruction.
  AggressiveInsts.insert(I);
  return true;
}

/// Extract ConstantInt from value, looking through IntToPtr
/// and PointerNullValue. Return NULL if value is not a constant int.
static ConstantInt *GetConstantInt(Value *V, const DataLayout &DL) {
  // Normal constant int.
  ConstantInt *CI = dyn_cast<ConstantInt>(V);
  if (CI || !isa<Constant>(V) || !V->getType()->isPointerTy())
    return CI;

  // This is some kind of pointer constant. Turn it into a pointer-sized
  // ConstantInt if possible.
  IntegerType *PtrTy = cast<IntegerType>(DL.getIntPtrType(V->getType()));

  // Null pointer means 0, see SelectionDAGBuilder::getValue(const Value*).
  if (isa<ConstantPointerNull>(V))
    return ConstantInt::get(PtrTy, 0);

  // IntToPtr const int.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(V))
    if (CE->getOpcode() == Instruction::IntToPtr)
      if (ConstantInt *CI = dyn_cast<ConstantInt>(CE->getOperand(0))) {
        // The constant is very likely to have the right type already.
        if (CI->getType() == PtrTy)
          return CI;
        else
          return cast<ConstantInt>(
              ConstantExpr::getIntegerCast(CI, PtrTy, /*isSigned=*/false));
      }
  return nullptr;
}

namespace {

/// Given a chain of or (||) or and (&&) comparison of a value against a
/// constant, this will try to recover the information required for a switch
/// structure.
/// It will depth-first traverse the chain of comparison, seeking for patterns
/// like %a == 12 or %a < 4 and combine them to produce a set of integer
/// representing the different cases for the switch.
/// Note that if the chain is composed of '||' it will build the set of elements
/// that matches the comparisons (i.e. any of this value validate the chain)
/// while for a chain of '&&' it will build the set elements that make the test
/// fail.
struct ConstantComparesGatherer {
  const DataLayout &DL;

  /// Value found for the switch comparison
  Value *CompValue = nullptr;

  /// Extra clause to be checked before the switch
  Value *Extra = nullptr;

  /// Set of integers to match in switch
  SmallVector<ConstantInt *, 8> Vals;

  /// Number of comparisons matched in the and/or chain
  unsigned UsedICmps = 0;

  /// Construct and compute the result for the comparison instruction Cond
  ConstantComparesGatherer(Instruction *Cond, const DataLayout &DL) : DL(DL) {
    gather(Cond);
  }

  ConstantComparesGatherer(const ConstantComparesGatherer &) = delete;
  ConstantComparesGatherer &
  operator=(const ConstantComparesGatherer &) = delete;

private:
  /// Try to set the current value used for the comparison, it succeeds only if
  /// it wasn't set before or if the new value is the same as the old one
  bool setValueOnce(Value *NewVal) {
    if (CompValue && CompValue != NewVal)
      return false;
    CompValue = NewVal;
    return (CompValue != nullptr);
  }

  /// Try to match Instruction "I" as a comparison against a constant and
  /// populates the array Vals with the set of values that match (or do not
  /// match depending on isEQ).
  /// Return false on failure. On success, the Value the comparison matched
  /// against is placed in CompValue.
  /// If CompValue is already set, the function is expected to fail if a match
  /// is found but the value compared to is different.
  bool matchInstruction(Instruction *I, bool isEQ) {
    // If this is an icmp against a constant, handle this as one of the cases.
    ICmpInst *ICI;
    ConstantInt *C;
    if (!((ICI = dyn_cast<ICmpInst>(I)) &&
          (C = GetConstantInt(I->getOperand(1), DL)))) {
      return false;
    }

    Value *RHSVal;
    const APInt *RHSC;

    // Pattern match a special case
    // (x & ~2^z) == y --> x == y || x == y|2^z
    // This undoes a transformation done by instcombine to fuse 2 compares.
    if (ICI->getPredicate() == (isEQ ? ICmpInst::ICMP_EQ : ICmpInst::ICMP_NE)) {
      // It's a little bit hard to see why the following transformations are
      // correct. Here is a CVC3 program to verify them for 64-bit values:

      /*
         ONE  : BITVECTOR(64) = BVZEROEXTEND(0bin1, 63);
         x    : BITVECTOR(64);
         y    : BITVECTOR(64);
         z    : BITVECTOR(64);
         mask : BITVECTOR(64) = BVSHL(ONE, z);
         QUERY( (y & ~mask = y) =>
                ((x & ~mask = y) <=> (x = y OR x = (y |  mask)))
         );
         QUERY( (y |  mask = y) =>
                ((x |  mask = y) <=> (x = y OR x = (y & ~mask)))
         );
      */

      // Please note that each pattern must be a dual implication (<--> or
      // iff). One directional implication can create spurious matches. If the
      // implication is only one-way, an unsatisfiable condition on the left
      // side can imply a satisfiable condition on the right side. Dual
      // implication ensures that satisfiable conditions are transformed to
      // other satisfiable conditions and unsatisfiable conditions are
      // transformed to other unsatisfiable conditions.

      // Here is a concrete example of a unsatisfiable condition on the left
      // implying a satisfiable condition on the right:
      //
      // mask = (1 << z)
      // (x & ~mask) == y  --> (x == y || x == (y | mask))
      //
      // Substituting y = 3, z = 0 yields:
      // (x & -2) == 3 --> (x == 3 || x == 2)

      // Pattern match a special case:
      /*
        QUERY( (y & ~mask = y) =>
               ((x & ~mask = y) <=> (x = y OR x = (y |  mask)))
        );
      */
      if (match(ICI->getOperand(0),
                m_And(m_Value(RHSVal), m_APInt(RHSC)))) {
        APInt Mask = ~*RHSC;
        if (Mask.isPowerOf2() && (C->getValue() & ~Mask) == C->getValue()) {
          // If we already have a value for the switch, it has to match!
          if (!setValueOnce(RHSVal))
            return false;

          Vals.push_back(C);
          Vals.push_back(
              ConstantInt::get(C->getContext(),
                               C->getValue() | Mask));
          UsedICmps++;
          return true;
        }
      }

      // Pattern match a special case:
      /*
        QUERY( (y |  mask = y) =>
               ((x |  mask = y) <=> (x = y OR x = (y & ~mask)))
        );
      */
      if (match(ICI->getOperand(0),
                m_Or(m_Value(RHSVal), m_APInt(RHSC)))) {
        APInt Mask = *RHSC;
        if (Mask.isPowerOf2() && (C->getValue() | Mask) == C->getValue()) {
          // If we already have a value for the switch, it has to match!
          if (!setValueOnce(RHSVal))
            return false;

          Vals.push_back(C);
          Vals.push_back(ConstantInt::get(C->getContext(),
                                          C->getValue() & ~Mask));
          UsedICmps++;
          return true;
        }
      }

      // If we already have a value for the switch, it has to match!
      if (!setValueOnce(ICI->getOperand(0)))
        return false;

      UsedICmps++;
      Vals.push_back(C);
      return ICI->getOperand(0);
    }

    // If we have "x ult 3", for example, then we can add 0,1,2 to the set.
    ConstantRange Span = ConstantRange::makeAllowedICmpRegion(
        ICI->getPredicate(), C->getValue());

    // Shift the range if the compare is fed by an add. This is the range
    // compare idiom as emitted by instcombine.
    Value *CandidateVal = I->getOperand(0);
    if (match(I->getOperand(0), m_Add(m_Value(RHSVal), m_APInt(RHSC)))) {
      Span = Span.subtract(*RHSC);
      CandidateVal = RHSVal;
    }

    // If this is an and/!= check, then we are looking to build the set of
    // value that *don't* pass the and chain. I.e. to turn "x ugt 2" into
    // x != 0 && x != 1.
    if (!isEQ)
      Span = Span.inverse();

    // If there are a ton of values, we don't want to make a ginormous switch.
    if (Span.isSizeLargerThan(8) || Span.isEmptySet()) {
      return false;
    }

    // If we already have a value for the switch, it has to match!
    if (!setValueOnce(CandidateVal))
      return false;

    // Add all values from the range to the set
    for (APInt Tmp = Span.getLower(); Tmp != Span.getUpper(); ++Tmp)
      Vals.push_back(ConstantInt::get(I->getContext(), Tmp));

    UsedICmps++;
    return true;
  }

  /// Given a potentially 'or'd or 'and'd together collection of icmp
  /// eq/ne/lt/gt instructions that compare a value against a constant, extract
  /// the value being compared, and stick the list constants into the Vals
  /// vector.
  /// One "Extra" case is allowed to differ from the other.
  void gather(Value *V) {
    Instruction *I = dyn_cast<Instruction>(V);
    bool isEQ = (I->getOpcode() == Instruction::Or);

    // Keep a stack (SmallVector for efficiency) for depth-first traversal
    SmallVector<Value *, 8> DFT;
    SmallPtrSet<Value *, 8> Visited;

    // Initialize
    Visited.insert(V);
    DFT.push_back(V);

    while (!DFT.empty()) {
      V = DFT.pop_back_val();

      if (Instruction *I = dyn_cast<Instruction>(V)) {
        // If it is a || (or && depending on isEQ), process the operands.
        if (I->getOpcode() == (isEQ ? Instruction::Or : Instruction::And)) {
          if (Visited.insert(I->getOperand(1)).second)
            DFT.push_back(I->getOperand(1));
          if (Visited.insert(I->getOperand(0)).second)
            DFT.push_back(I->getOperand(0));
          continue;
        }

        // Try to match the current instruction
        if (matchInstruction(I, isEQ))
          // Match succeed, continue the loop
          continue;
      }

      // One element of the sequence of || (or &&) could not be match as a
      // comparison against the same value as the others.
      // We allow only one "Extra" case to be checked before the switch
      if (!Extra) {
        Extra = V;
        continue;
      }
      // Failed to parse a proper sequence, abort now
      CompValue = nullptr;
      break;
    }
  }
};

} // end anonymous namespace

static void EraseTerminatorAndDCECond(Instruction *TI) {
  Instruction *Cond = nullptr;
  if (SwitchInst *SI = dyn_cast<SwitchInst>(TI)) {
    Cond = dyn_cast<Instruction>(SI->getCondition());
  } else if (BranchInst *BI = dyn_cast<BranchInst>(TI)) {
    if (BI->isConditional())
      Cond = dyn_cast<Instruction>(BI->getCondition());
  } else if (IndirectBrInst *IBI = dyn_cast<IndirectBrInst>(TI)) {
    Cond = dyn_cast<Instruction>(IBI->getAddress());
  }

  TI->eraseFromParent();
  if (Cond)
    RecursivelyDeleteTriviallyDeadInstructions(Cond);
}

/// Return true if the specified terminator checks
/// to see if a value is equal to constant integer value.
Value *SimplifyCFGOpt::isValueEqualityComparison(Instruction *TI) {
  Value *CV = nullptr;
  if (SwitchInst *SI = dyn_cast<SwitchInst>(TI)) {
    // Do not permit merging of large switch instructions into their
    // predecessors unless there is only one predecessor.
    if (!SI->getParent()->hasNPredecessorsOrMore(128 / SI->getNumSuccessors()))
      CV = SI->getCondition();
  } else if (BranchInst *BI = dyn_cast<BranchInst>(TI))
    if (BI->isConditional() && BI->getCondition()->hasOneUse())
      if (ICmpInst *ICI = dyn_cast<ICmpInst>(BI->getCondition())) {
        if (ICI->isEquality() && GetConstantInt(ICI->getOperand(1), DL))
          CV = ICI->getOperand(0);
      }

  // Unwrap any lossless ptrtoint cast.
  if (CV) {
    if (PtrToIntInst *PTII = dyn_cast<PtrToIntInst>(CV)) {
      Value *Ptr = PTII->getPointerOperand();
      if (PTII->getType() == DL.getIntPtrType(Ptr->getType()))
        CV = Ptr;
    }
  }
  return CV;
}

/// Given a value comparison instruction,
/// decode all of the 'cases' that it represents and return the 'default' block.
BasicBlock *SimplifyCFGOpt::GetValueEqualityComparisonCases(
    Instruction *TI, std::vector<ValueEqualityComparisonCase> &Cases) {
  if (SwitchInst *SI = dyn_cast<SwitchInst>(TI)) {
    Cases.reserve(SI->getNumCases());
    for (auto Case : SI->cases())
      Cases.push_back(ValueEqualityComparisonCase(Case.getCaseValue(),
                                                  Case.getCaseSuccessor()));
    return SI->getDefaultDest();
  }

  BranchInst *BI = cast<BranchInst>(TI);
  ICmpInst *ICI = cast<ICmpInst>(BI->getCondition());
  BasicBlock *Succ = BI->getSuccessor(ICI->getPredicate() == ICmpInst::ICMP_NE);
  Cases.push_back(ValueEqualityComparisonCase(
      GetConstantInt(ICI->getOperand(1), DL), Succ));
  return BI->getSuccessor(ICI->getPredicate() == ICmpInst::ICMP_EQ);
}

/// Given a vector of bb/value pairs, remove any entries
/// in the list that match the specified block.
static void
EliminateBlockCases(BasicBlock *BB,
                    std::vector<ValueEqualityComparisonCase> &Cases) {
  Cases.erase(std::remove(Cases.begin(), Cases.end(), BB), Cases.end());
}

/// Return true if there are any keys in C1 that exist in C2 as well.
static bool ValuesOverlap(std::vector<ValueEqualityComparisonCase> &C1,
                          std::vector<ValueEqualityComparisonCase> &C2) {
  std::vector<ValueEqualityComparisonCase> *V1 = &C1, *V2 = &C2;

  // Make V1 be smaller than V2.
  if (V1->size() > V2->size())
    std::swap(V1, V2);

  if (V1->empty())
    return false;
  if (V1->size() == 1) {
    // Just scan V2.
    ConstantInt *TheVal = (*V1)[0].Value;
    for (unsigned i = 0, e = V2->size(); i != e; ++i)
      if (TheVal == (*V2)[i].Value)
        return true;
  }

  // Otherwise, just sort both lists and compare element by element.
  array_pod_sort(V1->begin(), V1->end());
  array_pod_sort(V2->begin(), V2->end());
  unsigned i1 = 0, i2 = 0, e1 = V1->size(), e2 = V2->size();
  while (i1 != e1 && i2 != e2) {
    if ((*V1)[i1].Value == (*V2)[i2].Value)
      return true;
    if ((*V1)[i1].Value < (*V2)[i2].Value)
      ++i1;
    else
      ++i2;
  }
  return false;
}

// Set branch weights on SwitchInst. This sets the metadata if there is at
// least one non-zero weight.
static void setBranchWeights(SwitchInst *SI, ArrayRef<uint32_t> Weights) {
  // Check that there is at least one non-zero weight. Otherwise, pass
  // nullptr to setMetadata which will erase the existing metadata.
  MDNode *N = nullptr;
  if (llvm::any_of(Weights, [](uint32_t W) { return W != 0; }))
    N = MDBuilder(SI->getParent()->getContext()).createBranchWeights(Weights);
  SI->setMetadata(LLVMContext::MD_prof, N);
}

// Similar to the above, but for branch and select instructions that take
// exactly 2 weights.
static void setBranchWeights(Instruction *I, uint32_t TrueWeight,
                             uint32_t FalseWeight) {
  assert(isa<BranchInst>(I) || isa<SelectInst>(I));
  // Check that there is at least one non-zero weight. Otherwise, pass
  // nullptr to setMetadata which will erase the existing metadata.
  MDNode *N = nullptr;
  if (TrueWeight || FalseWeight)
    N = MDBuilder(I->getParent()->getContext())
            .createBranchWeights(TrueWeight, FalseWeight);
  I->setMetadata(LLVMContext::MD_prof, N);
}

/// If TI is known to be a terminator instruction and its block is known to
/// only have a single predecessor block, check to see if that predecessor is
/// also a value comparison with the same value, and if that comparison
/// determines the outcome of this comparison. If so, simplify TI. This does a
/// very limited form of jump threading.
bool SimplifyCFGOpt::SimplifyEqualityComparisonWithOnlyPredecessor(
    Instruction *TI, BasicBlock *Pred, IRBuilder<> &Builder) {
  Value *PredVal = isValueEqualityComparison(Pred->getTerminator());
  if (!PredVal)
    return false; // Not a value comparison in predecessor.

  Value *ThisVal = isValueEqualityComparison(TI);
  assert(ThisVal && "This isn't a value comparison!!");
  if (ThisVal != PredVal)
    return false; // Different predicates.

  // TODO: Preserve branch weight metadata, similarly to how
  // FoldValueComparisonIntoPredecessors preserves it.

  // Find out information about when control will move from Pred to TI's block.
  std::vector<ValueEqualityComparisonCase> PredCases;
  BasicBlock *PredDef =
      GetValueEqualityComparisonCases(Pred->getTerminator(), PredCases);
  EliminateBlockCases(PredDef, PredCases); // Remove default from cases.

  // Find information about how control leaves this block.
  std::vector<ValueEqualityComparisonCase> ThisCases;
  BasicBlock *ThisDef = GetValueEqualityComparisonCases(TI, ThisCases);
  EliminateBlockCases(ThisDef, ThisCases); // Remove default from cases.

  // If TI's block is the default block from Pred's comparison, potentially
  // simplify TI based on this knowledge.
  if (PredDef == TI->getParent()) {
    // If we are here, we know that the value is none of those cases listed in
    // PredCases.  If there are any cases in ThisCases that are in PredCases, we
    // can simplify TI.
    if (!ValuesOverlap(PredCases, ThisCases))
      return false;

    if (isa<BranchInst>(TI)) {
      // Okay, one of the successors of this condbr is dead.  Convert it to a
      // uncond br.
      assert(ThisCases.size() == 1 && "Branch can only have one case!");
      // Insert the new branch.
      Instruction *NI = Builder.CreateBr(ThisDef);
      (void)NI;

      // Remove PHI node entries for the dead edge.
      ThisCases[0].Dest->removePredecessor(TI->getParent());

      LLVM_DEBUG(dbgs() << "Threading pred instr: " << *Pred->getTerminator()
                        << "Through successor TI: " << *TI << "Leaving: " << *NI
                        << "\n");

      EraseTerminatorAndDCECond(TI);
      return true;
    }

    SwitchInst *SI = cast<SwitchInst>(TI);
    // Okay, TI has cases that are statically dead, prune them away.
    SmallPtrSet<Constant *, 16> DeadCases;
    for (unsigned i = 0, e = PredCases.size(); i != e; ++i)
      DeadCases.insert(PredCases[i].Value);

    LLVM_DEBUG(dbgs() << "Threading pred instr: " << *Pred->getTerminator()
                      << "Through successor TI: " << *TI);

    // Collect branch weights into a vector.
    SmallVector<uint32_t, 8> Weights;
    MDNode *MD = SI->getMetadata(LLVMContext::MD_prof);
    bool HasWeight = MD && (MD->getNumOperands() == 2 + SI->getNumCases());
    if (HasWeight)
      for (unsigned MD_i = 1, MD_e = MD->getNumOperands(); MD_i < MD_e;
           ++MD_i) {
        ConstantInt *CI = mdconst::extract<ConstantInt>(MD->getOperand(MD_i));
        Weights.push_back(CI->getValue().getZExtValue());
      }
    for (SwitchInst::CaseIt i = SI->case_end(), e = SI->case_begin(); i != e;) {
      --i;
      if (DeadCases.count(i->getCaseValue())) {
        if (HasWeight) {
          std::swap(Weights[i->getCaseIndex() + 1], Weights.back());
          Weights.pop_back();
        }
        i->getCaseSuccessor()->removePredecessor(TI->getParent());
        SI->removeCase(i);
      }
    }
    if (HasWeight && Weights.size() >= 2)
      setBranchWeights(SI, Weights);

    LLVM_DEBUG(dbgs() << "Leaving: " << *TI << "\n");
    return true;
  }

  // Otherwise, TI's block must correspond to some matched value.  Find out
  // which value (or set of values) this is.
  ConstantInt *TIV = nullptr;
  BasicBlock *TIBB = TI->getParent();
  for (unsigned i = 0, e = PredCases.size(); i != e; ++i)
    if (PredCases[i].Dest == TIBB) {
      if (TIV)
        return false; // Cannot handle multiple values coming to this block.
      TIV = PredCases[i].Value;
    }
  assert(TIV && "No edge from pred to succ?");

  // Okay, we found the one constant that our value can be if we get into TI's
  // BB.  Find out which successor will unconditionally be branched to.
  BasicBlock *TheRealDest = nullptr;
  for (unsigned i = 0, e = ThisCases.size(); i != e; ++i)
    if (ThisCases[i].Value == TIV) {
      TheRealDest = ThisCases[i].Dest;
      break;
    }

  // If not handled by any explicit cases, it is handled by the default case.
  if (!TheRealDest)
    TheRealDest = ThisDef;

  // Remove PHI node entries for dead edges.
  BasicBlock *CheckEdge = TheRealDest;
  for (BasicBlock *Succ : successors(TIBB))
    if (Succ != CheckEdge)
      Succ->removePredecessor(TIBB);
    else
      CheckEdge = nullptr;

  // Insert the new branch.
  Instruction *NI = Builder.CreateBr(TheRealDest);
  (void)NI;

  LLVM_DEBUG(dbgs() << "Threading pred instr: " << *Pred->getTerminator()
                    << "Through successor TI: " << *TI << "Leaving: " << *NI
                    << "\n");

  EraseTerminatorAndDCECond(TI);
  return true;
}

namespace {

/// This class implements a stable ordering of constant
/// integers that does not depend on their address.  This is important for
/// applications that sort ConstantInt's to ensure uniqueness.
struct ConstantIntOrdering {
  bool operator()(const ConstantInt *LHS, const ConstantInt *RHS) const {
    return LHS->getValue().ult(RHS->getValue());
  }
};

} // end anonymous namespace

static int ConstantIntSortPredicate(ConstantInt *const *P1,
                                    ConstantInt *const *P2) {
  const ConstantInt *LHS = *P1;
  const ConstantInt *RHS = *P2;
  if (LHS == RHS)
    return 0;
  return LHS->getValue().ult(RHS->getValue()) ? 1 : -1;
}

static inline bool HasBranchWeights(const Instruction *I) {
  MDNode *ProfMD = I->getMetadata(LLVMContext::MD_prof);
  if (ProfMD && ProfMD->getOperand(0))
    if (MDString *MDS = dyn_cast<MDString>(ProfMD->getOperand(0)))
      return MDS->getString().equals("branch_weights");

  return false;
}

/// Get Weights of a given terminator, the default weight is at the front
/// of the vector. If TI is a conditional eq, we need to swap the branch-weight
/// metadata.
static void GetBranchWeights(Instruction *TI,
                             SmallVectorImpl<uint64_t> &Weights) {
  MDNode *MD = TI->getMetadata(LLVMContext::MD_prof);
  assert(MD);
  for (unsigned i = 1, e = MD->getNumOperands(); i < e; ++i) {
    ConstantInt *CI = mdconst::extract<ConstantInt>(MD->getOperand(i));
    Weights.push_back(CI->getValue().getZExtValue());
  }

  // If TI is a conditional eq, the default case is the false case,
  // and the corresponding branch-weight data is at index 2. We swap the
  // default weight to be the first entry.
  if (BranchInst *BI = dyn_cast<BranchInst>(TI)) {
    assert(Weights.size() == 2);
    ICmpInst *ICI = cast<ICmpInst>(BI->getCondition());
    if (ICI->getPredicate() == ICmpInst::ICMP_EQ)
      std::swap(Weights.front(), Weights.back());
  }
}

/// Keep halving the weights until all can fit in uint32_t.
static void FitWeights(MutableArrayRef<uint64_t> Weights) {
  uint64_t Max = *std::max_element(Weights.begin(), Weights.end());
  if (Max > UINT_MAX) {
    unsigned Offset = 32 - countLeadingZeros(Max);
    for (uint64_t &I : Weights)
      I >>= Offset;
  }
}

/// The specified terminator is a value equality comparison instruction
/// (either a switch or a branch on "X == c").
/// See if any of the predecessors of the terminator block are value comparisons
/// on the same value.  If so, and if safe to do so, fold them together.
bool SimplifyCFGOpt::FoldValueComparisonIntoPredecessors(Instruction *TI,
                                                         IRBuilder<> &Builder) {
  BasicBlock *BB = TI->getParent();
  Value *CV = isValueEqualityComparison(TI); // CondVal
  assert(CV && "Not a comparison?");
  bool Changed = false;

  SmallVector<BasicBlock *, 16> Preds(pred_begin(BB), pred_end(BB));
  while (!Preds.empty()) {
    BasicBlock *Pred = Preds.pop_back_val();

    // See if the predecessor is a comparison with the same value.
    Instruction *PTI = Pred->getTerminator();
    Value *PCV = isValueEqualityComparison(PTI); // PredCondVal

    if (PCV == CV && TI != PTI) {
      SmallSetVector<BasicBlock*, 4> FailBlocks;
      if (!SafeToMergeTerminators(TI, PTI, &FailBlocks)) {
        for (auto *Succ : FailBlocks) {
          if (!SplitBlockPredecessors(Succ, TI->getParent(), ".fold.split"))
            return false;
        }
      }

      // Figure out which 'cases' to copy from SI to PSI.
      std::vector<ValueEqualityComparisonCase> BBCases;
      BasicBlock *BBDefault = GetValueEqualityComparisonCases(TI, BBCases);

      std::vector<ValueEqualityComparisonCase> PredCases;
      BasicBlock *PredDefault = GetValueEqualityComparisonCases(PTI, PredCases);

      // Based on whether the default edge from PTI goes to BB or not, fill in
      // PredCases and PredDefault with the new switch cases we would like to
      // build.
      SmallVector<BasicBlock *, 8> NewSuccessors;

      // Update the branch weight metadata along the way
      SmallVector<uint64_t, 8> Weights;
      bool PredHasWeights = HasBranchWeights(PTI);
      bool SuccHasWeights = HasBranchWeights(TI);

      if (PredHasWeights) {
        GetBranchWeights(PTI, Weights);
        // branch-weight metadata is inconsistent here.
        if (Weights.size() != 1 + PredCases.size())
          PredHasWeights = SuccHasWeights = false;
      } else if (SuccHasWeights)
        // If there are no predecessor weights but there are successor weights,
        // populate Weights with 1, which will later be scaled to the sum of
        // successor's weights
        Weights.assign(1 + PredCases.size(), 1);

      SmallVector<uint64_t, 8> SuccWeights;
      if (SuccHasWeights) {
        GetBranchWeights(TI, SuccWeights);
        // branch-weight metadata is inconsistent here.
        if (SuccWeights.size() != 1 + BBCases.size())
          PredHasWeights = SuccHasWeights = false;
      } else if (PredHasWeights)
        SuccWeights.assign(1 + BBCases.size(), 1);

      if (PredDefault == BB) {
        // If this is the default destination from PTI, only the edges in TI
        // that don't occur in PTI, or that branch to BB will be activated.
        std::set<ConstantInt *, ConstantIntOrdering> PTIHandled;
        for (unsigned i = 0, e = PredCases.size(); i != e; ++i)
          if (PredCases[i].Dest != BB)
            PTIHandled.insert(PredCases[i].Value);
          else {
            // The default destination is BB, we don't need explicit targets.
            std::swap(PredCases[i], PredCases.back());

            if (PredHasWeights || SuccHasWeights) {
              // Increase weight for the default case.
              Weights[0] += Weights[i + 1];
              std::swap(Weights[i + 1], Weights.back());
              Weights.pop_back();
            }

            PredCases.pop_back();
            --i;
            --e;
          }

        // Reconstruct the new switch statement we will be building.
        if (PredDefault != BBDefault) {
          PredDefault->removePredecessor(Pred);
          PredDefault = BBDefault;
          NewSuccessors.push_back(BBDefault);
        }

        unsigned CasesFromPred = Weights.size();
        uint64_t ValidTotalSuccWeight = 0;
        for (unsigned i = 0, e = BBCases.size(); i != e; ++i)
          if (!PTIHandled.count(BBCases[i].Value) &&
              BBCases[i].Dest != BBDefault) {
            PredCases.push_back(BBCases[i]);
            NewSuccessors.push_back(BBCases[i].Dest);
            if (SuccHasWeights || PredHasWeights) {
              // The default weight is at index 0, so weight for the ith case
              // should be at index i+1. Scale the cases from successor by
              // PredDefaultWeight (Weights[0]).
              Weights.push_back(Weights[0] * SuccWeights[i + 1]);
              ValidTotalSuccWeight += SuccWeights[i + 1];
            }
          }

        if (SuccHasWeights || PredHasWeights) {
          ValidTotalSuccWeight += SuccWeights[0];
          // Scale the cases from predecessor by ValidTotalSuccWeight.
          for (unsigned i = 1; i < CasesFromPred; ++i)
            Weights[i] *= ValidTotalSuccWeight;
          // Scale the default weight by SuccDefaultWeight (SuccWeights[0]).
          Weights[0] *= SuccWeights[0];
        }
      } else {
        // If this is not the default destination from PSI, only the edges
        // in SI that occur in PSI with a destination of BB will be
        // activated.
        std::set<ConstantInt *, ConstantIntOrdering> PTIHandled;
        std::map<ConstantInt *, uint64_t> WeightsForHandled;
        for (unsigned i = 0, e = PredCases.size(); i != e; ++i)
          if (PredCases[i].Dest == BB) {
            PTIHandled.insert(PredCases[i].Value);

            if (PredHasWeights || SuccHasWeights) {
              WeightsForHandled[PredCases[i].Value] = Weights[i + 1];
              std::swap(Weights[i + 1], Weights.back());
              Weights.pop_back();
            }

            std::swap(PredCases[i], PredCases.back());
            PredCases.pop_back();
            --i;
            --e;
          }

        // Okay, now we know which constants were sent to BB from the
        // predecessor.  Figure out where they will all go now.
        for (unsigned i = 0, e = BBCases.size(); i != e; ++i)
          if (PTIHandled.count(BBCases[i].Value)) {
            // If this is one we are capable of getting...
            if (PredHasWeights || SuccHasWeights)
              Weights.push_back(WeightsForHandled[BBCases[i].Value]);
            PredCases.push_back(BBCases[i]);
            NewSuccessors.push_back(BBCases[i].Dest);
            PTIHandled.erase(
                BBCases[i].Value); // This constant is taken care of
          }

        // If there are any constants vectored to BB that TI doesn't handle,
        // they must go to the default destination of TI.
        for (ConstantInt *I : PTIHandled) {
          if (PredHasWeights || SuccHasWeights)
            Weights.push_back(WeightsForHandled[I]);
          PredCases.push_back(ValueEqualityComparisonCase(I, BBDefault));
          NewSuccessors.push_back(BBDefault);
        }
      }

      // Okay, at this point, we know which new successor Pred will get.  Make
      // sure we update the number of entries in the PHI nodes for these
      // successors.
      for (BasicBlock *NewSuccessor : NewSuccessors)
        AddPredecessorToBlock(NewSuccessor, Pred, BB);

      Builder.SetInsertPoint(PTI);
      // Convert pointer to int before we switch.
      if (CV->getType()->isPointerTy()) {
        CV = Builder.CreatePtrToInt(CV, DL.getIntPtrType(CV->getType()),
                                    "magicptr");
      }

      // Now that the successors are updated, create the new Switch instruction.
      SwitchInst *NewSI =
          Builder.CreateSwitch(CV, PredDefault, PredCases.size());
      NewSI->setDebugLoc(PTI->getDebugLoc());
      for (ValueEqualityComparisonCase &V : PredCases)
        NewSI->addCase(V.Value, V.Dest);

      if (PredHasWeights || SuccHasWeights) {
        // Halve the weights if any of them cannot fit in an uint32_t
        FitWeights(Weights);

        SmallVector<uint32_t, 8> MDWeights(Weights.begin(), Weights.end());

        setBranchWeights(NewSI, MDWeights);
      }

      EraseTerminatorAndDCECond(PTI);

      // Okay, last check.  If BB is still a successor of PSI, then we must
      // have an infinite loop case.  If so, add an infinitely looping block
      // to handle the case to preserve the behavior of the code.
      BasicBlock *InfLoopBlock = nullptr;
      for (unsigned i = 0, e = NewSI->getNumSuccessors(); i != e; ++i)
        if (NewSI->getSuccessor(i) == BB) {
          if (!InfLoopBlock) {
            // Insert it at the end of the function, because it's either code,
            // or it won't matter if it's hot. :)
            InfLoopBlock = BasicBlock::Create(BB->getContext(), "infloop",
                                              BB->getParent());
            BranchInst::Create(InfLoopBlock, InfLoopBlock);
          }
          NewSI->setSuccessor(i, InfLoopBlock);
        }

      Changed = true;
    }
  }
  return Changed;
}

// If we would need to insert a select that uses the value of this invoke
// (comments in HoistThenElseCodeToIf explain why we would need to do this), we
// can't hoist the invoke, as there is nowhere to put the select in this case.
static bool isSafeToHoistInvoke(BasicBlock *BB1, BasicBlock *BB2,
                                Instruction *I1, Instruction *I2) {
  for (BasicBlock *Succ : successors(BB1)) {
    for (const PHINode &PN : Succ->phis()) {
      Value *BB1V = PN.getIncomingValueForBlock(BB1);
      Value *BB2V = PN.getIncomingValueForBlock(BB2);
      if (BB1V != BB2V && (BB1V == I1 || BB2V == I2)) {
        return false;
      }
    }
  }
  return true;
}

static bool passingValueIsAlwaysUndefined(Value *V, Instruction *I);

/// Given a conditional branch that goes to BB1 and BB2, hoist any common code
/// in the two blocks up into the branch block. The caller of this function
/// guarantees that BI's block dominates BB1 and BB2.
static bool HoistThenElseCodeToIf(BranchInst *BI,
                                  const TargetTransformInfo &TTI) {
  // This does very trivial matching, with limited scanning, to find identical
  // instructions in the two blocks.  In particular, we don't want to get into
  // O(M*N) situations here where M and N are the sizes of BB1 and BB2.  As
  // such, we currently just scan for obviously identical instructions in an
  // identical order.
  BasicBlock *BB1 = BI->getSuccessor(0); // The true destination.
  BasicBlock *BB2 = BI->getSuccessor(1); // The false destination

  BasicBlock::iterator BB1_Itr = BB1->begin();
  BasicBlock::iterator BB2_Itr = BB2->begin();

  Instruction *I1 = &*BB1_Itr++, *I2 = &*BB2_Itr++;
  // Skip debug info if it is not identical.
  DbgInfoIntrinsic *DBI1 = dyn_cast<DbgInfoIntrinsic>(I1);
  DbgInfoIntrinsic *DBI2 = dyn_cast<DbgInfoIntrinsic>(I2);
  if (!DBI1 || !DBI2 || !DBI1->isIdenticalToWhenDefined(DBI2)) {
    while (isa<DbgInfoIntrinsic>(I1))
      I1 = &*BB1_Itr++;
    while (isa<DbgInfoIntrinsic>(I2))
      I2 = &*BB2_Itr++;
  }
  if (isa<PHINode>(I1) || !I1->isIdenticalToWhenDefined(I2) ||
      (isa<InvokeInst>(I1) && !isSafeToHoistInvoke(BB1, BB2, I1, I2)))
    return false;

  BasicBlock *BIParent = BI->getParent();

  bool Changed = false;
  do {
    // If we are hoisting the terminator instruction, don't move one (making a
    // broken BB), instead clone it, and remove BI.
    if (I1->isTerminator())
      goto HoistTerminator;

    // If we're going to hoist a call, make sure that the two instructions we're
    // commoning/hoisting are both marked with musttail, or neither of them is
    // marked as such. Otherwise, we might end up in a situation where we hoist
    // from a block where the terminator is a `ret` to a block where the terminator
    // is a `br`, and `musttail` calls expect to be followed by a return.
    auto *C1 = dyn_cast<CallInst>(I1);
    auto *C2 = dyn_cast<CallInst>(I2);
    if (C1 && C2)
      if (C1->isMustTailCall() != C2->isMustTailCall())
        return Changed;

    if (!TTI.isProfitableToHoist(I1) || !TTI.isProfitableToHoist(I2))
      return Changed;

    if (isa<DbgInfoIntrinsic>(I1) || isa<DbgInfoIntrinsic>(I2)) {
      assert (isa<DbgInfoIntrinsic>(I1) && isa<DbgInfoIntrinsic>(I2));
      // The debug location is an integral part of a debug info intrinsic
      // and can't be separated from it or replaced.  Instead of attempting
      // to merge locations, simply hoist both copies of the intrinsic.
      BIParent->getInstList().splice(BI->getIterator(),
                                     BB1->getInstList(), I1);
      BIParent->getInstList().splice(BI->getIterator(),
                                     BB2->getInstList(), I2);
      Changed = true;
    } else {
      // For a normal instruction, we just move one to right before the branch,
      // then replace all uses of the other with the first.  Finally, we remove
      // the now redundant second instruction.
      BIParent->getInstList().splice(BI->getIterator(),
                                     BB1->getInstList(), I1);
      if (!I2->use_empty())
        I2->replaceAllUsesWith(I1);
      I1->andIRFlags(I2);
      unsigned KnownIDs[] = {LLVMContext::MD_tbaa,
                             LLVMContext::MD_range,
                             LLVMContext::MD_fpmath,
                             LLVMContext::MD_invariant_load,
                             LLVMContext::MD_nonnull,
                             LLVMContext::MD_invariant_group,
                             LLVMContext::MD_align,
                             LLVMContext::MD_dereferenceable,
                             LLVMContext::MD_dereferenceable_or_null,
                             LLVMContext::MD_mem_parallel_loop_access,
                             LLVMContext::MD_access_group};
      combineMetadata(I1, I2, KnownIDs, true);

      // I1 and I2 are being combined into a single instruction.  Its debug
      // location is the merged locations of the original instructions.
      I1->applyMergedLocation(I1->getDebugLoc(), I2->getDebugLoc());

      I2->eraseFromParent();
      Changed = true;
    }

    I1 = &*BB1_Itr++;
    I2 = &*BB2_Itr++;
    // Skip debug info if it is not identical.
    DbgInfoIntrinsic *DBI1 = dyn_cast<DbgInfoIntrinsic>(I1);
    DbgInfoIntrinsic *DBI2 = dyn_cast<DbgInfoIntrinsic>(I2);
    if (!DBI1 || !DBI2 || !DBI1->isIdenticalToWhenDefined(DBI2)) {
      while (isa<DbgInfoIntrinsic>(I1))
        I1 = &*BB1_Itr++;
      while (isa<DbgInfoIntrinsic>(I2))
        I2 = &*BB2_Itr++;
    }
  } while (I1->isIdenticalToWhenDefined(I2));

  return true;

HoistTerminator:
  // It may not be possible to hoist an invoke.
  if (isa<InvokeInst>(I1) && !isSafeToHoistInvoke(BB1, BB2, I1, I2))
    return Changed;

  for (BasicBlock *Succ : successors(BB1)) {
    for (PHINode &PN : Succ->phis()) {
      Value *BB1V = PN.getIncomingValueForBlock(BB1);
      Value *BB2V = PN.getIncomingValueForBlock(BB2);
      if (BB1V == BB2V)
        continue;

      // Check for passingValueIsAlwaysUndefined here because we would rather
      // eliminate undefined control flow then converting it to a select.
      if (passingValueIsAlwaysUndefined(BB1V, &PN) ||
          passingValueIsAlwaysUndefined(BB2V, &PN))
        return Changed;

      if (isa<ConstantExpr>(BB1V) && !isSafeToSpeculativelyExecute(BB1V))
        return Changed;
      if (isa<ConstantExpr>(BB2V) && !isSafeToSpeculativelyExecute(BB2V))
        return Changed;
    }
  }

  // Okay, it is safe to hoist the terminator.
  Instruction *NT = I1->clone();
  BIParent->getInstList().insert(BI->getIterator(), NT);
  if (!NT->getType()->isVoidTy()) {
    I1->replaceAllUsesWith(NT);
    I2->replaceAllUsesWith(NT);
    NT->takeName(I1);
  }

  // Ensure terminator gets a debug location, even an unknown one, in case
  // it involves inlinable calls.
  NT->applyMergedLocation(I1->getDebugLoc(), I2->getDebugLoc());

  // PHIs created below will adopt NT's merged DebugLoc.
  IRBuilder<NoFolder> Builder(NT);

  // Hoisting one of the terminators from our successor is a great thing.
  // Unfortunately, the successors of the if/else blocks may have PHI nodes in
  // them.  If they do, all PHI entries for BB1/BB2 must agree for all PHI
  // nodes, so we insert select instruction to compute the final result.
  std::map<std::pair<Value *, Value *>, SelectInst *> InsertedSelects;
  for (BasicBlock *Succ : successors(BB1)) {
    for (PHINode &PN : Succ->phis()) {
      Value *BB1V = PN.getIncomingValueForBlock(BB1);
      Value *BB2V = PN.getIncomingValueForBlock(BB2);
      if (BB1V == BB2V)
        continue;

      // These values do not agree.  Insert a select instruction before NT
      // that determines the right value.
      SelectInst *&SI = InsertedSelects[std::make_pair(BB1V, BB2V)];
      if (!SI)
        SI = cast<SelectInst>(
            Builder.CreateSelect(BI->getCondition(), BB1V, BB2V,
                                 BB1V->getName() + "." + BB2V->getName(), BI));

      // Make the PHI node use the select for all incoming values for BB1/BB2
      for (unsigned i = 0, e = PN.getNumIncomingValues(); i != e; ++i)
        if (PN.getIncomingBlock(i) == BB1 || PN.getIncomingBlock(i) == BB2)
          PN.setIncomingValue(i, SI);
    }
  }

  // Update any PHI nodes in our new successors.
  for (BasicBlock *Succ : successors(BB1))
    AddPredecessorToBlock(Succ, BIParent, BB1);

  EraseTerminatorAndDCECond(BI);
  return true;
}

// All instructions in Insts belong to different blocks that all unconditionally
// branch to a common successor. Analyze each instruction and return true if it
// would be possible to sink them into their successor, creating one common
// instruction instead. For every value that would be required to be provided by
// PHI node (because an operand varies in each input block), add to PHIOperands.
static bool canSinkInstructions(
    ArrayRef<Instruction *> Insts,
    DenseMap<Instruction *, SmallVector<Value *, 4>> &PHIOperands) {
  // Prune out obviously bad instructions to move. Any non-store instruction
  // must have exactly one use, and we check later that use is by a single,
  // common PHI instruction in the successor.
  for (auto *I : Insts) {
    // These instructions may change or break semantics if moved.
    if (isa<PHINode>(I) || I->isEHPad() || isa<AllocaInst>(I) ||
        I->getType()->isTokenTy())
      return false;

    // Conservatively return false if I is an inline-asm instruction. Sinking
    // and merging inline-asm instructions can potentially create arguments
    // that cannot satisfy the inline-asm constraints.
    if (const auto *C = dyn_cast<CallInst>(I))
      if (C->isInlineAsm())
        return false;

    // Everything must have only one use too, apart from stores which
    // have no uses.
    if (!isa<StoreInst>(I) && !I->hasOneUse())
      return false;
  }

  const Instruction *I0 = Insts.front();
  for (auto *I : Insts)
    if (!I->isSameOperationAs(I0))
      return false;

  // All instructions in Insts are known to be the same opcode. If they aren't
  // stores, check the only user of each is a PHI or in the same block as the
  // instruction, because if a user is in the same block as an instruction
  // we're contemplating sinking, it must already be determined to be sinkable.
  if (!isa<StoreInst>(I0)) {
    auto *PNUse = dyn_cast<PHINode>(*I0->user_begin());
    auto *Succ = I0->getParent()->getTerminator()->getSuccessor(0);
    if (!all_of(Insts, [&PNUse,&Succ](const Instruction *I) -> bool {
          auto *U = cast<Instruction>(*I->user_begin());
          return (PNUse &&
                  PNUse->getParent() == Succ &&
                  PNUse->getIncomingValueForBlock(I->getParent()) == I) ||
                 U->getParent() == I->getParent();
        }))
      return false;
  }

  // Because SROA can't handle speculating stores of selects, try not
  // to sink loads or stores of allocas when we'd have to create a PHI for
  // the address operand. Also, because it is likely that loads or stores
  // of allocas will disappear when Mem2Reg/SROA is run, don't sink them.
  // This can cause code churn which can have unintended consequences down
  // the line - see https://llvm.org/bugs/show_bug.cgi?id=30244.
  // FIXME: This is a workaround for a deficiency in SROA - see
  // https://llvm.org/bugs/show_bug.cgi?id=30188
  if (isa<StoreInst>(I0) && any_of(Insts, [](const Instruction *I) {
        return isa<AllocaInst>(I->getOperand(1));
      }))
    return false;
  if (isa<LoadInst>(I0) && any_of(Insts, [](const Instruction *I) {
        return isa<AllocaInst>(I->getOperand(0));
      }))
    return false;

  for (unsigned OI = 0, OE = I0->getNumOperands(); OI != OE; ++OI) {
    if (I0->getOperand(OI)->getType()->isTokenTy())
      // Don't touch any operand of token type.
      return false;

    auto SameAsI0 = [&I0, OI](const Instruction *I) {
      assert(I->getNumOperands() == I0->getNumOperands());
      return I->getOperand(OI) == I0->getOperand(OI);
    };
    if (!all_of(Insts, SameAsI0)) {
      if (!canReplaceOperandWithVariable(I0, OI))
        // We can't create a PHI from this GEP.
        return false;
      // Don't create indirect calls! The called value is the final operand.
      if ((isa<CallInst>(I0) || isa<InvokeInst>(I0)) && OI == OE - 1) {
        // FIXME: if the call was *already* indirect, we should do this.
        return false;
      }
      for (auto *I : Insts)
        PHIOperands[I].push_back(I->getOperand(OI));
    }
  }
  return true;
}

// Assuming canSinkLastInstruction(Blocks) has returned true, sink the last
// instruction of every block in Blocks to their common successor, commoning
// into one instruction.
static bool sinkLastInstruction(ArrayRef<BasicBlock*> Blocks) {
  auto *BBEnd = Blocks[0]->getTerminator()->getSuccessor(0);

  // canSinkLastInstruction returning true guarantees that every block has at
  // least one non-terminator instruction.
  SmallVector<Instruction*,4> Insts;
  for (auto *BB : Blocks) {
    Instruction *I = BB->getTerminator();
    do {
      I = I->getPrevNode();
    } while (isa<DbgInfoIntrinsic>(I) && I != &BB->front());
    if (!isa<DbgInfoIntrinsic>(I))
      Insts.push_back(I);
  }

  // The only checking we need to do now is that all users of all instructions
  // are the same PHI node. canSinkLastInstruction should have checked this but
  // it is slightly over-aggressive - it gets confused by commutative instructions
  // so double-check it here.
  Instruction *I0 = Insts.front();
  if (!isa<StoreInst>(I0)) {
    auto *PNUse = dyn_cast<PHINode>(*I0->user_begin());
    if (!all_of(Insts, [&PNUse](const Instruction *I) -> bool {
          auto *U = cast<Instruction>(*I->user_begin());
          return U == PNUse;
        }))
      return false;
  }

  // We don't need to do any more checking here; canSinkLastInstruction should
  // have done it all for us.
  SmallVector<Value*, 4> NewOperands;
  for (unsigned O = 0, E = I0->getNumOperands(); O != E; ++O) {
    // This check is different to that in canSinkLastInstruction. There, we
    // cared about the global view once simplifycfg (and instcombine) have
    // completed - it takes into account PHIs that become trivially
    // simplifiable.  However here we need a more local view; if an operand
    // differs we create a PHI and rely on instcombine to clean up the very
    // small mess we may make.
    bool NeedPHI = any_of(Insts, [&I0, O](const Instruction *I) {
      return I->getOperand(O) != I0->getOperand(O);
    });
    if (!NeedPHI) {
      NewOperands.push_back(I0->getOperand(O));
      continue;
    }

    // Create a new PHI in the successor block and populate it.
    auto *Op = I0->getOperand(O);
    assert(!Op->getType()->isTokenTy() && "Can't PHI tokens!");
    auto *PN = PHINode::Create(Op->getType(), Insts.size(),
                               Op->getName() + ".sink", &BBEnd->front());
    for (auto *I : Insts)
      PN->addIncoming(I->getOperand(O), I->getParent());
    NewOperands.push_back(PN);
  }

  // Arbitrarily use I0 as the new "common" instruction; remap its operands
  // and move it to the start of the successor block.
  for (unsigned O = 0, E = I0->getNumOperands(); O != E; ++O)
    I0->getOperandUse(O).set(NewOperands[O]);
  I0->moveBefore(&*BBEnd->getFirstInsertionPt());

  // Update metadata and IR flags, and merge debug locations.
  for (auto *I : Insts)
    if (I != I0) {
      // The debug location for the "common" instruction is the merged locations
      // of all the commoned instructions.  We start with the original location
      // of the "common" instruction and iteratively merge each location in the
      // loop below.
      // This is an N-way merge, which will be inefficient if I0 is a CallInst.
      // However, as N-way merge for CallInst is rare, so we use simplified API
      // instead of using complex API for N-way merge.
      I0->applyMergedLocation(I0->getDebugLoc(), I->getDebugLoc());
      combineMetadataForCSE(I0, I, true);
      I0->andIRFlags(I);
    }

  if (!isa<StoreInst>(I0)) {
    // canSinkLastInstruction checked that all instructions were used by
    // one and only one PHI node. Find that now, RAUW it to our common
    // instruction and nuke it.
    assert(I0->hasOneUse());
    auto *PN = cast<PHINode>(*I0->user_begin());
    PN->replaceAllUsesWith(I0);
    PN->eraseFromParent();
  }

  // Finally nuke all instructions apart from the common instruction.
  for (auto *I : Insts)
    if (I != I0)
      I->eraseFromParent();

  return true;
}

namespace {

  // LockstepReverseIterator - Iterates through instructions
  // in a set of blocks in reverse order from the first non-terminator.
  // For example (assume all blocks have size n):
  //   LockstepReverseIterator I([B1, B2, B3]);
  //   *I-- = [B1[n], B2[n], B3[n]];
  //   *I-- = [B1[n-1], B2[n-1], B3[n-1]];
  //   *I-- = [B1[n-2], B2[n-2], B3[n-2]];
  //   ...
  class LockstepReverseIterator {
    ArrayRef<BasicBlock*> Blocks;
    SmallVector<Instruction*,4> Insts;
    bool Fail;

  public:
    LockstepReverseIterator(ArrayRef<BasicBlock*> Blocks) : Blocks(Blocks) {
      reset();
    }

    void reset() {
      Fail = false;
      Insts.clear();
      for (auto *BB : Blocks) {
        Instruction *Inst = BB->getTerminator();
        for (Inst = Inst->getPrevNode(); Inst && isa<DbgInfoIntrinsic>(Inst);)
          Inst = Inst->getPrevNode();
        if (!Inst) {
          // Block wasn't big enough.
          Fail = true;
          return;
        }
        Insts.push_back(Inst);
      }
    }

    bool isValid() const {
      return !Fail;
    }

    void operator--() {
      if (Fail)
        return;
      for (auto *&Inst : Insts) {
        for (Inst = Inst->getPrevNode(); Inst && isa<DbgInfoIntrinsic>(Inst);)
          Inst = Inst->getPrevNode();
        // Already at beginning of block.
        if (!Inst) {
          Fail = true;
          return;
        }
      }
    }

    ArrayRef<Instruction*> operator * () const {
      return Insts;
    }
  };

} // end anonymous namespace

/// Check whether BB's predecessors end with unconditional branches. If it is
/// true, sink any common code from the predecessors to BB.
/// We also allow one predecessor to end with conditional branch (but no more
/// than one).
static bool SinkCommonCodeFromPredecessors(BasicBlock *BB) {
  // We support two situations:
  //   (1) all incoming arcs are unconditional
  //   (2) one incoming arc is conditional
  //
  // (2) is very common in switch defaults and
  // else-if patterns;
  //
  //   if (a) f(1);
  //   else if (b) f(2);
  //
  // produces:
  //
  //       [if]
  //      /    \
  //    [f(1)] [if]
  //      |     | \
  //      |     |  |
  //      |  [f(2)]|
  //       \    | /
  //        [ end ]
  //
  // [end] has two unconditional predecessor arcs and one conditional. The
  // conditional refers to the implicit empty 'else' arc. This conditional
  // arc can also be caused by an empty default block in a switch.
  //
  // In this case, we attempt to sink code from all *unconditional* arcs.
  // If we can sink instructions from these arcs (determined during the scan
  // phase below) we insert a common successor for all unconditional arcs and
  // connect that to [end], to enable sinking:
  //
  //       [if]
  //      /    \
  //    [x(1)] [if]
  //      |     | \
  //      |     |  \
  //      |  [x(2)] |
  //       \   /    |
  //   [sink.split] |
  //         \     /
  //         [ end ]
  //
  SmallVector<BasicBlock*,4> UnconditionalPreds;
  Instruction *Cond = nullptr;
  for (auto *B : predecessors(BB)) {
    auto *T = B->getTerminator();
    if (isa<BranchInst>(T) && cast<BranchInst>(T)->isUnconditional())
      UnconditionalPreds.push_back(B);
    else if ((isa<BranchInst>(T) || isa<SwitchInst>(T)) && !Cond)
      Cond = T;
    else
      return false;
  }
  if (UnconditionalPreds.size() < 2)
    return false;

  bool Changed = false;
  // We take a two-step approach to tail sinking. First we scan from the end of
  // each block upwards in lockstep. If the n'th instruction from the end of each
  // block can be sunk, those instructions are added to ValuesToSink and we
  // carry on. If we can sink an instruction but need to PHI-merge some operands
  // (because they're not identical in each instruction) we add these to
  // PHIOperands.
  unsigned ScanIdx = 0;
  SmallPtrSet<Value*,4> InstructionsToSink;
  DenseMap<Instruction*, SmallVector<Value*,4>> PHIOperands;
  LockstepReverseIterator LRI(UnconditionalPreds);
  while (LRI.isValid() &&
         canSinkInstructions(*LRI, PHIOperands)) {
    LLVM_DEBUG(dbgs() << "SINK: instruction can be sunk: " << *(*LRI)[0]
                      << "\n");
    InstructionsToSink.insert((*LRI).begin(), (*LRI).end());
    ++ScanIdx;
    --LRI;
  }

  auto ProfitableToSinkInstruction = [&](LockstepReverseIterator &LRI) {
    unsigned NumPHIdValues = 0;
    for (auto *I : *LRI)
      for (auto *V : PHIOperands[I])
        if (InstructionsToSink.count(V) == 0)
          ++NumPHIdValues;
    LLVM_DEBUG(dbgs() << "SINK: #phid values: " << NumPHIdValues << "\n");
    unsigned NumPHIInsts = NumPHIdValues / UnconditionalPreds.size();
    if ((NumPHIdValues % UnconditionalPreds.size()) != 0)
        NumPHIInsts++;

    return NumPHIInsts <= 1;
  };

  if (ScanIdx > 0 && Cond) {
    // Check if we would actually sink anything first! This mutates the CFG and
    // adds an extra block. The goal in doing this is to allow instructions that
    // couldn't be sunk before to be sunk - obviously, speculatable instructions
    // (such as trunc, add) can be sunk and predicated already. So we check that
    // we're going to sink at least one non-speculatable instruction.
    LRI.reset();
    unsigned Idx = 0;
    bool Profitable = false;
    while (ProfitableToSinkInstruction(LRI) && Idx < ScanIdx) {
      if (!isSafeToSpeculativelyExecute((*LRI)[0])) {
        Profitable = true;
        break;
      }
      --LRI;
      ++Idx;
    }
    if (!Profitable)
      return false;

    LLVM_DEBUG(dbgs() << "SINK: Splitting edge\n");
    // We have a conditional edge and we're going to sink some instructions.
    // Insert a new block postdominating all blocks we're going to sink from.
    if (!SplitBlockPredecessors(BB, UnconditionalPreds, ".sink.split"))
      // Edges couldn't be split.
      return false;
    Changed = true;
  }

  // Now that we've analyzed all potential sinking candidates, perform the
  // actual sink. We iteratively sink the last non-terminator of the source
  // blocks into their common successor unless doing so would require too
  // many PHI instructions to be generated (currently only one PHI is allowed
  // per sunk instruction).
  //
  // We can use InstructionsToSink to discount values needing PHI-merging that will
  // actually be sunk in a later iteration. This allows us to be more
  // aggressive in what we sink. This does allow a false positive where we
  // sink presuming a later value will also be sunk, but stop half way through
  // and never actually sink it which means we produce more PHIs than intended.
  // This is unlikely in practice though.
  for (unsigned SinkIdx = 0; SinkIdx != ScanIdx; ++SinkIdx) {
    LLVM_DEBUG(dbgs() << "SINK: Sink: "
                      << *UnconditionalPreds[0]->getTerminator()->getPrevNode()
                      << "\n");

    // Because we've sunk every instruction in turn, the current instruction to
    // sink is always at index 0.
    LRI.reset();
    if (!ProfitableToSinkInstruction(LRI)) {
      // Too many PHIs would be created.
      LLVM_DEBUG(
          dbgs() << "SINK: stopping here, too many PHIs would be created!\n");
      break;
    }

    if (!sinkLastInstruction(UnconditionalPreds))
      return Changed;
    NumSinkCommons++;
    Changed = true;
  }
  return Changed;
}

/// Determine if we can hoist sink a sole store instruction out of a
/// conditional block.
///
/// We are looking for code like the following:
///   BrBB:
///     store i32 %add, i32* %arrayidx2
///     ... // No other stores or function calls (we could be calling a memory
///     ... // function).
///     %cmp = icmp ult %x, %y
///     br i1 %cmp, label %EndBB, label %ThenBB
///   ThenBB:
///     store i32 %add5, i32* %arrayidx2
///     br label EndBB
///   EndBB:
///     ...
///   We are going to transform this into:
///   BrBB:
///     store i32 %add, i32* %arrayidx2
///     ... //
///     %cmp = icmp ult %x, %y
///     %add.add5 = select i1 %cmp, i32 %add, %add5
///     store i32 %add.add5, i32* %arrayidx2
///     ...
///
/// \return The pointer to the value of the previous store if the store can be
///         hoisted into the predecessor block. 0 otherwise.
static Value *isSafeToSpeculateStore(Instruction *I, BasicBlock *BrBB,
                                     BasicBlock *StoreBB, BasicBlock *EndBB) {
  StoreInst *StoreToHoist = dyn_cast<StoreInst>(I);
  if (!StoreToHoist)
    return nullptr;

  // Volatile or atomic.
  if (!StoreToHoist->isSimple())
    return nullptr;

  Value *StorePtr = StoreToHoist->getPointerOperand();

  // Look for a store to the same pointer in BrBB.
  unsigned MaxNumInstToLookAt = 9;
  for (Instruction &CurI : reverse(BrBB->instructionsWithoutDebug())) {
    if (!MaxNumInstToLookAt)
      break;
    --MaxNumInstToLookAt;

    // Could be calling an instruction that affects memory like free().
    if (CurI.mayHaveSideEffects() && !isa<StoreInst>(CurI))
      return nullptr;

    if (auto *SI = dyn_cast<StoreInst>(&CurI)) {
      // Found the previous store make sure it stores to the same location.
      if (SI->getPointerOperand() == StorePtr)
        // Found the previous store, return its value operand.
        return SI->getValueOperand();
      return nullptr; // Unknown store.
    }
  }

  return nullptr;
}

/// Speculate a conditional basic block flattening the CFG.
///
/// Note that this is a very risky transform currently. Speculating
/// instructions like this is most often not desirable. Instead, there is an MI
/// pass which can do it with full awareness of the resource constraints.
/// However, some cases are "obvious" and we should do directly. An example of
/// this is speculating a single, reasonably cheap instruction.
///
/// There is only one distinct advantage to flattening the CFG at the IR level:
/// it makes very common but simplistic optimizations such as are common in
/// instcombine and the DAG combiner more powerful by removing CFG edges and
/// modeling their effects with easier to reason about SSA value graphs.
///
///
/// An illustration of this transform is turning this IR:
/// \code
///   BB:
///     %cmp = icmp ult %x, %y
///     br i1 %cmp, label %EndBB, label %ThenBB
///   ThenBB:
///     %sub = sub %x, %y
///     br label BB2
///   EndBB:
///     %phi = phi [ %sub, %ThenBB ], [ 0, %EndBB ]
///     ...
/// \endcode
///
/// Into this IR:
/// \code
///   BB:
///     %cmp = icmp ult %x, %y
///     %sub = sub %x, %y
///     %cond = select i1 %cmp, 0, %sub
///     ...
/// \endcode
///
/// \returns true if the conditional block is removed.
static bool SpeculativelyExecuteBB(BranchInst *BI, BasicBlock *ThenBB,
                                   const TargetTransformInfo &TTI) {
  // Be conservative for now. FP select instruction can often be expensive.
  Value *BrCond = BI->getCondition();
  if (isa<FCmpInst>(BrCond))
    return false;

  BasicBlock *BB = BI->getParent();
  BasicBlock *EndBB = ThenBB->getTerminator()->getSuccessor(0);

  // If ThenBB is actually on the false edge of the conditional branch, remember
  // to swap the select operands later.
  bool Invert = false;
  if (ThenBB != BI->getSuccessor(0)) {
    assert(ThenBB == BI->getSuccessor(1) && "No edge from 'if' block?");
    Invert = true;
  }
  assert(EndBB == BI->getSuccessor(!Invert) && "No edge from to end block");

  // Keep a count of how many times instructions are used within ThenBB when
  // they are candidates for sinking into ThenBB. Specifically:
  // - They are defined in BB, and
  // - They have no side effects, and
  // - All of their uses are in ThenBB.
  SmallDenseMap<Instruction *, unsigned, 4> SinkCandidateUseCounts;

  SmallVector<Instruction *, 4> SpeculatedDbgIntrinsics;

  unsigned SpeculationCost = 0;
  Value *SpeculatedStoreValue = nullptr;
  StoreInst *SpeculatedStore = nullptr;
  for (BasicBlock::iterator BBI = ThenBB->begin(),
                            BBE = std::prev(ThenBB->end());
       BBI != BBE; ++BBI) {
    Instruction *I = &*BBI;
    // Skip debug info.
    if (isa<DbgInfoIntrinsic>(I)) {
      SpeculatedDbgIntrinsics.push_back(I);
      continue;
    }

    // Only speculatively execute a single instruction (not counting the
    // terminator) for now.
    ++SpeculationCost;
    if (SpeculationCost > 1)
      return false;

    // Don't hoist the instruction if it's unsafe or expensive.
    if (!isSafeToSpeculativelyExecute(I) &&
        !(HoistCondStores && (SpeculatedStoreValue = isSafeToSpeculateStore(
                                  I, BB, ThenBB, EndBB))))
      return false;
    if (!SpeculatedStoreValue &&
        ComputeSpeculationCost(I, TTI) >
            PHINodeFoldingThreshold * TargetTransformInfo::TCC_Basic)
      return false;

    // Store the store speculation candidate.
    if (SpeculatedStoreValue)
      SpeculatedStore = cast<StoreInst>(I);

    // Do not hoist the instruction if any of its operands are defined but not
    // used in BB. The transformation will prevent the operand from
    // being sunk into the use block.
    for (User::op_iterator i = I->op_begin(), e = I->op_end(); i != e; ++i) {
      Instruction *OpI = dyn_cast<Instruction>(*i);
      if (!OpI || OpI->getParent() != BB || OpI->mayHaveSideEffects())
        continue; // Not a candidate for sinking.

      ++SinkCandidateUseCounts[OpI];
    }
  }

  // Consider any sink candidates which are only used in ThenBB as costs for
  // speculation. Note, while we iterate over a DenseMap here, we are summing
  // and so iteration order isn't significant.
  for (SmallDenseMap<Instruction *, unsigned, 4>::iterator
           I = SinkCandidateUseCounts.begin(),
           E = SinkCandidateUseCounts.end();
       I != E; ++I)
    if (I->first->hasNUses(I->second)) {
      ++SpeculationCost;
      if (SpeculationCost > 1)
        return false;
    }

  // Check that the PHI nodes can be converted to selects.
  bool HaveRewritablePHIs = false;
  for (PHINode &PN : EndBB->phis()) {
    Value *OrigV = PN.getIncomingValueForBlock(BB);
    Value *ThenV = PN.getIncomingValueForBlock(ThenBB);

    // FIXME: Try to remove some of the duplication with HoistThenElseCodeToIf.
    // Skip PHIs which are trivial.
    if (ThenV == OrigV)
      continue;

    // Don't convert to selects if we could remove undefined behavior instead.
    if (passingValueIsAlwaysUndefined(OrigV, &PN) ||
        passingValueIsAlwaysUndefined(ThenV, &PN))
      return false;

    HaveRewritablePHIs = true;
    ConstantExpr *OrigCE = dyn_cast<ConstantExpr>(OrigV);
    ConstantExpr *ThenCE = dyn_cast<ConstantExpr>(ThenV);
    if (!OrigCE && !ThenCE)
      continue; // Known safe and cheap.

    if ((ThenCE && !isSafeToSpeculativelyExecute(ThenCE)) ||
        (OrigCE && !isSafeToSpeculativelyExecute(OrigCE)))
      return false;
    unsigned OrigCost = OrigCE ? ComputeSpeculationCost(OrigCE, TTI) : 0;
    unsigned ThenCost = ThenCE ? ComputeSpeculationCost(ThenCE, TTI) : 0;
    unsigned MaxCost =
        2 * PHINodeFoldingThreshold * TargetTransformInfo::TCC_Basic;
    if (OrigCost + ThenCost > MaxCost)
      return false;

    // Account for the cost of an unfolded ConstantExpr which could end up
    // getting expanded into Instructions.
    // FIXME: This doesn't account for how many operations are combined in the
    // constant expression.
    ++SpeculationCost;
    if (SpeculationCost > 1)
      return false;
  }

  // If there are no PHIs to process, bail early. This helps ensure idempotence
  // as well.
  if (!HaveRewritablePHIs && !(HoistCondStores && SpeculatedStoreValue))
    return false;

  // If we get here, we can hoist the instruction and if-convert.
  LLVM_DEBUG(dbgs() << "SPECULATIVELY EXECUTING BB" << *ThenBB << "\n";);

  // Insert a select of the value of the speculated store.
  if (SpeculatedStoreValue) {
    IRBuilder<NoFolder> Builder(BI);
    Value *TrueV = SpeculatedStore->getValueOperand();
    Value *FalseV = SpeculatedStoreValue;
    if (Invert)
      std::swap(TrueV, FalseV);
    Value *S = Builder.CreateSelect(
        BrCond, TrueV, FalseV, "spec.store.select", BI);
    SpeculatedStore->setOperand(0, S);
    SpeculatedStore->applyMergedLocation(BI->getDebugLoc(),
                                         SpeculatedStore->getDebugLoc());
  }

  // Metadata can be dependent on the condition we are hoisting above.
  // Conservatively strip all metadata on the instruction.
  for (auto &I : *ThenBB)
    I.dropUnknownNonDebugMetadata();

  // Hoist the instructions.
  BB->getInstList().splice(BI->getIterator(), ThenBB->getInstList(),
                           ThenBB->begin(), std::prev(ThenBB->end()));

  // Insert selects and rewrite the PHI operands.
  IRBuilder<NoFolder> Builder(BI);
  for (PHINode &PN : EndBB->phis()) {
    unsigned OrigI = PN.getBasicBlockIndex(BB);
    unsigned ThenI = PN.getBasicBlockIndex(ThenBB);
    Value *OrigV = PN.getIncomingValue(OrigI);
    Value *ThenV = PN.getIncomingValue(ThenI);

    // Skip PHIs which are trivial.
    if (OrigV == ThenV)
      continue;

    // Create a select whose true value is the speculatively executed value and
    // false value is the preexisting value. Swap them if the branch
    // destinations were inverted.
    Value *TrueV = ThenV, *FalseV = OrigV;
    if (Invert)
      std::swap(TrueV, FalseV);
    Value *V = Builder.CreateSelect(
        BrCond, TrueV, FalseV, "spec.select", BI);
    PN.setIncomingValue(OrigI, V);
    PN.setIncomingValue(ThenI, V);
  }

  // Remove speculated dbg intrinsics.
  // FIXME: Is it possible to do this in a more elegant way? Moving/merging the
  // dbg value for the different flows and inserting it after the select.
  for (Instruction *I : SpeculatedDbgIntrinsics)
    I->eraseFromParent();

  ++NumSpeculations;
  return true;
}

/// Return true if we can thread a branch across this block.
static bool BlockIsSimpleEnoughToThreadThrough(BasicBlock *BB) {
  unsigned Size = 0;

  for (Instruction &I : BB->instructionsWithoutDebug()) {
    if (Size > 10)
      return false; // Don't clone large BB's.
    ++Size;

    // We can only support instructions that do not define values that are
    // live outside of the current basic block.
    for (User *U : I.users()) {
      Instruction *UI = cast<Instruction>(U);
      if (UI->getParent() != BB || isa<PHINode>(UI))
        return false;
    }

    // Looks ok, continue checking.
  }

  return true;
}

/// If we have a conditional branch on a PHI node value that is defined in the
/// same block as the branch and if any PHI entries are constants, thread edges
/// corresponding to that entry to be branches to their ultimate destination.
static bool FoldCondBranchOnPHI(BranchInst *BI, const DataLayout &DL,
                                AssumptionCache *AC) {
  BasicBlock *BB = BI->getParent();
  PHINode *PN = dyn_cast<PHINode>(BI->getCondition());
  // NOTE: we currently cannot transform this case if the PHI node is used
  // outside of the block.
  if (!PN || PN->getParent() != BB || !PN->hasOneUse())
    return false;

  // Degenerate case of a single entry PHI.
  if (PN->getNumIncomingValues() == 1) {
    FoldSingleEntryPHINodes(PN->getParent());
    return true;
  }

  // Now we know that this block has multiple preds and two succs.
  if (!BlockIsSimpleEnoughToThreadThrough(BB))
    return false;

  // Can't fold blocks that contain noduplicate or convergent calls.
  if (any_of(*BB, [](const Instruction &I) {
        const CallInst *CI = dyn_cast<CallInst>(&I);
        return CI && (CI->cannotDuplicate() || CI->isConvergent());
      }))
    return false;

  // Okay, this is a simple enough basic block.  See if any phi values are
  // constants.
  for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
    ConstantInt *CB = dyn_cast<ConstantInt>(PN->getIncomingValue(i));
    if (!CB || !CB->getType()->isIntegerTy(1))
      continue;

    // Okay, we now know that all edges from PredBB should be revectored to
    // branch to RealDest.
    BasicBlock *PredBB = PN->getIncomingBlock(i);
    BasicBlock *RealDest = BI->getSuccessor(!CB->getZExtValue());

    if (RealDest == BB)
      continue; // Skip self loops.
    // Skip if the predecessor's terminator is an indirect branch.
    if (isa<IndirectBrInst>(PredBB->getTerminator()))
      continue;

    // The dest block might have PHI nodes, other predecessors and other
    // difficult cases.  Instead of being smart about this, just insert a new
    // block that jumps to the destination block, effectively splitting
    // the edge we are about to create.
    BasicBlock *EdgeBB =
        BasicBlock::Create(BB->getContext(), RealDest->getName() + ".critedge",
                           RealDest->getParent(), RealDest);
    BranchInst::Create(RealDest, EdgeBB);

    // Update PHI nodes.
    AddPredecessorToBlock(RealDest, EdgeBB, BB);

    // BB may have instructions that are being threaded over.  Clone these
    // instructions into EdgeBB.  We know that there will be no uses of the
    // cloned instructions outside of EdgeBB.
    BasicBlock::iterator InsertPt = EdgeBB->begin();
    DenseMap<Value *, Value *> TranslateMap; // Track translated values.
    for (BasicBlock::iterator BBI = BB->begin(); &*BBI != BI; ++BBI) {
      if (PHINode *PN = dyn_cast<PHINode>(BBI)) {
        TranslateMap[PN] = PN->getIncomingValueForBlock(PredBB);
        continue;
      }
      // Clone the instruction.
      Instruction *N = BBI->clone();
      if (BBI->hasName())
        N->setName(BBI->getName() + ".c");

      // Update operands due to translation.
      for (User::op_iterator i = N->op_begin(), e = N->op_end(); i != e; ++i) {
        DenseMap<Value *, Value *>::iterator PI = TranslateMap.find(*i);
        if (PI != TranslateMap.end())
          *i = PI->second;
      }

      // Check for trivial simplification.
      if (Value *V = SimplifyInstruction(N, {DL, nullptr, nullptr, AC})) {
        if (!BBI->use_empty())
          TranslateMap[&*BBI] = V;
        if (!N->mayHaveSideEffects()) {
          N->deleteValue(); // Instruction folded away, don't need actual inst
          N = nullptr;
        }
      } else {
        if (!BBI->use_empty())
          TranslateMap[&*BBI] = N;
      }
      // Insert the new instruction into its new home.
      if (N)
        EdgeBB->getInstList().insert(InsertPt, N);

      // Register the new instruction with the assumption cache if necessary.
      if (auto *II = dyn_cast_or_null<IntrinsicInst>(N))
        if (II->getIntrinsicID() == Intrinsic::assume)
          AC->registerAssumption(II);
    }

    // Loop over all of the edges from PredBB to BB, changing them to branch
    // to EdgeBB instead.
    Instruction *PredBBTI = PredBB->getTerminator();
    for (unsigned i = 0, e = PredBBTI->getNumSuccessors(); i != e; ++i)
      if (PredBBTI->getSuccessor(i) == BB) {
        BB->removePredecessor(PredBB);
        PredBBTI->setSuccessor(i, EdgeBB);
      }

    // Recurse, simplifying any other constants.
    return FoldCondBranchOnPHI(BI, DL, AC) || true;
  }

  return false;
}

/// Given a BB that starts with the specified two-entry PHI node,
/// see if we can eliminate it.
static bool FoldTwoEntryPHINode(PHINode *PN, const TargetTransformInfo &TTI,
                                const DataLayout &DL) {
  // Ok, this is a two entry PHI node.  Check to see if this is a simple "if
  // statement", which has a very simple dominance structure.  Basically, we
  // are trying to find the condition that is being branched on, which
  // subsequently causes this merge to happen.  We really want control
  // dependence information for this check, but simplifycfg can't keep it up
  // to date, and this catches most of the cases we care about anyway.
  BasicBlock *BB = PN->getParent();
  const Function *Fn = BB->getParent();
  if (Fn && Fn->hasFnAttribute(Attribute::OptForFuzzing))
    return false;

  BasicBlock *IfTrue, *IfFalse;
  Value *IfCond = GetIfCondition(BB, IfTrue, IfFalse);
  if (!IfCond ||
      // Don't bother if the branch will be constant folded trivially.
      isa<ConstantInt>(IfCond))
    return false;

  // Okay, we found that we can merge this two-entry phi node into a select.
  // Doing so would require us to fold *all* two entry phi nodes in this block.
  // At some point this becomes non-profitable (particularly if the target
  // doesn't support cmov's).  Only do this transformation if there are two or
  // fewer PHI nodes in this block.
  unsigned NumPhis = 0;
  for (BasicBlock::iterator I = BB->begin(); isa<PHINode>(I); ++NumPhis, ++I)
    if (NumPhis > 2)
      return false;

  // Loop over the PHI's seeing if we can promote them all to select
  // instructions.  While we are at it, keep track of the instructions
  // that need to be moved to the dominating block.
  SmallPtrSet<Instruction *, 4> AggressiveInsts;
  unsigned MaxCostVal0 = PHINodeFoldingThreshold,
           MaxCostVal1 = PHINodeFoldingThreshold;
  MaxCostVal0 *= TargetTransformInfo::TCC_Basic;
  MaxCostVal1 *= TargetTransformInfo::TCC_Basic;

  for (BasicBlock::iterator II = BB->begin(); isa<PHINode>(II);) {
    PHINode *PN = cast<PHINode>(II++);
    if (Value *V = SimplifyInstruction(PN, {DL, PN})) {
      PN->replaceAllUsesWith(V);
      PN->eraseFromParent();
      continue;
    }

    if (!DominatesMergePoint(PN->getIncomingValue(0), BB, AggressiveInsts,
                             MaxCostVal0, TTI) ||
        !DominatesMergePoint(PN->getIncomingValue(1), BB, AggressiveInsts,
                             MaxCostVal1, TTI))
      return false;
  }

  // If we folded the first phi, PN dangles at this point.  Refresh it.  If
  // we ran out of PHIs then we simplified them all.
  PN = dyn_cast<PHINode>(BB->begin());
  if (!PN)
    return true;

  // Don't fold i1 branches on PHIs which contain binary operators.  These can
  // often be turned into switches and other things.
  if (PN->getType()->isIntegerTy(1) &&
      (isa<BinaryOperator>(PN->getIncomingValue(0)) ||
       isa<BinaryOperator>(PN->getIncomingValue(1)) ||
       isa<BinaryOperator>(IfCond)))
    return false;

  // If all PHI nodes are promotable, check to make sure that all instructions
  // in the predecessor blocks can be promoted as well. If not, we won't be able
  // to get rid of the control flow, so it's not worth promoting to select
  // instructions.
  BasicBlock *DomBlock = nullptr;
  BasicBlock *IfBlock1 = PN->getIncomingBlock(0);
  BasicBlock *IfBlock2 = PN->getIncomingBlock(1);
  if (cast<BranchInst>(IfBlock1->getTerminator())->isConditional()) {
    IfBlock1 = nullptr;
  } else {
    DomBlock = *pred_begin(IfBlock1);
    for (BasicBlock::iterator I = IfBlock1->begin(); !I->isTerminator(); ++I)
      if (!AggressiveInsts.count(&*I) && !isa<DbgInfoIntrinsic>(I)) {
        // This is not an aggressive instruction that we can promote.
        // Because of this, we won't be able to get rid of the control flow, so
        // the xform is not worth it.
        return false;
      }
  }

  if (cast<BranchInst>(IfBlock2->getTerminator())->isConditional()) {
    IfBlock2 = nullptr;
  } else {
    DomBlock = *pred_begin(IfBlock2);
    for (BasicBlock::iterator I = IfBlock2->begin(); !I->isTerminator(); ++I)
      if (!AggressiveInsts.count(&*I) && !isa<DbgInfoIntrinsic>(I)) {
        // This is not an aggressive instruction that we can promote.
        // Because of this, we won't be able to get rid of the control flow, so
        // the xform is not worth it.
        return false;
      }
  }

  LLVM_DEBUG(dbgs() << "FOUND IF CONDITION!  " << *IfCond
                    << "  T: " << IfTrue->getName()
                    << "  F: " << IfFalse->getName() << "\n");

  // If we can still promote the PHI nodes after this gauntlet of tests,
  // do all of the PHI's now.
  Instruction *InsertPt = DomBlock->getTerminator();
  IRBuilder<NoFolder> Builder(InsertPt);

  // Move all 'aggressive' instructions, which are defined in the
  // conditional parts of the if's up to the dominating block.
  if (IfBlock1)
    hoistAllInstructionsInto(DomBlock, InsertPt, IfBlock1);
  if (IfBlock2)
    hoistAllInstructionsInto(DomBlock, InsertPt, IfBlock2);

  while (PHINode *PN = dyn_cast<PHINode>(BB->begin())) {
    // Change the PHI node into a select instruction.
    Value *TrueVal = PN->getIncomingValue(PN->getIncomingBlock(0) == IfFalse);
    Value *FalseVal = PN->getIncomingValue(PN->getIncomingBlock(0) == IfTrue);

    Value *Sel = Builder.CreateSelect(IfCond, TrueVal, FalseVal, "", InsertPt);
    PN->replaceAllUsesWith(Sel);
    Sel->takeName(PN);
    PN->eraseFromParent();
  }

  // At this point, IfBlock1 and IfBlock2 are both empty, so our if statement
  // has been flattened.  Change DomBlock to jump directly to our new block to
  // avoid other simplifycfg's kicking in on the diamond.
  Instruction *OldTI = DomBlock->getTerminator();
  Builder.SetInsertPoint(OldTI);
  Builder.CreateBr(BB);
  OldTI->eraseFromParent();
  return true;
}

/// If we found a conditional branch that goes to two returning blocks,
/// try to merge them together into one return,
/// introducing a select if the return values disagree.
static bool SimplifyCondBranchToTwoReturns(BranchInst *BI,
                                           IRBuilder<> &Builder) {
  assert(BI->isConditional() && "Must be a conditional branch");
  BasicBlock *TrueSucc = BI->getSuccessor(0);
  BasicBlock *FalseSucc = BI->getSuccessor(1);
  ReturnInst *TrueRet = cast<ReturnInst>(TrueSucc->getTerminator());
  ReturnInst *FalseRet = cast<ReturnInst>(FalseSucc->getTerminator());

  // Check to ensure both blocks are empty (just a return) or optionally empty
  // with PHI nodes.  If there are other instructions, merging would cause extra
  // computation on one path or the other.
  if (!TrueSucc->getFirstNonPHIOrDbg()->isTerminator())
    return false;
  if (!FalseSucc->getFirstNonPHIOrDbg()->isTerminator())
    return false;

  Builder.SetInsertPoint(BI);
  // Okay, we found a branch that is going to two return nodes.  If
  // there is no return value for this function, just change the
  // branch into a return.
  if (FalseRet->getNumOperands() == 0) {
    TrueSucc->removePredecessor(BI->getParent());
    FalseSucc->removePredecessor(BI->getParent());
    Builder.CreateRetVoid();
    EraseTerminatorAndDCECond(BI);
    return true;
  }

  // Otherwise, figure out what the true and false return values are
  // so we can insert a new select instruction.
  Value *TrueValue = TrueRet->getReturnValue();
  Value *FalseValue = FalseRet->getReturnValue();

  // Unwrap any PHI nodes in the return blocks.
  if (PHINode *TVPN = dyn_cast_or_null<PHINode>(TrueValue))
    if (TVPN->getParent() == TrueSucc)
      TrueValue = TVPN->getIncomingValueForBlock(BI->getParent());
  if (PHINode *FVPN = dyn_cast_or_null<PHINode>(FalseValue))
    if (FVPN->getParent() == FalseSucc)
      FalseValue = FVPN->getIncomingValueForBlock(BI->getParent());

  // In order for this transformation to be safe, we must be able to
  // unconditionally execute both operands to the return.  This is
  // normally the case, but we could have a potentially-trapping
  // constant expression that prevents this transformation from being
  // safe.
  if (ConstantExpr *TCV = dyn_cast_or_null<ConstantExpr>(TrueValue))
    if (TCV->canTrap())
      return false;
  if (ConstantExpr *FCV = dyn_cast_or_null<ConstantExpr>(FalseValue))
    if (FCV->canTrap())
      return false;

  // Okay, we collected all the mapped values and checked them for sanity, and
  // defined to really do this transformation.  First, update the CFG.
  TrueSucc->removePredecessor(BI->getParent());
  FalseSucc->removePredecessor(BI->getParent());

  // Insert select instructions where needed.
  Value *BrCond = BI->getCondition();
  if (TrueValue) {
    // Insert a select if the results differ.
    if (TrueValue == FalseValue || isa<UndefValue>(FalseValue)) {
    } else if (isa<UndefValue>(TrueValue)) {
      TrueValue = FalseValue;
    } else {
      TrueValue =
          Builder.CreateSelect(BrCond, TrueValue, FalseValue, "retval", BI);
    }
  }

  Value *RI =
      !TrueValue ? Builder.CreateRetVoid() : Builder.CreateRet(TrueValue);

  (void)RI;

  LLVM_DEBUG(dbgs() << "\nCHANGING BRANCH TO TWO RETURNS INTO SELECT:"
                    << "\n  " << *BI << "NewRet = " << *RI << "TRUEBLOCK: "
                    << *TrueSucc << "FALSEBLOCK: " << *FalseSucc);

  EraseTerminatorAndDCECond(BI);

  return true;
}

/// Return true if the given instruction is available
/// in its predecessor block. If yes, the instruction will be removed.
static bool tryCSEWithPredecessor(Instruction *Inst, BasicBlock *PB) {
  if (!isa<BinaryOperator>(Inst) && !isa<CmpInst>(Inst))
    return false;
  for (Instruction &I : *PB) {
    Instruction *PBI = &I;
    // Check whether Inst and PBI generate the same value.
    if (Inst->isIdenticalTo(PBI)) {
      Inst->replaceAllUsesWith(PBI);
      Inst->eraseFromParent();
      return true;
    }
  }
  return false;
}

/// Return true if either PBI or BI has branch weight available, and store
/// the weights in {Pred|Succ}{True|False}Weight. If one of PBI and BI does
/// not have branch weight, use 1:1 as its weight.
static bool extractPredSuccWeights(BranchInst *PBI, BranchInst *BI,
                                   uint64_t &PredTrueWeight,
                                   uint64_t &PredFalseWeight,
                                   uint64_t &SuccTrueWeight,
                                   uint64_t &SuccFalseWeight) {
  bool PredHasWeights =
      PBI->extractProfMetadata(PredTrueWeight, PredFalseWeight);
  bool SuccHasWeights =
      BI->extractProfMetadata(SuccTrueWeight, SuccFalseWeight);
  if (PredHasWeights || SuccHasWeights) {
    if (!PredHasWeights)
      PredTrueWeight = PredFalseWeight = 1;
    if (!SuccHasWeights)
      SuccTrueWeight = SuccFalseWeight = 1;
    return true;
  } else {
    return false;
  }
}

/// If this basic block is simple enough, and if a predecessor branches to us
/// and one of our successors, fold the block into the predecessor and use
/// logical operations to pick the right destination.
bool llvm::FoldBranchToCommonDest(BranchInst *BI, unsigned BonusInstThreshold) {
  BasicBlock *BB = BI->getParent();

  const unsigned PredCount = pred_size(BB);

  Instruction *Cond = nullptr;
  if (BI->isConditional())
    Cond = dyn_cast<Instruction>(BI->getCondition());
  else {
    // For unconditional branch, check for a simple CFG pattern, where
    // BB has a single predecessor and BB's successor is also its predecessor's
    // successor. If such pattern exists, check for CSE between BB and its
    // predecessor.
    if (BasicBlock *PB = BB->getSinglePredecessor())
      if (BranchInst *PBI = dyn_cast<BranchInst>(PB->getTerminator()))
        if (PBI->isConditional() &&
            (BI->getSuccessor(0) == PBI->getSuccessor(0) ||
             BI->getSuccessor(0) == PBI->getSuccessor(1))) {
          for (auto I = BB->instructionsWithoutDebug().begin(),
                    E = BB->instructionsWithoutDebug().end();
               I != E;) {
            Instruction *Curr = &*I++;
            if (isa<CmpInst>(Curr)) {
              Cond = Curr;
              break;
            }
            // Quit if we can't remove this instruction.
            if (!tryCSEWithPredecessor(Curr, PB))
              return false;
          }
        }

    if (!Cond)
      return false;
  }

  if (!Cond || (!isa<CmpInst>(Cond) && !isa<BinaryOperator>(Cond)) ||
      Cond->getParent() != BB || !Cond->hasOneUse())
    return false;

  // Make sure the instruction after the condition is the cond branch.
  BasicBlock::iterator CondIt = ++Cond->getIterator();

  // Ignore dbg intrinsics.
  while (isa<DbgInfoIntrinsic>(CondIt))
    ++CondIt;

  if (&*CondIt != BI)
    return false;

  // Only allow this transformation if computing the condition doesn't involve
  // too many instructions and these involved instructions can be executed
  // unconditionally. We denote all involved instructions except the condition
  // as "bonus instructions", and only allow this transformation when the
  // number of the bonus instructions we'll need to create when cloning into
  // each predecessor does not exceed a certain threshold. 
  unsigned NumBonusInsts = 0;
  for (auto I = BB->begin(); Cond != &*I; ++I) {
    // Ignore dbg intrinsics.
    if (isa<DbgInfoIntrinsic>(I))
      continue;
    if (!I->hasOneUse() || !isSafeToSpeculativelyExecute(&*I))
      return false;
    // I has only one use and can be executed unconditionally.
    Instruction *User = dyn_cast<Instruction>(I->user_back());
    if (User == nullptr || User->getParent() != BB)
      return false;
    // I is used in the same BB. Since BI uses Cond and doesn't have more slots
    // to use any other instruction, User must be an instruction between next(I)
    // and Cond.

    // Account for the cost of duplicating this instruction into each
    // predecessor. 
    NumBonusInsts += PredCount;
    // Early exits once we reach the limit.
    if (NumBonusInsts > BonusInstThreshold)
      return false;
  }

  // Cond is known to be a compare or binary operator.  Check to make sure that
  // neither operand is a potentially-trapping constant expression.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(Cond->getOperand(0)))
    if (CE->canTrap())
      return false;
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(Cond->getOperand(1)))
    if (CE->canTrap())
      return false;

  // Finally, don't infinitely unroll conditional loops.
  BasicBlock *TrueDest = BI->getSuccessor(0);
  BasicBlock *FalseDest = (BI->isConditional()) ? BI->getSuccessor(1) : nullptr;
  if (TrueDest == BB || FalseDest == BB)
    return false;

  for (pred_iterator PI = pred_begin(BB), E = pred_end(BB); PI != E; ++PI) {
    BasicBlock *PredBlock = *PI;
    BranchInst *PBI = dyn_cast<BranchInst>(PredBlock->getTerminator());

    // Check that we have two conditional branches.  If there is a PHI node in
    // the common successor, verify that the same value flows in from both
    // blocks.
    SmallVector<PHINode *, 4> PHIs;
    if (!PBI || PBI->isUnconditional() ||
        (BI->isConditional() && !SafeToMergeTerminators(BI, PBI)) ||
        (!BI->isConditional() &&
         !isProfitableToFoldUnconditional(BI, PBI, Cond, PHIs)))
      continue;

    // Determine if the two branches share a common destination.
    Instruction::BinaryOps Opc = Instruction::BinaryOpsEnd;
    bool InvertPredCond = false;

    if (BI->isConditional()) {
      if (PBI->getSuccessor(0) == TrueDest) {
        Opc = Instruction::Or;
      } else if (PBI->getSuccessor(1) == FalseDest) {
        Opc = Instruction::And;
      } else if (PBI->getSuccessor(0) == FalseDest) {
        Opc = Instruction::And;
        InvertPredCond = true;
      } else if (PBI->getSuccessor(1) == TrueDest) {
        Opc = Instruction::Or;
        InvertPredCond = true;
      } else {
        continue;
      }
    } else {
      if (PBI->getSuccessor(0) != TrueDest && PBI->getSuccessor(1) != TrueDest)
        continue;
    }

    LLVM_DEBUG(dbgs() << "FOLDING BRANCH TO COMMON DEST:\n" << *PBI << *BB);
    IRBuilder<> Builder(PBI);

    // If we need to invert the condition in the pred block to match, do so now.
    if (InvertPredCond) {
      Value *NewCond = PBI->getCondition();

      if (NewCond->hasOneUse() && isa<CmpInst>(NewCond)) {
        CmpInst *CI = cast<CmpInst>(NewCond);
        CI->setPredicate(CI->getInversePredicate());
      } else {
        NewCond =
            Builder.CreateNot(NewCond, PBI->getCondition()->getName() + ".not");
      }

      PBI->setCondition(NewCond);
      PBI->swapSuccessors();
    }

    // If we have bonus instructions, clone them into the predecessor block.
    // Note that there may be multiple predecessor blocks, so we cannot move
    // bonus instructions to a predecessor block.
    ValueToValueMapTy VMap; // maps original values to cloned values
    // We already make sure Cond is the last instruction before BI. Therefore,
    // all instructions before Cond other than DbgInfoIntrinsic are bonus
    // instructions.
    for (auto BonusInst = BB->begin(); Cond != &*BonusInst; ++BonusInst) {
      if (isa<DbgInfoIntrinsic>(BonusInst))
        continue;
      Instruction *NewBonusInst = BonusInst->clone();
      RemapInstruction(NewBonusInst, VMap,
                       RF_NoModuleLevelChanges | RF_IgnoreMissingLocals);
      VMap[&*BonusInst] = NewBonusInst;

      // If we moved a load, we cannot any longer claim any knowledge about
      // its potential value. The previous information might have been valid
      // only given the branch precondition.
      // For an analogous reason, we must also drop all the metadata whose
      // semantics we don't understand.
      NewBonusInst->dropUnknownNonDebugMetadata();

      PredBlock->getInstList().insert(PBI->getIterator(), NewBonusInst);
      NewBonusInst->takeName(&*BonusInst);
      BonusInst->setName(BonusInst->getName() + ".old");
    }

    // Clone Cond into the predecessor basic block, and or/and the
    // two conditions together.
    Instruction *CondInPred = Cond->clone();
    RemapInstruction(CondInPred, VMap,
                     RF_NoModuleLevelChanges | RF_IgnoreMissingLocals);
    PredBlock->getInstList().insert(PBI->getIterator(), CondInPred);
    CondInPred->takeName(Cond);
    Cond->setName(CondInPred->getName() + ".old");

    if (BI->isConditional()) {
      Instruction *NewCond = cast<Instruction>(
          Builder.CreateBinOp(Opc, PBI->getCondition(), CondInPred, "or.cond"));
      PBI->setCondition(NewCond);

      uint64_t PredTrueWeight, PredFalseWeight, SuccTrueWeight, SuccFalseWeight;
      bool HasWeights =
          extractPredSuccWeights(PBI, BI, PredTrueWeight, PredFalseWeight,
                                 SuccTrueWeight, SuccFalseWeight);
      SmallVector<uint64_t, 8> NewWeights;

      if (PBI->getSuccessor(0) == BB) {
        if (HasWeights) {
          // PBI: br i1 %x, BB, FalseDest
          // BI:  br i1 %y, TrueDest, FalseDest
          // TrueWeight is TrueWeight for PBI * TrueWeight for BI.
          NewWeights.push_back(PredTrueWeight * SuccTrueWeight);
          // FalseWeight is FalseWeight for PBI * TotalWeight for BI +
          //               TrueWeight for PBI * FalseWeight for BI.
          // We assume that total weights of a BranchInst can fit into 32 bits.
          // Therefore, we will not have overflow using 64-bit arithmetic.
          NewWeights.push_back(PredFalseWeight *
                                   (SuccFalseWeight + SuccTrueWeight) +
                               PredTrueWeight * SuccFalseWeight);
        }
        AddPredecessorToBlock(TrueDest, PredBlock, BB);
        PBI->setSuccessor(0, TrueDest);
      }
      if (PBI->getSuccessor(1) == BB) {
        if (HasWeights) {
          // PBI: br i1 %x, TrueDest, BB
          // BI:  br i1 %y, TrueDest, FalseDest
          // TrueWeight is TrueWeight for PBI * TotalWeight for BI +
          //              FalseWeight for PBI * TrueWeight for BI.
          NewWeights.push_back(PredTrueWeight *
                                   (SuccFalseWeight + SuccTrueWeight) +
                               PredFalseWeight * SuccTrueWeight);
          // FalseWeight is FalseWeight for PBI * FalseWeight for BI.
          NewWeights.push_back(PredFalseWeight * SuccFalseWeight);
        }
        AddPredecessorToBlock(FalseDest, PredBlock, BB);
        PBI->setSuccessor(1, FalseDest);
      }
      if (NewWeights.size() == 2) {
        // Halve the weights if any of them cannot fit in an uint32_t
        FitWeights(NewWeights);

        SmallVector<uint32_t, 8> MDWeights(NewWeights.begin(),
                                           NewWeights.end());
        setBranchWeights(PBI, MDWeights[0], MDWeights[1]);
      } else
        PBI->setMetadata(LLVMContext::MD_prof, nullptr);
    } else {
      // Update PHI nodes in the common successors.
      for (unsigned i = 0, e = PHIs.size(); i != e; ++i) {
        ConstantInt *PBI_C = cast<ConstantInt>(
            PHIs[i]->getIncomingValueForBlock(PBI->getParent()));
        assert(PBI_C->getType()->isIntegerTy(1));
        Instruction *MergedCond = nullptr;
        if (PBI->getSuccessor(0) == TrueDest) {
          // Create (PBI_Cond and PBI_C) or (!PBI_Cond and BI_Value)
          // PBI_C is true: PBI_Cond or (!PBI_Cond and BI_Value)
          //       is false: !PBI_Cond and BI_Value
          Instruction *NotCond = cast<Instruction>(
              Builder.CreateNot(PBI->getCondition(), "not.cond"));
          MergedCond = cast<Instruction>(
               Builder.CreateBinOp(Instruction::And, NotCond, CondInPred,
                                   "and.cond"));
          if (PBI_C->isOne())
            MergedCond = cast<Instruction>(Builder.CreateBinOp(
                Instruction::Or, PBI->getCondition(), MergedCond, "or.cond"));
        } else {
          // Create (PBI_Cond and BI_Value) or (!PBI_Cond and PBI_C)
          // PBI_C is true: (PBI_Cond and BI_Value) or (!PBI_Cond)
          //       is false: PBI_Cond and BI_Value
          MergedCond = cast<Instruction>(Builder.CreateBinOp(
              Instruction::And, PBI->getCondition(), CondInPred, "and.cond"));
          if (PBI_C->isOne()) {
            Instruction *NotCond = cast<Instruction>(
                Builder.CreateNot(PBI->getCondition(), "not.cond"));
            MergedCond = cast<Instruction>(Builder.CreateBinOp(
                Instruction::Or, NotCond, MergedCond, "or.cond"));
          }
        }
        // Update PHI Node.
        PHIs[i]->setIncomingValue(PHIs[i]->getBasicBlockIndex(PBI->getParent()),
                                  MergedCond);
      }
      // Change PBI from Conditional to Unconditional.
      BranchInst *New_PBI = BranchInst::Create(TrueDest, PBI);
      EraseTerminatorAndDCECond(PBI);
      PBI = New_PBI;
    }

    // If BI was a loop latch, it may have had associated loop metadata.
    // We need to copy it to the new latch, that is, PBI.
    if (MDNode *LoopMD = BI->getMetadata(LLVMContext::MD_loop))
      PBI->setMetadata(LLVMContext::MD_loop, LoopMD);

    // TODO: If BB is reachable from all paths through PredBlock, then we
    // could replace PBI's branch probabilities with BI's.

    // Copy any debug value intrinsics into the end of PredBlock.
    for (Instruction &I : *BB)
      if (isa<DbgInfoIntrinsic>(I))
        I.clone()->insertBefore(PBI);

    return true;
  }
  return false;
}

// If there is only one store in BB1 and BB2, return it, otherwise return
// nullptr.
static StoreInst *findUniqueStoreInBlocks(BasicBlock *BB1, BasicBlock *BB2) {
  StoreInst *S = nullptr;
  for (auto *BB : {BB1, BB2}) {
    if (!BB)
      continue;
    for (auto &I : *BB)
      if (auto *SI = dyn_cast<StoreInst>(&I)) {
        if (S)
          // Multiple stores seen.
          return nullptr;
        else
          S = SI;
      }
  }
  return S;
}

static Value *ensureValueAvailableInSuccessor(Value *V, BasicBlock *BB,
                                              Value *AlternativeV = nullptr) {
  // PHI is going to be a PHI node that allows the value V that is defined in
  // BB to be referenced in BB's only successor.
  //
  // If AlternativeV is nullptr, the only value we care about in PHI is V. It
  // doesn't matter to us what the other operand is (it'll never get used). We
  // could just create a new PHI with an undef incoming value, but that could
  // increase register pressure if EarlyCSE/InstCombine can't fold it with some
  // other PHI. So here we directly look for some PHI in BB's successor with V
  // as an incoming operand. If we find one, we use it, else we create a new
  // one.
  //
  // If AlternativeV is not nullptr, we care about both incoming values in PHI.
  // PHI must be exactly: phi <ty> [ %BB, %V ], [ %OtherBB, %AlternativeV]
  // where OtherBB is the single other predecessor of BB's only successor.
  PHINode *PHI = nullptr;
  BasicBlock *Succ = BB->getSingleSuccessor();

  for (auto I = Succ->begin(); isa<PHINode>(I); ++I)
    if (cast<PHINode>(I)->getIncomingValueForBlock(BB) == V) {
      PHI = cast<PHINode>(I);
      if (!AlternativeV)
        break;

      assert(Succ->hasNPredecessors(2));
      auto PredI = pred_begin(Succ);
      BasicBlock *OtherPredBB = *PredI == BB ? *++PredI : *PredI;
      if (PHI->getIncomingValueForBlock(OtherPredBB) == AlternativeV)
        break;
      PHI = nullptr;
    }
  if (PHI)
    return PHI;

  // If V is not an instruction defined in BB, just return it.
  if (!AlternativeV &&
      (!isa<Instruction>(V) || cast<Instruction>(V)->getParent() != BB))
    return V;

  PHI = PHINode::Create(V->getType(), 2, "simplifycfg.merge", &Succ->front());
  PHI->addIncoming(V, BB);
  for (BasicBlock *PredBB : predecessors(Succ))
    if (PredBB != BB)
      PHI->addIncoming(
          AlternativeV ? AlternativeV : UndefValue::get(V->getType()), PredBB);
  return PHI;
}

static bool mergeConditionalStoreToAddress(BasicBlock *PTB, BasicBlock *PFB,
                                           BasicBlock *QTB, BasicBlock *QFB,
                                           BasicBlock *PostBB, Value *Address,
                                           bool InvertPCond, bool InvertQCond,
                                           const DataLayout &DL) {
  auto IsaBitcastOfPointerType = [](const Instruction &I) {
    return Operator::getOpcode(&I) == Instruction::BitCast &&
           I.getType()->isPointerTy();
  };

  // If we're not in aggressive mode, we only optimize if we have some
  // confidence that by optimizing we'll allow P and/or Q to be if-converted.
  auto IsWorthwhile = [&](BasicBlock *BB) {
    if (!BB)
      return true;
    // Heuristic: if the block can be if-converted/phi-folded and the
    // instructions inside are all cheap (arithmetic/GEPs), it's worthwhile to
    // thread this store.
    unsigned N = 0;
    for (auto &I : BB->instructionsWithoutDebug()) {
      // Cheap instructions viable for folding.
      if (isa<BinaryOperator>(I) || isa<GetElementPtrInst>(I) ||
          isa<StoreInst>(I))
        ++N;
      // Free instructions.
      else if (I.isTerminator() || IsaBitcastOfPointerType(I))
        continue;
      else
        return false;
    }
    // The store we want to merge is counted in N, so add 1 to make sure
    // we're counting the instructions that would be left.
    return N <= (PHINodeFoldingThreshold + 1);
  };

  if (!MergeCondStoresAggressively &&
      (!IsWorthwhile(PTB) || !IsWorthwhile(PFB) || !IsWorthwhile(QTB) ||
       !IsWorthwhile(QFB)))
    return false;

  // For every pointer, there must be exactly two stores, one coming from
  // PTB or PFB, and the other from QTB or QFB. We don't support more than one
  // store (to any address) in PTB,PFB or QTB,QFB.
  // FIXME: We could relax this restriction with a bit more work and performance
  // testing.
  StoreInst *PStore = findUniqueStoreInBlocks(PTB, PFB);
  StoreInst *QStore = findUniqueStoreInBlocks(QTB, QFB);
  if (!PStore || !QStore)
    return false;

  // Now check the stores are compatible.
  if (!QStore->isUnordered() || !PStore->isUnordered())
    return false;

  // Check that sinking the store won't cause program behavior changes. Sinking
  // the store out of the Q blocks won't change any behavior as we're sinking
  // from a block to its unconditional successor. But we're moving a store from
  // the P blocks down through the middle block (QBI) and past both QFB and QTB.
  // So we need to check that there are no aliasing loads or stores in
  // QBI, QTB and QFB. We also need to check there are no conflicting memory
  // operations between PStore and the end of its parent block.
  //
  // The ideal way to do this is to query AliasAnalysis, but we don't
  // preserve AA currently so that is dangerous. Be super safe and just
  // check there are no other memory operations at all.
  for (auto &I : *QFB->getSinglePredecessor())
    if (I.mayReadOrWriteMemory())
      return false;
  for (auto &I : *QFB)
    if (&I != QStore && I.mayReadOrWriteMemory())
      return false;
  if (QTB)
    for (auto &I : *QTB)
      if (&I != QStore && I.mayReadOrWriteMemory())
        return false;
  for (auto I = BasicBlock::iterator(PStore), E = PStore->getParent()->end();
       I != E; ++I)
    if (&*I != PStore && I->mayReadOrWriteMemory())
      return false;

  // If PostBB has more than two predecessors, we need to split it so we can
  // sink the store.
  if (std::next(pred_begin(PostBB), 2) != pred_end(PostBB)) {
    // We know that QFB's only successor is PostBB. And QFB has a single
    // predecessor. If QTB exists, then its only successor is also PostBB.
    // If QTB does not exist, then QFB's only predecessor has a conditional
    // branch to QFB and PostBB.
    BasicBlock *TruePred = QTB ? QTB : QFB->getSinglePredecessor();
    BasicBlock *NewBB = SplitBlockPredecessors(PostBB, { QFB, TruePred},
                                               "condstore.split");
    if (!NewBB)
      return false;
    PostBB = NewBB;
  }

  // OK, we're going to sink the stores to PostBB. The store has to be
  // conditional though, so first create the predicate.
  Value *PCond = cast<BranchInst>(PFB->getSinglePredecessor()->getTerminator())
                     ->getCondition();
  Value *QCond = cast<BranchInst>(QFB->getSinglePredecessor()->getTerminator())
                     ->getCondition();

  Value *PPHI = ensureValueAvailableInSuccessor(PStore->getValueOperand(),
                                                PStore->getParent());
  Value *QPHI = ensureValueAvailableInSuccessor(QStore->getValueOperand(),
                                                QStore->getParent(), PPHI);

  IRBuilder<> QB(&*PostBB->getFirstInsertionPt());

  Value *PPred = PStore->getParent() == PTB ? PCond : QB.CreateNot(PCond);
  Value *QPred = QStore->getParent() == QTB ? QCond : QB.CreateNot(QCond);

  if (InvertPCond)
    PPred = QB.CreateNot(PPred);
  if (InvertQCond)
    QPred = QB.CreateNot(QPred);
  Value *CombinedPred = QB.CreateOr(PPred, QPred);

  auto *T =
      SplitBlockAndInsertIfThen(CombinedPred, &*QB.GetInsertPoint(), false);
  QB.SetInsertPoint(T);
  StoreInst *SI = cast<StoreInst>(QB.CreateStore(QPHI, Address));
  AAMDNodes AAMD;
  PStore->getAAMetadata(AAMD, /*Merge=*/false);
  PStore->getAAMetadata(AAMD, /*Merge=*/true);
  SI->setAAMetadata(AAMD);
  unsigned PAlignment = PStore->getAlignment();
  unsigned QAlignment = QStore->getAlignment();
  unsigned TypeAlignment =
      DL.getABITypeAlignment(SI->getValueOperand()->getType());
  unsigned MinAlignment;
  unsigned MaxAlignment;
  std::tie(MinAlignment, MaxAlignment) = std::minmax(PAlignment, QAlignment);
  // Choose the minimum alignment. If we could prove both stores execute, we
  // could use biggest one.  In this case, though, we only know that one of the
  // stores executes.  And we don't know it's safe to take the alignment from a
  // store that doesn't execute.
  if (MinAlignment != 0) {
    // Choose the minimum of all non-zero alignments.
    SI->setAlignment(MinAlignment);
  } else if (MaxAlignment != 0) {
    // Choose the minimal alignment between the non-zero alignment and the ABI
    // default alignment for the type of the stored value.
    SI->setAlignment(std::min(MaxAlignment, TypeAlignment));
  } else {
    // If both alignments are zero, use ABI default alignment for the type of
    // the stored value.
    SI->setAlignment(TypeAlignment);
  }

  QStore->eraseFromParent();
  PStore->eraseFromParent();

  return true;
}

static bool mergeConditionalStores(BranchInst *PBI, BranchInst *QBI,
                                   const DataLayout &DL) {
  // The intention here is to find diamonds or triangles (see below) where each
  // conditional block contains a store to the same address. Both of these
  // stores are conditional, so they can't be unconditionally sunk. But it may
  // be profitable to speculatively sink the stores into one merged store at the
  // end, and predicate the merged store on the union of the two conditions of
  // PBI and QBI.
  //
  // This can reduce the number of stores executed if both of the conditions are
  // true, and can allow the blocks to become small enough to be if-converted.
  // This optimization will also chain, so that ladders of test-and-set
  // sequences can be if-converted away.
  //
  // We only deal with simple diamonds or triangles:
  //
  //     PBI       or      PBI        or a combination of the two
  //    /   \               | \
  //   PTB  PFB             |  PFB
  //    \   /               | /
  //     QBI                QBI
  //    /  \                | \
  //   QTB  QFB             |  QFB
  //    \  /                | /
  //    PostBB            PostBB
  //
  // We model triangles as a type of diamond with a nullptr "true" block.
  // Triangles are canonicalized so that the fallthrough edge is represented by
  // a true condition, as in the diagram above.
  BasicBlock *PTB = PBI->getSuccessor(0);
  BasicBlock *PFB = PBI->getSuccessor(1);
  BasicBlock *QTB = QBI->getSuccessor(0);
  BasicBlock *QFB = QBI->getSuccessor(1);
  BasicBlock *PostBB = QFB->getSingleSuccessor();

  // Make sure we have a good guess for PostBB. If QTB's only successor is
  // QFB, then QFB is a better PostBB.
  if (QTB->getSingleSuccessor() == QFB)
    PostBB = QFB;

  // If we couldn't find a good PostBB, stop.
  if (!PostBB)
    return false;

  bool InvertPCond = false, InvertQCond = false;
  // Canonicalize fallthroughs to the true branches.
  if (PFB == QBI->getParent()) {
    std::swap(PFB, PTB);
    InvertPCond = true;
  }
  if (QFB == PostBB) {
    std::swap(QFB, QTB);
    InvertQCond = true;
  }

  // From this point on we can assume PTB or QTB may be fallthroughs but PFB
  // and QFB may not. Model fallthroughs as a nullptr block.
  if (PTB == QBI->getParent())
    PTB = nullptr;
  if (QTB == PostBB)
    QTB = nullptr;

  // Legality bailouts. We must have at least the non-fallthrough blocks and
  // the post-dominating block, and the non-fallthroughs must only have one
  // predecessor.
  auto HasOnePredAndOneSucc = [](BasicBlock *BB, BasicBlock *P, BasicBlock *S) {
    return BB->getSinglePredecessor() == P && BB->getSingleSuccessor() == S;
  };
  if (!HasOnePredAndOneSucc(PFB, PBI->getParent(), QBI->getParent()) ||
      !HasOnePredAndOneSucc(QFB, QBI->getParent(), PostBB))
    return false;
  if ((PTB && !HasOnePredAndOneSucc(PTB, PBI->getParent(), QBI->getParent())) ||
      (QTB && !HasOnePredAndOneSucc(QTB, QBI->getParent(), PostBB)))
    return false;
  if (!QBI->getParent()->hasNUses(2))
    return false;

  // OK, this is a sequence of two diamonds or triangles.
  // Check if there are stores in PTB or PFB that are repeated in QTB or QFB.
  SmallPtrSet<Value *, 4> PStoreAddresses, QStoreAddresses;
  for (auto *BB : {PTB, PFB}) {
    if (!BB)
      continue;
    for (auto &I : *BB)
      if (StoreInst *SI = dyn_cast<StoreInst>(&I))
        PStoreAddresses.insert(SI->getPointerOperand());
  }
  for (auto *BB : {QTB, QFB}) {
    if (!BB)
      continue;
    for (auto &I : *BB)
      if (StoreInst *SI = dyn_cast<StoreInst>(&I))
        QStoreAddresses.insert(SI->getPointerOperand());
  }

  set_intersect(PStoreAddresses, QStoreAddresses);
  // set_intersect mutates PStoreAddresses in place. Rename it here to make it
  // clear what it contains.
  auto &CommonAddresses = PStoreAddresses;

  bool Changed = false;
  for (auto *Address : CommonAddresses)
    Changed |= mergeConditionalStoreToAddress(
        PTB, PFB, QTB, QFB, PostBB, Address, InvertPCond, InvertQCond, DL);
  return Changed;
}

/// If we have a conditional branch as a predecessor of another block,
/// this function tries to simplify it.  We know
/// that PBI and BI are both conditional branches, and BI is in one of the
/// successor blocks of PBI - PBI branches to BI.
static bool SimplifyCondBranchToCondBranch(BranchInst *PBI, BranchInst *BI,
                                           const DataLayout &DL) {
  assert(PBI->isConditional() && BI->isConditional());
  BasicBlock *BB = BI->getParent();

  // If this block ends with a branch instruction, and if there is a
  // predecessor that ends on a branch of the same condition, make
  // this conditional branch redundant.
  if (PBI->getCondition() == BI->getCondition() &&
      PBI->getSuccessor(0) != PBI->getSuccessor(1)) {
    // Okay, the outcome of this conditional branch is statically
    // knowable.  If this block had a single pred, handle specially.
    if (BB->getSinglePredecessor()) {
      // Turn this into a branch on constant.
      bool CondIsTrue = PBI->getSuccessor(0) == BB;
      BI->setCondition(
          ConstantInt::get(Type::getInt1Ty(BB->getContext()), CondIsTrue));
      return true; // Nuke the branch on constant.
    }

    // Otherwise, if there are multiple predecessors, insert a PHI that merges
    // in the constant and simplify the block result.  Subsequent passes of
    // simplifycfg will thread the block.
    if (BlockIsSimpleEnoughToThreadThrough(BB)) {
      pred_iterator PB = pred_begin(BB), PE = pred_end(BB);
      PHINode *NewPN = PHINode::Create(
          Type::getInt1Ty(BB->getContext()), std::distance(PB, PE),
          BI->getCondition()->getName() + ".pr", &BB->front());
      // Okay, we're going to insert the PHI node.  Since PBI is not the only
      // predecessor, compute the PHI'd conditional value for all of the preds.
      // Any predecessor where the condition is not computable we keep symbolic.
      for (pred_iterator PI = PB; PI != PE; ++PI) {
        BasicBlock *P = *PI;
        if ((PBI = dyn_cast<BranchInst>(P->getTerminator())) && PBI != BI &&
            PBI->isConditional() && PBI->getCondition() == BI->getCondition() &&
            PBI->getSuccessor(0) != PBI->getSuccessor(1)) {
          bool CondIsTrue = PBI->getSuccessor(0) == BB;
          NewPN->addIncoming(
              ConstantInt::get(Type::getInt1Ty(BB->getContext()), CondIsTrue),
              P);
        } else {
          NewPN->addIncoming(BI->getCondition(), P);
        }
      }

      BI->setCondition(NewPN);
      return true;
    }
  }

  if (auto *CE = dyn_cast<ConstantExpr>(BI->getCondition()))
    if (CE->canTrap())
      return false;

  // If both branches are conditional and both contain stores to the same
  // address, remove the stores from the conditionals and create a conditional
  // merged store at the end.
  if (MergeCondStores && mergeConditionalStores(PBI, BI, DL))
    return true;

  // If this is a conditional branch in an empty block, and if any
  // predecessors are a conditional branch to one of our destinations,
  // fold the conditions into logical ops and one cond br.

  // Ignore dbg intrinsics.
  if (&*BB->instructionsWithoutDebug().begin() != BI)
    return false;

  int PBIOp, BIOp;
  if (PBI->getSuccessor(0) == BI->getSuccessor(0)) {
    PBIOp = 0;
    BIOp = 0;
  } else if (PBI->getSuccessor(0) == BI->getSuccessor(1)) {
    PBIOp = 0;
    BIOp = 1;
  } else if (PBI->getSuccessor(1) == BI->getSuccessor(0)) {
    PBIOp = 1;
    BIOp = 0;
  } else if (PBI->getSuccessor(1) == BI->getSuccessor(1)) {
    PBIOp = 1;
    BIOp = 1;
  } else {
    return false;
  }

  // Check to make sure that the other destination of this branch
  // isn't BB itself.  If so, this is an infinite loop that will
  // keep getting unwound.
  if (PBI->getSuccessor(PBIOp) == BB)
    return false;

  // Do not perform this transformation if it would require
  // insertion of a large number of select instructions. For targets
  // without predication/cmovs, this is a big pessimization.

  // Also do not perform this transformation if any phi node in the common
  // destination block can trap when reached by BB or PBB (PR17073). In that
  // case, it would be unsafe to hoist the operation into a select instruction.

  BasicBlock *CommonDest = PBI->getSuccessor(PBIOp);
  unsigned NumPhis = 0;
  for (BasicBlock::iterator II = CommonDest->begin(); isa<PHINode>(II);
       ++II, ++NumPhis) {
    if (NumPhis > 2) // Disable this xform.
      return false;

    PHINode *PN = cast<PHINode>(II);
    Value *BIV = PN->getIncomingValueForBlock(BB);
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(BIV))
      if (CE->canTrap())
        return false;

    unsigned PBBIdx = PN->getBasicBlockIndex(PBI->getParent());
    Value *PBIV = PN->getIncomingValue(PBBIdx);
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(PBIV))
      if (CE->canTrap())
        return false;
  }

  // Finally, if everything is ok, fold the branches to logical ops.
  BasicBlock *OtherDest = BI->getSuccessor(BIOp ^ 1);

  LLVM_DEBUG(dbgs() << "FOLDING BRs:" << *PBI->getParent()
                    << "AND: " << *BI->getParent());

  // If OtherDest *is* BB, then BB is a basic block with a single conditional
  // branch in it, where one edge (OtherDest) goes back to itself but the other
  // exits.  We don't *know* that the program avoids the infinite loop
  // (even though that seems likely).  If we do this xform naively, we'll end up
  // recursively unpeeling the loop.  Since we know that (after the xform is
  // done) that the block *is* infinite if reached, we just make it an obviously
  // infinite loop with no cond branch.
  if (OtherDest == BB) {
    // Insert it at the end of the function, because it's either code,
    // or it won't matter if it's hot. :)
    BasicBlock *InfLoopBlock =
        BasicBlock::Create(BB->getContext(), "infloop", BB->getParent());
    BranchInst::Create(InfLoopBlock, InfLoopBlock);
    OtherDest = InfLoopBlock;
  }

  LLVM_DEBUG(dbgs() << *PBI->getParent()->getParent());

  // BI may have other predecessors.  Because of this, we leave
  // it alone, but modify PBI.

  // Make sure we get to CommonDest on True&True directions.
  Value *PBICond = PBI->getCondition();
  IRBuilder<NoFolder> Builder(PBI);
  if (PBIOp)
    PBICond = Builder.CreateNot(PBICond, PBICond->getName() + ".not");

  Value *BICond = BI->getCondition();
  if (BIOp)
    BICond = Builder.CreateNot(BICond, BICond->getName() + ".not");

  // Merge the conditions.
  Value *Cond = Builder.CreateOr(PBICond, BICond, "brmerge");

  // Modify PBI to branch on the new condition to the new dests.
  PBI->setCondition(Cond);
  PBI->setSuccessor(0, CommonDest);
  PBI->setSuccessor(1, OtherDest);

  // Update branch weight for PBI.
  uint64_t PredTrueWeight, PredFalseWeight, SuccTrueWeight, SuccFalseWeight;
  uint64_t PredCommon, PredOther, SuccCommon, SuccOther;
  bool HasWeights =
      extractPredSuccWeights(PBI, BI, PredTrueWeight, PredFalseWeight,
                             SuccTrueWeight, SuccFalseWeight);
  if (HasWeights) {
    PredCommon = PBIOp ? PredFalseWeight : PredTrueWeight;
    PredOther = PBIOp ? PredTrueWeight : PredFalseWeight;
    SuccCommon = BIOp ? SuccFalseWeight : SuccTrueWeight;
    SuccOther = BIOp ? SuccTrueWeight : SuccFalseWeight;
    // The weight to CommonDest should be PredCommon * SuccTotal +
    //                                    PredOther * SuccCommon.
    // The weight to OtherDest should be PredOther * SuccOther.
    uint64_t NewWeights[2] = {PredCommon * (SuccCommon + SuccOther) +
                                  PredOther * SuccCommon,
                              PredOther * SuccOther};
    // Halve the weights if any of them cannot fit in an uint32_t
    FitWeights(NewWeights);

    setBranchWeights(PBI, NewWeights[0], NewWeights[1]);
  }

  // OtherDest may have phi nodes.  If so, add an entry from PBI's
  // block that are identical to the entries for BI's block.
  AddPredecessorToBlock(OtherDest, PBI->getParent(), BB);

  // We know that the CommonDest already had an edge from PBI to
  // it.  If it has PHIs though, the PHIs may have different
  // entries for BB and PBI's BB.  If so, insert a select to make
  // them agree.
  for (PHINode &PN : CommonDest->phis()) {
    Value *BIV = PN.getIncomingValueForBlock(BB);
    unsigned PBBIdx = PN.getBasicBlockIndex(PBI->getParent());
    Value *PBIV = PN.getIncomingValue(PBBIdx);
    if (BIV != PBIV) {
      // Insert a select in PBI to pick the right value.
      SelectInst *NV = cast<SelectInst>(
          Builder.CreateSelect(PBICond, PBIV, BIV, PBIV->getName() + ".mux"));
      PN.setIncomingValue(PBBIdx, NV);
      // Although the select has the same condition as PBI, the original branch
      // weights for PBI do not apply to the new select because the select's
      // 'logical' edges are incoming edges of the phi that is eliminated, not
      // the outgoing edges of PBI.
      if (HasWeights) {
        uint64_t PredCommon = PBIOp ? PredFalseWeight : PredTrueWeight;
        uint64_t PredOther = PBIOp ? PredTrueWeight : PredFalseWeight;
        uint64_t SuccCommon = BIOp ? SuccFalseWeight : SuccTrueWeight;
        uint64_t SuccOther = BIOp ? SuccTrueWeight : SuccFalseWeight;
        // The weight to PredCommonDest should be PredCommon * SuccTotal.
        // The weight to PredOtherDest should be PredOther * SuccCommon.
        uint64_t NewWeights[2] = {PredCommon * (SuccCommon + SuccOther),
                                  PredOther * SuccCommon};

        FitWeights(NewWeights);

        setBranchWeights(NV, NewWeights[0], NewWeights[1]);
      }
    }
  }

  LLVM_DEBUG(dbgs() << "INTO: " << *PBI->getParent());
  LLVM_DEBUG(dbgs() << *PBI->getParent()->getParent());

  // This basic block is probably dead.  We know it has at least
  // one fewer predecessor.
  return true;
}

// Simplifies a terminator by replacing it with a branch to TrueBB if Cond is
// true or to FalseBB if Cond is false.
// Takes care of updating the successors and removing the old terminator.
// Also makes sure not to introduce new successors by assuming that edges to
// non-successor TrueBBs and FalseBBs aren't reachable.
static bool SimplifyTerminatorOnSelect(Instruction *OldTerm, Value *Cond,
                                       BasicBlock *TrueBB, BasicBlock *FalseBB,
                                       uint32_t TrueWeight,
                                       uint32_t FalseWeight) {
  // Remove any superfluous successor edges from the CFG.
  // First, figure out which successors to preserve.
  // If TrueBB and FalseBB are equal, only try to preserve one copy of that
  // successor.
  BasicBlock *KeepEdge1 = TrueBB;
  BasicBlock *KeepEdge2 = TrueBB != FalseBB ? FalseBB : nullptr;

  // Then remove the rest.
  for (BasicBlock *Succ : successors(OldTerm)) {
    // Make sure only to keep exactly one copy of each edge.
    if (Succ == KeepEdge1)
      KeepEdge1 = nullptr;
    else if (Succ == KeepEdge2)
      KeepEdge2 = nullptr;
    else
      Succ->removePredecessor(OldTerm->getParent(),
                              /*DontDeleteUselessPHIs=*/true);
  }

  IRBuilder<> Builder(OldTerm);
  Builder.SetCurrentDebugLocation(OldTerm->getDebugLoc());

  // Insert an appropriate new terminator.
  if (!KeepEdge1 && !KeepEdge2) {
    if (TrueBB == FalseBB)
      // We were only looking for one successor, and it was present.
      // Create an unconditional branch to it.
      Builder.CreateBr(TrueBB);
    else {
      // We found both of the successors we were looking for.
      // Create a conditional branch sharing the condition of the select.
      BranchInst *NewBI = Builder.CreateCondBr(Cond, TrueBB, FalseBB);
      if (TrueWeight != FalseWeight)
        setBranchWeights(NewBI, TrueWeight, FalseWeight);
    }
  } else if (KeepEdge1 && (KeepEdge2 || TrueBB == FalseBB)) {
    // Neither of the selected blocks were successors, so this
    // terminator must be unreachable.
    new UnreachableInst(OldTerm->getContext(), OldTerm);
  } else {
    // One of the selected values was a successor, but the other wasn't.
    // Insert an unconditional branch to the one that was found;
    // the edge to the one that wasn't must be unreachable.
    if (!KeepEdge1)
      // Only TrueBB was found.
      Builder.CreateBr(TrueBB);
    else
      // Only FalseBB was found.
      Builder.CreateBr(FalseBB);
  }

  EraseTerminatorAndDCECond(OldTerm);
  return true;
}

// Replaces
//   (switch (select cond, X, Y)) on constant X, Y
// with a branch - conditional if X and Y lead to distinct BBs,
// unconditional otherwise.
static bool SimplifySwitchOnSelect(SwitchInst *SI, SelectInst *Select) {
  // Check for constant integer values in the select.
  ConstantInt *TrueVal = dyn_cast<ConstantInt>(Select->getTrueValue());
  ConstantInt *FalseVal = dyn_cast<ConstantInt>(Select->getFalseValue());
  if (!TrueVal || !FalseVal)
    return false;

  // Find the relevant condition and destinations.
  Value *Condition = Select->getCondition();
  BasicBlock *TrueBB = SI->findCaseValue(TrueVal)->getCaseSuccessor();
  BasicBlock *FalseBB = SI->findCaseValue(FalseVal)->getCaseSuccessor();

  // Get weight for TrueBB and FalseBB.
  uint32_t TrueWeight = 0, FalseWeight = 0;
  SmallVector<uint64_t, 8> Weights;
  bool HasWeights = HasBranchWeights(SI);
  if (HasWeights) {
    GetBranchWeights(SI, Weights);
    if (Weights.size() == 1 + SI->getNumCases()) {
      TrueWeight =
          (uint32_t)Weights[SI->findCaseValue(TrueVal)->getSuccessorIndex()];
      FalseWeight =
          (uint32_t)Weights[SI->findCaseValue(FalseVal)->getSuccessorIndex()];
    }
  }

  // Perform the actual simplification.
  return SimplifyTerminatorOnSelect(SI, Condition, TrueBB, FalseBB, TrueWeight,
                                    FalseWeight);
}

// Replaces
//   (indirectbr (select cond, blockaddress(@fn, BlockA),
//                             blockaddress(@fn, BlockB)))
// with
//   (br cond, BlockA, BlockB).
static bool SimplifyIndirectBrOnSelect(IndirectBrInst *IBI, SelectInst *SI) {
  // Check that both operands of the select are block addresses.
  BlockAddress *TBA = dyn_cast<BlockAddress>(SI->getTrueValue());
  BlockAddress *FBA = dyn_cast<BlockAddress>(SI->getFalseValue());
  if (!TBA || !FBA)
    return false;

  // Extract the actual blocks.
  BasicBlock *TrueBB = TBA->getBasicBlock();
  BasicBlock *FalseBB = FBA->getBasicBlock();

  // Perform the actual simplification.
  return SimplifyTerminatorOnSelect(IBI, SI->getCondition(), TrueBB, FalseBB, 0,
                                    0);
}

/// This is called when we find an icmp instruction
/// (a seteq/setne with a constant) as the only instruction in a
/// block that ends with an uncond branch.  We are looking for a very specific
/// pattern that occurs when "A == 1 || A == 2 || A == 3" gets simplified.  In
/// this case, we merge the first two "or's of icmp" into a switch, but then the
/// default value goes to an uncond block with a seteq in it, we get something
/// like:
///
///   switch i8 %A, label %DEFAULT [ i8 1, label %end    i8 2, label %end ]
/// DEFAULT:
///   %tmp = icmp eq i8 %A, 92
///   br label %end
/// end:
///   ... = phi i1 [ true, %entry ], [ %tmp, %DEFAULT ], [ true, %entry ]
///
/// We prefer to split the edge to 'end' so that there is a true/false entry to
/// the PHI, merging the third icmp into the switch.
bool SimplifyCFGOpt::tryToSimplifyUncondBranchWithICmpInIt(
    ICmpInst *ICI, IRBuilder<> &Builder) {
  BasicBlock *BB = ICI->getParent();

  // If the block has any PHIs in it or the icmp has multiple uses, it is too
  // complex.
  if (isa<PHINode>(BB->begin()) || !ICI->hasOneUse())
    return false;

  Value *V = ICI->getOperand(0);
  ConstantInt *Cst = cast<ConstantInt>(ICI->getOperand(1));

  // The pattern we're looking for is where our only predecessor is a switch on
  // 'V' and this block is the default case for the switch.  In this case we can
  // fold the compared value into the switch to simplify things.
  BasicBlock *Pred = BB->getSinglePredecessor();
  if (!Pred || !isa<SwitchInst>(Pred->getTerminator()))
    return false;

  SwitchInst *SI = cast<SwitchInst>(Pred->getTerminator());
  if (SI->getCondition() != V)
    return false;

  // If BB is reachable on a non-default case, then we simply know the value of
  // V in this block.  Substitute it and constant fold the icmp instruction
  // away.
  if (SI->getDefaultDest() != BB) {
    ConstantInt *VVal = SI->findCaseDest(BB);
    assert(VVal && "Should have a unique destination value");
    ICI->setOperand(0, VVal);

    if (Value *V = SimplifyInstruction(ICI, {DL, ICI})) {
      ICI->replaceAllUsesWith(V);
      ICI->eraseFromParent();
    }
    // BB is now empty, so it is likely to simplify away.
    return requestResimplify();
  }

  // Ok, the block is reachable from the default dest.  If the constant we're
  // comparing exists in one of the other edges, then we can constant fold ICI
  // and zap it.
  if (SI->findCaseValue(Cst) != SI->case_default()) {
    Value *V;
    if (ICI->getPredicate() == ICmpInst::ICMP_EQ)
      V = ConstantInt::getFalse(BB->getContext());
    else
      V = ConstantInt::getTrue(BB->getContext());

    ICI->replaceAllUsesWith(V);
    ICI->eraseFromParent();
    // BB is now empty, so it is likely to simplify away.
    return requestResimplify();
  }

  // The use of the icmp has to be in the 'end' block, by the only PHI node in
  // the block.
  BasicBlock *SuccBlock = BB->getTerminator()->getSuccessor(0);
  PHINode *PHIUse = dyn_cast<PHINode>(ICI->user_back());
  if (PHIUse == nullptr || PHIUse != &SuccBlock->front() ||
      isa<PHINode>(++BasicBlock::iterator(PHIUse)))
    return false;

  // If the icmp is a SETEQ, then the default dest gets false, the new edge gets
  // true in the PHI.
  Constant *DefaultCst = ConstantInt::getTrue(BB->getContext());
  Constant *NewCst = ConstantInt::getFalse(BB->getContext());

  if (ICI->getPredicate() == ICmpInst::ICMP_EQ)
    std::swap(DefaultCst, NewCst);

  // Replace ICI (which is used by the PHI for the default value) with true or
  // false depending on if it is EQ or NE.
  ICI->replaceAllUsesWith(DefaultCst);
  ICI->eraseFromParent();

  // Okay, the switch goes to this block on a default value.  Add an edge from
  // the switch to the merge point on the compared value.
  BasicBlock *NewBB =
      BasicBlock::Create(BB->getContext(), "switch.edge", BB->getParent(), BB);
  SmallVector<uint64_t, 8> Weights;
  bool HasWeights = HasBranchWeights(SI);
  if (HasWeights) {
    GetBranchWeights(SI, Weights);
    if (Weights.size() == 1 + SI->getNumCases()) {
      // Split weight for default case to case for "Cst".
      Weights[0] = (Weights[0] + 1) >> 1;
      Weights.push_back(Weights[0]);

      SmallVector<uint32_t, 8> MDWeights(Weights.begin(), Weights.end());
      setBranchWeights(SI, MDWeights);
    }
  }
  SI->addCase(Cst, NewBB);

  // NewBB branches to the phi block, add the uncond branch and the phi entry.
  Builder.SetInsertPoint(NewBB);
  Builder.SetCurrentDebugLocation(SI->getDebugLoc());
  Builder.CreateBr(SuccBlock);
  PHIUse->addIncoming(NewCst, NewBB);
  return true;
}

/// The specified branch is a conditional branch.
/// Check to see if it is branching on an or/and chain of icmp instructions, and
/// fold it into a switch instruction if so.
static bool SimplifyBranchOnICmpChain(BranchInst *BI, IRBuilder<> &Builder,
                                      const DataLayout &DL) {
  Instruction *Cond = dyn_cast<Instruction>(BI->getCondition());
  if (!Cond)
    return false;

  // Change br (X == 0 | X == 1), T, F into a switch instruction.
  // If this is a bunch of seteq's or'd together, or if it's a bunch of
  // 'setne's and'ed together, collect them.

  // Try to gather values from a chain of and/or to be turned into a switch
  ConstantComparesGatherer ConstantCompare(Cond, DL);
  // Unpack the result
  SmallVectorImpl<ConstantInt *> &Values = ConstantCompare.Vals;
  Value *CompVal = ConstantCompare.CompValue;
  unsigned UsedICmps = ConstantCompare.UsedICmps;
  Value *ExtraCase = ConstantCompare.Extra;

  // If we didn't have a multiply compared value, fail.
  if (!CompVal)
    return false;

  // Avoid turning single icmps into a switch.
  if (UsedICmps <= 1)
    return false;

  bool TrueWhenEqual = (Cond->getOpcode() == Instruction::Or);

  // There might be duplicate constants in the list, which the switch
  // instruction can't handle, remove them now.
  array_pod_sort(Values.begin(), Values.end(), ConstantIntSortPredicate);
  Values.erase(std::unique(Values.begin(), Values.end()), Values.end());

  // If Extra was used, we require at least two switch values to do the
  // transformation.  A switch with one value is just a conditional branch.
  if (ExtraCase && Values.size() < 2)
    return false;

  // TODO: Preserve branch weight metadata, similarly to how
  // FoldValueComparisonIntoPredecessors preserves it.

  // Figure out which block is which destination.
  BasicBlock *DefaultBB = BI->getSuccessor(1);
  BasicBlock *EdgeBB = BI->getSuccessor(0);
  if (!TrueWhenEqual)
    std::swap(DefaultBB, EdgeBB);

  BasicBlock *BB = BI->getParent();

  LLVM_DEBUG(dbgs() << "Converting 'icmp' chain with " << Values.size()
                    << " cases into SWITCH.  BB is:\n"
                    << *BB);

  // If there are any extra values that couldn't be folded into the switch
  // then we evaluate them with an explicit branch first.  Split the block
  // right before the condbr to handle it.
  if (ExtraCase) {
    BasicBlock *NewBB =
        BB->splitBasicBlock(BI->getIterator(), "switch.early.test");
    // Remove the uncond branch added to the old block.
    Instruction *OldTI = BB->getTerminator();
    Builder.SetInsertPoint(OldTI);

    if (TrueWhenEqual)
      Builder.CreateCondBr(ExtraCase, EdgeBB, NewBB);
    else
      Builder.CreateCondBr(ExtraCase, NewBB, EdgeBB);

    OldTI->eraseFromParent();

    // If there are PHI nodes in EdgeBB, then we need to add a new entry to them
    // for the edge we just added.
    AddPredecessorToBlock(EdgeBB, BB, NewBB);

    LLVM_DEBUG(dbgs() << "  ** 'icmp' chain unhandled condition: " << *ExtraCase
                      << "\nEXTRABB = " << *BB);
    BB = NewBB;
  }

  Builder.SetInsertPoint(BI);
  // Convert pointer to int before we switch.
  if (CompVal->getType()->isPointerTy()) {
    CompVal = Builder.CreatePtrToInt(
        CompVal, DL.getIntPtrType(CompVal->getType()), "magicptr");
  }

  // Create the new switch instruction now.
  SwitchInst *New = Builder.CreateSwitch(CompVal, DefaultBB, Values.size());

  // Add all of the 'cases' to the switch instruction.
  for (unsigned i = 0, e = Values.size(); i != e; ++i)
    New->addCase(Values[i], EdgeBB);

  // We added edges from PI to the EdgeBB.  As such, if there were any
  // PHI nodes in EdgeBB, they need entries to be added corresponding to
  // the number of edges added.
  for (BasicBlock::iterator BBI = EdgeBB->begin(); isa<PHINode>(BBI); ++BBI) {
    PHINode *PN = cast<PHINode>(BBI);
    Value *InVal = PN->getIncomingValueForBlock(BB);
    for (unsigned i = 0, e = Values.size() - 1; i != e; ++i)
      PN->addIncoming(InVal, BB);
  }

  // Erase the old branch instruction.
  EraseTerminatorAndDCECond(BI);

  LLVM_DEBUG(dbgs() << "  ** 'icmp' chain result is:\n" << *BB << '\n');
  return true;
}

bool SimplifyCFGOpt::SimplifyResume(ResumeInst *RI, IRBuilder<> &Builder) {
  if (isa<PHINode>(RI->getValue()))
    return SimplifyCommonResume(RI);
  else if (isa<LandingPadInst>(RI->getParent()->getFirstNonPHI()) &&
           RI->getValue() == RI->getParent()->getFirstNonPHI())
    // The resume must unwind the exception that caused control to branch here.
    return SimplifySingleResume(RI);

  return false;
}

// Simplify resume that is shared by several landing pads (phi of landing pad).
bool SimplifyCFGOpt::SimplifyCommonResume(ResumeInst *RI) {
  BasicBlock *BB = RI->getParent();

  // Check that there are no other instructions except for debug intrinsics
  // between the phi of landing pads (RI->getValue()) and resume instruction.
  BasicBlock::iterator I = cast<Instruction>(RI->getValue())->getIterator(),
                       E = RI->getIterator();
  while (++I != E)
    if (!isa<DbgInfoIntrinsic>(I))
      return false;

  SmallSetVector<BasicBlock *, 4> TrivialUnwindBlocks;
  auto *PhiLPInst = cast<PHINode>(RI->getValue());

  // Check incoming blocks to see if any of them are trivial.
  for (unsigned Idx = 0, End = PhiLPInst->getNumIncomingValues(); Idx != End;
       Idx++) {
    auto *IncomingBB = PhiLPInst->getIncomingBlock(Idx);
    auto *IncomingValue = PhiLPInst->getIncomingValue(Idx);

    // If the block has other successors, we can not delete it because
    // it has other dependents.
    if (IncomingBB->getUniqueSuccessor() != BB)
      continue;

    auto *LandingPad = dyn_cast<LandingPadInst>(IncomingBB->getFirstNonPHI());
    // Not the landing pad that caused the control to branch here.
    if (IncomingValue != LandingPad)
      continue;

    bool isTrivial = true;

    I = IncomingBB->getFirstNonPHI()->getIterator();
    E = IncomingBB->getTerminator()->getIterator();
    while (++I != E)
      if (!isa<DbgInfoIntrinsic>(I)) {
        isTrivial = false;
        break;
      }

    if (isTrivial)
      TrivialUnwindBlocks.insert(IncomingBB);
  }

  // If no trivial unwind blocks, don't do any simplifications.
  if (TrivialUnwindBlocks.empty())
    return false;

  // Turn all invokes that unwind here into calls.
  for (auto *TrivialBB : TrivialUnwindBlocks) {
    // Blocks that will be simplified should be removed from the phi node.
    // Note there could be multiple edges to the resume block, and we need
    // to remove them all.
    while (PhiLPInst->getBasicBlockIndex(TrivialBB) != -1)
      BB->removePredecessor(TrivialBB, true);

    for (pred_iterator PI = pred_begin(TrivialBB), PE = pred_end(TrivialBB);
         PI != PE;) {
      BasicBlock *Pred = *PI++;
      removeUnwindEdge(Pred);
    }

    // In each SimplifyCFG run, only the current processed block can be erased.
    // Otherwise, it will break the iteration of SimplifyCFG pass. So instead
    // of erasing TrivialBB, we only remove the branch to the common resume
    // block so that we can later erase the resume block since it has no
    // predecessors.
    TrivialBB->getTerminator()->eraseFromParent();
    new UnreachableInst(RI->getContext(), TrivialBB);
  }

  // Delete the resume block if all its predecessors have been removed.
  if (pred_empty(BB))
    BB->eraseFromParent();

  return !TrivialUnwindBlocks.empty();
}

// Simplify resume that is only used by a single (non-phi) landing pad.
bool SimplifyCFGOpt::SimplifySingleResume(ResumeInst *RI) {
  BasicBlock *BB = RI->getParent();
  LandingPadInst *LPInst = dyn_cast<LandingPadInst>(BB->getFirstNonPHI());
  assert(RI->getValue() == LPInst &&
         "Resume must unwind the exception that caused control to here");

  // Check that there are no other instructions except for debug intrinsics.
  BasicBlock::iterator I = LPInst->getIterator(), E = RI->getIterator();
  while (++I != E)
    if (!isa<DbgInfoIntrinsic>(I))
      return false;

  // Turn all invokes that unwind here into calls and delete the basic block.
  for (pred_iterator PI = pred_begin(BB), PE = pred_end(BB); PI != PE;) {
    BasicBlock *Pred = *PI++;
    removeUnwindEdge(Pred);
  }

  // The landingpad is now unreachable.  Zap it.
  if (LoopHeaders)
    LoopHeaders->erase(BB);
  BB->eraseFromParent();
  return true;
}

static bool removeEmptyCleanup(CleanupReturnInst *RI) {
  // If this is a trivial cleanup pad that executes no instructions, it can be
  // eliminated.  If the cleanup pad continues to the caller, any predecessor
  // that is an EH pad will be updated to continue to the caller and any
  // predecessor that terminates with an invoke instruction will have its invoke
  // instruction converted to a call instruction.  If the cleanup pad being
  // simplified does not continue to the caller, each predecessor will be
  // updated to continue to the unwind destination of the cleanup pad being
  // simplified.
  BasicBlock *BB = RI->getParent();
  CleanupPadInst *CPInst = RI->getCleanupPad();
  if (CPInst->getParent() != BB)
    // This isn't an empty cleanup.
    return false;

  // We cannot kill the pad if it has multiple uses.  This typically arises
  // from unreachable basic blocks.
  if (!CPInst->hasOneUse())
    return false;

  // Check that there are no other instructions except for benign intrinsics.
  BasicBlock::iterator I = CPInst->getIterator(), E = RI->getIterator();
  while (++I != E) {
    auto *II = dyn_cast<IntrinsicInst>(I);
    if (!II)
      return false;

    Intrinsic::ID IntrinsicID = II->getIntrinsicID();
    switch (IntrinsicID) {
    case Intrinsic::dbg_declare:
    case Intrinsic::dbg_value:
    case Intrinsic::dbg_label:
    case Intrinsic::lifetime_end:
      break;
    default:
      return false;
    }
  }

  // If the cleanup return we are simplifying unwinds to the caller, this will
  // set UnwindDest to nullptr.
  BasicBlock *UnwindDest = RI->getUnwindDest();
  Instruction *DestEHPad = UnwindDest ? UnwindDest->getFirstNonPHI() : nullptr;

  // We're about to remove BB from the control flow.  Before we do, sink any
  // PHINodes into the unwind destination.  Doing this before changing the
  // control flow avoids some potentially slow checks, since we can currently
  // be certain that UnwindDest and BB have no common predecessors (since they
  // are both EH pads).
  if (UnwindDest) {
    // First, go through the PHI nodes in UnwindDest and update any nodes that
    // reference the block we are removing
    for (BasicBlock::iterator I = UnwindDest->begin(),
                              IE = DestEHPad->getIterator();
         I != IE; ++I) {
      PHINode *DestPN = cast<PHINode>(I);

      int Idx = DestPN->getBasicBlockIndex(BB);
      // Since BB unwinds to UnwindDest, it has to be in the PHI node.
      assert(Idx != -1);
      // This PHI node has an incoming value that corresponds to a control
      // path through the cleanup pad we are removing.  If the incoming
      // value is in the cleanup pad, it must be a PHINode (because we
      // verified above that the block is otherwise empty).  Otherwise, the
      // value is either a constant or a value that dominates the cleanup
      // pad being removed.
      //
      // Because BB and UnwindDest are both EH pads, all of their
      // predecessors must unwind to these blocks, and since no instruction
      // can have multiple unwind destinations, there will be no overlap in
      // incoming blocks between SrcPN and DestPN.
      Value *SrcVal = DestPN->getIncomingValue(Idx);
      PHINode *SrcPN = dyn_cast<PHINode>(SrcVal);

      // Remove the entry for the block we are deleting.
      DestPN->removeIncomingValue(Idx, false);

      if (SrcPN && SrcPN->getParent() == BB) {
        // If the incoming value was a PHI node in the cleanup pad we are
        // removing, we need to merge that PHI node's incoming values into
        // DestPN.
        for (unsigned SrcIdx = 0, SrcE = SrcPN->getNumIncomingValues();
             SrcIdx != SrcE; ++SrcIdx) {
          DestPN->addIncoming(SrcPN->getIncomingValue(SrcIdx),
                              SrcPN->getIncomingBlock(SrcIdx));
        }
      } else {
        // Otherwise, the incoming value came from above BB and
        // so we can just reuse it.  We must associate all of BB's
        // predecessors with this value.
        for (auto *pred : predecessors(BB)) {
          DestPN->addIncoming(SrcVal, pred);
        }
      }
    }

    // Sink any remaining PHI nodes directly into UnwindDest.
    Instruction *InsertPt = DestEHPad;
    for (BasicBlock::iterator I = BB->begin(),
                              IE = BB->getFirstNonPHI()->getIterator();
         I != IE;) {
      // The iterator must be incremented here because the instructions are
      // being moved to another block.
      PHINode *PN = cast<PHINode>(I++);
      if (PN->use_empty())
        // If the PHI node has no uses, just leave it.  It will be erased
        // when we erase BB below.
        continue;

      // Otherwise, sink this PHI node into UnwindDest.
      // Any predecessors to UnwindDest which are not already represented
      // must be back edges which inherit the value from the path through
      // BB.  In this case, the PHI value must reference itself.
      for (auto *pred : predecessors(UnwindDest))
        if (pred != BB)
          PN->addIncoming(PN, pred);
      PN->moveBefore(InsertPt);
    }
  }

  for (pred_iterator PI = pred_begin(BB), PE = pred_end(BB); PI != PE;) {
    // The iterator must be updated here because we are removing this pred.
    BasicBlock *PredBB = *PI++;
    if (UnwindDest == nullptr) {
      removeUnwindEdge(PredBB);
    } else {
      Instruction *TI = PredBB->getTerminator();
      TI->replaceUsesOfWith(BB, UnwindDest);
    }
  }

  // The cleanup pad is now unreachable.  Zap it.
  BB->eraseFromParent();
  return true;
}

// Try to merge two cleanuppads together.
static bool mergeCleanupPad(CleanupReturnInst *RI) {
  // Skip any cleanuprets which unwind to caller, there is nothing to merge
  // with.
  BasicBlock *UnwindDest = RI->getUnwindDest();
  if (!UnwindDest)
    return false;

  // This cleanupret isn't the only predecessor of this cleanuppad, it wouldn't
  // be safe to merge without code duplication.
  if (UnwindDest->getSinglePredecessor() != RI->getParent())
    return false;

  // Verify that our cleanuppad's unwind destination is another cleanuppad.
  auto *SuccessorCleanupPad = dyn_cast<CleanupPadInst>(&UnwindDest->front());
  if (!SuccessorCleanupPad)
    return false;

  CleanupPadInst *PredecessorCleanupPad = RI->getCleanupPad();
  // Replace any uses of the successor cleanupad with the predecessor pad
  // The only cleanuppad uses should be this cleanupret, it's cleanupret and
  // funclet bundle operands.
  SuccessorCleanupPad->replaceAllUsesWith(PredecessorCleanupPad);
  // Remove the old cleanuppad.
  SuccessorCleanupPad->eraseFromParent();
  // Now, we simply replace the cleanupret with a branch to the unwind
  // destination.
  BranchInst::Create(UnwindDest, RI->getParent());
  RI->eraseFromParent();

  return true;
}

bool SimplifyCFGOpt::SimplifyCleanupReturn(CleanupReturnInst *RI) {
  // It is possible to transiantly have an undef cleanuppad operand because we
  // have deleted some, but not all, dead blocks.
  // Eventually, this block will be deleted.
  if (isa<UndefValue>(RI->getOperand(0)))
    return false;

  if (mergeCleanupPad(RI))
    return true;

  if (removeEmptyCleanup(RI))
    return true;

  return false;
}

bool SimplifyCFGOpt::SimplifyReturn(ReturnInst *RI, IRBuilder<> &Builder) {
  BasicBlock *BB = RI->getParent();
  if (!BB->getFirstNonPHIOrDbg()->isTerminator())
    return false;

  // Find predecessors that end with branches.
  SmallVector<BasicBlock *, 8> UncondBranchPreds;
  SmallVector<BranchInst *, 8> CondBranchPreds;
  for (pred_iterator PI = pred_begin(BB), E = pred_end(BB); PI != E; ++PI) {
    BasicBlock *P = *PI;
    Instruction *PTI = P->getTerminator();
    if (BranchInst *BI = dyn_cast<BranchInst>(PTI)) {
      if (BI->isUnconditional())
        UncondBranchPreds.push_back(P);
      else
        CondBranchPreds.push_back(BI);
    }
  }

  // If we found some, do the transformation!
  if (!UncondBranchPreds.empty() && DupRet) {
    while (!UncondBranchPreds.empty()) {
      BasicBlock *Pred = UncondBranchPreds.pop_back_val();
      LLVM_DEBUG(dbgs() << "FOLDING: " << *BB
                        << "INTO UNCOND BRANCH PRED: " << *Pred);
      (void)FoldReturnIntoUncondBranch(RI, BB, Pred);
    }

    // If we eliminated all predecessors of the block, delete the block now.
    if (pred_empty(BB)) {
      // We know there are no successors, so just nuke the block.
      if (LoopHeaders)
        LoopHeaders->erase(BB);
      BB->eraseFromParent();
    }

    return true;
  }

  // Check out all of the conditional branches going to this return
  // instruction.  If any of them just select between returns, change the
  // branch itself into a select/return pair.
  while (!CondBranchPreds.empty()) {
    BranchInst *BI = CondBranchPreds.pop_back_val();

    // Check to see if the non-BB successor is also a return block.
    if (isa<ReturnInst>(BI->getSuccessor(0)->getTerminator()) &&
        isa<ReturnInst>(BI->getSuccessor(1)->getTerminator()) &&
        SimplifyCondBranchToTwoReturns(BI, Builder))
      return true;
  }
  return false;
}

bool SimplifyCFGOpt::SimplifyUnreachable(UnreachableInst *UI) {
  BasicBlock *BB = UI->getParent();

  bool Changed = false;

  // If there are any instructions immediately before the unreachable that can
  // be removed, do so.
  while (UI->getIterator() != BB->begin()) {
    BasicBlock::iterator BBI = UI->getIterator();
    --BBI;
    // Do not delete instructions that can have side effects which might cause
    // the unreachable to not be reachable; specifically, calls and volatile
    // operations may have this effect.
    if (isa<CallInst>(BBI) && !isa<DbgInfoIntrinsic>(BBI))
      break;

    if (BBI->mayHaveSideEffects()) {
      if (auto *SI = dyn_cast<StoreInst>(BBI)) {
        if (SI->isVolatile())
          break;
      } else if (auto *LI = dyn_cast<LoadInst>(BBI)) {
        if (LI->isVolatile())
          break;
      } else if (auto *RMWI = dyn_cast<AtomicRMWInst>(BBI)) {
        if (RMWI->isVolatile())
          break;
      } else if (auto *CXI = dyn_cast<AtomicCmpXchgInst>(BBI)) {
        if (CXI->isVolatile())
          break;
      } else if (isa<CatchPadInst>(BBI)) {
        // A catchpad may invoke exception object constructors and such, which
        // in some languages can be arbitrary code, so be conservative by
        // default.
        // For CoreCLR, it just involves a type test, so can be removed.
        if (classifyEHPersonality(BB->getParent()->getPersonalityFn()) !=
            EHPersonality::CoreCLR)
          break;
      } else if (!isa<FenceInst>(BBI) && !isa<VAArgInst>(BBI) &&
                 !isa<LandingPadInst>(BBI)) {
        break;
      }
      // Note that deleting LandingPad's here is in fact okay, although it
      // involves a bit of subtle reasoning. If this inst is a LandingPad,
      // all the predecessors of this block will be the unwind edges of Invokes,
      // and we can therefore guarantee this block will be erased.
    }

    // Delete this instruction (any uses are guaranteed to be dead)
    if (!BBI->use_empty())
      BBI->replaceAllUsesWith(UndefValue::get(BBI->getType()));
    BBI->eraseFromParent();
    Changed = true;
  }

  // If the unreachable instruction is the first in the block, take a gander
  // at all of the predecessors of this instruction, and simplify them.
  if (&BB->front() != UI)
    return Changed;

  SmallVector<BasicBlock *, 8> Preds(pred_begin(BB), pred_end(BB));
  for (unsigned i = 0, e = Preds.size(); i != e; ++i) {
    Instruction *TI = Preds[i]->getTerminator();
    IRBuilder<> Builder(TI);
    if (auto *BI = dyn_cast<BranchInst>(TI)) {
      if (BI->isUnconditional()) {
        if (BI->getSuccessor(0) == BB) {
          new UnreachableInst(TI->getContext(), TI);
          TI->eraseFromParent();
          Changed = true;
        }
      } else {
        if (BI->getSuccessor(0) == BB) {
          Builder.CreateBr(BI->getSuccessor(1));
          EraseTerminatorAndDCECond(BI);
        } else if (BI->getSuccessor(1) == BB) {
          Builder.CreateBr(BI->getSuccessor(0));
          EraseTerminatorAndDCECond(BI);
          Changed = true;
        }
      }
    } else if (auto *SI = dyn_cast<SwitchInst>(TI)) {
      for (auto i = SI->case_begin(), e = SI->case_end(); i != e;) {
        if (i->getCaseSuccessor() != BB) {
          ++i;
          continue;
        }
        BB->removePredecessor(SI->getParent());
        i = SI->removeCase(i);
        e = SI->case_end();
        Changed = true;
      }
    } else if (auto *II = dyn_cast<InvokeInst>(TI)) {
      if (II->getUnwindDest() == BB) {
        removeUnwindEdge(TI->getParent());
        Changed = true;
      }
    } else if (auto *CSI = dyn_cast<CatchSwitchInst>(TI)) {
      if (CSI->getUnwindDest() == BB) {
        removeUnwindEdge(TI->getParent());
        Changed = true;
        continue;
      }

      for (CatchSwitchInst::handler_iterator I = CSI->handler_begin(),
                                             E = CSI->handler_end();
           I != E; ++I) {
        if (*I == BB) {
          CSI->removeHandler(I);
          --I;
          --E;
          Changed = true;
        }
      }
      if (CSI->getNumHandlers() == 0) {
        BasicBlock *CatchSwitchBB = CSI->getParent();
        if (CSI->hasUnwindDest()) {
          // Redirect preds to the unwind dest
          CatchSwitchBB->replaceAllUsesWith(CSI->getUnwindDest());
        } else {
          // Rewrite all preds to unwind to caller (or from invoke to call).
          SmallVector<BasicBlock *, 8> EHPreds(predecessors(CatchSwitchBB));
          for (BasicBlock *EHPred : EHPreds)
            removeUnwindEdge(EHPred);
        }
        // The catchswitch is no longer reachable.
        new UnreachableInst(CSI->getContext(), CSI);
        CSI->eraseFromParent();
        Changed = true;
      }
    } else if (isa<CleanupReturnInst>(TI)) {
      new UnreachableInst(TI->getContext(), TI);
      TI->eraseFromParent();
      Changed = true;
    }
  }

  // If this block is now dead, remove it.
  if (pred_empty(BB) && BB != &BB->getParent()->getEntryBlock()) {
    // We know there are no successors, so just nuke the block.
    if (LoopHeaders)
      LoopHeaders->erase(BB);
    BB->eraseFromParent();
    return true;
  }

  return Changed;
}

static bool CasesAreContiguous(SmallVectorImpl<ConstantInt *> &Cases) {
  assert(Cases.size() >= 1);

  array_pod_sort(Cases.begin(), Cases.end(), ConstantIntSortPredicate);
  for (size_t I = 1, E = Cases.size(); I != E; ++I) {
    if (Cases[I - 1]->getValue() != Cases[I]->getValue() + 1)
      return false;
  }
  return true;
}

/// Turn a switch with two reachable destinations into an integer range
/// comparison and branch.
static bool TurnSwitchRangeIntoICmp(SwitchInst *SI, IRBuilder<> &Builder) {
  assert(SI->getNumCases() > 1 && "Degenerate switch?");

  bool HasDefault =
      !isa<UnreachableInst>(SI->getDefaultDest()->getFirstNonPHIOrDbg());

  // Partition the cases into two sets with different destinations.
  BasicBlock *DestA = HasDefault ? SI->getDefaultDest() : nullptr;
  BasicBlock *DestB = nullptr;
  SmallVector<ConstantInt *, 16> CasesA;
  SmallVector<ConstantInt *, 16> CasesB;

  for (auto Case : SI->cases()) {
    BasicBlock *Dest = Case.getCaseSuccessor();
    if (!DestA)
      DestA = Dest;
    if (Dest == DestA) {
      CasesA.push_back(Case.getCaseValue());
      continue;
    }
    if (!DestB)
      DestB = Dest;
    if (Dest == DestB) {
      CasesB.push_back(Case.getCaseValue());
      continue;
    }
    return false; // More than two destinations.
  }

  assert(DestA && DestB &&
         "Single-destination switch should have been folded.");
  assert(DestA != DestB);
  assert(DestB != SI->getDefaultDest());
  assert(!CasesB.empty() && "There must be non-default cases.");
  assert(!CasesA.empty() || HasDefault);

  // Figure out if one of the sets of cases form a contiguous range.
  SmallVectorImpl<ConstantInt *> *ContiguousCases = nullptr;
  BasicBlock *ContiguousDest = nullptr;
  BasicBlock *OtherDest = nullptr;
  if (!CasesA.empty() && CasesAreContiguous(CasesA)) {
    ContiguousCases = &CasesA;
    ContiguousDest = DestA;
    OtherDest = DestB;
  } else if (CasesAreContiguous(CasesB)) {
    ContiguousCases = &CasesB;
    ContiguousDest = DestB;
    OtherDest = DestA;
  } else
    return false;

  // Start building the compare and branch.

  Constant *Offset = ConstantExpr::getNeg(ContiguousCases->back());
  Constant *NumCases =
      ConstantInt::get(Offset->getType(), ContiguousCases->size());

  Value *Sub = SI->getCondition();
  if (!Offset->isNullValue())
    Sub = Builder.CreateAdd(Sub, Offset, Sub->getName() + ".off");

  Value *Cmp;
  // If NumCases overflowed, then all possible values jump to the successor.
  if (NumCases->isNullValue() && !ContiguousCases->empty())
    Cmp = ConstantInt::getTrue(SI->getContext());
  else
    Cmp = Builder.CreateICmpULT(Sub, NumCases, "switch");
  BranchInst *NewBI = Builder.CreateCondBr(Cmp, ContiguousDest, OtherDest);

  // Update weight for the newly-created conditional branch.
  if (HasBranchWeights(SI)) {
    SmallVector<uint64_t, 8> Weights;
    GetBranchWeights(SI, Weights);
    if (Weights.size() == 1 + SI->getNumCases()) {
      uint64_t TrueWeight = 0;
      uint64_t FalseWeight = 0;
      for (size_t I = 0, E = Weights.size(); I != E; ++I) {
        if (SI->getSuccessor(I) == ContiguousDest)
          TrueWeight += Weights[I];
        else
          FalseWeight += Weights[I];
      }
      while (TrueWeight > UINT32_MAX || FalseWeight > UINT32_MAX) {
        TrueWeight /= 2;
        FalseWeight /= 2;
      }
      setBranchWeights(NewBI, TrueWeight, FalseWeight);
    }
  }

  // Prune obsolete incoming values off the successors' PHI nodes.
  for (auto BBI = ContiguousDest->begin(); isa<PHINode>(BBI); ++BBI) {
    unsigned PreviousEdges = ContiguousCases->size();
    if (ContiguousDest == SI->getDefaultDest())
      ++PreviousEdges;
    for (unsigned I = 0, E = PreviousEdges - 1; I != E; ++I)
      cast<PHINode>(BBI)->removeIncomingValue(SI->getParent());
  }
  for (auto BBI = OtherDest->begin(); isa<PHINode>(BBI); ++BBI) {
    unsigned PreviousEdges = SI->getNumCases() - ContiguousCases->size();
    if (OtherDest == SI->getDefaultDest())
      ++PreviousEdges;
    for (unsigned I = 0, E = PreviousEdges - 1; I != E; ++I)
      cast<PHINode>(BBI)->removeIncomingValue(SI->getParent());
  }

  // Drop the switch.
  SI->eraseFromParent();

  return true;
}

/// Compute masked bits for the condition of a switch
/// and use it to remove dead cases.
static bool eliminateDeadSwitchCases(SwitchInst *SI, AssumptionCache *AC,
                                     const DataLayout &DL) {
  Value *Cond = SI->getCondition();
  unsigned Bits = Cond->getType()->getIntegerBitWidth();
  KnownBits Known = computeKnownBits(Cond, DL, 0, AC, SI);

  // We can also eliminate cases by determining that their values are outside of
  // the limited range of the condition based on how many significant (non-sign)
  // bits are in the condition value.
  unsigned ExtraSignBits = ComputeNumSignBits(Cond, DL, 0, AC, SI) - 1;
  unsigned MaxSignificantBitsInCond = Bits - ExtraSignBits;

  // Gather dead cases.
  SmallVector<ConstantInt *, 8> DeadCases;
  for (auto &Case : SI->cases()) {
    const APInt &CaseVal = Case.getCaseValue()->getValue();
    if (Known.Zero.intersects(CaseVal) || !Known.One.isSubsetOf(CaseVal) ||
        (CaseVal.getMinSignedBits() > MaxSignificantBitsInCond)) {
      DeadCases.push_back(Case.getCaseValue());
      LLVM_DEBUG(dbgs() << "SimplifyCFG: switch case " << CaseVal
                        << " is dead.\n");
    }
  }

  // If we can prove that the cases must cover all possible values, the
  // default destination becomes dead and we can remove it.  If we know some
  // of the bits in the value, we can use that to more precisely compute the
  // number of possible unique case values.
  bool HasDefault =
      !isa<UnreachableInst>(SI->getDefaultDest()->getFirstNonPHIOrDbg());
  const unsigned NumUnknownBits =
      Bits - (Known.Zero | Known.One).countPopulation();
  assert(NumUnknownBits <= Bits);
  if (HasDefault && DeadCases.empty() &&
      NumUnknownBits < 64 /* avoid overflow */ &&
      SI->getNumCases() == (1ULL << NumUnknownBits)) {
    LLVM_DEBUG(dbgs() << "SimplifyCFG: switch default is dead.\n");
    BasicBlock *NewDefault =
        SplitBlockPredecessors(SI->getDefaultDest(), SI->getParent(), "");
    SI->setDefaultDest(&*NewDefault);
    SplitBlock(&*NewDefault, &NewDefault->front());
    auto *OldTI = NewDefault->getTerminator();
    new UnreachableInst(SI->getContext(), OldTI);
    EraseTerminatorAndDCECond(OldTI);
    return true;
  }

  SmallVector<uint64_t, 8> Weights;
  bool HasWeight = HasBranchWeights(SI);
  if (HasWeight) {
    GetBranchWeights(SI, Weights);
    HasWeight = (Weights.size() == 1 + SI->getNumCases());
  }

  // Remove dead cases from the switch.
  for (ConstantInt *DeadCase : DeadCases) {
    SwitchInst::CaseIt CaseI = SI->findCaseValue(DeadCase);
    assert(CaseI != SI->case_default() &&
           "Case was not found. Probably mistake in DeadCases forming.");
    if (HasWeight) {
      std::swap(Weights[CaseI->getCaseIndex() + 1], Weights.back());
      Weights.pop_back();
    }

    // Prune unused values from PHI nodes.
    CaseI->getCaseSuccessor()->removePredecessor(SI->getParent());
    SI->removeCase(CaseI);
  }
  if (HasWeight && Weights.size() >= 2) {
    SmallVector<uint32_t, 8> MDWeights(Weights.begin(), Weights.end());
    setBranchWeights(SI, MDWeights);
  }

  return !DeadCases.empty();
}

/// If BB would be eligible for simplification by
/// TryToSimplifyUncondBranchFromEmptyBlock (i.e. it is empty and terminated
/// by an unconditional branch), look at the phi node for BB in the successor
/// block and see if the incoming value is equal to CaseValue. If so, return
/// the phi node, and set PhiIndex to BB's index in the phi node.
static PHINode *FindPHIForConditionForwarding(ConstantInt *CaseValue,
                                              BasicBlock *BB, int *PhiIndex) {
  if (BB->getFirstNonPHIOrDbg() != BB->getTerminator())
    return nullptr; // BB must be empty to be a candidate for simplification.
  if (!BB->getSinglePredecessor())
    return nullptr; // BB must be dominated by the switch.

  BranchInst *Branch = dyn_cast<BranchInst>(BB->getTerminator());
  if (!Branch || !Branch->isUnconditional())
    return nullptr; // Terminator must be unconditional branch.

  BasicBlock *Succ = Branch->getSuccessor(0);

  for (PHINode &PHI : Succ->phis()) {
    int Idx = PHI.getBasicBlockIndex(BB);
    assert(Idx >= 0 && "PHI has no entry for predecessor?");

    Value *InValue = PHI.getIncomingValue(Idx);
    if (InValue != CaseValue)
      continue;

    *PhiIndex = Idx;
    return &PHI;
  }

  return nullptr;
}

/// Try to forward the condition of a switch instruction to a phi node
/// dominated by the switch, if that would mean that some of the destination
/// blocks of the switch can be folded away. Return true if a change is made.
static bool ForwardSwitchConditionToPHI(SwitchInst *SI) {
  using ForwardingNodesMap = DenseMap<PHINode *, SmallVector<int, 4>>;

  ForwardingNodesMap ForwardingNodes;
  BasicBlock *SwitchBlock = SI->getParent();
  bool Changed = false;
  for (auto &Case : SI->cases()) {
    ConstantInt *CaseValue = Case.getCaseValue();
    BasicBlock *CaseDest = Case.getCaseSuccessor();

    // Replace phi operands in successor blocks that are using the constant case
    // value rather than the switch condition variable:
    //   switchbb:
    //   switch i32 %x, label %default [
    //     i32 17, label %succ
    //   ...
    //   succ:
    //     %r = phi i32 ... [ 17, %switchbb ] ...
    // -->
    //     %r = phi i32 ... [ %x, %switchbb ] ...

    for (PHINode &Phi : CaseDest->phis()) {
      // This only works if there is exactly 1 incoming edge from the switch to
      // a phi. If there is >1, that means multiple cases of the switch map to 1
      // value in the phi, and that phi value is not the switch condition. Thus,
      // this transform would not make sense (the phi would be invalid because
      // a phi can't have different incoming values from the same block).
      int SwitchBBIdx = Phi.getBasicBlockIndex(SwitchBlock);
      if (Phi.getIncomingValue(SwitchBBIdx) == CaseValue &&
          count(Phi.blocks(), SwitchBlock) == 1) {
        Phi.setIncomingValue(SwitchBBIdx, SI->getCondition());
        Changed = true;
      }
    }

    // Collect phi nodes that are indirectly using this switch's case constants.
    int PhiIdx;
    if (auto *Phi = FindPHIForConditionForwarding(CaseValue, CaseDest, &PhiIdx))
      ForwardingNodes[Phi].push_back(PhiIdx);
  }

  for (auto &ForwardingNode : ForwardingNodes) {
    PHINode *Phi = ForwardingNode.first;
    SmallVectorImpl<int> &Indexes = ForwardingNode.second;
    if (Indexes.size() < 2)
      continue;

    for (int Index : Indexes)
      Phi->setIncomingValue(Index, SI->getCondition());
    Changed = true;
  }

  return Changed;
}

/// Return true if the backend will be able to handle
/// initializing an array of constants like C.
static bool ValidLookupTableConstant(Constant *C, const TargetTransformInfo &TTI) {
  if (C->isThreadDependent())
    return false;
  if (C->isDLLImportDependent())
    return false;

  if (!isa<ConstantFP>(C) && !isa<ConstantInt>(C) &&
      !isa<ConstantPointerNull>(C) && !isa<GlobalValue>(C) &&
      !isa<UndefValue>(C) && !isa<ConstantExpr>(C))
    return false;

  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(C)) {
    if (!CE->isGEPWithNoNotionalOverIndexing())
      return false;
    if (!ValidLookupTableConstant(CE->getOperand(0), TTI))
      return false;
  }

  if (!TTI.shouldBuildLookupTablesForConstant(C))
    return false;

  return true;
}

/// If V is a Constant, return it. Otherwise, try to look up
/// its constant value in ConstantPool, returning 0 if it's not there.
static Constant *
LookupConstant(Value *V,
               const SmallDenseMap<Value *, Constant *> &ConstantPool) {
  if (Constant *C = dyn_cast<Constant>(V))
    return C;
  return ConstantPool.lookup(V);
}

/// Try to fold instruction I into a constant. This works for
/// simple instructions such as binary operations where both operands are
/// constant or can be replaced by constants from the ConstantPool. Returns the
/// resulting constant on success, 0 otherwise.
static Constant *
ConstantFold(Instruction *I, const DataLayout &DL,
             const SmallDenseMap<Value *, Constant *> &ConstantPool) {
  if (SelectInst *Select = dyn_cast<SelectInst>(I)) {
    Constant *A = LookupConstant(Select->getCondition(), ConstantPool);
    if (!A)
      return nullptr;
    if (A->isAllOnesValue())
      return LookupConstant(Select->getTrueValue(), ConstantPool);
    if (A->isNullValue())
      return LookupConstant(Select->getFalseValue(), ConstantPool);
    return nullptr;
  }

  SmallVector<Constant *, 4> COps;
  for (unsigned N = 0, E = I->getNumOperands(); N != E; ++N) {
    if (Constant *A = LookupConstant(I->getOperand(N), ConstantPool))
      COps.push_back(A);
    else
      return nullptr;
  }

  if (CmpInst *Cmp = dyn_cast<CmpInst>(I)) {
    return ConstantFoldCompareInstOperands(Cmp->getPredicate(), COps[0],
                                           COps[1], DL);
  }

  return ConstantFoldInstOperands(I, COps, DL);
}

/// Try to determine the resulting constant values in phi nodes
/// at the common destination basic block, *CommonDest, for one of the case
/// destionations CaseDest corresponding to value CaseVal (0 for the default
/// case), of a switch instruction SI.
static bool
GetCaseResults(SwitchInst *SI, ConstantInt *CaseVal, BasicBlock *CaseDest,
               BasicBlock **CommonDest,
               SmallVectorImpl<std::pair<PHINode *, Constant *>> &Res,
               const DataLayout &DL, const TargetTransformInfo &TTI) {
  // The block from which we enter the common destination.
  BasicBlock *Pred = SI->getParent();

  // If CaseDest is empty except for some side-effect free instructions through
  // which we can constant-propagate the CaseVal, continue to its successor.
  SmallDenseMap<Value *, Constant *> ConstantPool;
  ConstantPool.insert(std::make_pair(SI->getCondition(), CaseVal));
  for (Instruction &I :CaseDest->instructionsWithoutDebug()) {
    if (I.isTerminator()) {
      // If the terminator is a simple branch, continue to the next block.
      if (I.getNumSuccessors() != 1 || I.isExceptionalTerminator())
        return false;
      Pred = CaseDest;
      CaseDest = I.getSuccessor(0);
    } else if (Constant *C = ConstantFold(&I, DL, ConstantPool)) {
      // Instruction is side-effect free and constant.

      // If the instruction has uses outside this block or a phi node slot for
      // the block, it is not safe to bypass the instruction since it would then
      // no longer dominate all its uses.
      for (auto &Use : I.uses()) {
        User *User = Use.getUser();
        if (Instruction *I = dyn_cast<Instruction>(User))
          if (I->getParent() == CaseDest)
            continue;
        if (PHINode *Phi = dyn_cast<PHINode>(User))
          if (Phi->getIncomingBlock(Use) == CaseDest)
            continue;
        return false;
      }

      ConstantPool.insert(std::make_pair(&I, C));
    } else {
      break;
    }
  }

  // If we did not have a CommonDest before, use the current one.
  if (!*CommonDest)
    *CommonDest = CaseDest;
  // If the destination isn't the common one, abort.
  if (CaseDest != *CommonDest)
    return false;

  // Get the values for this case from phi nodes in the destination block.
  for (PHINode &PHI : (*CommonDest)->phis()) {
    int Idx = PHI.getBasicBlockIndex(Pred);
    if (Idx == -1)
      continue;

    Constant *ConstVal =
        LookupConstant(PHI.getIncomingValue(Idx), ConstantPool);
    if (!ConstVal)
      return false;

    // Be conservative about which kinds of constants we support.
    if (!ValidLookupTableConstant(ConstVal, TTI))
      return false;

    Res.push_back(std::make_pair(&PHI, ConstVal));
  }

  return Res.size() > 0;
}

// Helper function used to add CaseVal to the list of cases that generate
// Result. Returns the updated number of cases that generate this result.
static uintptr_t MapCaseToResult(ConstantInt *CaseVal,
                                 SwitchCaseResultVectorTy &UniqueResults,
                                 Constant *Result) {
  for (auto &I : UniqueResults) {
    if (I.first == Result) {
      I.second.push_back(CaseVal);
      return I.second.size();
    }
  }
  UniqueResults.push_back(
      std::make_pair(Result, SmallVector<ConstantInt *, 4>(1, CaseVal)));
  return 1;
}

// Helper function that initializes a map containing
// results for the PHI node of the common destination block for a switch
// instruction. Returns false if multiple PHI nodes have been found or if
// there is not a common destination block for the switch.
static bool
InitializeUniqueCases(SwitchInst *SI, PHINode *&PHI, BasicBlock *&CommonDest,
                      SwitchCaseResultVectorTy &UniqueResults,
                      Constant *&DefaultResult, const DataLayout &DL,
                      const TargetTransformInfo &TTI,
                      uintptr_t MaxUniqueResults, uintptr_t MaxCasesPerResult) {
  for (auto &I : SI->cases()) {
    ConstantInt *CaseVal = I.getCaseValue();

    // Resulting value at phi nodes for this case value.
    SwitchCaseResultsTy Results;
    if (!GetCaseResults(SI, CaseVal, I.getCaseSuccessor(), &CommonDest, Results,
                        DL, TTI))
      return false;

    // Only one value per case is permitted.
    if (Results.size() > 1)
      return false;

    // Add the case->result mapping to UniqueResults.
    const uintptr_t NumCasesForResult =
        MapCaseToResult(CaseVal, UniqueResults, Results.begin()->second);

    // Early out if there are too many cases for this result.
    if (NumCasesForResult > MaxCasesPerResult)
      return false;

    // Early out if there are too many unique results.
    if (UniqueResults.size() > MaxUniqueResults)
      return false;

    // Check the PHI consistency.
    if (!PHI)
      PHI = Results[0].first;
    else if (PHI != Results[0].first)
      return false;
  }
  // Find the default result value.
  SmallVector<std::pair<PHINode *, Constant *>, 1> DefaultResults;
  BasicBlock *DefaultDest = SI->getDefaultDest();
  GetCaseResults(SI, nullptr, SI->getDefaultDest(), &CommonDest, DefaultResults,
                 DL, TTI);
  // If the default value is not found abort unless the default destination
  // is unreachable.
  DefaultResult =
      DefaultResults.size() == 1 ? DefaultResults.begin()->second : nullptr;
  if ((!DefaultResult &&
       !isa<UnreachableInst>(DefaultDest->getFirstNonPHIOrDbg())))
    return false;

  return true;
}

// Helper function that checks if it is possible to transform a switch with only
// two cases (or two cases + default) that produces a result into a select.
// Example:
// switch (a) {
//   case 10:                %0 = icmp eq i32 %a, 10
//     return 10;            %1 = select i1 %0, i32 10, i32 4
//   case 20:        ---->   %2 = icmp eq i32 %a, 20
//     return 2;             %3 = select i1 %2, i32 2, i32 %1
//   default:
//     return 4;
// }
static Value *ConvertTwoCaseSwitch(const SwitchCaseResultVectorTy &ResultVector,
                                   Constant *DefaultResult, Value *Condition,
                                   IRBuilder<> &Builder) {
  assert(ResultVector.size() == 2 &&
         "We should have exactly two unique results at this point");
  // If we are selecting between only two cases transform into a simple
  // select or a two-way select if default is possible.
  if (ResultVector[0].second.size() == 1 &&
      ResultVector[1].second.size() == 1) {
    ConstantInt *const FirstCase = ResultVector[0].second[0];
    ConstantInt *const SecondCase = ResultVector[1].second[0];

    bool DefaultCanTrigger = DefaultResult;
    Value *SelectValue = ResultVector[1].first;
    if (DefaultCanTrigger) {
      Value *const ValueCompare =
          Builder.CreateICmpEQ(Condition, SecondCase, "switch.selectcmp");
      SelectValue = Builder.CreateSelect(ValueCompare, ResultVector[1].first,
                                         DefaultResult, "switch.select");
    }
    Value *const ValueCompare =
        Builder.CreateICmpEQ(Condition, FirstCase, "switch.selectcmp");
    return Builder.CreateSelect(ValueCompare, ResultVector[0].first,
                                SelectValue, "switch.select");
  }

  return nullptr;
}

// Helper function to cleanup a switch instruction that has been converted into
// a select, fixing up PHI nodes and basic blocks.
static void RemoveSwitchAfterSelectConversion(SwitchInst *SI, PHINode *PHI,
                                              Value *SelectValue,
                                              IRBuilder<> &Builder) {
  BasicBlock *SelectBB = SI->getParent();
  while (PHI->getBasicBlockIndex(SelectBB) >= 0)
    PHI->removeIncomingValue(SelectBB);
  PHI->addIncoming(SelectValue, SelectBB);

  Builder.CreateBr(PHI->getParent());

  // Remove the switch.
  for (unsigned i = 0, e = SI->getNumSuccessors(); i < e; ++i) {
    BasicBlock *Succ = SI->getSuccessor(i);

    if (Succ == PHI->getParent())
      continue;
    Succ->removePredecessor(SelectBB);
  }
  SI->eraseFromParent();
}

/// If the switch is only used to initialize one or more
/// phi nodes in a common successor block with only two different
/// constant values, replace the switch with select.
static bool switchToSelect(SwitchInst *SI, IRBuilder<> &Builder,
                           const DataLayout &DL,
                           const TargetTransformInfo &TTI) {
  Value *const Cond = SI->getCondition();
  PHINode *PHI = nullptr;
  BasicBlock *CommonDest = nullptr;
  Constant *DefaultResult;
  SwitchCaseResultVectorTy UniqueResults;
  // Collect all the cases that will deliver the same value from the switch.
  if (!InitializeUniqueCases(SI, PHI, CommonDest, UniqueResults, DefaultResult,
                             DL, TTI, 2, 1))
    return false;
  // Selects choose between maximum two values.
  if (UniqueResults.size() != 2)
    return false;
  assert(PHI != nullptr && "PHI for value select not found");

  Builder.SetInsertPoint(SI);
  Value *SelectValue =
      ConvertTwoCaseSwitch(UniqueResults, DefaultResult, Cond, Builder);
  if (SelectValue) {
    RemoveSwitchAfterSelectConversion(SI, PHI, SelectValue, Builder);
    return true;
  }
  // The switch couldn't be converted into a select.
  return false;
}

namespace {

/// This class represents a lookup table that can be used to replace a switch.
class SwitchLookupTable {
public:
  /// Create a lookup table to use as a switch replacement with the contents
  /// of Values, using DefaultValue to fill any holes in the table.
  SwitchLookupTable(
      Module &M, uint64_t TableSize, ConstantInt *Offset,
      const SmallVectorImpl<std::pair<ConstantInt *, Constant *>> &Values,
      Constant *DefaultValue, const DataLayout &DL, const StringRef &FuncName);

  /// Build instructions with Builder to retrieve the value at
  /// the position given by Index in the lookup table.
  Value *BuildLookup(Value *Index, IRBuilder<> &Builder);

  /// Return true if a table with TableSize elements of
  /// type ElementType would fit in a target-legal register.
  static bool WouldFitInRegister(const DataLayout &DL, uint64_t TableSize,
                                 Type *ElementType);

private:
  // Depending on the contents of the table, it can be represented in
  // different ways.
  enum {
    // For tables where each element contains the same value, we just have to
    // store that single value and return it for each lookup.
    SingleValueKind,

    // For tables where there is a linear relationship between table index
    // and values. We calculate the result with a simple multiplication
    // and addition instead of a table lookup.
    LinearMapKind,

    // For small tables with integer elements, we can pack them into a bitmap
    // that fits into a target-legal register. Values are retrieved by
    // shift and mask operations.
    BitMapKind,

    // The table is stored as an array of values. Values are retrieved by load
    // instructions from the table.
    ArrayKind
  } Kind;

  // For SingleValueKind, this is the single value.
  Constant *SingleValue = nullptr;

  // For BitMapKind, this is the bitmap.
  ConstantInt *BitMap = nullptr;
  IntegerType *BitMapElementTy = nullptr;

  // For LinearMapKind, these are the constants used to derive the value.
  ConstantInt *LinearOffset = nullptr;
  ConstantInt *LinearMultiplier = nullptr;

  // For ArrayKind, this is the array.
  GlobalVariable *Array = nullptr;
};

} // end anonymous namespace

SwitchLookupTable::SwitchLookupTable(
    Module &M, uint64_t TableSize, ConstantInt *Offset,
    const SmallVectorImpl<std::pair<ConstantInt *, Constant *>> &Values,
    Constant *DefaultValue, const DataLayout &DL, const StringRef &FuncName) {
  assert(Values.size() && "Can't build lookup table without values!");
  assert(TableSize >= Values.size() && "Can't fit values in table!");

  // If all values in the table are equal, this is that value.
  SingleValue = Values.begin()->second;

  Type *ValueType = Values.begin()->second->getType();

  // Build up the table contents.
  SmallVector<Constant *, 64> TableContents(TableSize);
  for (size_t I = 0, E = Values.size(); I != E; ++I) {
    ConstantInt *CaseVal = Values[I].first;
    Constant *CaseRes = Values[I].second;
    assert(CaseRes->getType() == ValueType);

    uint64_t Idx = (CaseVal->getValue() - Offset->getValue()).getLimitedValue();
    TableContents[Idx] = CaseRes;

    if (CaseRes != SingleValue)
      SingleValue = nullptr;
  }

  // Fill in any holes in the table with the default result.
  if (Values.size() < TableSize) {
    assert(DefaultValue &&
           "Need a default value to fill the lookup table holes.");
    assert(DefaultValue->getType() == ValueType);
    for (uint64_t I = 0; I < TableSize; ++I) {
      if (!TableContents[I])
        TableContents[I] = DefaultValue;
    }

    if (DefaultValue != SingleValue)
      SingleValue = nullptr;
  }

  // If each element in the table contains the same value, we only need to store
  // that single value.
  if (SingleValue) {
    Kind = SingleValueKind;
    return;
  }

  // Check if we can derive the value with a linear transformation from the
  // table index.
  if (isa<IntegerType>(ValueType)) {
    bool LinearMappingPossible = true;
    APInt PrevVal;
    APInt DistToPrev;
    assert(TableSize >= 2 && "Should be a SingleValue table.");
    // Check if there is the same distance between two consecutive values.
    for (uint64_t I = 0; I < TableSize; ++I) {
      ConstantInt *ConstVal = dyn_cast<ConstantInt>(TableContents[I]);
      if (!ConstVal) {
        // This is an undef. We could deal with it, but undefs in lookup tables
        // are very seldom. It's probably not worth the additional complexity.
        LinearMappingPossible = false;
        break;
      }
      const APInt &Val = ConstVal->getValue();
      if (I != 0) {
        APInt Dist = Val - PrevVal;
        if (I == 1) {
          DistToPrev = Dist;
        } else if (Dist != DistToPrev) {
          LinearMappingPossible = false;
          break;
        }
      }
      PrevVal = Val;
    }
    if (LinearMappingPossible) {
      LinearOffset = cast<ConstantInt>(TableContents[0]);
      LinearMultiplier = ConstantInt::get(M.getContext(), DistToPrev);
      Kind = LinearMapKind;
      ++NumLinearMaps;
      return;
    }
  }

  // If the type is integer and the table fits in a register, build a bitmap.
  if (WouldFitInRegister(DL, TableSize, ValueType)) {
    IntegerType *IT = cast<IntegerType>(ValueType);
    APInt TableInt(TableSize * IT->getBitWidth(), 0);
    for (uint64_t I = TableSize; I > 0; --I) {
      TableInt <<= IT->getBitWidth();
      // Insert values into the bitmap. Undef values are set to zero.
      if (!isa<UndefValue>(TableContents[I - 1])) {
        ConstantInt *Val = cast<ConstantInt>(TableContents[I - 1]);
        TableInt |= Val->getValue().zext(TableInt.getBitWidth());
      }
    }
    BitMap = ConstantInt::get(M.getContext(), TableInt);
    BitMapElementTy = IT;
    Kind = BitMapKind;
    ++NumBitMaps;
    return;
  }

  // Store the table in an array.
  ArrayType *ArrayTy = ArrayType::get(ValueType, TableSize);
  Constant *Initializer = ConstantArray::get(ArrayTy, TableContents);

  Array = new GlobalVariable(M, ArrayTy, /*constant=*/true,
                             GlobalVariable::PrivateLinkage, Initializer,
                             "switch.table." + FuncName);
  Array->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  // Set the alignment to that of an array items. We will be only loading one
  // value out of it.
  Array->setAlignment(DL.getPrefTypeAlignment(ValueType));
  Kind = ArrayKind;
}

Value *SwitchLookupTable::BuildLookup(Value *Index, IRBuilder<> &Builder) {
  switch (Kind) {
  case SingleValueKind:
    return SingleValue;
  case LinearMapKind: {
    // Derive the result value from the input value.
    Value *Result = Builder.CreateIntCast(Index, LinearMultiplier->getType(),
                                          false, "switch.idx.cast");
    if (!LinearMultiplier->isOne())
      Result = Builder.CreateMul(Result, LinearMultiplier, "switch.idx.mult");
    if (!LinearOffset->isZero())
      Result = Builder.CreateAdd(Result, LinearOffset, "switch.offset");
    return Result;
  }
  case BitMapKind: {
    // Type of the bitmap (e.g. i59).
    IntegerType *MapTy = BitMap->getType();

    // Cast Index to the same type as the bitmap.
    // Note: The Index is <= the number of elements in the table, so
    // truncating it to the width of the bitmask is safe.
    Value *ShiftAmt = Builder.CreateZExtOrTrunc(Index, MapTy, "switch.cast");

    // Multiply the shift amount by the element width.
    ShiftAmt = Builder.CreateMul(
        ShiftAmt, ConstantInt::get(MapTy, BitMapElementTy->getBitWidth()),
        "switch.shiftamt");

    // Shift down.
    Value *DownShifted =
        Builder.CreateLShr(BitMap, ShiftAmt, "switch.downshift");
    // Mask off.
    return Builder.CreateTrunc(DownShifted, BitMapElementTy, "switch.masked");
  }
  case ArrayKind: {
    // Make sure the table index will not overflow when treated as signed.
    IntegerType *IT = cast<IntegerType>(Index->getType());
    uint64_t TableSize =
        Array->getInitializer()->getType()->getArrayNumElements();
    if (TableSize > (1ULL << (IT->getBitWidth() - 1)))
      Index = Builder.CreateZExt(
          Index, IntegerType::get(IT->getContext(), IT->getBitWidth() + 1),
          "switch.tableidx.zext");

    Value *GEPIndices[] = {Builder.getInt32(0), Index};
    Value *GEP = Builder.CreateInBoundsGEP(Array->getValueType(), Array,
                                           GEPIndices, "switch.gep");
    return Builder.CreateLoad(GEP, "switch.load");
  }
  }
  llvm_unreachable("Unknown lookup table kind!");
}

bool SwitchLookupTable::WouldFitInRegister(const DataLayout &DL,
                                           uint64_t TableSize,
                                           Type *ElementType) {
  auto *IT = dyn_cast<IntegerType>(ElementType);
  if (!IT)
    return false;
  // FIXME: If the type is wider than it needs to be, e.g. i8 but all values
  // are <= 15, we could try to narrow the type.

  // Avoid overflow, fitsInLegalInteger uses unsigned int for the width.
  if (TableSize >= UINT_MAX / IT->getBitWidth())
    return false;
  return DL.fitsInLegalInteger(TableSize * IT->getBitWidth());
}

/// Determine whether a lookup table should be built for this switch, based on
/// the number of cases, size of the table, and the types of the results.
static bool
ShouldBuildLookupTable(SwitchInst *SI, uint64_t TableSize,
                       const TargetTransformInfo &TTI, const DataLayout &DL,
                       const SmallDenseMap<PHINode *, Type *> &ResultTypes) {
  if (SI->getNumCases() > TableSize || TableSize >= UINT64_MAX / 10)
    return false; // TableSize overflowed, or mul below might overflow.

  bool AllTablesFitInRegister = true;
  bool HasIllegalType = false;
  for (const auto &I : ResultTypes) {
    Type *Ty = I.second;

    // Saturate this flag to true.
    HasIllegalType = HasIllegalType || !TTI.isTypeLegal(Ty);

    // Saturate this flag to false.
    AllTablesFitInRegister =
        AllTablesFitInRegister &&
        SwitchLookupTable::WouldFitInRegister(DL, TableSize, Ty);

    // If both flags saturate, we're done. NOTE: This *only* works with
    // saturating flags, and all flags have to saturate first due to the
    // non-deterministic behavior of iterating over a dense map.
    if (HasIllegalType && !AllTablesFitInRegister)
      break;
  }

  // If each table would fit in a register, we should build it anyway.
  if (AllTablesFitInRegister)
    return true;

  // Don't build a table that doesn't fit in-register if it has illegal types.
  if (HasIllegalType)
    return false;

  // The table density should be at least 40%. This is the same criterion as for
  // jump tables, see SelectionDAGBuilder::handleJTSwitchCase.
  // FIXME: Find the best cut-off.
  return SI->getNumCases() * 10 >= TableSize * 4;
}

/// Try to reuse the switch table index compare. Following pattern:
/// \code
///     if (idx < tablesize)
///        r = table[idx]; // table does not contain default_value
///     else
///        r = default_value;
///     if (r != default_value)
///        ...
/// \endcode
/// Is optimized to:
/// \code
///     cond = idx < tablesize;
///     if (cond)
///        r = table[idx];
///     else
///        r = default_value;
///     if (cond)
///        ...
/// \endcode
/// Jump threading will then eliminate the second if(cond).
static void reuseTableCompare(
    User *PhiUser, BasicBlock *PhiBlock, BranchInst *RangeCheckBranch,
    Constant *DefaultValue,
    const SmallVectorImpl<std::pair<ConstantInt *, Constant *>> &Values) {
  ICmpInst *CmpInst = dyn_cast<ICmpInst>(PhiUser);
  if (!CmpInst)
    return;

  // We require that the compare is in the same block as the phi so that jump
  // threading can do its work afterwards.
  if (CmpInst->getParent() != PhiBlock)
    return;

  Constant *CmpOp1 = dyn_cast<Constant>(CmpInst->getOperand(1));
  if (!CmpOp1)
    return;

  Value *RangeCmp = RangeCheckBranch->getCondition();
  Constant *TrueConst = ConstantInt::getTrue(RangeCmp->getType());
  Constant *FalseConst = ConstantInt::getFalse(RangeCmp->getType());

  // Check if the compare with the default value is constant true or false.
  Constant *DefaultConst = ConstantExpr::getICmp(CmpInst->getPredicate(),
                                                 DefaultValue, CmpOp1, true);
  if (DefaultConst != TrueConst && DefaultConst != FalseConst)
    return;

  // Check if the compare with the case values is distinct from the default
  // compare result.
  for (auto ValuePair : Values) {
    Constant *CaseConst = ConstantExpr::getICmp(CmpInst->getPredicate(),
                                                ValuePair.second, CmpOp1, true);
    if (!CaseConst || CaseConst == DefaultConst || isa<UndefValue>(CaseConst))
      return;
    assert((CaseConst == TrueConst || CaseConst == FalseConst) &&
           "Expect true or false as compare result.");
  }

  // Check if the branch instruction dominates the phi node. It's a simple
  // dominance check, but sufficient for our needs.
  // Although this check is invariant in the calling loops, it's better to do it
  // at this late stage. Practically we do it at most once for a switch.
  BasicBlock *BranchBlock = RangeCheckBranch->getParent();
  for (auto PI = pred_begin(PhiBlock), E = pred_end(PhiBlock); PI != E; ++PI) {
    BasicBlock *Pred = *PI;
    if (Pred != BranchBlock && Pred->getUniquePredecessor() != BranchBlock)
      return;
  }

  if (DefaultConst == FalseConst) {
    // The compare yields the same result. We can replace it.
    CmpInst->replaceAllUsesWith(RangeCmp);
    ++NumTableCmpReuses;
  } else {
    // The compare yields the same result, just inverted. We can replace it.
    Value *InvertedTableCmp = BinaryOperator::CreateXor(
        RangeCmp, ConstantInt::get(RangeCmp->getType(), 1), "inverted.cmp",
        RangeCheckBranch);
    CmpInst->replaceAllUsesWith(InvertedTableCmp);
    ++NumTableCmpReuses;
  }
}

/// If the switch is only used to initialize one or more phi nodes in a common
/// successor block with different constant values, replace the switch with
/// lookup tables.
static bool SwitchToLookupTable(SwitchInst *SI, IRBuilder<> &Builder,
                                const DataLayout &DL,
                                const TargetTransformInfo &TTI) {
  assert(SI->getNumCases() > 1 && "Degenerate switch?");

  Function *Fn = SI->getParent()->getParent();
  // Only build lookup table when we have a target that supports it or the
  // attribute is not set.
  if (!TTI.shouldBuildLookupTables() ||
      (Fn->getFnAttribute("no-jump-tables").getValueAsString() == "true"))
    return false;

  // FIXME: If the switch is too sparse for a lookup table, perhaps we could
  // split off a dense part and build a lookup table for that.

  // FIXME: This creates arrays of GEPs to constant strings, which means each
  // GEP needs a runtime relocation in PIC code. We should just build one big
  // string and lookup indices into that.

  // Ignore switches with less than three cases. Lookup tables will not make
  // them faster, so we don't analyze them.
  if (SI->getNumCases() < 3)
    return false;

  // Figure out the corresponding result for each case value and phi node in the
  // common destination, as well as the min and max case values.
  assert(!empty(SI->cases()));
  SwitchInst::CaseIt CI = SI->case_begin();
  ConstantInt *MinCaseVal = CI->getCaseValue();
  ConstantInt *MaxCaseVal = CI->getCaseValue();

  BasicBlock *CommonDest = nullptr;

  using ResultListTy = SmallVector<std::pair<ConstantInt *, Constant *>, 4>;
  SmallDenseMap<PHINode *, ResultListTy> ResultLists;

  SmallDenseMap<PHINode *, Constant *> DefaultResults;
  SmallDenseMap<PHINode *, Type *> ResultTypes;
  SmallVector<PHINode *, 4> PHIs;

  for (SwitchInst::CaseIt E = SI->case_end(); CI != E; ++CI) {
    ConstantInt *CaseVal = CI->getCaseValue();
    if (CaseVal->getValue().slt(MinCaseVal->getValue()))
      MinCaseVal = CaseVal;
    if (CaseVal->getValue().sgt(MaxCaseVal->getValue()))
      MaxCaseVal = CaseVal;

    // Resulting value at phi nodes for this case value.
    using ResultsTy = SmallVector<std::pair<PHINode *, Constant *>, 4>;
    ResultsTy Results;
    if (!GetCaseResults(SI, CaseVal, CI->getCaseSuccessor(), &CommonDest,
                        Results, DL, TTI))
      return false;

    // Append the result from this case to the list for each phi.
    for (const auto &I : Results) {
      PHINode *PHI = I.first;
      Constant *Value = I.second;
      if (!ResultLists.count(PHI))
        PHIs.push_back(PHI);
      ResultLists[PHI].push_back(std::make_pair(CaseVal, Value));
    }
  }

  // Keep track of the result types.
  for (PHINode *PHI : PHIs) {
    ResultTypes[PHI] = ResultLists[PHI][0].second->getType();
  }

  uint64_t NumResults = ResultLists[PHIs[0]].size();
  APInt RangeSpread = MaxCaseVal->getValue() - MinCaseVal->getValue();
  uint64_t TableSize = RangeSpread.getLimitedValue() + 1;
  bool TableHasHoles = (NumResults < TableSize);

  // If the table has holes, we need a constant result for the default case
  // or a bitmask that fits in a register.
  SmallVector<std::pair<PHINode *, Constant *>, 4> DefaultResultsList;
  bool HasDefaultResults =
      GetCaseResults(SI, nullptr, SI->getDefaultDest(), &CommonDest,
                     DefaultResultsList, DL, TTI);

  bool NeedMask = (TableHasHoles && !HasDefaultResults);
  if (NeedMask) {
    // As an extra penalty for the validity test we require more cases.
    if (SI->getNumCases() < 4) // FIXME: Find best threshold value (benchmark).
      return false;
    if (!DL.fitsInLegalInteger(TableSize))
      return false;
  }

  for (const auto &I : DefaultResultsList) {
    PHINode *PHI = I.first;
    Constant *Result = I.second;
    DefaultResults[PHI] = Result;
  }

  if (!ShouldBuildLookupTable(SI, TableSize, TTI, DL, ResultTypes))
    return false;

  // Create the BB that does the lookups.
  Module &Mod = *CommonDest->getParent()->getParent();
  BasicBlock *LookupBB = BasicBlock::Create(
      Mod.getContext(), "switch.lookup", CommonDest->getParent(), CommonDest);

  // Compute the table index value.
  Builder.SetInsertPoint(SI);
  Value *TableIndex;
  if (MinCaseVal->isNullValue())
    TableIndex = SI->getCondition();
  else
    TableIndex = Builder.CreateSub(SI->getCondition(), MinCaseVal,
                                   "switch.tableidx");

  // Compute the maximum table size representable by the integer type we are
  // switching upon.
  unsigned CaseSize = MinCaseVal->getType()->getPrimitiveSizeInBits();
  uint64_t MaxTableSize = CaseSize > 63 ? UINT64_MAX : 1ULL << CaseSize;
  assert(MaxTableSize >= TableSize &&
         "It is impossible for a switch to have more entries than the max "
         "representable value of its input integer type's size.");

  // If the default destination is unreachable, or if the lookup table covers
  // all values of the conditional variable, branch directly to the lookup table
  // BB. Otherwise, check that the condition is within the case range.
  const bool DefaultIsReachable =
      !isa<UnreachableInst>(SI->getDefaultDest()->getFirstNonPHIOrDbg());
  const bool GeneratingCoveredLookupTable = (MaxTableSize == TableSize);
  BranchInst *RangeCheckBranch = nullptr;

  if (!DefaultIsReachable || GeneratingCoveredLookupTable) {
    Builder.CreateBr(LookupBB);
    // Note: We call removeProdecessor later since we need to be able to get the
    // PHI value for the default case in case we're using a bit mask.
  } else {
    Value *Cmp = Builder.CreateICmpULT(
        TableIndex, ConstantInt::get(MinCaseVal->getType(), TableSize));
    RangeCheckBranch =
        Builder.CreateCondBr(Cmp, LookupBB, SI->getDefaultDest());
  }

  // Populate the BB that does the lookups.
  Builder.SetInsertPoint(LookupBB);

  if (NeedMask) {
    // Before doing the lookup, we do the hole check. The LookupBB is therefore
    // re-purposed to do the hole check, and we create a new LookupBB.
    BasicBlock *MaskBB = LookupBB;
    MaskBB->setName("switch.hole_check");
    LookupBB = BasicBlock::Create(Mod.getContext(), "switch.lookup",
                                  CommonDest->getParent(), CommonDest);

    // Make the mask's bitwidth at least 8-bit and a power-of-2 to avoid
    // unnecessary illegal types.
    uint64_t TableSizePowOf2 = NextPowerOf2(std::max(7ULL, TableSize - 1ULL));
    APInt MaskInt(TableSizePowOf2, 0);
    APInt One(TableSizePowOf2, 1);
    // Build bitmask; fill in a 1 bit for every case.
    const ResultListTy &ResultList = ResultLists[PHIs[0]];
    for (size_t I = 0, E = ResultList.size(); I != E; ++I) {
      uint64_t Idx = (ResultList[I].first->getValue() - MinCaseVal->getValue())
                         .getLimitedValue();
      MaskInt |= One << Idx;
    }
    ConstantInt *TableMask = ConstantInt::get(Mod.getContext(), MaskInt);

    // Get the TableIndex'th bit of the bitmask.
    // If this bit is 0 (meaning hole) jump to the default destination,
    // else continue with table lookup.
    IntegerType *MapTy = TableMask->getType();
    Value *MaskIndex =
        Builder.CreateZExtOrTrunc(TableIndex, MapTy, "switch.maskindex");
    Value *Shifted = Builder.CreateLShr(TableMask, MaskIndex, "switch.shifted");
    Value *LoBit = Builder.CreateTrunc(
        Shifted, Type::getInt1Ty(Mod.getContext()), "switch.lobit");
    Builder.CreateCondBr(LoBit, LookupBB, SI->getDefaultDest());

    Builder.SetInsertPoint(LookupBB);
    AddPredecessorToBlock(SI->getDefaultDest(), MaskBB, SI->getParent());
  }

  if (!DefaultIsReachable || GeneratingCoveredLookupTable) {
    // We cached PHINodes in PHIs. To avoid accessing deleted PHINodes later,
    // do not delete PHINodes here.
    SI->getDefaultDest()->removePredecessor(SI->getParent(),
                                            /*DontDeleteUselessPHIs=*/true);
  }

  bool ReturnedEarly = false;
  for (PHINode *PHI : PHIs) {
    const ResultListTy &ResultList = ResultLists[PHI];

    // If using a bitmask, use any value to fill the lookup table holes.
    Constant *DV = NeedMask ? ResultLists[PHI][0].second : DefaultResults[PHI];
    StringRef FuncName = Fn->getName();
    SwitchLookupTable Table(Mod, TableSize, MinCaseVal, ResultList, DV, DL,
                            FuncName);

    Value *Result = Table.BuildLookup(TableIndex, Builder);

    // If the result is used to return immediately from the function, we want to
    // do that right here.
    if (PHI->hasOneUse() && isa<ReturnInst>(*PHI->user_begin()) &&
        PHI->user_back() == CommonDest->getFirstNonPHIOrDbg()) {
      Builder.CreateRet(Result);
      ReturnedEarly = true;
      break;
    }

    // Do a small peephole optimization: re-use the switch table compare if
    // possible.
    if (!TableHasHoles && HasDefaultResults && RangeCheckBranch) {
      BasicBlock *PhiBlock = PHI->getParent();
      // Search for compare instructions which use the phi.
      for (auto *User : PHI->users()) {
        reuseTableCompare(User, PhiBlock, RangeCheckBranch, DV, ResultList);
      }
    }

    PHI->addIncoming(Result, LookupBB);
  }

  if (!ReturnedEarly)
    Builder.CreateBr(CommonDest);

  // Remove the switch.
  for (unsigned i = 0, e = SI->getNumSuccessors(); i < e; ++i) {
    BasicBlock *Succ = SI->getSuccessor(i);

    if (Succ == SI->getDefaultDest())
      continue;
    Succ->removePredecessor(SI->getParent());
  }
  SI->eraseFromParent();

  ++NumLookupTables;
  if (NeedMask)
    ++NumLookupTablesHoles;
  return true;
}

static bool isSwitchDense(ArrayRef<int64_t> Values) {
  // See also SelectionDAGBuilder::isDense(), which this function was based on.
  uint64_t Diff = (uint64_t)Values.back() - (uint64_t)Values.front();
  uint64_t Range = Diff + 1;
  uint64_t NumCases = Values.size();
  // 40% is the default density for building a jump table in optsize/minsize mode.
  uint64_t MinDensity = 40;

  return NumCases * 100 >= Range * MinDensity;
}

/// Try to transform a switch that has "holes" in it to a contiguous sequence
/// of cases.
///
/// A switch such as: switch(i) {case 5: case 9: case 13: case 17:} can be
/// range-reduced to: switch ((i-5) / 4) {case 0: case 1: case 2: case 3:}.
///
/// This converts a sparse switch into a dense switch which allows better
/// lowering and could also allow transforming into a lookup table.
static bool ReduceSwitchRange(SwitchInst *SI, IRBuilder<> &Builder,
                              const DataLayout &DL,
                              const TargetTransformInfo &TTI) {
  auto *CondTy = cast<IntegerType>(SI->getCondition()->getType());
  if (CondTy->getIntegerBitWidth() > 64 ||
      !DL.fitsInLegalInteger(CondTy->getIntegerBitWidth()))
    return false;
  // Only bother with this optimization if there are more than 3 switch cases;
  // SDAG will only bother creating jump tables for 4 or more cases.
  if (SI->getNumCases() < 4)
    return false;

  // This transform is agnostic to the signedness of the input or case values. We
  // can treat the case values as signed or unsigned. We can optimize more common
  // cases such as a sequence crossing zero {-4,0,4,8} if we interpret case values
  // as signed.
  SmallVector<int64_t,4> Values;
  for (auto &C : SI->cases())
    Values.push_back(C.getCaseValue()->getValue().getSExtValue());
  llvm::sort(Values);

  // If the switch is already dense, there's nothing useful to do here.
  if (isSwitchDense(Values))
    return false;

  // First, transform the values such that they start at zero and ascend.
  int64_t Base = Values[0];
  for (auto &V : Values)
    V -= (uint64_t)(Base);

  // Now we have signed numbers that have been shifted so that, given enough
  // precision, there are no negative values. Since the rest of the transform
  // is bitwise only, we switch now to an unsigned representation.
  uint64_t GCD = 0;
  for (auto &V : Values)
    GCD = GreatestCommonDivisor64(GCD, (uint64_t)V);

  // This transform can be done speculatively because it is so cheap - it results
  // in a single rotate operation being inserted. This can only happen if the
  // factor extracted is a power of 2.
  // FIXME: If the GCD is an odd number we can multiply by the multiplicative
  // inverse of GCD and then perform this transform.
  // FIXME: It's possible that optimizing a switch on powers of two might also
  // be beneficial - flag values are often powers of two and we could use a CLZ
  // as the key function.
  if (GCD <= 1 || !isPowerOf2_64(GCD))
    // No common divisor found or too expensive to compute key function.
    return false;

  unsigned Shift = Log2_64(GCD);
  for (auto &V : Values)
    V = (int64_t)((uint64_t)V >> Shift);

  if (!isSwitchDense(Values))
    // Transform didn't create a dense switch.
    return false;

  // The obvious transform is to shift the switch condition right and emit a
  // check that the condition actually cleanly divided by GCD, i.e.
  //   C & (1 << Shift - 1) == 0
  // inserting a new CFG edge to handle the case where it didn't divide cleanly.
  //
  // A cheaper way of doing this is a simple ROTR(C, Shift). This performs the
  // shift and puts the shifted-off bits in the uppermost bits. If any of these
  // are nonzero then the switch condition will be very large and will hit the
  // default case.

  auto *Ty = cast<IntegerType>(SI->getCondition()->getType());
  Builder.SetInsertPoint(SI);
  auto *ShiftC = ConstantInt::get(Ty, Shift);
  auto *Sub = Builder.CreateSub(SI->getCondition(), ConstantInt::get(Ty, Base));
  auto *LShr = Builder.CreateLShr(Sub, ShiftC);
  auto *Shl = Builder.CreateShl(Sub, Ty->getBitWidth() - Shift);
  auto *Rot = Builder.CreateOr(LShr, Shl);
  SI->replaceUsesOfWith(SI->getCondition(), Rot);

  for (auto Case : SI->cases()) {
    auto *Orig = Case.getCaseValue();
    auto Sub = Orig->getValue() - APInt(Ty->getBitWidth(), Base);
    Case.setValue(
        cast<ConstantInt>(ConstantInt::get(Ty, Sub.lshr(ShiftC->getValue()))));
  }
  return true;
}

bool SimplifyCFGOpt::SimplifySwitch(SwitchInst *SI, IRBuilder<> &Builder) {
  BasicBlock *BB = SI->getParent();

  if (isValueEqualityComparison(SI)) {
    // If we only have one predecessor, and if it is a branch on this value,
    // see if that predecessor totally determines the outcome of this switch.
    if (BasicBlock *OnlyPred = BB->getSinglePredecessor())
      if (SimplifyEqualityComparisonWithOnlyPredecessor(SI, OnlyPred, Builder))
        return requestResimplify();

    Value *Cond = SI->getCondition();
    if (SelectInst *Select = dyn_cast<SelectInst>(Cond))
      if (SimplifySwitchOnSelect(SI, Select))
        return requestResimplify();

    // If the block only contains the switch, see if we can fold the block
    // away into any preds.
    if (SI == &*BB->instructionsWithoutDebug().begin())
      if (FoldValueComparisonIntoPredecessors(SI, Builder))
        return requestResimplify();
  }

  // Try to transform the switch into an icmp and a branch.
  if (TurnSwitchRangeIntoICmp(SI, Builder))
    return requestResimplify();

  // Remove unreachable cases.
  if (eliminateDeadSwitchCases(SI, Options.AC, DL))
    return requestResimplify();

  if (switchToSelect(SI, Builder, DL, TTI))
    return requestResimplify();

  if (Options.ForwardSwitchCondToPhi && ForwardSwitchConditionToPHI(SI))
    return requestResimplify();

  // The conversion from switch to lookup tables results in difficult-to-analyze
  // code and makes pruning branches much harder. This is a problem if the
  // switch expression itself can still be restricted as a result of inlining or
  // CVP. Therefore, only apply this transformation during late stages of the
  // optimisation pipeline.
  if (Options.ConvertSwitchToLookupTable &&
      SwitchToLookupTable(SI, Builder, DL, TTI))
    return requestResimplify();

  if (ReduceSwitchRange(SI, Builder, DL, TTI))
    return requestResimplify();

  return false;
}

bool SimplifyCFGOpt::SimplifyIndirectBr(IndirectBrInst *IBI) {
  BasicBlock *BB = IBI->getParent();
  bool Changed = false;

  // Eliminate redundant destinations.
  SmallPtrSet<Value *, 8> Succs;
  for (unsigned i = 0, e = IBI->getNumDestinations(); i != e; ++i) {
    BasicBlock *Dest = IBI->getDestination(i);
    if (!Dest->hasAddressTaken() || !Succs.insert(Dest).second) {
      Dest->removePredecessor(BB);
      IBI->removeDestination(i);
      --i;
      --e;
      Changed = true;
    }
  }

  if (IBI->getNumDestinations() == 0) {
    // If the indirectbr has no successors, change it to unreachable.
    new UnreachableInst(IBI->getContext(), IBI);
    EraseTerminatorAndDCECond(IBI);
    return true;
  }

  if (IBI->getNumDestinations() == 1) {
    // If the indirectbr has one successor, change it to a direct branch.
    BranchInst::Create(IBI->getDestination(0), IBI);
    EraseTerminatorAndDCECond(IBI);
    return true;
  }

  if (SelectInst *SI = dyn_cast<SelectInst>(IBI->getAddress())) {
    if (SimplifyIndirectBrOnSelect(IBI, SI))
      return requestResimplify();
  }
  return Changed;
}

/// Given an block with only a single landing pad and a unconditional branch
/// try to find another basic block which this one can be merged with.  This
/// handles cases where we have multiple invokes with unique landing pads, but
/// a shared handler.
///
/// We specifically choose to not worry about merging non-empty blocks
/// here.  That is a PRE/scheduling problem and is best solved elsewhere.  In
/// practice, the optimizer produces empty landing pad blocks quite frequently
/// when dealing with exception dense code.  (see: instcombine, gvn, if-else
/// sinking in this file)
///
/// This is primarily a code size optimization.  We need to avoid performing
/// any transform which might inhibit optimization (such as our ability to
/// specialize a particular handler via tail commoning).  We do this by not
/// merging any blocks which require us to introduce a phi.  Since the same
/// values are flowing through both blocks, we don't lose any ability to
/// specialize.  If anything, we make such specialization more likely.
///
/// TODO - This transformation could remove entries from a phi in the target
/// block when the inputs in the phi are the same for the two blocks being
/// merged.  In some cases, this could result in removal of the PHI entirely.
static bool TryToMergeLandingPad(LandingPadInst *LPad, BranchInst *BI,
                                 BasicBlock *BB) {
  auto Succ = BB->getUniqueSuccessor();
  assert(Succ);
  // If there's a phi in the successor block, we'd likely have to introduce
  // a phi into the merged landing pad block.
  if (isa<PHINode>(*Succ->begin()))
    return false;

  for (BasicBlock *OtherPred : predecessors(Succ)) {
    if (BB == OtherPred)
      continue;
    BasicBlock::iterator I = OtherPred->begin();
    LandingPadInst *LPad2 = dyn_cast<LandingPadInst>(I);
    if (!LPad2 || !LPad2->isIdenticalTo(LPad))
      continue;
    for (++I; isa<DbgInfoIntrinsic>(I); ++I)
      ;
    BranchInst *BI2 = dyn_cast<BranchInst>(I);
    if (!BI2 || !BI2->isIdenticalTo(BI))
      continue;

    // We've found an identical block.  Update our predecessors to take that
    // path instead and make ourselves dead.
    SmallPtrSet<BasicBlock *, 16> Preds;
    Preds.insert(pred_begin(BB), pred_end(BB));
    for (BasicBlock *Pred : Preds) {
      InvokeInst *II = cast<InvokeInst>(Pred->getTerminator());
      assert(II->getNormalDest() != BB && II->getUnwindDest() == BB &&
             "unexpected successor");
      II->setUnwindDest(OtherPred);
    }

    // The debug info in OtherPred doesn't cover the merged control flow that
    // used to go through BB.  We need to delete it or update it.
    for (auto I = OtherPred->begin(), E = OtherPred->end(); I != E;) {
      Instruction &Inst = *I;
      I++;
      if (isa<DbgInfoIntrinsic>(Inst))
        Inst.eraseFromParent();
    }

    SmallPtrSet<BasicBlock *, 16> Succs;
    Succs.insert(succ_begin(BB), succ_end(BB));
    for (BasicBlock *Succ : Succs) {
      Succ->removePredecessor(BB);
    }

    IRBuilder<> Builder(BI);
    Builder.CreateUnreachable();
    BI->eraseFromParent();
    return true;
  }
  return false;
}

bool SimplifyCFGOpt::SimplifyUncondBranch(BranchInst *BI,
                                          IRBuilder<> &Builder) {
  BasicBlock *BB = BI->getParent();
  BasicBlock *Succ = BI->getSuccessor(0);

  // If the Terminator is the only non-phi instruction, simplify the block.
  // If LoopHeader is provided, check if the block or its successor is a loop
  // header. (This is for early invocations before loop simplify and
  // vectorization to keep canonical loop forms for nested loops. These blocks
  // can be eliminated when the pass is invoked later in the back-end.)
  // Note that if BB has only one predecessor then we do not introduce new
  // backedge, so we can eliminate BB.
  bool NeedCanonicalLoop =
      Options.NeedCanonicalLoop &&
      (LoopHeaders && BB->hasNPredecessorsOrMore(2) &&
       (LoopHeaders->count(BB) || LoopHeaders->count(Succ)));
  BasicBlock::iterator I = BB->getFirstNonPHIOrDbg()->getIterator();
  if (I->isTerminator() && BB != &BB->getParent()->getEntryBlock() &&
      !NeedCanonicalLoop && TryToSimplifyUncondBranchFromEmptyBlock(BB))
    return true;

  // If the only instruction in the block is a seteq/setne comparison against a
  // constant, try to simplify the block.
  if (ICmpInst *ICI = dyn_cast<ICmpInst>(I))
    if (ICI->isEquality() && isa<ConstantInt>(ICI->getOperand(1))) {
      for (++I; isa<DbgInfoIntrinsic>(I); ++I)
        ;
      if (I->isTerminator() &&
          tryToSimplifyUncondBranchWithICmpInIt(ICI, Builder))
        return true;
    }

  // See if we can merge an empty landing pad block with another which is
  // equivalent.
  if (LandingPadInst *LPad = dyn_cast<LandingPadInst>(I)) {
    for (++I; isa<DbgInfoIntrinsic>(I); ++I)
      ;
    if (I->isTerminator() && TryToMergeLandingPad(LPad, BI, BB))
      return true;
  }

  // If this basic block is ONLY a compare and a branch, and if a predecessor
  // branches to us and our successor, fold the comparison into the
  // predecessor and use logical operations to update the incoming value
  // for PHI nodes in common successor.
  if (FoldBranchToCommonDest(BI, Options.BonusInstThreshold))
    return requestResimplify();
  return false;
}

static BasicBlock *allPredecessorsComeFromSameSource(BasicBlock *BB) {
  BasicBlock *PredPred = nullptr;
  for (auto *P : predecessors(BB)) {
    BasicBlock *PPred = P->getSinglePredecessor();
    if (!PPred || (PredPred && PredPred != PPred))
      return nullptr;
    PredPred = PPred;
  }
  return PredPred;
}

bool SimplifyCFGOpt::SimplifyCondBranch(BranchInst *BI, IRBuilder<> &Builder) {
  BasicBlock *BB = BI->getParent();
  const Function *Fn = BB->getParent();
  if (Fn && Fn->hasFnAttribute(Attribute::OptForFuzzing))
    return false;

  // Conditional branch
  if (isValueEqualityComparison(BI)) {
    // If we only have one predecessor, and if it is a branch on this value,
    // see if that predecessor totally determines the outcome of this
    // switch.
    if (BasicBlock *OnlyPred = BB->getSinglePredecessor())
      if (SimplifyEqualityComparisonWithOnlyPredecessor(BI, OnlyPred, Builder))
        return requestResimplify();

    // This block must be empty, except for the setcond inst, if it exists.
    // Ignore dbg intrinsics.
    auto I = BB->instructionsWithoutDebug().begin();
    if (&*I == BI) {
      if (FoldValueComparisonIntoPredecessors(BI, Builder))
        return requestResimplify();
    } else if (&*I == cast<Instruction>(BI->getCondition())) {
      ++I;
      if (&*I == BI && FoldValueComparisonIntoPredecessors(BI, Builder))
        return requestResimplify();
    }
  }

  // Try to turn "br (X == 0 | X == 1), T, F" into a switch instruction.
  if (SimplifyBranchOnICmpChain(BI, Builder, DL))
    return true;

  // If this basic block has dominating predecessor blocks and the dominating
  // blocks' conditions imply BI's condition, we know the direction of BI.
  Optional<bool> Imp = isImpliedByDomCondition(BI->getCondition(), BI, DL);
  if (Imp) {
    // Turn this into a branch on constant.
    auto *OldCond = BI->getCondition();
    ConstantInt *TorF = *Imp ? ConstantInt::getTrue(BB->getContext())
                             : ConstantInt::getFalse(BB->getContext());
    BI->setCondition(TorF);
    RecursivelyDeleteTriviallyDeadInstructions(OldCond);
    return requestResimplify();
  }

  // If this basic block is ONLY a compare and a branch, and if a predecessor
  // branches to us and one of our successors, fold the comparison into the
  // predecessor and use logical operations to pick the right destination.
  if (FoldBranchToCommonDest(BI, Options.BonusInstThreshold))
    return requestResimplify();

  // We have a conditional branch to two blocks that are only reachable
  // from BI.  We know that the condbr dominates the two blocks, so see if
  // there is any identical code in the "then" and "else" blocks.  If so, we
  // can hoist it up to the branching block.
  if (BI->getSuccessor(0)->getSinglePredecessor()) {
    if (BI->getSuccessor(1)->getSinglePredecessor()) {
      if (HoistThenElseCodeToIf(BI, TTI))
        return requestResimplify();
    } else {
      // If Successor #1 has multiple preds, we may be able to conditionally
      // execute Successor #0 if it branches to Successor #1.
      Instruction *Succ0TI = BI->getSuccessor(0)->getTerminator();
      if (Succ0TI->getNumSuccessors() == 1 &&
          Succ0TI->getSuccessor(0) == BI->getSuccessor(1))
        if (SpeculativelyExecuteBB(BI, BI->getSuccessor(0), TTI))
          return requestResimplify();
    }
  } else if (BI->getSuccessor(1)->getSinglePredecessor()) {
    // If Successor #0 has multiple preds, we may be able to conditionally
    // execute Successor #1 if it branches to Successor #0.
    Instruction *Succ1TI = BI->getSuccessor(1)->getTerminator();
    if (Succ1TI->getNumSuccessors() == 1 &&
        Succ1TI->getSuccessor(0) == BI->getSuccessor(0))
      if (SpeculativelyExecuteBB(BI, BI->getSuccessor(1), TTI))
        return requestResimplify();
  }

  // If this is a branch on a phi node in the current block, thread control
  // through this block if any PHI node entries are constants.
  if (PHINode *PN = dyn_cast<PHINode>(BI->getCondition()))
    if (PN->getParent() == BI->getParent())
      if (FoldCondBranchOnPHI(BI, DL, Options.AC))
        return requestResimplify();

  // Scan predecessor blocks for conditional branches.
  for (pred_iterator PI = pred_begin(BB), E = pred_end(BB); PI != E; ++PI)
    if (BranchInst *PBI = dyn_cast<BranchInst>((*PI)->getTerminator()))
      if (PBI != BI && PBI->isConditional())
        if (SimplifyCondBranchToCondBranch(PBI, BI, DL))
          return requestResimplify();

  // Look for diamond patterns.
  if (MergeCondStores)
    if (BasicBlock *PrevBB = allPredecessorsComeFromSameSource(BB))
      if (BranchInst *PBI = dyn_cast<BranchInst>(PrevBB->getTerminator()))
        if (PBI != BI && PBI->isConditional())
          if (mergeConditionalStores(PBI, BI, DL))
            return requestResimplify();

  return false;
}

/// Check if passing a value to an instruction will cause undefined behavior.
static bool passingValueIsAlwaysUndefined(Value *V, Instruction *I) {
  Constant *C = dyn_cast<Constant>(V);
  if (!C)
    return false;

  if (I->use_empty())
    return false;

  if (C->isNullValue() || isa<UndefValue>(C)) {
    // Only look at the first use, avoid hurting compile time with long uselists
    User *Use = *I->user_begin();

    // Now make sure that there are no instructions in between that can alter
    // control flow (eg. calls)
    for (BasicBlock::iterator
             i = ++BasicBlock::iterator(I),
             UI = BasicBlock::iterator(dyn_cast<Instruction>(Use));
         i != UI; ++i)
      if (i == I->getParent()->end() || i->mayHaveSideEffects())
        return false;

    // Look through GEPs. A load from a GEP derived from NULL is still undefined
    if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Use))
      if (GEP->getPointerOperand() == I)
        return passingValueIsAlwaysUndefined(V, GEP);

    // Look through bitcasts.
    if (BitCastInst *BC = dyn_cast<BitCastInst>(Use))
      return passingValueIsAlwaysUndefined(V, BC);

    // Load from null is undefined.
    if (LoadInst *LI = dyn_cast<LoadInst>(Use))
      if (!LI->isVolatile())
        return !NullPointerIsDefined(LI->getFunction(),
                                     LI->getPointerAddressSpace());

    // Store to null is undefined.
    if (StoreInst *SI = dyn_cast<StoreInst>(Use))
      if (!SI->isVolatile())
        return (!NullPointerIsDefined(SI->getFunction(),
                                      SI->getPointerAddressSpace())) &&
               SI->getPointerOperand() == I;

    // A call to null is undefined.
    if (auto CS = CallSite(Use))
      return !NullPointerIsDefined(CS->getFunction()) &&
             CS.getCalledValue() == I;
  }
  return false;
}

/// If BB has an incoming value that will always trigger undefined behavior
/// (eg. null pointer dereference), remove the branch leading here.
static bool removeUndefIntroducingPredecessor(BasicBlock *BB) {
  for (PHINode &PHI : BB->phis())
    for (unsigned i = 0, e = PHI.getNumIncomingValues(); i != e; ++i)
      if (passingValueIsAlwaysUndefined(PHI.getIncomingValue(i), &PHI)) {
        Instruction *T = PHI.getIncomingBlock(i)->getTerminator();
        IRBuilder<> Builder(T);
        if (BranchInst *BI = dyn_cast<BranchInst>(T)) {
          BB->removePredecessor(PHI.getIncomingBlock(i));
          // Turn uncoditional branches into unreachables and remove the dead
          // destination from conditional branches.
          if (BI->isUnconditional())
            Builder.CreateUnreachable();
          else
            Builder.CreateBr(BI->getSuccessor(0) == BB ? BI->getSuccessor(1)
                                                       : BI->getSuccessor(0));
          BI->eraseFromParent();
          return true;
        }
        // TODO: SwitchInst.
      }

  return false;
}

bool SimplifyCFGOpt::simplifyOnce(BasicBlock *BB) {
  bool Changed = false;

  assert(BB && BB->getParent() && "Block not embedded in function!");
  assert(BB->getTerminator() && "Degenerate basic block encountered!");

  // Remove basic blocks that have no predecessors (except the entry block)...
  // or that just have themself as a predecessor.  These are unreachable.
  if ((pred_empty(BB) && BB != &BB->getParent()->getEntryBlock()) ||
      BB->getSinglePredecessor() == BB) {
    LLVM_DEBUG(dbgs() << "Removing BB: \n" << *BB);
    DeleteDeadBlock(BB);
    return true;
  }

  // Check to see if we can constant propagate this terminator instruction
  // away...
  Changed |= ConstantFoldTerminator(BB, true);

  // Check for and eliminate duplicate PHI nodes in this block.
  Changed |= EliminateDuplicatePHINodes(BB);

  // Check for and remove branches that will always cause undefined behavior.
  Changed |= removeUndefIntroducingPredecessor(BB);

  // Merge basic blocks into their predecessor if there is only one distinct
  // pred, and if there is only one distinct successor of the predecessor, and
  // if there are no PHI nodes.
  if (MergeBlockIntoPredecessor(BB))
    return true;

  if (SinkCommon && Options.SinkCommonInsts)
    Changed |= SinkCommonCodeFromPredecessors(BB);

  IRBuilder<> Builder(BB);

  // If there is a trivial two-entry PHI node in this basic block, and we can
  // eliminate it, do so now.
  if (auto *PN = dyn_cast<PHINode>(BB->begin()))
    if (PN->getNumIncomingValues() == 2)
      Changed |= FoldTwoEntryPHINode(PN, TTI, DL);

  Builder.SetInsertPoint(BB->getTerminator());
  if (auto *BI = dyn_cast<BranchInst>(BB->getTerminator())) {
    if (BI->isUnconditional()) {
      if (SimplifyUncondBranch(BI, Builder))
        return true;
    } else {
      if (SimplifyCondBranch(BI, Builder))
        return true;
    }
  } else if (auto *RI = dyn_cast<ReturnInst>(BB->getTerminator())) {
    if (SimplifyReturn(RI, Builder))
      return true;
  } else if (auto *RI = dyn_cast<ResumeInst>(BB->getTerminator())) {
    if (SimplifyResume(RI, Builder))
      return true;
  } else if (auto *RI = dyn_cast<CleanupReturnInst>(BB->getTerminator())) {
    if (SimplifyCleanupReturn(RI))
      return true;
  } else if (auto *SI = dyn_cast<SwitchInst>(BB->getTerminator())) {
    if (SimplifySwitch(SI, Builder))
      return true;
  } else if (auto *UI = dyn_cast<UnreachableInst>(BB->getTerminator())) {
    if (SimplifyUnreachable(UI))
      return true;
  } else if (auto *IBI = dyn_cast<IndirectBrInst>(BB->getTerminator())) {
    if (SimplifyIndirectBr(IBI))
      return true;
  }

  return Changed;
}

bool SimplifyCFGOpt::run(BasicBlock *BB) {
  bool Changed = false;

  // Repeated simplify BB as long as resimplification is requested.
  do {
    Resimplify = false;

    // Perform one round of simplifcation. Resimplify flag will be set if
    // another iteration is requested.
    Changed |= simplifyOnce(BB);
  } while (Resimplify);

  return Changed;
}

bool llvm::simplifyCFG(BasicBlock *BB, const TargetTransformInfo &TTI,
                       const SimplifyCFGOptions &Options,
                       SmallPtrSetImpl<BasicBlock *> *LoopHeaders) {
  return SimplifyCFGOpt(TTI, BB->getModule()->getDataLayout(), LoopHeaders,
                        Options)
      .run(BB);
}
