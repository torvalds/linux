//===- InstSimplifyPass.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Defines passes for running instruction simplification across chunks of IR.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_INSTSIMPLIFYPASS_H
#define LLVM_TRANSFORMS_UTILS_INSTSIMPLIFYPASS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class FunctionPass;

/// Run instruction simplification across each instruction in the function.
///
/// Instruction simplification has useful constraints in some contexts:
/// - It will never introduce *new* instructions.
/// - There is no need to iterate to a fixed point.
///
/// Many passes use instruction simplification as a library facility, but it may
/// also be useful (in tests and other contexts) to have access to this very
/// restricted transform at a pass granularity. However, for a much more
/// powerful and comprehensive peephole optimization engine, see the
/// `instcombine` pass instead.
class InstSimplifyPass : public PassInfoMixin<InstSimplifyPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Create a legacy pass that does instruction simplification on each
/// instruction in a function.
FunctionPass *createInstSimplifyLegacyPass();

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_INSTSIMPLIFYPASS_H
