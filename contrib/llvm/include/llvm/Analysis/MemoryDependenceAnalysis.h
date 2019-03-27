//===- llvm/Analysis/MemoryDependenceAnalysis.h - Memory Deps ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the MemoryDependenceAnalysis analysis pass.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MEMORYDEPENDENCEANALYSIS_H
#define LLVM_ANALYSIS_MEMORYDEPENDENCEANALYSIS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/PointerEmbeddedInt.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerSumType.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PredIteratorCache.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

namespace llvm {

class AssumptionCache;
class DominatorTree;
class Function;
class Instruction;
class LoadInst;
class PHITransAddr;
class TargetLibraryInfo;
class PhiValues;
class Value;

/// A memory dependence query can return one of three different answers.
class MemDepResult {
  enum DepType {
    /// Clients of MemDep never see this.
    ///
    /// Entries with this marker occur in a LocalDeps map or NonLocalDeps map
    /// when the instruction they previously referenced was removed from
    /// MemDep.  In either case, the entry may include an instruction pointer.
    /// If so, the pointer is an instruction in the block where scanning can
    /// start from, saving some work.
    ///
    /// In a default-constructed MemDepResult object, the type will be Invalid
    /// and the instruction pointer will be null.
    Invalid = 0,

    /// This is a dependence on the specified instruction which clobbers the
    /// desired value.  The pointer member of the MemDepResult pair holds the
    /// instruction that clobbers the memory.  For example, this occurs when we
    /// see a may-aliased store to the memory location we care about.
    ///
    /// There are several cases that may be interesting here:
    ///   1. Loads are clobbered by may-alias stores.
    ///   2. Loads are considered clobbered by partially-aliased loads.  The
    ///      client may choose to analyze deeper into these cases.
    Clobber,

    /// This is a dependence on the specified instruction which defines or
    /// produces the desired memory location.  The pointer member of the
    /// MemDepResult pair holds the instruction that defines the memory.
    ///
    /// Cases of interest:
    ///   1. This could be a load or store for dependence queries on
    ///      load/store.  The value loaded or stored is the produced value.
    ///      Note that the pointer operand may be different than that of the
    ///      queried pointer due to must aliases and phi translation. Note
    ///      that the def may not be the same type as the query, the pointers
    ///      may just be must aliases.
    ///   2. For loads and stores, this could be an allocation instruction. In
    ///      this case, the load is loading an undef value or a store is the
    ///      first store to (that part of) the allocation.
    ///   3. Dependence queries on calls return Def only when they are readonly
    ///      calls or memory use intrinsics with identical callees and no
    ///      intervening clobbers.  No validation is done that the operands to
    ///      the calls are the same.
    Def,

    /// This marker indicates that the query has no known dependency in the
    /// specified block.
    ///
    /// More detailed state info is encoded in the upper part of the pair (i.e.
    /// the Instruction*)
    Other
  };

  /// If DepType is "Other", the upper part of the sum type is an encoding of
  /// the following more detailed type information.
  enum OtherType {
    /// This marker indicates that the query has no dependency in the specified
    /// block.
    ///
    /// To find out more, the client should query other predecessor blocks.
    NonLocal = 1,
    /// This marker indicates that the query has no dependency in the specified
    /// function.
    NonFuncLocal,
    /// This marker indicates that the query dependency is unknown.
    Unknown
  };

  using ValueTy = PointerSumType<
      DepType, PointerSumTypeMember<Invalid, Instruction *>,
      PointerSumTypeMember<Clobber, Instruction *>,
      PointerSumTypeMember<Def, Instruction *>,
      PointerSumTypeMember<Other, PointerEmbeddedInt<OtherType, 3>>>;
  ValueTy Value;

  explicit MemDepResult(ValueTy V) : Value(V) {}

public:
  MemDepResult() = default;

  /// get methods: These are static ctor methods for creating various
  /// MemDepResult kinds.
  static MemDepResult getDef(Instruction *Inst) {
    assert(Inst && "Def requires inst");
    return MemDepResult(ValueTy::create<Def>(Inst));
  }
  static MemDepResult getClobber(Instruction *Inst) {
    assert(Inst && "Clobber requires inst");
    return MemDepResult(ValueTy::create<Clobber>(Inst));
  }
  static MemDepResult getNonLocal() {
    return MemDepResult(ValueTy::create<Other>(NonLocal));
  }
  static MemDepResult getNonFuncLocal() {
    return MemDepResult(ValueTy::create<Other>(NonFuncLocal));
  }
  static MemDepResult getUnknown() {
    return MemDepResult(ValueTy::create<Other>(Unknown));
  }

  /// Tests if this MemDepResult represents a query that is an instruction
  /// clobber dependency.
  bool isClobber() const { return Value.is<Clobber>(); }

  /// Tests if this MemDepResult represents a query that is an instruction
  /// definition dependency.
  bool isDef() const { return Value.is<Def>(); }

  /// Tests if this MemDepResult represents a query that is transparent to the
  /// start of the block, but where a non-local hasn't been done.
  bool isNonLocal() const {
    return Value.is<Other>() && Value.cast<Other>() == NonLocal;
  }

  /// Tests if this MemDepResult represents a query that is transparent to the
  /// start of the function.
  bool isNonFuncLocal() const {
    return Value.is<Other>() && Value.cast<Other>() == NonFuncLocal;
  }

  /// Tests if this MemDepResult represents a query which cannot and/or will
  /// not be computed.
  bool isUnknown() const {
    return Value.is<Other>() && Value.cast<Other>() == Unknown;
  }

  /// If this is a normal dependency, returns the instruction that is depended
  /// on.  Otherwise, returns null.
  Instruction *getInst() const {
    switch (Value.getTag()) {
    case Invalid:
      return Value.cast<Invalid>();
    case Clobber:
      return Value.cast<Clobber>();
    case Def:
      return Value.cast<Def>();
    case Other:
      return nullptr;
    }
    llvm_unreachable("Unknown discriminant!");
  }

  bool operator==(const MemDepResult &M) const { return Value == M.Value; }
  bool operator!=(const MemDepResult &M) const { return Value != M.Value; }
  bool operator<(const MemDepResult &M) const { return Value < M.Value; }
  bool operator>(const MemDepResult &M) const { return Value > M.Value; }

private:
  friend class MemoryDependenceResults;

  /// Tests if this is a MemDepResult in its dirty/invalid. state.
  bool isDirty() const { return Value.is<Invalid>(); }

  static MemDepResult getDirty(Instruction *Inst) {
    return MemDepResult(ValueTy::create<Invalid>(Inst));
  }
};

/// This is an entry in the NonLocalDepInfo cache.
///
/// For each BasicBlock (the BB entry) it keeps a MemDepResult.
class NonLocalDepEntry {
  BasicBlock *BB;
  MemDepResult Result;

public:
  NonLocalDepEntry(BasicBlock *bb, MemDepResult result)
      : BB(bb), Result(result) {}

  // This is used for searches.
  NonLocalDepEntry(BasicBlock *bb) : BB(bb) {}

  // BB is the sort key, it can't be changed.
  BasicBlock *getBB() const { return BB; }

  void setResult(const MemDepResult &R) { Result = R; }

  const MemDepResult &getResult() const { return Result; }

  bool operator<(const NonLocalDepEntry &RHS) const { return BB < RHS.BB; }
};

/// This is a result from a NonLocal dependence query.
///
/// For each BasicBlock (the BB entry) it keeps a MemDepResult and the
/// (potentially phi translated) address that was live in the block.
class NonLocalDepResult {
  NonLocalDepEntry Entry;
  Value *Address;

public:
  NonLocalDepResult(BasicBlock *bb, MemDepResult result, Value *address)
      : Entry(bb, result), Address(address) {}

  // BB is the sort key, it can't be changed.
  BasicBlock *getBB() const { return Entry.getBB(); }

  void setResult(const MemDepResult &R, Value *Addr) {
    Entry.setResult(R);
    Address = Addr;
  }

  const MemDepResult &getResult() const { return Entry.getResult(); }

  /// Returns the address of this pointer in this block.
  ///
  /// This can be different than the address queried for the non-local result
  /// because of phi translation.  This returns null if the address was not
  /// available in a block (i.e. because phi translation failed) or if this is
  /// a cached result and that address was deleted.
  ///
  /// The address is always null for a non-local 'call' dependence.
  Value *getAddress() const { return Address; }
};

/// Provides a lazy, caching interface for making common memory aliasing
/// information queries, backed by LLVM's alias analysis passes.
///
/// The dependency information returned is somewhat unusual, but is pragmatic.
/// If queried about a store or call that might modify memory, the analysis
/// will return the instruction[s] that may either load from that memory or
/// store to it.  If queried with a load or call that can never modify memory,
/// the analysis will return calls and stores that might modify the pointer,
/// but generally does not return loads unless a) they are volatile, or
/// b) they load from *must-aliased* pointers.  Returning a dependence on
/// must-alias'd pointers instead of all pointers interacts well with the
/// internal caching mechanism.
class MemoryDependenceResults {
  // A map from instructions to their dependency.
  using LocalDepMapType = DenseMap<Instruction *, MemDepResult>;
  LocalDepMapType LocalDeps;

public:
  using NonLocalDepInfo = std::vector<NonLocalDepEntry>;

private:
  /// A pair<Value*, bool> where the bool is true if the dependence is a read
  /// only dependence, false if read/write.
  using ValueIsLoadPair = PointerIntPair<const Value *, 1, bool>;

  /// This pair is used when caching information for a block.
  ///
  /// If the pointer is null, the cache value is not a full query that starts
  /// at the specified block.  If non-null, the bool indicates whether or not
  /// the contents of the block was skipped.
  using BBSkipFirstBlockPair = PointerIntPair<BasicBlock *, 1, bool>;

  /// This record is the information kept for each (value, is load) pair.
  struct NonLocalPointerInfo {
    /// The pair of the block and the skip-first-block flag.
    BBSkipFirstBlockPair Pair;
    /// The results of the query for each relevant block.
    NonLocalDepInfo NonLocalDeps;
    /// The maximum size of the dereferences of the pointer.
    ///
    /// May be UnknownSize if the sizes are unknown.
    LocationSize Size = LocationSize::unknown();
    /// The AA tags associated with dereferences of the pointer.
    ///
    /// The members may be null if there are no tags or conflicting tags.
    AAMDNodes AATags;

    NonLocalPointerInfo() = default;
  };

  /// Cache storing single nonlocal def for the instruction.
  /// It is set when nonlocal def would be found in function returning only
  /// local dependencies.
  DenseMap<AssertingVH<const Value>, NonLocalDepResult> NonLocalDefsCache;
  using ReverseNonLocalDefsCacheTy =
    DenseMap<Instruction *, SmallPtrSet<const Value*, 4>>;
  ReverseNonLocalDefsCacheTy ReverseNonLocalDefsCache;

  /// This map stores the cached results of doing a pointer lookup at the
  /// bottom of a block.
  ///
  /// The key of this map is the pointer+isload bit, the value is a list of
  /// <bb->result> mappings.
  using CachedNonLocalPointerInfo =
      DenseMap<ValueIsLoadPair, NonLocalPointerInfo>;
  CachedNonLocalPointerInfo NonLocalPointerDeps;

  // A map from instructions to their non-local pointer dependencies.
  using ReverseNonLocalPtrDepTy =
      DenseMap<Instruction *, SmallPtrSet<ValueIsLoadPair, 4>>;
  ReverseNonLocalPtrDepTy ReverseNonLocalPtrDeps;

  /// This is the instruction we keep for each cached access that we have for
  /// an instruction.
  ///
  /// The pointer is an owning pointer and the bool indicates whether we have
  /// any dirty bits in the set.
  using PerInstNLInfo = std::pair<NonLocalDepInfo, bool>;

  // A map from instructions to their non-local dependencies.
  using NonLocalDepMapType = DenseMap<Instruction *, PerInstNLInfo>;

  NonLocalDepMapType NonLocalDeps;

  // A reverse mapping from dependencies to the dependees.  This is
  // used when removing instructions to keep the cache coherent.
  using ReverseDepMapType =
      DenseMap<Instruction *, SmallPtrSet<Instruction *, 4>>;
  ReverseDepMapType ReverseLocalDeps;

  // A reverse mapping from dependencies to the non-local dependees.
  ReverseDepMapType ReverseNonLocalDeps;

  /// Current AA implementation, just a cache.
  AliasAnalysis &AA;
  AssumptionCache &AC;
  const TargetLibraryInfo &TLI;
  DominatorTree &DT;
  PhiValues &PV;
  PredIteratorCache PredCache;

public:
  MemoryDependenceResults(AliasAnalysis &AA, AssumptionCache &AC,
                          const TargetLibraryInfo &TLI,
                          DominatorTree &DT, PhiValues &PV)
      : AA(AA), AC(AC), TLI(TLI), DT(DT), PV(PV) {}

  /// Handle invalidation in the new PM.
  bool invalidate(Function &F, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &Inv);

  /// Some methods limit the number of instructions they will examine.
  /// The return value of this method is the default limit that will be
  /// used if no limit is explicitly passed in.
  unsigned getDefaultBlockScanLimit() const;

  /// Returns the instruction on which a memory operation depends.
  ///
  /// See the class comment for more details. It is illegal to call this on
  /// non-memory instructions.
  MemDepResult getDependency(Instruction *QueryInst);

  /// Perform a full dependency query for the specified call, returning the set
  /// of blocks that the value is potentially live across.
  ///
  /// The returned set of results will include a "NonLocal" result for all
  /// blocks where the value is live across.
  ///
  /// This method assumes the instruction returns a "NonLocal" dependency
  /// within its own block.
  ///
  /// This returns a reference to an internal data structure that may be
  /// invalidated on the next non-local query or when an instruction is
  /// removed.  Clients must copy this data if they want it around longer than
  /// that.
  const NonLocalDepInfo &getNonLocalCallDependency(CallBase *QueryCall);

  /// Perform a full dependency query for an access to the QueryInst's
  /// specified memory location, returning the set of instructions that either
  /// define or clobber the value.
  ///
  /// Warning: For a volatile query instruction, the dependencies will be
  /// accurate, and thus usable for reordering, but it is never legal to
  /// remove the query instruction.
  ///
  /// This method assumes the pointer has a "NonLocal" dependency within
  /// QueryInst's parent basic block.
  void getNonLocalPointerDependency(Instruction *QueryInst,
                                    SmallVectorImpl<NonLocalDepResult> &Result);

  /// Removes an instruction from the dependence analysis, updating the
  /// dependence of instructions that previously depended on it.
  void removeInstruction(Instruction *InstToRemove);

  /// Invalidates cached information about the specified pointer, because it
  /// may be too conservative in memdep.
  ///
  /// This is an optional call that can be used when the client detects an
  /// equivalence between the pointer and some other value and replaces the
  /// other value with ptr. This can make Ptr available in more places that
  /// cached info does not necessarily keep.
  void invalidateCachedPointerInfo(Value *Ptr);

  /// Clears the PredIteratorCache info.
  ///
  /// This needs to be done when the CFG changes, e.g., due to splitting
  /// critical edges.
  void invalidateCachedPredecessors();

  /// Returns the instruction on which a memory location depends.
  ///
  /// If isLoad is true, this routine ignores may-aliases with read-only
  /// operations.  If isLoad is false, this routine ignores may-aliases
  /// with reads from read-only locations. If possible, pass the query
  /// instruction as well; this function may take advantage of the metadata
  /// annotated to the query instruction to refine the result. \p Limit
  /// can be used to set the maximum number of instructions that will be
  /// examined to find the pointer dependency. On return, it will be set to
  /// the number of instructions left to examine. If a null pointer is passed
  /// in, the limit will default to the value of -memdep-block-scan-limit.
  ///
  /// Note that this is an uncached query, and thus may be inefficient.
  MemDepResult getPointerDependencyFrom(const MemoryLocation &Loc, bool isLoad,
                                        BasicBlock::iterator ScanIt,
                                        BasicBlock *BB,
                                        Instruction *QueryInst = nullptr,
                                        unsigned *Limit = nullptr);

  MemDepResult getSimplePointerDependencyFrom(const MemoryLocation &MemLoc,
                                              bool isLoad,
                                              BasicBlock::iterator ScanIt,
                                              BasicBlock *BB,
                                              Instruction *QueryInst,
                                              unsigned *Limit = nullptr);

  /// This analysis looks for other loads and stores with invariant.group
  /// metadata and the same pointer operand. Returns Unknown if it does not
  /// find anything, and Def if it can be assumed that 2 instructions load or
  /// store the same value and NonLocal which indicate that non-local Def was
  /// found, which can be retrieved by calling getNonLocalPointerDependency
  /// with the same queried instruction.
  MemDepResult getInvariantGroupPointerDependency(LoadInst *LI, BasicBlock *BB);

  /// Looks at a memory location for a load (specified by MemLocBase, Offs, and
  /// Size) and compares it against a load.
  ///
  /// If the specified load could be safely widened to a larger integer load
  /// that is 1) still efficient, 2) safe for the target, and 3) would provide
  /// the specified memory location value, then this function returns the size
  /// in bytes of the load width to use.  If not, this returns zero.
  static unsigned getLoadLoadClobberFullWidthSize(const Value *MemLocBase,
                                                  int64_t MemLocOffs,
                                                  unsigned MemLocSize,
                                                  const LoadInst *LI);

  /// Release memory in caches.
  void releaseMemory();

private:
  MemDepResult getCallDependencyFrom(CallBase *Call, bool isReadOnlyCall,
                                     BasicBlock::iterator ScanIt,
                                     BasicBlock *BB);
  bool getNonLocalPointerDepFromBB(Instruction *QueryInst,
                                   const PHITransAddr &Pointer,
                                   const MemoryLocation &Loc, bool isLoad,
                                   BasicBlock *BB,
                                   SmallVectorImpl<NonLocalDepResult> &Result,
                                   DenseMap<BasicBlock *, Value *> &Visited,
                                   bool SkipFirstBlock = false);
  MemDepResult GetNonLocalInfoForBlock(Instruction *QueryInst,
                                       const MemoryLocation &Loc, bool isLoad,
                                       BasicBlock *BB, NonLocalDepInfo *Cache,
                                       unsigned NumSortedEntries);

  void RemoveCachedNonLocalPointerDependencies(ValueIsLoadPair P);

  void verifyRemoved(Instruction *Inst) const;
};

/// An analysis that produces \c MemoryDependenceResults for a function.
///
/// This is essentially a no-op because the results are computed entirely
/// lazily.
class MemoryDependenceAnalysis
    : public AnalysisInfoMixin<MemoryDependenceAnalysis> {
  friend AnalysisInfoMixin<MemoryDependenceAnalysis>;

  static AnalysisKey Key;

public:
  using Result = MemoryDependenceResults;

  MemoryDependenceResults run(Function &F, FunctionAnalysisManager &AM);
};

/// A wrapper analysis pass for the legacy pass manager that exposes a \c
/// MemoryDepnedenceResults instance.
class MemoryDependenceWrapperPass : public FunctionPass {
  Optional<MemoryDependenceResults> MemDep;

public:
  static char ID;

  MemoryDependenceWrapperPass();
  ~MemoryDependenceWrapperPass() override;

  /// Pass Implementation stuff.  This doesn't do any analysis eagerly.
  bool runOnFunction(Function &) override;

  /// Clean up memory in between runs
  void releaseMemory() override;

  /// Does not modify anything.  It uses Value Numbering and Alias Analysis.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  MemoryDependenceResults &getMemDep() { return *MemDep; }
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_MEMORYDEPENDENCEANALYSIS_H
