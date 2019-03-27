//===-- GDBRemoteCommunication.cpp ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "GDBRemoteCommunication.h"

#include <future>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>

#include "lldb/Core/StreamFile.h"
#include "lldb/Host/ConnectionFileDescriptor.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/Pipe.h"
#include "lldb/Host/Socket.h"
#include "lldb/Host/StringConvert.h"
#include "lldb/Host/ThreadLauncher.h"
#include "lldb/Host/common/TCPSocket.h"
#include "lldb/Host/posix/ConnectionFileDescriptorPosix.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/StreamString.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/ScopedPrinter.h"

#include "ProcessGDBRemoteLog.h"

#if defined(__APPLE__)
#define DEBUGSERVER_BASENAME "debugserver"
#else
#define DEBUGSERVER_BASENAME "lldb-server"
#endif

#if defined(__APPLE__)
#define HAVE_LIBCOMPRESSION
#include <compression.h>
#endif

#if defined(HAVE_LIBZ)
#include <zlib.h>
#endif

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_gdb_remote;

//----------------------------------------------------------------------
// GDBRemoteCommunication constructor
//----------------------------------------------------------------------
GDBRemoteCommunication::GDBRemoteCommunication(const char *comm_name,
                                               const char *listener_name)
    : Communication(comm_name),
#ifdef LLDB_CONFIGURATION_DEBUG
      m_packet_timeout(1000),
#else
      m_packet_timeout(1),
#endif
      m_echo_number(0), m_supports_qEcho(eLazyBoolCalculate), m_history(512),
      m_send_acks(true), m_compression_type(CompressionType::None),
      m_listen_url(), m_decompression_scratch_type(CompressionType::None),
      m_decompression_scratch(nullptr) {
  // Unused unless HAVE_LIBCOMPRESSION is defined.
  (void)m_decompression_scratch_type;
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
GDBRemoteCommunication::~GDBRemoteCommunication() {
  if (IsConnected()) {
    Disconnect();
  }

  if (m_decompression_scratch)
    free (m_decompression_scratch);

  // Stop the communications read thread which is used to parse all incoming
  // packets.  This function will block until the read thread returns.
  if (m_read_thread_enabled)
    StopReadThread();
}

char GDBRemoteCommunication::CalculcateChecksum(llvm::StringRef payload) {
  int checksum = 0;

  for (char c : payload)
    checksum += c;

  return checksum & 255;
}

size_t GDBRemoteCommunication::SendAck() {
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PACKETS));
  ConnectionStatus status = eConnectionStatusSuccess;
  char ch = '+';
  const size_t bytes_written = Write(&ch, 1, status, NULL);
  if (log)
    log->Printf("<%4" PRIu64 "> send packet: %c", (uint64_t)bytes_written, ch);
  m_history.AddPacket(ch, GDBRemoteCommunicationHistory::ePacketTypeSend,
                      bytes_written);
  return bytes_written;
}

size_t GDBRemoteCommunication::SendNack() {
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PACKETS));
  ConnectionStatus status = eConnectionStatusSuccess;
  char ch = '-';
  const size_t bytes_written = Write(&ch, 1, status, NULL);
  if (log)
    log->Printf("<%4" PRIu64 "> send packet: %c", (uint64_t)bytes_written, ch);
  m_history.AddPacket(ch, GDBRemoteCommunicationHistory::ePacketTypeSend,
                      bytes_written);
  return bytes_written;
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunication::SendPacketNoLock(llvm::StringRef payload) {
    StreamString packet(0, 4, eByteOrderBig);
    packet.PutChar('$');
    packet.Write(payload.data(), payload.size());
    packet.PutChar('#');
    packet.PutHex8(CalculcateChecksum(payload));
    std::string packet_str = packet.GetString();

    return SendRawPacketNoLock(packet_str);
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunication::SendRawPacketNoLock(llvm::StringRef packet,
                                            bool skip_ack) {
  if (IsConnected()) {
    Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PACKETS));
    ConnectionStatus status = eConnectionStatusSuccess;
    const char *packet_data = packet.data();
    const size_t packet_length = packet.size();
    size_t bytes_written = Write(packet_data, packet_length, status, NULL);
    if (log) {
      size_t binary_start_offset = 0;
      if (strncmp(packet_data, "$vFile:pwrite:", strlen("$vFile:pwrite:")) ==
          0) {
        const char *first_comma = strchr(packet_data, ',');
        if (first_comma) {
          const char *second_comma = strchr(first_comma + 1, ',');
          if (second_comma)
            binary_start_offset = second_comma - packet_data + 1;
        }
      }

      // If logging was just enabled and we have history, then dump out what we
      // have to the log so we get the historical context. The Dump() call that
      // logs all of the packet will set a boolean so that we don't dump this
      // more than once
      if (!m_history.DidDumpToLog())
        m_history.Dump(log);

      if (binary_start_offset) {
        StreamString strm;
        // Print non binary data header
        strm.Printf("<%4" PRIu64 "> send packet: %.*s", (uint64_t)bytes_written,
                    (int)binary_start_offset, packet_data);
        const uint8_t *p;
        // Print binary data exactly as sent
        for (p = (const uint8_t *)packet_data + binary_start_offset; *p != '#';
             ++p)
          strm.Printf("\\x%2.2x", *p);
        // Print the checksum
        strm.Printf("%*s", (int)3, p);
        log->PutString(strm.GetString());
      } else
        log->Printf("<%4" PRIu64 "> send packet: %.*s", (uint64_t)bytes_written,
                    (int)packet_length, packet_data);
    }

    m_history.AddPacket(packet.str(), packet_length,
                        GDBRemoteCommunicationHistory::ePacketTypeSend,
                        bytes_written);

    if (bytes_written == packet_length) {
      if (!skip_ack && GetSendAcks())
        return GetAck();
      else
        return PacketResult::Success;
    } else {
      if (log)
        log->Printf("error: failed to send packet: %.*s", (int)packet_length,
                    packet_data);
    }
  }
  return PacketResult::ErrorSendFailed;
}

GDBRemoteCommunication::PacketResult GDBRemoteCommunication::GetAck() {
  StringExtractorGDBRemote packet;
  PacketResult result = ReadPacket(packet, GetPacketTimeout(), false);
  if (result == PacketResult::Success) {
    if (packet.GetResponseType() ==
        StringExtractorGDBRemote::ResponseType::eAck)
      return PacketResult::Success;
    else
      return PacketResult::ErrorSendAck;
  }
  return result;
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunication::ReadPacketWithOutputSupport(
    StringExtractorGDBRemote &response, Timeout<std::micro> timeout,
    bool sync_on_timeout,
    llvm::function_ref<void(llvm::StringRef)> output_callback) {
  auto result = ReadPacket(response, timeout, sync_on_timeout);
  while (result == PacketResult::Success && response.IsNormalResponse() &&
         response.PeekChar() == 'O') {
    response.GetChar();
    std::string output;
    if (response.GetHexByteString(output))
      output_callback(output);
    result = ReadPacket(response, timeout, sync_on_timeout);
  }
  return result;
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunication::ReadPacket(StringExtractorGDBRemote &response,
                                   Timeout<std::micro> timeout,
                                   bool sync_on_timeout) {
  if (m_read_thread_enabled)
    return PopPacketFromQueue(response, timeout);
  else
    return WaitForPacketNoLock(response, timeout, sync_on_timeout);
}

// This function is called when a packet is requested.
// A whole packet is popped from the packet queue and returned to the caller.
// Packets are placed into this queue from the communication read thread. See
// GDBRemoteCommunication::AppendBytesToCache.
GDBRemoteCommunication::PacketResult
GDBRemoteCommunication::PopPacketFromQueue(StringExtractorGDBRemote &response,
                                           Timeout<std::micro> timeout) {
  auto pred = [&] { return !m_packet_queue.empty() && IsConnected(); };
  // lock down the packet queue
  std::unique_lock<std::mutex> lock(m_packet_queue_mutex);

  if (!timeout)
    m_condition_queue_not_empty.wait(lock, pred);
  else {
    if (!m_condition_queue_not_empty.wait_for(lock, *timeout, pred))
      return PacketResult::ErrorReplyTimeout;
    if (!IsConnected())
      return PacketResult::ErrorDisconnected;
  }

  // get the front element of the queue
  response = m_packet_queue.front();

  // remove the front element
  m_packet_queue.pop();

  // we got a packet
  return PacketResult::Success;
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunication::WaitForPacketNoLock(StringExtractorGDBRemote &packet,
                                            Timeout<std::micro> timeout,
                                            bool sync_on_timeout) {
  uint8_t buffer[8192];
  Status error;

  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PACKETS));

  // Check for a packet from our cache first without trying any reading...
  if (CheckForPacket(NULL, 0, packet) != PacketType::Invalid)
    return PacketResult::Success;

  bool timed_out = false;
  bool disconnected = false;
  while (IsConnected() && !timed_out) {
    lldb::ConnectionStatus status = eConnectionStatusNoConnection;
    size_t bytes_read = Read(buffer, sizeof(buffer), timeout, status, &error);

    LLDB_LOGV(log,
              "Read(buffer, sizeof(buffer), timeout = {0}, "
              "status = {1}, error = {2}) => bytes_read = {3}",
              timeout, Communication::ConnectionStatusAsCString(status), error,
              bytes_read);

    if (bytes_read > 0) {
      if (CheckForPacket(buffer, bytes_read, packet) != PacketType::Invalid)
        return PacketResult::Success;
    } else {
      switch (status) {
      case eConnectionStatusTimedOut:
      case eConnectionStatusInterrupted:
        if (sync_on_timeout) {
          //------------------------------------------------------------------
          /// Sync the remote GDB server and make sure we get a response that
          /// corresponds to what we send.
          ///
          /// Sends a "qEcho" packet and makes sure it gets the exact packet
          /// echoed back. If the qEcho packet isn't supported, we send a qC
          /// packet and make sure we get a valid thread ID back. We use the
          /// "qC" packet since its response if very unique: is responds with
          /// "QC%x" where %x is the thread ID of the current thread. This
          /// makes the response unique enough from other packet responses to
          /// ensure we are back on track.
          ///
          /// This packet is needed after we time out sending a packet so we
          /// can ensure that we are getting the response for the packet we
          /// are sending. There are no sequence IDs in the GDB remote
          /// protocol (there used to be, but they are not supported anymore)
          /// so if you timeout sending packet "abc", you might then send
          /// packet "cde" and get the response for the previous "abc" packet.
          /// Many responses are "OK" or "" (unsupported) or "EXX" (error) so
          /// many responses for packets can look like responses for other
          /// packets. So if we timeout, we need to ensure that we can get
          /// back on track. If we can't get back on track, we must
          /// disconnect.
          //------------------------------------------------------------------
          bool sync_success = false;
          bool got_actual_response = false;
          // We timed out, we need to sync back up with the
          char echo_packet[32];
          int echo_packet_len = 0;
          RegularExpression response_regex;

          if (m_supports_qEcho == eLazyBoolYes) {
            echo_packet_len = ::snprintf(echo_packet, sizeof(echo_packet),
                                         "qEcho:%u", ++m_echo_number);
            std::string regex_str = "^";
            regex_str += echo_packet;
            regex_str += "$";
            response_regex.Compile(regex_str);
          } else {
            echo_packet_len =
                ::snprintf(echo_packet, sizeof(echo_packet), "qC");
            response_regex.Compile(llvm::StringRef("^QC[0-9A-Fa-f]+$"));
          }

          PacketResult echo_packet_result =
              SendPacketNoLock(llvm::StringRef(echo_packet, echo_packet_len));
          if (echo_packet_result == PacketResult::Success) {
            const uint32_t max_retries = 3;
            uint32_t successful_responses = 0;
            for (uint32_t i = 0; i < max_retries; ++i) {
              StringExtractorGDBRemote echo_response;
              echo_packet_result =
                  WaitForPacketNoLock(echo_response, timeout, false);
              if (echo_packet_result == PacketResult::Success) {
                ++successful_responses;
                if (response_regex.Execute(echo_response.GetStringRef())) {
                  sync_success = true;
                  break;
                } else if (successful_responses == 1) {
                  // We got something else back as the first successful
                  // response, it probably is the  response to the packet we
                  // actually wanted, so copy it over if this is the first
                  // success and continue to try to get the qEcho response
                  packet = echo_response;
                  got_actual_response = true;
                }
              } else if (echo_packet_result == PacketResult::ErrorReplyTimeout)
                continue; // Packet timed out, continue waiting for a response
              else
                break; // Something else went wrong getting the packet back, we
                       // failed and are done trying
            }
          }

          // We weren't able to sync back up with the server, we must abort
          // otherwise all responses might not be from the right packets...
          if (sync_success) {
            // We timed out, but were able to recover
            if (got_actual_response) {
              // We initially timed out, but we did get a response that came in
              // before the successful reply to our qEcho packet, so lets say
              // everything is fine...
              return PacketResult::Success;
            }
          } else {
            disconnected = true;
            Disconnect();
          }
        }
        timed_out = true;
        break;
      case eConnectionStatusSuccess:
        // printf ("status = success but error = %s\n",
        // error.AsCString("<invalid>"));
        break;

      case eConnectionStatusEndOfFile:
      case eConnectionStatusNoConnection:
      case eConnectionStatusLostConnection:
      case eConnectionStatusError:
        disconnected = true;
        Disconnect();
        break;
      }
    }
  }
  packet.Clear();
  if (disconnected)
    return PacketResult::ErrorDisconnected;
  if (timed_out)
    return PacketResult::ErrorReplyTimeout;
  else
    return PacketResult::ErrorReplyFailed;
}

bool GDBRemoteCommunication::DecompressPacket() {
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PACKETS));

  if (!CompressionIsEnabled())
    return true;

  size_t pkt_size = m_bytes.size();

  // Smallest possible compressed packet is $N#00 - an uncompressed empty
  // reply, most commonly indicating an unsupported packet.  Anything less than
  // 5 characters, it's definitely not a compressed packet.
  if (pkt_size < 5)
    return true;

  if (m_bytes[0] != '$' && m_bytes[0] != '%')
    return true;
  if (m_bytes[1] != 'C' && m_bytes[1] != 'N')
    return true;

  size_t hash_mark_idx = m_bytes.find('#');
  if (hash_mark_idx == std::string::npos)
    return true;
  if (hash_mark_idx + 2 >= m_bytes.size())
    return true;

  if (!::isxdigit(m_bytes[hash_mark_idx + 1]) ||
      !::isxdigit(m_bytes[hash_mark_idx + 2]))
    return true;

  size_t content_length =
      pkt_size -
      5; // not counting '$', 'C' | 'N', '#', & the two hex checksum chars
  size_t content_start = 2; // The first character of the
                            // compressed/not-compressed text of the packet
  size_t checksum_idx =
      hash_mark_idx +
      1; // The first character of the two hex checksum characters

  // Normally size_of_first_packet == m_bytes.size() but m_bytes may contain
  // multiple packets. size_of_first_packet is the size of the initial packet
  // which we'll replace with the decompressed version of, leaving the rest of
  // m_bytes unmodified.
  size_t size_of_first_packet = hash_mark_idx + 3;

  // Compressed packets ("$C") start with a base10 number which is the size of
  // the uncompressed payload, then a : and then the compressed data.  e.g.
  // $C1024:<binary>#00 Update content_start and content_length to only include
  // the <binary> part of the packet.

  uint64_t decompressed_bufsize = ULONG_MAX;
  if (m_bytes[1] == 'C') {
    size_t i = content_start;
    while (i < hash_mark_idx && isdigit(m_bytes[i]))
      i++;
    if (i < hash_mark_idx && m_bytes[i] == ':') {
      i++;
      content_start = i;
      content_length = hash_mark_idx - content_start;
      std::string bufsize_str(m_bytes.data() + 2, i - 2 - 1);
      errno = 0;
      decompressed_bufsize = ::strtoul(bufsize_str.c_str(), NULL, 10);
      if (errno != 0 || decompressed_bufsize == ULONG_MAX) {
        m_bytes.erase(0, size_of_first_packet);
        return false;
      }
    }
  }

  if (GetSendAcks()) {
    char packet_checksum_cstr[3];
    packet_checksum_cstr[0] = m_bytes[checksum_idx];
    packet_checksum_cstr[1] = m_bytes[checksum_idx + 1];
    packet_checksum_cstr[2] = '\0';
    long packet_checksum = strtol(packet_checksum_cstr, NULL, 16);

    long actual_checksum = CalculcateChecksum(
        llvm::StringRef(m_bytes).substr(1, hash_mark_idx - 1));
    bool success = packet_checksum == actual_checksum;
    if (!success) {
      if (log)
        log->Printf(
            "error: checksum mismatch: %.*s expected 0x%2.2x, got 0x%2.2x",
            (int)(pkt_size), m_bytes.c_str(), (uint8_t)packet_checksum,
            (uint8_t)actual_checksum);
    }
    // Send the ack or nack if needed
    if (!success) {
      SendNack();
      m_bytes.erase(0, size_of_first_packet);
      return false;
    } else {
      SendAck();
    }
  }

  if (m_bytes[1] == 'N') {
    // This packet was not compressed -- delete the 'N' character at the start
    // and the packet may be processed as-is.
    m_bytes.erase(1, 1);
    return true;
  }

  // Reverse the gdb-remote binary escaping that was done to the compressed
  // text to guard characters like '$', '#', '}', etc.
  std::vector<uint8_t> unescaped_content;
  unescaped_content.reserve(content_length);
  size_t i = content_start;
  while (i < hash_mark_idx) {
    if (m_bytes[i] == '}') {
      i++;
      unescaped_content.push_back(m_bytes[i] ^ 0x20);
    } else {
      unescaped_content.push_back(m_bytes[i]);
    }
    i++;
  }

  uint8_t *decompressed_buffer = nullptr;
  size_t decompressed_bytes = 0;

  if (decompressed_bufsize != ULONG_MAX) {
    decompressed_buffer = (uint8_t *)malloc(decompressed_bufsize);
    if (decompressed_buffer == nullptr) {
      m_bytes.erase(0, size_of_first_packet);
      return false;
    }
  }

#if defined(HAVE_LIBCOMPRESSION)
  if (m_compression_type == CompressionType::ZlibDeflate ||
      m_compression_type == CompressionType::LZFSE ||
      m_compression_type == CompressionType::LZ4 || 
      m_compression_type == CompressionType::LZMA) {
    compression_algorithm compression_type;
    if (m_compression_type == CompressionType::LZFSE)
      compression_type = COMPRESSION_LZFSE;
    else if (m_compression_type == CompressionType::ZlibDeflate)
      compression_type = COMPRESSION_ZLIB;
    else if (m_compression_type == CompressionType::LZ4)
      compression_type = COMPRESSION_LZ4_RAW;
    else if (m_compression_type == CompressionType::LZMA)
      compression_type = COMPRESSION_LZMA;

    if (m_decompression_scratch_type != m_compression_type) {
      if (m_decompression_scratch) {
        free (m_decompression_scratch);
        m_decompression_scratch = nullptr;
      }
      size_t scratchbuf_size = 0;
      if (m_compression_type == CompressionType::LZFSE)
        scratchbuf_size = compression_decode_scratch_buffer_size (COMPRESSION_LZFSE);
      else if (m_compression_type == CompressionType::LZ4)
        scratchbuf_size = compression_decode_scratch_buffer_size (COMPRESSION_LZ4_RAW);
      else if (m_compression_type == CompressionType::ZlibDeflate)
        scratchbuf_size = compression_decode_scratch_buffer_size (COMPRESSION_ZLIB);
      else if (m_compression_type == CompressionType::LZMA)
        scratchbuf_size = compression_decode_scratch_buffer_size (COMPRESSION_LZMA);
      else if (m_compression_type == CompressionType::LZFSE)
        scratchbuf_size = compression_decode_scratch_buffer_size (COMPRESSION_LZFSE);
      if (scratchbuf_size > 0) {
        m_decompression_scratch = (void*) malloc (scratchbuf_size);
        m_decompression_scratch_type = m_compression_type;
      }
    }

    if (decompressed_bufsize != ULONG_MAX && decompressed_buffer != nullptr) {
      decompressed_bytes = compression_decode_buffer(
          decompressed_buffer, decompressed_bufsize,
          (uint8_t *)unescaped_content.data(), unescaped_content.size(), 
          m_decompression_scratch, compression_type);
    }
  }
#endif

#if defined(HAVE_LIBZ)
  if (decompressed_bytes == 0 && decompressed_bufsize != ULONG_MAX &&
      decompressed_buffer != nullptr &&
      m_compression_type == CompressionType::ZlibDeflate) {
    z_stream stream;
    memset(&stream, 0, sizeof(z_stream));
    stream.next_in = (Bytef *)unescaped_content.data();
    stream.avail_in = (uInt)unescaped_content.size();
    stream.total_in = 0;
    stream.next_out = (Bytef *)decompressed_buffer;
    stream.avail_out = decompressed_bufsize;
    stream.total_out = 0;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;

    if (inflateInit2(&stream, -15) == Z_OK) {
      int status = inflate(&stream, Z_NO_FLUSH);
      inflateEnd(&stream);
      if (status == Z_STREAM_END) {
        decompressed_bytes = stream.total_out;
      }
    }
  }
#endif

  if (decompressed_bytes == 0 || decompressed_buffer == nullptr) {
    if (decompressed_buffer)
      free(decompressed_buffer);
    m_bytes.erase(0, size_of_first_packet);
    return false;
  }

  std::string new_packet;
  new_packet.reserve(decompressed_bytes + 6);
  new_packet.push_back(m_bytes[0]);
  new_packet.append((const char *)decompressed_buffer, decompressed_bytes);
  new_packet.push_back('#');
  if (GetSendAcks()) {
    uint8_t decompressed_checksum = CalculcateChecksum(
        llvm::StringRef((const char *)decompressed_buffer, decompressed_bytes));
    char decompressed_checksum_str[3];
    snprintf(decompressed_checksum_str, 3, "%02x", decompressed_checksum);
    new_packet.append(decompressed_checksum_str);
  } else {
    new_packet.push_back('0');
    new_packet.push_back('0');
  }

  m_bytes.replace(0, size_of_first_packet, new_packet.data(),
                  new_packet.size());

  free(decompressed_buffer);
  return true;
}

GDBRemoteCommunication::PacketType
GDBRemoteCommunication::CheckForPacket(const uint8_t *src, size_t src_len,
                                       StringExtractorGDBRemote &packet) {
  // Put the packet data into the buffer in a thread safe fashion
  std::lock_guard<std::recursive_mutex> guard(m_bytes_mutex);

  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PACKETS));

  if (src && src_len > 0) {
    if (log && log->GetVerbose()) {
      StreamString s;
      log->Printf("GDBRemoteCommunication::%s adding %u bytes: %.*s",
                  __FUNCTION__, (uint32_t)src_len, (uint32_t)src_len, src);
    }
    m_bytes.append((const char *)src, src_len);
  }

  bool isNotifyPacket = false;

  // Parse up the packets into gdb remote packets
  if (!m_bytes.empty()) {
    // end_idx must be one past the last valid packet byte. Start it off with
    // an invalid value that is the same as the current index.
    size_t content_start = 0;
    size_t content_length = 0;
    size_t total_length = 0;
    size_t checksum_idx = std::string::npos;

    // Size of packet before it is decompressed, for logging purposes
    size_t original_packet_size = m_bytes.size();
    if (CompressionIsEnabled()) {
      if (!DecompressPacket()) {
        packet.Clear();
        return GDBRemoteCommunication::PacketType::Standard;
      }
    }

    switch (m_bytes[0]) {
    case '+':                            // Look for ack
    case '-':                            // Look for cancel
    case '\x03':                         // ^C to halt target
      content_length = total_length = 1; // The command is one byte long...
      break;

    case '%': // Async notify packet
      isNotifyPacket = true;
      LLVM_FALLTHROUGH;

    case '$':
      // Look for a standard gdb packet?
      {
        size_t hash_pos = m_bytes.find('#');
        if (hash_pos != std::string::npos) {
          if (hash_pos + 2 < m_bytes.size()) {
            checksum_idx = hash_pos + 1;
            // Skip the dollar sign
            content_start = 1;
            // Don't include the # in the content or the $ in the content
            // length
            content_length = hash_pos - 1;

            total_length =
                hash_pos + 3; // Skip the # and the two hex checksum bytes
          } else {
            // Checksum bytes aren't all here yet
            content_length = std::string::npos;
          }
        }
      }
      break;

    default: {
      // We have an unexpected byte and we need to flush all bad data that is
      // in m_bytes, so we need to find the first byte that is a '+' (ACK), '-'
      // (NACK), \x03 (CTRL+C interrupt), or '$' character (start of packet
      // header) or of course, the end of the data in m_bytes...
      const size_t bytes_len = m_bytes.size();
      bool done = false;
      uint32_t idx;
      for (idx = 1; !done && idx < bytes_len; ++idx) {
        switch (m_bytes[idx]) {
        case '+':
        case '-':
        case '\x03':
        case '%':
        case '$':
          done = true;
          break;

        default:
          break;
        }
      }
      if (log)
        log->Printf("GDBRemoteCommunication::%s tossing %u junk bytes: '%.*s'",
                    __FUNCTION__, idx - 1, idx - 1, m_bytes.c_str());
      m_bytes.erase(0, idx - 1);
    } break;
    }

    if (content_length == std::string::npos) {
      packet.Clear();
      return GDBRemoteCommunication::PacketType::Invalid;
    } else if (total_length > 0) {

      // We have a valid packet...
      assert(content_length <= m_bytes.size());
      assert(total_length <= m_bytes.size());
      assert(content_length <= total_length);
      size_t content_end = content_start + content_length;

      bool success = true;
      std::string &packet_str = packet.GetStringRef();
      if (log) {
        // If logging was just enabled and we have history, then dump out what
        // we have to the log so we get the historical context. The Dump() call
        // that logs all of the packet will set a boolean so that we don't dump
        // this more than once
        if (!m_history.DidDumpToLog())
          m_history.Dump(log);

        bool binary = false;
        // Only detect binary for packets that start with a '$' and have a
        // '#CC' checksum
        if (m_bytes[0] == '$' && total_length > 4) {
          for (size_t i = 0; !binary && i < total_length; ++i) {
            unsigned char c = m_bytes[i];
            if (isprint(c) == 0 && isspace(c) == 0) {
              binary = true;
            }
          }
        }
        if (binary) {
          StreamString strm;
          // Packet header...
          if (CompressionIsEnabled())
            strm.Printf("<%4" PRIu64 ":%" PRIu64 "> read packet: %c",
                        (uint64_t)original_packet_size, (uint64_t)total_length,
                        m_bytes[0]);
          else
            strm.Printf("<%4" PRIu64 "> read packet: %c",
                        (uint64_t)total_length, m_bytes[0]);
          for (size_t i = content_start; i < content_end; ++i) {
            // Remove binary escaped bytes when displaying the packet...
            const char ch = m_bytes[i];
            if (ch == 0x7d) {
              // 0x7d is the escape character.  The next character is to be
              // XOR'd with 0x20.
              const char escapee = m_bytes[++i] ^ 0x20;
              strm.Printf("%2.2x", escapee);
            } else {
              strm.Printf("%2.2x", (uint8_t)ch);
            }
          }
          // Packet footer...
          strm.Printf("%c%c%c", m_bytes[total_length - 3],
                      m_bytes[total_length - 2], m_bytes[total_length - 1]);
          log->PutString(strm.GetString());
        } else {
          if (CompressionIsEnabled())
            log->Printf("<%4" PRIu64 ":%" PRIu64 "> read packet: %.*s",
                        (uint64_t)original_packet_size, (uint64_t)total_length,
                        (int)(total_length), m_bytes.c_str());
          else
            log->Printf("<%4" PRIu64 "> read packet: %.*s",
                        (uint64_t)total_length, (int)(total_length),
                        m_bytes.c_str());
        }
      }

      m_history.AddPacket(m_bytes, total_length,
                          GDBRemoteCommunicationHistory::ePacketTypeRecv,
                          total_length);

      // Clear packet_str in case there is some existing data in it.
      packet_str.clear();
      // Copy the packet from m_bytes to packet_str expanding the run-length
      // encoding in the process. Reserve enough byte for the most common case
      // (no RLE used)
      packet_str.reserve(m_bytes.length());
      for (std::string::const_iterator c = m_bytes.begin() + content_start;
           c != m_bytes.begin() + content_end; ++c) {
        if (*c == '*') {
          // '*' indicates RLE. Next character will give us the repeat count
          // and previous character is what is to be repeated.
          char char_to_repeat = packet_str.back();
          // Number of time the previous character is repeated
          int repeat_count = *++c + 3 - ' ';
          // We have the char_to_repeat and repeat_count. Now push it in the
          // packet.
          for (int i = 0; i < repeat_count; ++i)
            packet_str.push_back(char_to_repeat);
        } else if (*c == 0x7d) {
          // 0x7d is the escape character.  The next character is to be XOR'd
          // with 0x20.
          char escapee = *++c ^ 0x20;
          packet_str.push_back(escapee);
        } else {
          packet_str.push_back(*c);
        }
      }

      if (m_bytes[0] == '$' || m_bytes[0] == '%') {
        assert(checksum_idx < m_bytes.size());
        if (::isxdigit(m_bytes[checksum_idx + 0]) ||
            ::isxdigit(m_bytes[checksum_idx + 1])) {
          if (GetSendAcks()) {
            const char *packet_checksum_cstr = &m_bytes[checksum_idx];
            char packet_checksum = strtol(packet_checksum_cstr, NULL, 16);
            char actual_checksum = CalculcateChecksum(
                llvm::StringRef(m_bytes).slice(content_start, content_end));
            success = packet_checksum == actual_checksum;
            if (!success) {
              if (log)
                log->Printf("error: checksum mismatch: %.*s expected 0x%2.2x, "
                            "got 0x%2.2x",
                            (int)(total_length), m_bytes.c_str(),
                            (uint8_t)packet_checksum, (uint8_t)actual_checksum);
            }
            // Send the ack or nack if needed
            if (!success)
              SendNack();
            else
              SendAck();
          }
        } else {
          success = false;
          if (log)
            log->Printf("error: invalid checksum in packet: '%s'\n",
                        m_bytes.c_str());
        }
      }

      m_bytes.erase(0, total_length);
      packet.SetFilePos(0);

      if (isNotifyPacket)
        return GDBRemoteCommunication::PacketType::Notify;
      else
        return GDBRemoteCommunication::PacketType::Standard;
    }
  }
  packet.Clear();
  return GDBRemoteCommunication::PacketType::Invalid;
}

Status GDBRemoteCommunication::StartListenThread(const char *hostname,
                                                 uint16_t port) {
  Status error;
  if (m_listen_thread.IsJoinable()) {
    error.SetErrorString("listen thread already running");
  } else {
    char listen_url[512];
    if (hostname && hostname[0])
      snprintf(listen_url, sizeof(listen_url), "listen://%s:%i", hostname,
               port);
    else
      snprintf(listen_url, sizeof(listen_url), "listen://%i", port);
    m_listen_url = listen_url;
    SetConnection(new ConnectionFileDescriptor());
    m_listen_thread = ThreadLauncher::LaunchThread(
        listen_url, GDBRemoteCommunication::ListenThread, this, &error);
  }
  return error;
}

bool GDBRemoteCommunication::JoinListenThread() {
  if (m_listen_thread.IsJoinable())
    m_listen_thread.Join(nullptr);
  return true;
}

lldb::thread_result_t
GDBRemoteCommunication::ListenThread(lldb::thread_arg_t arg) {
  GDBRemoteCommunication *comm = (GDBRemoteCommunication *)arg;
  Status error;
  ConnectionFileDescriptor *connection =
      (ConnectionFileDescriptor *)comm->GetConnection();

  if (connection) {
    // Do the listen on another thread so we can continue on...
    if (connection->Connect(comm->m_listen_url.c_str(), &error) !=
        eConnectionStatusSuccess)
      comm->SetConnection(NULL);
  }
  return NULL;
}

Status GDBRemoteCommunication::StartDebugserverProcess(
    const char *url, Platform *platform, ProcessLaunchInfo &launch_info,
    uint16_t *port, const Args *inferior_args, int pass_comm_fd) {
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
  if (log)
    log->Printf("GDBRemoteCommunication::%s(url=%s, port=%" PRIu16 ")",
                __FUNCTION__, url ? url : "<empty>",
                port ? *port : uint16_t(0));

  Status error;
  // If we locate debugserver, keep that located version around
  static FileSpec g_debugserver_file_spec;

  char debugserver_path[PATH_MAX];
  FileSpec &debugserver_file_spec = launch_info.GetExecutableFile();

  // Always check to see if we have an environment override for the path to the
  // debugserver to use and use it if we do.
  const char *env_debugserver_path = getenv("LLDB_DEBUGSERVER_PATH");
  if (env_debugserver_path) {
    debugserver_file_spec.SetFile(env_debugserver_path,
                                  FileSpec::Style::native);
    if (log)
      log->Printf("GDBRemoteCommunication::%s() gdb-remote stub exe path set "
                  "from environment variable: %s",
                  __FUNCTION__, env_debugserver_path);
  } else
    debugserver_file_spec = g_debugserver_file_spec;
  bool debugserver_exists =
      FileSystem::Instance().Exists(debugserver_file_spec);
  if (!debugserver_exists) {
    // The debugserver binary is in the LLDB.framework/Resources directory.
    debugserver_file_spec = HostInfo::GetSupportExeDir();
    if (debugserver_file_spec) {
      debugserver_file_spec.AppendPathComponent(DEBUGSERVER_BASENAME);
      debugserver_exists = FileSystem::Instance().Exists(debugserver_file_spec);
      if (debugserver_exists) {
        if (log)
          log->Printf(
              "GDBRemoteCommunication::%s() found gdb-remote stub exe '%s'",
              __FUNCTION__, debugserver_file_spec.GetPath().c_str());

        g_debugserver_file_spec = debugserver_file_spec;
      } else {
        debugserver_file_spec =
            platform->LocateExecutable(DEBUGSERVER_BASENAME);
        if (debugserver_file_spec) {
          // Platform::LocateExecutable() wouldn't return a path if it doesn't
          // exist
          debugserver_exists = true;
        } else {
          if (log)
            log->Printf("GDBRemoteCommunication::%s() could not find "
                        "gdb-remote stub exe '%s'",
                        __FUNCTION__, debugserver_file_spec.GetPath().c_str());
        }
        // Don't cache the platform specific GDB server binary as it could
        // change from platform to platform
        g_debugserver_file_spec.Clear();
      }
    }
  }

  if (debugserver_exists) {
    debugserver_file_spec.GetPath(debugserver_path, sizeof(debugserver_path));

    Args &debugserver_args = launch_info.GetArguments();
    debugserver_args.Clear();
    char arg_cstr[PATH_MAX];

    // Start args with "debugserver /file/path -r --"
    debugserver_args.AppendArgument(llvm::StringRef(debugserver_path));

#if !defined(__APPLE__)
    // First argument to lldb-server must be mode in which to run.
    debugserver_args.AppendArgument(llvm::StringRef("gdbserver"));
#endif

    // If a url is supplied then use it
    if (url)
      debugserver_args.AppendArgument(llvm::StringRef(url));

    if (pass_comm_fd >= 0) {
      StreamString fd_arg;
      fd_arg.Printf("--fd=%i", pass_comm_fd);
      debugserver_args.AppendArgument(fd_arg.GetString());
      // Send "pass_comm_fd" down to the inferior so it can use it to
      // communicate back with this process
      launch_info.AppendDuplicateFileAction(pass_comm_fd, pass_comm_fd);
    }

    // use native registers, not the GDB registers
    debugserver_args.AppendArgument(llvm::StringRef("--native-regs"));

    if (launch_info.GetLaunchInSeparateProcessGroup()) {
      debugserver_args.AppendArgument(llvm::StringRef("--setsid"));
    }

    llvm::SmallString<128> named_pipe_path;
    // socket_pipe is used by debug server to communicate back either
    // TCP port or domain socket name which it listens on.
    // The second purpose of the pipe to serve as a synchronization point -
    // once data is written to the pipe, debug server is up and running.
    Pipe socket_pipe;

    // port is null when debug server should listen on domain socket - we're
    // not interested in port value but rather waiting for debug server to
    // become available.
    if (pass_comm_fd == -1) {
      if (url) {
// Create a temporary file to get the stdout/stderr and redirect the output of
// the command into this file. We will later read this file if all goes well
// and fill the data into "command_output_ptr"
#if defined(__APPLE__)
        // Binding to port zero, we need to figure out what port it ends up
        // using using a named pipe...
        error = socket_pipe.CreateWithUniqueName("debugserver-named-pipe",
                                                 false, named_pipe_path);
        if (error.Fail()) {
          if (log)
            log->Printf("GDBRemoteCommunication::%s() "
                        "named pipe creation failed: %s",
                        __FUNCTION__, error.AsCString());
          return error;
        }
        debugserver_args.AppendArgument(llvm::StringRef("--named-pipe"));
        debugserver_args.AppendArgument(named_pipe_path);
#else
        // Binding to port zero, we need to figure out what port it ends up
        // using using an unnamed pipe...
        error = socket_pipe.CreateNew(true);
        if (error.Fail()) {
          if (log)
            log->Printf("GDBRemoteCommunication::%s() "
                        "unnamed pipe creation failed: %s",
                        __FUNCTION__, error.AsCString());
          return error;
        }
        pipe_t write = socket_pipe.GetWritePipe();
        debugserver_args.AppendArgument(llvm::StringRef("--pipe"));
        debugserver_args.AppendArgument(llvm::to_string(write));
        launch_info.AppendCloseFileAction(socket_pipe.GetReadFileDescriptor());
#endif
      } else {
        // No host and port given, so lets listen on our end and make the
        // debugserver connect to us..
        error = StartListenThread("127.0.0.1", 0);
        if (error.Fail()) {
          if (log)
            log->Printf("GDBRemoteCommunication::%s() unable to start listen "
                        "thread: %s",
                        __FUNCTION__, error.AsCString());
          return error;
        }

        ConnectionFileDescriptor *connection =
            (ConnectionFileDescriptor *)GetConnection();
        // Wait for 10 seconds to resolve the bound port
        uint16_t port_ = connection->GetListeningPort(std::chrono::seconds(10));
        if (port_ > 0) {
          char port_cstr[32];
          snprintf(port_cstr, sizeof(port_cstr), "127.0.0.1:%i", port_);
          // Send the host and port down that debugserver and specify an option
          // so that it connects back to the port we are listening to in this
          // process
          debugserver_args.AppendArgument(llvm::StringRef("--reverse-connect"));
          debugserver_args.AppendArgument(llvm::StringRef(port_cstr));
          if (port)
            *port = port_;
        } else {
          error.SetErrorString("failed to bind to port 0 on 127.0.0.1");
          if (log)
            log->Printf("GDBRemoteCommunication::%s() failed: %s", __FUNCTION__,
                        error.AsCString());
          return error;
        }
      }
    }

    const char *env_debugserver_log_file = getenv("LLDB_DEBUGSERVER_LOG_FILE");
    if (env_debugserver_log_file) {
      ::snprintf(arg_cstr, sizeof(arg_cstr), "--log-file=%s",
                 env_debugserver_log_file);
      debugserver_args.AppendArgument(llvm::StringRef(arg_cstr));
    }

#if defined(__APPLE__)
    const char *env_debugserver_log_flags =
        getenv("LLDB_DEBUGSERVER_LOG_FLAGS");
    if (env_debugserver_log_flags) {
      ::snprintf(arg_cstr, sizeof(arg_cstr), "--log-flags=%s",
                 env_debugserver_log_flags);
      debugserver_args.AppendArgument(llvm::StringRef(arg_cstr));
    }
#else
    const char *env_debugserver_log_channels =
        getenv("LLDB_SERVER_LOG_CHANNELS");
    if (env_debugserver_log_channels) {
      ::snprintf(arg_cstr, sizeof(arg_cstr), "--log-channels=%s",
                 env_debugserver_log_channels);
      debugserver_args.AppendArgument(llvm::StringRef(arg_cstr));
    }
#endif

    // Add additional args, starting with LLDB_DEBUGSERVER_EXTRA_ARG_1 until an
    // env var doesn't come back.
    uint32_t env_var_index = 1;
    bool has_env_var;
    do {
      char env_var_name[64];
      snprintf(env_var_name, sizeof(env_var_name),
               "LLDB_DEBUGSERVER_EXTRA_ARG_%" PRIu32, env_var_index++);
      const char *extra_arg = getenv(env_var_name);
      has_env_var = extra_arg != nullptr;

      if (has_env_var) {
        debugserver_args.AppendArgument(llvm::StringRef(extra_arg));
        if (log)
          log->Printf("GDBRemoteCommunication::%s adding env var %s contents "
                      "to stub command line (%s)",
                      __FUNCTION__, env_var_name, extra_arg);
      }
    } while (has_env_var);

    if (inferior_args && inferior_args->GetArgumentCount() > 0) {
      debugserver_args.AppendArgument(llvm::StringRef("--"));
      debugserver_args.AppendArguments(*inferior_args);
    }

    // Copy the current environment to the gdbserver/debugserver instance
    launch_info.GetEnvironment() = Host::GetEnvironment();

    // Close STDIN, STDOUT and STDERR.
    launch_info.AppendCloseFileAction(STDIN_FILENO);
    launch_info.AppendCloseFileAction(STDOUT_FILENO);
    launch_info.AppendCloseFileAction(STDERR_FILENO);

    // Redirect STDIN, STDOUT and STDERR to "/dev/null".
    launch_info.AppendSuppressFileAction(STDIN_FILENO, true, false);
    launch_info.AppendSuppressFileAction(STDOUT_FILENO, false, true);
    launch_info.AppendSuppressFileAction(STDERR_FILENO, false, true);

    if (log) {
      StreamString string_stream;
      Platform *const platform = nullptr;
      launch_info.Dump(string_stream, platform);
      log->Printf("launch info for gdb-remote stub:\n%s",
                  string_stream.GetData());
    }
    error = Host::LaunchProcess(launch_info);

    if (error.Success() &&
        (launch_info.GetProcessID() != LLDB_INVALID_PROCESS_ID) &&
        pass_comm_fd == -1) {
      if (named_pipe_path.size() > 0) {
        error = socket_pipe.OpenAsReader(named_pipe_path, false);
        if (error.Fail())
          if (log)
            log->Printf("GDBRemoteCommunication::%s() "
                        "failed to open named pipe %s for reading: %s",
                        __FUNCTION__, named_pipe_path.c_str(),
                        error.AsCString());
      }

      if (socket_pipe.CanWrite())
        socket_pipe.CloseWriteFileDescriptor();
      if (socket_pipe.CanRead()) {
        char port_cstr[PATH_MAX] = {0};
        port_cstr[0] = '\0';
        size_t num_bytes = sizeof(port_cstr);
        // Read port from pipe with 10 second timeout.
        error = socket_pipe.ReadWithTimeout(
            port_cstr, num_bytes, std::chrono::seconds{10}, num_bytes);
        if (error.Success() && (port != nullptr)) {
          assert(num_bytes > 0 && port_cstr[num_bytes - 1] == '\0');
          uint16_t child_port = StringConvert::ToUInt32(port_cstr, 0);
          if (*port == 0 || *port == child_port) {
            *port = child_port;
            if (log)
              log->Printf("GDBRemoteCommunication::%s() "
                          "debugserver listens %u port",
                          __FUNCTION__, *port);
          } else {
            if (log)
              log->Printf("GDBRemoteCommunication::%s() "
                          "debugserver listening on port "
                          "%d but requested port was %d",
                          __FUNCTION__, (uint32_t)child_port,
                          (uint32_t)(*port));
          }
        } else {
          if (log)
            log->Printf("GDBRemoteCommunication::%s() "
                        "failed to read a port value from pipe %s: %s",
                        __FUNCTION__, named_pipe_path.c_str(),
                        error.AsCString());
        }
        socket_pipe.Close();
      }

      if (named_pipe_path.size() > 0) {
        const auto err = socket_pipe.Delete(named_pipe_path);
        if (err.Fail()) {
          if (log)
            log->Printf(
                "GDBRemoteCommunication::%s failed to delete pipe %s: %s",
                __FUNCTION__, named_pipe_path.c_str(), err.AsCString());
        }
      }

      // Make sure we actually connect with the debugserver...
      JoinListenThread();
    }
  } else {
    error.SetErrorStringWithFormat("unable to locate " DEBUGSERVER_BASENAME);
  }

  if (error.Fail()) {
    if (log)
      log->Printf("GDBRemoteCommunication::%s() failed: %s", __FUNCTION__,
                  error.AsCString());
  }

  return error;
}

void GDBRemoteCommunication::DumpHistory(Stream &strm) { m_history.Dump(strm); }

void GDBRemoteCommunication::SetHistoryStream(llvm::raw_ostream *strm) {
  m_history.SetStream(strm);
};

llvm::Error
GDBRemoteCommunication::ConnectLocally(GDBRemoteCommunication &client,
                                       GDBRemoteCommunication &server) {
  const bool child_processes_inherit = false;
  const int backlog = 5;
  TCPSocket listen_socket(true, child_processes_inherit);
  if (llvm::Error error =
          listen_socket.Listen("127.0.0.1:0", backlog).ToError())
    return error;

  Socket *accept_socket;
  std::future<Status> accept_status = std::async(
      std::launch::async, [&] { return listen_socket.Accept(accept_socket); });

  llvm::SmallString<32> remote_addr;
  llvm::raw_svector_ostream(remote_addr)
      << "connect://localhost:" << listen_socket.GetLocalPortNumber();

  std::unique_ptr<ConnectionFileDescriptor> conn_up(
      new ConnectionFileDescriptor());
  if (conn_up->Connect(remote_addr, nullptr) != lldb::eConnectionStatusSuccess)
    return llvm::make_error<llvm::StringError>("Unable to connect",
                                               llvm::inconvertibleErrorCode());

  client.SetConnection(conn_up.release());
  if (llvm::Error error = accept_status.get().ToError())
    return error;

  server.SetConnection(new ConnectionFileDescriptor(accept_socket));
  return llvm::Error::success();
}

GDBRemoteCommunication::ScopedTimeout::ScopedTimeout(
    GDBRemoteCommunication &gdb_comm, std::chrono::seconds timeout)
  : m_gdb_comm(gdb_comm), m_timeout_modified(false) {
    auto curr_timeout = gdb_comm.GetPacketTimeout();
    // Only update the timeout if the timeout is greater than the current
    // timeout. If the current timeout is larger, then just use that.
    if (curr_timeout < timeout) {
      m_timeout_modified = true;
      m_saved_timeout = m_gdb_comm.SetPacketTimeout(timeout);
    }
}

GDBRemoteCommunication::ScopedTimeout::~ScopedTimeout() {
  // Only restore the timeout if we set it in the constructor.
  if (m_timeout_modified)
    m_gdb_comm.SetPacketTimeout(m_saved_timeout);
}

// This function is called via the Communications class read thread when bytes
// become available for this connection. This function will consume all
// incoming bytes and try to parse whole packets as they become available. Full
// packets are placed in a queue, so that all packet requests can simply pop
// from this queue. Async notification packets will be dispatched immediately
// to the ProcessGDBRemote Async thread via an event.
void GDBRemoteCommunication::AppendBytesToCache(const uint8_t *bytes,
                                                size_t len, bool broadcast,
                                                lldb::ConnectionStatus status) {
  StringExtractorGDBRemote packet;

  while (true) {
    PacketType type = CheckForPacket(bytes, len, packet);

    // scrub the data so we do not pass it back to CheckForPacket on future
    // passes of the loop
    bytes = nullptr;
    len = 0;

    // we may have received no packet so lets bail out
    if (type == PacketType::Invalid)
      break;

    if (type == PacketType::Standard) {
      // scope for the mutex
      {
        // lock down the packet queue
        std::lock_guard<std::mutex> guard(m_packet_queue_mutex);
        // push a new packet into the queue
        m_packet_queue.push(packet);
        // Signal condition variable that we have a packet
        m_condition_queue_not_empty.notify_one();
      }
    }

    if (type == PacketType::Notify) {
      // put this packet into an event
      const char *pdata = packet.GetStringRef().c_str();

      // as the communication class, we are a broadcaster and the async thread
      // is tuned to listen to us
      BroadcastEvent(eBroadcastBitGdbReadThreadGotNotify,
                     new EventDataBytes(pdata));
    }
  }
}

void llvm::format_provider<GDBRemoteCommunication::PacketResult>::format(
    const GDBRemoteCommunication::PacketResult &result, raw_ostream &Stream,
    StringRef Style) {
  using PacketResult = GDBRemoteCommunication::PacketResult;

  switch (result) {
  case PacketResult::Success:
    Stream << "Success";
    break;
  case PacketResult::ErrorSendFailed:
    Stream << "ErrorSendFailed";
    break;
  case PacketResult::ErrorSendAck:
    Stream << "ErrorSendAck";
    break;
  case PacketResult::ErrorReplyFailed:
    Stream << "ErrorReplyFailed";
    break;
  case PacketResult::ErrorReplyTimeout:
    Stream << "ErrorReplyTimeout";
    break;
  case PacketResult::ErrorReplyInvalid:
    Stream << "ErrorReplyInvalid";
    break;
  case PacketResult::ErrorReplyAck:
    Stream << "ErrorReplyAck";
    break;
  case PacketResult::ErrorDisconnected:
    Stream << "ErrorDisconnected";
    break;
  case PacketResult::ErrorNoSequenceLock:
    Stream << "ErrorNoSequenceLock";
    break;
  }
}
