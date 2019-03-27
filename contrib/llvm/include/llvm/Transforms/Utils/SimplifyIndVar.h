//===-- llvm/Transforms/Utils/SimplifyIndVar.h - Indvar Utils ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines in interface for induction variable simplification. It does
// not define any actual pass or policy, but provides a single function to
// simplify a loop's induction variables based on ScalarEvolution.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_SIMPLIFYINDVAR_H
#define LLVM_TRANSFORMS_UTILS_SIMPLIFYINDVAR_H

#include "llvm/IR/ValueHandle.h"

namespace llvm {

class CastInst;
class DominatorTree;
class Loop;
class LoopInfo;
class PHINode;
class ScalarEvolution;
class SCEVExpander;

/// Interface for visiting interesting IV users that are recognized but not
/// simplified by this utility.
class IVVisitor {
protected:
  const DominatorTree *DT = nullptr;

  virtual void anchor();

public:
  IVVisitor() = default;
  virtual ~IVVisitor() = default;

  const DominatorTree *getDomTree() const { return DT; }
  virtual void visitCast(CastInst *Cast) = 0;
};

/// simplifyUsersOfIV - Simplify instructions that use this induction variable
/// by using ScalarEvolution to analyze the IV's recurrence.
bool simplifyUsersOfIV(PHINode *CurrIV, ScalarEvolution *SE, DominatorTree *DT,
                       LoopInfo *LI, SmallVectorImpl<WeakTrackingVH> &Dead,
                       SCEVExpander &Rewriter, IVVisitor *V = nullptr);

/// SimplifyLoopIVs - Simplify users of induction variables within this
/// loop. This does not actually change or add IVs.
bool simplifyLoopIVs(Loop *L, ScalarEvolution *SE, DominatorTree *DT,
                     LoopInfo *LI, SmallVectorImpl<WeakTrackingVH> &Dead);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_SIMPLIFYINDVAR_H
