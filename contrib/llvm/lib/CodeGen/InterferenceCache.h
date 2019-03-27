//===- InterferenceCache.h - Caching per-block interference ----*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// InterferenceCache remembers per-block interference from LiveIntervalUnions,
// fixed RegUnit interference, and register masks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_INTERFERENCECACHE_H
#define LLVM_LIB_CODEGEN_INTERFERENCECACHE_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervalUnion.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstddef>
#include <cstdlib>

namespace llvm {

class LiveIntervals;
class MachineFunction;
class TargetRegisterInfo;

class LLVM_LIBRARY_VISIBILITY InterferenceCache {
  /// BlockInterference - information about the interference in a single basic
  /// block.
  struct BlockInterference {
    unsigned Tag = 0;
    SlotIndex First;
    SlotIndex Last;

    BlockInterference() {}
  };

  /// Entry - A cache entry containing interference information for all aliases
  /// of PhysReg in all basic blocks.
  class Entry {
    /// PhysReg - The register currently represented.
    unsigned PhysReg = 0;

    /// Tag - Cache tag is changed when any of the underlying LiveIntervalUnions
    /// change.
    unsigned Tag = 0;

    /// RefCount - The total number of Cursor instances referring to this Entry.
    unsigned RefCount = 0;

    /// MF - The current function.
    MachineFunction *MF;

    /// Indexes - Mapping block numbers to SlotIndex ranges.
    SlotIndexes *Indexes = nullptr;

    /// LIS - Used for accessing register mask interference maps.
    LiveIntervals *LIS = nullptr;

    /// PrevPos - The previous position the iterators were moved to.
    SlotIndex PrevPos;

    /// RegUnitInfo - Information tracked about each RegUnit in PhysReg.
    /// When PrevPos is set, the iterators are valid as if advanceTo(PrevPos)
    /// had just been called.
    struct RegUnitInfo {
      /// Iterator pointing into the LiveIntervalUnion containing virtual
      /// register interference.
      LiveIntervalUnion::SegmentIter VirtI;

      /// Tag of the LIU last time we looked.
      unsigned VirtTag;

      /// Fixed interference in RegUnit.
      LiveRange *Fixed = nullptr;

      /// Iterator pointing into the fixed RegUnit interference.
      LiveInterval::iterator FixedI;

      RegUnitInfo(LiveIntervalUnion &LIU) : VirtTag(LIU.getTag()) {
        VirtI.setMap(LIU.getMap());
      }
    };

    /// Info for each RegUnit in PhysReg. It is very rare ofr a PHysReg to have
    /// more than 4 RegUnits.
    SmallVector<RegUnitInfo, 4> RegUnits;

    /// Blocks - Interference for each block in the function.
    SmallVector<BlockInterference, 8> Blocks;

    /// update - Recompute Blocks[MBBNum]
    void update(unsigned MBBNum);

  public:
    Entry() = default;

    void clear(MachineFunction *mf, SlotIndexes *indexes, LiveIntervals *lis) {
      assert(!hasRefs() && "Cannot clear cache entry with references");
      PhysReg = 0;
      MF = mf;
      Indexes = indexes;
      LIS = lis;
    }

    unsigned getPhysReg() const { return PhysReg; }

    void addRef(int Delta) { RefCount += Delta; }

    bool hasRefs() const { return RefCount > 0; }

    void revalidate(LiveIntervalUnion *LIUArray, const TargetRegisterInfo *TRI);

    /// valid - Return true if this is a valid entry for physReg.
    bool valid(LiveIntervalUnion *LIUArray, const TargetRegisterInfo *TRI);

    /// reset - Initialize entry to represent physReg's aliases.
    void reset(unsigned physReg,
               LiveIntervalUnion *LIUArray,
               const TargetRegisterInfo *TRI,
               const MachineFunction *MF);

    /// get - Return an up to date BlockInterference.
    BlockInterference *get(unsigned MBBNum) {
      if (Blocks[MBBNum].Tag != Tag)
        update(MBBNum);
      return &Blocks[MBBNum];
    }
  };

  // We don't keep a cache entry for every physical register, that would use too
  // much memory. Instead, a fixed number of cache entries are used in a round-
  // robin manner.
  enum { CacheEntries = 32 };

  const TargetRegisterInfo *TRI = nullptr;
  LiveIntervalUnion *LIUArray = nullptr;
  MachineFunction *MF = nullptr;

  // Point to an entry for each physreg. The entry pointed to may not be up to
  // date, and it may have been reused for a different physreg.
  unsigned char* PhysRegEntries = nullptr;
  size_t PhysRegEntriesCount = 0;

  // Next round-robin entry to be picked.
  unsigned RoundRobin = 0;

  // The actual cache entries.
  Entry Entries[CacheEntries];

  // get - Get a valid entry for PhysReg.
  Entry *get(unsigned PhysReg);

public:
  friend class Cursor;

  InterferenceCache() = default;

  ~InterferenceCache() {
    free(PhysRegEntries);
  }

  void reinitPhysRegEntries();

  /// init - Prepare cache for a new function.
  void init(MachineFunction *mf, LiveIntervalUnion *liuarray,
            SlotIndexes *indexes, LiveIntervals *lis,
            const TargetRegisterInfo *tri);

  /// getMaxCursors - Return the maximum number of concurrent cursors that can
  /// be supported.
  unsigned getMaxCursors() const { return CacheEntries; }

  /// Cursor - The primary query interface for the block interference cache.
  class Cursor {
    Entry *CacheEntry = nullptr;
    const BlockInterference *Current = nullptr;
    static const BlockInterference NoInterference;

    void setEntry(Entry *E) {
      Current = nullptr;
      // Update reference counts. Nothing happens when RefCount reaches 0, so
      // we don't have to check for E == CacheEntry etc.
      if (CacheEntry)
        CacheEntry->addRef(-1);
      CacheEntry = E;
      if (CacheEntry)
        CacheEntry->addRef(+1);
    }

  public:
    /// Cursor - Create a dangling cursor.
    Cursor() = default;

    Cursor(const Cursor &O) {
      setEntry(O.CacheEntry);
    }

    Cursor &operator=(const Cursor &O) {
      setEntry(O.CacheEntry);
      return *this;
    }

    ~Cursor() { setEntry(nullptr); }

    /// setPhysReg - Point this cursor to PhysReg's interference.
    void setPhysReg(InterferenceCache &Cache, unsigned PhysReg) {
      // Release reference before getting a new one. That guarantees we can
      // actually have CacheEntries live cursors.
      setEntry(nullptr);
      if (PhysReg)
        setEntry(Cache.get(PhysReg));
    }

    /// moveTo - Move cursor to basic block MBBNum.
    void moveToBlock(unsigned MBBNum) {
      Current = CacheEntry ? CacheEntry->get(MBBNum) : &NoInterference;
    }

    /// hasInterference - Return true if the current block has any interference.
    bool hasInterference() {
      return Current->First.isValid();
    }

    /// first - Return the starting index of the first interfering range in the
    /// current block.
    SlotIndex first() {
      return Current->First;
    }

    /// last - Return the ending index of the last interfering range in the
    /// current block.
    SlotIndex last() {
      return Current->Last;
    }
  };
};

} // end namespace llvm

#endif // LLVM_LIB_CODEGEN_INTERFERENCECACHE_H
