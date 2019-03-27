//===- MacroFusion.cpp - Macro Fusion -------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file This file contains the implementation of the DAG scheduling mutation
/// to pair instructions back to back.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MacroFusion.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/ScheduleDAGMutation.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "machine-scheduler"

STATISTIC(NumFused, "Number of instr pairs fused");

using namespace llvm;

static cl::opt<bool> EnableMacroFusion("misched-fusion", cl::Hidden,
  cl::desc("Enable scheduling for macro fusion."), cl::init(true));

static bool isHazard(const SDep &Dep) {
  return Dep.getKind() == SDep::Anti || Dep.getKind() == SDep::Output;
}

static bool fuseInstructionPair(ScheduleDAGMI &DAG, SUnit &FirstSU,
                                SUnit &SecondSU) {
  // Check that neither instr is already paired with another along the edge
  // between them.
  for (SDep &SI : FirstSU.Succs)
    if (SI.isCluster())
      return false;

  for (SDep &SI : SecondSU.Preds)
    if (SI.isCluster())
      return false;
  // Though the reachability checks above could be made more generic,
  // perhaps as part of ScheduleDAGMI::addEdge(), since such edges are valid,
  // the extra computation cost makes it less interesting in general cases.

  // Create a single weak edge between the adjacent instrs. The only effect is
  // to cause bottom-up scheduling to heavily prioritize the clustered instrs.
  if (!DAG.addEdge(&SecondSU, SDep(&FirstSU, SDep::Cluster)))
    return false;

  // Adjust the latency between both instrs.
  for (SDep &SI : FirstSU.Succs)
    if (SI.getSUnit() == &SecondSU)
      SI.setLatency(0);

  for (SDep &SI : SecondSU.Preds)
    if (SI.getSUnit() == &FirstSU)
      SI.setLatency(0);

  LLVM_DEBUG(
      dbgs() << "Macro fuse: "; DAG.dumpNodeName(FirstSU); dbgs() << " - ";
      DAG.dumpNodeName(SecondSU); dbgs() << " /  ";
      dbgs() << DAG.TII->getName(FirstSU.getInstr()->getOpcode()) << " - "
             << DAG.TII->getName(SecondSU.getInstr()->getOpcode()) << '\n';);

  // Make data dependencies from the FirstSU also dependent on the SecondSU to
  // prevent them from being scheduled between the FirstSU and the SecondSU.
  if (&SecondSU != &DAG.ExitSU)
    for (const SDep &SI : FirstSU.Succs) {
      SUnit *SU = SI.getSUnit();
      if (SI.isWeak() || isHazard(SI) ||
          SU == &DAG.ExitSU || SU == &SecondSU || SU->isPred(&SecondSU))
        continue;
      LLVM_DEBUG(dbgs() << "  Bind "; DAG.dumpNodeName(SecondSU);
                 dbgs() << " - "; DAG.dumpNodeName(*SU); dbgs() << '\n';);
      DAG.addEdge(SU, SDep(&SecondSU, SDep::Artificial));
    }

  // Make the FirstSU also dependent on the dependencies of the SecondSU to
  // prevent them from being scheduled between the FirstSU and the SecondSU.
  if (&FirstSU != &DAG.EntrySU) {
    for (const SDep &SI : SecondSU.Preds) {
      SUnit *SU = SI.getSUnit();
      if (SI.isWeak() || isHazard(SI) || &FirstSU == SU || FirstSU.isSucc(SU))
        continue;
      LLVM_DEBUG(dbgs() << "  Bind "; DAG.dumpNodeName(*SU); dbgs() << " - ";
                 DAG.dumpNodeName(FirstSU); dbgs() << '\n';);
      DAG.addEdge(&FirstSU, SDep(SU, SDep::Artificial));
    }
    // ExitSU comes last by design, which acts like an implicit dependency
    // between ExitSU and any bottom root in the graph. We should transfer
    // this to FirstSU as well.
    if (&SecondSU == &DAG.ExitSU) {
      for (SUnit &SU : DAG.SUnits) {
        if (SU.Succs.empty())
          DAG.addEdge(&FirstSU, SDep(&SU, SDep::Artificial));
      }
    }
  }

  ++NumFused;
  return true;
}

namespace {

/// Post-process the DAG to create cluster edges between instrs that may
/// be fused by the processor into a single operation.
class MacroFusion : public ScheduleDAGMutation {
  ShouldSchedulePredTy shouldScheduleAdjacent;
  bool FuseBlock;
  bool scheduleAdjacentImpl(ScheduleDAGMI &DAG, SUnit &AnchorSU);

public:
  MacroFusion(ShouldSchedulePredTy shouldScheduleAdjacent, bool FuseBlock)
    : shouldScheduleAdjacent(shouldScheduleAdjacent), FuseBlock(FuseBlock) {}

  void apply(ScheduleDAGInstrs *DAGInstrs) override;
};

} // end anonymous namespace

void MacroFusion::apply(ScheduleDAGInstrs *DAGInstrs) {
  ScheduleDAGMI *DAG = static_cast<ScheduleDAGMI*>(DAGInstrs);

  if (FuseBlock)
    // For each of the SUnits in the scheduling block, try to fuse the instr in
    // it with one in its predecessors.
    for (SUnit &ISU : DAG->SUnits)
        scheduleAdjacentImpl(*DAG, ISU);

  if (DAG->ExitSU.getInstr())
    // Try to fuse the instr in the ExitSU with one in its predecessors.
    scheduleAdjacentImpl(*DAG, DAG->ExitSU);
}

/// Implement the fusion of instr pairs in the scheduling DAG,
/// anchored at the instr in AnchorSU..
bool MacroFusion::scheduleAdjacentImpl(ScheduleDAGMI &DAG, SUnit &AnchorSU) {
  const MachineInstr &AnchorMI = *AnchorSU.getInstr();
  const TargetInstrInfo &TII = *DAG.TII;
  const TargetSubtargetInfo &ST = DAG.MF.getSubtarget();

  // Check if the anchor instr may be fused.
  if (!shouldScheduleAdjacent(TII, ST, nullptr, AnchorMI))
    return false;

  // Explorer for fusion candidates among the dependencies of the anchor instr.
  for (SDep &Dep : AnchorSU.Preds) {
    // Ignore dependencies other than data or strong ordering.
    if (Dep.isWeak() || isHazard(Dep))
      continue;

    SUnit &DepSU = *Dep.getSUnit();
    if (DepSU.isBoundaryNode())
      continue;

    const MachineInstr *DepMI = DepSU.getInstr();
    if (!shouldScheduleAdjacent(TII, ST, DepMI, AnchorMI))
      continue;

    if (fuseInstructionPair(DAG, DepSU, AnchorSU))
      return true;
  }

  return false;
}

std::unique_ptr<ScheduleDAGMutation>
llvm::createMacroFusionDAGMutation(
     ShouldSchedulePredTy shouldScheduleAdjacent) {
  if(EnableMacroFusion)
    return llvm::make_unique<MacroFusion>(shouldScheduleAdjacent, true);
  return nullptr;
}

std::unique_ptr<ScheduleDAGMutation>
llvm::createBranchMacroFusionDAGMutation(
     ShouldSchedulePredTy shouldScheduleAdjacent) {
  if(EnableMacroFusion)
    return llvm::make_unique<MacroFusion>(shouldScheduleAdjacent, false);
  return nullptr;
}
