//===- NaryReassociate.h - Reassociate n-ary expressions --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass reassociates n-ary add expressions and eliminates the redundancy
// exposed by the reassociation.
//
// A motivating example:
//
//   void foo(int a, int b) {
//     bar(a + b);
//     bar((a + 2) + b);
//   }
//
// An ideal compiler should reassociate (a + 2) + b to (a + b) + 2 and simplify
// the above code to
//
//   int t = a + b;
//   bar(t);
//   bar(t + 2);
//
// However, the Reassociate pass is unable to do that because it processes each
// instruction individually and believes (a + 2) + b is the best form according
// to its rank system.
//
// To address this limitation, NaryReassociate reassociates an expression in a
// form that reuses existing instructions. As a result, NaryReassociate can
// reassociate (a + 2) + b in the example to (a + b) + 2 because it detects that
// (a + b) is computed before.
//
// NaryReassociate works as follows. For every instruction in the form of (a +
// b) + c, it checks whether a + c or b + c is already computed by a dominating
// instruction. If so, it then reassociates (a + b) + c into (a + c) + b or (b +
// c) + a and removes the redundancy accordingly. To efficiently look up whether
// an expression is computed before, we store each instruction seen and its SCEV
// into an SCEV-to-instruction map.
//
// Although the algorithm pattern-matches only ternary additions, it
// automatically handles many >3-ary expressions by walking through the function
// in the depth-first order. For example, given
//
//   (a + c) + d
//   ((a + b) + c) + d
//
// NaryReassociate first rewrites (a + b) + c to (a + c) + b, and then rewrites
// ((a + c) + b) + d into ((a + c) + d) + b.
//
// Finally, the above dominator-based algorithm may need to be run multiple
// iterations before emitting optimal code. One source of this need is that we
// only split an operand when it is used only once. The above algorithm can
// eliminate an instruction and decrease the usage count of its operands. As a
// result, an instruction that previously had multiple uses may become a
// single-use instruction and thus eligible for split consideration. For
// example,
//
//   ac = a + c
//   ab = a + b
//   abc = ab + c
//   ab2 = ab + b
//   ab2c = ab2 + c
//
// In the first iteration, we cannot reassociate abc to ac+b because ab is used
// twice. However, we can reassociate ab2c to abc+b in the first iteration. As a
// result, ab2 becomes dead and ab will be used only once in the second
// iteration.
//
// Limitations and TODO items:
//
// 1) We only considers n-ary adds and muls for now. This should be extended
// and generalized.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_NARYREASSOCIATE_H
#define LLVM_TRANSFORMS_SCALAR_NARYREASSOCIATE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"

namespace llvm {

class AssumptionCache;
class BinaryOperator;
class DataLayout;
class DominatorTree;
class Function;
class GetElementPtrInst;
class Instruction;
class ScalarEvolution;
class SCEV;
class TargetLibraryInfo;
class TargetTransformInfo;
class Type;
class Value;

class NaryReassociatePass : public PassInfoMixin<NaryReassociatePass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  // Glue for old PM.
  bool runImpl(Function &F, AssumptionCache *AC_, DominatorTree *DT_,
               ScalarEvolution *SE_, TargetLibraryInfo *TLI_,
               TargetTransformInfo *TTI_);

private:
  // Runs only one iteration of the dominator-based algorithm. See the header
  // comments for why we need multiple iterations.
  bool doOneIteration(Function &F);

  // Reassociates I for better CSE.
  Instruction *tryReassociate(Instruction *I);

  // Reassociate GEP for better CSE.
  Instruction *tryReassociateGEP(GetElementPtrInst *GEP);

  // Try splitting GEP at the I-th index and see whether either part can be
  // CSE'ed. This is a helper function for tryReassociateGEP.
  //
  // \p IndexedType The element type indexed by GEP's I-th index. This is
  //                equivalent to
  //                  GEP->getIndexedType(GEP->getPointerOperand(), 0-th index,
  //                                      ..., i-th index).
  GetElementPtrInst *tryReassociateGEPAtIndex(GetElementPtrInst *GEP,
                                              unsigned I, Type *IndexedType);

  // Given GEP's I-th index = LHS + RHS, see whether &Base[..][LHS][..] or
  // &Base[..][RHS][..] can be CSE'ed and rewrite GEP accordingly.
  GetElementPtrInst *tryReassociateGEPAtIndex(GetElementPtrInst *GEP,
                                              unsigned I, Value *LHS,
                                              Value *RHS, Type *IndexedType);

  // Reassociate binary operators for better CSE.
  Instruction *tryReassociateBinaryOp(BinaryOperator *I);

  // A helper function for tryReassociateBinaryOp. LHS and RHS are explicitly
  // passed.
  Instruction *tryReassociateBinaryOp(Value *LHS, Value *RHS,
                                      BinaryOperator *I);
  // Rewrites I to (LHS op RHS) if LHS is computed already.
  Instruction *tryReassociatedBinaryOp(const SCEV *LHS, Value *RHS,
                                       BinaryOperator *I);

  // Tries to match Op1 and Op2 by using V.
  bool matchTernaryOp(BinaryOperator *I, Value *V, Value *&Op1, Value *&Op2);

  // Gets SCEV for (LHS op RHS).
  const SCEV *getBinarySCEV(BinaryOperator *I, const SCEV *LHS,
                            const SCEV *RHS);

  // Returns the closest dominator of \c Dominatee that computes
  // \c CandidateExpr. Returns null if not found.
  Instruction *findClosestMatchingDominator(const SCEV *CandidateExpr,
                                            Instruction *Dominatee);

  // GetElementPtrInst implicitly sign-extends an index if the index is shorter
  // than the pointer size. This function returns whether Index is shorter than
  // GEP's pointer size, i.e., whether Index needs to be sign-extended in order
  // to be an index of GEP.
  bool requiresSignExtension(Value *Index, GetElementPtrInst *GEP);

  AssumptionCache *AC;
  const DataLayout *DL;
  DominatorTree *DT;
  ScalarEvolution *SE;
  TargetLibraryInfo *TLI;
  TargetTransformInfo *TTI;

  // A lookup table quickly telling which instructions compute the given SCEV.
  // Note that there can be multiple instructions at different locations
  // computing to the same SCEV, so we map a SCEV to an instruction list.  For
  // example,
  //
  //   if (p1)
  //     foo(a + b);
  //   if (p2)
  //     bar(a + b);
  DenseMap<const SCEV *, SmallVector<WeakTrackingVH, 2>> SeenExprs;
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_NARYREASSOCIATE_H
