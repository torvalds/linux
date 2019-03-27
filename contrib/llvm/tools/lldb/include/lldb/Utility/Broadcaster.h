//===-- Broadcaster.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_BROADCASTER_H
#define LLDB_UTILITY_BROADCASTER_H

#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-forward.h"

#include "llvm/ADT/SmallVector.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace lldb_private {
class Broadcaster;
}
namespace lldb_private {
class EventData;
}
namespace lldb_private {
class Listener;
}
namespace lldb_private {
class Stream;
}

namespace lldb_private {

//----------------------------------------------------------------------
// lldb::BroadcastEventSpec
//
// This class is used to specify a kind of event to register for.  The Debugger
// maintains a list of BroadcastEventSpec's and when it is made
//----------------------------------------------------------------------
class BroadcastEventSpec {
public:
  BroadcastEventSpec(const ConstString &broadcaster_class, uint32_t event_bits)
      : m_broadcaster_class(broadcaster_class), m_event_bits(event_bits) {}

  BroadcastEventSpec(const BroadcastEventSpec &rhs);

  ~BroadcastEventSpec() = default;

  const ConstString &GetBroadcasterClass() const { return m_broadcaster_class; }

  uint32_t GetEventBits() const { return m_event_bits; }

  // Tell whether this BroadcastEventSpec is contained in in_spec. That is: (a)
  // the two spec's share the same broadcaster class (b) the event bits of this
  // spec are wholly contained in those of in_spec.
  bool IsContainedIn(BroadcastEventSpec in_spec) const {
    if (m_broadcaster_class != in_spec.GetBroadcasterClass())
      return false;
    uint32_t in_bits = in_spec.GetEventBits();
    if (in_bits == m_event_bits)
      return true;
    else {
      if ((m_event_bits & in_bits) != 0 && (m_event_bits & ~in_bits) == 0)
        return true;
    }
    return false;
  }

  bool operator<(const BroadcastEventSpec &rhs) const;
  BroadcastEventSpec &operator=(const BroadcastEventSpec &rhs);

private:
  ConstString m_broadcaster_class;
  uint32_t m_event_bits;
};

class BroadcasterManager
    : public std::enable_shared_from_this<BroadcasterManager> {
public:
  friend class Listener;

protected:
  BroadcasterManager();

public:
  // Listeners hold onto weak pointers to their broadcaster managers.  So they
  // must be made into shared pointers, which you do with
  // MakeBroadcasterManager.

  static lldb::BroadcasterManagerSP MakeBroadcasterManager();

  ~BroadcasterManager() = default;

  uint32_t RegisterListenerForEvents(const lldb::ListenerSP &listener_sp,
                                     BroadcastEventSpec event_spec);

  bool UnregisterListenerForEvents(const lldb::ListenerSP &listener_sp,
                                   BroadcastEventSpec event_spec);

  lldb::ListenerSP GetListenerForEventSpec(BroadcastEventSpec event_spec) const;

  void SignUpListenersForBroadcaster(Broadcaster &broadcaster);

  void RemoveListener(const lldb::ListenerSP &listener_sp);

  void RemoveListener(Listener *listener);

  void Clear();

private:
  typedef std::pair<BroadcastEventSpec, lldb::ListenerSP> event_listener_key;
  typedef std::map<BroadcastEventSpec, lldb::ListenerSP> collection;
  typedef std::set<lldb::ListenerSP> listener_collection;
  collection m_event_map;
  listener_collection m_listeners;

  mutable std::recursive_mutex m_manager_mutex;

  // A couple of comparator classes for find_if:

  class BroadcasterClassMatches {
  public:
    BroadcasterClassMatches(const ConstString &broadcaster_class)
        : m_broadcaster_class(broadcaster_class) {}

    ~BroadcasterClassMatches() = default;

    bool operator()(const event_listener_key input) const {
      return (input.first.GetBroadcasterClass() == m_broadcaster_class);
    }

  private:
    ConstString m_broadcaster_class;
  };

  class BroadcastEventSpecMatches {
  public:
    BroadcastEventSpecMatches(BroadcastEventSpec broadcaster_spec)
        : m_broadcaster_spec(broadcaster_spec) {}

    ~BroadcastEventSpecMatches() = default;

    bool operator()(const event_listener_key input) const {
      return (input.first.IsContainedIn(m_broadcaster_spec));
    }

  private:
    BroadcastEventSpec m_broadcaster_spec;
  };

  class ListenerMatchesAndSharedBits {
  public:
    explicit ListenerMatchesAndSharedBits(BroadcastEventSpec broadcaster_spec,
                                          const lldb::ListenerSP listener_sp)
        : m_broadcaster_spec(broadcaster_spec), m_listener_sp(listener_sp) {}

    ~ListenerMatchesAndSharedBits() = default;

    bool operator()(const event_listener_key input) const {
      return (input.first.GetBroadcasterClass() ==
                  m_broadcaster_spec.GetBroadcasterClass() &&
              (input.first.GetEventBits() &
               m_broadcaster_spec.GetEventBits()) != 0 &&
              input.second == m_listener_sp);
    }

  private:
    BroadcastEventSpec m_broadcaster_spec;
    const lldb::ListenerSP m_listener_sp;
  };

  class ListenerMatches {
  public:
    explicit ListenerMatches(const lldb::ListenerSP in_listener_sp)
        : m_listener_sp(in_listener_sp) {}

    ~ListenerMatches() = default;

    bool operator()(const event_listener_key input) const {
      if (input.second == m_listener_sp)
        return true;
      else
        return false;
    }

  private:
    const lldb::ListenerSP m_listener_sp;
  };

  class ListenerMatchesPointer {
  public:
    ListenerMatchesPointer(const Listener *in_listener)
        : m_listener(in_listener) {}

    ~ListenerMatchesPointer() = default;

    bool operator()(const event_listener_key input) const {
      if (input.second.get() == m_listener)
        return true;
      else
        return false;
    }

    bool operator()(const lldb::ListenerSP input) const {
      if (input.get() == m_listener)
        return true;
      else
        return false;
    }

  private:
    const Listener *m_listener;
  };
};

//----------------------------------------------------------------------
/// @class Broadcaster Broadcaster.h "lldb/Utility/Broadcaster.h" An event
/// broadcasting class.
///
/// The Broadcaster class is designed to be subclassed by objects that wish to
/// vend events in a multi-threaded environment. Broadcaster objects can each
/// vend 32 events. Each event is represented by a bit in a 32 bit value and
/// these bits can be set:
///     @see Broadcaster::SetEventBits(uint32_t)
/// or cleared:
///     @see Broadcaster::ResetEventBits(uint32_t)
/// When an event gets set the Broadcaster object will notify the Listener
/// object that is listening for the event (if there is one).
///
/// Subclasses should provide broadcast bit definitions for any events they
/// vend, typically using an enumeration:
///     \code
///         class Foo : public Broadcaster
///         {
///         public:
///         //----------------------------------------------------------
///         // Broadcaster event bits definitions.
///         //----------------------------------------------------------
///         enum
///         {
///             eBroadcastBitOne   = (1 << 0),
///             eBroadcastBitTwo   = (1 << 1),
///             eBroadcastBitThree = (1 << 2),
///             ...
///         };
///     \endcode
//----------------------------------------------------------------------
class Broadcaster {
  friend class Listener;
  friend class Event;

public:
  //------------------------------------------------------------------
  /// Construct with a broadcaster with a name.
  ///
  /// @param[in] name
  ///     A NULL terminated C string that contains the name of the
  ///     broadcaster object.
  //------------------------------------------------------------------
  Broadcaster(lldb::BroadcasterManagerSP manager_sp, const char *name);

  //------------------------------------------------------------------
  /// Destructor.
  ///
  /// The destructor is virtual since this class gets subclassed.
  //------------------------------------------------------------------
  virtual ~Broadcaster();

  void CheckInWithManager();

  //------------------------------------------------------------------
  /// Broadcast an event which has no associated data.
  ///
  /// @param[in] event_type
  ///     The element from the enum defining this broadcaster's events
  ///     that is being broadcast.
  ///
  /// @param[in] event_data
  ///     User event data that will be owned by the lldb::Event that
  ///     is created internally.
  ///
  /// @param[in] unique
  ///     If true, then only add an event of this type if there isn't
  ///     one already in the queue.
  ///
  //------------------------------------------------------------------
  void BroadcastEvent(lldb::EventSP &event_sp) {
    m_broadcaster_sp->BroadcastEvent(event_sp);
  }

  void BroadcastEventIfUnique(lldb::EventSP &event_sp) {
    m_broadcaster_sp->BroadcastEventIfUnique(event_sp);
  }

  void BroadcastEvent(uint32_t event_type,
                      const lldb::EventDataSP &event_data_sp) {
    m_broadcaster_sp->BroadcastEvent(event_type, event_data_sp);
  }

  void BroadcastEvent(uint32_t event_type, EventData *event_data = nullptr) {
    m_broadcaster_sp->BroadcastEvent(event_type, event_data);
  }

  void BroadcastEventIfUnique(uint32_t event_type,
                              EventData *event_data = nullptr) {
    m_broadcaster_sp->BroadcastEventIfUnique(event_type, event_data);
  }

  void Clear() { m_broadcaster_sp->Clear(); }

  virtual void AddInitialEventsToListener(const lldb::ListenerSP &listener_sp,
                                          uint32_t requested_events);

  //------------------------------------------------------------------
  /// Listen for any events specified by \a event_mask.
  ///
  /// Only one listener can listen to each event bit in a given Broadcaster.
  /// Once a listener has acquired an event bit, no other broadcaster will
  /// have access to it until it is relinquished by the first listener that
  /// gets it. The actual event bits that get acquired by \a listener may be
  /// different from what is requested in \a event_mask, and to track this the
  /// actual event bits that are acquired get returned.
  ///
  /// @param[in] listener
  ///     The Listener object that wants to monitor the events that
  ///     get broadcast by this object.
  ///
  /// @param[in] event_mask
  ///     A bit mask that indicates which events the listener is
  ///     asking to monitor.
  ///
  /// @return
  ///     The actual event bits that were acquired by \a listener.
  //------------------------------------------------------------------
  uint32_t AddListener(const lldb::ListenerSP &listener_sp,
                       uint32_t event_mask) {
    return m_broadcaster_sp->AddListener(listener_sp, event_mask);
  }

  //------------------------------------------------------------------
  /// Get the NULL terminated C string name of this Broadcaster object.
  ///
  /// @return
  ///     The NULL terminated C string name of this Broadcaster.
  //------------------------------------------------------------------
  const ConstString &GetBroadcasterName() { return m_broadcaster_name; }

  //------------------------------------------------------------------
  /// Get the event name(s) for one or more event bits.
  ///
  /// @param[in] event_mask
  ///     A bit mask that indicates which events to get names for.
  ///
  /// @return
  ///     The NULL terminated C string name of this Broadcaster.
  //------------------------------------------------------------------
  bool GetEventNames(Stream &s, const uint32_t event_mask,
                     bool prefix_with_broadcaster_name) const {
    return m_broadcaster_sp->GetEventNames(s, event_mask,
                                           prefix_with_broadcaster_name);
  }

  //------------------------------------------------------------------
  /// Set the name for an event bit.
  ///
  /// @param[in] event_mask
  ///     A bit mask that indicates which events the listener is
  ///     asking to monitor.
  ///
  /// @return
  ///     The NULL terminated C string name of this Broadcaster.
  //------------------------------------------------------------------
  void SetEventName(uint32_t event_mask, const char *name) {
    m_broadcaster_sp->SetEventName(event_mask, name);
  }

  const char *GetEventName(uint32_t event_mask) const {
    return m_broadcaster_sp->GetEventName(event_mask);
  }

  bool EventTypeHasListeners(uint32_t event_type) {
    return m_broadcaster_sp->EventTypeHasListeners(event_type);
  }

  //------------------------------------------------------------------
  /// Removes a Listener from this broadcasters list and frees the event bits
  /// specified by \a event_mask that were previously acquired by \a listener
  /// (assuming \a listener was listening to this object) for other listener
  /// objects to use.
  ///
  /// @param[in] listener
  ///     A Listener object that previously called AddListener.
  ///
  /// @param[in] event_mask
  ///     The event bits \a listener wishes to relinquish.
  ///
  /// @return
  ///     \b True if the listener was listening to this broadcaster
  ///     and was removed, \b false otherwise.
  ///
  /// @see uint32_t Broadcaster::AddListener (Listener*, uint32_t)
  //------------------------------------------------------------------
  bool RemoveListener(const lldb::ListenerSP &listener_sp,
                      uint32_t event_mask = UINT32_MAX) {
    return m_broadcaster_sp->RemoveListener(listener_sp, event_mask);
  }

  //------------------------------------------------------------------
  /// Provides a simple mechanism to temporarily redirect events from
  /// broadcaster.  When you call this function passing in a listener and
  /// event type mask, all events from the broadcaster matching the mask will
  /// now go to the hijacking listener. Only one hijack can occur at a time.
  /// If we need more than this we will have to implement a Listener stack.
  ///
  /// @param[in] listener
  ///     A Listener object.  You do not need to call StartListeningForEvents
  ///     for this broadcaster (that would fail anyway since the event bits
  ///     would most likely be taken by the listener(s) you are usurping.
  ///
  /// @param[in] event_mask
  ///     The event bits \a listener wishes to hijack.
  ///
  /// @return
  ///     \b True if the event mask could be hijacked, \b false otherwise.
  ///
  /// @see uint32_t Broadcaster::AddListener (Listener*, uint32_t)
  //------------------------------------------------------------------
  bool HijackBroadcaster(const lldb::ListenerSP &listener_sp,
                         uint32_t event_mask = UINT32_MAX) {
    return m_broadcaster_sp->HijackBroadcaster(listener_sp, event_mask);
  }

  bool IsHijackedForEvent(uint32_t event_mask) {
    return m_broadcaster_sp->IsHijackedForEvent(event_mask);
  }

  //------------------------------------------------------------------
  /// Restore the state of the Broadcaster from a previous hijack attempt.
  ///
  //------------------------------------------------------------------
  void RestoreBroadcaster() { m_broadcaster_sp->RestoreBroadcaster(); }

  // This needs to be filled in if you are going to register the broadcaster
  // with the broadcaster manager and do broadcaster class matching.
  // FIXME: Probably should make a ManagedBroadcaster subclass with all the bits
  // needed to work
  // with the BroadcasterManager, so that it is clearer how to add one.
  virtual ConstString &GetBroadcasterClass() const;

  lldb::BroadcasterManagerSP GetManager();

protected:
  // BroadcasterImpl contains the actual Broadcaster implementation.  The
  // Broadcaster makes a BroadcasterImpl which lives as long as it does.  The
  // Listeners & the Events hold a weak pointer to the BroadcasterImpl, so that
  // they can survive if a Broadcaster they were listening to is destroyed w/o
  // their being able to unregister from it (which can happen if the
  // Broadcasters & Listeners are being destroyed on separate threads
  // simultaneously. The Broadcaster itself can't be shared out as a weak
  // pointer, because some things that are broadcasters (e.g. the Target and
  // the Process) are shared in their own right.
  //
  // For the most part, the Broadcaster functions dispatch to the
  // BroadcasterImpl, and are documented in the public Broadcaster API above.

  class BroadcasterImpl {
    friend class Listener;
    friend class Broadcaster;

  public:
    BroadcasterImpl(Broadcaster &broadcaster);

    ~BroadcasterImpl() = default;

    void BroadcastEvent(lldb::EventSP &event_sp);

    void BroadcastEventIfUnique(lldb::EventSP &event_sp);

    void BroadcastEvent(uint32_t event_type, EventData *event_data = nullptr);

    void BroadcastEvent(uint32_t event_type,
                        const lldb::EventDataSP &event_data_sp);

    void BroadcastEventIfUnique(uint32_t event_type,
                                EventData *event_data = nullptr);

    void Clear();

    uint32_t AddListener(const lldb::ListenerSP &listener_sp,
                         uint32_t event_mask);

    const char *GetBroadcasterName() const {
      return m_broadcaster.GetBroadcasterName().AsCString();
    }

    Broadcaster *GetBroadcaster();

    bool GetEventNames(Stream &s, const uint32_t event_mask,
                       bool prefix_with_broadcaster_name) const;

    void SetEventName(uint32_t event_mask, const char *name) {
      m_event_names[event_mask] = name;
    }

    const char *GetEventName(uint32_t event_mask) const {
      const auto pos = m_event_names.find(event_mask);
      if (pos != m_event_names.end())
        return pos->second.c_str();
      return nullptr;
    }

    bool EventTypeHasListeners(uint32_t event_type);

    bool RemoveListener(lldb_private::Listener *listener,
                        uint32_t event_mask = UINT32_MAX);

    bool RemoveListener(const lldb::ListenerSP &listener_sp,
                        uint32_t event_mask = UINT32_MAX);

    bool HijackBroadcaster(const lldb::ListenerSP &listener_sp,
                           uint32_t event_mask = UINT32_MAX);

    bool IsHijackedForEvent(uint32_t event_mask);

    void RestoreBroadcaster();

  protected:
    void PrivateBroadcastEvent(lldb::EventSP &event_sp, bool unique);

    const char *GetHijackingListenerName();

    //------------------------------------------------------------------
    //
    //------------------------------------------------------------------
    typedef llvm::SmallVector<std::pair<lldb::ListenerWP, uint32_t>, 4>
        collection;
    typedef std::map<uint32_t, std::string> event_names_map;

    llvm::SmallVector<std::pair<lldb::ListenerSP, uint32_t &>, 4>
    GetListeners();

    Broadcaster &m_broadcaster;    ///< The broadcaster that this implements
    event_names_map m_event_names; ///< Optionally define event names for
                                   ///readability and logging for each event bit
    collection m_listeners; ///< A list of Listener / event_mask pairs that are
                            ///listening to this broadcaster.
    std::recursive_mutex
        m_listeners_mutex; ///< A mutex that protects \a m_listeners.
    std::vector<lldb::ListenerSP> m_hijacking_listeners; // A simple mechanism
                                                         // to intercept events
                                                         // from a broadcaster
    std::vector<uint32_t> m_hijacking_masks; // At some point we may want to
                                             // have a stack or Listener
    // collections, but for now this is just for private hijacking.

  private:
    //------------------------------------------------------------------
    // For Broadcaster only
    //------------------------------------------------------------------
    DISALLOW_COPY_AND_ASSIGN(BroadcasterImpl);
  };

  typedef std::shared_ptr<BroadcasterImpl> BroadcasterImplSP;
  typedef std::weak_ptr<BroadcasterImpl> BroadcasterImplWP;

  BroadcasterImplSP GetBroadcasterImpl() { return m_broadcaster_sp; }

  const char *GetHijackingListenerName() {
    return m_broadcaster_sp->GetHijackingListenerName();
  }
  //------------------------------------------------------------------
  // Classes that inherit from Broadcaster can see and modify these
  //------------------------------------------------------------------

private:
  //------------------------------------------------------------------
  // For Broadcaster only
  //------------------------------------------------------------------
  BroadcasterImplSP m_broadcaster_sp;
  lldb::BroadcasterManagerSP m_manager_sp;
  const ConstString
      m_broadcaster_name; ///< The name of this broadcaster object.

  DISALLOW_COPY_AND_ASSIGN(Broadcaster);
};

} // namespace lldb_private

#endif // LLDB_UTILITY_BROADCASTER_H
