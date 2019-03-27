//===- BuildLibCalls.h - Utility builder for libcalls -----------*- C++ -*-===//
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

#ifndef LLVM_TRANSFORMS_UTILS_BUILDLIBCALLS_H
#define LLVM_TRANSFORMS_UTILS_BUILDLIBCALLS_H

#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/IRBuilder.h"

namespace llvm {
  class Value;
  class DataLayout;
  class TargetLibraryInfo;

  /// Analyze the name and prototype of the given function and set any
  /// applicable attributes.
  /// If the library function is unavailable, this doesn't modify it.
  ///
  /// Returns true if any attributes were set and false otherwise.
  bool inferLibFuncAttributes(Function &F, const TargetLibraryInfo &TLI);
  bool inferLibFuncAttributes(Module *M, StringRef Name, const TargetLibraryInfo &TLI);

  /// Check whether the overloaded unary floating point function
  /// corresponding to \a Ty is available.
  bool hasUnaryFloatFn(const TargetLibraryInfo *TLI, Type *Ty,
                       LibFunc DoubleFn, LibFunc FloatFn,
                       LibFunc LongDoubleFn);

  /// Get the name of the overloaded unary floating point function
  /// corresponding to \a Ty.
  StringRef getUnaryFloatFn(const TargetLibraryInfo *TLI, Type *Ty,
                            LibFunc DoubleFn, LibFunc FloatFn,
                            LibFunc LongDoubleFn);

  /// Return V if it is an i8*, otherwise cast it to i8*.
  Value *castToCStr(Value *V, IRBuilder<> &B);

  /// Emit a call to the strlen function to the builder, for the specified
  /// pointer. Ptr is required to be some pointer type, and the return value has
  /// 'intptr_t' type.
  Value *emitStrLen(Value *Ptr, IRBuilder<> &B, const DataLayout &DL,
                    const TargetLibraryInfo *TLI);

  /// Emit a call to the strnlen function to the builder, for the specified
  /// pointer. Ptr is required to be some pointer type, MaxLen must be of size_t
  /// type, and the return value has 'intptr_t' type.
  Value *emitStrNLen(Value *Ptr, Value *MaxLen, IRBuilder<> &B,
                     const DataLayout &DL, const TargetLibraryInfo *TLI);

  /// Emit a call to the strchr function to the builder, for the specified
  /// pointer and character. Ptr is required to be some pointer type, and the
  /// return value has 'i8*' type.
  Value *emitStrChr(Value *Ptr, char C, IRBuilder<> &B,
                    const TargetLibraryInfo *TLI);

  /// Emit a call to the strncmp function to the builder.
  Value *emitStrNCmp(Value *Ptr1, Value *Ptr2, Value *Len, IRBuilder<> &B,
                     const DataLayout &DL, const TargetLibraryInfo *TLI);

  /// Emit a call to the strcpy function to the builder, for the specified
  /// pointer arguments.
  Value *emitStrCpy(Value *Dst, Value *Src, IRBuilder<> &B,
                    const TargetLibraryInfo *TLI, StringRef Name = "strcpy");

  /// Emit a call to the strncpy function to the builder, for the specified
  /// pointer arguments and length.
  Value *emitStrNCpy(Value *Dst, Value *Src, Value *Len, IRBuilder<> &B,
                     const TargetLibraryInfo *TLI, StringRef Name = "strncpy");

  /// Emit a call to the __memcpy_chk function to the builder. This expects that
  /// the Len and ObjSize have type 'intptr_t' and Dst/Src are pointers.
  Value *emitMemCpyChk(Value *Dst, Value *Src, Value *Len, Value *ObjSize,
                       IRBuilder<> &B, const DataLayout &DL,
                       const TargetLibraryInfo *TLI);

  /// Emit a call to the memchr function. This assumes that Ptr is a pointer,
  /// Val is an i32 value, and Len is an 'intptr_t' value.
  Value *emitMemChr(Value *Ptr, Value *Val, Value *Len, IRBuilder<> &B,
                    const DataLayout &DL, const TargetLibraryInfo *TLI);

  /// Emit a call to the memcmp function.
  Value *emitMemCmp(Value *Ptr1, Value *Ptr2, Value *Len, IRBuilder<> &B,
                    const DataLayout &DL, const TargetLibraryInfo *TLI);

  /// Emit a call to the unary function named 'Name' (e.g.  'floor'). This
  /// function is known to take a single of type matching 'Op' and returns one
  /// value with the same type. If 'Op' is a long double, 'l' is added as the
  /// suffix of name, if 'Op' is a float, we add a 'f' suffix.
  Value *emitUnaryFloatFnCall(Value *Op, StringRef Name, IRBuilder<> &B,
                              const AttributeList &Attrs);

  /// Emit a call to the unary function DoubleFn, FloatFn or LongDoubleFn,
  /// depending of the type of Op.
  Value *emitUnaryFloatFnCall(Value *Op, const TargetLibraryInfo *TLI,
                              LibFunc DoubleFn, LibFunc FloatFn,
                              LibFunc LongDoubleFn, IRBuilder<> &B,
                              const AttributeList &Attrs);

  /// Emit a call to the binary function named 'Name' (e.g. 'fmin'). This
  /// function is known to take type matching 'Op1' and 'Op2' and return one
  /// value with the same type. If 'Op1/Op2' are long double, 'l' is added as
  /// the suffix of name, if 'Op1/Op2' are float, we add a 'f' suffix.
  Value *emitBinaryFloatFnCall(Value *Op1, Value *Op2, StringRef Name,
                               IRBuilder<> &B, const AttributeList &Attrs);

  /// Emit a call to the putchar function. This assumes that Char is an integer.
  Value *emitPutChar(Value *Char, IRBuilder<> &B, const TargetLibraryInfo *TLI);

  /// Emit a call to the puts function. This assumes that Str is some pointer.
  Value *emitPutS(Value *Str, IRBuilder<> &B, const TargetLibraryInfo *TLI);

  /// Emit a call to the fputc function. This assumes that Char is an i32, and
  /// File is a pointer to FILE.
  Value *emitFPutC(Value *Char, Value *File, IRBuilder<> &B,
                   const TargetLibraryInfo *TLI);

  /// Emit a call to the fputc_unlocked function. This assumes that Char is an
  /// i32, and File is a pointer to FILE.
  Value *emitFPutCUnlocked(Value *Char, Value *File, IRBuilder<> &B,
                           const TargetLibraryInfo *TLI);

  /// Emit a call to the fputs function. Str is required to be a pointer and
  /// File is a pointer to FILE.
  Value *emitFPutS(Value *Str, Value *File, IRBuilder<> &B,
                   const TargetLibraryInfo *TLI);

  /// Emit a call to the fputs_unlocked function. Str is required to be a
  /// pointer and File is a pointer to FILE.
  Value *emitFPutSUnlocked(Value *Str, Value *File, IRBuilder<> &B,
                           const TargetLibraryInfo *TLI);

  /// Emit a call to the fwrite function. This assumes that Ptr is a pointer,
  /// Size is an 'intptr_t', and File is a pointer to FILE.
  Value *emitFWrite(Value *Ptr, Value *Size, Value *File, IRBuilder<> &B,
                    const DataLayout &DL, const TargetLibraryInfo *TLI);

  /// Emit a call to the malloc function.
  Value *emitMalloc(Value *Num, IRBuilder<> &B, const DataLayout &DL,
                    const TargetLibraryInfo *TLI);

  /// Emit a call to the calloc function.
  Value *emitCalloc(Value *Num, Value *Size, const AttributeList &Attrs,
                    IRBuilder<> &B, const TargetLibraryInfo &TLI);

  /// Emit a call to the fwrite_unlocked function. This assumes that Ptr is a
  /// pointer, Size is an 'intptr_t', N is nmemb and File is a pointer to FILE.
  Value *emitFWriteUnlocked(Value *Ptr, Value *Size, Value *N, Value *File,
                            IRBuilder<> &B, const DataLayout &DL,
                            const TargetLibraryInfo *TLI);

  /// Emit a call to the fgetc_unlocked function. File is a pointer to FILE.
  Value *emitFGetCUnlocked(Value *File, IRBuilder<> &B,
                           const TargetLibraryInfo *TLI);

  /// Emit a call to the fgets_unlocked function. Str is required to be a
  /// pointer, Size is an i32 and File is a pointer to FILE.
  Value *emitFGetSUnlocked(Value *Str, Value *Size, Value *File, IRBuilder<> &B,
                           const TargetLibraryInfo *TLI);

  /// Emit a call to the fread_unlocked function. This assumes that Ptr is a
  /// pointer, Size is an 'intptr_t', N is nmemb and File is a pointer to FILE.
  Value *emitFReadUnlocked(Value *Ptr, Value *Size, Value *N, Value *File,
                           IRBuilder<> &B, const DataLayout &DL,
                           const TargetLibraryInfo *TLI);
}

#endif
