//===--------------------- DispatchStatistics.cpp ---------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements the DispatchStatistics interface.
///
//===----------------------------------------------------------------------===//

#include "Views/DispatchStatistics.h"
#include "llvm/Support/Format.h"

namespace llvm {
namespace mca {

void DispatchStatistics::onEvent(const HWStallEvent &Event) {
  if (Event.Type < HWStallEvent::LastGenericEvent)
    HWStalls[Event.Type]++;
}

void DispatchStatistics::onEvent(const HWInstructionEvent &Event) {
  if (Event.Type != HWInstructionEvent::Dispatched)
    return;

  const auto &DE = static_cast<const HWInstructionDispatchedEvent &>(Event);
  NumDispatched += DE.MicroOpcodes;
}

void DispatchStatistics::printDispatchHistogram(raw_ostream &OS) const {
  std::string Buffer;
  raw_string_ostream TempStream(Buffer);
  TempStream << "\n\nDispatch Logic - "
             << "number of cycles where we saw N micro opcodes dispatched:\n";
  TempStream << "[# dispatched], [# cycles]\n";
  for (const std::pair<unsigned, unsigned> &Entry : DispatchGroupSizePerCycle) {
    double Percentage = ((double)Entry.second / NumCycles) * 100.0;
    TempStream << " " << Entry.first << ",              " << Entry.second
               << "  (" << format("%.1f", floor((Percentage * 10) + 0.5) / 10)
               << "%)\n";
  }

  TempStream.flush();
  OS << Buffer;
}

static void printStalls(raw_ostream &OS, unsigned NumStalls,
                        unsigned NumCycles) {
  if (!NumStalls) {
    OS << NumStalls;
    return;
  }

  double Percentage = ((double)NumStalls / NumCycles) * 100.0;
  OS << NumStalls << "  ("
     << format("%.1f", floor((Percentage * 10) + 0.5) / 10) << "%)";
}

void DispatchStatistics::printDispatchStalls(raw_ostream &OS) const {
  std::string Buffer;
  raw_string_ostream SS(Buffer);
  SS << "\n\nDynamic Dispatch Stall Cycles:\n";
  SS << "RAT     - Register unavailable:                      ";
  printStalls(SS, HWStalls[HWStallEvent::RegisterFileStall], NumCycles);
  SS << "\nRCU     - Retire tokens unavailable:                 ";
  printStalls(SS, HWStalls[HWStallEvent::RetireControlUnitStall], NumCycles);
  SS << "\nSCHEDQ  - Scheduler full:                            ";
  printStalls(SS, HWStalls[HWStallEvent::SchedulerQueueFull], NumCycles);
  SS << "\nLQ      - Load queue full:                           ";
  printStalls(SS, HWStalls[HWStallEvent::LoadQueueFull], NumCycles);
  SS << "\nSQ      - Store queue full:                          ";
  printStalls(SS, HWStalls[HWStallEvent::StoreQueueFull], NumCycles);
  SS << "\nGROUP   - Static restrictions on the dispatch group: ";
  printStalls(SS, HWStalls[HWStallEvent::DispatchGroupStall], NumCycles);
  SS << '\n';
  SS.flush();
  OS << Buffer;
}

} // namespace mca
} // namespace llvm
