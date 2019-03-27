//===-- Constants.cpp - Implement Constant nodes --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Constant* classes.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Constants.h"
#include "ConstantFold.h"
#include "LLVMContextImpl.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

using namespace llvm;

//===----------------------------------------------------------------------===//
//                              Constant Class
//===----------------------------------------------------------------------===//

bool Constant::isNegativeZeroValue() const {
  // Floating point values have an explicit -0.0 value.
  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(this))
    return CFP->isZero() && CFP->isNegative();

  // Equivalent for a vector of -0.0's.
  if (const ConstantDataVector *CV = dyn_cast<ConstantDataVector>(this))
    if (CV->getElementType()->isFloatingPointTy() && CV->isSplat())
      if (CV->getElementAsAPFloat(0).isNegZero())
        return true;

  if (const ConstantVector *CV = dyn_cast<ConstantVector>(this))
    if (ConstantFP *SplatCFP = dyn_cast_or_null<ConstantFP>(CV->getSplatValue()))
      if (SplatCFP && SplatCFP->isZero() && SplatCFP->isNegative())
        return true;

  // We've already handled true FP case; any other FP vectors can't represent -0.0.
  if (getType()->isFPOrFPVectorTy())
    return false;

  // Otherwise, just use +0.0.
  return isNullValue();
}

// Return true iff this constant is positive zero (floating point), negative
// zero (floating point), or a null value.
bool Constant::isZeroValue() const {
  // Floating point values have an explicit -0.0 value.
  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(this))
    return CFP->isZero();

  // Equivalent for a vector of -0.0's.
  if (const ConstantDataVector *CV = dyn_cast<ConstantDataVector>(this))
    if (CV->getElementType()->isFloatingPointTy() && CV->isSplat())
      if (CV->getElementAsAPFloat(0).isZero())
        return true;

  if (const ConstantVector *CV = dyn_cast<ConstantVector>(this))
    if (ConstantFP *SplatCFP = dyn_cast_or_null<ConstantFP>(CV->getSplatValue()))
      if (SplatCFP && SplatCFP->isZero())
        return true;

  // Otherwise, just use +0.0.
  return isNullValue();
}

bool Constant::isNullValue() const {
  // 0 is null.
  if (const ConstantInt *CI = dyn_cast<ConstantInt>(this))
    return CI->isZero();

  // +0.0 is null.
  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(this))
    return CFP->isZero() && !CFP->isNegative();

  // constant zero is zero for aggregates, cpnull is null for pointers, none for
  // tokens.
  return isa<ConstantAggregateZero>(this) || isa<ConstantPointerNull>(this) ||
         isa<ConstantTokenNone>(this);
}

bool Constant::isAllOnesValue() const {
  // Check for -1 integers
  if (const ConstantInt *CI = dyn_cast<ConstantInt>(this))
    return CI->isMinusOne();

  // Check for FP which are bitcasted from -1 integers
  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(this))
    return CFP->getValueAPF().bitcastToAPInt().isAllOnesValue();

  // Check for constant vectors which are splats of -1 values.
  if (const ConstantVector *CV = dyn_cast<ConstantVector>(this))
    if (Constant *Splat = CV->getSplatValue())
      return Splat->isAllOnesValue();

  // Check for constant vectors which are splats of -1 values.
  if (const ConstantDataVector *CV = dyn_cast<ConstantDataVector>(this)) {
    if (CV->isSplat()) {
      if (CV->getElementType()->isFloatingPointTy())
        return CV->getElementAsAPFloat(0).bitcastToAPInt().isAllOnesValue();
      return CV->getElementAsAPInt(0).isAllOnesValue();
    }
  }

  return false;
}

bool Constant::isOneValue() const {
  // Check for 1 integers
  if (const ConstantInt *CI = dyn_cast<ConstantInt>(this))
    return CI->isOne();

  // Check for FP which are bitcasted from 1 integers
  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(this))
    return CFP->getValueAPF().bitcastToAPInt().isOneValue();

  // Check for constant vectors which are splats of 1 values.
  if (const ConstantVector *CV = dyn_cast<ConstantVector>(this))
    if (Constant *Splat = CV->getSplatValue())
      return Splat->isOneValue();

  // Check for constant vectors which are splats of 1 values.
  if (const ConstantDataVector *CV = dyn_cast<ConstantDataVector>(this)) {
    if (CV->isSplat()) {
      if (CV->getElementType()->isFloatingPointTy())
        return CV->getElementAsAPFloat(0).bitcastToAPInt().isOneValue();
      return CV->getElementAsAPInt(0).isOneValue();
    }
  }

  return false;
}

bool Constant::isMinSignedValue() const {
  // Check for INT_MIN integers
  if (const ConstantInt *CI = dyn_cast<ConstantInt>(this))
    return CI->isMinValue(/*isSigned=*/true);

  // Check for FP which are bitcasted from INT_MIN integers
  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(this))
    return CFP->getValueAPF().bitcastToAPInt().isMinSignedValue();

  // Check for constant vectors which are splats of INT_MIN values.
  if (const ConstantVector *CV = dyn_cast<ConstantVector>(this))
    if (Constant *Splat = CV->getSplatValue())
      return Splat->isMinSignedValue();

  // Check for constant vectors which are splats of INT_MIN values.
  if (const ConstantDataVector *CV = dyn_cast<ConstantDataVector>(this)) {
    if (CV->isSplat()) {
      if (CV->getElementType()->isFloatingPointTy())
        return CV->getElementAsAPFloat(0).bitcastToAPInt().isMinSignedValue();
      return CV->getElementAsAPInt(0).isMinSignedValue();
    }
  }

  return false;
}

bool Constant::isNotMinSignedValue() const {
  // Check for INT_MIN integers
  if (const ConstantInt *CI = dyn_cast<ConstantInt>(this))
    return !CI->isMinValue(/*isSigned=*/true);

  // Check for FP which are bitcasted from INT_MIN integers
  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(this))
    return !CFP->getValueAPF().bitcastToAPInt().isMinSignedValue();

  // Check that vectors don't contain INT_MIN
  if (this->getType()->isVectorTy()) {
    unsigned NumElts = this->getType()->getVectorNumElements();
    for (unsigned i = 0; i != NumElts; ++i) {
      Constant *Elt = this->getAggregateElement(i);
      if (!Elt || !Elt->isNotMinSignedValue())
        return false;
    }
    return true;
  }

  // It *may* contain INT_MIN, we can't tell.
  return false;
}

bool Constant::isFiniteNonZeroFP() const {
  if (auto *CFP = dyn_cast<ConstantFP>(this))
    return CFP->getValueAPF().isFiniteNonZero();
  if (!getType()->isVectorTy())
    return false;
  for (unsigned i = 0, e = getType()->getVectorNumElements(); i != e; ++i) {
    auto *CFP = dyn_cast_or_null<ConstantFP>(this->getAggregateElement(i));
    if (!CFP || !CFP->getValueAPF().isFiniteNonZero())
      return false;
  }
  return true;
}

bool Constant::isNormalFP() const {
  if (auto *CFP = dyn_cast<ConstantFP>(this))
    return CFP->getValueAPF().isNormal();
  if (!getType()->isVectorTy())
    return false;
  for (unsigned i = 0, e = getType()->getVectorNumElements(); i != e; ++i) {
    auto *CFP = dyn_cast_or_null<ConstantFP>(this->getAggregateElement(i));
    if (!CFP || !CFP->getValueAPF().isNormal())
      return false;
  }
  return true;
}

bool Constant::hasExactInverseFP() const {
  if (auto *CFP = dyn_cast<ConstantFP>(this))
    return CFP->getValueAPF().getExactInverse(nullptr);
  if (!getType()->isVectorTy())
    return false;
  for (unsigned i = 0, e = getType()->getVectorNumElements(); i != e; ++i) {
    auto *CFP = dyn_cast_or_null<ConstantFP>(this->getAggregateElement(i));
    if (!CFP || !CFP->getValueAPF().getExactInverse(nullptr))
      return false;
  }
  return true;
}

bool Constant::isNaN() const {
  if (auto *CFP = dyn_cast<ConstantFP>(this))
    return CFP->isNaN();
  if (!getType()->isVectorTy())
    return false;
  for (unsigned i = 0, e = getType()->getVectorNumElements(); i != e; ++i) {
    auto *CFP = dyn_cast_or_null<ConstantFP>(this->getAggregateElement(i));
    if (!CFP || !CFP->isNaN())
      return false;
  }
  return true;
}

bool Constant::containsUndefElement() const {
  if (!getType()->isVectorTy())
    return false;
  for (unsigned i = 0, e = getType()->getVectorNumElements(); i != e; ++i)
    if (isa<UndefValue>(getAggregateElement(i)))
      return true;

  return false;
}

/// Constructor to create a '0' constant of arbitrary type.
Constant *Constant::getNullValue(Type *Ty) {
  switch (Ty->getTypeID()) {
  case Type::IntegerTyID:
    return ConstantInt::get(Ty, 0);
  case Type::HalfTyID:
    return ConstantFP::get(Ty->getContext(),
                           APFloat::getZero(APFloat::IEEEhalf()));
  case Type::FloatTyID:
    return ConstantFP::get(Ty->getContext(),
                           APFloat::getZero(APFloat::IEEEsingle()));
  case Type::DoubleTyID:
    return ConstantFP::get(Ty->getContext(),
                           APFloat::getZero(APFloat::IEEEdouble()));
  case Type::X86_FP80TyID:
    return ConstantFP::get(Ty->getContext(),
                           APFloat::getZero(APFloat::x87DoubleExtended()));
  case Type::FP128TyID:
    return ConstantFP::get(Ty->getContext(),
                           APFloat::getZero(APFloat::IEEEquad()));
  case Type::PPC_FP128TyID:
    return ConstantFP::get(Ty->getContext(),
                           APFloat(APFloat::PPCDoubleDouble(),
                                   APInt::getNullValue(128)));
  case Type::PointerTyID:
    return ConstantPointerNull::get(cast<PointerType>(Ty));
  case Type::StructTyID:
  case Type::ArrayTyID:
  case Type::VectorTyID:
    return ConstantAggregateZero::get(Ty);
  case Type::TokenTyID:
    return ConstantTokenNone::get(Ty->getContext());
  default:
    // Function, Label, or Opaque type?
    llvm_unreachable("Cannot create a null constant of that type!");
  }
}

Constant *Constant::getIntegerValue(Type *Ty, const APInt &V) {
  Type *ScalarTy = Ty->getScalarType();

  // Create the base integer constant.
  Constant *C = ConstantInt::get(Ty->getContext(), V);

  // Convert an integer to a pointer, if necessary.
  if (PointerType *PTy = dyn_cast<PointerType>(ScalarTy))
    C = ConstantExpr::getIntToPtr(C, PTy);

  // Broadcast a scalar to a vector, if necessary.
  if (VectorType *VTy = dyn_cast<VectorType>(Ty))
    C = ConstantVector::getSplat(VTy->getNumElements(), C);

  return C;
}

Constant *Constant::getAllOnesValue(Type *Ty) {
  if (IntegerType *ITy = dyn_cast<IntegerType>(Ty))
    return ConstantInt::get(Ty->getContext(),
                            APInt::getAllOnesValue(ITy->getBitWidth()));

  if (Ty->isFloatingPointTy()) {
    APFloat FL = APFloat::getAllOnesValue(Ty->getPrimitiveSizeInBits(),
                                          !Ty->isPPC_FP128Ty());
    return ConstantFP::get(Ty->getContext(), FL);
  }

  VectorType *VTy = cast<VectorType>(Ty);
  return ConstantVector::getSplat(VTy->getNumElements(),
                                  getAllOnesValue(VTy->getElementType()));
}

Constant *Constant::getAggregateElement(unsigned Elt) const {
  if (const ConstantAggregate *CC = dyn_cast<ConstantAggregate>(this))
    return Elt < CC->getNumOperands() ? CC->getOperand(Elt) : nullptr;

  if (const ConstantAggregateZero *CAZ = dyn_cast<ConstantAggregateZero>(this))
    return Elt < CAZ->getNumElements() ? CAZ->getElementValue(Elt) : nullptr;

  if (const UndefValue *UV = dyn_cast<UndefValue>(this))
    return Elt < UV->getNumElements() ? UV->getElementValue(Elt) : nullptr;

  if (const ConstantDataSequential *CDS =dyn_cast<ConstantDataSequential>(this))
    return Elt < CDS->getNumElements() ? CDS->getElementAsConstant(Elt)
                                       : nullptr;
  return nullptr;
}

Constant *Constant::getAggregateElement(Constant *Elt) const {
  assert(isa<IntegerType>(Elt->getType()) && "Index must be an integer");
  if (ConstantInt *CI = dyn_cast<ConstantInt>(Elt)) {
    // Check if the constant fits into an uint64_t.
    if (CI->getValue().getActiveBits() > 64)
      return nullptr;
    return getAggregateElement(CI->getZExtValue());
  }
  return nullptr;
}

void Constant::destroyConstant() {
  /// First call destroyConstantImpl on the subclass.  This gives the subclass
  /// a chance to remove the constant from any maps/pools it's contained in.
  switch (getValueID()) {
  default:
    llvm_unreachable("Not a constant!");
#define HANDLE_CONSTANT(Name)                                                  \
  case Value::Name##Val:                                                       \
    cast<Name>(this)->destroyConstantImpl();                                   \
    break;
#include "llvm/IR/Value.def"
  }

  // When a Constant is destroyed, there may be lingering
  // references to the constant by other constants in the constant pool.  These
  // constants are implicitly dependent on the module that is being deleted,
  // but they don't know that.  Because we only find out when the CPV is
  // deleted, we must now notify all of our users (that should only be
  // Constants) that they are, in fact, invalid now and should be deleted.
  //
  while (!use_empty()) {
    Value *V = user_back();
#ifndef NDEBUG // Only in -g mode...
    if (!isa<Constant>(V)) {
      dbgs() << "While deleting: " << *this
             << "\n\nUse still stuck around after Def is destroyed: " << *V
             << "\n\n";
    }
#endif
    assert(isa<Constant>(V) && "References remain to Constant being destroyed");
    cast<Constant>(V)->destroyConstant();

    // The constant should remove itself from our use list...
    assert((use_empty() || user_back() != V) && "Constant not removed!");
  }

  // Value has no outstanding references it is safe to delete it now...
  delete this;
}

static bool canTrapImpl(const Constant *C,
                        SmallPtrSetImpl<const ConstantExpr *> &NonTrappingOps) {
  assert(C->getType()->isFirstClassType() && "Cannot evaluate aggregate vals!");
  // The only thing that could possibly trap are constant exprs.
  const ConstantExpr *CE = dyn_cast<ConstantExpr>(C);
  if (!CE)
    return false;

  // ConstantExpr traps if any operands can trap.
  for (unsigned i = 0, e = C->getNumOperands(); i != e; ++i) {
    if (ConstantExpr *Op = dyn_cast<ConstantExpr>(CE->getOperand(i))) {
      if (NonTrappingOps.insert(Op).second && canTrapImpl(Op, NonTrappingOps))
        return true;
    }
  }

  // Otherwise, only specific operations can trap.
  switch (CE->getOpcode()) {
  default:
    return false;
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::URem:
  case Instruction::SRem:
    // Div and rem can trap if the RHS is not known to be non-zero.
    if (!isa<ConstantInt>(CE->getOperand(1)) ||CE->getOperand(1)->isNullValue())
      return true;
    return false;
  }
}

bool Constant::canTrap() const {
  SmallPtrSet<const ConstantExpr *, 4> NonTrappingOps;
  return canTrapImpl(this, NonTrappingOps);
}

/// Check if C contains a GlobalValue for which Predicate is true.
static bool
ConstHasGlobalValuePredicate(const Constant *C,
                             bool (*Predicate)(const GlobalValue *)) {
  SmallPtrSet<const Constant *, 8> Visited;
  SmallVector<const Constant *, 8> WorkList;
  WorkList.push_back(C);
  Visited.insert(C);

  while (!WorkList.empty()) {
    const Constant *WorkItem = WorkList.pop_back_val();
    if (const auto *GV = dyn_cast<GlobalValue>(WorkItem))
      if (Predicate(GV))
        return true;
    for (const Value *Op : WorkItem->operands()) {
      const Constant *ConstOp = dyn_cast<Constant>(Op);
      if (!ConstOp)
        continue;
      if (Visited.insert(ConstOp).second)
        WorkList.push_back(ConstOp);
    }
  }
  return false;
}

bool Constant::isThreadDependent() const {
  auto DLLImportPredicate = [](const GlobalValue *GV) {
    return GV->isThreadLocal();
  };
  return ConstHasGlobalValuePredicate(this, DLLImportPredicate);
}

bool Constant::isDLLImportDependent() const {
  auto DLLImportPredicate = [](const GlobalValue *GV) {
    return GV->hasDLLImportStorageClass();
  };
  return ConstHasGlobalValuePredicate(this, DLLImportPredicate);
}

bool Constant::isConstantUsed() const {
  for (const User *U : users()) {
    const Constant *UC = dyn_cast<Constant>(U);
    if (!UC || isa<GlobalValue>(UC))
      return true;

    if (UC->isConstantUsed())
      return true;
  }
  return false;
}

bool Constant::needsRelocation() const {
  if (isa<GlobalValue>(this))
    return true; // Global reference.

  if (const BlockAddress *BA = dyn_cast<BlockAddress>(this))
    return BA->getFunction()->needsRelocation();

  // While raw uses of blockaddress need to be relocated, differences between
  // two of them don't when they are for labels in the same function.  This is a
  // common idiom when creating a table for the indirect goto extension, so we
  // handle it efficiently here.
  if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(this))
    if (CE->getOpcode() == Instruction::Sub) {
      ConstantExpr *LHS = dyn_cast<ConstantExpr>(CE->getOperand(0));
      ConstantExpr *RHS = dyn_cast<ConstantExpr>(CE->getOperand(1));
      if (LHS && RHS && LHS->getOpcode() == Instruction::PtrToInt &&
          RHS->getOpcode() == Instruction::PtrToInt &&
          isa<BlockAddress>(LHS->getOperand(0)) &&
          isa<BlockAddress>(RHS->getOperand(0)) &&
          cast<BlockAddress>(LHS->getOperand(0))->getFunction() ==
              cast<BlockAddress>(RHS->getOperand(0))->getFunction())
        return false;
    }

  bool Result = false;
  for (unsigned i = 0, e = getNumOperands(); i != e; ++i)
    Result |= cast<Constant>(getOperand(i))->needsRelocation();

  return Result;
}

/// If the specified constantexpr is dead, remove it. This involves recursively
/// eliminating any dead users of the constantexpr.
static bool removeDeadUsersOfConstant(const Constant *C) {
  if (isa<GlobalValue>(C)) return false; // Cannot remove this

  while (!C->use_empty()) {
    const Constant *User = dyn_cast<Constant>(C->user_back());
    if (!User) return false; // Non-constant usage;
    if (!removeDeadUsersOfConstant(User))
      return false; // Constant wasn't dead
  }

  const_cast<Constant*>(C)->destroyConstant();
  return true;
}


void Constant::removeDeadConstantUsers() const {
  Value::const_user_iterator I = user_begin(), E = user_end();
  Value::const_user_iterator LastNonDeadUser = E;
  while (I != E) {
    const Constant *User = dyn_cast<Constant>(*I);
    if (!User) {
      LastNonDeadUser = I;
      ++I;
      continue;
    }

    if (!removeDeadUsersOfConstant(User)) {
      // If the constant wasn't dead, remember that this was the last live use
      // and move on to the next constant.
      LastNonDeadUser = I;
      ++I;
      continue;
    }

    // If the constant was dead, then the iterator is invalidated.
    if (LastNonDeadUser == E) {
      I = user_begin();
      if (I == E) break;
    } else {
      I = LastNonDeadUser;
      ++I;
    }
  }
}



//===----------------------------------------------------------------------===//
//                                ConstantInt
//===----------------------------------------------------------------------===//

ConstantInt::ConstantInt(IntegerType *Ty, const APInt &V)
    : ConstantData(Ty, ConstantIntVal), Val(V) {
  assert(V.getBitWidth() == Ty->getBitWidth() && "Invalid constant for type");
}

ConstantInt *ConstantInt::getTrue(LLVMContext &Context) {
  LLVMContextImpl *pImpl = Context.pImpl;
  if (!pImpl->TheTrueVal)
    pImpl->TheTrueVal = ConstantInt::get(Type::getInt1Ty(Context), 1);
  return pImpl->TheTrueVal;
}

ConstantInt *ConstantInt::getFalse(LLVMContext &Context) {
  LLVMContextImpl *pImpl = Context.pImpl;
  if (!pImpl->TheFalseVal)
    pImpl->TheFalseVal = ConstantInt::get(Type::getInt1Ty(Context), 0);
  return pImpl->TheFalseVal;
}

Constant *ConstantInt::getTrue(Type *Ty) {
  assert(Ty->isIntOrIntVectorTy(1) && "Type not i1 or vector of i1.");
  ConstantInt *TrueC = ConstantInt::getTrue(Ty->getContext());
  if (auto *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getNumElements(), TrueC);
  return TrueC;
}

Constant *ConstantInt::getFalse(Type *Ty) {
  assert(Ty->isIntOrIntVectorTy(1) && "Type not i1 or vector of i1.");
  ConstantInt *FalseC = ConstantInt::getFalse(Ty->getContext());
  if (auto *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getNumElements(), FalseC);
  return FalseC;
}

// Get a ConstantInt from an APInt.
ConstantInt *ConstantInt::get(LLVMContext &Context, const APInt &V) {
  // get an existing value or the insertion position
  LLVMContextImpl *pImpl = Context.pImpl;
  std::unique_ptr<ConstantInt> &Slot = pImpl->IntConstants[V];
  if (!Slot) {
    // Get the corresponding integer type for the bit width of the value.
    IntegerType *ITy = IntegerType::get(Context, V.getBitWidth());
    Slot.reset(new ConstantInt(ITy, V));
  }
  assert(Slot->getType() == IntegerType::get(Context, V.getBitWidth()));
  return Slot.get();
}

Constant *ConstantInt::get(Type *Ty, uint64_t V, bool isSigned) {
  Constant *C = get(cast<IntegerType>(Ty->getScalarType()), V, isSigned);

  // For vectors, broadcast the value.
  if (VectorType *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getNumElements(), C);

  return C;
}

ConstantInt *ConstantInt::get(IntegerType *Ty, uint64_t V, bool isSigned) {
  return get(Ty->getContext(), APInt(Ty->getBitWidth(), V, isSigned));
}

ConstantInt *ConstantInt::getSigned(IntegerType *Ty, int64_t V) {
  return get(Ty, V, true);
}

Constant *ConstantInt::getSigned(Type *Ty, int64_t V) {
  return get(Ty, V, true);
}

Constant *ConstantInt::get(Type *Ty, const APInt& V) {
  ConstantInt *C = get(Ty->getContext(), V);
  assert(C->getType() == Ty->getScalarType() &&
         "ConstantInt type doesn't match the type implied by its value!");

  // For vectors, broadcast the value.
  if (VectorType *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getNumElements(), C);

  return C;
}

ConstantInt *ConstantInt::get(IntegerType* Ty, StringRef Str, uint8_t radix) {
  return get(Ty->getContext(), APInt(Ty->getBitWidth(), Str, radix));
}

/// Remove the constant from the constant table.
void ConstantInt::destroyConstantImpl() {
  llvm_unreachable("You can't ConstantInt->destroyConstantImpl()!");
}

//===----------------------------------------------------------------------===//
//                                ConstantFP
//===----------------------------------------------------------------------===//

static const fltSemantics *TypeToFloatSemantics(Type *Ty) {
  if (Ty->isHalfTy())
    return &APFloat::IEEEhalf();
  if (Ty->isFloatTy())
    return &APFloat::IEEEsingle();
  if (Ty->isDoubleTy())
    return &APFloat::IEEEdouble();
  if (Ty->isX86_FP80Ty())
    return &APFloat::x87DoubleExtended();
  else if (Ty->isFP128Ty())
    return &APFloat::IEEEquad();

  assert(Ty->isPPC_FP128Ty() && "Unknown FP format");
  return &APFloat::PPCDoubleDouble();
}

Constant *ConstantFP::get(Type *Ty, double V) {
  LLVMContext &Context = Ty->getContext();

  APFloat FV(V);
  bool ignored;
  FV.convert(*TypeToFloatSemantics(Ty->getScalarType()),
             APFloat::rmNearestTiesToEven, &ignored);
  Constant *C = get(Context, FV);

  // For vectors, broadcast the value.
  if (VectorType *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getNumElements(), C);

  return C;
}

Constant *ConstantFP::get(Type *Ty, const APFloat &V) {
  ConstantFP *C = get(Ty->getContext(), V);
  assert(C->getType() == Ty->getScalarType() &&
         "ConstantFP type doesn't match the type implied by its value!");

  // For vectors, broadcast the value.
  if (auto *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getNumElements(), C);

  return C;
}

Constant *ConstantFP::get(Type *Ty, StringRef Str) {
  LLVMContext &Context = Ty->getContext();

  APFloat FV(*TypeToFloatSemantics(Ty->getScalarType()), Str);
  Constant *C = get(Context, FV);

  // For vectors, broadcast the value.
  if (VectorType *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getNumElements(), C);

  return C;
}

Constant *ConstantFP::getNaN(Type *Ty, bool Negative, uint64_t Payload) {
  const fltSemantics &Semantics = *TypeToFloatSemantics(Ty->getScalarType());
  APFloat NaN = APFloat::getNaN(Semantics, Negative, Payload);
  Constant *C = get(Ty->getContext(), NaN);

  if (VectorType *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getNumElements(), C);

  return C;
}

Constant *ConstantFP::getQNaN(Type *Ty, bool Negative, APInt *Payload) {
  const fltSemantics &Semantics = *TypeToFloatSemantics(Ty->getScalarType());
  APFloat NaN = APFloat::getQNaN(Semantics, Negative, Payload);
  Constant *C = get(Ty->getContext(), NaN);
  
  if (VectorType *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getNumElements(), C);
  
  return C;
}

Constant *ConstantFP::getSNaN(Type *Ty, bool Negative, APInt *Payload) {
  const fltSemantics &Semantics = *TypeToFloatSemantics(Ty->getScalarType());
  APFloat NaN = APFloat::getSNaN(Semantics, Negative, Payload);
  Constant *C = get(Ty->getContext(), NaN);
  
  if (VectorType *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getNumElements(), C);
  
  return C;
}

Constant *ConstantFP::getNegativeZero(Type *Ty) {
  const fltSemantics &Semantics = *TypeToFloatSemantics(Ty->getScalarType());
  APFloat NegZero = APFloat::getZero(Semantics, /*Negative=*/true);
  Constant *C = get(Ty->getContext(), NegZero);

  if (VectorType *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getNumElements(), C);

  return C;
}


Constant *ConstantFP::getZeroValueForNegation(Type *Ty) {
  if (Ty->isFPOrFPVectorTy())
    return getNegativeZero(Ty);

  return Constant::getNullValue(Ty);
}


// ConstantFP accessors.
ConstantFP* ConstantFP::get(LLVMContext &Context, const APFloat& V) {
  LLVMContextImpl* pImpl = Context.pImpl;

  std::unique_ptr<ConstantFP> &Slot = pImpl->FPConstants[V];

  if (!Slot) {
    Type *Ty;
    if (&V.getSemantics() == &APFloat::IEEEhalf())
      Ty = Type::getHalfTy(Context);
    else if (&V.getSemantics() == &APFloat::IEEEsingle())
      Ty = Type::getFloatTy(Context);
    else if (&V.getSemantics() == &APFloat::IEEEdouble())
      Ty = Type::getDoubleTy(Context);
    else if (&V.getSemantics() == &APFloat::x87DoubleExtended())
      Ty = Type::getX86_FP80Ty(Context);
    else if (&V.getSemantics() == &APFloat::IEEEquad())
      Ty = Type::getFP128Ty(Context);
    else {
      assert(&V.getSemantics() == &APFloat::PPCDoubleDouble() &&
             "Unknown FP format");
      Ty = Type::getPPC_FP128Ty(Context);
    }
    Slot.reset(new ConstantFP(Ty, V));
  }

  return Slot.get();
}

Constant *ConstantFP::getInfinity(Type *Ty, bool Negative) {
  const fltSemantics &Semantics = *TypeToFloatSemantics(Ty->getScalarType());
  Constant *C = get(Ty->getContext(), APFloat::getInf(Semantics, Negative));

  if (VectorType *VTy = dyn_cast<VectorType>(Ty))
    return ConstantVector::getSplat(VTy->getNumElements(), C);

  return C;
}

ConstantFP::ConstantFP(Type *Ty, const APFloat &V)
    : ConstantData(Ty, ConstantFPVal), Val(V) {
  assert(&V.getSemantics() == TypeToFloatSemantics(Ty) &&
         "FP type Mismatch");
}

bool ConstantFP::isExactlyValue(const APFloat &V) const {
  return Val.bitwiseIsEqual(V);
}

/// Remove the constant from the constant table.
void ConstantFP::destroyConstantImpl() {
  llvm_unreachable("You can't ConstantFP->destroyConstantImpl()!");
}

//===----------------------------------------------------------------------===//
//                   ConstantAggregateZero Implementation
//===----------------------------------------------------------------------===//

Constant *ConstantAggregateZero::getSequentialElement() const {
  return Constant::getNullValue(getType()->getSequentialElementType());
}

Constant *ConstantAggregateZero::getStructElement(unsigned Elt) const {
  return Constant::getNullValue(getType()->getStructElementType(Elt));
}

Constant *ConstantAggregateZero::getElementValue(Constant *C) const {
  if (isa<SequentialType>(getType()))
    return getSequentialElement();
  return getStructElement(cast<ConstantInt>(C)->getZExtValue());
}

Constant *ConstantAggregateZero::getElementValue(unsigned Idx) const {
  if (isa<SequentialType>(getType()))
    return getSequentialElement();
  return getStructElement(Idx);
}

unsigned ConstantAggregateZero::getNumElements() const {
  Type *Ty = getType();
  if (auto *AT = dyn_cast<ArrayType>(Ty))
    return AT->getNumElements();
  if (auto *VT = dyn_cast<VectorType>(Ty))
    return VT->getNumElements();
  return Ty->getStructNumElements();
}

//===----------------------------------------------------------------------===//
//                         UndefValue Implementation
//===----------------------------------------------------------------------===//

UndefValue *UndefValue::getSequentialElement() const {
  return UndefValue::get(getType()->getSequentialElementType());
}

UndefValue *UndefValue::getStructElement(unsigned Elt) const {
  return UndefValue::get(getType()->getStructElementType(Elt));
}

UndefValue *UndefValue::getElementValue(Constant *C) const {
  if (isa<SequentialType>(getType()))
    return getSequentialElement();
  return getStructElement(cast<ConstantInt>(C)->getZExtValue());
}

UndefValue *UndefValue::getElementValue(unsigned Idx) const {
  if (isa<SequentialType>(getType()))
    return getSequentialElement();
  return getStructElement(Idx);
}

unsigned UndefValue::getNumElements() const {
  Type *Ty = getType();
  if (auto *ST = dyn_cast<SequentialType>(Ty))
    return ST->getNumElements();
  return Ty->getStructNumElements();
}

//===----------------------------------------------------------------------===//
//                            ConstantXXX Classes
//===----------------------------------------------------------------------===//

template <typename ItTy, typename EltTy>
static bool rangeOnlyContains(ItTy Start, ItTy End, EltTy Elt) {
  for (; Start != End; ++Start)
    if (*Start != Elt)
      return false;
  return true;
}

template <typename SequentialTy, typename ElementTy>
static Constant *getIntSequenceIfElementsMatch(ArrayRef<Constant *> V) {
  assert(!V.empty() && "Cannot get empty int sequence.");

  SmallVector<ElementTy, 16> Elts;
  for (Constant *C : V)
    if (auto *CI = dyn_cast<ConstantInt>(C))
      Elts.push_back(CI->getZExtValue());
    else
      return nullptr;
  return SequentialTy::get(V[0]->getContext(), Elts);
}

template <typename SequentialTy, typename ElementTy>
static Constant *getFPSequenceIfElementsMatch(ArrayRef<Constant *> V) {
  assert(!V.empty() && "Cannot get empty FP sequence.");

  SmallVector<ElementTy, 16> Elts;
  for (Constant *C : V)
    if (auto *CFP = dyn_cast<ConstantFP>(C))
      Elts.push_back(CFP->getValueAPF().bitcastToAPInt().getLimitedValue());
    else
      return nullptr;
  return SequentialTy::getFP(V[0]->getContext(), Elts);
}

template <typename SequenceTy>
static Constant *getSequenceIfElementsMatch(Constant *C,
                                            ArrayRef<Constant *> V) {
  // We speculatively build the elements here even if it turns out that there is
  // a constantexpr or something else weird, since it is so uncommon for that to
  // happen.
  if (ConstantInt *CI = dyn_cast<ConstantInt>(C)) {
    if (CI->getType()->isIntegerTy(8))
      return getIntSequenceIfElementsMatch<SequenceTy, uint8_t>(V);
    else if (CI->getType()->isIntegerTy(16))
      return getIntSequenceIfElementsMatch<SequenceTy, uint16_t>(V);
    else if (CI->getType()->isIntegerTy(32))
      return getIntSequenceIfElementsMatch<SequenceTy, uint32_t>(V);
    else if (CI->getType()->isIntegerTy(64))
      return getIntSequenceIfElementsMatch<SequenceTy, uint64_t>(V);
  } else if (ConstantFP *CFP = dyn_cast<ConstantFP>(C)) {
    if (CFP->getType()->isHalfTy())
      return getFPSequenceIfElementsMatch<SequenceTy, uint16_t>(V);
    else if (CFP->getType()->isFloatTy())
      return getFPSequenceIfElementsMatch<SequenceTy, uint32_t>(V);
    else if (CFP->getType()->isDoubleTy())
      return getFPSequenceIfElementsMatch<SequenceTy, uint64_t>(V);
  }

  return nullptr;
}

ConstantAggregate::ConstantAggregate(CompositeType *T, ValueTy VT,
                                     ArrayRef<Constant *> V)
    : Constant(T, VT, OperandTraits<ConstantAggregate>::op_end(this) - V.size(),
               V.size()) {
  llvm::copy(V, op_begin());

  // Check that types match, unless this is an opaque struct.
  if (auto *ST = dyn_cast<StructType>(T))
    if (ST->isOpaque())
      return;
  for (unsigned I = 0, E = V.size(); I != E; ++I)
    assert(V[I]->getType() == T->getTypeAtIndex(I) &&
           "Initializer for composite element doesn't match!");
}

ConstantArray::ConstantArray(ArrayType *T, ArrayRef<Constant *> V)
    : ConstantAggregate(T, ConstantArrayVal, V) {
  assert(V.size() == T->getNumElements() &&
         "Invalid initializer for constant array");
}

Constant *ConstantArray::get(ArrayType *Ty, ArrayRef<Constant*> V) {
  if (Constant *C = getImpl(Ty, V))
    return C;
  return Ty->getContext().pImpl->ArrayConstants.getOrCreate(Ty, V);
}

Constant *ConstantArray::getImpl(ArrayType *Ty, ArrayRef<Constant*> V) {
  // Empty arrays are canonicalized to ConstantAggregateZero.
  if (V.empty())
    return ConstantAggregateZero::get(Ty);

  for (unsigned i = 0, e = V.size(); i != e; ++i) {
    assert(V[i]->getType() == Ty->getElementType() &&
           "Wrong type in array element initializer");
  }

  // If this is an all-zero array, return a ConstantAggregateZero object.  If
  // all undef, return an UndefValue, if "all simple", then return a
  // ConstantDataArray.
  Constant *C = V[0];
  if (isa<UndefValue>(C) && rangeOnlyContains(V.begin(), V.end(), C))
    return UndefValue::get(Ty);

  if (C->isNullValue() && rangeOnlyContains(V.begin(), V.end(), C))
    return ConstantAggregateZero::get(Ty);

  // Check to see if all of the elements are ConstantFP or ConstantInt and if
  // the element type is compatible with ConstantDataVector.  If so, use it.
  if (ConstantDataSequential::isElementTypeCompatible(C->getType()))
    return getSequenceIfElementsMatch<ConstantDataArray>(C, V);

  // Otherwise, we really do want to create a ConstantArray.
  return nullptr;
}

StructType *ConstantStruct::getTypeForElements(LLVMContext &Context,
                                               ArrayRef<Constant*> V,
                                               bool Packed) {
  unsigned VecSize = V.size();
  SmallVector<Type*, 16> EltTypes(VecSize);
  for (unsigned i = 0; i != VecSize; ++i)
    EltTypes[i] = V[i]->getType();

  return StructType::get(Context, EltTypes, Packed);
}


StructType *ConstantStruct::getTypeForElements(ArrayRef<Constant*> V,
                                               bool Packed) {
  assert(!V.empty() &&
         "ConstantStruct::getTypeForElements cannot be called on empty list");
  return getTypeForElements(V[0]->getContext(), V, Packed);
}

ConstantStruct::ConstantStruct(StructType *T, ArrayRef<Constant *> V)
    : ConstantAggregate(T, ConstantStructVal, V) {
  assert((T->isOpaque() || V.size() == T->getNumElements()) &&
         "Invalid initializer for constant struct");
}

// ConstantStruct accessors.
Constant *ConstantStruct::get(StructType *ST, ArrayRef<Constant*> V) {
  assert((ST->isOpaque() || ST->getNumElements() == V.size()) &&
         "Incorrect # elements specified to ConstantStruct::get");

  // Create a ConstantAggregateZero value if all elements are zeros.
  bool isZero = true;
  bool isUndef = false;

  if (!V.empty()) {
    isUndef = isa<UndefValue>(V[0]);
    isZero = V[0]->isNullValue();
    if (isUndef || isZero) {
      for (unsigned i = 0, e = V.size(); i != e; ++i) {
        if (!V[i]->isNullValue())
          isZero = false;
        if (!isa<UndefValue>(V[i]))
          isUndef = false;
      }
    }
  }
  if (isZero)
    return ConstantAggregateZero::get(ST);
  if (isUndef)
    return UndefValue::get(ST);

  return ST->getContext().pImpl->StructConstants.getOrCreate(ST, V);
}

ConstantVector::ConstantVector(VectorType *T, ArrayRef<Constant *> V)
    : ConstantAggregate(T, ConstantVectorVal, V) {
  assert(V.size() == T->getNumElements() &&
         "Invalid initializer for constant vector");
}

// ConstantVector accessors.
Constant *ConstantVector::get(ArrayRef<Constant*> V) {
  if (Constant *C = getImpl(V))
    return C;
  VectorType *Ty = VectorType::get(V.front()->getType(), V.size());
  return Ty->getContext().pImpl->VectorConstants.getOrCreate(Ty, V);
}

Constant *ConstantVector::getImpl(ArrayRef<Constant*> V) {
  assert(!V.empty() && "Vectors can't be empty");
  VectorType *T = VectorType::get(V.front()->getType(), V.size());

  // If this is an all-undef or all-zero vector, return a
  // ConstantAggregateZero or UndefValue.
  Constant *C = V[0];
  bool isZero = C->isNullValue();
  bool isUndef = isa<UndefValue>(C);

  if (isZero || isUndef) {
    for (unsigned i = 1, e = V.size(); i != e; ++i)
      if (V[i] != C) {
        isZero = isUndef = false;
        break;
      }
  }

  if (isZero)
    return ConstantAggregateZero::get(T);
  if (isUndef)
    return UndefValue::get(T);

  // Check to see if all of the elements are ConstantFP or ConstantInt and if
  // the element type is compatible with ConstantDataVector.  If so, use it.
  if (ConstantDataSequential::isElementTypeCompatible(C->getType()))
    return getSequenceIfElementsMatch<ConstantDataVector>(C, V);

  // Otherwise, the element type isn't compatible with ConstantDataVector, or
  // the operand list contains a ConstantExpr or something else strange.
  return nullptr;
}

Constant *ConstantVector::getSplat(unsigned NumElts, Constant *V) {
  // If this splat is compatible with ConstantDataVector, use it instead of
  // ConstantVector.
  if ((isa<ConstantFP>(V) || isa<ConstantInt>(V)) &&
      ConstantDataSequential::isElementTypeCompatible(V->getType()))
    return ConstantDataVector::getSplat(NumElts, V);

  SmallVector<Constant*, 32> Elts(NumElts, V);
  return get(Elts);
}

ConstantTokenNone *ConstantTokenNone::get(LLVMContext &Context) {
  LLVMContextImpl *pImpl = Context.pImpl;
  if (!pImpl->TheNoneToken)
    pImpl->TheNoneToken.reset(new ConstantTokenNone(Context));
  return pImpl->TheNoneToken.get();
}

/// Remove the constant from the constant table.
void ConstantTokenNone::destroyConstantImpl() {
  llvm_unreachable("You can't ConstantTokenNone->destroyConstantImpl()!");
}

// Utility function for determining if a ConstantExpr is a CastOp or not. This
// can't be inline because we don't want to #include Instruction.h into
// Constant.h
bool ConstantExpr::isCast() const {
  return Instruction::isCast(getOpcode());
}

bool ConstantExpr::isCompare() const {
  return getOpcode() == Instruction::ICmp || getOpcode() == Instruction::FCmp;
}

bool ConstantExpr::isGEPWithNoNotionalOverIndexing() const {
  if (getOpcode() != Instruction::GetElementPtr) return false;

  gep_type_iterator GEPI = gep_type_begin(this), E = gep_type_end(this);
  User::const_op_iterator OI = std::next(this->op_begin());

  // The remaining indices may be compile-time known integers within the bounds
  // of the corresponding notional static array types.
  for (; GEPI != E; ++GEPI, ++OI) {
    if (isa<UndefValue>(*OI))
      continue;
    auto *CI = dyn_cast<ConstantInt>(*OI);
    if (!CI || (GEPI.isBoundedSequential() &&
                (CI->getValue().getActiveBits() > 64 ||
                 CI->getZExtValue() >= GEPI.getSequentialNumElements())))
      return false;
  }

  // All the indices checked out.
  return true;
}

bool ConstantExpr::hasIndices() const {
  return getOpcode() == Instruction::ExtractValue ||
         getOpcode() == Instruction::InsertValue;
}

ArrayRef<unsigned> ConstantExpr::getIndices() const {
  if (const ExtractValueConstantExpr *EVCE =
        dyn_cast<ExtractValueConstantExpr>(this))
    return EVCE->Indices;

  return cast<InsertValueConstantExpr>(this)->Indices;
}

unsigned ConstantExpr::getPredicate() const {
  return cast<CompareConstantExpr>(this)->predicate;
}

Constant *
ConstantExpr::getWithOperandReplaced(unsigned OpNo, Constant *Op) const {
  assert(Op->getType() == getOperand(OpNo)->getType() &&
         "Replacing operand with value of different type!");
  if (getOperand(OpNo) == Op)
    return const_cast<ConstantExpr*>(this);

  SmallVector<Constant*, 8> NewOps;
  for (unsigned i = 0, e = getNumOperands(); i != e; ++i)
    NewOps.push_back(i == OpNo ? Op : getOperand(i));

  return getWithOperands(NewOps);
}

Constant *ConstantExpr::getWithOperands(ArrayRef<Constant *> Ops, Type *Ty,
                                        bool OnlyIfReduced, Type *SrcTy) const {
  assert(Ops.size() == getNumOperands() && "Operand count mismatch!");

  // If no operands changed return self.
  if (Ty == getType() && std::equal(Ops.begin(), Ops.end(), op_begin()))
    return const_cast<ConstantExpr*>(this);

  Type *OnlyIfReducedTy = OnlyIfReduced ? Ty : nullptr;
  switch (getOpcode()) {
  case Instruction::Trunc:
  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::FPTrunc:
  case Instruction::FPExt:
  case Instruction::UIToFP:
  case Instruction::SIToFP:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
  case Instruction::PtrToInt:
  case Instruction::IntToPtr:
  case Instruction::BitCast:
  case Instruction::AddrSpaceCast:
    return ConstantExpr::getCast(getOpcode(), Ops[0], Ty, OnlyIfReduced);
  case Instruction::Select:
    return ConstantExpr::getSelect(Ops[0], Ops[1], Ops[2], OnlyIfReducedTy);
  case Instruction::InsertElement:
    return ConstantExpr::getInsertElement(Ops[0], Ops[1], Ops[2],
                                          OnlyIfReducedTy);
  case Instruction::ExtractElement:
    return ConstantExpr::getExtractElement(Ops[0], Ops[1], OnlyIfReducedTy);
  case Instruction::InsertValue:
    return ConstantExpr::getInsertValue(Ops[0], Ops[1], getIndices(),
                                        OnlyIfReducedTy);
  case Instruction::ExtractValue:
    return ConstantExpr::getExtractValue(Ops[0], getIndices(), OnlyIfReducedTy);
  case Instruction::ShuffleVector:
    return ConstantExpr::getShuffleVector(Ops[0], Ops[1], Ops[2],
                                          OnlyIfReducedTy);
  case Instruction::GetElementPtr: {
    auto *GEPO = cast<GEPOperator>(this);
    assert(SrcTy || (Ops[0]->getType() == getOperand(0)->getType()));
    return ConstantExpr::getGetElementPtr(
        SrcTy ? SrcTy : GEPO->getSourceElementType(), Ops[0], Ops.slice(1),
        GEPO->isInBounds(), GEPO->getInRangeIndex(), OnlyIfReducedTy);
  }
  case Instruction::ICmp:
  case Instruction::FCmp:
    return ConstantExpr::getCompare(getPredicate(), Ops[0], Ops[1],
                                    OnlyIfReducedTy);
  default:
    assert(getNumOperands() == 2 && "Must be binary operator?");
    return ConstantExpr::get(getOpcode(), Ops[0], Ops[1], SubclassOptionalData,
                             OnlyIfReducedTy);
  }
}


//===----------------------------------------------------------------------===//
//                      isValueValidForType implementations

bool ConstantInt::isValueValidForType(Type *Ty, uint64_t Val) {
  unsigned NumBits = Ty->getIntegerBitWidth(); // assert okay
  if (Ty->isIntegerTy(1))
    return Val == 0 || Val == 1;
  return isUIntN(NumBits, Val);
}

bool ConstantInt::isValueValidForType(Type *Ty, int64_t Val) {
  unsigned NumBits = Ty->getIntegerBitWidth();
  if (Ty->isIntegerTy(1))
    return Val == 0 || Val == 1 || Val == -1;
  return isIntN(NumBits, Val);
}

bool ConstantFP::isValueValidForType(Type *Ty, const APFloat& Val) {
  // convert modifies in place, so make a copy.
  APFloat Val2 = APFloat(Val);
  bool losesInfo;
  switch (Ty->getTypeID()) {
  default:
    return false;         // These can't be represented as floating point!

  // FIXME rounding mode needs to be more flexible
  case Type::HalfTyID: {
    if (&Val2.getSemantics() == &APFloat::IEEEhalf())
      return true;
    Val2.convert(APFloat::IEEEhalf(), APFloat::rmNearestTiesToEven, &losesInfo);
    return !losesInfo;
  }
  case Type::FloatTyID: {
    if (&Val2.getSemantics() == &APFloat::IEEEsingle())
      return true;
    Val2.convert(APFloat::IEEEsingle(), APFloat::rmNearestTiesToEven, &losesInfo);
    return !losesInfo;
  }
  case Type::DoubleTyID: {
    if (&Val2.getSemantics() == &APFloat::IEEEhalf() ||
        &Val2.getSemantics() == &APFloat::IEEEsingle() ||
        &Val2.getSemantics() == &APFloat::IEEEdouble())
      return true;
    Val2.convert(APFloat::IEEEdouble(), APFloat::rmNearestTiesToEven, &losesInfo);
    return !losesInfo;
  }
  case Type::X86_FP80TyID:
    return &Val2.getSemantics() == &APFloat::IEEEhalf() ||
           &Val2.getSemantics() == &APFloat::IEEEsingle() ||
           &Val2.getSemantics() == &APFloat::IEEEdouble() ||
           &Val2.getSemantics() == &APFloat::x87DoubleExtended();
  case Type::FP128TyID:
    return &Val2.getSemantics() == &APFloat::IEEEhalf() ||
           &Val2.getSemantics() == &APFloat::IEEEsingle() ||
           &Val2.getSemantics() == &APFloat::IEEEdouble() ||
           &Val2.getSemantics() == &APFloat::IEEEquad();
  case Type::PPC_FP128TyID:
    return &Val2.getSemantics() == &APFloat::IEEEhalf() ||
           &Val2.getSemantics() == &APFloat::IEEEsingle() ||
           &Val2.getSemantics() == &APFloat::IEEEdouble() ||
           &Val2.getSemantics() == &APFloat::PPCDoubleDouble();
  }
}


//===----------------------------------------------------------------------===//
//                      Factory Function Implementation

ConstantAggregateZero *ConstantAggregateZero::get(Type *Ty) {
  assert((Ty->isStructTy() || Ty->isArrayTy() || Ty->isVectorTy()) &&
         "Cannot create an aggregate zero of non-aggregate type!");

  std::unique_ptr<ConstantAggregateZero> &Entry =
      Ty->getContext().pImpl->CAZConstants[Ty];
  if (!Entry)
    Entry.reset(new ConstantAggregateZero(Ty));

  return Entry.get();
}

/// Remove the constant from the constant table.
void ConstantAggregateZero::destroyConstantImpl() {
  getContext().pImpl->CAZConstants.erase(getType());
}

/// Remove the constant from the constant table.
void ConstantArray::destroyConstantImpl() {
  getType()->getContext().pImpl->ArrayConstants.remove(this);
}


//---- ConstantStruct::get() implementation...
//

/// Remove the constant from the constant table.
void ConstantStruct::destroyConstantImpl() {
  getType()->getContext().pImpl->StructConstants.remove(this);
}

/// Remove the constant from the constant table.
void ConstantVector::destroyConstantImpl() {
  getType()->getContext().pImpl->VectorConstants.remove(this);
}

Constant *Constant::getSplatValue() const {
  assert(this->getType()->isVectorTy() && "Only valid for vectors!");
  if (isa<ConstantAggregateZero>(this))
    return getNullValue(this->getType()->getVectorElementType());
  if (const ConstantDataVector *CV = dyn_cast<ConstantDataVector>(this))
    return CV->getSplatValue();
  if (const ConstantVector *CV = dyn_cast<ConstantVector>(this))
    return CV->getSplatValue();
  return nullptr;
}

Constant *ConstantVector::getSplatValue() const {
  // Check out first element.
  Constant *Elt = getOperand(0);
  // Then make sure all remaining elements point to the same value.
  for (unsigned I = 1, E = getNumOperands(); I < E; ++I)
    if (getOperand(I) != Elt)
      return nullptr;
  return Elt;
}

const APInt &Constant::getUniqueInteger() const {
  if (const ConstantInt *CI = dyn_cast<ConstantInt>(this))
    return CI->getValue();
  assert(this->getSplatValue() && "Doesn't contain a unique integer!");
  const Constant *C = this->getAggregateElement(0U);
  assert(C && isa<ConstantInt>(C) && "Not a vector of numbers!");
  return cast<ConstantInt>(C)->getValue();
}

//---- ConstantPointerNull::get() implementation.
//

ConstantPointerNull *ConstantPointerNull::get(PointerType *Ty) {
  std::unique_ptr<ConstantPointerNull> &Entry =
      Ty->getContext().pImpl->CPNConstants[Ty];
  if (!Entry)
    Entry.reset(new ConstantPointerNull(Ty));

  return Entry.get();
}

/// Remove the constant from the constant table.
void ConstantPointerNull::destroyConstantImpl() {
  getContext().pImpl->CPNConstants.erase(getType());
}

UndefValue *UndefValue::get(Type *Ty) {
  std::unique_ptr<UndefValue> &Entry = Ty->getContext().pImpl->UVConstants[Ty];
  if (!Entry)
    Entry.reset(new UndefValue(Ty));

  return Entry.get();
}

/// Remove the constant from the constant table.
void UndefValue::destroyConstantImpl() {
  // Free the constant and any dangling references to it.
  getContext().pImpl->UVConstants.erase(getType());
}

BlockAddress *BlockAddress::get(BasicBlock *BB) {
  assert(BB->getParent() && "Block must have a parent");
  return get(BB->getParent(), BB);
}

BlockAddress *BlockAddress::get(Function *F, BasicBlock *BB) {
  BlockAddress *&BA =
    F->getContext().pImpl->BlockAddresses[std::make_pair(F, BB)];
  if (!BA)
    BA = new BlockAddress(F, BB);

  assert(BA->getFunction() == F && "Basic block moved between functions");
  return BA;
}

BlockAddress::BlockAddress(Function *F, BasicBlock *BB)
: Constant(Type::getInt8PtrTy(F->getContext()), Value::BlockAddressVal,
           &Op<0>(), 2) {
  setOperand(0, F);
  setOperand(1, BB);
  BB->AdjustBlockAddressRefCount(1);
}

BlockAddress *BlockAddress::lookup(const BasicBlock *BB) {
  if (!BB->hasAddressTaken())
    return nullptr;

  const Function *F = BB->getParent();
  assert(F && "Block must have a parent");
  BlockAddress *BA =
      F->getContext().pImpl->BlockAddresses.lookup(std::make_pair(F, BB));
  assert(BA && "Refcount and block address map disagree!");
  return BA;
}

/// Remove the constant from the constant table.
void BlockAddress::destroyConstantImpl() {
  getFunction()->getType()->getContext().pImpl
    ->BlockAddresses.erase(std::make_pair(getFunction(), getBasicBlock()));
  getBasicBlock()->AdjustBlockAddressRefCount(-1);
}

Value *BlockAddress::handleOperandChangeImpl(Value *From, Value *To) {
  // This could be replacing either the Basic Block or the Function.  In either
  // case, we have to remove the map entry.
  Function *NewF = getFunction();
  BasicBlock *NewBB = getBasicBlock();

  if (From == NewF)
    NewF = cast<Function>(To->stripPointerCasts());
  else {
    assert(From == NewBB && "From does not match any operand");
    NewBB = cast<BasicBlock>(To);
  }

  // See if the 'new' entry already exists, if not, just update this in place
  // and return early.
  BlockAddress *&NewBA =
    getContext().pImpl->BlockAddresses[std::make_pair(NewF, NewBB)];
  if (NewBA)
    return NewBA;

  getBasicBlock()->AdjustBlockAddressRefCount(-1);

  // Remove the old entry, this can't cause the map to rehash (just a
  // tombstone will get added).
  getContext().pImpl->BlockAddresses.erase(std::make_pair(getFunction(),
                                                          getBasicBlock()));
  NewBA = this;
  setOperand(0, NewF);
  setOperand(1, NewBB);
  getBasicBlock()->AdjustBlockAddressRefCount(1);

  // If we just want to keep the existing value, then return null.
  // Callers know that this means we shouldn't delete this value.
  return nullptr;
}

//---- ConstantExpr::get() implementations.
//

/// This is a utility function to handle folding of casts and lookup of the
/// cast in the ExprConstants map. It is used by the various get* methods below.
static Constant *getFoldedCast(Instruction::CastOps opc, Constant *C, Type *Ty,
                               bool OnlyIfReduced = false) {
  assert(Ty->isFirstClassType() && "Cannot cast to an aggregate type!");
  // Fold a few common cases
  if (Constant *FC = ConstantFoldCastInstruction(opc, C, Ty))
    return FC;

  if (OnlyIfReduced)
    return nullptr;

  LLVMContextImpl *pImpl = Ty->getContext().pImpl;

  // Look up the constant in the table first to ensure uniqueness.
  ConstantExprKeyType Key(opc, C);

  return pImpl->ExprConstants.getOrCreate(Ty, Key);
}

Constant *ConstantExpr::getCast(unsigned oc, Constant *C, Type *Ty,
                                bool OnlyIfReduced) {
  Instruction::CastOps opc = Instruction::CastOps(oc);
  assert(Instruction::isCast(opc) && "opcode out of range");
  assert(C && Ty && "Null arguments to getCast");
  assert(CastInst::castIsValid(opc, C, Ty) && "Invalid constantexpr cast!");

  switch (opc) {
  default:
    llvm_unreachable("Invalid cast opcode");
  case Instruction::Trunc:
    return getTrunc(C, Ty, OnlyIfReduced);
  case Instruction::ZExt:
    return getZExt(C, Ty, OnlyIfReduced);
  case Instruction::SExt:
    return getSExt(C, Ty, OnlyIfReduced);
  case Instruction::FPTrunc:
    return getFPTrunc(C, Ty, OnlyIfReduced);
  case Instruction::FPExt:
    return getFPExtend(C, Ty, OnlyIfReduced);
  case Instruction::UIToFP:
    return getUIToFP(C, Ty, OnlyIfReduced);
  case Instruction::SIToFP:
    return getSIToFP(C, Ty, OnlyIfReduced);
  case Instruction::FPToUI:
    return getFPToUI(C, Ty, OnlyIfReduced);
  case Instruction::FPToSI:
    return getFPToSI(C, Ty, OnlyIfReduced);
  case Instruction::PtrToInt:
    return getPtrToInt(C, Ty, OnlyIfReduced);
  case Instruction::IntToPtr:
    return getIntToPtr(C, Ty, OnlyIfReduced);
  case Instruction::BitCast:
    return getBitCast(C, Ty, OnlyIfReduced);
  case Instruction::AddrSpaceCast:
    return getAddrSpaceCast(C, Ty, OnlyIfReduced);
  }
}

Constant *ConstantExpr::getZExtOrBitCast(Constant *C, Type *Ty) {
  if (C->getType()->getScalarSizeInBits() == Ty->getScalarSizeInBits())
    return getBitCast(C, Ty);
  return getZExt(C, Ty);
}

Constant *ConstantExpr::getSExtOrBitCast(Constant *C, Type *Ty) {
  if (C->getType()->getScalarSizeInBits() == Ty->getScalarSizeInBits())
    return getBitCast(C, Ty);
  return getSExt(C, Ty);
}

Constant *ConstantExpr::getTruncOrBitCast(Constant *C, Type *Ty) {
  if (C->getType()->getScalarSizeInBits() == Ty->getScalarSizeInBits())
    return getBitCast(C, Ty);
  return getTrunc(C, Ty);
}

Constant *ConstantExpr::getPointerCast(Constant *S, Type *Ty) {
  assert(S->getType()->isPtrOrPtrVectorTy() && "Invalid cast");
  assert((Ty->isIntOrIntVectorTy() || Ty->isPtrOrPtrVectorTy()) &&
          "Invalid cast");

  if (Ty->isIntOrIntVectorTy())
    return getPtrToInt(S, Ty);

  unsigned SrcAS = S->getType()->getPointerAddressSpace();
  if (Ty->isPtrOrPtrVectorTy() && SrcAS != Ty->getPointerAddressSpace())
    return getAddrSpaceCast(S, Ty);

  return getBitCast(S, Ty);
}

Constant *ConstantExpr::getPointerBitCastOrAddrSpaceCast(Constant *S,
                                                         Type *Ty) {
  assert(S->getType()->isPtrOrPtrVectorTy() && "Invalid cast");
  assert(Ty->isPtrOrPtrVectorTy() && "Invalid cast");

  if (S->getType()->getPointerAddressSpace() != Ty->getPointerAddressSpace())
    return getAddrSpaceCast(S, Ty);

  return getBitCast(S, Ty);
}

Constant *ConstantExpr::getIntegerCast(Constant *C, Type *Ty, bool isSigned) {
  assert(C->getType()->isIntOrIntVectorTy() &&
         Ty->isIntOrIntVectorTy() && "Invalid cast");
  unsigned SrcBits = C->getType()->getScalarSizeInBits();
  unsigned DstBits = Ty->getScalarSizeInBits();
  Instruction::CastOps opcode =
    (SrcBits == DstBits ? Instruction::BitCast :
     (SrcBits > DstBits ? Instruction::Trunc :
      (isSigned ? Instruction::SExt : Instruction::ZExt)));
  return getCast(opcode, C, Ty);
}

Constant *ConstantExpr::getFPCast(Constant *C, Type *Ty) {
  assert(C->getType()->isFPOrFPVectorTy() && Ty->isFPOrFPVectorTy() &&
         "Invalid cast");
  unsigned SrcBits = C->getType()->getScalarSizeInBits();
  unsigned DstBits = Ty->getScalarSizeInBits();
  if (SrcBits == DstBits)
    return C; // Avoid a useless cast
  Instruction::CastOps opcode =
    (SrcBits > DstBits ? Instruction::FPTrunc : Instruction::FPExt);
  return getCast(opcode, C, Ty);
}

Constant *ConstantExpr::getTrunc(Constant *C, Type *Ty, bool OnlyIfReduced) {
#ifndef NDEBUG
  bool fromVec = C->getType()->getTypeID() == Type::VectorTyID;
  bool toVec = Ty->getTypeID() == Type::VectorTyID;
#endif
  assert((fromVec == toVec) && "Cannot convert from scalar to/from vector");
  assert(C->getType()->isIntOrIntVectorTy() && "Trunc operand must be integer");
  assert(Ty->isIntOrIntVectorTy() && "Trunc produces only integral");
  assert(C->getType()->getScalarSizeInBits() > Ty->getScalarSizeInBits()&&
         "SrcTy must be larger than DestTy for Trunc!");

  return getFoldedCast(Instruction::Trunc, C, Ty, OnlyIfReduced);
}

Constant *ConstantExpr::getSExt(Constant *C, Type *Ty, bool OnlyIfReduced) {
#ifndef NDEBUG
  bool fromVec = C->getType()->getTypeID() == Type::VectorTyID;
  bool toVec = Ty->getTypeID() == Type::VectorTyID;
#endif
  assert((fromVec == toVec) && "Cannot convert from scalar to/from vector");
  assert(C->getType()->isIntOrIntVectorTy() && "SExt operand must be integral");
  assert(Ty->isIntOrIntVectorTy() && "SExt produces only integer");
  assert(C->getType()->getScalarSizeInBits() < Ty->getScalarSizeInBits()&&
         "SrcTy must be smaller than DestTy for SExt!");

  return getFoldedCast(Instruction::SExt, C, Ty, OnlyIfReduced);
}

Constant *ConstantExpr::getZExt(Constant *C, Type *Ty, bool OnlyIfReduced) {
#ifndef NDEBUG
  bool fromVec = C->getType()->getTypeID() == Type::VectorTyID;
  bool toVec = Ty->getTypeID() == Type::VectorTyID;
#endif
  assert((fromVec == toVec) && "Cannot convert from scalar to/from vector");
  assert(C->getType()->isIntOrIntVectorTy() && "ZEXt operand must be integral");
  assert(Ty->isIntOrIntVectorTy() && "ZExt produces only integer");
  assert(C->getType()->getScalarSizeInBits() < Ty->getScalarSizeInBits()&&
         "SrcTy must be smaller than DestTy for ZExt!");

  return getFoldedCast(Instruction::ZExt, C, Ty, OnlyIfReduced);
}

Constant *ConstantExpr::getFPTrunc(Constant *C, Type *Ty, bool OnlyIfReduced) {
#ifndef NDEBUG
  bool fromVec = C->getType()->getTypeID() == Type::VectorTyID;
  bool toVec = Ty->getTypeID() == Type::VectorTyID;
#endif
  assert((fromVec == toVec) && "Cannot convert from scalar to/from vector");
  assert(C->getType()->isFPOrFPVectorTy() && Ty->isFPOrFPVectorTy() &&
         C->getType()->getScalarSizeInBits() > Ty->getScalarSizeInBits()&&
         "This is an illegal floating point truncation!");
  return getFoldedCast(Instruction::FPTrunc, C, Ty, OnlyIfReduced);
}

Constant *ConstantExpr::getFPExtend(Constant *C, Type *Ty, bool OnlyIfReduced) {
#ifndef NDEBUG
  bool fromVec = C->getType()->getTypeID() == Type::VectorTyID;
  bool toVec = Ty->getTypeID() == Type::VectorTyID;
#endif
  assert((fromVec == toVec) && "Cannot convert from scalar to/from vector");
  assert(C->getType()->isFPOrFPVectorTy() && Ty->isFPOrFPVectorTy() &&
         C->getType()->getScalarSizeInBits() < Ty->getScalarSizeInBits()&&
         "This is an illegal floating point extension!");
  return getFoldedCast(Instruction::FPExt, C, Ty, OnlyIfReduced);
}

Constant *ConstantExpr::getUIToFP(Constant *C, Type *Ty, bool OnlyIfReduced) {
#ifndef NDEBUG
  bool fromVec = C->getType()->getTypeID() == Type::VectorTyID;
  bool toVec = Ty->getTypeID() == Type::VectorTyID;
#endif
  assert((fromVec == toVec) && "Cannot convert from scalar to/from vector");
  assert(C->getType()->isIntOrIntVectorTy() && Ty->isFPOrFPVectorTy() &&
         "This is an illegal uint to floating point cast!");
  return getFoldedCast(Instruction::UIToFP, C, Ty, OnlyIfReduced);
}

Constant *ConstantExpr::getSIToFP(Constant *C, Type *Ty, bool OnlyIfReduced) {
#ifndef NDEBUG
  bool fromVec = C->getType()->getTypeID() == Type::VectorTyID;
  bool toVec = Ty->getTypeID() == Type::VectorTyID;
#endif
  assert((fromVec == toVec) && "Cannot convert from scalar to/from vector");
  assert(C->getType()->isIntOrIntVectorTy() && Ty->isFPOrFPVectorTy() &&
         "This is an illegal sint to floating point cast!");
  return getFoldedCast(Instruction::SIToFP, C, Ty, OnlyIfReduced);
}

Constant *ConstantExpr::getFPToUI(Constant *C, Type *Ty, bool OnlyIfReduced) {
#ifndef NDEBUG
  bool fromVec = C->getType()->getTypeID() == Type::VectorTyID;
  bool toVec = Ty->getTypeID() == Type::VectorTyID;
#endif
  assert((fromVec == toVec) && "Cannot convert from scalar to/from vector");
  assert(C->getType()->isFPOrFPVectorTy() && Ty->isIntOrIntVectorTy() &&
         "This is an illegal floating point to uint cast!");
  return getFoldedCast(Instruction::FPToUI, C, Ty, OnlyIfReduced);
}

Constant *ConstantExpr::getFPToSI(Constant *C, Type *Ty, bool OnlyIfReduced) {
#ifndef NDEBUG
  bool fromVec = C->getType()->getTypeID() == Type::VectorTyID;
  bool toVec = Ty->getTypeID() == Type::VectorTyID;
#endif
  assert((fromVec == toVec) && "Cannot convert from scalar to/from vector");
  assert(C->getType()->isFPOrFPVectorTy() && Ty->isIntOrIntVectorTy() &&
         "This is an illegal floating point to sint cast!");
  return getFoldedCast(Instruction::FPToSI, C, Ty, OnlyIfReduced);
}

Constant *ConstantExpr::getPtrToInt(Constant *C, Type *DstTy,
                                    bool OnlyIfReduced) {
  assert(C->getType()->isPtrOrPtrVectorTy() &&
         "PtrToInt source must be pointer or pointer vector");
  assert(DstTy->isIntOrIntVectorTy() &&
         "PtrToInt destination must be integer or integer vector");
  assert(isa<VectorType>(C->getType()) == isa<VectorType>(DstTy));
  if (isa<VectorType>(C->getType()))
    assert(C->getType()->getVectorNumElements()==DstTy->getVectorNumElements()&&
           "Invalid cast between a different number of vector elements");
  return getFoldedCast(Instruction::PtrToInt, C, DstTy, OnlyIfReduced);
}

Constant *ConstantExpr::getIntToPtr(Constant *C, Type *DstTy,
                                    bool OnlyIfReduced) {
  assert(C->getType()->isIntOrIntVectorTy() &&
         "IntToPtr source must be integer or integer vector");
  assert(DstTy->isPtrOrPtrVectorTy() &&
         "IntToPtr destination must be a pointer or pointer vector");
  assert(isa<VectorType>(C->getType()) == isa<VectorType>(DstTy));
  if (isa<VectorType>(C->getType()))
    assert(C->getType()->getVectorNumElements()==DstTy->getVectorNumElements()&&
           "Invalid cast between a different number of vector elements");
  return getFoldedCast(Instruction::IntToPtr, C, DstTy, OnlyIfReduced);
}

Constant *ConstantExpr::getBitCast(Constant *C, Type *DstTy,
                                   bool OnlyIfReduced) {
  assert(CastInst::castIsValid(Instruction::BitCast, C, DstTy) &&
         "Invalid constantexpr bitcast!");

  // It is common to ask for a bitcast of a value to its own type, handle this
  // speedily.
  if (C->getType() == DstTy) return C;

  return getFoldedCast(Instruction::BitCast, C, DstTy, OnlyIfReduced);
}

Constant *ConstantExpr::getAddrSpaceCast(Constant *C, Type *DstTy,
                                         bool OnlyIfReduced) {
  assert(CastInst::castIsValid(Instruction::AddrSpaceCast, C, DstTy) &&
         "Invalid constantexpr addrspacecast!");

  // Canonicalize addrspacecasts between different pointer types by first
  // bitcasting the pointer type and then converting the address space.
  PointerType *SrcScalarTy = cast<PointerType>(C->getType()->getScalarType());
  PointerType *DstScalarTy = cast<PointerType>(DstTy->getScalarType());
  Type *DstElemTy = DstScalarTy->getElementType();
  if (SrcScalarTy->getElementType() != DstElemTy) {
    Type *MidTy = PointerType::get(DstElemTy, SrcScalarTy->getAddressSpace());
    if (VectorType *VT = dyn_cast<VectorType>(DstTy)) {
      // Handle vectors of pointers.
      MidTy = VectorType::get(MidTy, VT->getNumElements());
    }
    C = getBitCast(C, MidTy);
  }
  return getFoldedCast(Instruction::AddrSpaceCast, C, DstTy, OnlyIfReduced);
}

Constant *ConstantExpr::get(unsigned Opcode, Constant *C, unsigned Flags, 
                            Type *OnlyIfReducedTy) {
  // Check the operands for consistency first.
  assert(Instruction::isUnaryOp(Opcode) &&
         "Invalid opcode in unary constant expression");

#ifndef NDEBUG
  switch (Opcode) {
  case Instruction::FNeg:
    assert(C->getType()->isFPOrFPVectorTy() &&
           "Tried to create a floating-point operation on a "
           "non-floating-point type!");
    break;
  default:
    break;
  }
#endif

  // TODO: Try to constant fold operation.

  if (OnlyIfReducedTy == C->getType())
    return nullptr;

  Constant *ArgVec[] = { C };
  ConstantExprKeyType Key(Opcode, ArgVec, 0, Flags);

  LLVMContextImpl *pImpl = C->getContext().pImpl;
  return pImpl->ExprConstants.getOrCreate(C->getType(), Key);
}

Constant *ConstantExpr::get(unsigned Opcode, Constant *C1, Constant *C2,
                            unsigned Flags, Type *OnlyIfReducedTy) {
  // Check the operands for consistency first.
  assert(Instruction::isBinaryOp(Opcode) &&
         "Invalid opcode in binary constant expression");
  assert(C1->getType() == C2->getType() &&
         "Operand types in binary constant expression should match");

#ifndef NDEBUG
  switch (Opcode) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
    assert(C1->getType() == C2->getType() && "Op types should be identical!");
    assert(C1->getType()->isIntOrIntVectorTy() &&
           "Tried to create an integer operation on a non-integer type!");
    break;
  case Instruction::FAdd:
  case Instruction::FSub:
  case Instruction::FMul:
    assert(C1->getType() == C2->getType() && "Op types should be identical!");
    assert(C1->getType()->isFPOrFPVectorTy() &&
           "Tried to create a floating-point operation on a "
           "non-floating-point type!");
    break;
  case Instruction::UDiv:
  case Instruction::SDiv:
    assert(C1->getType() == C2->getType() && "Op types should be identical!");
    assert(C1->getType()->isIntOrIntVectorTy() &&
           "Tried to create an arithmetic operation on a non-arithmetic type!");
    break;
  case Instruction::FDiv:
    assert(C1->getType() == C2->getType() && "Op types should be identical!");
    assert(C1->getType()->isFPOrFPVectorTy() &&
           "Tried to create an arithmetic operation on a non-arithmetic type!");
    break;
  case Instruction::URem:
  case Instruction::SRem:
    assert(C1->getType() == C2->getType() && "Op types should be identical!");
    assert(C1->getType()->isIntOrIntVectorTy() &&
           "Tried to create an arithmetic operation on a non-arithmetic type!");
    break;
  case Instruction::FRem:
    assert(C1->getType() == C2->getType() && "Op types should be identical!");
    assert(C1->getType()->isFPOrFPVectorTy() &&
           "Tried to create an arithmetic operation on a non-arithmetic type!");
    break;
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
    assert(C1->getType() == C2->getType() && "Op types should be identical!");
    assert(C1->getType()->isIntOrIntVectorTy() &&
           "Tried to create a logical operation on a non-integral type!");
    break;
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
    assert(C1->getType() == C2->getType() && "Op types should be identical!");
    assert(C1->getType()->isIntOrIntVectorTy() &&
           "Tried to create a shift operation on a non-integer type!");
    break;
  default:
    break;
  }
#endif

  if (Constant *FC = ConstantFoldBinaryInstruction(Opcode, C1, C2))
    return FC;          // Fold a few common cases.

  if (OnlyIfReducedTy == C1->getType())
    return nullptr;

  Constant *ArgVec[] = { C1, C2 };
  ConstantExprKeyType Key(Opcode, ArgVec, 0, Flags);

  LLVMContextImpl *pImpl = C1->getContext().pImpl;
  return pImpl->ExprConstants.getOrCreate(C1->getType(), Key);
}

Constant *ConstantExpr::getSizeOf(Type* Ty) {
  // sizeof is implemented as: (i64) gep (Ty*)null, 1
  // Note that a non-inbounds gep is used, as null isn't within any object.
  Constant *GEPIdx = ConstantInt::get(Type::getInt32Ty(Ty->getContext()), 1);
  Constant *GEP = getGetElementPtr(
      Ty, Constant::getNullValue(PointerType::getUnqual(Ty)), GEPIdx);
  return getPtrToInt(GEP,
                     Type::getInt64Ty(Ty->getContext()));
}

Constant *ConstantExpr::getAlignOf(Type* Ty) {
  // alignof is implemented as: (i64) gep ({i1,Ty}*)null, 0, 1
  // Note that a non-inbounds gep is used, as null isn't within any object.
  Type *AligningTy = StructType::get(Type::getInt1Ty(Ty->getContext()), Ty);
  Constant *NullPtr = Constant::getNullValue(AligningTy->getPointerTo(0));
  Constant *Zero = ConstantInt::get(Type::getInt64Ty(Ty->getContext()), 0);
  Constant *One = ConstantInt::get(Type::getInt32Ty(Ty->getContext()), 1);
  Constant *Indices[2] = { Zero, One };
  Constant *GEP = getGetElementPtr(AligningTy, NullPtr, Indices);
  return getPtrToInt(GEP,
                     Type::getInt64Ty(Ty->getContext()));
}

Constant *ConstantExpr::getOffsetOf(StructType* STy, unsigned FieldNo) {
  return getOffsetOf(STy, ConstantInt::get(Type::getInt32Ty(STy->getContext()),
                                           FieldNo));
}

Constant *ConstantExpr::getOffsetOf(Type* Ty, Constant *FieldNo) {
  // offsetof is implemented as: (i64) gep (Ty*)null, 0, FieldNo
  // Note that a non-inbounds gep is used, as null isn't within any object.
  Constant *GEPIdx[] = {
    ConstantInt::get(Type::getInt64Ty(Ty->getContext()), 0),
    FieldNo
  };
  Constant *GEP = getGetElementPtr(
      Ty, Constant::getNullValue(PointerType::getUnqual(Ty)), GEPIdx);
  return getPtrToInt(GEP,
                     Type::getInt64Ty(Ty->getContext()));
}

Constant *ConstantExpr::getCompare(unsigned short Predicate, Constant *C1,
                                   Constant *C2, bool OnlyIfReduced) {
  assert(C1->getType() == C2->getType() && "Op types should be identical!");

  switch (Predicate) {
  default: llvm_unreachable("Invalid CmpInst predicate");
  case CmpInst::FCMP_FALSE: case CmpInst::FCMP_OEQ: case CmpInst::FCMP_OGT:
  case CmpInst::FCMP_OGE:   case CmpInst::FCMP_OLT: case CmpInst::FCMP_OLE:
  case CmpInst::FCMP_ONE:   case CmpInst::FCMP_ORD: case CmpInst::FCMP_UNO:
  case CmpInst::FCMP_UEQ:   case CmpInst::FCMP_UGT: case CmpInst::FCMP_UGE:
  case CmpInst::FCMP_ULT:   case CmpInst::FCMP_ULE: case CmpInst::FCMP_UNE:
  case CmpInst::FCMP_TRUE:
    return getFCmp(Predicate, C1, C2, OnlyIfReduced);

  case CmpInst::ICMP_EQ:  case CmpInst::ICMP_NE:  case CmpInst::ICMP_UGT:
  case CmpInst::ICMP_UGE: case CmpInst::ICMP_ULT: case CmpInst::ICMP_ULE:
  case CmpInst::ICMP_SGT: case CmpInst::ICMP_SGE: case CmpInst::ICMP_SLT:
  case CmpInst::ICMP_SLE:
    return getICmp(Predicate, C1, C2, OnlyIfReduced);
  }
}

Constant *ConstantExpr::getSelect(Constant *C, Constant *V1, Constant *V2,
                                  Type *OnlyIfReducedTy) {
  assert(!SelectInst::areInvalidOperands(C, V1, V2)&&"Invalid select operands");

  if (Constant *SC = ConstantFoldSelectInstruction(C, V1, V2))
    return SC;        // Fold common cases

  if (OnlyIfReducedTy == V1->getType())
    return nullptr;

  Constant *ArgVec[] = { C, V1, V2 };
  ConstantExprKeyType Key(Instruction::Select, ArgVec);

  LLVMContextImpl *pImpl = C->getContext().pImpl;
  return pImpl->ExprConstants.getOrCreate(V1->getType(), Key);
}

Constant *ConstantExpr::getGetElementPtr(Type *Ty, Constant *C,
                                         ArrayRef<Value *> Idxs, bool InBounds,
                                         Optional<unsigned> InRangeIndex,
                                         Type *OnlyIfReducedTy) {
  if (!Ty)
    Ty = cast<PointerType>(C->getType()->getScalarType())->getElementType();
  else
    assert(Ty ==
           cast<PointerType>(C->getType()->getScalarType())->getElementType());

  if (Constant *FC =
          ConstantFoldGetElementPtr(Ty, C, InBounds, InRangeIndex, Idxs))
    return FC;          // Fold a few common cases.

  // Get the result type of the getelementptr!
  Type *DestTy = GetElementPtrInst::getIndexedType(Ty, Idxs);
  assert(DestTy && "GEP indices invalid!");
  unsigned AS = C->getType()->getPointerAddressSpace();
  Type *ReqTy = DestTy->getPointerTo(AS);

  unsigned NumVecElts = 0;
  if (C->getType()->isVectorTy())
    NumVecElts = C->getType()->getVectorNumElements();
  else for (auto Idx : Idxs)
    if (Idx->getType()->isVectorTy())
      NumVecElts = Idx->getType()->getVectorNumElements();

  if (NumVecElts)
    ReqTy = VectorType::get(ReqTy, NumVecElts);

  if (OnlyIfReducedTy == ReqTy)
    return nullptr;

  // Look up the constant in the table first to ensure uniqueness
  std::vector<Constant*> ArgVec;
  ArgVec.reserve(1 + Idxs.size());
  ArgVec.push_back(C);
  for (unsigned i = 0, e = Idxs.size(); i != e; ++i) {
    assert((!Idxs[i]->getType()->isVectorTy() ||
            Idxs[i]->getType()->getVectorNumElements() == NumVecElts) &&
           "getelementptr index type missmatch");

    Constant *Idx = cast<Constant>(Idxs[i]);
    if (NumVecElts && !Idxs[i]->getType()->isVectorTy())
      Idx = ConstantVector::getSplat(NumVecElts, Idx);
    ArgVec.push_back(Idx);
  }

  unsigned SubClassOptionalData = InBounds ? GEPOperator::IsInBounds : 0;
  if (InRangeIndex && *InRangeIndex < 63)
    SubClassOptionalData |= (*InRangeIndex + 1) << 1;
  const ConstantExprKeyType Key(Instruction::GetElementPtr, ArgVec, 0,
                                SubClassOptionalData, None, Ty);

  LLVMContextImpl *pImpl = C->getContext().pImpl;
  return pImpl->ExprConstants.getOrCreate(ReqTy, Key);
}

Constant *ConstantExpr::getICmp(unsigned short pred, Constant *LHS,
                                Constant *RHS, bool OnlyIfReduced) {
  assert(LHS->getType() == RHS->getType());
  assert(CmpInst::isIntPredicate((CmpInst::Predicate)pred) &&
         "Invalid ICmp Predicate");

  if (Constant *FC = ConstantFoldCompareInstruction(pred, LHS, RHS))
    return FC;          // Fold a few common cases...

  if (OnlyIfReduced)
    return nullptr;

  // Look up the constant in the table first to ensure uniqueness
  Constant *ArgVec[] = { LHS, RHS };
  // Get the key type with both the opcode and predicate
  const ConstantExprKeyType Key(Instruction::ICmp, ArgVec, pred);

  Type *ResultTy = Type::getInt1Ty(LHS->getContext());
  if (VectorType *VT = dyn_cast<VectorType>(LHS->getType()))
    ResultTy = VectorType::get(ResultTy, VT->getNumElements());

  LLVMContextImpl *pImpl = LHS->getType()->getContext().pImpl;
  return pImpl->ExprConstants.getOrCreate(ResultTy, Key);
}

Constant *ConstantExpr::getFCmp(unsigned short pred, Constant *LHS,
                                Constant *RHS, bool OnlyIfReduced) {
  assert(LHS->getType() == RHS->getType());
  assert(CmpInst::isFPPredicate((CmpInst::Predicate)pred) &&
         "Invalid FCmp Predicate");

  if (Constant *FC = ConstantFoldCompareInstruction(pred, LHS, RHS))
    return FC;          // Fold a few common cases...

  if (OnlyIfReduced)
    return nullptr;

  // Look up the constant in the table first to ensure uniqueness
  Constant *ArgVec[] = { LHS, RHS };
  // Get the key type with both the opcode and predicate
  const ConstantExprKeyType Key(Instruction::FCmp, ArgVec, pred);

  Type *ResultTy = Type::getInt1Ty(LHS->getContext());
  if (VectorType *VT = dyn_cast<VectorType>(LHS->getType()))
    ResultTy = VectorType::get(ResultTy, VT->getNumElements());

  LLVMContextImpl *pImpl = LHS->getType()->getContext().pImpl;
  return pImpl->ExprConstants.getOrCreate(ResultTy, Key);
}

Constant *ConstantExpr::getExtractElement(Constant *Val, Constant *Idx,
                                          Type *OnlyIfReducedTy) {
  assert(Val->getType()->isVectorTy() &&
         "Tried to create extractelement operation on non-vector type!");
  assert(Idx->getType()->isIntegerTy() &&
         "Extractelement index must be an integer type!");

  if (Constant *FC = ConstantFoldExtractElementInstruction(Val, Idx))
    return FC;          // Fold a few common cases.

  Type *ReqTy = Val->getType()->getVectorElementType();
  if (OnlyIfReducedTy == ReqTy)
    return nullptr;

  // Look up the constant in the table first to ensure uniqueness
  Constant *ArgVec[] = { Val, Idx };
  const ConstantExprKeyType Key(Instruction::ExtractElement, ArgVec);

  LLVMContextImpl *pImpl = Val->getContext().pImpl;
  return pImpl->ExprConstants.getOrCreate(ReqTy, Key);
}

Constant *ConstantExpr::getInsertElement(Constant *Val, Constant *Elt,
                                         Constant *Idx, Type *OnlyIfReducedTy) {
  assert(Val->getType()->isVectorTy() &&
         "Tried to create insertelement operation on non-vector type!");
  assert(Elt->getType() == Val->getType()->getVectorElementType() &&
         "Insertelement types must match!");
  assert(Idx->getType()->isIntegerTy() &&
         "Insertelement index must be i32 type!");

  if (Constant *FC = ConstantFoldInsertElementInstruction(Val, Elt, Idx))
    return FC;          // Fold a few common cases.

  if (OnlyIfReducedTy == Val->getType())
    return nullptr;

  // Look up the constant in the table first to ensure uniqueness
  Constant *ArgVec[] = { Val, Elt, Idx };
  const ConstantExprKeyType Key(Instruction::InsertElement, ArgVec);

  LLVMContextImpl *pImpl = Val->getContext().pImpl;
  return pImpl->ExprConstants.getOrCreate(Val->getType(), Key);
}

Constant *ConstantExpr::getShuffleVector(Constant *V1, Constant *V2,
                                         Constant *Mask, Type *OnlyIfReducedTy) {
  assert(ShuffleVectorInst::isValidOperands(V1, V2, Mask) &&
         "Invalid shuffle vector constant expr operands!");

  if (Constant *FC = ConstantFoldShuffleVectorInstruction(V1, V2, Mask))
    return FC;          // Fold a few common cases.

  unsigned NElts = Mask->getType()->getVectorNumElements();
  Type *EltTy = V1->getType()->getVectorElementType();
  Type *ShufTy = VectorType::get(EltTy, NElts);

  if (OnlyIfReducedTy == ShufTy)
    return nullptr;

  // Look up the constant in the table first to ensure uniqueness
  Constant *ArgVec[] = { V1, V2, Mask };
  const ConstantExprKeyType Key(Instruction::ShuffleVector, ArgVec);

  LLVMContextImpl *pImpl = ShufTy->getContext().pImpl;
  return pImpl->ExprConstants.getOrCreate(ShufTy, Key);
}

Constant *ConstantExpr::getInsertValue(Constant *Agg, Constant *Val,
                                       ArrayRef<unsigned> Idxs,
                                       Type *OnlyIfReducedTy) {
  assert(Agg->getType()->isFirstClassType() &&
         "Non-first-class type for constant insertvalue expression");

  assert(ExtractValueInst::getIndexedType(Agg->getType(),
                                          Idxs) == Val->getType() &&
         "insertvalue indices invalid!");
  Type *ReqTy = Val->getType();

  if (Constant *FC = ConstantFoldInsertValueInstruction(Agg, Val, Idxs))
    return FC;

  if (OnlyIfReducedTy == ReqTy)
    return nullptr;

  Constant *ArgVec[] = { Agg, Val };
  const ConstantExprKeyType Key(Instruction::InsertValue, ArgVec, 0, 0, Idxs);

  LLVMContextImpl *pImpl = Agg->getContext().pImpl;
  return pImpl->ExprConstants.getOrCreate(ReqTy, Key);
}

Constant *ConstantExpr::getExtractValue(Constant *Agg, ArrayRef<unsigned> Idxs,
                                        Type *OnlyIfReducedTy) {
  assert(Agg->getType()->isFirstClassType() &&
         "Tried to create extractelement operation on non-first-class type!");

  Type *ReqTy = ExtractValueInst::getIndexedType(Agg->getType(), Idxs);
  (void)ReqTy;
  assert(ReqTy && "extractvalue indices invalid!");

  assert(Agg->getType()->isFirstClassType() &&
         "Non-first-class type for constant extractvalue expression");
  if (Constant *FC = ConstantFoldExtractValueInstruction(Agg, Idxs))
    return FC;

  if (OnlyIfReducedTy == ReqTy)
    return nullptr;

  Constant *ArgVec[] = { Agg };
  const ConstantExprKeyType Key(Instruction::ExtractValue, ArgVec, 0, 0, Idxs);

  LLVMContextImpl *pImpl = Agg->getContext().pImpl;
  return pImpl->ExprConstants.getOrCreate(ReqTy, Key);
}

Constant *ConstantExpr::getNeg(Constant *C, bool HasNUW, bool HasNSW) {
  assert(C->getType()->isIntOrIntVectorTy() &&
         "Cannot NEG a nonintegral value!");
  return getSub(ConstantFP::getZeroValueForNegation(C->getType()),
                C, HasNUW, HasNSW);
}

Constant *ConstantExpr::getFNeg(Constant *C) {
  assert(C->getType()->isFPOrFPVectorTy() &&
         "Cannot FNEG a non-floating-point value!");
  return getFSub(ConstantFP::getZeroValueForNegation(C->getType()), C);
}

Constant *ConstantExpr::getNot(Constant *C) {
  assert(C->getType()->isIntOrIntVectorTy() &&
         "Cannot NOT a nonintegral value!");
  return get(Instruction::Xor, C, Constant::getAllOnesValue(C->getType()));
}

Constant *ConstantExpr::getAdd(Constant *C1, Constant *C2,
                               bool HasNUW, bool HasNSW) {
  unsigned Flags = (HasNUW ? OverflowingBinaryOperator::NoUnsignedWrap : 0) |
                   (HasNSW ? OverflowingBinaryOperator::NoSignedWrap   : 0);
  return get(Instruction::Add, C1, C2, Flags);
}

Constant *ConstantExpr::getFAdd(Constant *C1, Constant *C2) {
  return get(Instruction::FAdd, C1, C2);
}

Constant *ConstantExpr::getSub(Constant *C1, Constant *C2,
                               bool HasNUW, bool HasNSW) {
  unsigned Flags = (HasNUW ? OverflowingBinaryOperator::NoUnsignedWrap : 0) |
                   (HasNSW ? OverflowingBinaryOperator::NoSignedWrap   : 0);
  return get(Instruction::Sub, C1, C2, Flags);
}

Constant *ConstantExpr::getFSub(Constant *C1, Constant *C2) {
  return get(Instruction::FSub, C1, C2);
}

Constant *ConstantExpr::getMul(Constant *C1, Constant *C2,
                               bool HasNUW, bool HasNSW) {
  unsigned Flags = (HasNUW ? OverflowingBinaryOperator::NoUnsignedWrap : 0) |
                   (HasNSW ? OverflowingBinaryOperator::NoSignedWrap   : 0);
  return get(Instruction::Mul, C1, C2, Flags);
}

Constant *ConstantExpr::getFMul(Constant *C1, Constant *C2) {
  return get(Instruction::FMul, C1, C2);
}

Constant *ConstantExpr::getUDiv(Constant *C1, Constant *C2, bool isExact) {
  return get(Instruction::UDiv, C1, C2,
             isExact ? PossiblyExactOperator::IsExact : 0);
}

Constant *ConstantExpr::getSDiv(Constant *C1, Constant *C2, bool isExact) {
  return get(Instruction::SDiv, C1, C2,
             isExact ? PossiblyExactOperator::IsExact : 0);
}

Constant *ConstantExpr::getFDiv(Constant *C1, Constant *C2) {
  return get(Instruction::FDiv, C1, C2);
}

Constant *ConstantExpr::getURem(Constant *C1, Constant *C2) {
  return get(Instruction::URem, C1, C2);
}

Constant *ConstantExpr::getSRem(Constant *C1, Constant *C2) {
  return get(Instruction::SRem, C1, C2);
}

Constant *ConstantExpr::getFRem(Constant *C1, Constant *C2) {
  return get(Instruction::FRem, C1, C2);
}

Constant *ConstantExpr::getAnd(Constant *C1, Constant *C2) {
  return get(Instruction::And, C1, C2);
}

Constant *ConstantExpr::getOr(Constant *C1, Constant *C2) {
  return get(Instruction::Or, C1, C2);
}

Constant *ConstantExpr::getXor(Constant *C1, Constant *C2) {
  return get(Instruction::Xor, C1, C2);
}

Constant *ConstantExpr::getShl(Constant *C1, Constant *C2,
                               bool HasNUW, bool HasNSW) {
  unsigned Flags = (HasNUW ? OverflowingBinaryOperator::NoUnsignedWrap : 0) |
                   (HasNSW ? OverflowingBinaryOperator::NoSignedWrap   : 0);
  return get(Instruction::Shl, C1, C2, Flags);
}

Constant *ConstantExpr::getLShr(Constant *C1, Constant *C2, bool isExact) {
  return get(Instruction::LShr, C1, C2,
             isExact ? PossiblyExactOperator::IsExact : 0);
}

Constant *ConstantExpr::getAShr(Constant *C1, Constant *C2, bool isExact) {
  return get(Instruction::AShr, C1, C2,
             isExact ? PossiblyExactOperator::IsExact : 0);
}

Constant *ConstantExpr::getBinOpIdentity(unsigned Opcode, Type *Ty,
                                         bool AllowRHSConstant) {
  assert(Instruction::isBinaryOp(Opcode) && "Only binops allowed");

  // Commutative opcodes: it does not matter if AllowRHSConstant is set.
  if (Instruction::isCommutative(Opcode)) {
    switch (Opcode) {
      case Instruction::Add: // X + 0 = X
      case Instruction::Or:  // X | 0 = X
      case Instruction::Xor: // X ^ 0 = X
        return Constant::getNullValue(Ty);
      case Instruction::Mul: // X * 1 = X
        return ConstantInt::get(Ty, 1);
      case Instruction::And: // X & -1 = X
        return Constant::getAllOnesValue(Ty);
      case Instruction::FAdd: // X + -0.0 = X
        // TODO: If the fadd has 'nsz', should we return +0.0?
        return ConstantFP::getNegativeZero(Ty);
      case Instruction::FMul: // X * 1.0 = X
        return ConstantFP::get(Ty, 1.0);
      default:
        llvm_unreachable("Every commutative binop has an identity constant");
    }
  }

  // Non-commutative opcodes: AllowRHSConstant must be set.
  if (!AllowRHSConstant)
    return nullptr;

  switch (Opcode) {
    case Instruction::Sub:  // X - 0 = X
    case Instruction::Shl:  // X << 0 = X
    case Instruction::LShr: // X >>u 0 = X
    case Instruction::AShr: // X >> 0 = X
    case Instruction::FSub: // X - 0.0 = X
      return Constant::getNullValue(Ty);
    case Instruction::SDiv: // X / 1 = X
    case Instruction::UDiv: // X /u 1 = X
      return ConstantInt::get(Ty, 1);
    case Instruction::FDiv: // X / 1.0 = X
      return ConstantFP::get(Ty, 1.0);
    default:
      return nullptr;
  }
}

Constant *ConstantExpr::getBinOpAbsorber(unsigned Opcode, Type *Ty) {
  switch (Opcode) {
  default:
    // Doesn't have an absorber.
    return nullptr;

  case Instruction::Or:
    return Constant::getAllOnesValue(Ty);

  case Instruction::And:
  case Instruction::Mul:
    return Constant::getNullValue(Ty);
  }
}

/// Remove the constant from the constant table.
void ConstantExpr::destroyConstantImpl() {
  getType()->getContext().pImpl->ExprConstants.remove(this);
}

const char *ConstantExpr::getOpcodeName() const {
  return Instruction::getOpcodeName(getOpcode());
}

GetElementPtrConstantExpr::GetElementPtrConstantExpr(
    Type *SrcElementTy, Constant *C, ArrayRef<Constant *> IdxList, Type *DestTy)
    : ConstantExpr(DestTy, Instruction::GetElementPtr,
                   OperandTraits<GetElementPtrConstantExpr>::op_end(this) -
                       (IdxList.size() + 1),
                   IdxList.size() + 1),
      SrcElementTy(SrcElementTy),
      ResElementTy(GetElementPtrInst::getIndexedType(SrcElementTy, IdxList)) {
  Op<0>() = C;
  Use *OperandList = getOperandList();
  for (unsigned i = 0, E = IdxList.size(); i != E; ++i)
    OperandList[i+1] = IdxList[i];
}

Type *GetElementPtrConstantExpr::getSourceElementType() const {
  return SrcElementTy;
}

Type *GetElementPtrConstantExpr::getResultElementType() const {
  return ResElementTy;
}

//===----------------------------------------------------------------------===//
//                       ConstantData* implementations

Type *ConstantDataSequential::getElementType() const {
  return getType()->getElementType();
}

StringRef ConstantDataSequential::getRawDataValues() const {
  return StringRef(DataElements, getNumElements()*getElementByteSize());
}

bool ConstantDataSequential::isElementTypeCompatible(Type *Ty) {
  if (Ty->isHalfTy() || Ty->isFloatTy() || Ty->isDoubleTy()) return true;
  if (auto *IT = dyn_cast<IntegerType>(Ty)) {
    switch (IT->getBitWidth()) {
    case 8:
    case 16:
    case 32:
    case 64:
      return true;
    default: break;
    }
  }
  return false;
}

unsigned ConstantDataSequential::getNumElements() const {
  if (ArrayType *AT = dyn_cast<ArrayType>(getType()))
    return AT->getNumElements();
  return getType()->getVectorNumElements();
}


uint64_t ConstantDataSequential::getElementByteSize() const {
  return getElementType()->getPrimitiveSizeInBits()/8;
}

/// Return the start of the specified element.
const char *ConstantDataSequential::getElementPointer(unsigned Elt) const {
  assert(Elt < getNumElements() && "Invalid Elt");
  return DataElements+Elt*getElementByteSize();
}


/// Return true if the array is empty or all zeros.
static bool isAllZeros(StringRef Arr) {
  for (char I : Arr)
    if (I != 0)
      return false;
  return true;
}

/// This is the underlying implementation of all of the
/// ConstantDataSequential::get methods.  They all thunk down to here, providing
/// the correct element type.  We take the bytes in as a StringRef because
/// we *want* an underlying "char*" to avoid TBAA type punning violations.
Constant *ConstantDataSequential::getImpl(StringRef Elements, Type *Ty) {
  assert(isElementTypeCompatible(Ty->getSequentialElementType()));
  // If the elements are all zero or there are no elements, return a CAZ, which
  // is more dense and canonical.
  if (isAllZeros(Elements))
    return ConstantAggregateZero::get(Ty);

  // Do a lookup to see if we have already formed one of these.
  auto &Slot =
      *Ty->getContext()
           .pImpl->CDSConstants.insert(std::make_pair(Elements, nullptr))
           .first;

  // The bucket can point to a linked list of different CDS's that have the same
  // body but different types.  For example, 0,0,0,1 could be a 4 element array
  // of i8, or a 1-element array of i32.  They'll both end up in the same
  /// StringMap bucket, linked up by their Next pointers.  Walk the list.
  ConstantDataSequential **Entry = &Slot.second;
  for (ConstantDataSequential *Node = *Entry; Node;
       Entry = &Node->Next, Node = *Entry)
    if (Node->getType() == Ty)
      return Node;

  // Okay, we didn't get a hit.  Create a node of the right class, link it in,
  // and return it.
  if (isa<ArrayType>(Ty))
    return *Entry = new ConstantDataArray(Ty, Slot.first().data());

  assert(isa<VectorType>(Ty));
  return *Entry = new ConstantDataVector(Ty, Slot.first().data());
}

void ConstantDataSequential::destroyConstantImpl() {
  // Remove the constant from the StringMap.
  StringMap<ConstantDataSequential*> &CDSConstants =
    getType()->getContext().pImpl->CDSConstants;

  StringMap<ConstantDataSequential*>::iterator Slot =
    CDSConstants.find(getRawDataValues());

  assert(Slot != CDSConstants.end() && "CDS not found in uniquing table");

  ConstantDataSequential **Entry = &Slot->getValue();

  // Remove the entry from the hash table.
  if (!(*Entry)->Next) {
    // If there is only one value in the bucket (common case) it must be this
    // entry, and removing the entry should remove the bucket completely.
    assert((*Entry) == this && "Hash mismatch in ConstantDataSequential");
    getContext().pImpl->CDSConstants.erase(Slot);
  } else {
    // Otherwise, there are multiple entries linked off the bucket, unlink the
    // node we care about but keep the bucket around.
    for (ConstantDataSequential *Node = *Entry; ;
         Entry = &Node->Next, Node = *Entry) {
      assert(Node && "Didn't find entry in its uniquing hash table!");
      // If we found our entry, unlink it from the list and we're done.
      if (Node == this) {
        *Entry = Node->Next;
        break;
      }
    }
  }

  // If we were part of a list, make sure that we don't delete the list that is
  // still owned by the uniquing map.
  Next = nullptr;
}

/// getFP() constructors - Return a constant with array type with an element
/// count and element type of float with precision matching the number of
/// bits in the ArrayRef passed in. (i.e. half for 16bits, float for 32bits,
/// double for 64bits) Note that this can return a ConstantAggregateZero
/// object.
Constant *ConstantDataArray::getFP(LLVMContext &Context,
                                   ArrayRef<uint16_t> Elts) {
  Type *Ty = ArrayType::get(Type::getHalfTy(Context), Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 2), Ty);
}
Constant *ConstantDataArray::getFP(LLVMContext &Context,
                                   ArrayRef<uint32_t> Elts) {
  Type *Ty = ArrayType::get(Type::getFloatTy(Context), Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 4), Ty);
}
Constant *ConstantDataArray::getFP(LLVMContext &Context,
                                   ArrayRef<uint64_t> Elts) {
  Type *Ty = ArrayType::get(Type::getDoubleTy(Context), Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 8), Ty);
}

Constant *ConstantDataArray::getString(LLVMContext &Context,
                                       StringRef Str, bool AddNull) {
  if (!AddNull) {
    const uint8_t *Data = reinterpret_cast<const uint8_t *>(Str.data());
    return get(Context, makeArrayRef(Data, Str.size()));
  }

  SmallVector<uint8_t, 64> ElementVals;
  ElementVals.append(Str.begin(), Str.end());
  ElementVals.push_back(0);
  return get(Context, ElementVals);
}

/// get() constructors - Return a constant with vector type with an element
/// count and element type matching the ArrayRef passed in.  Note that this
/// can return a ConstantAggregateZero object.
Constant *ConstantDataVector::get(LLVMContext &Context, ArrayRef<uint8_t> Elts){
  Type *Ty = VectorType::get(Type::getInt8Ty(Context), Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 1), Ty);
}
Constant *ConstantDataVector::get(LLVMContext &Context, ArrayRef<uint16_t> Elts){
  Type *Ty = VectorType::get(Type::getInt16Ty(Context), Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 2), Ty);
}
Constant *ConstantDataVector::get(LLVMContext &Context, ArrayRef<uint32_t> Elts){
  Type *Ty = VectorType::get(Type::getInt32Ty(Context), Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 4), Ty);
}
Constant *ConstantDataVector::get(LLVMContext &Context, ArrayRef<uint64_t> Elts){
  Type *Ty = VectorType::get(Type::getInt64Ty(Context), Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 8), Ty);
}
Constant *ConstantDataVector::get(LLVMContext &Context, ArrayRef<float> Elts) {
  Type *Ty = VectorType::get(Type::getFloatTy(Context), Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 4), Ty);
}
Constant *ConstantDataVector::get(LLVMContext &Context, ArrayRef<double> Elts) {
  Type *Ty = VectorType::get(Type::getDoubleTy(Context), Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 8), Ty);
}

/// getFP() constructors - Return a constant with vector type with an element
/// count and element type of float with the precision matching the number of
/// bits in the ArrayRef passed in.  (i.e. half for 16bits, float for 32bits,
/// double for 64bits) Note that this can return a ConstantAggregateZero
/// object.
Constant *ConstantDataVector::getFP(LLVMContext &Context,
                                    ArrayRef<uint16_t> Elts) {
  Type *Ty = VectorType::get(Type::getHalfTy(Context), Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 2), Ty);
}
Constant *ConstantDataVector::getFP(LLVMContext &Context,
                                    ArrayRef<uint32_t> Elts) {
  Type *Ty = VectorType::get(Type::getFloatTy(Context), Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 4), Ty);
}
Constant *ConstantDataVector::getFP(LLVMContext &Context,
                                    ArrayRef<uint64_t> Elts) {
  Type *Ty = VectorType::get(Type::getDoubleTy(Context), Elts.size());
  const char *Data = reinterpret_cast<const char *>(Elts.data());
  return getImpl(StringRef(Data, Elts.size() * 8), Ty);
}

Constant *ConstantDataVector::getSplat(unsigned NumElts, Constant *V) {
  assert(isElementTypeCompatible(V->getType()) &&
         "Element type not compatible with ConstantData");
  if (ConstantInt *CI = dyn_cast<ConstantInt>(V)) {
    if (CI->getType()->isIntegerTy(8)) {
      SmallVector<uint8_t, 16> Elts(NumElts, CI->getZExtValue());
      return get(V->getContext(), Elts);
    }
    if (CI->getType()->isIntegerTy(16)) {
      SmallVector<uint16_t, 16> Elts(NumElts, CI->getZExtValue());
      return get(V->getContext(), Elts);
    }
    if (CI->getType()->isIntegerTy(32)) {
      SmallVector<uint32_t, 16> Elts(NumElts, CI->getZExtValue());
      return get(V->getContext(), Elts);
    }
    assert(CI->getType()->isIntegerTy(64) && "Unsupported ConstantData type");
    SmallVector<uint64_t, 16> Elts(NumElts, CI->getZExtValue());
    return get(V->getContext(), Elts);
  }

  if (ConstantFP *CFP = dyn_cast<ConstantFP>(V)) {
    if (CFP->getType()->isHalfTy()) {
      SmallVector<uint16_t, 16> Elts(
          NumElts, CFP->getValueAPF().bitcastToAPInt().getLimitedValue());
      return getFP(V->getContext(), Elts);
    }
    if (CFP->getType()->isFloatTy()) {
      SmallVector<uint32_t, 16> Elts(
          NumElts, CFP->getValueAPF().bitcastToAPInt().getLimitedValue());
      return getFP(V->getContext(), Elts);
    }
    if (CFP->getType()->isDoubleTy()) {
      SmallVector<uint64_t, 16> Elts(
          NumElts, CFP->getValueAPF().bitcastToAPInt().getLimitedValue());
      return getFP(V->getContext(), Elts);
    }
  }
  return ConstantVector::getSplat(NumElts, V);
}


uint64_t ConstantDataSequential::getElementAsInteger(unsigned Elt) const {
  assert(isa<IntegerType>(getElementType()) &&
         "Accessor can only be used when element is an integer");
  const char *EltPtr = getElementPointer(Elt);

  // The data is stored in host byte order, make sure to cast back to the right
  // type to load with the right endianness.
  switch (getElementType()->getIntegerBitWidth()) {
  default: llvm_unreachable("Invalid bitwidth for CDS");
  case 8:
    return *reinterpret_cast<const uint8_t *>(EltPtr);
  case 16:
    return *reinterpret_cast<const uint16_t *>(EltPtr);
  case 32:
    return *reinterpret_cast<const uint32_t *>(EltPtr);
  case 64:
    return *reinterpret_cast<const uint64_t *>(EltPtr);
  }
}

APInt ConstantDataSequential::getElementAsAPInt(unsigned Elt) const {
  assert(isa<IntegerType>(getElementType()) &&
         "Accessor can only be used when element is an integer");
  const char *EltPtr = getElementPointer(Elt);

  // The data is stored in host byte order, make sure to cast back to the right
  // type to load with the right endianness.
  switch (getElementType()->getIntegerBitWidth()) {
  default: llvm_unreachable("Invalid bitwidth for CDS");
  case 8: {
    auto EltVal = *reinterpret_cast<const uint8_t *>(EltPtr);
    return APInt(8, EltVal);
  }
  case 16: {
    auto EltVal = *reinterpret_cast<const uint16_t *>(EltPtr);
    return APInt(16, EltVal);
  }
  case 32: {
    auto EltVal = *reinterpret_cast<const uint32_t *>(EltPtr);
    return APInt(32, EltVal);
  }
  case 64: {
    auto EltVal = *reinterpret_cast<const uint64_t *>(EltPtr);
    return APInt(64, EltVal);
  }
  }
}

APFloat ConstantDataSequential::getElementAsAPFloat(unsigned Elt) const {
  const char *EltPtr = getElementPointer(Elt);

  switch (getElementType()->getTypeID()) {
  default:
    llvm_unreachable("Accessor can only be used when element is float/double!");
  case Type::HalfTyID: {
    auto EltVal = *reinterpret_cast<const uint16_t *>(EltPtr);
    return APFloat(APFloat::IEEEhalf(), APInt(16, EltVal));
  }
  case Type::FloatTyID: {
    auto EltVal = *reinterpret_cast<const uint32_t *>(EltPtr);
    return APFloat(APFloat::IEEEsingle(), APInt(32, EltVal));
  }
  case Type::DoubleTyID: {
    auto EltVal = *reinterpret_cast<const uint64_t *>(EltPtr);
    return APFloat(APFloat::IEEEdouble(), APInt(64, EltVal));
  }
  }
}

float ConstantDataSequential::getElementAsFloat(unsigned Elt) const {
  assert(getElementType()->isFloatTy() &&
         "Accessor can only be used when element is a 'float'");
  return *reinterpret_cast<const float *>(getElementPointer(Elt));
}

double ConstantDataSequential::getElementAsDouble(unsigned Elt) const {
  assert(getElementType()->isDoubleTy() &&
         "Accessor can only be used when element is a 'float'");
  return *reinterpret_cast<const double *>(getElementPointer(Elt));
}

Constant *ConstantDataSequential::getElementAsConstant(unsigned Elt) const {
  if (getElementType()->isHalfTy() || getElementType()->isFloatTy() ||
      getElementType()->isDoubleTy())
    return ConstantFP::get(getContext(), getElementAsAPFloat(Elt));

  return ConstantInt::get(getElementType(), getElementAsInteger(Elt));
}

bool ConstantDataSequential::isString(unsigned CharSize) const {
  return isa<ArrayType>(getType()) && getElementType()->isIntegerTy(CharSize);
}

bool ConstantDataSequential::isCString() const {
  if (!isString())
    return false;

  StringRef Str = getAsString();

  // The last value must be nul.
  if (Str.back() != 0) return false;

  // Other elements must be non-nul.
  return Str.drop_back().find(0) == StringRef::npos;
}

bool ConstantDataVector::isSplat() const {
  const char *Base = getRawDataValues().data();

  // Compare elements 1+ to the 0'th element.
  unsigned EltSize = getElementByteSize();
  for (unsigned i = 1, e = getNumElements(); i != e; ++i)
    if (memcmp(Base, Base+i*EltSize, EltSize))
      return false;

  return true;
}

Constant *ConstantDataVector::getSplatValue() const {
  // If they're all the same, return the 0th one as a representative.
  return isSplat() ? getElementAsConstant(0) : nullptr;
}

//===----------------------------------------------------------------------===//
//                handleOperandChange implementations

/// Update this constant array to change uses of
/// 'From' to be uses of 'To'.  This must update the uniquing data structures
/// etc.
///
/// Note that we intentionally replace all uses of From with To here.  Consider
/// a large array that uses 'From' 1000 times.  By handling this case all here,
/// ConstantArray::handleOperandChange is only invoked once, and that
/// single invocation handles all 1000 uses.  Handling them one at a time would
/// work, but would be really slow because it would have to unique each updated
/// array instance.
///
void Constant::handleOperandChange(Value *From, Value *To) {
  Value *Replacement = nullptr;
  switch (getValueID()) {
  default:
    llvm_unreachable("Not a constant!");
#define HANDLE_CONSTANT(Name)                                                  \
  case Value::Name##Val:                                                       \
    Replacement = cast<Name>(this)->handleOperandChangeImpl(From, To);         \
    break;
#include "llvm/IR/Value.def"
  }

  // If handleOperandChangeImpl returned nullptr, then it handled
  // replacing itself and we don't want to delete or replace anything else here.
  if (!Replacement)
    return;

  // I do need to replace this with an existing value.
  assert(Replacement != this && "I didn't contain From!");

  // Everyone using this now uses the replacement.
  replaceAllUsesWith(Replacement);

  // Delete the old constant!
  destroyConstant();
}

Value *ConstantArray::handleOperandChangeImpl(Value *From, Value *To) {
  assert(isa<Constant>(To) && "Cannot make Constant refer to non-constant!");
  Constant *ToC = cast<Constant>(To);

  SmallVector<Constant*, 8> Values;
  Values.reserve(getNumOperands());  // Build replacement array.

  // Fill values with the modified operands of the constant array.  Also,
  // compute whether this turns into an all-zeros array.
  unsigned NumUpdated = 0;

  // Keep track of whether all the values in the array are "ToC".
  bool AllSame = true;
  Use *OperandList = getOperandList();
  unsigned OperandNo = 0;
  for (Use *O = OperandList, *E = OperandList+getNumOperands(); O != E; ++O) {
    Constant *Val = cast<Constant>(O->get());
    if (Val == From) {
      OperandNo = (O - OperandList);
      Val = ToC;
      ++NumUpdated;
    }
    Values.push_back(Val);
    AllSame &= Val == ToC;
  }

  if (AllSame && ToC->isNullValue())
    return ConstantAggregateZero::get(getType());

  if (AllSame && isa<UndefValue>(ToC))
    return UndefValue::get(getType());

  // Check for any other type of constant-folding.
  if (Constant *C = getImpl(getType(), Values))
    return C;

  // Update to the new value.
  return getContext().pImpl->ArrayConstants.replaceOperandsInPlace(
      Values, this, From, ToC, NumUpdated, OperandNo);
}

Value *ConstantStruct::handleOperandChangeImpl(Value *From, Value *To) {
  assert(isa<Constant>(To) && "Cannot make Constant refer to non-constant!");
  Constant *ToC = cast<Constant>(To);

  Use *OperandList = getOperandList();

  SmallVector<Constant*, 8> Values;
  Values.reserve(getNumOperands());  // Build replacement struct.

  // Fill values with the modified operands of the constant struct.  Also,
  // compute whether this turns into an all-zeros struct.
  unsigned NumUpdated = 0;
  bool AllSame = true;
  unsigned OperandNo = 0;
  for (Use *O = OperandList, *E = OperandList + getNumOperands(); O != E; ++O) {
    Constant *Val = cast<Constant>(O->get());
    if (Val == From) {
      OperandNo = (O - OperandList);
      Val = ToC;
      ++NumUpdated;
    }
    Values.push_back(Val);
    AllSame &= Val == ToC;
  }

  if (AllSame && ToC->isNullValue())
    return ConstantAggregateZero::get(getType());

  if (AllSame && isa<UndefValue>(ToC))
    return UndefValue::get(getType());

  // Update to the new value.
  return getContext().pImpl->StructConstants.replaceOperandsInPlace(
      Values, this, From, ToC, NumUpdated, OperandNo);
}

Value *ConstantVector::handleOperandChangeImpl(Value *From, Value *To) {
  assert(isa<Constant>(To) && "Cannot make Constant refer to non-constant!");
  Constant *ToC = cast<Constant>(To);

  SmallVector<Constant*, 8> Values;
  Values.reserve(getNumOperands());  // Build replacement array...
  unsigned NumUpdated = 0;
  unsigned OperandNo = 0;
  for (unsigned i = 0, e = getNumOperands(); i != e; ++i) {
    Constant *Val = getOperand(i);
    if (Val == From) {
      OperandNo = i;
      ++NumUpdated;
      Val = ToC;
    }
    Values.push_back(Val);
  }

  if (Constant *C = getImpl(Values))
    return C;

  // Update to the new value.
  return getContext().pImpl->VectorConstants.replaceOperandsInPlace(
      Values, this, From, ToC, NumUpdated, OperandNo);
}

Value *ConstantExpr::handleOperandChangeImpl(Value *From, Value *ToV) {
  assert(isa<Constant>(ToV) && "Cannot make Constant refer to non-constant!");
  Constant *To = cast<Constant>(ToV);

  SmallVector<Constant*, 8> NewOps;
  unsigned NumUpdated = 0;
  unsigned OperandNo = 0;
  for (unsigned i = 0, e = getNumOperands(); i != e; ++i) {
    Constant *Op = getOperand(i);
    if (Op == From) {
      OperandNo = i;
      ++NumUpdated;
      Op = To;
    }
    NewOps.push_back(Op);
  }
  assert(NumUpdated && "I didn't contain From!");

  if (Constant *C = getWithOperands(NewOps, getType(), true))
    return C;

  // Update to the new value.
  return getContext().pImpl->ExprConstants.replaceOperandsInPlace(
      NewOps, this, From, To, NumUpdated, OperandNo);
}

Instruction *ConstantExpr::getAsInstruction() {
  SmallVector<Value *, 4> ValueOperands(op_begin(), op_end());
  ArrayRef<Value*> Ops(ValueOperands);

  switch (getOpcode()) {
  case Instruction::Trunc:
  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::FPTrunc:
  case Instruction::FPExt:
  case Instruction::UIToFP:
  case Instruction::SIToFP:
  case Instruction::FPToUI:
  case Instruction::FPToSI:
  case Instruction::PtrToInt:
  case Instruction::IntToPtr:
  case Instruction::BitCast:
  case Instruction::AddrSpaceCast:
    return CastInst::Create((Instruction::CastOps)getOpcode(),
                            Ops[0], getType());
  case Instruction::Select:
    return SelectInst::Create(Ops[0], Ops[1], Ops[2]);
  case Instruction::InsertElement:
    return InsertElementInst::Create(Ops[0], Ops[1], Ops[2]);
  case Instruction::ExtractElement:
    return ExtractElementInst::Create(Ops[0], Ops[1]);
  case Instruction::InsertValue:
    return InsertValueInst::Create(Ops[0], Ops[1], getIndices());
  case Instruction::ExtractValue:
    return ExtractValueInst::Create(Ops[0], getIndices());
  case Instruction::ShuffleVector:
    return new ShuffleVectorInst(Ops[0], Ops[1], Ops[2]);

  case Instruction::GetElementPtr: {
    const auto *GO = cast<GEPOperator>(this);
    if (GO->isInBounds())
      return GetElementPtrInst::CreateInBounds(GO->getSourceElementType(),
                                               Ops[0], Ops.slice(1));
    return GetElementPtrInst::Create(GO->getSourceElementType(), Ops[0],
                                     Ops.slice(1));
  }
  case Instruction::ICmp:
  case Instruction::FCmp:
    return CmpInst::Create((Instruction::OtherOps)getOpcode(),
                           (CmpInst::Predicate)getPredicate(), Ops[0], Ops[1]);

  default:
    assert(getNumOperands() == 2 && "Must be binary operator?");
    BinaryOperator *BO =
      BinaryOperator::Create((Instruction::BinaryOps)getOpcode(),
                             Ops[0], Ops[1]);
    if (isa<OverflowingBinaryOperator>(BO)) {
      BO->setHasNoUnsignedWrap(SubclassOptionalData &
                               OverflowingBinaryOperator::NoUnsignedWrap);
      BO->setHasNoSignedWrap(SubclassOptionalData &
                             OverflowingBinaryOperator::NoSignedWrap);
    }
    if (isa<PossiblyExactOperator>(BO))
      BO->setIsExact(SubclassOptionalData & PossiblyExactOperator::IsExact);
    return BO;
  }
}
