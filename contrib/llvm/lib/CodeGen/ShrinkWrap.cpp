//===- ShrinkWrap.cpp - Compute safe point for prolog/epilog insertion ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass looks for safe point where the prologue and epilogue can be
// inserted.
// The safe point for the prologue (resp. epilogue) is called Save
// (resp. Restore).
// A point is safe for prologue (resp. epilogue) if and only if
// it 1) dominates (resp. post-dominates) all the frame related operations and
// between 2) two executions of the Save (resp. Restore) point there is an
// execution of the Restore (resp. Save) point.
//
// For instance, the following points are safe:
// for (int i = 0; i < 10; ++i) {
//   Save
//   ...
//   Restore
// }
// Indeed, the execution looks like Save -> Restore -> Save -> Restore ...
// And the following points are not:
// for (int i = 0; i < 10; ++i) {
//   Save
//   ...
// }
// for (int i = 0; i < 10; ++i) {
//   ...
//   Restore
// }
// Indeed, the execution looks like Save -> Save -> ... -> Restore -> Restore.
//
// This pass also ensures that the safe points are 3) cheaper than the regular
// entry and exits blocks.
//
// Property #1 is ensured via the use of MachineDominatorTree and
// MachinePostDominatorTree.
// Property #2 is ensured via property #1 and MachineLoopInfo, i.e., both
// points must be in the same loop.
// Property #3 is ensured via the MachineBlockFrequencyInfo.
//
// If this pass found points matching all these properties, then
// MachineFrameInfo is updated with this information.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <cassert>
#include <cstdint>
#include <memory>

using namespace llvm;

#define DEBUG_TYPE "shrink-wrap"

STATISTIC(NumFunc, "Number of functions");
STATISTIC(NumCandidates, "Number of shrink-wrapping candidates");
STATISTIC(NumCandidatesDropped,
          "Number of shrink-wrapping candidates dropped because of frequency");

static cl::opt<cl::boolOrDefault>
EnableShrinkWrapOpt("enable-shrink-wrap", cl::Hidden,
                    cl::desc("enable the shrink-wrapping pass"));

namespace {

/// Class to determine where the safe point to insert the
/// prologue and epilogue are.
/// Unlike the paper from Fred C. Chow, PLDI'88, that introduces the
/// shrink-wrapping term for prologue/epilogue placement, this pass
/// does not rely on expensive data-flow analysis. Instead we use the
/// dominance properties and loop information to decide which point
/// are safe for such insertion.
class ShrinkWrap : public MachineFunctionPass {
  /// Hold callee-saved information.
  RegisterClassInfo RCI;
  MachineDominatorTree *MDT;
  MachinePostDominatorTree *MPDT;

  /// Current safe point found for the prologue.
  /// The prologue will be inserted before the first instruction
  /// in this basic block.
  MachineBasicBlock *Save;

  /// Current safe point found for the epilogue.
  /// The epilogue will be inserted before the first terminator instruction
  /// in this basic block.
  MachineBasicBlock *Restore;

  /// Hold the information of the basic block frequency.
  /// Use to check the profitability of the new points.
  MachineBlockFrequencyInfo *MBFI;

  /// Hold the loop information. Used to determine if Save and Restore
  /// are in the same loop.
  MachineLoopInfo *MLI;

  // Emit remarks.
  MachineOptimizationRemarkEmitter *ORE = nullptr;

  /// Frequency of the Entry block.
  uint64_t EntryFreq;

  /// Current opcode for frame setup.
  unsigned FrameSetupOpcode;

  /// Current opcode for frame destroy.
  unsigned FrameDestroyOpcode;

  /// Stack pointer register, used by llvm.{savestack,restorestack}
  unsigned SP;

  /// Entry block.
  const MachineBasicBlock *Entry;

  using SetOfRegs = SmallSetVector<unsigned, 16>;

  /// Registers that need to be saved for the current function.
  mutable SetOfRegs CurrentCSRs;

  /// Current MachineFunction.
  MachineFunction *MachineFunc;

  /// Check if \p MI uses or defines a callee-saved register or
  /// a frame index. If this is the case, this means \p MI must happen
  /// after Save and before Restore.
  bool useOrDefCSROrFI(const MachineInstr &MI, RegScavenger *RS) const;

  const SetOfRegs &getCurrentCSRs(RegScavenger *RS) const {
    if (CurrentCSRs.empty()) {
      BitVector SavedRegs;
      const TargetFrameLowering *TFI =
          MachineFunc->getSubtarget().getFrameLowering();

      TFI->determineCalleeSaves(*MachineFunc, SavedRegs, RS);

      for (int Reg = SavedRegs.find_first(); Reg != -1;
           Reg = SavedRegs.find_next(Reg))
        CurrentCSRs.insert((unsigned)Reg);
    }
    return CurrentCSRs;
  }

  /// Update the Save and Restore points such that \p MBB is in
  /// the region that is dominated by Save and post-dominated by Restore
  /// and Save and Restore still match the safe point definition.
  /// Such point may not exist and Save and/or Restore may be null after
  /// this call.
  void updateSaveRestorePoints(MachineBasicBlock &MBB, RegScavenger *RS);

  /// Initialize the pass for \p MF.
  void init(MachineFunction &MF) {
    RCI.runOnMachineFunction(MF);
    MDT = &getAnalysis<MachineDominatorTree>();
    MPDT = &getAnalysis<MachinePostDominatorTree>();
    Save = nullptr;
    Restore = nullptr;
    MBFI = &getAnalysis<MachineBlockFrequencyInfo>();
    MLI = &getAnalysis<MachineLoopInfo>();
    ORE = &getAnalysis<MachineOptimizationRemarkEmitterPass>().getORE();
    EntryFreq = MBFI->getEntryFreq();
    const TargetSubtargetInfo &Subtarget = MF.getSubtarget();
    const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
    FrameSetupOpcode = TII.getCallFrameSetupOpcode();
    FrameDestroyOpcode = TII.getCallFrameDestroyOpcode();
    SP = Subtarget.getTargetLowering()->getStackPointerRegisterToSaveRestore();
    Entry = &MF.front();
    CurrentCSRs.clear();
    MachineFunc = &MF;

    ++NumFunc;
  }

  /// Check whether or not Save and Restore points are still interesting for
  /// shrink-wrapping.
  bool ArePointsInteresting() const { return Save != Entry && Save && Restore; }

  /// Check if shrink wrapping is enabled for this target and function.
  static bool isShrinkWrapEnabled(const MachineFunction &MF);

public:
  static char ID;

  ShrinkWrap() : MachineFunctionPass(ID) {
    initializeShrinkWrapPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<MachineBlockFrequencyInfo>();
    AU.addRequired<MachineDominatorTree>();
    AU.addRequired<MachinePostDominatorTree>();
    AU.addRequired<MachineLoopInfo>();
    AU.addRequired<MachineOptimizationRemarkEmitterPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
      MachineFunctionProperties::Property::NoVRegs);
  }

  StringRef getPassName() const override { return "Shrink Wrapping analysis"; }

  /// Perform the shrink-wrapping analysis and update
  /// the MachineFrameInfo attached to \p MF with the results.
  bool runOnMachineFunction(MachineFunction &MF) override;
};

} // end anonymous namespace

char ShrinkWrap::ID = 0;

char &llvm::ShrinkWrapID = ShrinkWrap::ID;

INITIALIZE_PASS_BEGIN(ShrinkWrap, DEBUG_TYPE, "Shrink Wrap Pass", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineBlockFrequencyInfo)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_PASS_DEPENDENCY(MachineOptimizationRemarkEmitterPass)
INITIALIZE_PASS_END(ShrinkWrap, DEBUG_TYPE, "Shrink Wrap Pass", false, false)

bool ShrinkWrap::useOrDefCSROrFI(const MachineInstr &MI,
                                 RegScavenger *RS) const {
  if (MI.getOpcode() == FrameSetupOpcode ||
      MI.getOpcode() == FrameDestroyOpcode) {
    LLVM_DEBUG(dbgs() << "Frame instruction: " << MI << '\n');
    return true;
  }
  for (const MachineOperand &MO : MI.operands()) {
    bool UseOrDefCSR = false;
    if (MO.isReg()) {
      // Ignore instructions like DBG_VALUE which don't read/def the register.
      if (!MO.isDef() && !MO.readsReg())
        continue;
      unsigned PhysReg = MO.getReg();
      if (!PhysReg)
        continue;
      assert(TargetRegisterInfo::isPhysicalRegister(PhysReg) &&
             "Unallocated register?!");
      // The stack pointer is not normally described as a callee-saved register
      // in calling convention definitions, so we need to watch for it
      // separately. An SP mentioned by a call instruction, we can ignore,
      // though, as it's harmless and we do not want to effectively disable tail
      // calls by forcing the restore point to post-dominate them.
      UseOrDefCSR = (!MI.isCall() && PhysReg == SP) ||
                    RCI.getLastCalleeSavedAlias(PhysReg);
    } else if (MO.isRegMask()) {
      // Check if this regmask clobbers any of the CSRs.
      for (unsigned Reg : getCurrentCSRs(RS)) {
        if (MO.clobbersPhysReg(Reg)) {
          UseOrDefCSR = true;
          break;
        }
      }
    }
    // Skip FrameIndex operands in DBG_VALUE instructions.
    if (UseOrDefCSR || (MO.isFI() && !MI.isDebugValue())) {
      LLVM_DEBUG(dbgs() << "Use or define CSR(" << UseOrDefCSR << ") or FI("
                        << MO.isFI() << "): " << MI << '\n');
      return true;
    }
  }
  return false;
}

/// Helper function to find the immediate (post) dominator.
template <typename ListOfBBs, typename DominanceAnalysis>
static MachineBasicBlock *FindIDom(MachineBasicBlock &Block, ListOfBBs BBs,
                                   DominanceAnalysis &Dom) {
  MachineBasicBlock *IDom = &Block;
  for (MachineBasicBlock *BB : BBs) {
    IDom = Dom.findNearestCommonDominator(IDom, BB);
    if (!IDom)
      break;
  }
  if (IDom == &Block)
    return nullptr;
  return IDom;
}

void ShrinkWrap::updateSaveRestorePoints(MachineBasicBlock &MBB,
                                         RegScavenger *RS) {
  // Get rid of the easy cases first.
  if (!Save)
    Save = &MBB;
  else
    Save = MDT->findNearestCommonDominator(Save, &MBB);

  if (!Save) {
    LLVM_DEBUG(dbgs() << "Found a block that is not reachable from Entry\n");
    return;
  }

  if (!Restore)
    Restore = &MBB;
  else if (MPDT->getNode(&MBB)) // If the block is not in the post dom tree, it
                                // means the block never returns. If that's the
                                // case, we don't want to call
                                // `findNearestCommonDominator`, which will
                                // return `Restore`.
    Restore = MPDT->findNearestCommonDominator(Restore, &MBB);
  else
    Restore = nullptr; // Abort, we can't find a restore point in this case.

  // Make sure we would be able to insert the restore code before the
  // terminator.
  if (Restore == &MBB) {
    for (const MachineInstr &Terminator : MBB.terminators()) {
      if (!useOrDefCSROrFI(Terminator, RS))
        continue;
      // One of the terminator needs to happen before the restore point.
      if (MBB.succ_empty()) {
        Restore = nullptr; // Abort, we can't find a restore point in this case.
        break;
      }
      // Look for a restore point that post-dominates all the successors.
      // The immediate post-dominator is what we are looking for.
      Restore = FindIDom<>(*Restore, Restore->successors(), *MPDT);
      break;
    }
  }

  if (!Restore) {
    LLVM_DEBUG(
        dbgs() << "Restore point needs to be spanned on several blocks\n");
    return;
  }

  // Make sure Save and Restore are suitable for shrink-wrapping:
  // 1. all path from Save needs to lead to Restore before exiting.
  // 2. all path to Restore needs to go through Save from Entry.
  // We achieve that by making sure that:
  // A. Save dominates Restore.
  // B. Restore post-dominates Save.
  // C. Save and Restore are in the same loop.
  bool SaveDominatesRestore = false;
  bool RestorePostDominatesSave = false;
  while (Save && Restore &&
         (!(SaveDominatesRestore = MDT->dominates(Save, Restore)) ||
          !(RestorePostDominatesSave = MPDT->dominates(Restore, Save)) ||
          // Post-dominance is not enough in loops to ensure that all uses/defs
          // are after the prologue and before the epilogue at runtime.
          // E.g.,
          // while(1) {
          //  Save
          //  Restore
          //   if (...)
          //     break;
          //  use/def CSRs
          // }
          // All the uses/defs of CSRs are dominated by Save and post-dominated
          // by Restore. However, the CSRs uses are still reachable after
          // Restore and before Save are executed.
          //
          // For now, just push the restore/save points outside of loops.
          // FIXME: Refine the criteria to still find interesting cases
          // for loops.
          MLI->getLoopFor(Save) || MLI->getLoopFor(Restore))) {
    // Fix (A).
    if (!SaveDominatesRestore) {
      Save = MDT->findNearestCommonDominator(Save, Restore);
      continue;
    }
    // Fix (B).
    if (!RestorePostDominatesSave)
      Restore = MPDT->findNearestCommonDominator(Restore, Save);

    // Fix (C).
    if (Save && Restore &&
        (MLI->getLoopFor(Save) || MLI->getLoopFor(Restore))) {
      if (MLI->getLoopDepth(Save) > MLI->getLoopDepth(Restore)) {
        // Push Save outside of this loop if immediate dominator is different
        // from save block. If immediate dominator is not different, bail out.
        Save = FindIDom<>(*Save, Save->predecessors(), *MDT);
        if (!Save)
          break;
      } else {
        // If the loop does not exit, there is no point in looking
        // for a post-dominator outside the loop.
        SmallVector<MachineBasicBlock*, 4> ExitBlocks;
        MLI->getLoopFor(Restore)->getExitingBlocks(ExitBlocks);
        // Push Restore outside of this loop.
        // Look for the immediate post-dominator of the loop exits.
        MachineBasicBlock *IPdom = Restore;
        for (MachineBasicBlock *LoopExitBB: ExitBlocks) {
          IPdom = FindIDom<>(*IPdom, LoopExitBB->successors(), *MPDT);
          if (!IPdom)
            break;
        }
        // If the immediate post-dominator is not in a less nested loop,
        // then we are stuck in a program with an infinite loop.
        // In that case, we will not find a safe point, hence, bail out.
        if (IPdom && MLI->getLoopDepth(IPdom) < MLI->getLoopDepth(Restore))
          Restore = IPdom;
        else {
          Restore = nullptr;
          break;
        }
      }
    }
  }
}

static bool giveUpWithRemarks(MachineOptimizationRemarkEmitter *ORE,
                              StringRef RemarkName, StringRef RemarkMessage,
                              const DiagnosticLocation &Loc,
                              const MachineBasicBlock *MBB) {
  ORE->emit([&]() {
    return MachineOptimizationRemarkMissed(DEBUG_TYPE, RemarkName, Loc, MBB)
           << RemarkMessage;
  });

  LLVM_DEBUG(dbgs() << RemarkMessage << '\n');
  return false;
}

bool ShrinkWrap::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()) || MF.empty() || !isShrinkWrapEnabled(MF))
    return false;

  LLVM_DEBUG(dbgs() << "**** Analysing " << MF.getName() << '\n');

  init(MF);

  ReversePostOrderTraversal<MachineBasicBlock *> RPOT(&*MF.begin());
  if (containsIrreducibleCFG<MachineBasicBlock *>(RPOT, *MLI)) {
    // If MF is irreducible, a block may be in a loop without
    // MachineLoopInfo reporting it. I.e., we may use the
    // post-dominance property in loops, which lead to incorrect
    // results. Moreover, we may miss that the prologue and
    // epilogue are not in the same loop, leading to unbalanced
    // construction/deconstruction of the stack frame.
    return giveUpWithRemarks(ORE, "UnsupportedIrreducibleCFG",
                             "Irreducible CFGs are not supported yet.",
                             MF.getFunction().getSubprogram(), &MF.front());
  }

  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  std::unique_ptr<RegScavenger> RS(
      TRI->requiresRegisterScavenging(MF) ? new RegScavenger() : nullptr);

  for (MachineBasicBlock &MBB : MF) {
    LLVM_DEBUG(dbgs() << "Look into: " << MBB.getNumber() << ' '
                      << MBB.getName() << '\n');

    if (MBB.isEHFuncletEntry())
      return giveUpWithRemarks(ORE, "UnsupportedEHFunclets",
                               "EH Funclets are not supported yet.",
                               MBB.front().getDebugLoc(), &MBB);

    if (MBB.isEHPad()) {
      // Push the prologue and epilogue outside of
      // the region that may throw by making sure
      // that all the landing pads are at least at the
      // boundary of the save and restore points.
      // The problem with exceptions is that the throw
      // is not properly modeled and in particular, a
      // basic block can jump out from the middle.
      updateSaveRestorePoints(MBB, RS.get());
      if (!ArePointsInteresting()) {
        LLVM_DEBUG(dbgs() << "EHPad prevents shrink-wrapping\n");
        return false;
      }
      continue;
    }

    for (const MachineInstr &MI : MBB) {
      if (!useOrDefCSROrFI(MI, RS.get()))
        continue;
      // Save (resp. restore) point must dominate (resp. post dominate)
      // MI. Look for the proper basic block for those.
      updateSaveRestorePoints(MBB, RS.get());
      // If we are at a point where we cannot improve the placement of
      // save/restore instructions, just give up.
      if (!ArePointsInteresting()) {
        LLVM_DEBUG(dbgs() << "No Shrink wrap candidate found\n");
        return false;
      }
      // No need to look for other instructions, this basic block
      // will already be part of the handled region.
      break;
    }
  }
  if (!ArePointsInteresting()) {
    // If the points are not interesting at this point, then they must be null
    // because it means we did not encounter any frame/CSR related code.
    // Otherwise, we would have returned from the previous loop.
    assert(!Save && !Restore && "We miss a shrink-wrap opportunity?!");
    LLVM_DEBUG(dbgs() << "Nothing to shrink-wrap\n");
    return false;
  }

  LLVM_DEBUG(dbgs() << "\n ** Results **\nFrequency of the Entry: " << EntryFreq
                    << '\n');

  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  do {
    LLVM_DEBUG(dbgs() << "Shrink wrap candidates (#, Name, Freq):\nSave: "
                      << Save->getNumber() << ' ' << Save->getName() << ' '
                      << MBFI->getBlockFreq(Save).getFrequency()
                      << "\nRestore: " << Restore->getNumber() << ' '
                      << Restore->getName() << ' '
                      << MBFI->getBlockFreq(Restore).getFrequency() << '\n');

    bool IsSaveCheap, TargetCanUseSaveAsPrologue = false;
    if (((IsSaveCheap = EntryFreq >= MBFI->getBlockFreq(Save).getFrequency()) &&
         EntryFreq >= MBFI->getBlockFreq(Restore).getFrequency()) &&
        ((TargetCanUseSaveAsPrologue = TFI->canUseAsPrologue(*Save)) &&
         TFI->canUseAsEpilogue(*Restore)))
      break;
    LLVM_DEBUG(
        dbgs() << "New points are too expensive or invalid for the target\n");
    MachineBasicBlock *NewBB;
    if (!IsSaveCheap || !TargetCanUseSaveAsPrologue) {
      Save = FindIDom<>(*Save, Save->predecessors(), *MDT);
      if (!Save)
        break;
      NewBB = Save;
    } else {
      // Restore is expensive.
      Restore = FindIDom<>(*Restore, Restore->successors(), *MPDT);
      if (!Restore)
        break;
      NewBB = Restore;
    }
    updateSaveRestorePoints(*NewBB, RS.get());
  } while (Save && Restore);

  if (!ArePointsInteresting()) {
    ++NumCandidatesDropped;
    return false;
  }

  LLVM_DEBUG(dbgs() << "Final shrink wrap candidates:\nSave: "
                    << Save->getNumber() << ' ' << Save->getName()
                    << "\nRestore: " << Restore->getNumber() << ' '
                    << Restore->getName() << '\n');

  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setSavePoint(Save);
  MFI.setRestorePoint(Restore);
  ++NumCandidates;
  return false;
}

bool ShrinkWrap::isShrinkWrapEnabled(const MachineFunction &MF) {
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();

  switch (EnableShrinkWrapOpt) {
  case cl::BOU_UNSET:
    return TFI->enableShrinkWrapping(MF) &&
           // Windows with CFI has some limitations that make it impossible
           // to use shrink-wrapping.
           !MF.getTarget().getMCAsmInfo()->usesWindowsCFI() &&
           // Sanitizers look at the value of the stack at the location
           // of the crash. Since a crash can happen anywhere, the
           // frame must be lowered before anything else happen for the
           // sanitizers to be able to get a correct stack frame.
           !(MF.getFunction().hasFnAttribute(Attribute::SanitizeAddress) ||
             MF.getFunction().hasFnAttribute(Attribute::SanitizeThread) ||
             MF.getFunction().hasFnAttribute(Attribute::SanitizeMemory) ||
             MF.getFunction().hasFnAttribute(Attribute::SanitizeHWAddress));
  // If EnableShrinkWrap is set, it takes precedence on whatever the
  // target sets. The rational is that we assume we want to test
  // something related to shrink-wrapping.
  case cl::BOU_TRUE:
    return true;
  case cl::BOU_FALSE:
    return false;
  }
  llvm_unreachable("Invalid shrink-wrapping state");
}
