//===-- Event.cpp -----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Event.h"

#include "lldb/Utility/Broadcaster.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/lldb-enumerations.h"

#include <algorithm>

#include <ctype.h>

using namespace lldb;
using namespace lldb_private;

#pragma mark -
#pragma mark Event

//------------------------------------------------------------------
// Event functions
//------------------------------------------------------------------

Event::Event(Broadcaster *broadcaster, uint32_t event_type, EventData *data)
    : m_broadcaster_wp(broadcaster->GetBroadcasterImpl()), m_type(event_type),
      m_data_sp(data) {}

Event::Event(Broadcaster *broadcaster, uint32_t event_type,
             const EventDataSP &event_data_sp)
    : m_broadcaster_wp(broadcaster->GetBroadcasterImpl()), m_type(event_type),
      m_data_sp(event_data_sp) {}

Event::Event(uint32_t event_type, EventData *data)
    : m_broadcaster_wp(), m_type(event_type), m_data_sp(data) {}

Event::Event(uint32_t event_type, const EventDataSP &event_data_sp)
    : m_broadcaster_wp(), m_type(event_type), m_data_sp(event_data_sp) {}

Event::~Event() = default;

void Event::Dump(Stream *s) const {
  Broadcaster *broadcaster;
  Broadcaster::BroadcasterImplSP broadcaster_impl_sp(m_broadcaster_wp.lock());
  if (broadcaster_impl_sp)
    broadcaster = broadcaster_impl_sp->GetBroadcaster();
  else
    broadcaster = nullptr;

  if (broadcaster) {
    StreamString event_name;
    if (broadcaster->GetEventNames(event_name, m_type, false))
      s->Printf("%p Event: broadcaster = %p (%s), type = 0x%8.8x (%s), data = ",
                static_cast<const void *>(this),
                static_cast<void *>(broadcaster),
                broadcaster->GetBroadcasterName().GetCString(), m_type,
                event_name.GetData());
    else
      s->Printf("%p Event: broadcaster = %p (%s), type = 0x%8.8x, data = ",
                static_cast<const void *>(this),
                static_cast<void *>(broadcaster),
                broadcaster->GetBroadcasterName().GetCString(), m_type);
  } else
    s->Printf("%p Event: broadcaster = NULL, type = 0x%8.8x, data = ",
              static_cast<const void *>(this), m_type);

  if (m_data_sp) {
    s->PutChar('{');
    m_data_sp->Dump(s);
    s->PutChar('}');
  } else
    s->Printf("<NULL>");
}

void Event::DoOnRemoval() {
  if (m_data_sp)
    m_data_sp->DoOnRemoval(this);
}

#pragma mark -
#pragma mark EventData

//------------------------------------------------------------------
// EventData functions
//------------------------------------------------------------------

EventData::EventData() = default;

EventData::~EventData() = default;

void EventData::Dump(Stream *s) const { s->PutCString("Generic Event Data"); }

#pragma mark -
#pragma mark EventDataBytes

//------------------------------------------------------------------
// EventDataBytes functions
//------------------------------------------------------------------

EventDataBytes::EventDataBytes() : m_bytes() {}

EventDataBytes::EventDataBytes(const char *cstr) : m_bytes() {
  SetBytesFromCString(cstr);
}

EventDataBytes::EventDataBytes(llvm::StringRef str) : m_bytes() {
  SetBytes(str.data(), str.size());
}

EventDataBytes::EventDataBytes(const void *src, size_t src_len) : m_bytes() {
  SetBytes(src, src_len);
}

EventDataBytes::~EventDataBytes() = default;

const ConstString &EventDataBytes::GetFlavorString() {
  static ConstString g_flavor("EventDataBytes");
  return g_flavor;
}

const ConstString &EventDataBytes::GetFlavor() const {
  return EventDataBytes::GetFlavorString();
}

void EventDataBytes::Dump(Stream *s) const {
  size_t num_printable_chars =
      std::count_if(m_bytes.begin(), m_bytes.end(), isprint);
  if (num_printable_chars == m_bytes.size())
    s->Format("\"{0}\"", m_bytes);
  else
    s->Format("{0:$[ ]@[x-2]}", llvm::make_range(
                         reinterpret_cast<const uint8_t *>(m_bytes.data()),
                         reinterpret_cast<const uint8_t *>(m_bytes.data() +
                                                           m_bytes.size())));
}

const void *EventDataBytes::GetBytes() const {
  return (m_bytes.empty() ? nullptr : m_bytes.data());
}

size_t EventDataBytes::GetByteSize() const { return m_bytes.size(); }

void EventDataBytes::SetBytes(const void *src, size_t src_len) {
  if (src != nullptr && src_len > 0)
    m_bytes.assign((const char *)src, src_len);
  else
    m_bytes.clear();
}

void EventDataBytes::SetBytesFromCString(const char *cstr) {
  if (cstr != nullptr && cstr[0])
    m_bytes.assign(cstr);
  else
    m_bytes.clear();
}

const void *EventDataBytes::GetBytesFromEvent(const Event *event_ptr) {
  const EventDataBytes *e = GetEventDataFromEvent(event_ptr);
  if (e != nullptr)
    return e->GetBytes();
  return nullptr;
}

size_t EventDataBytes::GetByteSizeFromEvent(const Event *event_ptr) {
  const EventDataBytes *e = GetEventDataFromEvent(event_ptr);
  if (e != nullptr)
    return e->GetByteSize();
  return 0;
}

const EventDataBytes *
EventDataBytes::GetEventDataFromEvent(const Event *event_ptr) {
  if (event_ptr != nullptr) {
    const EventData *event_data = event_ptr->GetData();
    if (event_data &&
        event_data->GetFlavor() == EventDataBytes::GetFlavorString())
      return static_cast<const EventDataBytes *>(event_data);
  }
  return nullptr;
}

void EventDataBytes::SwapBytes(std::string &new_bytes) {
  m_bytes.swap(new_bytes);
}

#pragma mark -
#pragma mark EventStructuredData

//------------------------------------------------------------------
// EventDataStructuredData definitions
//------------------------------------------------------------------

EventDataStructuredData::EventDataStructuredData()
    : EventData(), m_process_sp(), m_object_sp(), m_plugin_sp() {}

EventDataStructuredData::EventDataStructuredData(
    const ProcessSP &process_sp, const StructuredData::ObjectSP &object_sp,
    const lldb::StructuredDataPluginSP &plugin_sp)
    : EventData(), m_process_sp(process_sp), m_object_sp(object_sp),
      m_plugin_sp(plugin_sp) {}

EventDataStructuredData::~EventDataStructuredData() {}

//------------------------------------------------------------------
// EventDataStructuredData member functions
//------------------------------------------------------------------

const ConstString &EventDataStructuredData::GetFlavor() const {
  return EventDataStructuredData::GetFlavorString();
}

void EventDataStructuredData::Dump(Stream *s) const {
  if (!s)
    return;

  if (m_object_sp)
    m_object_sp->Dump(*s);
}

const ProcessSP &EventDataStructuredData::GetProcess() const {
  return m_process_sp;
}

const StructuredData::ObjectSP &EventDataStructuredData::GetObject() const {
  return m_object_sp;
}

const lldb::StructuredDataPluginSP &
EventDataStructuredData::GetStructuredDataPlugin() const {
  return m_plugin_sp;
}

void EventDataStructuredData::SetProcess(const ProcessSP &process_sp) {
  m_process_sp = process_sp;
}

void EventDataStructuredData::SetObject(
    const StructuredData::ObjectSP &object_sp) {
  m_object_sp = object_sp;
}

void EventDataStructuredData::SetStructuredDataPlugin(
    const lldb::StructuredDataPluginSP &plugin_sp) {
  m_plugin_sp = plugin_sp;
}

//------------------------------------------------------------------
// EventDataStructuredData static functions
//------------------------------------------------------------------

const EventDataStructuredData *
EventDataStructuredData::GetEventDataFromEvent(const Event *event_ptr) {
  if (event_ptr == nullptr)
    return nullptr;

  const EventData *event_data = event_ptr->GetData();
  if (!event_data ||
      event_data->GetFlavor() != EventDataStructuredData::GetFlavorString())
    return nullptr;

  return static_cast<const EventDataStructuredData *>(event_data);
}

ProcessSP EventDataStructuredData::GetProcessFromEvent(const Event *event_ptr) {
  auto event_data = EventDataStructuredData::GetEventDataFromEvent(event_ptr);
  if (event_data)
    return event_data->GetProcess();
  else
    return ProcessSP();
}

StructuredData::ObjectSP
EventDataStructuredData::GetObjectFromEvent(const Event *event_ptr) {
  auto event_data = EventDataStructuredData::GetEventDataFromEvent(event_ptr);
  if (event_data)
    return event_data->GetObject();
  else
    return StructuredData::ObjectSP();
}

lldb::StructuredDataPluginSP
EventDataStructuredData::GetPluginFromEvent(const Event *event_ptr) {
  auto event_data = EventDataStructuredData::GetEventDataFromEvent(event_ptr);
  if (event_data)
    return event_data->GetStructuredDataPlugin();
  else
    return StructuredDataPluginSP();
}

const ConstString &EventDataStructuredData::GetFlavorString() {
  static ConstString s_flavor("EventDataStructuredData");
  return s_flavor;
}
