//===-- GDBRemoteCommunicationHistory.h--------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_GDBRemoteCommunicationHistory_h_
#define liblldb_GDBRemoteCommunicationHistory_h_

#include <string>
#include <vector>

#include "lldb/lldb-public.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"

namespace lldb_private {
namespace process_gdb_remote {

/// The history keeps a circular buffer of GDB remote packets. The history is
/// used for logging and replaying GDB remote packets.
class GDBRemoteCommunicationHistory {
public:
  friend llvm::yaml::MappingTraits<GDBRemoteCommunicationHistory>;

  enum PacketType { ePacketTypeInvalid = 0, ePacketTypeSend, ePacketTypeRecv };

  /// Entry in the ring buffer containing the packet data, its type, size and
  /// index. Entries can be serialized to file.
  struct Entry {
    Entry()
        : packet(), type(ePacketTypeInvalid), bytes_transmitted(0),
          packet_idx(0), tid(LLDB_INVALID_THREAD_ID) {}

    void Clear() {
      packet.data.clear();
      type = ePacketTypeInvalid;
      bytes_transmitted = 0;
      packet_idx = 0;
      tid = LLDB_INVALID_THREAD_ID;
    }

    struct BinaryData {
      std::string data;
    };

    void Serialize(llvm::raw_ostream &strm) const;

    BinaryData packet;
    PacketType type;
    uint32_t bytes_transmitted;
    uint32_t packet_idx;
    lldb::tid_t tid;
  };

  GDBRemoteCommunicationHistory(uint32_t size = 0);

  ~GDBRemoteCommunicationHistory();

  // For single char packets for ack, nack and /x03
  void AddPacket(char packet_char, PacketType type, uint32_t bytes_transmitted);

  void AddPacket(const std::string &src, uint32_t src_len, PacketType type,
                 uint32_t bytes_transmitted);

  void Dump(Stream &strm) const;
  void Dump(Log *log) const;
  bool DidDumpToLog() const { return m_dumped_to_log; }

  void SetStream(llvm::raw_ostream *strm) { m_stream = strm; }

private:
  uint32_t GetFirstSavedPacketIndex() const {
    if (m_total_packet_count < m_packets.size())
      return 0;
    else
      return m_curr_idx + 1;
  }

  uint32_t GetNumPacketsInHistory() const {
    if (m_total_packet_count < m_packets.size())
      return m_total_packet_count;
    else
      return (uint32_t)m_packets.size();
  }

  uint32_t GetNextIndex() {
    ++m_total_packet_count;
    const uint32_t idx = m_curr_idx;
    m_curr_idx = NormalizeIndex(idx + 1);
    return idx;
  }

  uint32_t NormalizeIndex(uint32_t i) const {
    return m_packets.empty() ? 0 : i % m_packets.size();
  }

  std::vector<Entry> m_packets;
  uint32_t m_curr_idx;
  uint32_t m_total_packet_count;
  mutable bool m_dumped_to_log;
  llvm::raw_ostream *m_stream = nullptr;
};

} // namespace process_gdb_remote
} // namespace lldb_private

LLVM_YAML_IS_DOCUMENT_LIST_VECTOR(
    lldb_private::process_gdb_remote::GDBRemoteCommunicationHistory::Entry)

namespace llvm {
namespace yaml {

template <>
struct ScalarEnumerationTraits<lldb_private::process_gdb_remote::
                                   GDBRemoteCommunicationHistory::PacketType> {
  static void enumeration(IO &io,
                          lldb_private::process_gdb_remote::
                              GDBRemoteCommunicationHistory::PacketType &value);
};

template <>
struct ScalarTraits<lldb_private::process_gdb_remote::
                        GDBRemoteCommunicationHistory::Entry::BinaryData> {
  static void output(const lldb_private::process_gdb_remote::
                         GDBRemoteCommunicationHistory::Entry::BinaryData &,
                     void *, raw_ostream &);

  static StringRef
  input(StringRef, void *,
        lldb_private::process_gdb_remote::GDBRemoteCommunicationHistory::Entry::
            BinaryData &);

  static QuotingType mustQuote(StringRef S) { return QuotingType::None; }
};

template <>
struct MappingTraits<
    lldb_private::process_gdb_remote::GDBRemoteCommunicationHistory::Entry> {
  static void
  mapping(IO &io,
          lldb_private::process_gdb_remote::GDBRemoteCommunicationHistory::Entry
              &Entry);

  static StringRef validate(
      IO &io,
      lldb_private::process_gdb_remote::GDBRemoteCommunicationHistory::Entry &);
};

} // namespace yaml
} // namespace llvm

#endif // liblldb_GDBRemoteCommunicationHistory_h_
