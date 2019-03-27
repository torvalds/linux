//===- LiveRangeCalc.h - Calculate live ranges ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// The LiveRangeCalc class can be used to compute live ranges from scratch.  It
// caches information about values in the CFG to speed up repeated operations
// on the same live range.  The cache can be shared by non-overlapping live
// ranges.  SplitKit uses that when computing the live range of split products.
//
// A low-level interface is available to clients that know where a variable is
// live, but don't know which value it has as every point.  LiveRangeCalc will
// propagate values down the dominator tree, and even insert PHI-defs where
// needed.  SplitKit uses this faster interface when possible.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_LIVERANGECALC_H
#define LLVM_LIB_CODEGEN_LIVERANGECALC_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/MC/LaneBitmask.h"
#include <utility>

namespace llvm {

template <class NodeT> class DomTreeNodeBase;
class MachineDominatorTree;
class MachineFunction;
class MachineRegisterInfo;

using MachineDomTreeNode = DomTreeNodeBase<MachineBasicBlock>;

class LiveRangeCalc {
  const MachineFunction *MF = nullptr;
  const MachineRegisterInfo *MRI = nullptr;
  SlotIndexes *Indexes = nullptr;
  MachineDominatorTree *DomTree = nullptr;
  VNInfo::Allocator *Alloc = nullptr;

  /// LiveOutPair - A value and the block that defined it.  The domtree node is
  /// redundant, it can be computed as: MDT[Indexes.getMBBFromIndex(VNI->def)].
  using LiveOutPair = std::pair<VNInfo *, MachineDomTreeNode *>;

  /// LiveOutMap - Map basic blocks to the value leaving the block.
  using LiveOutMap = IndexedMap<LiveOutPair, MBB2NumberFunctor>;

  /// Bit vector of active entries in LiveOut, also used as a visited set by
  /// findReachingDefs.  One entry per basic block, indexed by block number.
  /// This is kept as a separate bit vector because it can be cleared quickly
  /// when switching live ranges.
  BitVector Seen;

  /// Map LiveRange to sets of blocks (represented by bit vectors) that
  /// in the live range are defined on entry and undefined on entry.
  /// A block is defined on entry if there is a path from at least one of
  /// the defs in the live range to the entry of the block, and conversely,
  /// a block is undefined on entry, if there is no such path (i.e. no
  /// definition reaches the entry of the block). A single LiveRangeCalc
  /// object is used to track live-out information for multiple registers
  /// in live range splitting (which is ok, since the live ranges of these
  /// registers do not overlap), but the defined/undefined information must
  /// be kept separate for each individual range.
  /// By convention, EntryInfoMap[&LR] = { Defined, Undefined }.
  using EntryInfoMap = DenseMap<LiveRange *, std::pair<BitVector, BitVector>>;
  EntryInfoMap EntryInfos;

  /// Map each basic block where a live range is live out to the live-out value
  /// and its defining block.
  ///
  /// For every basic block, MBB, one of these conditions shall be true:
  ///
  ///  1. !Seen.count(MBB->getNumber())
  ///     Blocks without a Seen bit are ignored.
  ///  2. LiveOut[MBB].second.getNode() == MBB
  ///     The live-out value is defined in MBB.
  ///  3. forall P in preds(MBB): LiveOut[P] == LiveOut[MBB]
  ///     The live-out value passses through MBB. All predecessors must carry
  ///     the same value.
  ///
  /// The domtree node may be null, it can be computed.
  ///
  /// The map can be shared by multiple live ranges as long as no two are
  /// live-out of the same block.
  LiveOutMap Map;

  /// LiveInBlock - Information about a basic block where a live range is known
  /// to be live-in, but the value has not yet been determined.
  struct LiveInBlock {
    // The live range set that is live-in to this block.  The algorithms can
    // handle multiple non-overlapping live ranges simultaneously.
    LiveRange &LR;

    // DomNode - Dominator tree node for the block.
    // Cleared when the final value has been determined and LI has been updated.
    MachineDomTreeNode *DomNode;

    // Position in block where the live-in range ends, or SlotIndex() if the
    // range passes through the block.  When the final value has been
    // determined, the range from the block start to Kill will be added to LI.
    SlotIndex Kill;

    // Live-in value filled in by updateSSA once it is known.
    VNInfo *Value = nullptr;

    LiveInBlock(LiveRange &LR, MachineDomTreeNode *node, SlotIndex kill)
      : LR(LR), DomNode(node), Kill(kill) {}
  };

  /// LiveIn - Work list of blocks where the live-in value has yet to be
  /// determined.  This list is typically computed by findReachingDefs() and
  /// used as a work list by updateSSA().  The low-level interface may also be
  /// used to add entries directly.
  SmallVector<LiveInBlock, 16> LiveIn;

  /// Check if the entry to block @p MBB can be reached by any of the defs
  /// in @p LR. Return true if none of the defs reach the entry to @p MBB.
  bool isDefOnEntry(LiveRange &LR, ArrayRef<SlotIndex> Undefs,
                    MachineBasicBlock &MBB, BitVector &DefOnEntry,
                    BitVector &UndefOnEntry);

  /// Find the set of defs that can reach @p Kill. @p Kill must belong to
  /// @p UseMBB.
  ///
  /// If exactly one def can reach @p UseMBB, and the def dominates @p Kill,
  /// all paths from the def to @p UseMBB are added to @p LR, and the function
  /// returns true.
  ///
  /// If multiple values can reach @p UseMBB, the blocks that need @p LR to be
  /// live in are added to the LiveIn array, and the function returns false.
  ///
  /// The array @p Undef provides the locations where the range @p LR becomes
  /// undefined by <def,read-undef> operands on other subranges. If @p Undef
  /// is non-empty and @p Kill is jointly dominated only by the entries of
  /// @p Undef, the function returns false.
  ///
  /// PhysReg, when set, is used to verify live-in lists on basic blocks.
  bool findReachingDefs(LiveRange &LR, MachineBasicBlock &UseMBB,
                        SlotIndex Use, unsigned PhysReg,
                        ArrayRef<SlotIndex> Undefs);

  /// updateSSA - Compute the values that will be live in to all requested
  /// blocks in LiveIn.  Create PHI-def values as required to preserve SSA form.
  ///
  /// Every live-in block must be jointly dominated by the added live-out
  /// blocks.  No values are read from the live ranges.
  void updateSSA();

  /// Transfer information from the LiveIn vector to the live ranges and update
  /// the given @p LiveOuts.
  void updateFromLiveIns();

  /// Extend the live range of @p LR to reach all uses of Reg.
  ///
  /// If @p LR is a main range, or if @p LI is null, then all uses must be
  /// jointly dominated by the definitions from @p LR. If @p LR is a subrange
  /// of the live interval @p LI, corresponding to lane mask @p LaneMask,
  /// all uses must be jointly dominated by the definitions from @p LR
  /// together with definitions of other lanes where @p LR becomes undefined
  /// (via <def,read-undef> operands).
  /// If @p LR is a main range, the @p LaneMask should be set to ~0, i.e.
  /// LaneBitmask::getAll().
  void extendToUses(LiveRange &LR, unsigned Reg, LaneBitmask LaneMask,
                    LiveInterval *LI = nullptr);

  /// Reset Map and Seen fields.
  void resetLiveOutMap();

public:
  LiveRangeCalc() = default;

  //===--------------------------------------------------------------------===//
  // High-level interface.
  //===--------------------------------------------------------------------===//
  //
  // Calculate live ranges from scratch.
  //

  /// reset - Prepare caches for a new set of non-overlapping live ranges.  The
  /// caches must be reset before attempting calculations with a live range
  /// that may overlap a previously computed live range, and before the first
  /// live range in a function.  If live ranges are not known to be
  /// non-overlapping, call reset before each.
  void reset(const MachineFunction *mf, SlotIndexes *SI,
             MachineDominatorTree *MDT, VNInfo::Allocator *VNIA);

  //===--------------------------------------------------------------------===//
  // Mid-level interface.
  //===--------------------------------------------------------------------===//
  //
  // Modify existing live ranges.
  //

  /// Extend the live range of @p LR to reach @p Use.
  ///
  /// The existing values in @p LR must be live so they jointly dominate @p Use.
  /// If @p Use is not dominated by a single existing value, PHI-defs are
  /// inserted as required to preserve SSA form.
  ///
  /// PhysReg, when set, is used to verify live-in lists on basic blocks.
  void extend(LiveRange &LR, SlotIndex Use, unsigned PhysReg,
              ArrayRef<SlotIndex> Undefs);

  /// createDeadDefs - Create a dead def in LI for every def operand of Reg.
  /// Each instruction defining Reg gets a new VNInfo with a corresponding
  /// minimal live range.
  void createDeadDefs(LiveRange &LR, unsigned Reg);

  /// Extend the live range of @p LR to reach all uses of Reg.
  ///
  /// All uses must be jointly dominated by existing liveness.  PHI-defs are
  /// inserted as needed to preserve SSA form.
  void extendToUses(LiveRange &LR, unsigned PhysReg) {
    extendToUses(LR, PhysReg, LaneBitmask::getAll());
  }

  /// Calculates liveness for the register specified in live interval @p LI.
  /// Creates subregister live ranges as needed if subreg liveness tracking is
  /// enabled.
  void calculate(LiveInterval &LI, bool TrackSubRegs);

  /// For live interval \p LI with correct SubRanges construct matching
  /// information for the main live range. Expects the main live range to not
  /// have any segments or value numbers.
  void constructMainRangeFromSubranges(LiveInterval &LI);

  //===--------------------------------------------------------------------===//
  // Low-level interface.
  //===--------------------------------------------------------------------===//
  //
  // These functions can be used to compute live ranges where the live-in and
  // live-out blocks are already known, but the SSA value in each block is
  // unknown.
  //
  // After calling reset(), add known live-out values and known live-in blocks.
  // Then call calculateValues() to compute the actual value that is
  // live-in to each block, and add liveness to the live ranges.
  //

  /// setLiveOutValue - Indicate that VNI is live out from MBB.  The
  /// calculateValues() function will not add liveness for MBB, the caller
  /// should take care of that.
  ///
  /// VNI may be null only if MBB is a live-through block also passed to
  /// addLiveInBlock().
  void setLiveOutValue(MachineBasicBlock *MBB, VNInfo *VNI) {
    Seen.set(MBB->getNumber());
    Map[MBB] = LiveOutPair(VNI, nullptr);
  }

  /// addLiveInBlock - Add a block with an unknown live-in value.  This
  /// function can only be called once per basic block.  Once the live-in value
  /// has been determined, calculateValues() will add liveness to LI.
  ///
  /// @param LR      The live range that is live-in to the block.
  /// @param DomNode The domtree node for the block.
  /// @param Kill    Index in block where LI is killed.  If the value is
  ///                live-through, set Kill = SLotIndex() and also call
  ///                setLiveOutValue(MBB, 0).
  void addLiveInBlock(LiveRange &LR,
                      MachineDomTreeNode *DomNode,
                      SlotIndex Kill = SlotIndex()) {
    LiveIn.push_back(LiveInBlock(LR, DomNode, Kill));
  }

  /// calculateValues - Calculate the value that will be live-in to each block
  /// added with addLiveInBlock.  Add PHI-def values as needed to preserve SSA
  /// form.  Add liveness to all live-in blocks up to the Kill point, or the
  /// whole block for live-through blocks.
  ///
  /// Every predecessor of a live-in block must have been given a value with
  /// setLiveOutValue, the value may be null for live-trough blocks.
  void calculateValues();

  /// A diagnostic function to check if the end of the block @p MBB is
  /// jointly dominated by the blocks corresponding to the slot indices
  /// in @p Defs. This function is mainly for use in self-verification
  /// checks.
  LLVM_ATTRIBUTE_UNUSED
  static bool isJointlyDominated(const MachineBasicBlock *MBB,
                                 ArrayRef<SlotIndex> Defs,
                                 const SlotIndexes &Indexes);
};

} // end namespace llvm

#endif // LLVM_LIB_CODEGEN_LIVERANGECALC_H
