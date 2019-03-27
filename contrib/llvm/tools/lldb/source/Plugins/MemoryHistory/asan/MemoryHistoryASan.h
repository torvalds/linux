//===-- MemoryHistoryASan.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_MemoryHistoryASan_h_
#define liblldb_MemoryHistoryASan_h_

#include "lldb/Target/ABI.h"
#include "lldb/Target/MemoryHistory.h"
#include "lldb/Target/Process.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class MemoryHistoryASan : public lldb_private::MemoryHistory {
public:
  ~MemoryHistoryASan() override = default;

  static lldb::MemoryHistorySP
  CreateInstance(const lldb::ProcessSP &process_sp);

  static void Initialize();

  static void Terminate();

  static lldb_private::ConstString GetPluginNameStatic();

  lldb_private::ConstString GetPluginName() override {
    return GetPluginNameStatic();
  }

  uint32_t GetPluginVersion() override { return 1; }

  lldb_private::HistoryThreads GetHistoryThreads(lldb::addr_t address) override;

private:
  MemoryHistoryASan(const lldb::ProcessSP &process_sp);

  lldb::ProcessWP m_process_wp;
};

} // namespace lldb_private

#endif // liblldb_MemoryHistoryASan_h_
