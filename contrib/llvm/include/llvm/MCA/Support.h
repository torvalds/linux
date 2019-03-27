//===--------------------- Support.h ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Helper functions used by various pipeline components.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_SUPPORT_H
#define LLVM_MCA_SUPPORT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace mca {

template <typename T>
class InstructionError : public ErrorInfo<InstructionError<T>> {
public:
  static char ID;
  std::string Message;
  const T &Inst;

  InstructionError(std::string M, const T &MCI)
      : Message(std::move(M)), Inst(MCI) {}

  void log(raw_ostream &OS) const override { OS << Message; }

  std::error_code convertToErrorCode() const override {
    return inconvertibleErrorCode();
  }
};

template <typename T> char InstructionError<T>::ID;

/// This class represents the number of cycles per resource (fractions of
/// cycles).  That quantity is managed here as a ratio, and accessed via the
/// double cast-operator below.  The two quantities, number of cycles and
/// number of resources, are kept separate.  This is used by the
/// ResourcePressureView to calculate the average resource cycles
/// per instruction/iteration.
class ResourceCycles {
  unsigned Numerator, Denominator;

public:
  ResourceCycles() : Numerator(0), Denominator(1) {}
  ResourceCycles(unsigned Cycles, unsigned ResourceUnits = 1)
      : Numerator(Cycles), Denominator(ResourceUnits) {}

  operator double() const {
    assert(Denominator && "Invalid denominator (must be non-zero).");
    return (Denominator == 1) ? Numerator : (double)Numerator / Denominator;
  }

  // Add the components of RHS to this instance.  Instead of calculating
  // the final value here, we keep track of the numerator and denominator
  // separately, to reduce floating point error.
  ResourceCycles &operator+=(const ResourceCycles &RHS) {
    if (Denominator == RHS.Denominator)
      Numerator += RHS.Numerator;
    else {
      // Create a common denominator for LHS and RHS by calculating the least
      // common multiple from the GCD.
      unsigned GCD = GreatestCommonDivisor64(Denominator, RHS.Denominator);
      unsigned LCM = (Denominator * RHS.Denominator) / GCD;
      unsigned LHSNumerator = Numerator * (LCM / Denominator);
      unsigned RHSNumerator = RHS.Numerator * (LCM / RHS.Denominator);
      Numerator = LHSNumerator + RHSNumerator;
      Denominator = LCM;
    }
    return *this;
  }
};

/// Populates vector Masks with processor resource masks.
///
/// The number of bits set in a mask depends on the processor resource type.
/// Each processor resource mask has at least one bit set. For groups, the
/// number of bits set in the mask is equal to the cardinality of the group plus
/// one. Excluding the most significant bit, the remaining bits in the mask
/// identify processor resources that are part of the group.
///
/// Example:
///
///  ResourceA  -- Mask: 0b001
///  ResourceB  -- Mask: 0b010
///  ResourceAB -- Mask: 0b100 U (ResourceA::Mask | ResourceB::Mask) == 0b111
///
/// ResourceAB is a processor resource group containing ResourceA and ResourceB.
/// Each resource mask uniquely identifies a resource; both ResourceA and
/// ResourceB only have one bit set.
/// ResourceAB is a group; excluding the most significant bit in the mask, the
/// remaining bits identify the composition of the group.
///
/// Resource masks are used by the ResourceManager to solve set membership
/// problems with simple bit manipulation operations.
void computeProcResourceMasks(const MCSchedModel &SM,
                              MutableArrayRef<uint64_t> Masks);

/// Compute the reciprocal block throughput from a set of processor resource
/// cycles. The reciprocal block throughput is computed as the MAX between:
///  - NumMicroOps / DispatchWidth
///  - ProcResourceCycles / #ProcResourceUnits  (for every consumed resource).
double computeBlockRThroughput(const MCSchedModel &SM, unsigned DispatchWidth,
                               unsigned NumMicroOps,
                               ArrayRef<unsigned> ProcResourceUsage);
} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_SUPPORT_H
