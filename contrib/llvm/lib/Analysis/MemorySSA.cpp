//===- MemorySSA.cpp - Memory SSA Builder ---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the MemorySSA class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/MemorySSA.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/IteratedDominanceFrontier.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/AssemblyAnnotationWriter.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Use.h"
#include "llvm/Pass.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <memory>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "memoryssa"

INITIALIZE_PASS_BEGIN(MemorySSAWrapperPass, "memoryssa", "Memory SSA", false,
                      true)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(MemorySSAWrapperPass, "memoryssa", "Memory SSA", false,
                    true)

INITIALIZE_PASS_BEGIN(MemorySSAPrinterLegacyPass, "print-memoryssa",
                      "Memory SSA Printer", false, false)
INITIALIZE_PASS_DEPENDENCY(MemorySSAWrapperPass)
INITIALIZE_PASS_END(MemorySSAPrinterLegacyPass, "print-memoryssa",
                    "Memory SSA Printer", false, false)

static cl::opt<unsigned> MaxCheckLimit(
    "memssa-check-limit", cl::Hidden, cl::init(100),
    cl::desc("The maximum number of stores/phis MemorySSA"
             "will consider trying to walk past (default = 100)"));

// Always verify MemorySSA if expensive checking is enabled.
#ifdef EXPENSIVE_CHECKS
bool llvm::VerifyMemorySSA = true;
#else
bool llvm::VerifyMemorySSA = false;
#endif
static cl::opt<bool, true>
    VerifyMemorySSAX("verify-memoryssa", cl::location(VerifyMemorySSA),
                     cl::Hidden, cl::desc("Enable verification of MemorySSA."));

namespace llvm {

/// An assembly annotator class to print Memory SSA information in
/// comments.
class MemorySSAAnnotatedWriter : public AssemblyAnnotationWriter {
  friend class MemorySSA;

  const MemorySSA *MSSA;

public:
  MemorySSAAnnotatedWriter(const MemorySSA *M) : MSSA(M) {}

  void emitBasicBlockStartAnnot(const BasicBlock *BB,
                                formatted_raw_ostream &OS) override {
    if (MemoryAccess *MA = MSSA->getMemoryAccess(BB))
      OS << "; " << *MA << "\n";
  }

  void emitInstructionAnnot(const Instruction *I,
                            formatted_raw_ostream &OS) override {
    if (MemoryAccess *MA = MSSA->getMemoryAccess(I))
      OS << "; " << *MA << "\n";
  }
};

} // end namespace llvm

namespace {

/// Our current alias analysis API differentiates heavily between calls and
/// non-calls, and functions called on one usually assert on the other.
/// This class encapsulates the distinction to simplify other code that wants
/// "Memory affecting instructions and related data" to use as a key.
/// For example, this class is used as a densemap key in the use optimizer.
class MemoryLocOrCall {
public:
  bool IsCall = false;

  MemoryLocOrCall(MemoryUseOrDef *MUD)
      : MemoryLocOrCall(MUD->getMemoryInst()) {}
  MemoryLocOrCall(const MemoryUseOrDef *MUD)
      : MemoryLocOrCall(MUD->getMemoryInst()) {}

  MemoryLocOrCall(Instruction *Inst) {
    if (auto *C = dyn_cast<CallBase>(Inst)) {
      IsCall = true;
      Call = C;
    } else {
      IsCall = false;
      // There is no such thing as a memorylocation for a fence inst, and it is
      // unique in that regard.
      if (!isa<FenceInst>(Inst))
        Loc = MemoryLocation::get(Inst);
    }
  }

  explicit MemoryLocOrCall(const MemoryLocation &Loc) : Loc(Loc) {}

  const CallBase *getCall() const {
    assert(IsCall);
    return Call;
  }

  MemoryLocation getLoc() const {
    assert(!IsCall);
    return Loc;
  }

  bool operator==(const MemoryLocOrCall &Other) const {
    if (IsCall != Other.IsCall)
      return false;

    if (!IsCall)
      return Loc == Other.Loc;

    if (Call->getCalledValue() != Other.Call->getCalledValue())
      return false;

    return Call->arg_size() == Other.Call->arg_size() &&
           std::equal(Call->arg_begin(), Call->arg_end(),
                      Other.Call->arg_begin());
  }

private:
  union {
    const CallBase *Call;
    MemoryLocation Loc;
  };
};

} // end anonymous namespace

namespace llvm {

template <> struct DenseMapInfo<MemoryLocOrCall> {
  static inline MemoryLocOrCall getEmptyKey() {
    return MemoryLocOrCall(DenseMapInfo<MemoryLocation>::getEmptyKey());
  }

  static inline MemoryLocOrCall getTombstoneKey() {
    return MemoryLocOrCall(DenseMapInfo<MemoryLocation>::getTombstoneKey());
  }

  static unsigned getHashValue(const MemoryLocOrCall &MLOC) {
    if (!MLOC.IsCall)
      return hash_combine(
          MLOC.IsCall,
          DenseMapInfo<MemoryLocation>::getHashValue(MLOC.getLoc()));

    hash_code hash =
        hash_combine(MLOC.IsCall, DenseMapInfo<const Value *>::getHashValue(
                                      MLOC.getCall()->getCalledValue()));

    for (const Value *Arg : MLOC.getCall()->args())
      hash = hash_combine(hash, DenseMapInfo<const Value *>::getHashValue(Arg));
    return hash;
  }

  static bool isEqual(const MemoryLocOrCall &LHS, const MemoryLocOrCall &RHS) {
    return LHS == RHS;
  }
};

} // end namespace llvm

/// This does one-way checks to see if Use could theoretically be hoisted above
/// MayClobber. This will not check the other way around.
///
/// This assumes that, for the purposes of MemorySSA, Use comes directly after
/// MayClobber, with no potentially clobbering operations in between them.
/// (Where potentially clobbering ops are memory barriers, aliased stores, etc.)
static bool areLoadsReorderable(const LoadInst *Use,
                                const LoadInst *MayClobber) {
  bool VolatileUse = Use->isVolatile();
  bool VolatileClobber = MayClobber->isVolatile();
  // Volatile operations may never be reordered with other volatile operations.
  if (VolatileUse && VolatileClobber)
    return false;
  // Otherwise, volatile doesn't matter here. From the language reference:
  // 'optimizers may change the order of volatile operations relative to
  // non-volatile operations.'"

  // If a load is seq_cst, it cannot be moved above other loads. If its ordering
  // is weaker, it can be moved above other loads. We just need to be sure that
  // MayClobber isn't an acquire load, because loads can't be moved above
  // acquire loads.
  //
  // Note that this explicitly *does* allow the free reordering of monotonic (or
  // weaker) loads of the same address.
  bool SeqCstUse = Use->getOrdering() == AtomicOrdering::SequentiallyConsistent;
  bool MayClobberIsAcquire = isAtLeastOrStrongerThan(MayClobber->getOrdering(),
                                                     AtomicOrdering::Acquire);
  return !(SeqCstUse || MayClobberIsAcquire);
}

namespace {

struct ClobberAlias {
  bool IsClobber;
  Optional<AliasResult> AR;
};

} // end anonymous namespace

// Return a pair of {IsClobber (bool), AR (AliasResult)}. It relies on AR being
// ignored if IsClobber = false.
static ClobberAlias instructionClobbersQuery(const MemoryDef *MD,
                                             const MemoryLocation &UseLoc,
                                             const Instruction *UseInst,
                                             AliasAnalysis &AA) {
  Instruction *DefInst = MD->getMemoryInst();
  assert(DefInst && "Defining instruction not actually an instruction");
  const auto *UseCall = dyn_cast<CallBase>(UseInst);
  Optional<AliasResult> AR;

  if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(DefInst)) {
    // These intrinsics will show up as affecting memory, but they are just
    // markers, mostly.
    //
    // FIXME: We probably don't actually want MemorySSA to model these at all
    // (including creating MemoryAccesses for them): we just end up inventing
    // clobbers where they don't really exist at all. Please see D43269 for
    // context.
    switch (II->getIntrinsicID()) {
    case Intrinsic::lifetime_start:
      if (UseCall)
        return {false, NoAlias};
      AR = AA.alias(MemoryLocation(II->getArgOperand(1)), UseLoc);
      return {AR != NoAlias, AR};
    case Intrinsic::lifetime_end:
    case Intrinsic::invariant_start:
    case Intrinsic::invariant_end:
    case Intrinsic::assume:
      return {false, NoAlias};
    default:
      break;
    }
  }

  if (UseCall) {
    ModRefInfo I = AA.getModRefInfo(DefInst, UseCall);
    AR = isMustSet(I) ? MustAlias : MayAlias;
    return {isModOrRefSet(I), AR};
  }

  if (auto *DefLoad = dyn_cast<LoadInst>(DefInst))
    if (auto *UseLoad = dyn_cast<LoadInst>(UseInst))
      return {!areLoadsReorderable(UseLoad, DefLoad), MayAlias};

  ModRefInfo I = AA.getModRefInfo(DefInst, UseLoc);
  AR = isMustSet(I) ? MustAlias : MayAlias;
  return {isModSet(I), AR};
}

static ClobberAlias instructionClobbersQuery(MemoryDef *MD,
                                             const MemoryUseOrDef *MU,
                                             const MemoryLocOrCall &UseMLOC,
                                             AliasAnalysis &AA) {
  // FIXME: This is a temporary hack to allow a single instructionClobbersQuery
  // to exist while MemoryLocOrCall is pushed through places.
  if (UseMLOC.IsCall)
    return instructionClobbersQuery(MD, MemoryLocation(), MU->getMemoryInst(),
                                    AA);
  return instructionClobbersQuery(MD, UseMLOC.getLoc(), MU->getMemoryInst(),
                                  AA);
}

// Return true when MD may alias MU, return false otherwise.
bool MemorySSAUtil::defClobbersUseOrDef(MemoryDef *MD, const MemoryUseOrDef *MU,
                                        AliasAnalysis &AA) {
  return instructionClobbersQuery(MD, MU, MemoryLocOrCall(MU), AA).IsClobber;
}

namespace {

struct UpwardsMemoryQuery {
  // True if our original query started off as a call
  bool IsCall = false;
  // The pointer location we started the query with. This will be empty if
  // IsCall is true.
  MemoryLocation StartingLoc;
  // This is the instruction we were querying about.
  const Instruction *Inst = nullptr;
  // The MemoryAccess we actually got called with, used to test local domination
  const MemoryAccess *OriginalAccess = nullptr;
  Optional<AliasResult> AR = MayAlias;
  bool SkipSelfAccess = false;

  UpwardsMemoryQuery() = default;

  UpwardsMemoryQuery(const Instruction *Inst, const MemoryAccess *Access)
      : IsCall(isa<CallBase>(Inst)), Inst(Inst), OriginalAccess(Access) {
    if (!IsCall)
      StartingLoc = MemoryLocation::get(Inst);
  }
};

} // end anonymous namespace

static bool lifetimeEndsAt(MemoryDef *MD, const MemoryLocation &Loc,
                           AliasAnalysis &AA) {
  Instruction *Inst = MD->getMemoryInst();
  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(Inst)) {
    switch (II->getIntrinsicID()) {
    case Intrinsic::lifetime_end:
      return AA.isMustAlias(MemoryLocation(II->getArgOperand(1)), Loc);
    default:
      return false;
    }
  }
  return false;
}

static bool isUseTriviallyOptimizableToLiveOnEntry(AliasAnalysis &AA,
                                                   const Instruction *I) {
  // If the memory can't be changed, then loads of the memory can't be
  // clobbered.
  return isa<LoadInst>(I) && (I->getMetadata(LLVMContext::MD_invariant_load) ||
                              AA.pointsToConstantMemory(cast<LoadInst>(I)->
                                                          getPointerOperand()));
}

/// Verifies that `Start` is clobbered by `ClobberAt`, and that nothing
/// inbetween `Start` and `ClobberAt` can clobbers `Start`.
///
/// This is meant to be as simple and self-contained as possible. Because it
/// uses no cache, etc., it can be relatively expensive.
///
/// \param Start     The MemoryAccess that we want to walk from.
/// \param ClobberAt A clobber for Start.
/// \param StartLoc  The MemoryLocation for Start.
/// \param MSSA      The MemorySSA instance that Start and ClobberAt belong to.
/// \param Query     The UpwardsMemoryQuery we used for our search.
/// \param AA        The AliasAnalysis we used for our search.
/// \param AllowImpreciseClobber Always false, unless we do relaxed verify.
static void
checkClobberSanity(const MemoryAccess *Start, MemoryAccess *ClobberAt,
                   const MemoryLocation &StartLoc, const MemorySSA &MSSA,
                   const UpwardsMemoryQuery &Query, AliasAnalysis &AA,
                   bool AllowImpreciseClobber = false) {
  assert(MSSA.dominates(ClobberAt, Start) && "Clobber doesn't dominate start?");

  if (MSSA.isLiveOnEntryDef(Start)) {
    assert(MSSA.isLiveOnEntryDef(ClobberAt) &&
           "liveOnEntry must clobber itself");
    return;
  }

  bool FoundClobber = false;
  DenseSet<ConstMemoryAccessPair> VisitedPhis;
  SmallVector<ConstMemoryAccessPair, 8> Worklist;
  Worklist.emplace_back(Start, StartLoc);
  // Walk all paths from Start to ClobberAt, while looking for clobbers. If one
  // is found, complain.
  while (!Worklist.empty()) {
    auto MAP = Worklist.pop_back_val();
    // All we care about is that nothing from Start to ClobberAt clobbers Start.
    // We learn nothing from revisiting nodes.
    if (!VisitedPhis.insert(MAP).second)
      continue;

    for (const auto *MA : def_chain(MAP.first)) {
      if (MA == ClobberAt) {
        if (const auto *MD = dyn_cast<MemoryDef>(MA)) {
          // instructionClobbersQuery isn't essentially free, so don't use `|=`,
          // since it won't let us short-circuit.
          //
          // Also, note that this can't be hoisted out of the `Worklist` loop,
          // since MD may only act as a clobber for 1 of N MemoryLocations.
          FoundClobber = FoundClobber || MSSA.isLiveOnEntryDef(MD);
          if (!FoundClobber) {
            ClobberAlias CA =
                instructionClobbersQuery(MD, MAP.second, Query.Inst, AA);
            if (CA.IsClobber) {
              FoundClobber = true;
              // Not used: CA.AR;
            }
          }
        }
        break;
      }

      // We should never hit liveOnEntry, unless it's the clobber.
      assert(!MSSA.isLiveOnEntryDef(MA) && "Hit liveOnEntry before clobber?");

      if (const auto *MD = dyn_cast<MemoryDef>(MA)) {
        // If Start is a Def, skip self.
        if (MD == Start)
          continue;

        assert(!instructionClobbersQuery(MD, MAP.second, Query.Inst, AA)
                    .IsClobber &&
               "Found clobber before reaching ClobberAt!");
        continue;
      }

      if (const auto *MU = dyn_cast<MemoryUse>(MA)) {
        (void)MU;
        assert (MU == Start &&
                "Can only find use in def chain if Start is a use");
        continue;
      }

      assert(isa<MemoryPhi>(MA));
      Worklist.append(
          upward_defs_begin({const_cast<MemoryAccess *>(MA), MAP.second}),
          upward_defs_end());
    }
  }

  // If the verify is done following an optimization, it's possible that
  // ClobberAt was a conservative clobbering, that we can now infer is not a
  // true clobbering access. Don't fail the verify if that's the case.
  // We do have accesses that claim they're optimized, but could be optimized
  // further. Updating all these can be expensive, so allow it for now (FIXME).
  if (AllowImpreciseClobber)
    return;

  // If ClobberAt is a MemoryPhi, we can assume something above it acted as a
  // clobber. Otherwise, `ClobberAt` should've acted as a clobber at some point.
  assert((isa<MemoryPhi>(ClobberAt) || FoundClobber) &&
         "ClobberAt never acted as a clobber");
}

namespace {

/// Our algorithm for walking (and trying to optimize) clobbers, all wrapped up
/// in one class.
class ClobberWalker {
  /// Save a few bytes by using unsigned instead of size_t.
  using ListIndex = unsigned;

  /// Represents a span of contiguous MemoryDefs, potentially ending in a
  /// MemoryPhi.
  struct DefPath {
    MemoryLocation Loc;
    // Note that, because we always walk in reverse, Last will always dominate
    // First. Also note that First and Last are inclusive.
    MemoryAccess *First;
    MemoryAccess *Last;
    Optional<ListIndex> Previous;

    DefPath(const MemoryLocation &Loc, MemoryAccess *First, MemoryAccess *Last,
            Optional<ListIndex> Previous)
        : Loc(Loc), First(First), Last(Last), Previous(Previous) {}

    DefPath(const MemoryLocation &Loc, MemoryAccess *Init,
            Optional<ListIndex> Previous)
        : DefPath(Loc, Init, Init, Previous) {}
  };

  const MemorySSA &MSSA;
  AliasAnalysis &AA;
  DominatorTree &DT;
  UpwardsMemoryQuery *Query;

  // Phi optimization bookkeeping
  SmallVector<DefPath, 32> Paths;
  DenseSet<ConstMemoryAccessPair> VisitedPhis;

  /// Find the nearest def or phi that `From` can legally be optimized to.
  const MemoryAccess *getWalkTarget(const MemoryPhi *From) const {
    assert(From->getNumOperands() && "Phi with no operands?");

    BasicBlock *BB = From->getBlock();
    MemoryAccess *Result = MSSA.getLiveOnEntryDef();
    DomTreeNode *Node = DT.getNode(BB);
    while ((Node = Node->getIDom())) {
      auto *Defs = MSSA.getBlockDefs(Node->getBlock());
      if (Defs)
        return &*Defs->rbegin();
    }
    return Result;
  }

  /// Result of calling walkToPhiOrClobber.
  struct UpwardsWalkResult {
    /// The "Result" of the walk. Either a clobber, the last thing we walked, or
    /// both. Include alias info when clobber found.
    MemoryAccess *Result;
    bool IsKnownClobber;
    Optional<AliasResult> AR;
  };

  /// Walk to the next Phi or Clobber in the def chain starting at Desc.Last.
  /// This will update Desc.Last as it walks. It will (optionally) also stop at
  /// StopAt.
  ///
  /// This does not test for whether StopAt is a clobber
  UpwardsWalkResult
  walkToPhiOrClobber(DefPath &Desc, const MemoryAccess *StopAt = nullptr,
                     const MemoryAccess *SkipStopAt = nullptr) const {
    assert(!isa<MemoryUse>(Desc.Last) && "Uses don't exist in my world");

    for (MemoryAccess *Current : def_chain(Desc.Last)) {
      Desc.Last = Current;
      if (Current == StopAt || Current == SkipStopAt)
        return {Current, false, MayAlias};

      if (auto *MD = dyn_cast<MemoryDef>(Current)) {
        if (MSSA.isLiveOnEntryDef(MD))
          return {MD, true, MustAlias};
        ClobberAlias CA =
            instructionClobbersQuery(MD, Desc.Loc, Query->Inst, AA);
        if (CA.IsClobber)
          return {MD, true, CA.AR};
      }
    }

    assert(isa<MemoryPhi>(Desc.Last) &&
           "Ended at a non-clobber that's not a phi?");
    return {Desc.Last, false, MayAlias};
  }

  void addSearches(MemoryPhi *Phi, SmallVectorImpl<ListIndex> &PausedSearches,
                   ListIndex PriorNode) {
    auto UpwardDefs = make_range(upward_defs_begin({Phi, Paths[PriorNode].Loc}),
                                 upward_defs_end());
    for (const MemoryAccessPair &P : UpwardDefs) {
      PausedSearches.push_back(Paths.size());
      Paths.emplace_back(P.second, P.first, PriorNode);
    }
  }

  /// Represents a search that terminated after finding a clobber. This clobber
  /// may or may not be present in the path of defs from LastNode..SearchStart,
  /// since it may have been retrieved from cache.
  struct TerminatedPath {
    MemoryAccess *Clobber;
    ListIndex LastNode;
  };

  /// Get an access that keeps us from optimizing to the given phi.
  ///
  /// PausedSearches is an array of indices into the Paths array. Its incoming
  /// value is the indices of searches that stopped at the last phi optimization
  /// target. It's left in an unspecified state.
  ///
  /// If this returns None, NewPaused is a vector of searches that terminated
  /// at StopWhere. Otherwise, NewPaused is left in an unspecified state.
  Optional<TerminatedPath>
  getBlockingAccess(const MemoryAccess *StopWhere,
                    SmallVectorImpl<ListIndex> &PausedSearches,
                    SmallVectorImpl<ListIndex> &NewPaused,
                    SmallVectorImpl<TerminatedPath> &Terminated) {
    assert(!PausedSearches.empty() && "No searches to continue?");

    // BFS vs DFS really doesn't make a difference here, so just do a DFS with
    // PausedSearches as our stack.
    while (!PausedSearches.empty()) {
      ListIndex PathIndex = PausedSearches.pop_back_val();
      DefPath &Node = Paths[PathIndex];

      // If we've already visited this path with this MemoryLocation, we don't
      // need to do so again.
      //
      // NOTE: That we just drop these paths on the ground makes caching
      // behavior sporadic. e.g. given a diamond:
      //  A
      // B C
      //  D
      //
      // ...If we walk D, B, A, C, we'll only cache the result of phi
      // optimization for A, B, and D; C will be skipped because it dies here.
      // This arguably isn't the worst thing ever, since:
      //   - We generally query things in a top-down order, so if we got below D
      //     without needing cache entries for {C, MemLoc}, then chances are
      //     that those cache entries would end up ultimately unused.
      //   - We still cache things for A, so C only needs to walk up a bit.
      // If this behavior becomes problematic, we can fix without a ton of extra
      // work.
      if (!VisitedPhis.insert({Node.Last, Node.Loc}).second)
        continue;

      const MemoryAccess *SkipStopWhere = nullptr;
      if (Query->SkipSelfAccess && Node.Loc == Query->StartingLoc) {
        assert(isa<MemoryDef>(Query->OriginalAccess));
        SkipStopWhere = Query->OriginalAccess;
      }

      UpwardsWalkResult Res = walkToPhiOrClobber(Node, /*StopAt=*/StopWhere,
                                                 /*SkipStopAt=*/SkipStopWhere);
      if (Res.IsKnownClobber) {
        assert(Res.Result != StopWhere && Res.Result != SkipStopWhere);
        // If this wasn't a cache hit, we hit a clobber when walking. That's a
        // failure.
        TerminatedPath Term{Res.Result, PathIndex};
        if (!MSSA.dominates(Res.Result, StopWhere))
          return Term;

        // Otherwise, it's a valid thing to potentially optimize to.
        Terminated.push_back(Term);
        continue;
      }

      if (Res.Result == StopWhere || Res.Result == SkipStopWhere) {
        // We've hit our target. Save this path off for if we want to continue
        // walking. If we are in the mode of skipping the OriginalAccess, and
        // we've reached back to the OriginalAccess, do not save path, we've
        // just looped back to self.
        if (Res.Result != SkipStopWhere)
          NewPaused.push_back(PathIndex);
        continue;
      }

      assert(!MSSA.isLiveOnEntryDef(Res.Result) && "liveOnEntry is a clobber");
      addSearches(cast<MemoryPhi>(Res.Result), PausedSearches, PathIndex);
    }

    return None;
  }

  template <typename T, typename Walker>
  struct generic_def_path_iterator
      : public iterator_facade_base<generic_def_path_iterator<T, Walker>,
                                    std::forward_iterator_tag, T *> {
    generic_def_path_iterator() = default;
    generic_def_path_iterator(Walker *W, ListIndex N) : W(W), N(N) {}

    T &operator*() const { return curNode(); }

    generic_def_path_iterator &operator++() {
      N = curNode().Previous;
      return *this;
    }

    bool operator==(const generic_def_path_iterator &O) const {
      if (N.hasValue() != O.N.hasValue())
        return false;
      return !N.hasValue() || *N == *O.N;
    }

  private:
    T &curNode() const { return W->Paths[*N]; }

    Walker *W = nullptr;
    Optional<ListIndex> N = None;
  };

  using def_path_iterator = generic_def_path_iterator<DefPath, ClobberWalker>;
  using const_def_path_iterator =
      generic_def_path_iterator<const DefPath, const ClobberWalker>;

  iterator_range<def_path_iterator> def_path(ListIndex From) {
    return make_range(def_path_iterator(this, From), def_path_iterator());
  }

  iterator_range<const_def_path_iterator> const_def_path(ListIndex From) const {
    return make_range(const_def_path_iterator(this, From),
                      const_def_path_iterator());
  }

  struct OptznResult {
    /// The path that contains our result.
    TerminatedPath PrimaryClobber;
    /// The paths that we can legally cache back from, but that aren't
    /// necessarily the result of the Phi optimization.
    SmallVector<TerminatedPath, 4> OtherClobbers;
  };

  ListIndex defPathIndex(const DefPath &N) const {
    // The assert looks nicer if we don't need to do &N
    const DefPath *NP = &N;
    assert(!Paths.empty() && NP >= &Paths.front() && NP <= &Paths.back() &&
           "Out of bounds DefPath!");
    return NP - &Paths.front();
  }

  /// Try to optimize a phi as best as we can. Returns a SmallVector of Paths
  /// that act as legal clobbers. Note that this won't return *all* clobbers.
  ///
  /// Phi optimization algorithm tl;dr:
  ///   - Find the earliest def/phi, A, we can optimize to
  ///   - Find if all paths from the starting memory access ultimately reach A
  ///     - If not, optimization isn't possible.
  ///     - Otherwise, walk from A to another clobber or phi, A'.
  ///       - If A' is a def, we're done.
  ///       - If A' is a phi, try to optimize it.
  ///
  /// A path is a series of {MemoryAccess, MemoryLocation} pairs. A path
  /// terminates when a MemoryAccess that clobbers said MemoryLocation is found.
  OptznResult tryOptimizePhi(MemoryPhi *Phi, MemoryAccess *Start,
                             const MemoryLocation &Loc) {
    assert(Paths.empty() && VisitedPhis.empty() &&
           "Reset the optimization state.");

    Paths.emplace_back(Loc, Start, Phi, None);
    // Stores how many "valid" optimization nodes we had prior to calling
    // addSearches/getBlockingAccess. Necessary for caching if we had a blocker.
    auto PriorPathsSize = Paths.size();

    SmallVector<ListIndex, 16> PausedSearches;
    SmallVector<ListIndex, 8> NewPaused;
    SmallVector<TerminatedPath, 4> TerminatedPaths;

    addSearches(Phi, PausedSearches, 0);

    // Moves the TerminatedPath with the "most dominated" Clobber to the end of
    // Paths.
    auto MoveDominatedPathToEnd = [&](SmallVectorImpl<TerminatedPath> &Paths) {
      assert(!Paths.empty() && "Need a path to move");
      auto Dom = Paths.begin();
      for (auto I = std::next(Dom), E = Paths.end(); I != E; ++I)
        if (!MSSA.dominates(I->Clobber, Dom->Clobber))
          Dom = I;
      auto Last = Paths.end() - 1;
      if (Last != Dom)
        std::iter_swap(Last, Dom);
    };

    MemoryPhi *Current = Phi;
    while (true) {
      assert(!MSSA.isLiveOnEntryDef(Current) &&
             "liveOnEntry wasn't treated as a clobber?");

      const auto *Target = getWalkTarget(Current);
      // If a TerminatedPath doesn't dominate Target, then it wasn't a legal
      // optimization for the prior phi.
      assert(all_of(TerminatedPaths, [&](const TerminatedPath &P) {
        return MSSA.dominates(P.Clobber, Target);
      }));

      // FIXME: This is broken, because the Blocker may be reported to be
      // liveOnEntry, and we'll happily wait for that to disappear (read: never)
      // For the moment, this is fine, since we do nothing with blocker info.
      if (Optional<TerminatedPath> Blocker = getBlockingAccess(
              Target, PausedSearches, NewPaused, TerminatedPaths)) {

        // Find the node we started at. We can't search based on N->Last, since
        // we may have gone around a loop with a different MemoryLocation.
        auto Iter = find_if(def_path(Blocker->LastNode), [&](const DefPath &N) {
          return defPathIndex(N) < PriorPathsSize;
        });
        assert(Iter != def_path_iterator());

        DefPath &CurNode = *Iter;
        assert(CurNode.Last == Current);

        // Two things:
        // A. We can't reliably cache all of NewPaused back. Consider a case
        //    where we have two paths in NewPaused; one of which can't optimize
        //    above this phi, whereas the other can. If we cache the second path
        //    back, we'll end up with suboptimal cache entries. We can handle
        //    cases like this a bit better when we either try to find all
        //    clobbers that block phi optimization, or when our cache starts
        //    supporting unfinished searches.
        // B. We can't reliably cache TerminatedPaths back here without doing
        //    extra checks; consider a case like:
        //       T
        //      / \
        //     D   C
        //      \ /
        //       S
        //    Where T is our target, C is a node with a clobber on it, D is a
        //    diamond (with a clobber *only* on the left or right node, N), and
        //    S is our start. Say we walk to D, through the node opposite N
        //    (read: ignoring the clobber), and see a cache entry in the top
        //    node of D. That cache entry gets put into TerminatedPaths. We then
        //    walk up to C (N is later in our worklist), find the clobber, and
        //    quit. If we append TerminatedPaths to OtherClobbers, we'll cache
        //    the bottom part of D to the cached clobber, ignoring the clobber
        //    in N. Again, this problem goes away if we start tracking all
        //    blockers for a given phi optimization.
        TerminatedPath Result{CurNode.Last, defPathIndex(CurNode)};
        return {Result, {}};
      }

      // If there's nothing left to search, then all paths led to valid clobbers
      // that we got from our cache; pick the nearest to the start, and allow
      // the rest to be cached back.
      if (NewPaused.empty()) {
        MoveDominatedPathToEnd(TerminatedPaths);
        TerminatedPath Result = TerminatedPaths.pop_back_val();
        return {Result, std::move(TerminatedPaths)};
      }

      MemoryAccess *DefChainEnd = nullptr;
      SmallVector<TerminatedPath, 4> Clobbers;
      for (ListIndex Paused : NewPaused) {
        UpwardsWalkResult WR = walkToPhiOrClobber(Paths[Paused]);
        if (WR.IsKnownClobber)
          Clobbers.push_back({WR.Result, Paused});
        else
          // Micro-opt: If we hit the end of the chain, save it.
          DefChainEnd = WR.Result;
      }

      if (!TerminatedPaths.empty()) {
        // If we couldn't find the dominating phi/liveOnEntry in the above loop,
        // do it now.
        if (!DefChainEnd)
          for (auto *MA : def_chain(const_cast<MemoryAccess *>(Target)))
            DefChainEnd = MA;

        // If any of the terminated paths don't dominate the phi we'll try to
        // optimize, we need to figure out what they are and quit.
        const BasicBlock *ChainBB = DefChainEnd->getBlock();
        for (const TerminatedPath &TP : TerminatedPaths) {
          // Because we know that DefChainEnd is as "high" as we can go, we
          // don't need local dominance checks; BB dominance is sufficient.
          if (DT.dominates(ChainBB, TP.Clobber->getBlock()))
            Clobbers.push_back(TP);
        }
      }

      // If we have clobbers in the def chain, find the one closest to Current
      // and quit.
      if (!Clobbers.empty()) {
        MoveDominatedPathToEnd(Clobbers);
        TerminatedPath Result = Clobbers.pop_back_val();
        return {Result, std::move(Clobbers)};
      }

      assert(all_of(NewPaused,
                    [&](ListIndex I) { return Paths[I].Last == DefChainEnd; }));

      // Because liveOnEntry is a clobber, this must be a phi.
      auto *DefChainPhi = cast<MemoryPhi>(DefChainEnd);

      PriorPathsSize = Paths.size();
      PausedSearches.clear();
      for (ListIndex I : NewPaused)
        addSearches(DefChainPhi, PausedSearches, I);
      NewPaused.clear();

      Current = DefChainPhi;
    }
  }

  void verifyOptResult(const OptznResult &R) const {
    assert(all_of(R.OtherClobbers, [&](const TerminatedPath &P) {
      return MSSA.dominates(P.Clobber, R.PrimaryClobber.Clobber);
    }));
  }

  void resetPhiOptznState() {
    Paths.clear();
    VisitedPhis.clear();
  }

public:
  ClobberWalker(const MemorySSA &MSSA, AliasAnalysis &AA, DominatorTree &DT)
      : MSSA(MSSA), AA(AA), DT(DT) {}

  /// Finds the nearest clobber for the given query, optimizing phis if
  /// possible.
  MemoryAccess *findClobber(MemoryAccess *Start, UpwardsMemoryQuery &Q) {
    Query = &Q;

    MemoryAccess *Current = Start;
    // This walker pretends uses don't exist. If we're handed one, silently grab
    // its def. (This has the nice side-effect of ensuring we never cache uses)
    if (auto *MU = dyn_cast<MemoryUse>(Start))
      Current = MU->getDefiningAccess();

    DefPath FirstDesc(Q.StartingLoc, Current, Current, None);
    // Fast path for the overly-common case (no crazy phi optimization
    // necessary)
    UpwardsWalkResult WalkResult = walkToPhiOrClobber(FirstDesc);
    MemoryAccess *Result;
    if (WalkResult.IsKnownClobber) {
      Result = WalkResult.Result;
      Q.AR = WalkResult.AR;
    } else {
      OptznResult OptRes = tryOptimizePhi(cast<MemoryPhi>(FirstDesc.Last),
                                          Current, Q.StartingLoc);
      verifyOptResult(OptRes);
      resetPhiOptznState();
      Result = OptRes.PrimaryClobber.Clobber;
    }

#ifdef EXPENSIVE_CHECKS
    if (!Q.SkipSelfAccess)
      checkClobberSanity(Current, Result, Q.StartingLoc, MSSA, Q, AA);
#endif
    return Result;
  }

  void verify(const MemorySSA *MSSA) { assert(MSSA == &this->MSSA); }
};

struct RenamePassData {
  DomTreeNode *DTN;
  DomTreeNode::const_iterator ChildIt;
  MemoryAccess *IncomingVal;

  RenamePassData(DomTreeNode *D, DomTreeNode::const_iterator It,
                 MemoryAccess *M)
      : DTN(D), ChildIt(It), IncomingVal(M) {}

  void swap(RenamePassData &RHS) {
    std::swap(DTN, RHS.DTN);
    std::swap(ChildIt, RHS.ChildIt);
    std::swap(IncomingVal, RHS.IncomingVal);
  }
};

} // end anonymous namespace

namespace llvm {

class MemorySSA::ClobberWalkerBase {
  ClobberWalker Walker;
  MemorySSA *MSSA;

public:
  ClobberWalkerBase(MemorySSA *M, AliasAnalysis *A, DominatorTree *D)
      : Walker(*M, *A, *D), MSSA(M) {}

  MemoryAccess *getClobberingMemoryAccessBase(MemoryAccess *,
                                              const MemoryLocation &);
  // Second argument (bool), defines whether the clobber search should skip the
  // original queried access. If true, there will be a follow-up query searching
  // for a clobber access past "self". Note that the Optimized access is not
  // updated if a new clobber is found by this SkipSelf search. If this
  // additional query becomes heavily used we may decide to cache the result.
  // Walker instantiations will decide how to set the SkipSelf bool.
  MemoryAccess *getClobberingMemoryAccessBase(MemoryAccess *, bool);
  void verify(const MemorySSA *MSSA) { Walker.verify(MSSA); }
};

/// A MemorySSAWalker that does AA walks to disambiguate accesses. It no
/// longer does caching on its own, but the name has been retained for the
/// moment.
class MemorySSA::CachingWalker final : public MemorySSAWalker {
  ClobberWalkerBase *Walker;

public:
  CachingWalker(MemorySSA *M, ClobberWalkerBase *W)
      : MemorySSAWalker(M), Walker(W) {}
  ~CachingWalker() override = default;

  using MemorySSAWalker::getClobberingMemoryAccess;

  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA) override;
  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA,
                                          const MemoryLocation &Loc) override;

  void invalidateInfo(MemoryAccess *MA) override {
    if (auto *MUD = dyn_cast<MemoryUseOrDef>(MA))
      MUD->resetOptimized();
  }

  void verify(const MemorySSA *MSSA) override {
    MemorySSAWalker::verify(MSSA);
    Walker->verify(MSSA);
  }
};

class MemorySSA::SkipSelfWalker final : public MemorySSAWalker {
  ClobberWalkerBase *Walker;

public:
  SkipSelfWalker(MemorySSA *M, ClobberWalkerBase *W)
      : MemorySSAWalker(M), Walker(W) {}
  ~SkipSelfWalker() override = default;

  using MemorySSAWalker::getClobberingMemoryAccess;

  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA) override;
  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA,
                                          const MemoryLocation &Loc) override;

  void invalidateInfo(MemoryAccess *MA) override {
    if (auto *MUD = dyn_cast<MemoryUseOrDef>(MA))
      MUD->resetOptimized();
  }

  void verify(const MemorySSA *MSSA) override {
    MemorySSAWalker::verify(MSSA);
    Walker->verify(MSSA);
  }
};

} // end namespace llvm

void MemorySSA::renameSuccessorPhis(BasicBlock *BB, MemoryAccess *IncomingVal,
                                    bool RenameAllUses) {
  // Pass through values to our successors
  for (const BasicBlock *S : successors(BB)) {
    auto It = PerBlockAccesses.find(S);
    // Rename the phi nodes in our successor block
    if (It == PerBlockAccesses.end() || !isa<MemoryPhi>(It->second->front()))
      continue;
    AccessList *Accesses = It->second.get();
    auto *Phi = cast<MemoryPhi>(&Accesses->front());
    if (RenameAllUses) {
      int PhiIndex = Phi->getBasicBlockIndex(BB);
      assert(PhiIndex != -1 && "Incomplete phi during partial rename");
      Phi->setIncomingValue(PhiIndex, IncomingVal);
    } else
      Phi->addIncoming(IncomingVal, BB);
  }
}

/// Rename a single basic block into MemorySSA form.
/// Uses the standard SSA renaming algorithm.
/// \returns The new incoming value.
MemoryAccess *MemorySSA::renameBlock(BasicBlock *BB, MemoryAccess *IncomingVal,
                                     bool RenameAllUses) {
  auto It = PerBlockAccesses.find(BB);
  // Skip most processing if the list is empty.
  if (It != PerBlockAccesses.end()) {
    AccessList *Accesses = It->second.get();
    for (MemoryAccess &L : *Accesses) {
      if (MemoryUseOrDef *MUD = dyn_cast<MemoryUseOrDef>(&L)) {
        if (MUD->getDefiningAccess() == nullptr || RenameAllUses)
          MUD->setDefiningAccess(IncomingVal);
        if (isa<MemoryDef>(&L))
          IncomingVal = &L;
      } else {
        IncomingVal = &L;
      }
    }
  }
  return IncomingVal;
}

/// This is the standard SSA renaming algorithm.
///
/// We walk the dominator tree in preorder, renaming accesses, and then filling
/// in phi nodes in our successors.
void MemorySSA::renamePass(DomTreeNode *Root, MemoryAccess *IncomingVal,
                           SmallPtrSetImpl<BasicBlock *> &Visited,
                           bool SkipVisited, bool RenameAllUses) {
  SmallVector<RenamePassData, 32> WorkStack;
  // Skip everything if we already renamed this block and we are skipping.
  // Note: You can't sink this into the if, because we need it to occur
  // regardless of whether we skip blocks or not.
  bool AlreadyVisited = !Visited.insert(Root->getBlock()).second;
  if (SkipVisited && AlreadyVisited)
    return;

  IncomingVal = renameBlock(Root->getBlock(), IncomingVal, RenameAllUses);
  renameSuccessorPhis(Root->getBlock(), IncomingVal, RenameAllUses);
  WorkStack.push_back({Root, Root->begin(), IncomingVal});

  while (!WorkStack.empty()) {
    DomTreeNode *Node = WorkStack.back().DTN;
    DomTreeNode::const_iterator ChildIt = WorkStack.back().ChildIt;
    IncomingVal = WorkStack.back().IncomingVal;

    if (ChildIt == Node->end()) {
      WorkStack.pop_back();
    } else {
      DomTreeNode *Child = *ChildIt;
      ++WorkStack.back().ChildIt;
      BasicBlock *BB = Child->getBlock();
      // Note: You can't sink this into the if, because we need it to occur
      // regardless of whether we skip blocks or not.
      AlreadyVisited = !Visited.insert(BB).second;
      if (SkipVisited && AlreadyVisited) {
        // We already visited this during our renaming, which can happen when
        // being asked to rename multiple blocks. Figure out the incoming val,
        // which is the last def.
        // Incoming value can only change if there is a block def, and in that
        // case, it's the last block def in the list.
        if (auto *BlockDefs = getWritableBlockDefs(BB))
          IncomingVal = &*BlockDefs->rbegin();
      } else
        IncomingVal = renameBlock(BB, IncomingVal, RenameAllUses);
      renameSuccessorPhis(BB, IncomingVal, RenameAllUses);
      WorkStack.push_back({Child, Child->begin(), IncomingVal});
    }
  }
}

/// This handles unreachable block accesses by deleting phi nodes in
/// unreachable blocks, and marking all other unreachable MemoryAccess's as
/// being uses of the live on entry definition.
void MemorySSA::markUnreachableAsLiveOnEntry(BasicBlock *BB) {
  assert(!DT->isReachableFromEntry(BB) &&
         "Reachable block found while handling unreachable blocks");

  // Make sure phi nodes in our reachable successors end up with a
  // LiveOnEntryDef for our incoming edge, even though our block is forward
  // unreachable.  We could just disconnect these blocks from the CFG fully,
  // but we do not right now.
  for (const BasicBlock *S : successors(BB)) {
    if (!DT->isReachableFromEntry(S))
      continue;
    auto It = PerBlockAccesses.find(S);
    // Rename the phi nodes in our successor block
    if (It == PerBlockAccesses.end() || !isa<MemoryPhi>(It->second->front()))
      continue;
    AccessList *Accesses = It->second.get();
    auto *Phi = cast<MemoryPhi>(&Accesses->front());
    Phi->addIncoming(LiveOnEntryDef.get(), BB);
  }

  auto It = PerBlockAccesses.find(BB);
  if (It == PerBlockAccesses.end())
    return;

  auto &Accesses = It->second;
  for (auto AI = Accesses->begin(), AE = Accesses->end(); AI != AE;) {
    auto Next = std::next(AI);
    // If we have a phi, just remove it. We are going to replace all
    // users with live on entry.
    if (auto *UseOrDef = dyn_cast<MemoryUseOrDef>(AI))
      UseOrDef->setDefiningAccess(LiveOnEntryDef.get());
    else
      Accesses->erase(AI);
    AI = Next;
  }
}

MemorySSA::MemorySSA(Function &Func, AliasAnalysis *AA, DominatorTree *DT)
    : AA(AA), DT(DT), F(Func), LiveOnEntryDef(nullptr), Walker(nullptr),
      SkipWalker(nullptr), NextID(0) {
  buildMemorySSA();
}

MemorySSA::~MemorySSA() {
  // Drop all our references
  for (const auto &Pair : PerBlockAccesses)
    for (MemoryAccess &MA : *Pair.second)
      MA.dropAllReferences();
}

MemorySSA::AccessList *MemorySSA::getOrCreateAccessList(const BasicBlock *BB) {
  auto Res = PerBlockAccesses.insert(std::make_pair(BB, nullptr));

  if (Res.second)
    Res.first->second = llvm::make_unique<AccessList>();
  return Res.first->second.get();
}

MemorySSA::DefsList *MemorySSA::getOrCreateDefsList(const BasicBlock *BB) {
  auto Res = PerBlockDefs.insert(std::make_pair(BB, nullptr));

  if (Res.second)
    Res.first->second = llvm::make_unique<DefsList>();
  return Res.first->second.get();
}

namespace llvm {

/// This class is a batch walker of all MemoryUse's in the program, and points
/// their defining access at the thing that actually clobbers them.  Because it
/// is a batch walker that touches everything, it does not operate like the
/// other walkers.  This walker is basically performing a top-down SSA renaming
/// pass, where the version stack is used as the cache.  This enables it to be
/// significantly more time and memory efficient than using the regular walker,
/// which is walking bottom-up.
class MemorySSA::OptimizeUses {
public:
  OptimizeUses(MemorySSA *MSSA, MemorySSAWalker *Walker, AliasAnalysis *AA,
               DominatorTree *DT)
      : MSSA(MSSA), Walker(Walker), AA(AA), DT(DT) {
    Walker = MSSA->getWalker();
  }

  void optimizeUses();

private:
  /// This represents where a given memorylocation is in the stack.
  struct MemlocStackInfo {
    // This essentially is keeping track of versions of the stack. Whenever
    // the stack changes due to pushes or pops, these versions increase.
    unsigned long StackEpoch;
    unsigned long PopEpoch;
    // This is the lower bound of places on the stack to check. It is equal to
    // the place the last stack walk ended.
    // Note: Correctness depends on this being initialized to 0, which densemap
    // does
    unsigned long LowerBound;
    const BasicBlock *LowerBoundBlock;
    // This is where the last walk for this memory location ended.
    unsigned long LastKill;
    bool LastKillValid;
    Optional<AliasResult> AR;
  };

  void optimizeUsesInBlock(const BasicBlock *, unsigned long &, unsigned long &,
                           SmallVectorImpl<MemoryAccess *> &,
                           DenseMap<MemoryLocOrCall, MemlocStackInfo> &);

  MemorySSA *MSSA;
  MemorySSAWalker *Walker;
  AliasAnalysis *AA;
  DominatorTree *DT;
};

} // end namespace llvm

/// Optimize the uses in a given block This is basically the SSA renaming
/// algorithm, with one caveat: We are able to use a single stack for all
/// MemoryUses.  This is because the set of *possible* reaching MemoryDefs is
/// the same for every MemoryUse.  The *actual* clobbering MemoryDef is just
/// going to be some position in that stack of possible ones.
///
/// We track the stack positions that each MemoryLocation needs
/// to check, and last ended at.  This is because we only want to check the
/// things that changed since last time.  The same MemoryLocation should
/// get clobbered by the same store (getModRefInfo does not use invariantness or
/// things like this, and if they start, we can modify MemoryLocOrCall to
/// include relevant data)
void MemorySSA::OptimizeUses::optimizeUsesInBlock(
    const BasicBlock *BB, unsigned long &StackEpoch, unsigned long &PopEpoch,
    SmallVectorImpl<MemoryAccess *> &VersionStack,
    DenseMap<MemoryLocOrCall, MemlocStackInfo> &LocStackInfo) {

  /// If no accesses, nothing to do.
  MemorySSA::AccessList *Accesses = MSSA->getWritableBlockAccesses(BB);
  if (Accesses == nullptr)
    return;

  // Pop everything that doesn't dominate the current block off the stack,
  // increment the PopEpoch to account for this.
  while (true) {
    assert(
        !VersionStack.empty() &&
        "Version stack should have liveOnEntry sentinel dominating everything");
    BasicBlock *BackBlock = VersionStack.back()->getBlock();
    if (DT->dominates(BackBlock, BB))
      break;
    while (VersionStack.back()->getBlock() == BackBlock)
      VersionStack.pop_back();
    ++PopEpoch;
  }

  for (MemoryAccess &MA : *Accesses) {
    auto *MU = dyn_cast<MemoryUse>(&MA);
    if (!MU) {
      VersionStack.push_back(&MA);
      ++StackEpoch;
      continue;
    }

    if (isUseTriviallyOptimizableToLiveOnEntry(*AA, MU->getMemoryInst())) {
      MU->setDefiningAccess(MSSA->getLiveOnEntryDef(), true, None);
      continue;
    }

    MemoryLocOrCall UseMLOC(MU);
    auto &LocInfo = LocStackInfo[UseMLOC];
    // If the pop epoch changed, it means we've removed stuff from top of
    // stack due to changing blocks. We may have to reset the lower bound or
    // last kill info.
    if (LocInfo.PopEpoch != PopEpoch) {
      LocInfo.PopEpoch = PopEpoch;
      LocInfo.StackEpoch = StackEpoch;
      // If the lower bound was in something that no longer dominates us, we
      // have to reset it.
      // We can't simply track stack size, because the stack may have had
      // pushes/pops in the meantime.
      // XXX: This is non-optimal, but only is slower cases with heavily
      // branching dominator trees.  To get the optimal number of queries would
      // be to make lowerbound and lastkill a per-loc stack, and pop it until
      // the top of that stack dominates us.  This does not seem worth it ATM.
      // A much cheaper optimization would be to always explore the deepest
      // branch of the dominator tree first. This will guarantee this resets on
      // the smallest set of blocks.
      if (LocInfo.LowerBoundBlock && LocInfo.LowerBoundBlock != BB &&
          !DT->dominates(LocInfo.LowerBoundBlock, BB)) {
        // Reset the lower bound of things to check.
        // TODO: Some day we should be able to reset to last kill, rather than
        // 0.
        LocInfo.LowerBound = 0;
        LocInfo.LowerBoundBlock = VersionStack[0]->getBlock();
        LocInfo.LastKillValid = false;
      }
    } else if (LocInfo.StackEpoch != StackEpoch) {
      // If all that has changed is the StackEpoch, we only have to check the
      // new things on the stack, because we've checked everything before.  In
      // this case, the lower bound of things to check remains the same.
      LocInfo.PopEpoch = PopEpoch;
      LocInfo.StackEpoch = StackEpoch;
    }
    if (!LocInfo.LastKillValid) {
      LocInfo.LastKill = VersionStack.size() - 1;
      LocInfo.LastKillValid = true;
      LocInfo.AR = MayAlias;
    }

    // At this point, we should have corrected last kill and LowerBound to be
    // in bounds.
    assert(LocInfo.LowerBound < VersionStack.size() &&
           "Lower bound out of range");
    assert(LocInfo.LastKill < VersionStack.size() &&
           "Last kill info out of range");
    // In any case, the new upper bound is the top of the stack.
    unsigned long UpperBound = VersionStack.size() - 1;

    if (UpperBound - LocInfo.LowerBound > MaxCheckLimit) {
      LLVM_DEBUG(dbgs() << "MemorySSA skipping optimization of " << *MU << " ("
                        << *(MU->getMemoryInst()) << ")"
                        << " because there are "
                        << UpperBound - LocInfo.LowerBound
                        << " stores to disambiguate\n");
      // Because we did not walk, LastKill is no longer valid, as this may
      // have been a kill.
      LocInfo.LastKillValid = false;
      continue;
    }
    bool FoundClobberResult = false;
    while (UpperBound > LocInfo.LowerBound) {
      if (isa<MemoryPhi>(VersionStack[UpperBound])) {
        // For phis, use the walker, see where we ended up, go there
        Instruction *UseInst = MU->getMemoryInst();
        MemoryAccess *Result = Walker->getClobberingMemoryAccess(UseInst);
        // We are guaranteed to find it or something is wrong
        while (VersionStack[UpperBound] != Result) {
          assert(UpperBound != 0);
          --UpperBound;
        }
        FoundClobberResult = true;
        break;
      }

      MemoryDef *MD = cast<MemoryDef>(VersionStack[UpperBound]);
      // If the lifetime of the pointer ends at this instruction, it's live on
      // entry.
      if (!UseMLOC.IsCall && lifetimeEndsAt(MD, UseMLOC.getLoc(), *AA)) {
        // Reset UpperBound to liveOnEntryDef's place in the stack
        UpperBound = 0;
        FoundClobberResult = true;
        LocInfo.AR = MustAlias;
        break;
      }
      ClobberAlias CA = instructionClobbersQuery(MD, MU, UseMLOC, *AA);
      if (CA.IsClobber) {
        FoundClobberResult = true;
        LocInfo.AR = CA.AR;
        break;
      }
      --UpperBound;
    }

    // Note: Phis always have AliasResult AR set to MayAlias ATM.

    // At the end of this loop, UpperBound is either a clobber, or lower bound
    // PHI walking may cause it to be < LowerBound, and in fact, < LastKill.
    if (FoundClobberResult || UpperBound < LocInfo.LastKill) {
      // We were last killed now by where we got to
      if (MSSA->isLiveOnEntryDef(VersionStack[UpperBound]))
        LocInfo.AR = None;
      MU->setDefiningAccess(VersionStack[UpperBound], true, LocInfo.AR);
      LocInfo.LastKill = UpperBound;
    } else {
      // Otherwise, we checked all the new ones, and now we know we can get to
      // LastKill.
      MU->setDefiningAccess(VersionStack[LocInfo.LastKill], true, LocInfo.AR);
    }
    LocInfo.LowerBound = VersionStack.size() - 1;
    LocInfo.LowerBoundBlock = BB;
  }
}

/// Optimize uses to point to their actual clobbering definitions.
void MemorySSA::OptimizeUses::optimizeUses() {
  SmallVector<MemoryAccess *, 16> VersionStack;
  DenseMap<MemoryLocOrCall, MemlocStackInfo> LocStackInfo;
  VersionStack.push_back(MSSA->getLiveOnEntryDef());

  unsigned long StackEpoch = 1;
  unsigned long PopEpoch = 1;
  // We perform a non-recursive top-down dominator tree walk.
  for (const auto *DomNode : depth_first(DT->getRootNode()))
    optimizeUsesInBlock(DomNode->getBlock(), StackEpoch, PopEpoch, VersionStack,
                        LocStackInfo);
}

void MemorySSA::placePHINodes(
    const SmallPtrSetImpl<BasicBlock *> &DefiningBlocks) {
  // Determine where our MemoryPhi's should go
  ForwardIDFCalculator IDFs(*DT);
  IDFs.setDefiningBlocks(DefiningBlocks);
  SmallVector<BasicBlock *, 32> IDFBlocks;
  IDFs.calculate(IDFBlocks);

  // Now place MemoryPhi nodes.
  for (auto &BB : IDFBlocks)
    createMemoryPhi(BB);
}

void MemorySSA::buildMemorySSA() {
  // We create an access to represent "live on entry", for things like
  // arguments or users of globals, where the memory they use is defined before
  // the beginning of the function. We do not actually insert it into the IR.
  // We do not define a live on exit for the immediate uses, and thus our
  // semantics do *not* imply that something with no immediate uses can simply
  // be removed.
  BasicBlock &StartingPoint = F.getEntryBlock();
  LiveOnEntryDef.reset(new MemoryDef(F.getContext(), nullptr, nullptr,
                                     &StartingPoint, NextID++));

  // We maintain lists of memory accesses per-block, trading memory for time. We
  // could just look up the memory access for every possible instruction in the
  // stream.
  SmallPtrSet<BasicBlock *, 32> DefiningBlocks;
  // Go through each block, figure out where defs occur, and chain together all
  // the accesses.
  for (BasicBlock &B : F) {
    bool InsertIntoDef = false;
    AccessList *Accesses = nullptr;
    DefsList *Defs = nullptr;
    for (Instruction &I : B) {
      MemoryUseOrDef *MUD = createNewAccess(&I);
      if (!MUD)
        continue;

      if (!Accesses)
        Accesses = getOrCreateAccessList(&B);
      Accesses->push_back(MUD);
      if (isa<MemoryDef>(MUD)) {
        InsertIntoDef = true;
        if (!Defs)
          Defs = getOrCreateDefsList(&B);
        Defs->push_back(*MUD);
      }
    }
    if (InsertIntoDef)
      DefiningBlocks.insert(&B);
  }
  placePHINodes(DefiningBlocks);

  // Now do regular SSA renaming on the MemoryDef/MemoryUse. Visited will get
  // filled in with all blocks.
  SmallPtrSet<BasicBlock *, 16> Visited;
  renamePass(DT->getRootNode(), LiveOnEntryDef.get(), Visited);

  CachingWalker *Walker = getWalkerImpl();

  OptimizeUses(this, Walker, AA, DT).optimizeUses();

  // Mark the uses in unreachable blocks as live on entry, so that they go
  // somewhere.
  for (auto &BB : F)
    if (!Visited.count(&BB))
      markUnreachableAsLiveOnEntry(&BB);
}

MemorySSAWalker *MemorySSA::getWalker() { return getWalkerImpl(); }

MemorySSA::CachingWalker *MemorySSA::getWalkerImpl() {
  if (Walker)
    return Walker.get();

  if (!WalkerBase)
    WalkerBase = llvm::make_unique<ClobberWalkerBase>(this, AA, DT);

  Walker = llvm::make_unique<CachingWalker>(this, WalkerBase.get());
  return Walker.get();
}

MemorySSAWalker *MemorySSA::getSkipSelfWalker() {
  if (SkipWalker)
    return SkipWalker.get();

  if (!WalkerBase)
    WalkerBase = llvm::make_unique<ClobberWalkerBase>(this, AA, DT);

  SkipWalker = llvm::make_unique<SkipSelfWalker>(this, WalkerBase.get());
  return SkipWalker.get();
 }


// This is a helper function used by the creation routines. It places NewAccess
// into the access and defs lists for a given basic block, at the given
// insertion point.
void MemorySSA::insertIntoListsForBlock(MemoryAccess *NewAccess,
                                        const BasicBlock *BB,
                                        InsertionPlace Point) {
  auto *Accesses = getOrCreateAccessList(BB);
  if (Point == Beginning) {
    // If it's a phi node, it goes first, otherwise, it goes after any phi
    // nodes.
    if (isa<MemoryPhi>(NewAccess)) {
      Accesses->push_front(NewAccess);
      auto *Defs = getOrCreateDefsList(BB);
      Defs->push_front(*NewAccess);
    } else {
      auto AI = find_if_not(
          *Accesses, [](const MemoryAccess &MA) { return isa<MemoryPhi>(MA); });
      Accesses->insert(AI, NewAccess);
      if (!isa<MemoryUse>(NewAccess)) {
        auto *Defs = getOrCreateDefsList(BB);
        auto DI = find_if_not(
            *Defs, [](const MemoryAccess &MA) { return isa<MemoryPhi>(MA); });
        Defs->insert(DI, *NewAccess);
      }
    }
  } else {
    Accesses->push_back(NewAccess);
    if (!isa<MemoryUse>(NewAccess)) {
      auto *Defs = getOrCreateDefsList(BB);
      Defs->push_back(*NewAccess);
    }
  }
  BlockNumberingValid.erase(BB);
}

void MemorySSA::insertIntoListsBefore(MemoryAccess *What, const BasicBlock *BB,
                                      AccessList::iterator InsertPt) {
  auto *Accesses = getWritableBlockAccesses(BB);
  bool WasEnd = InsertPt == Accesses->end();
  Accesses->insert(AccessList::iterator(InsertPt), What);
  if (!isa<MemoryUse>(What)) {
    auto *Defs = getOrCreateDefsList(BB);
    // If we got asked to insert at the end, we have an easy job, just shove it
    // at the end. If we got asked to insert before an existing def, we also get
    // an iterator. If we got asked to insert before a use, we have to hunt for
    // the next def.
    if (WasEnd) {
      Defs->push_back(*What);
    } else if (isa<MemoryDef>(InsertPt)) {
      Defs->insert(InsertPt->getDefsIterator(), *What);
    } else {
      while (InsertPt != Accesses->end() && !isa<MemoryDef>(InsertPt))
        ++InsertPt;
      // Either we found a def, or we are inserting at the end
      if (InsertPt == Accesses->end())
        Defs->push_back(*What);
      else
        Defs->insert(InsertPt->getDefsIterator(), *What);
    }
  }
  BlockNumberingValid.erase(BB);
}

void MemorySSA::prepareForMoveTo(MemoryAccess *What, BasicBlock *BB) {
  // Keep it in the lookup tables, remove from the lists
  removeFromLists(What, false);

  // Note that moving should implicitly invalidate the optimized state of a
  // MemoryUse (and Phis can't be optimized). However, it doesn't do so for a
  // MemoryDef.
  if (auto *MD = dyn_cast<MemoryDef>(What))
    MD->resetOptimized();
  What->setBlock(BB);
}

// Move What before Where in the IR.  The end result is that What will belong to
// the right lists and have the right Block set, but will not otherwise be
// correct. It will not have the right defining access, and if it is a def,
// things below it will not properly be updated.
void MemorySSA::moveTo(MemoryUseOrDef *What, BasicBlock *BB,
                       AccessList::iterator Where) {
  prepareForMoveTo(What, BB);
  insertIntoListsBefore(What, BB, Where);
}

void MemorySSA::moveTo(MemoryAccess *What, BasicBlock *BB,
                       InsertionPlace Point) {
  if (isa<MemoryPhi>(What)) {
    assert(Point == Beginning &&
           "Can only move a Phi at the beginning of the block");
    // Update lookup table entry
    ValueToMemoryAccess.erase(What->getBlock());
    bool Inserted = ValueToMemoryAccess.insert({BB, What}).second;
    (void)Inserted;
    assert(Inserted && "Cannot move a Phi to a block that already has one");
  }

  prepareForMoveTo(What, BB);
  insertIntoListsForBlock(What, BB, Point);
}

MemoryPhi *MemorySSA::createMemoryPhi(BasicBlock *BB) {
  assert(!getMemoryAccess(BB) && "MemoryPhi already exists for this BB");
  MemoryPhi *Phi = new MemoryPhi(BB->getContext(), BB, NextID++);
  // Phi's always are placed at the front of the block.
  insertIntoListsForBlock(Phi, BB, Beginning);
  ValueToMemoryAccess[BB] = Phi;
  return Phi;
}

MemoryUseOrDef *MemorySSA::createDefinedAccess(Instruction *I,
                                               MemoryAccess *Definition,
                                               const MemoryUseOrDef *Template) {
  assert(!isa<PHINode>(I) && "Cannot create a defined access for a PHI");
  MemoryUseOrDef *NewAccess = createNewAccess(I, Template);
  assert(
      NewAccess != nullptr &&
      "Tried to create a memory access for a non-memory touching instruction");
  NewAccess->setDefiningAccess(Definition);
  return NewAccess;
}

// Return true if the instruction has ordering constraints.
// Note specifically that this only considers stores and loads
// because others are still considered ModRef by getModRefInfo.
static inline bool isOrdered(const Instruction *I) {
  if (auto *SI = dyn_cast<StoreInst>(I)) {
    if (!SI->isUnordered())
      return true;
  } else if (auto *LI = dyn_cast<LoadInst>(I)) {
    if (!LI->isUnordered())
      return true;
  }
  return false;
}

/// Helper function to create new memory accesses
MemoryUseOrDef *MemorySSA::createNewAccess(Instruction *I,
                                           const MemoryUseOrDef *Template) {
  // The assume intrinsic has a control dependency which we model by claiming
  // that it writes arbitrarily. Ignore that fake memory dependency here.
  // FIXME: Replace this special casing with a more accurate modelling of
  // assume's control dependency.
  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I))
    if (II->getIntrinsicID() == Intrinsic::assume)
      return nullptr;

  bool Def, Use;
  if (Template) {
    Def = dyn_cast_or_null<MemoryDef>(Template) != nullptr;
    Use = dyn_cast_or_null<MemoryUse>(Template) != nullptr;
#if !defined(NDEBUG)
    ModRefInfo ModRef = AA->getModRefInfo(I, None);
    bool DefCheck, UseCheck;
    DefCheck = isModSet(ModRef) || isOrdered(I);
    UseCheck = isRefSet(ModRef);
    assert(Def == DefCheck && (Def || Use == UseCheck) && "Invalid template");
#endif
  } else {
    // Find out what affect this instruction has on memory.
    ModRefInfo ModRef = AA->getModRefInfo(I, None);
    // The isOrdered check is used to ensure that volatiles end up as defs
    // (atomics end up as ModRef right now anyway).  Until we separate the
    // ordering chain from the memory chain, this enables people to see at least
    // some relative ordering to volatiles.  Note that getClobberingMemoryAccess
    // will still give an answer that bypasses other volatile loads.  TODO:
    // Separate memory aliasing and ordering into two different chains so that
    // we can precisely represent both "what memory will this read/write/is
    // clobbered by" and "what instructions can I move this past".
    Def = isModSet(ModRef) || isOrdered(I);
    Use = isRefSet(ModRef);
  }

  // It's possible for an instruction to not modify memory at all. During
  // construction, we ignore them.
  if (!Def && !Use)
    return nullptr;

  MemoryUseOrDef *MUD;
  if (Def)
    MUD = new MemoryDef(I->getContext(), nullptr, I, I->getParent(), NextID++);
  else
    MUD = new MemoryUse(I->getContext(), nullptr, I, I->getParent());
  ValueToMemoryAccess[I] = MUD;
  return MUD;
}

/// Returns true if \p Replacer dominates \p Replacee .
bool MemorySSA::dominatesUse(const MemoryAccess *Replacer,
                             const MemoryAccess *Replacee) const {
  if (isa<MemoryUseOrDef>(Replacee))
    return DT->dominates(Replacer->getBlock(), Replacee->getBlock());
  const auto *MP = cast<MemoryPhi>(Replacee);
  // For a phi node, the use occurs in the predecessor block of the phi node.
  // Since we may occur multiple times in the phi node, we have to check each
  // operand to ensure Replacer dominates each operand where Replacee occurs.
  for (const Use &Arg : MP->operands()) {
    if (Arg.get() != Replacee &&
        !DT->dominates(Replacer->getBlock(), MP->getIncomingBlock(Arg)))
      return false;
  }
  return true;
}

/// Properly remove \p MA from all of MemorySSA's lookup tables.
void MemorySSA::removeFromLookups(MemoryAccess *MA) {
  assert(MA->use_empty() &&
         "Trying to remove memory access that still has uses");
  BlockNumbering.erase(MA);
  if (auto *MUD = dyn_cast<MemoryUseOrDef>(MA))
    MUD->setDefiningAccess(nullptr);
  // Invalidate our walker's cache if necessary
  if (!isa<MemoryUse>(MA))
    Walker->invalidateInfo(MA);

  Value *MemoryInst;
  if (const auto *MUD = dyn_cast<MemoryUseOrDef>(MA))
    MemoryInst = MUD->getMemoryInst();
  else
    MemoryInst = MA->getBlock();

  auto VMA = ValueToMemoryAccess.find(MemoryInst);
  if (VMA->second == MA)
    ValueToMemoryAccess.erase(VMA);
}

/// Properly remove \p MA from all of MemorySSA's lists.
///
/// Because of the way the intrusive list and use lists work, it is important to
/// do removal in the right order.
/// ShouldDelete defaults to true, and will cause the memory access to also be
/// deleted, not just removed.
void MemorySSA::removeFromLists(MemoryAccess *MA, bool ShouldDelete) {
  BasicBlock *BB = MA->getBlock();
  // The access list owns the reference, so we erase it from the non-owning list
  // first.
  if (!isa<MemoryUse>(MA)) {
    auto DefsIt = PerBlockDefs.find(BB);
    std::unique_ptr<DefsList> &Defs = DefsIt->second;
    Defs->remove(*MA);
    if (Defs->empty())
      PerBlockDefs.erase(DefsIt);
  }

  // The erase call here will delete it. If we don't want it deleted, we call
  // remove instead.
  auto AccessIt = PerBlockAccesses.find(BB);
  std::unique_ptr<AccessList> &Accesses = AccessIt->second;
  if (ShouldDelete)
    Accesses->erase(MA);
  else
    Accesses->remove(MA);

  if (Accesses->empty()) {
    PerBlockAccesses.erase(AccessIt);
    BlockNumberingValid.erase(BB);
  }
}

void MemorySSA::print(raw_ostream &OS) const {
  MemorySSAAnnotatedWriter Writer(this);
  F.print(OS, &Writer);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void MemorySSA::dump() const { print(dbgs()); }
#endif

void MemorySSA::verifyMemorySSA() const {
  verifyDefUses(F);
  verifyDomination(F);
  verifyOrdering(F);
  verifyDominationNumbers(F);
  Walker->verify(this);
  verifyClobberSanity(F);
}

/// Check sanity of the clobbering instruction for access MA.
void MemorySSA::checkClobberSanityAccess(const MemoryAccess *MA) const {
  if (const auto *MUD = dyn_cast<MemoryUseOrDef>(MA)) {
    if (!MUD->isOptimized())
      return;
    auto *I = MUD->getMemoryInst();
    auto Loc = MemoryLocation::getOrNone(I);
    if (Loc == None)
      return;
    auto *Clobber = MUD->getOptimized();
    UpwardsMemoryQuery Q(I, MUD);
    checkClobberSanity(MUD, Clobber, *Loc, *this, Q, *AA, true);
  }
}

void MemorySSA::verifyClobberSanity(const Function &F) const {
#if !defined(NDEBUG) && defined(EXPENSIVE_CHECKS)
  for (const BasicBlock &BB : F) {
    const AccessList *Accesses = getBlockAccesses(&BB);
    if (!Accesses)
      continue;
    for (const MemoryAccess &MA : *Accesses)
      checkClobberSanityAccess(&MA);
  }
#endif
}

/// Verify that all of the blocks we believe to have valid domination numbers
/// actually have valid domination numbers.
void MemorySSA::verifyDominationNumbers(const Function &F) const {
#ifndef NDEBUG
  if (BlockNumberingValid.empty())
    return;

  SmallPtrSet<const BasicBlock *, 16> ValidBlocks = BlockNumberingValid;
  for (const BasicBlock &BB : F) {
    if (!ValidBlocks.count(&BB))
      continue;

    ValidBlocks.erase(&BB);

    const AccessList *Accesses = getBlockAccesses(&BB);
    // It's correct to say an empty block has valid numbering.
    if (!Accesses)
      continue;

    // Block numbering starts at 1.
    unsigned long LastNumber = 0;
    for (const MemoryAccess &MA : *Accesses) {
      auto ThisNumberIter = BlockNumbering.find(&MA);
      assert(ThisNumberIter != BlockNumbering.end() &&
             "MemoryAccess has no domination number in a valid block!");

      unsigned long ThisNumber = ThisNumberIter->second;
      assert(ThisNumber > LastNumber &&
             "Domination numbers should be strictly increasing!");
      LastNumber = ThisNumber;
    }
  }

  assert(ValidBlocks.empty() &&
         "All valid BasicBlocks should exist in F -- dangling pointers?");
#endif
}

/// Verify that the order and existence of MemoryAccesses matches the
/// order and existence of memory affecting instructions.
void MemorySSA::verifyOrdering(Function &F) const {
#ifndef NDEBUG
  // Walk all the blocks, comparing what the lookups think and what the access
  // lists think, as well as the order in the blocks vs the order in the access
  // lists.
  SmallVector<MemoryAccess *, 32> ActualAccesses;
  SmallVector<MemoryAccess *, 32> ActualDefs;
  for (BasicBlock &B : F) {
    const AccessList *AL = getBlockAccesses(&B);
    const auto *DL = getBlockDefs(&B);
    MemoryAccess *Phi = getMemoryAccess(&B);
    if (Phi) {
      ActualAccesses.push_back(Phi);
      ActualDefs.push_back(Phi);
    }

    for (Instruction &I : B) {
      MemoryAccess *MA = getMemoryAccess(&I);
      assert((!MA || (AL && (isa<MemoryUse>(MA) || DL))) &&
             "We have memory affecting instructions "
             "in this block but they are not in the "
             "access list or defs list");
      if (MA) {
        ActualAccesses.push_back(MA);
        if (isa<MemoryDef>(MA))
          ActualDefs.push_back(MA);
      }
    }
    // Either we hit the assert, really have no accesses, or we have both
    // accesses and an access list.
    // Same with defs.
    if (!AL && !DL)
      continue;
    assert(AL->size() == ActualAccesses.size() &&
           "We don't have the same number of accesses in the block as on the "
           "access list");
    assert((DL || ActualDefs.size() == 0) &&
           "Either we should have a defs list, or we should have no defs");
    assert((!DL || DL->size() == ActualDefs.size()) &&
           "We don't have the same number of defs in the block as on the "
           "def list");
    auto ALI = AL->begin();
    auto AAI = ActualAccesses.begin();
    while (ALI != AL->end() && AAI != ActualAccesses.end()) {
      assert(&*ALI == *AAI && "Not the same accesses in the same order");
      ++ALI;
      ++AAI;
    }
    ActualAccesses.clear();
    if (DL) {
      auto DLI = DL->begin();
      auto ADI = ActualDefs.begin();
      while (DLI != DL->end() && ADI != ActualDefs.end()) {
        assert(&*DLI == *ADI && "Not the same defs in the same order");
        ++DLI;
        ++ADI;
      }
    }
    ActualDefs.clear();
  }
#endif
}

/// Verify the domination properties of MemorySSA by checking that each
/// definition dominates all of its uses.
void MemorySSA::verifyDomination(Function &F) const {
#ifndef NDEBUG
  for (BasicBlock &B : F) {
    // Phi nodes are attached to basic blocks
    if (MemoryPhi *MP = getMemoryAccess(&B))
      for (const Use &U : MP->uses())
        assert(dominates(MP, U) && "Memory PHI does not dominate it's uses");

    for (Instruction &I : B) {
      MemoryAccess *MD = dyn_cast_or_null<MemoryDef>(getMemoryAccess(&I));
      if (!MD)
        continue;

      for (const Use &U : MD->uses())
        assert(dominates(MD, U) && "Memory Def does not dominate it's uses");
    }
  }
#endif
}

/// Verify the def-use lists in MemorySSA, by verifying that \p Use
/// appears in the use list of \p Def.
void MemorySSA::verifyUseInDefs(MemoryAccess *Def, MemoryAccess *Use) const {
#ifndef NDEBUG
  // The live on entry use may cause us to get a NULL def here
  if (!Def)
    assert(isLiveOnEntryDef(Use) &&
           "Null def but use not point to live on entry def");
  else
    assert(is_contained(Def->users(), Use) &&
           "Did not find use in def's use list");
#endif
}

/// Verify the immediate use information, by walking all the memory
/// accesses and verifying that, for each use, it appears in the
/// appropriate def's use list
void MemorySSA::verifyDefUses(Function &F) const {
#ifndef NDEBUG
  for (BasicBlock &B : F) {
    // Phi nodes are attached to basic blocks
    if (MemoryPhi *Phi = getMemoryAccess(&B)) {
      assert(Phi->getNumOperands() == static_cast<unsigned>(std::distance(
                                          pred_begin(&B), pred_end(&B))) &&
             "Incomplete MemoryPhi Node");
      for (unsigned I = 0, E = Phi->getNumIncomingValues(); I != E; ++I) {
        verifyUseInDefs(Phi->getIncomingValue(I), Phi);
        assert(find(predecessors(&B), Phi->getIncomingBlock(I)) !=
                   pred_end(&B) &&
               "Incoming phi block not a block predecessor");
      }
    }

    for (Instruction &I : B) {
      if (MemoryUseOrDef *MA = getMemoryAccess(&I)) {
        verifyUseInDefs(MA->getDefiningAccess(), MA);
      }
    }
  }
#endif
}

/// Perform a local numbering on blocks so that instruction ordering can be
/// determined in constant time.
/// TODO: We currently just number in order.  If we numbered by N, we could
/// allow at least N-1 sequences of insertBefore or insertAfter (and at least
/// log2(N) sequences of mixed before and after) without needing to invalidate
/// the numbering.
void MemorySSA::renumberBlock(const BasicBlock *B) const {
  // The pre-increment ensures the numbers really start at 1.
  unsigned long CurrentNumber = 0;
  const AccessList *AL = getBlockAccesses(B);
  assert(AL != nullptr && "Asking to renumber an empty block");
  for (const auto &I : *AL)
    BlockNumbering[&I] = ++CurrentNumber;
  BlockNumberingValid.insert(B);
}

/// Determine, for two memory accesses in the same block,
/// whether \p Dominator dominates \p Dominatee.
/// \returns True if \p Dominator dominates \p Dominatee.
bool MemorySSA::locallyDominates(const MemoryAccess *Dominator,
                                 const MemoryAccess *Dominatee) const {
  const BasicBlock *DominatorBlock = Dominator->getBlock();

  assert((DominatorBlock == Dominatee->getBlock()) &&
         "Asking for local domination when accesses are in different blocks!");
  // A node dominates itself.
  if (Dominatee == Dominator)
    return true;

  // When Dominatee is defined on function entry, it is not dominated by another
  // memory access.
  if (isLiveOnEntryDef(Dominatee))
    return false;

  // When Dominator is defined on function entry, it dominates the other memory
  // access.
  if (isLiveOnEntryDef(Dominator))
    return true;

  if (!BlockNumberingValid.count(DominatorBlock))
    renumberBlock(DominatorBlock);

  unsigned long DominatorNum = BlockNumbering.lookup(Dominator);
  // All numbers start with 1
  assert(DominatorNum != 0 && "Block was not numbered properly");
  unsigned long DominateeNum = BlockNumbering.lookup(Dominatee);
  assert(DominateeNum != 0 && "Block was not numbered properly");
  return DominatorNum < DominateeNum;
}

bool MemorySSA::dominates(const MemoryAccess *Dominator,
                          const MemoryAccess *Dominatee) const {
  if (Dominator == Dominatee)
    return true;

  if (isLiveOnEntryDef(Dominatee))
    return false;

  if (Dominator->getBlock() != Dominatee->getBlock())
    return DT->dominates(Dominator->getBlock(), Dominatee->getBlock());
  return locallyDominates(Dominator, Dominatee);
}

bool MemorySSA::dominates(const MemoryAccess *Dominator,
                          const Use &Dominatee) const {
  if (MemoryPhi *MP = dyn_cast<MemoryPhi>(Dominatee.getUser())) {
    BasicBlock *UseBB = MP->getIncomingBlock(Dominatee);
    // The def must dominate the incoming block of the phi.
    if (UseBB != Dominator->getBlock())
      return DT->dominates(Dominator->getBlock(), UseBB);
    // If the UseBB and the DefBB are the same, compare locally.
    return locallyDominates(Dominator, cast<MemoryAccess>(Dominatee));
  }
  // If it's not a PHI node use, the normal dominates can already handle it.
  return dominates(Dominator, cast<MemoryAccess>(Dominatee.getUser()));
}

const static char LiveOnEntryStr[] = "liveOnEntry";

void MemoryAccess::print(raw_ostream &OS) const {
  switch (getValueID()) {
  case MemoryPhiVal: return static_cast<const MemoryPhi *>(this)->print(OS);
  case MemoryDefVal: return static_cast<const MemoryDef *>(this)->print(OS);
  case MemoryUseVal: return static_cast<const MemoryUse *>(this)->print(OS);
  }
  llvm_unreachable("invalid value id");
}

void MemoryDef::print(raw_ostream &OS) const {
  MemoryAccess *UO = getDefiningAccess();

  auto printID = [&OS](MemoryAccess *A) {
    if (A && A->getID())
      OS << A->getID();
    else
      OS << LiveOnEntryStr;
  };

  OS << getID() << " = MemoryDef(";
  printID(UO);
  OS << ")";

  if (isOptimized()) {
    OS << "->";
    printID(getOptimized());

    if (Optional<AliasResult> AR = getOptimizedAccessType())
      OS << " " << *AR;
  }
}

void MemoryPhi::print(raw_ostream &OS) const {
  bool First = true;
  OS << getID() << " = MemoryPhi(";
  for (const auto &Op : operands()) {
    BasicBlock *BB = getIncomingBlock(Op);
    MemoryAccess *MA = cast<MemoryAccess>(Op);
    if (!First)
      OS << ',';
    else
      First = false;

    OS << '{';
    if (BB->hasName())
      OS << BB->getName();
    else
      BB->printAsOperand(OS, false);
    OS << ',';
    if (unsigned ID = MA->getID())
      OS << ID;
    else
      OS << LiveOnEntryStr;
    OS << '}';
  }
  OS << ')';
}

void MemoryUse::print(raw_ostream &OS) const {
  MemoryAccess *UO = getDefiningAccess();
  OS << "MemoryUse(";
  if (UO && UO->getID())
    OS << UO->getID();
  else
    OS << LiveOnEntryStr;
  OS << ')';

  if (Optional<AliasResult> AR = getOptimizedAccessType())
    OS << " " << *AR;
}

void MemoryAccess::dump() const {
// Cannot completely remove virtual function even in release mode.
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  print(dbgs());
  dbgs() << "\n";
#endif
}

char MemorySSAPrinterLegacyPass::ID = 0;

MemorySSAPrinterLegacyPass::MemorySSAPrinterLegacyPass() : FunctionPass(ID) {
  initializeMemorySSAPrinterLegacyPassPass(*PassRegistry::getPassRegistry());
}

void MemorySSAPrinterLegacyPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<MemorySSAWrapperPass>();
}

bool MemorySSAPrinterLegacyPass::runOnFunction(Function &F) {
  auto &MSSA = getAnalysis<MemorySSAWrapperPass>().getMSSA();
  MSSA.print(dbgs());
  if (VerifyMemorySSA)
    MSSA.verifyMemorySSA();
  return false;
}

AnalysisKey MemorySSAAnalysis::Key;

MemorySSAAnalysis::Result MemorySSAAnalysis::run(Function &F,
                                                 FunctionAnalysisManager &AM) {
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &AA = AM.getResult<AAManager>(F);
  return MemorySSAAnalysis::Result(llvm::make_unique<MemorySSA>(F, &AA, &DT));
}

PreservedAnalyses MemorySSAPrinterPass::run(Function &F,
                                            FunctionAnalysisManager &AM) {
  OS << "MemorySSA for function: " << F.getName() << "\n";
  AM.getResult<MemorySSAAnalysis>(F).getMSSA().print(OS);

  return PreservedAnalyses::all();
}

PreservedAnalyses MemorySSAVerifierPass::run(Function &F,
                                             FunctionAnalysisManager &AM) {
  AM.getResult<MemorySSAAnalysis>(F).getMSSA().verifyMemorySSA();

  return PreservedAnalyses::all();
}

char MemorySSAWrapperPass::ID = 0;

MemorySSAWrapperPass::MemorySSAWrapperPass() : FunctionPass(ID) {
  initializeMemorySSAWrapperPassPass(*PassRegistry::getPassRegistry());
}

void MemorySSAWrapperPass::releaseMemory() { MSSA.reset(); }

void MemorySSAWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequiredTransitive<DominatorTreeWrapperPass>();
  AU.addRequiredTransitive<AAResultsWrapperPass>();
}

bool MemorySSAWrapperPass::runOnFunction(Function &F) {
  auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  auto &AA = getAnalysis<AAResultsWrapperPass>().getAAResults();
  MSSA.reset(new MemorySSA(F, &AA, &DT));
  return false;
}

void MemorySSAWrapperPass::verifyAnalysis() const { MSSA->verifyMemorySSA(); }

void MemorySSAWrapperPass::print(raw_ostream &OS, const Module *M) const {
  MSSA->print(OS);
}

MemorySSAWalker::MemorySSAWalker(MemorySSA *M) : MSSA(M) {}

/// Walk the use-def chains starting at \p StartingAccess and find
/// the MemoryAccess that actually clobbers Loc.
///
/// \returns our clobbering memory access
MemoryAccess *MemorySSA::ClobberWalkerBase::getClobberingMemoryAccessBase(
    MemoryAccess *StartingAccess, const MemoryLocation &Loc) {
  if (isa<MemoryPhi>(StartingAccess))
    return StartingAccess;

  auto *StartingUseOrDef = cast<MemoryUseOrDef>(StartingAccess);
  if (MSSA->isLiveOnEntryDef(StartingUseOrDef))
    return StartingUseOrDef;

  Instruction *I = StartingUseOrDef->getMemoryInst();

  // Conservatively, fences are always clobbers, so don't perform the walk if we
  // hit a fence.
  if (!isa<CallBase>(I) && I->isFenceLike())
    return StartingUseOrDef;

  UpwardsMemoryQuery Q;
  Q.OriginalAccess = StartingUseOrDef;
  Q.StartingLoc = Loc;
  Q.Inst = I;
  Q.IsCall = false;

  // Unlike the other function, do not walk to the def of a def, because we are
  // handed something we already believe is the clobbering access.
  // We never set SkipSelf to true in Q in this method.
  MemoryAccess *DefiningAccess = isa<MemoryUse>(StartingUseOrDef)
                                     ? StartingUseOrDef->getDefiningAccess()
                                     : StartingUseOrDef;

  MemoryAccess *Clobber = Walker.findClobber(DefiningAccess, Q);
  LLVM_DEBUG(dbgs() << "Starting Memory SSA clobber for " << *I << " is ");
  LLVM_DEBUG(dbgs() << *StartingUseOrDef << "\n");
  LLVM_DEBUG(dbgs() << "Final Memory SSA clobber for " << *I << " is ");
  LLVM_DEBUG(dbgs() << *Clobber << "\n");
  return Clobber;
}

MemoryAccess *
MemorySSA::ClobberWalkerBase::getClobberingMemoryAccessBase(MemoryAccess *MA,
                                                            bool SkipSelf) {
  auto *StartingAccess = dyn_cast<MemoryUseOrDef>(MA);
  // If this is a MemoryPhi, we can't do anything.
  if (!StartingAccess)
    return MA;

  bool IsOptimized = false;

  // If this is an already optimized use or def, return the optimized result.
  // Note: Currently, we store the optimized def result in a separate field,
  // since we can't use the defining access.
  if (StartingAccess->isOptimized()) {
    if (!SkipSelf || !isa<MemoryDef>(StartingAccess))
      return StartingAccess->getOptimized();
    IsOptimized = true;
  }

  const Instruction *I = StartingAccess->getMemoryInst();
  // We can't sanely do anything with a fence, since they conservatively clobber
  // all memory, and have no locations to get pointers from to try to
  // disambiguate.
  if (!isa<CallBase>(I) && I->isFenceLike())
    return StartingAccess;

  UpwardsMemoryQuery Q(I, StartingAccess);

  if (isUseTriviallyOptimizableToLiveOnEntry(*MSSA->AA, I)) {
    MemoryAccess *LiveOnEntry = MSSA->getLiveOnEntryDef();
    StartingAccess->setOptimized(LiveOnEntry);
    StartingAccess->setOptimizedAccessType(None);
    return LiveOnEntry;
  }

  MemoryAccess *OptimizedAccess;
  if (!IsOptimized) {
    // Start with the thing we already think clobbers this location
    MemoryAccess *DefiningAccess = StartingAccess->getDefiningAccess();

    // At this point, DefiningAccess may be the live on entry def.
    // If it is, we will not get a better result.
    if (MSSA->isLiveOnEntryDef(DefiningAccess)) {
      StartingAccess->setOptimized(DefiningAccess);
      StartingAccess->setOptimizedAccessType(None);
      return DefiningAccess;
    }

    OptimizedAccess = Walker.findClobber(DefiningAccess, Q);
    StartingAccess->setOptimized(OptimizedAccess);
    if (MSSA->isLiveOnEntryDef(OptimizedAccess))
      StartingAccess->setOptimizedAccessType(None);
    else if (Q.AR == MustAlias)
      StartingAccess->setOptimizedAccessType(MustAlias);
  } else
    OptimizedAccess = StartingAccess->getOptimized();

  LLVM_DEBUG(dbgs() << "Starting Memory SSA clobber for " << *I << " is ");
  LLVM_DEBUG(dbgs() << *StartingAccess << "\n");
  LLVM_DEBUG(dbgs() << "Optimized Memory SSA clobber for " << *I << " is ");
  LLVM_DEBUG(dbgs() << *OptimizedAccess << "\n");

  MemoryAccess *Result;
  if (SkipSelf && isa<MemoryPhi>(OptimizedAccess) &&
      isa<MemoryDef>(StartingAccess)) {
    assert(isa<MemoryDef>(Q.OriginalAccess));
    Q.SkipSelfAccess = true;
    Result = Walker.findClobber(OptimizedAccess, Q);
  } else
    Result = OptimizedAccess;

  LLVM_DEBUG(dbgs() << "Result Memory SSA clobber [SkipSelf = " << SkipSelf);
  LLVM_DEBUG(dbgs() << "] for " << *I << " is " << *Result << "\n");

  return Result;
}

MemoryAccess *
MemorySSA::CachingWalker::getClobberingMemoryAccess(MemoryAccess *MA) {
  return Walker->getClobberingMemoryAccessBase(MA, false);
}

MemoryAccess *
MemorySSA::CachingWalker::getClobberingMemoryAccess(MemoryAccess *MA,
                                                    const MemoryLocation &Loc) {
  return Walker->getClobberingMemoryAccessBase(MA, Loc);
}

MemoryAccess *
MemorySSA::SkipSelfWalker::getClobberingMemoryAccess(MemoryAccess *MA) {
  return Walker->getClobberingMemoryAccessBase(MA, true);
}

MemoryAccess *
MemorySSA::SkipSelfWalker::getClobberingMemoryAccess(MemoryAccess *MA,
                                                    const MemoryLocation &Loc) {
  return Walker->getClobberingMemoryAccessBase(MA, Loc);
}

MemoryAccess *
DoNothingMemorySSAWalker::getClobberingMemoryAccess(MemoryAccess *MA) {
  if (auto *Use = dyn_cast<MemoryUseOrDef>(MA))
    return Use->getDefiningAccess();
  return MA;
}

MemoryAccess *DoNothingMemorySSAWalker::getClobberingMemoryAccess(
    MemoryAccess *StartingAccess, const MemoryLocation &) {
  if (auto *Use = dyn_cast<MemoryUseOrDef>(StartingAccess))
    return Use->getDefiningAccess();
  return StartingAccess;
}

void MemoryPhi::deleteMe(DerivedUser *Self) {
  delete static_cast<MemoryPhi *>(Self);
}

void MemoryDef::deleteMe(DerivedUser *Self) {
  delete static_cast<MemoryDef *>(Self);
}

void MemoryUse::deleteMe(DerivedUser *Self) {
  delete static_cast<MemoryUse *>(Self);
}
