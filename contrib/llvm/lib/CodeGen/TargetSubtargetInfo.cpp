//===- TargetSubtargetInfo.cpp - General Target Information ----------------==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file This file describes the general parts of a Subtarget.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/ADT/Optional.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <string>

using namespace llvm;

TargetSubtargetInfo::TargetSubtargetInfo(
    const Triple &TT, StringRef CPU, StringRef FS,
    ArrayRef<SubtargetFeatureKV> PF, ArrayRef<SubtargetFeatureKV> PD,
    const SubtargetInfoKV *ProcSched, const MCWriteProcResEntry *WPR,
    const MCWriteLatencyEntry *WL, const MCReadAdvanceEntry *RA,
    const InstrStage *IS, const unsigned *OC, const unsigned *FP)
    : MCSubtargetInfo(TT, CPU, FS, PF, PD, ProcSched, WPR, WL, RA, IS, OC, FP) {
}

TargetSubtargetInfo::~TargetSubtargetInfo() = default;

bool TargetSubtargetInfo::enableAtomicExpand() const {
  return true;
}

bool TargetSubtargetInfo::enableIndirectBrExpand() const {
  return false;
}

bool TargetSubtargetInfo::enableMachineScheduler() const {
  return false;
}

bool TargetSubtargetInfo::enableJoinGlobalCopies() const {
  return enableMachineScheduler();
}

bool TargetSubtargetInfo::enableRALocalReassignment(
    CodeGenOpt::Level OptLevel) const {
  return true;
}

bool TargetSubtargetInfo::enableAdvancedRASplitCost() const {
  return false;
}

bool TargetSubtargetInfo::enablePostRAScheduler() const {
  return getSchedModel().PostRAScheduler;
}

bool TargetSubtargetInfo::useAA() const {
  return false;
}

static std::string createSchedInfoStr(unsigned Latency, double RThroughput) {
  static const char *SchedPrefix = " sched: [";
  std::string Comment;
  raw_string_ostream CS(Comment);
  if (RThroughput != 0.0)
    CS << SchedPrefix << Latency << format(":%2.2f", RThroughput)
       << "]";
  else
    CS << SchedPrefix << Latency << ":?]";
  CS.flush();
  return Comment;
}

/// Returns string representation of scheduler comment
std::string TargetSubtargetInfo::getSchedInfoStr(const MachineInstr &MI) const {
  if (MI.isPseudo() || MI.isTerminator())
    return std::string();
  // We don't cache TSchedModel because it depends on TargetInstrInfo
  // that could be changed during the compilation
  TargetSchedModel TSchedModel;
  TSchedModel.init(this);
  unsigned Latency = TSchedModel.computeInstrLatency(&MI);
  double RThroughput = TSchedModel.computeReciprocalThroughput(&MI);
  return createSchedInfoStr(Latency, RThroughput);
}

/// Returns string representation of scheduler comment
std::string TargetSubtargetInfo::getSchedInfoStr(MCInst const &MCI) const {
  // We don't cache TSchedModel because it depends on TargetInstrInfo
  // that could be changed during the compilation
  TargetSchedModel TSchedModel;
  TSchedModel.init(this);
  unsigned Latency;
  if (TSchedModel.hasInstrSchedModel())
    Latency = TSchedModel.computeInstrLatency(MCI);
  else if (TSchedModel.hasInstrItineraries()) {
    auto *ItinData = TSchedModel.getInstrItineraries();
    Latency = ItinData->getStageLatency(
        getInstrInfo()->get(MCI.getOpcode()).getSchedClass());
  } else
    return std::string();
  double RThroughput = TSchedModel.computeReciprocalThroughput(MCI);
  return createSchedInfoStr(Latency, RThroughput);
}

void TargetSubtargetInfo::mirFileLoaded(MachineFunction &MF) const {
}
