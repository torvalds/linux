//===-- GuardUtils.h - Utils for work with guards ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Utils that are used to perform transformations related to guards and their
// conditions.
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_GUARDUTILS_H
#define LLVM_TRANSFORMS_UTILS_GUARDUTILS_H

namespace llvm {

class CallInst;
class Function;

/// Splits control flow at point of \p Guard, replacing it with explicit branch
/// by the condition of guard's first argument. The taken branch then goes to
/// the block that contains  \p Guard's successors, and the non-taken branch
/// goes to a newly-created deopt block that contains a sole call of the
/// deoptimize function \p DeoptIntrinsic.
void makeGuardControlFlowExplicit(Function *DeoptIntrinsic, CallInst *Guard);

} // llvm

#endif // LLVM_TRANSFORMS_UTILS_GUARDUTILS_H
