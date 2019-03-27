//===- lib/CodeGen/CalcSpillWeights.h ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_CALCSPILLWEIGHTS_H
#define LLVM_CODEGEN_CALCSPILLWEIGHTS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/SlotIndexes.h"

namespace llvm {

class LiveInterval;
class LiveIntervals;
class MachineBlockFrequencyInfo;
class MachineFunction;
class MachineLoopInfo;
class VirtRegMap;

  /// Normalize the spill weight of a live interval
  ///
  /// The spill weight of a live interval is computed as:
  ///
  ///   (sum(use freq) + sum(def freq)) / (K + size)
  ///
  /// @param UseDefFreq Expected number of executed use and def instructions
  ///                   per function call. Derived from block frequencies.
  /// @param Size       Size of live interval as returnexd by getSize()
  /// @param NumInstr   Number of instructions using this live interval
  static inline float normalizeSpillWeight(float UseDefFreq, unsigned Size,
                                           unsigned NumInstr) {
    // The constant 25 instructions is added to avoid depending too much on
    // accidental SlotIndex gaps for small intervals. The effect is that small
    // intervals have a spill weight that is mostly proportional to the number
    // of uses, while large intervals get a spill weight that is closer to a use
    // density.
    return UseDefFreq / (Size + 25*SlotIndex::InstrDist);
  }

  /// Calculate auxiliary information for a virtual register such as its
  /// spill weight and allocation hint.
  class VirtRegAuxInfo {
  public:
    using NormalizingFn = float (*)(float, unsigned, unsigned);

  private:
    MachineFunction &MF;
    LiveIntervals &LIS;
    VirtRegMap *VRM;
    const MachineLoopInfo &Loops;
    const MachineBlockFrequencyInfo &MBFI;
    DenseMap<unsigned, float> Hint;
    NormalizingFn normalize;

  public:
    VirtRegAuxInfo(MachineFunction &mf, LiveIntervals &lis,
                   VirtRegMap *vrm, const MachineLoopInfo &loops,
                   const MachineBlockFrequencyInfo &mbfi,
                   NormalizingFn norm = normalizeSpillWeight)
        : MF(mf), LIS(lis), VRM(vrm), Loops(loops), MBFI(mbfi), normalize(norm) {}

    /// (re)compute li's spill weight and allocation hint.
    void calculateSpillWeightAndHint(LiveInterval &li);

    /// Compute future expected spill weight of a split artifact of li
    /// that will span between start and end slot indexes.
    /// \param li     The live interval to be split.
    /// \param start  The expected begining of the split artifact. Instructions
    ///               before start will not affect the weight.
    /// \param end    The expected end of the split artifact. Instructions
    ///               after end will not affect the weight.
    /// \return The expected spill weight of the split artifact. Returns
    /// negative weight for unspillable li.
    float futureWeight(LiveInterval &li, SlotIndex start, SlotIndex end);

    /// Helper function for weight calculations.
    /// (Re)compute li's spill weight and allocation hint, or, for non null
    /// start and end - compute future expected spill weight of a split
    /// artifact of li that will span between start and end slot indexes.
    /// \param li     The live interval for which to compute the weight.
    /// \param start  The expected begining of the split artifact. Instructions
    ///               before start will not affect the weight. Relevant for
    ///               weight calculation of future split artifact.
    /// \param end    The expected end of the split artifact. Instructions
    ///               after end will not affect the weight. Relevant for
    ///               weight calculation of future split artifact.
    /// \return The spill weight. Returns negative weight for unspillable li.
    float weightCalcHelper(LiveInterval &li, SlotIndex *start = nullptr,
                           SlotIndex *end = nullptr);
  };

  /// Compute spill weights and allocation hints for all virtual register
  /// live intervals.
  void calculateSpillWeightsAndHints(LiveIntervals &LIS, MachineFunction &MF,
                                     VirtRegMap *VRM,
                                     const MachineLoopInfo &MLI,
                                     const MachineBlockFrequencyInfo &MBFI,
                                     VirtRegAuxInfo::NormalizingFn norm =
                                         normalizeSpillWeight);

} // end namespace llvm

#endif // LLVM_CODEGEN_CALCSPILLWEIGHTS_H
