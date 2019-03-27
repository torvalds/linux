//===- AggressiveInstCombineInternal.h --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the instruction pattern combiner classes.
// Currently, it handles pattern expressions for:
//  * Truncate instruction
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TRANSFORMS_AGGRESSIVEINSTCOMBINE_COMBINEINTERNAL_H
#define LLVM_LIB_TRANSFORMS_AGGRESSIVEINSTCOMBINE_COMBINEINTERNAL_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Pass.h"
using namespace llvm;

//===----------------------------------------------------------------------===//
// TruncInstCombine - looks for expression dags dominated by trunc instructions
// and for each eligible dag, it will create a reduced bit-width expression and
// replace the old expression with this new one and remove the old one.
// Eligible expression dag is such that:
//   1. Contains only supported instructions.
//   2. Supported leaves: ZExtInst, SExtInst, TruncInst and Constant value.
//   3. Can be evaluated into type with reduced legal bit-width (or Trunc type).
//   4. All instructions in the dag must not have users outside the dag.
//      Only exception is for {ZExt, SExt}Inst with operand type equal to the
//      new reduced type chosen in (3).
//
// The motivation for this optimization is that evaluating and expression using
// smaller bit-width is preferable, especially for vectorization where we can
// fit more values in one vectorized instruction. In addition, this optimization
// may decrease the number of cast instructions, but will not increase it.
//===----------------------------------------------------------------------===//

namespace llvm {
  class DataLayout;
  class DominatorTree;
  class TargetLibraryInfo;

class TruncInstCombine {
  TargetLibraryInfo &TLI;
  const DataLayout &DL;
  const DominatorTree &DT;

  /// List of all TruncInst instructions to be processed.
  SmallVector<TruncInst *, 4> Worklist;

  /// Current processed TruncInst instruction.
  TruncInst *CurrentTruncInst;

  /// Information per each instruction in the expression dag.
  struct Info {
    /// Number of LSBs that are needed to generate a valid expression.
    unsigned ValidBitWidth = 0;
    /// Minimum number of LSBs needed to generate the ValidBitWidth.
    unsigned MinBitWidth = 0;
    /// The reduced value generated to replace the old instruction.
    Value *NewValue = nullptr;
  };
  /// An ordered map representing expression dag post-dominated by current
  /// processed TruncInst. It maps each instruction in the dag to its Info
  /// structure. The map is ordered such that each instruction appears before
  /// all other instructions in the dag that uses it.
  MapVector<Instruction *, Info> InstInfoMap;

public:
  TruncInstCombine(TargetLibraryInfo &TLI, const DataLayout &DL,
                   const DominatorTree &DT)
      : TLI(TLI), DL(DL), DT(DT), CurrentTruncInst(nullptr) {}

  /// Perform TruncInst pattern optimization on given function.
  bool run(Function &F);

private:
  /// Build expression dag dominated by the /p CurrentTruncInst and append it to
  /// the InstInfoMap container.
  ///
  /// \return true only if succeed to generate an eligible sub expression dag.
  bool buildTruncExpressionDag();

  /// Calculate the minimal allowed bit-width of the chain ending with the
  /// currently visited truncate's operand.
  ///
  /// \return minimum number of bits to which the chain ending with the
  /// truncate's operand can be shrunk to.
  unsigned getMinBitWidth();

  /// Build an expression dag dominated by the current processed TruncInst and
  /// Check if it is eligible to be reduced to a smaller type.
  ///
  /// \return the scalar version of the new type to be used for the reduced
  ///         expression dag, or nullptr if the expression dag is not eligible
  ///         to be reduced.
  Type *getBestTruncatedType();

  /// Given a \p V value and a \p SclTy scalar type return the generated reduced
  /// value of \p V based on the type \p SclTy.
  ///
  /// \param V value to be reduced.
  /// \param SclTy scalar version of new type to reduce to.
  /// \return the new reduced value.
  Value *getReducedOperand(Value *V, Type *SclTy);

  /// Create a new expression dag using the reduced /p SclTy type and replace
  /// the old expression dag with it. Also erase all instructions in the old
  /// dag, except those that are still needed outside the dag.
  ///
  /// \param SclTy scalar version of new type to reduce expression dag into.
  void ReduceExpressionDag(Type *SclTy);
};
} // end namespace llvm.

#endif
