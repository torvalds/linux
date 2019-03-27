//===-- SIFixWWMLiveness.cpp - Fix WWM live intervals ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Computations in WWM can overwrite values in inactive channels for
/// variables that the register allocator thinks are dead. This pass adds fake
/// uses of those variables to their def(s) to make sure that they aren't
/// overwritten.
///
/// As an example, consider this snippet:
/// %vgpr0 = V_MOV_B32_e32 0.0
/// if (...) {
///   %vgpr1 = ...
///   %vgpr2 = WWM killed %vgpr1
///   ... = killed %vgpr2
///   %vgpr0 = V_MOV_B32_e32 1.0
/// }
/// ... = %vgpr0
///
/// The live intervals of %vgpr0 don't overlap with those of %vgpr1. Normally,
/// we can safely allocate %vgpr0 and %vgpr1 in the same register, since
/// writing %vgpr1 would only write to channels that would be clobbered by the
/// second write to %vgpr0 anyways. But if %vgpr1 is written with WWM enabled,
/// it would clobber even the inactive channels for which the if-condition is
/// false, for which %vgpr0 is supposed to be 0. This pass adds an implicit use
/// of %vgpr0 to its def to make sure they aren't allocated to the
/// same register.
///
/// In general, we need to figure out what registers might have their inactive
/// channels which are eventually used accidentally clobbered by a WWM
/// instruction. We do that by spotting three separate cases of registers:
///
/// 1. A "then phi": the value resulting from phi elimination of a phi node at
///    the end of an if..endif. If there is WWM code in the "then", then we
///    make the def at the end of the "then" branch a partial def by adding an
///    implicit use of the register.
///
/// 2. A "loop exit register": a value written inside a loop but used outside the
///    loop, where there is WWM code inside the loop (the case in the example
///    above). We add an implicit_def of the register in the loop pre-header,
///    and make the original def a partial def by adding an implicit use of the
///    register.
///
/// 3. A "loop exit phi": the value resulting from phi elimination of a phi node
///    in a loop header. If there is WWM code inside the loop, then we make all
///    defs inside the loop partial defs by adding an implicit use of the
///    register on each one.
///
/// Note that we do not need to consider an if..else..endif phi. We only need to
/// consider non-uniform control flow, and control flow structurization would
/// have transformed a non-uniform if..else..endif into two if..endifs.
///
/// The analysis to detect these cases relies on a property of the MIR
/// arising from this pass running straight after PHIElimination and before any
/// coalescing: that any virtual register with more than one definition must be
/// the new register added to lower a phi node by PHIElimination.
///
/// FIXME: We should detect whether a register in one of the above categories is
/// already live at the WWM code before deciding to add the implicit uses to
/// synthesize its liveness.
///
/// FIXME: I believe this whole scheme may be flawed due to the possibility of
/// the register allocator doing live interval splitting.
///
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUSubtarget.h"
#include "SIInstrInfo.h"
#include "SIRegisterInfo.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SparseBitVector.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"

using namespace llvm;

#define DEBUG_TYPE "si-fix-wwm-liveness"

namespace {

class SIFixWWMLiveness : public MachineFunctionPass {
private:
  MachineDominatorTree *DomTree;
  MachineLoopInfo *LoopInfo;
  LiveIntervals *LIS = nullptr;
  const SIInstrInfo *TII;
  const SIRegisterInfo *TRI;
  MachineRegisterInfo *MRI;

  std::vector<MachineInstr *> WWMs;
  std::vector<MachineOperand *> ThenDefs;
  std::vector<std::pair<MachineOperand *, MachineLoop *>> LoopExitDefs;
  std::vector<std::pair<MachineOperand *, MachineLoop *>> LoopPhiDefs;

public:
  static char ID;

  SIFixWWMLiveness() : MachineFunctionPass(ID) {
    initializeSIFixWWMLivenessPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override { return "SI Fix WWM Liveness"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequiredID(MachineDominatorsID);
    AU.addRequiredID(MachineLoopInfoID);
    // Should preserve the same set that TwoAddressInstructions does.
    AU.addPreserved<SlotIndexes>();
    AU.addPreserved<LiveIntervals>();
    AU.addPreservedID(LiveVariablesID);
    AU.addPreservedID(MachineLoopInfoID);
    AU.addPreservedID(MachineDominatorsID);
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

private:
  void processDef(MachineOperand &DefOpnd);
  bool processThenDef(MachineOperand *DefOpnd);
  bool processLoopExitDef(MachineOperand *DefOpnd, MachineLoop *Loop);
  bool processLoopPhiDef(MachineOperand *DefOpnd, MachineLoop *Loop);
};

} // End anonymous namespace.

INITIALIZE_PASS_BEGIN(SIFixWWMLiveness, DEBUG_TYPE,
                "SI fix WWM liveness", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_PASS_END(SIFixWWMLiveness, DEBUG_TYPE,
                "SI fix WWM liveness", false, false)

char SIFixWWMLiveness::ID = 0;

char &llvm::SIFixWWMLivenessID = SIFixWWMLiveness::ID;

FunctionPass *llvm::createSIFixWWMLivenessPass() {
  return new SIFixWWMLiveness();
}

bool SIFixWWMLiveness::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "SIFixWWMLiveness: function " << MF.getName() << "\n");
  bool Modified = false;

  // This doesn't actually need LiveIntervals, but we can preserve them.
  LIS = getAnalysisIfAvailable<LiveIntervals>();

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();

  TII = ST.getInstrInfo();
  TRI = &TII->getRegisterInfo();
  MRI = &MF.getRegInfo();

  DomTree = &getAnalysis<MachineDominatorTree>();
  LoopInfo = &getAnalysis<MachineLoopInfo>();

  // Scan the function to find the WWM sections and the candidate registers for
  // having liveness modified.
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.getOpcode() == AMDGPU::EXIT_WWM)
        WWMs.push_back(&MI);
      else {
        for (MachineOperand &DefOpnd : MI.defs()) {
          if (DefOpnd.isReg()) {
            unsigned Reg = DefOpnd.getReg();
            if (TRI->isVGPR(*MRI, Reg))
              processDef(DefOpnd);
          }
        }
      }
    }
  }
  if (!WWMs.empty()) {
    // Synthesize liveness over WWM sections as required.
    for (auto ThenDef : ThenDefs)
      Modified |= processThenDef(ThenDef);
    for (auto LoopExitDef : LoopExitDefs)
      Modified |= processLoopExitDef(LoopExitDef.first, LoopExitDef.second);
    for (auto LoopPhiDef : LoopPhiDefs)
      Modified |= processLoopPhiDef(LoopPhiDef.first, LoopPhiDef.second);
  }

  WWMs.clear();
  ThenDefs.clear();
  LoopExitDefs.clear();
  LoopPhiDefs.clear();

  return Modified;
}

// During the function scan, process an operand that defines a VGPR.
// This categorizes the register and puts it in the appropriate list for later
// use when processing a WWM section.
void SIFixWWMLiveness::processDef(MachineOperand &DefOpnd) {
  unsigned Reg = DefOpnd.getReg();
  // Get all the defining instructions. For convenience, make Defs[0] the def
  // we are on now.
  SmallVector<const MachineInstr *, 4> Defs;
  Defs.push_back(DefOpnd.getParent());
  for (auto &MI : MRI->def_instructions(Reg)) {
    if (&MI != DefOpnd.getParent())
      Defs.push_back(&MI);
  }
  // Check whether this def dominates all the others. If not, ignore this def.
  // Either it is going to be processed when the scan encounters its other def
  // that dominates all defs, or there is no def that dominates all others.
  // The latter case is an eliminated phi from an if..else..endif or similar,
  // which must be for uniform control flow so can be ignored.
  // Because this pass runs shortly after PHIElimination, we assume that any
  // multi-def register is a lowered phi, and thus has each def in a separate
  // basic block.
  for (unsigned I = 1; I != Defs.size(); ++I) {
    if (!DomTree->dominates(Defs[0]->getParent(), Defs[I]->getParent()))
      return;
  }
  // Check for the case of an if..endif lowered phi: It has two defs, one
  // dominates the other, and there is a single use in a successor of the
  // dominant def.
  // Later we will spot any WWM code inside
  // the "then" clause and turn the second def into a partial def so its
  // liveness goes through the WWM code in the "then" clause.
  if (Defs.size() == 2) {
    auto DomDefBlock = Defs[0]->getParent();
    if (DomDefBlock->succ_size() == 2 && MRI->hasOneUse(Reg)) {
      auto UseBlock = MRI->use_begin(Reg)->getParent()->getParent();
      for (auto Succ : DomDefBlock->successors()) {
        if (Succ == UseBlock) {
          LLVM_DEBUG(dbgs() << printReg(Reg, TRI) << " is a then phi reg\n");
          ThenDefs.push_back(&DefOpnd);
          return;
        }
      }
    }
  }
  // Check for the case of a non-lowered-phi register (single def) that exits
  // a loop, that is, it has a use that is outside a loop that the def is
  // inside. We find the outermost loop that the def is inside but a use is
  // outside. Later we will spot any WWM code inside that loop and then make
  // the def a partial def so its liveness goes round the loop and through the
  // WWM code.
  if (Defs.size() == 1) {
    auto Loop = LoopInfo->getLoopFor(Defs[0]->getParent());
    if (!Loop)
      return;
    bool IsLoopExit = false;
    for (auto &Use : MRI->use_instructions(Reg)) {
      auto UseBlock = Use.getParent();
      if (Loop->contains(UseBlock))
        continue;
      IsLoopExit = true;
      while (auto Parent = Loop->getParentLoop()) {
        if (Parent->contains(UseBlock))
          break;
        Loop = Parent;
      }
    }
    if (!IsLoopExit)
      return;
    LLVM_DEBUG(dbgs() << printReg(Reg, TRI)
        << " is a loop exit reg with loop header at "
        << "bb." << Loop->getHeader()->getNumber() << "\n");
    LoopExitDefs.push_back(std::pair<MachineOperand *, MachineLoop *>(
            &DefOpnd, Loop));
    return;
  }
  // Check for the case of a lowered single-preheader-loop phi, that is, a
  // multi-def register where the dominating def is in the loop pre-header and
  // all other defs are in backedges. Later we will spot any WWM code inside
  // that loop and then make the backedge defs partial defs so the liveness
  // goes through the WWM code.
  // Note that we are ignoring multi-preheader loops on the basis that the
  // structurizer does not allow that for non-uniform loops.
  // There must be a single use in the loop header.
  if (!MRI->hasOneUse(Reg))
    return;
  auto UseBlock = MRI->use_begin(Reg)->getParent()->getParent();
  auto Loop = LoopInfo->getLoopFor(UseBlock);
  if (!Loop || Loop->getHeader() != UseBlock
      || Loop->contains(Defs[0]->getParent())) {
    LLVM_DEBUG(dbgs() << printReg(Reg, TRI)
        << " is multi-def but single use not in loop header\n");
    return;
  }
  for (unsigned I = 1; I != Defs.size(); ++I) {
    if (!Loop->contains(Defs[I]->getParent()))
      return;
  }
  LLVM_DEBUG(dbgs() << printReg(Reg, TRI)
      << " is a loop phi reg with loop header at "
      << "bb." << Loop->getHeader()->getNumber() << "\n");
  LoopPhiDefs.push_back(
      std::pair<MachineOperand *, MachineLoop *>(&DefOpnd, Loop));
}

// Process a then phi def: It has two defs, one dominates the other, and there
// is a single use in a successor of the dominant def. Here we spot any WWM
// code inside the "then" clause and turn the second def into a partial def so
// its liveness goes through the WWM code in the "then" clause.
bool SIFixWWMLiveness::processThenDef(MachineOperand *DefOpnd) {
  LLVM_DEBUG(dbgs() << "Processing then def: " << *DefOpnd->getParent());
  if (DefOpnd->getParent()->getOpcode() == TargetOpcode::IMPLICIT_DEF) {
    // Ignore if dominating def is undef.
    LLVM_DEBUG(dbgs() << "  ignoring as dominating def is undef\n");
    return false;
  }
  unsigned Reg = DefOpnd->getReg();
  // Get the use block, which is the endif block.
  auto UseBlock = MRI->use_instr_begin(Reg)->getParent();
  // Check whether there is WWM code inside the then branch. The WWM code must
  // be dominated by the if but not dominated by the endif.
  bool ContainsWWM = false;
  for (auto WWM : WWMs) {
    if (DomTree->dominates(DefOpnd->getParent()->getParent(), WWM->getParent())
        && !DomTree->dominates(UseBlock, WWM->getParent())) {
      LLVM_DEBUG(dbgs() << "  contains WWM: " << *WWM);
      ContainsWWM = true;
      break;
    }
  }
  if (!ContainsWWM)
    return false;
  // Get the other def.
  MachineInstr *OtherDef = nullptr;
  for (auto &MI : MRI->def_instructions(Reg)) {
    if (&MI != DefOpnd->getParent())
      OtherDef = &MI;
  }
  // Make it a partial def.
  OtherDef->addOperand(MachineOperand::CreateReg(Reg, false, /*isImp=*/true));
  LLVM_DEBUG(dbgs() << *OtherDef);
  return true;
}

// Process a loop exit def, that is, a register with a single use in a loop
// that has a use outside the loop.  Here we spot any WWM code inside that loop
// and then make the def a partial def so its liveness goes round the loop and
// through the WWM code.
bool SIFixWWMLiveness::processLoopExitDef(MachineOperand *DefOpnd,
      MachineLoop *Loop) {
  LLVM_DEBUG(dbgs() << "Processing loop exit def: " << *DefOpnd->getParent());
  // Check whether there is WWM code inside the loop.
  bool ContainsWWM = false;
  for (auto WWM : WWMs) {
    if (Loop->contains(WWM->getParent())) {
      LLVM_DEBUG(dbgs() << "  contains WWM: " << *WWM);
      ContainsWWM = true;
      break;
    }
  }
  if (!ContainsWWM)
    return false;
  unsigned Reg = DefOpnd->getReg();
  // Add a new implicit_def in loop preheader(s).
  for (auto Pred : Loop->getHeader()->predecessors()) {
    if (!Loop->contains(Pred)) {
      auto ImplicitDef = BuildMI(*Pred, Pred->getFirstTerminator(), DebugLoc(),
          TII->get(TargetOpcode::IMPLICIT_DEF), Reg);
      LLVM_DEBUG(dbgs() << *ImplicitDef);
      (void)ImplicitDef;
    }
  }
  // Make the original def partial.
  DefOpnd->getParent()->addOperand(MachineOperand::CreateReg(
          Reg, false, /*isImp=*/true));
  LLVM_DEBUG(dbgs() << *DefOpnd->getParent());
  return true;
}

// Process a loop phi def, that is, a multi-def register where the dominating
// def is in the loop pre-header and all other defs are in backedges. Here we
// spot any WWM code inside that loop and then make the backedge defs partial
// defs so the liveness goes through the WWM code.
bool SIFixWWMLiveness::processLoopPhiDef(MachineOperand *DefOpnd,
      MachineLoop *Loop) {
  LLVM_DEBUG(dbgs() << "Processing loop phi def: " << *DefOpnd->getParent());
  // Check whether there is WWM code inside the loop.
  bool ContainsWWM = false;
  for (auto WWM : WWMs) {
    if (Loop->contains(WWM->getParent())) {
      LLVM_DEBUG(dbgs() << "  contains WWM: " << *WWM);
      ContainsWWM = true;
      break;
    }
  }
  if (!ContainsWWM)
    return false;
  unsigned Reg = DefOpnd->getReg();
  // Remove kill mark from uses.
  for (auto &Use : MRI->use_operands(Reg))
    Use.setIsKill(false);
  // Make all defs except the dominating one partial defs.
  SmallVector<MachineInstr *, 4> Defs;
  for (auto &Def : MRI->def_instructions(Reg))
    Defs.push_back(&Def);
  for (auto Def : Defs) {
    if (DefOpnd->getParent() == Def)
      continue;
    Def->addOperand(MachineOperand::CreateReg(Reg, false, /*isImp=*/true));
    LLVM_DEBUG(dbgs() << *Def);
  }
  return true;
}

