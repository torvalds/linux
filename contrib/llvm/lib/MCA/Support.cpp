//===--------------------- Support.cpp --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file implements a few helper functions used by various pipeline
/// components.
///
//===----------------------------------------------------------------------===//

#include "llvm/MCA/Support.h"
#include "llvm/MC/MCSchedule.h"

namespace llvm {
namespace mca {

#define DEBUG_TYPE "llvm-mca"

void computeProcResourceMasks(const MCSchedModel &SM,
                              MutableArrayRef<uint64_t> Masks) {
  unsigned ProcResourceID = 0;

  assert(Masks.size() == SM.getNumProcResourceKinds() &&
         "Invalid number of elements");
  // Resource at index 0 is the 'InvalidUnit'. Set an invalid mask for it.
  Masks[0] = 0;

  // Create a unique bitmask for every processor resource unit.
  for (unsigned I = 1, E = SM.getNumProcResourceKinds(); I < E; ++I) {
    const MCProcResourceDesc &Desc = *SM.getProcResource(I);
    if (Desc.SubUnitsIdxBegin)
      continue;
    Masks[I] = 1ULL << ProcResourceID;
    ProcResourceID++;
  }

  // Create a unique bitmask for every processor resource group.
  for (unsigned I = 1, E = SM.getNumProcResourceKinds(); I < E; ++I) {
    const MCProcResourceDesc &Desc = *SM.getProcResource(I);
    if (!Desc.SubUnitsIdxBegin)
      continue;
    Masks[I] = 1ULL << ProcResourceID;
    for (unsigned U = 0; U < Desc.NumUnits; ++U) {
      uint64_t OtherMask = Masks[Desc.SubUnitsIdxBegin[U]];
      Masks[I] |= OtherMask;
    }
    ProcResourceID++;
  }

#ifndef NDEBUG
  LLVM_DEBUG(dbgs() << "\nProcessor resource masks:"
                    << "\n");
  for (unsigned I = 0, E = SM.getNumProcResourceKinds(); I < E; ++I) {
    const MCProcResourceDesc &Desc = *SM.getProcResource(I);
    LLVM_DEBUG(dbgs() << '[' << I << "] " << Desc.Name << " - " << Masks[I]
                      << '\n');
  }
#endif
}

double computeBlockRThroughput(const MCSchedModel &SM, unsigned DispatchWidth,
                               unsigned NumMicroOps,
                               ArrayRef<unsigned> ProcResourceUsage) {
  // The block throughput is bounded from above by the hardware dispatch
  // throughput. That is because the DispatchWidth is an upper bound on the
  // number of opcodes that can be part of a single dispatch group.
  double Max = static_cast<double>(NumMicroOps) / DispatchWidth;

  // The block throughput is also limited by the amount of hardware parallelism.
  // The number of available resource units affects the resource pressure
  // distribution, as well as how many blocks can be executed every cycle.
  for (unsigned I = 0, E = SM.getNumProcResourceKinds(); I < E; ++I) {
    unsigned ResourceCycles = ProcResourceUsage[I];
    if (!ResourceCycles)
      continue;

    const MCProcResourceDesc &MCDesc = *SM.getProcResource(I);
    double Throughput = static_cast<double>(ResourceCycles) / MCDesc.NumUnits;
    Max = std::max(Max, Throughput);
  }

  // The block reciprocal throughput is computed as the MAX of:
  //  - (NumMicroOps / DispatchWidth)
  //  - (NumUnits / ResourceCycles)   for every consumed processor resource.
  return Max;
}

} // namespace mca
} // namespace llvm
