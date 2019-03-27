//===--------------------- TimelineView.cpp ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \brief
///
/// This file implements the TimelineView interface.
///
//===----------------------------------------------------------------------===//

#include "Views/TimelineView.h"

namespace llvm {
namespace mca {

TimelineView::TimelineView(const MCSubtargetInfo &sti, MCInstPrinter &Printer,
                           llvm::ArrayRef<llvm::MCInst> S, unsigned Iterations,
                           unsigned Cycles)
    : STI(sti), MCIP(Printer), Source(S), CurrentCycle(0),
      MaxCycle(Cycles == 0 ? 80 : Cycles), LastCycle(0), WaitTime(S.size()),
      UsedBuffer(S.size()) {
  unsigned NumInstructions = Source.size();
  assert(Iterations && "Invalid number of iterations specified!");
  NumInstructions *= Iterations;
  Timeline.resize(NumInstructions);
  TimelineViewEntry InvalidTVEntry = {-1, 0, 0, 0, 0};
  std::fill(Timeline.begin(), Timeline.end(), InvalidTVEntry);

  WaitTimeEntry NullWTEntry = {0, 0, 0};
  std::fill(WaitTime.begin(), WaitTime.end(), NullWTEntry);

  std::pair<unsigned, int> NullUsedBufferEntry = {/* Invalid resource ID*/ 0,
                                                  /* unknown buffer size */ -1};
  std::fill(UsedBuffer.begin(), UsedBuffer.end(), NullUsedBufferEntry);
}

void TimelineView::onReservedBuffers(const InstRef &IR,
                                     ArrayRef<unsigned> Buffers) {
  if (IR.getSourceIndex() >= Source.size())
    return;

  const MCSchedModel &SM = STI.getSchedModel();
  std::pair<unsigned, int> BufferInfo = {0, -1};
  for (const unsigned Buffer : Buffers) {
    const MCProcResourceDesc &MCDesc = *SM.getProcResource(Buffer);
    if (!BufferInfo.first || BufferInfo.second > MCDesc.BufferSize) {
      BufferInfo.first = Buffer;
      BufferInfo.second = MCDesc.BufferSize;
    }
  }

  UsedBuffer[IR.getSourceIndex()] = BufferInfo;
}

void TimelineView::onEvent(const HWInstructionEvent &Event) {
  const unsigned Index = Event.IR.getSourceIndex();
  if (Index >= Timeline.size())
    return;

  switch (Event.Type) {
  case HWInstructionEvent::Retired: {
    TimelineViewEntry &TVEntry = Timeline[Index];
    if (CurrentCycle < MaxCycle)
      TVEntry.CycleRetired = CurrentCycle;

    // Update the WaitTime entry which corresponds to this Index.
    assert(TVEntry.CycleDispatched >= 0 && "Invalid TVEntry found!");
    unsigned CycleDispatched = static_cast<unsigned>(TVEntry.CycleDispatched);
    WaitTimeEntry &WTEntry = WaitTime[Index % Source.size()];
    WTEntry.CyclesSpentInSchedulerQueue +=
        TVEntry.CycleIssued - CycleDispatched;
    assert(CycleDispatched <= TVEntry.CycleReady &&
           "Instruction cannot be ready if it hasn't been dispatched yet!");
    WTEntry.CyclesSpentInSQWhileReady +=
        TVEntry.CycleIssued - TVEntry.CycleReady;
    WTEntry.CyclesSpentAfterWBAndBeforeRetire +=
        (CurrentCycle - 1) - TVEntry.CycleExecuted;
    break;
  }
  case HWInstructionEvent::Ready:
    Timeline[Index].CycleReady = CurrentCycle;
    break;
  case HWInstructionEvent::Issued:
    Timeline[Index].CycleIssued = CurrentCycle;
    break;
  case HWInstructionEvent::Executed:
    Timeline[Index].CycleExecuted = CurrentCycle;
    break;
  case HWInstructionEvent::Dispatched:
    // There may be multiple dispatch events. Microcoded instructions that are
    // expanded into multiple uOps may require multiple dispatch cycles. Here,
    // we want to capture the first dispatch cycle.
    if (Timeline[Index].CycleDispatched == -1)
      Timeline[Index].CycleDispatched = static_cast<int>(CurrentCycle);
    break;
  default:
    return;
  }
  if (CurrentCycle < MaxCycle)
    LastCycle = std::max(LastCycle, CurrentCycle);
}

static raw_ostream::Colors chooseColor(unsigned CumulativeCycles,
                                       unsigned Executions, int BufferSize) {
  if (CumulativeCycles && BufferSize < 0)
    return raw_ostream::MAGENTA;
  unsigned Size = static_cast<unsigned>(BufferSize);
  if (CumulativeCycles >= Size * Executions)
    return raw_ostream::RED;
  if ((CumulativeCycles * 2) >= Size * Executions)
    return raw_ostream::YELLOW;
  return raw_ostream::SAVEDCOLOR;
}

static void tryChangeColor(raw_ostream &OS, unsigned Cycles,
                           unsigned Executions, int BufferSize) {
  if (!OS.has_colors())
    return;

  raw_ostream::Colors Color = chooseColor(Cycles, Executions, BufferSize);
  if (Color == raw_ostream::SAVEDCOLOR) {
    OS.resetColor();
    return;
  }
  OS.changeColor(Color, /* bold */ true, /* BG */ false);
}

void TimelineView::printWaitTimeEntry(formatted_raw_ostream &OS,
                                      const WaitTimeEntry &Entry,
                                      unsigned SourceIndex,
                                      unsigned Executions) const {
  OS << SourceIndex << '.';
  OS.PadToColumn(7);

  double AverageTime1, AverageTime2, AverageTime3;
  AverageTime1 = (double)Entry.CyclesSpentInSchedulerQueue / Executions;
  AverageTime2 = (double)Entry.CyclesSpentInSQWhileReady / Executions;
  AverageTime3 = (double)Entry.CyclesSpentAfterWBAndBeforeRetire / Executions;

  OS << Executions;
  OS.PadToColumn(13);
  int BufferSize = UsedBuffer[SourceIndex].second;
  tryChangeColor(OS, Entry.CyclesSpentInSchedulerQueue, Executions, BufferSize);
  OS << format("%.1f", floor((AverageTime1 * 10) + 0.5) / 10);
  OS.PadToColumn(20);
  tryChangeColor(OS, Entry.CyclesSpentInSQWhileReady, Executions, BufferSize);
  OS << format("%.1f", floor((AverageTime2 * 10) + 0.5) / 10);
  OS.PadToColumn(27);
  tryChangeColor(OS, Entry.CyclesSpentAfterWBAndBeforeRetire, Executions,
                 STI.getSchedModel().MicroOpBufferSize);
  OS << format("%.1f", floor((AverageTime3 * 10) + 0.5) / 10);

  if (OS.has_colors())
    OS.resetColor();
  OS.PadToColumn(34);
}

void TimelineView::printAverageWaitTimes(raw_ostream &OS) const {
  std::string Header =
      "\n\nAverage Wait times (based on the timeline view):\n"
      "[0]: Executions\n"
      "[1]: Average time spent waiting in a scheduler's queue\n"
      "[2]: Average time spent waiting in a scheduler's queue while ready\n"
      "[3]: Average time elapsed from WB until retire stage\n\n"
      "      [0]    [1]    [2]    [3]\n";
  OS << Header;

  // Use a different string stream for printing instructions.
  std::string Instruction;
  raw_string_ostream InstrStream(Instruction);

  formatted_raw_ostream FOS(OS);
  unsigned Executions = Timeline.size() / Source.size();
  unsigned IID = 0;
  for (const MCInst &Inst : Source) {
    printWaitTimeEntry(FOS, WaitTime[IID], IID, Executions);
    // Append the instruction info at the end of the line.
    MCIP.printInst(&Inst, InstrStream, "", STI);
    InstrStream.flush();

    // Consume any tabs or spaces at the beginning of the string.
    StringRef Str(Instruction);
    Str = Str.ltrim();
    FOS << "   " << Str << '\n';
    FOS.flush();
    Instruction = "";

    ++IID;
  }
}

void TimelineView::printTimelineViewEntry(formatted_raw_ostream &OS,
                                          const TimelineViewEntry &Entry,
                                          unsigned Iteration,
                                          unsigned SourceIndex) const {
  if (Iteration == 0 && SourceIndex == 0)
    OS << '\n';
  OS << '[' << Iteration << ',' << SourceIndex << ']';
  OS.PadToColumn(10);
  assert(Entry.CycleDispatched >= 0 && "Invalid TimelineViewEntry!");
  unsigned CycleDispatched = static_cast<unsigned>(Entry.CycleDispatched);
  for (unsigned I = 0, E = CycleDispatched; I < E; ++I)
    OS << ((I % 5 == 0) ? '.' : ' ');
  OS << TimelineView::DisplayChar::Dispatched;
  if (CycleDispatched != Entry.CycleExecuted) {
    // Zero latency instructions have the same value for CycleDispatched,
    // CycleIssued and CycleExecuted.
    for (unsigned I = CycleDispatched + 1, E = Entry.CycleIssued; I < E; ++I)
      OS << TimelineView::DisplayChar::Waiting;
    if (Entry.CycleIssued == Entry.CycleExecuted)
      OS << TimelineView::DisplayChar::DisplayChar::Executed;
    else {
      if (CycleDispatched != Entry.CycleIssued)
        OS << TimelineView::DisplayChar::Executing;
      for (unsigned I = Entry.CycleIssued + 1, E = Entry.CycleExecuted; I < E;
           ++I)
        OS << TimelineView::DisplayChar::Executing;
      OS << TimelineView::DisplayChar::Executed;
    }
  }

  for (unsigned I = Entry.CycleExecuted + 1, E = Entry.CycleRetired; I < E; ++I)
    OS << TimelineView::DisplayChar::RetireLag;
  OS << TimelineView::DisplayChar::Retired;

  // Skip other columns.
  for (unsigned I = Entry.CycleRetired + 1, E = LastCycle; I <= E; ++I)
    OS << ((I % 5 == 0 || I == LastCycle) ? '.' : ' ');
}

static void printTimelineHeader(formatted_raw_ostream &OS, unsigned Cycles) {
  OS << "\n\nTimeline view:\n";
  if (Cycles >= 10) {
    OS.PadToColumn(10);
    for (unsigned I = 0; I <= Cycles; ++I) {
      if (((I / 10) & 1) == 0)
        OS << ' ';
      else
        OS << I % 10;
    }
    OS << '\n';
  }

  OS << "Index";
  OS.PadToColumn(10);
  for (unsigned I = 0; I <= Cycles; ++I) {
    if (((I / 10) & 1) == 0)
      OS << I % 10;
    else
      OS << ' ';
  }
  OS << '\n';
}

void TimelineView::printTimeline(raw_ostream &OS) const {
  formatted_raw_ostream FOS(OS);
  printTimelineHeader(FOS, LastCycle);
  FOS.flush();

  // Use a different string stream for the instruction.
  std::string Instruction;
  raw_string_ostream InstrStream(Instruction);

  unsigned IID = 0;
  const unsigned Iterations = Timeline.size() / Source.size();
  for (unsigned Iteration = 0; Iteration < Iterations; ++Iteration) {
    for (const MCInst &Inst : Source) {
      const TimelineViewEntry &Entry = Timeline[IID];
      if (Entry.CycleRetired == 0)
        return;

      unsigned SourceIndex = IID % Source.size();
      printTimelineViewEntry(FOS, Entry, Iteration, SourceIndex);
      // Append the instruction info at the end of the line.
      MCIP.printInst(&Inst, InstrStream, "", STI);
      InstrStream.flush();

      // Consume any tabs or spaces at the beginning of the string.
      StringRef Str(Instruction);
      Str = Str.ltrim();
      FOS << "   " << Str << '\n';
      FOS.flush();
      Instruction = "";

      ++IID;
    }
  }
}
} // namespace mca
} // namespace llvm
