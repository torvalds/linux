//===-- SIAddIMGInit.cpp - Add any required IMG inits ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Any MIMG instructions that use tfe or lwe require an initialization of the
/// result register that will be written in the case of a memory access failure
/// The required code is also added to tie this init code to the result of the
/// img instruction
///
//===----------------------------------------------------------------------===//
//

#include "AMDGPU.h"
#include "AMDGPUSubtarget.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "SIInstrInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/TargetMachine.h"

#define DEBUG_TYPE "si-img-init"

using namespace llvm;

namespace {

class SIAddIMGInit : public MachineFunctionPass {
public:
  static char ID;

public:
  SIAddIMGInit() : MachineFunctionPass(ID) {
    initializeSIAddIMGInitPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // End anonymous namespace.

INITIALIZE_PASS(SIAddIMGInit, DEBUG_TYPE, "SI Add IMG Init", false, false)

char SIAddIMGInit::ID = 0;

char &llvm::SIAddIMGInitID = SIAddIMGInit::ID;

FunctionPass *llvm::createSIAddIMGInitPass() { return new SIAddIMGInit(); }

bool SIAddIMGInit::runOnMachineFunction(MachineFunction &MF) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo *RI = ST.getRegisterInfo();
  bool Changed = false;

  for (MachineFunction::iterator BI = MF.begin(), BE = MF.end(); BI != BE;
       ++BI) {
    MachineBasicBlock &MBB = *BI;
    MachineBasicBlock::iterator I, Next;
    for (I = MBB.begin(); I != MBB.end(); I = Next) {
      Next = std::next(I);
      MachineInstr &MI = *I;

      auto Opcode = MI.getOpcode();
      if (TII->isMIMG(Opcode) && !MI.mayStore()) {
        MachineOperand *TFE = TII->getNamedOperand(MI, AMDGPU::OpName::tfe);
        MachineOperand *LWE = TII->getNamedOperand(MI, AMDGPU::OpName::lwe);
        MachineOperand *D16 = TII->getNamedOperand(MI, AMDGPU::OpName::d16);

        // Check for instructions that don't have tfe or lwe fields
        // There shouldn't be any at this point.
        assert( (TFE && LWE) && "Expected tfe and lwe operands in instruction");

        unsigned TFEVal = TFE->getImm();
        unsigned LWEVal = LWE->getImm();
        unsigned D16Val = D16 ? D16->getImm() : 0;

        if (TFEVal || LWEVal) {
          // At least one of TFE or LWE are non-zero
          // We have to insert a suitable initialization of the result value and
          // tie this to the dest of the image instruction.

          const DebugLoc &DL = MI.getDebugLoc();

          int DstIdx =
              AMDGPU::getNamedOperandIdx(MI.getOpcode(), AMDGPU::OpName::vdata);

          // Calculate which dword we have to initialize to 0.
          MachineOperand *MO_Dmask =
              TII->getNamedOperand(MI, AMDGPU::OpName::dmask);

          // check that dmask operand is found.
          assert(MO_Dmask && "Expected dmask operand in instruction");

          unsigned dmask = MO_Dmask->getImm();
          // Determine the number of active lanes taking into account the
          // Gather4 special case
          unsigned ActiveLanes =
              TII->isGather4(Opcode) ? 4 : countPopulation(dmask);

          // Subreg indices are counted from 1
          // When D16 then we want next whole VGPR after write data.
          static_assert(AMDGPU::sub0 == 1 && AMDGPU::sub4 == 5, "Subreg indices different from expected");

          bool Packed = !ST.hasUnpackedD16VMem();

          unsigned InitIdx =
              D16Val && Packed ? ((ActiveLanes + 1) >> 1) + 1 : ActiveLanes + 1;

          // Abandon attempt if the dst size isn't large enough
          // - this is in fact an error but this is picked up elsewhere and
          // reported correctly.
          uint32_t DstSize =
              RI->getRegSizeInBits(*TII->getOpRegClass(MI, DstIdx)) / 32;
          if (DstSize < InitIdx)
            continue;

          // Create a register for the intialization value.
          unsigned PrevDst =
              MRI.createVirtualRegister(TII->getOpRegClass(MI, DstIdx));
          unsigned NewDst = 0; // Final initialized value will be in here

          // If PRTStrictNull feature is enabled (the default) then initialize
          // all the result registers to 0, otherwise just the error indication
          // register (VGPRn+1)
          unsigned SizeLeft = ST.usePRTStrictNull() ? InitIdx : 1;
          unsigned CurrIdx = ST.usePRTStrictNull() ? 1 : InitIdx;

          if (DstSize == 1) {
            // In this case we can just initialize the result directly
            BuildMI(MBB, MI, DL, TII->get(AMDGPU::V_MOV_B32_e32), PrevDst)
                .addImm(0);
            NewDst = PrevDst;
          } else {
            BuildMI(MBB, MI, DL, TII->get(AMDGPU::IMPLICIT_DEF), PrevDst);
            for (; SizeLeft; SizeLeft--, CurrIdx++) {
              NewDst =
                  MRI.createVirtualRegister(TII->getOpRegClass(MI, DstIdx));
              // Initialize dword
              unsigned SubReg =
                  MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
              BuildMI(MBB, MI, DL, TII->get(AMDGPU::V_MOV_B32_e32), SubReg)
                  .addImm(0);
              // Insert into the super-reg
              BuildMI(MBB, I, DL, TII->get(TargetOpcode::INSERT_SUBREG), NewDst)
                  .addReg(PrevDst)
                  .addReg(SubReg)
                  .addImm(CurrIdx);

              PrevDst = NewDst;
            }
          }

          // Add as an implicit operand
          MachineInstrBuilder(MF, MI).addReg(NewDst, RegState::Implicit);

          // Tie the just added implicit operand to the dst
          MI.tieOperands(DstIdx, MI.getNumOperands() - 1);

          Changed = true;
        }
      }
    }
  }

  return Changed;
}
