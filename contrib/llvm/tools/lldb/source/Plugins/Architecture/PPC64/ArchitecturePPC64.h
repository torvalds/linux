//===-- ArchitecturePPC64.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGIN_ARCHITECTURE_PPC64_H
#define LLDB_PLUGIN_ARCHITECTURE_PPC64_H

#include "lldb/Core/Architecture.h"

namespace lldb_private {

class ArchitecturePPC64 : public Architecture {
public:
  static ConstString GetPluginNameStatic();
  static void Initialize();
  static void Terminate();

  ConstString GetPluginName() override;
  uint32_t GetPluginVersion() override;

  void OverrideStopInfo(Thread &thread) const override {}

  //------------------------------------------------------------------
  /// This method compares current address with current function's
  /// local entry point, returning the bytes to skip if they match.
  //------------------------------------------------------------------
  size_t GetBytesToSkip(Symbol &func, const Address &curr_addr) const override;

  void AdjustBreakpointAddress(const Symbol &func,
                               Address &addr) const override;

private:
  static std::unique_ptr<Architecture> Create(const ArchSpec &arch);
  ArchitecturePPC64() = default;
};

} // namespace lldb_private

#endif // LLDB_PLUGIN_ARCHITECTURE_PPC64_H
