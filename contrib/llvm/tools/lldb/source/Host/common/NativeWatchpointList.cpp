//===-- NativeWatchpointList.cpp --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/common/NativeWatchpointList.h"

#include "lldb/Utility/Log.h"

using namespace lldb;
using namespace lldb_private;

Status NativeWatchpointList::Add(addr_t addr, size_t size, uint32_t watch_flags,
                                 bool hardware) {
  m_watchpoints[addr] = {addr, size, watch_flags, hardware};
  return Status();
}

Status NativeWatchpointList::Remove(addr_t addr) {
  m_watchpoints.erase(addr);
  return Status();
}

const NativeWatchpointList::WatchpointMap &
NativeWatchpointList::GetWatchpointMap() const {
  return m_watchpoints;
}
