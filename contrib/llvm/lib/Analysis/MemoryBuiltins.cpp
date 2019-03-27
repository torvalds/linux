//===- MemoryBuiltins.cpp - Identify calls to memory builtins -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This family of functions identifies calls to builtin functions that allocate
// or free memory.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/TargetFolder.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/Utils/Local.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <iterator>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "memory-builtins"

enum AllocType : uint8_t {
  OpNewLike          = 1<<0, // allocates; never returns null
  MallocLike         = 1<<1 | OpNewLike, // allocates; may return null
  CallocLike         = 1<<2, // allocates + bzero
  ReallocLike        = 1<<3, // reallocates
  StrDupLike         = 1<<4,
  MallocOrCallocLike = MallocLike | CallocLike,
  AllocLike          = MallocLike | CallocLike | StrDupLike,
  AnyAlloc           = AllocLike | ReallocLike
};

struct AllocFnsTy {
  AllocType AllocTy;
  unsigned NumParams;
  // First and Second size parameters (or -1 if unused)
  int FstParam, SndParam;
};

// FIXME: certain users need more information. E.g., SimplifyLibCalls needs to
// know which functions are nounwind, noalias, nocapture parameters, etc.
static const std::pair<LibFunc, AllocFnsTy> AllocationFnData[] = {
  {LibFunc_malloc,              {MallocLike,  1, 0,  -1}},
  {LibFunc_valloc,              {MallocLike,  1, 0,  -1}},
  {LibFunc_Znwj,                {OpNewLike,   1, 0,  -1}}, // new(unsigned int)
  {LibFunc_ZnwjRKSt9nothrow_t,  {MallocLike,  2, 0,  -1}}, // new(unsigned int, nothrow)
  {LibFunc_ZnwjSt11align_val_t, {OpNewLike,   2, 0,  -1}}, // new(unsigned int, align_val_t)
  {LibFunc_ZnwjSt11align_val_tRKSt9nothrow_t, // new(unsigned int, align_val_t, nothrow)
                                {MallocLike,  3, 0,  -1}},
  {LibFunc_Znwm,                {OpNewLike,   1, 0,  -1}}, // new(unsigned long)
  {LibFunc_ZnwmRKSt9nothrow_t,  {MallocLike,  2, 0,  -1}}, // new(unsigned long, nothrow)
  {LibFunc_ZnwmSt11align_val_t, {OpNewLike,   2, 0,  -1}}, // new(unsigned long, align_val_t)
  {LibFunc_ZnwmSt11align_val_tRKSt9nothrow_t, // new(unsigned long, align_val_t, nothrow)
                                {MallocLike,  3, 0,  -1}},
  {LibFunc_Znaj,                {OpNewLike,   1, 0,  -1}}, // new[](unsigned int)
  {LibFunc_ZnajRKSt9nothrow_t,  {MallocLike,  2, 0,  -1}}, // new[](unsigned int, nothrow)
  {LibFunc_ZnajSt11align_val_t, {OpNewLike,   2, 0,  -1}}, // new[](unsigned int, align_val_t)
  {LibFunc_ZnajSt11align_val_tRKSt9nothrow_t, // new[](unsigned int, align_val_t, nothrow)
                                {MallocLike,  3, 0,  -1}},
  {LibFunc_Znam,                {OpNewLike,   1, 0,  -1}}, // new[](unsigned long)
  {LibFunc_ZnamRKSt9nothrow_t,  {MallocLike,  2, 0,  -1}}, // new[](unsigned long, nothrow)
  {LibFunc_ZnamSt11align_val_t, {OpNewLike,   2, 0,  -1}}, // new[](unsigned long, align_val_t)
  {LibFunc_ZnamSt11align_val_tRKSt9nothrow_t, // new[](unsigned long, align_val_t, nothrow)
                                 {MallocLike,  3, 0,  -1}},
  {LibFunc_msvc_new_int,         {OpNewLike,   1, 0,  -1}}, // new(unsigned int)
  {LibFunc_msvc_new_int_nothrow, {MallocLike,  2, 0,  -1}}, // new(unsigned int, nothrow)
  {LibFunc_msvc_new_longlong,         {OpNewLike,   1, 0,  -1}}, // new(unsigned long long)
  {LibFunc_msvc_new_longlong_nothrow, {MallocLike,  2, 0,  -1}}, // new(unsigned long long, nothrow)
  {LibFunc_msvc_new_array_int,         {OpNewLike,   1, 0,  -1}}, // new[](unsigned int)
  {LibFunc_msvc_new_array_int_nothrow, {MallocLike,  2, 0,  -1}}, // new[](unsigned int, nothrow)
  {LibFunc_msvc_new_array_longlong,         {OpNewLike,   1, 0,  -1}}, // new[](unsigned long long)
  {LibFunc_msvc_new_array_longlong_nothrow, {MallocLike,  2, 0,  -1}}, // new[](unsigned long long, nothrow)
  {LibFunc_calloc,              {CallocLike,  2, 0,   1}},
  {LibFunc_realloc,             {ReallocLike, 2, 1,  -1}},
  {LibFunc_reallocf,            {ReallocLike, 2, 1,  -1}},
  {LibFunc_strdup,              {StrDupLike,  1, -1, -1}},
  {LibFunc_strndup,             {StrDupLike,  2, 1,  -1}}
  // TODO: Handle "int posix_memalign(void **, size_t, size_t)"
};

static const Function *getCalledFunction(const Value *V, bool LookThroughBitCast,
                                         bool &IsNoBuiltin) {
  // Don't care about intrinsics in this case.
  if (isa<IntrinsicInst>(V))
    return nullptr;

  if (LookThroughBitCast)
    V = V->stripPointerCasts();

  ImmutableCallSite CS(V);
  if (!CS.getInstruction())
    return nullptr;

  IsNoBuiltin = CS.isNoBuiltin();

  if (const Function *Callee = CS.getCalledFunction())
    return Callee;
  return nullptr;
}

/// Returns the allocation data for the given value if it's either a call to a
/// known allocation function, or a call to a function with the allocsize
/// attribute.
static Optional<AllocFnsTy>
getAllocationDataForFunction(const Function *Callee, AllocType AllocTy,
                             const TargetLibraryInfo *TLI) {
  // Make sure that the function is available.
  StringRef FnName = Callee->getName();
  LibFunc TLIFn;
  if (!TLI || !TLI->getLibFunc(FnName, TLIFn) || !TLI->has(TLIFn))
    return None;

  const auto *Iter = find_if(
      AllocationFnData, [TLIFn](const std::pair<LibFunc, AllocFnsTy> &P) {
        return P.first == TLIFn;
      });

  if (Iter == std::end(AllocationFnData))
    return None;

  const AllocFnsTy *FnData = &Iter->second;
  if ((FnData->AllocTy & AllocTy) != FnData->AllocTy)
    return None;

  // Check function prototype.
  int FstParam = FnData->FstParam;
  int SndParam = FnData->SndParam;
  FunctionType *FTy = Callee->getFunctionType();

  if (FTy->getReturnType() == Type::getInt8PtrTy(FTy->getContext()) &&
      FTy->getNumParams() == FnData->NumParams &&
      (FstParam < 0 ||
       (FTy->getParamType(FstParam)->isIntegerTy(32) ||
        FTy->getParamType(FstParam)->isIntegerTy(64))) &&
      (SndParam < 0 ||
       FTy->getParamType(SndParam)->isIntegerTy(32) ||
       FTy->getParamType(SndParam)->isIntegerTy(64)))
    return *FnData;
  return None;
}

static Optional<AllocFnsTy> getAllocationData(const Value *V, AllocType AllocTy,
                                              const TargetLibraryInfo *TLI,
                                              bool LookThroughBitCast = false) {
  bool IsNoBuiltinCall;
  if (const Function *Callee =
          getCalledFunction(V, LookThroughBitCast, IsNoBuiltinCall))
    if (!IsNoBuiltinCall)
      return getAllocationDataForFunction(Callee, AllocTy, TLI);
  return None;
}

static Optional<AllocFnsTy> getAllocationSize(const Value *V,
                                              const TargetLibraryInfo *TLI) {
  bool IsNoBuiltinCall;
  const Function *Callee =
      getCalledFunction(V, /*LookThroughBitCast=*/false, IsNoBuiltinCall);
  if (!Callee)
    return None;

  // Prefer to use existing information over allocsize. This will give us an
  // accurate AllocTy.
  if (!IsNoBuiltinCall)
    if (Optional<AllocFnsTy> Data =
            getAllocationDataForFunction(Callee, AnyAlloc, TLI))
      return Data;

  Attribute Attr = Callee->getFnAttribute(Attribute::AllocSize);
  if (Attr == Attribute())
    return None;

  std::pair<unsigned, Optional<unsigned>> Args = Attr.getAllocSizeArgs();

  AllocFnsTy Result;
  // Because allocsize only tells us how many bytes are allocated, we're not
  // really allowed to assume anything, so we use MallocLike.
  Result.AllocTy = MallocLike;
  Result.NumParams = Callee->getNumOperands();
  Result.FstParam = Args.first;
  Result.SndParam = Args.second.getValueOr(-1);
  return Result;
}

static bool hasNoAliasAttr(const Value *V, bool LookThroughBitCast) {
  ImmutableCallSite CS(LookThroughBitCast ? V->stripPointerCasts() : V);
  return CS && CS.hasRetAttr(Attribute::NoAlias);
}

/// Tests if a value is a call or invoke to a library function that
/// allocates or reallocates memory (either malloc, calloc, realloc, or strdup
/// like).
bool llvm::isAllocationFn(const Value *V, const TargetLibraryInfo *TLI,
                          bool LookThroughBitCast) {
  return getAllocationData(V, AnyAlloc, TLI, LookThroughBitCast).hasValue();
}

/// Tests if a value is a call or invoke to a function that returns a
/// NoAlias pointer (including malloc/calloc/realloc/strdup-like functions).
bool llvm::isNoAliasFn(const Value *V, const TargetLibraryInfo *TLI,
                       bool LookThroughBitCast) {
  // it's safe to consider realloc as noalias since accessing the original
  // pointer is undefined behavior
  return isAllocationFn(V, TLI, LookThroughBitCast) ||
         hasNoAliasAttr(V, LookThroughBitCast);
}

/// Tests if a value is a call or invoke to a library function that
/// allocates uninitialized memory (such as malloc).
bool llvm::isMallocLikeFn(const Value *V, const TargetLibraryInfo *TLI,
                          bool LookThroughBitCast) {
  return getAllocationData(V, MallocLike, TLI, LookThroughBitCast).hasValue();
}

/// Tests if a value is a call or invoke to a library function that
/// allocates zero-filled memory (such as calloc).
bool llvm::isCallocLikeFn(const Value *V, const TargetLibraryInfo *TLI,
                          bool LookThroughBitCast) {
  return getAllocationData(V, CallocLike, TLI, LookThroughBitCast).hasValue();
}

/// Tests if a value is a call or invoke to a library function that
/// allocates memory similar to malloc or calloc.
bool llvm::isMallocOrCallocLikeFn(const Value *V, const TargetLibraryInfo *TLI,
                                  bool LookThroughBitCast) {
  return getAllocationData(V, MallocOrCallocLike, TLI,
                           LookThroughBitCast).hasValue();
}

/// Tests if a value is a call or invoke to a library function that
/// allocates memory (either malloc, calloc, or strdup like).
bool llvm::isAllocLikeFn(const Value *V, const TargetLibraryInfo *TLI,
                         bool LookThroughBitCast) {
  return getAllocationData(V, AllocLike, TLI, LookThroughBitCast).hasValue();
}

/// extractMallocCall - Returns the corresponding CallInst if the instruction
/// is a malloc call.  Since CallInst::CreateMalloc() only creates calls, we
/// ignore InvokeInst here.
const CallInst *llvm::extractMallocCall(const Value *I,
                                        const TargetLibraryInfo *TLI) {
  return isMallocLikeFn(I, TLI) ? dyn_cast<CallInst>(I) : nullptr;
}

static Value *computeArraySize(const CallInst *CI, const DataLayout &DL,
                               const TargetLibraryInfo *TLI,
                               bool LookThroughSExt = false) {
  if (!CI)
    return nullptr;

  // The size of the malloc's result type must be known to determine array size.
  Type *T = getMallocAllocatedType(CI, TLI);
  if (!T || !T->isSized())
    return nullptr;

  unsigned ElementSize = DL.getTypeAllocSize(T);
  if (StructType *ST = dyn_cast<StructType>(T))
    ElementSize = DL.getStructLayout(ST)->getSizeInBytes();

  // If malloc call's arg can be determined to be a multiple of ElementSize,
  // return the multiple.  Otherwise, return NULL.
  Value *MallocArg = CI->getArgOperand(0);
  Value *Multiple = nullptr;
  if (ComputeMultiple(MallocArg, ElementSize, Multiple, LookThroughSExt))
    return Multiple;

  return nullptr;
}

/// getMallocType - Returns the PointerType resulting from the malloc call.
/// The PointerType depends on the number of bitcast uses of the malloc call:
///   0: PointerType is the calls' return type.
///   1: PointerType is the bitcast's result type.
///  >1: Unique PointerType cannot be determined, return NULL.
PointerType *llvm::getMallocType(const CallInst *CI,
                                 const TargetLibraryInfo *TLI) {
  assert(isMallocLikeFn(CI, TLI) && "getMallocType and not malloc call");

  PointerType *MallocType = nullptr;
  unsigned NumOfBitCastUses = 0;

  // Determine if CallInst has a bitcast use.
  for (Value::const_user_iterator UI = CI->user_begin(), E = CI->user_end();
       UI != E;)
    if (const BitCastInst *BCI = dyn_cast<BitCastInst>(*UI++)) {
      MallocType = cast<PointerType>(BCI->getDestTy());
      NumOfBitCastUses++;
    }

  // Malloc call has 1 bitcast use, so type is the bitcast's destination type.
  if (NumOfBitCastUses == 1)
    return MallocType;

  // Malloc call was not bitcast, so type is the malloc function's return type.
  if (NumOfBitCastUses == 0)
    return cast<PointerType>(CI->getType());

  // Type could not be determined.
  return nullptr;
}

/// getMallocAllocatedType - Returns the Type allocated by malloc call.
/// The Type depends on the number of bitcast uses of the malloc call:
///   0: PointerType is the malloc calls' return type.
///   1: PointerType is the bitcast's result type.
///  >1: Unique PointerType cannot be determined, return NULL.
Type *llvm::getMallocAllocatedType(const CallInst *CI,
                                   const TargetLibraryInfo *TLI) {
  PointerType *PT = getMallocType(CI, TLI);
  return PT ? PT->getElementType() : nullptr;
}

/// getMallocArraySize - Returns the array size of a malloc call.  If the
/// argument passed to malloc is a multiple of the size of the malloced type,
/// then return that multiple.  For non-array mallocs, the multiple is
/// constant 1.  Otherwise, return NULL for mallocs whose array size cannot be
/// determined.
Value *llvm::getMallocArraySize(CallInst *CI, const DataLayout &DL,
                                const TargetLibraryInfo *TLI,
                                bool LookThroughSExt) {
  assert(isMallocLikeFn(CI, TLI) && "getMallocArraySize and not malloc call");
  return computeArraySize(CI, DL, TLI, LookThroughSExt);
}

/// extractCallocCall - Returns the corresponding CallInst if the instruction
/// is a calloc call.
const CallInst *llvm::extractCallocCall(const Value *I,
                                        const TargetLibraryInfo *TLI) {
  return isCallocLikeFn(I, TLI) ? cast<CallInst>(I) : nullptr;
}

/// isFreeCall - Returns non-null if the value is a call to the builtin free()
const CallInst *llvm::isFreeCall(const Value *I, const TargetLibraryInfo *TLI) {
  bool IsNoBuiltinCall;
  const Function *Callee =
      getCalledFunction(I, /*LookThroughBitCast=*/false, IsNoBuiltinCall);
  if (Callee == nullptr || IsNoBuiltinCall)
    return nullptr;

  StringRef FnName = Callee->getName();
  LibFunc TLIFn;
  if (!TLI || !TLI->getLibFunc(FnName, TLIFn) || !TLI->has(TLIFn))
    return nullptr;

  unsigned ExpectedNumParams;
  if (TLIFn == LibFunc_free ||
      TLIFn == LibFunc_ZdlPv || // operator delete(void*)
      TLIFn == LibFunc_ZdaPv || // operator delete[](void*)
      TLIFn == LibFunc_msvc_delete_ptr32 || // operator delete(void*)
      TLIFn == LibFunc_msvc_delete_ptr64 || // operator delete(void*)
      TLIFn == LibFunc_msvc_delete_array_ptr32 || // operator delete[](void*)
      TLIFn == LibFunc_msvc_delete_array_ptr64)   // operator delete[](void*)
    ExpectedNumParams = 1;
  else if (TLIFn == LibFunc_ZdlPvj ||              // delete(void*, uint)
           TLIFn == LibFunc_ZdlPvm ||              // delete(void*, ulong)
           TLIFn == LibFunc_ZdlPvRKSt9nothrow_t || // delete(void*, nothrow)
           TLIFn == LibFunc_ZdlPvSt11align_val_t || // delete(void*, align_val_t)
           TLIFn == LibFunc_ZdaPvj ||              // delete[](void*, uint)
           TLIFn == LibFunc_ZdaPvm ||              // delete[](void*, ulong)
           TLIFn == LibFunc_ZdaPvRKSt9nothrow_t || // delete[](void*, nothrow)
           TLIFn == LibFunc_ZdaPvSt11align_val_t || // delete[](void*, align_val_t)
           TLIFn == LibFunc_msvc_delete_ptr32_int ||      // delete(void*, uint)
           TLIFn == LibFunc_msvc_delete_ptr64_longlong || // delete(void*, ulonglong)
           TLIFn == LibFunc_msvc_delete_ptr32_nothrow || // delete(void*, nothrow)
           TLIFn == LibFunc_msvc_delete_ptr64_nothrow || // delete(void*, nothrow)
           TLIFn == LibFunc_msvc_delete_array_ptr32_int ||      // delete[](void*, uint)
           TLIFn == LibFunc_msvc_delete_array_ptr64_longlong || // delete[](void*, ulonglong)
           TLIFn == LibFunc_msvc_delete_array_ptr32_nothrow || // delete[](void*, nothrow)
           TLIFn == LibFunc_msvc_delete_array_ptr64_nothrow)   // delete[](void*, nothrow)
    ExpectedNumParams = 2;
  else if (TLIFn == LibFunc_ZdaPvSt11align_val_tRKSt9nothrow_t || // delete(void*, align_val_t, nothrow)
           TLIFn == LibFunc_ZdlPvSt11align_val_tRKSt9nothrow_t) // delete[](void*, align_val_t, nothrow)
    ExpectedNumParams = 3;
  else
    return nullptr;

  // Check free prototype.
  // FIXME: workaround for PR5130, this will be obsolete when a nobuiltin
  // attribute will exist.
  FunctionType *FTy = Callee->getFunctionType();
  if (!FTy->getReturnType()->isVoidTy())
    return nullptr;
  if (FTy->getNumParams() != ExpectedNumParams)
    return nullptr;
  if (FTy->getParamType(0) != Type::getInt8PtrTy(Callee->getContext()))
    return nullptr;

  return dyn_cast<CallInst>(I);
}

//===----------------------------------------------------------------------===//
//  Utility functions to compute size of objects.
//
static APInt getSizeWithOverflow(const SizeOffsetType &Data) {
  if (Data.second.isNegative() || Data.first.ult(Data.second))
    return APInt(Data.first.getBitWidth(), 0);
  return Data.first - Data.second;
}

/// Compute the size of the object pointed by Ptr. Returns true and the
/// object size in Size if successful, and false otherwise.
/// If RoundToAlign is true, then Size is rounded up to the alignment of
/// allocas, byval arguments, and global variables.
bool llvm::getObjectSize(const Value *Ptr, uint64_t &Size, const DataLayout &DL,
                         const TargetLibraryInfo *TLI, ObjectSizeOpts Opts) {
  ObjectSizeOffsetVisitor Visitor(DL, TLI, Ptr->getContext(), Opts);
  SizeOffsetType Data = Visitor.compute(const_cast<Value*>(Ptr));
  if (!Visitor.bothKnown(Data))
    return false;

  Size = getSizeWithOverflow(Data).getZExtValue();
  return true;
}

ConstantInt *llvm::lowerObjectSizeCall(IntrinsicInst *ObjectSize,
                                       const DataLayout &DL,
                                       const TargetLibraryInfo *TLI,
                                       bool MustSucceed) {
  assert(ObjectSize->getIntrinsicID() == Intrinsic::objectsize &&
         "ObjectSize must be a call to llvm.objectsize!");

  bool MaxVal = cast<ConstantInt>(ObjectSize->getArgOperand(1))->isZero();
  ObjectSizeOpts EvalOptions;
  // Unless we have to fold this to something, try to be as accurate as
  // possible.
  if (MustSucceed)
    EvalOptions.EvalMode =
        MaxVal ? ObjectSizeOpts::Mode::Max : ObjectSizeOpts::Mode::Min;
  else
    EvalOptions.EvalMode = ObjectSizeOpts::Mode::Exact;

  EvalOptions.NullIsUnknownSize =
      cast<ConstantInt>(ObjectSize->getArgOperand(2))->isOne();

  // FIXME: Does it make sense to just return a failure value if the size won't
  // fit in the output and `!MustSucceed`?
  uint64_t Size;
  auto *ResultType = cast<IntegerType>(ObjectSize->getType());
  if (getObjectSize(ObjectSize->getArgOperand(0), Size, DL, TLI, EvalOptions) &&
      isUIntN(ResultType->getBitWidth(), Size))
    return ConstantInt::get(ResultType, Size);

  if (!MustSucceed)
    return nullptr;

  return ConstantInt::get(ResultType, MaxVal ? -1ULL : 0);
}

STATISTIC(ObjectVisitorArgument,
          "Number of arguments with unsolved size and offset");
STATISTIC(ObjectVisitorLoad,
          "Number of load instructions with unsolved size and offset");

APInt ObjectSizeOffsetVisitor::align(APInt Size, uint64_t Align) {
  if (Options.RoundToAlign && Align)
    return APInt(IntTyBits, alignTo(Size.getZExtValue(), Align));
  return Size;
}

ObjectSizeOffsetVisitor::ObjectSizeOffsetVisitor(const DataLayout &DL,
                                                 const TargetLibraryInfo *TLI,
                                                 LLVMContext &Context,
                                                 ObjectSizeOpts Options)
    : DL(DL), TLI(TLI), Options(Options) {
  // Pointer size must be rechecked for each object visited since it could have
  // a different address space.
}

SizeOffsetType ObjectSizeOffsetVisitor::compute(Value *V) {
  IntTyBits = DL.getPointerTypeSizeInBits(V->getType());
  Zero = APInt::getNullValue(IntTyBits);

  V = V->stripPointerCasts();
  if (Instruction *I = dyn_cast<Instruction>(V)) {
    // If we have already seen this instruction, bail out. Cycles can happen in
    // unreachable code after constant propagation.
    if (!SeenInsts.insert(I).second)
      return unknown();

    if (GEPOperator *GEP = dyn_cast<GEPOperator>(V))
      return visitGEPOperator(*GEP);
    return visit(*I);
  }
  if (Argument *A = dyn_cast<Argument>(V))
    return visitArgument(*A);
  if (ConstantPointerNull *P = dyn_cast<ConstantPointerNull>(V))
    return visitConstantPointerNull(*P);
  if (GlobalAlias *GA = dyn_cast<GlobalAlias>(V))
    return visitGlobalAlias(*GA);
  if (GlobalVariable *GV = dyn_cast<GlobalVariable>(V))
    return visitGlobalVariable(*GV);
  if (UndefValue *UV = dyn_cast<UndefValue>(V))
    return visitUndefValue(*UV);
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(V)) {
    if (CE->getOpcode() == Instruction::IntToPtr)
      return unknown(); // clueless
    if (CE->getOpcode() == Instruction::GetElementPtr)
      return visitGEPOperator(cast<GEPOperator>(*CE));
  }

  LLVM_DEBUG(dbgs() << "ObjectSizeOffsetVisitor::compute() unhandled value: "
                    << *V << '\n');
  return unknown();
}

/// When we're compiling N-bit code, and the user uses parameters that are
/// greater than N bits (e.g. uint64_t on a 32-bit build), we can run into
/// trouble with APInt size issues. This function handles resizing + overflow
/// checks for us. Check and zext or trunc \p I depending on IntTyBits and
/// I's value.
bool ObjectSizeOffsetVisitor::CheckedZextOrTrunc(APInt &I) {
  // More bits than we can handle. Checking the bit width isn't necessary, but
  // it's faster than checking active bits, and should give `false` in the
  // vast majority of cases.
  if (I.getBitWidth() > IntTyBits && I.getActiveBits() > IntTyBits)
    return false;
  if (I.getBitWidth() != IntTyBits)
    I = I.zextOrTrunc(IntTyBits);
  return true;
}

SizeOffsetType ObjectSizeOffsetVisitor::visitAllocaInst(AllocaInst &I) {
  if (!I.getAllocatedType()->isSized())
    return unknown();

  APInt Size(IntTyBits, DL.getTypeAllocSize(I.getAllocatedType()));
  if (!I.isArrayAllocation())
    return std::make_pair(align(Size, I.getAlignment()), Zero);

  Value *ArraySize = I.getArraySize();
  if (const ConstantInt *C = dyn_cast<ConstantInt>(ArraySize)) {
    APInt NumElems = C->getValue();
    if (!CheckedZextOrTrunc(NumElems))
      return unknown();

    bool Overflow;
    Size = Size.umul_ov(NumElems, Overflow);
    return Overflow ? unknown() : std::make_pair(align(Size, I.getAlignment()),
                                                 Zero);
  }
  return unknown();
}

SizeOffsetType ObjectSizeOffsetVisitor::visitArgument(Argument &A) {
  // No interprocedural analysis is done at the moment.
  if (!A.hasByValOrInAllocaAttr()) {
    ++ObjectVisitorArgument;
    return unknown();
  }
  PointerType *PT = cast<PointerType>(A.getType());
  APInt Size(IntTyBits, DL.getTypeAllocSize(PT->getElementType()));
  return std::make_pair(align(Size, A.getParamAlignment()), Zero);
}

SizeOffsetType ObjectSizeOffsetVisitor::visitCallSite(CallSite CS) {
  Optional<AllocFnsTy> FnData = getAllocationSize(CS.getInstruction(), TLI);
  if (!FnData)
    return unknown();

  // Handle strdup-like functions separately.
  if (FnData->AllocTy == StrDupLike) {
    APInt Size(IntTyBits, GetStringLength(CS.getArgument(0)));
    if (!Size)
      return unknown();

    // Strndup limits strlen.
    if (FnData->FstParam > 0) {
      ConstantInt *Arg =
          dyn_cast<ConstantInt>(CS.getArgument(FnData->FstParam));
      if (!Arg)
        return unknown();

      APInt MaxSize = Arg->getValue().zextOrSelf(IntTyBits);
      if (Size.ugt(MaxSize))
        Size = MaxSize + 1;
    }
    return std::make_pair(Size, Zero);
  }

  ConstantInt *Arg = dyn_cast<ConstantInt>(CS.getArgument(FnData->FstParam));
  if (!Arg)
    return unknown();

  APInt Size = Arg->getValue();
  if (!CheckedZextOrTrunc(Size))
    return unknown();

  // Size is determined by just 1 parameter.
  if (FnData->SndParam < 0)
    return std::make_pair(Size, Zero);

  Arg = dyn_cast<ConstantInt>(CS.getArgument(FnData->SndParam));
  if (!Arg)
    return unknown();

  APInt NumElems = Arg->getValue();
  if (!CheckedZextOrTrunc(NumElems))
    return unknown();

  bool Overflow;
  Size = Size.umul_ov(NumElems, Overflow);
  return Overflow ? unknown() : std::make_pair(Size, Zero);

  // TODO: handle more standard functions (+ wchar cousins):
  // - strdup / strndup
  // - strcpy / strncpy
  // - strcat / strncat
  // - memcpy / memmove
  // - strcat / strncat
  // - memset
}

SizeOffsetType
ObjectSizeOffsetVisitor::visitConstantPointerNull(ConstantPointerNull& CPN) {
  // If null is unknown, there's nothing we can do. Additionally, non-zero
  // address spaces can make use of null, so we don't presume to know anything
  // about that.
  //
  // TODO: How should this work with address space casts? We currently just drop
  // them on the floor, but it's unclear what we should do when a NULL from
  // addrspace(1) gets casted to addrspace(0) (or vice-versa).
  if (Options.NullIsUnknownSize || CPN.getType()->getAddressSpace())
    return unknown();
  return std::make_pair(Zero, Zero);
}

SizeOffsetType
ObjectSizeOffsetVisitor::visitExtractElementInst(ExtractElementInst&) {
  return unknown();
}

SizeOffsetType
ObjectSizeOffsetVisitor::visitExtractValueInst(ExtractValueInst&) {
  // Easy cases were already folded by previous passes.
  return unknown();
}

SizeOffsetType ObjectSizeOffsetVisitor::visitGEPOperator(GEPOperator &GEP) {
  SizeOffsetType PtrData = compute(GEP.getPointerOperand());
  APInt Offset(IntTyBits, 0);
  if (!bothKnown(PtrData) || !GEP.accumulateConstantOffset(DL, Offset))
    return unknown();

  return std::make_pair(PtrData.first, PtrData.second + Offset);
}

SizeOffsetType ObjectSizeOffsetVisitor::visitGlobalAlias(GlobalAlias &GA) {
  if (GA.isInterposable())
    return unknown();
  return compute(GA.getAliasee());
}

SizeOffsetType ObjectSizeOffsetVisitor::visitGlobalVariable(GlobalVariable &GV){
  if (!GV.hasDefinitiveInitializer())
    return unknown();

  APInt Size(IntTyBits, DL.getTypeAllocSize(GV.getType()->getElementType()));
  return std::make_pair(align(Size, GV.getAlignment()), Zero);
}

SizeOffsetType ObjectSizeOffsetVisitor::visitIntToPtrInst(IntToPtrInst&) {
  // clueless
  return unknown();
}

SizeOffsetType ObjectSizeOffsetVisitor::visitLoadInst(LoadInst&) {
  ++ObjectVisitorLoad;
  return unknown();
}

SizeOffsetType ObjectSizeOffsetVisitor::visitPHINode(PHINode&) {
  // too complex to analyze statically.
  return unknown();
}

SizeOffsetType ObjectSizeOffsetVisitor::visitSelectInst(SelectInst &I) {
  SizeOffsetType TrueSide  = compute(I.getTrueValue());
  SizeOffsetType FalseSide = compute(I.getFalseValue());
  if (bothKnown(TrueSide) && bothKnown(FalseSide)) {
    if (TrueSide == FalseSide) {
        return TrueSide;
    }

    APInt TrueResult = getSizeWithOverflow(TrueSide);
    APInt FalseResult = getSizeWithOverflow(FalseSide);

    if (TrueResult == FalseResult) {
      return TrueSide;
    }
    if (Options.EvalMode == ObjectSizeOpts::Mode::Min) {
      if (TrueResult.slt(FalseResult))
        return TrueSide;
      return FalseSide;
    }
    if (Options.EvalMode == ObjectSizeOpts::Mode::Max) {
      if (TrueResult.sgt(FalseResult))
        return TrueSide;
      return FalseSide;
    }
  }
  return unknown();
}

SizeOffsetType ObjectSizeOffsetVisitor::visitUndefValue(UndefValue&) {
  return std::make_pair(Zero, Zero);
}

SizeOffsetType ObjectSizeOffsetVisitor::visitInstruction(Instruction &I) {
  LLVM_DEBUG(dbgs() << "ObjectSizeOffsetVisitor unknown instruction:" << I
                    << '\n');
  return unknown();
}

ObjectSizeOffsetEvaluator::ObjectSizeOffsetEvaluator(
    const DataLayout &DL, const TargetLibraryInfo *TLI, LLVMContext &Context,
    bool RoundToAlign)
    : DL(DL), TLI(TLI), Context(Context), Builder(Context, TargetFolder(DL)),
      RoundToAlign(RoundToAlign) {
  // IntTy and Zero must be set for each compute() since the address space may
  // be different for later objects.
}

SizeOffsetEvalType ObjectSizeOffsetEvaluator::compute(Value *V) {
  // XXX - Are vectors of pointers possible here?
  IntTy = cast<IntegerType>(DL.getIntPtrType(V->getType()));
  Zero = ConstantInt::get(IntTy, 0);

  SizeOffsetEvalType Result = compute_(V);

  if (!bothKnown(Result)) {
    // Erase everything that was computed in this iteration from the cache, so
    // that no dangling references are left behind. We could be a bit smarter if
    // we kept a dependency graph. It's probably not worth the complexity.
    for (const Value *SeenVal : SeenVals) {
      CacheMapTy::iterator CacheIt = CacheMap.find(SeenVal);
      // non-computable results can be safely cached
      if (CacheIt != CacheMap.end() && anyKnown(CacheIt->second))
        CacheMap.erase(CacheIt);
    }
  }

  SeenVals.clear();
  return Result;
}

SizeOffsetEvalType ObjectSizeOffsetEvaluator::compute_(Value *V) {
  ObjectSizeOpts ObjSizeOptions;
  ObjSizeOptions.RoundToAlign = RoundToAlign;

  ObjectSizeOffsetVisitor Visitor(DL, TLI, Context, ObjSizeOptions);
  SizeOffsetType Const = Visitor.compute(V);
  if (Visitor.bothKnown(Const))
    return std::make_pair(ConstantInt::get(Context, Const.first),
                          ConstantInt::get(Context, Const.second));

  V = V->stripPointerCasts();

  // Check cache.
  CacheMapTy::iterator CacheIt = CacheMap.find(V);
  if (CacheIt != CacheMap.end())
    return CacheIt->second;

  // Always generate code immediately before the instruction being
  // processed, so that the generated code dominates the same BBs.
  BuilderTy::InsertPointGuard Guard(Builder);
  if (Instruction *I = dyn_cast<Instruction>(V))
    Builder.SetInsertPoint(I);

  // Now compute the size and offset.
  SizeOffsetEvalType Result;

  // Record the pointers that were handled in this run, so that they can be
  // cleaned later if something fails. We also use this set to break cycles that
  // can occur in dead code.
  if (!SeenVals.insert(V).second) {
    Result = unknown();
  } else if (GEPOperator *GEP = dyn_cast<GEPOperator>(V)) {
    Result = visitGEPOperator(*GEP);
  } else if (Instruction *I = dyn_cast<Instruction>(V)) {
    Result = visit(*I);
  } else if (isa<Argument>(V) ||
             (isa<ConstantExpr>(V) &&
              cast<ConstantExpr>(V)->getOpcode() == Instruction::IntToPtr) ||
             isa<GlobalAlias>(V) ||
             isa<GlobalVariable>(V)) {
    // Ignore values where we cannot do more than ObjectSizeVisitor.
    Result = unknown();
  } else {
    LLVM_DEBUG(
        dbgs() << "ObjectSizeOffsetEvaluator::compute() unhandled value: " << *V
               << '\n');
    Result = unknown();
  }

  // Don't reuse CacheIt since it may be invalid at this point.
  CacheMap[V] = Result;
  return Result;
}

SizeOffsetEvalType ObjectSizeOffsetEvaluator::visitAllocaInst(AllocaInst &I) {
  if (!I.getAllocatedType()->isSized())
    return unknown();

  // must be a VLA
  assert(I.isArrayAllocation());
  Value *ArraySize = I.getArraySize();
  Value *Size = ConstantInt::get(ArraySize->getType(),
                                 DL.getTypeAllocSize(I.getAllocatedType()));
  Size = Builder.CreateMul(Size, ArraySize);
  return std::make_pair(Size, Zero);
}

SizeOffsetEvalType ObjectSizeOffsetEvaluator::visitCallSite(CallSite CS) {
  Optional<AllocFnsTy> FnData = getAllocationSize(CS.getInstruction(), TLI);
  if (!FnData)
    return unknown();

  // Handle strdup-like functions separately.
  if (FnData->AllocTy == StrDupLike) {
    // TODO
    return unknown();
  }

  Value *FirstArg = CS.getArgument(FnData->FstParam);
  FirstArg = Builder.CreateZExt(FirstArg, IntTy);
  if (FnData->SndParam < 0)
    return std::make_pair(FirstArg, Zero);

  Value *SecondArg = CS.getArgument(FnData->SndParam);
  SecondArg = Builder.CreateZExt(SecondArg, IntTy);
  Value *Size = Builder.CreateMul(FirstArg, SecondArg);
  return std::make_pair(Size, Zero);

  // TODO: handle more standard functions (+ wchar cousins):
  // - strdup / strndup
  // - strcpy / strncpy
  // - strcat / strncat
  // - memcpy / memmove
  // - strcat / strncat
  // - memset
}

SizeOffsetEvalType
ObjectSizeOffsetEvaluator::visitExtractElementInst(ExtractElementInst&) {
  return unknown();
}

SizeOffsetEvalType
ObjectSizeOffsetEvaluator::visitExtractValueInst(ExtractValueInst&) {
  return unknown();
}

SizeOffsetEvalType
ObjectSizeOffsetEvaluator::visitGEPOperator(GEPOperator &GEP) {
  SizeOffsetEvalType PtrData = compute_(GEP.getPointerOperand());
  if (!bothKnown(PtrData))
    return unknown();

  Value *Offset = EmitGEPOffset(&Builder, DL, &GEP, /*NoAssumptions=*/true);
  Offset = Builder.CreateAdd(PtrData.second, Offset);
  return std::make_pair(PtrData.first, Offset);
}

SizeOffsetEvalType ObjectSizeOffsetEvaluator::visitIntToPtrInst(IntToPtrInst&) {
  // clueless
  return unknown();
}

SizeOffsetEvalType ObjectSizeOffsetEvaluator::visitLoadInst(LoadInst&) {
  return unknown();
}

SizeOffsetEvalType ObjectSizeOffsetEvaluator::visitPHINode(PHINode &PHI) {
  // Create 2 PHIs: one for size and another for offset.
  PHINode *SizePHI   = Builder.CreatePHI(IntTy, PHI.getNumIncomingValues());
  PHINode *OffsetPHI = Builder.CreatePHI(IntTy, PHI.getNumIncomingValues());

  // Insert right away in the cache to handle recursive PHIs.
  CacheMap[&PHI] = std::make_pair(SizePHI, OffsetPHI);

  // Compute offset/size for each PHI incoming pointer.
  for (unsigned i = 0, e = PHI.getNumIncomingValues(); i != e; ++i) {
    Builder.SetInsertPoint(&*PHI.getIncomingBlock(i)->getFirstInsertionPt());
    SizeOffsetEvalType EdgeData = compute_(PHI.getIncomingValue(i));

    if (!bothKnown(EdgeData)) {
      OffsetPHI->replaceAllUsesWith(UndefValue::get(IntTy));
      OffsetPHI->eraseFromParent();
      SizePHI->replaceAllUsesWith(UndefValue::get(IntTy));
      SizePHI->eraseFromParent();
      return unknown();
    }
    SizePHI->addIncoming(EdgeData.first, PHI.getIncomingBlock(i));
    OffsetPHI->addIncoming(EdgeData.second, PHI.getIncomingBlock(i));
  }

  Value *Size = SizePHI, *Offset = OffsetPHI, *Tmp;
  if ((Tmp = SizePHI->hasConstantValue())) {
    Size = Tmp;
    SizePHI->replaceAllUsesWith(Size);
    SizePHI->eraseFromParent();
  }
  if ((Tmp = OffsetPHI->hasConstantValue())) {
    Offset = Tmp;
    OffsetPHI->replaceAllUsesWith(Offset);
    OffsetPHI->eraseFromParent();
  }
  return std::make_pair(Size, Offset);
}

SizeOffsetEvalType ObjectSizeOffsetEvaluator::visitSelectInst(SelectInst &I) {
  SizeOffsetEvalType TrueSide  = compute_(I.getTrueValue());
  SizeOffsetEvalType FalseSide = compute_(I.getFalseValue());

  if (!bothKnown(TrueSide) || !bothKnown(FalseSide))
    return unknown();
  if (TrueSide == FalseSide)
    return TrueSide;

  Value *Size = Builder.CreateSelect(I.getCondition(), TrueSide.first,
                                     FalseSide.first);
  Value *Offset = Builder.CreateSelect(I.getCondition(), TrueSide.second,
                                       FalseSide.second);
  return std::make_pair(Size, Offset);
}

SizeOffsetEvalType ObjectSizeOffsetEvaluator::visitInstruction(Instruction &I) {
  LLVM_DEBUG(dbgs() << "ObjectSizeOffsetEvaluator unknown instruction:" << I
                    << '\n');
  return unknown();
}
