//===- MCSubtargetInfo.cpp - Subtarget Information ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstring>

using namespace llvm;

static FeatureBitset getFeatures(StringRef CPU, StringRef FS,
                                 ArrayRef<SubtargetFeatureKV> ProcDesc,
                                 ArrayRef<SubtargetFeatureKV> ProcFeatures) {
  SubtargetFeatures Features(FS);
  return Features.getFeatureBits(CPU, ProcDesc, ProcFeatures);
}

void MCSubtargetInfo::InitMCProcessorInfo(StringRef CPU, StringRef FS) {
  FeatureBits = getFeatures(CPU, FS, ProcDesc, ProcFeatures);
  if (!CPU.empty())
    CPUSchedModel = &getSchedModelForCPU(CPU);
  else
    CPUSchedModel = &MCSchedModel::GetDefaultSchedModel();
}

void MCSubtargetInfo::setDefaultFeatures(StringRef CPU, StringRef FS) {
  FeatureBits = getFeatures(CPU, FS, ProcDesc, ProcFeatures);
}

MCSubtargetInfo::MCSubtargetInfo(
    const Triple &TT, StringRef C, StringRef FS,
    ArrayRef<SubtargetFeatureKV> PF, ArrayRef<SubtargetFeatureKV> PD,
    const SubtargetInfoKV *ProcSched, const MCWriteProcResEntry *WPR,
    const MCWriteLatencyEntry *WL, const MCReadAdvanceEntry *RA,
    const InstrStage *IS, const unsigned *OC, const unsigned *FP)
    : TargetTriple(TT), CPU(C), ProcFeatures(PF), ProcDesc(PD),
      ProcSchedModels(ProcSched), WriteProcResTable(WPR), WriteLatencyTable(WL),
      ReadAdvanceTable(RA), Stages(IS), OperandCycles(OC), ForwardingPaths(FP) {
  InitMCProcessorInfo(CPU, FS);
}

FeatureBitset MCSubtargetInfo::ToggleFeature(uint64_t FB) {
  FeatureBits.flip(FB);
  return FeatureBits;
}

FeatureBitset MCSubtargetInfo::ToggleFeature(const FeatureBitset &FB) {
  FeatureBits ^= FB;
  return FeatureBits;
}

FeatureBitset MCSubtargetInfo::ToggleFeature(StringRef FS) {
  SubtargetFeatures::ToggleFeature(FeatureBits, FS, ProcFeatures);
  return FeatureBits;
}

FeatureBitset MCSubtargetInfo::ApplyFeatureFlag(StringRef FS) {
  SubtargetFeatures::ApplyFeatureFlag(FeatureBits, FS, ProcFeatures);
  return FeatureBits;
}

bool MCSubtargetInfo::checkFeatures(StringRef FS) const {
  SubtargetFeatures T(FS);
  FeatureBitset Set, All;
  for (std::string F : T.getFeatures()) {
    SubtargetFeatures::ApplyFeatureFlag(Set, F, ProcFeatures);
    if (F[0] == '-')
      F[0] = '+';
    SubtargetFeatures::ApplyFeatureFlag(All, F, ProcFeatures);
  }
  return (FeatureBits & All) == Set;
}

const MCSchedModel &MCSubtargetInfo::getSchedModelForCPU(StringRef CPU) const {
  assert(ProcSchedModels && "Processor machine model not available!");

  ArrayRef<SubtargetInfoKV> SchedModels(ProcSchedModels, ProcDesc.size());

  assert(std::is_sorted(SchedModels.begin(), SchedModels.end(),
                    [](const SubtargetInfoKV &LHS, const SubtargetInfoKV &RHS) {
                      return strcmp(LHS.Key, RHS.Key) < 0;
                    }) &&
         "Processor machine model table is not sorted");

  // Find entry
  auto Found =
    std::lower_bound(SchedModels.begin(), SchedModels.end(), CPU);
  if (Found == SchedModels.end() || StringRef(Found->Key) != CPU) {
    if (CPU != "help") // Don't error if the user asked for help.
      errs() << "'" << CPU
             << "' is not a recognized processor for this target"
             << " (ignoring processor)\n";
    return MCSchedModel::GetDefaultSchedModel();
  }
  assert(Found->Value && "Missing processor SchedModel value");
  return *(const MCSchedModel *)Found->Value;
}

InstrItineraryData
MCSubtargetInfo::getInstrItineraryForCPU(StringRef CPU) const {
  const MCSchedModel &SchedModel = getSchedModelForCPU(CPU);
  return InstrItineraryData(SchedModel, Stages, OperandCycles, ForwardingPaths);
}

void MCSubtargetInfo::initInstrItins(InstrItineraryData &InstrItins) const {
  InstrItins = InstrItineraryData(getSchedModel(), Stages, OperandCycles,
                                  ForwardingPaths);
}
