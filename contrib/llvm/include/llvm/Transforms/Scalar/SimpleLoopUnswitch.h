//===- SimpleLoopUnswitch.h - Hoist loop-invariant control flow -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_SIMPLELOOPUNSWITCH_H
#define LLVM_TRANSFORMS_SCALAR_SIMPLELOOPUNSWITCH_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"

namespace llvm {

/// This pass transforms loops that contain branches or switches on loop-
/// invariant conditions to have multiple loops. For example, it turns the left
/// into the right code:
///
///  for (...)                  if (lic)
///    A                          for (...)
///    if (lic)                     A; B; C
///      B                      else
///    C                          for (...)
///                                 A; C
///
/// This can increase the size of the code exponentially (doubling it every time
/// a loop is unswitched) so we only unswitch if the resultant code will be
/// smaller than a threshold.
///
/// This pass expects LICM to be run before it to hoist invariant conditions out
/// of the loop, to make the unswitching opportunity obvious.
///
/// There is a taxonomy of unswitching that we use to classify different forms
/// of this transformaiton:
///
/// - Trival unswitching: this is when the condition can be unswitched without
///   cloning any code from inside the loop. A non-trivial unswitch requires
///   code duplication.
///
/// - Full unswitching: this is when the branch or switch is completely moved
///   from inside the loop to outside the loop. Partial unswitching removes the
///   branch from the clone of the loop but must leave a (somewhat simplified)
///   branch in the original loop. While theoretically partial unswitching can
///   be done for switches, the requirements are extreme - we need the loop
///   invariant input to the switch to be sufficient to collapse to a single
///   successor in each clone.
///
/// This pass always does trivial, full unswitching for both branches and
/// switches. For branches, it also always does trivial, partial unswitching.
///
/// If enabled (via the constructor's `NonTrivial` parameter), this pass will
/// additionally do non-trivial, full unswitching for branches and switches, and
/// will do non-trivial, partial unswitching for branches.
///
/// Because partial unswitching of switches is extremely unlikely to be possible
/// in practice and significantly complicates the implementation, this pass does
/// not currently implement that in any mode.
class SimpleLoopUnswitchPass : public PassInfoMixin<SimpleLoopUnswitchPass> {
  bool NonTrivial;

public:
  SimpleLoopUnswitchPass(bool NonTrivial = false) : NonTrivial(NonTrivial) {}

  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);
};

/// Create the legacy pass object for the simple loop unswitcher.
///
/// See the documentaion for `SimpleLoopUnswitchPass` for details.
Pass *createSimpleLoopUnswitchLegacyPass(bool NonTrivial = false);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_SIMPLELOOPUNSWITCH_H
