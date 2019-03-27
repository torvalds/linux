//===- AMDGPUMacroFusion.h - AMDGPU Macro Fusion ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineScheduler.h"

namespace llvm {

/// Note that you have to add:
///   DAG.addMutation(createAMDGPUMacroFusionDAGMutation());
/// to AMDGPUPassConfig::createMachineScheduler() to have an effect.
std::unique_ptr<ScheduleDAGMutation> createAMDGPUMacroFusionDAGMutation();

} // llvm
