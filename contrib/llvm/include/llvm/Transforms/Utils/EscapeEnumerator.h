//===-- EscapeEnumerator.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Defines a helper class that enumerates all possible exits from a function,
// including exception handling.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_ESCAPEENUMERATOR_H
#define LLVM_TRANSFORMS_UTILS_ESCAPEENUMERATOR_H

#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"

namespace llvm {

/// EscapeEnumerator - This is a little algorithm to find all escape points
/// from a function so that "finally"-style code can be inserted. In addition
/// to finding the existing return and unwind instructions, it also (if
/// necessary) transforms any call instructions into invokes and sends them to
/// a landing pad.
class EscapeEnumerator {
  Function &F;
  const char *CleanupBBName;

  Function::iterator StateBB, StateE;
  IRBuilder<> Builder;
  bool Done;
  bool HandleExceptions;

public:
  EscapeEnumerator(Function &F, const char *N = "cleanup",
                   bool HandleExceptions = true)
      : F(F), CleanupBBName(N), StateBB(F.begin()), StateE(F.end()),
        Builder(F.getContext()), Done(false),
        HandleExceptions(HandleExceptions) {}

  IRBuilder<> *Next();
};

}

#endif // LLVM_TRANSFORMS_UTILS_ESCAPEENUMERATOR_H
