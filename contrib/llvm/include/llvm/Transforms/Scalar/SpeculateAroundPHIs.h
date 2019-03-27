//===- SpeculateAroundPHIs.h - Speculate around PHIs ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_SPECULATEAROUNDPHIS_H
#define LLVM_TRANSFORMS_SCALAR_SPECULATEAROUNDPHIS_H

#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"
#include <vector>

namespace llvm {

/// This pass handles simple speculating of  instructions around PHIs when
/// doing so is profitable for a particular target despite duplicated
/// instructions.
///
/// The motivating example are PHIs of constants which will require
/// materializing the constants along each edge. If the PHI is used by an
/// instruction where the target can materialize the constant as part of the
/// instruction, it is profitable to speculate those instructions around the
/// PHI node. This can reduce dynamic instruction count as well as decrease
/// register pressure.
///
/// Consider this IR for example:
///   ```
///   entry:
///     br i1 %flag, label %a, label %b
///
///   a:
///     br label %exit
///
///   b:
///     br label %exit
///
///   exit:
///     %p = phi i32 [ 7, %a ], [ 11, %b ]
///     %sum = add i32 %arg, %p
///     ret i32 %sum
///   ```
/// To materialize the inputs to this PHI node may require an explicit
/// instruction. For example, on x86 this would turn into something like
///   ```
///     testq %eax, %eax
///     movl $7, %rNN
///     jne .L
///     movl $11, %rNN
///   .L:
///     addl %edi, %rNN
///     movl %rNN, %eax
///     retq
///   ```
/// When these constants can be folded directly into another instruction, it
/// would be preferable to avoid the potential for register pressure (above we
/// can easily avoid it, but that isn't always true) and simply duplicate the
/// instruction using the PHI:
///   ```
///   entry:
///     br i1 %flag, label %a, label %b
///
///   a:
///     %sum.1 = add i32 %arg, 7
///     br label %exit
///
///   b:
///     %sum.2 = add i32 %arg, 11
///     br label %exit
///
///   exit:
///     %p = phi i32 [ %sum.1, %a ], [ %sum.2, %b ]
///     ret i32 %p
///   ```
/// Which will generate something like the following on x86:
///   ```
///     testq %eax, %eax
///     addl $7, %edi
///     jne .L
///     addl $11, %edi
///   .L:
///     movl %edi, %eax
///     retq
///   ```
///
/// It is important to note that this pass is never intended to handle more
/// complex cases where speculating around PHIs allows simplifications of the
/// IR itself or other subsequent optimizations. Those can and should already
/// be handled before this pass is ever run by a more powerful analysis that
/// can reason about equivalences and common subexpressions. Classically, those
/// cases would be handled by a GVN-powered PRE or similar transform. This
/// pass, in contrast, is *only* interested in cases where despite no
/// simplifications to the IR itself, speculation is *faster* to execute. The
/// result of this is that the cost models which are appropriate to consider
/// here are relatively simple ones around execution and codesize cost, without
/// any need to consider simplifications or other transformations.
struct SpeculateAroundPHIsPass : PassInfoMixin<SpeculateAroundPHIsPass> {
  /// Run the pass over the function.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_SPECULATEAROUNDPHIS_H
