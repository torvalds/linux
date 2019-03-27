//===- IRBuilder.cpp - Builder for LLVM Instrs ----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the IRBuilder class, which is used as a convenient way
// to create LLVM instructions with a consistent and simplified interface.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/IRBuilder.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/None.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Statepoint.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cstdint>
#include <vector>

using namespace llvm;

/// CreateGlobalString - Make a new global variable with an initializer that
/// has array of i8 type filled in with the nul terminated string value
/// specified.  If Name is specified, it is the name of the global variable
/// created.
GlobalVariable *IRBuilderBase::CreateGlobalString(StringRef Str,
                                                  const Twine &Name,
                                                  unsigned AddressSpace) {
  Constant *StrConstant = ConstantDataArray::getString(Context, Str);
  Module &M = *BB->getParent()->getParent();
  auto *GV = new GlobalVariable(M, StrConstant->getType(), true,
                                GlobalValue::PrivateLinkage, StrConstant, Name,
                                nullptr, GlobalVariable::NotThreadLocal,
                                AddressSpace);
  GV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  GV->setAlignment(1);
  return GV;
}

Type *IRBuilderBase::getCurrentFunctionReturnType() const {
  assert(BB && BB->getParent() && "No current function!");
  return BB->getParent()->getReturnType();
}

Value *IRBuilderBase::getCastedInt8PtrValue(Value *Ptr) {
  auto *PT = cast<PointerType>(Ptr->getType());
  if (PT->getElementType()->isIntegerTy(8))
    return Ptr;

  // Otherwise, we need to insert a bitcast.
  PT = getInt8PtrTy(PT->getAddressSpace());
  BitCastInst *BCI = new BitCastInst(Ptr, PT, "");
  BB->getInstList().insert(InsertPt, BCI);
  SetInstDebugLocation(BCI);
  return BCI;
}

static CallInst *createCallHelper(Value *Callee, ArrayRef<Value *> Ops,
                                  IRBuilderBase *Builder,
                                  const Twine &Name = "",
                                  Instruction *FMFSource = nullptr) {
  CallInst *CI = CallInst::Create(Callee, Ops, Name);
  if (FMFSource)
    CI->copyFastMathFlags(FMFSource);
  Builder->GetInsertBlock()->getInstList().insert(Builder->GetInsertPoint(),CI);
  Builder->SetInstDebugLocation(CI);
  return CI;
}

static InvokeInst *createInvokeHelper(Value *Invokee, BasicBlock *NormalDest,
                                      BasicBlock *UnwindDest,
                                      ArrayRef<Value *> Ops,
                                      IRBuilderBase *Builder,
                                      const Twine &Name = "") {
  InvokeInst *II =
      InvokeInst::Create(Invokee, NormalDest, UnwindDest, Ops, Name);
  Builder->GetInsertBlock()->getInstList().insert(Builder->GetInsertPoint(),
                                                  II);
  Builder->SetInstDebugLocation(II);
  return II;
}

CallInst *IRBuilderBase::
CreateMemSet(Value *Ptr, Value *Val, Value *Size, unsigned Align,
             bool isVolatile, MDNode *TBAATag, MDNode *ScopeTag,
             MDNode *NoAliasTag) {
  Ptr = getCastedInt8PtrValue(Ptr);
  Value *Ops[] = {Ptr, Val, Size, getInt1(isVolatile)};
  Type *Tys[] = { Ptr->getType(), Size->getType() };
  Module *M = BB->getParent()->getParent();
  Value *TheFn = Intrinsic::getDeclaration(M, Intrinsic::memset, Tys);

  CallInst *CI = createCallHelper(TheFn, Ops, this);

  if (Align > 0)
    cast<MemSetInst>(CI)->setDestAlignment(Align);

  // Set the TBAA info if present.
  if (TBAATag)
    CI->setMetadata(LLVMContext::MD_tbaa, TBAATag);

  if (ScopeTag)
    CI->setMetadata(LLVMContext::MD_alias_scope, ScopeTag);

  if (NoAliasTag)
    CI->setMetadata(LLVMContext::MD_noalias, NoAliasTag);

  return CI;
}

CallInst *IRBuilderBase::CreateElementUnorderedAtomicMemSet(
    Value *Ptr, Value *Val, Value *Size, unsigned Align, uint32_t ElementSize,
    MDNode *TBAATag, MDNode *ScopeTag, MDNode *NoAliasTag) {
  assert(Align >= ElementSize &&
         "Pointer alignment must be at least element size.");

  Ptr = getCastedInt8PtrValue(Ptr);
  Value *Ops[] = {Ptr, Val, Size, getInt32(ElementSize)};
  Type *Tys[] = {Ptr->getType(), Size->getType()};
  Module *M = BB->getParent()->getParent();
  Value *TheFn = Intrinsic::getDeclaration(
      M, Intrinsic::memset_element_unordered_atomic, Tys);

  CallInst *CI = createCallHelper(TheFn, Ops, this);

  cast<AtomicMemSetInst>(CI)->setDestAlignment(Align);

  // Set the TBAA info if present.
  if (TBAATag)
    CI->setMetadata(LLVMContext::MD_tbaa, TBAATag);

  if (ScopeTag)
    CI->setMetadata(LLVMContext::MD_alias_scope, ScopeTag);

  if (NoAliasTag)
    CI->setMetadata(LLVMContext::MD_noalias, NoAliasTag);

  return CI;
}

CallInst *IRBuilderBase::
CreateMemCpy(Value *Dst, unsigned DstAlign, Value *Src, unsigned SrcAlign,
             Value *Size, bool isVolatile, MDNode *TBAATag,
             MDNode *TBAAStructTag, MDNode *ScopeTag, MDNode *NoAliasTag) {
  assert((DstAlign == 0 || isPowerOf2_32(DstAlign)) && "Must be 0 or a power of 2");
  assert((SrcAlign == 0 || isPowerOf2_32(SrcAlign)) && "Must be 0 or a power of 2");
  Dst = getCastedInt8PtrValue(Dst);
  Src = getCastedInt8PtrValue(Src);

  Value *Ops[] = {Dst, Src, Size, getInt1(isVolatile)};
  Type *Tys[] = { Dst->getType(), Src->getType(), Size->getType() };
  Module *M = BB->getParent()->getParent();
  Value *TheFn = Intrinsic::getDeclaration(M, Intrinsic::memcpy, Tys);

  CallInst *CI = createCallHelper(TheFn, Ops, this);

  auto* MCI = cast<MemCpyInst>(CI);
  if (DstAlign > 0)
    MCI->setDestAlignment(DstAlign);
  if (SrcAlign > 0)
    MCI->setSourceAlignment(SrcAlign);

  // Set the TBAA info if present.
  if (TBAATag)
    CI->setMetadata(LLVMContext::MD_tbaa, TBAATag);

  // Set the TBAA Struct info if present.
  if (TBAAStructTag)
    CI->setMetadata(LLVMContext::MD_tbaa_struct, TBAAStructTag);

  if (ScopeTag)
    CI->setMetadata(LLVMContext::MD_alias_scope, ScopeTag);

  if (NoAliasTag)
    CI->setMetadata(LLVMContext::MD_noalias, NoAliasTag);

  return CI;
}

CallInst *IRBuilderBase::CreateElementUnorderedAtomicMemCpy(
    Value *Dst, unsigned DstAlign, Value *Src, unsigned SrcAlign, Value *Size,
    uint32_t ElementSize, MDNode *TBAATag, MDNode *TBAAStructTag,
    MDNode *ScopeTag, MDNode *NoAliasTag) {
  assert(DstAlign >= ElementSize &&
         "Pointer alignment must be at least element size");
  assert(SrcAlign >= ElementSize &&
         "Pointer alignment must be at least element size");
  Dst = getCastedInt8PtrValue(Dst);
  Src = getCastedInt8PtrValue(Src);

  Value *Ops[] = {Dst, Src, Size, getInt32(ElementSize)};
  Type *Tys[] = {Dst->getType(), Src->getType(), Size->getType()};
  Module *M = BB->getParent()->getParent();
  Value *TheFn = Intrinsic::getDeclaration(
      M, Intrinsic::memcpy_element_unordered_atomic, Tys);

  CallInst *CI = createCallHelper(TheFn, Ops, this);

  // Set the alignment of the pointer args.
  auto *AMCI = cast<AtomicMemCpyInst>(CI);
  AMCI->setDestAlignment(DstAlign);
  AMCI->setSourceAlignment(SrcAlign);

  // Set the TBAA info if present.
  if (TBAATag)
    CI->setMetadata(LLVMContext::MD_tbaa, TBAATag);

  // Set the TBAA Struct info if present.
  if (TBAAStructTag)
    CI->setMetadata(LLVMContext::MD_tbaa_struct, TBAAStructTag);

  if (ScopeTag)
    CI->setMetadata(LLVMContext::MD_alias_scope, ScopeTag);

  if (NoAliasTag)
    CI->setMetadata(LLVMContext::MD_noalias, NoAliasTag);

  return CI;
}

CallInst *IRBuilderBase::
CreateMemMove(Value *Dst, unsigned DstAlign, Value *Src, unsigned SrcAlign,
              Value *Size, bool isVolatile, MDNode *TBAATag, MDNode *ScopeTag,
              MDNode *NoAliasTag) {
  assert((DstAlign == 0 || isPowerOf2_32(DstAlign)) && "Must be 0 or a power of 2");
  assert((SrcAlign == 0 || isPowerOf2_32(SrcAlign)) && "Must be 0 or a power of 2");
  Dst = getCastedInt8PtrValue(Dst);
  Src = getCastedInt8PtrValue(Src);

  Value *Ops[] = {Dst, Src, Size, getInt1(isVolatile)};
  Type *Tys[] = { Dst->getType(), Src->getType(), Size->getType() };
  Module *M = BB->getParent()->getParent();
  Value *TheFn = Intrinsic::getDeclaration(M, Intrinsic::memmove, Tys);

  CallInst *CI = createCallHelper(TheFn, Ops, this);

  auto *MMI = cast<MemMoveInst>(CI);
  if (DstAlign > 0)
    MMI->setDestAlignment(DstAlign);
  if (SrcAlign > 0)
    MMI->setSourceAlignment(SrcAlign);

  // Set the TBAA info if present.
  if (TBAATag)
    CI->setMetadata(LLVMContext::MD_tbaa, TBAATag);

  if (ScopeTag)
    CI->setMetadata(LLVMContext::MD_alias_scope, ScopeTag);

  if (NoAliasTag)
    CI->setMetadata(LLVMContext::MD_noalias, NoAliasTag);

  return CI;
}

CallInst *IRBuilderBase::CreateElementUnorderedAtomicMemMove(
    Value *Dst, unsigned DstAlign, Value *Src, unsigned SrcAlign, Value *Size,
    uint32_t ElementSize, MDNode *TBAATag, MDNode *TBAAStructTag,
    MDNode *ScopeTag, MDNode *NoAliasTag) {
  assert(DstAlign >= ElementSize &&
         "Pointer alignment must be at least element size");
  assert(SrcAlign >= ElementSize &&
         "Pointer alignment must be at least element size");
  Dst = getCastedInt8PtrValue(Dst);
  Src = getCastedInt8PtrValue(Src);

  Value *Ops[] = {Dst, Src, Size, getInt32(ElementSize)};
  Type *Tys[] = {Dst->getType(), Src->getType(), Size->getType()};
  Module *M = BB->getParent()->getParent();
  Value *TheFn = Intrinsic::getDeclaration(
      M, Intrinsic::memmove_element_unordered_atomic, Tys);

  CallInst *CI = createCallHelper(TheFn, Ops, this);

  // Set the alignment of the pointer args.
  CI->addParamAttr(0, Attribute::getWithAlignment(CI->getContext(), DstAlign));
  CI->addParamAttr(1, Attribute::getWithAlignment(CI->getContext(), SrcAlign));

  // Set the TBAA info if present.
  if (TBAATag)
    CI->setMetadata(LLVMContext::MD_tbaa, TBAATag);

  // Set the TBAA Struct info if present.
  if (TBAAStructTag)
    CI->setMetadata(LLVMContext::MD_tbaa_struct, TBAAStructTag);

  if (ScopeTag)
    CI->setMetadata(LLVMContext::MD_alias_scope, ScopeTag);

  if (NoAliasTag)
    CI->setMetadata(LLVMContext::MD_noalias, NoAliasTag);

  return CI;
}

static CallInst *getReductionIntrinsic(IRBuilderBase *Builder, Intrinsic::ID ID,
                                    Value *Src) {
  Module *M = Builder->GetInsertBlock()->getParent()->getParent();
  Value *Ops[] = {Src};
  Type *Tys[] = { Src->getType()->getVectorElementType(), Src->getType() };
  auto Decl = Intrinsic::getDeclaration(M, ID, Tys);
  return createCallHelper(Decl, Ops, Builder);
}

CallInst *IRBuilderBase::CreateFAddReduce(Value *Acc, Value *Src) {
  Module *M = GetInsertBlock()->getParent()->getParent();
  Value *Ops[] = {Acc, Src};
  Type *Tys[] = {Src->getType()->getVectorElementType(), Acc->getType(),
                 Src->getType()};
  auto Decl = Intrinsic::getDeclaration(
      M, Intrinsic::experimental_vector_reduce_fadd, Tys);
  return createCallHelper(Decl, Ops, this);
}

CallInst *IRBuilderBase::CreateFMulReduce(Value *Acc, Value *Src) {
  Module *M = GetInsertBlock()->getParent()->getParent();
  Value *Ops[] = {Acc, Src};
  Type *Tys[] = {Src->getType()->getVectorElementType(), Acc->getType(),
                 Src->getType()};
  auto Decl = Intrinsic::getDeclaration(
      M, Intrinsic::experimental_vector_reduce_fmul, Tys);
  return createCallHelper(Decl, Ops, this);
}

CallInst *IRBuilderBase::CreateAddReduce(Value *Src) {
  return getReductionIntrinsic(this, Intrinsic::experimental_vector_reduce_add,
                               Src);
}

CallInst *IRBuilderBase::CreateMulReduce(Value *Src) {
  return getReductionIntrinsic(this, Intrinsic::experimental_vector_reduce_mul,
                               Src);
}

CallInst *IRBuilderBase::CreateAndReduce(Value *Src) {
  return getReductionIntrinsic(this, Intrinsic::experimental_vector_reduce_and,
                               Src);
}

CallInst *IRBuilderBase::CreateOrReduce(Value *Src) {
  return getReductionIntrinsic(this, Intrinsic::experimental_vector_reduce_or,
                               Src);
}

CallInst *IRBuilderBase::CreateXorReduce(Value *Src) {
  return getReductionIntrinsic(this, Intrinsic::experimental_vector_reduce_xor,
                               Src);
}

CallInst *IRBuilderBase::CreateIntMaxReduce(Value *Src, bool IsSigned) {
  auto ID = IsSigned ? Intrinsic::experimental_vector_reduce_smax
                     : Intrinsic::experimental_vector_reduce_umax;
  return getReductionIntrinsic(this, ID, Src);
}

CallInst *IRBuilderBase::CreateIntMinReduce(Value *Src, bool IsSigned) {
  auto ID = IsSigned ? Intrinsic::experimental_vector_reduce_smin
                     : Intrinsic::experimental_vector_reduce_umin;
  return getReductionIntrinsic(this, ID, Src);
}

CallInst *IRBuilderBase::CreateFPMaxReduce(Value *Src, bool NoNaN) {
  auto Rdx = getReductionIntrinsic(
      this, Intrinsic::experimental_vector_reduce_fmax, Src);
  if (NoNaN) {
    FastMathFlags FMF;
    FMF.setNoNaNs();
    Rdx->setFastMathFlags(FMF);
  }
  return Rdx;
}

CallInst *IRBuilderBase::CreateFPMinReduce(Value *Src, bool NoNaN) {
  auto Rdx = getReductionIntrinsic(
      this, Intrinsic::experimental_vector_reduce_fmin, Src);
  if (NoNaN) {
    FastMathFlags FMF;
    FMF.setNoNaNs();
    Rdx->setFastMathFlags(FMF);
  }
  return Rdx;
}

CallInst *IRBuilderBase::CreateLifetimeStart(Value *Ptr, ConstantInt *Size) {
  assert(isa<PointerType>(Ptr->getType()) &&
         "lifetime.start only applies to pointers.");
  Ptr = getCastedInt8PtrValue(Ptr);
  if (!Size)
    Size = getInt64(-1);
  else
    assert(Size->getType() == getInt64Ty() &&
           "lifetime.start requires the size to be an i64");
  Value *Ops[] = { Size, Ptr };
  Module *M = BB->getParent()->getParent();
  Value *TheFn = Intrinsic::getDeclaration(M, Intrinsic::lifetime_start,
                                           { Ptr->getType() });
  return createCallHelper(TheFn, Ops, this);
}

CallInst *IRBuilderBase::CreateLifetimeEnd(Value *Ptr, ConstantInt *Size) {
  assert(isa<PointerType>(Ptr->getType()) &&
         "lifetime.end only applies to pointers.");
  Ptr = getCastedInt8PtrValue(Ptr);
  if (!Size)
    Size = getInt64(-1);
  else
    assert(Size->getType() == getInt64Ty() &&
           "lifetime.end requires the size to be an i64");
  Value *Ops[] = { Size, Ptr };
  Module *M = BB->getParent()->getParent();
  Value *TheFn = Intrinsic::getDeclaration(M, Intrinsic::lifetime_end,
                                           { Ptr->getType() });
  return createCallHelper(TheFn, Ops, this);
}

CallInst *IRBuilderBase::CreateInvariantStart(Value *Ptr, ConstantInt *Size) {

  assert(isa<PointerType>(Ptr->getType()) &&
         "invariant.start only applies to pointers.");
  Ptr = getCastedInt8PtrValue(Ptr);
  if (!Size)
    Size = getInt64(-1);
  else
    assert(Size->getType() == getInt64Ty() &&
           "invariant.start requires the size to be an i64");

  Value *Ops[] = {Size, Ptr};
  // Fill in the single overloaded type: memory object type.
  Type *ObjectPtr[1] = {Ptr->getType()};
  Module *M = BB->getParent()->getParent();
  Value *TheFn =
      Intrinsic::getDeclaration(M, Intrinsic::invariant_start, ObjectPtr);
  return createCallHelper(TheFn, Ops, this);
}

CallInst *IRBuilderBase::CreateAssumption(Value *Cond) {
  assert(Cond->getType() == getInt1Ty() &&
         "an assumption condition must be of type i1");

  Value *Ops[] = { Cond };
  Module *M = BB->getParent()->getParent();
  Value *FnAssume = Intrinsic::getDeclaration(M, Intrinsic::assume);
  return createCallHelper(FnAssume, Ops, this);
}

/// Create a call to a Masked Load intrinsic.
/// \p Ptr      - base pointer for the load
/// \p Align    - alignment of the source location
/// \p Mask     - vector of booleans which indicates what vector lanes should
///               be accessed in memory
/// \p PassThru - pass-through value that is used to fill the masked-off lanes
///               of the result
/// \p Name     - name of the result variable
CallInst *IRBuilderBase::CreateMaskedLoad(Value *Ptr, unsigned Align,
                                          Value *Mask, Value *PassThru,
                                          const Twine &Name) {
  auto *PtrTy = cast<PointerType>(Ptr->getType());
  Type *DataTy = PtrTy->getElementType();
  assert(DataTy->isVectorTy() && "Ptr should point to a vector");
  assert(Mask && "Mask should not be all-ones (null)");
  if (!PassThru)
    PassThru = UndefValue::get(DataTy);
  Type *OverloadedTypes[] = { DataTy, PtrTy };
  Value *Ops[] = { Ptr, getInt32(Align), Mask,  PassThru};
  return CreateMaskedIntrinsic(Intrinsic::masked_load, Ops,
                               OverloadedTypes, Name);
}

/// Create a call to a Masked Store intrinsic.
/// \p Val   - data to be stored,
/// \p Ptr   - base pointer for the store
/// \p Align - alignment of the destination location
/// \p Mask  - vector of booleans which indicates what vector lanes should
///            be accessed in memory
CallInst *IRBuilderBase::CreateMaskedStore(Value *Val, Value *Ptr,
                                           unsigned Align, Value *Mask) {
  auto *PtrTy = cast<PointerType>(Ptr->getType());
  Type *DataTy = PtrTy->getElementType();
  assert(DataTy->isVectorTy() && "Ptr should point to a vector");
  assert(Mask && "Mask should not be all-ones (null)");
  Type *OverloadedTypes[] = { DataTy, PtrTy };
  Value *Ops[] = { Val, Ptr, getInt32(Align), Mask };
  return CreateMaskedIntrinsic(Intrinsic::masked_store, Ops, OverloadedTypes);
}

/// Create a call to a Masked intrinsic, with given intrinsic Id,
/// an array of operands - Ops, and an array of overloaded types -
/// OverloadedTypes.
CallInst *IRBuilderBase::CreateMaskedIntrinsic(Intrinsic::ID Id,
                                               ArrayRef<Value *> Ops,
                                               ArrayRef<Type *> OverloadedTypes,
                                               const Twine &Name) {
  Module *M = BB->getParent()->getParent();
  Value *TheFn = Intrinsic::getDeclaration(M, Id, OverloadedTypes);
  return createCallHelper(TheFn, Ops, this, Name);
}

/// Create a call to a Masked Gather intrinsic.
/// \p Ptrs     - vector of pointers for loading
/// \p Align    - alignment for one element
/// \p Mask     - vector of booleans which indicates what vector lanes should
///               be accessed in memory
/// \p PassThru - pass-through value that is used to fill the masked-off lanes
///               of the result
/// \p Name     - name of the result variable
CallInst *IRBuilderBase::CreateMaskedGather(Value *Ptrs, unsigned Align,
                                            Value *Mask,  Value *PassThru,
                                            const Twine& Name) {
  auto PtrsTy = cast<VectorType>(Ptrs->getType());
  auto PtrTy = cast<PointerType>(PtrsTy->getElementType());
  unsigned NumElts = PtrsTy->getVectorNumElements();
  Type *DataTy = VectorType::get(PtrTy->getElementType(), NumElts);

  if (!Mask)
    Mask = Constant::getAllOnesValue(VectorType::get(Type::getInt1Ty(Context),
                                     NumElts));

  if (!PassThru)
    PassThru = UndefValue::get(DataTy);

  Type *OverloadedTypes[] = {DataTy, PtrsTy};
  Value * Ops[] = {Ptrs, getInt32(Align), Mask, PassThru};

  // We specify only one type when we create this intrinsic. Types of other
  // arguments are derived from this type.
  return CreateMaskedIntrinsic(Intrinsic::masked_gather, Ops, OverloadedTypes,
                               Name);
}

/// Create a call to a Masked Scatter intrinsic.
/// \p Data  - data to be stored,
/// \p Ptrs  - the vector of pointers, where the \p Data elements should be
///            stored
/// \p Align - alignment for one element
/// \p Mask  - vector of booleans which indicates what vector lanes should
///            be accessed in memory
CallInst *IRBuilderBase::CreateMaskedScatter(Value *Data, Value *Ptrs,
                                             unsigned Align, Value *Mask) {
  auto PtrsTy = cast<VectorType>(Ptrs->getType());
  auto DataTy = cast<VectorType>(Data->getType());
  unsigned NumElts = PtrsTy->getVectorNumElements();

#ifndef NDEBUG
  auto PtrTy = cast<PointerType>(PtrsTy->getElementType());
  assert(NumElts == DataTy->getVectorNumElements() &&
         PtrTy->getElementType() == DataTy->getElementType() &&
         "Incompatible pointer and data types");
#endif

  if (!Mask)
    Mask = Constant::getAllOnesValue(VectorType::get(Type::getInt1Ty(Context),
                                     NumElts));

  Type *OverloadedTypes[] = {DataTy, PtrsTy};
  Value * Ops[] = {Data, Ptrs, getInt32(Align), Mask};

  // We specify only one type when we create this intrinsic. Types of other
  // arguments are derived from this type.
  return CreateMaskedIntrinsic(Intrinsic::masked_scatter, Ops, OverloadedTypes);
}

template <typename T0, typename T1, typename T2, typename T3>
static std::vector<Value *>
getStatepointArgs(IRBuilderBase &B, uint64_t ID, uint32_t NumPatchBytes,
                  Value *ActualCallee, uint32_t Flags, ArrayRef<T0> CallArgs,
                  ArrayRef<T1> TransitionArgs, ArrayRef<T2> DeoptArgs,
                  ArrayRef<T3> GCArgs) {
  std::vector<Value *> Args;
  Args.push_back(B.getInt64(ID));
  Args.push_back(B.getInt32(NumPatchBytes));
  Args.push_back(ActualCallee);
  Args.push_back(B.getInt32(CallArgs.size()));
  Args.push_back(B.getInt32(Flags));
  Args.insert(Args.end(), CallArgs.begin(), CallArgs.end());
  Args.push_back(B.getInt32(TransitionArgs.size()));
  Args.insert(Args.end(), TransitionArgs.begin(), TransitionArgs.end());
  Args.push_back(B.getInt32(DeoptArgs.size()));
  Args.insert(Args.end(), DeoptArgs.begin(), DeoptArgs.end());
  Args.insert(Args.end(), GCArgs.begin(), GCArgs.end());

  return Args;
}

template <typename T0, typename T1, typename T2, typename T3>
static CallInst *CreateGCStatepointCallCommon(
    IRBuilderBase *Builder, uint64_t ID, uint32_t NumPatchBytes,
    Value *ActualCallee, uint32_t Flags, ArrayRef<T0> CallArgs,
    ArrayRef<T1> TransitionArgs, ArrayRef<T2> DeoptArgs, ArrayRef<T3> GCArgs,
    const Twine &Name) {
  // Extract out the type of the callee.
  auto *FuncPtrType = cast<PointerType>(ActualCallee->getType());
  assert(isa<FunctionType>(FuncPtrType->getElementType()) &&
         "actual callee must be a callable value");

  Module *M = Builder->GetInsertBlock()->getParent()->getParent();
  // Fill in the one generic type'd argument (the function is also vararg)
  Type *ArgTypes[] = { FuncPtrType };
  Function *FnStatepoint =
    Intrinsic::getDeclaration(M, Intrinsic::experimental_gc_statepoint,
                              ArgTypes);

  std::vector<Value *> Args =
      getStatepointArgs(*Builder, ID, NumPatchBytes, ActualCallee, Flags,
                        CallArgs, TransitionArgs, DeoptArgs, GCArgs);
  return createCallHelper(FnStatepoint, Args, Builder, Name);
}

CallInst *IRBuilderBase::CreateGCStatepointCall(
    uint64_t ID, uint32_t NumPatchBytes, Value *ActualCallee,
    ArrayRef<Value *> CallArgs, ArrayRef<Value *> DeoptArgs,
    ArrayRef<Value *> GCArgs, const Twine &Name) {
  return CreateGCStatepointCallCommon<Value *, Value *, Value *, Value *>(
      this, ID, NumPatchBytes, ActualCallee, uint32_t(StatepointFlags::None),
      CallArgs, None /* No Transition Args */, DeoptArgs, GCArgs, Name);
}

CallInst *IRBuilderBase::CreateGCStatepointCall(
    uint64_t ID, uint32_t NumPatchBytes, Value *ActualCallee, uint32_t Flags,
    ArrayRef<Use> CallArgs, ArrayRef<Use> TransitionArgs,
    ArrayRef<Use> DeoptArgs, ArrayRef<Value *> GCArgs, const Twine &Name) {
  return CreateGCStatepointCallCommon<Use, Use, Use, Value *>(
      this, ID, NumPatchBytes, ActualCallee, Flags, CallArgs, TransitionArgs,
      DeoptArgs, GCArgs, Name);
}

CallInst *IRBuilderBase::CreateGCStatepointCall(
    uint64_t ID, uint32_t NumPatchBytes, Value *ActualCallee,
    ArrayRef<Use> CallArgs, ArrayRef<Value *> DeoptArgs,
    ArrayRef<Value *> GCArgs, const Twine &Name) {
  return CreateGCStatepointCallCommon<Use, Value *, Value *, Value *>(
      this, ID, NumPatchBytes, ActualCallee, uint32_t(StatepointFlags::None),
      CallArgs, None, DeoptArgs, GCArgs, Name);
}

template <typename T0, typename T1, typename T2, typename T3>
static InvokeInst *CreateGCStatepointInvokeCommon(
    IRBuilderBase *Builder, uint64_t ID, uint32_t NumPatchBytes,
    Value *ActualInvokee, BasicBlock *NormalDest, BasicBlock *UnwindDest,
    uint32_t Flags, ArrayRef<T0> InvokeArgs, ArrayRef<T1> TransitionArgs,
    ArrayRef<T2> DeoptArgs, ArrayRef<T3> GCArgs, const Twine &Name) {
  // Extract out the type of the callee.
  auto *FuncPtrType = cast<PointerType>(ActualInvokee->getType());
  assert(isa<FunctionType>(FuncPtrType->getElementType()) &&
         "actual callee must be a callable value");

  Module *M = Builder->GetInsertBlock()->getParent()->getParent();
  // Fill in the one generic type'd argument (the function is also vararg)
  Function *FnStatepoint = Intrinsic::getDeclaration(
      M, Intrinsic::experimental_gc_statepoint, {FuncPtrType});

  std::vector<Value *> Args =
      getStatepointArgs(*Builder, ID, NumPatchBytes, ActualInvokee, Flags,
                        InvokeArgs, TransitionArgs, DeoptArgs, GCArgs);
  return createInvokeHelper(FnStatepoint, NormalDest, UnwindDest, Args, Builder,
                            Name);
}

InvokeInst *IRBuilderBase::CreateGCStatepointInvoke(
    uint64_t ID, uint32_t NumPatchBytes, Value *ActualInvokee,
    BasicBlock *NormalDest, BasicBlock *UnwindDest,
    ArrayRef<Value *> InvokeArgs, ArrayRef<Value *> DeoptArgs,
    ArrayRef<Value *> GCArgs, const Twine &Name) {
  return CreateGCStatepointInvokeCommon<Value *, Value *, Value *, Value *>(
      this, ID, NumPatchBytes, ActualInvokee, NormalDest, UnwindDest,
      uint32_t(StatepointFlags::None), InvokeArgs, None /* No Transition Args*/,
      DeoptArgs, GCArgs, Name);
}

InvokeInst *IRBuilderBase::CreateGCStatepointInvoke(
    uint64_t ID, uint32_t NumPatchBytes, Value *ActualInvokee,
    BasicBlock *NormalDest, BasicBlock *UnwindDest, uint32_t Flags,
    ArrayRef<Use> InvokeArgs, ArrayRef<Use> TransitionArgs,
    ArrayRef<Use> DeoptArgs, ArrayRef<Value *> GCArgs, const Twine &Name) {
  return CreateGCStatepointInvokeCommon<Use, Use, Use, Value *>(
      this, ID, NumPatchBytes, ActualInvokee, NormalDest, UnwindDest, Flags,
      InvokeArgs, TransitionArgs, DeoptArgs, GCArgs, Name);
}

InvokeInst *IRBuilderBase::CreateGCStatepointInvoke(
    uint64_t ID, uint32_t NumPatchBytes, Value *ActualInvokee,
    BasicBlock *NormalDest, BasicBlock *UnwindDest, ArrayRef<Use> InvokeArgs,
    ArrayRef<Value *> DeoptArgs, ArrayRef<Value *> GCArgs, const Twine &Name) {
  return CreateGCStatepointInvokeCommon<Use, Value *, Value *, Value *>(
      this, ID, NumPatchBytes, ActualInvokee, NormalDest, UnwindDest,
      uint32_t(StatepointFlags::None), InvokeArgs, None, DeoptArgs, GCArgs,
      Name);
}

CallInst *IRBuilderBase::CreateGCResult(Instruction *Statepoint,
                                       Type *ResultType,
                                       const Twine &Name) {
 Intrinsic::ID ID = Intrinsic::experimental_gc_result;
 Module *M = BB->getParent()->getParent();
 Type *Types[] = {ResultType};
 Value *FnGCResult = Intrinsic::getDeclaration(M, ID, Types);

 Value *Args[] = {Statepoint};
 return createCallHelper(FnGCResult, Args, this, Name);
}

CallInst *IRBuilderBase::CreateGCRelocate(Instruction *Statepoint,
                                         int BaseOffset,
                                         int DerivedOffset,
                                         Type *ResultType,
                                         const Twine &Name) {
 Module *M = BB->getParent()->getParent();
 Type *Types[] = {ResultType};
 Value *FnGCRelocate =
   Intrinsic::getDeclaration(M, Intrinsic::experimental_gc_relocate, Types);

 Value *Args[] = {Statepoint,
                  getInt32(BaseOffset),
                  getInt32(DerivedOffset)};
 return createCallHelper(FnGCRelocate, Args, this, Name);
}

CallInst *IRBuilderBase::CreateUnaryIntrinsic(Intrinsic::ID ID, Value *V,
                                              Instruction *FMFSource,
                                              const Twine &Name) {
  Module *M = BB->getModule();
  Function *Fn = Intrinsic::getDeclaration(M, ID, {V->getType()});
  return createCallHelper(Fn, {V}, this, Name, FMFSource);
}

CallInst *IRBuilderBase::CreateBinaryIntrinsic(Intrinsic::ID ID, Value *LHS,
                                               Value *RHS,
                                               Instruction *FMFSource,
                                               const Twine &Name) {
  Module *M = BB->getModule();
  Function *Fn = Intrinsic::getDeclaration(M, ID, { LHS->getType() });
  return createCallHelper(Fn, {LHS, RHS}, this, Name, FMFSource);
}

CallInst *IRBuilderBase::CreateIntrinsic(Intrinsic::ID ID,
                                         ArrayRef<Type *> Types,
                                         ArrayRef<Value *> Args,
                                         Instruction *FMFSource,
                                         const Twine &Name) {
  Module *M = BB->getModule();
  Function *Fn = Intrinsic::getDeclaration(M, ID, Types);
  return createCallHelper(Fn, Args, this, Name, FMFSource);
}
