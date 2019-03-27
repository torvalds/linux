//===- MachineCSE.cpp - Machine Common Subexpression Elimination Pass -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass performs global common subexpression elimination on machine
// instructions using a scoped hash table based value numbering scheme. It
// must be run while the machine function is still in SSA form.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/ScopedHashTable.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/RecyclingAllocator.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <iterator>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "machine-cse"

STATISTIC(NumCoalesces, "Number of copies coalesced");
STATISTIC(NumCSEs,      "Number of common subexpression eliminated");
STATISTIC(NumPhysCSEs,
          "Number of physreg referencing common subexpr eliminated");
STATISTIC(NumCrossBBCSEs,
          "Number of cross-MBB physreg referencing CS eliminated");
STATISTIC(NumCommutes,  "Number of copies coalesced after commuting");

namespace {

  class MachineCSE : public MachineFunctionPass {
    const TargetInstrInfo *TII;
    const TargetRegisterInfo *TRI;
    AliasAnalysis *AA;
    MachineDominatorTree *DT;
    MachineRegisterInfo *MRI;

  public:
    static char ID; // Pass identification

    MachineCSE() : MachineFunctionPass(ID) {
      initializeMachineCSEPass(*PassRegistry::getPassRegistry());
    }

    bool runOnMachineFunction(MachineFunction &MF) override;

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesCFG();
      MachineFunctionPass::getAnalysisUsage(AU);
      AU.addRequired<AAResultsWrapperPass>();
      AU.addPreservedID(MachineLoopInfoID);
      AU.addRequired<MachineDominatorTree>();
      AU.addPreserved<MachineDominatorTree>();
    }

    void releaseMemory() override {
      ScopeMap.clear();
      Exps.clear();
    }

  private:
    using AllocatorTy = RecyclingAllocator<BumpPtrAllocator,
                            ScopedHashTableVal<MachineInstr *, unsigned>>;
    using ScopedHTType =
        ScopedHashTable<MachineInstr *, unsigned, MachineInstrExpressionTrait,
                        AllocatorTy>;
    using ScopeType = ScopedHTType::ScopeTy;

    unsigned LookAheadLimit = 0;
    DenseMap<MachineBasicBlock *, ScopeType *> ScopeMap;
    ScopedHTType VNT;
    SmallVector<MachineInstr *, 64> Exps;
    unsigned CurrVN = 0;

    bool PerformTrivialCopyPropagation(MachineInstr *MI,
                                       MachineBasicBlock *MBB);
    bool isPhysDefTriviallyDead(unsigned Reg,
                                MachineBasicBlock::const_iterator I,
                                MachineBasicBlock::const_iterator E) const;
    bool hasLivePhysRegDefUses(const MachineInstr *MI,
                               const MachineBasicBlock *MBB,
                               SmallSet<unsigned,8> &PhysRefs,
                               SmallVectorImpl<unsigned> &PhysDefs,
                               bool &PhysUseDef) const;
    bool PhysRegDefsReach(MachineInstr *CSMI, MachineInstr *MI,
                          SmallSet<unsigned,8> &PhysRefs,
                          SmallVectorImpl<unsigned> &PhysDefs,
                          bool &NonLocal) const;
    bool isCSECandidate(MachineInstr *MI);
    bool isProfitableToCSE(unsigned CSReg, unsigned Reg,
                           MachineInstr *CSMI, MachineInstr *MI);
    void EnterScope(MachineBasicBlock *MBB);
    void ExitScope(MachineBasicBlock *MBB);
    bool ProcessBlock(MachineBasicBlock *MBB);
    void ExitScopeIfDone(MachineDomTreeNode *Node,
                         DenseMap<MachineDomTreeNode*, unsigned> &OpenChildren);
    bool PerformCSE(MachineDomTreeNode *Node);
  };

} // end anonymous namespace

char MachineCSE::ID = 0;

char &llvm::MachineCSEID = MachineCSE::ID;

INITIALIZE_PASS_BEGIN(MachineCSE, DEBUG_TYPE,
                      "Machine Common Subexpression Elimination", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(MachineCSE, DEBUG_TYPE,
                    "Machine Common Subexpression Elimination", false, false)

/// The source register of a COPY machine instruction can be propagated to all
/// its users, and this propagation could increase the probability of finding
/// common subexpressions. If the COPY has only one user, the COPY itself can
/// be removed.
bool MachineCSE::PerformTrivialCopyPropagation(MachineInstr *MI,
                                               MachineBasicBlock *MBB) {
  bool Changed = false;
  for (MachineOperand &MO : MI->operands()) {
    if (!MO.isReg() || !MO.isUse())
      continue;
    unsigned Reg = MO.getReg();
    if (!TargetRegisterInfo::isVirtualRegister(Reg))
      continue;
    bool OnlyOneUse = MRI->hasOneNonDBGUse(Reg);
    MachineInstr *DefMI = MRI->getVRegDef(Reg);
    if (!DefMI->isCopy())
      continue;
    unsigned SrcReg = DefMI->getOperand(1).getReg();
    if (!TargetRegisterInfo::isVirtualRegister(SrcReg))
      continue;
    if (DefMI->getOperand(0).getSubReg())
      continue;
    // FIXME: We should trivially coalesce subregister copies to expose CSE
    // opportunities on instructions with truncated operands (see
    // cse-add-with-overflow.ll). This can be done here as follows:
    // if (SrcSubReg)
    //  RC = TRI->getMatchingSuperRegClass(MRI->getRegClass(SrcReg), RC,
    //                                     SrcSubReg);
    // MO.substVirtReg(SrcReg, SrcSubReg, *TRI);
    //
    // The 2-addr pass has been updated to handle coalesced subregs. However,
    // some machine-specific code still can't handle it.
    // To handle it properly we also need a way find a constrained subregister
    // class given a super-reg class and subreg index.
    if (DefMI->getOperand(1).getSubReg())
      continue;
    if (!MRI->constrainRegAttrs(SrcReg, Reg))
      continue;
    LLVM_DEBUG(dbgs() << "Coalescing: " << *DefMI);
    LLVM_DEBUG(dbgs() << "***     to: " << *MI);

    // Update matching debug values.
    DefMI->changeDebugValuesDefReg(SrcReg);

    // Propagate SrcReg of copies to MI.
    MO.setReg(SrcReg);
    MRI->clearKillFlags(SrcReg);
    // Coalesce single use copies.
    if (OnlyOneUse) {
      DefMI->eraseFromParent();
      ++NumCoalesces;
    }
    Changed = true;
  }

  return Changed;
}

bool
MachineCSE::isPhysDefTriviallyDead(unsigned Reg,
                                   MachineBasicBlock::const_iterator I,
                                   MachineBasicBlock::const_iterator E) const {
  unsigned LookAheadLeft = LookAheadLimit;
  while (LookAheadLeft) {
    // Skip over dbg_value's.
    I = skipDebugInstructionsForward(I, E);

    if (I == E)
      // Reached end of block, we don't know if register is dead or not.
      return false;

    bool SeenDef = false;
    for (const MachineOperand &MO : I->operands()) {
      if (MO.isRegMask() && MO.clobbersPhysReg(Reg))
        SeenDef = true;
      if (!MO.isReg() || !MO.getReg())
        continue;
      if (!TRI->regsOverlap(MO.getReg(), Reg))
        continue;
      if (MO.isUse())
        // Found a use!
        return false;
      SeenDef = true;
    }
    if (SeenDef)
      // See a def of Reg (or an alias) before encountering any use, it's
      // trivially dead.
      return true;

    --LookAheadLeft;
    ++I;
  }
  return false;
}

static bool isCallerPreservedOrConstPhysReg(unsigned Reg,
                                            const MachineFunction &MF,
                                            const TargetRegisterInfo &TRI) {
  // MachineRegisterInfo::isConstantPhysReg directly called by
  // MachineRegisterInfo::isCallerPreservedOrConstPhysReg expects the
  // reserved registers to be frozen. That doesn't cause a problem  post-ISel as
  // most (if not all) targets freeze reserved registers right after ISel.
  //
  // It does cause issues mid-GlobalISel, however, hence the additional
  // reservedRegsFrozen check.
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  return TRI.isCallerPreservedPhysReg(Reg, MF) ||
         (MRI.reservedRegsFrozen() && MRI.isConstantPhysReg(Reg));
}

/// hasLivePhysRegDefUses - Return true if the specified instruction read/write
/// physical registers (except for dead defs of physical registers). It also
/// returns the physical register def by reference if it's the only one and the
/// instruction does not uses a physical register.
bool MachineCSE::hasLivePhysRegDefUses(const MachineInstr *MI,
                                       const MachineBasicBlock *MBB,
                                       SmallSet<unsigned,8> &PhysRefs,
                                       SmallVectorImpl<unsigned> &PhysDefs,
                                       bool &PhysUseDef) const{
  // First, add all uses to PhysRefs.
  for (const MachineOperand &MO : MI->operands()) {
    if (!MO.isReg() || MO.isDef())
      continue;
    unsigned Reg = MO.getReg();
    if (!Reg)
      continue;
    if (TargetRegisterInfo::isVirtualRegister(Reg))
      continue;
    // Reading either caller preserved or constant physregs is ok.
    if (!isCallerPreservedOrConstPhysReg(Reg, *MI->getMF(), *TRI))
      for (MCRegAliasIterator AI(Reg, TRI, true); AI.isValid(); ++AI)
        PhysRefs.insert(*AI);
  }

  // Next, collect all defs into PhysDefs.  If any is already in PhysRefs
  // (which currently contains only uses), set the PhysUseDef flag.
  PhysUseDef = false;
  MachineBasicBlock::const_iterator I = MI; I = std::next(I);
  for (const MachineOperand &MO : MI->operands()) {
    if (!MO.isReg() || !MO.isDef())
      continue;
    unsigned Reg = MO.getReg();
    if (!Reg)
      continue;
    if (TargetRegisterInfo::isVirtualRegister(Reg))
      continue;
    // Check against PhysRefs even if the def is "dead".
    if (PhysRefs.count(Reg))
      PhysUseDef = true;
    // If the def is dead, it's ok. But the def may not marked "dead". That's
    // common since this pass is run before livevariables. We can scan
    // forward a few instructions and check if it is obviously dead.
    if (!MO.isDead() && !isPhysDefTriviallyDead(Reg, I, MBB->end()))
      PhysDefs.push_back(Reg);
  }

  // Finally, add all defs to PhysRefs as well.
  for (unsigned i = 0, e = PhysDefs.size(); i != e; ++i)
    for (MCRegAliasIterator AI(PhysDefs[i], TRI, true); AI.isValid(); ++AI)
      PhysRefs.insert(*AI);

  return !PhysRefs.empty();
}

bool MachineCSE::PhysRegDefsReach(MachineInstr *CSMI, MachineInstr *MI,
                                  SmallSet<unsigned,8> &PhysRefs,
                                  SmallVectorImpl<unsigned> &PhysDefs,
                                  bool &NonLocal) const {
  // For now conservatively returns false if the common subexpression is
  // not in the same basic block as the given instruction. The only exception
  // is if the common subexpression is in the sole predecessor block.
  const MachineBasicBlock *MBB = MI->getParent();
  const MachineBasicBlock *CSMBB = CSMI->getParent();

  bool CrossMBB = false;
  if (CSMBB != MBB) {
    if (MBB->pred_size() != 1 || *MBB->pred_begin() != CSMBB)
      return false;

    for (unsigned i = 0, e = PhysDefs.size(); i != e; ++i) {
      if (MRI->isAllocatable(PhysDefs[i]) || MRI->isReserved(PhysDefs[i]))
        // Avoid extending live range of physical registers if they are
        //allocatable or reserved.
        return false;
    }
    CrossMBB = true;
  }
  MachineBasicBlock::const_iterator I = CSMI; I = std::next(I);
  MachineBasicBlock::const_iterator E = MI;
  MachineBasicBlock::const_iterator EE = CSMBB->end();
  unsigned LookAheadLeft = LookAheadLimit;
  while (LookAheadLeft) {
    // Skip over dbg_value's.
    while (I != E && I != EE && I->isDebugInstr())
      ++I;

    if (I == EE) {
      assert(CrossMBB && "Reaching end-of-MBB without finding MI?");
      (void)CrossMBB;
      CrossMBB = false;
      NonLocal = true;
      I = MBB->begin();
      EE = MBB->end();
      continue;
    }

    if (I == E)
      return true;

    for (const MachineOperand &MO : I->operands()) {
      // RegMasks go on instructions like calls that clobber lots of physregs.
      // Don't attempt to CSE across such an instruction.
      if (MO.isRegMask())
        return false;
      if (!MO.isReg() || !MO.isDef())
        continue;
      unsigned MOReg = MO.getReg();
      if (TargetRegisterInfo::isVirtualRegister(MOReg))
        continue;
      if (PhysRefs.count(MOReg))
        return false;
    }

    --LookAheadLeft;
    ++I;
  }

  return false;
}

bool MachineCSE::isCSECandidate(MachineInstr *MI) {
  if (MI->isPosition() || MI->isPHI() || MI->isImplicitDef() || MI->isKill() ||
      MI->isInlineAsm() || MI->isDebugInstr())
    return false;

  // Ignore copies.
  if (MI->isCopyLike())
    return false;

  // Ignore stuff that we obviously can't move.
  if (MI->mayStore() || MI->isCall() || MI->isTerminator() ||
      MI->hasUnmodeledSideEffects())
    return false;

  if (MI->mayLoad()) {
    // Okay, this instruction does a load. As a refinement, we allow the target
    // to decide whether the loaded value is actually a constant. If so, we can
    // actually use it as a load.
    if (!MI->isDereferenceableInvariantLoad(AA))
      // FIXME: we should be able to hoist loads with no other side effects if
      // there are no other instructions which can change memory in this loop.
      // This is a trivial form of alias analysis.
      return false;
  }

  // Ignore stack guard loads, otherwise the register that holds CSEed value may
  // be spilled and get loaded back with corrupted data.
  if (MI->getOpcode() == TargetOpcode::LOAD_STACK_GUARD)
    return false;

  return true;
}

/// isProfitableToCSE - Return true if it's profitable to eliminate MI with a
/// common expression that defines Reg.
bool MachineCSE::isProfitableToCSE(unsigned CSReg, unsigned Reg,
                                   MachineInstr *CSMI, MachineInstr *MI) {
  // FIXME: Heuristics that works around the lack the live range splitting.

  // If CSReg is used at all uses of Reg, CSE should not increase register
  // pressure of CSReg.
  bool MayIncreasePressure = true;
  if (TargetRegisterInfo::isVirtualRegister(CSReg) &&
      TargetRegisterInfo::isVirtualRegister(Reg)) {
    MayIncreasePressure = false;
    SmallPtrSet<MachineInstr*, 8> CSUses;
    for (MachineInstr &MI : MRI->use_nodbg_instructions(CSReg)) {
      CSUses.insert(&MI);
    }
    for (MachineInstr &MI : MRI->use_nodbg_instructions(Reg)) {
      if (!CSUses.count(&MI)) {
        MayIncreasePressure = true;
        break;
      }
    }
  }
  if (!MayIncreasePressure) return true;

  // Heuristics #1: Don't CSE "cheap" computation if the def is not local or in
  // an immediate predecessor. We don't want to increase register pressure and
  // end up causing other computation to be spilled.
  if (TII->isAsCheapAsAMove(*MI)) {
    MachineBasicBlock *CSBB = CSMI->getParent();
    MachineBasicBlock *BB = MI->getParent();
    if (CSBB != BB && !CSBB->isSuccessor(BB))
      return false;
  }

  // Heuristics #2: If the expression doesn't not use a vr and the only use
  // of the redundant computation are copies, do not cse.
  bool HasVRegUse = false;
  for (const MachineOperand &MO : MI->operands()) {
    if (MO.isReg() && MO.isUse() &&
        TargetRegisterInfo::isVirtualRegister(MO.getReg())) {
      HasVRegUse = true;
      break;
    }
  }
  if (!HasVRegUse) {
    bool HasNonCopyUse = false;
    for (MachineInstr &MI : MRI->use_nodbg_instructions(Reg)) {
      // Ignore copies.
      if (!MI.isCopyLike()) {
        HasNonCopyUse = true;
        break;
      }
    }
    if (!HasNonCopyUse)
      return false;
  }

  // Heuristics #3: If the common subexpression is used by PHIs, do not reuse
  // it unless the defined value is already used in the BB of the new use.
  bool HasPHI = false;
  for (MachineInstr &UseMI : MRI->use_nodbg_instructions(CSReg)) {
    HasPHI |= UseMI.isPHI();
    if (UseMI.getParent() == MI->getParent())
      return true;
  }

  return !HasPHI;
}

void MachineCSE::EnterScope(MachineBasicBlock *MBB) {
  LLVM_DEBUG(dbgs() << "Entering: " << MBB->getName() << '\n');
  ScopeType *Scope = new ScopeType(VNT);
  ScopeMap[MBB] = Scope;
}

void MachineCSE::ExitScope(MachineBasicBlock *MBB) {
  LLVM_DEBUG(dbgs() << "Exiting: " << MBB->getName() << '\n');
  DenseMap<MachineBasicBlock*, ScopeType*>::iterator SI = ScopeMap.find(MBB);
  assert(SI != ScopeMap.end());
  delete SI->second;
  ScopeMap.erase(SI);
}

bool MachineCSE::ProcessBlock(MachineBasicBlock *MBB) {
  bool Changed = false;

  SmallVector<std::pair<unsigned, unsigned>, 8> CSEPairs;
  SmallVector<unsigned, 2> ImplicitDefsToUpdate;
  SmallVector<unsigned, 2> ImplicitDefs;
  for (MachineBasicBlock::iterator I = MBB->begin(), E = MBB->end(); I != E; ) {
    MachineInstr *MI = &*I;
    ++I;

    if (!isCSECandidate(MI))
      continue;

    bool FoundCSE = VNT.count(MI);
    if (!FoundCSE) {
      // Using trivial copy propagation to find more CSE opportunities.
      if (PerformTrivialCopyPropagation(MI, MBB)) {
        Changed = true;

        // After coalescing MI itself may become a copy.
        if (MI->isCopyLike())
          continue;

        // Try again to see if CSE is possible.
        FoundCSE = VNT.count(MI);
      }
    }

    // Commute commutable instructions.
    bool Commuted = false;
    if (!FoundCSE && MI->isCommutable()) {
      if (MachineInstr *NewMI = TII->commuteInstruction(*MI)) {
        Commuted = true;
        FoundCSE = VNT.count(NewMI);
        if (NewMI != MI) {
          // New instruction. It doesn't need to be kept.
          NewMI->eraseFromParent();
          Changed = true;
        } else if (!FoundCSE)
          // MI was changed but it didn't help, commute it back!
          (void)TII->commuteInstruction(*MI);
      }
    }

    // If the instruction defines physical registers and the values *may* be
    // used, then it's not safe to replace it with a common subexpression.
    // It's also not safe if the instruction uses physical registers.
    bool CrossMBBPhysDef = false;
    SmallSet<unsigned, 8> PhysRefs;
    SmallVector<unsigned, 2> PhysDefs;
    bool PhysUseDef = false;
    if (FoundCSE && hasLivePhysRegDefUses(MI, MBB, PhysRefs,
                                          PhysDefs, PhysUseDef)) {
      FoundCSE = false;

      // ... Unless the CS is local or is in the sole predecessor block
      // and it also defines the physical register which is not clobbered
      // in between and the physical register uses were not clobbered.
      // This can never be the case if the instruction both uses and
      // defines the same physical register, which was detected above.
      if (!PhysUseDef) {
        unsigned CSVN = VNT.lookup(MI);
        MachineInstr *CSMI = Exps[CSVN];
        if (PhysRegDefsReach(CSMI, MI, PhysRefs, PhysDefs, CrossMBBPhysDef))
          FoundCSE = true;
      }
    }

    if (!FoundCSE) {
      VNT.insert(MI, CurrVN++);
      Exps.push_back(MI);
      continue;
    }

    // Found a common subexpression, eliminate it.
    unsigned CSVN = VNT.lookup(MI);
    MachineInstr *CSMI = Exps[CSVN];
    LLVM_DEBUG(dbgs() << "Examining: " << *MI);
    LLVM_DEBUG(dbgs() << "*** Found a common subexpression: " << *CSMI);

    // Check if it's profitable to perform this CSE.
    bool DoCSE = true;
    unsigned NumDefs = MI->getNumDefs();

    for (unsigned i = 0, e = MI->getNumOperands(); NumDefs && i != e; ++i) {
      MachineOperand &MO = MI->getOperand(i);
      if (!MO.isReg() || !MO.isDef())
        continue;
      unsigned OldReg = MO.getReg();
      unsigned NewReg = CSMI->getOperand(i).getReg();

      // Go through implicit defs of CSMI and MI, if a def is not dead at MI,
      // we should make sure it is not dead at CSMI.
      if (MO.isImplicit() && !MO.isDead() && CSMI->getOperand(i).isDead())
        ImplicitDefsToUpdate.push_back(i);

      // Keep track of implicit defs of CSMI and MI, to clear possibly
      // made-redundant kill flags.
      if (MO.isImplicit() && !MO.isDead() && OldReg == NewReg)
        ImplicitDefs.push_back(OldReg);

      if (OldReg == NewReg) {
        --NumDefs;
        continue;
      }

      assert(TargetRegisterInfo::isVirtualRegister(OldReg) &&
             TargetRegisterInfo::isVirtualRegister(NewReg) &&
             "Do not CSE physical register defs!");

      if (!isProfitableToCSE(NewReg, OldReg, CSMI, MI)) {
        LLVM_DEBUG(dbgs() << "*** Not profitable, avoid CSE!\n");
        DoCSE = false;
        break;
      }

      // Don't perform CSE if the result of the new instruction cannot exist
      // within the constraints (register class, bank, or low-level type) of
      // the old instruction.
      if (!MRI->constrainRegAttrs(NewReg, OldReg)) {
        LLVM_DEBUG(
            dbgs() << "*** Not the same register constraints, avoid CSE!\n");
        DoCSE = false;
        break;
      }

      CSEPairs.push_back(std::make_pair(OldReg, NewReg));
      --NumDefs;
    }

    // Actually perform the elimination.
    if (DoCSE) {
      for (std::pair<unsigned, unsigned> &CSEPair : CSEPairs) {
        unsigned OldReg = CSEPair.first;
        unsigned NewReg = CSEPair.second;
        // OldReg may have been unused but is used now, clear the Dead flag
        MachineInstr *Def = MRI->getUniqueVRegDef(NewReg);
        assert(Def != nullptr && "CSEd register has no unique definition?");
        Def->clearRegisterDeads(NewReg);
        // Replace with NewReg and clear kill flags which may be wrong now.
        MRI->replaceRegWith(OldReg, NewReg);
        MRI->clearKillFlags(NewReg);
      }

      // Go through implicit defs of CSMI and MI, if a def is not dead at MI,
      // we should make sure it is not dead at CSMI.
      for (unsigned ImplicitDefToUpdate : ImplicitDefsToUpdate)
        CSMI->getOperand(ImplicitDefToUpdate).setIsDead(false);

      // Go through implicit defs of CSMI and MI, and clear the kill flags on
      // their uses in all the instructions between CSMI and MI.
      // We might have made some of the kill flags redundant, consider:
      //   subs  ... implicit-def %nzcv    <- CSMI
      //   csinc ... implicit killed %nzcv <- this kill flag isn't valid anymore
      //   subs  ... implicit-def %nzcv    <- MI, to be eliminated
      //   csinc ... implicit killed %nzcv
      // Since we eliminated MI, and reused a register imp-def'd by CSMI
      // (here %nzcv), that register, if it was killed before MI, should have
      // that kill flag removed, because it's lifetime was extended.
      if (CSMI->getParent() == MI->getParent()) {
        for (MachineBasicBlock::iterator II = CSMI, IE = MI; II != IE; ++II)
          for (auto ImplicitDef : ImplicitDefs)
            if (MachineOperand *MO = II->findRegisterUseOperand(
                    ImplicitDef, /*isKill=*/true, TRI))
              MO->setIsKill(false);
      } else {
        // If the instructions aren't in the same BB, bail out and clear the
        // kill flag on all uses of the imp-def'd register.
        for (auto ImplicitDef : ImplicitDefs)
          MRI->clearKillFlags(ImplicitDef);
      }

      if (CrossMBBPhysDef) {
        // Add physical register defs now coming in from a predecessor to MBB
        // livein list.
        while (!PhysDefs.empty()) {
          unsigned LiveIn = PhysDefs.pop_back_val();
          if (!MBB->isLiveIn(LiveIn))
            MBB->addLiveIn(LiveIn);
        }
        ++NumCrossBBCSEs;
      }

      MI->eraseFromParent();
      ++NumCSEs;
      if (!PhysRefs.empty())
        ++NumPhysCSEs;
      if (Commuted)
        ++NumCommutes;
      Changed = true;
    } else {
      VNT.insert(MI, CurrVN++);
      Exps.push_back(MI);
    }
    CSEPairs.clear();
    ImplicitDefsToUpdate.clear();
    ImplicitDefs.clear();
  }

  return Changed;
}

/// ExitScopeIfDone - Destroy scope for the MBB that corresponds to the given
/// dominator tree node if its a leaf or all of its children are done. Walk
/// up the dominator tree to destroy ancestors which are now done.
void
MachineCSE::ExitScopeIfDone(MachineDomTreeNode *Node,
                        DenseMap<MachineDomTreeNode*, unsigned> &OpenChildren) {
  if (OpenChildren[Node])
    return;

  // Pop scope.
  ExitScope(Node->getBlock());

  // Now traverse upwards to pop ancestors whose offsprings are all done.
  while (MachineDomTreeNode *Parent = Node->getIDom()) {
    unsigned Left = --OpenChildren[Parent];
    if (Left != 0)
      break;
    ExitScope(Parent->getBlock());
    Node = Parent;
  }
}

bool MachineCSE::PerformCSE(MachineDomTreeNode *Node) {
  SmallVector<MachineDomTreeNode*, 32> Scopes;
  SmallVector<MachineDomTreeNode*, 8> WorkList;
  DenseMap<MachineDomTreeNode*, unsigned> OpenChildren;

  CurrVN = 0;

  // Perform a DFS walk to determine the order of visit.
  WorkList.push_back(Node);
  do {
    Node = WorkList.pop_back_val();
    Scopes.push_back(Node);
    const std::vector<MachineDomTreeNode*> &Children = Node->getChildren();
    OpenChildren[Node] = Children.size();
    for (MachineDomTreeNode *Child : Children)
      WorkList.push_back(Child);
  } while (!WorkList.empty());

  // Now perform CSE.
  bool Changed = false;
  for (MachineDomTreeNode *Node : Scopes) {
    MachineBasicBlock *MBB = Node->getBlock();
    EnterScope(MBB);
    Changed |= ProcessBlock(MBB);
    // If it's a leaf node, it's done. Traverse upwards to pop ancestors.
    ExitScopeIfDone(Node, OpenChildren);
  }

  return Changed;
}

bool MachineCSE::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  TII = MF.getSubtarget().getInstrInfo();
  TRI = MF.getSubtarget().getRegisterInfo();
  MRI = &MF.getRegInfo();
  AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
  DT = &getAnalysis<MachineDominatorTree>();
  LookAheadLimit = TII->getMachineCSELookAheadLimit();
  return PerformCSE(DT->getRootNode());
}
