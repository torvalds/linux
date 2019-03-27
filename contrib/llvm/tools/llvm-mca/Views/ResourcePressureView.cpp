//===--------------------- ResourcePressureView.cpp -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements methods in the ResourcePressureView interface.
///
//===----------------------------------------------------------------------===//

#include "Views/ResourcePressureView.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace mca {

ResourcePressureView::ResourcePressureView(const llvm::MCSubtargetInfo &sti,
                                           MCInstPrinter &Printer,
                                           ArrayRef<MCInst> S)
    : STI(sti), MCIP(Printer), Source(S), LastInstructionIdx(0) {
  // Populate the map of resource descriptors.
  unsigned R2VIndex = 0;
  const MCSchedModel &SM = STI.getSchedModel();
  for (unsigned I = 0, E = SM.getNumProcResourceKinds(); I < E; ++I) {
    const MCProcResourceDesc &ProcResource = *SM.getProcResource(I);
    unsigned NumUnits = ProcResource.NumUnits;
    // Skip groups and invalid resources with zero units.
    if (ProcResource.SubUnitsIdxBegin || !NumUnits)
      continue;

    Resource2VecIndex.insert(std::pair<unsigned, unsigned>(I, R2VIndex));
    R2VIndex += ProcResource.NumUnits;
  }

  NumResourceUnits = R2VIndex;
  ResourceUsage.resize(NumResourceUnits * (Source.size() + 1));
  std::fill(ResourceUsage.begin(), ResourceUsage.end(), 0.0);
}

void ResourcePressureView::onEvent(const HWInstructionEvent &Event) {
  if (Event.Type == HWInstructionEvent::Dispatched) {
    LastInstructionIdx = Event.IR.getSourceIndex();
    return;
  }

  // We're only interested in Issue events.
  if (Event.Type != HWInstructionEvent::Issued)
    return;

  const auto &IssueEvent = static_cast<const HWInstructionIssuedEvent &>(Event);
  const unsigned SourceIdx = Event.IR.getSourceIndex() % Source.size();
  for (const std::pair<ResourceRef, ResourceCycles> &Use :
       IssueEvent.UsedResources) {
    const ResourceRef &RR = Use.first;
    assert(Resource2VecIndex.find(RR.first) != Resource2VecIndex.end());
    unsigned R2VIndex = Resource2VecIndex[RR.first];
    R2VIndex += countTrailingZeros(RR.second);
    ResourceUsage[R2VIndex + NumResourceUnits * SourceIdx] += Use.second;
    ResourceUsage[R2VIndex + NumResourceUnits * Source.size()] += Use.second;
  }
}

static void printColumnNames(formatted_raw_ostream &OS,
                             const MCSchedModel &SM) {
  unsigned Column = OS.getColumn();
  for (unsigned I = 1, ResourceIndex = 0, E = SM.getNumProcResourceKinds();
       I < E; ++I) {
    const MCProcResourceDesc &ProcResource = *SM.getProcResource(I);
    unsigned NumUnits = ProcResource.NumUnits;
    // Skip groups and invalid resources with zero units.
    if (ProcResource.SubUnitsIdxBegin || !NumUnits)
      continue;

    for (unsigned J = 0; J < NumUnits; ++J) {
      Column += 7;
      OS << "[" << ResourceIndex;
      if (NumUnits > 1)
        OS << '.' << J;
      OS << ']';
      OS.PadToColumn(Column);
    }

    ResourceIndex++;
  }
}

static void printResourcePressure(formatted_raw_ostream &OS, double Pressure,
                                  unsigned Col) {
  if (!Pressure || Pressure < 0.005) {
    OS << " - ";
  } else {
    // Round to the value to the nearest hundredth and then print it.
    OS << format("%.2f", floor((Pressure * 100) + 0.5) / 100);
  }
  OS.PadToColumn(Col);
}

void ResourcePressureView::printResourcePressurePerIter(raw_ostream &OS) const {
  std::string Buffer;
  raw_string_ostream TempStream(Buffer);
  formatted_raw_ostream FOS(TempStream);

  FOS << "\n\nResources:\n";
  const MCSchedModel &SM = STI.getSchedModel();
  for (unsigned I = 1, ResourceIndex = 0, E = SM.getNumProcResourceKinds();
       I < E; ++I) {
    const MCProcResourceDesc &ProcResource = *SM.getProcResource(I);
    unsigned NumUnits = ProcResource.NumUnits;
    // Skip groups and invalid resources with zero units.
    if (ProcResource.SubUnitsIdxBegin || !NumUnits)
      continue;

    for (unsigned J = 0; J < NumUnits; ++J) {
      FOS << '[' << ResourceIndex;
      if (NumUnits > 1)
        FOS << '.' << J;
      FOS << ']';
      FOS.PadToColumn(6);
      FOS << "- " << ProcResource.Name << '\n';
    }

    ResourceIndex++;
  }

  FOS << "\n\nResource pressure per iteration:\n";
  FOS.flush();
  printColumnNames(FOS, SM);
  FOS << '\n';
  FOS.flush();

  const unsigned Executions = LastInstructionIdx / Source.size() + 1;
  for (unsigned I = 0, E = NumResourceUnits; I < E; ++I) {
    double Usage = ResourceUsage[I + Source.size() * E];
    printResourcePressure(FOS, Usage / Executions, (I + 1) * 7);
  }

  FOS.flush();
  OS << Buffer;
}

void ResourcePressureView::printResourcePressurePerInst(raw_ostream &OS) const {
  std::string Buffer;
  raw_string_ostream TempStream(Buffer);
  formatted_raw_ostream FOS(TempStream);

  FOS << "\n\nResource pressure by instruction:\n";
  printColumnNames(FOS, STI.getSchedModel());
  FOS << "Instructions:\n";

  std::string Instruction;
  raw_string_ostream InstrStream(Instruction);

  unsigned InstrIndex = 0;
  const unsigned Executions = LastInstructionIdx / Source.size() + 1;
  for (const MCInst &MCI : Source) {
    unsigned BaseEltIdx = InstrIndex * NumResourceUnits;
    for (unsigned J = 0; J < NumResourceUnits; ++J) {
      double Usage = ResourceUsage[J + BaseEltIdx];
      printResourcePressure(FOS, Usage / Executions, (J + 1) * 7);
    }

    MCIP.printInst(&MCI, InstrStream, "", STI);
    InstrStream.flush();
    StringRef Str(Instruction);

    // Remove any tabs or spaces at the beginning of the instruction.
    Str = Str.ltrim();

    FOS << Str << '\n';
    Instruction = "";

    FOS.flush();
    OS << Buffer;
    Buffer = "";

    ++InstrIndex;
  }
}
} // namespace mca
} // namespace llvm
