//===- PhiElimination.cpp - Eliminate PHI nodes by inserting copies -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass eliminates machine instruction PHI nodes by inserting copy
// instructions.  This destroys SSA information, but is the desired input for
// some register allocators.
//
//===----------------------------------------------------------------------===//

#include "PHIEliminationUtils.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <iterator>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "phi-node-elimination"

static cl::opt<bool>
DisableEdgeSplitting("disable-phi-elim-edge-splitting", cl::init(false),
                     cl::Hidden, cl::desc("Disable critical edge splitting "
                                          "during PHI elimination"));

static cl::opt<bool>
SplitAllCriticalEdges("phi-elim-split-all-critical-edges", cl::init(false),
                      cl::Hidden, cl::desc("Split all critical edges during "
                                           "PHI elimination"));

static cl::opt<bool> NoPhiElimLiveOutEarlyExit(
    "no-phi-elim-live-out-early-exit", cl::init(false), cl::Hidden,
    cl::desc("Do not use an early exit if isLiveOutPastPHIs returns true."));

namespace {

  class PHIElimination : public MachineFunctionPass {
    MachineRegisterInfo *MRI; // Machine register information
    LiveVariables *LV;
    LiveIntervals *LIS;

  public:
    static char ID; // Pass identification, replacement for typeid

    PHIElimination() : MachineFunctionPass(ID) {
      initializePHIEliminationPass(*PassRegistry::getPassRegistry());
    }

    bool runOnMachineFunction(MachineFunction &MF) override;
    void getAnalysisUsage(AnalysisUsage &AU) const override;

  private:
    /// EliminatePHINodes - Eliminate phi nodes by inserting copy instructions
    /// in predecessor basic blocks.
    bool EliminatePHINodes(MachineFunction &MF, MachineBasicBlock &MBB);

    void LowerPHINode(MachineBasicBlock &MBB,
                      MachineBasicBlock::iterator LastPHIIt);

    /// analyzePHINodes - Gather information about the PHI nodes in
    /// here. In particular, we want to map the number of uses of a virtual
    /// register which is used in a PHI node. We map that to the BB the
    /// vreg is coming from. This is used later to determine when the vreg
    /// is killed in the BB.
    void analyzePHINodes(const MachineFunction& MF);

    /// Split critical edges where necessary for good coalescer performance.
    bool SplitPHIEdges(MachineFunction &MF, MachineBasicBlock &MBB,
                       MachineLoopInfo *MLI);

    // These functions are temporary abstractions around LiveVariables and
    // LiveIntervals, so they can go away when LiveVariables does.
    bool isLiveIn(unsigned Reg, const MachineBasicBlock *MBB);
    bool isLiveOutPastPHIs(unsigned Reg, const MachineBasicBlock *MBB);

    using BBVRegPair = std::pair<unsigned, unsigned>;
    using VRegPHIUse = DenseMap<BBVRegPair, unsigned>;

    VRegPHIUse VRegPHIUseCount;

    // Defs of PHI sources which are implicit_def.
    SmallPtrSet<MachineInstr*, 4> ImpDefs;

    // Map reusable lowered PHI node -> incoming join register.
    using LoweredPHIMap =
        DenseMap<MachineInstr*, unsigned, MachineInstrExpressionTrait>;
    LoweredPHIMap LoweredPHIs;
  };

} // end anonymous namespace

STATISTIC(NumLowered, "Number of phis lowered");
STATISTIC(NumCriticalEdgesSplit, "Number of critical edges split");
STATISTIC(NumReused, "Number of reused lowered phis");

char PHIElimination::ID = 0;

char& llvm::PHIEliminationID = PHIElimination::ID;

INITIALIZE_PASS_BEGIN(PHIElimination, DEBUG_TYPE,
                      "Eliminate PHI nodes for register allocation",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(LiveVariables)
INITIALIZE_PASS_END(PHIElimination, DEBUG_TYPE,
                    "Eliminate PHI nodes for register allocation", false, false)

void PHIElimination::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addUsedIfAvailable<LiveVariables>();
  AU.addPreserved<LiveVariables>();
  AU.addPreserved<SlotIndexes>();
  AU.addPreserved<LiveIntervals>();
  AU.addPreserved<MachineDominatorTree>();
  AU.addPreserved<MachineLoopInfo>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool PHIElimination::runOnMachineFunction(MachineFunction &MF) {
  MRI = &MF.getRegInfo();
  LV = getAnalysisIfAvailable<LiveVariables>();
  LIS = getAnalysisIfAvailable<LiveIntervals>();

  bool Changed = false;

  // This pass takes the function out of SSA form.
  MRI->leaveSSA();

  // Split critical edges to help the coalescer.
  if (!DisableEdgeSplitting && (LV || LIS)) {
    MachineLoopInfo *MLI = getAnalysisIfAvailable<MachineLoopInfo>();
    for (auto &MBB : MF)
      Changed |= SplitPHIEdges(MF, MBB, MLI);
  }

  // Populate VRegPHIUseCount
  analyzePHINodes(MF);

  // Eliminate PHI instructions by inserting copies into predecessor blocks.
  for (auto &MBB : MF)
    Changed |= EliminatePHINodes(MF, MBB);

  // Remove dead IMPLICIT_DEF instructions.
  for (MachineInstr *DefMI : ImpDefs) {
    unsigned DefReg = DefMI->getOperand(0).getReg();
    if (MRI->use_nodbg_empty(DefReg)) {
      if (LIS)
        LIS->RemoveMachineInstrFromMaps(*DefMI);
      DefMI->eraseFromParent();
    }
  }

  // Clean up the lowered PHI instructions.
  for (auto &I : LoweredPHIs) {
    if (LIS)
      LIS->RemoveMachineInstrFromMaps(*I.first);
    MF.DeleteMachineInstr(I.first);
  }

  LoweredPHIs.clear();
  ImpDefs.clear();
  VRegPHIUseCount.clear();

  MF.getProperties().set(MachineFunctionProperties::Property::NoPHIs);

  return Changed;
}

/// EliminatePHINodes - Eliminate phi nodes by inserting copy instructions in
/// predecessor basic blocks.
bool PHIElimination::EliminatePHINodes(MachineFunction &MF,
                                       MachineBasicBlock &MBB) {
  if (MBB.empty() || !MBB.front().isPHI())
    return false;   // Quick exit for basic blocks without PHIs.

  // Get an iterator to the last PHI node.
  MachineBasicBlock::iterator LastPHIIt =
    std::prev(MBB.SkipPHIsAndLabels(MBB.begin()));

  while (MBB.front().isPHI())
    LowerPHINode(MBB, LastPHIIt);

  return true;
}

/// Return true if all defs of VirtReg are implicit-defs.
/// This includes registers with no defs.
static bool isImplicitlyDefined(unsigned VirtReg,
                                const MachineRegisterInfo &MRI) {
  for (MachineInstr &DI : MRI.def_instructions(VirtReg))
    if (!DI.isImplicitDef())
      return false;
  return true;
}

/// Return true if all sources of the phi node are implicit_def's, or undef's.
static bool allPhiOperandsUndefined(const MachineInstr &MPhi,
                                    const MachineRegisterInfo &MRI) {
  for (unsigned I = 1, E = MPhi.getNumOperands(); I != E; I += 2) {
    const MachineOperand &MO = MPhi.getOperand(I);
    if (!isImplicitlyDefined(MO.getReg(), MRI) && !MO.isUndef())
      return false;
  }
  return true;
}
/// LowerPHINode - Lower the PHI node at the top of the specified block.
void PHIElimination::LowerPHINode(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator LastPHIIt) {
  ++NumLowered;

  MachineBasicBlock::iterator AfterPHIsIt = std::next(LastPHIIt);

  // Unlink the PHI node from the basic block, but don't delete the PHI yet.
  MachineInstr *MPhi = MBB.remove(&*MBB.begin());

  unsigned NumSrcs = (MPhi->getNumOperands() - 1) / 2;
  unsigned DestReg = MPhi->getOperand(0).getReg();
  assert(MPhi->getOperand(0).getSubReg() == 0 && "Can't handle sub-reg PHIs");
  bool isDead = MPhi->getOperand(0).isDead();

  // Create a new register for the incoming PHI arguments.
  MachineFunction &MF = *MBB.getParent();
  unsigned IncomingReg = 0;
  bool reusedIncoming = false;  // Is IncomingReg reused from an earlier PHI?

  // Insert a register to register copy at the top of the current block (but
  // after any remaining phi nodes) which copies the new incoming register
  // into the phi node destination.
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  if (allPhiOperandsUndefined(*MPhi, *MRI))
    // If all sources of a PHI node are implicit_def or undef uses, just emit an
    // implicit_def instead of a copy.
    BuildMI(MBB, AfterPHIsIt, MPhi->getDebugLoc(),
            TII->get(TargetOpcode::IMPLICIT_DEF), DestReg);
  else {
    // Can we reuse an earlier PHI node? This only happens for critical edges,
    // typically those created by tail duplication.
    unsigned &entry = LoweredPHIs[MPhi];
    if (entry) {
      // An identical PHI node was already lowered. Reuse the incoming register.
      IncomingReg = entry;
      reusedIncoming = true;
      ++NumReused;
      LLVM_DEBUG(dbgs() << "Reusing " << printReg(IncomingReg) << " for "
                        << *MPhi);
    } else {
      const TargetRegisterClass *RC = MF.getRegInfo().getRegClass(DestReg);
      entry = IncomingReg = MF.getRegInfo().createVirtualRegister(RC);
    }
    BuildMI(MBB, AfterPHIsIt, MPhi->getDebugLoc(),
            TII->get(TargetOpcode::COPY), DestReg)
      .addReg(IncomingReg);
  }

  // Update live variable information if there is any.
  if (LV) {
    MachineInstr &PHICopy = *std::prev(AfterPHIsIt);

    if (IncomingReg) {
      LiveVariables::VarInfo &VI = LV->getVarInfo(IncomingReg);

      // Increment use count of the newly created virtual register.
      LV->setPHIJoin(IncomingReg);

      // When we are reusing the incoming register, it may already have been
      // killed in this block. The old kill will also have been inserted at
      // AfterPHIsIt, so it appears before the current PHICopy.
      if (reusedIncoming)
        if (MachineInstr *OldKill = VI.findKill(&MBB)) {
          LLVM_DEBUG(dbgs() << "Remove old kill from " << *OldKill);
          LV->removeVirtualRegisterKilled(IncomingReg, *OldKill);
          LLVM_DEBUG(MBB.dump());
        }

      // Add information to LiveVariables to know that the incoming value is
      // killed.  Note that because the value is defined in several places (once
      // each for each incoming block), the "def" block and instruction fields
      // for the VarInfo is not filled in.
      LV->addVirtualRegisterKilled(IncomingReg, PHICopy);
    }

    // Since we are going to be deleting the PHI node, if it is the last use of
    // any registers, or if the value itself is dead, we need to move this
    // information over to the new copy we just inserted.
    LV->removeVirtualRegistersKilled(*MPhi);

    // If the result is dead, update LV.
    if (isDead) {
      LV->addVirtualRegisterDead(DestReg, PHICopy);
      LV->removeVirtualRegisterDead(DestReg, *MPhi);
    }
  }

  // Update LiveIntervals for the new copy or implicit def.
  if (LIS) {
    SlotIndex DestCopyIndex =
        LIS->InsertMachineInstrInMaps(*std::prev(AfterPHIsIt));

    SlotIndex MBBStartIndex = LIS->getMBBStartIdx(&MBB);
    if (IncomingReg) {
      // Add the region from the beginning of MBB to the copy instruction to
      // IncomingReg's live interval.
      LiveInterval &IncomingLI = LIS->createEmptyInterval(IncomingReg);
      VNInfo *IncomingVNI = IncomingLI.getVNInfoAt(MBBStartIndex);
      if (!IncomingVNI)
        IncomingVNI = IncomingLI.getNextValue(MBBStartIndex,
                                              LIS->getVNInfoAllocator());
      IncomingLI.addSegment(LiveInterval::Segment(MBBStartIndex,
                                                  DestCopyIndex.getRegSlot(),
                                                  IncomingVNI));
    }

    LiveInterval &DestLI = LIS->getInterval(DestReg);
    assert(DestLI.begin() != DestLI.end() &&
           "PHIs should have nonempty LiveIntervals.");
    if (DestLI.endIndex().isDead()) {
      // A dead PHI's live range begins and ends at the start of the MBB, but
      // the lowered copy, which will still be dead, needs to begin and end at
      // the copy instruction.
      VNInfo *OrigDestVNI = DestLI.getVNInfoAt(MBBStartIndex);
      assert(OrigDestVNI && "PHI destination should be live at block entry.");
      DestLI.removeSegment(MBBStartIndex, MBBStartIndex.getDeadSlot());
      DestLI.createDeadDef(DestCopyIndex.getRegSlot(),
                           LIS->getVNInfoAllocator());
      DestLI.removeValNo(OrigDestVNI);
    } else {
      // Otherwise, remove the region from the beginning of MBB to the copy
      // instruction from DestReg's live interval.
      DestLI.removeSegment(MBBStartIndex, DestCopyIndex.getRegSlot());
      VNInfo *DestVNI = DestLI.getVNInfoAt(DestCopyIndex.getRegSlot());
      assert(DestVNI && "PHI destination should be live at its definition.");
      DestVNI->def = DestCopyIndex.getRegSlot();
    }
  }

  // Adjust the VRegPHIUseCount map to account for the removal of this PHI node.
  for (unsigned i = 1; i != MPhi->getNumOperands(); i += 2)
    --VRegPHIUseCount[BBVRegPair(MPhi->getOperand(i+1).getMBB()->getNumber(),
                                 MPhi->getOperand(i).getReg())];

  // Now loop over all of the incoming arguments, changing them to copy into the
  // IncomingReg register in the corresponding predecessor basic block.
  SmallPtrSet<MachineBasicBlock*, 8> MBBsInsertedInto;
  for (int i = NumSrcs - 1; i >= 0; --i) {
    unsigned SrcReg = MPhi->getOperand(i*2+1).getReg();
    unsigned SrcSubReg = MPhi->getOperand(i*2+1).getSubReg();
    bool SrcUndef = MPhi->getOperand(i*2+1).isUndef() ||
      isImplicitlyDefined(SrcReg, *MRI);
    assert(TargetRegisterInfo::isVirtualRegister(SrcReg) &&
           "Machine PHI Operands must all be virtual registers!");

    // Get the MachineBasicBlock equivalent of the BasicBlock that is the source
    // path the PHI.
    MachineBasicBlock &opBlock = *MPhi->getOperand(i*2+2).getMBB();

    // Check to make sure we haven't already emitted the copy for this block.
    // This can happen because PHI nodes may have multiple entries for the same
    // basic block.
    if (!MBBsInsertedInto.insert(&opBlock).second)
      continue;  // If the copy has already been emitted, we're done.

    // Find a safe location to insert the copy, this may be the first terminator
    // in the block (or end()).
    MachineBasicBlock::iterator InsertPos =
      findPHICopyInsertPoint(&opBlock, &MBB, SrcReg);

    // Insert the copy.
    MachineInstr *NewSrcInstr = nullptr;
    if (!reusedIncoming && IncomingReg) {
      if (SrcUndef) {
        // The source register is undefined, so there is no need for a real
        // COPY, but we still need to ensure joint dominance by defs.
        // Insert an IMPLICIT_DEF instruction.
        NewSrcInstr = BuildMI(opBlock, InsertPos, MPhi->getDebugLoc(),
                              TII->get(TargetOpcode::IMPLICIT_DEF),
                              IncomingReg);

        // Clean up the old implicit-def, if there even was one.
        if (MachineInstr *DefMI = MRI->getVRegDef(SrcReg))
          if (DefMI->isImplicitDef())
            ImpDefs.insert(DefMI);
      } else {
        NewSrcInstr = BuildMI(opBlock, InsertPos, MPhi->getDebugLoc(),
                            TII->get(TargetOpcode::COPY), IncomingReg)
                        .addReg(SrcReg, 0, SrcSubReg);
      }
    }

    // We only need to update the LiveVariables kill of SrcReg if this was the
    // last PHI use of SrcReg to be lowered on this CFG edge and it is not live
    // out of the predecessor. We can also ignore undef sources.
    if (LV && !SrcUndef &&
        !VRegPHIUseCount[BBVRegPair(opBlock.getNumber(), SrcReg)] &&
        !LV->isLiveOut(SrcReg, opBlock)) {
      // We want to be able to insert a kill of the register if this PHI (aka,
      // the copy we just inserted) is the last use of the source value. Live
      // variable analysis conservatively handles this by saying that the value
      // is live until the end of the block the PHI entry lives in. If the value
      // really is dead at the PHI copy, there will be no successor blocks which
      // have the value live-in.

      // Okay, if we now know that the value is not live out of the block, we
      // can add a kill marker in this block saying that it kills the incoming
      // value!

      // In our final twist, we have to decide which instruction kills the
      // register.  In most cases this is the copy, however, terminator
      // instructions at the end of the block may also use the value. In this
      // case, we should mark the last such terminator as being the killing
      // block, not the copy.
      MachineBasicBlock::iterator KillInst = opBlock.end();
      MachineBasicBlock::iterator FirstTerm = opBlock.getFirstTerminator();
      for (MachineBasicBlock::iterator Term = FirstTerm;
          Term != opBlock.end(); ++Term) {
        if (Term->readsRegister(SrcReg))
          KillInst = Term;
      }

      if (KillInst == opBlock.end()) {
        // No terminator uses the register.

        if (reusedIncoming || !IncomingReg) {
          // We may have to rewind a bit if we didn't insert a copy this time.
          KillInst = FirstTerm;
          while (KillInst != opBlock.begin()) {
            --KillInst;
            if (KillInst->isDebugInstr())
              continue;
            if (KillInst->readsRegister(SrcReg))
              break;
          }
        } else {
          // We just inserted this copy.
          KillInst = std::prev(InsertPos);
        }
      }
      assert(KillInst->readsRegister(SrcReg) && "Cannot find kill instruction");

      // Finally, mark it killed.
      LV->addVirtualRegisterKilled(SrcReg, *KillInst);

      // This vreg no longer lives all of the way through opBlock.
      unsigned opBlockNum = opBlock.getNumber();
      LV->getVarInfo(SrcReg).AliveBlocks.reset(opBlockNum);
    }

    if (LIS) {
      if (NewSrcInstr) {
        LIS->InsertMachineInstrInMaps(*NewSrcInstr);
        LIS->addSegmentToEndOfBlock(IncomingReg, *NewSrcInstr);
      }

      if (!SrcUndef &&
          !VRegPHIUseCount[BBVRegPair(opBlock.getNumber(), SrcReg)]) {
        LiveInterval &SrcLI = LIS->getInterval(SrcReg);

        bool isLiveOut = false;
        for (MachineBasicBlock::succ_iterator SI = opBlock.succ_begin(),
             SE = opBlock.succ_end(); SI != SE; ++SI) {
          SlotIndex startIdx = LIS->getMBBStartIdx(*SI);
          VNInfo *VNI = SrcLI.getVNInfoAt(startIdx);

          // Definitions by other PHIs are not truly live-in for our purposes.
          if (VNI && VNI->def != startIdx) {
            isLiveOut = true;
            break;
          }
        }

        if (!isLiveOut) {
          MachineBasicBlock::iterator KillInst = opBlock.end();
          MachineBasicBlock::iterator FirstTerm = opBlock.getFirstTerminator();
          for (MachineBasicBlock::iterator Term = FirstTerm;
              Term != opBlock.end(); ++Term) {
            if (Term->readsRegister(SrcReg))
              KillInst = Term;
          }

          if (KillInst == opBlock.end()) {
            // No terminator uses the register.

            if (reusedIncoming || !IncomingReg) {
              // We may have to rewind a bit if we didn't just insert a copy.
              KillInst = FirstTerm;
              while (KillInst != opBlock.begin()) {
                --KillInst;
                if (KillInst->isDebugInstr())
                  continue;
                if (KillInst->readsRegister(SrcReg))
                  break;
              }
            } else {
              // We just inserted this copy.
              KillInst = std::prev(InsertPos);
            }
          }
          assert(KillInst->readsRegister(SrcReg) &&
                 "Cannot find kill instruction");

          SlotIndex LastUseIndex = LIS->getInstructionIndex(*KillInst);
          SrcLI.removeSegment(LastUseIndex.getRegSlot(),
                              LIS->getMBBEndIdx(&opBlock));
        }
      }
    }
  }

  // Really delete the PHI instruction now, if it is not in the LoweredPHIs map.
  if (reusedIncoming || !IncomingReg) {
    if (LIS)
      LIS->RemoveMachineInstrFromMaps(*MPhi);
    MF.DeleteMachineInstr(MPhi);
  }
}

/// analyzePHINodes - Gather information about the PHI nodes in here. In
/// particular, we want to map the number of uses of a virtual register which is
/// used in a PHI node. We map that to the BB the vreg is coming from. This is
/// used later to determine when the vreg is killed in the BB.
void PHIElimination::analyzePHINodes(const MachineFunction& MF) {
  for (const auto &MBB : MF)
    for (const auto &BBI : MBB) {
      if (!BBI.isPHI())
        break;
      for (unsigned i = 1, e = BBI.getNumOperands(); i != e; i += 2)
        ++VRegPHIUseCount[BBVRegPair(BBI.getOperand(i+1).getMBB()->getNumber(),
                                     BBI.getOperand(i).getReg())];
    }
}

bool PHIElimination::SplitPHIEdges(MachineFunction &MF,
                                   MachineBasicBlock &MBB,
                                   MachineLoopInfo *MLI) {
  if (MBB.empty() || !MBB.front().isPHI() || MBB.isEHPad())
    return false;   // Quick exit for basic blocks without PHIs.

  const MachineLoop *CurLoop = MLI ? MLI->getLoopFor(&MBB) : nullptr;
  bool IsLoopHeader = CurLoop && &MBB == CurLoop->getHeader();

  bool Changed = false;
  for (MachineBasicBlock::iterator BBI = MBB.begin(), BBE = MBB.end();
       BBI != BBE && BBI->isPHI(); ++BBI) {
    for (unsigned i = 1, e = BBI->getNumOperands(); i != e; i += 2) {
      unsigned Reg = BBI->getOperand(i).getReg();
      MachineBasicBlock *PreMBB = BBI->getOperand(i+1).getMBB();
      // Is there a critical edge from PreMBB to MBB?
      if (PreMBB->succ_size() == 1)
        continue;

      // Avoid splitting backedges of loops. It would introduce small
      // out-of-line blocks into the loop which is very bad for code placement.
      if (PreMBB == &MBB && !SplitAllCriticalEdges)
        continue;
      const MachineLoop *PreLoop = MLI ? MLI->getLoopFor(PreMBB) : nullptr;
      if (IsLoopHeader && PreLoop == CurLoop && !SplitAllCriticalEdges)
        continue;

      // LV doesn't consider a phi use live-out, so isLiveOut only returns true
      // when the source register is live-out for some other reason than a phi
      // use. That means the copy we will insert in PreMBB won't be a kill, and
      // there is a risk it may not be coalesced away.
      //
      // If the copy would be a kill, there is no need to split the edge.
      bool ShouldSplit = isLiveOutPastPHIs(Reg, PreMBB);
      if (!ShouldSplit && !NoPhiElimLiveOutEarlyExit)
        continue;
      if (ShouldSplit) {
        LLVM_DEBUG(dbgs() << printReg(Reg) << " live-out before critical edge "
                          << printMBBReference(*PreMBB) << " -> "
                          << printMBBReference(MBB) << ": " << *BBI);
      }

      // If Reg is not live-in to MBB, it means it must be live-in to some
      // other PreMBB successor, and we can avoid the interference by splitting
      // the edge.
      //
      // If Reg *is* live-in to MBB, the interference is inevitable and a copy
      // is likely to be left after coalescing. If we are looking at a loop
      // exiting edge, split it so we won't insert code in the loop, otherwise
      // don't bother.
      ShouldSplit = ShouldSplit && !isLiveIn(Reg, &MBB);

      // Check for a loop exiting edge.
      if (!ShouldSplit && CurLoop != PreLoop) {
        LLVM_DEBUG({
          dbgs() << "Split wouldn't help, maybe avoid loop copies?\n";
          if (PreLoop)
            dbgs() << "PreLoop: " << *PreLoop;
          if (CurLoop)
            dbgs() << "CurLoop: " << *CurLoop;
        });
        // This edge could be entering a loop, exiting a loop, or it could be
        // both: Jumping directly form one loop to the header of a sibling
        // loop.
        // Split unless this edge is entering CurLoop from an outer loop.
        ShouldSplit = PreLoop && !PreLoop->contains(CurLoop);
      }
      if (!ShouldSplit && !SplitAllCriticalEdges)
        continue;
      if (!PreMBB->SplitCriticalEdge(&MBB, *this)) {
        LLVM_DEBUG(dbgs() << "Failed to split critical edge.\n");
        continue;
      }
      Changed = true;
      ++NumCriticalEdgesSplit;
    }
  }
  return Changed;
}

bool PHIElimination::isLiveIn(unsigned Reg, const MachineBasicBlock *MBB) {
  assert((LV || LIS) &&
         "isLiveIn() requires either LiveVariables or LiveIntervals");
  if (LIS)
    return LIS->isLiveInToMBB(LIS->getInterval(Reg), MBB);
  else
    return LV->isLiveIn(Reg, *MBB);
}

bool PHIElimination::isLiveOutPastPHIs(unsigned Reg,
                                       const MachineBasicBlock *MBB) {
  assert((LV || LIS) &&
         "isLiveOutPastPHIs() requires either LiveVariables or LiveIntervals");
  // LiveVariables considers uses in PHIs to be in the predecessor basic block,
  // so that a register used only in a PHI is not live out of the block. In
  // contrast, LiveIntervals considers uses in PHIs to be on the edge rather than
  // in the predecessor basic block, so that a register used only in a PHI is live
  // out of the block.
  if (LIS) {
    const LiveInterval &LI = LIS->getInterval(Reg);
    for (const MachineBasicBlock *SI : MBB->successors())
      if (LI.liveAt(LIS->getMBBStartIdx(SI)))
        return true;
    return false;
  } else {
    return LV->isLiveOut(Reg, *MBB);
  }
}
