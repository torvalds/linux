//===-- Listener.cpp --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Listener.h"

#include "lldb/Utility/Broadcaster.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Event.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Logging.h"

#include "llvm/ADT/Optional.h"

#include <algorithm>
#include <memory>
#include <utility>

using namespace lldb;
using namespace lldb_private;

namespace {
class BroadcasterManagerWPMatcher {
public:
  BroadcasterManagerWPMatcher(BroadcasterManagerSP manager_sp)
      : m_manager_sp(manager_sp) {}
  bool operator()(const BroadcasterManagerWP input_wp) const {
    BroadcasterManagerSP input_sp = input_wp.lock();
    return (input_sp && input_sp == m_manager_sp);
  }

  BroadcasterManagerSP m_manager_sp;
};
} // anonymous namespace

Listener::Listener(const char *name)
    : m_name(name), m_broadcasters(), m_broadcasters_mutex(), m_events(),
      m_events_mutex() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_OBJECT));
  if (log != nullptr)
    log->Printf("%p Listener::Listener('%s')", static_cast<void *>(this),
                m_name.c_str());
}

Listener::~Listener() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_OBJECT));

  Clear();

  if (log)
    log->Printf("%p Listener::%s('%s')", static_cast<void *>(this),
                __FUNCTION__, m_name.c_str());
}

void Listener::Clear() {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_OBJECT));
  std::lock_guard<std::recursive_mutex> broadcasters_guard(
      m_broadcasters_mutex);
  broadcaster_collection::iterator pos, end = m_broadcasters.end();
  for (pos = m_broadcasters.begin(); pos != end; ++pos) {
    Broadcaster::BroadcasterImplSP broadcaster_sp(pos->first.lock());
    if (broadcaster_sp)
      broadcaster_sp->RemoveListener(this, pos->second.event_mask);
  }
  m_broadcasters.clear();

  std::lock_guard<std::mutex> events_guard(m_events_mutex);
  m_events.clear();
  size_t num_managers = m_broadcaster_managers.size();

  for (size_t i = 0; i < num_managers; i++) {
    BroadcasterManagerSP manager_sp(m_broadcaster_managers[i].lock());
    if (manager_sp)
      manager_sp->RemoveListener(this);
  }

  if (log)
    log->Printf("%p Listener::%s('%s')", static_cast<void *>(this),
                __FUNCTION__, m_name.c_str());
}

uint32_t Listener::StartListeningForEvents(Broadcaster *broadcaster,
                                           uint32_t event_mask) {
  if (broadcaster) {
    // Scope for "locker"
    // Tell the broadcaster to add this object as a listener
    {
      std::lock_guard<std::recursive_mutex> broadcasters_guard(
          m_broadcasters_mutex);
      Broadcaster::BroadcasterImplWP impl_wp(broadcaster->GetBroadcasterImpl());
      m_broadcasters.insert(
          std::make_pair(impl_wp, BroadcasterInfo(event_mask)));
    }

    uint32_t acquired_mask =
        broadcaster->AddListener(this->shared_from_this(), event_mask);

    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EVENTS));
    if (log != nullptr)
      log->Printf("%p Listener::StartListeningForEvents (broadcaster = %p, "
                  "mask = 0x%8.8x) acquired_mask = 0x%8.8x for %s",
                  static_cast<void *>(this), static_cast<void *>(broadcaster),
                  event_mask, acquired_mask, m_name.c_str());

    return acquired_mask;
  }
  return 0;
}

uint32_t Listener::StartListeningForEvents(Broadcaster *broadcaster,
                                           uint32_t event_mask,
                                           HandleBroadcastCallback callback,
                                           void *callback_user_data) {
  if (broadcaster) {
    // Scope for "locker"
    // Tell the broadcaster to add this object as a listener
    {
      std::lock_guard<std::recursive_mutex> broadcasters_guard(
          m_broadcasters_mutex);
      Broadcaster::BroadcasterImplWP impl_wp(broadcaster->GetBroadcasterImpl());
      m_broadcasters.insert(std::make_pair(
          impl_wp, BroadcasterInfo(event_mask, callback, callback_user_data)));
    }

    uint32_t acquired_mask =
        broadcaster->AddListener(this->shared_from_this(), event_mask);

    Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EVENTS));
    if (log != nullptr) {
      void **pointer = reinterpret_cast<void **>(&callback);
      log->Printf("%p Listener::StartListeningForEvents (broadcaster = %p, "
                  "mask = 0x%8.8x, callback = %p, user_data = %p) "
                  "acquired_mask = 0x%8.8x for %s",
                  static_cast<void *>(this), static_cast<void *>(broadcaster),
                  event_mask, *pointer, static_cast<void *>(callback_user_data),
                  acquired_mask, m_name.c_str());
    }

    return acquired_mask;
  }
  return 0;
}

bool Listener::StopListeningForEvents(Broadcaster *broadcaster,
                                      uint32_t event_mask) {
  if (broadcaster) {
    // Scope for "locker"
    {
      std::lock_guard<std::recursive_mutex> broadcasters_guard(
          m_broadcasters_mutex);
      m_broadcasters.erase(broadcaster->GetBroadcasterImpl());
    }
    // Remove the broadcaster from our set of broadcasters
    return broadcaster->RemoveListener(this->shared_from_this(), event_mask);
  }

  return false;
}

// Called when a Broadcaster is in its destructor. We need to remove all
// knowledge of this broadcaster and any events that it may have queued up
void Listener::BroadcasterWillDestruct(Broadcaster *broadcaster) {
  // Scope for "broadcasters_locker"
  {
    std::lock_guard<std::recursive_mutex> broadcasters_guard(
        m_broadcasters_mutex);
    m_broadcasters.erase(broadcaster->GetBroadcasterImpl());
  }

  // Scope for "event_locker"
  {
    std::lock_guard<std::mutex> events_guard(m_events_mutex);
    // Remove all events for this broadcaster object.
    event_collection::iterator pos = m_events.begin();
    while (pos != m_events.end()) {
      if ((*pos)->GetBroadcaster() == broadcaster)
        pos = m_events.erase(pos);
      else
        ++pos;
    }
  }
}

void Listener::BroadcasterManagerWillDestruct(BroadcasterManagerSP manager_sp) {
  // Just need to remove this broadcast manager from the list of managers:
  broadcaster_manager_collection::iterator iter,
      end_iter = m_broadcaster_managers.end();
  BroadcasterManagerWP manager_wp;

  BroadcasterManagerWPMatcher matcher(manager_sp);
  iter = std::find_if<broadcaster_manager_collection::iterator,
                      BroadcasterManagerWPMatcher>(
      m_broadcaster_managers.begin(), end_iter, matcher);
  if (iter != end_iter)
    m_broadcaster_managers.erase(iter);
}

void Listener::AddEvent(EventSP &event_sp) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EVENTS));
  if (log != nullptr)
    log->Printf("%p Listener('%s')::AddEvent (event_sp = {%p})",
                static_cast<void *>(this), m_name.c_str(),
                static_cast<void *>(event_sp.get()));

  std::lock_guard<std::mutex> guard(m_events_mutex);
  m_events.push_back(event_sp);
  m_events_condition.notify_all();
}

class EventBroadcasterMatches {
public:
  EventBroadcasterMatches(Broadcaster *broadcaster)
      : m_broadcaster(broadcaster) {}

  bool operator()(const EventSP &event_sp) const {
    return event_sp->BroadcasterIs(m_broadcaster);
  }

private:
  Broadcaster *m_broadcaster;
};

class EventMatcher {
public:
  EventMatcher(Broadcaster *broadcaster, const ConstString *broadcaster_names,
               uint32_t num_broadcaster_names, uint32_t event_type_mask)
      : m_broadcaster(broadcaster), m_broadcaster_names(broadcaster_names),
        m_num_broadcaster_names(num_broadcaster_names),
        m_event_type_mask(event_type_mask) {}

  bool operator()(const EventSP &event_sp) const {
    if (m_broadcaster && !event_sp->BroadcasterIs(m_broadcaster))
      return false;

    if (m_broadcaster_names) {
      bool found_source = false;
      const ConstString &event_broadcaster_name =
          event_sp->GetBroadcaster()->GetBroadcasterName();
      for (uint32_t i = 0; i < m_num_broadcaster_names; ++i) {
        if (m_broadcaster_names[i] == event_broadcaster_name) {
          found_source = true;
          break;
        }
      }
      if (!found_source)
        return false;
    }

    return m_event_type_mask == 0 || m_event_type_mask & event_sp->GetType();
  }

private:
  Broadcaster *m_broadcaster;
  const ConstString *m_broadcaster_names;
  const uint32_t m_num_broadcaster_names;
  const uint32_t m_event_type_mask;
};

bool Listener::FindNextEventInternal(
    std::unique_lock<std::mutex> &lock,
    Broadcaster *broadcaster,             // nullptr for any broadcaster
    const ConstString *broadcaster_names, // nullptr for any event
    uint32_t num_broadcaster_names, uint32_t event_type_mask, EventSP &event_sp,
    bool remove) {
  // NOTE: callers of this function must lock m_events_mutex using a
  // Mutex::Locker
  // and pass the locker as the first argument. m_events_mutex is no longer
  // recursive.
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EVENTS));

  if (m_events.empty())
    return false;

  Listener::event_collection::iterator pos = m_events.end();

  if (broadcaster == nullptr && broadcaster_names == nullptr &&
      event_type_mask == 0) {
    pos = m_events.begin();
  } else {
    pos = std::find_if(m_events.begin(), m_events.end(),
                       EventMatcher(broadcaster, broadcaster_names,
                                    num_broadcaster_names, event_type_mask));
  }

  if (pos != m_events.end()) {
    event_sp = *pos;

    if (log != nullptr)
      log->Printf("%p '%s' Listener::FindNextEventInternal(broadcaster=%p, "
                  "broadcaster_names=%p[%u], event_type_mask=0x%8.8x, "
                  "remove=%i) event %p",
                  static_cast<void *>(this), GetName(),
                  static_cast<void *>(broadcaster),
                  static_cast<const void *>(broadcaster_names),
                  num_broadcaster_names, event_type_mask, remove,
                  static_cast<void *>(event_sp.get()));

    if (remove) {
      m_events.erase(pos);
      // Unlock the event queue here.  We've removed this event and are about
      // to return it so it should be okay to get the next event off the queue
      // here - and it might be useful to do that in the "DoOnRemoval".
      lock.unlock();
      event_sp->DoOnRemoval();
    }
    return true;
  }

  event_sp.reset();
  return false;
}

Event *Listener::PeekAtNextEvent() {
  std::unique_lock<std::mutex> guard(m_events_mutex);
  EventSP event_sp;
  if (FindNextEventInternal(guard, nullptr, nullptr, 0, 0, event_sp, false))
    return event_sp.get();
  return nullptr;
}

Event *Listener::PeekAtNextEventForBroadcaster(Broadcaster *broadcaster) {
  std::unique_lock<std::mutex> guard(m_events_mutex);
  EventSP event_sp;
  if (FindNextEventInternal(guard, broadcaster, nullptr, 0, 0, event_sp, false))
    return event_sp.get();
  return nullptr;
}

Event *
Listener::PeekAtNextEventForBroadcasterWithType(Broadcaster *broadcaster,
                                                uint32_t event_type_mask) {
  std::unique_lock<std::mutex> guard(m_events_mutex);
  EventSP event_sp;
  if (FindNextEventInternal(guard, broadcaster, nullptr, 0, event_type_mask,
                            event_sp, false))
    return event_sp.get();
  return nullptr;
}

bool Listener::GetEventInternal(
    const Timeout<std::micro> &timeout,
    Broadcaster *broadcaster,             // nullptr for any broadcaster
    const ConstString *broadcaster_names, // nullptr for any event
    uint32_t num_broadcaster_names, uint32_t event_type_mask,
    EventSP &event_sp) {
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EVENTS));
  LLDB_LOG(log, "this = {0}, timeout = {1} for {2}", this, timeout, m_name);

  std::unique_lock<std::mutex> lock(m_events_mutex);

  while (true) {
    if (FindNextEventInternal(lock, broadcaster, broadcaster_names,
                              num_broadcaster_names, event_type_mask, event_sp,
                              true)) {
      return true;
    } else {
      std::cv_status result = std::cv_status::no_timeout;
      if (!timeout)
        m_events_condition.wait(lock);
      else
        result = m_events_condition.wait_for(lock, *timeout);

      if (result == std::cv_status::timeout) {
        log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EVENTS);
        if (log)
          log->Printf("%p Listener::GetEventInternal() timed out for %s",
                      static_cast<void *>(this), m_name.c_str());
        return false;
      } else if (result != std::cv_status::no_timeout) {
        log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EVENTS);
        if (log)
          log->Printf("%p Listener::GetEventInternal() unknown error for %s",
                      static_cast<void *>(this), m_name.c_str());
        return false;
      }
    }
  }

  return false;
}

bool Listener::GetEventForBroadcasterWithType(
    Broadcaster *broadcaster, uint32_t event_type_mask, EventSP &event_sp,
    const Timeout<std::micro> &timeout) {
  return GetEventInternal(timeout, broadcaster, nullptr, 0, event_type_mask,
                          event_sp);
}

bool Listener::GetEventForBroadcaster(Broadcaster *broadcaster,
                                      EventSP &event_sp,
                                      const Timeout<std::micro> &timeout) {
  return GetEventInternal(timeout, broadcaster, nullptr, 0, 0, event_sp);
}

bool Listener::GetEvent(EventSP &event_sp, const Timeout<std::micro> &timeout) {
  return GetEventInternal(timeout, nullptr, nullptr, 0, 0, event_sp);
}

size_t Listener::HandleBroadcastEvent(EventSP &event_sp) {
  size_t num_handled = 0;
  std::lock_guard<std::recursive_mutex> guard(m_broadcasters_mutex);
  Broadcaster *broadcaster = event_sp->GetBroadcaster();
  if (!broadcaster)
    return 0;
  broadcaster_collection::iterator pos;
  broadcaster_collection::iterator end = m_broadcasters.end();
  Broadcaster::BroadcasterImplSP broadcaster_impl_sp(
      broadcaster->GetBroadcasterImpl());
  for (pos = m_broadcasters.find(broadcaster_impl_sp);
       pos != end && pos->first.lock() == broadcaster_impl_sp; ++pos) {
    BroadcasterInfo info = pos->second;
    if (event_sp->GetType() & info.event_mask) {
      if (info.callback != nullptr) {
        info.callback(event_sp, info.callback_user_data);
        ++num_handled;
      }
    }
  }
  return num_handled;
}

uint32_t
Listener::StartListeningForEventSpec(BroadcasterManagerSP manager_sp,
                                     const BroadcastEventSpec &event_spec) {
  if (!manager_sp)
    return 0;

  // The BroadcasterManager mutex must be locked before m_broadcasters_mutex to
  // avoid violating the lock hierarchy (manager before broadcasters).
  std::lock_guard<std::recursive_mutex> manager_guard(
      manager_sp->m_manager_mutex);
  std::lock_guard<std::recursive_mutex> guard(m_broadcasters_mutex);

  uint32_t bits_acquired = manager_sp->RegisterListenerForEvents(
      this->shared_from_this(), event_spec);
  if (bits_acquired) {
    broadcaster_manager_collection::iterator iter,
        end_iter = m_broadcaster_managers.end();
    BroadcasterManagerWP manager_wp(manager_sp);
    BroadcasterManagerWPMatcher matcher(manager_sp);
    iter = std::find_if<broadcaster_manager_collection::iterator,
                        BroadcasterManagerWPMatcher>(
        m_broadcaster_managers.begin(), end_iter, matcher);
    if (iter == end_iter)
      m_broadcaster_managers.push_back(manager_wp);
  }

  return bits_acquired;
}

bool Listener::StopListeningForEventSpec(BroadcasterManagerSP manager_sp,
                                         const BroadcastEventSpec &event_spec) {
  if (!manager_sp)
    return false;

  std::lock_guard<std::recursive_mutex> guard(m_broadcasters_mutex);
  return manager_sp->UnregisterListenerForEvents(this->shared_from_this(),
                                                 event_spec);
}

ListenerSP Listener::MakeListener(const char *name) {
  return ListenerSP(new Listener(name));
}
