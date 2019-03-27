//===-- SIOptimizeExecMaskingPreRA.cpp ------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This pass removes redundant S_OR_B64 instructions enabling lanes in
/// the exec. If two SI_END_CF (lowered as S_OR_B64) come together without any
/// vector instructions between them we can only keep outer SI_END_CF, given
/// that CFG is structured and exec bits of the outer end statement are always
/// not less than exec bit of the inner one.
///
/// This needs to be done before the RA to eliminate saved exec bits registers
/// but after register coalescer to have no vector registers copies in between
/// of different end cf statements.
///
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUSubtarget.h"
#include "SIInstrInfo.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineFunctionPass.h"

using namespace llvm;

#define DEBUG_TYPE "si-optimize-exec-masking-pre-ra"

namespace {

class SIOptimizeExecMaskingPreRA : public MachineFunctionPass {
public:
  static char ID;

public:
  SIOptimizeExecMaskingPreRA() : MachineFunctionPass(ID) {
    initializeSIOptimizeExecMaskingPreRAPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "SI optimize exec mask operations pre-RA";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<LiveIntervals>();
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // End anonymous namespace.

INITIALIZE_PASS_BEGIN(SIOptimizeExecMaskingPreRA, DEBUG_TYPE,
                      "SI optimize exec mask operations pre-RA", false, false)
INITIALIZE_PASS_DEPENDENCY(LiveIntervals)
INITIALIZE_PASS_END(SIOptimizeExecMaskingPreRA, DEBUG_TYPE,
                    "SI optimize exec mask operations pre-RA", false, false)

char SIOptimizeExecMaskingPreRA::ID = 0;

char &llvm::SIOptimizeExecMaskingPreRAID = SIOptimizeExecMaskingPreRA::ID;

FunctionPass *llvm::createSIOptimizeExecMaskingPreRAPass() {
  return new SIOptimizeExecMaskingPreRA();
}

static bool isEndCF(const MachineInstr& MI, const SIRegisterInfo* TRI) {
  return MI.getOpcode() == AMDGPU::S_OR_B64 &&
         MI.modifiesRegister(AMDGPU::EXEC, TRI);
}

static bool isFullExecCopy(const MachineInstr& MI) {
  return MI.isFullCopy() && MI.getOperand(1).getReg() == AMDGPU::EXEC;
}

static unsigned getOrNonExecReg(const MachineInstr &MI,
                                const SIInstrInfo &TII) {
  auto Op = TII.getNamedOperand(MI, AMDGPU::OpName::src1);
  if (Op->isReg() && Op->getReg() != AMDGPU::EXEC)
     return Op->getReg();
  Op = TII.getNamedOperand(MI, AMDGPU::OpName::src0);
  if (Op->isReg() && Op->getReg() != AMDGPU::EXEC)
     return Op->getReg();
  return AMDGPU::NoRegister;
}

static MachineInstr* getOrExecSource(const MachineInstr &MI,
                                     const SIInstrInfo &TII,
                                     const MachineRegisterInfo &MRI) {
  auto SavedExec = getOrNonExecReg(MI, TII);
  if (SavedExec == AMDGPU::NoRegister)
    return nullptr;
  auto SaveExecInst = MRI.getUniqueVRegDef(SavedExec);
  if (!SaveExecInst || !isFullExecCopy(*SaveExecInst))
    return nullptr;
  return SaveExecInst;
}

// Optimize sequence
//    %sel = V_CNDMASK_B32_e64 0, 1, %cc
//    %cmp = V_CMP_NE_U32 1, %1
//    $vcc = S_AND_B64 $exec, %cmp
//    S_CBRANCH_VCC[N]Z
// =>
//    $vcc = S_ANDN2_B64 $exec, %cc
//    S_CBRANCH_VCC[N]Z
//
// It is the negation pattern inserted by DAGCombiner::visitBRCOND() in the
// rebuildSetCC(). We start with S_CBRANCH to avoid exhaustive search, but
// only 3 first instructions are really needed. S_AND_B64 with exec is a
// required part of the pattern since V_CNDMASK_B32 writes zeroes for inactive
// lanes.
//
// Returns %cc register on success.
static unsigned optimizeVcndVcmpPair(MachineBasicBlock &MBB,
                                     const GCNSubtarget &ST,
                                     MachineRegisterInfo &MRI,
                                     LiveIntervals *LIS) {
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  const SIInstrInfo *TII = ST.getInstrInfo();
  const unsigned AndOpc = AMDGPU::S_AND_B64;
  const unsigned Andn2Opc = AMDGPU::S_ANDN2_B64;
  const unsigned CondReg = AMDGPU::VCC;
  const unsigned ExecReg = AMDGPU::EXEC;

  auto I = llvm::find_if(MBB.terminators(), [](const MachineInstr &MI) {
                           unsigned Opc = MI.getOpcode();
                           return Opc == AMDGPU::S_CBRANCH_VCCZ ||
                                  Opc == AMDGPU::S_CBRANCH_VCCNZ; });
  if (I == MBB.terminators().end())
    return AMDGPU::NoRegister;

  auto *And = TRI->findReachingDef(CondReg, AMDGPU::NoSubRegister,
                                   *I, MRI, LIS);
  if (!And || And->getOpcode() != AndOpc ||
      !And->getOperand(1).isReg() || !And->getOperand(2).isReg())
    return AMDGPU::NoRegister;

  MachineOperand *AndCC = &And->getOperand(1);
  unsigned CmpReg = AndCC->getReg();
  unsigned CmpSubReg = AndCC->getSubReg();
  if (CmpReg == ExecReg) {
    AndCC = &And->getOperand(2);
    CmpReg = AndCC->getReg();
    CmpSubReg = AndCC->getSubReg();
  } else if (And->getOperand(2).getReg() != ExecReg) {
    return AMDGPU::NoRegister;
  }

  auto *Cmp = TRI->findReachingDef(CmpReg, CmpSubReg, *And, MRI, LIS);
  if (!Cmp || !(Cmp->getOpcode() == AMDGPU::V_CMP_NE_U32_e32 ||
                Cmp->getOpcode() == AMDGPU::V_CMP_NE_U32_e64) ||
      Cmp->getParent() != And->getParent())
    return AMDGPU::NoRegister;

  MachineOperand *Op1 = TII->getNamedOperand(*Cmp, AMDGPU::OpName::src0);
  MachineOperand *Op2 = TII->getNamedOperand(*Cmp, AMDGPU::OpName::src1);
  if (Op1->isImm() && Op2->isReg())
    std::swap(Op1, Op2);
  if (!Op1->isReg() || !Op2->isImm() || Op2->getImm() != 1)
    return AMDGPU::NoRegister;

  unsigned SelReg = Op1->getReg();
  auto *Sel = TRI->findReachingDef(SelReg, Op1->getSubReg(), *Cmp, MRI, LIS);
  if (!Sel || Sel->getOpcode() != AMDGPU::V_CNDMASK_B32_e64)
    return AMDGPU::NoRegister;

  Op1 = TII->getNamedOperand(*Sel, AMDGPU::OpName::src0);
  Op2 = TII->getNamedOperand(*Sel, AMDGPU::OpName::src1);
  MachineOperand *CC = TII->getNamedOperand(*Sel, AMDGPU::OpName::src2);
  if (!Op1->isImm() || !Op2->isImm() || !CC->isReg() ||
      Op1->getImm() != 0 || Op2->getImm() != 1)
    return AMDGPU::NoRegister;

  LLVM_DEBUG(dbgs() << "Folding sequence:\n\t" << *Sel << '\t'
                    << *Cmp << '\t' << *And);

  unsigned CCReg = CC->getReg();
  LIS->RemoveMachineInstrFromMaps(*And);
  MachineInstr *Andn2 = BuildMI(MBB, *And, And->getDebugLoc(),
                                TII->get(Andn2Opc), And->getOperand(0).getReg())
                            .addReg(ExecReg)
                            .addReg(CCReg, CC->getSubReg());
  And->eraseFromParent();
  LIS->InsertMachineInstrInMaps(*Andn2);

  LLVM_DEBUG(dbgs() << "=>\n\t" << *Andn2 << '\n');

  // Try to remove compare. Cmp value should not used in between of cmp
  // and s_and_b64 if VCC or just unused if any other register.
  if ((TargetRegisterInfo::isVirtualRegister(CmpReg) &&
       MRI.use_nodbg_empty(CmpReg)) ||
      (CmpReg == CondReg &&
       std::none_of(std::next(Cmp->getIterator()), Andn2->getIterator(),
                    [&](const MachineInstr &MI) {
                      return MI.readsRegister(CondReg, TRI); }))) {
    LLVM_DEBUG(dbgs() << "Erasing: " << *Cmp << '\n');

    LIS->RemoveMachineInstrFromMaps(*Cmp);
    Cmp->eraseFromParent();

    // Try to remove v_cndmask_b32.
    if (TargetRegisterInfo::isVirtualRegister(SelReg) &&
        MRI.use_nodbg_empty(SelReg)) {
      LLVM_DEBUG(dbgs() << "Erasing: " << *Sel << '\n');

      LIS->RemoveMachineInstrFromMaps(*Sel);
      Sel->eraseFromParent();
    }
  }

  return CCReg;
}

bool SIOptimizeExecMaskingPreRA::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  const SIInstrInfo *TII = ST.getInstrInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  LiveIntervals *LIS = &getAnalysis<LiveIntervals>();
  DenseSet<unsigned> RecalcRegs({AMDGPU::EXEC_LO, AMDGPU::EXEC_HI});
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {

    if (unsigned Reg = optimizeVcndVcmpPair(MBB, ST, MRI, LIS)) {
      RecalcRegs.insert(Reg);
      RecalcRegs.insert(AMDGPU::VCC_LO);
      RecalcRegs.insert(AMDGPU::VCC_HI);
      RecalcRegs.insert(AMDGPU::SCC);
      Changed = true;
    }

    // Try to remove unneeded instructions before s_endpgm.
    if (MBB.succ_empty()) {
      if (MBB.empty())
        continue;

      // Skip this if the endpgm has any implicit uses, otherwise we would need
      // to be careful to update / remove them.
      MachineInstr &Term = MBB.back();
      if (Term.getOpcode() != AMDGPU::S_ENDPGM ||
          Term.getNumOperands() != 0)
        continue;

      SmallVector<MachineBasicBlock*, 4> Blocks({&MBB});

      while (!Blocks.empty()) {
        auto CurBB = Blocks.pop_back_val();
        auto I = CurBB->rbegin(), E = CurBB->rend();
        if (I != E) {
          if (I->isUnconditionalBranch() || I->getOpcode() == AMDGPU::S_ENDPGM)
            ++I;
          else if (I->isBranch())
            continue;
        }

        while (I != E) {
          if (I->isDebugInstr()) {
            I = std::next(I);
            continue;
          }

          if (I->mayStore() || I->isBarrier() || I->isCall() ||
              I->hasUnmodeledSideEffects() || I->hasOrderedMemoryRef())
            break;

          LLVM_DEBUG(dbgs()
                     << "Removing no effect instruction: " << *I << '\n');

          for (auto &Op : I->operands()) {
            if (Op.isReg())
              RecalcRegs.insert(Op.getReg());
          }

          auto Next = std::next(I);
          LIS->RemoveMachineInstrFromMaps(*I);
          I->eraseFromParent();
          I = Next;

          Changed = true;
        }

        if (I != E)
          continue;

        // Try to ascend predecessors.
        for (auto *Pred : CurBB->predecessors()) {
          if (Pred->succ_size() == 1)
            Blocks.push_back(Pred);
        }
      }
      continue;
    }

    // Try to collapse adjacent endifs.
    auto Lead = MBB.begin(), E = MBB.end();
    if (MBB.succ_size() != 1 || Lead == E || !isEndCF(*Lead, TRI))
      continue;

    const MachineBasicBlock* Succ = *MBB.succ_begin();
    if (!MBB.isLayoutSuccessor(Succ))
      continue;

    auto I = std::next(Lead);

    for ( ; I != E; ++I)
      if (!TII->isSALU(*I) || I->readsRegister(AMDGPU::EXEC, TRI))
        break;

    if (I != E)
      continue;

    const auto NextLead = Succ->begin();
    if (NextLead == Succ->end() || !isEndCF(*NextLead, TRI) ||
        !getOrExecSource(*NextLead, *TII, MRI))
      continue;

    LLVM_DEBUG(dbgs() << "Redundant EXEC = S_OR_B64 found: " << *Lead << '\n');

    auto SaveExec = getOrExecSource(*Lead, *TII, MRI);
    unsigned SaveExecReg = getOrNonExecReg(*Lead, *TII);
    for (auto &Op : Lead->operands()) {
      if (Op.isReg())
        RecalcRegs.insert(Op.getReg());
    }

    LIS->RemoveMachineInstrFromMaps(*Lead);
    Lead->eraseFromParent();
    if (SaveExecReg) {
      LIS->removeInterval(SaveExecReg);
      LIS->createAndComputeVirtRegInterval(SaveExecReg);
    }

    Changed = true;

    // If the only use of saved exec in the removed instruction is S_AND_B64
    // fold the copy now.
    if (!SaveExec || !SaveExec->isFullCopy())
      continue;

    unsigned SavedExec = SaveExec->getOperand(0).getReg();
    bool SafeToReplace = true;
    for (auto& U : MRI.use_nodbg_instructions(SavedExec)) {
      if (U.getParent() != SaveExec->getParent()) {
        SafeToReplace = false;
        break;
      }

      LLVM_DEBUG(dbgs() << "Redundant EXEC COPY: " << *SaveExec << '\n');
    }

    if (SafeToReplace) {
      LIS->RemoveMachineInstrFromMaps(*SaveExec);
      SaveExec->eraseFromParent();
      MRI.replaceRegWith(SavedExec, AMDGPU::EXEC);
      LIS->removeInterval(SavedExec);
    }
  }

  if (Changed) {
    for (auto Reg : RecalcRegs) {
      if (TargetRegisterInfo::isVirtualRegister(Reg)) {
        LIS->removeInterval(Reg);
        if (!MRI.reg_empty(Reg))
          LIS->createAndComputeVirtRegInterval(Reg);
      } else {
        for (MCRegUnitIterator U(Reg, TRI); U.isValid(); ++U)
          LIS->removeRegUnit(*U);
      }
    }
  }

  return Changed;
}
