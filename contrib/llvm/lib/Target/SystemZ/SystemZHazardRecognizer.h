//=-- SystemZHazardRecognizer.h - SystemZ Hazard Recognizer -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares a hazard recognizer for the SystemZ scheduler.
//
// This class is used by the SystemZ scheduling strategy to maintain
// the state during scheduling, and provide cost functions for
// scheduling candidates. This includes:
//
// * Decoder grouping. A decoder group can maximally hold 3 uops, and
// instructions that always begin a new group should be scheduled when
// the current decoder group is empty.
// * Processor resources usage. It is beneficial to balance the use of
// resources.
//
// A goal is to consider all instructions, also those outside of any
// scheduling region. Such instructions are "advanced" past and include
// single instructions before a scheduling region, branches etc.
//
// A block that has only one predecessor continues scheduling with the state
// of it (which may be updated by emitting branches).
//
// ===---------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZHAZARDRECOGNIZER_H
#define LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZHAZARDRECOGNIZER_H

#include "SystemZSubtarget.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/ScheduleHazardRecognizer.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Support/raw_ostream.h"
#include <string>

namespace llvm {

/// SystemZHazardRecognizer maintains the state for one MBB during scheduling.
class SystemZHazardRecognizer : public ScheduleHazardRecognizer {

  const SystemZInstrInfo *TII;
  const TargetSchedModel *SchedModel;

  /// Keep track of the number of decoder slots used in the current
  /// decoder group.
  unsigned CurrGroupSize;

  /// True if an instruction with four reg operands have been scheduled into
  /// the current decoder group.
  bool CurrGroupHas4RegOps;

  /// The tracking of resources here are quite similar to the common
  /// code use of a critical resource. However, z13 differs in the way
  /// that it has two processor sides which may be interesting to
  /// model in the future (a work in progress).

  /// Counters for the number of uops scheduled per processor
  /// resource.
  SmallVector<int, 0> ProcResourceCounters;

  /// This is the resource with the greatest queue, which the
  /// scheduler tries to avoid.
  unsigned CriticalResourceIdx;

  /// Return the number of decoder slots MI requires.
  inline unsigned getNumDecoderSlots(SUnit *SU) const;

  /// Return true if MI fits into current decoder group.
  bool fitsIntoCurrentGroup(SUnit *SU) const;

  /// Return true if this instruction has four register operands.
  bool has4RegOps(const MachineInstr *MI) const;

  /// Two decoder groups per cycle are formed (for z13), meaning 2x3
  /// instructions. This function returns a number between 0 and 5,
  /// representing the current decoder slot of the current cycle.  If an SU
  /// is passed which will begin a new decoder group, the returned value is
  /// the cycle index of the next group.
  unsigned getCurrCycleIdx(SUnit *SU = nullptr) const;

  /// LastFPdOpCycleIdx stores the numbeer returned by getCurrCycleIdx()
  /// when a stalling operation is scheduled (which uses the FPd resource).
  unsigned LastFPdOpCycleIdx;

  /// A counter of decoder groups scheduled.
  unsigned GrpCount;

  unsigned getCurrGroupSize() {return CurrGroupSize;};

  /// Start next decoder group.
  void nextGroup();

  /// Clear all counters for processor resources.
  void clearProcResCounters();

  /// With the goal of alternating processor sides for stalling (FPd)
  /// ops, return true if it seems good to schedule an FPd op next.
  bool isFPdOpPreferred_distance(SUnit *SU) const;

  /// Last emitted instruction or nullptr.
  MachineInstr *LastEmittedMI;

public:
  SystemZHazardRecognizer(const SystemZInstrInfo *tii,
                          const TargetSchedModel *SM)
      : TII(tii), SchedModel(SM) {
    Reset();
  }

  HazardType getHazardType(SUnit *m, int Stalls = 0) override;
  void Reset() override;
  void EmitInstruction(SUnit *SU) override;

  /// Resolves and cache a resolved scheduling class for an SUnit.
  const MCSchedClassDesc *getSchedClass(SUnit *SU) const {
    if (!SU->SchedClass && SchedModel->hasInstrSchedModel())
      SU->SchedClass = SchedModel->resolveSchedClass(SU->getInstr());
    return SU->SchedClass;
  }

  /// Wrap a non-scheduled instruction in an SU and emit it.
  void emitInstruction(MachineInstr *MI, bool TakenBranch = false);

  // Cost functions used by SystemZPostRASchedStrategy while
  // evaluating candidates.

  /// Return the cost of decoder grouping for SU. If SU must start a
  /// new decoder group, this is negative if this fits the schedule or
  /// positive if it would mean ending a group prematurely. For normal
  /// instructions this returns 0.
  int groupingCost(SUnit *SU) const;

  /// Return the cost of SU in regards to processor resources usage.
  /// A positive value means it would be better to wait with SU, while
  /// a negative value means it would be good to schedule SU next.
  int resourcesCost(SUnit *SU);

#ifndef NDEBUG
  // Debug dumping.
  std::string CurGroupDbg; // current group as text
  void dumpSU(SUnit *SU, raw_ostream &OS) const;
  void dumpCurrGroup(std::string Msg = "") const;
  void dumpProcResourceCounters() const;
  void dumpState() const;
#endif

  MachineBasicBlock::iterator getLastEmittedMI() { return LastEmittedMI; }

  /// Copy counters from end of single predecessor.
  void copyState(SystemZHazardRecognizer *Incoming);
};

} // namespace llvm

#endif /* LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZHAZARDRECOGNIZER_H */
