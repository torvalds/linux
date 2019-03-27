//===-- NativeWatchpointList.h ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_NativeWatchpointList_h_
#define liblldb_NativeWatchpointList_h_

#include "lldb/Utility/Status.h"
#include "lldb/lldb-private-forward.h"

#include <map>

namespace lldb_private {
struct NativeWatchpoint {
  lldb::addr_t m_addr;
  size_t m_size;
  uint32_t m_watch_flags;
  bool m_hardware;
};

class NativeWatchpointList {
public:
  Status Add(lldb::addr_t addr, size_t size, uint32_t watch_flags,
             bool hardware);

  Status Remove(lldb::addr_t addr);

  using WatchpointMap = std::map<lldb::addr_t, NativeWatchpoint>;

  const WatchpointMap &GetWatchpointMap() const;

private:
  WatchpointMap m_watchpoints;
};
}

#endif // ifndef liblldb_NativeWatchpointList_h_
