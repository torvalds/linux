//===- CalledValuePropagation.cpp - Propagate called values -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a transformation that attaches !callees metadata to
// indirect call sites. For a given call site, the metadata, if present,
// indicates the set of functions the call site could possibly target at
// run-time. This metadata is added to indirect call sites when the set of
// possible targets can be determined by analysis and is known to be small. The
// analysis driving the transformation is similar to constant propagation and
// makes uses of the generic sparse propagation solver.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/CalledValuePropagation.h"
#include "llvm/Analysis/SparsePropagation.h"
#include "llvm/Analysis/ValueLatticeUtils.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/Transforms/IPO.h"
using namespace llvm;

#define DEBUG_TYPE "called-value-propagation"

/// The maximum number of functions to track per lattice value. Once the number
/// of functions a call site can possibly target exceeds this threshold, it's
/// lattice value becomes overdefined. The number of possible lattice values is
/// bounded by Ch(F, M), where F is the number of functions in the module and M
/// is MaxFunctionsPerValue. As such, this value should be kept very small. We
/// likely can't do anything useful for call sites with a large number of
/// possible targets, anyway.
static cl::opt<unsigned> MaxFunctionsPerValue(
    "cvp-max-functions-per-value", cl::Hidden, cl::init(4),
    cl::desc("The maximum number of functions to track per lattice value"));

namespace {
/// To enable interprocedural analysis, we assign LLVM values to the following
/// groups. The register group represents SSA registers, the return group
/// represents the return values of functions, and the memory group represents
/// in-memory values. An LLVM Value can technically be in more than one group.
/// It's necessary to distinguish these groups so we can, for example, track a
/// global variable separately from the value stored at its location.
enum class IPOGrouping { Register, Return, Memory };

/// Our LatticeKeys are PointerIntPairs composed of LLVM values and groupings.
using CVPLatticeKey = PointerIntPair<Value *, 2, IPOGrouping>;

/// The lattice value type used by our custom lattice function. It holds the
/// lattice state, and a set of functions.
class CVPLatticeVal {
public:
  /// The states of the lattice values. Only the FunctionSet state is
  /// interesting. It indicates the set of functions to which an LLVM value may
  /// refer.
  enum CVPLatticeStateTy { Undefined, FunctionSet, Overdefined, Untracked };

  /// Comparator for sorting the functions set. We want to keep the order
  /// deterministic for testing, etc.
  struct Compare {
    bool operator()(const Function *LHS, const Function *RHS) const {
      return LHS->getName() < RHS->getName();
    }
  };

  CVPLatticeVal() : LatticeState(Undefined) {}
  CVPLatticeVal(CVPLatticeStateTy LatticeState) : LatticeState(LatticeState) {}
  CVPLatticeVal(std::vector<Function *> &&Functions)
      : LatticeState(FunctionSet), Functions(std::move(Functions)) {
    assert(std::is_sorted(this->Functions.begin(), this->Functions.end(),
                          Compare()));
  }

  /// Get a reference to the functions held by this lattice value. The number
  /// of functions will be zero for states other than FunctionSet.
  const std::vector<Function *> &getFunctions() const {
    return Functions;
  }

  /// Returns true if the lattice value is in the FunctionSet state.
  bool isFunctionSet() const { return LatticeState == FunctionSet; }

  bool operator==(const CVPLatticeVal &RHS) const {
    return LatticeState == RHS.LatticeState && Functions == RHS.Functions;
  }

  bool operator!=(const CVPLatticeVal &RHS) const {
    return LatticeState != RHS.LatticeState || Functions != RHS.Functions;
  }

private:
  /// Holds the state this lattice value is in.
  CVPLatticeStateTy LatticeState;

  /// Holds functions indicating the possible targets of call sites. This set
  /// is empty for lattice values in the undefined, overdefined, and untracked
  /// states. The maximum size of the set is controlled by
  /// MaxFunctionsPerValue. Since most LLVM values are expected to be in
  /// uninteresting states (i.e., overdefined), CVPLatticeVal objects should be
  /// small and efficiently copyable.
  // FIXME: This could be a TinyPtrVector and/or merge with LatticeState.
  std::vector<Function *> Functions;
};

/// The custom lattice function used by the generic sparse propagation solver.
/// It handles merging lattice values and computing new lattice values for
/// constants, arguments, values returned from trackable functions, and values
/// located in trackable global variables. It also computes the lattice values
/// that change as a result of executing instructions.
class CVPLatticeFunc
    : public AbstractLatticeFunction<CVPLatticeKey, CVPLatticeVal> {
public:
  CVPLatticeFunc()
      : AbstractLatticeFunction(CVPLatticeVal(CVPLatticeVal::Undefined),
                                CVPLatticeVal(CVPLatticeVal::Overdefined),
                                CVPLatticeVal(CVPLatticeVal::Untracked)) {}

  /// Compute and return a CVPLatticeVal for the given CVPLatticeKey.
  CVPLatticeVal ComputeLatticeVal(CVPLatticeKey Key) override {
    switch (Key.getInt()) {
    case IPOGrouping::Register:
      if (isa<Instruction>(Key.getPointer())) {
        return getUndefVal();
      } else if (auto *A = dyn_cast<Argument>(Key.getPointer())) {
        if (canTrackArgumentsInterprocedurally(A->getParent()))
          return getUndefVal();
      } else if (auto *C = dyn_cast<Constant>(Key.getPointer())) {
        return computeConstant(C);
      }
      return getOverdefinedVal();
    case IPOGrouping::Memory:
    case IPOGrouping::Return:
      if (auto *GV = dyn_cast<GlobalVariable>(Key.getPointer())) {
        if (canTrackGlobalVariableInterprocedurally(GV))
          return computeConstant(GV->getInitializer());
      } else if (auto *F = cast<Function>(Key.getPointer()))
        if (canTrackReturnsInterprocedurally(F))
          return getUndefVal();
    }
    return getOverdefinedVal();
  }

  /// Merge the two given lattice values. The interesting cases are merging two
  /// FunctionSet values and a FunctionSet value with an Undefined value. For
  /// these cases, we simply union the function sets. If the size of the union
  /// is greater than the maximum functions we track, the merged value is
  /// overdefined.
  CVPLatticeVal MergeValues(CVPLatticeVal X, CVPLatticeVal Y) override {
    if (X == getOverdefinedVal() || Y == getOverdefinedVal())
      return getOverdefinedVal();
    if (X == getUndefVal() && Y == getUndefVal())
      return getUndefVal();
    std::vector<Function *> Union;
    std::set_union(X.getFunctions().begin(), X.getFunctions().end(),
                   Y.getFunctions().begin(), Y.getFunctions().end(),
                   std::back_inserter(Union), CVPLatticeVal::Compare{});
    if (Union.size() > MaxFunctionsPerValue)
      return getOverdefinedVal();
    return CVPLatticeVal(std::move(Union));
  }

  /// Compute the lattice values that change as a result of executing the given
  /// instruction. The changed values are stored in \p ChangedValues. We handle
  /// just a few kinds of instructions since we're only propagating values that
  /// can be called.
  void ComputeInstructionState(
      Instruction &I, DenseMap<CVPLatticeKey, CVPLatticeVal> &ChangedValues,
      SparseSolver<CVPLatticeKey, CVPLatticeVal> &SS) override {
    switch (I.getOpcode()) {
    case Instruction::Call:
      return visitCallSite(cast<CallInst>(&I), ChangedValues, SS);
    case Instruction::Invoke:
      return visitCallSite(cast<InvokeInst>(&I), ChangedValues, SS);
    case Instruction::Load:
      return visitLoad(*cast<LoadInst>(&I), ChangedValues, SS);
    case Instruction::Ret:
      return visitReturn(*cast<ReturnInst>(&I), ChangedValues, SS);
    case Instruction::Select:
      return visitSelect(*cast<SelectInst>(&I), ChangedValues, SS);
    case Instruction::Store:
      return visitStore(*cast<StoreInst>(&I), ChangedValues, SS);
    default:
      return visitInst(I, ChangedValues, SS);
    }
  }

  /// Print the given CVPLatticeVal to the specified stream.
  void PrintLatticeVal(CVPLatticeVal LV, raw_ostream &OS) override {
    if (LV == getUndefVal())
      OS << "Undefined  ";
    else if (LV == getOverdefinedVal())
      OS << "Overdefined";
    else if (LV == getUntrackedVal())
      OS << "Untracked  ";
    else
      OS << "FunctionSet";
  }

  /// Print the given CVPLatticeKey to the specified stream.
  void PrintLatticeKey(CVPLatticeKey Key, raw_ostream &OS) override {
    if (Key.getInt() == IPOGrouping::Register)
      OS << "<reg> ";
    else if (Key.getInt() == IPOGrouping::Memory)
      OS << "<mem> ";
    else if (Key.getInt() == IPOGrouping::Return)
      OS << "<ret> ";
    if (isa<Function>(Key.getPointer()))
      OS << Key.getPointer()->getName();
    else
      OS << *Key.getPointer();
  }

  /// We collect a set of indirect calls when visiting call sites. This method
  /// returns a reference to that set.
  SmallPtrSetImpl<Instruction *> &getIndirectCalls() { return IndirectCalls; }

private:
  /// Holds the indirect calls we encounter during the analysis. We will attach
  /// metadata to these calls after the analysis indicating the functions the
  /// calls can possibly target.
  SmallPtrSet<Instruction *, 32> IndirectCalls;

  /// Compute a new lattice value for the given constant. The constant, after
  /// stripping any pointer casts, should be a Function. We ignore null
  /// pointers as an optimization, since calling these values is undefined
  /// behavior.
  CVPLatticeVal computeConstant(Constant *C) {
    if (isa<ConstantPointerNull>(C))
      return CVPLatticeVal(CVPLatticeVal::FunctionSet);
    if (auto *F = dyn_cast<Function>(C->stripPointerCasts()))
      return CVPLatticeVal({F});
    return getOverdefinedVal();
  }

  /// Handle return instructions. The function's return state is the merge of
  /// the returned value state and the function's return state.
  void visitReturn(ReturnInst &I,
                   DenseMap<CVPLatticeKey, CVPLatticeVal> &ChangedValues,
                   SparseSolver<CVPLatticeKey, CVPLatticeVal> &SS) {
    Function *F = I.getParent()->getParent();
    if (F->getReturnType()->isVoidTy())
      return;
    auto RegI = CVPLatticeKey(I.getReturnValue(), IPOGrouping::Register);
    auto RetF = CVPLatticeKey(F, IPOGrouping::Return);
    ChangedValues[RetF] =
        MergeValues(SS.getValueState(RegI), SS.getValueState(RetF));
  }

  /// Handle call sites. The state of a called function's formal arguments is
  /// the merge of the argument state with the call sites corresponding actual
  /// argument state. The call site state is the merge of the call site state
  /// with the returned value state of the called function.
  void visitCallSite(CallSite CS,
                     DenseMap<CVPLatticeKey, CVPLatticeVal> &ChangedValues,
                     SparseSolver<CVPLatticeKey, CVPLatticeVal> &SS) {
    Function *F = CS.getCalledFunction();
    Instruction *I = CS.getInstruction();
    auto RegI = CVPLatticeKey(I, IPOGrouping::Register);

    // If this is an indirect call, save it so we can quickly revisit it when
    // attaching metadata.
    if (!F)
      IndirectCalls.insert(I);

    // If we can't track the function's return values, there's nothing to do.
    if (!F || !canTrackReturnsInterprocedurally(F)) {
      // Void return, No need to create and update CVPLattice state as no one
      // can use it.
      if (I->getType()->isVoidTy())
        return;
      ChangedValues[RegI] = getOverdefinedVal();
      return;
    }

    // Inform the solver that the called function is executable, and perform
    // the merges for the arguments and return value.
    SS.MarkBlockExecutable(&F->front());
    auto RetF = CVPLatticeKey(F, IPOGrouping::Return);
    for (Argument &A : F->args()) {
      auto RegFormal = CVPLatticeKey(&A, IPOGrouping::Register);
      auto RegActual =
          CVPLatticeKey(CS.getArgument(A.getArgNo()), IPOGrouping::Register);
      ChangedValues[RegFormal] =
          MergeValues(SS.getValueState(RegFormal), SS.getValueState(RegActual));
    }

    // Void return, No need to create and update CVPLattice state as no one can
    // use it.
    if (I->getType()->isVoidTy())
      return;

    ChangedValues[RegI] =
        MergeValues(SS.getValueState(RegI), SS.getValueState(RetF));
  }

  /// Handle select instructions. The select instruction state is the merge the
  /// true and false value states.
  void visitSelect(SelectInst &I,
                   DenseMap<CVPLatticeKey, CVPLatticeVal> &ChangedValues,
                   SparseSolver<CVPLatticeKey, CVPLatticeVal> &SS) {
    auto RegI = CVPLatticeKey(&I, IPOGrouping::Register);
    auto RegT = CVPLatticeKey(I.getTrueValue(), IPOGrouping::Register);
    auto RegF = CVPLatticeKey(I.getFalseValue(), IPOGrouping::Register);
    ChangedValues[RegI] =
        MergeValues(SS.getValueState(RegT), SS.getValueState(RegF));
  }

  /// Handle load instructions. If the pointer operand of the load is a global
  /// variable, we attempt to track the value. The loaded value state is the
  /// merge of the loaded value state with the global variable state.
  void visitLoad(LoadInst &I,
                 DenseMap<CVPLatticeKey, CVPLatticeVal> &ChangedValues,
                 SparseSolver<CVPLatticeKey, CVPLatticeVal> &SS) {
    auto RegI = CVPLatticeKey(&I, IPOGrouping::Register);
    if (auto *GV = dyn_cast<GlobalVariable>(I.getPointerOperand())) {
      auto MemGV = CVPLatticeKey(GV, IPOGrouping::Memory);
      ChangedValues[RegI] =
          MergeValues(SS.getValueState(RegI), SS.getValueState(MemGV));
    } else {
      ChangedValues[RegI] = getOverdefinedVal();
    }
  }

  /// Handle store instructions. If the pointer operand of the store is a
  /// global variable, we attempt to track the value. The global variable state
  /// is the merge of the stored value state with the global variable state.
  void visitStore(StoreInst &I,
                  DenseMap<CVPLatticeKey, CVPLatticeVal> &ChangedValues,
                  SparseSolver<CVPLatticeKey, CVPLatticeVal> &SS) {
    auto *GV = dyn_cast<GlobalVariable>(I.getPointerOperand());
    if (!GV)
      return;
    auto RegI = CVPLatticeKey(I.getValueOperand(), IPOGrouping::Register);
    auto MemGV = CVPLatticeKey(GV, IPOGrouping::Memory);
    ChangedValues[MemGV] =
        MergeValues(SS.getValueState(RegI), SS.getValueState(MemGV));
  }

  /// Handle all other instructions. All other instructions are marked
  /// overdefined.
  void visitInst(Instruction &I,
                 DenseMap<CVPLatticeKey, CVPLatticeVal> &ChangedValues,
                 SparseSolver<CVPLatticeKey, CVPLatticeVal> &SS) {
    // Simply bail if this instruction has no user.
    if (I.use_empty())
      return;
    auto RegI = CVPLatticeKey(&I, IPOGrouping::Register);
    ChangedValues[RegI] = getOverdefinedVal();
  }
};
} // namespace

namespace llvm {
/// A specialization of LatticeKeyInfo for CVPLatticeKeys. The generic solver
/// must translate between LatticeKeys and LLVM Values when adding Values to
/// its work list and inspecting the state of control-flow related values.
template <> struct LatticeKeyInfo<CVPLatticeKey> {
  static inline Value *getValueFromLatticeKey(CVPLatticeKey Key) {
    return Key.getPointer();
  }
  static inline CVPLatticeKey getLatticeKeyFromValue(Value *V) {
    return CVPLatticeKey(V, IPOGrouping::Register);
  }
};
} // namespace llvm

static bool runCVP(Module &M) {
  // Our custom lattice function and generic sparse propagation solver.
  CVPLatticeFunc Lattice;
  SparseSolver<CVPLatticeKey, CVPLatticeVal> Solver(&Lattice);

  // For each function in the module, if we can't track its arguments, let the
  // generic solver assume it is executable.
  for (Function &F : M)
    if (!F.isDeclaration() && !canTrackArgumentsInterprocedurally(&F))
      Solver.MarkBlockExecutable(&F.front());

  // Solver our custom lattice. In doing so, we will also build a set of
  // indirect call sites.
  Solver.Solve();

  // Attach metadata to the indirect call sites that were collected indicating
  // the set of functions they can possibly target.
  bool Changed = false;
  MDBuilder MDB(M.getContext());
  for (Instruction *C : Lattice.getIndirectCalls()) {
    CallSite CS(C);
    auto RegI = CVPLatticeKey(CS.getCalledValue(), IPOGrouping::Register);
    CVPLatticeVal LV = Solver.getExistingValueState(RegI);
    if (!LV.isFunctionSet() || LV.getFunctions().empty())
      continue;
    MDNode *Callees = MDB.createCallees(LV.getFunctions());
    C->setMetadata(LLVMContext::MD_callees, Callees);
    Changed = true;
  }

  return Changed;
}

PreservedAnalyses CalledValuePropagationPass::run(Module &M,
                                                  ModuleAnalysisManager &) {
  runCVP(M);
  return PreservedAnalyses::all();
}

namespace {
class CalledValuePropagationLegacyPass : public ModulePass {
public:
  static char ID;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  CalledValuePropagationLegacyPass() : ModulePass(ID) {
    initializeCalledValuePropagationLegacyPassPass(
        *PassRegistry::getPassRegistry());
  }

  bool runOnModule(Module &M) override {
    if (skipModule(M))
      return false;
    return runCVP(M);
  }
};
} // namespace

char CalledValuePropagationLegacyPass::ID = 0;
INITIALIZE_PASS(CalledValuePropagationLegacyPass, "called-value-propagation",
                "Called Value Propagation", false, false)

ModulePass *llvm::createCalledValuePropagationPass() {
  return new CalledValuePropagationLegacyPass();
}
