//===- MCSchedule.cpp - Scheduling ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the default scheduling model.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCSchedule.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include <type_traits>

using namespace llvm;

static_assert(std::is_pod<MCSchedModel>::value,
              "We shouldn't have a static constructor here");
const MCSchedModel MCSchedModel::Default = {DefaultIssueWidth,
                                            DefaultMicroOpBufferSize,
                                            DefaultLoopMicroOpBufferSize,
                                            DefaultLoadLatency,
                                            DefaultHighLatency,
                                            DefaultMispredictPenalty,
                                            false,
                                            true,
                                            0,
                                            nullptr,
                                            nullptr,
                                            0,
                                            0,
                                            nullptr,
                                            nullptr};

int MCSchedModel::computeInstrLatency(const MCSubtargetInfo &STI,
                                      const MCSchedClassDesc &SCDesc) {
  int Latency = 0;
  for (unsigned DefIdx = 0, DefEnd = SCDesc.NumWriteLatencyEntries;
       DefIdx != DefEnd; ++DefIdx) {
    // Lookup the definition's write latency in SubtargetInfo.
    const MCWriteLatencyEntry *WLEntry =
        STI.getWriteLatencyEntry(&SCDesc, DefIdx);
    // Early exit if we found an invalid latency.
    if (WLEntry->Cycles < 0)
      return WLEntry->Cycles;
    Latency = std::max(Latency, static_cast<int>(WLEntry->Cycles));
  }
  return Latency;
}

int MCSchedModel::computeInstrLatency(const MCSubtargetInfo &STI,
                                      unsigned SchedClass) const {
  const MCSchedClassDesc &SCDesc = *getSchedClassDesc(SchedClass);
  if (!SCDesc.isValid())
    return 0;
  if (!SCDesc.isVariant())
    return MCSchedModel::computeInstrLatency(STI, SCDesc);

  llvm_unreachable("unsupported variant scheduling class");
}

int MCSchedModel::computeInstrLatency(const MCSubtargetInfo &STI,
                                      const MCInstrInfo &MCII,
                                      const MCInst &Inst) const {
  unsigned SchedClass = MCII.get(Inst.getOpcode()).getSchedClass();
  const MCSchedClassDesc *SCDesc = getSchedClassDesc(SchedClass);
  if (!SCDesc->isValid())
    return 0;

  unsigned CPUID = getProcessorID();
  while (SCDesc->isVariant()) {
    SchedClass = STI.resolveVariantSchedClass(SchedClass, &Inst, CPUID);
    SCDesc = getSchedClassDesc(SchedClass);
  }

  if (SchedClass)
    return MCSchedModel::computeInstrLatency(STI, *SCDesc);

  llvm_unreachable("unsupported variant scheduling class");
}

double
MCSchedModel::getReciprocalThroughput(const MCSubtargetInfo &STI,
                                      const MCSchedClassDesc &SCDesc) {
  Optional<double> Throughput;
  const MCSchedModel &SM = STI.getSchedModel();
  const MCWriteProcResEntry *I = STI.getWriteProcResBegin(&SCDesc);
  const MCWriteProcResEntry *E = STI.getWriteProcResEnd(&SCDesc);
  for (; I != E; ++I) {
    if (!I->Cycles)
      continue;
    unsigned NumUnits = SM.getProcResource(I->ProcResourceIdx)->NumUnits;
    double Temp = NumUnits * 1.0 / I->Cycles;
    Throughput = Throughput ? std::min(Throughput.getValue(), Temp) : Temp;
  }
  if (Throughput.hasValue())
    return 1.0 / Throughput.getValue();

  // If no throughput value was calculated, assume that we can execute at the
  // maximum issue width scaled by number of micro-ops for the schedule class.
  return ((double)SCDesc.NumMicroOps) / SM.IssueWidth;
}

double
MCSchedModel::getReciprocalThroughput(const MCSubtargetInfo &STI,
                                      const MCInstrInfo &MCII,
                                      const MCInst &Inst) const {
  unsigned SchedClass = MCII.get(Inst.getOpcode()).getSchedClass();
  const MCSchedClassDesc *SCDesc = getSchedClassDesc(SchedClass);

  // If there's no valid class, assume that the instruction executes/completes
  // at the maximum issue width.
  if (!SCDesc->isValid())
    return 1.0 / IssueWidth;

  unsigned CPUID = getProcessorID();
  while (SCDesc->isVariant()) {
    SchedClass = STI.resolveVariantSchedClass(SchedClass, &Inst, CPUID);
    SCDesc = getSchedClassDesc(SchedClass);
  }

  if (SchedClass)
    return MCSchedModel::getReciprocalThroughput(STI, *SCDesc);

  llvm_unreachable("unsupported variant scheduling class");
}

double
MCSchedModel::getReciprocalThroughput(unsigned SchedClass,
                                      const InstrItineraryData &IID) {
  Optional<double> Throughput;
  const InstrStage *I = IID.beginStage(SchedClass);
  const InstrStage *E = IID.endStage(SchedClass);
  for (; I != E; ++I) {
    if (!I->getCycles())
      continue;
    double Temp = countPopulation(I->getUnits()) * 1.0 / I->getCycles();
    Throughput = Throughput ? std::min(Throughput.getValue(), Temp) : Temp;
  }
  if (Throughput.hasValue())
    return 1.0 / Throughput.getValue();

  // If there are no execution resources specified for this class, then assume
  // that it can execute at the maximum default issue width.
  return 1.0 / DefaultIssueWidth;
}
