//===- CFLSteensAliasAnalysis.cpp - Unification-based Alias Analysis ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a CFL-base, summary-based alias analysis algorithm. It
// does not depend on types. The algorithm is a mixture of the one described in
// "Demand-driven alias analysis for C" by Xin Zheng and Radu Rugina, and "Fast
// algorithms for Dyck-CFL-reachability with applications to Alias Analysis" by
// Zhang Q, Lyu M R, Yuan H, and Su Z. -- to summarize the papers, we build a
// graph of the uses of a variable, where each node is a memory location, and
// each edge is an action that happened on that memory location.  The "actions"
// can be one of Dereference, Reference, or Assign. The precision of this
// analysis is roughly the same as that of an one level context-sensitive
// Steensgaard's algorithm.
//
// Two variables are considered as aliasing iff you can reach one value's node
// from the other value's node and the language formed by concatenating all of
// the edge labels (actions) conforms to a context-free grammar.
//
// Because this algorithm requires a graph search on each query, we execute the
// algorithm outlined in "Fast algorithms..." (mentioned above)
// in order to transform the graph into sets of variables that may alias in
// ~nlogn time (n = number of variables), which makes queries take constant
// time.
//===----------------------------------------------------------------------===//

// N.B. AliasAnalysis as a whole is phrased as a FunctionPass at the moment, and
// CFLSteensAA is interprocedural. This is *technically* A Bad Thing, because
// FunctionPasses are only allowed to inspect the Function that they're being
// run on. Realistically, this likely isn't a problem until we allow
// FunctionPasses to run concurrently.

#include "llvm/Analysis/CFLSteensAliasAnalysis.h"
#include "AliasAnalysisSummary.h"
#include "CFLGraph.h"
#include "StratifiedSets.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <memory>
#include <utility>

using namespace llvm;
using namespace llvm::cflaa;

#define DEBUG_TYPE "cfl-steens-aa"

CFLSteensAAResult::CFLSteensAAResult(const TargetLibraryInfo &TLI)
    : AAResultBase(), TLI(TLI) {}
CFLSteensAAResult::CFLSteensAAResult(CFLSteensAAResult &&Arg)
    : AAResultBase(std::move(Arg)), TLI(Arg.TLI) {}
CFLSteensAAResult::~CFLSteensAAResult() = default;

/// Information we have about a function and would like to keep around.
class CFLSteensAAResult::FunctionInfo {
  StratifiedSets<InstantiatedValue> Sets;
  AliasSummary Summary;

public:
  FunctionInfo(Function &Fn, const SmallVectorImpl<Value *> &RetVals,
               StratifiedSets<InstantiatedValue> S);

  const StratifiedSets<InstantiatedValue> &getStratifiedSets() const {
    return Sets;
  }

  const AliasSummary &getAliasSummary() const { return Summary; }
};

const StratifiedIndex StratifiedLink::SetSentinel =
    std::numeric_limits<StratifiedIndex>::max();

//===----------------------------------------------------------------------===//
// Function declarations that require types defined in the namespace above
//===----------------------------------------------------------------------===//

/// Determines whether it would be pointless to add the given Value to our sets.
static bool canSkipAddingToSets(Value *Val) {
  // Constants can share instances, which may falsely unify multiple
  // sets, e.g. in
  // store i32* null, i32** %ptr1
  // store i32* null, i32** %ptr2
  // clearly ptr1 and ptr2 should not be unified into the same set, so
  // we should filter out the (potentially shared) instance to
  // i32* null.
  if (isa<Constant>(Val)) {
    // TODO: Because all of these things are constant, we can determine whether
    // the data is *actually* mutable at graph building time. This will probably
    // come for free/cheap with offset awareness.
    bool CanStoreMutableData = isa<GlobalValue>(Val) ||
                               isa<ConstantExpr>(Val) ||
                               isa<ConstantAggregate>(Val);
    return !CanStoreMutableData;
  }

  return false;
}

CFLSteensAAResult::FunctionInfo::FunctionInfo(
    Function &Fn, const SmallVectorImpl<Value *> &RetVals,
    StratifiedSets<InstantiatedValue> S)
    : Sets(std::move(S)) {
  // Historically, an arbitrary upper-bound of 50 args was selected. We may want
  // to remove this if it doesn't really matter in practice.
  if (Fn.arg_size() > MaxSupportedArgsInSummary)
    return;

  DenseMap<StratifiedIndex, InterfaceValue> InterfaceMap;

  // Our intention here is to record all InterfaceValues that share the same
  // StratifiedIndex in RetParamRelations. For each valid InterfaceValue, we
  // have its StratifiedIndex scanned here and check if the index is presented
  // in InterfaceMap: if it is not, we add the correspondence to the map;
  // otherwise, an aliasing relation is found and we add it to
  // RetParamRelations.

  auto AddToRetParamRelations = [&](unsigned InterfaceIndex,
                                    StratifiedIndex SetIndex) {
    unsigned Level = 0;
    while (true) {
      InterfaceValue CurrValue{InterfaceIndex, Level};

      auto Itr = InterfaceMap.find(SetIndex);
      if (Itr != InterfaceMap.end()) {
        if (CurrValue != Itr->second)
          Summary.RetParamRelations.push_back(
              ExternalRelation{CurrValue, Itr->second, UnknownOffset});
        break;
      }

      auto &Link = Sets.getLink(SetIndex);
      InterfaceMap.insert(std::make_pair(SetIndex, CurrValue));
      auto ExternalAttrs = getExternallyVisibleAttrs(Link.Attrs);
      if (ExternalAttrs.any())
        Summary.RetParamAttributes.push_back(
            ExternalAttribute{CurrValue, ExternalAttrs});

      if (!Link.hasBelow())
        break;

      ++Level;
      SetIndex = Link.Below;
    }
  };

  // Populate RetParamRelations for return values
  for (auto *RetVal : RetVals) {
    assert(RetVal != nullptr);
    assert(RetVal->getType()->isPointerTy());
    auto RetInfo = Sets.find(InstantiatedValue{RetVal, 0});
    if (RetInfo.hasValue())
      AddToRetParamRelations(0, RetInfo->Index);
  }

  // Populate RetParamRelations for parameters
  unsigned I = 0;
  for (auto &Param : Fn.args()) {
    if (Param.getType()->isPointerTy()) {
      auto ParamInfo = Sets.find(InstantiatedValue{&Param, 0});
      if (ParamInfo.hasValue())
        AddToRetParamRelations(I + 1, ParamInfo->Index);
    }
    ++I;
  }
}

// Builds the graph + StratifiedSets for a function.
CFLSteensAAResult::FunctionInfo CFLSteensAAResult::buildSetsFrom(Function *Fn) {
  CFLGraphBuilder<CFLSteensAAResult> GraphBuilder(*this, TLI, *Fn);
  StratifiedSetsBuilder<InstantiatedValue> SetBuilder;

  // Add all CFLGraph nodes and all Dereference edges to StratifiedSets
  auto &Graph = GraphBuilder.getCFLGraph();
  for (const auto &Mapping : Graph.value_mappings()) {
    auto Val = Mapping.first;
    if (canSkipAddingToSets(Val))
      continue;
    auto &ValueInfo = Mapping.second;

    assert(ValueInfo.getNumLevels() > 0);
    SetBuilder.add(InstantiatedValue{Val, 0});
    SetBuilder.noteAttributes(InstantiatedValue{Val, 0},
                              ValueInfo.getNodeInfoAtLevel(0).Attr);
    for (unsigned I = 0, E = ValueInfo.getNumLevels() - 1; I < E; ++I) {
      SetBuilder.add(InstantiatedValue{Val, I + 1});
      SetBuilder.noteAttributes(InstantiatedValue{Val, I + 1},
                                ValueInfo.getNodeInfoAtLevel(I + 1).Attr);
      SetBuilder.addBelow(InstantiatedValue{Val, I},
                          InstantiatedValue{Val, I + 1});
    }
  }

  // Add all assign edges to StratifiedSets
  for (const auto &Mapping : Graph.value_mappings()) {
    auto Val = Mapping.first;
    if (canSkipAddingToSets(Val))
      continue;
    auto &ValueInfo = Mapping.second;

    for (unsigned I = 0, E = ValueInfo.getNumLevels(); I < E; ++I) {
      auto Src = InstantiatedValue{Val, I};
      for (auto &Edge : ValueInfo.getNodeInfoAtLevel(I).Edges)
        SetBuilder.addWith(Src, Edge.Other);
    }
  }

  return FunctionInfo(*Fn, GraphBuilder.getReturnValues(), SetBuilder.build());
}

void CFLSteensAAResult::scan(Function *Fn) {
  auto InsertPair = Cache.insert(std::make_pair(Fn, Optional<FunctionInfo>()));
  (void)InsertPair;
  assert(InsertPair.second &&
         "Trying to scan a function that has already been cached");

  // Note that we can't do Cache[Fn] = buildSetsFrom(Fn) here: the function call
  // may get evaluated after operator[], potentially triggering a DenseMap
  // resize and invalidating the reference returned by operator[]
  auto FunInfo = buildSetsFrom(Fn);
  Cache[Fn] = std::move(FunInfo);

  Handles.emplace_front(Fn, this);
}

void CFLSteensAAResult::evict(Function *Fn) { Cache.erase(Fn); }

/// Ensures that the given function is available in the cache, and returns the
/// entry.
const Optional<CFLSteensAAResult::FunctionInfo> &
CFLSteensAAResult::ensureCached(Function *Fn) {
  auto Iter = Cache.find(Fn);
  if (Iter == Cache.end()) {
    scan(Fn);
    Iter = Cache.find(Fn);
    assert(Iter != Cache.end());
    assert(Iter->second.hasValue());
  }
  return Iter->second;
}

const AliasSummary *CFLSteensAAResult::getAliasSummary(Function &Fn) {
  auto &FunInfo = ensureCached(&Fn);
  if (FunInfo.hasValue())
    return &FunInfo->getAliasSummary();
  else
    return nullptr;
}

AliasResult CFLSteensAAResult::query(const MemoryLocation &LocA,
                                     const MemoryLocation &LocB) {
  auto *ValA = const_cast<Value *>(LocA.Ptr);
  auto *ValB = const_cast<Value *>(LocB.Ptr);

  if (!ValA->getType()->isPointerTy() || !ValB->getType()->isPointerTy())
    return NoAlias;

  Function *Fn = nullptr;
  Function *MaybeFnA = const_cast<Function *>(parentFunctionOfValue(ValA));
  Function *MaybeFnB = const_cast<Function *>(parentFunctionOfValue(ValB));
  if (!MaybeFnA && !MaybeFnB) {
    // The only times this is known to happen are when globals + InlineAsm are
    // involved
    LLVM_DEBUG(
        dbgs()
        << "CFLSteensAA: could not extract parent function information.\n");
    return MayAlias;
  }

  if (MaybeFnA) {
    Fn = MaybeFnA;
    assert((!MaybeFnB || MaybeFnB == MaybeFnA) &&
           "Interprocedural queries not supported");
  } else {
    Fn = MaybeFnB;
  }

  assert(Fn != nullptr);
  auto &MaybeInfo = ensureCached(Fn);
  assert(MaybeInfo.hasValue());

  auto &Sets = MaybeInfo->getStratifiedSets();
  auto MaybeA = Sets.find(InstantiatedValue{ValA, 0});
  if (!MaybeA.hasValue())
    return MayAlias;

  auto MaybeB = Sets.find(InstantiatedValue{ValB, 0});
  if (!MaybeB.hasValue())
    return MayAlias;

  auto SetA = *MaybeA;
  auto SetB = *MaybeB;
  auto AttrsA = Sets.getLink(SetA.Index).Attrs;
  auto AttrsB = Sets.getLink(SetB.Index).Attrs;

  // If both values are local (meaning the corresponding set has attribute
  // AttrNone or AttrEscaped), then we know that CFLSteensAA fully models them:
  // they may-alias each other if and only if they are in the same set.
  // If at least one value is non-local (meaning it either is global/argument or
  // it comes from unknown sources like integer cast), the situation becomes a
  // bit more interesting. We follow three general rules described below:
  // - Non-local values may alias each other
  // - AttrNone values do not alias any non-local values
  // - AttrEscaped do not alias globals/arguments, but they may alias
  // AttrUnknown values
  if (SetA.Index == SetB.Index)
    return MayAlias;
  if (AttrsA.none() || AttrsB.none())
    return NoAlias;
  if (hasUnknownOrCallerAttr(AttrsA) || hasUnknownOrCallerAttr(AttrsB))
    return MayAlias;
  if (isGlobalOrArgAttr(AttrsA) && isGlobalOrArgAttr(AttrsB))
    return MayAlias;
  return NoAlias;
}

AnalysisKey CFLSteensAA::Key;

CFLSteensAAResult CFLSteensAA::run(Function &F, FunctionAnalysisManager &AM) {
  return CFLSteensAAResult(AM.getResult<TargetLibraryAnalysis>(F));
}

char CFLSteensAAWrapperPass::ID = 0;
INITIALIZE_PASS(CFLSteensAAWrapperPass, "cfl-steens-aa",
                "Unification-Based CFL Alias Analysis", false, true)

ImmutablePass *llvm::createCFLSteensAAWrapperPass() {
  return new CFLSteensAAWrapperPass();
}

CFLSteensAAWrapperPass::CFLSteensAAWrapperPass() : ImmutablePass(ID) {
  initializeCFLSteensAAWrapperPassPass(*PassRegistry::getPassRegistry());
}

void CFLSteensAAWrapperPass::initializePass() {
  auto &TLIWP = getAnalysis<TargetLibraryInfoWrapperPass>();
  Result.reset(new CFLSteensAAResult(TLIWP.getTLI()));
}

void CFLSteensAAWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<TargetLibraryInfoWrapperPass>();
}
