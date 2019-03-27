//===- LCSSA.h - Loop-closed SSA transform Pass -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass transforms loops by placing phi nodes at the end of the loops for
// all values that are live across the loop boundary.  For example, it turns
// the left into the right code:
//
// for (...)                for (...)
//   if (c)                   if (c)
//     X1 = ...                 X1 = ...
//   else                     else
//     X2 = ...                 X2 = ...
//   X3 = phi(X1, X2)         X3 = phi(X1, X2)
// ... = X3 + 4             X4 = phi(X3)
//                          ... = X4 + 4
//
// This is still valid LLVM; the extra phi nodes are purely redundant, and will
// be trivially eliminated by InstCombine.  The major benefit of this
// transformation is that it makes many other loop optimizations, such as
// LoopUnswitching, simpler.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_LCSSA_H
#define LLVM_TRANSFORMS_UTILS_LCSSA_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Converts loops into loop-closed SSA form.
class LCSSAPass : public PassInfoMixin<LCSSAPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_LCSSA_H
