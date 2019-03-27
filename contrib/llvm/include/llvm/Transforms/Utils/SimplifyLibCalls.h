//===- SimplifyLibCalls.h - Library call simplifier -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file exposes an interface to build some C language libcalls for
// optimization passes that need to call the various functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_SIMPLIFYLIBCALLS_H
#define LLVM_TRANSFORMS_UTILS_SIMPLIFYLIBCALLS_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/IRBuilder.h"

namespace llvm {
class StringRef;
class Value;
class CallInst;
class DataLayout;
class Instruction;
class TargetLibraryInfo;
class BasicBlock;
class Function;
class OptimizationRemarkEmitter;

/// This class implements simplifications for calls to fortified library
/// functions (__st*cpy_chk, __memcpy_chk, __memmove_chk, __memset_chk), to,
/// when possible, replace them with their non-checking counterparts.
/// Other optimizations can also be done, but it's possible to disable them and
/// only simplify needless use of the checking versions (when the object size
/// is unknown) by passing true for OnlyLowerUnknownSize.
class FortifiedLibCallSimplifier {
private:
  const TargetLibraryInfo *TLI;
  bool OnlyLowerUnknownSize;

public:
  FortifiedLibCallSimplifier(const TargetLibraryInfo *TLI,
                             bool OnlyLowerUnknownSize = false);

  /// Take the given call instruction and return a more
  /// optimal value to replace the instruction with or 0 if a more
  /// optimal form can't be found.
  /// The call must not be an indirect call.
  Value *optimizeCall(CallInst *CI);

private:
  Value *optimizeMemCpyChk(CallInst *CI, IRBuilder<> &B);
  Value *optimizeMemMoveChk(CallInst *CI, IRBuilder<> &B);
  Value *optimizeMemSetChk(CallInst *CI, IRBuilder<> &B);

  // Str/Stp cpy are similar enough to be handled in the same functions.
  Value *optimizeStrpCpyChk(CallInst *CI, IRBuilder<> &B, LibFunc Func);
  Value *optimizeStrpNCpyChk(CallInst *CI, IRBuilder<> &B, LibFunc Func);

  /// Checks whether the call \p CI to a fortified libcall is foldable
  /// to the non-fortified version.
  bool isFortifiedCallFoldable(CallInst *CI, unsigned ObjSizeOp,
                               unsigned SizeOp, bool isString);
};

/// LibCallSimplifier - This class implements a collection of optimizations
/// that replace well formed calls to library functions with a more optimal
/// form.  For example, replacing 'printf("Hello!")' with 'puts("Hello!")'.
class LibCallSimplifier {
private:
  FortifiedLibCallSimplifier FortifiedSimplifier;
  const DataLayout &DL;
  const TargetLibraryInfo *TLI;
  OptimizationRemarkEmitter &ORE;
  bool UnsafeFPShrink;
  function_ref<void(Instruction *, Value *)> Replacer;
  function_ref<void(Instruction *)> Eraser;

  /// Internal wrapper for RAUW that is the default implementation.
  ///
  /// Other users may provide an alternate function with this signature instead
  /// of this one.
  static void replaceAllUsesWithDefault(Instruction *I, Value *With) {
    I->replaceAllUsesWith(With);
  }

  /// Internal wrapper for eraseFromParent that is the default implementation.
  static void eraseFromParentDefault(Instruction *I) { I->eraseFromParent(); }

  /// Replace an instruction's uses with a value using our replacer.
  void replaceAllUsesWith(Instruction *I, Value *With);

  /// Erase an instruction from its parent with our eraser.
  void eraseFromParent(Instruction *I);

  Value *foldMallocMemset(CallInst *Memset, IRBuilder<> &B);

public:
  LibCallSimplifier(
      const DataLayout &DL, const TargetLibraryInfo *TLI,
      OptimizationRemarkEmitter &ORE,
      function_ref<void(Instruction *, Value *)> Replacer =
          &replaceAllUsesWithDefault,
      function_ref<void(Instruction *)> Eraser = &eraseFromParentDefault);

  /// optimizeCall - Take the given call instruction and return a more
  /// optimal value to replace the instruction with or 0 if a more
  /// optimal form can't be found.  Note that the returned value may
  /// be equal to the instruction being optimized.  In this case all
  /// other instructions that use the given instruction were modified
  /// and the given instruction is dead.
  /// The call must not be an indirect call.
  Value *optimizeCall(CallInst *CI);

private:
  // String and Memory Library Call Optimizations
  Value *optimizeStrCat(CallInst *CI, IRBuilder<> &B);
  Value *optimizeStrNCat(CallInst *CI, IRBuilder<> &B);
  Value *optimizeStrChr(CallInst *CI, IRBuilder<> &B);
  Value *optimizeStrRChr(CallInst *CI, IRBuilder<> &B);
  Value *optimizeStrCmp(CallInst *CI, IRBuilder<> &B);
  Value *optimizeStrNCmp(CallInst *CI, IRBuilder<> &B);
  Value *optimizeStrCpy(CallInst *CI, IRBuilder<> &B);
  Value *optimizeStpCpy(CallInst *CI, IRBuilder<> &B);
  Value *optimizeStrNCpy(CallInst *CI, IRBuilder<> &B);
  Value *optimizeStrLen(CallInst *CI, IRBuilder<> &B);
  Value *optimizeStrPBrk(CallInst *CI, IRBuilder<> &B);
  Value *optimizeStrTo(CallInst *CI, IRBuilder<> &B);
  Value *optimizeStrSpn(CallInst *CI, IRBuilder<> &B);
  Value *optimizeStrCSpn(CallInst *CI, IRBuilder<> &B);
  Value *optimizeStrStr(CallInst *CI, IRBuilder<> &B);
  Value *optimizeMemChr(CallInst *CI, IRBuilder<> &B);
  Value *optimizeMemCmp(CallInst *CI, IRBuilder<> &B);
  Value *optimizeMemCpy(CallInst *CI, IRBuilder<> &B);
  Value *optimizeMemMove(CallInst *CI, IRBuilder<> &B);
  Value *optimizeMemSet(CallInst *CI, IRBuilder<> &B);
  Value *optimizeRealloc(CallInst *CI, IRBuilder<> &B);
  Value *optimizeWcslen(CallInst *CI, IRBuilder<> &B);
  // Wrapper for all String/Memory Library Call Optimizations
  Value *optimizeStringMemoryLibCall(CallInst *CI, IRBuilder<> &B);

  // Math Library Optimizations
  Value *optimizeCAbs(CallInst *CI, IRBuilder<> &B);
  Value *optimizePow(CallInst *CI, IRBuilder<> &B);
  Value *replacePowWithExp(CallInst *Pow, IRBuilder<> &B);
  Value *replacePowWithSqrt(CallInst *Pow, IRBuilder<> &B);
  Value *optimizeExp2(CallInst *CI, IRBuilder<> &B);
  Value *optimizeFMinFMax(CallInst *CI, IRBuilder<> &B);
  Value *optimizeLog(CallInst *CI, IRBuilder<> &B);
  Value *optimizeSqrt(CallInst *CI, IRBuilder<> &B);
  Value *optimizeSinCosPi(CallInst *CI, IRBuilder<> &B);
  Value *optimizeTan(CallInst *CI, IRBuilder<> &B);
  // Wrapper for all floating point library call optimizations
  Value *optimizeFloatingPointLibCall(CallInst *CI, LibFunc Func,
                                      IRBuilder<> &B);

  // Integer Library Call Optimizations
  Value *optimizeFFS(CallInst *CI, IRBuilder<> &B);
  Value *optimizeFls(CallInst *CI, IRBuilder<> &B);
  Value *optimizeAbs(CallInst *CI, IRBuilder<> &B);
  Value *optimizeIsDigit(CallInst *CI, IRBuilder<> &B);
  Value *optimizeIsAscii(CallInst *CI, IRBuilder<> &B);
  Value *optimizeToAscii(CallInst *CI, IRBuilder<> &B);
  Value *optimizeAtoi(CallInst *CI, IRBuilder<> &B);
  Value *optimizeStrtol(CallInst *CI, IRBuilder<> &B);

  // Formatting and IO Library Call Optimizations
  Value *optimizeErrorReporting(CallInst *CI, IRBuilder<> &B,
                                int StreamArg = -1);
  Value *optimizePrintF(CallInst *CI, IRBuilder<> &B);
  Value *optimizeSPrintF(CallInst *CI, IRBuilder<> &B);
  Value *optimizeSnPrintF(CallInst *CI, IRBuilder<> &B);
  Value *optimizeFPrintF(CallInst *CI, IRBuilder<> &B);
  Value *optimizeFWrite(CallInst *CI, IRBuilder<> &B);
  Value *optimizeFRead(CallInst *CI, IRBuilder<> &B);
  Value *optimizeFPuts(CallInst *CI, IRBuilder<> &B);
  Value *optimizeFGets(CallInst *CI, IRBuilder<> &B);
  Value *optimizeFPutc(CallInst *CI, IRBuilder<> &B);
  Value *optimizeFGetc(CallInst *CI, IRBuilder<> &B);
  Value *optimizePuts(CallInst *CI, IRBuilder<> &B);

  // Helper methods
  Value *emitStrLenMemCpy(Value *Src, Value *Dst, uint64_t Len, IRBuilder<> &B);
  void classifyArgUse(Value *Val, Function *F, bool IsFloat,
                      SmallVectorImpl<CallInst *> &SinCalls,
                      SmallVectorImpl<CallInst *> &CosCalls,
                      SmallVectorImpl<CallInst *> &SinCosCalls);
  Value *optimizePrintFString(CallInst *CI, IRBuilder<> &B);
  Value *optimizeSPrintFString(CallInst *CI, IRBuilder<> &B);
  Value *optimizeSnPrintFString(CallInst *CI, IRBuilder<> &B);
  Value *optimizeFPrintFString(CallInst *CI, IRBuilder<> &B);

  /// hasFloatVersion - Checks if there is a float version of the specified
  /// function by checking for an existing function with name FuncName + f
  bool hasFloatVersion(StringRef FuncName);

  /// Shared code to optimize strlen+wcslen.
  Value *optimizeStringLength(CallInst *CI, IRBuilder<> &B, unsigned CharSize);
};
} // End llvm namespace

#endif
