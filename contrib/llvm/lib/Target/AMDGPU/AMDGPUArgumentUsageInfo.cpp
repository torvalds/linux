//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUArgumentUsageInfo.h"
#include "SIRegisterInfo.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "amdgpu-argument-reg-usage-info"

INITIALIZE_PASS(AMDGPUArgumentUsageInfo, DEBUG_TYPE,
                "Argument Register Usage Information Storage", false, true)

void ArgDescriptor::print(raw_ostream &OS,
                          const TargetRegisterInfo *TRI) const {
  if (!isSet()) {
    OS << "<not set>\n";
    return;
  }

  if (isRegister())
    OS << "Reg " << printReg(getRegister(), TRI) << '\n';
  else
    OS << "Stack offset " << getStackOffset() << '\n';
}

char AMDGPUArgumentUsageInfo::ID = 0;

const AMDGPUFunctionArgInfo AMDGPUArgumentUsageInfo::ExternFunctionInfo{};

bool AMDGPUArgumentUsageInfo::doInitialization(Module &M) {
  return false;
}

bool AMDGPUArgumentUsageInfo::doFinalization(Module &M) {
  ArgInfoMap.clear();
  return false;
}

void AMDGPUArgumentUsageInfo::print(raw_ostream &OS, const Module *M) const {
  for (const auto &FI : ArgInfoMap) {
    OS << "Arguments for " << FI.first->getName() << '\n'
       << "  PrivateSegmentBuffer: " << FI.second.PrivateSegmentBuffer
       << "  DispatchPtr: " << FI.second.DispatchPtr
       << "  QueuePtr: " << FI.second.QueuePtr
       << "  KernargSegmentPtr: " << FI.second.KernargSegmentPtr
       << "  DispatchID: " << FI.second.DispatchID
       << "  FlatScratchInit: " << FI.second.FlatScratchInit
       << "  PrivateSegmentSize: " << FI.second.PrivateSegmentSize
       << "  WorkGroupIDX: " << FI.second.WorkGroupIDX
       << "  WorkGroupIDY: " << FI.second.WorkGroupIDY
       << "  WorkGroupIDZ: " << FI.second.WorkGroupIDZ
       << "  WorkGroupInfo: " << FI.second.WorkGroupInfo
       << "  PrivateSegmentWaveByteOffset: "
          << FI.second.PrivateSegmentWaveByteOffset
       << "  ImplicitBufferPtr: " << FI.second.ImplicitBufferPtr
       << "  ImplicitArgPtr: " << FI.second.ImplicitArgPtr
       << "  WorkItemIDX " << FI.second.WorkItemIDX
       << "  WorkItemIDY " << FI.second.WorkItemIDY
       << "  WorkItemIDZ " << FI.second.WorkItemIDZ
       << '\n';
  }
}

std::pair<const ArgDescriptor *, const TargetRegisterClass *>
AMDGPUFunctionArgInfo::getPreloadedValue(
  AMDGPUFunctionArgInfo::PreloadedValue Value) const {
  switch (Value) {
  case AMDGPUFunctionArgInfo::PRIVATE_SEGMENT_BUFFER: {
    return std::make_pair(
      PrivateSegmentBuffer ? &PrivateSegmentBuffer : nullptr,
      &AMDGPU::SGPR_128RegClass);
  }
  case AMDGPUFunctionArgInfo::IMPLICIT_BUFFER_PTR:
    return std::make_pair(ImplicitBufferPtr ? &ImplicitBufferPtr : nullptr,
                          &AMDGPU::SGPR_64RegClass);
  case AMDGPUFunctionArgInfo::WORKGROUP_ID_X:
    return std::make_pair(WorkGroupIDX ? &WorkGroupIDX : nullptr,
                          &AMDGPU::SGPR_32RegClass);

  case AMDGPUFunctionArgInfo::WORKGROUP_ID_Y:
    return std::make_pair(WorkGroupIDY ? &WorkGroupIDY : nullptr,
                          &AMDGPU::SGPR_32RegClass);
  case AMDGPUFunctionArgInfo::WORKGROUP_ID_Z:
    return std::make_pair(WorkGroupIDZ ? &WorkGroupIDZ : nullptr,
                          &AMDGPU::SGPR_32RegClass);
  case AMDGPUFunctionArgInfo::PRIVATE_SEGMENT_WAVE_BYTE_OFFSET:
    return std::make_pair(
      PrivateSegmentWaveByteOffset ? &PrivateSegmentWaveByteOffset : nullptr,
      &AMDGPU::SGPR_32RegClass);
  case AMDGPUFunctionArgInfo::KERNARG_SEGMENT_PTR:
    return std::make_pair(KernargSegmentPtr ? &KernargSegmentPtr : nullptr,
                          &AMDGPU::SGPR_64RegClass);
  case AMDGPUFunctionArgInfo::IMPLICIT_ARG_PTR:
    return std::make_pair(ImplicitArgPtr ? &ImplicitArgPtr : nullptr,
                          &AMDGPU::SGPR_64RegClass);
  case AMDGPUFunctionArgInfo::DISPATCH_ID:
    return std::make_pair(DispatchID ? &DispatchID : nullptr,
                          &AMDGPU::SGPR_64RegClass);
  case AMDGPUFunctionArgInfo::FLAT_SCRATCH_INIT:
    return std::make_pair(FlatScratchInit ? &FlatScratchInit : nullptr,
                          &AMDGPU::SGPR_64RegClass);
  case AMDGPUFunctionArgInfo::DISPATCH_PTR:
    return std::make_pair(DispatchPtr ? &DispatchPtr : nullptr,
                          &AMDGPU::SGPR_64RegClass);
  case AMDGPUFunctionArgInfo::QUEUE_PTR:
    return std::make_pair(QueuePtr ? &QueuePtr : nullptr,
                          &AMDGPU::SGPR_64RegClass);
  case AMDGPUFunctionArgInfo::WORKITEM_ID_X:
    return std::make_pair(WorkItemIDX ? &WorkItemIDX : nullptr,
                          &AMDGPU::VGPR_32RegClass);
  case AMDGPUFunctionArgInfo::WORKITEM_ID_Y:
    return std::make_pair(WorkItemIDY ? &WorkItemIDY : nullptr,
                          &AMDGPU::VGPR_32RegClass);
  case AMDGPUFunctionArgInfo::WORKITEM_ID_Z:
    return std::make_pair(WorkItemIDZ ? &WorkItemIDZ : nullptr,
                          &AMDGPU::VGPR_32RegClass);
  }
  llvm_unreachable("unexpected preloaded value type");
}
