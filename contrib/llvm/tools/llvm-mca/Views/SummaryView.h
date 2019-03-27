//===--------------------- SummaryView.h ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements the summary view.
///
/// The goal of the summary view is to give a very quick overview of the
/// performance throughput. Below is an example of summary view:
///
///
/// Iterations:        300
/// Instructions:      900
/// Total Cycles:      610
/// Dispatch Width:    2
/// IPC:               1.48
/// Block RThroughput: 2.0
///
/// The summary view collects a few performance numbers. The two main
/// performance indicators are 'Total Cycles' and IPC (Instructions Per Cycle).
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_MCA_SUMMARYVIEW_H
#define LLVM_TOOLS_LLVM_MCA_SUMMARYVIEW_H

#include "Views/View.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace mca {

/// A view that collects and prints a few performance numbers.
class SummaryView : public View {
  const llvm::MCSchedModel &SM;
  llvm::ArrayRef<llvm::MCInst> Source;
  const unsigned DispatchWidth;
  unsigned LastInstructionIdx;
  unsigned TotalCycles;
  // The total number of micro opcodes contributed by a block of instructions.
  unsigned NumMicroOps;
  // For each processor resource, this vector stores the cumulative number of
  // resource cycles consumed by the analyzed code block.
  llvm::SmallVector<unsigned, 8> ProcResourceUsage;

  // Each processor resource is associated with a so-called processor resource
  // mask. This vector allows to correlate processor resource IDs with processor
  // resource masks. There is exactly one element per each processor resource
  // declared by the scheduling model.
  llvm::SmallVector<uint64_t, 8> ProcResourceMasks;

  // Compute the reciprocal throughput for the analyzed code block.
  // The reciprocal block throughput is computed as the MAX between:
  //   - NumMicroOps / DispatchWidth
  //   - Total Resource Cycles / #Units   (for every resource consumed).
  double getBlockRThroughput() const;

public:
  SummaryView(const llvm::MCSchedModel &Model, llvm::ArrayRef<llvm::MCInst> S,
              unsigned Width);

  void onCycleEnd() override { ++TotalCycles; }
  void onEvent(const HWInstructionEvent &Event) override;

  void printView(llvm::raw_ostream &OS) const override;
};
} // namespace mca
} // namespace llvm

#endif
