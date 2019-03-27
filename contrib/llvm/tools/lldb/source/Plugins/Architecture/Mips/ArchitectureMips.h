//===-- ArchitectureMips.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGIN_ARCHITECTURE_MIPS_H
#define LLDB_PLUGIN_ARCHITECTURE_MIPS_H

#include "lldb/Core/Architecture.h"
#include "lldb/Utility/ArchSpec.h"

namespace lldb_private {

class ArchitectureMips : public Architecture {
public:
  static ConstString GetPluginNameStatic();
  static void Initialize();
  static void Terminate();

  ConstString GetPluginName() override;
  uint32_t GetPluginVersion() override;

  void OverrideStopInfo(Thread &thread) const override {}

  lldb::addr_t GetBreakableLoadAddress(lldb::addr_t addr,
                                       Target &) const override;

  lldb::addr_t GetCallableLoadAddress(lldb::addr_t load_addr,
                                      AddressClass addr_class) const override;

  lldb::addr_t GetOpcodeLoadAddress(lldb::addr_t load_addr,
                                    AddressClass addr_class) const override;

private:
  Instruction *GetInstructionAtAddress(const ExecutionContext &exe_ctx,
                                       const Address &resolved_addr,
                                       lldb::addr_t symbol_offset) const;


  static std::unique_ptr<Architecture> Create(const ArchSpec &arch);
  ArchitectureMips(const ArchSpec &arch) : m_arch(arch) {}

  ArchSpec m_arch;
};

} // namespace lldb_private

#endif // LLDB_PLUGIN_ARCHITECTURE_MIPS_H
