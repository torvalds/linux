//===-- Listener.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_LISTENER_H
#define LLDB_UTILITY_LISTENER_H

#include "lldb/Utility/Broadcaster.h"
#include "lldb/Utility/Timeout.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-forward.h"

#include <condition_variable>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <ratio>
#include <string>
#include <vector>

#include <stddef.h>
#include <stdint.h>

namespace lldb_private {
class ConstString;
}
namespace lldb_private {
class Event;
}

namespace lldb_private {

class Listener : public std::enable_shared_from_this<Listener> {
public:
  typedef bool (*HandleBroadcastCallback)(lldb::EventSP &event_sp, void *baton);

  friend class Broadcaster;
  friend class BroadcasterManager;

  //------------------------------------------------------------------
  // Constructors and Destructors
  //------------------------------------------------------------------
  //
  // Listeners have to be constructed into shared pointers - at least if you
  // want them to listen to Broadcasters,
protected:
  Listener(const char *name);

public:
  static lldb::ListenerSP MakeListener(const char *name);

  ~Listener();

  void AddEvent(lldb::EventSP &event);

  void Clear();

  const char *GetName() { return m_name.c_str(); }

  uint32_t StartListeningForEventSpec(lldb::BroadcasterManagerSP manager_sp,
                                      const BroadcastEventSpec &event_spec);

  bool StopListeningForEventSpec(lldb::BroadcasterManagerSP manager_sp,
                                 const BroadcastEventSpec &event_spec);

  uint32_t StartListeningForEvents(Broadcaster *broadcaster,
                                   uint32_t event_mask);

  uint32_t StartListeningForEvents(Broadcaster *broadcaster,
                                   uint32_t event_mask,
                                   HandleBroadcastCallback callback,
                                   void *callback_user_data);

  bool StopListeningForEvents(Broadcaster *broadcaster, uint32_t event_mask);

  Event *PeekAtNextEvent();

  Event *PeekAtNextEventForBroadcaster(Broadcaster *broadcaster);

  Event *PeekAtNextEventForBroadcasterWithType(Broadcaster *broadcaster,
                                               uint32_t event_type_mask);

  // Returns true if an event was received, false if we timed out.
  bool GetEvent(lldb::EventSP &event_sp, const Timeout<std::micro> &timeout);

  bool GetEventForBroadcaster(Broadcaster *broadcaster, lldb::EventSP &event_sp,
                              const Timeout<std::micro> &timeout);

  bool GetEventForBroadcasterWithType(Broadcaster *broadcaster,
                                      uint32_t event_type_mask,
                                      lldb::EventSP &event_sp,
                                      const Timeout<std::micro> &timeout);

  size_t HandleBroadcastEvent(lldb::EventSP &event_sp);

private:
  //------------------------------------------------------------------
  // Classes that inherit from Listener can see and modify these
  //------------------------------------------------------------------
  struct BroadcasterInfo {
    BroadcasterInfo(uint32_t mask, HandleBroadcastCallback cb = nullptr,
                    void *ud = nullptr)
        : event_mask(mask), callback(cb), callback_user_data(ud) {}

    uint32_t event_mask;
    HandleBroadcastCallback callback;
    void *callback_user_data;
  };

  typedef std::multimap<Broadcaster::BroadcasterImplWP, BroadcasterInfo,
                        std::owner_less<Broadcaster::BroadcasterImplWP>>
      broadcaster_collection;
  typedef std::list<lldb::EventSP> event_collection;
  typedef std::vector<lldb::BroadcasterManagerWP>
      broadcaster_manager_collection;

  bool
  FindNextEventInternal(std::unique_lock<std::mutex> &lock,
                        Broadcaster *broadcaster, // nullptr for any broadcaster
                        const ConstString *sources, // nullptr for any event
                        uint32_t num_sources, uint32_t event_type_mask,
                        lldb::EventSP &event_sp, bool remove);

  bool GetEventInternal(const Timeout<std::micro> &timeout,
                        Broadcaster *broadcaster, // nullptr for any broadcaster
                        const ConstString *sources, // nullptr for any event
                        uint32_t num_sources, uint32_t event_type_mask,
                        lldb::EventSP &event_sp);

  std::string m_name;
  broadcaster_collection m_broadcasters;
  std::recursive_mutex m_broadcasters_mutex; // Protects m_broadcasters
  event_collection m_events;
  std::mutex m_events_mutex; // Protects m_broadcasters and m_events
  std::condition_variable m_events_condition;
  broadcaster_manager_collection m_broadcaster_managers;

  void BroadcasterWillDestruct(Broadcaster *);

  void BroadcasterManagerWillDestruct(lldb::BroadcasterManagerSP manager_sp);

  //    broadcaster_collection::iterator
  //    FindBroadcasterWithMask (Broadcaster *broadcaster,
  //                             uint32_t event_mask,
  //                             bool exact);

  //------------------------------------------------------------------
  // For Listener only
  //------------------------------------------------------------------
  DISALLOW_COPY_AND_ASSIGN(Listener);
};

} // namespace lldb_private

#endif // LLDB_UTILITY_LISTENER_H
