//===- Evaluator.h - LLVM IR evaluator --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Function evaluator for LLVM IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_EVALUATOR_H
#define LLVM_TRANSFORMS_UTILS_EVALUATOR_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <deque>
#include <memory>

namespace llvm {

class DataLayout;
class Function;
class TargetLibraryInfo;

/// This class evaluates LLVM IR, producing the Constant representing each SSA
/// instruction.  Changes to global variables are stored in a mapping that can
/// be iterated over after the evaluation is complete.  Once an evaluation call
/// fails, the evaluation object should not be reused.
class Evaluator {
public:
  Evaluator(const DataLayout &DL, const TargetLibraryInfo *TLI)
      : DL(DL), TLI(TLI) {
    ValueStack.emplace_back();
  }

  ~Evaluator() {
    for (auto &Tmp : AllocaTmps)
      // If there are still users of the alloca, the program is doing something
      // silly, e.g. storing the address of the alloca somewhere and using it
      // later.  Since this is undefined, we'll just make it be null.
      if (!Tmp->use_empty())
        Tmp->replaceAllUsesWith(Constant::getNullValue(Tmp->getType()));
  }

  /// Evaluate a call to function F, returning true if successful, false if we
  /// can't evaluate it.  ActualArgs contains the formal arguments for the
  /// function.
  bool EvaluateFunction(Function *F, Constant *&RetVal,
                        const SmallVectorImpl<Constant*> &ActualArgs);

  /// Evaluate all instructions in block BB, returning true if successful, false
  /// if we can't evaluate it.  NewBB returns the next BB that control flows
  /// into, or null upon return.
  bool EvaluateBlock(BasicBlock::iterator CurInst, BasicBlock *&NextBB);

  Constant *getVal(Value *V) {
    if (Constant *CV = dyn_cast<Constant>(V)) return CV;
    Constant *R = ValueStack.back().lookup(V);
    assert(R && "Reference to an uncomputed value!");
    return R;
  }

  void setVal(Value *V, Constant *C) {
    ValueStack.back()[V] = C;
  }

  /// Given call site return callee and list of its formal arguments
  Function *getCalleeWithFormalArgs(CallSite &CS,
                                    SmallVector<Constant *, 8> &Formals);

  /// Given call site and callee returns list of callee formal argument
  /// values converting them when necessary
  bool getFormalParams(CallSite &CS, Function *F,
                       SmallVector<Constant *, 8> &Formals);

  /// Casts call result to a type of bitcast call expression
  Constant *castCallResultIfNeeded(Value *CallExpr, Constant *RV);

  const DenseMap<Constant*, Constant*> &getMutatedMemory() const {
    return MutatedMemory;
  }

  const SmallPtrSetImpl<GlobalVariable*> &getInvariants() const {
    return Invariants;
  }

private:
  Constant *ComputeLoadResult(Constant *P);

  /// As we compute SSA register values, we store their contents here. The back
  /// of the deque contains the current function and the stack contains the
  /// values in the calling frames.
  std::deque<DenseMap<Value*, Constant*>> ValueStack;

  /// This is used to detect recursion.  In pathological situations we could hit
  /// exponential behavior, but at least there is nothing unbounded.
  SmallVector<Function*, 4> CallStack;

  /// For each store we execute, we update this map.  Loads check this to get
  /// the most up-to-date value.  If evaluation is successful, this state is
  /// committed to the process.
  DenseMap<Constant*, Constant*> MutatedMemory;

  /// To 'execute' an alloca, we create a temporary global variable to represent
  /// its body.  This vector is needed so we can delete the temporary globals
  /// when we are done.
  SmallVector<std::unique_ptr<GlobalVariable>, 32> AllocaTmps;

  /// These global variables have been marked invariant by the static
  /// constructor.
  SmallPtrSet<GlobalVariable*, 8> Invariants;

  /// These are constants we have checked and know to be simple enough to live
  /// in a static initializer of a global.
  SmallPtrSet<Constant*, 8> SimpleConstants;

  const DataLayout &DL;
  const TargetLibraryInfo *TLI;
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_EVALUATOR_H
