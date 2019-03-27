//===-- MemoryHistory.h ---------------------------------------------------*-
//C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_MemoryHistory_h_
#define liblldb_MemoryHistory_h_

#include <vector>

#include "lldb/Core/PluginInterface.h"
#include "lldb/lldb-private.h"
#include "lldb/lldb-types.h"

namespace lldb_private {

typedef std::vector<lldb::ThreadSP> HistoryThreads;

class MemoryHistory : public std::enable_shared_from_this<MemoryHistory>,
                      public PluginInterface {
public:
  static lldb::MemoryHistorySP FindPlugin(const lldb::ProcessSP process);

  virtual HistoryThreads GetHistoryThreads(lldb::addr_t address) = 0;
};

} // namespace lldb_private

#endif // liblldb_MemoryHistory_h_
