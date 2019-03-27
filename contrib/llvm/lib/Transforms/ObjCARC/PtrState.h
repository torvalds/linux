//===- PtrState.h - ARC State for a Ptr -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file contains declarations for the ARC state associated with a ptr. It
//  is only used by the ARC Sequence Dataflow computation. By separating this
//  from the actual dataflow, it is easier to consider the mechanics of the ARC
//  optimization separate from the actual predicates being used.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TRANSFORMS_OBJCARC_PTRSTATE_H
#define LLVM_LIB_TRANSFORMS_OBJCARC_PTRSTATE_H

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/ObjCARCInstKind.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class BasicBlock;
class Instruction;
class MDNode;
class raw_ostream;
class Value;

namespace objcarc {

class ARCMDKindCache;
class ProvenanceAnalysis;

/// \enum Sequence
///
/// A sequence of states that a pointer may go through in which an
/// objc_retain and objc_release are actually needed.
enum Sequence {
  S_None,
  S_Retain,        ///< objc_retain(x).
  S_CanRelease,    ///< foo(x) -- x could possibly see a ref count decrement.
  S_Use,           ///< any use of x.
  S_Stop,          ///< like S_Release, but code motion is stopped.
  S_Release,       ///< objc_release(x).
  S_MovableRelease ///< objc_release(x), !clang.imprecise_release.
};

raw_ostream &operator<<(raw_ostream &OS,
                        const Sequence S) LLVM_ATTRIBUTE_UNUSED;

/// Unidirectional information about either a
/// retain-decrement-use-release sequence or release-use-decrement-retain
/// reverse sequence.
struct RRInfo {
  /// After an objc_retain, the reference count of the referenced
  /// object is known to be positive. Similarly, before an objc_release, the
  /// reference count of the referenced object is known to be positive. If
  /// there are retain-release pairs in code regions where the retain count
  /// is known to be positive, they can be eliminated, regardless of any side
  /// effects between them.
  ///
  /// Also, a retain+release pair nested within another retain+release
  /// pair all on the known same pointer value can be eliminated, regardless
  /// of any intervening side effects.
  ///
  /// KnownSafe is true when either of these conditions is satisfied.
  bool KnownSafe = false;

  /// True of the objc_release calls are all marked with the "tail" keyword.
  bool IsTailCallRelease = false;

  /// If the Calls are objc_release calls and they all have a
  /// clang.imprecise_release tag, this is the metadata tag.
  MDNode *ReleaseMetadata = nullptr;

  /// For a top-down sequence, the set of objc_retains or
  /// objc_retainBlocks. For bottom-up, the set of objc_releases.
  SmallPtrSet<Instruction *, 2> Calls;

  /// The set of optimal insert positions for moving calls in the opposite
  /// sequence.
  SmallPtrSet<Instruction *, 2> ReverseInsertPts;

  /// If this is true, we cannot perform code motion but can still remove
  /// retain/release pairs.
  bool CFGHazardAfflicted = false;

  RRInfo() = default;

  void clear();

  /// Conservatively merge the two RRInfo. Returns true if a partial merge has
  /// occurred, false otherwise.
  bool Merge(const RRInfo &Other);
};

/// This class summarizes several per-pointer runtime properties which
/// are propagated through the flow graph.
class PtrState {
protected:
  /// True if the reference count is known to be incremented.
  bool KnownPositiveRefCount = false;

  /// True if we've seen an opportunity for partial RR elimination, such as
  /// pushing calls into a CFG triangle or into one side of a CFG diamond.
  bool Partial = false;

  /// The current position in the sequence.
  unsigned char Seq : 8;

  /// Unidirectional information about the current sequence.
  RRInfo RRI;

  PtrState() : Seq(S_None) {}

public:
  bool IsKnownSafe() const { return RRI.KnownSafe; }

  void SetKnownSafe(const bool NewValue) { RRI.KnownSafe = NewValue; }

  bool IsTailCallRelease() const { return RRI.IsTailCallRelease; }

  void SetTailCallRelease(const bool NewValue) {
    RRI.IsTailCallRelease = NewValue;
  }

  bool IsTrackingImpreciseReleases() const {
    return RRI.ReleaseMetadata != nullptr;
  }

  const MDNode *GetReleaseMetadata() const { return RRI.ReleaseMetadata; }

  void SetReleaseMetadata(MDNode *NewValue) { RRI.ReleaseMetadata = NewValue; }

  bool IsCFGHazardAfflicted() const { return RRI.CFGHazardAfflicted; }

  void SetCFGHazardAfflicted(const bool NewValue) {
    RRI.CFGHazardAfflicted = NewValue;
  }

  void SetKnownPositiveRefCount();
  void ClearKnownPositiveRefCount();

  bool HasKnownPositiveRefCount() const { return KnownPositiveRefCount; }

  void SetSeq(Sequence NewSeq);

  Sequence GetSeq() const { return static_cast<Sequence>(Seq); }

  void ClearSequenceProgress() { ResetSequenceProgress(S_None); }

  void ResetSequenceProgress(Sequence NewSeq);
  void Merge(const PtrState &Other, bool TopDown);

  void InsertCall(Instruction *I) { RRI.Calls.insert(I); }

  void InsertReverseInsertPt(Instruction *I) { RRI.ReverseInsertPts.insert(I); }

  void ClearReverseInsertPts() { RRI.ReverseInsertPts.clear(); }

  bool HasReverseInsertPts() const { return !RRI.ReverseInsertPts.empty(); }

  const RRInfo &GetRRInfo() const { return RRI; }
};

struct BottomUpPtrState : PtrState {
  BottomUpPtrState() = default;

  /// (Re-)Initialize this bottom up pointer returning true if we detected a
  /// pointer with nested releases.
  bool InitBottomUp(ARCMDKindCache &Cache, Instruction *I);

  /// Return true if this set of releases can be paired with a release. Modifies
  /// state appropriately to reflect that the matching occurred if it is
  /// successful.
  ///
  /// It is assumed that one has already checked that the RCIdentity of the
  /// retain and the RCIdentity of this ptr state are the same.
  bool MatchWithRetain();

  void HandlePotentialUse(BasicBlock *BB, Instruction *Inst, const Value *Ptr,
                          ProvenanceAnalysis &PA, ARCInstKind Class);
  bool HandlePotentialAlterRefCount(Instruction *Inst, const Value *Ptr,
                                    ProvenanceAnalysis &PA, ARCInstKind Class);
};

struct TopDownPtrState : PtrState {
  TopDownPtrState() = default;

  /// (Re-)Initialize this bottom up pointer returning true if we detected a
  /// pointer with nested releases.
  bool InitTopDown(ARCInstKind Kind, Instruction *I);

  /// Return true if this set of retains can be paired with the given
  /// release. Modifies state appropriately to reflect that the matching
  /// occurred.
  bool MatchWithRelease(ARCMDKindCache &Cache, Instruction *Release);

  void HandlePotentialUse(Instruction *Inst, const Value *Ptr,
                          ProvenanceAnalysis &PA, ARCInstKind Class);

  bool HandlePotentialAlterRefCount(Instruction *Inst, const Value *Ptr,
                                    ProvenanceAnalysis &PA, ARCInstKind Class);
};

} // end namespace objcarc

} // end namespace llvm

#endif // LLVM_LIB_TRANSFORMS_OBJCARC_PTRSTATE_H
