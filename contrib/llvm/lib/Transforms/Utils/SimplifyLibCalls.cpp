//===------ SimplifyLibCalls.cpp - Library calls simplifier ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the library calls simplifier. It does not implement
// any pass, but can't be used by other passes to do simplifications.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/SimplifyLibCalls.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Transforms/Utils/BuildLibCalls.h"

using namespace llvm;
using namespace PatternMatch;

static cl::opt<bool>
    EnableUnsafeFPShrink("enable-double-float-shrink", cl::Hidden,
                         cl::init(false),
                         cl::desc("Enable unsafe double to float "
                                  "shrinking for math lib calls"));


//===----------------------------------------------------------------------===//
// Helper Functions
//===----------------------------------------------------------------------===//

static bool ignoreCallingConv(LibFunc Func) {
  return Func == LibFunc_abs || Func == LibFunc_labs ||
         Func == LibFunc_llabs || Func == LibFunc_strlen;
}

static bool isCallingConvCCompatible(CallInst *CI) {
  switch(CI->getCallingConv()) {
  default:
    return false;
  case llvm::CallingConv::C:
    return true;
  case llvm::CallingConv::ARM_APCS:
  case llvm::CallingConv::ARM_AAPCS:
  case llvm::CallingConv::ARM_AAPCS_VFP: {

    // The iOS ABI diverges from the standard in some cases, so for now don't
    // try to simplify those calls.
    if (Triple(CI->getModule()->getTargetTriple()).isiOS())
      return false;

    auto *FuncTy = CI->getFunctionType();

    if (!FuncTy->getReturnType()->isPointerTy() &&
        !FuncTy->getReturnType()->isIntegerTy() &&
        !FuncTy->getReturnType()->isVoidTy())
      return false;

    for (auto Param : FuncTy->params()) {
      if (!Param->isPointerTy() && !Param->isIntegerTy())
        return false;
    }
    return true;
  }
  }
  return false;
}

/// Return true if it is only used in equality comparisons with With.
static bool isOnlyUsedInEqualityComparison(Value *V, Value *With) {
  for (User *U : V->users()) {
    if (ICmpInst *IC = dyn_cast<ICmpInst>(U))
      if (IC->isEquality() && IC->getOperand(1) == With)
        continue;
    // Unknown instruction.
    return false;
  }
  return true;
}

static bool callHasFloatingPointArgument(const CallInst *CI) {
  return any_of(CI->operands(), [](const Use &OI) {
    return OI->getType()->isFloatingPointTy();
  });
}

static Value *convertStrToNumber(CallInst *CI, StringRef &Str, int64_t Base) {
  if (Base < 2 || Base > 36)
    // handle special zero base
    if (Base != 0)
      return nullptr;

  char *End;
  std::string nptr = Str.str();
  errno = 0;
  long long int Result = strtoll(nptr.c_str(), &End, Base);
  if (errno)
    return nullptr;

  // if we assume all possible target locales are ASCII supersets,
  // then if strtoll successfully parses a number on the host,
  // it will also successfully parse the same way on the target
  if (*End != '\0')
    return nullptr;

  if (!isIntN(CI->getType()->getPrimitiveSizeInBits(), Result))
    return nullptr;

  return ConstantInt::get(CI->getType(), Result);
}

static bool isLocallyOpenedFile(Value *File, CallInst *CI, IRBuilder<> &B,
                                const TargetLibraryInfo *TLI) {
  CallInst *FOpen = dyn_cast<CallInst>(File);
  if (!FOpen)
    return false;

  Function *InnerCallee = FOpen->getCalledFunction();
  if (!InnerCallee)
    return false;

  LibFunc Func;
  if (!TLI->getLibFunc(*InnerCallee, Func) || !TLI->has(Func) ||
      Func != LibFunc_fopen)
    return false;

  inferLibFuncAttributes(*CI->getCalledFunction(), *TLI);
  if (PointerMayBeCaptured(File, true, true))
    return false;

  return true;
}

static bool isOnlyUsedInComparisonWithZero(Value *V) {
  for (User *U : V->users()) {
    if (ICmpInst *IC = dyn_cast<ICmpInst>(U))
      if (Constant *C = dyn_cast<Constant>(IC->getOperand(1)))
        if (C->isNullValue())
          continue;
    // Unknown instruction.
    return false;
  }
  return true;
}

static bool canTransformToMemCmp(CallInst *CI, Value *Str, uint64_t Len,
                                 const DataLayout &DL) {
  if (!isOnlyUsedInComparisonWithZero(CI))
    return false;

  if (!isDereferenceableAndAlignedPointer(Str, 1, APInt(64, Len), DL))
    return false;

  if (CI->getFunction()->hasFnAttribute(Attribute::SanitizeMemory))
    return false;

  return true;
}

//===----------------------------------------------------------------------===//
// String and Memory Library Call Optimizations
//===----------------------------------------------------------------------===//

Value *LibCallSimplifier::optimizeStrCat(CallInst *CI, IRBuilder<> &B) {
  // Extract some information from the instruction
  Value *Dst = CI->getArgOperand(0);
  Value *Src = CI->getArgOperand(1);

  // See if we can get the length of the input string.
  uint64_t Len = GetStringLength(Src);
  if (Len == 0)
    return nullptr;
  --Len; // Unbias length.

  // Handle the simple, do-nothing case: strcat(x, "") -> x
  if (Len == 0)
    return Dst;

  return emitStrLenMemCpy(Src, Dst, Len, B);
}

Value *LibCallSimplifier::emitStrLenMemCpy(Value *Src, Value *Dst, uint64_t Len,
                                           IRBuilder<> &B) {
  // We need to find the end of the destination string.  That's where the
  // memory is to be moved to. We just generate a call to strlen.
  Value *DstLen = emitStrLen(Dst, B, DL, TLI);
  if (!DstLen)
    return nullptr;

  // Now that we have the destination's length, we must index into the
  // destination's pointer to get the actual memcpy destination (end of
  // the string .. we're concatenating).
  Value *CpyDst = B.CreateGEP(B.getInt8Ty(), Dst, DstLen, "endptr");

  // We have enough information to now generate the memcpy call to do the
  // concatenation for us.  Make a memcpy to copy the nul byte with align = 1.
  B.CreateMemCpy(CpyDst, 1, Src, 1,
                 ConstantInt::get(DL.getIntPtrType(Src->getContext()), Len + 1));
  return Dst;
}

Value *LibCallSimplifier::optimizeStrNCat(CallInst *CI, IRBuilder<> &B) {
  // Extract some information from the instruction.
  Value *Dst = CI->getArgOperand(0);
  Value *Src = CI->getArgOperand(1);
  uint64_t Len;

  // We don't do anything if length is not constant.
  if (ConstantInt *LengthArg = dyn_cast<ConstantInt>(CI->getArgOperand(2)))
    Len = LengthArg->getZExtValue();
  else
    return nullptr;

  // See if we can get the length of the input string.
  uint64_t SrcLen = GetStringLength(Src);
  if (SrcLen == 0)
    return nullptr;
  --SrcLen; // Unbias length.

  // Handle the simple, do-nothing cases:
  // strncat(x, "", c) -> x
  // strncat(x,  c, 0) -> x
  if (SrcLen == 0 || Len == 0)
    return Dst;

  // We don't optimize this case.
  if (Len < SrcLen)
    return nullptr;

  // strncat(x, s, c) -> strcat(x, s)
  // s is constant so the strcat can be optimized further.
  return emitStrLenMemCpy(Src, Dst, SrcLen, B);
}

Value *LibCallSimplifier::optimizeStrChr(CallInst *CI, IRBuilder<> &B) {
  Function *Callee = CI->getCalledFunction();
  FunctionType *FT = Callee->getFunctionType();
  Value *SrcStr = CI->getArgOperand(0);

  // If the second operand is non-constant, see if we can compute the length
  // of the input string and turn this into memchr.
  ConstantInt *CharC = dyn_cast<ConstantInt>(CI->getArgOperand(1));
  if (!CharC) {
    uint64_t Len = GetStringLength(SrcStr);
    if (Len == 0 || !FT->getParamType(1)->isIntegerTy(32)) // memchr needs i32.
      return nullptr;

    return emitMemChr(SrcStr, CI->getArgOperand(1), // include nul.
                      ConstantInt::get(DL.getIntPtrType(CI->getContext()), Len),
                      B, DL, TLI);
  }

  // Otherwise, the character is a constant, see if the first argument is
  // a string literal.  If so, we can constant fold.
  StringRef Str;
  if (!getConstantStringInfo(SrcStr, Str)) {
    if (CharC->isZero()) // strchr(p, 0) -> p + strlen(p)
      return B.CreateGEP(B.getInt8Ty(), SrcStr, emitStrLen(SrcStr, B, DL, TLI),
                         "strchr");
    return nullptr;
  }

  // Compute the offset, make sure to handle the case when we're searching for
  // zero (a weird way to spell strlen).
  size_t I = (0xFF & CharC->getSExtValue()) == 0
                 ? Str.size()
                 : Str.find(CharC->getSExtValue());
  if (I == StringRef::npos) // Didn't find the char.  strchr returns null.
    return Constant::getNullValue(CI->getType());

  // strchr(s+n,c)  -> gep(s+n+i,c)
  return B.CreateGEP(B.getInt8Ty(), SrcStr, B.getInt64(I), "strchr");
}

Value *LibCallSimplifier::optimizeStrRChr(CallInst *CI, IRBuilder<> &B) {
  Value *SrcStr = CI->getArgOperand(0);
  ConstantInt *CharC = dyn_cast<ConstantInt>(CI->getArgOperand(1));

  // Cannot fold anything if we're not looking for a constant.
  if (!CharC)
    return nullptr;

  StringRef Str;
  if (!getConstantStringInfo(SrcStr, Str)) {
    // strrchr(s, 0) -> strchr(s, 0)
    if (CharC->isZero())
      return emitStrChr(SrcStr, '\0', B, TLI);
    return nullptr;
  }

  // Compute the offset.
  size_t I = (0xFF & CharC->getSExtValue()) == 0
                 ? Str.size()
                 : Str.rfind(CharC->getSExtValue());
  if (I == StringRef::npos) // Didn't find the char. Return null.
    return Constant::getNullValue(CI->getType());

  // strrchr(s+n,c) -> gep(s+n+i,c)
  return B.CreateGEP(B.getInt8Ty(), SrcStr, B.getInt64(I), "strrchr");
}

Value *LibCallSimplifier::optimizeStrCmp(CallInst *CI, IRBuilder<> &B) {
  Value *Str1P = CI->getArgOperand(0), *Str2P = CI->getArgOperand(1);
  if (Str1P == Str2P) // strcmp(x,x)  -> 0
    return ConstantInt::get(CI->getType(), 0);

  StringRef Str1, Str2;
  bool HasStr1 = getConstantStringInfo(Str1P, Str1);
  bool HasStr2 = getConstantStringInfo(Str2P, Str2);

  // strcmp(x, y)  -> cnst  (if both x and y are constant strings)
  if (HasStr1 && HasStr2)
    return ConstantInt::get(CI->getType(), Str1.compare(Str2));

  if (HasStr1 && Str1.empty()) // strcmp("", x) -> -*x
    return B.CreateNeg(
        B.CreateZExt(B.CreateLoad(Str2P, "strcmpload"), CI->getType()));

  if (HasStr2 && Str2.empty()) // strcmp(x,"") -> *x
    return B.CreateZExt(B.CreateLoad(Str1P, "strcmpload"), CI->getType());

  // strcmp(P, "x") -> memcmp(P, "x", 2)
  uint64_t Len1 = GetStringLength(Str1P);
  uint64_t Len2 = GetStringLength(Str2P);
  if (Len1 && Len2) {
    return emitMemCmp(Str1P, Str2P,
                      ConstantInt::get(DL.getIntPtrType(CI->getContext()),
                                       std::min(Len1, Len2)),
                      B, DL, TLI);
  }

  // strcmp to memcmp
  if (!HasStr1 && HasStr2) {
    if (canTransformToMemCmp(CI, Str1P, Len2, DL))
      return emitMemCmp(
          Str1P, Str2P,
          ConstantInt::get(DL.getIntPtrType(CI->getContext()), Len2), B, DL,
          TLI);
  } else if (HasStr1 && !HasStr2) {
    if (canTransformToMemCmp(CI, Str2P, Len1, DL))
      return emitMemCmp(
          Str1P, Str2P,
          ConstantInt::get(DL.getIntPtrType(CI->getContext()), Len1), B, DL,
          TLI);
  }

  return nullptr;
}

Value *LibCallSimplifier::optimizeStrNCmp(CallInst *CI, IRBuilder<> &B) {
  Value *Str1P = CI->getArgOperand(0), *Str2P = CI->getArgOperand(1);
  if (Str1P == Str2P) // strncmp(x,x,n)  -> 0
    return ConstantInt::get(CI->getType(), 0);

  // Get the length argument if it is constant.
  uint64_t Length;
  if (ConstantInt *LengthArg = dyn_cast<ConstantInt>(CI->getArgOperand(2)))
    Length = LengthArg->getZExtValue();
  else
    return nullptr;

  if (Length == 0) // strncmp(x,y,0)   -> 0
    return ConstantInt::get(CI->getType(), 0);

  if (Length == 1) // strncmp(x,y,1) -> memcmp(x,y,1)
    return emitMemCmp(Str1P, Str2P, CI->getArgOperand(2), B, DL, TLI);

  StringRef Str1, Str2;
  bool HasStr1 = getConstantStringInfo(Str1P, Str1);
  bool HasStr2 = getConstantStringInfo(Str2P, Str2);

  // strncmp(x, y)  -> cnst  (if both x and y are constant strings)
  if (HasStr1 && HasStr2) {
    StringRef SubStr1 = Str1.substr(0, Length);
    StringRef SubStr2 = Str2.substr(0, Length);
    return ConstantInt::get(CI->getType(), SubStr1.compare(SubStr2));
  }

  if (HasStr1 && Str1.empty()) // strncmp("", x, n) -> -*x
    return B.CreateNeg(
        B.CreateZExt(B.CreateLoad(Str2P, "strcmpload"), CI->getType()));

  if (HasStr2 && Str2.empty()) // strncmp(x, "", n) -> *x
    return B.CreateZExt(B.CreateLoad(Str1P, "strcmpload"), CI->getType());

  uint64_t Len1 = GetStringLength(Str1P);
  uint64_t Len2 = GetStringLength(Str2P);

  // strncmp to memcmp
  if (!HasStr1 && HasStr2) {
    Len2 = std::min(Len2, Length);
    if (canTransformToMemCmp(CI, Str1P, Len2, DL))
      return emitMemCmp(
          Str1P, Str2P,
          ConstantInt::get(DL.getIntPtrType(CI->getContext()), Len2), B, DL,
          TLI);
  } else if (HasStr1 && !HasStr2) {
    Len1 = std::min(Len1, Length);
    if (canTransformToMemCmp(CI, Str2P, Len1, DL))
      return emitMemCmp(
          Str1P, Str2P,
          ConstantInt::get(DL.getIntPtrType(CI->getContext()), Len1), B, DL,
          TLI);
  }

  return nullptr;
}

Value *LibCallSimplifier::optimizeStrCpy(CallInst *CI, IRBuilder<> &B) {
  Value *Dst = CI->getArgOperand(0), *Src = CI->getArgOperand(1);
  if (Dst == Src) // strcpy(x,x)  -> x
    return Src;

  // See if we can get the length of the input string.
  uint64_t Len = GetStringLength(Src);
  if (Len == 0)
    return nullptr;

  // We have enough information to now generate the memcpy call to do the
  // copy for us.  Make a memcpy to copy the nul byte with align = 1.
  B.CreateMemCpy(Dst, 1, Src, 1,
                 ConstantInt::get(DL.getIntPtrType(CI->getContext()), Len));
  return Dst;
}

Value *LibCallSimplifier::optimizeStpCpy(CallInst *CI, IRBuilder<> &B) {
  Function *Callee = CI->getCalledFunction();
  Value *Dst = CI->getArgOperand(0), *Src = CI->getArgOperand(1);
  if (Dst == Src) { // stpcpy(x,x)  -> x+strlen(x)
    Value *StrLen = emitStrLen(Src, B, DL, TLI);
    return StrLen ? B.CreateInBoundsGEP(B.getInt8Ty(), Dst, StrLen) : nullptr;
  }

  // See if we can get the length of the input string.
  uint64_t Len = GetStringLength(Src);
  if (Len == 0)
    return nullptr;

  Type *PT = Callee->getFunctionType()->getParamType(0);
  Value *LenV = ConstantInt::get(DL.getIntPtrType(PT), Len);
  Value *DstEnd = B.CreateGEP(B.getInt8Ty(), Dst,
                              ConstantInt::get(DL.getIntPtrType(PT), Len - 1));

  // We have enough information to now generate the memcpy call to do the
  // copy for us.  Make a memcpy to copy the nul byte with align = 1.
  B.CreateMemCpy(Dst, 1, Src, 1, LenV);
  return DstEnd;
}

Value *LibCallSimplifier::optimizeStrNCpy(CallInst *CI, IRBuilder<> &B) {
  Function *Callee = CI->getCalledFunction();
  Value *Dst = CI->getArgOperand(0);
  Value *Src = CI->getArgOperand(1);
  Value *LenOp = CI->getArgOperand(2);

  // See if we can get the length of the input string.
  uint64_t SrcLen = GetStringLength(Src);
  if (SrcLen == 0)
    return nullptr;
  --SrcLen;

  if (SrcLen == 0) {
    // strncpy(x, "", y) -> memset(align 1 x, '\0', y)
    B.CreateMemSet(Dst, B.getInt8('\0'), LenOp, 1);
    return Dst;
  }

  uint64_t Len;
  if (ConstantInt *LengthArg = dyn_cast<ConstantInt>(LenOp))
    Len = LengthArg->getZExtValue();
  else
    return nullptr;

  if (Len == 0)
    return Dst; // strncpy(x, y, 0) -> x

  // Let strncpy handle the zero padding
  if (Len > SrcLen + 1)
    return nullptr;

  Type *PT = Callee->getFunctionType()->getParamType(0);
  // strncpy(x, s, c) -> memcpy(align 1 x, align 1 s, c) [s and c are constant]
  B.CreateMemCpy(Dst, 1, Src, 1, ConstantInt::get(DL.getIntPtrType(PT), Len));

  return Dst;
}

Value *LibCallSimplifier::optimizeStringLength(CallInst *CI, IRBuilder<> &B,
                                               unsigned CharSize) {
  Value *Src = CI->getArgOperand(0);

  // Constant folding: strlen("xyz") -> 3
  if (uint64_t Len = GetStringLength(Src, CharSize))
    return ConstantInt::get(CI->getType(), Len - 1);

  // If s is a constant pointer pointing to a string literal, we can fold
  // strlen(s + x) to strlen(s) - x, when x is known to be in the range
  // [0, strlen(s)] or the string has a single null terminator '\0' at the end.
  // We only try to simplify strlen when the pointer s points to an array
  // of i8. Otherwise, we would need to scale the offset x before doing the
  // subtraction. This will make the optimization more complex, and it's not
  // very useful because calling strlen for a pointer of other types is
  // very uncommon.
  if (GEPOperator *GEP = dyn_cast<GEPOperator>(Src)) {
    if (!isGEPBasedOnPointerToString(GEP, CharSize))
      return nullptr;

    ConstantDataArraySlice Slice;
    if (getConstantDataArrayInfo(GEP->getOperand(0), Slice, CharSize)) {
      uint64_t NullTermIdx;
      if (Slice.Array == nullptr) {
        NullTermIdx = 0;
      } else {
        NullTermIdx = ~((uint64_t)0);
        for (uint64_t I = 0, E = Slice.Length; I < E; ++I) {
          if (Slice.Array->getElementAsInteger(I + Slice.Offset) == 0) {
            NullTermIdx = I;
            break;
          }
        }
        // If the string does not have '\0', leave it to strlen to compute
        // its length.
        if (NullTermIdx == ~((uint64_t)0))
          return nullptr;
      }

      Value *Offset = GEP->getOperand(2);
      KnownBits Known = computeKnownBits(Offset, DL, 0, nullptr, CI, nullptr);
      Known.Zero.flipAllBits();
      uint64_t ArrSize =
             cast<ArrayType>(GEP->getSourceElementType())->getNumElements();

      // KnownZero's bits are flipped, so zeros in KnownZero now represent
      // bits known to be zeros in Offset, and ones in KnowZero represent
      // bits unknown in Offset. Therefore, Offset is known to be in range
      // [0, NullTermIdx] when the flipped KnownZero is non-negative and
      // unsigned-less-than NullTermIdx.
      //
      // If Offset is not provably in the range [0, NullTermIdx], we can still
      // optimize if we can prove that the program has undefined behavior when
      // Offset is outside that range. That is the case when GEP->getOperand(0)
      // is a pointer to an object whose memory extent is NullTermIdx+1.
      if ((Known.Zero.isNonNegative() && Known.Zero.ule(NullTermIdx)) ||
          (GEP->isInBounds() && isa<GlobalVariable>(GEP->getOperand(0)) &&
           NullTermIdx == ArrSize - 1)) {
        Offset = B.CreateSExtOrTrunc(Offset, CI->getType());
        return B.CreateSub(ConstantInt::get(CI->getType(), NullTermIdx),
                           Offset);
      }
    }

    return nullptr;
  }

  // strlen(x?"foo":"bars") --> x ? 3 : 4
  if (SelectInst *SI = dyn_cast<SelectInst>(Src)) {
    uint64_t LenTrue = GetStringLength(SI->getTrueValue(), CharSize);
    uint64_t LenFalse = GetStringLength(SI->getFalseValue(), CharSize);
    if (LenTrue && LenFalse) {
      ORE.emit([&]() {
        return OptimizationRemark("instcombine", "simplify-libcalls", CI)
               << "folded strlen(select) to select of constants";
      });
      return B.CreateSelect(SI->getCondition(),
                            ConstantInt::get(CI->getType(), LenTrue - 1),
                            ConstantInt::get(CI->getType(), LenFalse - 1));
    }
  }

  // strlen(x) != 0 --> *x != 0
  // strlen(x) == 0 --> *x == 0
  if (isOnlyUsedInZeroEqualityComparison(CI))
    return B.CreateZExt(B.CreateLoad(Src, "strlenfirst"), CI->getType());

  return nullptr;
}

Value *LibCallSimplifier::optimizeStrLen(CallInst *CI, IRBuilder<> &B) {
  return optimizeStringLength(CI, B, 8);
}

Value *LibCallSimplifier::optimizeWcslen(CallInst *CI, IRBuilder<> &B) {
  Module &M = *CI->getModule();
  unsigned WCharSize = TLI->getWCharSize(M) * 8;
  // We cannot perform this optimization without wchar_size metadata.
  if (WCharSize == 0)
    return nullptr;

  return optimizeStringLength(CI, B, WCharSize);
}

Value *LibCallSimplifier::optimizeStrPBrk(CallInst *CI, IRBuilder<> &B) {
  StringRef S1, S2;
  bool HasS1 = getConstantStringInfo(CI->getArgOperand(0), S1);
  bool HasS2 = getConstantStringInfo(CI->getArgOperand(1), S2);

  // strpbrk(s, "") -> nullptr
  // strpbrk("", s) -> nullptr
  if ((HasS1 && S1.empty()) || (HasS2 && S2.empty()))
    return Constant::getNullValue(CI->getType());

  // Constant folding.
  if (HasS1 && HasS2) {
    size_t I = S1.find_first_of(S2);
    if (I == StringRef::npos) // No match.
      return Constant::getNullValue(CI->getType());

    return B.CreateGEP(B.getInt8Ty(), CI->getArgOperand(0), B.getInt64(I),
                       "strpbrk");
  }

  // strpbrk(s, "a") -> strchr(s, 'a')
  if (HasS2 && S2.size() == 1)
    return emitStrChr(CI->getArgOperand(0), S2[0], B, TLI);

  return nullptr;
}

Value *LibCallSimplifier::optimizeStrTo(CallInst *CI, IRBuilder<> &B) {
  Value *EndPtr = CI->getArgOperand(1);
  if (isa<ConstantPointerNull>(EndPtr)) {
    // With a null EndPtr, this function won't capture the main argument.
    // It would be readonly too, except that it still may write to errno.
    CI->addParamAttr(0, Attribute::NoCapture);
  }

  return nullptr;
}

Value *LibCallSimplifier::optimizeStrSpn(CallInst *CI, IRBuilder<> &B) {
  StringRef S1, S2;
  bool HasS1 = getConstantStringInfo(CI->getArgOperand(0), S1);
  bool HasS2 = getConstantStringInfo(CI->getArgOperand(1), S2);

  // strspn(s, "") -> 0
  // strspn("", s) -> 0
  if ((HasS1 && S1.empty()) || (HasS2 && S2.empty()))
    return Constant::getNullValue(CI->getType());

  // Constant folding.
  if (HasS1 && HasS2) {
    size_t Pos = S1.find_first_not_of(S2);
    if (Pos == StringRef::npos)
      Pos = S1.size();
    return ConstantInt::get(CI->getType(), Pos);
  }

  return nullptr;
}

Value *LibCallSimplifier::optimizeStrCSpn(CallInst *CI, IRBuilder<> &B) {
  StringRef S1, S2;
  bool HasS1 = getConstantStringInfo(CI->getArgOperand(0), S1);
  bool HasS2 = getConstantStringInfo(CI->getArgOperand(1), S2);

  // strcspn("", s) -> 0
  if (HasS1 && S1.empty())
    return Constant::getNullValue(CI->getType());

  // Constant folding.
  if (HasS1 && HasS2) {
    size_t Pos = S1.find_first_of(S2);
    if (Pos == StringRef::npos)
      Pos = S1.size();
    return ConstantInt::get(CI->getType(), Pos);
  }

  // strcspn(s, "") -> strlen(s)
  if (HasS2 && S2.empty())
    return emitStrLen(CI->getArgOperand(0), B, DL, TLI);

  return nullptr;
}

Value *LibCallSimplifier::optimizeStrStr(CallInst *CI, IRBuilder<> &B) {
  // fold strstr(x, x) -> x.
  if (CI->getArgOperand(0) == CI->getArgOperand(1))
    return B.CreateBitCast(CI->getArgOperand(0), CI->getType());

  // fold strstr(a, b) == a -> strncmp(a, b, strlen(b)) == 0
  if (isOnlyUsedInEqualityComparison(CI, CI->getArgOperand(0))) {
    Value *StrLen = emitStrLen(CI->getArgOperand(1), B, DL, TLI);
    if (!StrLen)
      return nullptr;
    Value *StrNCmp = emitStrNCmp(CI->getArgOperand(0), CI->getArgOperand(1),
                                 StrLen, B, DL, TLI);
    if (!StrNCmp)
      return nullptr;
    for (auto UI = CI->user_begin(), UE = CI->user_end(); UI != UE;) {
      ICmpInst *Old = cast<ICmpInst>(*UI++);
      Value *Cmp =
          B.CreateICmp(Old->getPredicate(), StrNCmp,
                       ConstantInt::getNullValue(StrNCmp->getType()), "cmp");
      replaceAllUsesWith(Old, Cmp);
    }
    return CI;
  }

  // See if either input string is a constant string.
  StringRef SearchStr, ToFindStr;
  bool HasStr1 = getConstantStringInfo(CI->getArgOperand(0), SearchStr);
  bool HasStr2 = getConstantStringInfo(CI->getArgOperand(1), ToFindStr);

  // fold strstr(x, "") -> x.
  if (HasStr2 && ToFindStr.empty())
    return B.CreateBitCast(CI->getArgOperand(0), CI->getType());

  // If both strings are known, constant fold it.
  if (HasStr1 && HasStr2) {
    size_t Offset = SearchStr.find(ToFindStr);

    if (Offset == StringRef::npos) // strstr("foo", "bar") -> null
      return Constant::getNullValue(CI->getType());

    // strstr("abcd", "bc") -> gep((char*)"abcd", 1)
    Value *Result = castToCStr(CI->getArgOperand(0), B);
    Result = B.CreateConstInBoundsGEP1_64(Result, Offset, "strstr");
    return B.CreateBitCast(Result, CI->getType());
  }

  // fold strstr(x, "y") -> strchr(x, 'y').
  if (HasStr2 && ToFindStr.size() == 1) {
    Value *StrChr = emitStrChr(CI->getArgOperand(0), ToFindStr[0], B, TLI);
    return StrChr ? B.CreateBitCast(StrChr, CI->getType()) : nullptr;
  }
  return nullptr;
}

Value *LibCallSimplifier::optimizeMemChr(CallInst *CI, IRBuilder<> &B) {
  Value *SrcStr = CI->getArgOperand(0);
  ConstantInt *CharC = dyn_cast<ConstantInt>(CI->getArgOperand(1));
  ConstantInt *LenC = dyn_cast<ConstantInt>(CI->getArgOperand(2));

  // memchr(x, y, 0) -> null
  if (LenC && LenC->isZero())
    return Constant::getNullValue(CI->getType());

  // From now on we need at least constant length and string.
  StringRef Str;
  if (!LenC || !getConstantStringInfo(SrcStr, Str, 0, /*TrimAtNul=*/false))
    return nullptr;

  // Truncate the string to LenC. If Str is smaller than LenC we will still only
  // scan the string, as reading past the end of it is undefined and we can just
  // return null if we don't find the char.
  Str = Str.substr(0, LenC->getZExtValue());

  // If the char is variable but the input str and length are not we can turn
  // this memchr call into a simple bit field test. Of course this only works
  // when the return value is only checked against null.
  //
  // It would be really nice to reuse switch lowering here but we can't change
  // the CFG at this point.
  //
  // memchr("\r\n", C, 2) != nullptr -> (C & ((1 << '\r') | (1 << '\n'))) != 0
  //   after bounds check.
  if (!CharC && !Str.empty() && isOnlyUsedInZeroEqualityComparison(CI)) {
    unsigned char Max =
        *std::max_element(reinterpret_cast<const unsigned char *>(Str.begin()),
                          reinterpret_cast<const unsigned char *>(Str.end()));

    // Make sure the bit field we're about to create fits in a register on the
    // target.
    // FIXME: On a 64 bit architecture this prevents us from using the
    // interesting range of alpha ascii chars. We could do better by emitting
    // two bitfields or shifting the range by 64 if no lower chars are used.
    if (!DL.fitsInLegalInteger(Max + 1))
      return nullptr;

    // For the bit field use a power-of-2 type with at least 8 bits to avoid
    // creating unnecessary illegal types.
    unsigned char Width = NextPowerOf2(std::max((unsigned char)7, Max));

    // Now build the bit field.
    APInt Bitfield(Width, 0);
    for (char C : Str)
      Bitfield.setBit((unsigned char)C);
    Value *BitfieldC = B.getInt(Bitfield);

    // Adjust width of "C" to the bitfield width, then mask off the high bits.
    Value *C = B.CreateZExtOrTrunc(CI->getArgOperand(1), BitfieldC->getType());
    C = B.CreateAnd(C, B.getIntN(Width, 0xFF));

    // First check that the bit field access is within bounds.
    Value *Bounds = B.CreateICmp(ICmpInst::ICMP_ULT, C, B.getIntN(Width, Width),
                                 "memchr.bounds");

    // Create code that checks if the given bit is set in the field.
    Value *Shl = B.CreateShl(B.getIntN(Width, 1ULL), C);
    Value *Bits = B.CreateIsNotNull(B.CreateAnd(Shl, BitfieldC), "memchr.bits");

    // Finally merge both checks and cast to pointer type. The inttoptr
    // implicitly zexts the i1 to intptr type.
    return B.CreateIntToPtr(B.CreateAnd(Bounds, Bits, "memchr"), CI->getType());
  }

  // Check if all arguments are constants.  If so, we can constant fold.
  if (!CharC)
    return nullptr;

  // Compute the offset.
  size_t I = Str.find(CharC->getSExtValue() & 0xFF);
  if (I == StringRef::npos) // Didn't find the char.  memchr returns null.
    return Constant::getNullValue(CI->getType());

  // memchr(s+n,c,l) -> gep(s+n+i,c)
  return B.CreateGEP(B.getInt8Ty(), SrcStr, B.getInt64(I), "memchr");
}

Value *LibCallSimplifier::optimizeMemCmp(CallInst *CI, IRBuilder<> &B) {
  Value *LHS = CI->getArgOperand(0), *RHS = CI->getArgOperand(1);

  if (LHS == RHS) // memcmp(s,s,x) -> 0
    return Constant::getNullValue(CI->getType());

  // Make sure we have a constant length.
  ConstantInt *LenC = dyn_cast<ConstantInt>(CI->getArgOperand(2));
  if (!LenC)
    return nullptr;

  uint64_t Len = LenC->getZExtValue();
  if (Len == 0) // memcmp(s1,s2,0) -> 0
    return Constant::getNullValue(CI->getType());

  // memcmp(S1,S2,1) -> *(unsigned char*)LHS - *(unsigned char*)RHS
  if (Len == 1) {
    Value *LHSV = B.CreateZExt(B.CreateLoad(castToCStr(LHS, B), "lhsc"),
                               CI->getType(), "lhsv");
    Value *RHSV = B.CreateZExt(B.CreateLoad(castToCStr(RHS, B), "rhsc"),
                               CI->getType(), "rhsv");
    return B.CreateSub(LHSV, RHSV, "chardiff");
  }

  // memcmp(S1,S2,N/8)==0 -> (*(intN_t*)S1 != *(intN_t*)S2)==0
  // TODO: The case where both inputs are constants does not need to be limited
  // to legal integers or equality comparison. See block below this.
  if (DL.isLegalInteger(Len * 8) && isOnlyUsedInZeroEqualityComparison(CI)) {
    IntegerType *IntType = IntegerType::get(CI->getContext(), Len * 8);
    unsigned PrefAlignment = DL.getPrefTypeAlignment(IntType);

    // First, see if we can fold either argument to a constant.
    Value *LHSV = nullptr;
    if (auto *LHSC = dyn_cast<Constant>(LHS)) {
      LHSC = ConstantExpr::getBitCast(LHSC, IntType->getPointerTo());
      LHSV = ConstantFoldLoadFromConstPtr(LHSC, IntType, DL);
    }
    Value *RHSV = nullptr;
    if (auto *RHSC = dyn_cast<Constant>(RHS)) {
      RHSC = ConstantExpr::getBitCast(RHSC, IntType->getPointerTo());
      RHSV = ConstantFoldLoadFromConstPtr(RHSC, IntType, DL);
    }

    // Don't generate unaligned loads. If either source is constant data,
    // alignment doesn't matter for that source because there is no load.
    if ((LHSV || getKnownAlignment(LHS, DL, CI) >= PrefAlignment) &&
        (RHSV || getKnownAlignment(RHS, DL, CI) >= PrefAlignment)) {
      if (!LHSV) {
        Type *LHSPtrTy =
            IntType->getPointerTo(LHS->getType()->getPointerAddressSpace());
        LHSV = B.CreateLoad(B.CreateBitCast(LHS, LHSPtrTy), "lhsv");
      }
      if (!RHSV) {
        Type *RHSPtrTy =
            IntType->getPointerTo(RHS->getType()->getPointerAddressSpace());
        RHSV = B.CreateLoad(B.CreateBitCast(RHS, RHSPtrTy), "rhsv");
      }
      return B.CreateZExt(B.CreateICmpNE(LHSV, RHSV), CI->getType(), "memcmp");
    }
  }

  // Constant folding: memcmp(x, y, Len) -> constant (all arguments are const).
  // TODO: This is limited to i8 arrays.
  StringRef LHSStr, RHSStr;
  if (getConstantStringInfo(LHS, LHSStr) &&
      getConstantStringInfo(RHS, RHSStr)) {
    // Make sure we're not reading out-of-bounds memory.
    if (Len > LHSStr.size() || Len > RHSStr.size())
      return nullptr;
    // Fold the memcmp and normalize the result.  This way we get consistent
    // results across multiple platforms.
    uint64_t Ret = 0;
    int Cmp = memcmp(LHSStr.data(), RHSStr.data(), Len);
    if (Cmp < 0)
      Ret = -1;
    else if (Cmp > 0)
      Ret = 1;
    return ConstantInt::get(CI->getType(), Ret);
  }

  return nullptr;
}

Value *LibCallSimplifier::optimizeMemCpy(CallInst *CI, IRBuilder<> &B) {
  // memcpy(x, y, n) -> llvm.memcpy(align 1 x, align 1 y, n)
  B.CreateMemCpy(CI->getArgOperand(0), 1, CI->getArgOperand(1), 1,
                 CI->getArgOperand(2));
  return CI->getArgOperand(0);
}

Value *LibCallSimplifier::optimizeMemMove(CallInst *CI, IRBuilder<> &B) {
  // memmove(x, y, n) -> llvm.memmove(align 1 x, align 1 y, n)
  B.CreateMemMove(CI->getArgOperand(0), 1, CI->getArgOperand(1), 1,
                  CI->getArgOperand(2));
  return CI->getArgOperand(0);
}

/// Fold memset[_chk](malloc(n), 0, n) --> calloc(1, n).
Value *LibCallSimplifier::foldMallocMemset(CallInst *Memset, IRBuilder<> &B) {
  // This has to be a memset of zeros (bzero).
  auto *FillValue = dyn_cast<ConstantInt>(Memset->getArgOperand(1));
  if (!FillValue || FillValue->getZExtValue() != 0)
    return nullptr;

  // TODO: We should handle the case where the malloc has more than one use.
  // This is necessary to optimize common patterns such as when the result of
  // the malloc is checked against null or when a memset intrinsic is used in
  // place of a memset library call.
  auto *Malloc = dyn_cast<CallInst>(Memset->getArgOperand(0));
  if (!Malloc || !Malloc->hasOneUse())
    return nullptr;

  // Is the inner call really malloc()?
  Function *InnerCallee = Malloc->getCalledFunction();
  if (!InnerCallee)
    return nullptr;

  LibFunc Func;
  if (!TLI->getLibFunc(*InnerCallee, Func) || !TLI->has(Func) ||
      Func != LibFunc_malloc)
    return nullptr;

  // The memset must cover the same number of bytes that are malloc'd.
  if (Memset->getArgOperand(2) != Malloc->getArgOperand(0))
    return nullptr;

  // Replace the malloc with a calloc. We need the data layout to know what the
  // actual size of a 'size_t' parameter is.
  B.SetInsertPoint(Malloc->getParent(), ++Malloc->getIterator());
  const DataLayout &DL = Malloc->getModule()->getDataLayout();
  IntegerType *SizeType = DL.getIntPtrType(B.GetInsertBlock()->getContext());
  Value *Calloc = emitCalloc(ConstantInt::get(SizeType, 1),
                             Malloc->getArgOperand(0), Malloc->getAttributes(),
                             B, *TLI);
  if (!Calloc)
    return nullptr;

  Malloc->replaceAllUsesWith(Calloc);
  eraseFromParent(Malloc);

  return Calloc;
}

Value *LibCallSimplifier::optimizeMemSet(CallInst *CI, IRBuilder<> &B) {
  if (auto *Calloc = foldMallocMemset(CI, B))
    return Calloc;

  // memset(p, v, n) -> llvm.memset(align 1 p, v, n)
  Value *Val = B.CreateIntCast(CI->getArgOperand(1), B.getInt8Ty(), false);
  B.CreateMemSet(CI->getArgOperand(0), Val, CI->getArgOperand(2), 1);
  return CI->getArgOperand(0);
}

Value *LibCallSimplifier::optimizeRealloc(CallInst *CI, IRBuilder<> &B) {
  if (isa<ConstantPointerNull>(CI->getArgOperand(0)))
    return emitMalloc(CI->getArgOperand(1), B, DL, TLI);

  return nullptr;
}

//===----------------------------------------------------------------------===//
// Math Library Optimizations
//===----------------------------------------------------------------------===//

// Replace a libcall \p CI with a call to intrinsic \p IID
static Value *replaceUnaryCall(CallInst *CI, IRBuilder<> &B, Intrinsic::ID IID) {
  // Propagate fast-math flags from the existing call to the new call.
  IRBuilder<>::FastMathFlagGuard Guard(B);
  B.setFastMathFlags(CI->getFastMathFlags());

  Module *M = CI->getModule();
  Value *V = CI->getArgOperand(0);
  Function *F = Intrinsic::getDeclaration(M, IID, CI->getType());
  CallInst *NewCall = B.CreateCall(F, V);
  NewCall->takeName(CI);
  return NewCall;
}

/// Return a variant of Val with float type.
/// Currently this works in two cases: If Val is an FPExtension of a float
/// value to something bigger, simply return the operand.
/// If Val is a ConstantFP but can be converted to a float ConstantFP without
/// loss of precision do so.
static Value *valueHasFloatPrecision(Value *Val) {
  if (FPExtInst *Cast = dyn_cast<FPExtInst>(Val)) {
    Value *Op = Cast->getOperand(0);
    if (Op->getType()->isFloatTy())
      return Op;
  }
  if (ConstantFP *Const = dyn_cast<ConstantFP>(Val)) {
    APFloat F = Const->getValueAPF();
    bool losesInfo;
    (void)F.convert(APFloat::IEEEsingle(), APFloat::rmNearestTiesToEven,
                    &losesInfo);
    if (!losesInfo)
      return ConstantFP::get(Const->getContext(), F);
  }
  return nullptr;
}

/// Shrink double -> float functions.
static Value *optimizeDoubleFP(CallInst *CI, IRBuilder<> &B,
                               bool isBinary, bool isPrecise = false) {
  if (!CI->getType()->isDoubleTy())
    return nullptr;

  // If not all the uses of the function are converted to float, then bail out.
  // This matters if the precision of the result is more important than the
  // precision of the arguments.
  if (isPrecise)
    for (User *U : CI->users()) {
      FPTruncInst *Cast = dyn_cast<FPTruncInst>(U);
      if (!Cast || !Cast->getType()->isFloatTy())
        return nullptr;
    }

  // If this is something like 'g((double) float)', convert to 'gf(float)'.
  Value *V[2];
  V[0] = valueHasFloatPrecision(CI->getArgOperand(0));
  V[1] = isBinary ? valueHasFloatPrecision(CI->getArgOperand(1)) : nullptr;
  if (!V[0] || (isBinary && !V[1]))
    return nullptr;

  // If call isn't an intrinsic, check that it isn't within a function with the
  // same name as the float version of this call, otherwise the result is an
  // infinite loop.  For example, from MinGW-w64:
  //
  // float expf(float val) { return (float) exp((double) val); }
  Function *CalleeFn = CI->getCalledFunction();
  StringRef CalleeNm = CalleeFn->getName();
  AttributeList CalleeAt = CalleeFn->getAttributes();
  if (CalleeFn && !CalleeFn->isIntrinsic()) {
    const Function *Fn = CI->getFunction();
    StringRef FnName = Fn->getName();
    if (FnName.back() == 'f' &&
        FnName.size() == (CalleeNm.size() + 1) &&
        FnName.startswith(CalleeNm))
      return nullptr;
  }

  // Propagate the math semantics from the current function to the new function.
  IRBuilder<>::FastMathFlagGuard Guard(B);
  B.setFastMathFlags(CI->getFastMathFlags());

  // g((double) float) -> (double) gf(float)
  Value *R;
  if (CalleeFn->isIntrinsic()) {
    Module *M = CI->getModule();
    Intrinsic::ID IID = CalleeFn->getIntrinsicID();
    Function *Fn = Intrinsic::getDeclaration(M, IID, B.getFloatTy());
    R = isBinary ? B.CreateCall(Fn, V) : B.CreateCall(Fn, V[0]);
  }
  else
    R = isBinary ? emitBinaryFloatFnCall(V[0], V[1], CalleeNm, B, CalleeAt)
                 : emitUnaryFloatFnCall(V[0], CalleeNm, B, CalleeAt);

  return B.CreateFPExt(R, B.getDoubleTy());
}

/// Shrink double -> float for unary functions.
static Value *optimizeUnaryDoubleFP(CallInst *CI, IRBuilder<> &B,
                                    bool isPrecise = false) {
  return optimizeDoubleFP(CI, B, false, isPrecise);
}

/// Shrink double -> float for binary functions.
static Value *optimizeBinaryDoubleFP(CallInst *CI, IRBuilder<> &B,
                                     bool isPrecise = false) {
  return optimizeDoubleFP(CI, B, true, isPrecise);
}

// cabs(z) -> sqrt((creal(z)*creal(z)) + (cimag(z)*cimag(z)))
Value *LibCallSimplifier::optimizeCAbs(CallInst *CI, IRBuilder<> &B) {
  if (!CI->isFast())
    return nullptr;

  // Propagate fast-math flags from the existing call to new instructions.
  IRBuilder<>::FastMathFlagGuard Guard(B);
  B.setFastMathFlags(CI->getFastMathFlags());

  Value *Real, *Imag;
  if (CI->getNumArgOperands() == 1) {
    Value *Op = CI->getArgOperand(0);
    assert(Op->getType()->isArrayTy() && "Unexpected signature for cabs!");
    Real = B.CreateExtractValue(Op, 0, "real");
    Imag = B.CreateExtractValue(Op, 1, "imag");
  } else {
    assert(CI->getNumArgOperands() == 2 && "Unexpected signature for cabs!");
    Real = CI->getArgOperand(0);
    Imag = CI->getArgOperand(1);
  }

  Value *RealReal = B.CreateFMul(Real, Real);
  Value *ImagImag = B.CreateFMul(Imag, Imag);

  Function *FSqrt = Intrinsic::getDeclaration(CI->getModule(), Intrinsic::sqrt,
                                              CI->getType());
  return B.CreateCall(FSqrt, B.CreateFAdd(RealReal, ImagImag), "cabs");
}

static Value *optimizeTrigReflections(CallInst *Call, LibFunc Func,
                                      IRBuilder<> &B) {
  if (!isa<FPMathOperator>(Call))
    return nullptr;
  
  IRBuilder<>::FastMathFlagGuard Guard(B);
  B.setFastMathFlags(Call->getFastMathFlags());
  
  // TODO: Can this be shared to also handle LLVM intrinsics?
  Value *X;
  switch (Func) {
  case LibFunc_sin:
  case LibFunc_sinf:
  case LibFunc_sinl:
  case LibFunc_tan:
  case LibFunc_tanf:
  case LibFunc_tanl:
    // sin(-X) --> -sin(X)
    // tan(-X) --> -tan(X)
    if (match(Call->getArgOperand(0), m_OneUse(m_FNeg(m_Value(X)))))
      return B.CreateFNeg(B.CreateCall(Call->getCalledFunction(), X));
    break;
  case LibFunc_cos:
  case LibFunc_cosf:
  case LibFunc_cosl:
    // cos(-X) --> cos(X)
    if (match(Call->getArgOperand(0), m_FNeg(m_Value(X))))
      return B.CreateCall(Call->getCalledFunction(), X, "cos");
    break;
  default:
    break;
  }
  return nullptr;
}

static Value *getPow(Value *InnerChain[33], unsigned Exp, IRBuilder<> &B) {
  // Multiplications calculated using Addition Chains.
  // Refer: http://wwwhomes.uni-bielefeld.de/achim/addition_chain.html

  assert(Exp != 0 && "Incorrect exponent 0 not handled");

  if (InnerChain[Exp])
    return InnerChain[Exp];

  static const unsigned AddChain[33][2] = {
      {0, 0}, // Unused.
      {0, 0}, // Unused (base case = pow1).
      {1, 1}, // Unused (pre-computed).
      {1, 2},  {2, 2},   {2, 3},  {3, 3},   {2, 5},  {4, 4},
      {1, 8},  {5, 5},   {1, 10}, {6, 6},   {4, 9},  {7, 7},
      {3, 12}, {8, 8},   {8, 9},  {2, 16},  {1, 18}, {10, 10},
      {6, 15}, {11, 11}, {3, 20}, {12, 12}, {8, 17}, {13, 13},
      {3, 24}, {14, 14}, {4, 25}, {15, 15}, {3, 28}, {16, 16},
  };

  InnerChain[Exp] = B.CreateFMul(getPow(InnerChain, AddChain[Exp][0], B),
                                 getPow(InnerChain, AddChain[Exp][1], B));
  return InnerChain[Exp];
}

/// Use exp{,2}(x * y) for pow(exp{,2}(x), y);
/// exp2(n * x) for pow(2.0 ** n, x); exp10(x) for pow(10.0, x).
Value *LibCallSimplifier::replacePowWithExp(CallInst *Pow, IRBuilder<> &B) {
  Value *Base = Pow->getArgOperand(0), *Expo = Pow->getArgOperand(1);
  AttributeList Attrs = Pow->getCalledFunction()->getAttributes();
  Module *Mod = Pow->getModule();
  Type *Ty = Pow->getType();
  bool Ignored;

  // Evaluate special cases related to a nested function as the base.

  // pow(exp(x), y) -> exp(x * y)
  // pow(exp2(x), y) -> exp2(x * y)
  // If exp{,2}() is used only once, it is better to fold two transcendental
  // math functions into one.  If used again, exp{,2}() would still have to be
  // called with the original argument, then keep both original transcendental
  // functions.  However, this transformation is only safe with fully relaxed
  // math semantics, since, besides rounding differences, it changes overflow
  // and underflow behavior quite dramatically.  For example:
  //   pow(exp(1000), 0.001) = pow(inf, 0.001) = inf
  // Whereas:
  //   exp(1000 * 0.001) = exp(1)
  // TODO: Loosen the requirement for fully relaxed math semantics.
  // TODO: Handle exp10() when more targets have it available.
  CallInst *BaseFn = dyn_cast<CallInst>(Base);
  if (BaseFn && BaseFn->hasOneUse() && BaseFn->isFast() && Pow->isFast()) {
    LibFunc LibFn;

    Function *CalleeFn = BaseFn->getCalledFunction();
    if (CalleeFn &&
        TLI->getLibFunc(CalleeFn->getName(), LibFn) && TLI->has(LibFn)) {
      StringRef ExpName;
      Intrinsic::ID ID;
      Value *ExpFn;
      LibFunc LibFnFloat;
      LibFunc LibFnDouble;
      LibFunc LibFnLongDouble;

      switch (LibFn) {
      default:
        return nullptr;
      case LibFunc_expf:  case LibFunc_exp:  case LibFunc_expl:
        ExpName = TLI->getName(LibFunc_exp);
        ID = Intrinsic::exp;
        LibFnFloat = LibFunc_expf;
        LibFnDouble = LibFunc_exp;
        LibFnLongDouble = LibFunc_expl;
        break;
      case LibFunc_exp2f: case LibFunc_exp2: case LibFunc_exp2l:
        ExpName = TLI->getName(LibFunc_exp2);
        ID = Intrinsic::exp2;
        LibFnFloat = LibFunc_exp2f;
        LibFnDouble = LibFunc_exp2;
        LibFnLongDouble = LibFunc_exp2l;
        break;
      }

      // Create new exp{,2}() with the product as its argument.
      Value *FMul = B.CreateFMul(BaseFn->getArgOperand(0), Expo, "mul");
      ExpFn = BaseFn->doesNotAccessMemory()
              ? B.CreateCall(Intrinsic::getDeclaration(Mod, ID, Ty),
                             FMul, ExpName)
              : emitUnaryFloatFnCall(FMul, TLI, LibFnDouble, LibFnFloat,
                                     LibFnLongDouble, B,
                                     BaseFn->getAttributes());

      // Since the new exp{,2}() is different from the original one, dead code
      // elimination cannot be trusted to remove it, since it may have side
      // effects (e.g., errno).  When the only consumer for the original
      // exp{,2}() is pow(), then it has to be explicitly erased.
      BaseFn->replaceAllUsesWith(ExpFn);
      eraseFromParent(BaseFn);

      return ExpFn;
    }
  }

  // Evaluate special cases related to a constant base.

  const APFloat *BaseF;
  if (!match(Pow->getArgOperand(0), m_APFloat(BaseF)))
    return nullptr;

  // pow(2.0 ** n, x) -> exp2(n * x)
  if (hasUnaryFloatFn(TLI, Ty, LibFunc_exp2, LibFunc_exp2f, LibFunc_exp2l)) {
    APFloat BaseR = APFloat(1.0);
    BaseR.convert(BaseF->getSemantics(), APFloat::rmTowardZero, &Ignored);
    BaseR = BaseR / *BaseF;
    bool IsInteger    = BaseF->isInteger(),
         IsReciprocal = BaseR.isInteger();
    const APFloat *NF = IsReciprocal ? &BaseR : BaseF;
    APSInt NI(64, false);
    if ((IsInteger || IsReciprocal) &&
        !NF->convertToInteger(NI, APFloat::rmTowardZero, &Ignored) &&
        NI > 1 && NI.isPowerOf2()) {
      double N = NI.logBase2() * (IsReciprocal ? -1.0 : 1.0);
      Value *FMul = B.CreateFMul(Expo, ConstantFP::get(Ty, N), "mul");
      if (Pow->doesNotAccessMemory())
        return B.CreateCall(Intrinsic::getDeclaration(Mod, Intrinsic::exp2, Ty),
                            FMul, "exp2");
      else
        return emitUnaryFloatFnCall(FMul, TLI, LibFunc_exp2, LibFunc_exp2f,
                                    LibFunc_exp2l, B, Attrs);
    }
  }

  // pow(10.0, x) -> exp10(x)
  // TODO: There is no exp10() intrinsic yet, but some day there shall be one.
  if (match(Base, m_SpecificFP(10.0)) &&
      hasUnaryFloatFn(TLI, Ty, LibFunc_exp10, LibFunc_exp10f, LibFunc_exp10l))
    return emitUnaryFloatFnCall(Expo, TLI, LibFunc_exp10, LibFunc_exp10f,
                                LibFunc_exp10l, B, Attrs);

  return nullptr;
}

static Value *getSqrtCall(Value *V, AttributeList Attrs, bool NoErrno,
                          Module *M, IRBuilder<> &B,
                          const TargetLibraryInfo *TLI) {
  // If errno is never set, then use the intrinsic for sqrt().
  if (NoErrno) {
    Function *SqrtFn =
        Intrinsic::getDeclaration(M, Intrinsic::sqrt, V->getType());
    return B.CreateCall(SqrtFn, V, "sqrt");
  }

  // Otherwise, use the libcall for sqrt().
  if (hasUnaryFloatFn(TLI, V->getType(), LibFunc_sqrt, LibFunc_sqrtf,
                      LibFunc_sqrtl))
    // TODO: We also should check that the target can in fact lower the sqrt()
    // libcall. We currently have no way to ask this question, so we ask if
    // the target has a sqrt() libcall, which is not exactly the same.
    return emitUnaryFloatFnCall(V, TLI, LibFunc_sqrt, LibFunc_sqrtf,
                                LibFunc_sqrtl, B, Attrs);

  return nullptr;
}

/// Use square root in place of pow(x, +/-0.5).
Value *LibCallSimplifier::replacePowWithSqrt(CallInst *Pow, IRBuilder<> &B) {
  Value *Sqrt, *Base = Pow->getArgOperand(0), *Expo = Pow->getArgOperand(1);
  AttributeList Attrs = Pow->getCalledFunction()->getAttributes();
  Module *Mod = Pow->getModule();
  Type *Ty = Pow->getType();

  const APFloat *ExpoF;
  if (!match(Expo, m_APFloat(ExpoF)) ||
      (!ExpoF->isExactlyValue(0.5) && !ExpoF->isExactlyValue(-0.5)))
    return nullptr;

  Sqrt = getSqrtCall(Base, Attrs, Pow->doesNotAccessMemory(), Mod, B, TLI);
  if (!Sqrt)
    return nullptr;

  // Handle signed zero base by expanding to fabs(sqrt(x)).
  if (!Pow->hasNoSignedZeros()) {
    Function *FAbsFn = Intrinsic::getDeclaration(Mod, Intrinsic::fabs, Ty);
    Sqrt = B.CreateCall(FAbsFn, Sqrt, "abs");
  }

  // Handle non finite base by expanding to
  // (x == -infinity ? +infinity : sqrt(x)).
  if (!Pow->hasNoInfs()) {
    Value *PosInf = ConstantFP::getInfinity(Ty),
          *NegInf = ConstantFP::getInfinity(Ty, true);
    Value *FCmp = B.CreateFCmpOEQ(Base, NegInf, "isinf");
    Sqrt = B.CreateSelect(FCmp, PosInf, Sqrt);
  }

  // If the exponent is negative, then get the reciprocal.
  if (ExpoF->isNegative())
    Sqrt = B.CreateFDiv(ConstantFP::get(Ty, 1.0), Sqrt, "reciprocal");

  return Sqrt;
}

Value *LibCallSimplifier::optimizePow(CallInst *Pow, IRBuilder<> &B) {
  Value *Base = Pow->getArgOperand(0), *Expo = Pow->getArgOperand(1);
  Function *Callee = Pow->getCalledFunction();
  StringRef Name = Callee->getName();
  Type *Ty = Pow->getType();
  Value *Shrunk = nullptr;
  bool Ignored;

  // Bail out if simplifying libcalls to pow() is disabled.
  if (!hasUnaryFloatFn(TLI, Ty, LibFunc_pow, LibFunc_powf, LibFunc_powl))
    return nullptr;

  // Propagate the math semantics from the call to any created instructions.
  IRBuilder<>::FastMathFlagGuard Guard(B);
  B.setFastMathFlags(Pow->getFastMathFlags());

  // Shrink pow() to powf() if the arguments are single precision,
  // unless the result is expected to be double precision.
  if (UnsafeFPShrink &&
      Name == TLI->getName(LibFunc_pow) && hasFloatVersion(Name))
    Shrunk = optimizeBinaryDoubleFP(Pow, B, true);

  // Evaluate special cases related to the base.

  // pow(1.0, x) -> 1.0
  if (match(Base, m_FPOne()))
    return Base;

  if (Value *Exp = replacePowWithExp(Pow, B))
    return Exp;

  // Evaluate special cases related to the exponent.

  // pow(x, -1.0) -> 1.0 / x
  if (match(Expo, m_SpecificFP(-1.0)))
    return B.CreateFDiv(ConstantFP::get(Ty, 1.0), Base, "reciprocal");

  // pow(x, 0.0) -> 1.0
  if (match(Expo, m_SpecificFP(0.0)))
      return ConstantFP::get(Ty, 1.0);

  // pow(x, 1.0) -> x
  if (match(Expo, m_FPOne()))
    return Base;

  // pow(x, 2.0) -> x * x
  if (match(Expo, m_SpecificFP(2.0)))
    return B.CreateFMul(Base, Base, "square");

  if (Value *Sqrt = replacePowWithSqrt(Pow, B))
    return Sqrt;

  // pow(x, n) -> x * x * x * ...
  const APFloat *ExpoF;
  if (Pow->isFast() && match(Expo, m_APFloat(ExpoF))) {
    // We limit to a max of 7 multiplications, thus the maximum exponent is 32.
    // If the exponent is an integer+0.5 we generate a call to sqrt and an
    // additional fmul.
    // TODO: This whole transformation should be backend specific (e.g. some
    //       backends might prefer libcalls or the limit for the exponent might
    //       be different) and it should also consider optimizing for size.
    APFloat LimF(ExpoF->getSemantics(), 33.0),
            ExpoA(abs(*ExpoF));
    if (ExpoA.compare(LimF) == APFloat::cmpLessThan) {
      // This transformation applies to integer or integer+0.5 exponents only.
      // For integer+0.5, we create a sqrt(Base) call.
      Value *Sqrt = nullptr;
      if (!ExpoA.isInteger()) {
        APFloat Expo2 = ExpoA;
        // To check if ExpoA is an integer + 0.5, we add it to itself. If there
        // is no floating point exception and the result is an integer, then
        // ExpoA == integer + 0.5
        if (Expo2.add(ExpoA, APFloat::rmNearestTiesToEven) != APFloat::opOK)
          return nullptr;

        if (!Expo2.isInteger())
          return nullptr;

        Sqrt =
            getSqrtCall(Base, Pow->getCalledFunction()->getAttributes(),
                        Pow->doesNotAccessMemory(), Pow->getModule(), B, TLI);
      }

      // We will memoize intermediate products of the Addition Chain.
      Value *InnerChain[33] = {nullptr};
      InnerChain[1] = Base;
      InnerChain[2] = B.CreateFMul(Base, Base, "square");

      // We cannot readily convert a non-double type (like float) to a double.
      // So we first convert it to something which could be converted to double.
      ExpoA.convert(APFloat::IEEEdouble(), APFloat::rmTowardZero, &Ignored);
      Value *FMul = getPow(InnerChain, ExpoA.convertToDouble(), B);

      // Expand pow(x, y+0.5) to pow(x, y) * sqrt(x).
      if (Sqrt)
        FMul = B.CreateFMul(FMul, Sqrt);

      // If the exponent is negative, then get the reciprocal.
      if (ExpoF->isNegative())
        FMul = B.CreateFDiv(ConstantFP::get(Ty, 1.0), FMul, "reciprocal");

      return FMul;
    }
  }

  return Shrunk;
}

Value *LibCallSimplifier::optimizeExp2(CallInst *CI, IRBuilder<> &B) {
  Function *Callee = CI->getCalledFunction();
  Value *Ret = nullptr;
  StringRef Name = Callee->getName();
  if (UnsafeFPShrink && Name == "exp2" && hasFloatVersion(Name))
    Ret = optimizeUnaryDoubleFP(CI, B, true);

  Value *Op = CI->getArgOperand(0);
  // Turn exp2(sitofp(x)) -> ldexp(1.0, sext(x))  if sizeof(x) <= 32
  // Turn exp2(uitofp(x)) -> ldexp(1.0, zext(x))  if sizeof(x) < 32
  LibFunc LdExp = LibFunc_ldexpl;
  if (Op->getType()->isFloatTy())
    LdExp = LibFunc_ldexpf;
  else if (Op->getType()->isDoubleTy())
    LdExp = LibFunc_ldexp;

  if (TLI->has(LdExp)) {
    Value *LdExpArg = nullptr;
    if (SIToFPInst *OpC = dyn_cast<SIToFPInst>(Op)) {
      if (OpC->getOperand(0)->getType()->getPrimitiveSizeInBits() <= 32)
        LdExpArg = B.CreateSExt(OpC->getOperand(0), B.getInt32Ty());
    } else if (UIToFPInst *OpC = dyn_cast<UIToFPInst>(Op)) {
      if (OpC->getOperand(0)->getType()->getPrimitiveSizeInBits() < 32)
        LdExpArg = B.CreateZExt(OpC->getOperand(0), B.getInt32Ty());
    }

    if (LdExpArg) {
      Constant *One = ConstantFP::get(CI->getContext(), APFloat(1.0f));
      if (!Op->getType()->isFloatTy())
        One = ConstantExpr::getFPExtend(One, Op->getType());

      Module *M = CI->getModule();
      Value *NewCallee =
          M->getOrInsertFunction(TLI->getName(LdExp), Op->getType(),
                                 Op->getType(), B.getInt32Ty());
      CallInst *CI = B.CreateCall(NewCallee, {One, LdExpArg});
      if (const Function *F = dyn_cast<Function>(Callee->stripPointerCasts()))
        CI->setCallingConv(F->getCallingConv());

      return CI;
    }
  }
  return Ret;
}

Value *LibCallSimplifier::optimizeFMinFMax(CallInst *CI, IRBuilder<> &B) {
  Function *Callee = CI->getCalledFunction();
  // If we can shrink the call to a float function rather than a double
  // function, do that first.
  StringRef Name = Callee->getName();
  if ((Name == "fmin" || Name == "fmax") && hasFloatVersion(Name))
    if (Value *Ret = optimizeBinaryDoubleFP(CI, B))
      return Ret;

  IRBuilder<>::FastMathFlagGuard Guard(B);
  FastMathFlags FMF;
  if (CI->isFast()) {
    // If the call is 'fast', then anything we create here will also be 'fast'.
    FMF.setFast();
  } else {
    // At a minimum, no-nans-fp-math must be true.
    if (!CI->hasNoNaNs())
      return nullptr;
    // No-signed-zeros is implied by the definitions of fmax/fmin themselves:
    // "Ideally, fmax would be sensitive to the sign of zero, for example
    // fmax(-0. 0, +0. 0) would return +0; however, implementation in software
    // might be impractical."
    FMF.setNoSignedZeros();
    FMF.setNoNaNs();
  }
  B.setFastMathFlags(FMF);

  // We have a relaxed floating-point environment. We can ignore NaN-handling
  // and transform to a compare and select. We do not have to consider errno or
  // exceptions, because fmin/fmax do not have those.
  Value *Op0 = CI->getArgOperand(0);
  Value *Op1 = CI->getArgOperand(1);
  Value *Cmp = Callee->getName().startswith("fmin") ?
    B.CreateFCmpOLT(Op0, Op1) : B.CreateFCmpOGT(Op0, Op1);
  return B.CreateSelect(Cmp, Op0, Op1);
}

Value *LibCallSimplifier::optimizeLog(CallInst *CI, IRBuilder<> &B) {
  Function *Callee = CI->getCalledFunction();
  Value *Ret = nullptr;
  StringRef Name = Callee->getName();
  if (UnsafeFPShrink && hasFloatVersion(Name))
    Ret = optimizeUnaryDoubleFP(CI, B, true);

  if (!CI->isFast())
    return Ret;
  Value *Op1 = CI->getArgOperand(0);
  auto *OpC = dyn_cast<CallInst>(Op1);

  // The earlier call must also be 'fast' in order to do these transforms.
  if (!OpC || !OpC->isFast())
    return Ret;

  // log(pow(x,y)) -> y*log(x)
  // This is only applicable to log, log2, log10.
  if (Name != "log" && Name != "log2" && Name != "log10")
    return Ret;

  IRBuilder<>::FastMathFlagGuard Guard(B);
  FastMathFlags FMF;
  FMF.setFast();
  B.setFastMathFlags(FMF);

  LibFunc Func;
  Function *F = OpC->getCalledFunction();
  if (F && ((TLI->getLibFunc(F->getName(), Func) && TLI->has(Func) &&
      Func == LibFunc_pow) || F->getIntrinsicID() == Intrinsic::pow))
    return B.CreateFMul(OpC->getArgOperand(1),
      emitUnaryFloatFnCall(OpC->getOperand(0), Callee->getName(), B,
                           Callee->getAttributes()), "mul");

  // log(exp2(y)) -> y*log(2)
  if (F && Name == "log" && TLI->getLibFunc(F->getName(), Func) &&
      TLI->has(Func) && Func == LibFunc_exp2)
    return B.CreateFMul(
        OpC->getArgOperand(0),
        emitUnaryFloatFnCall(ConstantFP::get(CI->getType(), 2.0),
                             Callee->getName(), B, Callee->getAttributes()),
        "logmul");
  return Ret;
}

Value *LibCallSimplifier::optimizeSqrt(CallInst *CI, IRBuilder<> &B) {
  Function *Callee = CI->getCalledFunction();
  Value *Ret = nullptr;
  // TODO: Once we have a way (other than checking for the existince of the
  // libcall) to tell whether our target can lower @llvm.sqrt, relax the
  // condition below.
  if (TLI->has(LibFunc_sqrtf) && (Callee->getName() == "sqrt" ||
                                  Callee->getIntrinsicID() == Intrinsic::sqrt))
    Ret = optimizeUnaryDoubleFP(CI, B, true);

  if (!CI->isFast())
    return Ret;

  Instruction *I = dyn_cast<Instruction>(CI->getArgOperand(0));
  if (!I || I->getOpcode() != Instruction::FMul || !I->isFast())
    return Ret;

  // We're looking for a repeated factor in a multiplication tree,
  // so we can do this fold: sqrt(x * x) -> fabs(x);
  // or this fold: sqrt((x * x) * y) -> fabs(x) * sqrt(y).
  Value *Op0 = I->getOperand(0);
  Value *Op1 = I->getOperand(1);
  Value *RepeatOp = nullptr;
  Value *OtherOp = nullptr;
  if (Op0 == Op1) {
    // Simple match: the operands of the multiply are identical.
    RepeatOp = Op0;
  } else {
    // Look for a more complicated pattern: one of the operands is itself
    // a multiply, so search for a common factor in that multiply.
    // Note: We don't bother looking any deeper than this first level or for
    // variations of this pattern because instcombine's visitFMUL and/or the
    // reassociation pass should give us this form.
    Value *OtherMul0, *OtherMul1;
    if (match(Op0, m_FMul(m_Value(OtherMul0), m_Value(OtherMul1)))) {
      // Pattern: sqrt((x * y) * z)
      if (OtherMul0 == OtherMul1 && cast<Instruction>(Op0)->isFast()) {
        // Matched: sqrt((x * x) * z)
        RepeatOp = OtherMul0;
        OtherOp = Op1;
      }
    }
  }
  if (!RepeatOp)
    return Ret;

  // Fast math flags for any created instructions should match the sqrt
  // and multiply.
  IRBuilder<>::FastMathFlagGuard Guard(B);
  B.setFastMathFlags(I->getFastMathFlags());

  // If we found a repeated factor, hoist it out of the square root and
  // replace it with the fabs of that factor.
  Module *M = Callee->getParent();
  Type *ArgType = I->getType();
  Value *Fabs = Intrinsic::getDeclaration(M, Intrinsic::fabs, ArgType);
  Value *FabsCall = B.CreateCall(Fabs, RepeatOp, "fabs");
  if (OtherOp) {
    // If we found a non-repeated factor, we still need to get its square
    // root. We then multiply that by the value that was simplified out
    // of the square root calculation.
    Value *Sqrt = Intrinsic::getDeclaration(M, Intrinsic::sqrt, ArgType);
    Value *SqrtCall = B.CreateCall(Sqrt, OtherOp, "sqrt");
    return B.CreateFMul(FabsCall, SqrtCall);
  }
  return FabsCall;
}

// TODO: Generalize to handle any trig function and its inverse.
Value *LibCallSimplifier::optimizeTan(CallInst *CI, IRBuilder<> &B) {
  Function *Callee = CI->getCalledFunction();
  Value *Ret = nullptr;
  StringRef Name = Callee->getName();
  if (UnsafeFPShrink && Name == "tan" && hasFloatVersion(Name))
    Ret = optimizeUnaryDoubleFP(CI, B, true);

  Value *Op1 = CI->getArgOperand(0);
  auto *OpC = dyn_cast<CallInst>(Op1);
  if (!OpC)
    return Ret;

  // Both calls must be 'fast' in order to remove them.
  if (!CI->isFast() || !OpC->isFast())
    return Ret;

  // tan(atan(x)) -> x
  // tanf(atanf(x)) -> x
  // tanl(atanl(x)) -> x
  LibFunc Func;
  Function *F = OpC->getCalledFunction();
  if (F && TLI->getLibFunc(F->getName(), Func) && TLI->has(Func) &&
      ((Func == LibFunc_atan && Callee->getName() == "tan") ||
       (Func == LibFunc_atanf && Callee->getName() == "tanf") ||
       (Func == LibFunc_atanl && Callee->getName() == "tanl")))
    Ret = OpC->getArgOperand(0);
  return Ret;
}

static bool isTrigLibCall(CallInst *CI) {
  // We can only hope to do anything useful if we can ignore things like errno
  // and floating-point exceptions.
  // We already checked the prototype.
  return CI->hasFnAttr(Attribute::NoUnwind) &&
         CI->hasFnAttr(Attribute::ReadNone);
}

static void insertSinCosCall(IRBuilder<> &B, Function *OrigCallee, Value *Arg,
                             bool UseFloat, Value *&Sin, Value *&Cos,
                             Value *&SinCos) {
  Type *ArgTy = Arg->getType();
  Type *ResTy;
  StringRef Name;

  Triple T(OrigCallee->getParent()->getTargetTriple());
  if (UseFloat) {
    Name = "__sincospif_stret";

    assert(T.getArch() != Triple::x86 && "x86 messy and unsupported for now");
    // x86_64 can't use {float, float} since that would be returned in both
    // xmm0 and xmm1, which isn't what a real struct would do.
    ResTy = T.getArch() == Triple::x86_64
                ? static_cast<Type *>(VectorType::get(ArgTy, 2))
                : static_cast<Type *>(StructType::get(ArgTy, ArgTy));
  } else {
    Name = "__sincospi_stret";
    ResTy = StructType::get(ArgTy, ArgTy);
  }

  Module *M = OrigCallee->getParent();
  Value *Callee = M->getOrInsertFunction(Name, OrigCallee->getAttributes(),
                                         ResTy, ArgTy);

  if (Instruction *ArgInst = dyn_cast<Instruction>(Arg)) {
    // If the argument is an instruction, it must dominate all uses so put our
    // sincos call there.
    B.SetInsertPoint(ArgInst->getParent(), ++ArgInst->getIterator());
  } else {
    // Otherwise (e.g. for a constant) the beginning of the function is as
    // good a place as any.
    BasicBlock &EntryBB = B.GetInsertBlock()->getParent()->getEntryBlock();
    B.SetInsertPoint(&EntryBB, EntryBB.begin());
  }

  SinCos = B.CreateCall(Callee, Arg, "sincospi");

  if (SinCos->getType()->isStructTy()) {
    Sin = B.CreateExtractValue(SinCos, 0, "sinpi");
    Cos = B.CreateExtractValue(SinCos, 1, "cospi");
  } else {
    Sin = B.CreateExtractElement(SinCos, ConstantInt::get(B.getInt32Ty(), 0),
                                 "sinpi");
    Cos = B.CreateExtractElement(SinCos, ConstantInt::get(B.getInt32Ty(), 1),
                                 "cospi");
  }
}

Value *LibCallSimplifier::optimizeSinCosPi(CallInst *CI, IRBuilder<> &B) {
  // Make sure the prototype is as expected, otherwise the rest of the
  // function is probably invalid and likely to abort.
  if (!isTrigLibCall(CI))
    return nullptr;

  Value *Arg = CI->getArgOperand(0);
  SmallVector<CallInst *, 1> SinCalls;
  SmallVector<CallInst *, 1> CosCalls;
  SmallVector<CallInst *, 1> SinCosCalls;

  bool IsFloat = Arg->getType()->isFloatTy();

  // Look for all compatible sinpi, cospi and sincospi calls with the same
  // argument. If there are enough (in some sense) we can make the
  // substitution.
  Function *F = CI->getFunction();
  for (User *U : Arg->users())
    classifyArgUse(U, F, IsFloat, SinCalls, CosCalls, SinCosCalls);

  // It's only worthwhile if both sinpi and cospi are actually used.
  if (SinCosCalls.empty() && (SinCalls.empty() || CosCalls.empty()))
    return nullptr;

  Value *Sin, *Cos, *SinCos;
  insertSinCosCall(B, CI->getCalledFunction(), Arg, IsFloat, Sin, Cos, SinCos);

  auto replaceTrigInsts = [this](SmallVectorImpl<CallInst *> &Calls,
                                 Value *Res) {
    for (CallInst *C : Calls)
      replaceAllUsesWith(C, Res);
  };

  replaceTrigInsts(SinCalls, Sin);
  replaceTrigInsts(CosCalls, Cos);
  replaceTrigInsts(SinCosCalls, SinCos);

  return nullptr;
}

void LibCallSimplifier::classifyArgUse(
    Value *Val, Function *F, bool IsFloat,
    SmallVectorImpl<CallInst *> &SinCalls,
    SmallVectorImpl<CallInst *> &CosCalls,
    SmallVectorImpl<CallInst *> &SinCosCalls) {
  CallInst *CI = dyn_cast<CallInst>(Val);

  if (!CI)
    return;

  // Don't consider calls in other functions.
  if (CI->getFunction() != F)
    return;

  Function *Callee = CI->getCalledFunction();
  LibFunc Func;
  if (!Callee || !TLI->getLibFunc(*Callee, Func) || !TLI->has(Func) ||
      !isTrigLibCall(CI))
    return;

  if (IsFloat) {
    if (Func == LibFunc_sinpif)
      SinCalls.push_back(CI);
    else if (Func == LibFunc_cospif)
      CosCalls.push_back(CI);
    else if (Func == LibFunc_sincospif_stret)
      SinCosCalls.push_back(CI);
  } else {
    if (Func == LibFunc_sinpi)
      SinCalls.push_back(CI);
    else if (Func == LibFunc_cospi)
      CosCalls.push_back(CI);
    else if (Func == LibFunc_sincospi_stret)
      SinCosCalls.push_back(CI);
  }
}

//===----------------------------------------------------------------------===//
// Integer Library Call Optimizations
//===----------------------------------------------------------------------===//

Value *LibCallSimplifier::optimizeFFS(CallInst *CI, IRBuilder<> &B) {
  // ffs(x) -> x != 0 ? (i32)llvm.cttz(x)+1 : 0
  Value *Op = CI->getArgOperand(0);
  Type *ArgType = Op->getType();
  Value *F = Intrinsic::getDeclaration(CI->getCalledFunction()->getParent(),
                                       Intrinsic::cttz, ArgType);
  Value *V = B.CreateCall(F, {Op, B.getTrue()}, "cttz");
  V = B.CreateAdd(V, ConstantInt::get(V->getType(), 1));
  V = B.CreateIntCast(V, B.getInt32Ty(), false);

  Value *Cond = B.CreateICmpNE(Op, Constant::getNullValue(ArgType));
  return B.CreateSelect(Cond, V, B.getInt32(0));
}

Value *LibCallSimplifier::optimizeFls(CallInst *CI, IRBuilder<> &B) {
  // fls(x) -> (i32)(sizeInBits(x) - llvm.ctlz(x, false))
  Value *Op = CI->getArgOperand(0);
  Type *ArgType = Op->getType();
  Value *F = Intrinsic::getDeclaration(CI->getCalledFunction()->getParent(),
                                       Intrinsic::ctlz, ArgType);
  Value *V = B.CreateCall(F, {Op, B.getFalse()}, "ctlz");
  V = B.CreateSub(ConstantInt::get(V->getType(), ArgType->getIntegerBitWidth()),
                  V);
  return B.CreateIntCast(V, CI->getType(), false);
}

Value *LibCallSimplifier::optimizeAbs(CallInst *CI, IRBuilder<> &B) {
  // abs(x) -> x <s 0 ? -x : x
  // The negation has 'nsw' because abs of INT_MIN is undefined.
  Value *X = CI->getArgOperand(0);
  Value *IsNeg = B.CreateICmpSLT(X, Constant::getNullValue(X->getType()));
  Value *NegX = B.CreateNSWNeg(X, "neg");
  return B.CreateSelect(IsNeg, NegX, X);
}

Value *LibCallSimplifier::optimizeIsDigit(CallInst *CI, IRBuilder<> &B) {
  // isdigit(c) -> (c-'0') <u 10
  Value *Op = CI->getArgOperand(0);
  Op = B.CreateSub(Op, B.getInt32('0'), "isdigittmp");
  Op = B.CreateICmpULT(Op, B.getInt32(10), "isdigit");
  return B.CreateZExt(Op, CI->getType());
}

Value *LibCallSimplifier::optimizeIsAscii(CallInst *CI, IRBuilder<> &B) {
  // isascii(c) -> c <u 128
  Value *Op = CI->getArgOperand(0);
  Op = B.CreateICmpULT(Op, B.getInt32(128), "isascii");
  return B.CreateZExt(Op, CI->getType());
}

Value *LibCallSimplifier::optimizeToAscii(CallInst *CI, IRBuilder<> &B) {
  // toascii(c) -> c & 0x7f
  return B.CreateAnd(CI->getArgOperand(0),
                     ConstantInt::get(CI->getType(), 0x7F));
}

Value *LibCallSimplifier::optimizeAtoi(CallInst *CI, IRBuilder<> &B) {
  StringRef Str;
  if (!getConstantStringInfo(CI->getArgOperand(0), Str))
    return nullptr;

  return convertStrToNumber(CI, Str, 10);
}

Value *LibCallSimplifier::optimizeStrtol(CallInst *CI, IRBuilder<> &B) {
  StringRef Str;
  if (!getConstantStringInfo(CI->getArgOperand(0), Str))
    return nullptr;

  if (!isa<ConstantPointerNull>(CI->getArgOperand(1)))
    return nullptr;

  if (ConstantInt *CInt = dyn_cast<ConstantInt>(CI->getArgOperand(2))) {
    return convertStrToNumber(CI, Str, CInt->getSExtValue());
  }

  return nullptr;
}

//===----------------------------------------------------------------------===//
// Formatting and IO Library Call Optimizations
//===----------------------------------------------------------------------===//

static bool isReportingError(Function *Callee, CallInst *CI, int StreamArg);

Value *LibCallSimplifier::optimizeErrorReporting(CallInst *CI, IRBuilder<> &B,
                                                 int StreamArg) {
  Function *Callee = CI->getCalledFunction();
  // Error reporting calls should be cold, mark them as such.
  // This applies even to non-builtin calls: it is only a hint and applies to
  // functions that the frontend might not understand as builtins.

  // This heuristic was suggested in:
  // Improving Static Branch Prediction in a Compiler
  // Brian L. Deitrich, Ben-Chung Cheng, Wen-mei W. Hwu
  // Proceedings of PACT'98, Oct. 1998, IEEE
  if (!CI->hasFnAttr(Attribute::Cold) &&
      isReportingError(Callee, CI, StreamArg)) {
    CI->addAttribute(AttributeList::FunctionIndex, Attribute::Cold);
  }

  return nullptr;
}

static bool isReportingError(Function *Callee, CallInst *CI, int StreamArg) {
  if (!Callee || !Callee->isDeclaration())
    return false;

  if (StreamArg < 0)
    return true;

  // These functions might be considered cold, but only if their stream
  // argument is stderr.

  if (StreamArg >= (int)CI->getNumArgOperands())
    return false;
  LoadInst *LI = dyn_cast<LoadInst>(CI->getArgOperand(StreamArg));
  if (!LI)
    return false;
  GlobalVariable *GV = dyn_cast<GlobalVariable>(LI->getPointerOperand());
  if (!GV || !GV->isDeclaration())
    return false;
  return GV->getName() == "stderr";
}

Value *LibCallSimplifier::optimizePrintFString(CallInst *CI, IRBuilder<> &B) {
  // Check for a fixed format string.
  StringRef FormatStr;
  if (!getConstantStringInfo(CI->getArgOperand(0), FormatStr))
    return nullptr;

  // Empty format string -> noop.
  if (FormatStr.empty()) // Tolerate printf's declared void.
    return CI->use_empty() ? (Value *)CI : ConstantInt::get(CI->getType(), 0);

  // Do not do any of the following transformations if the printf return value
  // is used, in general the printf return value is not compatible with either
  // putchar() or puts().
  if (!CI->use_empty())
    return nullptr;

  // printf("x") -> putchar('x'), even for "%" and "%%".
  if (FormatStr.size() == 1 || FormatStr == "%%")
    return emitPutChar(B.getInt32(FormatStr[0]), B, TLI);

  // printf("%s", "a") --> putchar('a')
  if (FormatStr == "%s" && CI->getNumArgOperands() > 1) {
    StringRef ChrStr;
    if (!getConstantStringInfo(CI->getOperand(1), ChrStr))
      return nullptr;
    if (ChrStr.size() != 1)
      return nullptr;
    return emitPutChar(B.getInt32(ChrStr[0]), B, TLI);
  }

  // printf("foo\n") --> puts("foo")
  if (FormatStr[FormatStr.size() - 1] == '\n' &&
      FormatStr.find('%') == StringRef::npos) { // No format characters.
    // Create a string literal with no \n on it.  We expect the constant merge
    // pass to be run after this pass, to merge duplicate strings.
    FormatStr = FormatStr.drop_back();
    Value *GV = B.CreateGlobalString(FormatStr, "str");
    return emitPutS(GV, B, TLI);
  }

  // Optimize specific format strings.
  // printf("%c", chr) --> putchar(chr)
  if (FormatStr == "%c" && CI->getNumArgOperands() > 1 &&
      CI->getArgOperand(1)->getType()->isIntegerTy())
    return emitPutChar(CI->getArgOperand(1), B, TLI);

  // printf("%s\n", str) --> puts(str)
  if (FormatStr == "%s\n" && CI->getNumArgOperands() > 1 &&
      CI->getArgOperand(1)->getType()->isPointerTy())
    return emitPutS(CI->getArgOperand(1), B, TLI);
  return nullptr;
}

Value *LibCallSimplifier::optimizePrintF(CallInst *CI, IRBuilder<> &B) {

  Function *Callee = CI->getCalledFunction();
  FunctionType *FT = Callee->getFunctionType();
  if (Value *V = optimizePrintFString(CI, B)) {
    return V;
  }

  // printf(format, ...) -> iprintf(format, ...) if no floating point
  // arguments.
  if (TLI->has(LibFunc_iprintf) && !callHasFloatingPointArgument(CI)) {
    Module *M = B.GetInsertBlock()->getParent()->getParent();
    Constant *IPrintFFn =
        M->getOrInsertFunction("iprintf", FT, Callee->getAttributes());
    CallInst *New = cast<CallInst>(CI->clone());
    New->setCalledFunction(IPrintFFn);
    B.Insert(New);
    return New;
  }
  return nullptr;
}

Value *LibCallSimplifier::optimizeSPrintFString(CallInst *CI, IRBuilder<> &B) {
  // Check for a fixed format string.
  StringRef FormatStr;
  if (!getConstantStringInfo(CI->getArgOperand(1), FormatStr))
    return nullptr;

  // If we just have a format string (nothing else crazy) transform it.
  if (CI->getNumArgOperands() == 2) {
    // Make sure there's no % in the constant array.  We could try to handle
    // %% -> % in the future if we cared.
    if (FormatStr.find('%') != StringRef::npos)
      return nullptr; // we found a format specifier, bail out.

    // sprintf(str, fmt) -> llvm.memcpy(align 1 str, align 1 fmt, strlen(fmt)+1)
    B.CreateMemCpy(CI->getArgOperand(0), 1, CI->getArgOperand(1), 1,
                   ConstantInt::get(DL.getIntPtrType(CI->getContext()),
                                    FormatStr.size() + 1)); // Copy the null byte.
    return ConstantInt::get(CI->getType(), FormatStr.size());
  }

  // The remaining optimizations require the format string to be "%s" or "%c"
  // and have an extra operand.
  if (FormatStr.size() != 2 || FormatStr[0] != '%' ||
      CI->getNumArgOperands() < 3)
    return nullptr;

  // Decode the second character of the format string.
  if (FormatStr[1] == 'c') {
    // sprintf(dst, "%c", chr) --> *(i8*)dst = chr; *((i8*)dst+1) = 0
    if (!CI->getArgOperand(2)->getType()->isIntegerTy())
      return nullptr;
    Value *V = B.CreateTrunc(CI->getArgOperand(2), B.getInt8Ty(), "char");
    Value *Ptr = castToCStr(CI->getArgOperand(0), B);
    B.CreateStore(V, Ptr);
    Ptr = B.CreateGEP(B.getInt8Ty(), Ptr, B.getInt32(1), "nul");
    B.CreateStore(B.getInt8(0), Ptr);

    return ConstantInt::get(CI->getType(), 1);
  }

  if (FormatStr[1] == 's') {
    // sprintf(dest, "%s", str) -> llvm.memcpy(dest, str, strlen(str)+1, 1)
    if (!CI->getArgOperand(2)->getType()->isPointerTy())
      return nullptr;

    Value *Len = emitStrLen(CI->getArgOperand(2), B, DL, TLI);
    if (!Len)
      return nullptr;
    Value *IncLen =
        B.CreateAdd(Len, ConstantInt::get(Len->getType(), 1), "leninc");
    B.CreateMemCpy(CI->getArgOperand(0), 1, CI->getArgOperand(2), 1, IncLen);

    // The sprintf result is the unincremented number of bytes in the string.
    return B.CreateIntCast(Len, CI->getType(), false);
  }
  return nullptr;
}

Value *LibCallSimplifier::optimizeSPrintF(CallInst *CI, IRBuilder<> &B) {
  Function *Callee = CI->getCalledFunction();
  FunctionType *FT = Callee->getFunctionType();
  if (Value *V = optimizeSPrintFString(CI, B)) {
    return V;
  }

  // sprintf(str, format, ...) -> siprintf(str, format, ...) if no floating
  // point arguments.
  if (TLI->has(LibFunc_siprintf) && !callHasFloatingPointArgument(CI)) {
    Module *M = B.GetInsertBlock()->getParent()->getParent();
    Constant *SIPrintFFn =
        M->getOrInsertFunction("siprintf", FT, Callee->getAttributes());
    CallInst *New = cast<CallInst>(CI->clone());
    New->setCalledFunction(SIPrintFFn);
    B.Insert(New);
    return New;
  }
  return nullptr;
}

Value *LibCallSimplifier::optimizeSnPrintFString(CallInst *CI, IRBuilder<> &B) {
  // Check for a fixed format string.
  StringRef FormatStr;
  if (!getConstantStringInfo(CI->getArgOperand(2), FormatStr))
    return nullptr;

  // Check for size
  ConstantInt *Size = dyn_cast<ConstantInt>(CI->getArgOperand(1));
  if (!Size)
    return nullptr;

  uint64_t N = Size->getZExtValue();

  // If we just have a format string (nothing else crazy) transform it.
  if (CI->getNumArgOperands() == 3) {
    // Make sure there's no % in the constant array.  We could try to handle
    // %% -> % in the future if we cared.
    if (FormatStr.find('%') != StringRef::npos)
      return nullptr; // we found a format specifier, bail out.

    if (N == 0)
      return ConstantInt::get(CI->getType(), FormatStr.size());
    else if (N < FormatStr.size() + 1)
      return nullptr;

    // sprintf(str, size, fmt) -> llvm.memcpy(align 1 str, align 1 fmt,
    // strlen(fmt)+1)
    B.CreateMemCpy(
        CI->getArgOperand(0), 1, CI->getArgOperand(2), 1,
        ConstantInt::get(DL.getIntPtrType(CI->getContext()),
                         FormatStr.size() + 1)); // Copy the null byte.
    return ConstantInt::get(CI->getType(), FormatStr.size());
  }

  // The remaining optimizations require the format string to be "%s" or "%c"
  // and have an extra operand.
  if (FormatStr.size() == 2 && FormatStr[0] == '%' &&
      CI->getNumArgOperands() == 4) {

    // Decode the second character of the format string.
    if (FormatStr[1] == 'c') {
      if (N == 0)
        return ConstantInt::get(CI->getType(), 1);
      else if (N == 1)
        return nullptr;

      // snprintf(dst, size, "%c", chr) --> *(i8*)dst = chr; *((i8*)dst+1) = 0
      if (!CI->getArgOperand(3)->getType()->isIntegerTy())
        return nullptr;
      Value *V = B.CreateTrunc(CI->getArgOperand(3), B.getInt8Ty(), "char");
      Value *Ptr = castToCStr(CI->getArgOperand(0), B);
      B.CreateStore(V, Ptr);
      Ptr = B.CreateGEP(B.getInt8Ty(), Ptr, B.getInt32(1), "nul");
      B.CreateStore(B.getInt8(0), Ptr);

      return ConstantInt::get(CI->getType(), 1);
    }

    if (FormatStr[1] == 's') {
      // snprintf(dest, size, "%s", str) to llvm.memcpy(dest, str, len+1, 1)
      StringRef Str;
      if (!getConstantStringInfo(CI->getArgOperand(3), Str))
        return nullptr;

      if (N == 0)
        return ConstantInt::get(CI->getType(), Str.size());
      else if (N < Str.size() + 1)
        return nullptr;

      B.CreateMemCpy(CI->getArgOperand(0), 1, CI->getArgOperand(3), 1,
                     ConstantInt::get(CI->getType(), Str.size() + 1));

      // The snprintf result is the unincremented number of bytes in the string.
      return ConstantInt::get(CI->getType(), Str.size());
    }
  }
  return nullptr;
}

Value *LibCallSimplifier::optimizeSnPrintF(CallInst *CI, IRBuilder<> &B) {
  if (Value *V = optimizeSnPrintFString(CI, B)) {
    return V;
  }

  return nullptr;
}

Value *LibCallSimplifier::optimizeFPrintFString(CallInst *CI, IRBuilder<> &B) {
  optimizeErrorReporting(CI, B, 0);

  // All the optimizations depend on the format string.
  StringRef FormatStr;
  if (!getConstantStringInfo(CI->getArgOperand(1), FormatStr))
    return nullptr;

  // Do not do any of the following transformations if the fprintf return
  // value is used, in general the fprintf return value is not compatible
  // with fwrite(), fputc() or fputs().
  if (!CI->use_empty())
    return nullptr;

  // fprintf(F, "foo") --> fwrite("foo", 3, 1, F)
  if (CI->getNumArgOperands() == 2) {
    // Could handle %% -> % if we cared.
    if (FormatStr.find('%') != StringRef::npos)
      return nullptr; // We found a format specifier.

    return emitFWrite(
        CI->getArgOperand(1),
        ConstantInt::get(DL.getIntPtrType(CI->getContext()), FormatStr.size()),
        CI->getArgOperand(0), B, DL, TLI);
  }

  // The remaining optimizations require the format string to be "%s" or "%c"
  // and have an extra operand.
  if (FormatStr.size() != 2 || FormatStr[0] != '%' ||
      CI->getNumArgOperands() < 3)
    return nullptr;

  // Decode the second character of the format string.
  if (FormatStr[1] == 'c') {
    // fprintf(F, "%c", chr) --> fputc(chr, F)
    if (!CI->getArgOperand(2)->getType()->isIntegerTy())
      return nullptr;
    return emitFPutC(CI->getArgOperand(2), CI->getArgOperand(0), B, TLI);
  }

  if (FormatStr[1] == 's') {
    // fprintf(F, "%s", str) --> fputs(str, F)
    if (!CI->getArgOperand(2)->getType()->isPointerTy())
      return nullptr;
    return emitFPutS(CI->getArgOperand(2), CI->getArgOperand(0), B, TLI);
  }
  return nullptr;
}

Value *LibCallSimplifier::optimizeFPrintF(CallInst *CI, IRBuilder<> &B) {
  Function *Callee = CI->getCalledFunction();
  FunctionType *FT = Callee->getFunctionType();
  if (Value *V = optimizeFPrintFString(CI, B)) {
    return V;
  }

  // fprintf(stream, format, ...) -> fiprintf(stream, format, ...) if no
  // floating point arguments.
  if (TLI->has(LibFunc_fiprintf) && !callHasFloatingPointArgument(CI)) {
    Module *M = B.GetInsertBlock()->getParent()->getParent();
    Constant *FIPrintFFn =
        M->getOrInsertFunction("fiprintf", FT, Callee->getAttributes());
    CallInst *New = cast<CallInst>(CI->clone());
    New->setCalledFunction(FIPrintFFn);
    B.Insert(New);
    return New;
  }
  return nullptr;
}

Value *LibCallSimplifier::optimizeFWrite(CallInst *CI, IRBuilder<> &B) {
  optimizeErrorReporting(CI, B, 3);

  // Get the element size and count.
  ConstantInt *SizeC = dyn_cast<ConstantInt>(CI->getArgOperand(1));
  ConstantInt *CountC = dyn_cast<ConstantInt>(CI->getArgOperand(2));
  if (SizeC && CountC) {
    uint64_t Bytes = SizeC->getZExtValue() * CountC->getZExtValue();

    // If this is writing zero records, remove the call (it's a noop).
    if (Bytes == 0)
      return ConstantInt::get(CI->getType(), 0);

    // If this is writing one byte, turn it into fputc.
    // This optimisation is only valid, if the return value is unused.
    if (Bytes == 1 && CI->use_empty()) { // fwrite(S,1,1,F) -> fputc(S[0],F)
      Value *Char = B.CreateLoad(castToCStr(CI->getArgOperand(0), B), "char");
      Value *NewCI = emitFPutC(Char, CI->getArgOperand(3), B, TLI);
      return NewCI ? ConstantInt::get(CI->getType(), 1) : nullptr;
    }
  }

  if (isLocallyOpenedFile(CI->getArgOperand(3), CI, B, TLI))
    return emitFWriteUnlocked(CI->getArgOperand(0), CI->getArgOperand(1),
                              CI->getArgOperand(2), CI->getArgOperand(3), B, DL,
                              TLI);

  return nullptr;
}

Value *LibCallSimplifier::optimizeFPuts(CallInst *CI, IRBuilder<> &B) {
  optimizeErrorReporting(CI, B, 1);

  // Don't rewrite fputs to fwrite when optimising for size because fwrite
  // requires more arguments and thus extra MOVs are required.
  if (CI->getFunction()->optForSize())
    return nullptr;

  // Check if has any use
  if (!CI->use_empty()) {
    if (isLocallyOpenedFile(CI->getArgOperand(1), CI, B, TLI))
      return emitFPutSUnlocked(CI->getArgOperand(0), CI->getArgOperand(1), B,
                               TLI);
    else
      // We can't optimize if return value is used.
      return nullptr;
  }

  // fputs(s,F) --> fwrite(s,1,strlen(s),F)
  uint64_t Len = GetStringLength(CI->getArgOperand(0));
  if (!Len)
    return nullptr;

  // Known to have no uses (see above).
  return emitFWrite(
      CI->getArgOperand(0),
      ConstantInt::get(DL.getIntPtrType(CI->getContext()), Len - 1),
      CI->getArgOperand(1), B, DL, TLI);
}

Value *LibCallSimplifier::optimizeFPutc(CallInst *CI, IRBuilder<> &B) {
  optimizeErrorReporting(CI, B, 1);

  if (isLocallyOpenedFile(CI->getArgOperand(1), CI, B, TLI))
    return emitFPutCUnlocked(CI->getArgOperand(0), CI->getArgOperand(1), B,
                             TLI);

  return nullptr;
}

Value *LibCallSimplifier::optimizeFGetc(CallInst *CI, IRBuilder<> &B) {
  if (isLocallyOpenedFile(CI->getArgOperand(0), CI, B, TLI))
    return emitFGetCUnlocked(CI->getArgOperand(0), B, TLI);

  return nullptr;
}

Value *LibCallSimplifier::optimizeFGets(CallInst *CI, IRBuilder<> &B) {
  if (isLocallyOpenedFile(CI->getArgOperand(2), CI, B, TLI))
    return emitFGetSUnlocked(CI->getArgOperand(0), CI->getArgOperand(1),
                             CI->getArgOperand(2), B, TLI);

  return nullptr;
}

Value *LibCallSimplifier::optimizeFRead(CallInst *CI, IRBuilder<> &B) {
  if (isLocallyOpenedFile(CI->getArgOperand(3), CI, B, TLI))
    return emitFReadUnlocked(CI->getArgOperand(0), CI->getArgOperand(1),
                             CI->getArgOperand(2), CI->getArgOperand(3), B, DL,
                             TLI);

  return nullptr;
}

Value *LibCallSimplifier::optimizePuts(CallInst *CI, IRBuilder<> &B) {
  // Check for a constant string.
  StringRef Str;
  if (!getConstantStringInfo(CI->getArgOperand(0), Str))
    return nullptr;

  if (Str.empty() && CI->use_empty()) {
    // puts("") -> putchar('\n')
    Value *Res = emitPutChar(B.getInt32('\n'), B, TLI);
    if (CI->use_empty() || !Res)
      return Res;
    return B.CreateIntCast(Res, CI->getType(), true);
  }

  return nullptr;
}

bool LibCallSimplifier::hasFloatVersion(StringRef FuncName) {
  LibFunc Func;
  SmallString<20> FloatFuncName = FuncName;
  FloatFuncName += 'f';
  if (TLI->getLibFunc(FloatFuncName, Func))
    return TLI->has(Func);
  return false;
}

Value *LibCallSimplifier::optimizeStringMemoryLibCall(CallInst *CI,
                                                      IRBuilder<> &Builder) {
  LibFunc Func;
  Function *Callee = CI->getCalledFunction();
  // Check for string/memory library functions.
  if (TLI->getLibFunc(*Callee, Func) && TLI->has(Func)) {
    // Make sure we never change the calling convention.
    assert((ignoreCallingConv(Func) ||
            isCallingConvCCompatible(CI)) &&
      "Optimizing string/memory libcall would change the calling convention");
    switch (Func) {
    case LibFunc_strcat:
      return optimizeStrCat(CI, Builder);
    case LibFunc_strncat:
      return optimizeStrNCat(CI, Builder);
    case LibFunc_strchr:
      return optimizeStrChr(CI, Builder);
    case LibFunc_strrchr:
      return optimizeStrRChr(CI, Builder);
    case LibFunc_strcmp:
      return optimizeStrCmp(CI, Builder);
    case LibFunc_strncmp:
      return optimizeStrNCmp(CI, Builder);
    case LibFunc_strcpy:
      return optimizeStrCpy(CI, Builder);
    case LibFunc_stpcpy:
      return optimizeStpCpy(CI, Builder);
    case LibFunc_strncpy:
      return optimizeStrNCpy(CI, Builder);
    case LibFunc_strlen:
      return optimizeStrLen(CI, Builder);
    case LibFunc_strpbrk:
      return optimizeStrPBrk(CI, Builder);
    case LibFunc_strtol:
    case LibFunc_strtod:
    case LibFunc_strtof:
    case LibFunc_strtoul:
    case LibFunc_strtoll:
    case LibFunc_strtold:
    case LibFunc_strtoull:
      return optimizeStrTo(CI, Builder);
    case LibFunc_strspn:
      return optimizeStrSpn(CI, Builder);
    case LibFunc_strcspn:
      return optimizeStrCSpn(CI, Builder);
    case LibFunc_strstr:
      return optimizeStrStr(CI, Builder);
    case LibFunc_memchr:
      return optimizeMemChr(CI, Builder);
    case LibFunc_memcmp:
      return optimizeMemCmp(CI, Builder);
    case LibFunc_memcpy:
      return optimizeMemCpy(CI, Builder);
    case LibFunc_memmove:
      return optimizeMemMove(CI, Builder);
    case LibFunc_memset:
      return optimizeMemSet(CI, Builder);
    case LibFunc_realloc:
      return optimizeRealloc(CI, Builder);
    case LibFunc_wcslen:
      return optimizeWcslen(CI, Builder);
    default:
      break;
    }
  }
  return nullptr;
}

Value *LibCallSimplifier::optimizeFloatingPointLibCall(CallInst *CI,
                                                       LibFunc Func,
                                                       IRBuilder<> &Builder) {
  // Don't optimize calls that require strict floating point semantics.
  if (CI->isStrictFP())
    return nullptr;

  if (Value *V = optimizeTrigReflections(CI, Func, Builder))
    return V;

  switch (Func) {
  case LibFunc_sinpif:
  case LibFunc_sinpi:
  case LibFunc_cospif:
  case LibFunc_cospi:
    return optimizeSinCosPi(CI, Builder);
  case LibFunc_powf:
  case LibFunc_pow:
  case LibFunc_powl:
    return optimizePow(CI, Builder);
  case LibFunc_exp2l:
  case LibFunc_exp2:
  case LibFunc_exp2f:
    return optimizeExp2(CI, Builder);
  case LibFunc_fabsf:
  case LibFunc_fabs:
  case LibFunc_fabsl:
    return replaceUnaryCall(CI, Builder, Intrinsic::fabs);
  case LibFunc_sqrtf:
  case LibFunc_sqrt:
  case LibFunc_sqrtl:
    return optimizeSqrt(CI, Builder);
  case LibFunc_log:
  case LibFunc_log10:
  case LibFunc_log1p:
  case LibFunc_log2:
  case LibFunc_logb:
    return optimizeLog(CI, Builder);
  case LibFunc_tan:
  case LibFunc_tanf:
  case LibFunc_tanl:
    return optimizeTan(CI, Builder);
  case LibFunc_ceil:
    return replaceUnaryCall(CI, Builder, Intrinsic::ceil);
  case LibFunc_floor:
    return replaceUnaryCall(CI, Builder, Intrinsic::floor);
  case LibFunc_round:
    return replaceUnaryCall(CI, Builder, Intrinsic::round);
  case LibFunc_nearbyint:
    return replaceUnaryCall(CI, Builder, Intrinsic::nearbyint);
  case LibFunc_rint:
    return replaceUnaryCall(CI, Builder, Intrinsic::rint);
  case LibFunc_trunc:
    return replaceUnaryCall(CI, Builder, Intrinsic::trunc);
  case LibFunc_acos:
  case LibFunc_acosh:
  case LibFunc_asin:
  case LibFunc_asinh:
  case LibFunc_atan:
  case LibFunc_atanh:
  case LibFunc_cbrt:
  case LibFunc_cosh:
  case LibFunc_exp:
  case LibFunc_exp10:
  case LibFunc_expm1:
  case LibFunc_cos:
  case LibFunc_sin:
  case LibFunc_sinh:
  case LibFunc_tanh:
    if (UnsafeFPShrink && hasFloatVersion(CI->getCalledFunction()->getName()))
      return optimizeUnaryDoubleFP(CI, Builder, true);
    return nullptr;
  case LibFunc_copysign:
    if (hasFloatVersion(CI->getCalledFunction()->getName()))
      return optimizeBinaryDoubleFP(CI, Builder);
    return nullptr;
  case LibFunc_fminf:
  case LibFunc_fmin:
  case LibFunc_fminl:
  case LibFunc_fmaxf:
  case LibFunc_fmax:
  case LibFunc_fmaxl:
    return optimizeFMinFMax(CI, Builder);
  case LibFunc_cabs:
  case LibFunc_cabsf:
  case LibFunc_cabsl:
    return optimizeCAbs(CI, Builder);
  default:
    return nullptr;
  }
}

Value *LibCallSimplifier::optimizeCall(CallInst *CI) {
  // TODO: Split out the code below that operates on FP calls so that
  //       we can all non-FP calls with the StrictFP attribute to be
  //       optimized.
  if (CI->isNoBuiltin())
    return nullptr;

  LibFunc Func;
  Function *Callee = CI->getCalledFunction();

  SmallVector<OperandBundleDef, 2> OpBundles;
  CI->getOperandBundlesAsDefs(OpBundles);
  IRBuilder<> Builder(CI, /*FPMathTag=*/nullptr, OpBundles);
  bool isCallingConvC = isCallingConvCCompatible(CI);

  // Command-line parameter overrides instruction attribute.
  // This can't be moved to optimizeFloatingPointLibCall() because it may be
  // used by the intrinsic optimizations.
  if (EnableUnsafeFPShrink.getNumOccurrences() > 0)
    UnsafeFPShrink = EnableUnsafeFPShrink;
  else if (isa<FPMathOperator>(CI) && CI->isFast())
    UnsafeFPShrink = true;

  // First, check for intrinsics.
  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(CI)) {
    if (!isCallingConvC)
      return nullptr;
    // The FP intrinsics have corresponding constrained versions so we don't
    // need to check for the StrictFP attribute here.
    switch (II->getIntrinsicID()) {
    case Intrinsic::pow:
      return optimizePow(CI, Builder);
    case Intrinsic::exp2:
      return optimizeExp2(CI, Builder);
    case Intrinsic::log:
      return optimizeLog(CI, Builder);
    case Intrinsic::sqrt:
      return optimizeSqrt(CI, Builder);
    // TODO: Use foldMallocMemset() with memset intrinsic.
    default:
      return nullptr;
    }
  }

  // Also try to simplify calls to fortified library functions.
  if (Value *SimplifiedFortifiedCI = FortifiedSimplifier.optimizeCall(CI)) {
    // Try to further simplify the result.
    CallInst *SimplifiedCI = dyn_cast<CallInst>(SimplifiedFortifiedCI);
    if (SimplifiedCI && SimplifiedCI->getCalledFunction()) {
      // Use an IR Builder from SimplifiedCI if available instead of CI
      // to guarantee we reach all uses we might replace later on.
      IRBuilder<> TmpBuilder(SimplifiedCI);
      if (Value *V = optimizeStringMemoryLibCall(SimplifiedCI, TmpBuilder)) {
        // If we were able to further simplify, remove the now redundant call.
        SimplifiedCI->replaceAllUsesWith(V);
        eraseFromParent(SimplifiedCI);
        return V;
      }
    }
    return SimplifiedFortifiedCI;
  }

  // Then check for known library functions.
  if (TLI->getLibFunc(*Callee, Func) && TLI->has(Func)) {
    // We never change the calling convention.
    if (!ignoreCallingConv(Func) && !isCallingConvC)
      return nullptr;
    if (Value *V = optimizeStringMemoryLibCall(CI, Builder))
      return V;
    if (Value *V = optimizeFloatingPointLibCall(CI, Func, Builder))
      return V;
    switch (Func) {
    case LibFunc_ffs:
    case LibFunc_ffsl:
    case LibFunc_ffsll:
      return optimizeFFS(CI, Builder);
    case LibFunc_fls:
    case LibFunc_flsl:
    case LibFunc_flsll:
      return optimizeFls(CI, Builder);
    case LibFunc_abs:
    case LibFunc_labs:
    case LibFunc_llabs:
      return optimizeAbs(CI, Builder);
    case LibFunc_isdigit:
      return optimizeIsDigit(CI, Builder);
    case LibFunc_isascii:
      return optimizeIsAscii(CI, Builder);
    case LibFunc_toascii:
      return optimizeToAscii(CI, Builder);
    case LibFunc_atoi:
    case LibFunc_atol:
    case LibFunc_atoll:
      return optimizeAtoi(CI, Builder);
    case LibFunc_strtol:
    case LibFunc_strtoll:
      return optimizeStrtol(CI, Builder);
    case LibFunc_printf:
      return optimizePrintF(CI, Builder);
    case LibFunc_sprintf:
      return optimizeSPrintF(CI, Builder);
    case LibFunc_snprintf:
      return optimizeSnPrintF(CI, Builder);
    case LibFunc_fprintf:
      return optimizeFPrintF(CI, Builder);
    case LibFunc_fwrite:
      return optimizeFWrite(CI, Builder);
    case LibFunc_fread:
      return optimizeFRead(CI, Builder);
    case LibFunc_fputs:
      return optimizeFPuts(CI, Builder);
    case LibFunc_fgets:
      return optimizeFGets(CI, Builder);
    case LibFunc_fputc:
      return optimizeFPutc(CI, Builder);
    case LibFunc_fgetc:
      return optimizeFGetc(CI, Builder);
    case LibFunc_puts:
      return optimizePuts(CI, Builder);
    case LibFunc_perror:
      return optimizeErrorReporting(CI, Builder);
    case LibFunc_vfprintf:
    case LibFunc_fiprintf:
      return optimizeErrorReporting(CI, Builder, 0);
    default:
      return nullptr;
    }
  }
  return nullptr;
}

LibCallSimplifier::LibCallSimplifier(
    const DataLayout &DL, const TargetLibraryInfo *TLI,
    OptimizationRemarkEmitter &ORE,
    function_ref<void(Instruction *, Value *)> Replacer,
    function_ref<void(Instruction *)> Eraser)
    : FortifiedSimplifier(TLI), DL(DL), TLI(TLI), ORE(ORE),
      UnsafeFPShrink(false), Replacer(Replacer), Eraser(Eraser) {}

void LibCallSimplifier::replaceAllUsesWith(Instruction *I, Value *With) {
  // Indirect through the replacer used in this instance.
  Replacer(I, With);
}

void LibCallSimplifier::eraseFromParent(Instruction *I) {
  Eraser(I);
}

// TODO:
//   Additional cases that we need to add to this file:
//
// cbrt:
//   * cbrt(expN(X))  -> expN(x/3)
//   * cbrt(sqrt(x))  -> pow(x,1/6)
//   * cbrt(cbrt(x))  -> pow(x,1/9)
//
// exp, expf, expl:
//   * exp(log(x))  -> x
//
// log, logf, logl:
//   * log(exp(x))   -> x
//   * log(exp(y))   -> y*log(e)
//   * log(exp10(y)) -> y*log(10)
//   * log(sqrt(x))  -> 0.5*log(x)
//
// pow, powf, powl:
//   * pow(sqrt(x),y) -> pow(x,y*0.5)
//   * pow(pow(x,y),z)-> pow(x,y*z)
//
// signbit:
//   * signbit(cnst) -> cnst'
//   * signbit(nncst) -> 0 (if pstv is a non-negative constant)
//
// sqrt, sqrtf, sqrtl:
//   * sqrt(expN(x))  -> expN(x*0.5)
//   * sqrt(Nroot(x)) -> pow(x,1/(2*N))
//   * sqrt(pow(x,y)) -> pow(|x|,y*0.5)
//

//===----------------------------------------------------------------------===//
// Fortified Library Call Optimizations
//===----------------------------------------------------------------------===//

bool FortifiedLibCallSimplifier::isFortifiedCallFoldable(CallInst *CI,
                                                         unsigned ObjSizeOp,
                                                         unsigned SizeOp,
                                                         bool isString) {
  if (CI->getArgOperand(ObjSizeOp) == CI->getArgOperand(SizeOp))
    return true;
  if (ConstantInt *ObjSizeCI =
          dyn_cast<ConstantInt>(CI->getArgOperand(ObjSizeOp))) {
    if (ObjSizeCI->isMinusOne())
      return true;
    // If the object size wasn't -1 (unknown), bail out if we were asked to.
    if (OnlyLowerUnknownSize)
      return false;
    if (isString) {
      uint64_t Len = GetStringLength(CI->getArgOperand(SizeOp));
      // If the length is 0 we don't know how long it is and so we can't
      // remove the check.
      if (Len == 0)
        return false;
      return ObjSizeCI->getZExtValue() >= Len;
    }
    if (ConstantInt *SizeCI = dyn_cast<ConstantInt>(CI->getArgOperand(SizeOp)))
      return ObjSizeCI->getZExtValue() >= SizeCI->getZExtValue();
  }
  return false;
}

Value *FortifiedLibCallSimplifier::optimizeMemCpyChk(CallInst *CI,
                                                     IRBuilder<> &B) {
  if (isFortifiedCallFoldable(CI, 3, 2, false)) {
    B.CreateMemCpy(CI->getArgOperand(0), 1, CI->getArgOperand(1), 1,
                   CI->getArgOperand(2));
    return CI->getArgOperand(0);
  }
  return nullptr;
}

Value *FortifiedLibCallSimplifier::optimizeMemMoveChk(CallInst *CI,
                                                      IRBuilder<> &B) {
  if (isFortifiedCallFoldable(CI, 3, 2, false)) {
    B.CreateMemMove(CI->getArgOperand(0), 1, CI->getArgOperand(1), 1,
                    CI->getArgOperand(2));
    return CI->getArgOperand(0);
  }
  return nullptr;
}

Value *FortifiedLibCallSimplifier::optimizeMemSetChk(CallInst *CI,
                                                     IRBuilder<> &B) {
  // TODO: Try foldMallocMemset() here.

  if (isFortifiedCallFoldable(CI, 3, 2, false)) {
    Value *Val = B.CreateIntCast(CI->getArgOperand(1), B.getInt8Ty(), false);
    B.CreateMemSet(CI->getArgOperand(0), Val, CI->getArgOperand(2), 1);
    return CI->getArgOperand(0);
  }
  return nullptr;
}

Value *FortifiedLibCallSimplifier::optimizeStrpCpyChk(CallInst *CI,
                                                      IRBuilder<> &B,
                                                      LibFunc Func) {
  Function *Callee = CI->getCalledFunction();
  StringRef Name = Callee->getName();
  const DataLayout &DL = CI->getModule()->getDataLayout();
  Value *Dst = CI->getArgOperand(0), *Src = CI->getArgOperand(1),
        *ObjSize = CI->getArgOperand(2);

  // __stpcpy_chk(x,x,...)  -> x+strlen(x)
  if (Func == LibFunc_stpcpy_chk && !OnlyLowerUnknownSize && Dst == Src) {
    Value *StrLen = emitStrLen(Src, B, DL, TLI);
    return StrLen ? B.CreateInBoundsGEP(B.getInt8Ty(), Dst, StrLen) : nullptr;
  }

  // If a) we don't have any length information, or b) we know this will
  // fit then just lower to a plain st[rp]cpy. Otherwise we'll keep our
  // st[rp]cpy_chk call which may fail at runtime if the size is too long.
  // TODO: It might be nice to get a maximum length out of the possible
  // string lengths for varying.
  if (isFortifiedCallFoldable(CI, 2, 1, true))
    return emitStrCpy(Dst, Src, B, TLI, Name.substr(2, 6));

  if (OnlyLowerUnknownSize)
    return nullptr;

  // Maybe we can stil fold __st[rp]cpy_chk to __memcpy_chk.
  uint64_t Len = GetStringLength(Src);
  if (Len == 0)
    return nullptr;

  Type *SizeTTy = DL.getIntPtrType(CI->getContext());
  Value *LenV = ConstantInt::get(SizeTTy, Len);
  Value *Ret = emitMemCpyChk(Dst, Src, LenV, ObjSize, B, DL, TLI);
  // If the function was an __stpcpy_chk, and we were able to fold it into
  // a __memcpy_chk, we still need to return the correct end pointer.
  if (Ret && Func == LibFunc_stpcpy_chk)
    return B.CreateGEP(B.getInt8Ty(), Dst, ConstantInt::get(SizeTTy, Len - 1));
  return Ret;
}

Value *FortifiedLibCallSimplifier::optimizeStrpNCpyChk(CallInst *CI,
                                                       IRBuilder<> &B,
                                                       LibFunc Func) {
  Function *Callee = CI->getCalledFunction();
  StringRef Name = Callee->getName();
  if (isFortifiedCallFoldable(CI, 3, 2, false)) {
    Value *Ret = emitStrNCpy(CI->getArgOperand(0), CI->getArgOperand(1),
                             CI->getArgOperand(2), B, TLI, Name.substr(2, 7));
    return Ret;
  }
  return nullptr;
}

Value *FortifiedLibCallSimplifier::optimizeCall(CallInst *CI) {
  // FIXME: We shouldn't be changing "nobuiltin" or TLI unavailable calls here.
  // Some clang users checked for _chk libcall availability using:
  //   __has_builtin(__builtin___memcpy_chk)
  // When compiling with -fno-builtin, this is always true.
  // When passing -ffreestanding/-mkernel, which both imply -fno-builtin, we
  // end up with fortified libcalls, which isn't acceptable in a freestanding
  // environment which only provides their non-fortified counterparts.
  //
  // Until we change clang and/or teach external users to check for availability
  // differently, disregard the "nobuiltin" attribute and TLI::has.
  //
  // PR23093.

  LibFunc Func;
  Function *Callee = CI->getCalledFunction();

  SmallVector<OperandBundleDef, 2> OpBundles;
  CI->getOperandBundlesAsDefs(OpBundles);
  IRBuilder<> Builder(CI, /*FPMathTag=*/nullptr, OpBundles);
  bool isCallingConvC = isCallingConvCCompatible(CI);

  // First, check that this is a known library functions and that the prototype
  // is correct.
  if (!TLI->getLibFunc(*Callee, Func))
    return nullptr;

  // We never change the calling convention.
  if (!ignoreCallingConv(Func) && !isCallingConvC)
    return nullptr;

  switch (Func) {
  case LibFunc_memcpy_chk:
    return optimizeMemCpyChk(CI, Builder);
  case LibFunc_memmove_chk:
    return optimizeMemMoveChk(CI, Builder);
  case LibFunc_memset_chk:
    return optimizeMemSetChk(CI, Builder);
  case LibFunc_stpcpy_chk:
  case LibFunc_strcpy_chk:
    return optimizeStrpCpyChk(CI, Builder, Func);
  case LibFunc_stpncpy_chk:
  case LibFunc_strncpy_chk:
    return optimizeStrpNCpyChk(CI, Builder, Func);
  default:
    break;
  }
  return nullptr;
}

FortifiedLibCallSimplifier::FortifiedLibCallSimplifier(
    const TargetLibraryInfo *TLI, bool OnlyLowerUnknownSize)
    : TLI(TLI), OnlyLowerUnknownSize(OnlyLowerUnknownSize) {}
