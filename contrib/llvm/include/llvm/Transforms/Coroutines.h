//===-- Coroutines.h - Coroutine Transformations ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Declare accessor functions for coroutine lowering passes.
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_COROUTINES_H
#define LLVM_TRANSFORMS_COROUTINES_H

namespace llvm {

class Pass;
class PassManagerBuilder;

/// Add all coroutine passes to appropriate extension points.
void addCoroutinePassesToExtensionPoints(PassManagerBuilder &Builder);

/// Lower coroutine intrinsics that are not needed by later passes.
Pass *createCoroEarlyPass();

/// Split up coroutines into multiple functions driving their state machines.
Pass *createCoroSplitPass();

/// Analyze coroutines use sites, devirtualize resume/destroy calls and elide
/// heap allocation for coroutine frame where possible.
Pass *createCoroElidePass();

/// Lower all remaining coroutine intrinsics.
Pass *createCoroCleanupPass();

}

#endif
