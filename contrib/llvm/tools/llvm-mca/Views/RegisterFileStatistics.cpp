//===--------------------- RegisterFileStatistics.cpp -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements the RegisterFileStatistics interface.
///
//===----------------------------------------------------------------------===//

#include "Views/RegisterFileStatistics.h"
#include "llvm/Support/Format.h"

namespace llvm {
namespace mca {

RegisterFileStatistics::RegisterFileStatistics(const MCSubtargetInfo &sti)
    : STI(sti) {
  const MCSchedModel &SM = STI.getSchedModel();
  RegisterFileUsage RFUEmpty = {0, 0, 0};
  MoveEliminationInfo MEIEmpty = {0, 0, 0, 0, 0};
  if (!SM.hasExtraProcessorInfo()) {
    // Assume a single register file.
    PRFUsage.emplace_back(RFUEmpty);
    MoveElimInfo.emplace_back(MEIEmpty);
    return;
  }

  // Initialize a RegisterFileUsage for every user defined register file, plus
  // the default register file which is always at index #0.
  const MCExtraProcessorInfo &PI = SM.getExtraProcessorInfo();
  // There is always an "InvalidRegisterFile" entry in tablegen. That entry can
  // be skipped. If there are no user defined register files, then reserve a
  // single entry for the default register file at index #0.
  unsigned NumRegFiles = std::max(PI.NumRegisterFiles, 1U);

  PRFUsage.resize(NumRegFiles);
  std::fill(PRFUsage.begin(), PRFUsage.end(), RFUEmpty);

  MoveElimInfo.resize(NumRegFiles);
  std::fill(MoveElimInfo.begin(), MoveElimInfo.end(), MEIEmpty);
}

void RegisterFileStatistics::updateRegisterFileUsage(
    ArrayRef<unsigned> UsedPhysRegs) {
  for (unsigned I = 0, E = PRFUsage.size(); I < E; ++I) {
    RegisterFileUsage &RFU = PRFUsage[I];
    unsigned NumUsedPhysRegs = UsedPhysRegs[I];
    RFU.CurrentlyUsedMappings += NumUsedPhysRegs;
    RFU.TotalMappings += NumUsedPhysRegs;
    RFU.MaxUsedMappings =
        std::max(RFU.MaxUsedMappings, RFU.CurrentlyUsedMappings);
  }
}

void RegisterFileStatistics::updateMoveElimInfo(const Instruction &Inst) {
  if (!Inst.isOptimizableMove())
    return;

  assert(Inst.getDefs().size() == 1 && "Expected a single definition!");
  assert(Inst.getUses().size() == 1 && "Expected a single register use!");
  const WriteState &WS = Inst.getDefs()[0];
  const ReadState &RS = Inst.getUses()[0];

  MoveEliminationInfo &Info =
      MoveElimInfo[Inst.getDefs()[0].getRegisterFileID()];
  Info.TotalMoveEliminationCandidates++;
  if (WS.isEliminated())
    Info.CurrentMovesEliminated++;
  if (WS.isWriteZero() && RS.isReadZero())
    Info.TotalMovesThatPropagateZero++;
}

void RegisterFileStatistics::onEvent(const HWInstructionEvent &Event) {
  switch (Event.Type) {
  default:
    break;
  case HWInstructionEvent::Retired: {
    const auto &RE = static_cast<const HWInstructionRetiredEvent &>(Event);
    for (unsigned I = 0, E = PRFUsage.size(); I < E; ++I)
      PRFUsage[I].CurrentlyUsedMappings -= RE.FreedPhysRegs[I];
    break;
  }
  case HWInstructionEvent::Dispatched: {
    const auto &DE = static_cast<const HWInstructionDispatchedEvent &>(Event);
    updateRegisterFileUsage(DE.UsedPhysRegs);
    updateMoveElimInfo(*DE.IR.getInstruction());
  }
  }
}

void RegisterFileStatistics::onCycleEnd() {
  for (MoveEliminationInfo &MEI : MoveElimInfo) {
    unsigned &CurrentMax = MEI.MaxMovesEliminatedPerCycle;
    CurrentMax = std::max(CurrentMax, MEI.CurrentMovesEliminated);
    MEI.TotalMovesEliminated += MEI.CurrentMovesEliminated;
    MEI.CurrentMovesEliminated = 0;
  }
}

void RegisterFileStatistics::printView(raw_ostream &OS) const {
  std::string Buffer;
  raw_string_ostream TempStream(Buffer);

  TempStream << "\n\nRegister File statistics:";
  const RegisterFileUsage &GlobalUsage = PRFUsage[0];
  TempStream << "\nTotal number of mappings created:    "
             << GlobalUsage.TotalMappings;
  TempStream << "\nMax number of mappings used:         "
             << GlobalUsage.MaxUsedMappings << '\n';

  for (unsigned I = 1, E = PRFUsage.size(); I < E; ++I) {
    const RegisterFileUsage &RFU = PRFUsage[I];
    // Obtain the register file descriptor from the scheduling model.
    assert(STI.getSchedModel().hasExtraProcessorInfo() &&
           "Unable to find register file info!");
    const MCExtraProcessorInfo &PI =
        STI.getSchedModel().getExtraProcessorInfo();
    assert(I <= PI.NumRegisterFiles && "Unexpected register file index!");
    const MCRegisterFileDesc &RFDesc = PI.RegisterFiles[I];
    // Skip invalid register files.
    if (!RFDesc.NumPhysRegs)
      continue;

    TempStream << "\n*  Register File #" << I;
    TempStream << " -- " << StringRef(RFDesc.Name) << ':';
    TempStream << "\n   Number of physical registers:     ";
    if (!RFDesc.NumPhysRegs)
      TempStream << "unbounded";
    else
      TempStream << RFDesc.NumPhysRegs;
    TempStream << "\n   Total number of mappings created: "
               << RFU.TotalMappings;
    TempStream << "\n   Max number of mappings used:      "
               << RFU.MaxUsedMappings << '\n';
    const MoveEliminationInfo &MEI = MoveElimInfo[I];

    if (MEI.TotalMoveEliminationCandidates) {
      TempStream << "   Number of optimizable moves:      "
                 << MEI.TotalMoveEliminationCandidates;
      double EliminatedMovProportion = (double)MEI.TotalMovesEliminated /
                                       MEI.TotalMoveEliminationCandidates *
                                       100.0;
      double ZeroMovProportion = (double)MEI.TotalMovesThatPropagateZero /
                                 MEI.TotalMoveEliminationCandidates * 100.0;
      TempStream << "\n   Number of moves eliminated:       "
                 << MEI.TotalMovesEliminated << "  "
                 << format("(%.1f%%)",
                           floor((EliminatedMovProportion * 10) + 0.5) / 10);
      TempStream << "\n   Number of zero moves:             "
                 << MEI.TotalMovesThatPropagateZero << "  "
                 << format("(%.1f%%)",
                           floor((ZeroMovProportion * 10) + 0.5) / 10);
      TempStream << "\n   Max moves eliminated per cycle:   "
                 << MEI.MaxMovesEliminatedPerCycle << '\n';
    }
  }

  TempStream.flush();
  OS << Buffer;
}

} // namespace mca
} // namespace llvm
