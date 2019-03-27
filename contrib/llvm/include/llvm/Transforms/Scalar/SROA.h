//===- SROA.h - Scalar Replacement Of Aggregates ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides the interface for LLVM's Scalar Replacement of
/// Aggregates pass. This pass provides both aggregate splitting and the
/// primary SSA formation used in the compiler.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_SROA_H
#define LLVM_TRANSFORMS_SCALAR_SROA_H

#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"
#include <vector>

namespace llvm {

class AllocaInst;
class AssumptionCache;
class DominatorTree;
class Function;
class Instruction;
class LLVMContext;
class PHINode;
class SelectInst;
class Use;

/// A private "module" namespace for types and utilities used by SROA. These
/// are implementation details and should not be used by clients.
namespace sroa LLVM_LIBRARY_VISIBILITY {

class AllocaSliceRewriter;
class AllocaSlices;
class Partition;
class SROALegacyPass;

} // end namespace sroa

/// An optimization pass providing Scalar Replacement of Aggregates.
///
/// This pass takes allocations which can be completely analyzed (that is, they
/// don't escape) and tries to turn them into scalar SSA values. There are
/// a few steps to this process.
///
/// 1) It takes allocations of aggregates and analyzes the ways in which they
///    are used to try to split them into smaller allocations, ideally of
///    a single scalar data type. It will split up memcpy and memset accesses
///    as necessary and try to isolate individual scalar accesses.
/// 2) It will transform accesses into forms which are suitable for SSA value
///    promotion. This can be replacing a memset with a scalar store of an
///    integer value, or it can involve speculating operations on a PHI or
///    select to be a PHI or select of the results.
/// 3) Finally, this will try to detect a pattern of accesses which map cleanly
///    onto insert and extract operations on a vector value, and convert them to
///    this form. By doing so, it will enable promotion of vector aggregates to
///    SSA vector values.
class SROA : public PassInfoMixin<SROA> {
  LLVMContext *C = nullptr;
  DominatorTree *DT = nullptr;
  AssumptionCache *AC = nullptr;

  /// Worklist of alloca instructions to simplify.
  ///
  /// Each alloca in the function is added to this. Each new alloca formed gets
  /// added to it as well to recursively simplify unless that alloca can be
  /// directly promoted. Finally, each time we rewrite a use of an alloca other
  /// the one being actively rewritten, we add it back onto the list if not
  /// already present to ensure it is re-visited.
  SetVector<AllocaInst *, SmallVector<AllocaInst *, 16>> Worklist;

  /// A collection of instructions to delete.
  /// We try to batch deletions to simplify code and make things a bit more
  /// efficient.
  SetVector<Instruction *, SmallVector<Instruction *, 8>> DeadInsts;

  /// Post-promotion worklist.
  ///
  /// Sometimes we discover an alloca which has a high probability of becoming
  /// viable for SROA after a round of promotion takes place. In those cases,
  /// the alloca is enqueued here for re-processing.
  ///
  /// Note that we have to be very careful to clear allocas out of this list in
  /// the event they are deleted.
  SetVector<AllocaInst *, SmallVector<AllocaInst *, 16>> PostPromotionWorklist;

  /// A collection of alloca instructions we can directly promote.
  std::vector<AllocaInst *> PromotableAllocas;

  /// A worklist of PHIs to speculate prior to promoting allocas.
  ///
  /// All of these PHIs have been checked for the safety of speculation and by
  /// being speculated will allow promoting allocas currently in the promotable
  /// queue.
  SetVector<PHINode *, SmallVector<PHINode *, 2>> SpeculatablePHIs;

  /// A worklist of select instructions to speculate prior to promoting
  /// allocas.
  ///
  /// All of these select instructions have been checked for the safety of
  /// speculation and by being speculated will allow promoting allocas
  /// currently in the promotable queue.
  SetVector<SelectInst *, SmallVector<SelectInst *, 2>> SpeculatableSelects;

public:
  SROA() = default;

  /// Run the pass over the function.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

private:
  friend class sroa::AllocaSliceRewriter;
  friend class sroa::SROALegacyPass;

  /// Helper used by both the public run method and by the legacy pass.
  PreservedAnalyses runImpl(Function &F, DominatorTree &RunDT,
                            AssumptionCache &RunAC);

  bool presplitLoadsAndStores(AllocaInst &AI, sroa::AllocaSlices &AS);
  AllocaInst *rewritePartition(AllocaInst &AI, sroa::AllocaSlices &AS,
                               sroa::Partition &P);
  bool splitAlloca(AllocaInst &AI, sroa::AllocaSlices &AS);
  bool runOnAlloca(AllocaInst &AI);
  void clobberUse(Use &U);
  bool deleteDeadInstructions(SmallPtrSetImpl<AllocaInst *> &DeletedAllocas);
  bool promoteAllocas(Function &F);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_SROA_H
