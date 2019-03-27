//===-- ARMHazardRecognizer.h - ARM Hazard Recognizers ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines hazard recognizers for scheduling ARM functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ARMHAZARDRECOGNIZER_H
#define LLVM_LIB_TARGET_ARM_ARMHAZARDRECOGNIZER_H

#include "llvm/CodeGen/ScoreboardHazardRecognizer.h"

namespace llvm {

class ARMBaseInstrInfo;
class ARMBaseRegisterInfo;
class ARMSubtarget;
class MachineInstr;

/// ARMHazardRecognizer handles special constraints that are not expressed in
/// the scheduling itinerary. This is only used during postRA scheduling. The
/// ARM preRA scheduler uses an unspecialized instance of the
/// ScoreboardHazardRecognizer.
class ARMHazardRecognizer : public ScoreboardHazardRecognizer {
  MachineInstr *LastMI;
  unsigned FpMLxStalls;

public:
  ARMHazardRecognizer(const InstrItineraryData *ItinData,
                      const ScheduleDAG *DAG)
    : ScoreboardHazardRecognizer(ItinData, DAG, "post-RA-sched"),
      LastMI(nullptr) {}

  HazardType getHazardType(SUnit *SU, int Stalls) override;
  void Reset() override;
  void EmitInstruction(SUnit *SU) override;
  void AdvanceCycle() override;
  void RecedeCycle() override;
};

} // end namespace llvm

#endif
