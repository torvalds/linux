//===- CFLAndersAliasAnalysis.cpp - Unification-based Alias Analysis ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a CFL-based, summary-based alias analysis algorithm. It
// differs from CFLSteensAliasAnalysis in its inclusion-based nature while
// CFLSteensAliasAnalysis is unification-based. This pass has worse performance
// than CFLSteensAliasAnalysis (the worst case complexity of
// CFLAndersAliasAnalysis is cubic, while the worst case complexity of
// CFLSteensAliasAnalysis is almost linear), but it is able to yield more
// precise analysis result. The precision of this analysis is roughly the same
// as that of an one level context-sensitive Andersen's algorithm.
//
// The algorithm used here is based on recursive state machine matching scheme
// proposed in "Demand-driven alias analysis for C" by Xin Zheng and Radu
// Rugina. The general idea is to extend the traditional transitive closure
// algorithm to perform CFL matching along the way: instead of recording
// "whether X is reachable from Y", we keep track of "whether X is reachable
// from Y at state Z", where the "state" field indicates where we are in the CFL
// matching process. To understand the matching better, it is advisable to have
// the state machine shown in Figure 3 of the paper available when reading the
// codes: all we do here is to selectively expand the transitive closure by
// discarding edges that are not recognized by the state machine.
//
// There are two differences between our current implementation and the one
// described in the paper:
// - Our algorithm eagerly computes all alias pairs after the CFLGraph is built,
// while in the paper the authors did the computation in a demand-driven
// fashion. We did not implement the demand-driven algorithm due to the
// additional coding complexity and higher memory profile, but if we found it
// necessary we may switch to it eventually.
// - In the paper the authors use a state machine that does not distinguish
// value reads from value writes. For example, if Y is reachable from X at state
// S3, it may be the case that X is written into Y, or it may be the case that
// there's a third value Z that writes into both X and Y. To make that
// distinction (which is crucial in building function summary as well as
// retrieving mod-ref info), we choose to duplicate some of the states in the
// paper's proposed state machine. The duplication does not change the set the
// machine accepts. Given a pair of reachable values, it only provides more
// detailed information on which value is being written into and which is being
// read from.
//
//===----------------------------------------------------------------------===//

// N.B. AliasAnalysis as a whole is phrased as a FunctionPass at the moment, and
// CFLAndersAA is interprocedural. This is *technically* A Bad Thing, because
// FunctionPasses are only allowed to inspect the Function that they're being
// run on. Realistically, this likely isn't a problem until we allow
// FunctionPasses to run concurrently.

#include "llvm/Analysis/CFLAndersAliasAnalysis.h"
#include "AliasAnalysisSummary.h"
#include "CFLGraph.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <bitset>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::cflaa;

#define DEBUG_TYPE "cfl-anders-aa"

CFLAndersAAResult::CFLAndersAAResult(const TargetLibraryInfo &TLI) : TLI(TLI) {}
CFLAndersAAResult::CFLAndersAAResult(CFLAndersAAResult &&RHS)
    : AAResultBase(std::move(RHS)), TLI(RHS.TLI) {}
CFLAndersAAResult::~CFLAndersAAResult() = default;

namespace {

enum class MatchState : uint8_t {
  // The following state represents S1 in the paper.
  FlowFromReadOnly = 0,
  // The following two states together represent S2 in the paper.
  // The 'NoReadWrite' suffix indicates that there exists an alias path that
  // does not contain assignment and reverse assignment edges.
  // The 'ReadOnly' suffix indicates that there exists an alias path that
  // contains reverse assignment edges only.
  FlowFromMemAliasNoReadWrite,
  FlowFromMemAliasReadOnly,
  // The following two states together represent S3 in the paper.
  // The 'WriteOnly' suffix indicates that there exists an alias path that
  // contains assignment edges only.
  // The 'ReadWrite' suffix indicates that there exists an alias path that
  // contains both assignment and reverse assignment edges. Note that if X and Y
  // are reachable at 'ReadWrite' state, it does NOT mean X is both read from
  // and written to Y. Instead, it means that a third value Z is written to both
  // X and Y.
  FlowToWriteOnly,
  FlowToReadWrite,
  // The following two states together represent S4 in the paper.
  FlowToMemAliasWriteOnly,
  FlowToMemAliasReadWrite,
};

using StateSet = std::bitset<7>;

const unsigned ReadOnlyStateMask =
    (1U << static_cast<uint8_t>(MatchState::FlowFromReadOnly)) |
    (1U << static_cast<uint8_t>(MatchState::FlowFromMemAliasReadOnly));
const unsigned WriteOnlyStateMask =
    (1U << static_cast<uint8_t>(MatchState::FlowToWriteOnly)) |
    (1U << static_cast<uint8_t>(MatchState::FlowToMemAliasWriteOnly));

// A pair that consists of a value and an offset
struct OffsetValue {
  const Value *Val;
  int64_t Offset;
};

bool operator==(OffsetValue LHS, OffsetValue RHS) {
  return LHS.Val == RHS.Val && LHS.Offset == RHS.Offset;
}
bool operator<(OffsetValue LHS, OffsetValue RHS) {
  return std::less<const Value *>()(LHS.Val, RHS.Val) ||
         (LHS.Val == RHS.Val && LHS.Offset < RHS.Offset);
}

// A pair that consists of an InstantiatedValue and an offset
struct OffsetInstantiatedValue {
  InstantiatedValue IVal;
  int64_t Offset;
};

bool operator==(OffsetInstantiatedValue LHS, OffsetInstantiatedValue RHS) {
  return LHS.IVal == RHS.IVal && LHS.Offset == RHS.Offset;
}

// We use ReachabilitySet to keep track of value aliases (The nonterminal "V" in
// the paper) during the analysis.
class ReachabilitySet {
  using ValueStateMap = DenseMap<InstantiatedValue, StateSet>;
  using ValueReachMap = DenseMap<InstantiatedValue, ValueStateMap>;

  ValueReachMap ReachMap;

public:
  using const_valuestate_iterator = ValueStateMap::const_iterator;
  using const_value_iterator = ValueReachMap::const_iterator;

  // Insert edge 'From->To' at state 'State'
  bool insert(InstantiatedValue From, InstantiatedValue To, MatchState State) {
    assert(From != To);
    auto &States = ReachMap[To][From];
    auto Idx = static_cast<size_t>(State);
    if (!States.test(Idx)) {
      States.set(Idx);
      return true;
    }
    return false;
  }

  // Return the set of all ('From', 'State') pair for a given node 'To'
  iterator_range<const_valuestate_iterator>
  reachableValueAliases(InstantiatedValue V) const {
    auto Itr = ReachMap.find(V);
    if (Itr == ReachMap.end())
      return make_range<const_valuestate_iterator>(const_valuestate_iterator(),
                                                   const_valuestate_iterator());
    return make_range<const_valuestate_iterator>(Itr->second.begin(),
                                                 Itr->second.end());
  }

  iterator_range<const_value_iterator> value_mappings() const {
    return make_range<const_value_iterator>(ReachMap.begin(), ReachMap.end());
  }
};

// We use AliasMemSet to keep track of all memory aliases (the nonterminal "M"
// in the paper) during the analysis.
class AliasMemSet {
  using MemSet = DenseSet<InstantiatedValue>;
  using MemMapType = DenseMap<InstantiatedValue, MemSet>;

  MemMapType MemMap;

public:
  using const_mem_iterator = MemSet::const_iterator;

  bool insert(InstantiatedValue LHS, InstantiatedValue RHS) {
    // Top-level values can never be memory aliases because one cannot take the
    // addresses of them
    assert(LHS.DerefLevel > 0 && RHS.DerefLevel > 0);
    return MemMap[LHS].insert(RHS).second;
  }

  const MemSet *getMemoryAliases(InstantiatedValue V) const {
    auto Itr = MemMap.find(V);
    if (Itr == MemMap.end())
      return nullptr;
    return &Itr->second;
  }
};

// We use AliasAttrMap to keep track of the AliasAttr of each node.
class AliasAttrMap {
  using MapType = DenseMap<InstantiatedValue, AliasAttrs>;

  MapType AttrMap;

public:
  using const_iterator = MapType::const_iterator;

  bool add(InstantiatedValue V, AliasAttrs Attr) {
    auto &OldAttr = AttrMap[V];
    auto NewAttr = OldAttr | Attr;
    if (OldAttr == NewAttr)
      return false;
    OldAttr = NewAttr;
    return true;
  }

  AliasAttrs getAttrs(InstantiatedValue V) const {
    AliasAttrs Attr;
    auto Itr = AttrMap.find(V);
    if (Itr != AttrMap.end())
      Attr = Itr->second;
    return Attr;
  }

  iterator_range<const_iterator> mappings() const {
    return make_range<const_iterator>(AttrMap.begin(), AttrMap.end());
  }
};

struct WorkListItem {
  InstantiatedValue From;
  InstantiatedValue To;
  MatchState State;
};

struct ValueSummary {
  struct Record {
    InterfaceValue IValue;
    unsigned DerefLevel;
  };
  SmallVector<Record, 4> FromRecords, ToRecords;
};

} // end anonymous namespace

namespace llvm {

// Specialize DenseMapInfo for OffsetValue.
template <> struct DenseMapInfo<OffsetValue> {
  static OffsetValue getEmptyKey() {
    return OffsetValue{DenseMapInfo<const Value *>::getEmptyKey(),
                       DenseMapInfo<int64_t>::getEmptyKey()};
  }

  static OffsetValue getTombstoneKey() {
    return OffsetValue{DenseMapInfo<const Value *>::getTombstoneKey(),
                       DenseMapInfo<int64_t>::getEmptyKey()};
  }

  static unsigned getHashValue(const OffsetValue &OVal) {
    return DenseMapInfo<std::pair<const Value *, int64_t>>::getHashValue(
        std::make_pair(OVal.Val, OVal.Offset));
  }

  static bool isEqual(const OffsetValue &LHS, const OffsetValue &RHS) {
    return LHS == RHS;
  }
};

// Specialize DenseMapInfo for OffsetInstantiatedValue.
template <> struct DenseMapInfo<OffsetInstantiatedValue> {
  static OffsetInstantiatedValue getEmptyKey() {
    return OffsetInstantiatedValue{
        DenseMapInfo<InstantiatedValue>::getEmptyKey(),
        DenseMapInfo<int64_t>::getEmptyKey()};
  }

  static OffsetInstantiatedValue getTombstoneKey() {
    return OffsetInstantiatedValue{
        DenseMapInfo<InstantiatedValue>::getTombstoneKey(),
        DenseMapInfo<int64_t>::getEmptyKey()};
  }

  static unsigned getHashValue(const OffsetInstantiatedValue &OVal) {
    return DenseMapInfo<std::pair<InstantiatedValue, int64_t>>::getHashValue(
        std::make_pair(OVal.IVal, OVal.Offset));
  }

  static bool isEqual(const OffsetInstantiatedValue &LHS,
                      const OffsetInstantiatedValue &RHS) {
    return LHS == RHS;
  }
};

} // end namespace llvm

class CFLAndersAAResult::FunctionInfo {
  /// Map a value to other values that may alias it
  /// Since the alias relation is symmetric, to save some space we assume values
  /// are properly ordered: if a and b alias each other, and a < b, then b is in
  /// AliasMap[a] but not vice versa.
  DenseMap<const Value *, std::vector<OffsetValue>> AliasMap;

  /// Map a value to its corresponding AliasAttrs
  DenseMap<const Value *, AliasAttrs> AttrMap;

  /// Summary of externally visible effects.
  AliasSummary Summary;

  Optional<AliasAttrs> getAttrs(const Value *) const;

public:
  FunctionInfo(const Function &, const SmallVectorImpl<Value *> &,
               const ReachabilitySet &, const AliasAttrMap &);

  bool mayAlias(const Value *, LocationSize, const Value *, LocationSize) const;
  const AliasSummary &getAliasSummary() const { return Summary; }
};

static bool hasReadOnlyState(StateSet Set) {
  return (Set & StateSet(ReadOnlyStateMask)).any();
}

static bool hasWriteOnlyState(StateSet Set) {
  return (Set & StateSet(WriteOnlyStateMask)).any();
}

static Optional<InterfaceValue>
getInterfaceValue(InstantiatedValue IValue,
                  const SmallVectorImpl<Value *> &RetVals) {
  auto Val = IValue.Val;

  Optional<unsigned> Index;
  if (auto Arg = dyn_cast<Argument>(Val))
    Index = Arg->getArgNo() + 1;
  else if (is_contained(RetVals, Val))
    Index = 0;

  if (Index)
    return InterfaceValue{*Index, IValue.DerefLevel};
  return None;
}

static void populateAttrMap(DenseMap<const Value *, AliasAttrs> &AttrMap,
                            const AliasAttrMap &AMap) {
  for (const auto &Mapping : AMap.mappings()) {
    auto IVal = Mapping.first;

    // Insert IVal into the map
    auto &Attr = AttrMap[IVal.Val];
    // AttrMap only cares about top-level values
    if (IVal.DerefLevel == 0)
      Attr |= Mapping.second;
  }
}

static void
populateAliasMap(DenseMap<const Value *, std::vector<OffsetValue>> &AliasMap,
                 const ReachabilitySet &ReachSet) {
  for (const auto &OuterMapping : ReachSet.value_mappings()) {
    // AliasMap only cares about top-level values
    if (OuterMapping.first.DerefLevel > 0)
      continue;

    auto Val = OuterMapping.first.Val;
    auto &AliasList = AliasMap[Val];
    for (const auto &InnerMapping : OuterMapping.second) {
      // Again, AliasMap only cares about top-level values
      if (InnerMapping.first.DerefLevel == 0)
        AliasList.push_back(OffsetValue{InnerMapping.first.Val, UnknownOffset});
    }

    // Sort AliasList for faster lookup
    llvm::sort(AliasList);
  }
}

static void populateExternalRelations(
    SmallVectorImpl<ExternalRelation> &ExtRelations, const Function &Fn,
    const SmallVectorImpl<Value *> &RetVals, const ReachabilitySet &ReachSet) {
  // If a function only returns one of its argument X, then X will be both an
  // argument and a return value at the same time. This is an edge case that
  // needs special handling here.
  for (const auto &Arg : Fn.args()) {
    if (is_contained(RetVals, &Arg)) {
      auto ArgVal = InterfaceValue{Arg.getArgNo() + 1, 0};
      auto RetVal = InterfaceValue{0, 0};
      ExtRelations.push_back(ExternalRelation{ArgVal, RetVal, 0});
    }
  }

  // Below is the core summary construction logic.
  // A naive solution of adding only the value aliases that are parameters or
  // return values in ReachSet to the summary won't work: It is possible that a
  // parameter P is written into an intermediate value I, and the function
  // subsequently returns *I. In that case, *I is does not value alias anything
  // in ReachSet, and the naive solution will miss a summary edge from (P, 1) to
  // (I, 1).
  // To account for the aforementioned case, we need to check each non-parameter
  // and non-return value for the possibility of acting as an intermediate.
  // 'ValueMap' here records, for each value, which InterfaceValues read from or
  // write into it. If both the read list and the write list of a given value
  // are non-empty, we know that a particular value is an intermidate and we
  // need to add summary edges from the writes to the reads.
  DenseMap<Value *, ValueSummary> ValueMap;
  for (const auto &OuterMapping : ReachSet.value_mappings()) {
    if (auto Dst = getInterfaceValue(OuterMapping.first, RetVals)) {
      for (const auto &InnerMapping : OuterMapping.second) {
        // If Src is a param/return value, we get a same-level assignment.
        if (auto Src = getInterfaceValue(InnerMapping.first, RetVals)) {
          // This may happen if both Dst and Src are return values
          if (*Dst == *Src)
            continue;

          if (hasReadOnlyState(InnerMapping.second))
            ExtRelations.push_back(ExternalRelation{*Dst, *Src, UnknownOffset});
          // No need to check for WriteOnly state, since ReachSet is symmetric
        } else {
          // If Src is not a param/return, add it to ValueMap
          auto SrcIVal = InnerMapping.first;
          if (hasReadOnlyState(InnerMapping.second))
            ValueMap[SrcIVal.Val].FromRecords.push_back(
                ValueSummary::Record{*Dst, SrcIVal.DerefLevel});
          if (hasWriteOnlyState(InnerMapping.second))
            ValueMap[SrcIVal.Val].ToRecords.push_back(
                ValueSummary::Record{*Dst, SrcIVal.DerefLevel});
        }
      }
    }
  }

  for (const auto &Mapping : ValueMap) {
    for (const auto &FromRecord : Mapping.second.FromRecords) {
      for (const auto &ToRecord : Mapping.second.ToRecords) {
        auto ToLevel = ToRecord.DerefLevel;
        auto FromLevel = FromRecord.DerefLevel;
        // Same-level assignments should have already been processed by now
        if (ToLevel == FromLevel)
          continue;

        auto SrcIndex = FromRecord.IValue.Index;
        auto SrcLevel = FromRecord.IValue.DerefLevel;
        auto DstIndex = ToRecord.IValue.Index;
        auto DstLevel = ToRecord.IValue.DerefLevel;
        if (ToLevel > FromLevel)
          SrcLevel += ToLevel - FromLevel;
        else
          DstLevel += FromLevel - ToLevel;

        ExtRelations.push_back(ExternalRelation{
            InterfaceValue{SrcIndex, SrcLevel},
            InterfaceValue{DstIndex, DstLevel}, UnknownOffset});
      }
    }
  }

  // Remove duplicates in ExtRelations
  llvm::sort(ExtRelations);
  ExtRelations.erase(std::unique(ExtRelations.begin(), ExtRelations.end()),
                     ExtRelations.end());
}

static void populateExternalAttributes(
    SmallVectorImpl<ExternalAttribute> &ExtAttributes, const Function &Fn,
    const SmallVectorImpl<Value *> &RetVals, const AliasAttrMap &AMap) {
  for (const auto &Mapping : AMap.mappings()) {
    if (auto IVal = getInterfaceValue(Mapping.first, RetVals)) {
      auto Attr = getExternallyVisibleAttrs(Mapping.second);
      if (Attr.any())
        ExtAttributes.push_back(ExternalAttribute{*IVal, Attr});
    }
  }
}

CFLAndersAAResult::FunctionInfo::FunctionInfo(
    const Function &Fn, const SmallVectorImpl<Value *> &RetVals,
    const ReachabilitySet &ReachSet, const AliasAttrMap &AMap) {
  populateAttrMap(AttrMap, AMap);
  populateExternalAttributes(Summary.RetParamAttributes, Fn, RetVals, AMap);
  populateAliasMap(AliasMap, ReachSet);
  populateExternalRelations(Summary.RetParamRelations, Fn, RetVals, ReachSet);
}

Optional<AliasAttrs>
CFLAndersAAResult::FunctionInfo::getAttrs(const Value *V) const {
  assert(V != nullptr);

  auto Itr = AttrMap.find(V);
  if (Itr != AttrMap.end())
    return Itr->second;
  return None;
}

bool CFLAndersAAResult::FunctionInfo::mayAlias(
    const Value *LHS, LocationSize MaybeLHSSize, const Value *RHS,
    LocationSize MaybeRHSSize) const {
  assert(LHS && RHS);

  // Check if we've seen LHS and RHS before. Sometimes LHS or RHS can be created
  // after the analysis gets executed, and we want to be conservative in those
  // cases.
  auto MaybeAttrsA = getAttrs(LHS);
  auto MaybeAttrsB = getAttrs(RHS);
  if (!MaybeAttrsA || !MaybeAttrsB)
    return true;

  // Check AliasAttrs before AliasMap lookup since it's cheaper
  auto AttrsA = *MaybeAttrsA;
  auto AttrsB = *MaybeAttrsB;
  if (hasUnknownOrCallerAttr(AttrsA))
    return AttrsB.any();
  if (hasUnknownOrCallerAttr(AttrsB))
    return AttrsA.any();
  if (isGlobalOrArgAttr(AttrsA))
    return isGlobalOrArgAttr(AttrsB);
  if (isGlobalOrArgAttr(AttrsB))
    return isGlobalOrArgAttr(AttrsA);

  // At this point both LHS and RHS should point to locally allocated objects

  auto Itr = AliasMap.find(LHS);
  if (Itr != AliasMap.end()) {

    // Find out all (X, Offset) where X == RHS
    auto Comparator = [](OffsetValue LHS, OffsetValue RHS) {
      return std::less<const Value *>()(LHS.Val, RHS.Val);
    };
#ifdef EXPENSIVE_CHECKS
    assert(std::is_sorted(Itr->second.begin(), Itr->second.end(), Comparator));
#endif
    auto RangePair = std::equal_range(Itr->second.begin(), Itr->second.end(),
                                      OffsetValue{RHS, 0}, Comparator);

    if (RangePair.first != RangePair.second) {
      // Be conservative about unknown sizes
      if (MaybeLHSSize == LocationSize::unknown() ||
          MaybeRHSSize == LocationSize::unknown())
        return true;

      const uint64_t LHSSize = MaybeLHSSize.getValue();
      const uint64_t RHSSize = MaybeRHSSize.getValue();

      for (const auto &OVal : make_range(RangePair)) {
        // Be conservative about UnknownOffset
        if (OVal.Offset == UnknownOffset)
          return true;

        // We know that LHS aliases (RHS + OVal.Offset) if the control flow
        // reaches here. The may-alias query essentially becomes integer
        // range-overlap queries over two ranges [OVal.Offset, OVal.Offset +
        // LHSSize) and [0, RHSSize).

        // Try to be conservative on super large offsets
        if (LLVM_UNLIKELY(LHSSize > INT64_MAX || RHSSize > INT64_MAX))
          return true;

        auto LHSStart = OVal.Offset;
        // FIXME: Do we need to guard against integer overflow?
        auto LHSEnd = OVal.Offset + static_cast<int64_t>(LHSSize);
        auto RHSStart = 0;
        auto RHSEnd = static_cast<int64_t>(RHSSize);
        if (LHSEnd > RHSStart && LHSStart < RHSEnd)
          return true;
      }
    }
  }

  return false;
}

static void propagate(InstantiatedValue From, InstantiatedValue To,
                      MatchState State, ReachabilitySet &ReachSet,
                      std::vector<WorkListItem> &WorkList) {
  if (From == To)
    return;
  if (ReachSet.insert(From, To, State))
    WorkList.push_back(WorkListItem{From, To, State});
}

static void initializeWorkList(std::vector<WorkListItem> &WorkList,
                               ReachabilitySet &ReachSet,
                               const CFLGraph &Graph) {
  for (const auto &Mapping : Graph.value_mappings()) {
    auto Val = Mapping.first;
    auto &ValueInfo = Mapping.second;
    assert(ValueInfo.getNumLevels() > 0);

    // Insert all immediate assignment neighbors to the worklist
    for (unsigned I = 0, E = ValueInfo.getNumLevels(); I < E; ++I) {
      auto Src = InstantiatedValue{Val, I};
      // If there's an assignment edge from X to Y, it means Y is reachable from
      // X at S2 and X is reachable from Y at S1
      for (auto &Edge : ValueInfo.getNodeInfoAtLevel(I).Edges) {
        propagate(Edge.Other, Src, MatchState::FlowFromReadOnly, ReachSet,
                  WorkList);
        propagate(Src, Edge.Other, MatchState::FlowToWriteOnly, ReachSet,
                  WorkList);
      }
    }
  }
}

static Optional<InstantiatedValue> getNodeBelow(const CFLGraph &Graph,
                                                InstantiatedValue V) {
  auto NodeBelow = InstantiatedValue{V.Val, V.DerefLevel + 1};
  if (Graph.getNode(NodeBelow))
    return NodeBelow;
  return None;
}

static void processWorkListItem(const WorkListItem &Item, const CFLGraph &Graph,
                                ReachabilitySet &ReachSet, AliasMemSet &MemSet,
                                std::vector<WorkListItem> &WorkList) {
  auto FromNode = Item.From;
  auto ToNode = Item.To;

  auto NodeInfo = Graph.getNode(ToNode);
  assert(NodeInfo != nullptr);

  // TODO: propagate field offsets

  // FIXME: Here is a neat trick we can do: since both ReachSet and MemSet holds
  // relations that are symmetric, we could actually cut the storage by half by
  // sorting FromNode and ToNode before insertion happens.

  // The newly added value alias pair may potentially generate more memory
  // alias pairs. Check for them here.
  auto FromNodeBelow = getNodeBelow(Graph, FromNode);
  auto ToNodeBelow = getNodeBelow(Graph, ToNode);
  if (FromNodeBelow && ToNodeBelow &&
      MemSet.insert(*FromNodeBelow, *ToNodeBelow)) {
    propagate(*FromNodeBelow, *ToNodeBelow,
              MatchState::FlowFromMemAliasNoReadWrite, ReachSet, WorkList);
    for (const auto &Mapping : ReachSet.reachableValueAliases(*FromNodeBelow)) {
      auto Src = Mapping.first;
      auto MemAliasPropagate = [&](MatchState FromState, MatchState ToState) {
        if (Mapping.second.test(static_cast<size_t>(FromState)))
          propagate(Src, *ToNodeBelow, ToState, ReachSet, WorkList);
      };

      MemAliasPropagate(MatchState::FlowFromReadOnly,
                        MatchState::FlowFromMemAliasReadOnly);
      MemAliasPropagate(MatchState::FlowToWriteOnly,
                        MatchState::FlowToMemAliasWriteOnly);
      MemAliasPropagate(MatchState::FlowToReadWrite,
                        MatchState::FlowToMemAliasReadWrite);
    }
  }

  // This is the core of the state machine walking algorithm. We expand ReachSet
  // based on which state we are at (which in turn dictates what edges we
  // should examine)
  // From a high-level point of view, the state machine here guarantees two
  // properties:
  // - If *X and *Y are memory aliases, then X and Y are value aliases
  // - If Y is an alias of X, then reverse assignment edges (if there is any)
  // should precede any assignment edges on the path from X to Y.
  auto NextAssignState = [&](MatchState State) {
    for (const auto &AssignEdge : NodeInfo->Edges)
      propagate(FromNode, AssignEdge.Other, State, ReachSet, WorkList);
  };
  auto NextRevAssignState = [&](MatchState State) {
    for (const auto &RevAssignEdge : NodeInfo->ReverseEdges)
      propagate(FromNode, RevAssignEdge.Other, State, ReachSet, WorkList);
  };
  auto NextMemState = [&](MatchState State) {
    if (auto AliasSet = MemSet.getMemoryAliases(ToNode)) {
      for (const auto &MemAlias : *AliasSet)
        propagate(FromNode, MemAlias, State, ReachSet, WorkList);
    }
  };

  switch (Item.State) {
  case MatchState::FlowFromReadOnly:
    NextRevAssignState(MatchState::FlowFromReadOnly);
    NextAssignState(MatchState::FlowToReadWrite);
    NextMemState(MatchState::FlowFromMemAliasReadOnly);
    break;

  case MatchState::FlowFromMemAliasNoReadWrite:
    NextRevAssignState(MatchState::FlowFromReadOnly);
    NextAssignState(MatchState::FlowToWriteOnly);
    break;

  case MatchState::FlowFromMemAliasReadOnly:
    NextRevAssignState(MatchState::FlowFromReadOnly);
    NextAssignState(MatchState::FlowToReadWrite);
    break;

  case MatchState::FlowToWriteOnly:
    NextAssignState(MatchState::FlowToWriteOnly);
    NextMemState(MatchState::FlowToMemAliasWriteOnly);
    break;

  case MatchState::FlowToReadWrite:
    NextAssignState(MatchState::FlowToReadWrite);
    NextMemState(MatchState::FlowToMemAliasReadWrite);
    break;

  case MatchState::FlowToMemAliasWriteOnly:
    NextAssignState(MatchState::FlowToWriteOnly);
    break;

  case MatchState::FlowToMemAliasReadWrite:
    NextAssignState(MatchState::FlowToReadWrite);
    break;
  }
}

static AliasAttrMap buildAttrMap(const CFLGraph &Graph,
                                 const ReachabilitySet &ReachSet) {
  AliasAttrMap AttrMap;
  std::vector<InstantiatedValue> WorkList, NextList;

  // Initialize each node with its original AliasAttrs in CFLGraph
  for (const auto &Mapping : Graph.value_mappings()) {
    auto Val = Mapping.first;
    auto &ValueInfo = Mapping.second;
    for (unsigned I = 0, E = ValueInfo.getNumLevels(); I < E; ++I) {
      auto Node = InstantiatedValue{Val, I};
      AttrMap.add(Node, ValueInfo.getNodeInfoAtLevel(I).Attr);
      WorkList.push_back(Node);
    }
  }

  while (!WorkList.empty()) {
    for (const auto &Dst : WorkList) {
      auto DstAttr = AttrMap.getAttrs(Dst);
      if (DstAttr.none())
        continue;

      // Propagate attr on the same level
      for (const auto &Mapping : ReachSet.reachableValueAliases(Dst)) {
        auto Src = Mapping.first;
        if (AttrMap.add(Src, DstAttr))
          NextList.push_back(Src);
      }

      // Propagate attr to the levels below
      auto DstBelow = getNodeBelow(Graph, Dst);
      while (DstBelow) {
        if (AttrMap.add(*DstBelow, DstAttr)) {
          NextList.push_back(*DstBelow);
          break;
        }
        DstBelow = getNodeBelow(Graph, *DstBelow);
      }
    }
    WorkList.swap(NextList);
    NextList.clear();
  }

  return AttrMap;
}

CFLAndersAAResult::FunctionInfo
CFLAndersAAResult::buildInfoFrom(const Function &Fn) {
  CFLGraphBuilder<CFLAndersAAResult> GraphBuilder(
      *this, TLI,
      // Cast away the constness here due to GraphBuilder's API requirement
      const_cast<Function &>(Fn));
  auto &Graph = GraphBuilder.getCFLGraph();

  ReachabilitySet ReachSet;
  AliasMemSet MemSet;

  std::vector<WorkListItem> WorkList, NextList;
  initializeWorkList(WorkList, ReachSet, Graph);
  // TODO: make sure we don't stop before the fix point is reached
  while (!WorkList.empty()) {
    for (const auto &Item : WorkList)
      processWorkListItem(Item, Graph, ReachSet, MemSet, NextList);

    NextList.swap(WorkList);
    NextList.clear();
  }

  // Now that we have all the reachability info, propagate AliasAttrs according
  // to it
  auto IValueAttrMap = buildAttrMap(Graph, ReachSet);

  return FunctionInfo(Fn, GraphBuilder.getReturnValues(), ReachSet,
                      std::move(IValueAttrMap));
}

void CFLAndersAAResult::scan(const Function &Fn) {
  auto InsertPair = Cache.insert(std::make_pair(&Fn, Optional<FunctionInfo>()));
  (void)InsertPair;
  assert(InsertPair.second &&
         "Trying to scan a function that has already been cached");

  // Note that we can't do Cache[Fn] = buildSetsFrom(Fn) here: the function call
  // may get evaluated after operator[], potentially triggering a DenseMap
  // resize and invalidating the reference returned by operator[]
  auto FunInfo = buildInfoFrom(Fn);
  Cache[&Fn] = std::move(FunInfo);
  Handles.emplace_front(const_cast<Function *>(&Fn), this);
}

void CFLAndersAAResult::evict(const Function *Fn) { Cache.erase(Fn); }

const Optional<CFLAndersAAResult::FunctionInfo> &
CFLAndersAAResult::ensureCached(const Function &Fn) {
  auto Iter = Cache.find(&Fn);
  if (Iter == Cache.end()) {
    scan(Fn);
    Iter = Cache.find(&Fn);
    assert(Iter != Cache.end());
    assert(Iter->second.hasValue());
  }
  return Iter->second;
}

const AliasSummary *CFLAndersAAResult::getAliasSummary(const Function &Fn) {
  auto &FunInfo = ensureCached(Fn);
  if (FunInfo.hasValue())
    return &FunInfo->getAliasSummary();
  else
    return nullptr;
}

AliasResult CFLAndersAAResult::query(const MemoryLocation &LocA,
                                     const MemoryLocation &LocB) {
  auto *ValA = LocA.Ptr;
  auto *ValB = LocB.Ptr;

  if (!ValA->getType()->isPointerTy() || !ValB->getType()->isPointerTy())
    return NoAlias;

  auto *Fn = parentFunctionOfValue(ValA);
  if (!Fn) {
    Fn = parentFunctionOfValue(ValB);
    if (!Fn) {
      // The only times this is known to happen are when globals + InlineAsm are
      // involved
      LLVM_DEBUG(
          dbgs()
          << "CFLAndersAA: could not extract parent function information.\n");
      return MayAlias;
    }
  } else {
    assert(!parentFunctionOfValue(ValB) || parentFunctionOfValue(ValB) == Fn);
  }

  assert(Fn != nullptr);
  auto &FunInfo = ensureCached(*Fn);

  // AliasMap lookup
  if (FunInfo->mayAlias(ValA, LocA.Size, ValB, LocB.Size))
    return MayAlias;
  return NoAlias;
}

AliasResult CFLAndersAAResult::alias(const MemoryLocation &LocA,
                                     const MemoryLocation &LocB) {
  if (LocA.Ptr == LocB.Ptr)
    return MustAlias;

  // Comparisons between global variables and other constants should be
  // handled by BasicAA.
  // CFLAndersAA may report NoAlias when comparing a GlobalValue and
  // ConstantExpr, but every query needs to have at least one Value tied to a
  // Function, and neither GlobalValues nor ConstantExprs are.
  if (isa<Constant>(LocA.Ptr) && isa<Constant>(LocB.Ptr))
    return AAResultBase::alias(LocA, LocB);

  AliasResult QueryResult = query(LocA, LocB);
  if (QueryResult == MayAlias)
    return AAResultBase::alias(LocA, LocB);

  return QueryResult;
}

AnalysisKey CFLAndersAA::Key;

CFLAndersAAResult CFLAndersAA::run(Function &F, FunctionAnalysisManager &AM) {
  return CFLAndersAAResult(AM.getResult<TargetLibraryAnalysis>(F));
}

char CFLAndersAAWrapperPass::ID = 0;
INITIALIZE_PASS(CFLAndersAAWrapperPass, "cfl-anders-aa",
                "Inclusion-Based CFL Alias Analysis", false, true)

ImmutablePass *llvm::createCFLAndersAAWrapperPass() {
  return new CFLAndersAAWrapperPass();
}

CFLAndersAAWrapperPass::CFLAndersAAWrapperPass() : ImmutablePass(ID) {
  initializeCFLAndersAAWrapperPassPass(*PassRegistry::getPassRegistry());
}

void CFLAndersAAWrapperPass::initializePass() {
  auto &TLIWP = getAnalysis<TargetLibraryInfoWrapperPass>();
  Result.reset(new CFLAndersAAResult(TLIWP.getTLI()));
}

void CFLAndersAAWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<TargetLibraryInfoWrapperPass>();
}
