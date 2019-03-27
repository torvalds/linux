//===-- SBBroadcaster.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Broadcaster.h"
#include "lldb/Utility/Log.h"

#include "lldb/API/SBBroadcaster.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBListener.h"

using namespace lldb;
using namespace lldb_private;

SBBroadcaster::SBBroadcaster() : m_opaque_sp(), m_opaque_ptr(NULL) {}

SBBroadcaster::SBBroadcaster(const char *name)
    : m_opaque_sp(new Broadcaster(NULL, name)), m_opaque_ptr(NULL) {
  m_opaque_ptr = m_opaque_sp.get();
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  LLDB_LOGV(log, "(name=\"{0}\") => SBBroadcaster({1})", name, m_opaque_ptr);
}

SBBroadcaster::SBBroadcaster(lldb_private::Broadcaster *broadcaster, bool owns)
    : m_opaque_sp(owns ? broadcaster : NULL), m_opaque_ptr(broadcaster) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  LLDB_LOGV(log, "(broadcaster={0}, owns={1}) => SBBroadcaster({2})",
            broadcaster, owns, m_opaque_ptr);
}

SBBroadcaster::SBBroadcaster(const SBBroadcaster &rhs)
    : m_opaque_sp(rhs.m_opaque_sp), m_opaque_ptr(rhs.m_opaque_ptr) {}

const SBBroadcaster &SBBroadcaster::operator=(const SBBroadcaster &rhs) {
  if (this != &rhs) {
    m_opaque_sp = rhs.m_opaque_sp;
    m_opaque_ptr = rhs.m_opaque_ptr;
  }
  return *this;
}

SBBroadcaster::~SBBroadcaster() { reset(NULL, false); }

void SBBroadcaster::BroadcastEventByType(uint32_t event_type, bool unique) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (log)
    log->Printf("SBBroadcaster(%p)::BroadcastEventByType (event_type=0x%8.8x, "
                "unique=%i)",
                static_cast<void *>(m_opaque_ptr), event_type, unique);

  if (m_opaque_ptr == NULL)
    return;

  if (unique)
    m_opaque_ptr->BroadcastEventIfUnique(event_type);
  else
    m_opaque_ptr->BroadcastEvent(event_type);
}

void SBBroadcaster::BroadcastEvent(const SBEvent &event, bool unique) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));

  if (log)
    log->Printf(
        "SBBroadcaster(%p)::BroadcastEventByType (SBEvent(%p), unique=%i)",
        static_cast<void *>(m_opaque_ptr), static_cast<void *>(event.get()),
        unique);

  if (m_opaque_ptr == NULL)
    return;

  EventSP event_sp = event.GetSP();
  if (unique)
    m_opaque_ptr->BroadcastEventIfUnique(event_sp);
  else
    m_opaque_ptr->BroadcastEvent(event_sp);
}

void SBBroadcaster::AddInitialEventsToListener(const SBListener &listener,
                                               uint32_t requested_events) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_API));
  if (log)
    log->Printf("SBBroadcaster(%p)::AddInitialEventsToListener "
                "(SBListener(%p), event_mask=0x%8.8x)",
                static_cast<void *>(m_opaque_ptr),
                static_cast<void *>(listener.get()), requested_events);
  if (m_opaque_ptr)
    m_opaque_ptr->AddInitialEventsToListener(listener.m_opaque_sp,
                                             requested_events);
}

uint32_t SBBroadcaster::AddListener(const SBListener &listener,
                                    uint32_t event_mask) {
  if (m_opaque_ptr)
    return m_opaque_ptr->AddListener(listener.m_opaque_sp, event_mask);
  return 0;
}

const char *SBBroadcaster::GetName() const {
  if (m_opaque_ptr)
    return m_opaque_ptr->GetBroadcasterName().GetCString();
  return NULL;
}

bool SBBroadcaster::EventTypeHasListeners(uint32_t event_type) {
  if (m_opaque_ptr)
    return m_opaque_ptr->EventTypeHasListeners(event_type);
  return false;
}

bool SBBroadcaster::RemoveListener(const SBListener &listener,
                                   uint32_t event_mask) {
  if (m_opaque_ptr)
    return m_opaque_ptr->RemoveListener(listener.m_opaque_sp, event_mask);
  return false;
}

Broadcaster *SBBroadcaster::get() const { return m_opaque_ptr; }

void SBBroadcaster::reset(Broadcaster *broadcaster, bool owns) {
  if (owns)
    m_opaque_sp.reset(broadcaster);
  else
    m_opaque_sp.reset();
  m_opaque_ptr = broadcaster;
}

bool SBBroadcaster::IsValid() const { return m_opaque_ptr != NULL; }

void SBBroadcaster::Clear() {
  m_opaque_sp.reset();
  m_opaque_ptr = NULL;
}

bool SBBroadcaster::operator==(const SBBroadcaster &rhs) const {
  return m_opaque_ptr == rhs.m_opaque_ptr;
}

bool SBBroadcaster::operator!=(const SBBroadcaster &rhs) const {
  return m_opaque_ptr != rhs.m_opaque_ptr;
}

bool SBBroadcaster::operator<(const SBBroadcaster &rhs) const {
  return m_opaque_ptr < rhs.m_opaque_ptr;
}
