//===-- GDBRemoteCommunicationHistory.cpp -----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "GDBRemoteCommunicationHistory.h"

// Other libraries and framework includes
#include "lldb/Core/StreamFile.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Log.h"

using namespace llvm;
using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_gdb_remote;

void GDBRemoteCommunicationHistory::Entry::Serialize(raw_ostream &strm) const {
  yaml::Output yout(strm);
  yout << const_cast<GDBRemoteCommunicationHistory::Entry &>(*this);
  strm.flush();
}

GDBRemoteCommunicationHistory::GDBRemoteCommunicationHistory(uint32_t size)
    : m_packets(), m_curr_idx(0), m_total_packet_count(0),
      m_dumped_to_log(false) {
  if (size)
    m_packets.resize(size);
}

GDBRemoteCommunicationHistory::~GDBRemoteCommunicationHistory() {}

void GDBRemoteCommunicationHistory::AddPacket(char packet_char, PacketType type,
                                              uint32_t bytes_transmitted) {
  const size_t size = m_packets.size();
  if (size == 0)
    return;

  const uint32_t idx = GetNextIndex();
  m_packets[idx].packet.data.assign(1, packet_char);
  m_packets[idx].type = type;
  m_packets[idx].bytes_transmitted = bytes_transmitted;
  m_packets[idx].packet_idx = m_total_packet_count;
  m_packets[idx].tid = llvm::get_threadid();
  if (m_stream && type == ePacketTypeRecv)
    m_packets[idx].Serialize(*m_stream);
}

void GDBRemoteCommunicationHistory::AddPacket(const std::string &src,
                                              uint32_t src_len, PacketType type,
                                              uint32_t bytes_transmitted) {
  const size_t size = m_packets.size();
  if (size == 0)
    return;

  const uint32_t idx = GetNextIndex();
  m_packets[idx].packet.data.assign(src, 0, src_len);
  m_packets[idx].type = type;
  m_packets[idx].bytes_transmitted = bytes_transmitted;
  m_packets[idx].packet_idx = m_total_packet_count;
  m_packets[idx].tid = llvm::get_threadid();
  if (m_stream && type == ePacketTypeRecv)
    m_packets[idx].Serialize(*m_stream);
}

void GDBRemoteCommunicationHistory::Dump(Stream &strm) const {
  const uint32_t size = GetNumPacketsInHistory();
  const uint32_t first_idx = GetFirstSavedPacketIndex();
  const uint32_t stop_idx = m_curr_idx + size;
  for (uint32_t i = first_idx; i < stop_idx; ++i) {
    const uint32_t idx = NormalizeIndex(i);
    const Entry &entry = m_packets[idx];
    if (entry.type == ePacketTypeInvalid || entry.packet.data.empty())
      break;
    strm.Printf("history[%u] tid=0x%4.4" PRIx64 " <%4u> %s packet: %s\n",
                entry.packet_idx, entry.tid, entry.bytes_transmitted,
                (entry.type == ePacketTypeSend) ? "send" : "read",
                entry.packet.data.c_str());
  }
}

void GDBRemoteCommunicationHistory::Dump(Log *log) const {
  if (!log || m_dumped_to_log)
    return;

  m_dumped_to_log = true;
  const uint32_t size = GetNumPacketsInHistory();
  const uint32_t first_idx = GetFirstSavedPacketIndex();
  const uint32_t stop_idx = m_curr_idx + size;
  for (uint32_t i = first_idx; i < stop_idx; ++i) {
    const uint32_t idx = NormalizeIndex(i);
    const Entry &entry = m_packets[idx];
    if (entry.type == ePacketTypeInvalid || entry.packet.data.empty())
      break;
    log->Printf("history[%u] tid=0x%4.4" PRIx64 " <%4u> %s packet: %s",
                entry.packet_idx, entry.tid, entry.bytes_transmitted,
                (entry.type == ePacketTypeSend) ? "send" : "read",
                entry.packet.data.c_str());
  }
}

void yaml::ScalarEnumerationTraits<GDBRemoteCommunicationHistory::PacketType>::
    enumeration(IO &io, GDBRemoteCommunicationHistory::PacketType &value) {
  io.enumCase(value, "Invalid",
              GDBRemoteCommunicationHistory::ePacketTypeInvalid);
  io.enumCase(value, "Send", GDBRemoteCommunicationHistory::ePacketTypeSend);
  io.enumCase(value, "Recv", GDBRemoteCommunicationHistory::ePacketTypeRecv);
}

void yaml::ScalarTraits<GDBRemoteCommunicationHistory::Entry::BinaryData>::
    output(const GDBRemoteCommunicationHistory::Entry::BinaryData &Val, void *,
           raw_ostream &Out) {
  Out << toHex(Val.data);
}

StringRef
yaml::ScalarTraits<GDBRemoteCommunicationHistory::Entry::BinaryData>::input(
    StringRef Scalar, void *,
    GDBRemoteCommunicationHistory::Entry::BinaryData &Val) {
  Val.data = fromHex(Scalar);
  return {};
}

void yaml::MappingTraits<GDBRemoteCommunicationHistory::Entry>::mapping(
    IO &io, GDBRemoteCommunicationHistory::Entry &Entry) {
  io.mapRequired("packet", Entry.packet);
  io.mapRequired("type", Entry.type);
  io.mapRequired("bytes", Entry.bytes_transmitted);
  io.mapRequired("index", Entry.packet_idx);
  io.mapRequired("tid", Entry.tid);
}

StringRef yaml::MappingTraits<GDBRemoteCommunicationHistory::Entry>::validate(
    IO &io, GDBRemoteCommunicationHistory::Entry &Entry) {
  if (Entry.bytes_transmitted != Entry.packet.data.size())
    return "BinaryData size doesn't match bytes transmitted";

  return {};
}
