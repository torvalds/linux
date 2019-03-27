//===- llvm/CodeGen/SlotIndexes.h - Slot indexes representation -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements SlotIndex and related classes. The purpose of SlotIndex
// is to describe a position at which a register can become live, or cease to
// be live.
//
// SlotIndex is mostly a proxy for entries of the SlotIndexList, a class which
// is held is LiveIntervals and provides the real numbering. This allows
// LiveIntervals to perform largely transparent renumbering.
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SLOTINDEXES_H
#define LLVM_CODEGEN_SLOTINDEXES_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IntervalMap.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/ilist.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/Pass.h"
#include "llvm/Support/Allocator.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <utility>

namespace llvm {

class raw_ostream;

  /// This class represents an entry in the slot index list held in the
  /// SlotIndexes pass. It should not be used directly. See the
  /// SlotIndex & SlotIndexes classes for the public interface to this
  /// information.
  class IndexListEntry : public ilist_node<IndexListEntry> {
    MachineInstr *mi;
    unsigned index;

  public:
    IndexListEntry(MachineInstr *mi, unsigned index) : mi(mi), index(index) {}

    MachineInstr* getInstr() const { return mi; }
    void setInstr(MachineInstr *mi) {
      this->mi = mi;
    }

    unsigned getIndex() const { return index; }
    void setIndex(unsigned index) {
      this->index = index;
    }

#ifdef EXPENSIVE_CHECKS
    // When EXPENSIVE_CHECKS is defined, "erased" index list entries will
    // actually be moved to a "graveyard" list, and have their pointers
    // poisoned, so that dangling SlotIndex access can be reliably detected.
    void setPoison() {
      intptr_t tmp = reinterpret_cast<intptr_t>(mi);
      assert(((tmp & 0x1) == 0x0) && "Pointer already poisoned?");
      tmp |= 0x1;
      mi = reinterpret_cast<MachineInstr*>(tmp);
    }

    bool isPoisoned() const { return (reinterpret_cast<intptr_t>(mi) & 0x1) == 0x1; }
#endif // EXPENSIVE_CHECKS
  };

  template <>
  struct ilist_alloc_traits<IndexListEntry>
      : public ilist_noalloc_traits<IndexListEntry> {};

  /// SlotIndex - An opaque wrapper around machine indexes.
  class SlotIndex {
    friend class SlotIndexes;

    enum Slot {
      /// Basic block boundary.  Used for live ranges entering and leaving a
      /// block without being live in the layout neighbor.  Also used as the
      /// def slot of PHI-defs.
      Slot_Block,

      /// Early-clobber register use/def slot.  A live range defined at
      /// Slot_EarlyClobber interferes with normal live ranges killed at
      /// Slot_Register.  Also used as the kill slot for live ranges tied to an
      /// early-clobber def.
      Slot_EarlyClobber,

      /// Normal register use/def slot.  Normal instructions kill and define
      /// register live ranges at this slot.
      Slot_Register,

      /// Dead def kill point.  Kill slot for a live range that is defined by
      /// the same instruction (Slot_Register or Slot_EarlyClobber), but isn't
      /// used anywhere.
      Slot_Dead,

      Slot_Count
    };

    PointerIntPair<IndexListEntry*, 2, unsigned> lie;

    SlotIndex(IndexListEntry *entry, unsigned slot)
      : lie(entry, slot) {}

    IndexListEntry* listEntry() const {
      assert(isValid() && "Attempt to compare reserved index.");
#ifdef EXPENSIVE_CHECKS
      assert(!lie.getPointer()->isPoisoned() &&
             "Attempt to access deleted list-entry.");
#endif // EXPENSIVE_CHECKS
      return lie.getPointer();
    }

    unsigned getIndex() const {
      return listEntry()->getIndex() | getSlot();
    }

    /// Returns the slot for this SlotIndex.
    Slot getSlot() const {
      return static_cast<Slot>(lie.getInt());
    }

  public:
    enum {
      /// The default distance between instructions as returned by distance().
      /// This may vary as instructions are inserted and removed.
      InstrDist = 4 * Slot_Count
    };

    /// Construct an invalid index.
    SlotIndex() = default;

    // Construct a new slot index from the given one, and set the slot.
    SlotIndex(const SlotIndex &li, Slot s) : lie(li.listEntry(), unsigned(s)) {
      assert(lie.getPointer() != nullptr &&
             "Attempt to construct index with 0 pointer.");
    }

    /// Returns true if this is a valid index. Invalid indices do
    /// not point into an index table, and cannot be compared.
    bool isValid() const {
      return lie.getPointer();
    }

    /// Return true for a valid index.
    explicit operator bool() const { return isValid(); }

    /// Print this index to the given raw_ostream.
    void print(raw_ostream &os) const;

    /// Dump this index to stderr.
    void dump() const;

    /// Compare two SlotIndex objects for equality.
    bool operator==(SlotIndex other) const {
      return lie == other.lie;
    }
    /// Compare two SlotIndex objects for inequality.
    bool operator!=(SlotIndex other) const {
      return lie != other.lie;
    }

    /// Compare two SlotIndex objects. Return true if the first index
    /// is strictly lower than the second.
    bool operator<(SlotIndex other) const {
      return getIndex() < other.getIndex();
    }
    /// Compare two SlotIndex objects. Return true if the first index
    /// is lower than, or equal to, the second.
    bool operator<=(SlotIndex other) const {
      return getIndex() <= other.getIndex();
    }

    /// Compare two SlotIndex objects. Return true if the first index
    /// is greater than the second.
    bool operator>(SlotIndex other) const {
      return getIndex() > other.getIndex();
    }

    /// Compare two SlotIndex objects. Return true if the first index
    /// is greater than, or equal to, the second.
    bool operator>=(SlotIndex other) const {
      return getIndex() >= other.getIndex();
    }

    /// isSameInstr - Return true if A and B refer to the same instruction.
    static bool isSameInstr(SlotIndex A, SlotIndex B) {
      return A.lie.getPointer() == B.lie.getPointer();
    }

    /// isEarlierInstr - Return true if A refers to an instruction earlier than
    /// B. This is equivalent to A < B && !isSameInstr(A, B).
    static bool isEarlierInstr(SlotIndex A, SlotIndex B) {
      return A.listEntry()->getIndex() < B.listEntry()->getIndex();
    }

    /// Return true if A refers to the same instruction as B or an earlier one.
    /// This is equivalent to !isEarlierInstr(B, A).
    static bool isEarlierEqualInstr(SlotIndex A, SlotIndex B) {
      return !isEarlierInstr(B, A);
    }

    /// Return the distance from this index to the given one.
    int distance(SlotIndex other) const {
      return other.getIndex() - getIndex();
    }

    /// Return the scaled distance from this index to the given one, where all
    /// slots on the same instruction have zero distance.
    int getInstrDistance(SlotIndex other) const {
      return (other.listEntry()->getIndex() - listEntry()->getIndex())
        / Slot_Count;
    }

    /// isBlock - Returns true if this is a block boundary slot.
    bool isBlock() const { return getSlot() == Slot_Block; }

    /// isEarlyClobber - Returns true if this is an early-clobber slot.
    bool isEarlyClobber() const { return getSlot() == Slot_EarlyClobber; }

    /// isRegister - Returns true if this is a normal register use/def slot.
    /// Note that early-clobber slots may also be used for uses and defs.
    bool isRegister() const { return getSlot() == Slot_Register; }

    /// isDead - Returns true if this is a dead def kill slot.
    bool isDead() const { return getSlot() == Slot_Dead; }

    /// Returns the base index for associated with this index. The base index
    /// is the one associated with the Slot_Block slot for the instruction
    /// pointed to by this index.
    SlotIndex getBaseIndex() const {
      return SlotIndex(listEntry(), Slot_Block);
    }

    /// Returns the boundary index for associated with this index. The boundary
    /// index is the one associated with the Slot_Block slot for the instruction
    /// pointed to by this index.
    SlotIndex getBoundaryIndex() const {
      return SlotIndex(listEntry(), Slot_Dead);
    }

    /// Returns the register use/def slot in the current instruction for a
    /// normal or early-clobber def.
    SlotIndex getRegSlot(bool EC = false) const {
      return SlotIndex(listEntry(), EC ? Slot_EarlyClobber : Slot_Register);
    }

    /// Returns the dead def kill slot for the current instruction.
    SlotIndex getDeadSlot() const {
      return SlotIndex(listEntry(), Slot_Dead);
    }

    /// Returns the next slot in the index list. This could be either the
    /// next slot for the instruction pointed to by this index or, if this
    /// index is a STORE, the first slot for the next instruction.
    /// WARNING: This method is considerably more expensive than the methods
    /// that return specific slots (getUseIndex(), etc). If you can - please
    /// use one of those methods.
    SlotIndex getNextSlot() const {
      Slot s = getSlot();
      if (s == Slot_Dead) {
        return SlotIndex(&*++listEntry()->getIterator(), Slot_Block);
      }
      return SlotIndex(listEntry(), s + 1);
    }

    /// Returns the next index. This is the index corresponding to the this
    /// index's slot, but for the next instruction.
    SlotIndex getNextIndex() const {
      return SlotIndex(&*++listEntry()->getIterator(), getSlot());
    }

    /// Returns the previous slot in the index list. This could be either the
    /// previous slot for the instruction pointed to by this index or, if this
    /// index is a Slot_Block, the last slot for the previous instruction.
    /// WARNING: This method is considerably more expensive than the methods
    /// that return specific slots (getUseIndex(), etc). If you can - please
    /// use one of those methods.
    SlotIndex getPrevSlot() const {
      Slot s = getSlot();
      if (s == Slot_Block) {
        return SlotIndex(&*--listEntry()->getIterator(), Slot_Dead);
      }
      return SlotIndex(listEntry(), s - 1);
    }

    /// Returns the previous index. This is the index corresponding to this
    /// index's slot, but for the previous instruction.
    SlotIndex getPrevIndex() const {
      return SlotIndex(&*--listEntry()->getIterator(), getSlot());
    }
  };

  template <> struct isPodLike<SlotIndex> { static const bool value = true; };

  inline raw_ostream& operator<<(raw_ostream &os, SlotIndex li) {
    li.print(os);
    return os;
  }

  using IdxMBBPair = std::pair<SlotIndex, MachineBasicBlock *>;

  inline bool operator<(SlotIndex V, const IdxMBBPair &IM) {
    return V < IM.first;
  }

  inline bool operator<(const IdxMBBPair &IM, SlotIndex V) {
    return IM.first < V;
  }

  struct Idx2MBBCompare {
    bool operator()(const IdxMBBPair &LHS, const IdxMBBPair &RHS) const {
      return LHS.first < RHS.first;
    }
  };

  /// SlotIndexes pass.
  ///
  /// This pass assigns indexes to each instruction.
  class SlotIndexes : public MachineFunctionPass {
  private:
    // IndexListEntry allocator.
    BumpPtrAllocator ileAllocator;

    using IndexList = ilist<IndexListEntry>;
    IndexList indexList;

#ifdef EXPENSIVE_CHECKS
    IndexList graveyardList;
#endif // EXPENSIVE_CHECKS

    MachineFunction *mf;

    using Mi2IndexMap = DenseMap<const MachineInstr *, SlotIndex>;
    Mi2IndexMap mi2iMap;

    /// MBBRanges - Map MBB number to (start, stop) indexes.
    SmallVector<std::pair<SlotIndex, SlotIndex>, 8> MBBRanges;

    /// Idx2MBBMap - Sorted list of pairs of index of first instruction
    /// and MBB id.
    SmallVector<IdxMBBPair, 8> idx2MBBMap;

    IndexListEntry* createEntry(MachineInstr *mi, unsigned index) {
      IndexListEntry *entry =
          static_cast<IndexListEntry *>(ileAllocator.Allocate(
              sizeof(IndexListEntry), alignof(IndexListEntry)));

      new (entry) IndexListEntry(mi, index);

      return entry;
    }

    /// Renumber locally after inserting curItr.
    void renumberIndexes(IndexList::iterator curItr);

  public:
    static char ID;

    SlotIndexes() : MachineFunctionPass(ID) {
      initializeSlotIndexesPass(*PassRegistry::getPassRegistry());
    }

    ~SlotIndexes() override {
      // The indexList's nodes are all allocated in the BumpPtrAllocator.
      indexList.clearAndLeakNodesUnsafely();
    }

    void getAnalysisUsage(AnalysisUsage &au) const override;
    void releaseMemory() override;

    bool runOnMachineFunction(MachineFunction &fn) override;

    /// Dump the indexes.
    void dump() const;

    /// Renumber the index list, providing space for new instructions.
    void renumberIndexes();

    /// Repair indexes after adding and removing instructions.
    void repairIndexesInRange(MachineBasicBlock *MBB,
                              MachineBasicBlock::iterator Begin,
                              MachineBasicBlock::iterator End);

    /// Returns the zero index for this analysis.
    SlotIndex getZeroIndex() {
      assert(indexList.front().getIndex() == 0 && "First index is not 0?");
      return SlotIndex(&indexList.front(), 0);
    }

    /// Returns the base index of the last slot in this analysis.
    SlotIndex getLastIndex() {
      return SlotIndex(&indexList.back(), 0);
    }

    /// Returns true if the given machine instr is mapped to an index,
    /// otherwise returns false.
    bool hasIndex(const MachineInstr &instr) const {
      return mi2iMap.count(&instr);
    }

    /// Returns the base index for the given instruction.
    SlotIndex getInstructionIndex(const MachineInstr &MI) const {
      // Instructions inside a bundle have the same number as the bundle itself.
      auto BundleStart = getBundleStart(MI.getIterator());
      auto BundleEnd = getBundleEnd(MI.getIterator());
      // Use the first non-debug instruction in the bundle to get SlotIndex.
      const MachineInstr &BundleNonDebug =
          *skipDebugInstructionsForward(BundleStart, BundleEnd);
      assert(!BundleNonDebug.isDebugInstr() &&
             "Could not use a debug instruction to query mi2iMap.");
      Mi2IndexMap::const_iterator itr = mi2iMap.find(&BundleNonDebug);
      assert(itr != mi2iMap.end() && "Instruction not found in maps.");
      return itr->second;
    }

    /// Returns the instruction for the given index, or null if the given
    /// index has no instruction associated with it.
    MachineInstr* getInstructionFromIndex(SlotIndex index) const {
      return index.isValid() ? index.listEntry()->getInstr() : nullptr;
    }

    /// Returns the next non-null index, if one exists.
    /// Otherwise returns getLastIndex().
    SlotIndex getNextNonNullIndex(SlotIndex Index) {
      IndexList::iterator I = Index.listEntry()->getIterator();
      IndexList::iterator E = indexList.end();
      while (++I != E)
        if (I->getInstr())
          return SlotIndex(&*I, Index.getSlot());
      // We reached the end of the function.
      return getLastIndex();
    }

    /// getIndexBefore - Returns the index of the last indexed instruction
    /// before MI, or the start index of its basic block.
    /// MI is not required to have an index.
    SlotIndex getIndexBefore(const MachineInstr &MI) const {
      const MachineBasicBlock *MBB = MI.getParent();
      assert(MBB && "MI must be inserted in a basic block");
      MachineBasicBlock::const_iterator I = MI, B = MBB->begin();
      while (true) {
        if (I == B)
          return getMBBStartIdx(MBB);
        --I;
        Mi2IndexMap::const_iterator MapItr = mi2iMap.find(&*I);
        if (MapItr != mi2iMap.end())
          return MapItr->second;
      }
    }

    /// getIndexAfter - Returns the index of the first indexed instruction
    /// after MI, or the end index of its basic block.
    /// MI is not required to have an index.
    SlotIndex getIndexAfter(const MachineInstr &MI) const {
      const MachineBasicBlock *MBB = MI.getParent();
      assert(MBB && "MI must be inserted in a basic block");
      MachineBasicBlock::const_iterator I = MI, E = MBB->end();
      while (true) {
        ++I;
        if (I == E)
          return getMBBEndIdx(MBB);
        Mi2IndexMap::const_iterator MapItr = mi2iMap.find(&*I);
        if (MapItr != mi2iMap.end())
          return MapItr->second;
      }
    }

    /// Return the (start,end) range of the given basic block number.
    const std::pair<SlotIndex, SlotIndex> &
    getMBBRange(unsigned Num) const {
      return MBBRanges[Num];
    }

    /// Return the (start,end) range of the given basic block.
    const std::pair<SlotIndex, SlotIndex> &
    getMBBRange(const MachineBasicBlock *MBB) const {
      return getMBBRange(MBB->getNumber());
    }

    /// Returns the first index in the given basic block number.
    SlotIndex getMBBStartIdx(unsigned Num) const {
      return getMBBRange(Num).first;
    }

    /// Returns the first index in the given basic block.
    SlotIndex getMBBStartIdx(const MachineBasicBlock *mbb) const {
      return getMBBRange(mbb).first;
    }

    /// Returns the last index in the given basic block number.
    SlotIndex getMBBEndIdx(unsigned Num) const {
      return getMBBRange(Num).second;
    }

    /// Returns the last index in the given basic block.
    SlotIndex getMBBEndIdx(const MachineBasicBlock *mbb) const {
      return getMBBRange(mbb).second;
    }

    /// Iterator over the idx2MBBMap (sorted pairs of slot index of basic block
    /// begin and basic block)
    using MBBIndexIterator = SmallVectorImpl<IdxMBBPair>::const_iterator;

    /// Move iterator to the next IdxMBBPair where the SlotIndex is greater or
    /// equal to \p To.
    MBBIndexIterator advanceMBBIndex(MBBIndexIterator I, SlotIndex To) const {
      return std::lower_bound(I, idx2MBBMap.end(), To);
    }

    /// Get an iterator pointing to the IdxMBBPair with the biggest SlotIndex
    /// that is greater or equal to \p Idx.
    MBBIndexIterator findMBBIndex(SlotIndex Idx) const {
      return advanceMBBIndex(idx2MBBMap.begin(), Idx);
    }

    /// Returns an iterator for the begin of the idx2MBBMap.
    MBBIndexIterator MBBIndexBegin() const {
      return idx2MBBMap.begin();
    }

    /// Return an iterator for the end of the idx2MBBMap.
    MBBIndexIterator MBBIndexEnd() const {
      return idx2MBBMap.end();
    }

    /// Returns the basic block which the given index falls in.
    MachineBasicBlock* getMBBFromIndex(SlotIndex index) const {
      if (MachineInstr *MI = getInstructionFromIndex(index))
        return MI->getParent();

      MBBIndexIterator I = findMBBIndex(index);
      // Take the pair containing the index
      MBBIndexIterator J =
        ((I != MBBIndexEnd() && I->first > index) ||
         (I == MBBIndexEnd() && !idx2MBBMap.empty())) ? std::prev(I) : I;

      assert(J != MBBIndexEnd() && J->first <= index &&
             index < getMBBEndIdx(J->second) &&
             "index does not correspond to an MBB");
      return J->second;
    }

    /// Returns the MBB covering the given range, or null if the range covers
    /// more than one basic block.
    MachineBasicBlock* getMBBCoveringRange(SlotIndex start, SlotIndex end) const {

      assert(start < end && "Backwards ranges not allowed.");
      MBBIndexIterator itr = findMBBIndex(start);
      if (itr == MBBIndexEnd()) {
        itr = std::prev(itr);
        return itr->second;
      }

      // Check that we don't cross the boundary into this block.
      if (itr->first < end)
        return nullptr;

      itr = std::prev(itr);

      if (itr->first <= start)
        return itr->second;

      return nullptr;
    }

    /// Insert the given machine instruction into the mapping. Returns the
    /// assigned index.
    /// If Late is set and there are null indexes between mi's neighboring
    /// instructions, create the new index after the null indexes instead of
    /// before them.
    SlotIndex insertMachineInstrInMaps(MachineInstr &MI, bool Late = false) {
      assert(!MI.isInsideBundle() &&
             "Instructions inside bundles should use bundle start's slot.");
      assert(mi2iMap.find(&MI) == mi2iMap.end() && "Instr already indexed.");
      // Numbering debug instructions could cause code generation to be
      // affected by debug information.
      assert(!MI.isDebugInstr() && "Cannot number debug instructions.");

      assert(MI.getParent() != nullptr && "Instr must be added to function.");

      // Get the entries where MI should be inserted.
      IndexList::iterator prevItr, nextItr;
      if (Late) {
        // Insert MI's index immediately before the following instruction.
        nextItr = getIndexAfter(MI).listEntry()->getIterator();
        prevItr = std::prev(nextItr);
      } else {
        // Insert MI's index immediately after the preceding instruction.
        prevItr = getIndexBefore(MI).listEntry()->getIterator();
        nextItr = std::next(prevItr);
      }

      // Get a number for the new instr, or 0 if there's no room currently.
      // In the latter case we'll force a renumber later.
      unsigned dist = ((nextItr->getIndex() - prevItr->getIndex())/2) & ~3u;
      unsigned newNumber = prevItr->getIndex() + dist;

      // Insert a new list entry for MI.
      IndexList::iterator newItr =
          indexList.insert(nextItr, createEntry(&MI, newNumber));

      // Renumber locally if we need to.
      if (dist == 0)
        renumberIndexes(newItr);

      SlotIndex newIndex(&*newItr, SlotIndex::Slot_Block);
      mi2iMap.insert(std::make_pair(&MI, newIndex));
      return newIndex;
    }

    /// Removes machine instruction (bundle) \p MI from the mapping.
    /// This should be called before MachineInstr::eraseFromParent() is used to
    /// remove a whole bundle or an unbundled instruction.
    void removeMachineInstrFromMaps(MachineInstr &MI);

    /// Removes a single machine instruction \p MI from the mapping.
    /// This should be called before MachineInstr::eraseFromBundle() is used to
    /// remove a single instruction (out of a bundle).
    void removeSingleMachineInstrFromMaps(MachineInstr &MI);

    /// ReplaceMachineInstrInMaps - Replacing a machine instr with a new one in
    /// maps used by register allocator. \returns the index where the new
    /// instruction was inserted.
    SlotIndex replaceMachineInstrInMaps(MachineInstr &MI, MachineInstr &NewMI) {
      Mi2IndexMap::iterator mi2iItr = mi2iMap.find(&MI);
      if (mi2iItr == mi2iMap.end())
        return SlotIndex();
      SlotIndex replaceBaseIndex = mi2iItr->second;
      IndexListEntry *miEntry(replaceBaseIndex.listEntry());
      assert(miEntry->getInstr() == &MI &&
             "Mismatched instruction in index tables.");
      miEntry->setInstr(&NewMI);
      mi2iMap.erase(mi2iItr);
      mi2iMap.insert(std::make_pair(&NewMI, replaceBaseIndex));
      return replaceBaseIndex;
    }

    /// Add the given MachineBasicBlock into the maps.
    void insertMBBInMaps(MachineBasicBlock *mbb) {
      MachineFunction::iterator nextMBB =
        std::next(MachineFunction::iterator(mbb));

      IndexListEntry *startEntry = nullptr;
      IndexListEntry *endEntry = nullptr;
      IndexList::iterator newItr;
      if (nextMBB == mbb->getParent()->end()) {
        startEntry = &indexList.back();
        endEntry = createEntry(nullptr, 0);
        newItr = indexList.insertAfter(startEntry->getIterator(), endEntry);
      } else {
        startEntry = createEntry(nullptr, 0);
        endEntry = getMBBStartIdx(&*nextMBB).listEntry();
        newItr = indexList.insert(endEntry->getIterator(), startEntry);
      }

      SlotIndex startIdx(startEntry, SlotIndex::Slot_Block);
      SlotIndex endIdx(endEntry, SlotIndex::Slot_Block);

      MachineFunction::iterator prevMBB(mbb);
      assert(prevMBB != mbb->getParent()->end() &&
             "Can't insert a new block at the beginning of a function.");
      --prevMBB;
      MBBRanges[prevMBB->getNumber()].second = startIdx;

      assert(unsigned(mbb->getNumber()) == MBBRanges.size() &&
             "Blocks must be added in order");
      MBBRanges.push_back(std::make_pair(startIdx, endIdx));
      idx2MBBMap.push_back(IdxMBBPair(startIdx, mbb));

      renumberIndexes(newItr);
      llvm::sort(idx2MBBMap, Idx2MBBCompare());
    }

    /// Free the resources that were required to maintain a SlotIndex.
    ///
    /// Once an index is no longer needed (for instance because the instruction
    /// at that index has been moved), the resources required to maintain the
    /// index can be relinquished to reduce memory use and improve renumbering
    /// performance. Any remaining SlotIndex objects that point to the same
    /// index are left 'dangling' (much the same as a dangling pointer to a
    /// freed object) and should not be accessed, except to destruct them.
    ///
    /// Like dangling pointers, access to dangling SlotIndexes can cause
    /// painful-to-track-down bugs, especially if the memory for the index
    /// previously pointed to has been re-used. To detect dangling SlotIndex
    /// bugs, build with EXPENSIVE_CHECKS=1. This will cause "erased" indexes to
    /// be retained in a graveyard instead of being freed. Operations on indexes
    /// in the graveyard will trigger an assertion.
    void eraseIndex(SlotIndex index) {
      IndexListEntry *entry = index.listEntry();
#ifdef EXPENSIVE_CHECKS
      indexList.remove(entry);
      graveyardList.push_back(entry);
      entry->setPoison();
#else
      indexList.erase(entry);
#endif
    }
  };

  // Specialize IntervalMapInfo for half-open slot index intervals.
  template <>
  struct IntervalMapInfo<SlotIndex> : IntervalMapHalfOpenInfo<SlotIndex> {
  };

} // end namespace llvm

#endif // LLVM_CODEGEN_SLOTINDEXES_H
