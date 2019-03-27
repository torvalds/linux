//===- SLPVectorizer.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This pass implements the Bottom Up SLP vectorizer. It detects consecutive
// stores that can be put together into vector-stores. Next, it attempts to
// construct vectorizable tree using the use-def chains. If a profitable tree
// was found, the SLP vectorizer performs vectorization on the tree.
//
// The pass is inspired by the work described in the paper:
//  "Loop-Aware SLP in GCC" by Ira Rosen, Dorit Nuzman, Ayal Zaks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_SLPVECTORIZER_H
#define LLVM_TRANSFORMS_VECTORIZE_SLPVECTORIZER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"

namespace llvm {

class AssumptionCache;
class BasicBlock;
class CmpInst;
class DataLayout;
class DemandedBits;
class DominatorTree;
class Function;
class InsertElementInst;
class InsertValueInst;
class Instruction;
class LoopInfo;
class OptimizationRemarkEmitter;
class PHINode;
class ScalarEvolution;
class StoreInst;
class TargetLibraryInfo;
class TargetTransformInfo;
class Value;

/// A private "module" namespace for types and utilities used by this pass.
/// These are implementation details and should not be used by clients.
namespace slpvectorizer {

class BoUpSLP;

} // end namespace slpvectorizer

struct SLPVectorizerPass : public PassInfoMixin<SLPVectorizerPass> {
  using StoreList = SmallVector<StoreInst *, 8>;
  using StoreListMap = MapVector<Value *, StoreList>;
  using WeakTrackingVHList = SmallVector<WeakTrackingVH, 8>;
  using WeakTrackingVHListMap = MapVector<Value *, WeakTrackingVHList>;

  ScalarEvolution *SE = nullptr;
  TargetTransformInfo *TTI = nullptr;
  TargetLibraryInfo *TLI = nullptr;
  AliasAnalysis *AA = nullptr;
  LoopInfo *LI = nullptr;
  DominatorTree *DT = nullptr;
  AssumptionCache *AC = nullptr;
  DemandedBits *DB = nullptr;
  const DataLayout *DL = nullptr;

public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  // Glue for old PM.
  bool runImpl(Function &F, ScalarEvolution *SE_, TargetTransformInfo *TTI_,
               TargetLibraryInfo *TLI_, AliasAnalysis *AA_, LoopInfo *LI_,
               DominatorTree *DT_, AssumptionCache *AC_, DemandedBits *DB_,
               OptimizationRemarkEmitter *ORE_);

private:
  /// Collect store and getelementptr instructions and organize them
  /// according to the underlying object of their pointer operands. We sort the
  /// instructions by their underlying objects to reduce the cost of
  /// consecutive access queries.
  ///
  /// TODO: We can further reduce this cost if we flush the chain creation
  ///       every time we run into a memory barrier.
  void collectSeedInstructions(BasicBlock *BB);

  /// Try to vectorize a chain that starts at two arithmetic instrs.
  bool tryToVectorizePair(Value *A, Value *B, slpvectorizer::BoUpSLP &R);

  /// Try to vectorize a list of operands.
  /// \param UserCost Cost of the user operations of \p VL if they may affect
  /// the cost of the vectorization.
  /// \returns true if a value was vectorized.
  bool tryToVectorizeList(ArrayRef<Value *> VL, slpvectorizer::BoUpSLP &R,
                          int UserCost = 0, bool AllowReorder = false);

  /// Try to vectorize a chain that may start at the operands of \p I.
  bool tryToVectorize(Instruction *I, slpvectorizer::BoUpSLP &R);

  /// Vectorize the store instructions collected in Stores.
  bool vectorizeStoreChains(slpvectorizer::BoUpSLP &R);

  /// Vectorize the index computations of the getelementptr instructions
  /// collected in GEPs.
  bool vectorizeGEPIndices(BasicBlock *BB, slpvectorizer::BoUpSLP &R);

  /// Try to find horizontal reduction or otherwise vectorize a chain of binary
  /// operators.
  bool vectorizeRootInstruction(PHINode *P, Value *V, BasicBlock *BB,
                                slpvectorizer::BoUpSLP &R,
                                TargetTransformInfo *TTI);

  /// Try to vectorize trees that start at insertvalue instructions.
  bool vectorizeInsertValueInst(InsertValueInst *IVI, BasicBlock *BB,
                                slpvectorizer::BoUpSLP &R);

  /// Try to vectorize trees that start at insertelement instructions.
  bool vectorizeInsertElementInst(InsertElementInst *IEI, BasicBlock *BB,
                                  slpvectorizer::BoUpSLP &R);

  /// Try to vectorize trees that start at compare instructions.
  bool vectorizeCmpInst(CmpInst *CI, BasicBlock *BB, slpvectorizer::BoUpSLP &R);

  /// Tries to vectorize constructs started from CmpInst, InsertValueInst or
  /// InsertElementInst instructions.
  bool vectorizeSimpleInstructions(SmallVectorImpl<WeakVH> &Instructions,
                                   BasicBlock *BB, slpvectorizer::BoUpSLP &R);

  /// Scan the basic block and look for patterns that are likely to start
  /// a vectorization chain.
  bool vectorizeChainsInBlock(BasicBlock *BB, slpvectorizer::BoUpSLP &R);

  bool vectorizeStoreChain(ArrayRef<Value *> Chain, slpvectorizer::BoUpSLP &R,
                           unsigned VecRegSize);

  bool vectorizeStores(ArrayRef<StoreInst *> Stores, slpvectorizer::BoUpSLP &R);

  /// The store instructions in a basic block organized by base pointer.
  StoreListMap Stores;

  /// The getelementptr instructions in a basic block organized by base pointer.
  WeakTrackingVHListMap GEPs;
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_VECTORIZE_SLPVECTORIZER_H
