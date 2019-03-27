//===- MachineSink.cpp - Sinking for machine instructions -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass moves instructions into successor blocks when possible, so that
// they aren't executed on paths where their results aren't needed.
//
// This pass is not intended to be a replacement or a complete alternative
// for an LLVM-IR-level sinking pass. It is only designed to sink simple
// constructs that are not exposed before lowering and instruction selection.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SparseBitVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Pass.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "machine-sink"

static cl::opt<bool>
SplitEdges("machine-sink-split",
           cl::desc("Split critical edges during machine sinking"),
           cl::init(true), cl::Hidden);

static cl::opt<bool>
UseBlockFreqInfo("machine-sink-bfi",
           cl::desc("Use block frequency info to find successors to sink"),
           cl::init(true), cl::Hidden);

static cl::opt<unsigned> SplitEdgeProbabilityThreshold(
    "machine-sink-split-probability-threshold",
    cl::desc(
        "Percentage threshold for splitting single-instruction critical edge. "
        "If the branch threshold is higher than this threshold, we allow "
        "speculative execution of up to 1 instruction to avoid branching to "
        "splitted critical edge"),
    cl::init(40), cl::Hidden);

STATISTIC(NumSunk,      "Number of machine instructions sunk");
STATISTIC(NumSplit,     "Number of critical edges split");
STATISTIC(NumCoalesces, "Number of copies coalesced");
STATISTIC(NumPostRACopySink, "Number of copies sunk after RA");

namespace {

  class MachineSinking : public MachineFunctionPass {
    const TargetInstrInfo *TII;
    const TargetRegisterInfo *TRI;
    MachineRegisterInfo  *MRI;     // Machine register information
    MachineDominatorTree *DT;      // Machine dominator tree
    MachinePostDominatorTree *PDT; // Machine post dominator tree
    MachineLoopInfo *LI;
    const MachineBlockFrequencyInfo *MBFI;
    const MachineBranchProbabilityInfo *MBPI;
    AliasAnalysis *AA;

    // Remember which edges have been considered for breaking.
    SmallSet<std::pair<MachineBasicBlock*, MachineBasicBlock*>, 8>
    CEBCandidates;
    // Remember which edges we are about to split.
    // This is different from CEBCandidates since those edges
    // will be split.
    SetVector<std::pair<MachineBasicBlock *, MachineBasicBlock *>> ToSplit;

    SparseBitVector<> RegsToClearKillFlags;

    using AllSuccsCache =
        std::map<MachineBasicBlock *, SmallVector<MachineBasicBlock *, 4>>;

  public:
    static char ID; // Pass identification

    MachineSinking() : MachineFunctionPass(ID) {
      initializeMachineSinkingPass(*PassRegistry::getPassRegistry());
    }

    bool runOnMachineFunction(MachineFunction &MF) override;

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesCFG();
      MachineFunctionPass::getAnalysisUsage(AU);
      AU.addRequired<AAResultsWrapperPass>();
      AU.addRequired<MachineDominatorTree>();
      AU.addRequired<MachinePostDominatorTree>();
      AU.addRequired<MachineLoopInfo>();
      AU.addRequired<MachineBranchProbabilityInfo>();
      AU.addPreserved<MachineDominatorTree>();
      AU.addPreserved<MachinePostDominatorTree>();
      AU.addPreserved<MachineLoopInfo>();
      if (UseBlockFreqInfo)
        AU.addRequired<MachineBlockFrequencyInfo>();
    }

    void releaseMemory() override {
      CEBCandidates.clear();
    }

  private:
    bool ProcessBlock(MachineBasicBlock &MBB);
    bool isWorthBreakingCriticalEdge(MachineInstr &MI,
                                     MachineBasicBlock *From,
                                     MachineBasicBlock *To);

    /// Postpone the splitting of the given critical
    /// edge (\p From, \p To).
    ///
    /// We do not split the edges on the fly. Indeed, this invalidates
    /// the dominance information and thus triggers a lot of updates
    /// of that information underneath.
    /// Instead, we postpone all the splits after each iteration of
    /// the main loop. That way, the information is at least valid
    /// for the lifetime of an iteration.
    ///
    /// \return True if the edge is marked as toSplit, false otherwise.
    /// False can be returned if, for instance, this is not profitable.
    bool PostponeSplitCriticalEdge(MachineInstr &MI,
                                   MachineBasicBlock *From,
                                   MachineBasicBlock *To,
                                   bool BreakPHIEdge);
    bool SinkInstruction(MachineInstr &MI, bool &SawStore,

                         AllSuccsCache &AllSuccessors);
    bool AllUsesDominatedByBlock(unsigned Reg, MachineBasicBlock *MBB,
                                 MachineBasicBlock *DefMBB,
                                 bool &BreakPHIEdge, bool &LocalUse) const;
    MachineBasicBlock *FindSuccToSinkTo(MachineInstr &MI, MachineBasicBlock *MBB,
               bool &BreakPHIEdge, AllSuccsCache &AllSuccessors);
    bool isProfitableToSinkTo(unsigned Reg, MachineInstr &MI,
                              MachineBasicBlock *MBB,
                              MachineBasicBlock *SuccToSinkTo,
                              AllSuccsCache &AllSuccessors);

    bool PerformTrivialForwardCoalescing(MachineInstr &MI,
                                         MachineBasicBlock *MBB);

    SmallVector<MachineBasicBlock *, 4> &
    GetAllSortedSuccessors(MachineInstr &MI, MachineBasicBlock *MBB,
                           AllSuccsCache &AllSuccessors) const;
  };

} // end anonymous namespace

char MachineSinking::ID = 0;

char &llvm::MachineSinkingID = MachineSinking::ID;

INITIALIZE_PASS_BEGIN(MachineSinking, DEBUG_TYPE,
                      "Machine code sinking", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineBranchProbabilityInfo)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(MachineSinking, DEBUG_TYPE,
                    "Machine code sinking", false, false)

bool MachineSinking::PerformTrivialForwardCoalescing(MachineInstr &MI,
                                                     MachineBasicBlock *MBB) {
  if (!MI.isCopy())
    return false;

  unsigned SrcReg = MI.getOperand(1).getReg();
  unsigned DstReg = MI.getOperand(0).getReg();
  if (!TargetRegisterInfo::isVirtualRegister(SrcReg) ||
      !TargetRegisterInfo::isVirtualRegister(DstReg) ||
      !MRI->hasOneNonDBGUse(SrcReg))
    return false;

  const TargetRegisterClass *SRC = MRI->getRegClass(SrcReg);
  const TargetRegisterClass *DRC = MRI->getRegClass(DstReg);
  if (SRC != DRC)
    return false;

  MachineInstr *DefMI = MRI->getVRegDef(SrcReg);
  if (DefMI->isCopyLike())
    return false;
  LLVM_DEBUG(dbgs() << "Coalescing: " << *DefMI);
  LLVM_DEBUG(dbgs() << "*** to: " << MI);
  MRI->replaceRegWith(DstReg, SrcReg);
  MI.eraseFromParent();

  // Conservatively, clear any kill flags, since it's possible that they are no
  // longer correct.
  MRI->clearKillFlags(SrcReg);

  ++NumCoalesces;
  return true;
}

/// AllUsesDominatedByBlock - Return true if all uses of the specified register
/// occur in blocks dominated by the specified block. If any use is in the
/// definition block, then return false since it is never legal to move def
/// after uses.
bool
MachineSinking::AllUsesDominatedByBlock(unsigned Reg,
                                        MachineBasicBlock *MBB,
                                        MachineBasicBlock *DefMBB,
                                        bool &BreakPHIEdge,
                                        bool &LocalUse) const {
  assert(TargetRegisterInfo::isVirtualRegister(Reg) &&
         "Only makes sense for vregs");

  // Ignore debug uses because debug info doesn't affect the code.
  if (MRI->use_nodbg_empty(Reg))
    return true;

  // BreakPHIEdge is true if all the uses are in the successor MBB being sunken
  // into and they are all PHI nodes. In this case, machine-sink must break
  // the critical edge first. e.g.
  //
  // %bb.1: derived from LLVM BB %bb4.preheader
  //   Predecessors according to CFG: %bb.0
  //     ...
  //     %reg16385 = DEC64_32r %reg16437, implicit-def dead %eflags
  //     ...
  //     JE_4 <%bb.37>, implicit %eflags
  //   Successors according to CFG: %bb.37 %bb.2
  //
  // %bb.2: derived from LLVM BB %bb.nph
  //   Predecessors according to CFG: %bb.0 %bb.1
  //     %reg16386 = PHI %reg16434, %bb.0, %reg16385, %bb.1
  BreakPHIEdge = true;
  for (MachineOperand &MO : MRI->use_nodbg_operands(Reg)) {
    MachineInstr *UseInst = MO.getParent();
    unsigned OpNo = &MO - &UseInst->getOperand(0);
    MachineBasicBlock *UseBlock = UseInst->getParent();
    if (!(UseBlock == MBB && UseInst->isPHI() &&
          UseInst->getOperand(OpNo+1).getMBB() == DefMBB)) {
      BreakPHIEdge = false;
      break;
    }
  }
  if (BreakPHIEdge)
    return true;

  for (MachineOperand &MO : MRI->use_nodbg_operands(Reg)) {
    // Determine the block of the use.
    MachineInstr *UseInst = MO.getParent();
    unsigned OpNo = &MO - &UseInst->getOperand(0);
    MachineBasicBlock *UseBlock = UseInst->getParent();
    if (UseInst->isPHI()) {
      // PHI nodes use the operand in the predecessor block, not the block with
      // the PHI.
      UseBlock = UseInst->getOperand(OpNo+1).getMBB();
    } else if (UseBlock == DefMBB) {
      LocalUse = true;
      return false;
    }

    // Check that it dominates.
    if (!DT->dominates(MBB, UseBlock))
      return false;
  }

  return true;
}

bool MachineSinking::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  LLVM_DEBUG(dbgs() << "******** Machine Sinking ********\n");

  TII = MF.getSubtarget().getInstrInfo();
  TRI = MF.getSubtarget().getRegisterInfo();
  MRI = &MF.getRegInfo();
  DT = &getAnalysis<MachineDominatorTree>();
  PDT = &getAnalysis<MachinePostDominatorTree>();
  LI = &getAnalysis<MachineLoopInfo>();
  MBFI = UseBlockFreqInfo ? &getAnalysis<MachineBlockFrequencyInfo>() : nullptr;
  MBPI = &getAnalysis<MachineBranchProbabilityInfo>();
  AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();

  bool EverMadeChange = false;

  while (true) {
    bool MadeChange = false;

    // Process all basic blocks.
    CEBCandidates.clear();
    ToSplit.clear();
    for (auto &MBB: MF)
      MadeChange |= ProcessBlock(MBB);

    // If we have anything we marked as toSplit, split it now.
    for (auto &Pair : ToSplit) {
      auto NewSucc = Pair.first->SplitCriticalEdge(Pair.second, *this);
      if (NewSucc != nullptr) {
        LLVM_DEBUG(dbgs() << " *** Splitting critical edge: "
                          << printMBBReference(*Pair.first) << " -- "
                          << printMBBReference(*NewSucc) << " -- "
                          << printMBBReference(*Pair.second) << '\n');
        MadeChange = true;
        ++NumSplit;
      } else
        LLVM_DEBUG(dbgs() << " *** Not legal to break critical edge\n");
    }
    // If this iteration over the code changed anything, keep iterating.
    if (!MadeChange) break;
    EverMadeChange = true;
  }

  // Now clear any kill flags for recorded registers.
  for (auto I : RegsToClearKillFlags)
    MRI->clearKillFlags(I);
  RegsToClearKillFlags.clear();

  return EverMadeChange;
}

bool MachineSinking::ProcessBlock(MachineBasicBlock &MBB) {
  // Can't sink anything out of a block that has less than two successors.
  if (MBB.succ_size() <= 1 || MBB.empty()) return false;

  // Don't bother sinking code out of unreachable blocks. In addition to being
  // unprofitable, it can also lead to infinite looping, because in an
  // unreachable loop there may be nowhere to stop.
  if (!DT->isReachableFromEntry(&MBB)) return false;

  bool MadeChange = false;

  // Cache all successors, sorted by frequency info and loop depth.
  AllSuccsCache AllSuccessors;

  // Walk the basic block bottom-up.  Remember if we saw a store.
  MachineBasicBlock::iterator I = MBB.end();
  --I;
  bool ProcessedBegin, SawStore = false;
  do {
    MachineInstr &MI = *I;  // The instruction to sink.

    // Predecrement I (if it's not begin) so that it isn't invalidated by
    // sinking.
    ProcessedBegin = I == MBB.begin();
    if (!ProcessedBegin)
      --I;

    if (MI.isDebugInstr())
      continue;

    bool Joined = PerformTrivialForwardCoalescing(MI, &MBB);
    if (Joined) {
      MadeChange = true;
      continue;
    }

    if (SinkInstruction(MI, SawStore, AllSuccessors)) {
      ++NumSunk;
      MadeChange = true;
    }

    // If we just processed the first instruction in the block, we're done.
  } while (!ProcessedBegin);

  return MadeChange;
}

bool MachineSinking::isWorthBreakingCriticalEdge(MachineInstr &MI,
                                                 MachineBasicBlock *From,
                                                 MachineBasicBlock *To) {
  // FIXME: Need much better heuristics.

  // If the pass has already considered breaking this edge (during this pass
  // through the function), then let's go ahead and break it. This means
  // sinking multiple "cheap" instructions into the same block.
  if (!CEBCandidates.insert(std::make_pair(From, To)).second)
    return true;

  if (!MI.isCopy() && !TII->isAsCheapAsAMove(MI))
    return true;

  if (From->isSuccessor(To) && MBPI->getEdgeProbability(From, To) <=
      BranchProbability(SplitEdgeProbabilityThreshold, 100))
    return true;

  // MI is cheap, we probably don't want to break the critical edge for it.
  // However, if this would allow some definitions of its source operands
  // to be sunk then it's probably worth it.
  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI.getOperand(i);
    if (!MO.isReg() || !MO.isUse())
      continue;
    unsigned Reg = MO.getReg();
    if (Reg == 0)
      continue;

    // We don't move live definitions of physical registers,
    // so sinking their uses won't enable any opportunities.
    if (TargetRegisterInfo::isPhysicalRegister(Reg))
      continue;

    // If this instruction is the only user of a virtual register,
    // check if breaking the edge will enable sinking
    // both this instruction and the defining instruction.
    if (MRI->hasOneNonDBGUse(Reg)) {
      // If the definition resides in same MBB,
      // claim it's likely we can sink these together.
      // If definition resides elsewhere, we aren't
      // blocking it from being sunk so don't break the edge.
      MachineInstr *DefMI = MRI->getVRegDef(Reg);
      if (DefMI->getParent() == MI.getParent())
        return true;
    }
  }

  return false;
}

bool MachineSinking::PostponeSplitCriticalEdge(MachineInstr &MI,
                                               MachineBasicBlock *FromBB,
                                               MachineBasicBlock *ToBB,
                                               bool BreakPHIEdge) {
  if (!isWorthBreakingCriticalEdge(MI, FromBB, ToBB))
    return false;

  // Avoid breaking back edge. From == To means backedge for single BB loop.
  if (!SplitEdges || FromBB == ToBB)
    return false;

  // Check for backedges of more "complex" loops.
  if (LI->getLoopFor(FromBB) == LI->getLoopFor(ToBB) &&
      LI->isLoopHeader(ToBB))
    return false;

  // It's not always legal to break critical edges and sink the computation
  // to the edge.
  //
  // %bb.1:
  // v1024
  // Beq %bb.3
  // <fallthrough>
  // %bb.2:
  // ... no uses of v1024
  // <fallthrough>
  // %bb.3:
  // ...
  //       = v1024
  //
  // If %bb.1 -> %bb.3 edge is broken and computation of v1024 is inserted:
  //
  // %bb.1:
  // ...
  // Bne %bb.2
  // %bb.4:
  // v1024 =
  // B %bb.3
  // %bb.2:
  // ... no uses of v1024
  // <fallthrough>
  // %bb.3:
  // ...
  //       = v1024
  //
  // This is incorrect since v1024 is not computed along the %bb.1->%bb.2->%bb.3
  // flow. We need to ensure the new basic block where the computation is
  // sunk to dominates all the uses.
  // It's only legal to break critical edge and sink the computation to the
  // new block if all the predecessors of "To", except for "From", are
  // not dominated by "From". Given SSA property, this means these
  // predecessors are dominated by "To".
  //
  // There is no need to do this check if all the uses are PHI nodes. PHI
  // sources are only defined on the specific predecessor edges.
  if (!BreakPHIEdge) {
    for (MachineBasicBlock::pred_iterator PI = ToBB->pred_begin(),
           E = ToBB->pred_end(); PI != E; ++PI) {
      if (*PI == FromBB)
        continue;
      if (!DT->dominates(ToBB, *PI))
        return false;
    }
  }

  ToSplit.insert(std::make_pair(FromBB, ToBB));

  return true;
}

/// isProfitableToSinkTo - Return true if it is profitable to sink MI.
bool MachineSinking::isProfitableToSinkTo(unsigned Reg, MachineInstr &MI,
                                          MachineBasicBlock *MBB,
                                          MachineBasicBlock *SuccToSinkTo,
                                          AllSuccsCache &AllSuccessors) {
  assert (SuccToSinkTo && "Invalid SinkTo Candidate BB");

  if (MBB == SuccToSinkTo)
    return false;

  // It is profitable if SuccToSinkTo does not post dominate current block.
  if (!PDT->dominates(SuccToSinkTo, MBB))
    return true;

  // It is profitable to sink an instruction from a deeper loop to a shallower
  // loop, even if the latter post-dominates the former (PR21115).
  if (LI->getLoopDepth(MBB) > LI->getLoopDepth(SuccToSinkTo))
    return true;

  // Check if only use in post dominated block is PHI instruction.
  bool NonPHIUse = false;
  for (MachineInstr &UseInst : MRI->use_nodbg_instructions(Reg)) {
    MachineBasicBlock *UseBlock = UseInst.getParent();
    if (UseBlock == SuccToSinkTo && !UseInst.isPHI())
      NonPHIUse = true;
  }
  if (!NonPHIUse)
    return true;

  // If SuccToSinkTo post dominates then also it may be profitable if MI
  // can further profitably sinked into another block in next round.
  bool BreakPHIEdge = false;
  // FIXME - If finding successor is compile time expensive then cache results.
  if (MachineBasicBlock *MBB2 =
          FindSuccToSinkTo(MI, SuccToSinkTo, BreakPHIEdge, AllSuccessors))
    return isProfitableToSinkTo(Reg, MI, SuccToSinkTo, MBB2, AllSuccessors);

  // If SuccToSinkTo is final destination and it is a post dominator of current
  // block then it is not profitable to sink MI into SuccToSinkTo block.
  return false;
}

/// Get the sorted sequence of successors for this MachineBasicBlock, possibly
/// computing it if it was not already cached.
SmallVector<MachineBasicBlock *, 4> &
MachineSinking::GetAllSortedSuccessors(MachineInstr &MI, MachineBasicBlock *MBB,
                                       AllSuccsCache &AllSuccessors) const {
  // Do we have the sorted successors in cache ?
  auto Succs = AllSuccessors.find(MBB);
  if (Succs != AllSuccessors.end())
    return Succs->second;

  SmallVector<MachineBasicBlock *, 4> AllSuccs(MBB->succ_begin(),
                                               MBB->succ_end());

  // Handle cases where sinking can happen but where the sink point isn't a
  // successor. For example:
  //
  //   x = computation
  //   if () {} else {}
  //   use x
  //
  const std::vector<MachineDomTreeNode *> &Children =
    DT->getNode(MBB)->getChildren();
  for (const auto &DTChild : Children)
    // DomTree children of MBB that have MBB as immediate dominator are added.
    if (DTChild->getIDom()->getBlock() == MI.getParent() &&
        // Skip MBBs already added to the AllSuccs vector above.
        !MBB->isSuccessor(DTChild->getBlock()))
      AllSuccs.push_back(DTChild->getBlock());

  // Sort Successors according to their loop depth or block frequency info.
  std::stable_sort(
      AllSuccs.begin(), AllSuccs.end(),
      [this](const MachineBasicBlock *L, const MachineBasicBlock *R) {
        uint64_t LHSFreq = MBFI ? MBFI->getBlockFreq(L).getFrequency() : 0;
        uint64_t RHSFreq = MBFI ? MBFI->getBlockFreq(R).getFrequency() : 0;
        bool HasBlockFreq = LHSFreq != 0 && RHSFreq != 0;
        return HasBlockFreq ? LHSFreq < RHSFreq
                            : LI->getLoopDepth(L) < LI->getLoopDepth(R);
      });

  auto it = AllSuccessors.insert(std::make_pair(MBB, AllSuccs));

  return it.first->second;
}

/// FindSuccToSinkTo - Find a successor to sink this instruction to.
MachineBasicBlock *
MachineSinking::FindSuccToSinkTo(MachineInstr &MI, MachineBasicBlock *MBB,
                                 bool &BreakPHIEdge,
                                 AllSuccsCache &AllSuccessors) {
  assert (MBB && "Invalid MachineBasicBlock!");

  // Loop over all the operands of the specified instruction.  If there is
  // anything we can't handle, bail out.

  // SuccToSinkTo - This is the successor to sink this instruction to, once we
  // decide.
  MachineBasicBlock *SuccToSinkTo = nullptr;
  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI.getOperand(i);
    if (!MO.isReg()) continue;  // Ignore non-register operands.

    unsigned Reg = MO.getReg();
    if (Reg == 0) continue;

    if (TargetRegisterInfo::isPhysicalRegister(Reg)) {
      if (MO.isUse()) {
        // If the physreg has no defs anywhere, it's just an ambient register
        // and we can freely move its uses. Alternatively, if it's allocatable,
        // it could get allocated to something with a def during allocation.
        if (!MRI->isConstantPhysReg(Reg))
          return nullptr;
      } else if (!MO.isDead()) {
        // A def that isn't dead. We can't move it.
        return nullptr;
      }
    } else {
      // Virtual register uses are always safe to sink.
      if (MO.isUse()) continue;

      // If it's not safe to move defs of the register class, then abort.
      if (!TII->isSafeToMoveRegClassDefs(MRI->getRegClass(Reg)))
        return nullptr;

      // Virtual register defs can only be sunk if all their uses are in blocks
      // dominated by one of the successors.
      if (SuccToSinkTo) {
        // If a previous operand picked a block to sink to, then this operand
        // must be sinkable to the same block.
        bool LocalUse = false;
        if (!AllUsesDominatedByBlock(Reg, SuccToSinkTo, MBB,
                                     BreakPHIEdge, LocalUse))
          return nullptr;

        continue;
      }

      // Otherwise, we should look at all the successors and decide which one
      // we should sink to. If we have reliable block frequency information
      // (frequency != 0) available, give successors with smaller frequencies
      // higher priority, otherwise prioritize smaller loop depths.
      for (MachineBasicBlock *SuccBlock :
           GetAllSortedSuccessors(MI, MBB, AllSuccessors)) {
        bool LocalUse = false;
        if (AllUsesDominatedByBlock(Reg, SuccBlock, MBB,
                                    BreakPHIEdge, LocalUse)) {
          SuccToSinkTo = SuccBlock;
          break;
        }
        if (LocalUse)
          // Def is used locally, it's never safe to move this def.
          return nullptr;
      }

      // If we couldn't find a block to sink to, ignore this instruction.
      if (!SuccToSinkTo)
        return nullptr;
      if (!isProfitableToSinkTo(Reg, MI, MBB, SuccToSinkTo, AllSuccessors))
        return nullptr;
    }
  }

  // It is not possible to sink an instruction into its own block.  This can
  // happen with loops.
  if (MBB == SuccToSinkTo)
    return nullptr;

  // It's not safe to sink instructions to EH landing pad. Control flow into
  // landing pad is implicitly defined.
  if (SuccToSinkTo && SuccToSinkTo->isEHPad())
    return nullptr;

  return SuccToSinkTo;
}

/// Return true if MI is likely to be usable as a memory operation by the
/// implicit null check optimization.
///
/// This is a "best effort" heuristic, and should not be relied upon for
/// correctness.  This returning true does not guarantee that the implicit null
/// check optimization is legal over MI, and this returning false does not
/// guarantee MI cannot possibly be used to do a null check.
static bool SinkingPreventsImplicitNullCheck(MachineInstr &MI,
                                             const TargetInstrInfo *TII,
                                             const TargetRegisterInfo *TRI) {
  using MachineBranchPredicate = TargetInstrInfo::MachineBranchPredicate;

  auto *MBB = MI.getParent();
  if (MBB->pred_size() != 1)
    return false;

  auto *PredMBB = *MBB->pred_begin();
  auto *PredBB = PredMBB->getBasicBlock();

  // Frontends that don't use implicit null checks have no reason to emit
  // branches with make.implicit metadata, and this function should always
  // return false for them.
  if (!PredBB ||
      !PredBB->getTerminator()->getMetadata(LLVMContext::MD_make_implicit))
    return false;

  MachineOperand *BaseOp;
  int64_t Offset;
  if (!TII->getMemOperandWithOffset(MI, BaseOp, Offset, TRI))
    return false;

  if (!BaseOp->isReg())
    return false;

  if (!(MI.mayLoad() && !MI.isPredicable()))
    return false;

  MachineBranchPredicate MBP;
  if (TII->analyzeBranchPredicate(*PredMBB, MBP, false))
    return false;

  return MBP.LHS.isReg() && MBP.RHS.isImm() && MBP.RHS.getImm() == 0 &&
         (MBP.Predicate == MachineBranchPredicate::PRED_NE ||
          MBP.Predicate == MachineBranchPredicate::PRED_EQ) &&
         MBP.LHS.getReg() == BaseOp->getReg();
}

/// Sink an instruction and its associated debug instructions. If the debug
/// instructions to be sunk are already known, they can be provided in DbgVals.
static void performSink(MachineInstr &MI, MachineBasicBlock &SuccToSinkTo,
                        MachineBasicBlock::iterator InsertPos,
                        SmallVectorImpl<MachineInstr *> *DbgVals = nullptr) {
  // If debug values are provided use those, otherwise call collectDebugValues.
  SmallVector<MachineInstr *, 2> DbgValuesToSink;
  if (DbgVals)
    DbgValuesToSink.insert(DbgValuesToSink.begin(),
                           DbgVals->begin(), DbgVals->end());
  else
    MI.collectDebugValues(DbgValuesToSink);

  // If we cannot find a location to use (merge with), then we erase the debug
  // location to prevent debug-info driven tools from potentially reporting
  // wrong location information.
  if (!SuccToSinkTo.empty() && InsertPos != SuccToSinkTo.end())
    MI.setDebugLoc(DILocation::getMergedLocation(MI.getDebugLoc(),
                                                 InsertPos->getDebugLoc()));
  else
    MI.setDebugLoc(DebugLoc());

  // Move the instruction.
  MachineBasicBlock *ParentBlock = MI.getParent();
  SuccToSinkTo.splice(InsertPos, ParentBlock, MI,
                      ++MachineBasicBlock::iterator(MI));

  // Move previously adjacent debug value instructions to the insert position.
  for (SmallVectorImpl<MachineInstr *>::iterator DBI = DbgValuesToSink.begin(),
                                                 DBE = DbgValuesToSink.end();
       DBI != DBE; ++DBI) {
    MachineInstr *DbgMI = *DBI;
    SuccToSinkTo.splice(InsertPos, ParentBlock, DbgMI,
                        ++MachineBasicBlock::iterator(DbgMI));
  }
}

/// SinkInstruction - Determine whether it is safe to sink the specified machine
/// instruction out of its current block into a successor.
bool MachineSinking::SinkInstruction(MachineInstr &MI, bool &SawStore,
                                     AllSuccsCache &AllSuccessors) {
  // Don't sink instructions that the target prefers not to sink.
  if (!TII->shouldSink(MI))
    return false;

  // Check if it's safe to move the instruction.
  if (!MI.isSafeToMove(AA, SawStore))
    return false;

  // Convergent operations may not be made control-dependent on additional
  // values.
  if (MI.isConvergent())
    return false;

  // Don't break implicit null checks.  This is a performance heuristic, and not
  // required for correctness.
  if (SinkingPreventsImplicitNullCheck(MI, TII, TRI))
    return false;

  // FIXME: This should include support for sinking instructions within the
  // block they are currently in to shorten the live ranges.  We often get
  // instructions sunk into the top of a large block, but it would be better to
  // also sink them down before their first use in the block.  This xform has to
  // be careful not to *increase* register pressure though, e.g. sinking
  // "x = y + z" down if it kills y and z would increase the live ranges of y
  // and z and only shrink the live range of x.

  bool BreakPHIEdge = false;
  MachineBasicBlock *ParentBlock = MI.getParent();
  MachineBasicBlock *SuccToSinkTo =
      FindSuccToSinkTo(MI, ParentBlock, BreakPHIEdge, AllSuccessors);

  // If there are no outputs, it must have side-effects.
  if (!SuccToSinkTo)
    return false;

  // If the instruction to move defines a dead physical register which is live
  // when leaving the basic block, don't move it because it could turn into a
  // "zombie" define of that preg. E.g., EFLAGS. (<rdar://problem/8030636>)
  for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
    const MachineOperand &MO = MI.getOperand(I);
    if (!MO.isReg()) continue;
    unsigned Reg = MO.getReg();
    if (Reg == 0 || !TargetRegisterInfo::isPhysicalRegister(Reg)) continue;
    if (SuccToSinkTo->isLiveIn(Reg))
      return false;
  }

  LLVM_DEBUG(dbgs() << "Sink instr " << MI << "\tinto block " << *SuccToSinkTo);

  // If the block has multiple predecessors, this is a critical edge.
  // Decide if we can sink along it or need to break the edge.
  if (SuccToSinkTo->pred_size() > 1) {
    // We cannot sink a load across a critical edge - there may be stores in
    // other code paths.
    bool TryBreak = false;
    bool store = true;
    if (!MI.isSafeToMove(AA, store)) {
      LLVM_DEBUG(dbgs() << " *** NOTE: Won't sink load along critical edge.\n");
      TryBreak = true;
    }

    // We don't want to sink across a critical edge if we don't dominate the
    // successor. We could be introducing calculations to new code paths.
    if (!TryBreak && !DT->dominates(ParentBlock, SuccToSinkTo)) {
      LLVM_DEBUG(dbgs() << " *** NOTE: Critical edge found\n");
      TryBreak = true;
    }

    // Don't sink instructions into a loop.
    if (!TryBreak && LI->isLoopHeader(SuccToSinkTo)) {
      LLVM_DEBUG(dbgs() << " *** NOTE: Loop header found\n");
      TryBreak = true;
    }

    // Otherwise we are OK with sinking along a critical edge.
    if (!TryBreak)
      LLVM_DEBUG(dbgs() << "Sinking along critical edge.\n");
    else {
      // Mark this edge as to be split.
      // If the edge can actually be split, the next iteration of the main loop
      // will sink MI in the newly created block.
      bool Status =
        PostponeSplitCriticalEdge(MI, ParentBlock, SuccToSinkTo, BreakPHIEdge);
      if (!Status)
        LLVM_DEBUG(dbgs() << " *** PUNTING: Not legal or profitable to "
                             "break critical edge\n");
      // The instruction will not be sunk this time.
      return false;
    }
  }

  if (BreakPHIEdge) {
    // BreakPHIEdge is true if all the uses are in the successor MBB being
    // sunken into and they are all PHI nodes. In this case, machine-sink must
    // break the critical edge first.
    bool Status = PostponeSplitCriticalEdge(MI, ParentBlock,
                                            SuccToSinkTo, BreakPHIEdge);
    if (!Status)
      LLVM_DEBUG(dbgs() << " *** PUNTING: Not legal or profitable to "
                           "break critical edge\n");
    // The instruction will not be sunk this time.
    return false;
  }

  // Determine where to insert into. Skip phi nodes.
  MachineBasicBlock::iterator InsertPos = SuccToSinkTo->begin();
  while (InsertPos != SuccToSinkTo->end() && InsertPos->isPHI())
    ++InsertPos;

  performSink(MI, *SuccToSinkTo, InsertPos);

  // Conservatively, clear any kill flags, since it's possible that they are no
  // longer correct.
  // Note that we have to clear the kill flags for any register this instruction
  // uses as we may sink over another instruction which currently kills the
  // used registers.
  for (MachineOperand &MO : MI.operands()) {
    if (MO.isReg() && MO.isUse())
      RegsToClearKillFlags.set(MO.getReg()); // Remember to clear kill flags.
  }

  return true;
}

//===----------------------------------------------------------------------===//
// This pass is not intended to be a replacement or a complete alternative
// for the pre-ra machine sink pass. It is only designed to sink COPY
// instructions which should be handled after RA.
//
// This pass sinks COPY instructions into a successor block, if the COPY is not
// used in the current block and the COPY is live-in to a single successor
// (i.e., doesn't require the COPY to be duplicated).  This avoids executing the
// copy on paths where their results aren't needed.  This also exposes
// additional opportunites for dead copy elimination and shrink wrapping.
//
// These copies were either not handled by or are inserted after the MachineSink
// pass. As an example of the former case, the MachineSink pass cannot sink
// COPY instructions with allocatable source registers; for AArch64 these type
// of copy instructions are frequently used to move function parameters (PhyReg)
// into virtual registers in the entry block.
//
// For the machine IR below, this pass will sink %w19 in the entry into its
// successor (%bb.1) because %w19 is only live-in in %bb.1.
// %bb.0:
//   %wzr = SUBSWri %w1, 1
//   %w19 = COPY %w0
//   Bcc 11, %bb.2
// %bb.1:
//   Live Ins: %w19
//   BL @fun
//   %w0 = ADDWrr %w0, %w19
//   RET %w0
// %bb.2:
//   %w0 = COPY %wzr
//   RET %w0
// As we sink %w19 (CSR in AArch64) into %bb.1, the shrink-wrapping pass will be
// able to see %bb.0 as a candidate.
//===----------------------------------------------------------------------===//
namespace {

class PostRAMachineSinking : public MachineFunctionPass {
public:
  bool runOnMachineFunction(MachineFunction &MF) override;

  static char ID;
  PostRAMachineSinking() : MachineFunctionPass(ID) {}
  StringRef getPassName() const override { return "PostRA Machine Sink"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

private:
  /// Track which register units have been modified and used.
  LiveRegUnits ModifiedRegUnits, UsedRegUnits;

  /// Track DBG_VALUEs of (unmodified) register units.
  DenseMap<unsigned, TinyPtrVector<MachineInstr*>> SeenDbgInstrs;

  /// Sink Copy instructions unused in the same block close to their uses in
  /// successors.
  bool tryToSinkCopy(MachineBasicBlock &BB, MachineFunction &MF,
                     const TargetRegisterInfo *TRI, const TargetInstrInfo *TII);
};
} // namespace

char PostRAMachineSinking::ID = 0;
char &llvm::PostRAMachineSinkingID = PostRAMachineSinking::ID;

INITIALIZE_PASS(PostRAMachineSinking, "postra-machine-sink",
                "PostRA Machine Sink", false, false)

static bool aliasWithRegsInLiveIn(MachineBasicBlock &MBB, unsigned Reg,
                                  const TargetRegisterInfo *TRI) {
  LiveRegUnits LiveInRegUnits(*TRI);
  LiveInRegUnits.addLiveIns(MBB);
  return !LiveInRegUnits.available(Reg);
}

static MachineBasicBlock *
getSingleLiveInSuccBB(MachineBasicBlock &CurBB,
                      const SmallPtrSetImpl<MachineBasicBlock *> &SinkableBBs,
                      unsigned Reg, const TargetRegisterInfo *TRI) {
  // Try to find a single sinkable successor in which Reg is live-in.
  MachineBasicBlock *BB = nullptr;
  for (auto *SI : SinkableBBs) {
    if (aliasWithRegsInLiveIn(*SI, Reg, TRI)) {
      // If BB is set here, Reg is live-in to at least two sinkable successors,
      // so quit.
      if (BB)
        return nullptr;
      BB = SI;
    }
  }
  // Reg is not live-in to any sinkable successors.
  if (!BB)
    return nullptr;

  // Check if any register aliased with Reg is live-in in other successors.
  for (auto *SI : CurBB.successors()) {
    if (!SinkableBBs.count(SI) && aliasWithRegsInLiveIn(*SI, Reg, TRI))
      return nullptr;
  }
  return BB;
}

static MachineBasicBlock *
getSingleLiveInSuccBB(MachineBasicBlock &CurBB,
                      const SmallPtrSetImpl<MachineBasicBlock *> &SinkableBBs,
                      ArrayRef<unsigned> DefedRegsInCopy,
                      const TargetRegisterInfo *TRI) {
  MachineBasicBlock *SingleBB = nullptr;
  for (auto DefReg : DefedRegsInCopy) {
    MachineBasicBlock *BB =
        getSingleLiveInSuccBB(CurBB, SinkableBBs, DefReg, TRI);
    if (!BB || (SingleBB && SingleBB != BB))
      return nullptr;
    SingleBB = BB;
  }
  return SingleBB;
}

static void clearKillFlags(MachineInstr *MI, MachineBasicBlock &CurBB,
                           SmallVectorImpl<unsigned> &UsedOpsInCopy,
                           LiveRegUnits &UsedRegUnits,
                           const TargetRegisterInfo *TRI) {
  for (auto U : UsedOpsInCopy) {
    MachineOperand &MO = MI->getOperand(U);
    unsigned SrcReg = MO.getReg();
    if (!UsedRegUnits.available(SrcReg)) {
      MachineBasicBlock::iterator NI = std::next(MI->getIterator());
      for (MachineInstr &UI : make_range(NI, CurBB.end())) {
        if (UI.killsRegister(SrcReg, TRI)) {
          UI.clearRegisterKills(SrcReg, TRI);
          MO.setIsKill(true);
          break;
        }
      }
    }
  }
}

static void updateLiveIn(MachineInstr *MI, MachineBasicBlock *SuccBB,
                         SmallVectorImpl<unsigned> &UsedOpsInCopy,
                         SmallVectorImpl<unsigned> &DefedRegsInCopy) {
  MachineFunction &MF = *SuccBB->getParent();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  for (unsigned DefReg : DefedRegsInCopy)
    for (MCSubRegIterator S(DefReg, TRI, true); S.isValid(); ++S)
      SuccBB->removeLiveIn(*S);
  for (auto U : UsedOpsInCopy) {
    unsigned Reg = MI->getOperand(U).getReg();
    if (!SuccBB->isLiveIn(Reg))
      SuccBB->addLiveIn(Reg);
  }
}

static bool hasRegisterDependency(MachineInstr *MI,
                                  SmallVectorImpl<unsigned> &UsedOpsInCopy,
                                  SmallVectorImpl<unsigned> &DefedRegsInCopy,
                                  LiveRegUnits &ModifiedRegUnits,
                                  LiveRegUnits &UsedRegUnits) {
  bool HasRegDependency = false;
  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    MachineOperand &MO = MI->getOperand(i);
    if (!MO.isReg())
      continue;
    unsigned Reg = MO.getReg();
    if (!Reg)
      continue;
    if (MO.isDef()) {
      if (!ModifiedRegUnits.available(Reg) || !UsedRegUnits.available(Reg)) {
        HasRegDependency = true;
        break;
      }
      DefedRegsInCopy.push_back(Reg);

      // FIXME: instead of isUse(), readsReg() would be a better fix here,
      // For example, we can ignore modifications in reg with undef. However,
      // it's not perfectly clear if skipping the internal read is safe in all
      // other targets.
    } else if (MO.isUse()) {
      if (!ModifiedRegUnits.available(Reg)) {
        HasRegDependency = true;
        break;
      }
      UsedOpsInCopy.push_back(i);
    }
  }
  return HasRegDependency;
}

bool PostRAMachineSinking::tryToSinkCopy(MachineBasicBlock &CurBB,
                                         MachineFunction &MF,
                                         const TargetRegisterInfo *TRI,
                                         const TargetInstrInfo *TII) {
  SmallPtrSet<MachineBasicBlock *, 2> SinkableBBs;
  // FIXME: For now, we sink only to a successor which has a single predecessor
  // so that we can directly sink COPY instructions to the successor without
  // adding any new block or branch instruction.
  for (MachineBasicBlock *SI : CurBB.successors())
    if (!SI->livein_empty() && SI->pred_size() == 1)
      SinkableBBs.insert(SI);

  if (SinkableBBs.empty())
    return false;

  bool Changed = false;

  // Track which registers have been modified and used between the end of the
  // block and the current instruction.
  ModifiedRegUnits.clear();
  UsedRegUnits.clear();
  SeenDbgInstrs.clear();

  for (auto I = CurBB.rbegin(), E = CurBB.rend(); I != E;) {
    MachineInstr *MI = &*I;
    ++I;

    // Track the operand index for use in Copy.
    SmallVector<unsigned, 2> UsedOpsInCopy;
    // Track the register number defed in Copy.
    SmallVector<unsigned, 2> DefedRegsInCopy;

    // We must sink this DBG_VALUE if its operand is sunk. To avoid searching
    // for DBG_VALUEs later, record them when they're encountered.
    if (MI->isDebugValue()) {
      auto &MO = MI->getOperand(0);
      if (MO.isReg() && TRI->isPhysicalRegister(MO.getReg())) {
        // Bail if we can already tell the sink would be rejected, rather
        // than needlessly accumulating lots of DBG_VALUEs.
        if (hasRegisterDependency(MI, UsedOpsInCopy, DefedRegsInCopy,
                                  ModifiedRegUnits, UsedRegUnits))
          continue;

        // Record debug use of this register.
        SeenDbgInstrs[MO.getReg()].push_back(MI);
      }
      continue;
    }

    if (MI->isDebugInstr())
      continue;

    // Do not move any instruction across function call.
    if (MI->isCall())
      return false;

    if (!MI->isCopy() || !MI->getOperand(0).isRenamable()) {
      LiveRegUnits::accumulateUsedDefed(*MI, ModifiedRegUnits, UsedRegUnits,
                                        TRI);
      continue;
    }

    // Don't sink the COPY if it would violate a register dependency.
    if (hasRegisterDependency(MI, UsedOpsInCopy, DefedRegsInCopy,
                              ModifiedRegUnits, UsedRegUnits)) {
      LiveRegUnits::accumulateUsedDefed(*MI, ModifiedRegUnits, UsedRegUnits,
                                        TRI);
      continue;
    }
    assert((!UsedOpsInCopy.empty() && !DefedRegsInCopy.empty()) &&
           "Unexpect SrcReg or DefReg");
    MachineBasicBlock *SuccBB =
        getSingleLiveInSuccBB(CurBB, SinkableBBs, DefedRegsInCopy, TRI);
    // Don't sink if we cannot find a single sinkable successor in which Reg
    // is live-in.
    if (!SuccBB) {
      LiveRegUnits::accumulateUsedDefed(*MI, ModifiedRegUnits, UsedRegUnits,
                                        TRI);
      continue;
    }
    assert((SuccBB->pred_size() == 1 && *SuccBB->pred_begin() == &CurBB) &&
           "Unexpected predecessor");

    // Collect DBG_VALUEs that must sink with this copy.
    SmallVector<MachineInstr *, 4> DbgValsToSink;
    for (auto &MO : MI->operands()) {
      if (!MO.isReg() || !MO.isDef())
        continue;
      unsigned reg = MO.getReg();
      for (auto *MI : SeenDbgInstrs.lookup(reg))
        DbgValsToSink.push_back(MI);
    }

    // Clear the kill flag if SrcReg is killed between MI and the end of the
    // block.
    clearKillFlags(MI, CurBB, UsedOpsInCopy, UsedRegUnits, TRI);
    MachineBasicBlock::iterator InsertPos = SuccBB->getFirstNonPHI();
    performSink(*MI, *SuccBB, InsertPos, &DbgValsToSink);
    updateLiveIn(MI, SuccBB, UsedOpsInCopy, DefedRegsInCopy);

    Changed = true;
    ++NumPostRACopySink;
  }
  return Changed;
}

bool PostRAMachineSinking::runOnMachineFunction(MachineFunction &MF) {
  bool Changed = false;
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();

  ModifiedRegUnits.init(*TRI);
  UsedRegUnits.init(*TRI);
  for (auto &BB : MF)
    Changed |= tryToSinkCopy(BB, MF, TRI, TII);

  return Changed;
}
