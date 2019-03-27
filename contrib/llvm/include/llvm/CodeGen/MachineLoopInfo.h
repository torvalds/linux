//===- llvm/CodeGen/MachineLoopInfo.h - Natural Loop Calculator -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the MachineLoopInfo class that is used to identify natural
// loops and determine the loop depth of various nodes of the CFG.  Note that
// natural loops may actually be several loops that share the same header node.
//
// This analysis calculates the nesting structure of loops in a function.  For
// each natural loop identified, this analysis identifies natural loops
// contained entirely within the loop and the basic blocks the make up the loop.
//
// It can calculate on the fly various bits of information, for example:
//
//  * whether there is a preheader for the loop
//  * the number of back edges to the header
//  * whether or not a particular block branches out of the loop
//  * the successor blocks of the loop
//  * the loop depth
//  * the trip count
//  * etc...
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINELOOPINFO_H
#define LLVM_CODEGEN_MACHINELOOPINFO_H

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Pass.h"

namespace llvm {

// Implementation in LoopInfoImpl.h
class MachineLoop;
extern template class LoopBase<MachineBasicBlock, MachineLoop>;

class MachineLoop : public LoopBase<MachineBasicBlock, MachineLoop> {
public:
  /// Return the "top" block in the loop, which is the first block in the linear
  /// layout, ignoring any parts of the loop not contiguous with the part that
  /// contains the header.
  MachineBasicBlock *getTopBlock();

  /// Return the "bottom" block in the loop, which is the last block in the
  /// linear layout, ignoring any parts of the loop not contiguous with the part
  /// that contains the header.
  MachineBasicBlock *getBottomBlock();

  /// Find the block that contains the loop control variable and the
  /// loop test. This will return the latch block if it's one of the exiting
  /// blocks. Otherwise, return the exiting block. Return 'null' when
  /// multiple exiting blocks are present.
  MachineBasicBlock *findLoopControlBlock();

  /// Return the debug location of the start of this loop.
  /// This looks for a BB terminating instruction with a known debug
  /// location by looking at the preheader and header blocks. If it
  /// cannot find a terminating instruction with location information,
  /// it returns an unknown location.
  DebugLoc getStartLoc() const;

  void dump() const;

private:
  friend class LoopInfoBase<MachineBasicBlock, MachineLoop>;

  explicit MachineLoop(MachineBasicBlock *MBB)
    : LoopBase<MachineBasicBlock, MachineLoop>(MBB) {}

  MachineLoop() = default;
};

// Implementation in LoopInfoImpl.h
extern template class LoopInfoBase<MachineBasicBlock, MachineLoop>;

class MachineLoopInfo : public MachineFunctionPass {
  friend class LoopBase<MachineBasicBlock, MachineLoop>;

  LoopInfoBase<MachineBasicBlock, MachineLoop> LI;

public:
  static char ID; // Pass identification, replacement for typeid

  MachineLoopInfo() : MachineFunctionPass(ID) {
    initializeMachineLoopInfoPass(*PassRegistry::getPassRegistry());
  }
  MachineLoopInfo(const MachineLoopInfo &) = delete;
  MachineLoopInfo &operator=(const MachineLoopInfo &) = delete;

  LoopInfoBase<MachineBasicBlock, MachineLoop>& getBase() { return LI; }

  /// Find the block that either is the loop preheader, or could
  /// speculatively be used as the preheader. This is e.g. useful to place
  /// loop setup code. Code that cannot be speculated should not be placed
  /// here. SpeculativePreheader is controlling whether it also tries to
  /// find the speculative preheader if the regular preheader is not present.
  MachineBasicBlock *findLoopPreheader(MachineLoop *L,
                                       bool SpeculativePreheader = false) const;

  /// The iterator interface to the top-level loops in the current function.
  using iterator = LoopInfoBase<MachineBasicBlock, MachineLoop>::iterator;
  inline iterator begin() const { return LI.begin(); }
  inline iterator end() const { return LI.end(); }
  bool empty() const { return LI.empty(); }

  /// Return the innermost loop that BB lives in. If a basic block is in no loop
  /// (for example the entry node), null is returned.
  inline MachineLoop *getLoopFor(const MachineBasicBlock *BB) const {
    return LI.getLoopFor(BB);
  }

  /// Same as getLoopFor.
  inline const MachineLoop *operator[](const MachineBasicBlock *BB) const {
    return LI.getLoopFor(BB);
  }

  /// Return the loop nesting level of the specified block.
  inline unsigned getLoopDepth(const MachineBasicBlock *BB) const {
    return LI.getLoopDepth(BB);
  }

  /// True if the block is a loop header node.
  inline bool isLoopHeader(const MachineBasicBlock *BB) const {
    return LI.isLoopHeader(BB);
  }

  /// Calculate the natural loop information.
  bool runOnMachineFunction(MachineFunction &F) override;

  void releaseMemory() override { LI.releaseMemory(); }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// This removes the specified top-level loop from this loop info object. The
  /// loop is not deleted, as it will presumably be inserted into another loop.
  inline MachineLoop *removeLoop(iterator I) { return LI.removeLoop(I); }

  /// Change the top-level loop that contains BB to the specified loop. This
  /// should be used by transformations that restructure the loop hierarchy
  /// tree.
  inline void changeLoopFor(MachineBasicBlock *BB, MachineLoop *L) {
    LI.changeLoopFor(BB, L);
  }

  /// Replace the specified loop in the top-level loops list with the indicated
  /// loop.
  inline void changeTopLevelLoop(MachineLoop *OldLoop, MachineLoop *NewLoop) {
    LI.changeTopLevelLoop(OldLoop, NewLoop);
  }

  /// This adds the specified loop to the collection of top-level loops.
  inline void addTopLevelLoop(MachineLoop *New) {
    LI.addTopLevelLoop(New);
  }

  /// This method completely removes BB from all data structures, including all
  /// of the Loop objects it is nested in and our mapping from
  /// MachineBasicBlocks to loops.
  void removeBlock(MachineBasicBlock *BB) {
    LI.removeBlock(BB);
  }
};

// Allow clients to walk the list of nested loops...
template <> struct GraphTraits<const MachineLoop*> {
  using NodeRef = const MachineLoop *;
  using ChildIteratorType = MachineLoopInfo::iterator;

  static NodeRef getEntryNode(const MachineLoop *L) { return L; }
  static ChildIteratorType child_begin(NodeRef N) { return N->begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->end(); }
};

template <> struct GraphTraits<MachineLoop*> {
  using NodeRef = MachineLoop *;
  using ChildIteratorType = MachineLoopInfo::iterator;

  static NodeRef getEntryNode(MachineLoop *L) { return L; }
  static ChildIteratorType child_begin(NodeRef N) { return N->begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->end(); }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINELOOPINFO_H
