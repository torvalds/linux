//===-- SBWatchpoint.cpp --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBWatchpoint.h"
#include "lldb/API/SBAddress.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBDefines.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBStream.h"

#include "lldb/Breakpoint/Watchpoint.h"
#include "lldb/Breakpoint/WatchpointList.h"
#include "lldb/Core/StreamFile.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-types.h"

using namespace lldb;
using namespace lldb_private;

SBWatchpoint::SBWatchpoint() {}

SBWatchpoint::SBWatchpoint(const lldb::WatchpointSP &wp_sp)
    : m_opaque_wp(wp_sp) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (log) {
    SBStream sstr;
    GetDescription(sstr, lldb::eDescriptionLevelBrief);
    LLDB_LOG(log, "watchpoint = {0} ({1})", wp_sp.get(), sstr.GetData());
  }
}

SBWatchpoint::SBWatchpoint(const SBWatchpoint &rhs)
    : m_opaque_wp(rhs.m_opaque_wp) {}

const SBWatchpoint &SBWatchpoint::operator=(const SBWatchpoint &rhs) {
  m_opaque_wp = rhs.m_opaque_wp;
  return *this;
}

SBWatchpoint::~SBWatchpoint() {}

watch_id_t SBWatchpoint::GetID() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  watch_id_t watch_id = LLDB_INVALID_WATCH_ID;
  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp)
    watch_id = watchpoint_sp->GetID();

  if (log) {
    if (watch_id == LLDB_INVALID_WATCH_ID)
      log->Printf("SBWatchpoint(%p)::GetID () => LLDB_INVALID_WATCH_ID",
                  static_cast<void *>(watchpoint_sp.get()));
    else
      log->Printf("SBWatchpoint(%p)::GetID () => %u",
                  static_cast<void *>(watchpoint_sp.get()), watch_id);
  }

  return watch_id;
}

bool SBWatchpoint::IsValid() const { return bool(m_opaque_wp.lock()); }

SBError SBWatchpoint::GetError() {
  SBError sb_error;
  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    sb_error.SetError(watchpoint_sp->GetError());
  }
  return sb_error;
}

int32_t SBWatchpoint::GetHardwareIndex() {
  int32_t hw_index = -1;

  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        watchpoint_sp->GetTarget().GetAPIMutex());
    hw_index = watchpoint_sp->GetHardwareIndex();
  }

  return hw_index;
}

addr_t SBWatchpoint::GetWatchAddress() {
  addr_t ret_addr = LLDB_INVALID_ADDRESS;

  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        watchpoint_sp->GetTarget().GetAPIMutex());
    ret_addr = watchpoint_sp->GetLoadAddress();
  }

  return ret_addr;
}

size_t SBWatchpoint::GetWatchSize() {
  size_t watch_size = 0;

  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        watchpoint_sp->GetTarget().GetAPIMutex());
    watch_size = watchpoint_sp->GetByteSize();
  }

  return watch_size;
}

void SBWatchpoint::SetEnabled(bool enabled) {
  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    Target &target = watchpoint_sp->GetTarget();
    std::lock_guard<std::recursive_mutex> guard(target.GetAPIMutex());
    ProcessSP process_sp = target.GetProcessSP();
    const bool notify = true;
    if (process_sp) {
      if (enabled)
        process_sp->EnableWatchpoint(watchpoint_sp.get(), notify);
      else
        process_sp->DisableWatchpoint(watchpoint_sp.get(), notify);
    } else {
      watchpoint_sp->SetEnabled(enabled, notify);
    }
  }
}

bool SBWatchpoint::IsEnabled() {
  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        watchpoint_sp->GetTarget().GetAPIMutex());
    return watchpoint_sp->IsEnabled();
  } else
    return false;
}

uint32_t SBWatchpoint::GetHitCount() {
  uint32_t count = 0;
  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        watchpoint_sp->GetTarget().GetAPIMutex());
    count = watchpoint_sp->GetHitCount();
  }

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBWatchpoint(%p)::GetHitCount () => %u",
                static_cast<void *>(watchpoint_sp.get()), count);

  return count;
}

uint32_t SBWatchpoint::GetIgnoreCount() {
  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        watchpoint_sp->GetTarget().GetAPIMutex());
    return watchpoint_sp->GetIgnoreCount();
  } else
    return 0;
}

void SBWatchpoint::SetIgnoreCount(uint32_t n) {
  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        watchpoint_sp->GetTarget().GetAPIMutex());
    watchpoint_sp->SetIgnoreCount(n);
  }
}

const char *SBWatchpoint::GetCondition() {
  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        watchpoint_sp->GetTarget().GetAPIMutex());
    return watchpoint_sp->GetConditionText();
  }
  return NULL;
}

void SBWatchpoint::SetCondition(const char *condition) {
  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        watchpoint_sp->GetTarget().GetAPIMutex());
    watchpoint_sp->SetCondition(condition);
  }
}

bool SBWatchpoint::GetDescription(SBStream &description,
                                  DescriptionLevel level) {
  Stream &strm = description.ref();

  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        watchpoint_sp->GetTarget().GetAPIMutex());
    watchpoint_sp->GetDescription(&strm, level);
    strm.EOL();
  } else
    strm.PutCString("No value");

  return true;
}

void SBWatchpoint::Clear() { m_opaque_wp.reset(); }

lldb::WatchpointSP SBWatchpoint::GetSP() const { return m_opaque_wp.lock(); }

void SBWatchpoint::SetSP(const lldb::WatchpointSP &sp) { m_opaque_wp = sp; }

bool SBWatchpoint::EventIsWatchpointEvent(const lldb::SBEvent &event) {
  return Watchpoint::WatchpointEventData::GetEventDataFromEvent(event.get()) !=
         NULL;
}

WatchpointEventType
SBWatchpoint::GetWatchpointEventTypeFromEvent(const SBEvent &event) {
  if (event.IsValid())
    return Watchpoint::WatchpointEventData::GetWatchpointEventTypeFromEvent(
        event.GetSP());
  return eWatchpointEventTypeInvalidType;
}

SBWatchpoint SBWatchpoint::GetWatchpointFromEvent(const lldb::SBEvent &event) {
  SBWatchpoint sb_watchpoint;
  if (event.IsValid())
    sb_watchpoint =
        Watchpoint::WatchpointEventData::GetWatchpointFromEvent(event.GetSP());
  return sb_watchpoint;
}
