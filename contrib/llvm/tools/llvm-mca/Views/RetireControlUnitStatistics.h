//===--------------------- RetireControlUnitStatistics.h --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines class RetireControlUnitStatistics: a view that knows how
/// to print general statistics related to the retire control unit.
///
/// Example:
/// ========
///
/// Retire Control Unit - number of cycles where we saw N instructions retired:
/// [# retired], [# cycles]
///  0,           109  (17.9%)
///  1,           102  (16.7%)
///  2,           399  (65.4%)
///
/// Total ROB Entries:                64
/// Max Used ROB Entries:             35  ( 54.7% )
/// Average Used ROB Entries per cy:  32  ( 50.0% )
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_MCA_RETIRECONTROLUNITSTATISTICS_H
#define LLVM_TOOLS_LLVM_MCA_RETIRECONTROLUNITSTATISTICS_H

#include "Views/View.h"
#include "llvm/MC/MCSchedule.h"
#include <map>

namespace llvm {
namespace mca {

class RetireControlUnitStatistics : public View {
  using Histogram = std::map<unsigned, unsigned>;
  Histogram RetiredPerCycle;

  unsigned NumRetired;
  unsigned NumCycles;
  unsigned TotalROBEntries;
  unsigned EntriesInUse;
  unsigned MaxUsedEntries;
  unsigned SumOfUsedEntries;

public:
  RetireControlUnitStatistics(const MCSchedModel &SM);

  void onEvent(const HWInstructionEvent &Event) override;
  void onCycleEnd() override;
  void printView(llvm::raw_ostream &OS) const override;
};

} // namespace mca
} // namespace llvm

#endif
