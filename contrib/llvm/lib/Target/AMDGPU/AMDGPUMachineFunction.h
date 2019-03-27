//===-- AMDGPUMachineFunctionInfo.h -------------------------------*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUMACHINEFUNCTION_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUMACHINEFUNCTION_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

class GCNSubtarget;

class AMDGPUMachineFunction : public MachineFunctionInfo {
  /// A map to keep track of local memory objects and their offsets within the
  /// local memory space.
  SmallDenseMap<const GlobalValue *, unsigned, 4> LocalMemoryObjects;

protected:
  uint64_t ExplicitKernArgSize; // Cache for this.
  unsigned MaxKernArgAlign; // Cache for this.

  /// Number of bytes in the LDS that are being used.
  unsigned LDSSize;

  // Kernels + shaders. i.e. functions called by the driver and not called
  // by other functions.
  bool IsEntryFunction;

  bool NoSignedZerosFPMath;

  // Function may be memory bound.
  bool MemoryBound;

  // Kernel may need limited waves per EU for better performance.
  bool WaveLimiter;

public:
  AMDGPUMachineFunction(const MachineFunction &MF);

  uint64_t getExplicitKernArgSize() const {
    return ExplicitKernArgSize;
  }

  unsigned getMaxKernArgAlign() const {
    return MaxKernArgAlign;
  }

  unsigned getLDSSize() const {
    return LDSSize;
  }

  bool isEntryFunction() const {
    return IsEntryFunction;
  }

  bool hasNoSignedZerosFPMath() const {
    return NoSignedZerosFPMath;
  }

  bool isMemoryBound() const {
    return MemoryBound;
  }

  bool needsWaveLimiter() const {
    return WaveLimiter;
  }

  unsigned allocateLDSGlobal(const DataLayout &DL, const GlobalValue &GV);
};

}
#endif
