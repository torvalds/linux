//===-- Event.h -------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_EVENT_H
#define LLDB_UTILITY_EVENT_H

#include "lldb/Utility/Broadcaster.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Predicate.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-forward.h"

#include "llvm/ADT/StringRef.h"

#include <chrono>
#include <memory>
#include <string>

#include <stddef.h>
#include <stdint.h>

namespace lldb_private {
class Event;
}
namespace lldb_private {
class Stream;
}

namespace lldb_private {

//----------------------------------------------------------------------
// lldb::EventData
//----------------------------------------------------------------------
class EventData {
  friend class Event;

public:
  EventData();

  virtual ~EventData();

  virtual const ConstString &GetFlavor() const = 0;

  virtual void Dump(Stream *s) const;

private:
  virtual void DoOnRemoval(Event *event_ptr) {}

  DISALLOW_COPY_AND_ASSIGN(EventData);
};

//----------------------------------------------------------------------
// lldb::EventDataBytes
//----------------------------------------------------------------------
class EventDataBytes : public EventData {
public:
  //------------------------------------------------------------------
  // Constructors
  //------------------------------------------------------------------
  EventDataBytes();

  EventDataBytes(const char *cstr);

  EventDataBytes(llvm::StringRef str);

  EventDataBytes(const void *src, size_t src_len);

  ~EventDataBytes() override;

  //------------------------------------------------------------------
  // Member functions
  //------------------------------------------------------------------
  const ConstString &GetFlavor() const override;

  void Dump(Stream *s) const override;

  const void *GetBytes() const;

  size_t GetByteSize() const;

  void SetBytes(const void *src, size_t src_len);

  void SwapBytes(std::string &new_bytes);

  void SetBytesFromCString(const char *cstr);

  //------------------------------------------------------------------
  // Static functions
  //------------------------------------------------------------------
  static const EventDataBytes *GetEventDataFromEvent(const Event *event_ptr);

  static const void *GetBytesFromEvent(const Event *event_ptr);

  static size_t GetByteSizeFromEvent(const Event *event_ptr);

  static const ConstString &GetFlavorString();

private:
  std::string m_bytes;

  DISALLOW_COPY_AND_ASSIGN(EventDataBytes);
};

class EventDataReceipt : public EventData {
public:
  EventDataReceipt() : EventData(), m_predicate(false) {}

  ~EventDataReceipt() override {}

  static const ConstString &GetFlavorString() {
    static ConstString g_flavor("Process::ProcessEventData");
    return g_flavor;
  }

  const ConstString &GetFlavor() const override { return GetFlavorString(); }

  bool WaitForEventReceived(const Timeout<std::micro> &timeout = llvm::None) {
    return m_predicate.WaitForValueEqualTo(true, timeout);
  }

private:
  Predicate<bool> m_predicate;

  void DoOnRemoval(Event *event_ptr) override {
    m_predicate.SetValue(true, eBroadcastAlways);
  }
};

//----------------------------------------------------------------------
/// This class handles one or more StructuredData::Dictionary entries
/// that are raised for structured data events.
//----------------------------------------------------------------------

class EventDataStructuredData : public EventData {
public:
  //------------------------------------------------------------------
  // Constructors
  //------------------------------------------------------------------
  EventDataStructuredData();

  EventDataStructuredData(const lldb::ProcessSP &process_sp,
                          const StructuredData::ObjectSP &object_sp,
                          const lldb::StructuredDataPluginSP &plugin_sp);

  ~EventDataStructuredData() override;

  //------------------------------------------------------------------
  // Member functions
  //------------------------------------------------------------------
  const ConstString &GetFlavor() const override;

  void Dump(Stream *s) const override;

  const lldb::ProcessSP &GetProcess() const;

  const StructuredData::ObjectSP &GetObject() const;

  const lldb::StructuredDataPluginSP &GetStructuredDataPlugin() const;

  void SetProcess(const lldb::ProcessSP &process_sp);

  void SetObject(const StructuredData::ObjectSP &object_sp);

  void SetStructuredDataPlugin(const lldb::StructuredDataPluginSP &plugin_sp);

  //------------------------------------------------------------------
  // Static functions
  //------------------------------------------------------------------
  static const EventDataStructuredData *
  GetEventDataFromEvent(const Event *event_ptr);

  static lldb::ProcessSP GetProcessFromEvent(const Event *event_ptr);

  static StructuredData::ObjectSP GetObjectFromEvent(const Event *event_ptr);

  static lldb::StructuredDataPluginSP
  GetPluginFromEvent(const Event *event_ptr);

  static const ConstString &GetFlavorString();

private:
  lldb::ProcessSP m_process_sp;
  StructuredData::ObjectSP m_object_sp;
  lldb::StructuredDataPluginSP m_plugin_sp;

  DISALLOW_COPY_AND_ASSIGN(EventDataStructuredData);
};

//----------------------------------------------------------------------
// lldb::Event
//----------------------------------------------------------------------
class Event {
  friend class Listener;
  friend class EventData;
  friend class Broadcaster::BroadcasterImpl;

public:
  Event(Broadcaster *broadcaster, uint32_t event_type,
        EventData *data = nullptr);

  Event(Broadcaster *broadcaster, uint32_t event_type,
        const lldb::EventDataSP &event_data_sp);

  Event(uint32_t event_type, EventData *data = nullptr);

  Event(uint32_t event_type, const lldb::EventDataSP &event_data_sp);

  ~Event();

  void Dump(Stream *s) const;

  EventData *GetData() { return m_data_sp.get(); }

  const EventData *GetData() const { return m_data_sp.get(); }

  void SetData(EventData *new_data) { m_data_sp.reset(new_data); }

  uint32_t GetType() const { return m_type; }

  void SetType(uint32_t new_type) { m_type = new_type; }

  Broadcaster *GetBroadcaster() const {
    Broadcaster::BroadcasterImplSP broadcaster_impl_sp =
        m_broadcaster_wp.lock();
    if (broadcaster_impl_sp)
      return broadcaster_impl_sp->GetBroadcaster();
    else
      return nullptr;
  }

  bool BroadcasterIs(Broadcaster *broadcaster) {
    Broadcaster::BroadcasterImplSP broadcaster_impl_sp =
        m_broadcaster_wp.lock();
    if (broadcaster_impl_sp)
      return broadcaster_impl_sp->GetBroadcaster() == broadcaster;
    else
      return false;
  }

  void Clear() { m_data_sp.reset(); }

private:
  // This is only called by Listener when it pops an event off the queue for
  // the listener.  It calls the Event Data's DoOnRemoval() method, which is
  // virtual and can be overridden by the specific data classes.

  void DoOnRemoval();

  // Called by Broadcaster::BroadcastEvent prior to letting all the listeners
  // know about it update the contained broadcaster so that events can be
  // popped off one queue and re-broadcast to others.
  void SetBroadcaster(Broadcaster *broadcaster) {
    m_broadcaster_wp = broadcaster->GetBroadcasterImpl();
  }

  Broadcaster::BroadcasterImplWP
      m_broadcaster_wp;        // The broadcaster that sent this event
  uint32_t m_type;             // The bit describing this event
  lldb::EventDataSP m_data_sp; // User specific data for this event

  DISALLOW_COPY_AND_ASSIGN(Event);
  Event(); // Disallow default constructor
};

} // namespace lldb_private

#endif // LLDB_UTILITY_EVENT_H
