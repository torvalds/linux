//===-- R600MachineScheduler.h - R600 Scheduler Interface -*- C++ -*-------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// R600 Machine Scheduler interface
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_R600MACHINESCHEDULER_H
#define LLVM_LIB_TARGET_AMDGPU_R600MACHINESCHEDULER_H

#include "llvm/CodeGen/MachineScheduler.h"
#include <vector>

using namespace llvm;

namespace llvm {

class R600InstrInfo;
struct R600RegisterInfo;

class R600SchedStrategy final : public MachineSchedStrategy {
  const ScheduleDAGMILive *DAG = nullptr;
  const R600InstrInfo *TII = nullptr;
  const R600RegisterInfo *TRI = nullptr;
  MachineRegisterInfo *MRI = nullptr;

  enum InstKind {
    IDAlu,
    IDFetch,
    IDOther,
    IDLast
  };

  enum AluKind {
    AluAny,
    AluT_X,
    AluT_Y,
    AluT_Z,
    AluT_W,
    AluT_XYZW,
    AluPredX,
    AluTrans,
    AluDiscarded, // LLVM Instructions that are going to be eliminated
    AluLast
  };

  std::vector<SUnit *> Available[IDLast], Pending[IDLast];
  std::vector<SUnit *> AvailableAlus[AluLast];
  std::vector<SUnit *> PhysicalRegCopy;

  InstKind CurInstKind;
  int CurEmitted;
  InstKind NextInstKind;

  unsigned AluInstCount;
  unsigned FetchInstCount;

  int InstKindLimit[IDLast];

  int OccupedSlotsMask;

public:
  R600SchedStrategy() = default;
  ~R600SchedStrategy() override = default;

  void initialize(ScheduleDAGMI *dag) override;
  SUnit *pickNode(bool &IsTopNode) override;
  void schedNode(SUnit *SU, bool IsTopNode) override;
  void releaseTopNode(SUnit *SU) override;
  void releaseBottomNode(SUnit *SU) override;

private:
  std::vector<MachineInstr *> InstructionsGroupCandidate;
  bool VLIW5;

  int getInstKind(SUnit *SU);
  bool regBelongsToClass(unsigned Reg, const TargetRegisterClass *RC) const;
  AluKind getAluKind(SUnit *SU) const;
  void LoadAlu();
  unsigned AvailablesAluCount() const;
  SUnit *AttemptFillSlot (unsigned Slot, bool AnyAlu);
  void PrepareNextSlot();
  SUnit *PopInst(std::vector<SUnit*> &Q, bool AnyALU);

  void AssignSlot(MachineInstr *MI, unsigned Slot);
  SUnit* pickAlu();
  SUnit* pickOther(int QID);
  void MoveUnits(std::vector<SUnit *> &QSrc, std::vector<SUnit *> &QDst);
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_R600MACHINESCHEDULER_H
