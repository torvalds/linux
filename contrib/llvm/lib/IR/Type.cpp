//===- Type.cpp - Implement the Type class --------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Type class for the IR library.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Type.h"
#include "LLVMContextImpl.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <utility>

using namespace llvm;

//===----------------------------------------------------------------------===//
//                         Type Class Implementation
//===----------------------------------------------------------------------===//

Type *Type::getPrimitiveType(LLVMContext &C, TypeID IDNumber) {
  switch (IDNumber) {
  case VoidTyID      : return getVoidTy(C);
  case HalfTyID      : return getHalfTy(C);
  case FloatTyID     : return getFloatTy(C);
  case DoubleTyID    : return getDoubleTy(C);
  case X86_FP80TyID  : return getX86_FP80Ty(C);
  case FP128TyID     : return getFP128Ty(C);
  case PPC_FP128TyID : return getPPC_FP128Ty(C);
  case LabelTyID     : return getLabelTy(C);
  case MetadataTyID  : return getMetadataTy(C);
  case X86_MMXTyID   : return getX86_MMXTy(C);
  case TokenTyID     : return getTokenTy(C);
  default:
    return nullptr;
  }
}

bool Type::isIntegerTy(unsigned Bitwidth) const {
  return isIntegerTy() && cast<IntegerType>(this)->getBitWidth() == Bitwidth;
}

bool Type::canLosslesslyBitCastTo(Type *Ty) const {
  // Identity cast means no change so return true
  if (this == Ty)
    return true;

  // They are not convertible unless they are at least first class types
  if (!this->isFirstClassType() || !Ty->isFirstClassType())
    return false;

  // Vector -> Vector conversions are always lossless if the two vector types
  // have the same size, otherwise not.  Also, 64-bit vector types can be
  // converted to x86mmx.
  if (auto *thisPTy = dyn_cast<VectorType>(this)) {
    if (auto *thatPTy = dyn_cast<VectorType>(Ty))
      return thisPTy->getBitWidth() == thatPTy->getBitWidth();
    if (Ty->getTypeID() == Type::X86_MMXTyID &&
        thisPTy->getBitWidth() == 64)
      return true;
  }

  if (this->getTypeID() == Type::X86_MMXTyID)
    if (auto *thatPTy = dyn_cast<VectorType>(Ty))
      if (thatPTy->getBitWidth() == 64)
        return true;

  // At this point we have only various mismatches of the first class types
  // remaining and ptr->ptr. Just select the lossless conversions. Everything
  // else is not lossless. Conservatively assume we can't losslessly convert
  // between pointers with different address spaces.
  if (auto *PTy = dyn_cast<PointerType>(this)) {
    if (auto *OtherPTy = dyn_cast<PointerType>(Ty))
      return PTy->getAddressSpace() == OtherPTy->getAddressSpace();
    return false;
  }
  return false;  // Other types have no identity values
}

bool Type::isEmptyTy() const {
  if (auto *ATy = dyn_cast<ArrayType>(this)) {
    unsigned NumElements = ATy->getNumElements();
    return NumElements == 0 || ATy->getElementType()->isEmptyTy();
  }

  if (auto *STy = dyn_cast<StructType>(this)) {
    unsigned NumElements = STy->getNumElements();
    for (unsigned i = 0; i < NumElements; ++i)
      if (!STy->getElementType(i)->isEmptyTy())
        return false;
    return true;
  }

  return false;
}

unsigned Type::getPrimitiveSizeInBits() const {
  switch (getTypeID()) {
  case Type::HalfTyID: return 16;
  case Type::FloatTyID: return 32;
  case Type::DoubleTyID: return 64;
  case Type::X86_FP80TyID: return 80;
  case Type::FP128TyID: return 128;
  case Type::PPC_FP128TyID: return 128;
  case Type::X86_MMXTyID: return 64;
  case Type::IntegerTyID: return cast<IntegerType>(this)->getBitWidth();
  case Type::VectorTyID:  return cast<VectorType>(this)->getBitWidth();
  default: return 0;
  }
}

unsigned Type::getScalarSizeInBits() const {
  return getScalarType()->getPrimitiveSizeInBits();
}

int Type::getFPMantissaWidth() const {
  if (auto *VTy = dyn_cast<VectorType>(this))
    return VTy->getElementType()->getFPMantissaWidth();
  assert(isFloatingPointTy() && "Not a floating point type!");
  if (getTypeID() == HalfTyID) return 11;
  if (getTypeID() == FloatTyID) return 24;
  if (getTypeID() == DoubleTyID) return 53;
  if (getTypeID() == X86_FP80TyID) return 64;
  if (getTypeID() == FP128TyID) return 113;
  assert(getTypeID() == PPC_FP128TyID && "unknown fp type");
  return -1;
}

bool Type::isSizedDerivedType(SmallPtrSetImpl<Type*> *Visited) const {
  if (auto *ATy = dyn_cast<ArrayType>(this))
    return ATy->getElementType()->isSized(Visited);

  if (auto *VTy = dyn_cast<VectorType>(this))
    return VTy->getElementType()->isSized(Visited);

  return cast<StructType>(this)->isSized(Visited);
}

//===----------------------------------------------------------------------===//
//                          Primitive 'Type' data
//===----------------------------------------------------------------------===//

Type *Type::getVoidTy(LLVMContext &C) { return &C.pImpl->VoidTy; }
Type *Type::getLabelTy(LLVMContext &C) { return &C.pImpl->LabelTy; }
Type *Type::getHalfTy(LLVMContext &C) { return &C.pImpl->HalfTy; }
Type *Type::getFloatTy(LLVMContext &C) { return &C.pImpl->FloatTy; }
Type *Type::getDoubleTy(LLVMContext &C) { return &C.pImpl->DoubleTy; }
Type *Type::getMetadataTy(LLVMContext &C) { return &C.pImpl->MetadataTy; }
Type *Type::getTokenTy(LLVMContext &C) { return &C.pImpl->TokenTy; }
Type *Type::getX86_FP80Ty(LLVMContext &C) { return &C.pImpl->X86_FP80Ty; }
Type *Type::getFP128Ty(LLVMContext &C) { return &C.pImpl->FP128Ty; }
Type *Type::getPPC_FP128Ty(LLVMContext &C) { return &C.pImpl->PPC_FP128Ty; }
Type *Type::getX86_MMXTy(LLVMContext &C) { return &C.pImpl->X86_MMXTy; }

IntegerType *Type::getInt1Ty(LLVMContext &C) { return &C.pImpl->Int1Ty; }
IntegerType *Type::getInt8Ty(LLVMContext &C) { return &C.pImpl->Int8Ty; }
IntegerType *Type::getInt16Ty(LLVMContext &C) { return &C.pImpl->Int16Ty; }
IntegerType *Type::getInt32Ty(LLVMContext &C) { return &C.pImpl->Int32Ty; }
IntegerType *Type::getInt64Ty(LLVMContext &C) { return &C.pImpl->Int64Ty; }
IntegerType *Type::getInt128Ty(LLVMContext &C) { return &C.pImpl->Int128Ty; }

IntegerType *Type::getIntNTy(LLVMContext &C, unsigned N) {
  return IntegerType::get(C, N);
}

PointerType *Type::getHalfPtrTy(LLVMContext &C, unsigned AS) {
  return getHalfTy(C)->getPointerTo(AS);
}

PointerType *Type::getFloatPtrTy(LLVMContext &C, unsigned AS) {
  return getFloatTy(C)->getPointerTo(AS);
}

PointerType *Type::getDoublePtrTy(LLVMContext &C, unsigned AS) {
  return getDoubleTy(C)->getPointerTo(AS);
}

PointerType *Type::getX86_FP80PtrTy(LLVMContext &C, unsigned AS) {
  return getX86_FP80Ty(C)->getPointerTo(AS);
}

PointerType *Type::getFP128PtrTy(LLVMContext &C, unsigned AS) {
  return getFP128Ty(C)->getPointerTo(AS);
}

PointerType *Type::getPPC_FP128PtrTy(LLVMContext &C, unsigned AS) {
  return getPPC_FP128Ty(C)->getPointerTo(AS);
}

PointerType *Type::getX86_MMXPtrTy(LLVMContext &C, unsigned AS) {
  return getX86_MMXTy(C)->getPointerTo(AS);
}

PointerType *Type::getIntNPtrTy(LLVMContext &C, unsigned N, unsigned AS) {
  return getIntNTy(C, N)->getPointerTo(AS);
}

PointerType *Type::getInt1PtrTy(LLVMContext &C, unsigned AS) {
  return getInt1Ty(C)->getPointerTo(AS);
}

PointerType *Type::getInt8PtrTy(LLVMContext &C, unsigned AS) {
  return getInt8Ty(C)->getPointerTo(AS);
}

PointerType *Type::getInt16PtrTy(LLVMContext &C, unsigned AS) {
  return getInt16Ty(C)->getPointerTo(AS);
}

PointerType *Type::getInt32PtrTy(LLVMContext &C, unsigned AS) {
  return getInt32Ty(C)->getPointerTo(AS);
}

PointerType *Type::getInt64PtrTy(LLVMContext &C, unsigned AS) {
  return getInt64Ty(C)->getPointerTo(AS);
}

//===----------------------------------------------------------------------===//
//                       IntegerType Implementation
//===----------------------------------------------------------------------===//

IntegerType *IntegerType::get(LLVMContext &C, unsigned NumBits) {
  assert(NumBits >= MIN_INT_BITS && "bitwidth too small");
  assert(NumBits <= MAX_INT_BITS && "bitwidth too large");

  // Check for the built-in integer types
  switch (NumBits) {
  case   1: return cast<IntegerType>(Type::getInt1Ty(C));
  case   8: return cast<IntegerType>(Type::getInt8Ty(C));
  case  16: return cast<IntegerType>(Type::getInt16Ty(C));
  case  32: return cast<IntegerType>(Type::getInt32Ty(C));
  case  64: return cast<IntegerType>(Type::getInt64Ty(C));
  case 128: return cast<IntegerType>(Type::getInt128Ty(C));
  default:
    break;
  }

  IntegerType *&Entry = C.pImpl->IntegerTypes[NumBits];

  if (!Entry)
    Entry = new (C.pImpl->TypeAllocator) IntegerType(C, NumBits);

  return Entry;
}

bool IntegerType::isPowerOf2ByteWidth() const {
  unsigned BitWidth = getBitWidth();
  return (BitWidth > 7) && isPowerOf2_32(BitWidth);
}

APInt IntegerType::getMask() const {
  return APInt::getAllOnesValue(getBitWidth());
}

//===----------------------------------------------------------------------===//
//                       FunctionType Implementation
//===----------------------------------------------------------------------===//

FunctionType::FunctionType(Type *Result, ArrayRef<Type*> Params,
                           bool IsVarArgs)
  : Type(Result->getContext(), FunctionTyID) {
  Type **SubTys = reinterpret_cast<Type**>(this+1);
  assert(isValidReturnType(Result) && "invalid return type for function");
  setSubclassData(IsVarArgs);

  SubTys[0] = Result;

  for (unsigned i = 0, e = Params.size(); i != e; ++i) {
    assert(isValidArgumentType(Params[i]) &&
           "Not a valid type for function argument!");
    SubTys[i+1] = Params[i];
  }

  ContainedTys = SubTys;
  NumContainedTys = Params.size() + 1; // + 1 for result type
}

// This is the factory function for the FunctionType class.
FunctionType *FunctionType::get(Type *ReturnType,
                                ArrayRef<Type*> Params, bool isVarArg) {
  LLVMContextImpl *pImpl = ReturnType->getContext().pImpl;
  const FunctionTypeKeyInfo::KeyTy Key(ReturnType, Params, isVarArg);
  FunctionType *FT;
  // Since we only want to allocate a fresh function type in case none is found
  // and we don't want to perform two lookups (one for checking if existent and
  // one for inserting the newly allocated one), here we instead lookup based on
  // Key and update the reference to the function type in-place to a newly
  // allocated one if not found.
  auto Insertion = pImpl->FunctionTypes.insert_as(nullptr, Key);
  if (Insertion.second) {
    // The function type was not found. Allocate one and update FunctionTypes
    // in-place.
    FT = (FunctionType *)pImpl->TypeAllocator.Allocate(
        sizeof(FunctionType) + sizeof(Type *) * (Params.size() + 1),
        alignof(FunctionType));
    new (FT) FunctionType(ReturnType, Params, isVarArg);
    *Insertion.first = FT;
  } else {
    // The function type was found. Just return it.
    FT = *Insertion.first;
  }
  return FT;
}

FunctionType *FunctionType::get(Type *Result, bool isVarArg) {
  return get(Result, None, isVarArg);
}

bool FunctionType::isValidReturnType(Type *RetTy) {
  return !RetTy->isFunctionTy() && !RetTy->isLabelTy() &&
  !RetTy->isMetadataTy();
}

bool FunctionType::isValidArgumentType(Type *ArgTy) {
  return ArgTy->isFirstClassType();
}

//===----------------------------------------------------------------------===//
//                       StructType Implementation
//===----------------------------------------------------------------------===//

// Primitive Constructors.

StructType *StructType::get(LLVMContext &Context, ArrayRef<Type*> ETypes,
                            bool isPacked) {
  LLVMContextImpl *pImpl = Context.pImpl;
  const AnonStructTypeKeyInfo::KeyTy Key(ETypes, isPacked);

  StructType *ST;
  // Since we only want to allocate a fresh struct type in case none is found
  // and we don't want to perform two lookups (one for checking if existent and
  // one for inserting the newly allocated one), here we instead lookup based on
  // Key and update the reference to the struct type in-place to a newly
  // allocated one if not found.
  auto Insertion = pImpl->AnonStructTypes.insert_as(nullptr, Key);
  if (Insertion.second) {
    // The struct type was not found. Allocate one and update AnonStructTypes
    // in-place.
    ST = new (Context.pImpl->TypeAllocator) StructType(Context);
    ST->setSubclassData(SCDB_IsLiteral);  // Literal struct.
    ST->setBody(ETypes, isPacked);
    *Insertion.first = ST;
  } else {
    // The struct type was found. Just return it.
    ST = *Insertion.first;
  }

  return ST;
}

void StructType::setBody(ArrayRef<Type*> Elements, bool isPacked) {
  assert(isOpaque() && "Struct body already set!");

  setSubclassData(getSubclassData() | SCDB_HasBody);
  if (isPacked)
    setSubclassData(getSubclassData() | SCDB_Packed);

  NumContainedTys = Elements.size();

  if (Elements.empty()) {
    ContainedTys = nullptr;
    return;
  }

  ContainedTys = Elements.copy(getContext().pImpl->TypeAllocator).data();
}

void StructType::setName(StringRef Name) {
  if (Name == getName()) return;

  StringMap<StructType *> &SymbolTable = getContext().pImpl->NamedStructTypes;

  using EntryTy = StringMap<StructType *>::MapEntryTy;

  // If this struct already had a name, remove its symbol table entry. Don't
  // delete the data yet because it may be part of the new name.
  if (SymbolTableEntry)
    SymbolTable.remove((EntryTy *)SymbolTableEntry);

  // If this is just removing the name, we're done.
  if (Name.empty()) {
    if (SymbolTableEntry) {
      // Delete the old string data.
      ((EntryTy *)SymbolTableEntry)->Destroy(SymbolTable.getAllocator());
      SymbolTableEntry = nullptr;
    }
    return;
  }

  // Look up the entry for the name.
  auto IterBool =
      getContext().pImpl->NamedStructTypes.insert(std::make_pair(Name, this));

  // While we have a name collision, try a random rename.
  if (!IterBool.second) {
    SmallString<64> TempStr(Name);
    TempStr.push_back('.');
    raw_svector_ostream TmpStream(TempStr);
    unsigned NameSize = Name.size();

    do {
      TempStr.resize(NameSize + 1);
      TmpStream << getContext().pImpl->NamedStructTypesUniqueID++;

      IterBool = getContext().pImpl->NamedStructTypes.insert(
          std::make_pair(TmpStream.str(), this));
    } while (!IterBool.second);
  }

  // Delete the old string data.
  if (SymbolTableEntry)
    ((EntryTy *)SymbolTableEntry)->Destroy(SymbolTable.getAllocator());
  SymbolTableEntry = &*IterBool.first;
}

//===----------------------------------------------------------------------===//
// StructType Helper functions.

StructType *StructType::create(LLVMContext &Context, StringRef Name) {
  StructType *ST = new (Context.pImpl->TypeAllocator) StructType(Context);
  if (!Name.empty())
    ST->setName(Name);
  return ST;
}

StructType *StructType::get(LLVMContext &Context, bool isPacked) {
  return get(Context, None, isPacked);
}

StructType *StructType::create(LLVMContext &Context, ArrayRef<Type*> Elements,
                               StringRef Name, bool isPacked) {
  StructType *ST = create(Context, Name);
  ST->setBody(Elements, isPacked);
  return ST;
}

StructType *StructType::create(LLVMContext &Context, ArrayRef<Type*> Elements) {
  return create(Context, Elements, StringRef());
}

StructType *StructType::create(LLVMContext &Context) {
  return create(Context, StringRef());
}

StructType *StructType::create(ArrayRef<Type*> Elements, StringRef Name,
                               bool isPacked) {
  assert(!Elements.empty() &&
         "This method may not be invoked with an empty list");
  return create(Elements[0]->getContext(), Elements, Name, isPacked);
}

StructType *StructType::create(ArrayRef<Type*> Elements) {
  assert(!Elements.empty() &&
         "This method may not be invoked with an empty list");
  return create(Elements[0]->getContext(), Elements, StringRef());
}

bool StructType::isSized(SmallPtrSetImpl<Type*> *Visited) const {
  if ((getSubclassData() & SCDB_IsSized) != 0)
    return true;
  if (isOpaque())
    return false;

  if (Visited && !Visited->insert(const_cast<StructType*>(this)).second)
    return false;

  // Okay, our struct is sized if all of the elements are, but if one of the
  // elements is opaque, the struct isn't sized *yet*, but may become sized in
  // the future, so just bail out without caching.
  for (element_iterator I = element_begin(), E = element_end(); I != E; ++I)
    if (!(*I)->isSized(Visited))
      return false;

  // Here we cheat a bit and cast away const-ness. The goal is to memoize when
  // we find a sized type, as types can only move from opaque to sized, not the
  // other way.
  const_cast<StructType*>(this)->setSubclassData(
    getSubclassData() | SCDB_IsSized);
  return true;
}

StringRef StructType::getName() const {
  assert(!isLiteral() && "Literal structs never have names");
  if (!SymbolTableEntry) return StringRef();

  return ((StringMapEntry<StructType*> *)SymbolTableEntry)->getKey();
}

bool StructType::isValidElementType(Type *ElemTy) {
  return !ElemTy->isVoidTy() && !ElemTy->isLabelTy() &&
         !ElemTy->isMetadataTy() && !ElemTy->isFunctionTy() &&
         !ElemTy->isTokenTy();
}

bool StructType::isLayoutIdentical(StructType *Other) const {
  if (this == Other) return true;

  if (isPacked() != Other->isPacked())
    return false;

  return elements() == Other->elements();
}

StructType *Module::getTypeByName(StringRef Name) const {
  return getContext().pImpl->NamedStructTypes.lookup(Name);
}

//===----------------------------------------------------------------------===//
//                       CompositeType Implementation
//===----------------------------------------------------------------------===//

Type *CompositeType::getTypeAtIndex(const Value *V) const {
  if (auto *STy = dyn_cast<StructType>(this)) {
    unsigned Idx =
      (unsigned)cast<Constant>(V)->getUniqueInteger().getZExtValue();
    assert(indexValid(Idx) && "Invalid structure index!");
    return STy->getElementType(Idx);
  }

  return cast<SequentialType>(this)->getElementType();
}

Type *CompositeType::getTypeAtIndex(unsigned Idx) const{
  if (auto *STy = dyn_cast<StructType>(this)) {
    assert(indexValid(Idx) && "Invalid structure index!");
    return STy->getElementType(Idx);
  }

  return cast<SequentialType>(this)->getElementType();
}

bool CompositeType::indexValid(const Value *V) const {
  if (auto *STy = dyn_cast<StructType>(this)) {
    // Structure indexes require (vectors of) 32-bit integer constants.  In the
    // vector case all of the indices must be equal.
    if (!V->getType()->isIntOrIntVectorTy(32))
      return false;
    const Constant *C = dyn_cast<Constant>(V);
    if (C && V->getType()->isVectorTy())
      C = C->getSplatValue();
    const ConstantInt *CU = dyn_cast_or_null<ConstantInt>(C);
    return CU && CU->getZExtValue() < STy->getNumElements();
  }

  // Sequential types can be indexed by any integer.
  return V->getType()->isIntOrIntVectorTy();
}

bool CompositeType::indexValid(unsigned Idx) const {
  if (auto *STy = dyn_cast<StructType>(this))
    return Idx < STy->getNumElements();
  // Sequential types can be indexed by any integer.
  return true;
}

//===----------------------------------------------------------------------===//
//                           ArrayType Implementation
//===----------------------------------------------------------------------===//

ArrayType::ArrayType(Type *ElType, uint64_t NumEl)
  : SequentialType(ArrayTyID, ElType, NumEl) {}

ArrayType *ArrayType::get(Type *ElementType, uint64_t NumElements) {
  assert(isValidElementType(ElementType) && "Invalid type for array element!");

  LLVMContextImpl *pImpl = ElementType->getContext().pImpl;
  ArrayType *&Entry =
    pImpl->ArrayTypes[std::make_pair(ElementType, NumElements)];

  if (!Entry)
    Entry = new (pImpl->TypeAllocator) ArrayType(ElementType, NumElements);
  return Entry;
}

bool ArrayType::isValidElementType(Type *ElemTy) {
  return !ElemTy->isVoidTy() && !ElemTy->isLabelTy() &&
         !ElemTy->isMetadataTy() && !ElemTy->isFunctionTy() &&
         !ElemTy->isTokenTy();
}

//===----------------------------------------------------------------------===//
//                          VectorType Implementation
//===----------------------------------------------------------------------===//

VectorType::VectorType(Type *ElType, unsigned NumEl)
  : SequentialType(VectorTyID, ElType, NumEl) {}

VectorType *VectorType::get(Type *ElementType, unsigned NumElements) {
  assert(NumElements > 0 && "#Elements of a VectorType must be greater than 0");
  assert(isValidElementType(ElementType) && "Element type of a VectorType must "
                                            "be an integer, floating point, or "
                                            "pointer type.");

  LLVMContextImpl *pImpl = ElementType->getContext().pImpl;
  VectorType *&Entry = ElementType->getContext().pImpl
    ->VectorTypes[std::make_pair(ElementType, NumElements)];

  if (!Entry)
    Entry = new (pImpl->TypeAllocator) VectorType(ElementType, NumElements);
  return Entry;
}

bool VectorType::isValidElementType(Type *ElemTy) {
  return ElemTy->isIntegerTy() || ElemTy->isFloatingPointTy() ||
    ElemTy->isPointerTy();
}

//===----------------------------------------------------------------------===//
//                         PointerType Implementation
//===----------------------------------------------------------------------===//

PointerType *PointerType::get(Type *EltTy, unsigned AddressSpace) {
  assert(EltTy && "Can't get a pointer to <null> type!");
  assert(isValidElementType(EltTy) && "Invalid type for pointer element!");

  LLVMContextImpl *CImpl = EltTy->getContext().pImpl;

  // Since AddressSpace #0 is the common case, we special case it.
  PointerType *&Entry = AddressSpace == 0 ? CImpl->PointerTypes[EltTy]
     : CImpl->ASPointerTypes[std::make_pair(EltTy, AddressSpace)];

  if (!Entry)
    Entry = new (CImpl->TypeAllocator) PointerType(EltTy, AddressSpace);
  return Entry;
}

PointerType::PointerType(Type *E, unsigned AddrSpace)
  : Type(E->getContext(), PointerTyID), PointeeTy(E) {
  ContainedTys = &PointeeTy;
  NumContainedTys = 1;
  setSubclassData(AddrSpace);
}

PointerType *Type::getPointerTo(unsigned addrs) const {
  return PointerType::get(const_cast<Type*>(this), addrs);
}

bool PointerType::isValidElementType(Type *ElemTy) {
  return !ElemTy->isVoidTy() && !ElemTy->isLabelTy() &&
         !ElemTy->isMetadataTy() && !ElemTy->isTokenTy();
}

bool PointerType::isLoadableOrStorableType(Type *ElemTy) {
  return isValidElementType(ElemTy) && !ElemTy->isFunctionTy();
}
