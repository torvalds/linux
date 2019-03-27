//===--------------------- DispatchStatistics.h -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements a view that prints a few statistics related to the
/// dispatch logic. It collects and analyzes instruction dispatch events as
/// well as static/dynamic dispatch stall events.
///
/// Example:
/// ========
///
/// Dynamic Dispatch Stall Cycles:
/// RAT     - Register unavailable:                      0
/// RCU     - Retire tokens unavailable:                 0
/// SCHEDQ  - Scheduler full:                            42
/// LQ      - Load queue full:                           0
/// SQ      - Store queue full:                          0
/// GROUP   - Static restrictions on the dispatch group: 0
///
///
/// Dispatch Logic - number of cycles where we saw N micro opcodes dispatched:
/// [# dispatched], [# cycles]
///  0,              15  (11.5%)
///  2,              4  (3.1%)
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_MCA_DISPATCHVIEW_H
#define LLVM_TOOLS_LLVM_MCA_DISPATCHVIEW_H

#include "Views/View.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include <map>

namespace llvm {
namespace mca {

class DispatchStatistics : public View {
  unsigned NumDispatched;
  unsigned NumCycles;

  // Counts dispatch stall events caused by unavailability of resources.  There
  // is one counter for every generic stall kind (see class HWStallEvent).
  llvm::SmallVector<unsigned, 8> HWStalls;

  using Histogram = std::map<unsigned, unsigned>;
  Histogram DispatchGroupSizePerCycle;

  void updateHistograms() {
    DispatchGroupSizePerCycle[NumDispatched]++;
    NumDispatched = 0;
  }

  void printDispatchHistogram(llvm::raw_ostream &OS) const;

  void printDispatchStalls(llvm::raw_ostream &OS) const;

public:
  DispatchStatistics()
      : NumDispatched(0), NumCycles(0),
        HWStalls(HWStallEvent::LastGenericEvent) {}

  void onEvent(const HWStallEvent &Event) override;

  void onEvent(const HWInstructionEvent &Event) override;

  void onCycleBegin() override { NumCycles++; }

  void onCycleEnd() override { updateHistograms(); }

  void printView(llvm::raw_ostream &OS) const override {
    printDispatchStalls(OS);
    printDispatchHistogram(OS);
  }
};
} // namespace mca
} // namespace llvm

#endif
