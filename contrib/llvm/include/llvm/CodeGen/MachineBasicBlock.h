//===- llvm/CodeGen/MachineBasicBlock.h -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Collect the sequence of machine instructions for a basic block.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEBASICBLOCK_H
#define LLVM_CODEGEN_MACHINEBASICBLOCK_H

#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/ADT/simple_ilist.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundleIterator.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Printable.h"
#include <cassert>
#include <cstdint>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

namespace llvm {

class BasicBlock;
class MachineFunction;
class MCSymbol;
class ModuleSlotTracker;
class Pass;
class SlotIndexes;
class StringRef;
class raw_ostream;
class TargetRegisterClass;
class TargetRegisterInfo;

template <> struct ilist_traits<MachineInstr> {
private:
  friend class MachineBasicBlock; // Set by the owning MachineBasicBlock.

  MachineBasicBlock *Parent;

  using instr_iterator =
      simple_ilist<MachineInstr, ilist_sentinel_tracking<true>>::iterator;

public:
  void addNodeToList(MachineInstr *N);
  void removeNodeFromList(MachineInstr *N);
  void transferNodesFromList(ilist_traits &FromList, instr_iterator First,
                             instr_iterator Last);
  void deleteNode(MachineInstr *MI);
};

class MachineBasicBlock
    : public ilist_node_with_parent<MachineBasicBlock, MachineFunction> {
public:
  /// Pair of physical register and lane mask.
  /// This is not simply a std::pair typedef because the members should be named
  /// clearly as they both have an integer type.
  struct RegisterMaskPair {
  public:
    MCPhysReg PhysReg;
    LaneBitmask LaneMask;

    RegisterMaskPair(MCPhysReg PhysReg, LaneBitmask LaneMask)
        : PhysReg(PhysReg), LaneMask(LaneMask) {}
  };

private:
  using Instructions = ilist<MachineInstr, ilist_sentinel_tracking<true>>;

  Instructions Insts;
  const BasicBlock *BB;
  int Number;
  MachineFunction *xParent;

  /// Keep track of the predecessor / successor basic blocks.
  std::vector<MachineBasicBlock *> Predecessors;
  std::vector<MachineBasicBlock *> Successors;

  /// Keep track of the probabilities to the successors. This vector has the
  /// same order as Successors, or it is empty if we don't use it (disable
  /// optimization).
  std::vector<BranchProbability> Probs;
  using probability_iterator = std::vector<BranchProbability>::iterator;
  using const_probability_iterator =
      std::vector<BranchProbability>::const_iterator;

  Optional<uint64_t> IrrLoopHeaderWeight;

  /// Keep track of the physical registers that are livein of the basicblock.
  using LiveInVector = std::vector<RegisterMaskPair>;
  LiveInVector LiveIns;

  /// Alignment of the basic block. Zero if the basic block does not need to be
  /// aligned. The alignment is specified as log2(bytes).
  unsigned Alignment = 0;

  /// Indicate that this basic block is entered via an exception handler.
  bool IsEHPad = false;

  /// Indicate that this basic block is potentially the target of an indirect
  /// branch.
  bool AddressTaken = false;

  /// Indicate that this basic block is the entry block of an EH scope, i.e.,
  /// the block that used to have a catchpad or cleanuppad instruction in the
  /// LLVM IR.
  bool IsEHScopeEntry = false;

  /// Indicate that this basic block is the entry block of an EH funclet.
  bool IsEHFuncletEntry = false;

  /// Indicate that this basic block is the entry block of a cleanup funclet.
  bool IsCleanupFuncletEntry = false;

  /// since getSymbol is a relatively heavy-weight operation, the symbol
  /// is only computed once and is cached.
  mutable MCSymbol *CachedMCSymbol = nullptr;

  // Intrusive list support
  MachineBasicBlock() = default;

  explicit MachineBasicBlock(MachineFunction &MF, const BasicBlock *BB);

  ~MachineBasicBlock();

  // MachineBasicBlocks are allocated and owned by MachineFunction.
  friend class MachineFunction;

public:
  /// Return the LLVM basic block that this instance corresponded to originally.
  /// Note that this may be NULL if this instance does not correspond directly
  /// to an LLVM basic block.
  const BasicBlock *getBasicBlock() const { return BB; }

  /// Return the name of the corresponding LLVM basic block, or an empty string.
  StringRef getName() const;

  /// Return a formatted string to identify this block and its parent function.
  std::string getFullName() const;

  /// Test whether this block is potentially the target of an indirect branch.
  bool hasAddressTaken() const { return AddressTaken; }

  /// Set this block to reflect that it potentially is the target of an indirect
  /// branch.
  void setHasAddressTaken() { AddressTaken = true; }

  /// Return the MachineFunction containing this basic block.
  const MachineFunction *getParent() const { return xParent; }
  MachineFunction *getParent() { return xParent; }

  using instr_iterator = Instructions::iterator;
  using const_instr_iterator = Instructions::const_iterator;
  using reverse_instr_iterator = Instructions::reverse_iterator;
  using const_reverse_instr_iterator = Instructions::const_reverse_iterator;

  using iterator = MachineInstrBundleIterator<MachineInstr>;
  using const_iterator = MachineInstrBundleIterator<const MachineInstr>;
  using reverse_iterator = MachineInstrBundleIterator<MachineInstr, true>;
  using const_reverse_iterator =
      MachineInstrBundleIterator<const MachineInstr, true>;

  unsigned size() const { return (unsigned)Insts.size(); }
  bool empty() const { return Insts.empty(); }

  MachineInstr       &instr_front()       { return Insts.front(); }
  MachineInstr       &instr_back()        { return Insts.back();  }
  const MachineInstr &instr_front() const { return Insts.front(); }
  const MachineInstr &instr_back()  const { return Insts.back();  }

  MachineInstr       &front()             { return Insts.front(); }
  MachineInstr       &back()              { return *--end();      }
  const MachineInstr &front()       const { return Insts.front(); }
  const MachineInstr &back()        const { return *--end();      }

  instr_iterator                instr_begin()       { return Insts.begin();  }
  const_instr_iterator          instr_begin() const { return Insts.begin();  }
  instr_iterator                  instr_end()       { return Insts.end();    }
  const_instr_iterator            instr_end() const { return Insts.end();    }
  reverse_instr_iterator       instr_rbegin()       { return Insts.rbegin(); }
  const_reverse_instr_iterator instr_rbegin() const { return Insts.rbegin(); }
  reverse_instr_iterator       instr_rend  ()       { return Insts.rend();   }
  const_reverse_instr_iterator instr_rend  () const { return Insts.rend();   }

  using instr_range = iterator_range<instr_iterator>;
  using const_instr_range = iterator_range<const_instr_iterator>;
  instr_range instrs() { return instr_range(instr_begin(), instr_end()); }
  const_instr_range instrs() const {
    return const_instr_range(instr_begin(), instr_end());
  }

  iterator                begin()       { return instr_begin();  }
  const_iterator          begin() const { return instr_begin();  }
  iterator                end  ()       { return instr_end();    }
  const_iterator          end  () const { return instr_end();    }
  reverse_iterator rbegin() {
    return reverse_iterator::getAtBundleBegin(instr_rbegin());
  }
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator::getAtBundleBegin(instr_rbegin());
  }
  reverse_iterator rend() { return reverse_iterator(instr_rend()); }
  const_reverse_iterator rend() const {
    return const_reverse_iterator(instr_rend());
  }

  /// Support for MachineInstr::getNextNode().
  static Instructions MachineBasicBlock::*getSublistAccess(MachineInstr *) {
    return &MachineBasicBlock::Insts;
  }

  inline iterator_range<iterator> terminators() {
    return make_range(getFirstTerminator(), end());
  }
  inline iterator_range<const_iterator> terminators() const {
    return make_range(getFirstTerminator(), end());
  }

  /// Returns a range that iterates over the phis in the basic block.
  inline iterator_range<iterator> phis() {
    return make_range(begin(), getFirstNonPHI());
  }
  inline iterator_range<const_iterator> phis() const {
    return const_cast<MachineBasicBlock *>(this)->phis();
  }

  // Machine-CFG iterators
  using pred_iterator = std::vector<MachineBasicBlock *>::iterator;
  using const_pred_iterator = std::vector<MachineBasicBlock *>::const_iterator;
  using succ_iterator = std::vector<MachineBasicBlock *>::iterator;
  using const_succ_iterator = std::vector<MachineBasicBlock *>::const_iterator;
  using pred_reverse_iterator =
      std::vector<MachineBasicBlock *>::reverse_iterator;
  using const_pred_reverse_iterator =
      std::vector<MachineBasicBlock *>::const_reverse_iterator;
  using succ_reverse_iterator =
      std::vector<MachineBasicBlock *>::reverse_iterator;
  using const_succ_reverse_iterator =
      std::vector<MachineBasicBlock *>::const_reverse_iterator;
  pred_iterator        pred_begin()       { return Predecessors.begin(); }
  const_pred_iterator  pred_begin() const { return Predecessors.begin(); }
  pred_iterator        pred_end()         { return Predecessors.end();   }
  const_pred_iterator  pred_end()   const { return Predecessors.end();   }
  pred_reverse_iterator        pred_rbegin()
                                          { return Predecessors.rbegin();}
  const_pred_reverse_iterator  pred_rbegin() const
                                          { return Predecessors.rbegin();}
  pred_reverse_iterator        pred_rend()
                                          { return Predecessors.rend();  }
  const_pred_reverse_iterator  pred_rend()   const
                                          { return Predecessors.rend();  }
  unsigned             pred_size()  const {
    return (unsigned)Predecessors.size();
  }
  bool                 pred_empty() const { return Predecessors.empty(); }
  succ_iterator        succ_begin()       { return Successors.begin();   }
  const_succ_iterator  succ_begin() const { return Successors.begin();   }
  succ_iterator        succ_end()         { return Successors.end();     }
  const_succ_iterator  succ_end()   const { return Successors.end();     }
  succ_reverse_iterator        succ_rbegin()
                                          { return Successors.rbegin();  }
  const_succ_reverse_iterator  succ_rbegin() const
                                          { return Successors.rbegin();  }
  succ_reverse_iterator        succ_rend()
                                          { return Successors.rend();    }
  const_succ_reverse_iterator  succ_rend()   const
                                          { return Successors.rend();    }
  unsigned             succ_size()  const {
    return (unsigned)Successors.size();
  }
  bool                 succ_empty() const { return Successors.empty();   }

  inline iterator_range<pred_iterator> predecessors() {
    return make_range(pred_begin(), pred_end());
  }
  inline iterator_range<const_pred_iterator> predecessors() const {
    return make_range(pred_begin(), pred_end());
  }
  inline iterator_range<succ_iterator> successors() {
    return make_range(succ_begin(), succ_end());
  }
  inline iterator_range<const_succ_iterator> successors() const {
    return make_range(succ_begin(), succ_end());
  }

  // LiveIn management methods.

  /// Adds the specified register as a live in. Note that it is an error to add
  /// the same register to the same set more than once unless the intention is
  /// to call sortUniqueLiveIns after all registers are added.
  void addLiveIn(MCPhysReg PhysReg,
                 LaneBitmask LaneMask = LaneBitmask::getAll()) {
    LiveIns.push_back(RegisterMaskPair(PhysReg, LaneMask));
  }
  void addLiveIn(const RegisterMaskPair &RegMaskPair) {
    LiveIns.push_back(RegMaskPair);
  }

  /// Sorts and uniques the LiveIns vector. It can be significantly faster to do
  /// this than repeatedly calling isLiveIn before calling addLiveIn for every
  /// LiveIn insertion.
  void sortUniqueLiveIns();

  /// Clear live in list.
  void clearLiveIns();

  /// Add PhysReg as live in to this block, and ensure that there is a copy of
  /// PhysReg to a virtual register of class RC. Return the virtual register
  /// that is a copy of the live in PhysReg.
  unsigned addLiveIn(MCPhysReg PhysReg, const TargetRegisterClass *RC);

  /// Remove the specified register from the live in set.
  void removeLiveIn(MCPhysReg Reg,
                    LaneBitmask LaneMask = LaneBitmask::getAll());

  /// Return true if the specified register is in the live in set.
  bool isLiveIn(MCPhysReg Reg,
                LaneBitmask LaneMask = LaneBitmask::getAll()) const;

  // Iteration support for live in sets.  These sets are kept in sorted
  // order by their register number.
  using livein_iterator = LiveInVector::const_iterator;
#ifndef NDEBUG
  /// Unlike livein_begin, this method does not check that the liveness
  /// information is accurate. Still for debug purposes it may be useful
  /// to have iterators that won't assert if the liveness information
  /// is not current.
  livein_iterator livein_begin_dbg() const { return LiveIns.begin(); }
  iterator_range<livein_iterator> liveins_dbg() const {
    return make_range(livein_begin_dbg(), livein_end());
  }
#endif
  livein_iterator livein_begin() const;
  livein_iterator livein_end()   const { return LiveIns.end(); }
  bool            livein_empty() const { return LiveIns.empty(); }
  iterator_range<livein_iterator> liveins() const {
    return make_range(livein_begin(), livein_end());
  }

  /// Remove entry from the livein set and return iterator to the next.
  livein_iterator removeLiveIn(livein_iterator I);

  /// Get the clobber mask for the start of this basic block. Funclets use this
  /// to prevent register allocation across funclet transitions.
  const uint32_t *getBeginClobberMask(const TargetRegisterInfo *TRI) const;

  /// Get the clobber mask for the end of the basic block.
  /// \see getBeginClobberMask()
  const uint32_t *getEndClobberMask(const TargetRegisterInfo *TRI) const;

  /// Return alignment of the basic block. The alignment is specified as
  /// log2(bytes).
  unsigned getAlignment() const { return Alignment; }

  /// Set alignment of the basic block. The alignment is specified as
  /// log2(bytes).
  void setAlignment(unsigned Align) { Alignment = Align; }

  /// Returns true if the block is a landing pad. That is this basic block is
  /// entered via an exception handler.
  bool isEHPad() const { return IsEHPad; }

  /// Indicates the block is a landing pad.  That is this basic block is entered
  /// via an exception handler.
  void setIsEHPad(bool V = true) { IsEHPad = V; }

  bool hasEHPadSuccessor() const;

  /// Returns true if this is the entry block of an EH scope, i.e., the block
  /// that used to have a catchpad or cleanuppad instruction in the LLVM IR.
  bool isEHScopeEntry() const { return IsEHScopeEntry; }

  /// Indicates if this is the entry block of an EH scope, i.e., the block that
  /// that used to have a catchpad or cleanuppad instruction in the LLVM IR.
  void setIsEHScopeEntry(bool V = true) { IsEHScopeEntry = V; }

  /// Returns true if this is the entry block of an EH funclet.
  bool isEHFuncletEntry() const { return IsEHFuncletEntry; }

  /// Indicates if this is the entry block of an EH funclet.
  void setIsEHFuncletEntry(bool V = true) { IsEHFuncletEntry = V; }

  /// Returns true if this is the entry block of a cleanup funclet.
  bool isCleanupFuncletEntry() const { return IsCleanupFuncletEntry; }

  /// Indicates if this is the entry block of a cleanup funclet.
  void setIsCleanupFuncletEntry(bool V = true) { IsCleanupFuncletEntry = V; }

  /// Returns true if it is legal to hoist instructions into this block.
  bool isLegalToHoistInto() const;

  // Code Layout methods.

  /// Move 'this' block before or after the specified block.  This only moves
  /// the block, it does not modify the CFG or adjust potential fall-throughs at
  /// the end of the block.
  void moveBefore(MachineBasicBlock *NewAfter);
  void moveAfter(MachineBasicBlock *NewBefore);

  /// Update the terminator instructions in block to account for changes to the
  /// layout. If the block previously used a fallthrough, it may now need a
  /// branch, and if it previously used branching it may now be able to use a
  /// fallthrough.
  void updateTerminator();

  // Machine-CFG mutators

  /// Add Succ as a successor of this MachineBasicBlock.  The Predecessors list
  /// of Succ is automatically updated. PROB parameter is stored in
  /// Probabilities list. The default probability is set as unknown. Mixing
  /// known and unknown probabilities in successor list is not allowed. When all
  /// successors have unknown probabilities, 1 / N is returned as the
  /// probability for each successor, where N is the number of successors.
  ///
  /// Note that duplicate Machine CFG edges are not allowed.
  void addSuccessor(MachineBasicBlock *Succ,
                    BranchProbability Prob = BranchProbability::getUnknown());

  /// Add Succ as a successor of this MachineBasicBlock.  The Predecessors list
  /// of Succ is automatically updated. The probability is not provided because
  /// BPI is not available (e.g. -O0 is used), in which case edge probabilities
  /// won't be used. Using this interface can save some space.
  void addSuccessorWithoutProb(MachineBasicBlock *Succ);

  /// Set successor probability of a given iterator.
  void setSuccProbability(succ_iterator I, BranchProbability Prob);

  /// Normalize probabilities of all successors so that the sum of them becomes
  /// one. This is usually done when the current update on this MBB is done, and
  /// the sum of its successors' probabilities is not guaranteed to be one. The
  /// user is responsible for the correct use of this function.
  /// MBB::removeSuccessor() has an option to do this automatically.
  void normalizeSuccProbs() {
    BranchProbability::normalizeProbabilities(Probs.begin(), Probs.end());
  }

  /// Validate successors' probabilities and check if the sum of them is
  /// approximate one. This only works in DEBUG mode.
  void validateSuccProbs() const;

  /// Remove successor from the successors list of this MachineBasicBlock. The
  /// Predecessors list of Succ is automatically updated.
  /// If NormalizeSuccProbs is true, then normalize successors' probabilities
  /// after the successor is removed.
  void removeSuccessor(MachineBasicBlock *Succ,
                       bool NormalizeSuccProbs = false);

  /// Remove specified successor from the successors list of this
  /// MachineBasicBlock. The Predecessors list of Succ is automatically updated.
  /// If NormalizeSuccProbs is true, then normalize successors' probabilities
  /// after the successor is removed.
  /// Return the iterator to the element after the one removed.
  succ_iterator removeSuccessor(succ_iterator I,
                                bool NormalizeSuccProbs = false);

  /// Replace successor OLD with NEW and update probability info.
  void replaceSuccessor(MachineBasicBlock *Old, MachineBasicBlock *New);

  /// Copy a successor (and any probability info) from original block to this
  /// block's. Uses an iterator into the original blocks successors.
  ///
  /// This is useful when doing a partial clone of successors. Afterward, the
  /// probabilities may need to be normalized.
  void copySuccessor(MachineBasicBlock *Orig, succ_iterator I);

  /// Split the old successor into old plus new and updates the probability
  /// info.
  void splitSuccessor(MachineBasicBlock *Old, MachineBasicBlock *New,
                      bool NormalizeSuccProbs = false);

  /// Transfers all the successors from MBB to this machine basic block (i.e.,
  /// copies all the successors FromMBB and remove all the successors from
  /// FromMBB).
  void transferSuccessors(MachineBasicBlock *FromMBB);

  /// Transfers all the successors, as in transferSuccessors, and update PHI
  /// operands in the successor blocks which refer to FromMBB to refer to this.
  void transferSuccessorsAndUpdatePHIs(MachineBasicBlock *FromMBB);

  /// Return true if any of the successors have probabilities attached to them.
  bool hasSuccessorProbabilities() const { return !Probs.empty(); }

  /// Return true if the specified MBB is a predecessor of this block.
  bool isPredecessor(const MachineBasicBlock *MBB) const;

  /// Return true if the specified MBB is a successor of this block.
  bool isSuccessor(const MachineBasicBlock *MBB) const;

  /// Return true if the specified MBB will be emitted immediately after this
  /// block, such that if this block exits by falling through, control will
  /// transfer to the specified MBB. Note that MBB need not be a successor at
  /// all, for example if this block ends with an unconditional branch to some
  /// other block.
  bool isLayoutSuccessor(const MachineBasicBlock *MBB) const;

  /// Return the fallthrough block if the block can implicitly
  /// transfer control to the block after it by falling off the end of
  /// it.  This should return null if it can reach the block after
  /// it, but it uses an explicit branch to do so (e.g., a table
  /// jump).  Non-null return  is a conservative answer.
  MachineBasicBlock *getFallThrough();

  /// Return true if the block can implicitly transfer control to the
  /// block after it by falling off the end of it.  This should return
  /// false if it can reach the block after it, but it uses an
  /// explicit branch to do so (e.g., a table jump).  True is a
  /// conservative answer.
  bool canFallThrough();

  /// Returns a pointer to the first instruction in this block that is not a
  /// PHINode instruction. When adding instructions to the beginning of the
  /// basic block, they should be added before the returned value, not before
  /// the first instruction, which might be PHI.
  /// Returns end() is there's no non-PHI instruction.
  iterator getFirstNonPHI();

  /// Return the first instruction in MBB after I that is not a PHI or a label.
  /// This is the correct point to insert lowered copies at the beginning of a
  /// basic block that must be before any debugging information.
  iterator SkipPHIsAndLabels(iterator I);

  /// Return the first instruction in MBB after I that is not a PHI, label or
  /// debug.  This is the correct point to insert copies at the beginning of a
  /// basic block.
  iterator SkipPHIsLabelsAndDebug(iterator I);

  /// Returns an iterator to the first terminator instruction of this basic
  /// block. If a terminator does not exist, it returns end().
  iterator getFirstTerminator();
  const_iterator getFirstTerminator() const {
    return const_cast<MachineBasicBlock *>(this)->getFirstTerminator();
  }

  /// Same getFirstTerminator but it ignores bundles and return an
  /// instr_iterator instead.
  instr_iterator getFirstInstrTerminator();

  /// Returns an iterator to the first non-debug instruction in the basic block,
  /// or end().
  iterator getFirstNonDebugInstr();
  const_iterator getFirstNonDebugInstr() const {
    return const_cast<MachineBasicBlock *>(this)->getFirstNonDebugInstr();
  }

  /// Returns an iterator to the last non-debug instruction in the basic block,
  /// or end().
  iterator getLastNonDebugInstr();
  const_iterator getLastNonDebugInstr() const {
    return const_cast<MachineBasicBlock *>(this)->getLastNonDebugInstr();
  }

  /// Convenience function that returns true if the block ends in a return
  /// instruction.
  bool isReturnBlock() const {
    return !empty() && back().isReturn();
  }

  /// Convenience function that returns true if the bock ends in a EH scope
  /// return instruction.
  bool isEHScopeReturnBlock() const {
    return !empty() && back().isEHScopeReturn();
  }

  /// Split the critical edge from this block to the given successor block, and
  /// return the newly created block, or null if splitting is not possible.
  ///
  /// This function updates LiveVariables, MachineDominatorTree, and
  /// MachineLoopInfo, as applicable.
  MachineBasicBlock *SplitCriticalEdge(MachineBasicBlock *Succ, Pass &P);

  /// Check if the edge between this block and the given successor \p
  /// Succ, can be split. If this returns true a subsequent call to
  /// SplitCriticalEdge is guaranteed to return a valid basic block if
  /// no changes occurred in the meantime.
  bool canSplitCriticalEdge(const MachineBasicBlock *Succ) const;

  void pop_front() { Insts.pop_front(); }
  void pop_back() { Insts.pop_back(); }
  void push_back(MachineInstr *MI) { Insts.push_back(MI); }

  /// Insert MI into the instruction list before I, possibly inside a bundle.
  ///
  /// If the insertion point is inside a bundle, MI will be added to the bundle,
  /// otherwise MI will not be added to any bundle. That means this function
  /// alone can't be used to prepend or append instructions to bundles. See
  /// MIBundleBuilder::insert() for a more reliable way of doing that.
  instr_iterator insert(instr_iterator I, MachineInstr *M);

  /// Insert a range of instructions into the instruction list before I.
  template<typename IT>
  void insert(iterator I, IT S, IT E) {
    assert((I == end() || I->getParent() == this) &&
           "iterator points outside of basic block");
    Insts.insert(I.getInstrIterator(), S, E);
  }

  /// Insert MI into the instruction list before I.
  iterator insert(iterator I, MachineInstr *MI) {
    assert((I == end() || I->getParent() == this) &&
           "iterator points outside of basic block");
    assert(!MI->isBundledWithPred() && !MI->isBundledWithSucc() &&
           "Cannot insert instruction with bundle flags");
    return Insts.insert(I.getInstrIterator(), MI);
  }

  /// Insert MI into the instruction list after I.
  iterator insertAfter(iterator I, MachineInstr *MI) {
    assert((I == end() || I->getParent() == this) &&
           "iterator points outside of basic block");
    assert(!MI->isBundledWithPred() && !MI->isBundledWithSucc() &&
           "Cannot insert instruction with bundle flags");
    return Insts.insertAfter(I.getInstrIterator(), MI);
  }

  /// Remove an instruction from the instruction list and delete it.
  ///
  /// If the instruction is part of a bundle, the other instructions in the
  /// bundle will still be bundled after removing the single instruction.
  instr_iterator erase(instr_iterator I);

  /// Remove an instruction from the instruction list and delete it.
  ///
  /// If the instruction is part of a bundle, the other instructions in the
  /// bundle will still be bundled after removing the single instruction.
  instr_iterator erase_instr(MachineInstr *I) {
    return erase(instr_iterator(I));
  }

  /// Remove a range of instructions from the instruction list and delete them.
  iterator erase(iterator I, iterator E) {
    return Insts.erase(I.getInstrIterator(), E.getInstrIterator());
  }

  /// Remove an instruction or bundle from the instruction list and delete it.
  ///
  /// If I points to a bundle of instructions, they are all erased.
  iterator erase(iterator I) {
    return erase(I, std::next(I));
  }

  /// Remove an instruction from the instruction list and delete it.
  ///
  /// If I is the head of a bundle of instructions, the whole bundle will be
  /// erased.
  iterator erase(MachineInstr *I) {
    return erase(iterator(I));
  }

  /// Remove the unbundled instruction from the instruction list without
  /// deleting it.
  ///
  /// This function can not be used to remove bundled instructions, use
  /// remove_instr to remove individual instructions from a bundle.
  MachineInstr *remove(MachineInstr *I) {
    assert(!I->isBundled() && "Cannot remove bundled instructions");
    return Insts.remove(instr_iterator(I));
  }

  /// Remove the possibly bundled instruction from the instruction list
  /// without deleting it.
  ///
  /// If the instruction is part of a bundle, the other instructions in the
  /// bundle will still be bundled after removing the single instruction.
  MachineInstr *remove_instr(MachineInstr *I);

  void clear() {
    Insts.clear();
  }

  /// Take an instruction from MBB 'Other' at the position From, and insert it
  /// into this MBB right before 'Where'.
  ///
  /// If From points to a bundle of instructions, the whole bundle is moved.
  void splice(iterator Where, MachineBasicBlock *Other, iterator From) {
    // The range splice() doesn't allow noop moves, but this one does.
    if (Where != From)
      splice(Where, Other, From, std::next(From));
  }

  /// Take a block of instructions from MBB 'Other' in the range [From, To),
  /// and insert them into this MBB right before 'Where'.
  ///
  /// The instruction at 'Where' must not be included in the range of
  /// instructions to move.
  void splice(iterator Where, MachineBasicBlock *Other,
              iterator From, iterator To) {
    Insts.splice(Where.getInstrIterator(), Other->Insts,
                 From.getInstrIterator(), To.getInstrIterator());
  }

  /// This method unlinks 'this' from the containing function, and returns it,
  /// but does not delete it.
  MachineBasicBlock *removeFromParent();

  /// This method unlinks 'this' from the containing function and deletes it.
  void eraseFromParent();

  /// Given a machine basic block that branched to 'Old', change the code and
  /// CFG so that it branches to 'New' instead.
  void ReplaceUsesOfBlockWith(MachineBasicBlock *Old, MachineBasicBlock *New);

  /// Various pieces of code can cause excess edges in the CFG to be inserted.
  /// If we have proven that MBB can only branch to DestA and DestB, remove any
  /// other MBB successors from the CFG. DestA and DestB can be null. Besides
  /// DestA and DestB, retain other edges leading to LandingPads (currently
  /// there can be only one; we don't check or require that here). Note it is
  /// possible that DestA and/or DestB are LandingPads.
  bool CorrectExtraCFGEdges(MachineBasicBlock *DestA,
                            MachineBasicBlock *DestB,
                            bool IsCond);

  /// Find the next valid DebugLoc starting at MBBI, skipping any DBG_VALUE
  /// and DBG_LABEL instructions.  Return UnknownLoc if there is none.
  DebugLoc findDebugLoc(instr_iterator MBBI);
  DebugLoc findDebugLoc(iterator MBBI) {
    return findDebugLoc(MBBI.getInstrIterator());
  }

  /// Find the previous valid DebugLoc preceding MBBI, skipping and DBG_VALUE
  /// instructions.  Return UnknownLoc if there is none.
  DebugLoc findPrevDebugLoc(instr_iterator MBBI);
  DebugLoc findPrevDebugLoc(iterator MBBI) {
    return findPrevDebugLoc(MBBI.getInstrIterator());
  }

  /// Find and return the merged DebugLoc of the branch instructions of the
  /// block. Return UnknownLoc if there is none.
  DebugLoc findBranchDebugLoc();

  /// Possible outcome of a register liveness query to computeRegisterLiveness()
  enum LivenessQueryResult {
    LQR_Live,   ///< Register is known to be (at least partially) live.
    LQR_Dead,   ///< Register is known to be fully dead.
    LQR_Unknown ///< Register liveness not decidable from local neighborhood.
  };

  /// Return whether (physical) register \p Reg has been defined and not
  /// killed as of just before \p Before.
  ///
  /// Search is localised to a neighborhood of \p Neighborhood instructions
  /// before (searching for defs or kills) and \p Neighborhood instructions
  /// after (searching just for defs) \p Before.
  ///
  /// \p Reg must be a physical register.
  LivenessQueryResult computeRegisterLiveness(const TargetRegisterInfo *TRI,
                                              unsigned Reg,
                                              const_iterator Before,
                                              unsigned Neighborhood = 10) const;

  // Debugging methods.
  void dump() const;
  void print(raw_ostream &OS, const SlotIndexes * = nullptr,
             bool IsStandalone = true) const;
  void print(raw_ostream &OS, ModuleSlotTracker &MST,
             const SlotIndexes * = nullptr, bool IsStandalone = true) const;

  // Printing method used by LoopInfo.
  void printAsOperand(raw_ostream &OS, bool PrintType = true) const;

  /// MachineBasicBlocks are uniquely numbered at the function level, unless
  /// they're not in a MachineFunction yet, in which case this will return -1.
  int getNumber() const { return Number; }
  void setNumber(int N) { Number = N; }

  /// Return the MCSymbol for this basic block.
  MCSymbol *getSymbol() const;

  Optional<uint64_t> getIrrLoopHeaderWeight() const {
    return IrrLoopHeaderWeight;
  }

  void setIrrLoopHeaderWeight(uint64_t Weight) {
    IrrLoopHeaderWeight = Weight;
  }

private:
  /// Return probability iterator corresponding to the I successor iterator.
  probability_iterator getProbabilityIterator(succ_iterator I);
  const_probability_iterator
  getProbabilityIterator(const_succ_iterator I) const;

  friend class MachineBranchProbabilityInfo;
  friend class MIPrinter;

  /// Return probability of the edge from this block to MBB. This method should
  /// NOT be called directly, but by using getEdgeProbability method from
  /// MachineBranchProbabilityInfo class.
  BranchProbability getSuccProbability(const_succ_iterator Succ) const;

  // Methods used to maintain doubly linked list of blocks...
  friend struct ilist_callback_traits<MachineBasicBlock>;

  // Machine-CFG mutators

  /// Add Pred as a predecessor of this MachineBasicBlock. Don't do this
  /// unless you know what you're doing, because it doesn't update Pred's
  /// successors list. Use Pred->addSuccessor instead.
  void addPredecessor(MachineBasicBlock *Pred);

  /// Remove Pred as a predecessor of this MachineBasicBlock. Don't do this
  /// unless you know what you're doing, because it doesn't update Pred's
  /// successors list. Use Pred->removeSuccessor instead.
  void removePredecessor(MachineBasicBlock *Pred);
};

raw_ostream& operator<<(raw_ostream &OS, const MachineBasicBlock &MBB);

/// Prints a machine basic block reference.
///
/// The format is:
///   %bb.5           - a machine basic block with MBB.getNumber() == 5.
///
/// Usage: OS << printMBBReference(MBB) << '\n';
Printable printMBBReference(const MachineBasicBlock &MBB);

// This is useful when building IndexedMaps keyed on basic block pointers.
struct MBB2NumberFunctor {
  using argument_type = const MachineBasicBlock *;
  unsigned operator()(const MachineBasicBlock *MBB) const {
    return MBB->getNumber();
  }
};

//===--------------------------------------------------------------------===//
// GraphTraits specializations for machine basic block graphs (machine-CFGs)
//===--------------------------------------------------------------------===//

// Provide specializations of GraphTraits to be able to treat a
// MachineFunction as a graph of MachineBasicBlocks.
//

template <> struct GraphTraits<MachineBasicBlock *> {
  using NodeRef = MachineBasicBlock *;
  using ChildIteratorType = MachineBasicBlock::succ_iterator;

  static NodeRef getEntryNode(MachineBasicBlock *BB) { return BB; }
  static ChildIteratorType child_begin(NodeRef N) { return N->succ_begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->succ_end(); }
};

template <> struct GraphTraits<const MachineBasicBlock *> {
  using NodeRef = const MachineBasicBlock *;
  using ChildIteratorType = MachineBasicBlock::const_succ_iterator;

  static NodeRef getEntryNode(const MachineBasicBlock *BB) { return BB; }
  static ChildIteratorType child_begin(NodeRef N) { return N->succ_begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->succ_end(); }
};

// Provide specializations of GraphTraits to be able to treat a
// MachineFunction as a graph of MachineBasicBlocks and to walk it
// in inverse order.  Inverse order for a function is considered
// to be when traversing the predecessor edges of a MBB
// instead of the successor edges.
//
template <> struct GraphTraits<Inverse<MachineBasicBlock*>> {
  using NodeRef = MachineBasicBlock *;
  using ChildIteratorType = MachineBasicBlock::pred_iterator;

  static NodeRef getEntryNode(Inverse<MachineBasicBlock *> G) {
    return G.Graph;
  }

  static ChildIteratorType child_begin(NodeRef N) { return N->pred_begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->pred_end(); }
};

template <> struct GraphTraits<Inverse<const MachineBasicBlock*>> {
  using NodeRef = const MachineBasicBlock *;
  using ChildIteratorType = MachineBasicBlock::const_pred_iterator;

  static NodeRef getEntryNode(Inverse<const MachineBasicBlock *> G) {
    return G.Graph;
  }

  static ChildIteratorType child_begin(NodeRef N) { return N->pred_begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->pred_end(); }
};

/// MachineInstrSpan provides an interface to get an iteration range
/// containing the instruction it was initialized with, along with all
/// those instructions inserted prior to or following that instruction
/// at some point after the MachineInstrSpan is constructed.
class MachineInstrSpan {
  MachineBasicBlock &MBB;
  MachineBasicBlock::iterator I, B, E;

public:
  MachineInstrSpan(MachineBasicBlock::iterator I)
    : MBB(*I->getParent()),
      I(I),
      B(I == MBB.begin() ? MBB.end() : std::prev(I)),
      E(std::next(I)) {}

  MachineBasicBlock::iterator begin() {
    return B == MBB.end() ? MBB.begin() : std::next(B);
  }
  MachineBasicBlock::iterator end() { return E; }
  bool empty() { return begin() == end(); }

  MachineBasicBlock::iterator getInitial() { return I; }
};

/// Increment \p It until it points to a non-debug instruction or to \p End
/// and return the resulting iterator. This function should only be used
/// MachineBasicBlock::{iterator, const_iterator, instr_iterator,
/// const_instr_iterator} and the respective reverse iterators.
template<typename IterT>
inline IterT skipDebugInstructionsForward(IterT It, IterT End) {
  while (It != End && It->isDebugInstr())
    It++;
  return It;
}

/// Decrement \p It until it points to a non-debug instruction or to \p Begin
/// and return the resulting iterator. This function should only be used
/// MachineBasicBlock::{iterator, const_iterator, instr_iterator,
/// const_instr_iterator} and the respective reverse iterators.
template<class IterT>
inline IterT skipDebugInstructionsBackward(IterT It, IterT Begin) {
  while (It != Begin && It->isDebugInstr())
    It--;
  return It;
}

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEBASICBLOCK_H
