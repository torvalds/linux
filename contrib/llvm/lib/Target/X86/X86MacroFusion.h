//===- X86MacroFusion.h - X86 Macro Fusion --------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file This file contains the X86 definition of the DAG scheduling mutation
///  to pair instructions back to back.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86MACROFUSION_H
#define LLVM_LIB_TARGET_X86_X86MACROFUSION_H

#include "llvm/CodeGen/MachineScheduler.h"

namespace llvm {

/// Note that you have to add:
///   DAG.addMutation(createX86MacroFusionDAGMutation());
/// to X86PassConfig::createMachineScheduler() to have an effect.
std::unique_ptr<ScheduleDAGMutation>
createX86MacroFusionDAGMutation();

} // end namespace llvm

#endif
