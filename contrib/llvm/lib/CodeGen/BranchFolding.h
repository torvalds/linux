//===- BranchFolding.h - Fold machine code branch instructions --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_BRANCHFOLDING_H
#define LLVM_LIB_CODEGEN_BRANCHFOLDING_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/Support/BlockFrequency.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <vector>

namespace llvm {

class BasicBlock;
class MachineBlockFrequencyInfo;
class MachineBranchProbabilityInfo;
class MachineFunction;
class MachineLoopInfo;
class MachineModuleInfo;
class MachineRegisterInfo;
class raw_ostream;
class TargetInstrInfo;
class TargetRegisterInfo;

  class LLVM_LIBRARY_VISIBILITY BranchFolder {
  public:
    class MBFIWrapper;

    explicit BranchFolder(bool defaultEnableTailMerge,
                          bool CommonHoist,
                          MBFIWrapper &FreqInfo,
                          const MachineBranchProbabilityInfo &ProbInfo,
                          // Min tail length to merge. Defaults to commandline
                          // flag. Ignored for optsize.
                          unsigned MinTailLength = 0);

    /// Perhaps branch folding, tail merging and other CFG optimizations on the
    /// given function.  Block placement changes the layout and may create new
    /// tail merging opportunities.
    bool OptimizeFunction(MachineFunction &MF, const TargetInstrInfo *tii,
                          const TargetRegisterInfo *tri, MachineModuleInfo *mmi,
                          MachineLoopInfo *mli = nullptr,
                          bool AfterPlacement = false);

  private:
    class MergePotentialsElt {
      unsigned Hash;
      MachineBasicBlock *Block;

    public:
      MergePotentialsElt(unsigned h, MachineBasicBlock *b)
        : Hash(h), Block(b) {}

      unsigned getHash() const { return Hash; }
      MachineBasicBlock *getBlock() const { return Block; }

      void setBlock(MachineBasicBlock *MBB) {
        Block = MBB;
      }

      bool operator<(const MergePotentialsElt &) const;
    };

    using MPIterator = std::vector<MergePotentialsElt>::iterator;

    std::vector<MergePotentialsElt> MergePotentials;
    SmallPtrSet<const MachineBasicBlock*, 2> TriedMerging;
    DenseMap<const MachineBasicBlock *, int> EHScopeMembership;

    class SameTailElt {
      MPIterator MPIter;
      MachineBasicBlock::iterator TailStartPos;

    public:
      SameTailElt(MPIterator mp, MachineBasicBlock::iterator tsp)
        : MPIter(mp), TailStartPos(tsp) {}

      MPIterator getMPIter() const {
        return MPIter;
      }

      MergePotentialsElt &getMergePotentialsElt() const {
        return *getMPIter();
      }

      MachineBasicBlock::iterator getTailStartPos() const {
        return TailStartPos;
      }

      unsigned getHash() const {
        return getMergePotentialsElt().getHash();
      }

      MachineBasicBlock *getBlock() const {
        return getMergePotentialsElt().getBlock();
      }

      bool tailIsWholeBlock() const {
        return TailStartPos == getBlock()->begin();
      }

      void setBlock(MachineBasicBlock *MBB) {
        getMergePotentialsElt().setBlock(MBB);
      }

      void setTailStartPos(MachineBasicBlock::iterator Pos) {
        TailStartPos = Pos;
      }
    };
    std::vector<SameTailElt> SameTails;

    bool AfterBlockPlacement;
    bool EnableTailMerge;
    bool EnableHoistCommonCode;
    bool UpdateLiveIns;
    unsigned MinCommonTailLength;
    const TargetInstrInfo *TII;
    const MachineRegisterInfo *MRI;
    const TargetRegisterInfo *TRI;
    MachineModuleInfo *MMI;
    MachineLoopInfo *MLI;
    LivePhysRegs LiveRegs;

  public:
    /// This class keeps track of branch frequencies of newly created
    /// blocks and tail-merged blocks.
    class MBFIWrapper {
    public:
      MBFIWrapper(const MachineBlockFrequencyInfo &I) : MBFI(I) {}

      BlockFrequency getBlockFreq(const MachineBasicBlock *MBB) const;
      void setBlockFreq(const MachineBasicBlock *MBB, BlockFrequency F);
      raw_ostream &printBlockFreq(raw_ostream &OS,
                                  const MachineBasicBlock *MBB) const;
      raw_ostream &printBlockFreq(raw_ostream &OS,
                                  const BlockFrequency Freq) const;
      void view(const Twine &Name, bool isSimple = true);
      uint64_t getEntryFreq() const;

    private:
      const MachineBlockFrequencyInfo &MBFI;
      DenseMap<const MachineBasicBlock *, BlockFrequency> MergedBBFreq;
    };

  private:
    MBFIWrapper &MBBFreqInfo;
    const MachineBranchProbabilityInfo &MBPI;

    bool TailMergeBlocks(MachineFunction &MF);
    bool TryTailMergeBlocks(MachineBasicBlock* SuccBB,
                       MachineBasicBlock* PredBB,
                       unsigned MinCommonTailLength);
    void setCommonTailEdgeWeights(MachineBasicBlock &TailMBB);

    /// Delete the instruction OldInst and everything after it, replacing it
    /// with an unconditional branch to NewDest.
    void replaceTailWithBranchTo(MachineBasicBlock::iterator OldInst,
                                 MachineBasicBlock &NewDest);

    /// Given a machine basic block and an iterator into it, split the MBB so
    /// that the part before the iterator falls into the part starting at the
    /// iterator.  This returns the new MBB.
    MachineBasicBlock *SplitMBBAt(MachineBasicBlock &CurMBB,
                                  MachineBasicBlock::iterator BBI1,
                                  const BasicBlock *BB);

    /// Look through all the blocks in MergePotentials that have hash CurHash
    /// (guaranteed to match the last element).  Build the vector SameTails of
    /// all those that have the (same) largest number of instructions in common
    /// of any pair of these blocks.  SameTails entries contain an iterator into
    /// MergePotentials (from which the MachineBasicBlock can be found) and a
    /// MachineBasicBlock::iterator into that MBB indicating the instruction
    /// where the matching code sequence begins.  Order of elements in SameTails
    /// is the reverse of the order in which those blocks appear in
    /// MergePotentials (where they are not necessarily consecutive).
    unsigned ComputeSameTails(unsigned CurHash, unsigned minCommonTailLength,
                              MachineBasicBlock *SuccBB,
                              MachineBasicBlock *PredBB);

    /// Remove all blocks with hash CurHash from MergePotentials, restoring
    /// branches at ends of blocks as appropriate.
    void RemoveBlocksWithHash(unsigned CurHash, MachineBasicBlock* SuccBB,
                                                MachineBasicBlock* PredBB);

    /// None of the blocks to be tail-merged consist only of the common tail.
    /// Create a block that does by splitting one.
    bool CreateCommonTailOnlyBlock(MachineBasicBlock *&PredBB,
                                   MachineBasicBlock *SuccBB,
                                   unsigned maxCommonTailLength,
                                   unsigned &commonTailIndex);

    /// Create merged DebugLocs of identical instructions across SameTails and
    /// assign it to the instruction in common tail; merge MMOs and undef flags.
    void mergeCommonTails(unsigned commonTailIndex);

    bool OptimizeBranches(MachineFunction &MF);

    /// Analyze and optimize control flow related to the specified block. This
    /// is never called on the entry block.
    bool OptimizeBlock(MachineBasicBlock *MBB);

    /// Remove the specified dead machine basic block from the function,
    /// updating the CFG.
    void RemoveDeadBlock(MachineBasicBlock *MBB);

    /// Hoist common instruction sequences at the start of basic blocks to their
    /// common predecessor.
    bool HoistCommonCode(MachineFunction &MF);

    /// If the successors of MBB has common instruction sequence at the start of
    /// the function, move the instructions before MBB terminator if it's legal.
    bool HoistCommonCodeInSuccs(MachineBasicBlock *MBB);
  };

} // end namespace llvm

#endif // LLVM_LIB_CODEGEN_BRANCHFOLDING_H
