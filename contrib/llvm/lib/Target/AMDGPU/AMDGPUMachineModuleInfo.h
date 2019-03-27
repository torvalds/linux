//===--- AMDGPUMachineModuleInfo.h ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// AMDGPU Machine Module Info.
///
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUMACHINEMODULEINFO_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUMACHINEMODULEINFO_H

#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineModuleInfoImpls.h"
#include "llvm/IR/LLVMContext.h"

namespace llvm {

class AMDGPUMachineModuleInfo final : public MachineModuleInfoELF {
private:

  // All supported memory/synchronization scopes can be found here:
  //   http://llvm.org/docs/AMDGPUUsage.html#memory-scopes

  /// Agent synchronization scope ID.
  SyncScope::ID AgentSSID;
  /// Workgroup synchronization scope ID.
  SyncScope::ID WorkgroupSSID;
  /// Wavefront synchronization scope ID.
  SyncScope::ID WavefrontSSID;

  /// In AMDGPU target synchronization scopes are inclusive, meaning a
  /// larger synchronization scope is inclusive of a smaller synchronization
  /// scope.
  ///
  /// \returns \p SSID's inclusion ordering, or "None" if \p SSID is not
  /// supported by the AMDGPU target.
  Optional<uint8_t> getSyncScopeInclusionOrdering(SyncScope::ID SSID) const {
    if (SSID == SyncScope::SingleThread)
      return 0;
    else if (SSID == getWavefrontSSID())
      return 1;
    else if (SSID == getWorkgroupSSID())
      return 2;
    else if (SSID == getAgentSSID())
      return 3;
    else if (SSID == SyncScope::System)
      return 4;

    return None;
  }

public:
  AMDGPUMachineModuleInfo(const MachineModuleInfo &MMI);

  /// \returns Agent synchronization scope ID.
  SyncScope::ID getAgentSSID() const {
    return AgentSSID;
  }
  /// \returns Workgroup synchronization scope ID.
  SyncScope::ID getWorkgroupSSID() const {
    return WorkgroupSSID;
  }
  /// \returns Wavefront synchronization scope ID.
  SyncScope::ID getWavefrontSSID() const {
    return WavefrontSSID;
  }

  /// In AMDGPU target synchronization scopes are inclusive, meaning a
  /// larger synchronization scope is inclusive of a smaller synchronization
  /// scope.
  ///
  /// \returns True if synchronization scope \p A is larger than or equal to
  /// synchronization scope \p B, false if synchronization scope \p A is smaller
  /// than synchronization scope \p B, or "None" if either synchronization scope
  /// \p A or \p B is not supported by the AMDGPU target.
  Optional<bool> isSyncScopeInclusion(SyncScope::ID A, SyncScope::ID B) const {
    const auto &AIO = getSyncScopeInclusionOrdering(A);
    const auto &BIO = getSyncScopeInclusionOrdering(B);
    if (!AIO || !BIO)
      return None;

    return AIO.getValue() > BIO.getValue();
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_AMDGPUMACHINEMODULEINFO_H
