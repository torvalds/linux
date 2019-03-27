//===- SROA.cpp - Scalar Replacement Of Aggregates ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This transformation implements the well known scalar replacement of
/// aggregates transformation. It tries to identify promotable elements of an
/// aggregate alloca, and promote them to registers. It will also try to
/// convert uses of an element (or set of elements) of an alloca into a vector
/// or bitfield-style integer scalar if appropriate.
///
/// It works to do this with minimal slicing of the alloca so that regions
/// which are merely transferred in and out of external memory remain unchanged
/// and are not decomposed to scalar code.
///
/// Because this also performs alloca promotion, it can be thought of as also
/// serving the purpose of SSA formation. The algorithm iterates on the
/// function until all opportunities for promotion have been realized.
///
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/SROA.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/PtrUseVisitor.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantFolder.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#ifndef NDEBUG
// We only use this for a debug check.
#include <random>
#endif

using namespace llvm;
using namespace llvm::sroa;

#define DEBUG_TYPE "sroa"

STATISTIC(NumAllocasAnalyzed, "Number of allocas analyzed for replacement");
STATISTIC(NumAllocaPartitions, "Number of alloca partitions formed");
STATISTIC(MaxPartitionsPerAlloca, "Maximum number of partitions per alloca");
STATISTIC(NumAllocaPartitionUses, "Number of alloca partition uses rewritten");
STATISTIC(MaxUsesPerAllocaPartition, "Maximum number of uses of a partition");
STATISTIC(NumNewAllocas, "Number of new, smaller allocas introduced");
STATISTIC(NumPromoted, "Number of allocas promoted to SSA values");
STATISTIC(NumLoadsSpeculated, "Number of loads speculated to allow promotion");
STATISTIC(NumDeleted, "Number of instructions deleted");
STATISTIC(NumVectorized, "Number of vectorized aggregates");

/// Hidden option to enable randomly shuffling the slices to help uncover
/// instability in their order.
static cl::opt<bool> SROARandomShuffleSlices("sroa-random-shuffle-slices",
                                             cl::init(false), cl::Hidden);

/// Hidden option to experiment with completely strict handling of inbounds
/// GEPs.
static cl::opt<bool> SROAStrictInbounds("sroa-strict-inbounds", cl::init(false),
                                        cl::Hidden);

namespace {

/// A custom IRBuilder inserter which prefixes all names, but only in
/// Assert builds.
class IRBuilderPrefixedInserter : public IRBuilderDefaultInserter {
  std::string Prefix;

  const Twine getNameWithPrefix(const Twine &Name) const {
    return Name.isTriviallyEmpty() ? Name : Prefix + Name;
  }

public:
  void SetNamePrefix(const Twine &P) { Prefix = P.str(); }

protected:
  void InsertHelper(Instruction *I, const Twine &Name, BasicBlock *BB,
                    BasicBlock::iterator InsertPt) const {
    IRBuilderDefaultInserter::InsertHelper(I, getNameWithPrefix(Name), BB,
                                           InsertPt);
  }
};

/// Provide a type for IRBuilder that drops names in release builds.
using IRBuilderTy = IRBuilder<ConstantFolder, IRBuilderPrefixedInserter>;

/// A used slice of an alloca.
///
/// This structure represents a slice of an alloca used by some instruction. It
/// stores both the begin and end offsets of this use, a pointer to the use
/// itself, and a flag indicating whether we can classify the use as splittable
/// or not when forming partitions of the alloca.
class Slice {
  /// The beginning offset of the range.
  uint64_t BeginOffset = 0;

  /// The ending offset, not included in the range.
  uint64_t EndOffset = 0;

  /// Storage for both the use of this slice and whether it can be
  /// split.
  PointerIntPair<Use *, 1, bool> UseAndIsSplittable;

public:
  Slice() = default;

  Slice(uint64_t BeginOffset, uint64_t EndOffset, Use *U, bool IsSplittable)
      : BeginOffset(BeginOffset), EndOffset(EndOffset),
        UseAndIsSplittable(U, IsSplittable) {}

  uint64_t beginOffset() const { return BeginOffset; }
  uint64_t endOffset() const { return EndOffset; }

  bool isSplittable() const { return UseAndIsSplittable.getInt(); }
  void makeUnsplittable() { UseAndIsSplittable.setInt(false); }

  Use *getUse() const { return UseAndIsSplittable.getPointer(); }

  bool isDead() const { return getUse() == nullptr; }
  void kill() { UseAndIsSplittable.setPointer(nullptr); }

  /// Support for ordering ranges.
  ///
  /// This provides an ordering over ranges such that start offsets are
  /// always increasing, and within equal start offsets, the end offsets are
  /// decreasing. Thus the spanning range comes first in a cluster with the
  /// same start position.
  bool operator<(const Slice &RHS) const {
    if (beginOffset() < RHS.beginOffset())
      return true;
    if (beginOffset() > RHS.beginOffset())
      return false;
    if (isSplittable() != RHS.isSplittable())
      return !isSplittable();
    if (endOffset() > RHS.endOffset())
      return true;
    return false;
  }

  /// Support comparison with a single offset to allow binary searches.
  friend LLVM_ATTRIBUTE_UNUSED bool operator<(const Slice &LHS,
                                              uint64_t RHSOffset) {
    return LHS.beginOffset() < RHSOffset;
  }
  friend LLVM_ATTRIBUTE_UNUSED bool operator<(uint64_t LHSOffset,
                                              const Slice &RHS) {
    return LHSOffset < RHS.beginOffset();
  }

  bool operator==(const Slice &RHS) const {
    return isSplittable() == RHS.isSplittable() &&
           beginOffset() == RHS.beginOffset() && endOffset() == RHS.endOffset();
  }
  bool operator!=(const Slice &RHS) const { return !operator==(RHS); }
};

} // end anonymous namespace

namespace llvm {

template <typename T> struct isPodLike;
template <> struct isPodLike<Slice> { static const bool value = true; };

} // end namespace llvm

/// Representation of the alloca slices.
///
/// This class represents the slices of an alloca which are formed by its
/// various uses. If a pointer escapes, we can't fully build a representation
/// for the slices used and we reflect that in this structure. The uses are
/// stored, sorted by increasing beginning offset and with unsplittable slices
/// starting at a particular offset before splittable slices.
class llvm::sroa::AllocaSlices {
public:
  /// Construct the slices of a particular alloca.
  AllocaSlices(const DataLayout &DL, AllocaInst &AI);

  /// Test whether a pointer to the allocation escapes our analysis.
  ///
  /// If this is true, the slices are never fully built and should be
  /// ignored.
  bool isEscaped() const { return PointerEscapingInstr; }

  /// Support for iterating over the slices.
  /// @{
  using iterator = SmallVectorImpl<Slice>::iterator;
  using range = iterator_range<iterator>;

  iterator begin() { return Slices.begin(); }
  iterator end() { return Slices.end(); }

  using const_iterator = SmallVectorImpl<Slice>::const_iterator;
  using const_range = iterator_range<const_iterator>;

  const_iterator begin() const { return Slices.begin(); }
  const_iterator end() const { return Slices.end(); }
  /// @}

  /// Erase a range of slices.
  void erase(iterator Start, iterator Stop) { Slices.erase(Start, Stop); }

  /// Insert new slices for this alloca.
  ///
  /// This moves the slices into the alloca's slices collection, and re-sorts
  /// everything so that the usual ordering properties of the alloca's slices
  /// hold.
  void insert(ArrayRef<Slice> NewSlices) {
    int OldSize = Slices.size();
    Slices.append(NewSlices.begin(), NewSlices.end());
    auto SliceI = Slices.begin() + OldSize;
    llvm::sort(SliceI, Slices.end());
    std::inplace_merge(Slices.begin(), SliceI, Slices.end());
  }

  // Forward declare the iterator and range accessor for walking the
  // partitions.
  class partition_iterator;
  iterator_range<partition_iterator> partitions();

  /// Access the dead users for this alloca.
  ArrayRef<Instruction *> getDeadUsers() const { return DeadUsers; }

  /// Access the dead operands referring to this alloca.
  ///
  /// These are operands which have cannot actually be used to refer to the
  /// alloca as they are outside its range and the user doesn't correct for
  /// that. These mostly consist of PHI node inputs and the like which we just
  /// need to replace with undef.
  ArrayRef<Use *> getDeadOperands() const { return DeadOperands; }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void print(raw_ostream &OS, const_iterator I, StringRef Indent = "  ") const;
  void printSlice(raw_ostream &OS, const_iterator I,
                  StringRef Indent = "  ") const;
  void printUse(raw_ostream &OS, const_iterator I,
                StringRef Indent = "  ") const;
  void print(raw_ostream &OS) const;
  void dump(const_iterator I) const;
  void dump() const;
#endif

private:
  template <typename DerivedT, typename RetT = void> class BuilderBase;
  class SliceBuilder;

  friend class AllocaSlices::SliceBuilder;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Handle to alloca instruction to simplify method interfaces.
  AllocaInst &AI;
#endif

  /// The instruction responsible for this alloca not having a known set
  /// of slices.
  ///
  /// When an instruction (potentially) escapes the pointer to the alloca, we
  /// store a pointer to that here and abort trying to form slices of the
  /// alloca. This will be null if the alloca slices are analyzed successfully.
  Instruction *PointerEscapingInstr;

  /// The slices of the alloca.
  ///
  /// We store a vector of the slices formed by uses of the alloca here. This
  /// vector is sorted by increasing begin offset, and then the unsplittable
  /// slices before the splittable ones. See the Slice inner class for more
  /// details.
  SmallVector<Slice, 8> Slices;

  /// Instructions which will become dead if we rewrite the alloca.
  ///
  /// Note that these are not separated by slice. This is because we expect an
  /// alloca to be completely rewritten or not rewritten at all. If rewritten,
  /// all these instructions can simply be removed and replaced with undef as
  /// they come from outside of the allocated space.
  SmallVector<Instruction *, 8> DeadUsers;

  /// Operands which will become dead if we rewrite the alloca.
  ///
  /// These are operands that in their particular use can be replaced with
  /// undef when we rewrite the alloca. These show up in out-of-bounds inputs
  /// to PHI nodes and the like. They aren't entirely dead (there might be
  /// a GEP back into the bounds using it elsewhere) and nor is the PHI, but we
  /// want to swap this particular input for undef to simplify the use lists of
  /// the alloca.
  SmallVector<Use *, 8> DeadOperands;
};

/// A partition of the slices.
///
/// An ephemeral representation for a range of slices which can be viewed as
/// a partition of the alloca. This range represents a span of the alloca's
/// memory which cannot be split, and provides access to all of the slices
/// overlapping some part of the partition.
///
/// Objects of this type are produced by traversing the alloca's slices, but
/// are only ephemeral and not persistent.
class llvm::sroa::Partition {
private:
  friend class AllocaSlices;
  friend class AllocaSlices::partition_iterator;

  using iterator = AllocaSlices::iterator;

  /// The beginning and ending offsets of the alloca for this
  /// partition.
  uint64_t BeginOffset, EndOffset;

  /// The start and end iterators of this partition.
  iterator SI, SJ;

  /// A collection of split slice tails overlapping the partition.
  SmallVector<Slice *, 4> SplitTails;

  /// Raw constructor builds an empty partition starting and ending at
  /// the given iterator.
  Partition(iterator SI) : SI(SI), SJ(SI) {}

public:
  /// The start offset of this partition.
  ///
  /// All of the contained slices start at or after this offset.
  uint64_t beginOffset() const { return BeginOffset; }

  /// The end offset of this partition.
  ///
  /// All of the contained slices end at or before this offset.
  uint64_t endOffset() const { return EndOffset; }

  /// The size of the partition.
  ///
  /// Note that this can never be zero.
  uint64_t size() const {
    assert(BeginOffset < EndOffset && "Partitions must span some bytes!");
    return EndOffset - BeginOffset;
  }

  /// Test whether this partition contains no slices, and merely spans
  /// a region occupied by split slices.
  bool empty() const { return SI == SJ; }

  /// \name Iterate slices that start within the partition.
  /// These may be splittable or unsplittable. They have a begin offset >= the
  /// partition begin offset.
  /// @{
  // FIXME: We should probably define a "concat_iterator" helper and use that
  // to stitch together pointee_iterators over the split tails and the
  // contiguous iterators of the partition. That would give a much nicer
  // interface here. We could then additionally expose filtered iterators for
  // split, unsplit, and unsplittable splices based on the usage patterns.
  iterator begin() const { return SI; }
  iterator end() const { return SJ; }
  /// @}

  /// Get the sequence of split slice tails.
  ///
  /// These tails are of slices which start before this partition but are
  /// split and overlap into the partition. We accumulate these while forming
  /// partitions.
  ArrayRef<Slice *> splitSliceTails() const { return SplitTails; }
};

/// An iterator over partitions of the alloca's slices.
///
/// This iterator implements the core algorithm for partitioning the alloca's
/// slices. It is a forward iterator as we don't support backtracking for
/// efficiency reasons, and re-use a single storage area to maintain the
/// current set of split slices.
///
/// It is templated on the slice iterator type to use so that it can operate
/// with either const or non-const slice iterators.
class AllocaSlices::partition_iterator
    : public iterator_facade_base<partition_iterator, std::forward_iterator_tag,
                                  Partition> {
  friend class AllocaSlices;

  /// Most of the state for walking the partitions is held in a class
  /// with a nice interface for examining them.
  Partition P;

  /// We need to keep the end of the slices to know when to stop.
  AllocaSlices::iterator SE;

  /// We also need to keep track of the maximum split end offset seen.
  /// FIXME: Do we really?
  uint64_t MaxSplitSliceEndOffset = 0;

  /// Sets the partition to be empty at given iterator, and sets the
  /// end iterator.
  partition_iterator(AllocaSlices::iterator SI, AllocaSlices::iterator SE)
      : P(SI), SE(SE) {
    // If not already at the end, advance our state to form the initial
    // partition.
    if (SI != SE)
      advance();
  }

  /// Advance the iterator to the next partition.
  ///
  /// Requires that the iterator not be at the end of the slices.
  void advance() {
    assert((P.SI != SE || !P.SplitTails.empty()) &&
           "Cannot advance past the end of the slices!");

    // Clear out any split uses which have ended.
    if (!P.SplitTails.empty()) {
      if (P.EndOffset >= MaxSplitSliceEndOffset) {
        // If we've finished all splits, this is easy.
        P.SplitTails.clear();
        MaxSplitSliceEndOffset = 0;
      } else {
        // Remove the uses which have ended in the prior partition. This
        // cannot change the max split slice end because we just checked that
        // the prior partition ended prior to that max.
        P.SplitTails.erase(llvm::remove_if(P.SplitTails,
                                           [&](Slice *S) {
                                             return S->endOffset() <=
                                                    P.EndOffset;
                                           }),
                           P.SplitTails.end());
        assert(llvm::any_of(P.SplitTails,
                            [&](Slice *S) {
                              return S->endOffset() == MaxSplitSliceEndOffset;
                            }) &&
               "Could not find the current max split slice offset!");
        assert(llvm::all_of(P.SplitTails,
                            [&](Slice *S) {
                              return S->endOffset() <= MaxSplitSliceEndOffset;
                            }) &&
               "Max split slice end offset is not actually the max!");
      }
    }

    // If P.SI is already at the end, then we've cleared the split tail and
    // now have an end iterator.
    if (P.SI == SE) {
      assert(P.SplitTails.empty() && "Failed to clear the split slices!");
      return;
    }

    // If we had a non-empty partition previously, set up the state for
    // subsequent partitions.
    if (P.SI != P.SJ) {
      // Accumulate all the splittable slices which started in the old
      // partition into the split list.
      for (Slice &S : P)
        if (S.isSplittable() && S.endOffset() > P.EndOffset) {
          P.SplitTails.push_back(&S);
          MaxSplitSliceEndOffset =
              std::max(S.endOffset(), MaxSplitSliceEndOffset);
        }

      // Start from the end of the previous partition.
      P.SI = P.SJ;

      // If P.SI is now at the end, we at most have a tail of split slices.
      if (P.SI == SE) {
        P.BeginOffset = P.EndOffset;
        P.EndOffset = MaxSplitSliceEndOffset;
        return;
      }

      // If the we have split slices and the next slice is after a gap and is
      // not splittable immediately form an empty partition for the split
      // slices up until the next slice begins.
      if (!P.SplitTails.empty() && P.SI->beginOffset() != P.EndOffset &&
          !P.SI->isSplittable()) {
        P.BeginOffset = P.EndOffset;
        P.EndOffset = P.SI->beginOffset();
        return;
      }
    }

    // OK, we need to consume new slices. Set the end offset based on the
    // current slice, and step SJ past it. The beginning offset of the
    // partition is the beginning offset of the next slice unless we have
    // pre-existing split slices that are continuing, in which case we begin
    // at the prior end offset.
    P.BeginOffset = P.SplitTails.empty() ? P.SI->beginOffset() : P.EndOffset;
    P.EndOffset = P.SI->endOffset();
    ++P.SJ;

    // There are two strategies to form a partition based on whether the
    // partition starts with an unsplittable slice or a splittable slice.
    if (!P.SI->isSplittable()) {
      // When we're forming an unsplittable region, it must always start at
      // the first slice and will extend through its end.
      assert(P.BeginOffset == P.SI->beginOffset());

      // Form a partition including all of the overlapping slices with this
      // unsplittable slice.
      while (P.SJ != SE && P.SJ->beginOffset() < P.EndOffset) {
        if (!P.SJ->isSplittable())
          P.EndOffset = std::max(P.EndOffset, P.SJ->endOffset());
        ++P.SJ;
      }

      // We have a partition across a set of overlapping unsplittable
      // partitions.
      return;
    }

    // If we're starting with a splittable slice, then we need to form
    // a synthetic partition spanning it and any other overlapping splittable
    // splices.
    assert(P.SI->isSplittable() && "Forming a splittable partition!");

    // Collect all of the overlapping splittable slices.
    while (P.SJ != SE && P.SJ->beginOffset() < P.EndOffset &&
           P.SJ->isSplittable()) {
      P.EndOffset = std::max(P.EndOffset, P.SJ->endOffset());
      ++P.SJ;
    }

    // Back upiP.EndOffset if we ended the span early when encountering an
    // unsplittable slice. This synthesizes the early end offset of
    // a partition spanning only splittable slices.
    if (P.SJ != SE && P.SJ->beginOffset() < P.EndOffset) {
      assert(!P.SJ->isSplittable());
      P.EndOffset = P.SJ->beginOffset();
    }
  }

public:
  bool operator==(const partition_iterator &RHS) const {
    assert(SE == RHS.SE &&
           "End iterators don't match between compared partition iterators!");

    // The observed positions of partitions is marked by the P.SI iterator and
    // the emptiness of the split slices. The latter is only relevant when
    // P.SI == SE, as the end iterator will additionally have an empty split
    // slices list, but the prior may have the same P.SI and a tail of split
    // slices.
    if (P.SI == RHS.P.SI && P.SplitTails.empty() == RHS.P.SplitTails.empty()) {
      assert(P.SJ == RHS.P.SJ &&
             "Same set of slices formed two different sized partitions!");
      assert(P.SplitTails.size() == RHS.P.SplitTails.size() &&
             "Same slice position with differently sized non-empty split "
             "slice tails!");
      return true;
    }
    return false;
  }

  partition_iterator &operator++() {
    advance();
    return *this;
  }

  Partition &operator*() { return P; }
};

/// A forward range over the partitions of the alloca's slices.
///
/// This accesses an iterator range over the partitions of the alloca's
/// slices. It computes these partitions on the fly based on the overlapping
/// offsets of the slices and the ability to split them. It will visit "empty"
/// partitions to cover regions of the alloca only accessed via split
/// slices.
iterator_range<AllocaSlices::partition_iterator> AllocaSlices::partitions() {
  return make_range(partition_iterator(begin(), end()),
                    partition_iterator(end(), end()));
}

static Value *foldSelectInst(SelectInst &SI) {
  // If the condition being selected on is a constant or the same value is
  // being selected between, fold the select. Yes this does (rarely) happen
  // early on.
  if (ConstantInt *CI = dyn_cast<ConstantInt>(SI.getCondition()))
    return SI.getOperand(1 + CI->isZero());
  if (SI.getOperand(1) == SI.getOperand(2))
    return SI.getOperand(1);

  return nullptr;
}

/// A helper that folds a PHI node or a select.
static Value *foldPHINodeOrSelectInst(Instruction &I) {
  if (PHINode *PN = dyn_cast<PHINode>(&I)) {
    // If PN merges together the same value, return that value.
    return PN->hasConstantValue();
  }
  return foldSelectInst(cast<SelectInst>(I));
}

/// Builder for the alloca slices.
///
/// This class builds a set of alloca slices by recursively visiting the uses
/// of an alloca and making a slice for each load and store at each offset.
class AllocaSlices::SliceBuilder : public PtrUseVisitor<SliceBuilder> {
  friend class PtrUseVisitor<SliceBuilder>;
  friend class InstVisitor<SliceBuilder>;

  using Base = PtrUseVisitor<SliceBuilder>;

  const uint64_t AllocSize;
  AllocaSlices &AS;

  SmallDenseMap<Instruction *, unsigned> MemTransferSliceMap;
  SmallDenseMap<Instruction *, uint64_t> PHIOrSelectSizes;

  /// Set to de-duplicate dead instructions found in the use walk.
  SmallPtrSet<Instruction *, 4> VisitedDeadInsts;

public:
  SliceBuilder(const DataLayout &DL, AllocaInst &AI, AllocaSlices &AS)
      : PtrUseVisitor<SliceBuilder>(DL),
        AllocSize(DL.getTypeAllocSize(AI.getAllocatedType())), AS(AS) {}

private:
  void markAsDead(Instruction &I) {
    if (VisitedDeadInsts.insert(&I).second)
      AS.DeadUsers.push_back(&I);
  }

  void insertUse(Instruction &I, const APInt &Offset, uint64_t Size,
                 bool IsSplittable = false) {
    // Completely skip uses which have a zero size or start either before or
    // past the end of the allocation.
    if (Size == 0 || Offset.uge(AllocSize)) {
      LLVM_DEBUG(dbgs() << "WARNING: Ignoring " << Size << " byte use @"
                        << Offset
                        << " which has zero size or starts outside of the "
                        << AllocSize << " byte alloca:\n"
                        << "    alloca: " << AS.AI << "\n"
                        << "       use: " << I << "\n");
      return markAsDead(I);
    }

    uint64_t BeginOffset = Offset.getZExtValue();
    uint64_t EndOffset = BeginOffset + Size;

    // Clamp the end offset to the end of the allocation. Note that this is
    // formulated to handle even the case where "BeginOffset + Size" overflows.
    // This may appear superficially to be something we could ignore entirely,
    // but that is not so! There may be widened loads or PHI-node uses where
    // some instructions are dead but not others. We can't completely ignore
    // them, and so have to record at least the information here.
    assert(AllocSize >= BeginOffset); // Established above.
    if (Size > AllocSize - BeginOffset) {
      LLVM_DEBUG(dbgs() << "WARNING: Clamping a " << Size << " byte use @"
                        << Offset << " to remain within the " << AllocSize
                        << " byte alloca:\n"
                        << "    alloca: " << AS.AI << "\n"
                        << "       use: " << I << "\n");
      EndOffset = AllocSize;
    }

    AS.Slices.push_back(Slice(BeginOffset, EndOffset, U, IsSplittable));
  }

  void visitBitCastInst(BitCastInst &BC) {
    if (BC.use_empty())
      return markAsDead(BC);

    return Base::visitBitCastInst(BC);
  }

  void visitGetElementPtrInst(GetElementPtrInst &GEPI) {
    if (GEPI.use_empty())
      return markAsDead(GEPI);

    if (SROAStrictInbounds && GEPI.isInBounds()) {
      // FIXME: This is a manually un-factored variant of the basic code inside
      // of GEPs with checking of the inbounds invariant specified in the
      // langref in a very strict sense. If we ever want to enable
      // SROAStrictInbounds, this code should be factored cleanly into
      // PtrUseVisitor, but it is easier to experiment with SROAStrictInbounds
      // by writing out the code here where we have the underlying allocation
      // size readily available.
      APInt GEPOffset = Offset;
      const DataLayout &DL = GEPI.getModule()->getDataLayout();
      for (gep_type_iterator GTI = gep_type_begin(GEPI),
                             GTE = gep_type_end(GEPI);
           GTI != GTE; ++GTI) {
        ConstantInt *OpC = dyn_cast<ConstantInt>(GTI.getOperand());
        if (!OpC)
          break;

        // Handle a struct index, which adds its field offset to the pointer.
        if (StructType *STy = GTI.getStructTypeOrNull()) {
          unsigned ElementIdx = OpC->getZExtValue();
          const StructLayout *SL = DL.getStructLayout(STy);
          GEPOffset +=
              APInt(Offset.getBitWidth(), SL->getElementOffset(ElementIdx));
        } else {
          // For array or vector indices, scale the index by the size of the
          // type.
          APInt Index = OpC->getValue().sextOrTrunc(Offset.getBitWidth());
          GEPOffset += Index * APInt(Offset.getBitWidth(),
                                     DL.getTypeAllocSize(GTI.getIndexedType()));
        }

        // If this index has computed an intermediate pointer which is not
        // inbounds, then the result of the GEP is a poison value and we can
        // delete it and all uses.
        if (GEPOffset.ugt(AllocSize))
          return markAsDead(GEPI);
      }
    }

    return Base::visitGetElementPtrInst(GEPI);
  }

  void handleLoadOrStore(Type *Ty, Instruction &I, const APInt &Offset,
                         uint64_t Size, bool IsVolatile) {
    // We allow splitting of non-volatile loads and stores where the type is an
    // integer type. These may be used to implement 'memcpy' or other "transfer
    // of bits" patterns.
    bool IsSplittable = Ty->isIntegerTy() && !IsVolatile;

    insertUse(I, Offset, Size, IsSplittable);
  }

  void visitLoadInst(LoadInst &LI) {
    assert((!LI.isSimple() || LI.getType()->isSingleValueType()) &&
           "All simple FCA loads should have been pre-split");

    if (!IsOffsetKnown)
      return PI.setAborted(&LI);

    const DataLayout &DL = LI.getModule()->getDataLayout();
    uint64_t Size = DL.getTypeStoreSize(LI.getType());
    return handleLoadOrStore(LI.getType(), LI, Offset, Size, LI.isVolatile());
  }

  void visitStoreInst(StoreInst &SI) {
    Value *ValOp = SI.getValueOperand();
    if (ValOp == *U)
      return PI.setEscapedAndAborted(&SI);
    if (!IsOffsetKnown)
      return PI.setAborted(&SI);

    const DataLayout &DL = SI.getModule()->getDataLayout();
    uint64_t Size = DL.getTypeStoreSize(ValOp->getType());

    // If this memory access can be shown to *statically* extend outside the
    // bounds of the allocation, it's behavior is undefined, so simply
    // ignore it. Note that this is more strict than the generic clamping
    // behavior of insertUse. We also try to handle cases which might run the
    // risk of overflow.
    // FIXME: We should instead consider the pointer to have escaped if this
    // function is being instrumented for addressing bugs or race conditions.
    if (Size > AllocSize || Offset.ugt(AllocSize - Size)) {
      LLVM_DEBUG(dbgs() << "WARNING: Ignoring " << Size << " byte store @"
                        << Offset << " which extends past the end of the "
                        << AllocSize << " byte alloca:\n"
                        << "    alloca: " << AS.AI << "\n"
                        << "       use: " << SI << "\n");
      return markAsDead(SI);
    }

    assert((!SI.isSimple() || ValOp->getType()->isSingleValueType()) &&
           "All simple FCA stores should have been pre-split");
    handleLoadOrStore(ValOp->getType(), SI, Offset, Size, SI.isVolatile());
  }

  void visitMemSetInst(MemSetInst &II) {
    assert(II.getRawDest() == *U && "Pointer use is not the destination?");
    ConstantInt *Length = dyn_cast<ConstantInt>(II.getLength());
    if ((Length && Length->getValue() == 0) ||
        (IsOffsetKnown && Offset.uge(AllocSize)))
      // Zero-length mem transfer intrinsics can be ignored entirely.
      return markAsDead(II);

    if (!IsOffsetKnown)
      return PI.setAborted(&II);

    insertUse(II, Offset, Length ? Length->getLimitedValue()
                                 : AllocSize - Offset.getLimitedValue(),
              (bool)Length);
  }

  void visitMemTransferInst(MemTransferInst &II) {
    ConstantInt *Length = dyn_cast<ConstantInt>(II.getLength());
    if (Length && Length->getValue() == 0)
      // Zero-length mem transfer intrinsics can be ignored entirely.
      return markAsDead(II);

    // Because we can visit these intrinsics twice, also check to see if the
    // first time marked this instruction as dead. If so, skip it.
    if (VisitedDeadInsts.count(&II))
      return;

    if (!IsOffsetKnown)
      return PI.setAborted(&II);

    // This side of the transfer is completely out-of-bounds, and so we can
    // nuke the entire transfer. However, we also need to nuke the other side
    // if already added to our partitions.
    // FIXME: Yet another place we really should bypass this when
    // instrumenting for ASan.
    if (Offset.uge(AllocSize)) {
      SmallDenseMap<Instruction *, unsigned>::iterator MTPI =
          MemTransferSliceMap.find(&II);
      if (MTPI != MemTransferSliceMap.end())
        AS.Slices[MTPI->second].kill();
      return markAsDead(II);
    }

    uint64_t RawOffset = Offset.getLimitedValue();
    uint64_t Size = Length ? Length->getLimitedValue() : AllocSize - RawOffset;

    // Check for the special case where the same exact value is used for both
    // source and dest.
    if (*U == II.getRawDest() && *U == II.getRawSource()) {
      // For non-volatile transfers this is a no-op.
      if (!II.isVolatile())
        return markAsDead(II);

      return insertUse(II, Offset, Size, /*IsSplittable=*/false);
    }

    // If we have seen both source and destination for a mem transfer, then
    // they both point to the same alloca.
    bool Inserted;
    SmallDenseMap<Instruction *, unsigned>::iterator MTPI;
    std::tie(MTPI, Inserted) =
        MemTransferSliceMap.insert(std::make_pair(&II, AS.Slices.size()));
    unsigned PrevIdx = MTPI->second;
    if (!Inserted) {
      Slice &PrevP = AS.Slices[PrevIdx];

      // Check if the begin offsets match and this is a non-volatile transfer.
      // In that case, we can completely elide the transfer.
      if (!II.isVolatile() && PrevP.beginOffset() == RawOffset) {
        PrevP.kill();
        return markAsDead(II);
      }

      // Otherwise we have an offset transfer within the same alloca. We can't
      // split those.
      PrevP.makeUnsplittable();
    }

    // Insert the use now that we've fixed up the splittable nature.
    insertUse(II, Offset, Size, /*IsSplittable=*/Inserted && Length);

    // Check that we ended up with a valid index in the map.
    assert(AS.Slices[PrevIdx].getUse()->getUser() == &II &&
           "Map index doesn't point back to a slice with this user.");
  }

  // Disable SRoA for any intrinsics except for lifetime invariants.
  // FIXME: What about debug intrinsics? This matches old behavior, but
  // doesn't make sense.
  void visitIntrinsicInst(IntrinsicInst &II) {
    if (!IsOffsetKnown)
      return PI.setAborted(&II);

    if (II.isLifetimeStartOrEnd()) {
      ConstantInt *Length = cast<ConstantInt>(II.getArgOperand(0));
      uint64_t Size = std::min(AllocSize - Offset.getLimitedValue(),
                               Length->getLimitedValue());
      insertUse(II, Offset, Size, true);
      return;
    }

    Base::visitIntrinsicInst(II);
  }

  Instruction *hasUnsafePHIOrSelectUse(Instruction *Root, uint64_t &Size) {
    // We consider any PHI or select that results in a direct load or store of
    // the same offset to be a viable use for slicing purposes. These uses
    // are considered unsplittable and the size is the maximum loaded or stored
    // size.
    SmallPtrSet<Instruction *, 4> Visited;
    SmallVector<std::pair<Instruction *, Instruction *>, 4> Uses;
    Visited.insert(Root);
    Uses.push_back(std::make_pair(cast<Instruction>(*U), Root));
    const DataLayout &DL = Root->getModule()->getDataLayout();
    // If there are no loads or stores, the access is dead. We mark that as
    // a size zero access.
    Size = 0;
    do {
      Instruction *I, *UsedI;
      std::tie(UsedI, I) = Uses.pop_back_val();

      if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
        Size = std::max(Size, DL.getTypeStoreSize(LI->getType()));
        continue;
      }
      if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
        Value *Op = SI->getOperand(0);
        if (Op == UsedI)
          return SI;
        Size = std::max(Size, DL.getTypeStoreSize(Op->getType()));
        continue;
      }

      if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(I)) {
        if (!GEP->hasAllZeroIndices())
          return GEP;
      } else if (!isa<BitCastInst>(I) && !isa<PHINode>(I) &&
                 !isa<SelectInst>(I)) {
        return I;
      }

      for (User *U : I->users())
        if (Visited.insert(cast<Instruction>(U)).second)
          Uses.push_back(std::make_pair(I, cast<Instruction>(U)));
    } while (!Uses.empty());

    return nullptr;
  }

  void visitPHINodeOrSelectInst(Instruction &I) {
    assert(isa<PHINode>(I) || isa<SelectInst>(I));
    if (I.use_empty())
      return markAsDead(I);

    // TODO: We could use SimplifyInstruction here to fold PHINodes and
    // SelectInsts. However, doing so requires to change the current
    // dead-operand-tracking mechanism. For instance, suppose neither loading
    // from %U nor %other traps. Then "load (select undef, %U, %other)" does not
    // trap either.  However, if we simply replace %U with undef using the
    // current dead-operand-tracking mechanism, "load (select undef, undef,
    // %other)" may trap because the select may return the first operand
    // "undef".
    if (Value *Result = foldPHINodeOrSelectInst(I)) {
      if (Result == *U)
        // If the result of the constant fold will be the pointer, recurse
        // through the PHI/select as if we had RAUW'ed it.
        enqueueUsers(I);
      else
        // Otherwise the operand to the PHI/select is dead, and we can replace
        // it with undef.
        AS.DeadOperands.push_back(U);

      return;
    }

    if (!IsOffsetKnown)
      return PI.setAborted(&I);

    // See if we already have computed info on this node.
    uint64_t &Size = PHIOrSelectSizes[&I];
    if (!Size) {
      // This is a new PHI/Select, check for an unsafe use of it.
      if (Instruction *UnsafeI = hasUnsafePHIOrSelectUse(&I, Size))
        return PI.setAborted(UnsafeI);
    }

    // For PHI and select operands outside the alloca, we can't nuke the entire
    // phi or select -- the other side might still be relevant, so we special
    // case them here and use a separate structure to track the operands
    // themselves which should be replaced with undef.
    // FIXME: This should instead be escaped in the event we're instrumenting
    // for address sanitization.
    if (Offset.uge(AllocSize)) {
      AS.DeadOperands.push_back(U);
      return;
    }

    insertUse(I, Offset, Size);
  }

  void visitPHINode(PHINode &PN) { visitPHINodeOrSelectInst(PN); }

  void visitSelectInst(SelectInst &SI) { visitPHINodeOrSelectInst(SI); }

  /// Disable SROA entirely if there are unhandled users of the alloca.
  void visitInstruction(Instruction &I) { PI.setAborted(&I); }
};

AllocaSlices::AllocaSlices(const DataLayout &DL, AllocaInst &AI)
    :
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
      AI(AI),
#endif
      PointerEscapingInstr(nullptr) {
  SliceBuilder PB(DL, AI, *this);
  SliceBuilder::PtrInfo PtrI = PB.visitPtr(AI);
  if (PtrI.isEscaped() || PtrI.isAborted()) {
    // FIXME: We should sink the escape vs. abort info into the caller nicely,
    // possibly by just storing the PtrInfo in the AllocaSlices.
    PointerEscapingInstr = PtrI.getEscapingInst() ? PtrI.getEscapingInst()
                                                  : PtrI.getAbortingInst();
    assert(PointerEscapingInstr && "Did not track a bad instruction");
    return;
  }

  Slices.erase(
      llvm::remove_if(Slices, [](const Slice &S) { return S.isDead(); }),
      Slices.end());

#ifndef NDEBUG
  if (SROARandomShuffleSlices) {
    std::mt19937 MT(static_cast<unsigned>(
        std::chrono::system_clock::now().time_since_epoch().count()));
    std::shuffle(Slices.begin(), Slices.end(), MT);
  }
#endif

  // Sort the uses. This arranges for the offsets to be in ascending order,
  // and the sizes to be in descending order.
  llvm::sort(Slices);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)

void AllocaSlices::print(raw_ostream &OS, const_iterator I,
                         StringRef Indent) const {
  printSlice(OS, I, Indent);
  OS << "\n";
  printUse(OS, I, Indent);
}

void AllocaSlices::printSlice(raw_ostream &OS, const_iterator I,
                              StringRef Indent) const {
  OS << Indent << "[" << I->beginOffset() << "," << I->endOffset() << ")"
     << " slice #" << (I - begin())
     << (I->isSplittable() ? " (splittable)" : "");
}

void AllocaSlices::printUse(raw_ostream &OS, const_iterator I,
                            StringRef Indent) const {
  OS << Indent << "  used by: " << *I->getUse()->getUser() << "\n";
}

void AllocaSlices::print(raw_ostream &OS) const {
  if (PointerEscapingInstr) {
    OS << "Can't analyze slices for alloca: " << AI << "\n"
       << "  A pointer to this alloca escaped by:\n"
       << "  " << *PointerEscapingInstr << "\n";
    return;
  }

  OS << "Slices of alloca: " << AI << "\n";
  for (const_iterator I = begin(), E = end(); I != E; ++I)
    print(OS, I);
}

LLVM_DUMP_METHOD void AllocaSlices::dump(const_iterator I) const {
  print(dbgs(), I);
}
LLVM_DUMP_METHOD void AllocaSlices::dump() const { print(dbgs()); }

#endif // !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)

/// Walk the range of a partitioning looking for a common type to cover this
/// sequence of slices.
static Type *findCommonType(AllocaSlices::const_iterator B,
                            AllocaSlices::const_iterator E,
                            uint64_t EndOffset) {
  Type *Ty = nullptr;
  bool TyIsCommon = true;
  IntegerType *ITy = nullptr;

  // Note that we need to look at *every* alloca slice's Use to ensure we
  // always get consistent results regardless of the order of slices.
  for (AllocaSlices::const_iterator I = B; I != E; ++I) {
    Use *U = I->getUse();
    if (isa<IntrinsicInst>(*U->getUser()))
      continue;
    if (I->beginOffset() != B->beginOffset() || I->endOffset() != EndOffset)
      continue;

    Type *UserTy = nullptr;
    if (LoadInst *LI = dyn_cast<LoadInst>(U->getUser())) {
      UserTy = LI->getType();
    } else if (StoreInst *SI = dyn_cast<StoreInst>(U->getUser())) {
      UserTy = SI->getValueOperand()->getType();
    }

    if (IntegerType *UserITy = dyn_cast_or_null<IntegerType>(UserTy)) {
      // If the type is larger than the partition, skip it. We only encounter
      // this for split integer operations where we want to use the type of the
      // entity causing the split. Also skip if the type is not a byte width
      // multiple.
      if (UserITy->getBitWidth() % 8 != 0 ||
          UserITy->getBitWidth() / 8 > (EndOffset - B->beginOffset()))
        continue;

      // Track the largest bitwidth integer type used in this way in case there
      // is no common type.
      if (!ITy || ITy->getBitWidth() < UserITy->getBitWidth())
        ITy = UserITy;
    }

    // To avoid depending on the order of slices, Ty and TyIsCommon must not
    // depend on types skipped above.
    if (!UserTy || (Ty && Ty != UserTy))
      TyIsCommon = false; // Give up on anything but an iN type.
    else
      Ty = UserTy;
  }

  return TyIsCommon ? Ty : ITy;
}

/// PHI instructions that use an alloca and are subsequently loaded can be
/// rewritten to load both input pointers in the pred blocks and then PHI the
/// results, allowing the load of the alloca to be promoted.
/// From this:
///   %P2 = phi [i32* %Alloca, i32* %Other]
///   %V = load i32* %P2
/// to:
///   %V1 = load i32* %Alloca      -> will be mem2reg'd
///   ...
///   %V2 = load i32* %Other
///   ...
///   %V = phi [i32 %V1, i32 %V2]
///
/// We can do this to a select if its only uses are loads and if the operands
/// to the select can be loaded unconditionally.
///
/// FIXME: This should be hoisted into a generic utility, likely in
/// Transforms/Util/Local.h
static bool isSafePHIToSpeculate(PHINode &PN) {
  // For now, we can only do this promotion if the load is in the same block
  // as the PHI, and if there are no stores between the phi and load.
  // TODO: Allow recursive phi users.
  // TODO: Allow stores.
  BasicBlock *BB = PN.getParent();
  unsigned MaxAlign = 0;
  bool HaveLoad = false;
  for (User *U : PN.users()) {
    LoadInst *LI = dyn_cast<LoadInst>(U);
    if (!LI || !LI->isSimple())
      return false;

    // For now we only allow loads in the same block as the PHI.  This is
    // a common case that happens when instcombine merges two loads through
    // a PHI.
    if (LI->getParent() != BB)
      return false;

    // Ensure that there are no instructions between the PHI and the load that
    // could store.
    for (BasicBlock::iterator BBI(PN); &*BBI != LI; ++BBI)
      if (BBI->mayWriteToMemory())
        return false;

    MaxAlign = std::max(MaxAlign, LI->getAlignment());
    HaveLoad = true;
  }

  if (!HaveLoad)
    return false;

  const DataLayout &DL = PN.getModule()->getDataLayout();

  // We can only transform this if it is safe to push the loads into the
  // predecessor blocks. The only thing to watch out for is that we can't put
  // a possibly trapping load in the predecessor if it is a critical edge.
  for (unsigned Idx = 0, Num = PN.getNumIncomingValues(); Idx != Num; ++Idx) {
    Instruction *TI = PN.getIncomingBlock(Idx)->getTerminator();
    Value *InVal = PN.getIncomingValue(Idx);

    // If the value is produced by the terminator of the predecessor (an
    // invoke) or it has side-effects, there is no valid place to put a load
    // in the predecessor.
    if (TI == InVal || TI->mayHaveSideEffects())
      return false;

    // If the predecessor has a single successor, then the edge isn't
    // critical.
    if (TI->getNumSuccessors() == 1)
      continue;

    // If this pointer is always safe to load, or if we can prove that there
    // is already a load in the block, then we can move the load to the pred
    // block.
    if (isSafeToLoadUnconditionally(InVal, MaxAlign, DL, TI))
      continue;

    return false;
  }

  return true;
}

static void speculatePHINodeLoads(PHINode &PN) {
  LLVM_DEBUG(dbgs() << "    original: " << PN << "\n");

  Type *LoadTy = cast<PointerType>(PN.getType())->getElementType();
  IRBuilderTy PHIBuilder(&PN);
  PHINode *NewPN = PHIBuilder.CreatePHI(LoadTy, PN.getNumIncomingValues(),
                                        PN.getName() + ".sroa.speculated");

  // Get the AA tags and alignment to use from one of the loads.  It doesn't
  // matter which one we get and if any differ.
  LoadInst *SomeLoad = cast<LoadInst>(PN.user_back());

  AAMDNodes AATags;
  SomeLoad->getAAMetadata(AATags);
  unsigned Align = SomeLoad->getAlignment();

  // Rewrite all loads of the PN to use the new PHI.
  while (!PN.use_empty()) {
    LoadInst *LI = cast<LoadInst>(PN.user_back());
    LI->replaceAllUsesWith(NewPN);
    LI->eraseFromParent();
  }

  // Inject loads into all of the pred blocks.
  DenseMap<BasicBlock*, Value*> InjectedLoads;
  for (unsigned Idx = 0, Num = PN.getNumIncomingValues(); Idx != Num; ++Idx) {
    BasicBlock *Pred = PN.getIncomingBlock(Idx);
    Value *InVal = PN.getIncomingValue(Idx);

    // A PHI node is allowed to have multiple (duplicated) entries for the same
    // basic block, as long as the value is the same. So if we already injected
    // a load in the predecessor, then we should reuse the same load for all
    // duplicated entries.
    if (Value* V = InjectedLoads.lookup(Pred)) {
      NewPN->addIncoming(V, Pred);
      continue;
    }

    Instruction *TI = Pred->getTerminator();
    IRBuilderTy PredBuilder(TI);

    LoadInst *Load = PredBuilder.CreateLoad(
        InVal, (PN.getName() + ".sroa.speculate.load." + Pred->getName()));
    ++NumLoadsSpeculated;
    Load->setAlignment(Align);
    if (AATags)
      Load->setAAMetadata(AATags);
    NewPN->addIncoming(Load, Pred);
    InjectedLoads[Pred] = Load;
  }

  LLVM_DEBUG(dbgs() << "          speculated to: " << *NewPN << "\n");
  PN.eraseFromParent();
}

/// Select instructions that use an alloca and are subsequently loaded can be
/// rewritten to load both input pointers and then select between the result,
/// allowing the load of the alloca to be promoted.
/// From this:
///   %P2 = select i1 %cond, i32* %Alloca, i32* %Other
///   %V = load i32* %P2
/// to:
///   %V1 = load i32* %Alloca      -> will be mem2reg'd
///   %V2 = load i32* %Other
///   %V = select i1 %cond, i32 %V1, i32 %V2
///
/// We can do this to a select if its only uses are loads and if the operand
/// to the select can be loaded unconditionally.
static bool isSafeSelectToSpeculate(SelectInst &SI) {
  Value *TValue = SI.getTrueValue();
  Value *FValue = SI.getFalseValue();
  const DataLayout &DL = SI.getModule()->getDataLayout();

  for (User *U : SI.users()) {
    LoadInst *LI = dyn_cast<LoadInst>(U);
    if (!LI || !LI->isSimple())
      return false;

    // Both operands to the select need to be dereferenceable, either
    // absolutely (e.g. allocas) or at this point because we can see other
    // accesses to it.
    if (!isSafeToLoadUnconditionally(TValue, LI->getAlignment(), DL, LI))
      return false;
    if (!isSafeToLoadUnconditionally(FValue, LI->getAlignment(), DL, LI))
      return false;
  }

  return true;
}

static void speculateSelectInstLoads(SelectInst &SI) {
  LLVM_DEBUG(dbgs() << "    original: " << SI << "\n");

  IRBuilderTy IRB(&SI);
  Value *TV = SI.getTrueValue();
  Value *FV = SI.getFalseValue();
  // Replace the loads of the select with a select of two loads.
  while (!SI.use_empty()) {
    LoadInst *LI = cast<LoadInst>(SI.user_back());
    assert(LI->isSimple() && "We only speculate simple loads");

    IRB.SetInsertPoint(LI);
    LoadInst *TL =
        IRB.CreateLoad(TV, LI->getName() + ".sroa.speculate.load.true");
    LoadInst *FL =
        IRB.CreateLoad(FV, LI->getName() + ".sroa.speculate.load.false");
    NumLoadsSpeculated += 2;

    // Transfer alignment and AA info if present.
    TL->setAlignment(LI->getAlignment());
    FL->setAlignment(LI->getAlignment());

    AAMDNodes Tags;
    LI->getAAMetadata(Tags);
    if (Tags) {
      TL->setAAMetadata(Tags);
      FL->setAAMetadata(Tags);
    }

    Value *V = IRB.CreateSelect(SI.getCondition(), TL, FL,
                                LI->getName() + ".sroa.speculated");

    LLVM_DEBUG(dbgs() << "          speculated to: " << *V << "\n");
    LI->replaceAllUsesWith(V);
    LI->eraseFromParent();
  }
  SI.eraseFromParent();
}

/// Build a GEP out of a base pointer and indices.
///
/// This will return the BasePtr if that is valid, or build a new GEP
/// instruction using the IRBuilder if GEP-ing is needed.
static Value *buildGEP(IRBuilderTy &IRB, Value *BasePtr,
                       SmallVectorImpl<Value *> &Indices, Twine NamePrefix) {
  if (Indices.empty())
    return BasePtr;

  // A single zero index is a no-op, so check for this and avoid building a GEP
  // in that case.
  if (Indices.size() == 1 && cast<ConstantInt>(Indices.back())->isZero())
    return BasePtr;

  return IRB.CreateInBoundsGEP(nullptr, BasePtr, Indices,
                               NamePrefix + "sroa_idx");
}

/// Get a natural GEP off of the BasePtr walking through Ty toward
/// TargetTy without changing the offset of the pointer.
///
/// This routine assumes we've already established a properly offset GEP with
/// Indices, and arrived at the Ty type. The goal is to continue to GEP with
/// zero-indices down through type layers until we find one the same as
/// TargetTy. If we can't find one with the same type, we at least try to use
/// one with the same size. If none of that works, we just produce the GEP as
/// indicated by Indices to have the correct offset.
static Value *getNaturalGEPWithType(IRBuilderTy &IRB, const DataLayout &DL,
                                    Value *BasePtr, Type *Ty, Type *TargetTy,
                                    SmallVectorImpl<Value *> &Indices,
                                    Twine NamePrefix) {
  if (Ty == TargetTy)
    return buildGEP(IRB, BasePtr, Indices, NamePrefix);

  // Offset size to use for the indices.
  unsigned OffsetSize = DL.getIndexTypeSizeInBits(BasePtr->getType());

  // See if we can descend into a struct and locate a field with the correct
  // type.
  unsigned NumLayers = 0;
  Type *ElementTy = Ty;
  do {
    if (ElementTy->isPointerTy())
      break;

    if (ArrayType *ArrayTy = dyn_cast<ArrayType>(ElementTy)) {
      ElementTy = ArrayTy->getElementType();
      Indices.push_back(IRB.getIntN(OffsetSize, 0));
    } else if (VectorType *VectorTy = dyn_cast<VectorType>(ElementTy)) {
      ElementTy = VectorTy->getElementType();
      Indices.push_back(IRB.getInt32(0));
    } else if (StructType *STy = dyn_cast<StructType>(ElementTy)) {
      if (STy->element_begin() == STy->element_end())
        break; // Nothing left to descend into.
      ElementTy = *STy->element_begin();
      Indices.push_back(IRB.getInt32(0));
    } else {
      break;
    }
    ++NumLayers;
  } while (ElementTy != TargetTy);
  if (ElementTy != TargetTy)
    Indices.erase(Indices.end() - NumLayers, Indices.end());

  return buildGEP(IRB, BasePtr, Indices, NamePrefix);
}

/// Recursively compute indices for a natural GEP.
///
/// This is the recursive step for getNaturalGEPWithOffset that walks down the
/// element types adding appropriate indices for the GEP.
static Value *getNaturalGEPRecursively(IRBuilderTy &IRB, const DataLayout &DL,
                                       Value *Ptr, Type *Ty, APInt &Offset,
                                       Type *TargetTy,
                                       SmallVectorImpl<Value *> &Indices,
                                       Twine NamePrefix) {
  if (Offset == 0)
    return getNaturalGEPWithType(IRB, DL, Ptr, Ty, TargetTy, Indices,
                                 NamePrefix);

  // We can't recurse through pointer types.
  if (Ty->isPointerTy())
    return nullptr;

  // We try to analyze GEPs over vectors here, but note that these GEPs are
  // extremely poorly defined currently. The long-term goal is to remove GEPing
  // over a vector from the IR completely.
  if (VectorType *VecTy = dyn_cast<VectorType>(Ty)) {
    unsigned ElementSizeInBits = DL.getTypeSizeInBits(VecTy->getScalarType());
    if (ElementSizeInBits % 8 != 0) {
      // GEPs over non-multiple of 8 size vector elements are invalid.
      return nullptr;
    }
    APInt ElementSize(Offset.getBitWidth(), ElementSizeInBits / 8);
    APInt NumSkippedElements = Offset.sdiv(ElementSize);
    if (NumSkippedElements.ugt(VecTy->getNumElements()))
      return nullptr;
    Offset -= NumSkippedElements * ElementSize;
    Indices.push_back(IRB.getInt(NumSkippedElements));
    return getNaturalGEPRecursively(IRB, DL, Ptr, VecTy->getElementType(),
                                    Offset, TargetTy, Indices, NamePrefix);
  }

  if (ArrayType *ArrTy = dyn_cast<ArrayType>(Ty)) {
    Type *ElementTy = ArrTy->getElementType();
    APInt ElementSize(Offset.getBitWidth(), DL.getTypeAllocSize(ElementTy));
    APInt NumSkippedElements = Offset.sdiv(ElementSize);
    if (NumSkippedElements.ugt(ArrTy->getNumElements()))
      return nullptr;

    Offset -= NumSkippedElements * ElementSize;
    Indices.push_back(IRB.getInt(NumSkippedElements));
    return getNaturalGEPRecursively(IRB, DL, Ptr, ElementTy, Offset, TargetTy,
                                    Indices, NamePrefix);
  }

  StructType *STy = dyn_cast<StructType>(Ty);
  if (!STy)
    return nullptr;

  const StructLayout *SL = DL.getStructLayout(STy);
  uint64_t StructOffset = Offset.getZExtValue();
  if (StructOffset >= SL->getSizeInBytes())
    return nullptr;
  unsigned Index = SL->getElementContainingOffset(StructOffset);
  Offset -= APInt(Offset.getBitWidth(), SL->getElementOffset(Index));
  Type *ElementTy = STy->getElementType(Index);
  if (Offset.uge(DL.getTypeAllocSize(ElementTy)))
    return nullptr; // The offset points into alignment padding.

  Indices.push_back(IRB.getInt32(Index));
  return getNaturalGEPRecursively(IRB, DL, Ptr, ElementTy, Offset, TargetTy,
                                  Indices, NamePrefix);
}

/// Get a natural GEP from a base pointer to a particular offset and
/// resulting in a particular type.
///
/// The goal is to produce a "natural" looking GEP that works with the existing
/// composite types to arrive at the appropriate offset and element type for
/// a pointer. TargetTy is the element type the returned GEP should point-to if
/// possible. We recurse by decreasing Offset, adding the appropriate index to
/// Indices, and setting Ty to the result subtype.
///
/// If no natural GEP can be constructed, this function returns null.
static Value *getNaturalGEPWithOffset(IRBuilderTy &IRB, const DataLayout &DL,
                                      Value *Ptr, APInt Offset, Type *TargetTy,
                                      SmallVectorImpl<Value *> &Indices,
                                      Twine NamePrefix) {
  PointerType *Ty = cast<PointerType>(Ptr->getType());

  // Don't consider any GEPs through an i8* as natural unless the TargetTy is
  // an i8.
  if (Ty == IRB.getInt8PtrTy(Ty->getAddressSpace()) && TargetTy->isIntegerTy(8))
    return nullptr;

  Type *ElementTy = Ty->getElementType();
  if (!ElementTy->isSized())
    return nullptr; // We can't GEP through an unsized element.
  APInt ElementSize(Offset.getBitWidth(), DL.getTypeAllocSize(ElementTy));
  if (ElementSize == 0)
    return nullptr; // Zero-length arrays can't help us build a natural GEP.
  APInt NumSkippedElements = Offset.sdiv(ElementSize);

  Offset -= NumSkippedElements * ElementSize;
  Indices.push_back(IRB.getInt(NumSkippedElements));
  return getNaturalGEPRecursively(IRB, DL, Ptr, ElementTy, Offset, TargetTy,
                                  Indices, NamePrefix);
}

/// Compute an adjusted pointer from Ptr by Offset bytes where the
/// resulting pointer has PointerTy.
///
/// This tries very hard to compute a "natural" GEP which arrives at the offset
/// and produces the pointer type desired. Where it cannot, it will try to use
/// the natural GEP to arrive at the offset and bitcast to the type. Where that
/// fails, it will try to use an existing i8* and GEP to the byte offset and
/// bitcast to the type.
///
/// The strategy for finding the more natural GEPs is to peel off layers of the
/// pointer, walking back through bit casts and GEPs, searching for a base
/// pointer from which we can compute a natural GEP with the desired
/// properties. The algorithm tries to fold as many constant indices into
/// a single GEP as possible, thus making each GEP more independent of the
/// surrounding code.
static Value *getAdjustedPtr(IRBuilderTy &IRB, const DataLayout &DL, Value *Ptr,
                             APInt Offset, Type *PointerTy, Twine NamePrefix) {
  // Even though we don't look through PHI nodes, we could be called on an
  // instruction in an unreachable block, which may be on a cycle.
  SmallPtrSet<Value *, 4> Visited;
  Visited.insert(Ptr);
  SmallVector<Value *, 4> Indices;

  // We may end up computing an offset pointer that has the wrong type. If we
  // never are able to compute one directly that has the correct type, we'll
  // fall back to it, so keep it and the base it was computed from around here.
  Value *OffsetPtr = nullptr;
  Value *OffsetBasePtr;

  // Remember any i8 pointer we come across to re-use if we need to do a raw
  // byte offset.
  Value *Int8Ptr = nullptr;
  APInt Int8PtrOffset(Offset.getBitWidth(), 0);

  Type *TargetTy = PointerTy->getPointerElementType();

  do {
    // First fold any existing GEPs into the offset.
    while (GEPOperator *GEP = dyn_cast<GEPOperator>(Ptr)) {
      APInt GEPOffset(Offset.getBitWidth(), 0);
      if (!GEP->accumulateConstantOffset(DL, GEPOffset))
        break;
      Offset += GEPOffset;
      Ptr = GEP->getPointerOperand();
      if (!Visited.insert(Ptr).second)
        break;
    }

    // See if we can perform a natural GEP here.
    Indices.clear();
    if (Value *P = getNaturalGEPWithOffset(IRB, DL, Ptr, Offset, TargetTy,
                                           Indices, NamePrefix)) {
      // If we have a new natural pointer at the offset, clear out any old
      // offset pointer we computed. Unless it is the base pointer or
      // a non-instruction, we built a GEP we don't need. Zap it.
      if (OffsetPtr && OffsetPtr != OffsetBasePtr)
        if (Instruction *I = dyn_cast<Instruction>(OffsetPtr)) {
          assert(I->use_empty() && "Built a GEP with uses some how!");
          I->eraseFromParent();
        }
      OffsetPtr = P;
      OffsetBasePtr = Ptr;
      // If we also found a pointer of the right type, we're done.
      if (P->getType() == PointerTy)
        return P;
    }

    // Stash this pointer if we've found an i8*.
    if (Ptr->getType()->isIntegerTy(8)) {
      Int8Ptr = Ptr;
      Int8PtrOffset = Offset;
    }

    // Peel off a layer of the pointer and update the offset appropriately.
    if (Operator::getOpcode(Ptr) == Instruction::BitCast) {
      Ptr = cast<Operator>(Ptr)->getOperand(0);
    } else if (GlobalAlias *GA = dyn_cast<GlobalAlias>(Ptr)) {
      if (GA->isInterposable())
        break;
      Ptr = GA->getAliasee();
    } else {
      break;
    }
    assert(Ptr->getType()->isPointerTy() && "Unexpected operand type!");
  } while (Visited.insert(Ptr).second);

  if (!OffsetPtr) {
    if (!Int8Ptr) {
      Int8Ptr = IRB.CreateBitCast(
          Ptr, IRB.getInt8PtrTy(PointerTy->getPointerAddressSpace()),
          NamePrefix + "sroa_raw_cast");
      Int8PtrOffset = Offset;
    }

    OffsetPtr = Int8PtrOffset == 0
                    ? Int8Ptr
                    : IRB.CreateInBoundsGEP(IRB.getInt8Ty(), Int8Ptr,
                                            IRB.getInt(Int8PtrOffset),
                                            NamePrefix + "sroa_raw_idx");
  }
  Ptr = OffsetPtr;

  // On the off chance we were targeting i8*, guard the bitcast here.
  if (Ptr->getType() != PointerTy)
    Ptr = IRB.CreateBitCast(Ptr, PointerTy, NamePrefix + "sroa_cast");

  return Ptr;
}

/// Compute the adjusted alignment for a load or store from an offset.
static unsigned getAdjustedAlignment(Instruction *I, uint64_t Offset,
                                     const DataLayout &DL) {
  unsigned Alignment;
  Type *Ty;
  if (auto *LI = dyn_cast<LoadInst>(I)) {
    Alignment = LI->getAlignment();
    Ty = LI->getType();
  } else if (auto *SI = dyn_cast<StoreInst>(I)) {
    Alignment = SI->getAlignment();
    Ty = SI->getValueOperand()->getType();
  } else {
    llvm_unreachable("Only loads and stores are allowed!");
  }

  if (!Alignment)
    Alignment = DL.getABITypeAlignment(Ty);

  return MinAlign(Alignment, Offset);
}

/// Test whether we can convert a value from the old to the new type.
///
/// This predicate should be used to guard calls to convertValue in order to
/// ensure that we only try to convert viable values. The strategy is that we
/// will peel off single element struct and array wrappings to get to an
/// underlying value, and convert that value.
static bool canConvertValue(const DataLayout &DL, Type *OldTy, Type *NewTy) {
  if (OldTy == NewTy)
    return true;

  // For integer types, we can't handle any bit-width differences. This would
  // break both vector conversions with extension and introduce endianness
  // issues when in conjunction with loads and stores.
  if (isa<IntegerType>(OldTy) && isa<IntegerType>(NewTy)) {
    assert(cast<IntegerType>(OldTy)->getBitWidth() !=
               cast<IntegerType>(NewTy)->getBitWidth() &&
           "We can't have the same bitwidth for different int types");
    return false;
  }

  if (DL.getTypeSizeInBits(NewTy) != DL.getTypeSizeInBits(OldTy))
    return false;
  if (!NewTy->isSingleValueType() || !OldTy->isSingleValueType())
    return false;

  // We can convert pointers to integers and vice-versa. Same for vectors
  // of pointers and integers.
  OldTy = OldTy->getScalarType();
  NewTy = NewTy->getScalarType();
  if (NewTy->isPointerTy() || OldTy->isPointerTy()) {
    if (NewTy->isPointerTy() && OldTy->isPointerTy()) {
      return cast<PointerType>(NewTy)->getPointerAddressSpace() ==
        cast<PointerType>(OldTy)->getPointerAddressSpace();
    }

    // We can convert integers to integral pointers, but not to non-integral
    // pointers.
    if (OldTy->isIntegerTy())
      return !DL.isNonIntegralPointerType(NewTy);

    // We can convert integral pointers to integers, but non-integral pointers
    // need to remain pointers.
    if (!DL.isNonIntegralPointerType(OldTy))
      return NewTy->isIntegerTy();

    return false;
  }

  return true;
}

/// Generic routine to convert an SSA value to a value of a different
/// type.
///
/// This will try various different casting techniques, such as bitcasts,
/// inttoptr, and ptrtoint casts. Use the \c canConvertValue predicate to test
/// two types for viability with this routine.
static Value *convertValue(const DataLayout &DL, IRBuilderTy &IRB, Value *V,
                           Type *NewTy) {
  Type *OldTy = V->getType();
  assert(canConvertValue(DL, OldTy, NewTy) && "Value not convertable to type");

  if (OldTy == NewTy)
    return V;

  assert(!(isa<IntegerType>(OldTy) && isa<IntegerType>(NewTy)) &&
         "Integer types must be the exact same to convert.");

  // See if we need inttoptr for this type pair. A cast involving both scalars
  // and vectors requires and additional bitcast.
  if (OldTy->isIntOrIntVectorTy() && NewTy->isPtrOrPtrVectorTy()) {
    // Expand <2 x i32> to i8* --> <2 x i32> to i64 to i8*
    if (OldTy->isVectorTy() && !NewTy->isVectorTy())
      return IRB.CreateIntToPtr(IRB.CreateBitCast(V, DL.getIntPtrType(NewTy)),
                                NewTy);

    // Expand i128 to <2 x i8*> --> i128 to <2 x i64> to <2 x i8*>
    if (!OldTy->isVectorTy() && NewTy->isVectorTy())
      return IRB.CreateIntToPtr(IRB.CreateBitCast(V, DL.getIntPtrType(NewTy)),
                                NewTy);

    return IRB.CreateIntToPtr(V, NewTy);
  }

  // See if we need ptrtoint for this type pair. A cast involving both scalars
  // and vectors requires and additional bitcast.
  if (OldTy->isPtrOrPtrVectorTy() && NewTy->isIntOrIntVectorTy()) {
    // Expand <2 x i8*> to i128 --> <2 x i8*> to <2 x i64> to i128
    if (OldTy->isVectorTy() && !NewTy->isVectorTy())
      return IRB.CreateBitCast(IRB.CreatePtrToInt(V, DL.getIntPtrType(OldTy)),
                               NewTy);

    // Expand i8* to <2 x i32> --> i8* to i64 to <2 x i32>
    if (!OldTy->isVectorTy() && NewTy->isVectorTy())
      return IRB.CreateBitCast(IRB.CreatePtrToInt(V, DL.getIntPtrType(OldTy)),
                               NewTy);

    return IRB.CreatePtrToInt(V, NewTy);
  }

  return IRB.CreateBitCast(V, NewTy);
}

/// Test whether the given slice use can be promoted to a vector.
///
/// This function is called to test each entry in a partition which is slated
/// for a single slice.
static bool isVectorPromotionViableForSlice(Partition &P, const Slice &S,
                                            VectorType *Ty,
                                            uint64_t ElementSize,
                                            const DataLayout &DL) {
  // First validate the slice offsets.
  uint64_t BeginOffset =
      std::max(S.beginOffset(), P.beginOffset()) - P.beginOffset();
  uint64_t BeginIndex = BeginOffset / ElementSize;
  if (BeginIndex * ElementSize != BeginOffset ||
      BeginIndex >= Ty->getNumElements())
    return false;
  uint64_t EndOffset =
      std::min(S.endOffset(), P.endOffset()) - P.beginOffset();
  uint64_t EndIndex = EndOffset / ElementSize;
  if (EndIndex * ElementSize != EndOffset || EndIndex > Ty->getNumElements())
    return false;

  assert(EndIndex > BeginIndex && "Empty vector!");
  uint64_t NumElements = EndIndex - BeginIndex;
  Type *SliceTy = (NumElements == 1)
                      ? Ty->getElementType()
                      : VectorType::get(Ty->getElementType(), NumElements);

  Type *SplitIntTy =
      Type::getIntNTy(Ty->getContext(), NumElements * ElementSize * 8);

  Use *U = S.getUse();

  if (MemIntrinsic *MI = dyn_cast<MemIntrinsic>(U->getUser())) {
    if (MI->isVolatile())
      return false;
    if (!S.isSplittable())
      return false; // Skip any unsplittable intrinsics.
  } else if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(U->getUser())) {
    if (!II->isLifetimeStartOrEnd())
      return false;
  } else if (U->get()->getType()->getPointerElementType()->isStructTy()) {
    // Disable vector promotion when there are loads or stores of an FCA.
    return false;
  } else if (LoadInst *LI = dyn_cast<LoadInst>(U->getUser())) {
    if (LI->isVolatile())
      return false;
    Type *LTy = LI->getType();
    if (P.beginOffset() > S.beginOffset() || P.endOffset() < S.endOffset()) {
      assert(LTy->isIntegerTy());
      LTy = SplitIntTy;
    }
    if (!canConvertValue(DL, SliceTy, LTy))
      return false;
  } else if (StoreInst *SI = dyn_cast<StoreInst>(U->getUser())) {
    if (SI->isVolatile())
      return false;
    Type *STy = SI->getValueOperand()->getType();
    if (P.beginOffset() > S.beginOffset() || P.endOffset() < S.endOffset()) {
      assert(STy->isIntegerTy());
      STy = SplitIntTy;
    }
    if (!canConvertValue(DL, STy, SliceTy))
      return false;
  } else {
    return false;
  }

  return true;
}

/// Test whether the given alloca partitioning and range of slices can be
/// promoted to a vector.
///
/// This is a quick test to check whether we can rewrite a particular alloca
/// partition (and its newly formed alloca) into a vector alloca with only
/// whole-vector loads and stores such that it could be promoted to a vector
/// SSA value. We only can ensure this for a limited set of operations, and we
/// don't want to do the rewrites unless we are confident that the result will
/// be promotable, so we have an early test here.
static VectorType *isVectorPromotionViable(Partition &P, const DataLayout &DL) {
  // Collect the candidate types for vector-based promotion. Also track whether
  // we have different element types.
  SmallVector<VectorType *, 4> CandidateTys;
  Type *CommonEltTy = nullptr;
  bool HaveCommonEltTy = true;
  auto CheckCandidateType = [&](Type *Ty) {
    if (auto *VTy = dyn_cast<VectorType>(Ty)) {
      CandidateTys.push_back(VTy);
      if (!CommonEltTy)
        CommonEltTy = VTy->getElementType();
      else if (CommonEltTy != VTy->getElementType())
        HaveCommonEltTy = false;
    }
  };
  // Consider any loads or stores that are the exact size of the slice.
  for (const Slice &S : P)
    if (S.beginOffset() == P.beginOffset() &&
        S.endOffset() == P.endOffset()) {
      if (auto *LI = dyn_cast<LoadInst>(S.getUse()->getUser()))
        CheckCandidateType(LI->getType());
      else if (auto *SI = dyn_cast<StoreInst>(S.getUse()->getUser()))
        CheckCandidateType(SI->getValueOperand()->getType());
    }

  // If we didn't find a vector type, nothing to do here.
  if (CandidateTys.empty())
    return nullptr;

  // Remove non-integer vector types if we had multiple common element types.
  // FIXME: It'd be nice to replace them with integer vector types, but we can't
  // do that until all the backends are known to produce good code for all
  // integer vector types.
  if (!HaveCommonEltTy) {
    CandidateTys.erase(
        llvm::remove_if(CandidateTys,
                        [](VectorType *VTy) {
                          return !VTy->getElementType()->isIntegerTy();
                        }),
        CandidateTys.end());

    // If there were no integer vector types, give up.
    if (CandidateTys.empty())
      return nullptr;

    // Rank the remaining candidate vector types. This is easy because we know
    // they're all integer vectors. We sort by ascending number of elements.
    auto RankVectorTypes = [&DL](VectorType *RHSTy, VectorType *LHSTy) {
      (void)DL;
      assert(DL.getTypeSizeInBits(RHSTy) == DL.getTypeSizeInBits(LHSTy) &&
             "Cannot have vector types of different sizes!");
      assert(RHSTy->getElementType()->isIntegerTy() &&
             "All non-integer types eliminated!");
      assert(LHSTy->getElementType()->isIntegerTy() &&
             "All non-integer types eliminated!");
      return RHSTy->getNumElements() < LHSTy->getNumElements();
    };
    llvm::sort(CandidateTys, RankVectorTypes);
    CandidateTys.erase(
        std::unique(CandidateTys.begin(), CandidateTys.end(), RankVectorTypes),
        CandidateTys.end());
  } else {
// The only way to have the same element type in every vector type is to
// have the same vector type. Check that and remove all but one.
#ifndef NDEBUG
    for (VectorType *VTy : CandidateTys) {
      assert(VTy->getElementType() == CommonEltTy &&
             "Unaccounted for element type!");
      assert(VTy == CandidateTys[0] &&
             "Different vector types with the same element type!");
    }
#endif
    CandidateTys.resize(1);
  }

  // Try each vector type, and return the one which works.
  auto CheckVectorTypeForPromotion = [&](VectorType *VTy) {
    uint64_t ElementSize = DL.getTypeSizeInBits(VTy->getElementType());

    // While the definition of LLVM vectors is bitpacked, we don't support sizes
    // that aren't byte sized.
    if (ElementSize % 8)
      return false;
    assert((DL.getTypeSizeInBits(VTy) % 8) == 0 &&
           "vector size not a multiple of element size?");
    ElementSize /= 8;

    for (const Slice &S : P)
      if (!isVectorPromotionViableForSlice(P, S, VTy, ElementSize, DL))
        return false;

    for (const Slice *S : P.splitSliceTails())
      if (!isVectorPromotionViableForSlice(P, *S, VTy, ElementSize, DL))
        return false;

    return true;
  };
  for (VectorType *VTy : CandidateTys)
    if (CheckVectorTypeForPromotion(VTy))
      return VTy;

  return nullptr;
}

/// Test whether a slice of an alloca is valid for integer widening.
///
/// This implements the necessary checking for the \c isIntegerWideningViable
/// test below on a single slice of the alloca.
static bool isIntegerWideningViableForSlice(const Slice &S,
                                            uint64_t AllocBeginOffset,
                                            Type *AllocaTy,
                                            const DataLayout &DL,
                                            bool &WholeAllocaOp) {
  uint64_t Size = DL.getTypeStoreSize(AllocaTy);

  uint64_t RelBegin = S.beginOffset() - AllocBeginOffset;
  uint64_t RelEnd = S.endOffset() - AllocBeginOffset;

  // We can't reasonably handle cases where the load or store extends past
  // the end of the alloca's type and into its padding.
  if (RelEnd > Size)
    return false;

  Use *U = S.getUse();

  if (LoadInst *LI = dyn_cast<LoadInst>(U->getUser())) {
    if (LI->isVolatile())
      return false;
    // We can't handle loads that extend past the allocated memory.
    if (DL.getTypeStoreSize(LI->getType()) > Size)
      return false;
    // So far, AllocaSliceRewriter does not support widening split slice tails
    // in rewriteIntegerLoad.
    if (S.beginOffset() < AllocBeginOffset)
      return false;
    // Note that we don't count vector loads or stores as whole-alloca
    // operations which enable integer widening because we would prefer to use
    // vector widening instead.
    if (!isa<VectorType>(LI->getType()) && RelBegin == 0 && RelEnd == Size)
      WholeAllocaOp = true;
    if (IntegerType *ITy = dyn_cast<IntegerType>(LI->getType())) {
      if (ITy->getBitWidth() < DL.getTypeStoreSizeInBits(ITy))
        return false;
    } else if (RelBegin != 0 || RelEnd != Size ||
               !canConvertValue(DL, AllocaTy, LI->getType())) {
      // Non-integer loads need to be convertible from the alloca type so that
      // they are promotable.
      return false;
    }
  } else if (StoreInst *SI = dyn_cast<StoreInst>(U->getUser())) {
    Type *ValueTy = SI->getValueOperand()->getType();
    if (SI->isVolatile())
      return false;
    // We can't handle stores that extend past the allocated memory.
    if (DL.getTypeStoreSize(ValueTy) > Size)
      return false;
    // So far, AllocaSliceRewriter does not support widening split slice tails
    // in rewriteIntegerStore.
    if (S.beginOffset() < AllocBeginOffset)
      return false;
    // Note that we don't count vector loads or stores as whole-alloca
    // operations which enable integer widening because we would prefer to use
    // vector widening instead.
    if (!isa<VectorType>(ValueTy) && RelBegin == 0 && RelEnd == Size)
      WholeAllocaOp = true;
    if (IntegerType *ITy = dyn_cast<IntegerType>(ValueTy)) {
      if (ITy->getBitWidth() < DL.getTypeStoreSizeInBits(ITy))
        return false;
    } else if (RelBegin != 0 || RelEnd != Size ||
               !canConvertValue(DL, ValueTy, AllocaTy)) {
      // Non-integer stores need to be convertible to the alloca type so that
      // they are promotable.
      return false;
    }
  } else if (MemIntrinsic *MI = dyn_cast<MemIntrinsic>(U->getUser())) {
    if (MI->isVolatile() || !isa<Constant>(MI->getLength()))
      return false;
    if (!S.isSplittable())
      return false; // Skip any unsplittable intrinsics.
  } else if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(U->getUser())) {
    if (!II->isLifetimeStartOrEnd())
      return false;
  } else {
    return false;
  }

  return true;
}

/// Test whether the given alloca partition's integer operations can be
/// widened to promotable ones.
///
/// This is a quick test to check whether we can rewrite the integer loads and
/// stores to a particular alloca into wider loads and stores and be able to
/// promote the resulting alloca.
static bool isIntegerWideningViable(Partition &P, Type *AllocaTy,
                                    const DataLayout &DL) {
  uint64_t SizeInBits = DL.getTypeSizeInBits(AllocaTy);
  // Don't create integer types larger than the maximum bitwidth.
  if (SizeInBits > IntegerType::MAX_INT_BITS)
    return false;

  // Don't try to handle allocas with bit-padding.
  if (SizeInBits != DL.getTypeStoreSizeInBits(AllocaTy))
    return false;

  // We need to ensure that an integer type with the appropriate bitwidth can
  // be converted to the alloca type, whatever that is. We don't want to force
  // the alloca itself to have an integer type if there is a more suitable one.
  Type *IntTy = Type::getIntNTy(AllocaTy->getContext(), SizeInBits);
  if (!canConvertValue(DL, AllocaTy, IntTy) ||
      !canConvertValue(DL, IntTy, AllocaTy))
    return false;

  // While examining uses, we ensure that the alloca has a covering load or
  // store. We don't want to widen the integer operations only to fail to
  // promote due to some other unsplittable entry (which we may make splittable
  // later). However, if there are only splittable uses, go ahead and assume
  // that we cover the alloca.
  // FIXME: We shouldn't consider split slices that happen to start in the
  // partition here...
  bool WholeAllocaOp =
      P.begin() != P.end() ? false : DL.isLegalInteger(SizeInBits);

  for (const Slice &S : P)
    if (!isIntegerWideningViableForSlice(S, P.beginOffset(), AllocaTy, DL,
                                         WholeAllocaOp))
      return false;

  for (const Slice *S : P.splitSliceTails())
    if (!isIntegerWideningViableForSlice(*S, P.beginOffset(), AllocaTy, DL,
                                         WholeAllocaOp))
      return false;

  return WholeAllocaOp;
}

static Value *extractInteger(const DataLayout &DL, IRBuilderTy &IRB, Value *V,
                             IntegerType *Ty, uint64_t Offset,
                             const Twine &Name) {
  LLVM_DEBUG(dbgs() << "       start: " << *V << "\n");
  IntegerType *IntTy = cast<IntegerType>(V->getType());
  assert(DL.getTypeStoreSize(Ty) + Offset <= DL.getTypeStoreSize(IntTy) &&
         "Element extends past full value");
  uint64_t ShAmt = 8 * Offset;
  if (DL.isBigEndian())
    ShAmt = 8 * (DL.getTypeStoreSize(IntTy) - DL.getTypeStoreSize(Ty) - Offset);
  if (ShAmt) {
    V = IRB.CreateLShr(V, ShAmt, Name + ".shift");
    LLVM_DEBUG(dbgs() << "     shifted: " << *V << "\n");
  }
  assert(Ty->getBitWidth() <= IntTy->getBitWidth() &&
         "Cannot extract to a larger integer!");
  if (Ty != IntTy) {
    V = IRB.CreateTrunc(V, Ty, Name + ".trunc");
    LLVM_DEBUG(dbgs() << "     trunced: " << *V << "\n");
  }
  return V;
}

static Value *insertInteger(const DataLayout &DL, IRBuilderTy &IRB, Value *Old,
                            Value *V, uint64_t Offset, const Twine &Name) {
  IntegerType *IntTy = cast<IntegerType>(Old->getType());
  IntegerType *Ty = cast<IntegerType>(V->getType());
  assert(Ty->getBitWidth() <= IntTy->getBitWidth() &&
         "Cannot insert a larger integer!");
  LLVM_DEBUG(dbgs() << "       start: " << *V << "\n");
  if (Ty != IntTy) {
    V = IRB.CreateZExt(V, IntTy, Name + ".ext");
    LLVM_DEBUG(dbgs() << "    extended: " << *V << "\n");
  }
  assert(DL.getTypeStoreSize(Ty) + Offset <= DL.getTypeStoreSize(IntTy) &&
         "Element store outside of alloca store");
  uint64_t ShAmt = 8 * Offset;
  if (DL.isBigEndian())
    ShAmt = 8 * (DL.getTypeStoreSize(IntTy) - DL.getTypeStoreSize(Ty) - Offset);
  if (ShAmt) {
    V = IRB.CreateShl(V, ShAmt, Name + ".shift");
    LLVM_DEBUG(dbgs() << "     shifted: " << *V << "\n");
  }

  if (ShAmt || Ty->getBitWidth() < IntTy->getBitWidth()) {
    APInt Mask = ~Ty->getMask().zext(IntTy->getBitWidth()).shl(ShAmt);
    Old = IRB.CreateAnd(Old, Mask, Name + ".mask");
    LLVM_DEBUG(dbgs() << "      masked: " << *Old << "\n");
    V = IRB.CreateOr(Old, V, Name + ".insert");
    LLVM_DEBUG(dbgs() << "    inserted: " << *V << "\n");
  }
  return V;
}

static Value *extractVector(IRBuilderTy &IRB, Value *V, unsigned BeginIndex,
                            unsigned EndIndex, const Twine &Name) {
  VectorType *VecTy = cast<VectorType>(V->getType());
  unsigned NumElements = EndIndex - BeginIndex;
  assert(NumElements <= VecTy->getNumElements() && "Too many elements!");

  if (NumElements == VecTy->getNumElements())
    return V;

  if (NumElements == 1) {
    V = IRB.CreateExtractElement(V, IRB.getInt32(BeginIndex),
                                 Name + ".extract");
    LLVM_DEBUG(dbgs() << "     extract: " << *V << "\n");
    return V;
  }

  SmallVector<Constant *, 8> Mask;
  Mask.reserve(NumElements);
  for (unsigned i = BeginIndex; i != EndIndex; ++i)
    Mask.push_back(IRB.getInt32(i));
  V = IRB.CreateShuffleVector(V, UndefValue::get(V->getType()),
                              ConstantVector::get(Mask), Name + ".extract");
  LLVM_DEBUG(dbgs() << "     shuffle: " << *V << "\n");
  return V;
}

static Value *insertVector(IRBuilderTy &IRB, Value *Old, Value *V,
                           unsigned BeginIndex, const Twine &Name) {
  VectorType *VecTy = cast<VectorType>(Old->getType());
  assert(VecTy && "Can only insert a vector into a vector");

  VectorType *Ty = dyn_cast<VectorType>(V->getType());
  if (!Ty) {
    // Single element to insert.
    V = IRB.CreateInsertElement(Old, V, IRB.getInt32(BeginIndex),
                                Name + ".insert");
    LLVM_DEBUG(dbgs() << "     insert: " << *V << "\n");
    return V;
  }

  assert(Ty->getNumElements() <= VecTy->getNumElements() &&
         "Too many elements!");
  if (Ty->getNumElements() == VecTy->getNumElements()) {
    assert(V->getType() == VecTy && "Vector type mismatch");
    return V;
  }
  unsigned EndIndex = BeginIndex + Ty->getNumElements();

  // When inserting a smaller vector into the larger to store, we first
  // use a shuffle vector to widen it with undef elements, and then
  // a second shuffle vector to select between the loaded vector and the
  // incoming vector.
  SmallVector<Constant *, 8> Mask;
  Mask.reserve(VecTy->getNumElements());
  for (unsigned i = 0; i != VecTy->getNumElements(); ++i)
    if (i >= BeginIndex && i < EndIndex)
      Mask.push_back(IRB.getInt32(i - BeginIndex));
    else
      Mask.push_back(UndefValue::get(IRB.getInt32Ty()));
  V = IRB.CreateShuffleVector(V, UndefValue::get(V->getType()),
                              ConstantVector::get(Mask), Name + ".expand");
  LLVM_DEBUG(dbgs() << "    shuffle: " << *V << "\n");

  Mask.clear();
  for (unsigned i = 0; i != VecTy->getNumElements(); ++i)
    Mask.push_back(IRB.getInt1(i >= BeginIndex && i < EndIndex));

  V = IRB.CreateSelect(ConstantVector::get(Mask), V, Old, Name + "blend");

  LLVM_DEBUG(dbgs() << "    blend: " << *V << "\n");
  return V;
}

/// Visitor to rewrite instructions using p particular slice of an alloca
/// to use a new alloca.
///
/// Also implements the rewriting to vector-based accesses when the partition
/// passes the isVectorPromotionViable predicate. Most of the rewriting logic
/// lives here.
class llvm::sroa::AllocaSliceRewriter
    : public InstVisitor<AllocaSliceRewriter, bool> {
  // Befriend the base class so it can delegate to private visit methods.
  friend class InstVisitor<AllocaSliceRewriter, bool>;

  using Base = InstVisitor<AllocaSliceRewriter, bool>;

  const DataLayout &DL;
  AllocaSlices &AS;
  SROA &Pass;
  AllocaInst &OldAI, &NewAI;
  const uint64_t NewAllocaBeginOffset, NewAllocaEndOffset;
  Type *NewAllocaTy;

  // This is a convenience and flag variable that will be null unless the new
  // alloca's integer operations should be widened to this integer type due to
  // passing isIntegerWideningViable above. If it is non-null, the desired
  // integer type will be stored here for easy access during rewriting.
  IntegerType *IntTy;

  // If we are rewriting an alloca partition which can be written as pure
  // vector operations, we stash extra information here. When VecTy is
  // non-null, we have some strict guarantees about the rewritten alloca:
  //   - The new alloca is exactly the size of the vector type here.
  //   - The accesses all either map to the entire vector or to a single
  //     element.
  //   - The set of accessing instructions is only one of those handled above
  //     in isVectorPromotionViable. Generally these are the same access kinds
  //     which are promotable via mem2reg.
  VectorType *VecTy;
  Type *ElementTy;
  uint64_t ElementSize;

  // The original offset of the slice currently being rewritten relative to
  // the original alloca.
  uint64_t BeginOffset = 0;
  uint64_t EndOffset = 0;

  // The new offsets of the slice currently being rewritten relative to the
  // original alloca.
  uint64_t NewBeginOffset, NewEndOffset;

  uint64_t SliceSize;
  bool IsSplittable = false;
  bool IsSplit = false;
  Use *OldUse = nullptr;
  Instruction *OldPtr = nullptr;

  // Track post-rewrite users which are PHI nodes and Selects.
  SmallSetVector<PHINode *, 8> &PHIUsers;
  SmallSetVector<SelectInst *, 8> &SelectUsers;

  // Utility IR builder, whose name prefix is setup for each visited use, and
  // the insertion point is set to point to the user.
  IRBuilderTy IRB;

public:
  AllocaSliceRewriter(const DataLayout &DL, AllocaSlices &AS, SROA &Pass,
                      AllocaInst &OldAI, AllocaInst &NewAI,
                      uint64_t NewAllocaBeginOffset,
                      uint64_t NewAllocaEndOffset, bool IsIntegerPromotable,
                      VectorType *PromotableVecTy,
                      SmallSetVector<PHINode *, 8> &PHIUsers,
                      SmallSetVector<SelectInst *, 8> &SelectUsers)
      : DL(DL), AS(AS), Pass(Pass), OldAI(OldAI), NewAI(NewAI),
        NewAllocaBeginOffset(NewAllocaBeginOffset),
        NewAllocaEndOffset(NewAllocaEndOffset),
        NewAllocaTy(NewAI.getAllocatedType()),
        IntTy(IsIntegerPromotable
                  ? Type::getIntNTy(
                        NewAI.getContext(),
                        DL.getTypeSizeInBits(NewAI.getAllocatedType()))
                  : nullptr),
        VecTy(PromotableVecTy),
        ElementTy(VecTy ? VecTy->getElementType() : nullptr),
        ElementSize(VecTy ? DL.getTypeSizeInBits(ElementTy) / 8 : 0),
        PHIUsers(PHIUsers), SelectUsers(SelectUsers),
        IRB(NewAI.getContext(), ConstantFolder()) {
    if (VecTy) {
      assert((DL.getTypeSizeInBits(ElementTy) % 8) == 0 &&
             "Only multiple-of-8 sized vector elements are viable");
      ++NumVectorized;
    }
    assert((!IntTy && !VecTy) || (IntTy && !VecTy) || (!IntTy && VecTy));
  }

  bool visit(AllocaSlices::const_iterator I) {
    bool CanSROA = true;
    BeginOffset = I->beginOffset();
    EndOffset = I->endOffset();
    IsSplittable = I->isSplittable();
    IsSplit =
        BeginOffset < NewAllocaBeginOffset || EndOffset > NewAllocaEndOffset;
    LLVM_DEBUG(dbgs() << "  rewriting " << (IsSplit ? "split " : ""));
    LLVM_DEBUG(AS.printSlice(dbgs(), I, ""));
    LLVM_DEBUG(dbgs() << "\n");

    // Compute the intersecting offset range.
    assert(BeginOffset < NewAllocaEndOffset);
    assert(EndOffset > NewAllocaBeginOffset);
    NewBeginOffset = std::max(BeginOffset, NewAllocaBeginOffset);
    NewEndOffset = std::min(EndOffset, NewAllocaEndOffset);

    SliceSize = NewEndOffset - NewBeginOffset;

    OldUse = I->getUse();
    OldPtr = cast<Instruction>(OldUse->get());

    Instruction *OldUserI = cast<Instruction>(OldUse->getUser());
    IRB.SetInsertPoint(OldUserI);
    IRB.SetCurrentDebugLocation(OldUserI->getDebugLoc());
    IRB.SetNamePrefix(Twine(NewAI.getName()) + "." + Twine(BeginOffset) + ".");

    CanSROA &= visit(cast<Instruction>(OldUse->getUser()));
    if (VecTy || IntTy)
      assert(CanSROA);
    return CanSROA;
  }

private:
  // Make sure the other visit overloads are visible.
  using Base::visit;

  // Every instruction which can end up as a user must have a rewrite rule.
  bool visitInstruction(Instruction &I) {
    LLVM_DEBUG(dbgs() << "    !!!! Cannot rewrite: " << I << "\n");
    llvm_unreachable("No rewrite rule for this instruction!");
  }

  Value *getNewAllocaSlicePtr(IRBuilderTy &IRB, Type *PointerTy) {
    // Note that the offset computation can use BeginOffset or NewBeginOffset
    // interchangeably for unsplit slices.
    assert(IsSplit || BeginOffset == NewBeginOffset);
    uint64_t Offset = NewBeginOffset - NewAllocaBeginOffset;

#ifndef NDEBUG
    StringRef OldName = OldPtr->getName();
    // Skip through the last '.sroa.' component of the name.
    size_t LastSROAPrefix = OldName.rfind(".sroa.");
    if (LastSROAPrefix != StringRef::npos) {
      OldName = OldName.substr(LastSROAPrefix + strlen(".sroa."));
      // Look for an SROA slice index.
      size_t IndexEnd = OldName.find_first_not_of("0123456789");
      if (IndexEnd != StringRef::npos && OldName[IndexEnd] == '.') {
        // Strip the index and look for the offset.
        OldName = OldName.substr(IndexEnd + 1);
        size_t OffsetEnd = OldName.find_first_not_of("0123456789");
        if (OffsetEnd != StringRef::npos && OldName[OffsetEnd] == '.')
          // Strip the offset.
          OldName = OldName.substr(OffsetEnd + 1);
      }
    }
    // Strip any SROA suffixes as well.
    OldName = OldName.substr(0, OldName.find(".sroa_"));
#endif

    return getAdjustedPtr(IRB, DL, &NewAI,
                          APInt(DL.getIndexTypeSizeInBits(PointerTy), Offset),
                          PointerTy,
#ifndef NDEBUG
                          Twine(OldName) + "."
#else
                          Twine()
#endif
                          );
  }

  /// Compute suitable alignment to access this slice of the *new*
  /// alloca.
  ///
  /// You can optionally pass a type to this routine and if that type's ABI
  /// alignment is itself suitable, this will return zero.
  unsigned getSliceAlign(Type *Ty = nullptr) {
    unsigned NewAIAlign = NewAI.getAlignment();
    if (!NewAIAlign)
      NewAIAlign = DL.getABITypeAlignment(NewAI.getAllocatedType());
    unsigned Align =
        MinAlign(NewAIAlign, NewBeginOffset - NewAllocaBeginOffset);
    return (Ty && Align == DL.getABITypeAlignment(Ty)) ? 0 : Align;
  }

  unsigned getIndex(uint64_t Offset) {
    assert(VecTy && "Can only call getIndex when rewriting a vector");
    uint64_t RelOffset = Offset - NewAllocaBeginOffset;
    assert(RelOffset / ElementSize < UINT32_MAX && "Index out of bounds");
    uint32_t Index = RelOffset / ElementSize;
    assert(Index * ElementSize == RelOffset);
    return Index;
  }

  void deleteIfTriviallyDead(Value *V) {
    Instruction *I = cast<Instruction>(V);
    if (isInstructionTriviallyDead(I))
      Pass.DeadInsts.insert(I);
  }

  Value *rewriteVectorizedLoadInst() {
    unsigned BeginIndex = getIndex(NewBeginOffset);
    unsigned EndIndex = getIndex(NewEndOffset);
    assert(EndIndex > BeginIndex && "Empty vector!");

    Value *V = IRB.CreateAlignedLoad(&NewAI, NewAI.getAlignment(), "load");
    return extractVector(IRB, V, BeginIndex, EndIndex, "vec");
  }

  Value *rewriteIntegerLoad(LoadInst &LI) {
    assert(IntTy && "We cannot insert an integer to the alloca");
    assert(!LI.isVolatile());
    Value *V = IRB.CreateAlignedLoad(&NewAI, NewAI.getAlignment(), "load");
    V = convertValue(DL, IRB, V, IntTy);
    assert(NewBeginOffset >= NewAllocaBeginOffset && "Out of bounds offset");
    uint64_t Offset = NewBeginOffset - NewAllocaBeginOffset;
    if (Offset > 0 || NewEndOffset < NewAllocaEndOffset) {
      IntegerType *ExtractTy = Type::getIntNTy(LI.getContext(), SliceSize * 8);
      V = extractInteger(DL, IRB, V, ExtractTy, Offset, "extract");
    }
    // It is possible that the extracted type is not the load type. This
    // happens if there is a load past the end of the alloca, and as
    // a consequence the slice is narrower but still a candidate for integer
    // lowering. To handle this case, we just zero extend the extracted
    // integer.
    assert(cast<IntegerType>(LI.getType())->getBitWidth() >= SliceSize * 8 &&
           "Can only handle an extract for an overly wide load");
    if (cast<IntegerType>(LI.getType())->getBitWidth() > SliceSize * 8)
      V = IRB.CreateZExt(V, LI.getType());
    return V;
  }

  bool visitLoadInst(LoadInst &LI) {
    LLVM_DEBUG(dbgs() << "    original: " << LI << "\n");
    Value *OldOp = LI.getOperand(0);
    assert(OldOp == OldPtr);

    AAMDNodes AATags;
    LI.getAAMetadata(AATags);

    unsigned AS = LI.getPointerAddressSpace();

    Type *TargetTy = IsSplit ? Type::getIntNTy(LI.getContext(), SliceSize * 8)
                             : LI.getType();
    const bool IsLoadPastEnd = DL.getTypeStoreSize(TargetTy) > SliceSize;
    bool IsPtrAdjusted = false;
    Value *V;
    if (VecTy) {
      V = rewriteVectorizedLoadInst();
    } else if (IntTy && LI.getType()->isIntegerTy()) {
      V = rewriteIntegerLoad(LI);
    } else if (NewBeginOffset == NewAllocaBeginOffset &&
               NewEndOffset == NewAllocaEndOffset &&
               (canConvertValue(DL, NewAllocaTy, TargetTy) ||
                (IsLoadPastEnd && NewAllocaTy->isIntegerTy() &&
                 TargetTy->isIntegerTy()))) {
      LoadInst *NewLI = IRB.CreateAlignedLoad(&NewAI, NewAI.getAlignment(),
                                              LI.isVolatile(), LI.getName());
      if (AATags)
        NewLI->setAAMetadata(AATags);
      if (LI.isVolatile())
        NewLI->setAtomic(LI.getOrdering(), LI.getSyncScopeID());

      // Any !nonnull metadata or !range metadata on the old load is also valid
      // on the new load. This is even true in some cases even when the loads
      // are different types, for example by mapping !nonnull metadata to
      // !range metadata by modeling the null pointer constant converted to the
      // integer type.
      // FIXME: Add support for range metadata here. Currently the utilities
      // for this don't propagate range metadata in trivial cases from one
      // integer load to another, don't handle non-addrspace-0 null pointers
      // correctly, and don't have any support for mapping ranges as the
      // integer type becomes winder or narrower.
      if (MDNode *N = LI.getMetadata(LLVMContext::MD_nonnull))
        copyNonnullMetadata(LI, N, *NewLI);

      // Try to preserve nonnull metadata
      V = NewLI;

      // If this is an integer load past the end of the slice (which means the
      // bytes outside the slice are undef or this load is dead) just forcibly
      // fix the integer size with correct handling of endianness.
      if (auto *AITy = dyn_cast<IntegerType>(NewAllocaTy))
        if (auto *TITy = dyn_cast<IntegerType>(TargetTy))
          if (AITy->getBitWidth() < TITy->getBitWidth()) {
            V = IRB.CreateZExt(V, TITy, "load.ext");
            if (DL.isBigEndian())
              V = IRB.CreateShl(V, TITy->getBitWidth() - AITy->getBitWidth(),
                                "endian_shift");
          }
    } else {
      Type *LTy = TargetTy->getPointerTo(AS);
      LoadInst *NewLI = IRB.CreateAlignedLoad(getNewAllocaSlicePtr(IRB, LTy),
                                              getSliceAlign(TargetTy),
                                              LI.isVolatile(), LI.getName());
      if (AATags)
        NewLI->setAAMetadata(AATags);
      if (LI.isVolatile())
        NewLI->setAtomic(LI.getOrdering(), LI.getSyncScopeID());

      V = NewLI;
      IsPtrAdjusted = true;
    }
    V = convertValue(DL, IRB, V, TargetTy);

    if (IsSplit) {
      assert(!LI.isVolatile());
      assert(LI.getType()->isIntegerTy() &&
             "Only integer type loads and stores are split");
      assert(SliceSize < DL.getTypeStoreSize(LI.getType()) &&
             "Split load isn't smaller than original load");
      assert(LI.getType()->getIntegerBitWidth() ==
                 DL.getTypeStoreSizeInBits(LI.getType()) &&
             "Non-byte-multiple bit width");
      // Move the insertion point just past the load so that we can refer to it.
      IRB.SetInsertPoint(&*std::next(BasicBlock::iterator(&LI)));
      // Create a placeholder value with the same type as LI to use as the
      // basis for the new value. This allows us to replace the uses of LI with
      // the computed value, and then replace the placeholder with LI, leaving
      // LI only used for this computation.
      Value *Placeholder =
          new LoadInst(UndefValue::get(LI.getType()->getPointerTo(AS)));
      V = insertInteger(DL, IRB, Placeholder, V, NewBeginOffset - BeginOffset,
                        "insert");
      LI.replaceAllUsesWith(V);
      Placeholder->replaceAllUsesWith(&LI);
      Placeholder->deleteValue();
    } else {
      LI.replaceAllUsesWith(V);
    }

    Pass.DeadInsts.insert(&LI);
    deleteIfTriviallyDead(OldOp);
    LLVM_DEBUG(dbgs() << "          to: " << *V << "\n");
    return !LI.isVolatile() && !IsPtrAdjusted;
  }

  bool rewriteVectorizedStoreInst(Value *V, StoreInst &SI, Value *OldOp,
                                  AAMDNodes AATags) {
    if (V->getType() != VecTy) {
      unsigned BeginIndex = getIndex(NewBeginOffset);
      unsigned EndIndex = getIndex(NewEndOffset);
      assert(EndIndex > BeginIndex && "Empty vector!");
      unsigned NumElements = EndIndex - BeginIndex;
      assert(NumElements <= VecTy->getNumElements() && "Too many elements!");
      Type *SliceTy = (NumElements == 1)
                          ? ElementTy
                          : VectorType::get(ElementTy, NumElements);
      if (V->getType() != SliceTy)
        V = convertValue(DL, IRB, V, SliceTy);

      // Mix in the existing elements.
      Value *Old = IRB.CreateAlignedLoad(&NewAI, NewAI.getAlignment(), "load");
      V = insertVector(IRB, Old, V, BeginIndex, "vec");
    }
    StoreInst *Store = IRB.CreateAlignedStore(V, &NewAI, NewAI.getAlignment());
    if (AATags)
      Store->setAAMetadata(AATags);
    Pass.DeadInsts.insert(&SI);

    LLVM_DEBUG(dbgs() << "          to: " << *Store << "\n");
    return true;
  }

  bool rewriteIntegerStore(Value *V, StoreInst &SI, AAMDNodes AATags) {
    assert(IntTy && "We cannot extract an integer from the alloca");
    assert(!SI.isVolatile());
    if (DL.getTypeSizeInBits(V->getType()) != IntTy->getBitWidth()) {
      Value *Old =
          IRB.CreateAlignedLoad(&NewAI, NewAI.getAlignment(), "oldload");
      Old = convertValue(DL, IRB, Old, IntTy);
      assert(BeginOffset >= NewAllocaBeginOffset && "Out of bounds offset");
      uint64_t Offset = BeginOffset - NewAllocaBeginOffset;
      V = insertInteger(DL, IRB, Old, SI.getValueOperand(), Offset, "insert");
    }
    V = convertValue(DL, IRB, V, NewAllocaTy);
    StoreInst *Store = IRB.CreateAlignedStore(V, &NewAI, NewAI.getAlignment());
    Store->copyMetadata(SI, {LLVMContext::MD_mem_parallel_loop_access,
                             LLVMContext::MD_access_group});
    if (AATags)
      Store->setAAMetadata(AATags);
    Pass.DeadInsts.insert(&SI);
    LLVM_DEBUG(dbgs() << "          to: " << *Store << "\n");
    return true;
  }

  bool visitStoreInst(StoreInst &SI) {
    LLVM_DEBUG(dbgs() << "    original: " << SI << "\n");
    Value *OldOp = SI.getOperand(1);
    assert(OldOp == OldPtr);

    AAMDNodes AATags;
    SI.getAAMetadata(AATags);

    Value *V = SI.getValueOperand();

    // Strip all inbounds GEPs and pointer casts to try to dig out any root
    // alloca that should be re-examined after promoting this alloca.
    if (V->getType()->isPointerTy())
      if (AllocaInst *AI = dyn_cast<AllocaInst>(V->stripInBoundsOffsets()))
        Pass.PostPromotionWorklist.insert(AI);

    if (SliceSize < DL.getTypeStoreSize(V->getType())) {
      assert(!SI.isVolatile());
      assert(V->getType()->isIntegerTy() &&
             "Only integer type loads and stores are split");
      assert(V->getType()->getIntegerBitWidth() ==
                 DL.getTypeStoreSizeInBits(V->getType()) &&
             "Non-byte-multiple bit width");
      IntegerType *NarrowTy = Type::getIntNTy(SI.getContext(), SliceSize * 8);
      V = extractInteger(DL, IRB, V, NarrowTy, NewBeginOffset - BeginOffset,
                         "extract");
    }

    if (VecTy)
      return rewriteVectorizedStoreInst(V, SI, OldOp, AATags);
    if (IntTy && V->getType()->isIntegerTy())
      return rewriteIntegerStore(V, SI, AATags);

    const bool IsStorePastEnd = DL.getTypeStoreSize(V->getType()) > SliceSize;
    StoreInst *NewSI;
    if (NewBeginOffset == NewAllocaBeginOffset &&
        NewEndOffset == NewAllocaEndOffset &&
        (canConvertValue(DL, V->getType(), NewAllocaTy) ||
         (IsStorePastEnd && NewAllocaTy->isIntegerTy() &&
          V->getType()->isIntegerTy()))) {
      // If this is an integer store past the end of slice (and thus the bytes
      // past that point are irrelevant or this is unreachable), truncate the
      // value prior to storing.
      if (auto *VITy = dyn_cast<IntegerType>(V->getType()))
        if (auto *AITy = dyn_cast<IntegerType>(NewAllocaTy))
          if (VITy->getBitWidth() > AITy->getBitWidth()) {
            if (DL.isBigEndian())
              V = IRB.CreateLShr(V, VITy->getBitWidth() - AITy->getBitWidth(),
                                 "endian_shift");
            V = IRB.CreateTrunc(V, AITy, "load.trunc");
          }

      V = convertValue(DL, IRB, V, NewAllocaTy);
      NewSI = IRB.CreateAlignedStore(V, &NewAI, NewAI.getAlignment(),
                                     SI.isVolatile());
    } else {
      unsigned AS = SI.getPointerAddressSpace();
      Value *NewPtr = getNewAllocaSlicePtr(IRB, V->getType()->getPointerTo(AS));
      NewSI = IRB.CreateAlignedStore(V, NewPtr, getSliceAlign(V->getType()),
                                     SI.isVolatile());
    }
    NewSI->copyMetadata(SI, {LLVMContext::MD_mem_parallel_loop_access,
                             LLVMContext::MD_access_group});
    if (AATags)
      NewSI->setAAMetadata(AATags);
    if (SI.isVolatile())
      NewSI->setAtomic(SI.getOrdering(), SI.getSyncScopeID());
    Pass.DeadInsts.insert(&SI);
    deleteIfTriviallyDead(OldOp);

    LLVM_DEBUG(dbgs() << "          to: " << *NewSI << "\n");
    return NewSI->getPointerOperand() == &NewAI && !SI.isVolatile();
  }

  /// Compute an integer value from splatting an i8 across the given
  /// number of bytes.
  ///
  /// Note that this routine assumes an i8 is a byte. If that isn't true, don't
  /// call this routine.
  /// FIXME: Heed the advice above.
  ///
  /// \param V The i8 value to splat.
  /// \param Size The number of bytes in the output (assuming i8 is one byte)
  Value *getIntegerSplat(Value *V, unsigned Size) {
    assert(Size > 0 && "Expected a positive number of bytes.");
    IntegerType *VTy = cast<IntegerType>(V->getType());
    assert(VTy->getBitWidth() == 8 && "Expected an i8 value for the byte");
    if (Size == 1)
      return V;

    Type *SplatIntTy = Type::getIntNTy(VTy->getContext(), Size * 8);
    V = IRB.CreateMul(
        IRB.CreateZExt(V, SplatIntTy, "zext"),
        ConstantExpr::getUDiv(
            Constant::getAllOnesValue(SplatIntTy),
            ConstantExpr::getZExt(Constant::getAllOnesValue(V->getType()),
                                  SplatIntTy)),
        "isplat");
    return V;
  }

  /// Compute a vector splat for a given element value.
  Value *getVectorSplat(Value *V, unsigned NumElements) {
    V = IRB.CreateVectorSplat(NumElements, V, "vsplat");
    LLVM_DEBUG(dbgs() << "       splat: " << *V << "\n");
    return V;
  }

  bool visitMemSetInst(MemSetInst &II) {
    LLVM_DEBUG(dbgs() << "    original: " << II << "\n");
    assert(II.getRawDest() == OldPtr);

    AAMDNodes AATags;
    II.getAAMetadata(AATags);

    // If the memset has a variable size, it cannot be split, just adjust the
    // pointer to the new alloca.
    if (!isa<Constant>(II.getLength())) {
      assert(!IsSplit);
      assert(NewBeginOffset == BeginOffset);
      II.setDest(getNewAllocaSlicePtr(IRB, OldPtr->getType()));
      II.setDestAlignment(getSliceAlign());

      deleteIfTriviallyDead(OldPtr);
      return false;
    }

    // Record this instruction for deletion.
    Pass.DeadInsts.insert(&II);

    Type *AllocaTy = NewAI.getAllocatedType();
    Type *ScalarTy = AllocaTy->getScalarType();

    // If this doesn't map cleanly onto the alloca type, and that type isn't
    // a single value type, just emit a memset.
    if (!VecTy && !IntTy &&
        (BeginOffset > NewAllocaBeginOffset || EndOffset < NewAllocaEndOffset ||
         SliceSize != DL.getTypeStoreSize(AllocaTy) ||
         !AllocaTy->isSingleValueType() ||
         !DL.isLegalInteger(DL.getTypeSizeInBits(ScalarTy)) ||
         DL.getTypeSizeInBits(ScalarTy) % 8 != 0)) {
      Type *SizeTy = II.getLength()->getType();
      Constant *Size = ConstantInt::get(SizeTy, NewEndOffset - NewBeginOffset);
      CallInst *New = IRB.CreateMemSet(
          getNewAllocaSlicePtr(IRB, OldPtr->getType()), II.getValue(), Size,
          getSliceAlign(), II.isVolatile());
      if (AATags)
        New->setAAMetadata(AATags);
      LLVM_DEBUG(dbgs() << "          to: " << *New << "\n");
      return false;
    }

    // If we can represent this as a simple value, we have to build the actual
    // value to store, which requires expanding the byte present in memset to
    // a sensible representation for the alloca type. This is essentially
    // splatting the byte to a sufficiently wide integer, splatting it across
    // any desired vector width, and bitcasting to the final type.
    Value *V;

    if (VecTy) {
      // If this is a memset of a vectorized alloca, insert it.
      assert(ElementTy == ScalarTy);

      unsigned BeginIndex = getIndex(NewBeginOffset);
      unsigned EndIndex = getIndex(NewEndOffset);
      assert(EndIndex > BeginIndex && "Empty vector!");
      unsigned NumElements = EndIndex - BeginIndex;
      assert(NumElements <= VecTy->getNumElements() && "Too many elements!");

      Value *Splat =
          getIntegerSplat(II.getValue(), DL.getTypeSizeInBits(ElementTy) / 8);
      Splat = convertValue(DL, IRB, Splat, ElementTy);
      if (NumElements > 1)
        Splat = getVectorSplat(Splat, NumElements);

      Value *Old =
          IRB.CreateAlignedLoad(&NewAI, NewAI.getAlignment(), "oldload");
      V = insertVector(IRB, Old, Splat, BeginIndex, "vec");
    } else if (IntTy) {
      // If this is a memset on an alloca where we can widen stores, insert the
      // set integer.
      assert(!II.isVolatile());

      uint64_t Size = NewEndOffset - NewBeginOffset;
      V = getIntegerSplat(II.getValue(), Size);

      if (IntTy && (BeginOffset != NewAllocaBeginOffset ||
                    EndOffset != NewAllocaBeginOffset)) {
        Value *Old =
            IRB.CreateAlignedLoad(&NewAI, NewAI.getAlignment(), "oldload");
        Old = convertValue(DL, IRB, Old, IntTy);
        uint64_t Offset = NewBeginOffset - NewAllocaBeginOffset;
        V = insertInteger(DL, IRB, Old, V, Offset, "insert");
      } else {
        assert(V->getType() == IntTy &&
               "Wrong type for an alloca wide integer!");
      }
      V = convertValue(DL, IRB, V, AllocaTy);
    } else {
      // Established these invariants above.
      assert(NewBeginOffset == NewAllocaBeginOffset);
      assert(NewEndOffset == NewAllocaEndOffset);

      V = getIntegerSplat(II.getValue(), DL.getTypeSizeInBits(ScalarTy) / 8);
      if (VectorType *AllocaVecTy = dyn_cast<VectorType>(AllocaTy))
        V = getVectorSplat(V, AllocaVecTy->getNumElements());

      V = convertValue(DL, IRB, V, AllocaTy);
    }

    StoreInst *New = IRB.CreateAlignedStore(V, &NewAI, NewAI.getAlignment(),
                                            II.isVolatile());
    if (AATags)
      New->setAAMetadata(AATags);
    LLVM_DEBUG(dbgs() << "          to: " << *New << "\n");
    return !II.isVolatile();
  }

  bool visitMemTransferInst(MemTransferInst &II) {
    // Rewriting of memory transfer instructions can be a bit tricky. We break
    // them into two categories: split intrinsics and unsplit intrinsics.

    LLVM_DEBUG(dbgs() << "    original: " << II << "\n");

    AAMDNodes AATags;
    II.getAAMetadata(AATags);

    bool IsDest = &II.getRawDestUse() == OldUse;
    assert((IsDest && II.getRawDest() == OldPtr) ||
           (!IsDest && II.getRawSource() == OldPtr));

    unsigned SliceAlign = getSliceAlign();

    // For unsplit intrinsics, we simply modify the source and destination
    // pointers in place. This isn't just an optimization, it is a matter of
    // correctness. With unsplit intrinsics we may be dealing with transfers
    // within a single alloca before SROA ran, or with transfers that have
    // a variable length. We may also be dealing with memmove instead of
    // memcpy, and so simply updating the pointers is the necessary for us to
    // update both source and dest of a single call.
    if (!IsSplittable) {
      Value *AdjustedPtr = getNewAllocaSlicePtr(IRB, OldPtr->getType());
      if (IsDest) {
        II.setDest(AdjustedPtr);
        II.setDestAlignment(SliceAlign);
      }
      else {
        II.setSource(AdjustedPtr);
        II.setSourceAlignment(SliceAlign);
      }

      LLVM_DEBUG(dbgs() << "          to: " << II << "\n");
      deleteIfTriviallyDead(OldPtr);
      return false;
    }
    // For split transfer intrinsics we have an incredibly useful assurance:
    // the source and destination do not reside within the same alloca, and at
    // least one of them does not escape. This means that we can replace
    // memmove with memcpy, and we don't need to worry about all manner of
    // downsides to splitting and transforming the operations.

    // If this doesn't map cleanly onto the alloca type, and that type isn't
    // a single value type, just emit a memcpy.
    bool EmitMemCpy =
        !VecTy && !IntTy &&
        (BeginOffset > NewAllocaBeginOffset || EndOffset < NewAllocaEndOffset ||
         SliceSize != DL.getTypeStoreSize(NewAI.getAllocatedType()) ||
         !NewAI.getAllocatedType()->isSingleValueType());

    // If we're just going to emit a memcpy, the alloca hasn't changed, and the
    // size hasn't been shrunk based on analysis of the viable range, this is
    // a no-op.
    if (EmitMemCpy && &OldAI == &NewAI) {
      // Ensure the start lines up.
      assert(NewBeginOffset == BeginOffset);

      // Rewrite the size as needed.
      if (NewEndOffset != EndOffset)
        II.setLength(ConstantInt::get(II.getLength()->getType(),
                                      NewEndOffset - NewBeginOffset));
      return false;
    }
    // Record this instruction for deletion.
    Pass.DeadInsts.insert(&II);

    // Strip all inbounds GEPs and pointer casts to try to dig out any root
    // alloca that should be re-examined after rewriting this instruction.
    Value *OtherPtr = IsDest ? II.getRawSource() : II.getRawDest();
    if (AllocaInst *AI =
            dyn_cast<AllocaInst>(OtherPtr->stripInBoundsOffsets())) {
      assert(AI != &OldAI && AI != &NewAI &&
             "Splittable transfers cannot reach the same alloca on both ends.");
      Pass.Worklist.insert(AI);
    }

    Type *OtherPtrTy = OtherPtr->getType();
    unsigned OtherAS = OtherPtrTy->getPointerAddressSpace();

    // Compute the relative offset for the other pointer within the transfer.
    unsigned OffsetWidth = DL.getIndexSizeInBits(OtherAS);
    APInt OtherOffset(OffsetWidth, NewBeginOffset - BeginOffset);
    unsigned OtherAlign =
      IsDest ? II.getSourceAlignment() : II.getDestAlignment();
    OtherAlign =  MinAlign(OtherAlign ? OtherAlign : 1,
                           OtherOffset.zextOrTrunc(64).getZExtValue());

    if (EmitMemCpy) {
      // Compute the other pointer, folding as much as possible to produce
      // a single, simple GEP in most cases.
      OtherPtr = getAdjustedPtr(IRB, DL, OtherPtr, OtherOffset, OtherPtrTy,
                                OtherPtr->getName() + ".");

      Value *OurPtr = getNewAllocaSlicePtr(IRB, OldPtr->getType());
      Type *SizeTy = II.getLength()->getType();
      Constant *Size = ConstantInt::get(SizeTy, NewEndOffset - NewBeginOffset);

      Value *DestPtr, *SrcPtr;
      unsigned DestAlign, SrcAlign;
      // Note: IsDest is true iff we're copying into the new alloca slice
      if (IsDest) {
        DestPtr = OurPtr;
        DestAlign = SliceAlign;
        SrcPtr = OtherPtr;
        SrcAlign = OtherAlign;
      } else {
        DestPtr = OtherPtr;
        DestAlign = OtherAlign;
        SrcPtr = OurPtr;
        SrcAlign = SliceAlign;
      }
      CallInst *New = IRB.CreateMemCpy(DestPtr, DestAlign, SrcPtr, SrcAlign,
                                       Size, II.isVolatile());
      if (AATags)
        New->setAAMetadata(AATags);
      LLVM_DEBUG(dbgs() << "          to: " << *New << "\n");
      return false;
    }

    bool IsWholeAlloca = NewBeginOffset == NewAllocaBeginOffset &&
                         NewEndOffset == NewAllocaEndOffset;
    uint64_t Size = NewEndOffset - NewBeginOffset;
    unsigned BeginIndex = VecTy ? getIndex(NewBeginOffset) : 0;
    unsigned EndIndex = VecTy ? getIndex(NewEndOffset) : 0;
    unsigned NumElements = EndIndex - BeginIndex;
    IntegerType *SubIntTy =
        IntTy ? Type::getIntNTy(IntTy->getContext(), Size * 8) : nullptr;

    // Reset the other pointer type to match the register type we're going to
    // use, but using the address space of the original other pointer.
    if (VecTy && !IsWholeAlloca) {
      if (NumElements == 1)
        OtherPtrTy = VecTy->getElementType();
      else
        OtherPtrTy = VectorType::get(VecTy->getElementType(), NumElements);

      OtherPtrTy = OtherPtrTy->getPointerTo(OtherAS);
    } else if (IntTy && !IsWholeAlloca) {
      OtherPtrTy = SubIntTy->getPointerTo(OtherAS);
    } else {
      OtherPtrTy = NewAllocaTy->getPointerTo(OtherAS);
    }

    Value *SrcPtr = getAdjustedPtr(IRB, DL, OtherPtr, OtherOffset, OtherPtrTy,
                                   OtherPtr->getName() + ".");
    unsigned SrcAlign = OtherAlign;
    Value *DstPtr = &NewAI;
    unsigned DstAlign = SliceAlign;
    if (!IsDest) {
      std::swap(SrcPtr, DstPtr);
      std::swap(SrcAlign, DstAlign);
    }

    Value *Src;
    if (VecTy && !IsWholeAlloca && !IsDest) {
      Src = IRB.CreateAlignedLoad(&NewAI, NewAI.getAlignment(), "load");
      Src = extractVector(IRB, Src, BeginIndex, EndIndex, "vec");
    } else if (IntTy && !IsWholeAlloca && !IsDest) {
      Src = IRB.CreateAlignedLoad(&NewAI, NewAI.getAlignment(), "load");
      Src = convertValue(DL, IRB, Src, IntTy);
      uint64_t Offset = NewBeginOffset - NewAllocaBeginOffset;
      Src = extractInteger(DL, IRB, Src, SubIntTy, Offset, "extract");
    } else {
      LoadInst *Load = IRB.CreateAlignedLoad(SrcPtr, SrcAlign, II.isVolatile(),
                                             "copyload");
      if (AATags)
        Load->setAAMetadata(AATags);
      Src = Load;
    }

    if (VecTy && !IsWholeAlloca && IsDest) {
      Value *Old =
          IRB.CreateAlignedLoad(&NewAI, NewAI.getAlignment(), "oldload");
      Src = insertVector(IRB, Old, Src, BeginIndex, "vec");
    } else if (IntTy && !IsWholeAlloca && IsDest) {
      Value *Old =
          IRB.CreateAlignedLoad(&NewAI, NewAI.getAlignment(), "oldload");
      Old = convertValue(DL, IRB, Old, IntTy);
      uint64_t Offset = NewBeginOffset - NewAllocaBeginOffset;
      Src = insertInteger(DL, IRB, Old, Src, Offset, "insert");
      Src = convertValue(DL, IRB, Src, NewAllocaTy);
    }

    StoreInst *Store = cast<StoreInst>(
        IRB.CreateAlignedStore(Src, DstPtr, DstAlign, II.isVolatile()));
    if (AATags)
      Store->setAAMetadata(AATags);
    LLVM_DEBUG(dbgs() << "          to: " << *Store << "\n");
    return !II.isVolatile();
  }

  bool visitIntrinsicInst(IntrinsicInst &II) {
    assert(II.isLifetimeStartOrEnd());
    LLVM_DEBUG(dbgs() << "    original: " << II << "\n");
    assert(II.getArgOperand(1) == OldPtr);

    // Record this instruction for deletion.
    Pass.DeadInsts.insert(&II);

    // Lifetime intrinsics are only promotable if they cover the whole alloca.
    // Therefore, we drop lifetime intrinsics which don't cover the whole
    // alloca.
    // (In theory, intrinsics which partially cover an alloca could be
    // promoted, but PromoteMemToReg doesn't handle that case.)
    // FIXME: Check whether the alloca is promotable before dropping the
    // lifetime intrinsics?
    if (NewBeginOffset != NewAllocaBeginOffset ||
        NewEndOffset != NewAllocaEndOffset)
      return true;

    ConstantInt *Size =
        ConstantInt::get(cast<IntegerType>(II.getArgOperand(0)->getType()),
                         NewEndOffset - NewBeginOffset);
    // Lifetime intrinsics always expect an i8* so directly get such a pointer
    // for the new alloca slice.
    Type *PointerTy = IRB.getInt8PtrTy(OldPtr->getType()->getPointerAddressSpace());
    Value *Ptr = getNewAllocaSlicePtr(IRB, PointerTy);
    Value *New;
    if (II.getIntrinsicID() == Intrinsic::lifetime_start)
      New = IRB.CreateLifetimeStart(Ptr, Size);
    else
      New = IRB.CreateLifetimeEnd(Ptr, Size);

    (void)New;
    LLVM_DEBUG(dbgs() << "          to: " << *New << "\n");

    return true;
  }

  void fixLoadStoreAlign(Instruction &Root) {
    // This algorithm implements the same visitor loop as
    // hasUnsafePHIOrSelectUse, and fixes the alignment of each load
    // or store found.
    SmallPtrSet<Instruction *, 4> Visited;
    SmallVector<Instruction *, 4> Uses;
    Visited.insert(&Root);
    Uses.push_back(&Root);
    do {
      Instruction *I = Uses.pop_back_val();

      if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
        unsigned LoadAlign = LI->getAlignment();
        if (!LoadAlign)
          LoadAlign = DL.getABITypeAlignment(LI->getType());
        LI->setAlignment(std::min(LoadAlign, getSliceAlign()));
        continue;
      }
      if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
        unsigned StoreAlign = SI->getAlignment();
        if (!StoreAlign) {
          Value *Op = SI->getOperand(0);
          StoreAlign = DL.getABITypeAlignment(Op->getType());
        }
        SI->setAlignment(std::min(StoreAlign, getSliceAlign()));
        continue;
      }

      assert(isa<BitCastInst>(I) || isa<PHINode>(I) ||
             isa<SelectInst>(I) || isa<GetElementPtrInst>(I));
      for (User *U : I->users())
        if (Visited.insert(cast<Instruction>(U)).second)
          Uses.push_back(cast<Instruction>(U));
    } while (!Uses.empty());
  }

  bool visitPHINode(PHINode &PN) {
    LLVM_DEBUG(dbgs() << "    original: " << PN << "\n");
    assert(BeginOffset >= NewAllocaBeginOffset && "PHIs are unsplittable");
    assert(EndOffset <= NewAllocaEndOffset && "PHIs are unsplittable");

    // We would like to compute a new pointer in only one place, but have it be
    // as local as possible to the PHI. To do that, we re-use the location of
    // the old pointer, which necessarily must be in the right position to
    // dominate the PHI.
    IRBuilderTy PtrBuilder(IRB);
    if (isa<PHINode>(OldPtr))
      PtrBuilder.SetInsertPoint(&*OldPtr->getParent()->getFirstInsertionPt());
    else
      PtrBuilder.SetInsertPoint(OldPtr);
    PtrBuilder.SetCurrentDebugLocation(OldPtr->getDebugLoc());

    Value *NewPtr = getNewAllocaSlicePtr(PtrBuilder, OldPtr->getType());
    // Replace the operands which were using the old pointer.
    std::replace(PN.op_begin(), PN.op_end(), cast<Value>(OldPtr), NewPtr);

    LLVM_DEBUG(dbgs() << "          to: " << PN << "\n");
    deleteIfTriviallyDead(OldPtr);

    // Fix the alignment of any loads or stores using this PHI node.
    fixLoadStoreAlign(PN);

    // PHIs can't be promoted on their own, but often can be speculated. We
    // check the speculation outside of the rewriter so that we see the
    // fully-rewritten alloca.
    PHIUsers.insert(&PN);
    return true;
  }

  bool visitSelectInst(SelectInst &SI) {
    LLVM_DEBUG(dbgs() << "    original: " << SI << "\n");
    assert((SI.getTrueValue() == OldPtr || SI.getFalseValue() == OldPtr) &&
           "Pointer isn't an operand!");
    assert(BeginOffset >= NewAllocaBeginOffset && "Selects are unsplittable");
    assert(EndOffset <= NewAllocaEndOffset && "Selects are unsplittable");

    Value *NewPtr = getNewAllocaSlicePtr(IRB, OldPtr->getType());
    // Replace the operands which were using the old pointer.
    if (SI.getOperand(1) == OldPtr)
      SI.setOperand(1, NewPtr);
    if (SI.getOperand(2) == OldPtr)
      SI.setOperand(2, NewPtr);

    LLVM_DEBUG(dbgs() << "          to: " << SI << "\n");
    deleteIfTriviallyDead(OldPtr);

    // Fix the alignment of any loads or stores using this select.
    fixLoadStoreAlign(SI);

    // Selects can't be promoted on their own, but often can be speculated. We
    // check the speculation outside of the rewriter so that we see the
    // fully-rewritten alloca.
    SelectUsers.insert(&SI);
    return true;
  }
};

namespace {

/// Visitor to rewrite aggregate loads and stores as scalar.
///
/// This pass aggressively rewrites all aggregate loads and stores on
/// a particular pointer (or any pointer derived from it which we can identify)
/// with scalar loads and stores.
class AggLoadStoreRewriter : public InstVisitor<AggLoadStoreRewriter, bool> {
  // Befriend the base class so it can delegate to private visit methods.
  friend class InstVisitor<AggLoadStoreRewriter, bool>;

  /// Queue of pointer uses to analyze and potentially rewrite.
  SmallVector<Use *, 8> Queue;

  /// Set to prevent us from cycling with phi nodes and loops.
  SmallPtrSet<User *, 8> Visited;

  /// The current pointer use being rewritten. This is used to dig up the used
  /// value (as opposed to the user).
  Use *U;

  /// Used to calculate offsets, and hence alignment, of subobjects.
  const DataLayout &DL;

public:
  AggLoadStoreRewriter(const DataLayout &DL) : DL(DL) {}

  /// Rewrite loads and stores through a pointer and all pointers derived from
  /// it.
  bool rewrite(Instruction &I) {
    LLVM_DEBUG(dbgs() << "  Rewriting FCA loads and stores...\n");
    enqueueUsers(I);
    bool Changed = false;
    while (!Queue.empty()) {
      U = Queue.pop_back_val();
      Changed |= visit(cast<Instruction>(U->getUser()));
    }
    return Changed;
  }

private:
  /// Enqueue all the users of the given instruction for further processing.
  /// This uses a set to de-duplicate users.
  void enqueueUsers(Instruction &I) {
    for (Use &U : I.uses())
      if (Visited.insert(U.getUser()).second)
        Queue.push_back(&U);
  }

  // Conservative default is to not rewrite anything.
  bool visitInstruction(Instruction &I) { return false; }

  /// Generic recursive split emission class.
  template <typename Derived> class OpSplitter {
  protected:
    /// The builder used to form new instructions.
    IRBuilderTy IRB;

    /// The indices which to be used with insert- or extractvalue to select the
    /// appropriate value within the aggregate.
    SmallVector<unsigned, 4> Indices;

    /// The indices to a GEP instruction which will move Ptr to the correct slot
    /// within the aggregate.
    SmallVector<Value *, 4> GEPIndices;

    /// The base pointer of the original op, used as a base for GEPing the
    /// split operations.
    Value *Ptr;

    /// The base pointee type being GEPed into.
    Type *BaseTy;

    /// Known alignment of the base pointer.
    unsigned BaseAlign;

    /// To calculate offset of each component so we can correctly deduce
    /// alignments.
    const DataLayout &DL;

    /// Initialize the splitter with an insertion point, Ptr and start with a
    /// single zero GEP index.
    OpSplitter(Instruction *InsertionPoint, Value *Ptr, Type *BaseTy,
               unsigned BaseAlign, const DataLayout &DL)
        : IRB(InsertionPoint), GEPIndices(1, IRB.getInt32(0)), Ptr(Ptr),
          BaseTy(BaseTy), BaseAlign(BaseAlign), DL(DL) {}

  public:
    /// Generic recursive split emission routine.
    ///
    /// This method recursively splits an aggregate op (load or store) into
    /// scalar or vector ops. It splits recursively until it hits a single value
    /// and emits that single value operation via the template argument.
    ///
    /// The logic of this routine relies on GEPs and insertvalue and
    /// extractvalue all operating with the same fundamental index list, merely
    /// formatted differently (GEPs need actual values).
    ///
    /// \param Ty  The type being split recursively into smaller ops.
    /// \param Agg The aggregate value being built up or stored, depending on
    /// whether this is splitting a load or a store respectively.
    void emitSplitOps(Type *Ty, Value *&Agg, const Twine &Name) {
      if (Ty->isSingleValueType()) {
        unsigned Offset = DL.getIndexedOffsetInType(BaseTy, GEPIndices);
        return static_cast<Derived *>(this)->emitFunc(
            Ty, Agg, MinAlign(BaseAlign, Offset), Name);
      }

      if (ArrayType *ATy = dyn_cast<ArrayType>(Ty)) {
        unsigned OldSize = Indices.size();
        (void)OldSize;
        for (unsigned Idx = 0, Size = ATy->getNumElements(); Idx != Size;
             ++Idx) {
          assert(Indices.size() == OldSize && "Did not return to the old size");
          Indices.push_back(Idx);
          GEPIndices.push_back(IRB.getInt32(Idx));
          emitSplitOps(ATy->getElementType(), Agg, Name + "." + Twine(Idx));
          GEPIndices.pop_back();
          Indices.pop_back();
        }
        return;
      }

      if (StructType *STy = dyn_cast<StructType>(Ty)) {
        unsigned OldSize = Indices.size();
        (void)OldSize;
        for (unsigned Idx = 0, Size = STy->getNumElements(); Idx != Size;
             ++Idx) {
          assert(Indices.size() == OldSize && "Did not return to the old size");
          Indices.push_back(Idx);
          GEPIndices.push_back(IRB.getInt32(Idx));
          emitSplitOps(STy->getElementType(Idx), Agg, Name + "." + Twine(Idx));
          GEPIndices.pop_back();
          Indices.pop_back();
        }
        return;
      }

      llvm_unreachable("Only arrays and structs are aggregate loadable types");
    }
  };

  struct LoadOpSplitter : public OpSplitter<LoadOpSplitter> {
    AAMDNodes AATags;

    LoadOpSplitter(Instruction *InsertionPoint, Value *Ptr, Type *BaseTy,
                   AAMDNodes AATags, unsigned BaseAlign, const DataLayout &DL)
        : OpSplitter<LoadOpSplitter>(InsertionPoint, Ptr, BaseTy, BaseAlign,
                                     DL), AATags(AATags) {}

    /// Emit a leaf load of a single value. This is called at the leaves of the
    /// recursive emission to actually load values.
    void emitFunc(Type *Ty, Value *&Agg, unsigned Align, const Twine &Name) {
      assert(Ty->isSingleValueType());
      // Load the single value and insert it using the indices.
      Value *GEP =
          IRB.CreateInBoundsGEP(nullptr, Ptr, GEPIndices, Name + ".gep");
      LoadInst *Load = IRB.CreateAlignedLoad(GEP, Align, Name + ".load");
      if (AATags)
        Load->setAAMetadata(AATags);
      Agg = IRB.CreateInsertValue(Agg, Load, Indices, Name + ".insert");
      LLVM_DEBUG(dbgs() << "          to: " << *Load << "\n");
    }
  };

  bool visitLoadInst(LoadInst &LI) {
    assert(LI.getPointerOperand() == *U);
    if (!LI.isSimple() || LI.getType()->isSingleValueType())
      return false;

    // We have an aggregate being loaded, split it apart.
    LLVM_DEBUG(dbgs() << "    original: " << LI << "\n");
    AAMDNodes AATags;
    LI.getAAMetadata(AATags);
    LoadOpSplitter Splitter(&LI, *U, LI.getType(), AATags,
                            getAdjustedAlignment(&LI, 0, DL), DL);
    Value *V = UndefValue::get(LI.getType());
    Splitter.emitSplitOps(LI.getType(), V, LI.getName() + ".fca");
    LI.replaceAllUsesWith(V);
    LI.eraseFromParent();
    return true;
  }

  struct StoreOpSplitter : public OpSplitter<StoreOpSplitter> {
    StoreOpSplitter(Instruction *InsertionPoint, Value *Ptr, Type *BaseTy,
                    AAMDNodes AATags, unsigned BaseAlign, const DataLayout &DL)
        : OpSplitter<StoreOpSplitter>(InsertionPoint, Ptr, BaseTy, BaseAlign,
                                      DL),
          AATags(AATags) {}
    AAMDNodes AATags;
    /// Emit a leaf store of a single value. This is called at the leaves of the
    /// recursive emission to actually produce stores.
    void emitFunc(Type *Ty, Value *&Agg, unsigned Align, const Twine &Name) {
      assert(Ty->isSingleValueType());
      // Extract the single value and store it using the indices.
      //
      // The gep and extractvalue values are factored out of the CreateStore
      // call to make the output independent of the argument evaluation order.
      Value *ExtractValue =
          IRB.CreateExtractValue(Agg, Indices, Name + ".extract");
      Value *InBoundsGEP =
          IRB.CreateInBoundsGEP(nullptr, Ptr, GEPIndices, Name + ".gep");
      StoreInst *Store =
          IRB.CreateAlignedStore(ExtractValue, InBoundsGEP, Align);
      if (AATags)
        Store->setAAMetadata(AATags);
      LLVM_DEBUG(dbgs() << "          to: " << *Store << "\n");
    }
  };

  bool visitStoreInst(StoreInst &SI) {
    if (!SI.isSimple() || SI.getPointerOperand() != *U)
      return false;
    Value *V = SI.getValueOperand();
    if (V->getType()->isSingleValueType())
      return false;

    // We have an aggregate being stored, split it apart.
    LLVM_DEBUG(dbgs() << "    original: " << SI << "\n");
    AAMDNodes AATags;
    SI.getAAMetadata(AATags);
    StoreOpSplitter Splitter(&SI, *U, V->getType(), AATags,
                             getAdjustedAlignment(&SI, 0, DL), DL);
    Splitter.emitSplitOps(V->getType(), V, V->getName() + ".fca");
    SI.eraseFromParent();
    return true;
  }

  bool visitBitCastInst(BitCastInst &BC) {
    enqueueUsers(BC);
    return false;
  }

  bool visitGetElementPtrInst(GetElementPtrInst &GEPI) {
    enqueueUsers(GEPI);
    return false;
  }

  bool visitPHINode(PHINode &PN) {
    enqueueUsers(PN);
    return false;
  }

  bool visitSelectInst(SelectInst &SI) {
    enqueueUsers(SI);
    return false;
  }
};

} // end anonymous namespace

/// Strip aggregate type wrapping.
///
/// This removes no-op aggregate types wrapping an underlying type. It will
/// strip as many layers of types as it can without changing either the type
/// size or the allocated size.
static Type *stripAggregateTypeWrapping(const DataLayout &DL, Type *Ty) {
  if (Ty->isSingleValueType())
    return Ty;

  uint64_t AllocSize = DL.getTypeAllocSize(Ty);
  uint64_t TypeSize = DL.getTypeSizeInBits(Ty);

  Type *InnerTy;
  if (ArrayType *ArrTy = dyn_cast<ArrayType>(Ty)) {
    InnerTy = ArrTy->getElementType();
  } else if (StructType *STy = dyn_cast<StructType>(Ty)) {
    const StructLayout *SL = DL.getStructLayout(STy);
    unsigned Index = SL->getElementContainingOffset(0);
    InnerTy = STy->getElementType(Index);
  } else {
    return Ty;
  }

  if (AllocSize > DL.getTypeAllocSize(InnerTy) ||
      TypeSize > DL.getTypeSizeInBits(InnerTy))
    return Ty;

  return stripAggregateTypeWrapping(DL, InnerTy);
}

/// Try to find a partition of the aggregate type passed in for a given
/// offset and size.
///
/// This recurses through the aggregate type and tries to compute a subtype
/// based on the offset and size. When the offset and size span a sub-section
/// of an array, it will even compute a new array type for that sub-section,
/// and the same for structs.
///
/// Note that this routine is very strict and tries to find a partition of the
/// type which produces the *exact* right offset and size. It is not forgiving
/// when the size or offset cause either end of type-based partition to be off.
/// Also, this is a best-effort routine. It is reasonable to give up and not
/// return a type if necessary.
static Type *getTypePartition(const DataLayout &DL, Type *Ty, uint64_t Offset,
                              uint64_t Size) {
  if (Offset == 0 && DL.getTypeAllocSize(Ty) == Size)
    return stripAggregateTypeWrapping(DL, Ty);
  if (Offset > DL.getTypeAllocSize(Ty) ||
      (DL.getTypeAllocSize(Ty) - Offset) < Size)
    return nullptr;

  if (SequentialType *SeqTy = dyn_cast<SequentialType>(Ty)) {
    Type *ElementTy = SeqTy->getElementType();
    uint64_t ElementSize = DL.getTypeAllocSize(ElementTy);
    uint64_t NumSkippedElements = Offset / ElementSize;
    if (NumSkippedElements >= SeqTy->getNumElements())
      return nullptr;
    Offset -= NumSkippedElements * ElementSize;

    // First check if we need to recurse.
    if (Offset > 0 || Size < ElementSize) {
      // Bail if the partition ends in a different array element.
      if ((Offset + Size) > ElementSize)
        return nullptr;
      // Recurse through the element type trying to peel off offset bytes.
      return getTypePartition(DL, ElementTy, Offset, Size);
    }
    assert(Offset == 0);

    if (Size == ElementSize)
      return stripAggregateTypeWrapping(DL, ElementTy);
    assert(Size > ElementSize);
    uint64_t NumElements = Size / ElementSize;
    if (NumElements * ElementSize != Size)
      return nullptr;
    return ArrayType::get(ElementTy, NumElements);
  }

  StructType *STy = dyn_cast<StructType>(Ty);
  if (!STy)
    return nullptr;

  const StructLayout *SL = DL.getStructLayout(STy);
  if (Offset >= SL->getSizeInBytes())
    return nullptr;
  uint64_t EndOffset = Offset + Size;
  if (EndOffset > SL->getSizeInBytes())
    return nullptr;

  unsigned Index = SL->getElementContainingOffset(Offset);
  Offset -= SL->getElementOffset(Index);

  Type *ElementTy = STy->getElementType(Index);
  uint64_t ElementSize = DL.getTypeAllocSize(ElementTy);
  if (Offset >= ElementSize)
    return nullptr; // The offset points into alignment padding.

  // See if any partition must be contained by the element.
  if (Offset > 0 || Size < ElementSize) {
    if ((Offset + Size) > ElementSize)
      return nullptr;
    return getTypePartition(DL, ElementTy, Offset, Size);
  }
  assert(Offset == 0);

  if (Size == ElementSize)
    return stripAggregateTypeWrapping(DL, ElementTy);

  StructType::element_iterator EI = STy->element_begin() + Index,
                               EE = STy->element_end();
  if (EndOffset < SL->getSizeInBytes()) {
    unsigned EndIndex = SL->getElementContainingOffset(EndOffset);
    if (Index == EndIndex)
      return nullptr; // Within a single element and its padding.

    // Don't try to form "natural" types if the elements don't line up with the
    // expected size.
    // FIXME: We could potentially recurse down through the last element in the
    // sub-struct to find a natural end point.
    if (SL->getElementOffset(EndIndex) != EndOffset)
      return nullptr;

    assert(Index < EndIndex);
    EE = STy->element_begin() + EndIndex;
  }

  // Try to build up a sub-structure.
  StructType *SubTy =
      StructType::get(STy->getContext(), makeArrayRef(EI, EE), STy->isPacked());
  const StructLayout *SubSL = DL.getStructLayout(SubTy);
  if (Size != SubSL->getSizeInBytes())
    return nullptr; // The sub-struct doesn't have quite the size needed.

  return SubTy;
}

/// Pre-split loads and stores to simplify rewriting.
///
/// We want to break up the splittable load+store pairs as much as
/// possible. This is important to do as a preprocessing step, as once we
/// start rewriting the accesses to partitions of the alloca we lose the
/// necessary information to correctly split apart paired loads and stores
/// which both point into this alloca. The case to consider is something like
/// the following:
///
///   %a = alloca [12 x i8]
///   %gep1 = getelementptr [12 x i8]* %a, i32 0, i32 0
///   %gep2 = getelementptr [12 x i8]* %a, i32 0, i32 4
///   %gep3 = getelementptr [12 x i8]* %a, i32 0, i32 8
///   %iptr1 = bitcast i8* %gep1 to i64*
///   %iptr2 = bitcast i8* %gep2 to i64*
///   %fptr1 = bitcast i8* %gep1 to float*
///   %fptr2 = bitcast i8* %gep2 to float*
///   %fptr3 = bitcast i8* %gep3 to float*
///   store float 0.0, float* %fptr1
///   store float 1.0, float* %fptr2
///   %v = load i64* %iptr1
///   store i64 %v, i64* %iptr2
///   %f1 = load float* %fptr2
///   %f2 = load float* %fptr3
///
/// Here we want to form 3 partitions of the alloca, each 4 bytes large, and
/// promote everything so we recover the 2 SSA values that should have been
/// there all along.
///
/// \returns true if any changes are made.
bool SROA::presplitLoadsAndStores(AllocaInst &AI, AllocaSlices &AS) {
  LLVM_DEBUG(dbgs() << "Pre-splitting loads and stores\n");

  // Track the loads and stores which are candidates for pre-splitting here, in
  // the order they first appear during the partition scan. These give stable
  // iteration order and a basis for tracking which loads and stores we
  // actually split.
  SmallVector<LoadInst *, 4> Loads;
  SmallVector<StoreInst *, 4> Stores;

  // We need to accumulate the splits required of each load or store where we
  // can find them via a direct lookup. This is important to cross-check loads
  // and stores against each other. We also track the slice so that we can kill
  // all the slices that end up split.
  struct SplitOffsets {
    Slice *S;
    std::vector<uint64_t> Splits;
  };
  SmallDenseMap<Instruction *, SplitOffsets, 8> SplitOffsetsMap;

  // Track loads out of this alloca which cannot, for any reason, be pre-split.
  // This is important as we also cannot pre-split stores of those loads!
  // FIXME: This is all pretty gross. It means that we can be more aggressive
  // in pre-splitting when the load feeding the store happens to come from
  // a separate alloca. Put another way, the effectiveness of SROA would be
  // decreased by a frontend which just concatenated all of its local allocas
  // into one big flat alloca. But defeating such patterns is exactly the job
  // SROA is tasked with! Sadly, to not have this discrepancy we would have
  // change store pre-splitting to actually force pre-splitting of the load
  // that feeds it *and all stores*. That makes pre-splitting much harder, but
  // maybe it would make it more principled?
  SmallPtrSet<LoadInst *, 8> UnsplittableLoads;

  LLVM_DEBUG(dbgs() << "  Searching for candidate loads and stores\n");
  for (auto &P : AS.partitions()) {
    for (Slice &S : P) {
      Instruction *I = cast<Instruction>(S.getUse()->getUser());
      if (!S.isSplittable() || S.endOffset() <= P.endOffset()) {
        // If this is a load we have to track that it can't participate in any
        // pre-splitting. If this is a store of a load we have to track that
        // that load also can't participate in any pre-splitting.
        if (auto *LI = dyn_cast<LoadInst>(I))
          UnsplittableLoads.insert(LI);
        else if (auto *SI = dyn_cast<StoreInst>(I))
          if (auto *LI = dyn_cast<LoadInst>(SI->getValueOperand()))
            UnsplittableLoads.insert(LI);
        continue;
      }
      assert(P.endOffset() > S.beginOffset() &&
             "Empty or backwards partition!");

      // Determine if this is a pre-splittable slice.
      if (auto *LI = dyn_cast<LoadInst>(I)) {
        assert(!LI->isVolatile() && "Cannot split volatile loads!");

        // The load must be used exclusively to store into other pointers for
        // us to be able to arbitrarily pre-split it. The stores must also be
        // simple to avoid changing semantics.
        auto IsLoadSimplyStored = [](LoadInst *LI) {
          for (User *LU : LI->users()) {
            auto *SI = dyn_cast<StoreInst>(LU);
            if (!SI || !SI->isSimple())
              return false;
          }
          return true;
        };
        if (!IsLoadSimplyStored(LI)) {
          UnsplittableLoads.insert(LI);
          continue;
        }

        Loads.push_back(LI);
      } else if (auto *SI = dyn_cast<StoreInst>(I)) {
        if (S.getUse() != &SI->getOperandUse(SI->getPointerOperandIndex()))
          // Skip stores *of* pointers. FIXME: This shouldn't even be possible!
          continue;
        auto *StoredLoad = dyn_cast<LoadInst>(SI->getValueOperand());
        if (!StoredLoad || !StoredLoad->isSimple())
          continue;
        assert(!SI->isVolatile() && "Cannot split volatile stores!");

        Stores.push_back(SI);
      } else {
        // Other uses cannot be pre-split.
        continue;
      }

      // Record the initial split.
      LLVM_DEBUG(dbgs() << "    Candidate: " << *I << "\n");
      auto &Offsets = SplitOffsetsMap[I];
      assert(Offsets.Splits.empty() &&
             "Should not have splits the first time we see an instruction!");
      Offsets.S = &S;
      Offsets.Splits.push_back(P.endOffset() - S.beginOffset());
    }

    // Now scan the already split slices, and add a split for any of them which
    // we're going to pre-split.
    for (Slice *S : P.splitSliceTails()) {
      auto SplitOffsetsMapI =
          SplitOffsetsMap.find(cast<Instruction>(S->getUse()->getUser()));
      if (SplitOffsetsMapI == SplitOffsetsMap.end())
        continue;
      auto &Offsets = SplitOffsetsMapI->second;

      assert(Offsets.S == S && "Found a mismatched slice!");
      assert(!Offsets.Splits.empty() &&
             "Cannot have an empty set of splits on the second partition!");
      assert(Offsets.Splits.back() ==
                 P.beginOffset() - Offsets.S->beginOffset() &&
             "Previous split does not end where this one begins!");

      // Record each split. The last partition's end isn't needed as the size
      // of the slice dictates that.
      if (S->endOffset() > P.endOffset())
        Offsets.Splits.push_back(P.endOffset() - Offsets.S->beginOffset());
    }
  }

  // We may have split loads where some of their stores are split stores. For
  // such loads and stores, we can only pre-split them if their splits exactly
  // match relative to their starting offset. We have to verify this prior to
  // any rewriting.
  Stores.erase(
      llvm::remove_if(Stores,
                      [&UnsplittableLoads, &SplitOffsetsMap](StoreInst *SI) {
                        // Lookup the load we are storing in our map of split
                        // offsets.
                        auto *LI = cast<LoadInst>(SI->getValueOperand());
                        // If it was completely unsplittable, then we're done,
                        // and this store can't be pre-split.
                        if (UnsplittableLoads.count(LI))
                          return true;

                        auto LoadOffsetsI = SplitOffsetsMap.find(LI);
                        if (LoadOffsetsI == SplitOffsetsMap.end())
                          return false; // Unrelated loads are definitely safe.
                        auto &LoadOffsets = LoadOffsetsI->second;

                        // Now lookup the store's offsets.
                        auto &StoreOffsets = SplitOffsetsMap[SI];

                        // If the relative offsets of each split in the load and
                        // store match exactly, then we can split them and we
                        // don't need to remove them here.
                        if (LoadOffsets.Splits == StoreOffsets.Splits)
                          return false;

                        LLVM_DEBUG(
                            dbgs()
                            << "    Mismatched splits for load and store:\n"
                            << "      " << *LI << "\n"
                            << "      " << *SI << "\n");

                        // We've found a store and load that we need to split
                        // with mismatched relative splits. Just give up on them
                        // and remove both instructions from our list of
                        // candidates.
                        UnsplittableLoads.insert(LI);
                        return true;
                      }),
      Stores.end());
  // Now we have to go *back* through all the stores, because a later store may
  // have caused an earlier store's load to become unsplittable and if it is
  // unsplittable for the later store, then we can't rely on it being split in
  // the earlier store either.
  Stores.erase(llvm::remove_if(Stores,
                               [&UnsplittableLoads](StoreInst *SI) {
                                 auto *LI =
                                     cast<LoadInst>(SI->getValueOperand());
                                 return UnsplittableLoads.count(LI);
                               }),
               Stores.end());
  // Once we've established all the loads that can't be split for some reason,
  // filter any that made it into our list out.
  Loads.erase(llvm::remove_if(Loads,
                              [&UnsplittableLoads](LoadInst *LI) {
                                return UnsplittableLoads.count(LI);
                              }),
              Loads.end());

  // If no loads or stores are left, there is no pre-splitting to be done for
  // this alloca.
  if (Loads.empty() && Stores.empty())
    return false;

  // From here on, we can't fail and will be building new accesses, so rig up
  // an IR builder.
  IRBuilderTy IRB(&AI);

  // Collect the new slices which we will merge into the alloca slices.
  SmallVector<Slice, 4> NewSlices;

  // Track any allocas we end up splitting loads and stores for so we iterate
  // on them.
  SmallPtrSet<AllocaInst *, 4> ResplitPromotableAllocas;

  // At this point, we have collected all of the loads and stores we can
  // pre-split, and the specific splits needed for them. We actually do the
  // splitting in a specific order in order to handle when one of the loads in
  // the value operand to one of the stores.
  //
  // First, we rewrite all of the split loads, and just accumulate each split
  // load in a parallel structure. We also build the slices for them and append
  // them to the alloca slices.
  SmallDenseMap<LoadInst *, std::vector<LoadInst *>, 1> SplitLoadsMap;
  std::vector<LoadInst *> SplitLoads;
  const DataLayout &DL = AI.getModule()->getDataLayout();
  for (LoadInst *LI : Loads) {
    SplitLoads.clear();

    IntegerType *Ty = cast<IntegerType>(LI->getType());
    uint64_t LoadSize = Ty->getBitWidth() / 8;
    assert(LoadSize > 0 && "Cannot have a zero-sized integer load!");

    auto &Offsets = SplitOffsetsMap[LI];
    assert(LoadSize == Offsets.S->endOffset() - Offsets.S->beginOffset() &&
           "Slice size should always match load size exactly!");
    uint64_t BaseOffset = Offsets.S->beginOffset();
    assert(BaseOffset + LoadSize > BaseOffset &&
           "Cannot represent alloca access size using 64-bit integers!");

    Instruction *BasePtr = cast<Instruction>(LI->getPointerOperand());
    IRB.SetInsertPoint(LI);

    LLVM_DEBUG(dbgs() << "  Splitting load: " << *LI << "\n");

    uint64_t PartOffset = 0, PartSize = Offsets.Splits.front();
    int Idx = 0, Size = Offsets.Splits.size();
    for (;;) {
      auto *PartTy = Type::getIntNTy(Ty->getContext(), PartSize * 8);
      auto AS = LI->getPointerAddressSpace();
      auto *PartPtrTy = PartTy->getPointerTo(AS);
      LoadInst *PLoad = IRB.CreateAlignedLoad(
          getAdjustedPtr(IRB, DL, BasePtr,
                         APInt(DL.getIndexSizeInBits(AS), PartOffset),
                         PartPtrTy, BasePtr->getName() + "."),
          getAdjustedAlignment(LI, PartOffset, DL), /*IsVolatile*/ false,
          LI->getName());
      PLoad->copyMetadata(*LI, {LLVMContext::MD_mem_parallel_loop_access,
                                LLVMContext::MD_access_group});

      // Append this load onto the list of split loads so we can find it later
      // to rewrite the stores.
      SplitLoads.push_back(PLoad);

      // Now build a new slice for the alloca.
      NewSlices.push_back(
          Slice(BaseOffset + PartOffset, BaseOffset + PartOffset + PartSize,
                &PLoad->getOperandUse(PLoad->getPointerOperandIndex()),
                /*IsSplittable*/ false));
      LLVM_DEBUG(dbgs() << "    new slice [" << NewSlices.back().beginOffset()
                        << ", " << NewSlices.back().endOffset()
                        << "): " << *PLoad << "\n");

      // See if we've handled all the splits.
      if (Idx >= Size)
        break;

      // Setup the next partition.
      PartOffset = Offsets.Splits[Idx];
      ++Idx;
      PartSize = (Idx < Size ? Offsets.Splits[Idx] : LoadSize) - PartOffset;
    }

    // Now that we have the split loads, do the slow walk over all uses of the
    // load and rewrite them as split stores, or save the split loads to use
    // below if the store is going to be split there anyways.
    bool DeferredStores = false;
    for (User *LU : LI->users()) {
      StoreInst *SI = cast<StoreInst>(LU);
      if (!Stores.empty() && SplitOffsetsMap.count(SI)) {
        DeferredStores = true;
        LLVM_DEBUG(dbgs() << "    Deferred splitting of store: " << *SI
                          << "\n");
        continue;
      }

      Value *StoreBasePtr = SI->getPointerOperand();
      IRB.SetInsertPoint(SI);

      LLVM_DEBUG(dbgs() << "    Splitting store of load: " << *SI << "\n");

      for (int Idx = 0, Size = SplitLoads.size(); Idx < Size; ++Idx) {
        LoadInst *PLoad = SplitLoads[Idx];
        uint64_t PartOffset = Idx == 0 ? 0 : Offsets.Splits[Idx - 1];
        auto *PartPtrTy =
            PLoad->getType()->getPointerTo(SI->getPointerAddressSpace());

        auto AS = SI->getPointerAddressSpace();
        StoreInst *PStore = IRB.CreateAlignedStore(
            PLoad,
            getAdjustedPtr(IRB, DL, StoreBasePtr,
                           APInt(DL.getIndexSizeInBits(AS), PartOffset),
                           PartPtrTy, StoreBasePtr->getName() + "."),
            getAdjustedAlignment(SI, PartOffset, DL), /*IsVolatile*/ false);
        PStore->copyMetadata(*LI, {LLVMContext::MD_mem_parallel_loop_access,
                                   LLVMContext::MD_access_group});
        LLVM_DEBUG(dbgs() << "      +" << PartOffset << ":" << *PStore << "\n");
      }

      // We want to immediately iterate on any allocas impacted by splitting
      // this store, and we have to track any promotable alloca (indicated by
      // a direct store) as needing to be resplit because it is no longer
      // promotable.
      if (AllocaInst *OtherAI = dyn_cast<AllocaInst>(StoreBasePtr)) {
        ResplitPromotableAllocas.insert(OtherAI);
        Worklist.insert(OtherAI);
      } else if (AllocaInst *OtherAI = dyn_cast<AllocaInst>(
                     StoreBasePtr->stripInBoundsOffsets())) {
        Worklist.insert(OtherAI);
      }

      // Mark the original store as dead.
      DeadInsts.insert(SI);
    }

    // Save the split loads if there are deferred stores among the users.
    if (DeferredStores)
      SplitLoadsMap.insert(std::make_pair(LI, std::move(SplitLoads)));

    // Mark the original load as dead and kill the original slice.
    DeadInsts.insert(LI);
    Offsets.S->kill();
  }

  // Second, we rewrite all of the split stores. At this point, we know that
  // all loads from this alloca have been split already. For stores of such
  // loads, we can simply look up the pre-existing split loads. For stores of
  // other loads, we split those loads first and then write split stores of
  // them.
  for (StoreInst *SI : Stores) {
    auto *LI = cast<LoadInst>(SI->getValueOperand());
    IntegerType *Ty = cast<IntegerType>(LI->getType());
    uint64_t StoreSize = Ty->getBitWidth() / 8;
    assert(StoreSize > 0 && "Cannot have a zero-sized integer store!");

    auto &Offsets = SplitOffsetsMap[SI];
    assert(StoreSize == Offsets.S->endOffset() - Offsets.S->beginOffset() &&
           "Slice size should always match load size exactly!");
    uint64_t BaseOffset = Offsets.S->beginOffset();
    assert(BaseOffset + StoreSize > BaseOffset &&
           "Cannot represent alloca access size using 64-bit integers!");

    Value *LoadBasePtr = LI->getPointerOperand();
    Instruction *StoreBasePtr = cast<Instruction>(SI->getPointerOperand());

    LLVM_DEBUG(dbgs() << "  Splitting store: " << *SI << "\n");

    // Check whether we have an already split load.
    auto SplitLoadsMapI = SplitLoadsMap.find(LI);
    std::vector<LoadInst *> *SplitLoads = nullptr;
    if (SplitLoadsMapI != SplitLoadsMap.end()) {
      SplitLoads = &SplitLoadsMapI->second;
      assert(SplitLoads->size() == Offsets.Splits.size() + 1 &&
             "Too few split loads for the number of splits in the store!");
    } else {
      LLVM_DEBUG(dbgs() << "          of load: " << *LI << "\n");
    }

    uint64_t PartOffset = 0, PartSize = Offsets.Splits.front();
    int Idx = 0, Size = Offsets.Splits.size();
    for (;;) {
      auto *PartTy = Type::getIntNTy(Ty->getContext(), PartSize * 8);
      auto *LoadPartPtrTy = PartTy->getPointerTo(LI->getPointerAddressSpace());
      auto *StorePartPtrTy = PartTy->getPointerTo(SI->getPointerAddressSpace());

      // Either lookup a split load or create one.
      LoadInst *PLoad;
      if (SplitLoads) {
        PLoad = (*SplitLoads)[Idx];
      } else {
        IRB.SetInsertPoint(LI);
        auto AS = LI->getPointerAddressSpace();
        PLoad = IRB.CreateAlignedLoad(
            getAdjustedPtr(IRB, DL, LoadBasePtr,
                           APInt(DL.getIndexSizeInBits(AS), PartOffset),
                           LoadPartPtrTy, LoadBasePtr->getName() + "."),
            getAdjustedAlignment(LI, PartOffset, DL), /*IsVolatile*/ false,
            LI->getName());
      }

      // And store this partition.
      IRB.SetInsertPoint(SI);
      auto AS = SI->getPointerAddressSpace();
      StoreInst *PStore = IRB.CreateAlignedStore(
          PLoad,
          getAdjustedPtr(IRB, DL, StoreBasePtr,
                         APInt(DL.getIndexSizeInBits(AS), PartOffset),
                         StorePartPtrTy, StoreBasePtr->getName() + "."),
          getAdjustedAlignment(SI, PartOffset, DL), /*IsVolatile*/ false);

      // Now build a new slice for the alloca.
      NewSlices.push_back(
          Slice(BaseOffset + PartOffset, BaseOffset + PartOffset + PartSize,
                &PStore->getOperandUse(PStore->getPointerOperandIndex()),
                /*IsSplittable*/ false));
      LLVM_DEBUG(dbgs() << "    new slice [" << NewSlices.back().beginOffset()
                        << ", " << NewSlices.back().endOffset()
                        << "): " << *PStore << "\n");
      if (!SplitLoads) {
        LLVM_DEBUG(dbgs() << "      of split load: " << *PLoad << "\n");
      }

      // See if we've finished all the splits.
      if (Idx >= Size)
        break;

      // Setup the next partition.
      PartOffset = Offsets.Splits[Idx];
      ++Idx;
      PartSize = (Idx < Size ? Offsets.Splits[Idx] : StoreSize) - PartOffset;
    }

    // We want to immediately iterate on any allocas impacted by splitting
    // this load, which is only relevant if it isn't a load of this alloca and
    // thus we didn't already split the loads above. We also have to keep track
    // of any promotable allocas we split loads on as they can no longer be
    // promoted.
    if (!SplitLoads) {
      if (AllocaInst *OtherAI = dyn_cast<AllocaInst>(LoadBasePtr)) {
        assert(OtherAI != &AI && "We can't re-split our own alloca!");
        ResplitPromotableAllocas.insert(OtherAI);
        Worklist.insert(OtherAI);
      } else if (AllocaInst *OtherAI = dyn_cast<AllocaInst>(
                     LoadBasePtr->stripInBoundsOffsets())) {
        assert(OtherAI != &AI && "We can't re-split our own alloca!");
        Worklist.insert(OtherAI);
      }
    }

    // Mark the original store as dead now that we've split it up and kill its
    // slice. Note that we leave the original load in place unless this store
    // was its only use. It may in turn be split up if it is an alloca load
    // for some other alloca, but it may be a normal load. This may introduce
    // redundant loads, but where those can be merged the rest of the optimizer
    // should handle the merging, and this uncovers SSA splits which is more
    // important. In practice, the original loads will almost always be fully
    // split and removed eventually, and the splits will be merged by any
    // trivial CSE, including instcombine.
    if (LI->hasOneUse()) {
      assert(*LI->user_begin() == SI && "Single use isn't this store!");
      DeadInsts.insert(LI);
    }
    DeadInsts.insert(SI);
    Offsets.S->kill();
  }

  // Remove the killed slices that have ben pre-split.
  AS.erase(llvm::remove_if(AS, [](const Slice &S) { return S.isDead(); }),
           AS.end());

  // Insert our new slices. This will sort and merge them into the sorted
  // sequence.
  AS.insert(NewSlices);

  LLVM_DEBUG(dbgs() << "  Pre-split slices:\n");
#ifndef NDEBUG
  for (auto I = AS.begin(), E = AS.end(); I != E; ++I)
    LLVM_DEBUG(AS.print(dbgs(), I, "    "));
#endif

  // Finally, don't try to promote any allocas that new require re-splitting.
  // They have already been added to the worklist above.
  PromotableAllocas.erase(
      llvm::remove_if(
          PromotableAllocas,
          [&](AllocaInst *AI) { return ResplitPromotableAllocas.count(AI); }),
      PromotableAllocas.end());

  return true;
}

/// Rewrite an alloca partition's users.
///
/// This routine drives both of the rewriting goals of the SROA pass. It tries
/// to rewrite uses of an alloca partition to be conducive for SSA value
/// promotion. If the partition needs a new, more refined alloca, this will
/// build that new alloca, preserving as much type information as possible, and
/// rewrite the uses of the old alloca to point at the new one and have the
/// appropriate new offsets. It also evaluates how successful the rewrite was
/// at enabling promotion and if it was successful queues the alloca to be
/// promoted.
AllocaInst *SROA::rewritePartition(AllocaInst &AI, AllocaSlices &AS,
                                   Partition &P) {
  // Try to compute a friendly type for this partition of the alloca. This
  // won't always succeed, in which case we fall back to a legal integer type
  // or an i8 array of an appropriate size.
  Type *SliceTy = nullptr;
  const DataLayout &DL = AI.getModule()->getDataLayout();
  if (Type *CommonUseTy = findCommonType(P.begin(), P.end(), P.endOffset()))
    if (DL.getTypeAllocSize(CommonUseTy) >= P.size())
      SliceTy = CommonUseTy;
  if (!SliceTy)
    if (Type *TypePartitionTy = getTypePartition(DL, AI.getAllocatedType(),
                                                 P.beginOffset(), P.size()))
      SliceTy = TypePartitionTy;
  if ((!SliceTy || (SliceTy->isArrayTy() &&
                    SliceTy->getArrayElementType()->isIntegerTy())) &&
      DL.isLegalInteger(P.size() * 8))
    SliceTy = Type::getIntNTy(*C, P.size() * 8);
  if (!SliceTy)
    SliceTy = ArrayType::get(Type::getInt8Ty(*C), P.size());
  assert(DL.getTypeAllocSize(SliceTy) >= P.size());

  bool IsIntegerPromotable = isIntegerWideningViable(P, SliceTy, DL);

  VectorType *VecTy =
      IsIntegerPromotable ? nullptr : isVectorPromotionViable(P, DL);
  if (VecTy)
    SliceTy = VecTy;

  // Check for the case where we're going to rewrite to a new alloca of the
  // exact same type as the original, and with the same access offsets. In that
  // case, re-use the existing alloca, but still run through the rewriter to
  // perform phi and select speculation.
  // P.beginOffset() can be non-zero even with the same type in a case with
  // out-of-bounds access (e.g. @PR35657 function in SROA/basictest.ll).
  AllocaInst *NewAI;
  if (SliceTy == AI.getAllocatedType() && P.beginOffset() == 0) {
    NewAI = &AI;
    // FIXME: We should be able to bail at this point with "nothing changed".
    // FIXME: We might want to defer PHI speculation until after here.
    // FIXME: return nullptr;
  } else {
    unsigned Alignment = AI.getAlignment();
    if (!Alignment) {
      // The minimum alignment which users can rely on when the explicit
      // alignment is omitted or zero is that required by the ABI for this
      // type.
      Alignment = DL.getABITypeAlignment(AI.getAllocatedType());
    }
    Alignment = MinAlign(Alignment, P.beginOffset());
    // If we will get at least this much alignment from the type alone, leave
    // the alloca's alignment unconstrained.
    if (Alignment <= DL.getABITypeAlignment(SliceTy))
      Alignment = 0;
    NewAI = new AllocaInst(
      SliceTy, AI.getType()->getAddressSpace(), nullptr, Alignment,
        AI.getName() + ".sroa." + Twine(P.begin() - AS.begin()), &AI);
    // Copy the old AI debug location over to the new one.
    NewAI->setDebugLoc(AI.getDebugLoc());
    ++NumNewAllocas;
  }

  LLVM_DEBUG(dbgs() << "Rewriting alloca partition "
                    << "[" << P.beginOffset() << "," << P.endOffset()
                    << ") to: " << *NewAI << "\n");

  // Track the high watermark on the worklist as it is only relevant for
  // promoted allocas. We will reset it to this point if the alloca is not in
  // fact scheduled for promotion.
  unsigned PPWOldSize = PostPromotionWorklist.size();
  unsigned NumUses = 0;
  SmallSetVector<PHINode *, 8> PHIUsers;
  SmallSetVector<SelectInst *, 8> SelectUsers;

  AllocaSliceRewriter Rewriter(DL, AS, *this, AI, *NewAI, P.beginOffset(),
                               P.endOffset(), IsIntegerPromotable, VecTy,
                               PHIUsers, SelectUsers);
  bool Promotable = true;
  for (Slice *S : P.splitSliceTails()) {
    Promotable &= Rewriter.visit(S);
    ++NumUses;
  }
  for (Slice &S : P) {
    Promotable &= Rewriter.visit(&S);
    ++NumUses;
  }

  NumAllocaPartitionUses += NumUses;
  MaxUsesPerAllocaPartition.updateMax(NumUses);

  // Now that we've processed all the slices in the new partition, check if any
  // PHIs or Selects would block promotion.
  for (PHINode *PHI : PHIUsers)
    if (!isSafePHIToSpeculate(*PHI)) {
      Promotable = false;
      PHIUsers.clear();
      SelectUsers.clear();
      break;
    }

  for (SelectInst *Sel : SelectUsers)
    if (!isSafeSelectToSpeculate(*Sel)) {
      Promotable = false;
      PHIUsers.clear();
      SelectUsers.clear();
      break;
    }

  if (Promotable) {
    if (PHIUsers.empty() && SelectUsers.empty()) {
      // Promote the alloca.
      PromotableAllocas.push_back(NewAI);
    } else {
      // If we have either PHIs or Selects to speculate, add them to those
      // worklists and re-queue the new alloca so that we promote in on the
      // next iteration.
      for (PHINode *PHIUser : PHIUsers)
        SpeculatablePHIs.insert(PHIUser);
      for (SelectInst *SelectUser : SelectUsers)
        SpeculatableSelects.insert(SelectUser);
      Worklist.insert(NewAI);
    }
  } else {
    // Drop any post-promotion work items if promotion didn't happen.
    while (PostPromotionWorklist.size() > PPWOldSize)
      PostPromotionWorklist.pop_back();

    // We couldn't promote and we didn't create a new partition, nothing
    // happened.
    if (NewAI == &AI)
      return nullptr;

    // If we can't promote the alloca, iterate on it to check for new
    // refinements exposed by splitting the current alloca. Don't iterate on an
    // alloca which didn't actually change and didn't get promoted.
    Worklist.insert(NewAI);
  }

  return NewAI;
}

/// Walks the slices of an alloca and form partitions based on them,
/// rewriting each of their uses.
bool SROA::splitAlloca(AllocaInst &AI, AllocaSlices &AS) {
  if (AS.begin() == AS.end())
    return false;

  unsigned NumPartitions = 0;
  bool Changed = false;
  const DataLayout &DL = AI.getModule()->getDataLayout();

  // First try to pre-split loads and stores.
  Changed |= presplitLoadsAndStores(AI, AS);

  // Now that we have identified any pre-splitting opportunities,
  // mark loads and stores unsplittable except for the following case.
  // We leave a slice splittable if all other slices are disjoint or fully
  // included in the slice, such as whole-alloca loads and stores.
  // If we fail to split these during pre-splitting, we want to force them
  // to be rewritten into a partition.
  bool IsSorted = true;

  uint64_t AllocaSize = DL.getTypeAllocSize(AI.getAllocatedType());
  const uint64_t MaxBitVectorSize = 1024;
  if (AllocaSize <= MaxBitVectorSize) {
    // If a byte boundary is included in any load or store, a slice starting or
    // ending at the boundary is not splittable.
    SmallBitVector SplittableOffset(AllocaSize + 1, true);
    for (Slice &S : AS)
      for (unsigned O = S.beginOffset() + 1;
           O < S.endOffset() && O < AllocaSize; O++)
        SplittableOffset.reset(O);

    for (Slice &S : AS) {
      if (!S.isSplittable())
        continue;

      if ((S.beginOffset() > AllocaSize || SplittableOffset[S.beginOffset()]) &&
          (S.endOffset() > AllocaSize || SplittableOffset[S.endOffset()]))
        continue;

      if (isa<LoadInst>(S.getUse()->getUser()) ||
          isa<StoreInst>(S.getUse()->getUser())) {
        S.makeUnsplittable();
        IsSorted = false;
      }
    }
  }
  else {
    // We only allow whole-alloca splittable loads and stores
    // for a large alloca to avoid creating too large BitVector.
    for (Slice &S : AS) {
      if (!S.isSplittable())
        continue;

      if (S.beginOffset() == 0 && S.endOffset() >= AllocaSize)
        continue;

      if (isa<LoadInst>(S.getUse()->getUser()) ||
          isa<StoreInst>(S.getUse()->getUser())) {
        S.makeUnsplittable();
        IsSorted = false;
      }
    }
  }

  if (!IsSorted)
    llvm::sort(AS);

  /// Describes the allocas introduced by rewritePartition in order to migrate
  /// the debug info.
  struct Fragment {
    AllocaInst *Alloca;
    uint64_t Offset;
    uint64_t Size;
    Fragment(AllocaInst *AI, uint64_t O, uint64_t S)
      : Alloca(AI), Offset(O), Size(S) {}
  };
  SmallVector<Fragment, 4> Fragments;

  // Rewrite each partition.
  for (auto &P : AS.partitions()) {
    if (AllocaInst *NewAI = rewritePartition(AI, AS, P)) {
      Changed = true;
      if (NewAI != &AI) {
        uint64_t SizeOfByte = 8;
        uint64_t AllocaSize = DL.getTypeSizeInBits(NewAI->getAllocatedType());
        // Don't include any padding.
        uint64_t Size = std::min(AllocaSize, P.size() * SizeOfByte);
        Fragments.push_back(Fragment(NewAI, P.beginOffset() * SizeOfByte, Size));
      }
    }
    ++NumPartitions;
  }

  NumAllocaPartitions += NumPartitions;
  MaxPartitionsPerAlloca.updateMax(NumPartitions);

  // Migrate debug information from the old alloca to the new alloca(s)
  // and the individual partitions.
  TinyPtrVector<DbgVariableIntrinsic *> DbgDeclares = FindDbgAddrUses(&AI);
  if (!DbgDeclares.empty()) {
    auto *Var = DbgDeclares.front()->getVariable();
    auto *Expr = DbgDeclares.front()->getExpression();
    auto VarSize = Var->getSizeInBits();
    DIBuilder DIB(*AI.getModule(), /*AllowUnresolved*/ false);
    uint64_t AllocaSize = DL.getTypeSizeInBits(AI.getAllocatedType());
    for (auto Fragment : Fragments) {
      // Create a fragment expression describing the new partition or reuse AI's
      // expression if there is only one partition.
      auto *FragmentExpr = Expr;
      if (Fragment.Size < AllocaSize || Expr->isFragment()) {
        // If this alloca is already a scalar replacement of a larger aggregate,
        // Fragment.Offset describes the offset inside the scalar.
        auto ExprFragment = Expr->getFragmentInfo();
        uint64_t Offset = ExprFragment ? ExprFragment->OffsetInBits : 0;
        uint64_t Start = Offset + Fragment.Offset;
        uint64_t Size = Fragment.Size;
        if (ExprFragment) {
          uint64_t AbsEnd =
              ExprFragment->OffsetInBits + ExprFragment->SizeInBits;
          if (Start >= AbsEnd)
            // No need to describe a SROAed padding.
            continue;
          Size = std::min(Size, AbsEnd - Start);
        }
        // The new, smaller fragment is stenciled out from the old fragment.
        if (auto OrigFragment = FragmentExpr->getFragmentInfo()) {
          assert(Start >= OrigFragment->OffsetInBits &&
                 "new fragment is outside of original fragment");
          Start -= OrigFragment->OffsetInBits;
        }

        // The alloca may be larger than the variable.
        if (VarSize) {
          if (Size > *VarSize)
            Size = *VarSize;
          if (Size == 0 || Start + Size > *VarSize)
            continue;
        }

        // Avoid creating a fragment expression that covers the entire variable.
        if (!VarSize || *VarSize != Size) {
          if (auto E =
                  DIExpression::createFragmentExpression(Expr, Start, Size))
            FragmentExpr = *E;
          else
            continue;
        }
      }

      // Remove any existing intrinsics describing the same alloca.
      for (DbgVariableIntrinsic *OldDII : FindDbgAddrUses(Fragment.Alloca))
        OldDII->eraseFromParent();

      DIB.insertDeclare(Fragment.Alloca, Var, FragmentExpr,
                        DbgDeclares.front()->getDebugLoc(), &AI);
    }
  }
  return Changed;
}

/// Clobber a use with undef, deleting the used value if it becomes dead.
void SROA::clobberUse(Use &U) {
  Value *OldV = U;
  // Replace the use with an undef value.
  U = UndefValue::get(OldV->getType());

  // Check for this making an instruction dead. We have to garbage collect
  // all the dead instructions to ensure the uses of any alloca end up being
  // minimal.
  if (Instruction *OldI = dyn_cast<Instruction>(OldV))
    if (isInstructionTriviallyDead(OldI)) {
      DeadInsts.insert(OldI);
    }
}

/// Analyze an alloca for SROA.
///
/// This analyzes the alloca to ensure we can reason about it, builds
/// the slices of the alloca, and then hands it off to be split and
/// rewritten as needed.
bool SROA::runOnAlloca(AllocaInst &AI) {
  LLVM_DEBUG(dbgs() << "SROA alloca: " << AI << "\n");
  ++NumAllocasAnalyzed;

  // Special case dead allocas, as they're trivial.
  if (AI.use_empty()) {
    AI.eraseFromParent();
    return true;
  }
  const DataLayout &DL = AI.getModule()->getDataLayout();

  // Skip alloca forms that this analysis can't handle.
  if (AI.isArrayAllocation() || !AI.getAllocatedType()->isSized() ||
      DL.getTypeAllocSize(AI.getAllocatedType()) == 0)
    return false;

  bool Changed = false;

  // First, split any FCA loads and stores touching this alloca to promote
  // better splitting and promotion opportunities.
  AggLoadStoreRewriter AggRewriter(DL);
  Changed |= AggRewriter.rewrite(AI);

  // Build the slices using a recursive instruction-visiting builder.
  AllocaSlices AS(DL, AI);
  LLVM_DEBUG(AS.print(dbgs()));
  if (AS.isEscaped())
    return Changed;

  // Delete all the dead users of this alloca before splitting and rewriting it.
  for (Instruction *DeadUser : AS.getDeadUsers()) {
    // Free up everything used by this instruction.
    for (Use &DeadOp : DeadUser->operands())
      clobberUse(DeadOp);

    // Now replace the uses of this instruction.
    DeadUser->replaceAllUsesWith(UndefValue::get(DeadUser->getType()));

    // And mark it for deletion.
    DeadInsts.insert(DeadUser);
    Changed = true;
  }
  for (Use *DeadOp : AS.getDeadOperands()) {
    clobberUse(*DeadOp);
    Changed = true;
  }

  // No slices to split. Leave the dead alloca for a later pass to clean up.
  if (AS.begin() == AS.end())
    return Changed;

  Changed |= splitAlloca(AI, AS);

  LLVM_DEBUG(dbgs() << "  Speculating PHIs\n");
  while (!SpeculatablePHIs.empty())
    speculatePHINodeLoads(*SpeculatablePHIs.pop_back_val());

  LLVM_DEBUG(dbgs() << "  Speculating Selects\n");
  while (!SpeculatableSelects.empty())
    speculateSelectInstLoads(*SpeculatableSelects.pop_back_val());

  return Changed;
}

/// Delete the dead instructions accumulated in this run.
///
/// Recursively deletes the dead instructions we've accumulated. This is done
/// at the very end to maximize locality of the recursive delete and to
/// minimize the problems of invalidated instruction pointers as such pointers
/// are used heavily in the intermediate stages of the algorithm.
///
/// We also record the alloca instructions deleted here so that they aren't
/// subsequently handed to mem2reg to promote.
bool SROA::deleteDeadInstructions(
    SmallPtrSetImpl<AllocaInst *> &DeletedAllocas) {
  bool Changed = false;
  while (!DeadInsts.empty()) {
    Instruction *I = DeadInsts.pop_back_val();
    LLVM_DEBUG(dbgs() << "Deleting dead instruction: " << *I << "\n");

    // If the instruction is an alloca, find the possible dbg.declare connected
    // to it, and remove it too. We must do this before calling RAUW or we will
    // not be able to find it.
    if (AllocaInst *AI = dyn_cast<AllocaInst>(I)) {
      DeletedAllocas.insert(AI);
      for (DbgVariableIntrinsic *OldDII : FindDbgAddrUses(AI))
        OldDII->eraseFromParent();
    }

    I->replaceAllUsesWith(UndefValue::get(I->getType()));

    for (Use &Operand : I->operands())
      if (Instruction *U = dyn_cast<Instruction>(Operand)) {
        // Zero out the operand and see if it becomes trivially dead.
        Operand = nullptr;
        if (isInstructionTriviallyDead(U))
          DeadInsts.insert(U);
      }

    ++NumDeleted;
    I->eraseFromParent();
    Changed = true;
  }
  return Changed;
}

/// Promote the allocas, using the best available technique.
///
/// This attempts to promote whatever allocas have been identified as viable in
/// the PromotableAllocas list. If that list is empty, there is nothing to do.
/// This function returns whether any promotion occurred.
bool SROA::promoteAllocas(Function &F) {
  if (PromotableAllocas.empty())
    return false;

  NumPromoted += PromotableAllocas.size();

  LLVM_DEBUG(dbgs() << "Promoting allocas with mem2reg...\n");
  PromoteMemToReg(PromotableAllocas, *DT, AC);
  PromotableAllocas.clear();
  return true;
}

PreservedAnalyses SROA::runImpl(Function &F, DominatorTree &RunDT,
                                AssumptionCache &RunAC) {
  LLVM_DEBUG(dbgs() << "SROA function: " << F.getName() << "\n");
  C = &F.getContext();
  DT = &RunDT;
  AC = &RunAC;

  BasicBlock &EntryBB = F.getEntryBlock();
  for (BasicBlock::iterator I = EntryBB.begin(), E = std::prev(EntryBB.end());
       I != E; ++I) {
    if (AllocaInst *AI = dyn_cast<AllocaInst>(I))
      Worklist.insert(AI);
  }

  bool Changed = false;
  // A set of deleted alloca instruction pointers which should be removed from
  // the list of promotable allocas.
  SmallPtrSet<AllocaInst *, 4> DeletedAllocas;

  do {
    while (!Worklist.empty()) {
      Changed |= runOnAlloca(*Worklist.pop_back_val());
      Changed |= deleteDeadInstructions(DeletedAllocas);

      // Remove the deleted allocas from various lists so that we don't try to
      // continue processing them.
      if (!DeletedAllocas.empty()) {
        auto IsInSet = [&](AllocaInst *AI) { return DeletedAllocas.count(AI); };
        Worklist.remove_if(IsInSet);
        PostPromotionWorklist.remove_if(IsInSet);
        PromotableAllocas.erase(llvm::remove_if(PromotableAllocas, IsInSet),
                                PromotableAllocas.end());
        DeletedAllocas.clear();
      }
    }

    Changed |= promoteAllocas(F);

    Worklist = PostPromotionWorklist;
    PostPromotionWorklist.clear();
  } while (!Worklist.empty());

  if (!Changed)
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  PA.preserve<GlobalsAA>();
  return PA;
}

PreservedAnalyses SROA::run(Function &F, FunctionAnalysisManager &AM) {
  return runImpl(F, AM.getResult<DominatorTreeAnalysis>(F),
                 AM.getResult<AssumptionAnalysis>(F));
}

/// A legacy pass for the legacy pass manager that wraps the \c SROA pass.
///
/// This is in the llvm namespace purely to allow it to be a friend of the \c
/// SROA pass.
class llvm::sroa::SROALegacyPass : public FunctionPass {
  /// The SROA implementation.
  SROA Impl;

public:
  static char ID;

  SROALegacyPass() : FunctionPass(ID) {
    initializeSROALegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    if (skipFunction(F))
      return false;

    auto PA = Impl.runImpl(
        F, getAnalysis<DominatorTreeWrapperPass>().getDomTree(),
        getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F));
    return !PA.areAllPreserved();
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addPreserved<GlobalsAAWrapperPass>();
    AU.setPreservesCFG();
  }

  StringRef getPassName() const override { return "SROA"; }
};

char SROALegacyPass::ID = 0;

FunctionPass *llvm::createSROAPass() { return new SROALegacyPass(); }

INITIALIZE_PASS_BEGIN(SROALegacyPass, "sroa",
                      "Scalar Replacement Of Aggregates", false, false)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(SROALegacyPass, "sroa", "Scalar Replacement Of Aggregates",
                    false, false)
