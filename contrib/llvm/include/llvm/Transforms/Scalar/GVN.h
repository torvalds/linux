//===- GVN.h - Eliminate redundant values and loads -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides the interface for LLVM's Global Value Numbering pass
/// which eliminates fully redundant instructions. It also does somewhat Ad-Hoc
/// PRE and dead load elimination.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_GVN_H
#define LLVM_TRANSFORMS_SCALAR_GVN_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/InstructionPrecedenceTracking.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <utility>
#include <vector>

namespace llvm {

class AssumptionCache;
class BasicBlock;
class BranchInst;
class CallInst;
class Constant;
class ExtractValueInst;
class Function;
class FunctionPass;
class IntrinsicInst;
class LoadInst;
class LoopInfo;
class OptimizationRemarkEmitter;
class PHINode;
class TargetLibraryInfo;
class Value;

/// A private "module" namespace for types and utilities used by GVN. These
/// are implementation details and should not be used by clients.
namespace gvn LLVM_LIBRARY_VISIBILITY {

struct AvailableValue;
struct AvailableValueInBlock;
class GVNLegacyPass;

} // end namespace gvn

/// The core GVN pass object.
///
/// FIXME: We should have a good summary of the GVN algorithm implemented by
/// this particular pass here.
class GVN : public PassInfoMixin<GVN> {
public:
  struct Expression;

  /// Run the pass over the function.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  /// This removes the specified instruction from
  /// our various maps and marks it for deletion.
  void markInstructionForDeletion(Instruction *I) {
    VN.erase(I);
    InstrsToErase.push_back(I);
  }

  DominatorTree &getDominatorTree() const { return *DT; }
  AliasAnalysis *getAliasAnalysis() const { return VN.getAliasAnalysis(); }
  MemoryDependenceResults &getMemDep() const { return *MD; }

  /// This class holds the mapping between values and value numbers.  It is used
  /// as an efficient mechanism to determine the expression-wise equivalence of
  /// two values.
  class ValueTable {
    DenseMap<Value *, uint32_t> valueNumbering;
    DenseMap<Expression, uint32_t> expressionNumbering;

    // Expressions is the vector of Expression. ExprIdx is the mapping from
    // value number to the index of Expression in Expressions. We use it
    // instead of a DenseMap because filling such mapping is faster than
    // filling a DenseMap and the compile time is a little better.
    uint32_t nextExprNumber;

    std::vector<Expression> Expressions;
    std::vector<uint32_t> ExprIdx;

    // Value number to PHINode mapping. Used for phi-translate in scalarpre.
    DenseMap<uint32_t, PHINode *> NumberingPhi;

    // Cache for phi-translate in scalarpre.
    using PhiTranslateMap =
        DenseMap<std::pair<uint32_t, const BasicBlock *>, uint32_t>;
    PhiTranslateMap PhiTranslateTable;

    AliasAnalysis *AA;
    MemoryDependenceResults *MD;
    DominatorTree *DT;

    uint32_t nextValueNumber = 1;

    Expression createExpr(Instruction *I);
    Expression createCmpExpr(unsigned Opcode, CmpInst::Predicate Predicate,
                             Value *LHS, Value *RHS);
    Expression createExtractvalueExpr(ExtractValueInst *EI);
    uint32_t lookupOrAddCall(CallInst *C);
    uint32_t phiTranslateImpl(const BasicBlock *BB, const BasicBlock *PhiBlock,
                              uint32_t Num, GVN &Gvn);
    std::pair<uint32_t, bool> assignExpNewValueNum(Expression &exp);
    bool areAllValsInBB(uint32_t num, const BasicBlock *BB, GVN &Gvn);

  public:
    ValueTable();
    ValueTable(const ValueTable &Arg);
    ValueTable(ValueTable &&Arg);
    ~ValueTable();

    uint32_t lookupOrAdd(Value *V);
    uint32_t lookup(Value *V, bool Verify = true) const;
    uint32_t lookupOrAddCmp(unsigned Opcode, CmpInst::Predicate Pred,
                            Value *LHS, Value *RHS);
    uint32_t phiTranslate(const BasicBlock *BB, const BasicBlock *PhiBlock,
                          uint32_t Num, GVN &Gvn);
    void eraseTranslateCacheEntry(uint32_t Num, const BasicBlock &CurrBlock);
    bool exists(Value *V) const;
    void add(Value *V, uint32_t num);
    void clear();
    void erase(Value *v);
    void setAliasAnalysis(AliasAnalysis *A) { AA = A; }
    AliasAnalysis *getAliasAnalysis() const { return AA; }
    void setMemDep(MemoryDependenceResults *M) { MD = M; }
    void setDomTree(DominatorTree *D) { DT = D; }
    uint32_t getNextUnusedValueNumber() { return nextValueNumber; }
    void verifyRemoved(const Value *) const;
  };

private:
  friend class gvn::GVNLegacyPass;
  friend struct DenseMapInfo<Expression>;

  MemoryDependenceResults *MD;
  DominatorTree *DT;
  const TargetLibraryInfo *TLI;
  AssumptionCache *AC;
  SetVector<BasicBlock *> DeadBlocks;
  OptimizationRemarkEmitter *ORE;
  ImplicitControlFlowTracking *ICF;

  ValueTable VN;

  /// A mapping from value numbers to lists of Value*'s that
  /// have that value number.  Use findLeader to query it.
  struct LeaderTableEntry {
    Value *Val;
    const BasicBlock *BB;
    LeaderTableEntry *Next;
  };
  DenseMap<uint32_t, LeaderTableEntry> LeaderTable;
  BumpPtrAllocator TableAllocator;

  // Block-local map of equivalent values to their leader, does not
  // propagate to any successors. Entries added mid-block are applied
  // to the remaining instructions in the block.
  SmallMapVector<Value *, Constant *, 4> ReplaceWithConstMap;
  SmallVector<Instruction *, 8> InstrsToErase;

  // Map the block to reversed postorder traversal number. It is used to
  // find back edge easily.
  DenseMap<AssertingVH<BasicBlock>, uint32_t> BlockRPONumber;

  // This is set 'true' initially and also when new blocks have been added to
  // the function being analyzed. This boolean is used to control the updating
  // of BlockRPONumber prior to accessing the contents of BlockRPONumber.
  bool InvalidBlockRPONumbers = true;

  using LoadDepVect = SmallVector<NonLocalDepResult, 64>;
  using AvailValInBlkVect = SmallVector<gvn::AvailableValueInBlock, 64>;
  using UnavailBlkVect = SmallVector<BasicBlock *, 64>;

  bool runImpl(Function &F, AssumptionCache &RunAC, DominatorTree &RunDT,
               const TargetLibraryInfo &RunTLI, AAResults &RunAA,
               MemoryDependenceResults *RunMD, LoopInfo *LI,
               OptimizationRemarkEmitter *ORE);

  /// Push a new Value to the LeaderTable onto the list for its value number.
  void addToLeaderTable(uint32_t N, Value *V, const BasicBlock *BB) {
    LeaderTableEntry &Curr = LeaderTable[N];
    if (!Curr.Val) {
      Curr.Val = V;
      Curr.BB = BB;
      return;
    }

    LeaderTableEntry *Node = TableAllocator.Allocate<LeaderTableEntry>();
    Node->Val = V;
    Node->BB = BB;
    Node->Next = Curr.Next;
    Curr.Next = Node;
  }

  /// Scan the list of values corresponding to a given
  /// value number, and remove the given instruction if encountered.
  void removeFromLeaderTable(uint32_t N, Instruction *I, BasicBlock *BB) {
    LeaderTableEntry *Prev = nullptr;
    LeaderTableEntry *Curr = &LeaderTable[N];

    while (Curr && (Curr->Val != I || Curr->BB != BB)) {
      Prev = Curr;
      Curr = Curr->Next;
    }

    if (!Curr)
      return;

    if (Prev) {
      Prev->Next = Curr->Next;
    } else {
      if (!Curr->Next) {
        Curr->Val = nullptr;
        Curr->BB = nullptr;
      } else {
        LeaderTableEntry *Next = Curr->Next;
        Curr->Val = Next->Val;
        Curr->BB = Next->BB;
        Curr->Next = Next->Next;
      }
    }
  }

  // List of critical edges to be split between iterations.
  SmallVector<std::pair<Instruction *, unsigned>, 4> toSplit;

  // Helper functions of redundant load elimination
  bool processLoad(LoadInst *L);
  bool processNonLocalLoad(LoadInst *L);
  bool processAssumeIntrinsic(IntrinsicInst *II);

  /// Given a local dependency (Def or Clobber) determine if a value is
  /// available for the load.  Returns true if an value is known to be
  /// available and populates Res.  Returns false otherwise.
  bool AnalyzeLoadAvailability(LoadInst *LI, MemDepResult DepInfo,
                               Value *Address, gvn::AvailableValue &Res);

  /// Given a list of non-local dependencies, determine if a value is
  /// available for the load in each specified block.  If it is, add it to
  /// ValuesPerBlock.  If not, add it to UnavailableBlocks.
  void AnalyzeLoadAvailability(LoadInst *LI, LoadDepVect &Deps,
                               AvailValInBlkVect &ValuesPerBlock,
                               UnavailBlkVect &UnavailableBlocks);

  bool PerformLoadPRE(LoadInst *LI, AvailValInBlkVect &ValuesPerBlock,
                      UnavailBlkVect &UnavailableBlocks);

  // Other helper routines
  bool processInstruction(Instruction *I);
  bool processBlock(BasicBlock *BB);
  void dump(DenseMap<uint32_t, Value *> &d) const;
  bool iterateOnFunction(Function &F);
  bool performPRE(Function &F);
  bool performScalarPRE(Instruction *I);
  bool performScalarPREInsertion(Instruction *Instr, BasicBlock *Pred,
                                 BasicBlock *Curr, unsigned int ValNo);
  Value *findLeader(const BasicBlock *BB, uint32_t num);
  void cleanupGlobalSets();
  void fillImplicitControlFlowInfo(BasicBlock *BB);
  void verifyRemoved(const Instruction *I) const;
  bool splitCriticalEdges();
  BasicBlock *splitCriticalEdges(BasicBlock *Pred, BasicBlock *Succ);
  bool replaceOperandsWithConsts(Instruction *I) const;
  bool propagateEquality(Value *LHS, Value *RHS, const BasicBlockEdge &Root,
                         bool DominatesByEdge);
  bool processFoldableCondBr(BranchInst *BI);
  void addDeadBlock(BasicBlock *BB);
  void assignValNumForDeadCode();
  void assignBlockRPONumber(Function &F);
};

/// Create a legacy GVN pass. This also allows parameterizing whether or not
/// loads are eliminated by the pass.
FunctionPass *createGVNPass(bool NoLoads = false);

/// A simple and fast domtree-based GVN pass to hoist common expressions
/// from sibling branches.
struct GVNHoistPass : PassInfoMixin<GVNHoistPass> {
  /// Run the pass over the function.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Uses an "inverted" value numbering to decide the similarity of
/// expressions and sinks similar expressions into successors.
struct GVNSinkPass : PassInfoMixin<GVNSinkPass> {
  /// Run the pass over the function.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_GVN_H
