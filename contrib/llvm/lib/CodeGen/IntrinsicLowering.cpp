//===-- IntrinsicLowering.cpp - Intrinsic Lowering default implementation -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the IntrinsicLowering class.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/IntrinsicLowering.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

template <class ArgIt>
static void EnsureFunctionExists(Module &M, const char *Name,
                                 ArgIt ArgBegin, ArgIt ArgEnd,
                                 Type *RetTy) {
  // Insert a correctly-typed definition now.
  std::vector<Type *> ParamTys;
  for (ArgIt I = ArgBegin; I != ArgEnd; ++I)
    ParamTys.push_back(I->getType());
  M.getOrInsertFunction(Name, FunctionType::get(RetTy, ParamTys, false));
}

static void EnsureFPIntrinsicsExist(Module &M, Function &Fn,
                                    const char *FName,
                                    const char *DName, const char *LDName) {
  // Insert definitions for all the floating point types.
  switch((int)Fn.arg_begin()->getType()->getTypeID()) {
  case Type::FloatTyID:
    EnsureFunctionExists(M, FName, Fn.arg_begin(), Fn.arg_end(),
                         Type::getFloatTy(M.getContext()));
    break;
  case Type::DoubleTyID:
    EnsureFunctionExists(M, DName, Fn.arg_begin(), Fn.arg_end(),
                         Type::getDoubleTy(M.getContext()));
    break;
  case Type::X86_FP80TyID:
  case Type::FP128TyID:
  case Type::PPC_FP128TyID:
    EnsureFunctionExists(M, LDName, Fn.arg_begin(), Fn.arg_end(),
                         Fn.arg_begin()->getType());
    break;
  }
}

/// This function is used when we want to lower an intrinsic call to a call of
/// an external function. This handles hard cases such as when there was already
/// a prototype for the external function, but that prototype doesn't match the
/// arguments we expect to pass in.
template <class ArgIt>
static CallInst *ReplaceCallWith(const char *NewFn, CallInst *CI,
                                 ArgIt ArgBegin, ArgIt ArgEnd,
                                 Type *RetTy) {
  // If we haven't already looked up this function, check to see if the
  // program already contains a function with this name.
  Module *M = CI->getModule();
  // Get or insert the definition now.
  std::vector<Type *> ParamTys;
  for (ArgIt I = ArgBegin; I != ArgEnd; ++I)
    ParamTys.push_back((*I)->getType());
  Constant* FCache = M->getOrInsertFunction(NewFn,
                                  FunctionType::get(RetTy, ParamTys, false));

  IRBuilder<> Builder(CI->getParent(), CI->getIterator());
  SmallVector<Value *, 8> Args(ArgBegin, ArgEnd);
  CallInst *NewCI = Builder.CreateCall(FCache, Args);
  NewCI->setName(CI->getName());
  if (!CI->use_empty())
    CI->replaceAllUsesWith(NewCI);
  return NewCI;
}

// VisualStudio defines setjmp as _setjmp
#if defined(_MSC_VER) && defined(setjmp) && \
                         !defined(setjmp_undefined_for_msvc)
#  pragma push_macro("setjmp")
#  undef setjmp
#  define setjmp_undefined_for_msvc
#endif

void IntrinsicLowering::AddPrototypes(Module &M) {
  LLVMContext &Context = M.getContext();
  for (auto &F : M)
    if (F.isDeclaration() && !F.use_empty())
      switch (F.getIntrinsicID()) {
      default: break;
      case Intrinsic::setjmp:
        EnsureFunctionExists(M, "setjmp", F.arg_begin(), F.arg_end(),
                             Type::getInt32Ty(M.getContext()));
        break;
      case Intrinsic::longjmp:
        EnsureFunctionExists(M, "longjmp", F.arg_begin(), F.arg_end(),
                             Type::getVoidTy(M.getContext()));
        break;
      case Intrinsic::siglongjmp:
        EnsureFunctionExists(M, "abort", F.arg_end(), F.arg_end(),
                             Type::getVoidTy(M.getContext()));
        break;
      case Intrinsic::memcpy:
        M.getOrInsertFunction("memcpy",
          Type::getInt8PtrTy(Context),
                              Type::getInt8PtrTy(Context),
                              Type::getInt8PtrTy(Context),
                              DL.getIntPtrType(Context));
        break;
      case Intrinsic::memmove:
        M.getOrInsertFunction("memmove",
          Type::getInt8PtrTy(Context),
                              Type::getInt8PtrTy(Context),
                              Type::getInt8PtrTy(Context),
                              DL.getIntPtrType(Context));
        break;
      case Intrinsic::memset:
        M.getOrInsertFunction("memset",
          Type::getInt8PtrTy(Context),
                              Type::getInt8PtrTy(Context),
                              Type::getInt32Ty(M.getContext()),
                              DL.getIntPtrType(Context));
        break;
      case Intrinsic::sqrt:
        EnsureFPIntrinsicsExist(M, F, "sqrtf", "sqrt", "sqrtl");
        break;
      case Intrinsic::sin:
        EnsureFPIntrinsicsExist(M, F, "sinf", "sin", "sinl");
        break;
      case Intrinsic::cos:
        EnsureFPIntrinsicsExist(M, F, "cosf", "cos", "cosl");
        break;
      case Intrinsic::pow:
        EnsureFPIntrinsicsExist(M, F, "powf", "pow", "powl");
        break;
      case Intrinsic::log:
        EnsureFPIntrinsicsExist(M, F, "logf", "log", "logl");
        break;
      case Intrinsic::log2:
        EnsureFPIntrinsicsExist(M, F, "log2f", "log2", "log2l");
        break;
      case Intrinsic::log10:
        EnsureFPIntrinsicsExist(M, F, "log10f", "log10", "log10l");
        break;
      case Intrinsic::exp:
        EnsureFPIntrinsicsExist(M, F, "expf", "exp", "expl");
        break;
      case Intrinsic::exp2:
        EnsureFPIntrinsicsExist(M, F, "exp2f", "exp2", "exp2l");
        break;
      }
}

/// Emit the code to lower bswap of V before the specified instruction IP.
static Value *LowerBSWAP(LLVMContext &Context, Value *V, Instruction *IP) {
  assert(V->getType()->isIntOrIntVectorTy() && "Can't bswap a non-integer type!");

  unsigned BitSize = V->getType()->getScalarSizeInBits();

  IRBuilder<> Builder(IP);

  switch(BitSize) {
  default: llvm_unreachable("Unhandled type size of value to byteswap!");
  case 16: {
    Value *Tmp1 = Builder.CreateShl(V, ConstantInt::get(V->getType(), 8),
                                    "bswap.2");
    Value *Tmp2 = Builder.CreateLShr(V, ConstantInt::get(V->getType(), 8),
                                     "bswap.1");
    V = Builder.CreateOr(Tmp1, Tmp2, "bswap.i16");
    break;
  }
  case 32: {
    Value *Tmp4 = Builder.CreateShl(V, ConstantInt::get(V->getType(), 24),
                                    "bswap.4");
    Value *Tmp3 = Builder.CreateShl(V, ConstantInt::get(V->getType(), 8),
                                    "bswap.3");
    Value *Tmp2 = Builder.CreateLShr(V, ConstantInt::get(V->getType(), 8),
                                     "bswap.2");
    Value *Tmp1 = Builder.CreateLShr(V,ConstantInt::get(V->getType(), 24),
                                     "bswap.1");
    Tmp3 = Builder.CreateAnd(Tmp3,
                         ConstantInt::get(V->getType(), 0xFF0000),
                             "bswap.and3");
    Tmp2 = Builder.CreateAnd(Tmp2,
                           ConstantInt::get(V->getType(), 0xFF00),
                             "bswap.and2");
    Tmp4 = Builder.CreateOr(Tmp4, Tmp3, "bswap.or1");
    Tmp2 = Builder.CreateOr(Tmp2, Tmp1, "bswap.or2");
    V = Builder.CreateOr(Tmp4, Tmp2, "bswap.i32");
    break;
  }
  case 64: {
    Value *Tmp8 = Builder.CreateShl(V, ConstantInt::get(V->getType(), 56),
                                    "bswap.8");
    Value *Tmp7 = Builder.CreateShl(V, ConstantInt::get(V->getType(), 40),
                                    "bswap.7");
    Value *Tmp6 = Builder.CreateShl(V, ConstantInt::get(V->getType(), 24),
                                    "bswap.6");
    Value *Tmp5 = Builder.CreateShl(V, ConstantInt::get(V->getType(), 8),
                                    "bswap.5");
    Value* Tmp4 = Builder.CreateLShr(V, ConstantInt::get(V->getType(), 8),
                                     "bswap.4");
    Value* Tmp3 = Builder.CreateLShr(V,
                                     ConstantInt::get(V->getType(), 24),
                                     "bswap.3");
    Value* Tmp2 = Builder.CreateLShr(V,
                                     ConstantInt::get(V->getType(), 40),
                                     "bswap.2");
    Value* Tmp1 = Builder.CreateLShr(V,
                                     ConstantInt::get(V->getType(), 56),
                                     "bswap.1");
    Tmp7 = Builder.CreateAnd(Tmp7,
                             ConstantInt::get(V->getType(),
                                              0xFF000000000000ULL),
                             "bswap.and7");
    Tmp6 = Builder.CreateAnd(Tmp6,
                             ConstantInt::get(V->getType(),
                                              0xFF0000000000ULL),
                             "bswap.and6");
    Tmp5 = Builder.CreateAnd(Tmp5,
                        ConstantInt::get(V->getType(),
                             0xFF00000000ULL),
                             "bswap.and5");
    Tmp4 = Builder.CreateAnd(Tmp4,
                        ConstantInt::get(V->getType(),
                             0xFF000000ULL),
                             "bswap.and4");
    Tmp3 = Builder.CreateAnd(Tmp3,
                             ConstantInt::get(V->getType(),
                             0xFF0000ULL),
                             "bswap.and3");
    Tmp2 = Builder.CreateAnd(Tmp2,
                             ConstantInt::get(V->getType(),
                             0xFF00ULL),
                             "bswap.and2");
    Tmp8 = Builder.CreateOr(Tmp8, Tmp7, "bswap.or1");
    Tmp6 = Builder.CreateOr(Tmp6, Tmp5, "bswap.or2");
    Tmp4 = Builder.CreateOr(Tmp4, Tmp3, "bswap.or3");
    Tmp2 = Builder.CreateOr(Tmp2, Tmp1, "bswap.or4");
    Tmp8 = Builder.CreateOr(Tmp8, Tmp6, "bswap.or5");
    Tmp4 = Builder.CreateOr(Tmp4, Tmp2, "bswap.or6");
    V = Builder.CreateOr(Tmp8, Tmp4, "bswap.i64");
    break;
  }
  }
  return V;
}

/// Emit the code to lower ctpop of V before the specified instruction IP.
static Value *LowerCTPOP(LLVMContext &Context, Value *V, Instruction *IP) {
  assert(V->getType()->isIntegerTy() && "Can't ctpop a non-integer type!");

  static const uint64_t MaskValues[6] = {
    0x5555555555555555ULL, 0x3333333333333333ULL,
    0x0F0F0F0F0F0F0F0FULL, 0x00FF00FF00FF00FFULL,
    0x0000FFFF0000FFFFULL, 0x00000000FFFFFFFFULL
  };

  IRBuilder<> Builder(IP);

  unsigned BitSize = V->getType()->getPrimitiveSizeInBits();
  unsigned WordSize = (BitSize + 63) / 64;
  Value *Count = ConstantInt::get(V->getType(), 0);

  for (unsigned n = 0; n < WordSize; ++n) {
    Value *PartValue = V;
    for (unsigned i = 1, ct = 0; i < (BitSize>64 ? 64 : BitSize);
         i <<= 1, ++ct) {
      Value *MaskCst = ConstantInt::get(V->getType(), MaskValues[ct]);
      Value *LHS = Builder.CreateAnd(PartValue, MaskCst, "cppop.and1");
      Value *VShift = Builder.CreateLShr(PartValue,
                                        ConstantInt::get(V->getType(), i),
                                         "ctpop.sh");
      Value *RHS = Builder.CreateAnd(VShift, MaskCst, "cppop.and2");
      PartValue = Builder.CreateAdd(LHS, RHS, "ctpop.step");
    }
    Count = Builder.CreateAdd(PartValue, Count, "ctpop.part");
    if (BitSize > 64) {
      V = Builder.CreateLShr(V, ConstantInt::get(V->getType(), 64),
                             "ctpop.part.sh");
      BitSize -= 64;
    }
  }

  return Count;
}

/// Emit the code to lower ctlz of V before the specified instruction IP.
static Value *LowerCTLZ(LLVMContext &Context, Value *V, Instruction *IP) {

  IRBuilder<> Builder(IP);

  unsigned BitSize = V->getType()->getPrimitiveSizeInBits();
  for (unsigned i = 1; i < BitSize; i <<= 1) {
    Value *ShVal = ConstantInt::get(V->getType(), i);
    ShVal = Builder.CreateLShr(V, ShVal, "ctlz.sh");
    V = Builder.CreateOr(V, ShVal, "ctlz.step");
  }

  V = Builder.CreateNot(V);
  return LowerCTPOP(Context, V, IP);
}

static void ReplaceFPIntrinsicWithCall(CallInst *CI, const char *Fname,
                                       const char *Dname,
                                       const char *LDname) {
  CallSite CS(CI);
  switch (CI->getArgOperand(0)->getType()->getTypeID()) {
  default: llvm_unreachable("Invalid type in intrinsic");
  case Type::FloatTyID:
    ReplaceCallWith(Fname, CI, CS.arg_begin(), CS.arg_end(),
                  Type::getFloatTy(CI->getContext()));
    break;
  case Type::DoubleTyID:
    ReplaceCallWith(Dname, CI, CS.arg_begin(), CS.arg_end(),
                  Type::getDoubleTy(CI->getContext()));
    break;
  case Type::X86_FP80TyID:
  case Type::FP128TyID:
  case Type::PPC_FP128TyID:
    ReplaceCallWith(LDname, CI, CS.arg_begin(), CS.arg_end(),
                  CI->getArgOperand(0)->getType());
    break;
  }
}

void IntrinsicLowering::LowerIntrinsicCall(CallInst *CI) {
  IRBuilder<> Builder(CI);
  LLVMContext &Context = CI->getContext();

  const Function *Callee = CI->getCalledFunction();
  assert(Callee && "Cannot lower an indirect call!");

  CallSite CS(CI);
  switch (Callee->getIntrinsicID()) {
  case Intrinsic::not_intrinsic:
    report_fatal_error("Cannot lower a call to a non-intrinsic function '"+
                      Callee->getName() + "'!");
  default:
    report_fatal_error("Code generator does not support intrinsic function '"+
                      Callee->getName()+"'!");

  case Intrinsic::expect: {
    // Just replace __builtin_expect(exp, c) with EXP.
    Value *V = CI->getArgOperand(0);
    CI->replaceAllUsesWith(V);
    break;
  }

    // The setjmp/longjmp intrinsics should only exist in the code if it was
    // never optimized (ie, right out of the CFE), or if it has been hacked on
    // by the lowerinvoke pass.  In both cases, the right thing to do is to
    // convert the call to an explicit setjmp or longjmp call.
  case Intrinsic::setjmp: {
    Value *V = ReplaceCallWith("setjmp", CI, CS.arg_begin(), CS.arg_end(),
                               Type::getInt32Ty(Context));
    if (!CI->getType()->isVoidTy())
      CI->replaceAllUsesWith(V);
    break;
  }
  case Intrinsic::sigsetjmp:
     if (!CI->getType()->isVoidTy())
       CI->replaceAllUsesWith(Constant::getNullValue(CI->getType()));
     break;

  case Intrinsic::longjmp: {
    ReplaceCallWith("longjmp", CI, CS.arg_begin(), CS.arg_end(),
                    Type::getVoidTy(Context));
    break;
  }

  case Intrinsic::siglongjmp: {
    // Insert the call to abort
    ReplaceCallWith("abort", CI, CS.arg_end(), CS.arg_end(),
                    Type::getVoidTy(Context));
    break;
  }
  case Intrinsic::ctpop:
    CI->replaceAllUsesWith(LowerCTPOP(Context, CI->getArgOperand(0), CI));
    break;

  case Intrinsic::bswap:
    CI->replaceAllUsesWith(LowerBSWAP(Context, CI->getArgOperand(0), CI));
    break;

  case Intrinsic::ctlz:
    CI->replaceAllUsesWith(LowerCTLZ(Context, CI->getArgOperand(0), CI));
    break;

  case Intrinsic::cttz: {
    // cttz(x) -> ctpop(~X & (X-1))
    Value *Src = CI->getArgOperand(0);
    Value *NotSrc = Builder.CreateNot(Src);
    NotSrc->setName(Src->getName() + ".not");
    Value *SrcM1 = ConstantInt::get(Src->getType(), 1);
    SrcM1 = Builder.CreateSub(Src, SrcM1);
    Src = LowerCTPOP(Context, Builder.CreateAnd(NotSrc, SrcM1), CI);
    CI->replaceAllUsesWith(Src);
    break;
  }

  case Intrinsic::stacksave:
  case Intrinsic::stackrestore: {
    if (!Warned)
      errs() << "WARNING: this target does not support the llvm.stack"
             << (Callee->getIntrinsicID() == Intrinsic::stacksave ?
               "save" : "restore") << " intrinsic.\n";
    Warned = true;
    if (Callee->getIntrinsicID() == Intrinsic::stacksave)
      CI->replaceAllUsesWith(Constant::getNullValue(CI->getType()));
    break;
  }

  case Intrinsic::get_dynamic_area_offset:
    errs() << "WARNING: this target does not support the custom llvm.get."
              "dynamic.area.offset.  It is being lowered to a constant 0\n";
    // Just lower it to a constant 0 because for most targets
    // @llvm.get.dynamic.area.offset is lowered to zero.
    CI->replaceAllUsesWith(ConstantInt::get(CI->getType(), 0));
    break;
  case Intrinsic::returnaddress:
  case Intrinsic::frameaddress:
    errs() << "WARNING: this target does not support the llvm."
           << (Callee->getIntrinsicID() == Intrinsic::returnaddress ?
             "return" : "frame") << "address intrinsic.\n";
    CI->replaceAllUsesWith(
        ConstantPointerNull::get(cast<PointerType>(CI->getType())));
    break;
  case Intrinsic::addressofreturnaddress:
    errs() << "WARNING: this target does not support the "
              "llvm.addressofreturnaddress intrinsic.\n";
    CI->replaceAllUsesWith(
        ConstantPointerNull::get(cast<PointerType>(CI->getType())));
    break;

  case Intrinsic::prefetch:
    break;    // Simply strip out prefetches on unsupported architectures

  case Intrinsic::pcmarker:
    break;    // Simply strip out pcmarker on unsupported architectures
  case Intrinsic::readcyclecounter: {
    errs() << "WARNING: this target does not support the llvm.readcyclecoun"
           << "ter intrinsic.  It is being lowered to a constant 0\n";
    CI->replaceAllUsesWith(ConstantInt::get(Type::getInt64Ty(Context), 0));
    break;
  }

  case Intrinsic::dbg_declare:
  case Intrinsic::dbg_label:
    break;    // Simply strip out debugging intrinsics

  case Intrinsic::eh_typeid_for:
    // Return something different to eh_selector.
    CI->replaceAllUsesWith(ConstantInt::get(CI->getType(), 1));
    break;

  case Intrinsic::annotation:
  case Intrinsic::ptr_annotation:
    // Just drop the annotation, but forward the value
    CI->replaceAllUsesWith(CI->getOperand(0));
    break;

  case Intrinsic::assume:
  case Intrinsic::var_annotation:
    break;   // Strip out these intrinsics

  case Intrinsic::memcpy: {
    Type *IntPtr = DL.getIntPtrType(Context);
    Value *Size = Builder.CreateIntCast(CI->getArgOperand(2), IntPtr,
                                        /* isSigned */ false);
    Value *Ops[3];
    Ops[0] = CI->getArgOperand(0);
    Ops[1] = CI->getArgOperand(1);
    Ops[2] = Size;
    ReplaceCallWith("memcpy", CI, Ops, Ops+3, CI->getArgOperand(0)->getType());
    break;
  }
  case Intrinsic::memmove: {
    Type *IntPtr = DL.getIntPtrType(Context);
    Value *Size = Builder.CreateIntCast(CI->getArgOperand(2), IntPtr,
                                        /* isSigned */ false);
    Value *Ops[3];
    Ops[0] = CI->getArgOperand(0);
    Ops[1] = CI->getArgOperand(1);
    Ops[2] = Size;
    ReplaceCallWith("memmove", CI, Ops, Ops+3, CI->getArgOperand(0)->getType());
    break;
  }
  case Intrinsic::memset: {
    Value *Op0 = CI->getArgOperand(0);
    Type *IntPtr = DL.getIntPtrType(Op0->getType());
    Value *Size = Builder.CreateIntCast(CI->getArgOperand(2), IntPtr,
                                        /* isSigned */ false);
    Value *Ops[3];
    Ops[0] = Op0;
    // Extend the amount to i32.
    Ops[1] = Builder.CreateIntCast(CI->getArgOperand(1),
                                   Type::getInt32Ty(Context),
                                   /* isSigned */ false);
    Ops[2] = Size;
    ReplaceCallWith("memset", CI, Ops, Ops+3, CI->getArgOperand(0)->getType());
    break;
  }
  case Intrinsic::sqrt: {
    ReplaceFPIntrinsicWithCall(CI, "sqrtf", "sqrt", "sqrtl");
    break;
  }
  case Intrinsic::log: {
    ReplaceFPIntrinsicWithCall(CI, "logf", "log", "logl");
    break;
  }
  case Intrinsic::log2: {
    ReplaceFPIntrinsicWithCall(CI, "log2f", "log2", "log2l");
    break;
  }
  case Intrinsic::log10: {
    ReplaceFPIntrinsicWithCall(CI, "log10f", "log10", "log10l");
    break;
  }
  case Intrinsic::exp: {
    ReplaceFPIntrinsicWithCall(CI, "expf", "exp", "expl");
    break;
  }
  case Intrinsic::exp2: {
    ReplaceFPIntrinsicWithCall(CI, "exp2f", "exp2", "exp2l");
    break;
  }
  case Intrinsic::pow: {
    ReplaceFPIntrinsicWithCall(CI, "powf", "pow", "powl");
    break;
  }
  case Intrinsic::sin: {
    ReplaceFPIntrinsicWithCall(CI, "sinf", "sin", "sinl");
    break;
  }
  case Intrinsic::cos: {
    ReplaceFPIntrinsicWithCall(CI, "cosf", "cos", "cosl");
    break;
  }
  case Intrinsic::floor: {
    ReplaceFPIntrinsicWithCall(CI, "floorf", "floor", "floorl");
    break;
  }
  case Intrinsic::ceil: {
    ReplaceFPIntrinsicWithCall(CI, "ceilf", "ceil", "ceill");
    break;
  }
  case Intrinsic::trunc: {
    ReplaceFPIntrinsicWithCall(CI, "truncf", "trunc", "truncl");
    break;
  }
  case Intrinsic::round: {
    ReplaceFPIntrinsicWithCall(CI, "roundf", "round", "roundl");
    break;
  }
  case Intrinsic::copysign: {
    ReplaceFPIntrinsicWithCall(CI, "copysignf", "copysign", "copysignl");
    break;
  }
  case Intrinsic::flt_rounds:
     // Lower to "round to the nearest"
     if (!CI->getType()->isVoidTy())
       CI->replaceAllUsesWith(ConstantInt::get(CI->getType(), 1));
     break;
  case Intrinsic::invariant_start:
  case Intrinsic::lifetime_start:
    // Discard region information.
    CI->replaceAllUsesWith(UndefValue::get(CI->getType()));
    break;
  case Intrinsic::invariant_end:
  case Intrinsic::lifetime_end:
    // Discard region information.
    break;
  }

  assert(CI->use_empty() &&
         "Lowering should have eliminated any uses of the intrinsic call!");
  CI->eraseFromParent();
}

bool IntrinsicLowering::LowerToByteSwap(CallInst *CI) {
  // Verify this is a simple bswap.
  if (CI->getNumArgOperands() != 1 ||
      CI->getType() != CI->getArgOperand(0)->getType() ||
      !CI->getType()->isIntegerTy())
    return false;

  IntegerType *Ty = dyn_cast<IntegerType>(CI->getType());
  if (!Ty)
    return false;

  // Okay, we can do this xform, do so now.
  Module *M = CI->getModule();
  Constant *Int = Intrinsic::getDeclaration(M, Intrinsic::bswap, Ty);

  Value *Op = CI->getArgOperand(0);
  Op = CallInst::Create(Int, Op, CI->getName(), CI);

  CI->replaceAllUsesWith(Op);
  CI->eraseFromParent();
  return true;
}
