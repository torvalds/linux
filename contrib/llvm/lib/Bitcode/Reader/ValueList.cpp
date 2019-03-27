//===- ValueList.cpp - Internal BitcodeReader implementation --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ValueList.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <utility>

using namespace llvm;

namespace llvm {

namespace {

/// A class for maintaining the slot number definition
/// as a placeholder for the actual definition for forward constants defs.
class ConstantPlaceHolder : public ConstantExpr {
public:
  explicit ConstantPlaceHolder(Type *Ty, LLVMContext &Context)
      : ConstantExpr(Ty, Instruction::UserOp1, &Op<0>(), 1) {
    Op<0>() = UndefValue::get(Type::getInt32Ty(Context));
  }

  ConstantPlaceHolder &operator=(const ConstantPlaceHolder &) = delete;

  // allocate space for exactly one operand
  void *operator new(size_t s) { return User::operator new(s, 1); }

  /// Methods to support type inquiry through isa, cast, and dyn_cast.
  static bool classof(const Value *V) {
    return isa<ConstantExpr>(V) &&
           cast<ConstantExpr>(V)->getOpcode() == Instruction::UserOp1;
  }

  /// Provide fast operand accessors
  DECLARE_TRANSPARENT_OPERAND_ACCESSORS(Value);
};

} // end anonymous namespace

// FIXME: can we inherit this from ConstantExpr?
template <>
struct OperandTraits<ConstantPlaceHolder>
    : public FixedNumOperandTraits<ConstantPlaceHolder, 1> {};
DEFINE_TRANSPARENT_OPERAND_ACCESSORS(ConstantPlaceHolder, Value)

} // end namespace llvm

void BitcodeReaderValueList::assignValue(Value *V, unsigned Idx) {
  if (Idx == size()) {
    push_back(V);
    return;
  }

  if (Idx >= size())
    resize(Idx + 1);

  WeakTrackingVH &OldV = ValuePtrs[Idx];
  if (!OldV) {
    OldV = V;
    return;
  }

  // Handle constants and non-constants (e.g. instrs) differently for
  // efficiency.
  if (Constant *PHC = dyn_cast<Constant>(&*OldV)) {
    ResolveConstants.push_back(std::make_pair(PHC, Idx));
    OldV = V;
  } else {
    // If there was a forward reference to this value, replace it.
    Value *PrevVal = OldV;
    OldV->replaceAllUsesWith(V);
    PrevVal->deleteValue();
  }
}

Constant *BitcodeReaderValueList::getConstantFwdRef(unsigned Idx, Type *Ty) {
  if (Idx >= size())
    resize(Idx + 1);

  if (Value *V = ValuePtrs[Idx]) {
    if (Ty != V->getType())
      report_fatal_error("Type mismatch in constant table!");
    return cast<Constant>(V);
  }

  // Create and return a placeholder, which will later be RAUW'd.
  Constant *C = new ConstantPlaceHolder(Ty, Context);
  ValuePtrs[Idx] = C;
  return C;
}

Value *BitcodeReaderValueList::getValueFwdRef(unsigned Idx, Type *Ty) {
  // Bail out for a clearly invalid value. This would make us call resize(0)
  if (Idx == std::numeric_limits<unsigned>::max())
    return nullptr;

  if (Idx >= size())
    resize(Idx + 1);

  if (Value *V = ValuePtrs[Idx]) {
    // If the types don't match, it's invalid.
    if (Ty && Ty != V->getType())
      return nullptr;
    return V;
  }

  // No type specified, must be invalid reference.
  if (!Ty)
    return nullptr;

  // Create and return a placeholder, which will later be RAUW'd.
  Value *V = new Argument(Ty);
  ValuePtrs[Idx] = V;
  return V;
}

/// Once all constants are read, this method bulk resolves any forward
/// references.  The idea behind this is that we sometimes get constants (such
/// as large arrays) which reference *many* forward ref constants.  Replacing
/// each of these causes a lot of thrashing when building/reuniquing the
/// constant.  Instead of doing this, we look at all the uses and rewrite all
/// the place holders at once for any constant that uses a placeholder.
void BitcodeReaderValueList::resolveConstantForwardRefs() {
  // Sort the values by-pointer so that they are efficient to look up with a
  // binary search.
  llvm::sort(ResolveConstants);

  SmallVector<Constant *, 64> NewOps;

  while (!ResolveConstants.empty()) {
    Value *RealVal = operator[](ResolveConstants.back().second);
    Constant *Placeholder = ResolveConstants.back().first;
    ResolveConstants.pop_back();

    // Loop over all users of the placeholder, updating them to reference the
    // new value.  If they reference more than one placeholder, update them all
    // at once.
    while (!Placeholder->use_empty()) {
      auto UI = Placeholder->user_begin();
      User *U = *UI;

      // If the using object isn't uniqued, just update the operands.  This
      // handles instructions and initializers for global variables.
      if (!isa<Constant>(U) || isa<GlobalValue>(U)) {
        UI.getUse().set(RealVal);
        continue;
      }

      // Otherwise, we have a constant that uses the placeholder.  Replace that
      // constant with a new constant that has *all* placeholder uses updated.
      Constant *UserC = cast<Constant>(U);
      for (User::op_iterator I = UserC->op_begin(), E = UserC->op_end(); I != E;
           ++I) {
        Value *NewOp;
        if (!isa<ConstantPlaceHolder>(*I)) {
          // Not a placeholder reference.
          NewOp = *I;
        } else if (*I == Placeholder) {
          // Common case is that it just references this one placeholder.
          NewOp = RealVal;
        } else {
          // Otherwise, look up the placeholder in ResolveConstants.
          ResolveConstantsTy::iterator It = std::lower_bound(
              ResolveConstants.begin(), ResolveConstants.end(),
              std::pair<Constant *, unsigned>(cast<Constant>(*I), 0));
          assert(It != ResolveConstants.end() && It->first == *I);
          NewOp = operator[](It->second);
        }

        NewOps.push_back(cast<Constant>(NewOp));
      }

      // Make the new constant.
      Constant *NewC;
      if (ConstantArray *UserCA = dyn_cast<ConstantArray>(UserC)) {
        NewC = ConstantArray::get(UserCA->getType(), NewOps);
      } else if (ConstantStruct *UserCS = dyn_cast<ConstantStruct>(UserC)) {
        NewC = ConstantStruct::get(UserCS->getType(), NewOps);
      } else if (isa<ConstantVector>(UserC)) {
        NewC = ConstantVector::get(NewOps);
      } else {
        assert(isa<ConstantExpr>(UserC) && "Must be a ConstantExpr.");
        NewC = cast<ConstantExpr>(UserC)->getWithOperands(NewOps);
      }

      UserC->replaceAllUsesWith(NewC);
      UserC->destroyConstant();
      NewOps.clear();
    }

    // Update all ValueHandles, they should be the only users at this point.
    Placeholder->replaceAllUsesWith(RealVal);
    Placeholder->deleteValue();
  }
}
