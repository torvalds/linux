//===--------------------- InstructionInfoView.cpp --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements the InstructionInfoView API.
///
//===----------------------------------------------------------------------===//

#include "Views/InstructionInfoView.h"

namespace llvm {
namespace mca {

void InstructionInfoView::printView(raw_ostream &OS) const {
  std::string Buffer;
  raw_string_ostream TempStream(Buffer);
  const MCSchedModel &SM = STI.getSchedModel();

  std::string Instruction;
  raw_string_ostream InstrStream(Instruction);

  TempStream << "\n\nInstruction Info:\n";
  TempStream << "[1]: #uOps\n[2]: Latency\n[3]: RThroughput\n"
             << "[4]: MayLoad\n[5]: MayStore\n[6]: HasSideEffects (U)\n\n";

  TempStream << "[1]    [2]    [3]    [4]    [5]    [6]    Instructions:\n";
  for (const MCInst &Inst : Source) {
    const MCInstrDesc &MCDesc = MCII.get(Inst.getOpcode());

    // Obtain the scheduling class information from the instruction.
    unsigned SchedClassID = MCDesc.getSchedClass();
    unsigned CPUID = SM.getProcessorID();

    // Try to solve variant scheduling classes.
    while (SchedClassID && SM.getSchedClassDesc(SchedClassID)->isVariant())
      SchedClassID = STI.resolveVariantSchedClass(SchedClassID, &Inst, CPUID);

    const MCSchedClassDesc &SCDesc = *SM.getSchedClassDesc(SchedClassID);
    unsigned NumMicroOpcodes = SCDesc.NumMicroOps;
    unsigned Latency = MCSchedModel::computeInstrLatency(STI, SCDesc);
    Optional<double> RThroughput =
        MCSchedModel::getReciprocalThroughput(STI, SCDesc);

    TempStream << ' ' << NumMicroOpcodes << "    ";
    if (NumMicroOpcodes < 10)
      TempStream << "  ";
    else if (NumMicroOpcodes < 100)
      TempStream << ' ';
    TempStream << Latency << "   ";
    if (Latency < 10)
      TempStream << "  ";
    else if (Latency < 100)
      TempStream << ' ';

    if (RThroughput.hasValue()) {
      double RT = RThroughput.getValue();
      TempStream << format("%.2f", RT) << ' ';
      if (RT < 10.0)
        TempStream << "  ";
      else if (RT < 100.0)
        TempStream << ' ';
    } else {
      TempStream << " -     ";
    }
    TempStream << (MCDesc.mayLoad() ? " *     " : "       ");
    TempStream << (MCDesc.mayStore() ? " *     " : "       ");
    TempStream << (MCDesc.hasUnmodeledSideEffects() ? " U " : "   ");

    MCIP.printInst(&Inst, InstrStream, "", STI);
    InstrStream.flush();

    // Consume any tabs or spaces at the beginning of the string.
    StringRef Str(Instruction);
    Str = Str.ltrim();
    TempStream << "    " << Str << '\n';
    Instruction = "";
  }

  TempStream.flush();
  OS << Buffer;
}
} // namespace mca.
} // namespace llvm
