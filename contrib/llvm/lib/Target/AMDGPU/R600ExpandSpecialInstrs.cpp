//===- R600ExpandSpecialInstrs.cpp - Expand special instructions ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Vector, Reduction, and Cube instructions need to fill the entire instruction
/// group to work correctly.  This pass expands these individual instructions
/// into several instructions that will completely fill the instruction group.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUSubtarget.h"
#include "R600Defines.h"
#include "R600InstrInfo.h"
#include "R600RegisterInfo.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/Pass.h"
#include <cassert>
#include <cstdint>
#include <iterator>

using namespace llvm;

#define DEBUG_TYPE "r600-expand-special-instrs"

namespace {

class R600ExpandSpecialInstrsPass : public MachineFunctionPass {
private:
  const R600InstrInfo *TII = nullptr;

  void SetFlagInNewMI(MachineInstr *NewMI, const MachineInstr *OldMI,
      unsigned Op);

public:
  static char ID;

  R600ExpandSpecialInstrsPass() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "R600 Expand special instructions pass";
  }
};

} // end anonymous namespace

INITIALIZE_PASS_BEGIN(R600ExpandSpecialInstrsPass, DEBUG_TYPE,
                     "R600 Expand Special Instrs", false, false)
INITIALIZE_PASS_END(R600ExpandSpecialInstrsPass, DEBUG_TYPE,
                    "R600ExpandSpecialInstrs", false, false)

char R600ExpandSpecialInstrsPass::ID = 0;

char &llvm::R600ExpandSpecialInstrsPassID = R600ExpandSpecialInstrsPass::ID;

FunctionPass *llvm::createR600ExpandSpecialInstrsPass() {
  return new R600ExpandSpecialInstrsPass();
}

void R600ExpandSpecialInstrsPass::SetFlagInNewMI(MachineInstr *NewMI,
    const MachineInstr *OldMI, unsigned Op) {
  int OpIdx = TII->getOperandIdx(*OldMI, Op);
  if (OpIdx > -1) {
    uint64_t Val = OldMI->getOperand(OpIdx).getImm();
    TII->setImmOperand(*NewMI, Op, Val);
  }
}

bool R600ExpandSpecialInstrsPass::runOnMachineFunction(MachineFunction &MF) {
  const R600Subtarget &ST = MF.getSubtarget<R600Subtarget>();
  TII = ST.getInstrInfo();

  const R600RegisterInfo &TRI = TII->getRegisterInfo();

  for (MachineFunction::iterator BB = MF.begin(), BB_E = MF.end();
                                                  BB != BB_E; ++BB) {
    MachineBasicBlock &MBB = *BB;
    MachineBasicBlock::iterator I = MBB.begin();
    while (I != MBB.end()) {
      MachineInstr &MI = *I;
      I = std::next(I);

      // Expand LDS_*_RET instructions
      if (TII->isLDSRetInstr(MI.getOpcode())) {
        int DstIdx = TII->getOperandIdx(MI.getOpcode(), R600::OpName::dst);
        assert(DstIdx != -1);
        MachineOperand &DstOp = MI.getOperand(DstIdx);
        MachineInstr *Mov = TII->buildMovInstr(&MBB, I,
                                               DstOp.getReg(), R600::OQAP);
        DstOp.setReg(R600::OQAP);
        int LDSPredSelIdx = TII->getOperandIdx(MI.getOpcode(),
                                           R600::OpName::pred_sel);
        int MovPredSelIdx = TII->getOperandIdx(Mov->getOpcode(),
                                           R600::OpName::pred_sel);
        // Copy the pred_sel bit
        Mov->getOperand(MovPredSelIdx).setReg(
            MI.getOperand(LDSPredSelIdx).getReg());
      }

      switch (MI.getOpcode()) {
      default: break;
      // Expand PRED_X to one of the PRED_SET instructions.
      case R600::PRED_X: {
        uint64_t Flags = MI.getOperand(3).getImm();
        // The native opcode used by PRED_X is stored as an immediate in the
        // third operand.
        MachineInstr *PredSet = TII->buildDefaultInstruction(MBB, I,
                                            MI.getOperand(2).getImm(), // opcode
                                            MI.getOperand(0).getReg(), // dst
                                            MI.getOperand(1).getReg(), // src0
                                            R600::ZERO);             // src1
        TII->addFlag(*PredSet, 0, MO_FLAG_MASK);
        if (Flags & MO_FLAG_PUSH) {
          TII->setImmOperand(*PredSet, R600::OpName::update_exec_mask, 1);
        } else {
          TII->setImmOperand(*PredSet, R600::OpName::update_pred, 1);
        }
        MI.eraseFromParent();
        continue;
        }
      case R600::DOT_4: {

        const R600RegisterInfo &TRI = TII->getRegisterInfo();

        unsigned DstReg = MI.getOperand(0).getReg();
        unsigned DstBase = TRI.getEncodingValue(DstReg) & HW_REG_MASK;

        for (unsigned Chan = 0; Chan < 4; ++Chan) {
          bool Mask = (Chan != TRI.getHWRegChan(DstReg));
          unsigned SubDstReg =
              R600::R600_TReg32RegClass.getRegister((DstBase * 4) + Chan);
          MachineInstr *BMI =
              TII->buildSlotOfVectorInstruction(MBB, &MI, Chan, SubDstReg);
          if (Chan > 0) {
            BMI->bundleWithPred();
          }
          if (Mask) {
            TII->addFlag(*BMI, 0, MO_FLAG_MASK);
          }
          if (Chan != 3)
            TII->addFlag(*BMI, 0, MO_FLAG_NOT_LAST);
          unsigned Opcode = BMI->getOpcode();
          // While not strictly necessary from hw point of view, we force
          // all src operands of a dot4 inst to belong to the same slot.
          unsigned Src0 = BMI->getOperand(
              TII->getOperandIdx(Opcode, R600::OpName::src0))
              .getReg();
          unsigned Src1 = BMI->getOperand(
              TII->getOperandIdx(Opcode, R600::OpName::src1))
              .getReg();
          (void) Src0;
          (void) Src1;
          if ((TRI.getEncodingValue(Src0) & 0xff) < 127 &&
              (TRI.getEncodingValue(Src1) & 0xff) < 127)
            assert(TRI.getHWRegChan(Src0) == TRI.getHWRegChan(Src1));
        }
        MI.eraseFromParent();
        continue;
      }
      }

      bool IsReduction = TII->isReductionOp(MI.getOpcode());
      bool IsVector = TII->isVector(MI);
      bool IsCube = TII->isCubeOp(MI.getOpcode());
      if (!IsReduction && !IsVector && !IsCube) {
        continue;
      }

      // Expand the instruction
      //
      // Reduction instructions:
      // T0_X = DP4 T1_XYZW, T2_XYZW
      // becomes:
      // TO_X = DP4 T1_X, T2_X
      // TO_Y (write masked) = DP4 T1_Y, T2_Y
      // TO_Z (write masked) = DP4 T1_Z, T2_Z
      // TO_W (write masked) = DP4 T1_W, T2_W
      //
      // Vector instructions:
      // T0_X = MULLO_INT T1_X, T2_X
      // becomes:
      // T0_X = MULLO_INT T1_X, T2_X
      // T0_Y (write masked) = MULLO_INT T1_X, T2_X
      // T0_Z (write masked) = MULLO_INT T1_X, T2_X
      // T0_W (write masked) = MULLO_INT T1_X, T2_X
      //
      // Cube instructions:
      // T0_XYZW = CUBE T1_XYZW
      // becomes:
      // TO_X = CUBE T1_Z, T1_Y
      // T0_Y = CUBE T1_Z, T1_X
      // T0_Z = CUBE T1_X, T1_Z
      // T0_W = CUBE T1_Y, T1_Z
      for (unsigned Chan = 0; Chan < 4; Chan++) {
        unsigned DstReg = MI.getOperand(
                            TII->getOperandIdx(MI, R600::OpName::dst)).getReg();
        unsigned Src0 = MI.getOperand(
                           TII->getOperandIdx(MI, R600::OpName::src0)).getReg();
        unsigned Src1 = 0;

        // Determine the correct source registers
        if (!IsCube) {
          int Src1Idx = TII->getOperandIdx(MI, R600::OpName::src1);
          if (Src1Idx != -1) {
            Src1 = MI.getOperand(Src1Idx).getReg();
          }
        }
        if (IsReduction) {
          unsigned SubRegIndex = AMDGPURegisterInfo::getSubRegFromChannel(Chan);
          Src0 = TRI.getSubReg(Src0, SubRegIndex);
          Src1 = TRI.getSubReg(Src1, SubRegIndex);
        } else if (IsCube) {
          static const int CubeSrcSwz[] = {2, 2, 0, 1};
          unsigned SubRegIndex0 = AMDGPURegisterInfo::getSubRegFromChannel(CubeSrcSwz[Chan]);
          unsigned SubRegIndex1 = AMDGPURegisterInfo::getSubRegFromChannel(CubeSrcSwz[3 - Chan]);
          Src1 = TRI.getSubReg(Src0, SubRegIndex1);
          Src0 = TRI.getSubReg(Src0, SubRegIndex0);
        }

        // Determine the correct destination registers;
        bool Mask = false;
        bool NotLast = true;
        if (IsCube) {
          unsigned SubRegIndex = AMDGPURegisterInfo::getSubRegFromChannel(Chan);
          DstReg = TRI.getSubReg(DstReg, SubRegIndex);
        } else {
          // Mask the write if the original instruction does not write to
          // the current Channel.
          Mask = (Chan != TRI.getHWRegChan(DstReg));
          unsigned DstBase = TRI.getEncodingValue(DstReg) & HW_REG_MASK;
          DstReg = R600::R600_TReg32RegClass.getRegister((DstBase * 4) + Chan);
        }

        // Set the IsLast bit
        NotLast = (Chan != 3 );

        // Add the new instruction
        unsigned Opcode = MI.getOpcode();
        switch (Opcode) {
        case R600::CUBE_r600_pseudo:
          Opcode = R600::CUBE_r600_real;
          break;
        case R600::CUBE_eg_pseudo:
          Opcode = R600::CUBE_eg_real;
          break;
        default:
          break;
        }

        MachineInstr *NewMI =
          TII->buildDefaultInstruction(MBB, I, Opcode, DstReg, Src0, Src1);

        if (Chan != 0)
          NewMI->bundleWithPred();
        if (Mask) {
          TII->addFlag(*NewMI, 0, MO_FLAG_MASK);
        }
        if (NotLast) {
          TII->addFlag(*NewMI, 0, MO_FLAG_NOT_LAST);
        }
        SetFlagInNewMI(NewMI, &MI, R600::OpName::clamp);
        SetFlagInNewMI(NewMI, &MI, R600::OpName::literal);
        SetFlagInNewMI(NewMI, &MI, R600::OpName::src0_abs);
        SetFlagInNewMI(NewMI, &MI, R600::OpName::src1_abs);
        SetFlagInNewMI(NewMI, &MI, R600::OpName::src0_neg);
        SetFlagInNewMI(NewMI, &MI, R600::OpName::src1_neg);
      }
      MI.eraseFromParent();
    }
  }
  return false;
}
