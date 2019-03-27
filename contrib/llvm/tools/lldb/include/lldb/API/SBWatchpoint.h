//===-- SBWatchpoint.h ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBWatchpoint_h_
#define LLDB_SBWatchpoint_h_

#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBWatchpoint {
public:
  SBWatchpoint();

  SBWatchpoint(const lldb::SBWatchpoint &rhs);

  SBWatchpoint(const lldb::WatchpointSP &wp_sp);

  ~SBWatchpoint();

  const lldb::SBWatchpoint &operator=(const lldb::SBWatchpoint &rhs);

  bool IsValid() const;

  SBError GetError();

  watch_id_t GetID();

  /// With -1 representing an invalid hardware index.
  int32_t GetHardwareIndex();

  lldb::addr_t GetWatchAddress();

  size_t GetWatchSize();

  void SetEnabled(bool enabled);

  bool IsEnabled();

  uint32_t GetHitCount();

  uint32_t GetIgnoreCount();

  void SetIgnoreCount(uint32_t n);

  const char *GetCondition();

  void SetCondition(const char *condition);

  bool GetDescription(lldb::SBStream &description, DescriptionLevel level);

  void Clear();

  lldb::WatchpointSP GetSP() const;

  void SetSP(const lldb::WatchpointSP &sp);

  static bool EventIsWatchpointEvent(const lldb::SBEvent &event);

  static lldb::WatchpointEventType
  GetWatchpointEventTypeFromEvent(const lldb::SBEvent &event);

  static lldb::SBWatchpoint GetWatchpointFromEvent(const lldb::SBEvent &event);

private:
  friend class SBTarget;
  friend class SBValue;

  std::weak_ptr<lldb_private::Watchpoint> m_opaque_wp;
};

} // namespace lldb

#endif // LLDB_SBWatchpoint_h_
