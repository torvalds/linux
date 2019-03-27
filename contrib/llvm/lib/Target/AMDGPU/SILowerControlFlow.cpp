//===-- SILowerControlFlow.cpp - Use predicates for control flow ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This pass lowers the pseudo control flow instructions to real
/// machine instructions.
///
/// All control flow is handled using predicated instructions and
/// a predicate stack.  Each Scalar ALU controls the operations of 64 Vector
/// ALUs.  The Scalar ALU can update the predicate for any of the Vector ALUs
/// by writting to the 64-bit EXEC register (each bit corresponds to a
/// single vector ALU).  Typically, for predicates, a vector ALU will write
/// to its bit of the VCC register (like EXEC VCC is 64-bits, one for each
/// Vector ALU) and then the ScalarALU will AND the VCC register with the
/// EXEC to update the predicates.
///
/// For example:
/// %vcc = V_CMP_GT_F32 %vgpr1, %vgpr2
/// %sgpr0 = SI_IF %vcc
///   %vgpr0 = V_ADD_F32 %vgpr0, %vgpr0
/// %sgpr0 = SI_ELSE %sgpr0
///   %vgpr0 = V_SUB_F32 %vgpr0, %vgpr0
/// SI_END_CF %sgpr0
///
/// becomes:
///
/// %sgpr0 = S_AND_SAVEEXEC_B64 %vcc  // Save and update the exec mask
/// %sgpr0 = S_XOR_B64 %sgpr0, %exec  // Clear live bits from saved exec mask
/// S_CBRANCH_EXECZ label0            // This instruction is an optional
///                                   // optimization which allows us to
///                                   // branch if all the bits of
///                                   // EXEC are zero.
/// %vgpr0 = V_ADD_F32 %vgpr0, %vgpr0 // Do the IF block of the branch
///
/// label0:
/// %sgpr0 = S_OR_SAVEEXEC_B64 %exec   // Restore the exec mask for the Then block
/// %exec = S_XOR_B64 %sgpr0, %exec    // Clear live bits from saved exec mask
/// S_BRANCH_EXECZ label1              // Use our branch optimization
///                                    // instruction again.
/// %vgpr0 = V_SUB_F32 %vgpr0, %vgpr   // Do the THEN block
/// label1:
/// %exec = S_OR_B64 %exec, %sgpr0     // Re-enable saved exec mask bits
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUSubtarget.h"
#include "SIInstrInfo.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include <cassert>
#include <iterator>

using namespace llvm;

#define DEBUG_TYPE "si-lower-control-flow"

namespace {

class SILowerControlFlow : public MachineFunctionPass {
private:
  const SIRegisterInfo *TRI = nullptr;
  const SIInstrInfo *TII = nullptr;
  LiveIntervals *LIS = nullptr;
  MachineRegisterInfo *MRI = nullptr;

  void emitIf(MachineInstr &MI);
  void emitElse(MachineInstr &MI);
  void emitIfBreak(MachineInstr &MI);
  void emitLoop(MachineInstr &MI);
  void emitEndCf(MachineInstr &MI);

  void findMaskOperands(MachineInstr &MI, unsigned OpNo,
                        SmallVectorImpl<MachineOperand> &Src) const;

  void combineMasks(MachineInstr &MI);

public:
  static char ID;

  SILowerControlFlow() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "SI Lower control flow pseudo instructions";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    // Should preserve the same set that TwoAddressInstructions does.
    AU.addPreserved<SlotIndexes>();
    AU.addPreserved<LiveIntervals>();
    AU.addPreservedID(LiveVariablesID);
    AU.addPreservedID(MachineLoopInfoID);
    AU.addPreservedID(MachineDominatorsID);
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // end anonymous namespace

char SILowerControlFlow::ID = 0;

INITIALIZE_PASS(SILowerControlFlow, DEBUG_TYPE,
               "SI lower control flow", false, false)

static void setImpSCCDefDead(MachineInstr &MI, bool IsDead) {
  MachineOperand &ImpDefSCC = MI.getOperand(3);
  assert(ImpDefSCC.getReg() == AMDGPU::SCC && ImpDefSCC.isDef());

  ImpDefSCC.setIsDead(IsDead);
}

char &llvm::SILowerControlFlowID = SILowerControlFlow::ID;

static bool isSimpleIf(const MachineInstr &MI, const MachineRegisterInfo *MRI,
                       const SIInstrInfo *TII) {
  unsigned SaveExecReg = MI.getOperand(0).getReg();
  auto U = MRI->use_instr_nodbg_begin(SaveExecReg);

  if (U == MRI->use_instr_nodbg_end() ||
      std::next(U) != MRI->use_instr_nodbg_end() ||
      U->getOpcode() != AMDGPU::SI_END_CF)
    return false;

  // Check for SI_KILL_*_TERMINATOR on path from if to endif.
  // if there is any such terminator simplififcations are not safe.
  auto SMBB = MI.getParent();
  auto EMBB = U->getParent();
  DenseSet<const MachineBasicBlock*> Visited;
  SmallVector<MachineBasicBlock*, 4> Worklist(SMBB->succ_begin(),
                                              SMBB->succ_end());

  while (!Worklist.empty()) {
    MachineBasicBlock *MBB = Worklist.pop_back_val();

    if (MBB == EMBB || !Visited.insert(MBB).second)
      continue;
    for(auto &Term : MBB->terminators())
      if (TII->isKillTerminator(Term.getOpcode()))
        return false;

    Worklist.append(MBB->succ_begin(), MBB->succ_end());
  }

  return true;
}

void SILowerControlFlow::emitIf(MachineInstr &MI) {
  MachineBasicBlock &MBB = *MI.getParent();
  const DebugLoc &DL = MI.getDebugLoc();
  MachineBasicBlock::iterator I(&MI);

  MachineOperand &SaveExec = MI.getOperand(0);
  MachineOperand &Cond = MI.getOperand(1);
  assert(SaveExec.getSubReg() == AMDGPU::NoSubRegister &&
         Cond.getSubReg() == AMDGPU::NoSubRegister);

  unsigned SaveExecReg = SaveExec.getReg();

  MachineOperand &ImpDefSCC = MI.getOperand(4);
  assert(ImpDefSCC.getReg() == AMDGPU::SCC && ImpDefSCC.isDef());

  // If there is only one use of save exec register and that use is SI_END_CF,
  // we can optimize SI_IF by returning the full saved exec mask instead of
  // just cleared bits.
  bool SimpleIf = isSimpleIf(MI, MRI, TII);

  // Add an implicit def of exec to discourage scheduling VALU after this which
  // will interfere with trying to form s_and_saveexec_b64 later.
  unsigned CopyReg = SimpleIf ? SaveExecReg
                       : MRI->createVirtualRegister(&AMDGPU::SReg_64RegClass);
  MachineInstr *CopyExec =
    BuildMI(MBB, I, DL, TII->get(AMDGPU::COPY), CopyReg)
    .addReg(AMDGPU::EXEC)
    .addReg(AMDGPU::EXEC, RegState::ImplicitDefine);

  unsigned Tmp = MRI->createVirtualRegister(&AMDGPU::SReg_64RegClass);

  MachineInstr *And =
    BuildMI(MBB, I, DL, TII->get(AMDGPU::S_AND_B64), Tmp)
    .addReg(CopyReg)
    //.addReg(AMDGPU::EXEC)
    .addReg(Cond.getReg());
  setImpSCCDefDead(*And, true);

  MachineInstr *Xor = nullptr;
  if (!SimpleIf) {
    Xor =
      BuildMI(MBB, I, DL, TII->get(AMDGPU::S_XOR_B64), SaveExecReg)
      .addReg(Tmp)
      .addReg(CopyReg);
    setImpSCCDefDead(*Xor, ImpDefSCC.isDead());
  }

  // Use a copy that is a terminator to get correct spill code placement it with
  // fast regalloc.
  MachineInstr *SetExec =
    BuildMI(MBB, I, DL, TII->get(AMDGPU::S_MOV_B64_term), AMDGPU::EXEC)
    .addReg(Tmp, RegState::Kill);

  // Insert a pseudo terminator to help keep the verifier happy. This will also
  // be used later when inserting skips.
  MachineInstr *NewBr = BuildMI(MBB, I, DL, TII->get(AMDGPU::SI_MASK_BRANCH))
                            .add(MI.getOperand(2));

  if (!LIS) {
    MI.eraseFromParent();
    return;
  }

  LIS->InsertMachineInstrInMaps(*CopyExec);

  // Replace with and so we don't need to fix the live interval for condition
  // register.
  LIS->ReplaceMachineInstrInMaps(MI, *And);

  if (!SimpleIf)
    LIS->InsertMachineInstrInMaps(*Xor);
  LIS->InsertMachineInstrInMaps(*SetExec);
  LIS->InsertMachineInstrInMaps(*NewBr);

  LIS->removeRegUnit(*MCRegUnitIterator(AMDGPU::EXEC, TRI));
  MI.eraseFromParent();

  // FIXME: Is there a better way of adjusting the liveness? It shouldn't be
  // hard to add another def here but I'm not sure how to correctly update the
  // valno.
  LIS->removeInterval(SaveExecReg);
  LIS->createAndComputeVirtRegInterval(SaveExecReg);
  LIS->createAndComputeVirtRegInterval(Tmp);
  if (!SimpleIf)
    LIS->createAndComputeVirtRegInterval(CopyReg);
}

void SILowerControlFlow::emitElse(MachineInstr &MI) {
  MachineBasicBlock &MBB = *MI.getParent();
  const DebugLoc &DL = MI.getDebugLoc();

  unsigned DstReg = MI.getOperand(0).getReg();
  assert(MI.getOperand(0).getSubReg() == AMDGPU::NoSubRegister);

  bool ExecModified = MI.getOperand(3).getImm() != 0;
  MachineBasicBlock::iterator Start = MBB.begin();

  // We are running before TwoAddressInstructions, and si_else's operands are
  // tied. In order to correctly tie the registers, split this into a copy of
  // the src like it does.
  unsigned CopyReg = MRI->createVirtualRegister(&AMDGPU::SReg_64RegClass);
  MachineInstr *CopyExec =
    BuildMI(MBB, Start, DL, TII->get(AMDGPU::COPY), CopyReg)
      .add(MI.getOperand(1)); // Saved EXEC

  // This must be inserted before phis and any spill code inserted before the
  // else.
  unsigned SaveReg = ExecModified ?
    MRI->createVirtualRegister(&AMDGPU::SReg_64RegClass) : DstReg;
  MachineInstr *OrSaveExec =
    BuildMI(MBB, Start, DL, TII->get(AMDGPU::S_OR_SAVEEXEC_B64), SaveReg)
    .addReg(CopyReg);

  MachineBasicBlock *DestBB = MI.getOperand(2).getMBB();

  MachineBasicBlock::iterator ElsePt(MI);

  if (ExecModified) {
    MachineInstr *And =
      BuildMI(MBB, ElsePt, DL, TII->get(AMDGPU::S_AND_B64), DstReg)
      .addReg(AMDGPU::EXEC)
      .addReg(SaveReg);

    if (LIS)
      LIS->InsertMachineInstrInMaps(*And);
  }

  MachineInstr *Xor =
    BuildMI(MBB, ElsePt, DL, TII->get(AMDGPU::S_XOR_B64_term), AMDGPU::EXEC)
    .addReg(AMDGPU::EXEC)
    .addReg(DstReg);

  MachineInstr *Branch =
    BuildMI(MBB, ElsePt, DL, TII->get(AMDGPU::SI_MASK_BRANCH))
    .addMBB(DestBB);

  if (!LIS) {
    MI.eraseFromParent();
    return;
  }

  LIS->RemoveMachineInstrFromMaps(MI);
  MI.eraseFromParent();

  LIS->InsertMachineInstrInMaps(*CopyExec);
  LIS->InsertMachineInstrInMaps(*OrSaveExec);

  LIS->InsertMachineInstrInMaps(*Xor);
  LIS->InsertMachineInstrInMaps(*Branch);

  // src reg is tied to dst reg.
  LIS->removeInterval(DstReg);
  LIS->createAndComputeVirtRegInterval(DstReg);
  LIS->createAndComputeVirtRegInterval(CopyReg);
  if (ExecModified)
    LIS->createAndComputeVirtRegInterval(SaveReg);

  // Let this be recomputed.
  LIS->removeRegUnit(*MCRegUnitIterator(AMDGPU::EXEC, TRI));
}

void SILowerControlFlow::emitIfBreak(MachineInstr &MI) {
  MachineBasicBlock &MBB = *MI.getParent();
  const DebugLoc &DL = MI.getDebugLoc();
  auto Dst = MI.getOperand(0).getReg();

  // Skip ANDing with exec if the break condition is already masked by exec
  // because it is a V_CMP in the same basic block. (We know the break
  // condition operand was an i1 in IR, so if it is a VALU instruction it must
  // be one with a carry-out.)
  bool SkipAnding = false;
  if (MI.getOperand(1).isReg()) {
    if (MachineInstr *Def = MRI->getUniqueVRegDef(MI.getOperand(1).getReg())) {
      SkipAnding = Def->getParent() == MI.getParent()
          && SIInstrInfo::isVALU(*Def);
    }
  }

  // AND the break condition operand with exec, then OR that into the "loop
  // exit" mask.
  MachineInstr *And = nullptr, *Or = nullptr;
  if (!SkipAnding) {
    And = BuildMI(MBB, &MI, DL, TII->get(AMDGPU::S_AND_B64), Dst)
             .addReg(AMDGPU::EXEC)
             .add(MI.getOperand(1));
    Or = BuildMI(MBB, &MI, DL, TII->get(AMDGPU::S_OR_B64), Dst)
             .addReg(Dst)
             .add(MI.getOperand(2));
  } else
    Or = BuildMI(MBB, &MI, DL, TII->get(AMDGPU::S_OR_B64), Dst)
             .add(MI.getOperand(1))
             .add(MI.getOperand(2));

  if (LIS) {
    if (And)
      LIS->InsertMachineInstrInMaps(*And);
    LIS->ReplaceMachineInstrInMaps(MI, *Or);
  }

  MI.eraseFromParent();
}

void SILowerControlFlow::emitLoop(MachineInstr &MI) {
  MachineBasicBlock &MBB = *MI.getParent();
  const DebugLoc &DL = MI.getDebugLoc();

  MachineInstr *AndN2 =
      BuildMI(MBB, &MI, DL, TII->get(AMDGPU::S_ANDN2_B64_term), AMDGPU::EXEC)
          .addReg(AMDGPU::EXEC)
          .add(MI.getOperand(0));

  MachineInstr *Branch =
      BuildMI(MBB, &MI, DL, TII->get(AMDGPU::S_CBRANCH_EXECNZ))
          .add(MI.getOperand(1));

  if (LIS) {
    LIS->ReplaceMachineInstrInMaps(MI, *AndN2);
    LIS->InsertMachineInstrInMaps(*Branch);
  }

  MI.eraseFromParent();
}

void SILowerControlFlow::emitEndCf(MachineInstr &MI) {
  MachineBasicBlock &MBB = *MI.getParent();
  const DebugLoc &DL = MI.getDebugLoc();

  MachineBasicBlock::iterator InsPt = MBB.begin();
  MachineInstr *NewMI =
      BuildMI(MBB, InsPt, DL, TII->get(AMDGPU::S_OR_B64), AMDGPU::EXEC)
          .addReg(AMDGPU::EXEC)
          .add(MI.getOperand(0));

  if (LIS)
    LIS->ReplaceMachineInstrInMaps(MI, *NewMI);

  MI.eraseFromParent();

  if (LIS)
    LIS->handleMove(*NewMI);
}

// Returns replace operands for a logical operation, either single result
// for exec or two operands if source was another equivalent operation.
void SILowerControlFlow::findMaskOperands(MachineInstr &MI, unsigned OpNo,
       SmallVectorImpl<MachineOperand> &Src) const {
  MachineOperand &Op = MI.getOperand(OpNo);
  if (!Op.isReg() || !TargetRegisterInfo::isVirtualRegister(Op.getReg())) {
    Src.push_back(Op);
    return;
  }

  MachineInstr *Def = MRI->getUniqueVRegDef(Op.getReg());
  if (!Def || Def->getParent() != MI.getParent() ||
      !(Def->isFullCopy() || (Def->getOpcode() == MI.getOpcode())))
    return;

  // Make sure we do not modify exec between def and use.
  // A copy with implcitly defined exec inserted earlier is an exclusion, it
  // does not really modify exec.
  for (auto I = Def->getIterator(); I != MI.getIterator(); ++I)
    if (I->modifiesRegister(AMDGPU::EXEC, TRI) &&
        !(I->isCopy() && I->getOperand(0).getReg() != AMDGPU::EXEC))
      return;

  for (const auto &SrcOp : Def->explicit_operands())
    if (SrcOp.isReg() && SrcOp.isUse() &&
        (TargetRegisterInfo::isVirtualRegister(SrcOp.getReg()) ||
        SrcOp.getReg() == AMDGPU::EXEC))
      Src.push_back(SrcOp);
}

// Search and combine pairs of equivalent instructions, like
// S_AND_B64 x, (S_AND_B64 x, y) => S_AND_B64 x, y
// S_OR_B64  x, (S_OR_B64  x, y) => S_OR_B64  x, y
// One of the operands is exec mask.
void SILowerControlFlow::combineMasks(MachineInstr &MI) {
  assert(MI.getNumExplicitOperands() == 3);
  SmallVector<MachineOperand, 4> Ops;
  unsigned OpToReplace = 1;
  findMaskOperands(MI, 1, Ops);
  if (Ops.size() == 1) OpToReplace = 2; // First operand can be exec or its copy
  findMaskOperands(MI, 2, Ops);
  if (Ops.size() != 3) return;

  unsigned UniqueOpndIdx;
  if (Ops[0].isIdenticalTo(Ops[1])) UniqueOpndIdx = 2;
  else if (Ops[0].isIdenticalTo(Ops[2])) UniqueOpndIdx = 1;
  else if (Ops[1].isIdenticalTo(Ops[2])) UniqueOpndIdx = 1;
  else return;

  unsigned Reg = MI.getOperand(OpToReplace).getReg();
  MI.RemoveOperand(OpToReplace);
  MI.addOperand(Ops[UniqueOpndIdx]);
  if (MRI->use_empty(Reg))
    MRI->getUniqueVRegDef(Reg)->eraseFromParent();
}

bool SILowerControlFlow::runOnMachineFunction(MachineFunction &MF) {
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  TII = ST.getInstrInfo();
  TRI = &TII->getRegisterInfo();

  // This doesn't actually need LiveIntervals, but we can preserve them.
  LIS = getAnalysisIfAvailable<LiveIntervals>();
  MRI = &MF.getRegInfo();

  MachineFunction::iterator NextBB;
  for (MachineFunction::iterator BI = MF.begin(), BE = MF.end();
       BI != BE; BI = NextBB) {
    NextBB = std::next(BI);
    MachineBasicBlock &MBB = *BI;

    MachineBasicBlock::iterator I, Next, Last;

    for (I = MBB.begin(), Last = MBB.end(); I != MBB.end(); I = Next) {
      Next = std::next(I);
      MachineInstr &MI = *I;

      switch (MI.getOpcode()) {
      case AMDGPU::SI_IF:
        emitIf(MI);
        break;

      case AMDGPU::SI_ELSE:
        emitElse(MI);
        break;

      case AMDGPU::SI_IF_BREAK:
        emitIfBreak(MI);
        break;

      case AMDGPU::SI_LOOP:
        emitLoop(MI);
        break;

      case AMDGPU::SI_END_CF:
        emitEndCf(MI);
        break;

      case AMDGPU::S_AND_B64:
      case AMDGPU::S_OR_B64:
        // Cleanup bit manipulations on exec mask
        combineMasks(MI);
        Last = I;
        continue;

      default:
        Last = I;
        continue;
      }

      // Replay newly inserted code to combine masks
      Next = (Last == MBB.end()) ? MBB.begin() : Last;
    }
  }

  return true;
}
