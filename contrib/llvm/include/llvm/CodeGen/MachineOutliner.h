//===---- MachineOutliner.h - Outliner data structures ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Contains all data structures shared between the outliner implemented in
/// MachineOutliner.cpp and target implementations of the outliner.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MACHINEOUTLINER_H
#define LLVM_MACHINEOUTLINER_H

#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/LivePhysRegs.h"

namespace llvm {
namespace outliner {

/// Represents how an instruction should be mapped by the outliner.
/// \p Legal instructions are those which are safe to outline.
/// \p LegalTerminator instructions are safe to outline, but only as the
/// last instruction in a sequence.
/// \p Illegal instructions are those which cannot be outlined.
/// \p Invisible instructions are instructions which can be outlined, but
/// shouldn't actually impact the outlining result.
enum InstrType { Legal, LegalTerminator, Illegal, Invisible };

/// An individual sequence of instructions to be replaced with a call to
/// an outlined function.
struct Candidate {
private:
  /// The start index of this \p Candidate in the instruction list.
  unsigned StartIdx;

  /// The number of instructions in this \p Candidate.
  unsigned Len;

  // The first instruction in this \p Candidate.
  MachineBasicBlock::iterator FirstInst;

  // The last instruction in this \p Candidate.
  MachineBasicBlock::iterator LastInst;

  // The basic block that contains this Candidate.
  MachineBasicBlock *MBB;

  /// Cost of calling an outlined function from this point as defined by the
  /// target.
  unsigned CallOverhead;

public:
  /// The index of this \p Candidate's \p OutlinedFunction in the list of
  /// \p OutlinedFunctions.
  unsigned FunctionIdx;

  /// Identifier denoting the instructions to emit to call an outlined function
  /// from this point. Defined by the target.
  unsigned CallConstructionID;

  /// Contains physical register liveness information for the MBB containing
  /// this \p Candidate.
  ///
  /// This is optionally used by the target to calculate more fine-grained
  /// cost model information.
  LiveRegUnits LRU;

  /// Contains the accumulated register liveness information for the
  /// instructions in this \p Candidate.
  ///
  /// This is optionally used by the target to determine which registers have
  /// been used across the sequence.
  LiveRegUnits UsedInSequence;

  /// Target-specific flags for this Candidate's MBB.
  unsigned Flags = 0x0;

  /// True if initLRU has been called on this Candidate.
  bool LRUWasSet = false;

  /// Return the number of instructions in this Candidate.
  unsigned getLength() const { return Len; }

  /// Return the start index of this candidate.
  unsigned getStartIdx() const { return StartIdx; }

  /// Return the end index of this candidate.
  unsigned getEndIdx() const { return StartIdx + Len - 1; }

  /// Set the CallConstructionID and CallOverhead of this candidate to CID and
  /// CO respectively.
  void setCallInfo(unsigned CID, unsigned CO) {
    CallConstructionID = CID;
    CallOverhead = CO;
  }

  /// Returns the call overhead of this candidate if it is in the list.
  unsigned getCallOverhead() const { return CallOverhead; }

  MachineBasicBlock::iterator &front() { return FirstInst; }
  MachineBasicBlock::iterator &back() { return LastInst; }
  MachineFunction *getMF() const { return MBB->getParent(); }
  MachineBasicBlock *getMBB() const { return MBB; }

  /// The number of instructions that would be saved by outlining every
  /// candidate of this type.
  ///
  /// This is a fixed value which is not updated during the candidate pruning
  /// process. It is only used for deciding which candidate to keep if two
  /// candidates overlap. The true benefit is stored in the OutlinedFunction
  /// for some given candidate.
  unsigned Benefit = 0;

  Candidate(unsigned StartIdx, unsigned Len,
            MachineBasicBlock::iterator &FirstInst,
            MachineBasicBlock::iterator &LastInst, MachineBasicBlock *MBB,
            unsigned FunctionIdx, unsigned Flags)
      : StartIdx(StartIdx), Len(Len), FirstInst(FirstInst), LastInst(LastInst),
        MBB(MBB), FunctionIdx(FunctionIdx), Flags(Flags) {}
  Candidate() {}

  /// Used to ensure that \p Candidates are outlined in an order that
  /// preserves the start and end indices of other \p Candidates.
  bool operator<(const Candidate &RHS) const {
    return getStartIdx() > RHS.getStartIdx();
  }

  /// Compute the registers that are live across this Candidate.
  /// Used by targets that need this information for cost model calculation.
  /// If a target does not need this information, then this should not be
  /// called.
  void initLRU(const TargetRegisterInfo &TRI) {
    assert(MBB->getParent()->getRegInfo().tracksLiveness() &&
           "Candidate's Machine Function must track liveness");
    // Only initialize once.
    if (LRUWasSet)
      return;
    LRUWasSet = true;
    LRU.init(TRI);
    LRU.addLiveOuts(*MBB);

    // Compute liveness from the end of the block up to the beginning of the
    // outlining candidate.
    std::for_each(MBB->rbegin(), (MachineBasicBlock::reverse_iterator)front(),
                  [this](MachineInstr &MI) { LRU.stepBackward(MI); });

    // Walk over the sequence itself and figure out which registers were used
    // in the sequence.
    UsedInSequence.init(TRI);
    std::for_each(front(), std::next(back()),
                  [this](MachineInstr &MI) { UsedInSequence.accumulate(MI); });
  }
};

/// The information necessary to create an outlined function for some
/// class of candidate.
struct OutlinedFunction {

public:
  std::vector<Candidate> Candidates;

  /// The actual outlined function created.
  /// This is initialized after we go through and create the actual function.
  MachineFunction *MF = nullptr;

  /// Represents the size of a sequence in bytes. (Some instructions vary
  /// widely in size, so just counting the instructions isn't very useful.)
  unsigned SequenceSize;

  /// Target-defined overhead of constructing a frame for this function.
  unsigned FrameOverhead;

  /// Target-defined identifier for constructing a frame for this function.
  unsigned FrameConstructionID;

  /// Return the number of candidates for this \p OutlinedFunction.
  unsigned getOccurrenceCount() const { return Candidates.size(); }

  /// Return the number of bytes it would take to outline this
  /// function.
  unsigned getOutliningCost() const {
    unsigned CallOverhead = 0;
    for (const Candidate &C : Candidates)
      CallOverhead += C.getCallOverhead();
    return CallOverhead + SequenceSize + FrameOverhead;
  }

  /// Return the size in bytes of the unoutlined sequences.
  unsigned getNotOutlinedCost() const {
    return getOccurrenceCount() * SequenceSize;
  }

  /// Return the number of instructions that would be saved by outlining
  /// this function.
  unsigned getBenefit() const {
    unsigned NotOutlinedCost = getNotOutlinedCost();
    unsigned OutlinedCost = getOutliningCost();
    return (NotOutlinedCost < OutlinedCost) ? 0
                                            : NotOutlinedCost - OutlinedCost;
  }

  /// Return the number of instructions in this sequence.
  unsigned getNumInstrs() const { return Candidates[0].getLength(); }

  OutlinedFunction(std::vector<Candidate> &Candidates, unsigned SequenceSize,
                   unsigned FrameOverhead, unsigned FrameConstructionID)
      : Candidates(Candidates), SequenceSize(SequenceSize),
        FrameOverhead(FrameOverhead), FrameConstructionID(FrameConstructionID) {
    const unsigned B = getBenefit();
    for (Candidate &C : Candidates)
      C.Benefit = B;
  }

  OutlinedFunction() {}
};
} // namespace outliner
} // namespace llvm

#endif
