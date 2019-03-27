//===- GVN.cpp - Eliminate redundant values and loads ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass performs global value numbering to eliminate fully redundant
// instructions.  It also performs simple dead load elimination.
//
// Note that this pass does the value numbering itself; it does not use the
// ValueNumbering analysis passes.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/PHITransAddr.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DomTreeUpdater.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"
#include "llvm/Transforms/Utils/VNCoercion.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::gvn;
using namespace llvm::VNCoercion;
using namespace PatternMatch;

#define DEBUG_TYPE "gvn"

STATISTIC(NumGVNInstr,  "Number of instructions deleted");
STATISTIC(NumGVNLoad,   "Number of loads deleted");
STATISTIC(NumGVNPRE,    "Number of instructions PRE'd");
STATISTIC(NumGVNBlocks, "Number of blocks merged");
STATISTIC(NumGVNSimpl,  "Number of instructions simplified");
STATISTIC(NumGVNEqProp, "Number of equalities propagated");
STATISTIC(NumPRELoad,   "Number of loads PRE'd");

static cl::opt<bool> EnablePRE("enable-pre",
                               cl::init(true), cl::Hidden);
static cl::opt<bool> EnableLoadPRE("enable-load-pre", cl::init(true));
static cl::opt<bool> EnableMemDep("enable-gvn-memdep", cl::init(true));

// Maximum allowed recursion depth.
static cl::opt<uint32_t>
MaxRecurseDepth("gvn-max-recurse-depth", cl::Hidden, cl::init(1000), cl::ZeroOrMore,
                cl::desc("Max recurse depth in GVN (default = 1000)"));

static cl::opt<uint32_t> MaxNumDeps(
    "gvn-max-num-deps", cl::Hidden, cl::init(100), cl::ZeroOrMore,
    cl::desc("Max number of dependences to attempt Load PRE (default = 100)"));

struct llvm::GVN::Expression {
  uint32_t opcode;
  Type *type;
  bool commutative = false;
  SmallVector<uint32_t, 4> varargs;

  Expression(uint32_t o = ~2U) : opcode(o) {}

  bool operator==(const Expression &other) const {
    if (opcode != other.opcode)
      return false;
    if (opcode == ~0U || opcode == ~1U)
      return true;
    if (type != other.type)
      return false;
    if (varargs != other.varargs)
      return false;
    return true;
  }

  friend hash_code hash_value(const Expression &Value) {
    return hash_combine(
        Value.opcode, Value.type,
        hash_combine_range(Value.varargs.begin(), Value.varargs.end()));
  }
};

namespace llvm {

template <> struct DenseMapInfo<GVN::Expression> {
  static inline GVN::Expression getEmptyKey() { return ~0U; }
  static inline GVN::Expression getTombstoneKey() { return ~1U; }

  static unsigned getHashValue(const GVN::Expression &e) {
    using llvm::hash_value;

    return static_cast<unsigned>(hash_value(e));
  }

  static bool isEqual(const GVN::Expression &LHS, const GVN::Expression &RHS) {
    return LHS == RHS;
  }
};

} // end namespace llvm

/// Represents a particular available value that we know how to materialize.
/// Materialization of an AvailableValue never fails.  An AvailableValue is
/// implicitly associated with a rematerialization point which is the
/// location of the instruction from which it was formed.
struct llvm::gvn::AvailableValue {
  enum ValType {
    SimpleVal, // A simple offsetted value that is accessed.
    LoadVal,   // A value produced by a load.
    MemIntrin, // A memory intrinsic which is loaded from.
    UndefVal   // A UndefValue representing a value from dead block (which
               // is not yet physically removed from the CFG).
  };

  /// V - The value that is live out of the block.
  PointerIntPair<Value *, 2, ValType> Val;

  /// Offset - The byte offset in Val that is interesting for the load query.
  unsigned Offset;

  static AvailableValue get(Value *V, unsigned Offset = 0) {
    AvailableValue Res;
    Res.Val.setPointer(V);
    Res.Val.setInt(SimpleVal);
    Res.Offset = Offset;
    return Res;
  }

  static AvailableValue getMI(MemIntrinsic *MI, unsigned Offset = 0) {
    AvailableValue Res;
    Res.Val.setPointer(MI);
    Res.Val.setInt(MemIntrin);
    Res.Offset = Offset;
    return Res;
  }

  static AvailableValue getLoad(LoadInst *LI, unsigned Offset = 0) {
    AvailableValue Res;
    Res.Val.setPointer(LI);
    Res.Val.setInt(LoadVal);
    Res.Offset = Offset;
    return Res;
  }

  static AvailableValue getUndef() {
    AvailableValue Res;
    Res.Val.setPointer(nullptr);
    Res.Val.setInt(UndefVal);
    Res.Offset = 0;
    return Res;
  }

  bool isSimpleValue() const { return Val.getInt() == SimpleVal; }
  bool isCoercedLoadValue() const { return Val.getInt() == LoadVal; }
  bool isMemIntrinValue() const { return Val.getInt() == MemIntrin; }
  bool isUndefValue() const { return Val.getInt() == UndefVal; }

  Value *getSimpleValue() const {
    assert(isSimpleValue() && "Wrong accessor");
    return Val.getPointer();
  }

  LoadInst *getCoercedLoadValue() const {
    assert(isCoercedLoadValue() && "Wrong accessor");
    return cast<LoadInst>(Val.getPointer());
  }

  MemIntrinsic *getMemIntrinValue() const {
    assert(isMemIntrinValue() && "Wrong accessor");
    return cast<MemIntrinsic>(Val.getPointer());
  }

  /// Emit code at the specified insertion point to adjust the value defined
  /// here to the specified type. This handles various coercion cases.
  Value *MaterializeAdjustedValue(LoadInst *LI, Instruction *InsertPt,
                                  GVN &gvn) const;
};

/// Represents an AvailableValue which can be rematerialized at the end of
/// the associated BasicBlock.
struct llvm::gvn::AvailableValueInBlock {
  /// BB - The basic block in question.
  BasicBlock *BB;

  /// AV - The actual available value
  AvailableValue AV;

  static AvailableValueInBlock get(BasicBlock *BB, AvailableValue &&AV) {
    AvailableValueInBlock Res;
    Res.BB = BB;
    Res.AV = std::move(AV);
    return Res;
  }

  static AvailableValueInBlock get(BasicBlock *BB, Value *V,
                                   unsigned Offset = 0) {
    return get(BB, AvailableValue::get(V, Offset));
  }

  static AvailableValueInBlock getUndef(BasicBlock *BB) {
    return get(BB, AvailableValue::getUndef());
  }

  /// Emit code at the end of this block to adjust the value defined here to
  /// the specified type. This handles various coercion cases.
  Value *MaterializeAdjustedValue(LoadInst *LI, GVN &gvn) const {
    return AV.MaterializeAdjustedValue(LI, BB->getTerminator(), gvn);
  }
};

//===----------------------------------------------------------------------===//
//                     ValueTable Internal Functions
//===----------------------------------------------------------------------===//

GVN::Expression GVN::ValueTable::createExpr(Instruction *I) {
  Expression e;
  e.type = I->getType();
  e.opcode = I->getOpcode();
  for (Instruction::op_iterator OI = I->op_begin(), OE = I->op_end();
       OI != OE; ++OI)
    e.varargs.push_back(lookupOrAdd(*OI));
  if (I->isCommutative()) {
    // Ensure that commutative instructions that only differ by a permutation
    // of their operands get the same value number by sorting the operand value
    // numbers.  Since all commutative instructions have two operands it is more
    // efficient to sort by hand rather than using, say, std::sort.
    assert(I->getNumOperands() == 2 && "Unsupported commutative instruction!");
    if (e.varargs[0] > e.varargs[1])
      std::swap(e.varargs[0], e.varargs[1]);
    e.commutative = true;
  }

  if (CmpInst *C = dyn_cast<CmpInst>(I)) {
    // Sort the operand value numbers so x<y and y>x get the same value number.
    CmpInst::Predicate Predicate = C->getPredicate();
    if (e.varargs[0] > e.varargs[1]) {
      std::swap(e.varargs[0], e.varargs[1]);
      Predicate = CmpInst::getSwappedPredicate(Predicate);
    }
    e.opcode = (C->getOpcode() << 8) | Predicate;
    e.commutative = true;
  } else if (InsertValueInst *E = dyn_cast<InsertValueInst>(I)) {
    for (InsertValueInst::idx_iterator II = E->idx_begin(), IE = E->idx_end();
         II != IE; ++II)
      e.varargs.push_back(*II);
  }

  return e;
}

GVN::Expression GVN::ValueTable::createCmpExpr(unsigned Opcode,
                                               CmpInst::Predicate Predicate,
                                               Value *LHS, Value *RHS) {
  assert((Opcode == Instruction::ICmp || Opcode == Instruction::FCmp) &&
         "Not a comparison!");
  Expression e;
  e.type = CmpInst::makeCmpResultType(LHS->getType());
  e.varargs.push_back(lookupOrAdd(LHS));
  e.varargs.push_back(lookupOrAdd(RHS));

  // Sort the operand value numbers so x<y and y>x get the same value number.
  if (e.varargs[0] > e.varargs[1]) {
    std::swap(e.varargs[0], e.varargs[1]);
    Predicate = CmpInst::getSwappedPredicate(Predicate);
  }
  e.opcode = (Opcode << 8) | Predicate;
  e.commutative = true;
  return e;
}

GVN::Expression GVN::ValueTable::createExtractvalueExpr(ExtractValueInst *EI) {
  assert(EI && "Not an ExtractValueInst?");
  Expression e;
  e.type = EI->getType();
  e.opcode = 0;

  IntrinsicInst *I = dyn_cast<IntrinsicInst>(EI->getAggregateOperand());
  if (I != nullptr && EI->getNumIndices() == 1 && *EI->idx_begin() == 0 ) {
    // EI might be an extract from one of our recognised intrinsics. If it
    // is we'll synthesize a semantically equivalent expression instead on
    // an extract value expression.
    switch (I->getIntrinsicID()) {
      case Intrinsic::sadd_with_overflow:
      case Intrinsic::uadd_with_overflow:
        e.opcode = Instruction::Add;
        break;
      case Intrinsic::ssub_with_overflow:
      case Intrinsic::usub_with_overflow:
        e.opcode = Instruction::Sub;
        break;
      case Intrinsic::smul_with_overflow:
      case Intrinsic::umul_with_overflow:
        e.opcode = Instruction::Mul;
        break;
      default:
        break;
    }

    if (e.opcode != 0) {
      // Intrinsic recognized. Grab its args to finish building the expression.
      assert(I->getNumArgOperands() == 2 &&
             "Expect two args for recognised intrinsics.");
      e.varargs.push_back(lookupOrAdd(I->getArgOperand(0)));
      e.varargs.push_back(lookupOrAdd(I->getArgOperand(1)));
      return e;
    }
  }

  // Not a recognised intrinsic. Fall back to producing an extract value
  // expression.
  e.opcode = EI->getOpcode();
  for (Instruction::op_iterator OI = EI->op_begin(), OE = EI->op_end();
       OI != OE; ++OI)
    e.varargs.push_back(lookupOrAdd(*OI));

  for (ExtractValueInst::idx_iterator II = EI->idx_begin(), IE = EI->idx_end();
         II != IE; ++II)
    e.varargs.push_back(*II);

  return e;
}

//===----------------------------------------------------------------------===//
//                     ValueTable External Functions
//===----------------------------------------------------------------------===//

GVN::ValueTable::ValueTable() = default;
GVN::ValueTable::ValueTable(const ValueTable &) = default;
GVN::ValueTable::ValueTable(ValueTable &&) = default;
GVN::ValueTable::~ValueTable() = default;

/// add - Insert a value into the table with a specified value number.
void GVN::ValueTable::add(Value *V, uint32_t num) {
  valueNumbering.insert(std::make_pair(V, num));
  if (PHINode *PN = dyn_cast<PHINode>(V))
    NumberingPhi[num] = PN;
}

uint32_t GVN::ValueTable::lookupOrAddCall(CallInst *C) {
  if (AA->doesNotAccessMemory(C)) {
    Expression exp = createExpr(C);
    uint32_t e = assignExpNewValueNum(exp).first;
    valueNumbering[C] = e;
    return e;
  } else if (MD && AA->onlyReadsMemory(C)) {
    Expression exp = createExpr(C);
    auto ValNum = assignExpNewValueNum(exp);
    if (ValNum.second) {
      valueNumbering[C] = ValNum.first;
      return ValNum.first;
    }

    MemDepResult local_dep = MD->getDependency(C);

    if (!local_dep.isDef() && !local_dep.isNonLocal()) {
      valueNumbering[C] =  nextValueNumber;
      return nextValueNumber++;
    }

    if (local_dep.isDef()) {
      CallInst* local_cdep = cast<CallInst>(local_dep.getInst());

      if (local_cdep->getNumArgOperands() != C->getNumArgOperands()) {
        valueNumbering[C] = nextValueNumber;
        return nextValueNumber++;
      }

      for (unsigned i = 0, e = C->getNumArgOperands(); i < e; ++i) {
        uint32_t c_vn = lookupOrAdd(C->getArgOperand(i));
        uint32_t cd_vn = lookupOrAdd(local_cdep->getArgOperand(i));
        if (c_vn != cd_vn) {
          valueNumbering[C] = nextValueNumber;
          return nextValueNumber++;
        }
      }

      uint32_t v = lookupOrAdd(local_cdep);
      valueNumbering[C] = v;
      return v;
    }

    // Non-local case.
    const MemoryDependenceResults::NonLocalDepInfo &deps =
        MD->getNonLocalCallDependency(C);
    // FIXME: Move the checking logic to MemDep!
    CallInst* cdep = nullptr;

    // Check to see if we have a single dominating call instruction that is
    // identical to C.
    for (unsigned i = 0, e = deps.size(); i != e; ++i) {
      const NonLocalDepEntry *I = &deps[i];
      if (I->getResult().isNonLocal())
        continue;

      // We don't handle non-definitions.  If we already have a call, reject
      // instruction dependencies.
      if (!I->getResult().isDef() || cdep != nullptr) {
        cdep = nullptr;
        break;
      }

      CallInst *NonLocalDepCall = dyn_cast<CallInst>(I->getResult().getInst());
      // FIXME: All duplicated with non-local case.
      if (NonLocalDepCall && DT->properlyDominates(I->getBB(), C->getParent())){
        cdep = NonLocalDepCall;
        continue;
      }

      cdep = nullptr;
      break;
    }

    if (!cdep) {
      valueNumbering[C] = nextValueNumber;
      return nextValueNumber++;
    }

    if (cdep->getNumArgOperands() != C->getNumArgOperands()) {
      valueNumbering[C] = nextValueNumber;
      return nextValueNumber++;
    }
    for (unsigned i = 0, e = C->getNumArgOperands(); i < e; ++i) {
      uint32_t c_vn = lookupOrAdd(C->getArgOperand(i));
      uint32_t cd_vn = lookupOrAdd(cdep->getArgOperand(i));
      if (c_vn != cd_vn) {
        valueNumbering[C] = nextValueNumber;
        return nextValueNumber++;
      }
    }

    uint32_t v = lookupOrAdd(cdep);
    valueNumbering[C] = v;
    return v;
  } else {
    valueNumbering[C] = nextValueNumber;
    return nextValueNumber++;
  }
}

/// Returns true if a value number exists for the specified value.
bool GVN::ValueTable::exists(Value *V) const { return valueNumbering.count(V) != 0; }

/// lookup_or_add - Returns the value number for the specified value, assigning
/// it a new number if it did not have one before.
uint32_t GVN::ValueTable::lookupOrAdd(Value *V) {
  DenseMap<Value*, uint32_t>::iterator VI = valueNumbering.find(V);
  if (VI != valueNumbering.end())
    return VI->second;

  if (!isa<Instruction>(V)) {
    valueNumbering[V] = nextValueNumber;
    return nextValueNumber++;
  }

  Instruction* I = cast<Instruction>(V);
  Expression exp;
  switch (I->getOpcode()) {
    case Instruction::Call:
      return lookupOrAddCall(cast<CallInst>(I));
    case Instruction::Add:
    case Instruction::FAdd:
    case Instruction::Sub:
    case Instruction::FSub:
    case Instruction::Mul:
    case Instruction::FMul:
    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::FDiv:
    case Instruction::URem:
    case Instruction::SRem:
    case Instruction::FRem:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
    case Instruction::ICmp:
    case Instruction::FCmp:
    case Instruction::Trunc:
    case Instruction::ZExt:
    case Instruction::SExt:
    case Instruction::FPToUI:
    case Instruction::FPToSI:
    case Instruction::UIToFP:
    case Instruction::SIToFP:
    case Instruction::FPTrunc:
    case Instruction::FPExt:
    case Instruction::PtrToInt:
    case Instruction::IntToPtr:
    case Instruction::BitCast:
    case Instruction::Select:
    case Instruction::ExtractElement:
    case Instruction::InsertElement:
    case Instruction::ShuffleVector:
    case Instruction::InsertValue:
    case Instruction::GetElementPtr:
      exp = createExpr(I);
      break;
    case Instruction::ExtractValue:
      exp = createExtractvalueExpr(cast<ExtractValueInst>(I));
      break;
    case Instruction::PHI:
      valueNumbering[V] = nextValueNumber;
      NumberingPhi[nextValueNumber] = cast<PHINode>(V);
      return nextValueNumber++;
    default:
      valueNumbering[V] = nextValueNumber;
      return nextValueNumber++;
  }

  uint32_t e = assignExpNewValueNum(exp).first;
  valueNumbering[V] = e;
  return e;
}

/// Returns the value number of the specified value. Fails if
/// the value has not yet been numbered.
uint32_t GVN::ValueTable::lookup(Value *V, bool Verify) const {
  DenseMap<Value*, uint32_t>::const_iterator VI = valueNumbering.find(V);
  if (Verify) {
    assert(VI != valueNumbering.end() && "Value not numbered?");
    return VI->second;
  }
  return (VI != valueNumbering.end()) ? VI->second : 0;
}

/// Returns the value number of the given comparison,
/// assigning it a new number if it did not have one before.  Useful when
/// we deduced the result of a comparison, but don't immediately have an
/// instruction realizing that comparison to hand.
uint32_t GVN::ValueTable::lookupOrAddCmp(unsigned Opcode,
                                         CmpInst::Predicate Predicate,
                                         Value *LHS, Value *RHS) {
  Expression exp = createCmpExpr(Opcode, Predicate, LHS, RHS);
  return assignExpNewValueNum(exp).first;
}

/// Remove all entries from the ValueTable.
void GVN::ValueTable::clear() {
  valueNumbering.clear();
  expressionNumbering.clear();
  NumberingPhi.clear();
  PhiTranslateTable.clear();
  nextValueNumber = 1;
  Expressions.clear();
  ExprIdx.clear();
  nextExprNumber = 0;
}

/// Remove a value from the value numbering.
void GVN::ValueTable::erase(Value *V) {
  uint32_t Num = valueNumbering.lookup(V);
  valueNumbering.erase(V);
  // If V is PHINode, V <--> value number is an one-to-one mapping.
  if (isa<PHINode>(V))
    NumberingPhi.erase(Num);
}

/// verifyRemoved - Verify that the value is removed from all internal data
/// structures.
void GVN::ValueTable::verifyRemoved(const Value *V) const {
  for (DenseMap<Value*, uint32_t>::const_iterator
         I = valueNumbering.begin(), E = valueNumbering.end(); I != E; ++I) {
    assert(I->first != V && "Inst still occurs in value numbering map!");
  }
}

//===----------------------------------------------------------------------===//
//                                GVN Pass
//===----------------------------------------------------------------------===//

PreservedAnalyses GVN::run(Function &F, FunctionAnalysisManager &AM) {
  // FIXME: The order of evaluation of these 'getResult' calls is very
  // significant! Re-ordering these variables will cause GVN when run alone to
  // be less effective! We should fix memdep and basic-aa to not exhibit this
  // behavior, but until then don't change the order here.
  auto &AC = AM.getResult<AssumptionAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  auto &AA = AM.getResult<AAManager>(F);
  auto &MemDep = AM.getResult<MemoryDependenceAnalysis>(F);
  auto *LI = AM.getCachedResult<LoopAnalysis>(F);
  auto &ORE = AM.getResult<OptimizationRemarkEmitterAnalysis>(F);
  bool Changed = runImpl(F, AC, DT, TLI, AA, &MemDep, LI, &ORE);
  if (!Changed)
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserve<DominatorTreeAnalysis>();
  PA.preserve<GlobalsAA>();
  PA.preserve<TargetLibraryAnalysis>();
  return PA;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void GVN::dump(DenseMap<uint32_t, Value*>& d) const {
  errs() << "{\n";
  for (DenseMap<uint32_t, Value*>::iterator I = d.begin(),
       E = d.end(); I != E; ++I) {
      errs() << I->first << "\n";
      I->second->dump();
  }
  errs() << "}\n";
}
#endif

/// Return true if we can prove that the value
/// we're analyzing is fully available in the specified block.  As we go, keep
/// track of which blocks we know are fully alive in FullyAvailableBlocks.  This
/// map is actually a tri-state map with the following values:
///   0) we know the block *is not* fully available.
///   1) we know the block *is* fully available.
///   2) we do not know whether the block is fully available or not, but we are
///      currently speculating that it will be.
///   3) we are speculating for this block and have used that to speculate for
///      other blocks.
static bool IsValueFullyAvailableInBlock(BasicBlock *BB,
                            DenseMap<BasicBlock*, char> &FullyAvailableBlocks,
                            uint32_t RecurseDepth) {
  if (RecurseDepth > MaxRecurseDepth)
    return false;

  // Optimistically assume that the block is fully available and check to see
  // if we already know about this block in one lookup.
  std::pair<DenseMap<BasicBlock*, char>::iterator, bool> IV =
    FullyAvailableBlocks.insert(std::make_pair(BB, 2));

  // If the entry already existed for this block, return the precomputed value.
  if (!IV.second) {
    // If this is a speculative "available" value, mark it as being used for
    // speculation of other blocks.
    if (IV.first->second == 2)
      IV.first->second = 3;
    return IV.first->second != 0;
  }

  // Otherwise, see if it is fully available in all predecessors.
  pred_iterator PI = pred_begin(BB), PE = pred_end(BB);

  // If this block has no predecessors, it isn't live-in here.
  if (PI == PE)
    goto SpeculationFailure;

  for (; PI != PE; ++PI)
    // If the value isn't fully available in one of our predecessors, then it
    // isn't fully available in this block either.  Undo our previous
    // optimistic assumption and bail out.
    if (!IsValueFullyAvailableInBlock(*PI, FullyAvailableBlocks,RecurseDepth+1))
      goto SpeculationFailure;

  return true;

// If we get here, we found out that this is not, after
// all, a fully-available block.  We have a problem if we speculated on this and
// used the speculation to mark other blocks as available.
SpeculationFailure:
  char &BBVal = FullyAvailableBlocks[BB];

  // If we didn't speculate on this, just return with it set to false.
  if (BBVal == 2) {
    BBVal = 0;
    return false;
  }

  // If we did speculate on this value, we could have blocks set to 1 that are
  // incorrect.  Walk the (transitive) successors of this block and mark them as
  // 0 if set to one.
  SmallVector<BasicBlock*, 32> BBWorklist;
  BBWorklist.push_back(BB);

  do {
    BasicBlock *Entry = BBWorklist.pop_back_val();
    // Note that this sets blocks to 0 (unavailable) if they happen to not
    // already be in FullyAvailableBlocks.  This is safe.
    char &EntryVal = FullyAvailableBlocks[Entry];
    if (EntryVal == 0) continue;  // Already unavailable.

    // Mark as unavailable.
    EntryVal = 0;

    BBWorklist.append(succ_begin(Entry), succ_end(Entry));
  } while (!BBWorklist.empty());

  return false;
}

/// Given a set of loads specified by ValuesPerBlock,
/// construct SSA form, allowing us to eliminate LI.  This returns the value
/// that should be used at LI's definition site.
static Value *ConstructSSAForLoadSet(LoadInst *LI,
                         SmallVectorImpl<AvailableValueInBlock> &ValuesPerBlock,
                                     GVN &gvn) {
  // Check for the fully redundant, dominating load case.  In this case, we can
  // just use the dominating value directly.
  if (ValuesPerBlock.size() == 1 &&
      gvn.getDominatorTree().properlyDominates(ValuesPerBlock[0].BB,
                                               LI->getParent())) {
    assert(!ValuesPerBlock[0].AV.isUndefValue() &&
           "Dead BB dominate this block");
    return ValuesPerBlock[0].MaterializeAdjustedValue(LI, gvn);
  }

  // Otherwise, we have to construct SSA form.
  SmallVector<PHINode*, 8> NewPHIs;
  SSAUpdater SSAUpdate(&NewPHIs);
  SSAUpdate.Initialize(LI->getType(), LI->getName());

  for (const AvailableValueInBlock &AV : ValuesPerBlock) {
    BasicBlock *BB = AV.BB;

    if (SSAUpdate.HasValueForBlock(BB))
      continue;

    // If the value is the load that we will be eliminating, and the block it's
    // available in is the block that the load is in, then don't add it as
    // SSAUpdater will resolve the value to the relevant phi which may let it
    // avoid phi construction entirely if there's actually only one value.
    if (BB == LI->getParent() &&
        ((AV.AV.isSimpleValue() && AV.AV.getSimpleValue() == LI) ||
         (AV.AV.isCoercedLoadValue() && AV.AV.getCoercedLoadValue() == LI)))
      continue;

    SSAUpdate.AddAvailableValue(BB, AV.MaterializeAdjustedValue(LI, gvn));
  }

  // Perform PHI construction.
  return SSAUpdate.GetValueInMiddleOfBlock(LI->getParent());
}

Value *AvailableValue::MaterializeAdjustedValue(LoadInst *LI,
                                                Instruction *InsertPt,
                                                GVN &gvn) const {
  Value *Res;
  Type *LoadTy = LI->getType();
  const DataLayout &DL = LI->getModule()->getDataLayout();
  if (isSimpleValue()) {
    Res = getSimpleValue();
    if (Res->getType() != LoadTy) {
      Res = getStoreValueForLoad(Res, Offset, LoadTy, InsertPt, DL);

      LLVM_DEBUG(dbgs() << "GVN COERCED NONLOCAL VAL:\nOffset: " << Offset
                        << "  " << *getSimpleValue() << '\n'
                        << *Res << '\n'
                        << "\n\n\n");
    }
  } else if (isCoercedLoadValue()) {
    LoadInst *Load = getCoercedLoadValue();
    if (Load->getType() == LoadTy && Offset == 0) {
      Res = Load;
    } else {
      Res = getLoadValueForLoad(Load, Offset, LoadTy, InsertPt, DL);
      // We would like to use gvn.markInstructionForDeletion here, but we can't
      // because the load is already memoized into the leader map table that GVN
      // tracks.  It is potentially possible to remove the load from the table,
      // but then there all of the operations based on it would need to be
      // rehashed.  Just leave the dead load around.
      gvn.getMemDep().removeInstruction(Load);
      LLVM_DEBUG(dbgs() << "GVN COERCED NONLOCAL LOAD:\nOffset: " << Offset
                        << "  " << *getCoercedLoadValue() << '\n'
                        << *Res << '\n'
                        << "\n\n\n");
    }
  } else if (isMemIntrinValue()) {
    Res = getMemInstValueForLoad(getMemIntrinValue(), Offset, LoadTy,
                                 InsertPt, DL);
    LLVM_DEBUG(dbgs() << "GVN COERCED NONLOCAL MEM INTRIN:\nOffset: " << Offset
                      << "  " << *getMemIntrinValue() << '\n'
                      << *Res << '\n'
                      << "\n\n\n");
  } else {
    assert(isUndefValue() && "Should be UndefVal");
    LLVM_DEBUG(dbgs() << "GVN COERCED NONLOCAL Undef:\n";);
    return UndefValue::get(LoadTy);
  }
  assert(Res && "failed to materialize?");
  return Res;
}

static bool isLifetimeStart(const Instruction *Inst) {
  if (const IntrinsicInst* II = dyn_cast<IntrinsicInst>(Inst))
    return II->getIntrinsicID() == Intrinsic::lifetime_start;
  return false;
}

/// Try to locate the three instruction involved in a missed
/// load-elimination case that is due to an intervening store.
static void reportMayClobberedLoad(LoadInst *LI, MemDepResult DepInfo,
                                   DominatorTree *DT,
                                   OptimizationRemarkEmitter *ORE) {
  using namespace ore;

  User *OtherAccess = nullptr;

  OptimizationRemarkMissed R(DEBUG_TYPE, "LoadClobbered", LI);
  R << "load of type " << NV("Type", LI->getType()) << " not eliminated"
    << setExtraArgs();

  for (auto *U : LI->getPointerOperand()->users())
    if (U != LI && (isa<LoadInst>(U) || isa<StoreInst>(U)) &&
        DT->dominates(cast<Instruction>(U), LI)) {
      // FIXME: for now give up if there are multiple memory accesses that
      // dominate the load.  We need further analysis to decide which one is
      // that we're forwarding from.
      if (OtherAccess)
        OtherAccess = nullptr;
      else
        OtherAccess = U;
    }

  if (OtherAccess)
    R << " in favor of " << NV("OtherAccess", OtherAccess);

  R << " because it is clobbered by " << NV("ClobberedBy", DepInfo.getInst());

  ORE->emit(R);
}

bool GVN::AnalyzeLoadAvailability(LoadInst *LI, MemDepResult DepInfo,
                                  Value *Address, AvailableValue &Res) {
  assert((DepInfo.isDef() || DepInfo.isClobber()) &&
         "expected a local dependence");
  assert(LI->isUnordered() && "rules below are incorrect for ordered access");

  const DataLayout &DL = LI->getModule()->getDataLayout();

  if (DepInfo.isClobber()) {
    // If the dependence is to a store that writes to a superset of the bits
    // read by the load, we can extract the bits we need for the load from the
    // stored value.
    if (StoreInst *DepSI = dyn_cast<StoreInst>(DepInfo.getInst())) {
      // Can't forward from non-atomic to atomic without violating memory model.
      if (Address && LI->isAtomic() <= DepSI->isAtomic()) {
        int Offset =
          analyzeLoadFromClobberingStore(LI->getType(), Address, DepSI, DL);
        if (Offset != -1) {
          Res = AvailableValue::get(DepSI->getValueOperand(), Offset);
          return true;
        }
      }
    }

    // Check to see if we have something like this:
    //    load i32* P
    //    load i8* (P+1)
    // if we have this, replace the later with an extraction from the former.
    if (LoadInst *DepLI = dyn_cast<LoadInst>(DepInfo.getInst())) {
      // If this is a clobber and L is the first instruction in its block, then
      // we have the first instruction in the entry block.
      // Can't forward from non-atomic to atomic without violating memory model.
      if (DepLI != LI && Address && LI->isAtomic() <= DepLI->isAtomic()) {
        int Offset =
          analyzeLoadFromClobberingLoad(LI->getType(), Address, DepLI, DL);

        if (Offset != -1) {
          Res = AvailableValue::getLoad(DepLI, Offset);
          return true;
        }
      }
    }

    // If the clobbering value is a memset/memcpy/memmove, see if we can
    // forward a value on from it.
    if (MemIntrinsic *DepMI = dyn_cast<MemIntrinsic>(DepInfo.getInst())) {
      if (Address && !LI->isAtomic()) {
        int Offset = analyzeLoadFromClobberingMemInst(LI->getType(), Address,
                                                      DepMI, DL);
        if (Offset != -1) {
          Res = AvailableValue::getMI(DepMI, Offset);
          return true;
        }
      }
    }
    // Nothing known about this clobber, have to be conservative
    LLVM_DEBUG(
        // fast print dep, using operator<< on instruction is too slow.
        dbgs() << "GVN: load "; LI->printAsOperand(dbgs());
        Instruction *I = DepInfo.getInst();
        dbgs() << " is clobbered by " << *I << '\n';);
    if (ORE->allowExtraAnalysis(DEBUG_TYPE))
      reportMayClobberedLoad(LI, DepInfo, DT, ORE);

    return false;
  }
  assert(DepInfo.isDef() && "follows from above");

  Instruction *DepInst = DepInfo.getInst();

  // Loading the allocation -> undef.
  if (isa<AllocaInst>(DepInst) || isMallocLikeFn(DepInst, TLI) ||
      // Loading immediately after lifetime begin -> undef.
      isLifetimeStart(DepInst)) {
    Res = AvailableValue::get(UndefValue::get(LI->getType()));
    return true;
  }

  // Loading from calloc (which zero initializes memory) -> zero
  if (isCallocLikeFn(DepInst, TLI)) {
    Res = AvailableValue::get(Constant::getNullValue(LI->getType()));
    return true;
  }

  if (StoreInst *S = dyn_cast<StoreInst>(DepInst)) {
    // Reject loads and stores that are to the same address but are of
    // different types if we have to. If the stored value is larger or equal to
    // the loaded value, we can reuse it.
    if (S->getValueOperand()->getType() != LI->getType() &&
        !canCoerceMustAliasedValueToLoad(S->getValueOperand(),
                                         LI->getType(), DL))
      return false;

    // Can't forward from non-atomic to atomic without violating memory model.
    if (S->isAtomic() < LI->isAtomic())
      return false;

    Res = AvailableValue::get(S->getValueOperand());
    return true;
  }

  if (LoadInst *LD = dyn_cast<LoadInst>(DepInst)) {
    // If the types mismatch and we can't handle it, reject reuse of the load.
    // If the stored value is larger or equal to the loaded value, we can reuse
    // it.
    if (LD->getType() != LI->getType() &&
        !canCoerceMustAliasedValueToLoad(LD, LI->getType(), DL))
      return false;

    // Can't forward from non-atomic to atomic without violating memory model.
    if (LD->isAtomic() < LI->isAtomic())
      return false;

    Res = AvailableValue::getLoad(LD);
    return true;
  }

  // Unknown def - must be conservative
  LLVM_DEBUG(
      // fast print dep, using operator<< on instruction is too slow.
      dbgs() << "GVN: load "; LI->printAsOperand(dbgs());
      dbgs() << " has unknown def " << *DepInst << '\n';);
  return false;
}

void GVN::AnalyzeLoadAvailability(LoadInst *LI, LoadDepVect &Deps,
                                  AvailValInBlkVect &ValuesPerBlock,
                                  UnavailBlkVect &UnavailableBlocks) {
  // Filter out useless results (non-locals, etc).  Keep track of the blocks
  // where we have a value available in repl, also keep track of whether we see
  // dependencies that produce an unknown value for the load (such as a call
  // that could potentially clobber the load).
  unsigned NumDeps = Deps.size();
  for (unsigned i = 0, e = NumDeps; i != e; ++i) {
    BasicBlock *DepBB = Deps[i].getBB();
    MemDepResult DepInfo = Deps[i].getResult();

    if (DeadBlocks.count(DepBB)) {
      // Dead dependent mem-op disguise as a load evaluating the same value
      // as the load in question.
      ValuesPerBlock.push_back(AvailableValueInBlock::getUndef(DepBB));
      continue;
    }

    if (!DepInfo.isDef() && !DepInfo.isClobber()) {
      UnavailableBlocks.push_back(DepBB);
      continue;
    }

    // The address being loaded in this non-local block may not be the same as
    // the pointer operand of the load if PHI translation occurs.  Make sure
    // to consider the right address.
    Value *Address = Deps[i].getAddress();

    AvailableValue AV;
    if (AnalyzeLoadAvailability(LI, DepInfo, Address, AV)) {
      // subtlety: because we know this was a non-local dependency, we know
      // it's safe to materialize anywhere between the instruction within
      // DepInfo and the end of it's block.
      ValuesPerBlock.push_back(AvailableValueInBlock::get(DepBB,
                                                          std::move(AV)));
    } else {
      UnavailableBlocks.push_back(DepBB);
    }
  }

  assert(NumDeps == ValuesPerBlock.size() + UnavailableBlocks.size() &&
         "post condition violation");
}

bool GVN::PerformLoadPRE(LoadInst *LI, AvailValInBlkVect &ValuesPerBlock,
                         UnavailBlkVect &UnavailableBlocks) {
  // Okay, we have *some* definitions of the value.  This means that the value
  // is available in some of our (transitive) predecessors.  Lets think about
  // doing PRE of this load.  This will involve inserting a new load into the
  // predecessor when it's not available.  We could do this in general, but
  // prefer to not increase code size.  As such, we only do this when we know
  // that we only have to insert *one* load (which means we're basically moving
  // the load, not inserting a new one).

  SmallPtrSet<BasicBlock *, 4> Blockers(UnavailableBlocks.begin(),
                                        UnavailableBlocks.end());

  // Let's find the first basic block with more than one predecessor.  Walk
  // backwards through predecessors if needed.
  BasicBlock *LoadBB = LI->getParent();
  BasicBlock *TmpBB = LoadBB;
  bool IsSafeToSpeculativelyExecute = isSafeToSpeculativelyExecute(LI);

  // Check that there is no implicit control flow instructions above our load in
  // its block. If there is an instruction that doesn't always pass the
  // execution to the following instruction, then moving through it may become
  // invalid. For example:
  //
  // int arr[LEN];
  // int index = ???;
  // ...
  // guard(0 <= index && index < LEN);
  // use(arr[index]);
  //
  // It is illegal to move the array access to any point above the guard,
  // because if the index is out of bounds we should deoptimize rather than
  // access the array.
  // Check that there is no guard in this block above our instruction.
  if (!IsSafeToSpeculativelyExecute && ICF->isDominatedByICFIFromSameBlock(LI))
    return false;
  while (TmpBB->getSinglePredecessor()) {
    TmpBB = TmpBB->getSinglePredecessor();
    if (TmpBB == LoadBB) // Infinite (unreachable) loop.
      return false;
    if (Blockers.count(TmpBB))
      return false;

    // If any of these blocks has more than one successor (i.e. if the edge we
    // just traversed was critical), then there are other paths through this
    // block along which the load may not be anticipated.  Hoisting the load
    // above this block would be adding the load to execution paths along
    // which it was not previously executed.
    if (TmpBB->getTerminator()->getNumSuccessors() != 1)
      return false;

    // Check that there is no implicit control flow in a block above.
    if (!IsSafeToSpeculativelyExecute && ICF->hasICF(TmpBB))
      return false;
  }

  assert(TmpBB);
  LoadBB = TmpBB;

  // Check to see how many predecessors have the loaded value fully
  // available.
  MapVector<BasicBlock *, Value *> PredLoads;
  DenseMap<BasicBlock*, char> FullyAvailableBlocks;
  for (const AvailableValueInBlock &AV : ValuesPerBlock)
    FullyAvailableBlocks[AV.BB] = true;
  for (BasicBlock *UnavailableBB : UnavailableBlocks)
    FullyAvailableBlocks[UnavailableBB] = false;

  SmallVector<BasicBlock *, 4> CriticalEdgePred;
  for (BasicBlock *Pred : predecessors(LoadBB)) {
    // If any predecessor block is an EH pad that does not allow non-PHI
    // instructions before the terminator, we can't PRE the load.
    if (Pred->getTerminator()->isEHPad()) {
      LLVM_DEBUG(
          dbgs() << "COULD NOT PRE LOAD BECAUSE OF AN EH PAD PREDECESSOR '"
                 << Pred->getName() << "': " << *LI << '\n');
      return false;
    }

    if (IsValueFullyAvailableInBlock(Pred, FullyAvailableBlocks, 0)) {
      continue;
    }

    if (Pred->getTerminator()->getNumSuccessors() != 1) {
      if (isa<IndirectBrInst>(Pred->getTerminator())) {
        LLVM_DEBUG(
            dbgs() << "COULD NOT PRE LOAD BECAUSE OF INDBR CRITICAL EDGE '"
                   << Pred->getName() << "': " << *LI << '\n');
        return false;
      }

      if (LoadBB->isEHPad()) {
        LLVM_DEBUG(
            dbgs() << "COULD NOT PRE LOAD BECAUSE OF AN EH PAD CRITICAL EDGE '"
                   << Pred->getName() << "': " << *LI << '\n');
        return false;
      }

      CriticalEdgePred.push_back(Pred);
    } else {
      // Only add the predecessors that will not be split for now.
      PredLoads[Pred] = nullptr;
    }
  }

  // Decide whether PRE is profitable for this load.
  unsigned NumUnavailablePreds = PredLoads.size() + CriticalEdgePred.size();
  assert(NumUnavailablePreds != 0 &&
         "Fully available value should already be eliminated!");

  // If this load is unavailable in multiple predecessors, reject it.
  // FIXME: If we could restructure the CFG, we could make a common pred with
  // all the preds that don't have an available LI and insert a new load into
  // that one block.
  if (NumUnavailablePreds != 1)
      return false;

  // Split critical edges, and update the unavailable predecessors accordingly.
  for (BasicBlock *OrigPred : CriticalEdgePred) {
    BasicBlock *NewPred = splitCriticalEdges(OrigPred, LoadBB);
    assert(!PredLoads.count(OrigPred) && "Split edges shouldn't be in map!");
    PredLoads[NewPred] = nullptr;
    LLVM_DEBUG(dbgs() << "Split critical edge " << OrigPred->getName() << "->"
                      << LoadBB->getName() << '\n');
  }

  // Check if the load can safely be moved to all the unavailable predecessors.
  bool CanDoPRE = true;
  const DataLayout &DL = LI->getModule()->getDataLayout();
  SmallVector<Instruction*, 8> NewInsts;
  for (auto &PredLoad : PredLoads) {
    BasicBlock *UnavailablePred = PredLoad.first;

    // Do PHI translation to get its value in the predecessor if necessary.  The
    // returned pointer (if non-null) is guaranteed to dominate UnavailablePred.

    // If all preds have a single successor, then we know it is safe to insert
    // the load on the pred (?!?), so we can insert code to materialize the
    // pointer if it is not available.
    PHITransAddr Address(LI->getPointerOperand(), DL, AC);
    Value *LoadPtr = nullptr;
    LoadPtr = Address.PHITranslateWithInsertion(LoadBB, UnavailablePred,
                                                *DT, NewInsts);

    // If we couldn't find or insert a computation of this phi translated value,
    // we fail PRE.
    if (!LoadPtr) {
      LLVM_DEBUG(dbgs() << "COULDN'T INSERT PHI TRANSLATED VALUE OF: "
                        << *LI->getPointerOperand() << "\n");
      CanDoPRE = false;
      break;
    }

    PredLoad.second = LoadPtr;
  }

  if (!CanDoPRE) {
    while (!NewInsts.empty()) {
      Instruction *I = NewInsts.pop_back_val();
      markInstructionForDeletion(I);
    }
    // HINT: Don't revert the edge-splitting as following transformation may
    // also need to split these critical edges.
    return !CriticalEdgePred.empty();
  }

  // Okay, we can eliminate this load by inserting a reload in the predecessor
  // and using PHI construction to get the value in the other predecessors, do
  // it.
  LLVM_DEBUG(dbgs() << "GVN REMOVING PRE LOAD: " << *LI << '\n');
  LLVM_DEBUG(if (!NewInsts.empty()) dbgs()
             << "INSERTED " << NewInsts.size() << " INSTS: " << *NewInsts.back()
             << '\n');

  // Assign value numbers to the new instructions.
  for (Instruction *I : NewInsts) {
    // Instructions that have been inserted in predecessor(s) to materialize
    // the load address do not retain their original debug locations. Doing
    // so could lead to confusing (but correct) source attributions.
    // FIXME: How do we retain source locations without causing poor debugging
    // behavior?
    I->setDebugLoc(DebugLoc());

    // FIXME: We really _ought_ to insert these value numbers into their
    // parent's availability map.  However, in doing so, we risk getting into
    // ordering issues.  If a block hasn't been processed yet, we would be
    // marking a value as AVAIL-IN, which isn't what we intend.
    VN.lookupOrAdd(I);
  }

  for (const auto &PredLoad : PredLoads) {
    BasicBlock *UnavailablePred = PredLoad.first;
    Value *LoadPtr = PredLoad.second;

    auto *NewLoad = new LoadInst(LoadPtr, LI->getName()+".pre",
                                 LI->isVolatile(), LI->getAlignment(),
                                 LI->getOrdering(), LI->getSyncScopeID(),
                                 UnavailablePred->getTerminator());
    NewLoad->setDebugLoc(LI->getDebugLoc());

    // Transfer the old load's AA tags to the new load.
    AAMDNodes Tags;
    LI->getAAMetadata(Tags);
    if (Tags)
      NewLoad->setAAMetadata(Tags);

    if (auto *MD = LI->getMetadata(LLVMContext::MD_invariant_load))
      NewLoad->setMetadata(LLVMContext::MD_invariant_load, MD);
    if (auto *InvGroupMD = LI->getMetadata(LLVMContext::MD_invariant_group))
      NewLoad->setMetadata(LLVMContext::MD_invariant_group, InvGroupMD);
    if (auto *RangeMD = LI->getMetadata(LLVMContext::MD_range))
      NewLoad->setMetadata(LLVMContext::MD_range, RangeMD);

    // We do not propagate the old load's debug location, because the new
    // load now lives in a different BB, and we want to avoid a jumpy line
    // table.
    // FIXME: How do we retain source locations without causing poor debugging
    // behavior?

    // Add the newly created load.
    ValuesPerBlock.push_back(AvailableValueInBlock::get(UnavailablePred,
                                                        NewLoad));
    MD->invalidateCachedPointerInfo(LoadPtr);
    LLVM_DEBUG(dbgs() << "GVN INSERTED " << *NewLoad << '\n');
  }

  // Perform PHI construction.
  Value *V = ConstructSSAForLoadSet(LI, ValuesPerBlock, *this);
  LI->replaceAllUsesWith(V);
  if (isa<PHINode>(V))
    V->takeName(LI);
  if (Instruction *I = dyn_cast<Instruction>(V))
    I->setDebugLoc(LI->getDebugLoc());
  if (V->getType()->isPtrOrPtrVectorTy())
    MD->invalidateCachedPointerInfo(V);
  markInstructionForDeletion(LI);
  ORE->emit([&]() {
    return OptimizationRemark(DEBUG_TYPE, "LoadPRE", LI)
           << "load eliminated by PRE";
  });
  ++NumPRELoad;
  return true;
}

static void reportLoadElim(LoadInst *LI, Value *AvailableValue,
                           OptimizationRemarkEmitter *ORE) {
  using namespace ore;

  ORE->emit([&]() {
    return OptimizationRemark(DEBUG_TYPE, "LoadElim", LI)
           << "load of type " << NV("Type", LI->getType()) << " eliminated"
           << setExtraArgs() << " in favor of "
           << NV("InfavorOfValue", AvailableValue);
  });
}

/// Attempt to eliminate a load whose dependencies are
/// non-local by performing PHI construction.
bool GVN::processNonLocalLoad(LoadInst *LI) {
  // non-local speculations are not allowed under asan.
  if (LI->getParent()->getParent()->hasFnAttribute(
          Attribute::SanitizeAddress) ||
      LI->getParent()->getParent()->hasFnAttribute(
          Attribute::SanitizeHWAddress))
    return false;

  // Step 1: Find the non-local dependencies of the load.
  LoadDepVect Deps;
  MD->getNonLocalPointerDependency(LI, Deps);

  // If we had to process more than one hundred blocks to find the
  // dependencies, this load isn't worth worrying about.  Optimizing
  // it will be too expensive.
  unsigned NumDeps = Deps.size();
  if (NumDeps > MaxNumDeps)
    return false;

  // If we had a phi translation failure, we'll have a single entry which is a
  // clobber in the current block.  Reject this early.
  if (NumDeps == 1 &&
      !Deps[0].getResult().isDef() && !Deps[0].getResult().isClobber()) {
    LLVM_DEBUG(dbgs() << "GVN: non-local load "; LI->printAsOperand(dbgs());
               dbgs() << " has unknown dependencies\n";);
    return false;
  }

  // If this load follows a GEP, see if we can PRE the indices before analyzing.
  if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(LI->getOperand(0))) {
    for (GetElementPtrInst::op_iterator OI = GEP->idx_begin(),
                                        OE = GEP->idx_end();
         OI != OE; ++OI)
      if (Instruction *I = dyn_cast<Instruction>(OI->get()))
        performScalarPRE(I);
  }

  // Step 2: Analyze the availability of the load
  AvailValInBlkVect ValuesPerBlock;
  UnavailBlkVect UnavailableBlocks;
  AnalyzeLoadAvailability(LI, Deps, ValuesPerBlock, UnavailableBlocks);

  // If we have no predecessors that produce a known value for this load, exit
  // early.
  if (ValuesPerBlock.empty())
    return false;

  // Step 3: Eliminate fully redundancy.
  //
  // If all of the instructions we depend on produce a known value for this
  // load, then it is fully redundant and we can use PHI insertion to compute
  // its value.  Insert PHIs and remove the fully redundant value now.
  if (UnavailableBlocks.empty()) {
    LLVM_DEBUG(dbgs() << "GVN REMOVING NONLOCAL LOAD: " << *LI << '\n');

    // Perform PHI construction.
    Value *V = ConstructSSAForLoadSet(LI, ValuesPerBlock, *this);
    LI->replaceAllUsesWith(V);

    if (isa<PHINode>(V))
      V->takeName(LI);
    if (Instruction *I = dyn_cast<Instruction>(V))
      // If instruction I has debug info, then we should not update it.
      // Also, if I has a null DebugLoc, then it is still potentially incorrect
      // to propagate LI's DebugLoc because LI may not post-dominate I.
      if (LI->getDebugLoc() && LI->getParent() == I->getParent())
        I->setDebugLoc(LI->getDebugLoc());
    if (V->getType()->isPtrOrPtrVectorTy())
      MD->invalidateCachedPointerInfo(V);
    markInstructionForDeletion(LI);
    ++NumGVNLoad;
    reportLoadElim(LI, V, ORE);
    return true;
  }

  // Step 4: Eliminate partial redundancy.
  if (!EnablePRE || !EnableLoadPRE)
    return false;

  return PerformLoadPRE(LI, ValuesPerBlock, UnavailableBlocks);
}

bool GVN::processAssumeIntrinsic(IntrinsicInst *IntrinsicI) {
  assert(IntrinsicI->getIntrinsicID() == Intrinsic::assume &&
         "This function can only be called with llvm.assume intrinsic");
  Value *V = IntrinsicI->getArgOperand(0);

  if (ConstantInt *Cond = dyn_cast<ConstantInt>(V)) {
    if (Cond->isZero()) {
      Type *Int8Ty = Type::getInt8Ty(V->getContext());
      // Insert a new store to null instruction before the load to indicate that
      // this code is not reachable.  FIXME: We could insert unreachable
      // instruction directly because we can modify the CFG.
      new StoreInst(UndefValue::get(Int8Ty),
                    Constant::getNullValue(Int8Ty->getPointerTo()),
                    IntrinsicI);
    }
    markInstructionForDeletion(IntrinsicI);
    return false;
  } else if (isa<Constant>(V)) {
    // If it's not false, and constant, it must evaluate to true. This means our
    // assume is assume(true), and thus, pointless, and we don't want to do
    // anything more here.
    return false;
  }

  Constant *True = ConstantInt::getTrue(V->getContext());
  bool Changed = false;

  for (BasicBlock *Successor : successors(IntrinsicI->getParent())) {
    BasicBlockEdge Edge(IntrinsicI->getParent(), Successor);

    // This property is only true in dominated successors, propagateEquality
    // will check dominance for us.
    Changed |= propagateEquality(V, True, Edge, false);
  }

  // We can replace assume value with true, which covers cases like this:
  // call void @llvm.assume(i1 %cmp)
  // br i1 %cmp, label %bb1, label %bb2 ; will change %cmp to true
  ReplaceWithConstMap[V] = True;

  // If one of *cmp *eq operand is const, adding it to map will cover this:
  // %cmp = fcmp oeq float 3.000000e+00, %0 ; const on lhs could happen
  // call void @llvm.assume(i1 %cmp)
  // ret float %0 ; will change it to ret float 3.000000e+00
  if (auto *CmpI = dyn_cast<CmpInst>(V)) {
    if (CmpI->getPredicate() == CmpInst::Predicate::ICMP_EQ ||
        CmpI->getPredicate() == CmpInst::Predicate::FCMP_OEQ ||
        (CmpI->getPredicate() == CmpInst::Predicate::FCMP_UEQ &&
         CmpI->getFastMathFlags().noNaNs())) {
      Value *CmpLHS = CmpI->getOperand(0);
      Value *CmpRHS = CmpI->getOperand(1);
      if (isa<Constant>(CmpLHS))
        std::swap(CmpLHS, CmpRHS);
      auto *RHSConst = dyn_cast<Constant>(CmpRHS);

      // If only one operand is constant.
      if (RHSConst != nullptr && !isa<Constant>(CmpLHS))
        ReplaceWithConstMap[CmpLHS] = RHSConst;
    }
  }
  return Changed;
}

static void patchAndReplaceAllUsesWith(Instruction *I, Value *Repl) {
  patchReplacementInstruction(I, Repl);
  I->replaceAllUsesWith(Repl);
}

/// Attempt to eliminate a load, first by eliminating it
/// locally, and then attempting non-local elimination if that fails.
bool GVN::processLoad(LoadInst *L) {
  if (!MD)
    return false;

  // This code hasn't been audited for ordered or volatile memory access
  if (!L->isUnordered())
    return false;

  if (L->use_empty()) {
    markInstructionForDeletion(L);
    return true;
  }

  // ... to a pointer that has been loaded from before...
  MemDepResult Dep = MD->getDependency(L);

  // If it is defined in another block, try harder.
  if (Dep.isNonLocal())
    return processNonLocalLoad(L);

  // Only handle the local case below
  if (!Dep.isDef() && !Dep.isClobber()) {
    // This might be a NonFuncLocal or an Unknown
    LLVM_DEBUG(
        // fast print dep, using operator<< on instruction is too slow.
        dbgs() << "GVN: load "; L->printAsOperand(dbgs());
        dbgs() << " has unknown dependence\n";);
    return false;
  }

  AvailableValue AV;
  if (AnalyzeLoadAvailability(L, Dep, L->getPointerOperand(), AV)) {
    Value *AvailableValue = AV.MaterializeAdjustedValue(L, L, *this);

    // Replace the load!
    patchAndReplaceAllUsesWith(L, AvailableValue);
    markInstructionForDeletion(L);
    ++NumGVNLoad;
    reportLoadElim(L, AvailableValue, ORE);
    // Tell MDA to rexamine the reused pointer since we might have more
    // information after forwarding it.
    if (MD && AvailableValue->getType()->isPtrOrPtrVectorTy())
      MD->invalidateCachedPointerInfo(AvailableValue);
    return true;
  }

  return false;
}

/// Return a pair the first field showing the value number of \p Exp and the
/// second field showing whether it is a value number newly created.
std::pair<uint32_t, bool>
GVN::ValueTable::assignExpNewValueNum(Expression &Exp) {
  uint32_t &e = expressionNumbering[Exp];
  bool CreateNewValNum = !e;
  if (CreateNewValNum) {
    Expressions.push_back(Exp);
    if (ExprIdx.size() < nextValueNumber + 1)
      ExprIdx.resize(nextValueNumber * 2);
    e = nextValueNumber;
    ExprIdx[nextValueNumber++] = nextExprNumber++;
  }
  return {e, CreateNewValNum};
}

/// Return whether all the values related with the same \p num are
/// defined in \p BB.
bool GVN::ValueTable::areAllValsInBB(uint32_t Num, const BasicBlock *BB,
                                     GVN &Gvn) {
  LeaderTableEntry *Vals = &Gvn.LeaderTable[Num];
  while (Vals && Vals->BB == BB)
    Vals = Vals->Next;
  return !Vals;
}

/// Wrap phiTranslateImpl to provide caching functionality.
uint32_t GVN::ValueTable::phiTranslate(const BasicBlock *Pred,
                                       const BasicBlock *PhiBlock, uint32_t Num,
                                       GVN &Gvn) {
  auto FindRes = PhiTranslateTable.find({Num, Pred});
  if (FindRes != PhiTranslateTable.end())
    return FindRes->second;
  uint32_t NewNum = phiTranslateImpl(Pred, PhiBlock, Num, Gvn);
  PhiTranslateTable.insert({{Num, Pred}, NewNum});
  return NewNum;
}

/// Translate value number \p Num using phis, so that it has the values of
/// the phis in BB.
uint32_t GVN::ValueTable::phiTranslateImpl(const BasicBlock *Pred,
                                           const BasicBlock *PhiBlock,
                                           uint32_t Num, GVN &Gvn) {
  if (PHINode *PN = NumberingPhi[Num]) {
    for (unsigned i = 0; i != PN->getNumIncomingValues(); ++i) {
      if (PN->getParent() == PhiBlock && PN->getIncomingBlock(i) == Pred)
        if (uint32_t TransVal = lookup(PN->getIncomingValue(i), false))
          return TransVal;
    }
    return Num;
  }

  // If there is any value related with Num is defined in a BB other than
  // PhiBlock, it cannot depend on a phi in PhiBlock without going through
  // a backedge. We can do an early exit in that case to save compile time.
  if (!areAllValsInBB(Num, PhiBlock, Gvn))
    return Num;

  if (Num >= ExprIdx.size() || ExprIdx[Num] == 0)
    return Num;
  Expression Exp = Expressions[ExprIdx[Num]];

  for (unsigned i = 0; i < Exp.varargs.size(); i++) {
    // For InsertValue and ExtractValue, some varargs are index numbers
    // instead of value numbers. Those index numbers should not be
    // translated.
    if ((i > 1 && Exp.opcode == Instruction::InsertValue) ||
        (i > 0 && Exp.opcode == Instruction::ExtractValue))
      continue;
    Exp.varargs[i] = phiTranslate(Pred, PhiBlock, Exp.varargs[i], Gvn);
  }

  if (Exp.commutative) {
    assert(Exp.varargs.size() == 2 && "Unsupported commutative expression!");
    if (Exp.varargs[0] > Exp.varargs[1]) {
      std::swap(Exp.varargs[0], Exp.varargs[1]);
      uint32_t Opcode = Exp.opcode >> 8;
      if (Opcode == Instruction::ICmp || Opcode == Instruction::FCmp)
        Exp.opcode = (Opcode << 8) |
                     CmpInst::getSwappedPredicate(
                         static_cast<CmpInst::Predicate>(Exp.opcode & 255));
    }
  }

  if (uint32_t NewNum = expressionNumbering[Exp])
    return NewNum;
  return Num;
}

/// Erase stale entry from phiTranslate cache so phiTranslate can be computed
/// again.
void GVN::ValueTable::eraseTranslateCacheEntry(uint32_t Num,
                                               const BasicBlock &CurrBlock) {
  for (const BasicBlock *Pred : predecessors(&CurrBlock)) {
    auto FindRes = PhiTranslateTable.find({Num, Pred});
    if (FindRes != PhiTranslateTable.end())
      PhiTranslateTable.erase(FindRes);
  }
}

// In order to find a leader for a given value number at a
// specific basic block, we first obtain the list of all Values for that number,
// and then scan the list to find one whose block dominates the block in
// question.  This is fast because dominator tree queries consist of only
// a few comparisons of DFS numbers.
Value *GVN::findLeader(const BasicBlock *BB, uint32_t num) {
  LeaderTableEntry Vals = LeaderTable[num];
  if (!Vals.Val) return nullptr;

  Value *Val = nullptr;
  if (DT->dominates(Vals.BB, BB)) {
    Val = Vals.Val;
    if (isa<Constant>(Val)) return Val;
  }

  LeaderTableEntry* Next = Vals.Next;
  while (Next) {
    if (DT->dominates(Next->BB, BB)) {
      if (isa<Constant>(Next->Val)) return Next->Val;
      if (!Val) Val = Next->Val;
    }

    Next = Next->Next;
  }

  return Val;
}

/// There is an edge from 'Src' to 'Dst'.  Return
/// true if every path from the entry block to 'Dst' passes via this edge.  In
/// particular 'Dst' must not be reachable via another edge from 'Src'.
static bool isOnlyReachableViaThisEdge(const BasicBlockEdge &E,
                                       DominatorTree *DT) {
  // While in theory it is interesting to consider the case in which Dst has
  // more than one predecessor, because Dst might be part of a loop which is
  // only reachable from Src, in practice it is pointless since at the time
  // GVN runs all such loops have preheaders, which means that Dst will have
  // been changed to have only one predecessor, namely Src.
  const BasicBlock *Pred = E.getEnd()->getSinglePredecessor();
  assert((!Pred || Pred == E.getStart()) &&
         "No edge between these basic blocks!");
  return Pred != nullptr;
}

void GVN::assignBlockRPONumber(Function &F) {
  BlockRPONumber.clear();
  uint32_t NextBlockNumber = 1;
  ReversePostOrderTraversal<Function *> RPOT(&F);
  for (BasicBlock *BB : RPOT)
    BlockRPONumber[BB] = NextBlockNumber++;
  InvalidBlockRPONumbers = false;
}

// Tries to replace instruction with const, using information from
// ReplaceWithConstMap.
bool GVN::replaceOperandsWithConsts(Instruction *Instr) const {
  bool Changed = false;
  for (unsigned OpNum = 0; OpNum < Instr->getNumOperands(); ++OpNum) {
    Value *Operand = Instr->getOperand(OpNum);
    auto it = ReplaceWithConstMap.find(Operand);
    if (it != ReplaceWithConstMap.end()) {
      assert(!isa<Constant>(Operand) &&
             "Replacing constants with constants is invalid");
      LLVM_DEBUG(dbgs() << "GVN replacing: " << *Operand << " with "
                        << *it->second << " in instruction " << *Instr << '\n');
      Instr->setOperand(OpNum, it->second);
      Changed = true;
    }
  }
  return Changed;
}

/// The given values are known to be equal in every block
/// dominated by 'Root'.  Exploit this, for example by replacing 'LHS' with
/// 'RHS' everywhere in the scope.  Returns whether a change was made.
/// If DominatesByEdge is false, then it means that we will propagate the RHS
/// value starting from the end of Root.Start.
bool GVN::propagateEquality(Value *LHS, Value *RHS, const BasicBlockEdge &Root,
                            bool DominatesByEdge) {
  SmallVector<std::pair<Value*, Value*>, 4> Worklist;
  Worklist.push_back(std::make_pair(LHS, RHS));
  bool Changed = false;
  // For speed, compute a conservative fast approximation to
  // DT->dominates(Root, Root.getEnd());
  const bool RootDominatesEnd = isOnlyReachableViaThisEdge(Root, DT);

  while (!Worklist.empty()) {
    std::pair<Value*, Value*> Item = Worklist.pop_back_val();
    LHS = Item.first; RHS = Item.second;

    if (LHS == RHS)
      continue;
    assert(LHS->getType() == RHS->getType() && "Equality but unequal types!");

    // Don't try to propagate equalities between constants.
    if (isa<Constant>(LHS) && isa<Constant>(RHS))
      continue;

    // Prefer a constant on the right-hand side, or an Argument if no constants.
    if (isa<Constant>(LHS) || (isa<Argument>(LHS) && !isa<Constant>(RHS)))
      std::swap(LHS, RHS);
    assert((isa<Argument>(LHS) || isa<Instruction>(LHS)) && "Unexpected value!");

    // If there is no obvious reason to prefer the left-hand side over the
    // right-hand side, ensure the longest lived term is on the right-hand side,
    // so the shortest lived term will be replaced by the longest lived.
    // This tends to expose more simplifications.
    uint32_t LVN = VN.lookupOrAdd(LHS);
    if ((isa<Argument>(LHS) && isa<Argument>(RHS)) ||
        (isa<Instruction>(LHS) && isa<Instruction>(RHS))) {
      // Move the 'oldest' value to the right-hand side, using the value number
      // as a proxy for age.
      uint32_t RVN = VN.lookupOrAdd(RHS);
      if (LVN < RVN) {
        std::swap(LHS, RHS);
        LVN = RVN;
      }
    }

    // If value numbering later sees that an instruction in the scope is equal
    // to 'LHS' then ensure it will be turned into 'RHS'.  In order to preserve
    // the invariant that instructions only occur in the leader table for their
    // own value number (this is used by removeFromLeaderTable), do not do this
    // if RHS is an instruction (if an instruction in the scope is morphed into
    // LHS then it will be turned into RHS by the next GVN iteration anyway, so
    // using the leader table is about compiling faster, not optimizing better).
    // The leader table only tracks basic blocks, not edges. Only add to if we
    // have the simple case where the edge dominates the end.
    if (RootDominatesEnd && !isa<Instruction>(RHS))
      addToLeaderTable(LVN, RHS, Root.getEnd());

    // Replace all occurrences of 'LHS' with 'RHS' everywhere in the scope.  As
    // LHS always has at least one use that is not dominated by Root, this will
    // never do anything if LHS has only one use.
    if (!LHS->hasOneUse()) {
      unsigned NumReplacements =
          DominatesByEdge
              ? replaceDominatedUsesWith(LHS, RHS, *DT, Root)
              : replaceDominatedUsesWith(LHS, RHS, *DT, Root.getStart());

      Changed |= NumReplacements > 0;
      NumGVNEqProp += NumReplacements;
      // Cached information for anything that uses LHS will be invalid.
      if (MD)
        MD->invalidateCachedPointerInfo(LHS);
    }

    // Now try to deduce additional equalities from this one. For example, if
    // the known equality was "(A != B)" == "false" then it follows that A and B
    // are equal in the scope. Only boolean equalities with an explicit true or
    // false RHS are currently supported.
    if (!RHS->getType()->isIntegerTy(1))
      // Not a boolean equality - bail out.
      continue;
    ConstantInt *CI = dyn_cast<ConstantInt>(RHS);
    if (!CI)
      // RHS neither 'true' nor 'false' - bail out.
      continue;
    // Whether RHS equals 'true'.  Otherwise it equals 'false'.
    bool isKnownTrue = CI->isMinusOne();
    bool isKnownFalse = !isKnownTrue;

    // If "A && B" is known true then both A and B are known true.  If "A || B"
    // is known false then both A and B are known false.
    Value *A, *B;
    if ((isKnownTrue && match(LHS, m_And(m_Value(A), m_Value(B)))) ||
        (isKnownFalse && match(LHS, m_Or(m_Value(A), m_Value(B))))) {
      Worklist.push_back(std::make_pair(A, RHS));
      Worklist.push_back(std::make_pair(B, RHS));
      continue;
    }

    // If we are propagating an equality like "(A == B)" == "true" then also
    // propagate the equality A == B.  When propagating a comparison such as
    // "(A >= B)" == "true", replace all instances of "A < B" with "false".
    if (CmpInst *Cmp = dyn_cast<CmpInst>(LHS)) {
      Value *Op0 = Cmp->getOperand(0), *Op1 = Cmp->getOperand(1);

      // If "A == B" is known true, or "A != B" is known false, then replace
      // A with B everywhere in the scope.
      if ((isKnownTrue && Cmp->getPredicate() == CmpInst::ICMP_EQ) ||
          (isKnownFalse && Cmp->getPredicate() == CmpInst::ICMP_NE))
        Worklist.push_back(std::make_pair(Op0, Op1));

      // Handle the floating point versions of equality comparisons too.
      if ((isKnownTrue && Cmp->getPredicate() == CmpInst::FCMP_OEQ) ||
          (isKnownFalse && Cmp->getPredicate() == CmpInst::FCMP_UNE)) {

        // Floating point -0.0 and 0.0 compare equal, so we can only
        // propagate values if we know that we have a constant and that
        // its value is non-zero.

        // FIXME: We should do this optimization if 'no signed zeros' is
        // applicable via an instruction-level fast-math-flag or some other
        // indicator that relaxed FP semantics are being used.

        if (isa<ConstantFP>(Op1) && !cast<ConstantFP>(Op1)->isZero())
          Worklist.push_back(std::make_pair(Op0, Op1));
      }

      // If "A >= B" is known true, replace "A < B" with false everywhere.
      CmpInst::Predicate NotPred = Cmp->getInversePredicate();
      Constant *NotVal = ConstantInt::get(Cmp->getType(), isKnownFalse);
      // Since we don't have the instruction "A < B" immediately to hand, work
      // out the value number that it would have and use that to find an
      // appropriate instruction (if any).
      uint32_t NextNum = VN.getNextUnusedValueNumber();
      uint32_t Num = VN.lookupOrAddCmp(Cmp->getOpcode(), NotPred, Op0, Op1);
      // If the number we were assigned was brand new then there is no point in
      // looking for an instruction realizing it: there cannot be one!
      if (Num < NextNum) {
        Value *NotCmp = findLeader(Root.getEnd(), Num);
        if (NotCmp && isa<Instruction>(NotCmp)) {
          unsigned NumReplacements =
              DominatesByEdge
                  ? replaceDominatedUsesWith(NotCmp, NotVal, *DT, Root)
                  : replaceDominatedUsesWith(NotCmp, NotVal, *DT,
                                             Root.getStart());
          Changed |= NumReplacements > 0;
          NumGVNEqProp += NumReplacements;
          // Cached information for anything that uses NotCmp will be invalid.
          if (MD)
            MD->invalidateCachedPointerInfo(NotCmp);
        }
      }
      // Ensure that any instruction in scope that gets the "A < B" value number
      // is replaced with false.
      // The leader table only tracks basic blocks, not edges. Only add to if we
      // have the simple case where the edge dominates the end.
      if (RootDominatesEnd)
        addToLeaderTable(Num, NotVal, Root.getEnd());

      continue;
    }
  }

  return Changed;
}

/// When calculating availability, handle an instruction
/// by inserting it into the appropriate sets
bool GVN::processInstruction(Instruction *I) {
  // Ignore dbg info intrinsics.
  if (isa<DbgInfoIntrinsic>(I))
    return false;

  // If the instruction can be easily simplified then do so now in preference
  // to value numbering it.  Value numbering often exposes redundancies, for
  // example if it determines that %y is equal to %x then the instruction
  // "%z = and i32 %x, %y" becomes "%z = and i32 %x, %x" which we now simplify.
  const DataLayout &DL = I->getModule()->getDataLayout();
  if (Value *V = SimplifyInstruction(I, {DL, TLI, DT, AC})) {
    bool Changed = false;
    if (!I->use_empty()) {
      I->replaceAllUsesWith(V);
      Changed = true;
    }
    if (isInstructionTriviallyDead(I, TLI)) {
      markInstructionForDeletion(I);
      Changed = true;
    }
    if (Changed) {
      if (MD && V->getType()->isPtrOrPtrVectorTy())
        MD->invalidateCachedPointerInfo(V);
      ++NumGVNSimpl;
      return true;
    }
  }

  if (IntrinsicInst *IntrinsicI = dyn_cast<IntrinsicInst>(I))
    if (IntrinsicI->getIntrinsicID() == Intrinsic::assume)
      return processAssumeIntrinsic(IntrinsicI);

  if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
    if (processLoad(LI))
      return true;

    unsigned Num = VN.lookupOrAdd(LI);
    addToLeaderTable(Num, LI, LI->getParent());
    return false;
  }

  // For conditional branches, we can perform simple conditional propagation on
  // the condition value itself.
  if (BranchInst *BI = dyn_cast<BranchInst>(I)) {
    if (!BI->isConditional())
      return false;

    if (isa<Constant>(BI->getCondition()))
      return processFoldableCondBr(BI);

    Value *BranchCond = BI->getCondition();
    BasicBlock *TrueSucc = BI->getSuccessor(0);
    BasicBlock *FalseSucc = BI->getSuccessor(1);
    // Avoid multiple edges early.
    if (TrueSucc == FalseSucc)
      return false;

    BasicBlock *Parent = BI->getParent();
    bool Changed = false;

    Value *TrueVal = ConstantInt::getTrue(TrueSucc->getContext());
    BasicBlockEdge TrueE(Parent, TrueSucc);
    Changed |= propagateEquality(BranchCond, TrueVal, TrueE, true);

    Value *FalseVal = ConstantInt::getFalse(FalseSucc->getContext());
    BasicBlockEdge FalseE(Parent, FalseSucc);
    Changed |= propagateEquality(BranchCond, FalseVal, FalseE, true);

    return Changed;
  }

  // For switches, propagate the case values into the case destinations.
  if (SwitchInst *SI = dyn_cast<SwitchInst>(I)) {
    Value *SwitchCond = SI->getCondition();
    BasicBlock *Parent = SI->getParent();
    bool Changed = false;

    // Remember how many outgoing edges there are to every successor.
    SmallDenseMap<BasicBlock *, unsigned, 16> SwitchEdges;
    for (unsigned i = 0, n = SI->getNumSuccessors(); i != n; ++i)
      ++SwitchEdges[SI->getSuccessor(i)];

    for (SwitchInst::CaseIt i = SI->case_begin(), e = SI->case_end();
         i != e; ++i) {
      BasicBlock *Dst = i->getCaseSuccessor();
      // If there is only a single edge, propagate the case value into it.
      if (SwitchEdges.lookup(Dst) == 1) {
        BasicBlockEdge E(Parent, Dst);
        Changed |= propagateEquality(SwitchCond, i->getCaseValue(), E, true);
      }
    }
    return Changed;
  }

  // Instructions with void type don't return a value, so there's
  // no point in trying to find redundancies in them.
  if (I->getType()->isVoidTy())
    return false;

  uint32_t NextNum = VN.getNextUnusedValueNumber();
  unsigned Num = VN.lookupOrAdd(I);

  // Allocations are always uniquely numbered, so we can save time and memory
  // by fast failing them.
  if (isa<AllocaInst>(I) || I->isTerminator() || isa<PHINode>(I)) {
    addToLeaderTable(Num, I, I->getParent());
    return false;
  }

  // If the number we were assigned was a brand new VN, then we don't
  // need to do a lookup to see if the number already exists
  // somewhere in the domtree: it can't!
  if (Num >= NextNum) {
    addToLeaderTable(Num, I, I->getParent());
    return false;
  }

  // Perform fast-path value-number based elimination of values inherited from
  // dominators.
  Value *Repl = findLeader(I->getParent(), Num);
  if (!Repl) {
    // Failure, just remember this instance for future use.
    addToLeaderTable(Num, I, I->getParent());
    return false;
  } else if (Repl == I) {
    // If I was the result of a shortcut PRE, it might already be in the table
    // and the best replacement for itself. Nothing to do.
    return false;
  }

  // Remove it!
  patchAndReplaceAllUsesWith(I, Repl);
  if (MD && Repl->getType()->isPtrOrPtrVectorTy())
    MD->invalidateCachedPointerInfo(Repl);
  markInstructionForDeletion(I);
  return true;
}

/// runOnFunction - This is the main transformation entry point for a function.
bool GVN::runImpl(Function &F, AssumptionCache &RunAC, DominatorTree &RunDT,
                  const TargetLibraryInfo &RunTLI, AAResults &RunAA,
                  MemoryDependenceResults *RunMD, LoopInfo *LI,
                  OptimizationRemarkEmitter *RunORE) {
  AC = &RunAC;
  DT = &RunDT;
  VN.setDomTree(DT);
  TLI = &RunTLI;
  VN.setAliasAnalysis(&RunAA);
  MD = RunMD;
  ImplicitControlFlowTracking ImplicitCFT(DT);
  ICF = &ImplicitCFT;
  VN.setMemDep(MD);
  ORE = RunORE;
  InvalidBlockRPONumbers = true;

  bool Changed = false;
  bool ShouldContinue = true;

  DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Eager);
  // Merge unconditional branches, allowing PRE to catch more
  // optimization opportunities.
  for (Function::iterator FI = F.begin(), FE = F.end(); FI != FE; ) {
    BasicBlock *BB = &*FI++;

    bool removedBlock = MergeBlockIntoPredecessor(BB, &DTU, LI, nullptr, MD);
    if (removedBlock)
      ++NumGVNBlocks;

    Changed |= removedBlock;
  }

  unsigned Iteration = 0;
  while (ShouldContinue) {
    LLVM_DEBUG(dbgs() << "GVN iteration: " << Iteration << "\n");
    ShouldContinue = iterateOnFunction(F);
    Changed |= ShouldContinue;
    ++Iteration;
  }

  if (EnablePRE) {
    // Fabricate val-num for dead-code in order to suppress assertion in
    // performPRE().
    assignValNumForDeadCode();
    bool PREChanged = true;
    while (PREChanged) {
      PREChanged = performPRE(F);
      Changed |= PREChanged;
    }
  }

  // FIXME: Should perform GVN again after PRE does something.  PRE can move
  // computations into blocks where they become fully redundant.  Note that
  // we can't do this until PRE's critical edge splitting updates memdep.
  // Actually, when this happens, we should just fully integrate PRE into GVN.

  cleanupGlobalSets();
  // Do not cleanup DeadBlocks in cleanupGlobalSets() as it's called for each
  // iteration.
  DeadBlocks.clear();

  return Changed;
}

bool GVN::processBlock(BasicBlock *BB) {
  // FIXME: Kill off InstrsToErase by doing erasing eagerly in a helper function
  // (and incrementing BI before processing an instruction).
  assert(InstrsToErase.empty() &&
         "We expect InstrsToErase to be empty across iterations");
  if (DeadBlocks.count(BB))
    return false;

  // Clearing map before every BB because it can be used only for single BB.
  ReplaceWithConstMap.clear();
  bool ChangedFunction = false;

  for (BasicBlock::iterator BI = BB->begin(), BE = BB->end();
       BI != BE;) {
    if (!ReplaceWithConstMap.empty())
      ChangedFunction |= replaceOperandsWithConsts(&*BI);
    ChangedFunction |= processInstruction(&*BI);

    if (InstrsToErase.empty()) {
      ++BI;
      continue;
    }

    // If we need some instructions deleted, do it now.
    NumGVNInstr += InstrsToErase.size();

    // Avoid iterator invalidation.
    bool AtStart = BI == BB->begin();
    if (!AtStart)
      --BI;

    for (auto *I : InstrsToErase) {
      assert(I->getParent() == BB && "Removing instruction from wrong block?");
      LLVM_DEBUG(dbgs() << "GVN removed: " << *I << '\n');
      salvageDebugInfo(*I);
      if (MD) MD->removeInstruction(I);
      LLVM_DEBUG(verifyRemoved(I));
      ICF->removeInstruction(I);
      I->eraseFromParent();
    }
    InstrsToErase.clear();

    if (AtStart)
      BI = BB->begin();
    else
      ++BI;
  }

  return ChangedFunction;
}

// Instantiate an expression in a predecessor that lacked it.
bool GVN::performScalarPREInsertion(Instruction *Instr, BasicBlock *Pred,
                                    BasicBlock *Curr, unsigned int ValNo) {
  // Because we are going top-down through the block, all value numbers
  // will be available in the predecessor by the time we need them.  Any
  // that weren't originally present will have been instantiated earlier
  // in this loop.
  bool success = true;
  for (unsigned i = 0, e = Instr->getNumOperands(); i != e; ++i) {
    Value *Op = Instr->getOperand(i);
    if (isa<Argument>(Op) || isa<Constant>(Op) || isa<GlobalValue>(Op))
      continue;
    // This could be a newly inserted instruction, in which case, we won't
    // find a value number, and should give up before we hurt ourselves.
    // FIXME: Rewrite the infrastructure to let it easier to value number
    // and process newly inserted instructions.
    if (!VN.exists(Op)) {
      success = false;
      break;
    }
    uint32_t TValNo =
        VN.phiTranslate(Pred, Curr, VN.lookup(Op), *this);
    if (Value *V = findLeader(Pred, TValNo)) {
      Instr->setOperand(i, V);
    } else {
      success = false;
      break;
    }
  }

  // Fail out if we encounter an operand that is not available in
  // the PRE predecessor.  This is typically because of loads which
  // are not value numbered precisely.
  if (!success)
    return false;

  Instr->insertBefore(Pred->getTerminator());
  Instr->setName(Instr->getName() + ".pre");
  Instr->setDebugLoc(Instr->getDebugLoc());

  unsigned Num = VN.lookupOrAdd(Instr);
  VN.add(Instr, Num);

  // Update the availability map to include the new instruction.
  addToLeaderTable(Num, Instr, Pred);
  return true;
}

bool GVN::performScalarPRE(Instruction *CurInst) {
  if (isa<AllocaInst>(CurInst) || CurInst->isTerminator() ||
      isa<PHINode>(CurInst) || CurInst->getType()->isVoidTy() ||
      CurInst->mayReadFromMemory() || CurInst->mayHaveSideEffects() ||
      isa<DbgInfoIntrinsic>(CurInst))
    return false;

  // Don't do PRE on compares. The PHI would prevent CodeGenPrepare from
  // sinking the compare again, and it would force the code generator to
  // move the i1 from processor flags or predicate registers into a general
  // purpose register.
  if (isa<CmpInst>(CurInst))
    return false;

  // Don't do PRE on GEPs. The inserted PHI would prevent CodeGenPrepare from
  // sinking the addressing mode computation back to its uses. Extending the
  // GEP's live range increases the register pressure, and therefore it can
  // introduce unnecessary spills.
  //
  // This doesn't prevent Load PRE. PHI translation will make the GEP available
  // to the load by moving it to the predecessor block if necessary.
  if (isa<GetElementPtrInst>(CurInst))
    return false;

  // We don't currently value number ANY inline asm calls.
  if (CallInst *CallI = dyn_cast<CallInst>(CurInst))
    if (CallI->isInlineAsm())
      return false;

  uint32_t ValNo = VN.lookup(CurInst);

  // Look for the predecessors for PRE opportunities.  We're
  // only trying to solve the basic diamond case, where
  // a value is computed in the successor and one predecessor,
  // but not the other.  We also explicitly disallow cases
  // where the successor is its own predecessor, because they're
  // more complicated to get right.
  unsigned NumWith = 0;
  unsigned NumWithout = 0;
  BasicBlock *PREPred = nullptr;
  BasicBlock *CurrentBlock = CurInst->getParent();

  // Update the RPO numbers for this function.
  if (InvalidBlockRPONumbers)
    assignBlockRPONumber(*CurrentBlock->getParent());

  SmallVector<std::pair<Value *, BasicBlock *>, 8> predMap;
  for (BasicBlock *P : predecessors(CurrentBlock)) {
    // We're not interested in PRE where blocks with predecessors that are
    // not reachable.
    if (!DT->isReachableFromEntry(P)) {
      NumWithout = 2;
      break;
    }
    // It is not safe to do PRE when P->CurrentBlock is a loop backedge, and
    // when CurInst has operand defined in CurrentBlock (so it may be defined
    // by phi in the loop header).
    assert(BlockRPONumber.count(P) && BlockRPONumber.count(CurrentBlock) &&
           "Invalid BlockRPONumber map.");
    if (BlockRPONumber[P] >= BlockRPONumber[CurrentBlock] &&
        llvm::any_of(CurInst->operands(), [&](const Use &U) {
          if (auto *Inst = dyn_cast<Instruction>(U.get()))
            return Inst->getParent() == CurrentBlock;
          return false;
        })) {
      NumWithout = 2;
      break;
    }

    uint32_t TValNo = VN.phiTranslate(P, CurrentBlock, ValNo, *this);
    Value *predV = findLeader(P, TValNo);
    if (!predV) {
      predMap.push_back(std::make_pair(static_cast<Value *>(nullptr), P));
      PREPred = P;
      ++NumWithout;
    } else if (predV == CurInst) {
      /* CurInst dominates this predecessor. */
      NumWithout = 2;
      break;
    } else {
      predMap.push_back(std::make_pair(predV, P));
      ++NumWith;
    }
  }

  // Don't do PRE when it might increase code size, i.e. when
  // we would need to insert instructions in more than one pred.
  if (NumWithout > 1 || NumWith == 0)
    return false;

  // We may have a case where all predecessors have the instruction,
  // and we just need to insert a phi node. Otherwise, perform
  // insertion.
  Instruction *PREInstr = nullptr;

  if (NumWithout != 0) {
    if (!isSafeToSpeculativelyExecute(CurInst)) {
      // It is only valid to insert a new instruction if the current instruction
      // is always executed. An instruction with implicit control flow could
      // prevent us from doing it. If we cannot speculate the execution, then
      // PRE should be prohibited.
      if (ICF->isDominatedByICFIFromSameBlock(CurInst))
        return false;
    }

    // Don't do PRE across indirect branch.
    if (isa<IndirectBrInst>(PREPred->getTerminator()))
      return false;

    // We can't do PRE safely on a critical edge, so instead we schedule
    // the edge to be split and perform the PRE the next time we iterate
    // on the function.
    unsigned SuccNum = GetSuccessorNumber(PREPred, CurrentBlock);
    if (isCriticalEdge(PREPred->getTerminator(), SuccNum)) {
      toSplit.push_back(std::make_pair(PREPred->getTerminator(), SuccNum));
      return false;
    }
    // We need to insert somewhere, so let's give it a shot
    PREInstr = CurInst->clone();
    if (!performScalarPREInsertion(PREInstr, PREPred, CurrentBlock, ValNo)) {
      // If we failed insertion, make sure we remove the instruction.
      LLVM_DEBUG(verifyRemoved(PREInstr));
      PREInstr->deleteValue();
      return false;
    }
  }

  // Either we should have filled in the PRE instruction, or we should
  // not have needed insertions.
  assert(PREInstr != nullptr || NumWithout == 0);

  ++NumGVNPRE;

  // Create a PHI to make the value available in this block.
  PHINode *Phi =
      PHINode::Create(CurInst->getType(), predMap.size(),
                      CurInst->getName() + ".pre-phi", &CurrentBlock->front());
  for (unsigned i = 0, e = predMap.size(); i != e; ++i) {
    if (Value *V = predMap[i].first) {
      // If we use an existing value in this phi, we have to patch the original
      // value because the phi will be used to replace a later value.
      patchReplacementInstruction(CurInst, V);
      Phi->addIncoming(V, predMap[i].second);
    } else
      Phi->addIncoming(PREInstr, PREPred);
  }

  VN.add(Phi, ValNo);
  // After creating a new PHI for ValNo, the phi translate result for ValNo will
  // be changed, so erase the related stale entries in phi translate cache.
  VN.eraseTranslateCacheEntry(ValNo, *CurrentBlock);
  addToLeaderTable(ValNo, Phi, CurrentBlock);
  Phi->setDebugLoc(CurInst->getDebugLoc());
  CurInst->replaceAllUsesWith(Phi);
  if (MD && Phi->getType()->isPtrOrPtrVectorTy())
    MD->invalidateCachedPointerInfo(Phi);
  VN.erase(CurInst);
  removeFromLeaderTable(ValNo, CurInst, CurrentBlock);

  LLVM_DEBUG(dbgs() << "GVN PRE removed: " << *CurInst << '\n');
  if (MD)
    MD->removeInstruction(CurInst);
  LLVM_DEBUG(verifyRemoved(CurInst));
  // FIXME: Intended to be markInstructionForDeletion(CurInst), but it causes
  // some assertion failures.
  ICF->removeInstruction(CurInst);
  CurInst->eraseFromParent();
  ++NumGVNInstr;

  return true;
}

/// Perform a purely local form of PRE that looks for diamond
/// control flow patterns and attempts to perform simple PRE at the join point.
bool GVN::performPRE(Function &F) {
  bool Changed = false;
  for (BasicBlock *CurrentBlock : depth_first(&F.getEntryBlock())) {
    // Nothing to PRE in the entry block.
    if (CurrentBlock == &F.getEntryBlock())
      continue;

    // Don't perform PRE on an EH pad.
    if (CurrentBlock->isEHPad())
      continue;

    for (BasicBlock::iterator BI = CurrentBlock->begin(),
                              BE = CurrentBlock->end();
         BI != BE;) {
      Instruction *CurInst = &*BI++;
      Changed |= performScalarPRE(CurInst);
    }
  }

  if (splitCriticalEdges())
    Changed = true;

  return Changed;
}

/// Split the critical edge connecting the given two blocks, and return
/// the block inserted to the critical edge.
BasicBlock *GVN::splitCriticalEdges(BasicBlock *Pred, BasicBlock *Succ) {
  BasicBlock *BB =
      SplitCriticalEdge(Pred, Succ, CriticalEdgeSplittingOptions(DT));
  if (MD)
    MD->invalidateCachedPredecessors();
  InvalidBlockRPONumbers = true;
  return BB;
}

/// Split critical edges found during the previous
/// iteration that may enable further optimization.
bool GVN::splitCriticalEdges() {
  if (toSplit.empty())
    return false;
  do {
    std::pair<Instruction *, unsigned> Edge = toSplit.pop_back_val();
    SplitCriticalEdge(Edge.first, Edge.second,
                      CriticalEdgeSplittingOptions(DT));
  } while (!toSplit.empty());
  if (MD) MD->invalidateCachedPredecessors();
  InvalidBlockRPONumbers = true;
  return true;
}

/// Executes one iteration of GVN
bool GVN::iterateOnFunction(Function &F) {
  cleanupGlobalSets();

  // Top-down walk of the dominator tree
  bool Changed = false;
  // Needed for value numbering with phi construction to work.
  // RPOT walks the graph in its constructor and will not be invalidated during
  // processBlock.
  ReversePostOrderTraversal<Function *> RPOT(&F);

  for (BasicBlock *BB : RPOT)
    Changed |= processBlock(BB);

  return Changed;
}

void GVN::cleanupGlobalSets() {
  VN.clear();
  LeaderTable.clear();
  BlockRPONumber.clear();
  TableAllocator.Reset();
  ICF->clear();
  InvalidBlockRPONumbers = true;
}

/// Verify that the specified instruction does not occur in our
/// internal data structures.
void GVN::verifyRemoved(const Instruction *Inst) const {
  VN.verifyRemoved(Inst);

  // Walk through the value number scope to make sure the instruction isn't
  // ferreted away in it.
  for (DenseMap<uint32_t, LeaderTableEntry>::const_iterator
       I = LeaderTable.begin(), E = LeaderTable.end(); I != E; ++I) {
    const LeaderTableEntry *Node = &I->second;
    assert(Node->Val != Inst && "Inst still in value numbering scope!");

    while (Node->Next) {
      Node = Node->Next;
      assert(Node->Val != Inst && "Inst still in value numbering scope!");
    }
  }
}

/// BB is declared dead, which implied other blocks become dead as well. This
/// function is to add all these blocks to "DeadBlocks". For the dead blocks'
/// live successors, update their phi nodes by replacing the operands
/// corresponding to dead blocks with UndefVal.
void GVN::addDeadBlock(BasicBlock *BB) {
  SmallVector<BasicBlock *, 4> NewDead;
  SmallSetVector<BasicBlock *, 4> DF;

  NewDead.push_back(BB);
  while (!NewDead.empty()) {
    BasicBlock *D = NewDead.pop_back_val();
    if (DeadBlocks.count(D))
      continue;

    // All blocks dominated by D are dead.
    SmallVector<BasicBlock *, 8> Dom;
    DT->getDescendants(D, Dom);
    DeadBlocks.insert(Dom.begin(), Dom.end());

    // Figure out the dominance-frontier(D).
    for (BasicBlock *B : Dom) {
      for (BasicBlock *S : successors(B)) {
        if (DeadBlocks.count(S))
          continue;

        bool AllPredDead = true;
        for (BasicBlock *P : predecessors(S))
          if (!DeadBlocks.count(P)) {
            AllPredDead = false;
            break;
          }

        if (!AllPredDead) {
          // S could be proved dead later on. That is why we don't update phi
          // operands at this moment.
          DF.insert(S);
        } else {
          // While S is not dominated by D, it is dead by now. This could take
          // place if S already have a dead predecessor before D is declared
          // dead.
          NewDead.push_back(S);
        }
      }
    }
  }

  // For the dead blocks' live successors, update their phi nodes by replacing
  // the operands corresponding to dead blocks with UndefVal.
  for(SmallSetVector<BasicBlock *, 4>::iterator I = DF.begin(), E = DF.end();
        I != E; I++) {
    BasicBlock *B = *I;
    if (DeadBlocks.count(B))
      continue;

    SmallVector<BasicBlock *, 4> Preds(pred_begin(B), pred_end(B));
    for (BasicBlock *P : Preds) {
      if (!DeadBlocks.count(P))
        continue;

      if (isCriticalEdge(P->getTerminator(), GetSuccessorNumber(P, B))) {
        if (BasicBlock *S = splitCriticalEdges(P, B))
          DeadBlocks.insert(P = S);
      }

      for (BasicBlock::iterator II = B->begin(); isa<PHINode>(II); ++II) {
        PHINode &Phi = cast<PHINode>(*II);
        Phi.setIncomingValue(Phi.getBasicBlockIndex(P),
                             UndefValue::get(Phi.getType()));
        if (MD)
          MD->invalidateCachedPointerInfo(&Phi);
      }
    }
  }
}

// If the given branch is recognized as a foldable branch (i.e. conditional
// branch with constant condition), it will perform following analyses and
// transformation.
//  1) If the dead out-coming edge is a critical-edge, split it. Let
//     R be the target of the dead out-coming edge.
//  1) Identify the set of dead blocks implied by the branch's dead outcoming
//     edge. The result of this step will be {X| X is dominated by R}
//  2) Identify those blocks which haves at least one dead predecessor. The
//     result of this step will be dominance-frontier(R).
//  3) Update the PHIs in DF(R) by replacing the operands corresponding to
//     dead blocks with "UndefVal" in an hope these PHIs will optimized away.
//
// Return true iff *NEW* dead code are found.
bool GVN::processFoldableCondBr(BranchInst *BI) {
  if (!BI || BI->isUnconditional())
    return false;

  // If a branch has two identical successors, we cannot declare either dead.
  if (BI->getSuccessor(0) == BI->getSuccessor(1))
    return false;

  ConstantInt *Cond = dyn_cast<ConstantInt>(BI->getCondition());
  if (!Cond)
    return false;

  BasicBlock *DeadRoot =
      Cond->getZExtValue() ? BI->getSuccessor(1) : BI->getSuccessor(0);
  if (DeadBlocks.count(DeadRoot))
    return false;

  if (!DeadRoot->getSinglePredecessor())
    DeadRoot = splitCriticalEdges(BI->getParent(), DeadRoot);

  addDeadBlock(DeadRoot);
  return true;
}

// performPRE() will trigger assert if it comes across an instruction without
// associated val-num. As it normally has far more live instructions than dead
// instructions, it makes more sense just to "fabricate" a val-number for the
// dead code than checking if instruction involved is dead or not.
void GVN::assignValNumForDeadCode() {
  for (BasicBlock *BB : DeadBlocks) {
    for (Instruction &Inst : *BB) {
      unsigned ValNum = VN.lookupOrAdd(&Inst);
      addToLeaderTable(ValNum, &Inst, BB);
    }
  }
}

class llvm::gvn::GVNLegacyPass : public FunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid

  explicit GVNLegacyPass(bool NoMemDepAnalysis = !EnableMemDep)
      : FunctionPass(ID), NoMemDepAnalysis(NoMemDepAnalysis) {
    initializeGVNLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    if (skipFunction(F))
      return false;

    auto *LIWP = getAnalysisIfAvailable<LoopInfoWrapperPass>();

    return Impl.runImpl(
        F, getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F),
        getAnalysis<DominatorTreeWrapperPass>().getDomTree(),
        getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(),
        getAnalysis<AAResultsWrapperPass>().getAAResults(),
        NoMemDepAnalysis ? nullptr
                : &getAnalysis<MemoryDependenceWrapperPass>().getMemDep(),
        LIWP ? &LIWP->getLoopInfo() : nullptr,
        &getAnalysis<OptimizationRemarkEmitterWrapperPass>().getORE());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    if (!NoMemDepAnalysis)
      AU.addRequired<MemoryDependenceWrapperPass>();
    AU.addRequired<AAResultsWrapperPass>();

    AU.addPreserved<DominatorTreeWrapperPass>();
    AU.addPreserved<GlobalsAAWrapperPass>();
    AU.addPreserved<TargetLibraryInfoWrapperPass>();
    AU.addRequired<OptimizationRemarkEmitterWrapperPass>();
  }

private:
  bool NoMemDepAnalysis;
  GVN Impl;
};

char GVNLegacyPass::ID = 0;

INITIALIZE_PASS_BEGIN(GVNLegacyPass, "gvn", "Global Value Numbering", false, false)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(MemoryDependenceWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(GlobalsAAWrapperPass)
INITIALIZE_PASS_DEPENDENCY(OptimizationRemarkEmitterWrapperPass)
INITIALIZE_PASS_END(GVNLegacyPass, "gvn", "Global Value Numbering", false, false)

// The public interface to this file...
FunctionPass *llvm::createGVNPass(bool NoMemDepAnalysis) {
  return new GVNLegacyPass(NoMemDepAnalysis);
}
