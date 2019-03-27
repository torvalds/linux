//===- MacroFusion.h - Macro Fusion -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file This file contains the definition of the DAG scheduling mutation to
/// pair instructions back to back.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACROFUSION_H
#define LLVM_CODEGEN_MACROFUSION_H

#include <functional>
#include <memory>

namespace llvm {

class MachineInstr;
class ScheduleDAGMutation;
class TargetInstrInfo;
class TargetSubtargetInfo;

/// Check if the instr pair, FirstMI and SecondMI, should be fused
/// together. Given SecondMI, when FirstMI is unspecified, then check if
/// SecondMI may be part of a fused pair at all.
using ShouldSchedulePredTy = std::function<bool(const TargetInstrInfo &TII,
                                                const TargetSubtargetInfo &TSI,
                                                const MachineInstr *FirstMI,
                                                const MachineInstr &SecondMI)>;

/// Create a DAG scheduling mutation to pair instructions back to back
/// for instructions that benefit according to the target-specific
/// shouldScheduleAdjacent predicate function.
std::unique_ptr<ScheduleDAGMutation>
createMacroFusionDAGMutation(ShouldSchedulePredTy shouldScheduleAdjacent);

/// Create a DAG scheduling mutation to pair branch instructions with one
/// of their predecessors back to back for instructions that benefit according
/// to the target-specific shouldScheduleAdjacent predicate function.
std::unique_ptr<ScheduleDAGMutation>
createBranchMacroFusionDAGMutation(ShouldSchedulePredTy shouldScheduleAdjacent);

} // end namespace llvm

#endif // LLVM_CODEGEN_MACROFUSION_H
