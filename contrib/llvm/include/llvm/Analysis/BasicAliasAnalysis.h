//===- BasicAliasAnalysis.h - Stateless, local Alias Analysis ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This is the interface for LLVM's primary stateless and local alias analysis.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_BASICALIASANALYSIS_H
#define LLVM_ANALYSIS_BASICALIASANALYSIS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

namespace llvm {

struct AAMDNodes;
class APInt;
class AssumptionCache;
class BasicBlock;
class DataLayout;
class DominatorTree;
class Function;
class GEPOperator;
class LoopInfo;
class PHINode;
class SelectInst;
class TargetLibraryInfo;
class PhiValues;
class Value;

/// This is the AA result object for the basic, local, and stateless alias
/// analysis. It implements the AA query interface in an entirely stateless
/// manner. As one consequence, it is never invalidated due to IR changes.
/// While it does retain some storage, that is used as an optimization and not
/// to preserve information from query to query. However it does retain handles
/// to various other analyses and must be recomputed when those analyses are.
class BasicAAResult : public AAResultBase<BasicAAResult> {
  friend AAResultBase<BasicAAResult>;

  const DataLayout &DL;
  const Function &F;
  const TargetLibraryInfo &TLI;
  AssumptionCache &AC;
  DominatorTree *DT;
  LoopInfo *LI;
  PhiValues *PV;

public:
  BasicAAResult(const DataLayout &DL, const Function &F,
                const TargetLibraryInfo &TLI, AssumptionCache &AC,
                DominatorTree *DT = nullptr, LoopInfo *LI = nullptr,
                PhiValues *PV = nullptr)
      : AAResultBase(), DL(DL), F(F), TLI(TLI), AC(AC), DT(DT), LI(LI), PV(PV)
        {}

  BasicAAResult(const BasicAAResult &Arg)
      : AAResultBase(Arg), DL(Arg.DL), F(Arg.F), TLI(Arg.TLI), AC(Arg.AC),
        DT(Arg.DT),  LI(Arg.LI), PV(Arg.PV) {}
  BasicAAResult(BasicAAResult &&Arg)
      : AAResultBase(std::move(Arg)), DL(Arg.DL), F(Arg.F), TLI(Arg.TLI),
        AC(Arg.AC), DT(Arg.DT), LI(Arg.LI), PV(Arg.PV) {}

  /// Handle invalidation events in the new pass manager.
  bool invalidate(Function &Fn, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &Inv);

  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB);

  ModRefInfo getModRefInfo(const CallBase *Call, const MemoryLocation &Loc);

  ModRefInfo getModRefInfo(const CallBase *Call1, const CallBase *Call2);

  /// Chases pointers until we find a (constant global) or not.
  bool pointsToConstantMemory(const MemoryLocation &Loc, bool OrLocal);

  /// Get the location associated with a pointer argument of a callsite.
  ModRefInfo getArgModRefInfo(const CallBase *Call, unsigned ArgIdx);

  /// Returns the behavior when calling the given call site.
  FunctionModRefBehavior getModRefBehavior(const CallBase *Call);

  /// Returns the behavior when calling the given function. For use when the
  /// call site is not known.
  FunctionModRefBehavior getModRefBehavior(const Function *Fn);

private:
  // A linear transformation of a Value; this class represents ZExt(SExt(V,
  // SExtBits), ZExtBits) * Scale + Offset.
  struct VariableGEPIndex {
    // An opaque Value - we can't decompose this further.
    const Value *V;

    // We need to track what extensions we've done as we consider the same Value
    // with different extensions as different variables in a GEP's linear
    // expression;
    // e.g.: if V == -1, then sext(x) != zext(x).
    unsigned ZExtBits;
    unsigned SExtBits;

    APInt Scale;

    bool operator==(const VariableGEPIndex &Other) const {
      return V == Other.V && ZExtBits == Other.ZExtBits &&
             SExtBits == Other.SExtBits && Scale == Other.Scale;
    }

    bool operator!=(const VariableGEPIndex &Other) const {
      return !operator==(Other);
    }
  };

  // Represents the internal structure of a GEP, decomposed into a base pointer,
  // constant offsets, and variable scaled indices.
  struct DecomposedGEP {
    // Base pointer of the GEP
    const Value *Base;
    // Total constant offset w.r.t the base from indexing into structs
    APInt StructOffset;
    // Total constant offset w.r.t the base from indexing through
    // pointers/arrays/vectors
    APInt OtherOffset;
    // Scaled variable (non-constant) indices.
    SmallVector<VariableGEPIndex, 4> VarIndices;
  };

  /// Track alias queries to guard against recursion.
  using LocPair = std::pair<MemoryLocation, MemoryLocation>;
  using AliasCacheTy = SmallDenseMap<LocPair, AliasResult, 8>;
  AliasCacheTy AliasCache;

  /// Tracks phi nodes we have visited.
  ///
  /// When interpret "Value" pointer equality as value equality we need to make
  /// sure that the "Value" is not part of a cycle. Otherwise, two uses could
  /// come from different "iterations" of a cycle and see different values for
  /// the same "Value" pointer.
  ///
  /// The following example shows the problem:
  ///   %p = phi(%alloca1, %addr2)
  ///   %l = load %ptr
  ///   %addr1 = gep, %alloca2, 0, %l
  ///   %addr2 = gep  %alloca2, 0, (%l + 1)
  ///      alias(%p, %addr1) -> MayAlias !
  ///   store %l, ...
  SmallPtrSet<const BasicBlock *, 8> VisitedPhiBBs;

  /// Tracks instructions visited by pointsToConstantMemory.
  SmallPtrSet<const Value *, 16> Visited;

  static const Value *
  GetLinearExpression(const Value *V, APInt &Scale, APInt &Offset,
                      unsigned &ZExtBits, unsigned &SExtBits,
                      const DataLayout &DL, unsigned Depth, AssumptionCache *AC,
                      DominatorTree *DT, bool &NSW, bool &NUW);

  static bool DecomposeGEPExpression(const Value *V, DecomposedGEP &Decomposed,
      const DataLayout &DL, AssumptionCache *AC, DominatorTree *DT);

  static bool isGEPBaseAtNegativeOffset(const GEPOperator *GEPOp,
      const DecomposedGEP &DecompGEP, const DecomposedGEP &DecompObject,
      LocationSize ObjectAccessSize);

  /// A Heuristic for aliasGEP that searches for a constant offset
  /// between the variables.
  ///
  /// GetLinearExpression has some limitations, as generally zext(%x + 1)
  /// != zext(%x) + zext(1) if the arithmetic overflows. GetLinearExpression
  /// will therefore conservatively refuse to decompose these expressions.
  /// However, we know that, for all %x, zext(%x) != zext(%x + 1), even if
  /// the addition overflows.
  bool
  constantOffsetHeuristic(const SmallVectorImpl<VariableGEPIndex> &VarIndices,
                          LocationSize V1Size, LocationSize V2Size,
                          APInt BaseOffset, AssumptionCache *AC,
                          DominatorTree *DT);

  bool isValueEqualInPotentialCycles(const Value *V1, const Value *V2);

  void GetIndexDifference(SmallVectorImpl<VariableGEPIndex> &Dest,
                          const SmallVectorImpl<VariableGEPIndex> &Src);

  AliasResult aliasGEP(const GEPOperator *V1, LocationSize V1Size,
                       const AAMDNodes &V1AAInfo, const Value *V2,
                       LocationSize V2Size, const AAMDNodes &V2AAInfo,
                       const Value *UnderlyingV1, const Value *UnderlyingV2);

  AliasResult aliasPHI(const PHINode *PN, LocationSize PNSize,
                       const AAMDNodes &PNAAInfo, const Value *V2,
                       LocationSize V2Size, const AAMDNodes &V2AAInfo,
                       const Value *UnderV2);

  AliasResult aliasSelect(const SelectInst *SI, LocationSize SISize,
                          const AAMDNodes &SIAAInfo, const Value *V2,
                          LocationSize V2Size, const AAMDNodes &V2AAInfo,
                          const Value *UnderV2);

  AliasResult aliasCheck(const Value *V1, LocationSize V1Size,
                         AAMDNodes V1AATag, const Value *V2,
                         LocationSize V2Size, AAMDNodes V2AATag,
                         const Value *O1 = nullptr, const Value *O2 = nullptr);
};

/// Analysis pass providing a never-invalidated alias analysis result.
class BasicAA : public AnalysisInfoMixin<BasicAA> {
  friend AnalysisInfoMixin<BasicAA>;

  static AnalysisKey Key;

public:
  using Result = BasicAAResult;

  BasicAAResult run(Function &F, FunctionAnalysisManager &AM);
};

/// Legacy wrapper pass to provide the BasicAAResult object.
class BasicAAWrapperPass : public FunctionPass {
  std::unique_ptr<BasicAAResult> Result;

  virtual void anchor();

public:
  static char ID;

  BasicAAWrapperPass();

  BasicAAResult &getResult() { return *Result; }
  const BasicAAResult &getResult() const { return *Result; }

  bool runOnFunction(Function &F) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

FunctionPass *createBasicAAWrapperPass();

/// A helper for the legacy pass manager to create a \c BasicAAResult object
/// populated to the best of our ability for a particular function when inside
/// of a \c ModulePass or a \c CallGraphSCCPass.
BasicAAResult createLegacyPMBasicAAResult(Pass &P, Function &F);

/// This class is a functor to be used in legacy module or SCC passes for
/// computing AA results for a function. We store the results in fields so that
/// they live long enough to be queried, but we re-use them each time.
class LegacyAARGetter {
  Pass &P;
  Optional<BasicAAResult> BAR;
  Optional<AAResults> AAR;

public:
  LegacyAARGetter(Pass &P) : P(P) {}
  AAResults &operator()(Function &F) {
    BAR.emplace(createLegacyPMBasicAAResult(P, F));
    AAR.emplace(createLegacyPMAAResults(P, F, *BAR));
    return *AAR;
  }
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_BASICALIASANALYSIS_H
