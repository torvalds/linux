//===-- AMDGPUMachineFunctionInfo.cpp ---------------------------------------=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "AMDGPUMachineFunction.h"
#include "AMDGPUSubtarget.h"
#include "AMDGPUPerfHintAnalysis.h"
#include "llvm/CodeGen/MachineModuleInfo.h"

using namespace llvm;

AMDGPUMachineFunction::AMDGPUMachineFunction(const MachineFunction &MF) :
  MachineFunctionInfo(),
  LocalMemoryObjects(),
  ExplicitKernArgSize(0),
  MaxKernArgAlign(0),
  LDSSize(0),
  IsEntryFunction(AMDGPU::isEntryFunctionCC(MF.getFunction().getCallingConv())),
  NoSignedZerosFPMath(MF.getTarget().Options.NoSignedZerosFPMath),
  MemoryBound(false),
  WaveLimiter(false) {
  const AMDGPUSubtarget &ST = AMDGPUSubtarget::get(MF);

  // FIXME: Should initialize KernArgSize based on ExplicitKernelArgOffset,
  // except reserved size is not correctly aligned.
  const Function &F = MF.getFunction();

  if (auto *Resolver = MF.getMMI().getResolver()) {
    if (AMDGPUPerfHintAnalysis *PHA = static_cast<AMDGPUPerfHintAnalysis*>(
          Resolver->getAnalysisIfAvailable(&AMDGPUPerfHintAnalysisID, true))) {
      MemoryBound = PHA->isMemoryBound(&F);
      WaveLimiter = PHA->needsWaveLimiter(&F);
    }
  }

  CallingConv::ID CC = F.getCallingConv();
  if (CC == CallingConv::AMDGPU_KERNEL || CC == CallingConv::SPIR_KERNEL)
    ExplicitKernArgSize = ST.getExplicitKernArgSize(F, MaxKernArgAlign);
}

unsigned AMDGPUMachineFunction::allocateLDSGlobal(const DataLayout &DL,
                                                  const GlobalValue &GV) {
  auto Entry = LocalMemoryObjects.insert(std::make_pair(&GV, 0));
  if (!Entry.second)
    return Entry.first->second;

  unsigned Align = GV.getAlignment();
  if (Align == 0)
    Align = DL.getABITypeAlignment(GV.getValueType());

  /// TODO: We should sort these to minimize wasted space due to alignment
  /// padding. Currently the padding is decided by the first encountered use
  /// during lowering.
  unsigned Offset = LDSSize = alignTo(LDSSize, Align);

  Entry.first->second = Offset;
  LDSSize += DL.getTypeAllocSize(GV.getValueType());

  return Offset;
}
