//===--- AMDGPUMacroFusion.cpp - AMDGPU Macro Fusion ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file This file contains the AMDGPU implementation of the DAG scheduling
///  mutation to pair instructions back to back.
//
//===----------------------------------------------------------------------===//

#include "AMDGPUMacroFusion.h"
#include "AMDGPUSubtarget.h"
#include "SIInstrInfo.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"

#include "llvm/CodeGen/MacroFusion.h"

using namespace llvm;

namespace {

/// Check if the instr pair, FirstMI and SecondMI, should be fused
/// together. Given SecondMI, when FirstMI is unspecified, then check if
/// SecondMI may be part of a fused pair at all.
static bool shouldScheduleAdjacent(const TargetInstrInfo &TII_,
                                   const TargetSubtargetInfo &TSI,
                                   const MachineInstr *FirstMI,
                                   const MachineInstr &SecondMI) {
  const SIInstrInfo &TII = static_cast<const SIInstrInfo&>(TII_);

  switch (SecondMI.getOpcode()) {
  case AMDGPU::V_ADDC_U32_e64:
  case AMDGPU::V_SUBB_U32_e64:
  case AMDGPU::V_CNDMASK_B32_e64: {
    // Try to cluster defs of condition registers to their uses. This improves
    // the chance VCC will be available which will allow shrinking to VOP2
    // encodings.
    if (!FirstMI)
      return true;

    const MachineBasicBlock &MBB = *FirstMI->getParent();
    const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
    const TargetRegisterInfo *TRI = MRI.getTargetRegisterInfo();
    const MachineOperand *Src2 = TII.getNamedOperand(SecondMI,
                                                     AMDGPU::OpName::src2);
    return FirstMI->definesRegister(Src2->getReg(), TRI);
  }
  default:
    return false;
  }

  return false;
}

} // end namespace


namespace llvm {

std::unique_ptr<ScheduleDAGMutation> createAMDGPUMacroFusionDAGMutation () {
  return createMacroFusionDAGMutation(shouldScheduleAdjacent);
}

} // end namespace llvm
