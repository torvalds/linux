//===-- IntrinsicLowering.h - Intrinsic Function Lowering -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the IntrinsicLowering interface.  This interface allows
// addition of domain-specific or front-end specific intrinsics to LLVM without
// having to modify all of the C backend or interpreter.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_INTRINSICLOWERING_H
#define LLVM_CODEGEN_INTRINSICLOWERING_H

#include "llvm/IR/Intrinsics.h"

namespace llvm {
class CallInst;
class Module;
class DataLayout;

class IntrinsicLowering {
  const DataLayout &DL;

  bool Warned;

public:
  explicit IntrinsicLowering(const DataLayout &DL) : DL(DL), Warned(false) {}

  /// Add all of the prototypes that might be needed by an intrinsic lowering
  /// implementation to be inserted into the module specified.
  void AddPrototypes(Module &M);

  /// Replace a call to the specified intrinsic function.
  /// If an intrinsic function must be implemented by the code generator
  /// (such as va_start), this function should print a message and abort.
  ///
  /// Otherwise, if an intrinsic function call can be lowered, the code to
  /// implement it (often a call to a non-intrinsic function) is inserted
  /// _after_ the call instruction and the call is deleted. The caller must
  /// be capable of handling this kind of change.
  void LowerIntrinsicCall(CallInst *CI);

  /// Try to replace a call instruction with a call to a bswap intrinsic. Return
  /// false if the call is not a simple integer bswap.
  static bool LowerToByteSwap(CallInst *CI);
};
}

#endif
