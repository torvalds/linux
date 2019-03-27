//===- llvm/Analysis/VectorUtils.h - Vector utilities -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines some vectorizer utilities.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_VECTORUTILS_H
#define LLVM_ANALYSIS_VECTORUTILS_H

#include "llvm/ADT/MapVector.h"
#include "llvm/Analysis/LoopAccessAnalysis.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/IRBuilder.h"

namespace llvm {

template <typename T> class ArrayRef;
class DemandedBits;
class GetElementPtrInst;
template <typename InstTy> class InterleaveGroup;
class Loop;
class ScalarEvolution;
class TargetTransformInfo;
class Type;
class Value;

namespace Intrinsic {
enum ID : unsigned;
}

/// Identify if the intrinsic is trivially vectorizable.
/// This method returns true if the intrinsic's argument types are all
/// scalars for the scalar form of the intrinsic and all vectors for
/// the vector form of the intrinsic.
bool isTriviallyVectorizable(Intrinsic::ID ID);

/// Identifies if the intrinsic has a scalar operand. It checks for
/// ctlz,cttz and powi special intrinsics whose argument is scalar.
bool hasVectorInstrinsicScalarOpd(Intrinsic::ID ID, unsigned ScalarOpdIdx);

/// Returns intrinsic ID for call.
/// For the input call instruction it finds mapping intrinsic and returns
/// its intrinsic ID, in case it does not found it return not_intrinsic.
Intrinsic::ID getVectorIntrinsicIDForCall(const CallInst *CI,
                                          const TargetLibraryInfo *TLI);

/// Find the operand of the GEP that should be checked for consecutive
/// stores. This ignores trailing indices that have no effect on the final
/// pointer.
unsigned getGEPInductionOperand(const GetElementPtrInst *Gep);

/// If the argument is a GEP, then returns the operand identified by
/// getGEPInductionOperand. However, if there is some other non-loop-invariant
/// operand, it returns that instead.
Value *stripGetElementPtr(Value *Ptr, ScalarEvolution *SE, Loop *Lp);

/// If a value has only one user that is a CastInst, return it.
Value *getUniqueCastUse(Value *Ptr, Loop *Lp, Type *Ty);

/// Get the stride of a pointer access in a loop. Looks for symbolic
/// strides "a[i*stride]". Returns the symbolic stride, or null otherwise.
Value *getStrideFromPointer(Value *Ptr, ScalarEvolution *SE, Loop *Lp);

/// Given a vector and an element number, see if the scalar value is
/// already around as a register, for example if it were inserted then extracted
/// from the vector.
Value *findScalarElement(Value *V, unsigned EltNo);

/// Get splat value if the input is a splat vector or return nullptr.
/// The value may be extracted from a splat constants vector or from
/// a sequence of instructions that broadcast a single value into a vector.
const Value *getSplatValue(const Value *V);

/// Compute a map of integer instructions to their minimum legal type
/// size.
///
/// C semantics force sub-int-sized values (e.g. i8, i16) to be promoted to int
/// type (e.g. i32) whenever arithmetic is performed on them.
///
/// For targets with native i8 or i16 operations, usually InstCombine can shrink
/// the arithmetic type down again. However InstCombine refuses to create
/// illegal types, so for targets without i8 or i16 registers, the lengthening
/// and shrinking remains.
///
/// Most SIMD ISAs (e.g. NEON) however support vectors of i8 or i16 even when
/// their scalar equivalents do not, so during vectorization it is important to
/// remove these lengthens and truncates when deciding the profitability of
/// vectorization.
///
/// This function analyzes the given range of instructions and determines the
/// minimum type size each can be converted to. It attempts to remove or
/// minimize type size changes across each def-use chain, so for example in the
/// following code:
///
///   %1 = load i8, i8*
///   %2 = add i8 %1, 2
///   %3 = load i16, i16*
///   %4 = zext i8 %2 to i32
///   %5 = zext i16 %3 to i32
///   %6 = add i32 %4, %5
///   %7 = trunc i32 %6 to i16
///
/// Instruction %6 must be done at least in i16, so computeMinimumValueSizes
/// will return: {%1: 16, %2: 16, %3: 16, %4: 16, %5: 16, %6: 16, %7: 16}.
///
/// If the optional TargetTransformInfo is provided, this function tries harder
/// to do less work by only looking at illegal types.
MapVector<Instruction*, uint64_t>
computeMinimumValueSizes(ArrayRef<BasicBlock*> Blocks,
                         DemandedBits &DB,
                         const TargetTransformInfo *TTI=nullptr);

/// Compute the union of two access-group lists.
///
/// If the list contains just one access group, it is returned directly. If the
/// list is empty, returns nullptr.
MDNode *uniteAccessGroups(MDNode *AccGroups1, MDNode *AccGroups2);

/// Compute the access-group list of access groups that @p Inst1 and @p Inst2
/// are both in. If either instruction does not access memory at all, it is
/// considered to be in every list.
///
/// If the list contains just one access group, it is returned directly. If the
/// list is empty, returns nullptr.
MDNode *intersectAccessGroups(const Instruction *Inst1,
                              const Instruction *Inst2);

/// Specifically, let Kinds = [MD_tbaa, MD_alias_scope, MD_noalias, MD_fpmath,
/// MD_nontemporal, MD_access_group].
/// For K in Kinds, we get the MDNode for K from each of the
/// elements of VL, compute their "intersection" (i.e., the most generic
/// metadata value that covers all of the individual values), and set I's
/// metadata for M equal to the intersection value.
///
/// This function always sets a (possibly null) value for each K in Kinds.
Instruction *propagateMetadata(Instruction *I, ArrayRef<Value *> VL);

/// Create a mask that filters the members of an interleave group where there
/// are gaps.
///
/// For example, the mask for \p Group with interleave-factor 3
/// and \p VF 4, that has only its first member present is:
///
///   <1,0,0,1,0,0,1,0,0,1,0,0>
///
/// Note: The result is a mask of 0's and 1's, as opposed to the other
/// create[*]Mask() utilities which create a shuffle mask (mask that
/// consists of indices).
Constant *createBitMaskForGaps(IRBuilder<> &Builder, unsigned VF,
                               const InterleaveGroup<Instruction> &Group);

/// Create a mask with replicated elements.
///
/// This function creates a shuffle mask for replicating each of the \p VF
/// elements in a vector \p ReplicationFactor times. It can be used to
/// transform a mask of \p VF elements into a mask of
/// \p VF * \p ReplicationFactor elements used by a predicated
/// interleaved-group of loads/stores whose Interleaved-factor ==
/// \p ReplicationFactor.
///
/// For example, the mask for \p ReplicationFactor=3 and \p VF=4 is:
///
///   <0,0,0,1,1,1,2,2,2,3,3,3>
Constant *createReplicatedMask(IRBuilder<> &Builder, unsigned ReplicationFactor,
                               unsigned VF);

/// Create an interleave shuffle mask.
///
/// This function creates a shuffle mask for interleaving \p NumVecs vectors of
/// vectorization factor \p VF into a single wide vector. The mask is of the
/// form:
///
///   <0, VF, VF * 2, ..., VF * (NumVecs - 1), 1, VF + 1, VF * 2 + 1, ...>
///
/// For example, the mask for VF = 4 and NumVecs = 2 is:
///
///   <0, 4, 1, 5, 2, 6, 3, 7>.
Constant *createInterleaveMask(IRBuilder<> &Builder, unsigned VF,
                               unsigned NumVecs);

/// Create a stride shuffle mask.
///
/// This function creates a shuffle mask whose elements begin at \p Start and
/// are incremented by \p Stride. The mask can be used to deinterleave an
/// interleaved vector into separate vectors of vectorization factor \p VF. The
/// mask is of the form:
///
///   <Start, Start + Stride, ..., Start + Stride * (VF - 1)>
///
/// For example, the mask for Start = 0, Stride = 2, and VF = 4 is:
///
///   <0, 2, 4, 6>
Constant *createStrideMask(IRBuilder<> &Builder, unsigned Start,
                           unsigned Stride, unsigned VF);

/// Create a sequential shuffle mask.
///
/// This function creates shuffle mask whose elements are sequential and begin
/// at \p Start.  The mask contains \p NumInts integers and is padded with \p
/// NumUndefs undef values. The mask is of the form:
///
///   <Start, Start + 1, ... Start + NumInts - 1, undef_1, ... undef_NumUndefs>
///
/// For example, the mask for Start = 0, NumInsts = 4, and NumUndefs = 4 is:
///
///   <0, 1, 2, 3, undef, undef, undef, undef>
Constant *createSequentialMask(IRBuilder<> &Builder, unsigned Start,
                               unsigned NumInts, unsigned NumUndefs);

/// Concatenate a list of vectors.
///
/// This function generates code that concatenate the vectors in \p Vecs into a
/// single large vector. The number of vectors should be greater than one, and
/// their element types should be the same. The number of elements in the
/// vectors should also be the same; however, if the last vector has fewer
/// elements, it will be padded with undefs.
Value *concatenateVectors(IRBuilder<> &Builder, ArrayRef<Value *> Vecs);

/// The group of interleaved loads/stores sharing the same stride and
/// close to each other.
///
/// Each member in this group has an index starting from 0, and the largest
/// index should be less than interleaved factor, which is equal to the absolute
/// value of the access's stride.
///
/// E.g. An interleaved load group of factor 4:
///        for (unsigned i = 0; i < 1024; i+=4) {
///          a = A[i];                           // Member of index 0
///          b = A[i+1];                         // Member of index 1
///          d = A[i+3];                         // Member of index 3
///          ...
///        }
///
///      An interleaved store group of factor 4:
///        for (unsigned i = 0; i < 1024; i+=4) {
///          ...
///          A[i]   = a;                         // Member of index 0
///          A[i+1] = b;                         // Member of index 1
///          A[i+2] = c;                         // Member of index 2
///          A[i+3] = d;                         // Member of index 3
///        }
///
/// Note: the interleaved load group could have gaps (missing members), but
/// the interleaved store group doesn't allow gaps.
template <typename InstTy> class InterleaveGroup {
public:
  InterleaveGroup(unsigned Factor, bool Reverse, unsigned Align)
      : Factor(Factor), Reverse(Reverse), Align(Align), InsertPos(nullptr) {}

  InterleaveGroup(InstTy *Instr, int Stride, unsigned Align)
      : Align(Align), InsertPos(Instr) {
    assert(Align && "The alignment should be non-zero");

    Factor = std::abs(Stride);
    assert(Factor > 1 && "Invalid interleave factor");

    Reverse = Stride < 0;
    Members[0] = Instr;
  }

  bool isReverse() const { return Reverse; }
  unsigned getFactor() const { return Factor; }
  unsigned getAlignment() const { return Align; }
  unsigned getNumMembers() const { return Members.size(); }

  /// Try to insert a new member \p Instr with index \p Index and
  /// alignment \p NewAlign. The index is related to the leader and it could be
  /// negative if it is the new leader.
  ///
  /// \returns false if the instruction doesn't belong to the group.
  bool insertMember(InstTy *Instr, int Index, unsigned NewAlign) {
    assert(NewAlign && "The new member's alignment should be non-zero");

    int Key = Index + SmallestKey;

    // Skip if there is already a member with the same index.
    if (Members.find(Key) != Members.end())
      return false;

    if (Key > LargestKey) {
      // The largest index is always less than the interleave factor.
      if (Index >= static_cast<int>(Factor))
        return false;

      LargestKey = Key;
    } else if (Key < SmallestKey) {
      // The largest index is always less than the interleave factor.
      if (LargestKey - Key >= static_cast<int>(Factor))
        return false;

      SmallestKey = Key;
    }

    // It's always safe to select the minimum alignment.
    Align = std::min(Align, NewAlign);
    Members[Key] = Instr;
    return true;
  }

  /// Get the member with the given index \p Index
  ///
  /// \returns nullptr if contains no such member.
  InstTy *getMember(unsigned Index) const {
    int Key = SmallestKey + Index;
    auto Member = Members.find(Key);
    if (Member == Members.end())
      return nullptr;

    return Member->second;
  }

  /// Get the index for the given member. Unlike the key in the member
  /// map, the index starts from 0.
  unsigned getIndex(const InstTy *Instr) const {
    for (auto I : Members) {
      if (I.second == Instr)
        return I.first - SmallestKey;
    }

    llvm_unreachable("InterleaveGroup contains no such member");
  }

  InstTy *getInsertPos() const { return InsertPos; }
  void setInsertPos(InstTy *Inst) { InsertPos = Inst; }

  /// Add metadata (e.g. alias info) from the instructions in this group to \p
  /// NewInst.
  ///
  /// FIXME: this function currently does not add noalias metadata a'la
  /// addNewMedata.  To do that we need to compute the intersection of the
  /// noalias info from all members.
  void addMetadata(InstTy *NewInst) const;

  /// Returns true if this Group requires a scalar iteration to handle gaps.
  bool requiresScalarEpilogue() const {
    // If the last member of the Group exists, then a scalar epilog is not
    // needed for this group.
    if (getMember(getFactor() - 1))
      return false;

    // We have a group with gaps. It therefore cannot be a group of stores,
    // and it can't be a reversed access, because such groups get invalidated.
    assert(!getMember(0)->mayWriteToMemory() &&
           "Group should have been invalidated");
    assert(!isReverse() && "Group should have been invalidated");

    // This is a group of loads, with gaps, and without a last-member
    return true;
  }

private:
  unsigned Factor; // Interleave Factor.
  bool Reverse;
  unsigned Align;
  DenseMap<int, InstTy *> Members;
  int SmallestKey = 0;
  int LargestKey = 0;

  // To avoid breaking dependences, vectorized instructions of an interleave
  // group should be inserted at either the first load or the last store in
  // program order.
  //
  // E.g. %even = load i32             // Insert Position
  //      %add = add i32 %even         // Use of %even
  //      %odd = load i32
  //
  //      store i32 %even
  //      %odd = add i32               // Def of %odd
  //      store i32 %odd               // Insert Position
  InstTy *InsertPos;
};

/// Drive the analysis of interleaved memory accesses in the loop.
///
/// Use this class to analyze interleaved accesses only when we can vectorize
/// a loop. Otherwise it's meaningless to do analysis as the vectorization
/// on interleaved accesses is unsafe.
///
/// The analysis collects interleave groups and records the relationships
/// between the member and the group in a map.
class InterleavedAccessInfo {
public:
  InterleavedAccessInfo(PredicatedScalarEvolution &PSE, Loop *L,
                        DominatorTree *DT, LoopInfo *LI,
                        const LoopAccessInfo *LAI)
      : PSE(PSE), TheLoop(L), DT(DT), LI(LI), LAI(LAI) {}

  ~InterleavedAccessInfo() { reset(); }

  /// Analyze the interleaved accesses and collect them in interleave
  /// groups. Substitute symbolic strides using \p Strides.
  /// Consider also predicated loads/stores in the analysis if
  /// \p EnableMaskedInterleavedGroup is true.
  void analyzeInterleaving(bool EnableMaskedInterleavedGroup);

  /// Invalidate groups, e.g., in case all blocks in loop will be predicated
  /// contrary to original assumption. Although we currently prevent group
  /// formation for predicated accesses, we may be able to relax this limitation
  /// in the future once we handle more complicated blocks.
  void reset() {
    SmallPtrSet<InterleaveGroup<Instruction> *, 4> DelSet;
    // Avoid releasing a pointer twice.
    for (auto &I : InterleaveGroupMap)
      DelSet.insert(I.second);
    for (auto *Ptr : DelSet)
      delete Ptr;
    InterleaveGroupMap.clear();
    RequiresScalarEpilogue = false;
  }


  /// Check if \p Instr belongs to any interleave group.
  bool isInterleaved(Instruction *Instr) const {
    return InterleaveGroupMap.find(Instr) != InterleaveGroupMap.end();
  }

  /// Get the interleave group that \p Instr belongs to.
  ///
  /// \returns nullptr if doesn't have such group.
  InterleaveGroup<Instruction> *
  getInterleaveGroup(const Instruction *Instr) const {
    if (InterleaveGroupMap.count(Instr))
      return InterleaveGroupMap.find(Instr)->second;
    return nullptr;
  }

  iterator_range<SmallPtrSetIterator<llvm::InterleaveGroup<Instruction> *>>
  getInterleaveGroups() {
    return make_range(InterleaveGroups.begin(), InterleaveGroups.end());
  }

  /// Returns true if an interleaved group that may access memory
  /// out-of-bounds requires a scalar epilogue iteration for correctness.
  bool requiresScalarEpilogue() const { return RequiresScalarEpilogue; }

  /// Invalidate groups that require a scalar epilogue (due to gaps). This can
  /// happen when optimizing for size forbids a scalar epilogue, and the gap
  /// cannot be filtered by masking the load/store.
  void invalidateGroupsRequiringScalarEpilogue();

private:
  /// A wrapper around ScalarEvolution, used to add runtime SCEV checks.
  /// Simplifies SCEV expressions in the context of existing SCEV assumptions.
  /// The interleaved access analysis can also add new predicates (for example
  /// by versioning strides of pointers).
  PredicatedScalarEvolution &PSE;

  Loop *TheLoop;
  DominatorTree *DT;
  LoopInfo *LI;
  const LoopAccessInfo *LAI;

  /// True if the loop may contain non-reversed interleaved groups with
  /// out-of-bounds accesses. We ensure we don't speculatively access memory
  /// out-of-bounds by executing at least one scalar epilogue iteration.
  bool RequiresScalarEpilogue = false;

  /// Holds the relationships between the members and the interleave group.
  DenseMap<Instruction *, InterleaveGroup<Instruction> *> InterleaveGroupMap;

  SmallPtrSet<InterleaveGroup<Instruction> *, 4> InterleaveGroups;

  /// Holds dependences among the memory accesses in the loop. It maps a source
  /// access to a set of dependent sink accesses.
  DenseMap<Instruction *, SmallPtrSet<Instruction *, 2>> Dependences;

  /// The descriptor for a strided memory access.
  struct StrideDescriptor {
    StrideDescriptor() = default;
    StrideDescriptor(int64_t Stride, const SCEV *Scev, uint64_t Size,
                     unsigned Align)
        : Stride(Stride), Scev(Scev), Size(Size), Align(Align) {}

    // The access's stride. It is negative for a reverse access.
    int64_t Stride = 0;

    // The scalar expression of this access.
    const SCEV *Scev = nullptr;

    // The size of the memory object.
    uint64_t Size = 0;

    // The alignment of this access.
    unsigned Align = 0;
  };

  /// A type for holding instructions and their stride descriptors.
  using StrideEntry = std::pair<Instruction *, StrideDescriptor>;

  /// Create a new interleave group with the given instruction \p Instr,
  /// stride \p Stride and alignment \p Align.
  ///
  /// \returns the newly created interleave group.
  InterleaveGroup<Instruction> *
  createInterleaveGroup(Instruction *Instr, int Stride, unsigned Align) {
    assert(!InterleaveGroupMap.count(Instr) &&
           "Already in an interleaved access group");
    InterleaveGroupMap[Instr] =
        new InterleaveGroup<Instruction>(Instr, Stride, Align);
    InterleaveGroups.insert(InterleaveGroupMap[Instr]);
    return InterleaveGroupMap[Instr];
  }

  /// Release the group and remove all the relationships.
  void releaseGroup(InterleaveGroup<Instruction> *Group) {
    for (unsigned i = 0; i < Group->getFactor(); i++)
      if (Instruction *Member = Group->getMember(i))
        InterleaveGroupMap.erase(Member);

    InterleaveGroups.erase(Group);
    delete Group;
  }

  /// Collect all the accesses with a constant stride in program order.
  void collectConstStrideAccesses(
      MapVector<Instruction *, StrideDescriptor> &AccessStrideInfo,
      const ValueToValueMap &Strides);

  /// Returns true if \p Stride is allowed in an interleaved group.
  static bool isStrided(int Stride);

  /// Returns true if \p BB is a predicated block.
  bool isPredicated(BasicBlock *BB) const {
    return LoopAccessInfo::blockNeedsPredication(BB, TheLoop, DT);
  }

  /// Returns true if LoopAccessInfo can be used for dependence queries.
  bool areDependencesValid() const {
    return LAI && LAI->getDepChecker().getDependences();
  }

  /// Returns true if memory accesses \p A and \p B can be reordered, if
  /// necessary, when constructing interleaved groups.
  ///
  /// \p A must precede \p B in program order. We return false if reordering is
  /// not necessary or is prevented because \p A and \p B may be dependent.
  bool canReorderMemAccessesForInterleavedGroups(StrideEntry *A,
                                                 StrideEntry *B) const {
    // Code motion for interleaved accesses can potentially hoist strided loads
    // and sink strided stores. The code below checks the legality of the
    // following two conditions:
    //
    // 1. Potentially moving a strided load (B) before any store (A) that
    //    precedes B, or
    //
    // 2. Potentially moving a strided store (A) after any load or store (B)
    //    that A precedes.
    //
    // It's legal to reorder A and B if we know there isn't a dependence from A
    // to B. Note that this determination is conservative since some
    // dependences could potentially be reordered safely.

    // A is potentially the source of a dependence.
    auto *Src = A->first;
    auto SrcDes = A->second;

    // B is potentially the sink of a dependence.
    auto *Sink = B->first;
    auto SinkDes = B->second;

    // Code motion for interleaved accesses can't violate WAR dependences.
    // Thus, reordering is legal if the source isn't a write.
    if (!Src->mayWriteToMemory())
      return true;

    // At least one of the accesses must be strided.
    if (!isStrided(SrcDes.Stride) && !isStrided(SinkDes.Stride))
      return true;

    // If dependence information is not available from LoopAccessInfo,
    // conservatively assume the instructions can't be reordered.
    if (!areDependencesValid())
      return false;

    // If we know there is a dependence from source to sink, assume the
    // instructions can't be reordered. Otherwise, reordering is legal.
    return Dependences.find(Src) == Dependences.end() ||
           !Dependences.lookup(Src).count(Sink);
  }

  /// Collect the dependences from LoopAccessInfo.
  ///
  /// We process the dependences once during the interleaved access analysis to
  /// enable constant-time dependence queries.
  void collectDependences() {
    if (!areDependencesValid())
      return;
    auto *Deps = LAI->getDepChecker().getDependences();
    for (auto Dep : *Deps)
      Dependences[Dep.getSource(*LAI)].insert(Dep.getDestination(*LAI));
  }
};

} // llvm namespace

#endif
