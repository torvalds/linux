//===- SSAUpdaterBulk.h - Unstructured SSA Update Tool ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the SSAUpdaterBulk class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_SSAUPDATERBULK_H
#define LLVM_TRANSFORMS_UTILS_SSAUPDATERBULK_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/PredIteratorCache.h"

namespace llvm {

class BasicBlock;
class PHINode;
template <typename T> class SmallVectorImpl;
class Type;
class Use;
class Value;
class DominatorTree;

/// Helper class for SSA formation on a set of values defined in multiple
/// blocks.
///
/// This is used when code duplication or another unstructured transformation
/// wants to rewrite a set of uses of one value with uses of a set of values.
/// The update is done only when RewriteAllUses is called, all other methods are
/// used for book-keeping. That helps to share some common computations between
/// updates of different uses (which is not the case when traditional SSAUpdater
/// is used).
class SSAUpdaterBulk {
  struct RewriteInfo {
    DenseMap<BasicBlock *, Value *> Defines;
    SmallVector<Use *, 4> Uses;
    StringRef Name;
    Type *Ty;
    RewriteInfo(){};
    RewriteInfo(StringRef &N, Type *T) : Name(N), Ty(T){};
  };
  SmallVector<RewriteInfo, 4> Rewrites;

  PredIteratorCache PredCache;

  Value *computeValueAt(BasicBlock *BB, RewriteInfo &R, DominatorTree *DT);

public:
  explicit SSAUpdaterBulk(){};
  SSAUpdaterBulk(const SSAUpdaterBulk &) = delete;
  SSAUpdaterBulk &operator=(const SSAUpdaterBulk &) = delete;
  ~SSAUpdaterBulk(){};

  /// Add a new variable to the SSA rewriter. This needs to be called before
  /// AddAvailableValue or AddUse calls. The return value is the variable ID,
  /// which needs to be passed to AddAvailableValue and AddUse.
  unsigned AddVariable(StringRef Name, Type *Ty);

  /// Indicate that a rewritten value is available in the specified block with
  /// the specified value.
  void AddAvailableValue(unsigned Var, BasicBlock *BB, Value *V);

  /// Record a use of the symbolic value. This use will be updated with a
  /// rewritten value when RewriteAllUses is called.
  void AddUse(unsigned Var, Use *U);

  /// Return true if the SSAUpdater already has a value for the specified
  /// variable in the specified block.
  bool HasValueForBlock(unsigned Var, BasicBlock *BB);

  /// Perform all the necessary updates, including new PHI-nodes insertion and
  /// the requested uses update.
  ///
  /// The function requires dominator tree DT, which is used for computing
  /// locations for new phi-nodes insertions. If a nonnull pointer to a vector
  /// InsertedPHIs is passed, all the new phi-nodes will be added to this
  /// vector.
  void RewriteAllUses(DominatorTree *DT,
                      SmallVectorImpl<PHINode *> *InsertedPHIs = nullptr);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_SSAUPDATERBULK_H
