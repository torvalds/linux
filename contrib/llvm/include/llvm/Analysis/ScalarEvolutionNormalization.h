//===- llvm/Analysis/ScalarEvolutionNormalization.h - See below -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines utilities for working with "normalized" ScalarEvolution
// expressions.
//
// The following example illustrates post-increment uses and how normalized
// expressions help.
//
//   for (i=0; i!=n; ++i) {
//     ...
//   }
//   use(i);
//
// While the expression for most uses of i inside the loop is {0,+,1}<%L>, the
// expression for the use of i outside the loop is {1,+,1}<%L>, since i is
// incremented at the end of the loop body. This is inconveient, since it
// suggests that we need two different induction variables, one that starts
// at 0 and one that starts at 1. We'd prefer to be able to think of these as
// the same induction variable, with uses inside the loop using the
// "pre-incremented" value, and uses after the loop using the
// "post-incremented" value.
//
// Expressions for post-incremented uses are represented as an expression
// paired with a set of loops for which the expression is in "post-increment"
// mode (there may be multiple loops).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_SCALAREVOLUTIONNORMALIZATION_H
#define LLVM_ANALYSIS_SCALAREVOLUTIONNORMALIZATION_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"

namespace llvm {

class Loop;
class ScalarEvolution;
class SCEV;

typedef SmallPtrSet<const Loop *, 2> PostIncLoopSet;

typedef function_ref<bool(const SCEVAddRecExpr *)> NormalizePredTy;

/// Normalize \p S to be post-increment for all loops present in \p
/// Loops.
const SCEV *normalizeForPostIncUse(const SCEV *S, const PostIncLoopSet &Loops,
                                   ScalarEvolution &SE);

/// Normalize \p S for all add recurrence sub-expressions for which \p
/// Pred returns true.
const SCEV *normalizeForPostIncUseIf(const SCEV *S, NormalizePredTy Pred,
                                     ScalarEvolution &SE);

/// Denormalize \p S to be post-increment for all loops present in \p
/// Loops.
const SCEV *denormalizeForPostIncUse(const SCEV *S, const PostIncLoopSet &Loops,
                                     ScalarEvolution &SE);
} // namespace llvm

#endif
