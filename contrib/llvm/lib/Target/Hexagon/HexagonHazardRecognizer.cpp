//===-- HexagonHazardRecognizer.cpp - Hexagon Post RA Hazard Recognizer ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the hazard recognizer for scheduling on Hexagon.
// Use a DFA based hazard recognizer.
//
//===----------------------------------------------------------------------===//

#include "HexagonHazardRecognizer.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;

#define DEBUG_TYPE "post-RA-sched"

void HexagonHazardRecognizer::Reset() {
  LLVM_DEBUG(dbgs() << "Reset hazard recognizer\n");
  Resources->clearResources();
  PacketNum = 0;
  UsesDotCur = nullptr;
  DotCurPNum = -1;
  UsesLoad = false;
  PrefVectorStoreNew = nullptr;
  RegDefs.clear();
}

ScheduleHazardRecognizer::HazardType
HexagonHazardRecognizer::getHazardType(SUnit *SU, int stalls) {
  MachineInstr *MI = SU->getInstr();
  if (!MI || TII->isZeroCost(MI->getOpcode()))
    return NoHazard;

  if (!Resources->canReserveResources(*MI)) {
    LLVM_DEBUG(dbgs() << "*** Hazard in cycle " << PacketNum << ", " << *MI);
    HazardType RetVal = Hazard;
    if (TII->mayBeNewStore(*MI)) {
      // Make sure the register to be stored is defined by an instruction in the
      // packet.
      MachineOperand &MO = MI->getOperand(MI->getNumOperands() - 1);
      if (!MO.isReg() || RegDefs.count(MO.getReg()) == 0)
        return Hazard;
      // The .new store version uses different resources so check if it
      // causes a hazard.
      MachineFunction *MF = MI->getParent()->getParent();
      MachineInstr *NewMI =
        MF->CreateMachineInstr(TII->get(TII->getDotNewOp(*MI)),
                               MI->getDebugLoc());
      if (Resources->canReserveResources(*NewMI))
        RetVal = NoHazard;
      LLVM_DEBUG(dbgs() << "*** Try .new version? " << (RetVal == NoHazard)
                        << "\n");
      MF->DeleteMachineInstr(NewMI);
    }
    return RetVal;
  }

  if (SU == UsesDotCur && DotCurPNum != (int)PacketNum) {
    LLVM_DEBUG(dbgs() << "*** .cur Hazard in cycle " << PacketNum << ", "
                      << *MI);
    return Hazard;
  }

  return NoHazard;
}

void HexagonHazardRecognizer::AdvanceCycle() {
  LLVM_DEBUG(dbgs() << "Advance cycle, clear state\n");
  Resources->clearResources();
  if (DotCurPNum != -1 && DotCurPNum != (int)PacketNum) {
    UsesDotCur = nullptr;
    DotCurPNum = -1;
  }
  UsesLoad = false;
  PrefVectorStoreNew = nullptr;
  PacketNum++;
  RegDefs.clear();
}

/// Handle the cases when we prefer one instruction over another. Case 1 - we
/// prefer not to generate multiple loads in the packet to avoid a potential
/// bank conflict. Case 2 - if a packet contains a dot cur instruction, then we
/// prefer the instruction that can use the dot cur result. However, if the use
/// is not scheduled in the same packet, then prefer other instructions in the
/// subsequent packet. Case 3 - we prefer a vector store that can be converted
/// to a .new store. The packetizer will not generate the .new store if the
/// store doesn't have resources to fit in the packet (but the .new store may
/// have resources). We attempt to schedule the store as soon as possible to
/// help packetize the two instructions together.
bool HexagonHazardRecognizer::ShouldPreferAnother(SUnit *SU) {
  if (PrefVectorStoreNew != nullptr && PrefVectorStoreNew != SU)
    return true;
  if (UsesLoad && SU->isInstr() && SU->getInstr()->mayLoad())
    return true;
  return UsesDotCur && ((SU == UsesDotCur) ^ (DotCurPNum == (int)PacketNum));
}

void HexagonHazardRecognizer::EmitInstruction(SUnit *SU) {
  MachineInstr *MI = SU->getInstr();
  if (!MI)
    return;

  // Keep the set of definitions for each packet, which is used to determine
  // if a .new can be used.
  for (const MachineOperand &MO : MI->operands())
    if (MO.isReg() && MO.isDef() && !MO.isImplicit())
      RegDefs.insert(MO.getReg());

  if (TII->isZeroCost(MI->getOpcode()))
    return;

  if (!Resources->canReserveResources(*MI)) {
    // It must be a .new store since other instructions must be able to be
    // reserved at this point.
    assert(TII->mayBeNewStore(*MI) && "Expecting .new store");
    MachineFunction *MF = MI->getParent()->getParent();
    MachineInstr *NewMI =
        MF->CreateMachineInstr(TII->get(TII->getDotNewOp(*MI)),
                               MI->getDebugLoc());
    assert(Resources->canReserveResources(*NewMI));
    Resources->reserveResources(*NewMI);
    MF->DeleteMachineInstr(NewMI);
  }
  else
    Resources->reserveResources(*MI);
  LLVM_DEBUG(dbgs() << " Add instruction " << *MI);

  // When scheduling a dot cur instruction, check if there is an instruction
  // that can use the dot cur in the same packet. If so, we'll attempt to
  // schedule it before other instructions. We only do this if the load has a
  // single zero-latency use.
  if (TII->mayBeCurLoad(*MI))
    for (auto &S : SU->Succs)
      if (S.isAssignedRegDep() && S.getLatency() == 0 &&
          S.getSUnit()->NumPredsLeft == 1) {
        UsesDotCur = S.getSUnit();
        DotCurPNum = PacketNum;
        break;
      }
  if (SU == UsesDotCur) {
    UsesDotCur = nullptr;
    DotCurPNum = -1;
  }

  UsesLoad = MI->mayLoad();

  if (TII->isHVXVec(*MI) && !MI->mayLoad() && !MI->mayStore())
    for (auto &S : SU->Succs)
      if (S.isAssignedRegDep() && S.getLatency() == 0 &&
          TII->mayBeNewStore(*S.getSUnit()->getInstr()) &&
          Resources->canReserveResources(*S.getSUnit()->getInstr())) {
        PrefVectorStoreNew = S.getSUnit();
        break;
      }
}
