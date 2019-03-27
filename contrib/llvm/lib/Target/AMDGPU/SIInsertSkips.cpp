//===-- SIInsertSkips.cpp - Use predicates for control flow ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This pass inserts branches on the 0 exec mask over divergent branches
/// branches when it's expected that jumping over the untaken control flow will
/// be cheaper than having every workitem no-op through it.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUSubtarget.h"
#include "SIInstrInfo.h"
#include "SIMachineFunctionInfo.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetMachine.h"
#include <cassert>
#include <cstdint>
#include <iterator>

using namespace llvm;

#define DEBUG_TYPE "si-insert-skips"

static cl::opt<unsigned> SkipThresholdFlag(
  "amdgpu-skip-threshold",
  cl::desc("Number of instructions before jumping over divergent control flow"),
  cl::init(12), cl::Hidden);

namespace {

class SIInsertSkips : public MachineFunctionPass {
private:
  const SIRegisterInfo *TRI = nullptr;
  const SIInstrInfo *TII = nullptr;
  unsigned SkipThreshold = 0;

  bool shouldSkip(const MachineBasicBlock &From,
                  const MachineBasicBlock &To) const;

  bool skipIfDead(MachineInstr &MI, MachineBasicBlock &NextBB);

  void kill(MachineInstr &MI);

  MachineBasicBlock *insertSkipBlock(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator I) const;

  bool skipMaskBranch(MachineInstr &MI, MachineBasicBlock &MBB);

  bool optimizeVccBranch(MachineInstr &MI) const;

public:
  static char ID;

  SIInsertSkips() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "SI insert s_cbranch_execz instructions";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // end anonymous namespace

char SIInsertSkips::ID = 0;

INITIALIZE_PASS(SIInsertSkips, DEBUG_TYPE,
                "SI insert s_cbranch_execz instructions", false, false)

char &llvm::SIInsertSkipsPassID = SIInsertSkips::ID;

static bool opcodeEmitsNoInsts(unsigned Opc) {
  switch (Opc) {
  case TargetOpcode::IMPLICIT_DEF:
  case TargetOpcode::KILL:
  case TargetOpcode::BUNDLE:
  case TargetOpcode::CFI_INSTRUCTION:
  case TargetOpcode::EH_LABEL:
  case TargetOpcode::GC_LABEL:
  case TargetOpcode::DBG_VALUE:
    return true;
  default:
    return false;
  }
}

bool SIInsertSkips::shouldSkip(const MachineBasicBlock &From,
                               const MachineBasicBlock &To) const {
  if (From.succ_empty())
    return false;

  unsigned NumInstr = 0;
  const MachineFunction *MF = From.getParent();

  for (MachineFunction::const_iterator MBBI(&From), ToI(&To), End = MF->end();
       MBBI != End && MBBI != ToI; ++MBBI) {
    const MachineBasicBlock &MBB = *MBBI;

    for (MachineBasicBlock::const_iterator I = MBB.begin(), E = MBB.end();
         NumInstr < SkipThreshold && I != E; ++I) {
      if (opcodeEmitsNoInsts(I->getOpcode()))
        continue;

      // FIXME: Since this is required for correctness, this should be inserted
      // during SILowerControlFlow.

      // When a uniform loop is inside non-uniform control flow, the branch
      // leaving the loop might be an S_CBRANCH_VCCNZ, which is never taken
      // when EXEC = 0. We should skip the loop lest it becomes infinite.
      if (I->getOpcode() == AMDGPU::S_CBRANCH_VCCNZ ||
          I->getOpcode() == AMDGPU::S_CBRANCH_VCCZ)
        return true;

      if (TII->hasUnwantedEffectsWhenEXECEmpty(*I))
        return true;

      ++NumInstr;
      if (NumInstr >= SkipThreshold)
        return true;
    }
  }

  return false;
}

bool SIInsertSkips::skipIfDead(MachineInstr &MI, MachineBasicBlock &NextBB) {
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction *MF = MBB.getParent();

  if (MF->getFunction().getCallingConv() != CallingConv::AMDGPU_PS ||
      !shouldSkip(MBB, MBB.getParent()->back()))
    return false;

  MachineBasicBlock *SkipBB = insertSkipBlock(MBB, MI.getIterator());

  const DebugLoc &DL = MI.getDebugLoc();

  // If the exec mask is non-zero, skip the next two instructions
  BuildMI(&MBB, DL, TII->get(AMDGPU::S_CBRANCH_EXECNZ))
    .addMBB(&NextBB);

  MachineBasicBlock::iterator Insert = SkipBB->begin();

  // Exec mask is zero: Export to NULL target...
  BuildMI(*SkipBB, Insert, DL, TII->get(AMDGPU::EXP_DONE))
    .addImm(0x09) // V_008DFC_SQ_EXP_NULL
    .addReg(AMDGPU::VGPR0, RegState::Undef)
    .addReg(AMDGPU::VGPR0, RegState::Undef)
    .addReg(AMDGPU::VGPR0, RegState::Undef)
    .addReg(AMDGPU::VGPR0, RegState::Undef)
    .addImm(1)  // vm
    .addImm(0)  // compr
    .addImm(0); // en

  // ... and terminate wavefront.
  BuildMI(*SkipBB, Insert, DL, TII->get(AMDGPU::S_ENDPGM));

  return true;
}

void SIInsertSkips::kill(MachineInstr &MI) {
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MI.getDebugLoc();

  switch (MI.getOpcode()) {
  case AMDGPU::SI_KILL_F32_COND_IMM_TERMINATOR: {
    unsigned Opcode = 0;

    // The opcodes are inverted because the inline immediate has to be
    // the first operand, e.g. from "x < imm" to "imm > x"
    switch (MI.getOperand(2).getImm()) {
    case ISD::SETOEQ:
    case ISD::SETEQ:
      Opcode = AMDGPU::V_CMPX_EQ_F32_e64;
      break;
    case ISD::SETOGT:
    case ISD::SETGT:
      Opcode = AMDGPU::V_CMPX_LT_F32_e64;
      break;
    case ISD::SETOGE:
    case ISD::SETGE:
      Opcode = AMDGPU::V_CMPX_LE_F32_e64;
      break;
    case ISD::SETOLT:
    case ISD::SETLT:
      Opcode = AMDGPU::V_CMPX_GT_F32_e64;
      break;
    case ISD::SETOLE:
    case ISD::SETLE:
      Opcode = AMDGPU::V_CMPX_GE_F32_e64;
      break;
    case ISD::SETONE:
    case ISD::SETNE:
      Opcode = AMDGPU::V_CMPX_LG_F32_e64;
      break;
    case ISD::SETO:
      Opcode = AMDGPU::V_CMPX_O_F32_e64;
      break;
    case ISD::SETUO:
      Opcode = AMDGPU::V_CMPX_U_F32_e64;
      break;
    case ISD::SETUEQ:
      Opcode = AMDGPU::V_CMPX_NLG_F32_e64;
      break;
    case ISD::SETUGT:
      Opcode = AMDGPU::V_CMPX_NGE_F32_e64;
      break;
    case ISD::SETUGE:
      Opcode = AMDGPU::V_CMPX_NGT_F32_e64;
      break;
    case ISD::SETULT:
      Opcode = AMDGPU::V_CMPX_NLE_F32_e64;
      break;
    case ISD::SETULE:
      Opcode = AMDGPU::V_CMPX_NLT_F32_e64;
      break;
    case ISD::SETUNE:
      Opcode = AMDGPU::V_CMPX_NEQ_F32_e64;
      break;
    default:
      llvm_unreachable("invalid ISD:SET cond code");
    }

    assert(MI.getOperand(0).isReg());

    if (TRI->isVGPR(MBB.getParent()->getRegInfo(),
                    MI.getOperand(0).getReg())) {
      Opcode = AMDGPU::getVOPe32(Opcode);
      BuildMI(MBB, &MI, DL, TII->get(Opcode))
          .add(MI.getOperand(1))
          .add(MI.getOperand(0));
    } else {
      BuildMI(MBB, &MI, DL, TII->get(Opcode))
          .addReg(AMDGPU::VCC, RegState::Define)
          .addImm(0)  // src0 modifiers
          .add(MI.getOperand(1))
          .addImm(0)  // src1 modifiers
          .add(MI.getOperand(0))
          .addImm(0);  // omod
    }
    break;
  }
  case AMDGPU::SI_KILL_I1_TERMINATOR: {
    const MachineOperand &Op = MI.getOperand(0);
    int64_t KillVal = MI.getOperand(1).getImm();
    assert(KillVal == 0 || KillVal == -1);

    // Kill all threads if Op0 is an immediate and equal to the Kill value.
    if (Op.isImm()) {
      int64_t Imm = Op.getImm();
      assert(Imm == 0 || Imm == -1);

      if (Imm == KillVal)
        BuildMI(MBB, &MI, DL, TII->get(AMDGPU::S_MOV_B64), AMDGPU::EXEC)
          .addImm(0);
      break;
    }

    unsigned Opcode = KillVal ? AMDGPU::S_ANDN2_B64 : AMDGPU::S_AND_B64;
    BuildMI(MBB, &MI, DL, TII->get(Opcode), AMDGPU::EXEC)
        .addReg(AMDGPU::EXEC)
        .add(Op);
    break;
  }
  default:
    llvm_unreachable("invalid opcode, expected SI_KILL_*_TERMINATOR");
  }
}

MachineBasicBlock *SIInsertSkips::insertSkipBlock(
  MachineBasicBlock &MBB, MachineBasicBlock::iterator I) const {
  MachineFunction *MF = MBB.getParent();

  MachineBasicBlock *SkipBB = MF->CreateMachineBasicBlock();
  MachineFunction::iterator MBBI(MBB);
  ++MBBI;

  MF->insert(MBBI, SkipBB);
  MBB.addSuccessor(SkipBB);

  return SkipBB;
}

// Returns true if a branch over the block was inserted.
bool SIInsertSkips::skipMaskBranch(MachineInstr &MI,
                                   MachineBasicBlock &SrcMBB) {
  MachineBasicBlock *DestBB = MI.getOperand(0).getMBB();

  if (!shouldSkip(**SrcMBB.succ_begin(), *DestBB))
    return false;

  const DebugLoc &DL = MI.getDebugLoc();
  MachineBasicBlock::iterator InsPt = std::next(MI.getIterator());

  BuildMI(SrcMBB, InsPt, DL, TII->get(AMDGPU::S_CBRANCH_EXECZ))
    .addMBB(DestBB);

  return true;
}

bool SIInsertSkips::optimizeVccBranch(MachineInstr &MI) const {
  // Match:
  // sreg = -1
  // vcc = S_AND_B64 exec, sreg
  // S_CBRANCH_VCC[N]Z
  // =>
  // S_CBRANCH_EXEC[N]Z
  bool Changed = false;
  MachineBasicBlock &MBB = *MI.getParent();
  const unsigned CondReg = AMDGPU::VCC;
  const unsigned ExecReg = AMDGPU::EXEC;
  const unsigned And = AMDGPU::S_AND_B64;

  MachineBasicBlock::reverse_iterator A = MI.getReverseIterator(),
                                      E = MBB.rend();
  bool ReadsCond = false;
  unsigned Threshold = 5;
  for (++A ; A != E ; ++A) {
    if (!--Threshold)
      return false;
    if (A->modifiesRegister(ExecReg, TRI))
      return false;
    if (A->modifiesRegister(CondReg, TRI)) {
      if (!A->definesRegister(CondReg, TRI) || A->getOpcode() != And)
        return false;
      break;
    }
    ReadsCond |= A->readsRegister(CondReg, TRI);
  }
  if (A == E)
    return false;

  MachineOperand &Op1 = A->getOperand(1);
  MachineOperand &Op2 = A->getOperand(2);
  if (Op1.getReg() != ExecReg && Op2.isReg() && Op2.getReg() == ExecReg) {
    TII->commuteInstruction(*A);
    Changed = true;
  }
  if (Op1.getReg() != ExecReg)
    return Changed;
  if (Op2.isImm() && Op2.getImm() != -1)
    return Changed;

  unsigned SReg = AMDGPU::NoRegister;
  if (Op2.isReg()) {
    SReg = Op2.getReg();
    auto M = std::next(A);
    bool ReadsSreg = false;
    for ( ; M != E ; ++M) {
      if (M->definesRegister(SReg, TRI))
        break;
      if (M->modifiesRegister(SReg, TRI))
        return Changed;
      ReadsSreg |= M->readsRegister(SReg, TRI);
    }
    if (M == E ||
        !M->isMoveImmediate() ||
        !M->getOperand(1).isImm() ||
        M->getOperand(1).getImm() != -1)
      return Changed;
    // First if sreg is only used in and instruction fold the immediate
    // into that and.
    if (!ReadsSreg && Op2.isKill()) {
      A->getOperand(2).ChangeToImmediate(-1);
      M->eraseFromParent();
    }
  }

  if (!ReadsCond && A->registerDefIsDead(AMDGPU::SCC) &&
      MI.killsRegister(CondReg, TRI))
    A->eraseFromParent();

  bool IsVCCZ = MI.getOpcode() == AMDGPU::S_CBRANCH_VCCZ;
  if (SReg == ExecReg) {
    if (IsVCCZ) {
      MI.eraseFromParent();
      return true;
    }
    MI.setDesc(TII->get(AMDGPU::S_BRANCH));
  } else {
    MI.setDesc(TII->get(IsVCCZ ? AMDGPU::S_CBRANCH_EXECZ
                               : AMDGPU::S_CBRANCH_EXECNZ));
  }

  MI.RemoveOperand(MI.findRegisterUseOperandIdx(CondReg, false /*Kill*/, TRI));
  MI.addImplicitDefUseOperands(*MBB.getParent());

  return true;
}

bool SIInsertSkips::runOnMachineFunction(MachineFunction &MF) {
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  TII = ST.getInstrInfo();
  TRI = &TII->getRegisterInfo();
  SkipThreshold = SkipThresholdFlag;

  bool HaveKill = false;
  bool MadeChange = false;

  // Track depth of exec mask, divergent branches.
  SmallVector<MachineBasicBlock *, 16> ExecBranchStack;

  MachineFunction::iterator NextBB;

  MachineBasicBlock *EmptyMBBAtEnd = nullptr;

  for (MachineFunction::iterator BI = MF.begin(), BE = MF.end();
       BI != BE; BI = NextBB) {
    NextBB = std::next(BI);
    MachineBasicBlock &MBB = *BI;
    bool HaveSkipBlock = false;

    if (!ExecBranchStack.empty() && ExecBranchStack.back() == &MBB) {
      // Reached convergence point for last divergent branch.
      ExecBranchStack.pop_back();
    }

    if (HaveKill && ExecBranchStack.empty()) {
      HaveKill = false;

      // TODO: Insert skip if exec is 0?
    }

    MachineBasicBlock::iterator I, Next;
    for (I = MBB.begin(); I != MBB.end(); I = Next) {
      Next = std::next(I);

      MachineInstr &MI = *I;

      switch (MI.getOpcode()) {
      case AMDGPU::SI_MASK_BRANCH:
        ExecBranchStack.push_back(MI.getOperand(0).getMBB());
        MadeChange |= skipMaskBranch(MI, MBB);
        break;

      case AMDGPU::S_BRANCH:
        // Optimize out branches to the next block.
        // FIXME: Shouldn't this be handled by BranchFolding?
        if (MBB.isLayoutSuccessor(MI.getOperand(0).getMBB())) {
          MI.eraseFromParent();
        } else if (HaveSkipBlock) {
          // Remove the given unconditional branch when a skip block has been
          // inserted after the current one and let skip the two instructions
          // performing the kill if the exec mask is non-zero.
          MI.eraseFromParent();
        }
        break;

      case AMDGPU::SI_KILL_F32_COND_IMM_TERMINATOR:
      case AMDGPU::SI_KILL_I1_TERMINATOR:
        MadeChange = true;
        kill(MI);

        if (ExecBranchStack.empty()) {
          if (NextBB != BE && skipIfDead(MI, *NextBB)) {
            HaveSkipBlock = true;
            NextBB = std::next(BI);
            BE = MF.end();
          }
        } else {
          HaveKill = true;
        }

        MI.eraseFromParent();
        break;

      case AMDGPU::SI_RETURN_TO_EPILOG:
        // FIXME: Should move somewhere else
        assert(!MF.getInfo<SIMachineFunctionInfo>()->returnsVoid());

        // Graphics shaders returning non-void shouldn't contain S_ENDPGM,
        // because external bytecode will be appended at the end.
        if (BI != --MF.end() || I != MBB.getFirstTerminator()) {
          // SI_RETURN_TO_EPILOG is not the last instruction. Add an empty block at
          // the end and jump there.
          if (!EmptyMBBAtEnd) {
            EmptyMBBAtEnd = MF.CreateMachineBasicBlock();
            MF.insert(MF.end(), EmptyMBBAtEnd);
          }

          MBB.addSuccessor(EmptyMBBAtEnd);
          BuildMI(*BI, I, MI.getDebugLoc(), TII->get(AMDGPU::S_BRANCH))
            .addMBB(EmptyMBBAtEnd);
          I->eraseFromParent();
        }
        break;

      case AMDGPU::S_CBRANCH_VCCZ:
      case AMDGPU::S_CBRANCH_VCCNZ:
        MadeChange |= optimizeVccBranch(MI);
        break;

      default:
        break;
      }
    }
  }

  return MadeChange;
}
