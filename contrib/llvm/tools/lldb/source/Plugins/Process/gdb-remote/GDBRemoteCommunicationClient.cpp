//===-- GDBRemoteCommunicationClient.cpp ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "GDBRemoteCommunicationClient.h"

#include <math.h>
#include <sys/stat.h>

#include <numeric>
#include <sstream>

#include "lldb/Core/ModuleSpec.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/XML.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/UnixSignals.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/JSON.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/StreamString.h"

#include "ProcessGDBRemote.h"
#include "ProcessGDBRemoteLog.h"
#include "lldb/Host/Config.h"
#include "lldb/Utility/StringExtractorGDBRemote.h"

#include "llvm/ADT/StringSwitch.h"

#if defined(__APPLE__)
#define HAVE_LIBCOMPRESSION
#include <compression.h>
#endif

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_gdb_remote;
using namespace std::chrono;

//----------------------------------------------------------------------
// GDBRemoteCommunicationClient constructor
//----------------------------------------------------------------------
GDBRemoteCommunicationClient::GDBRemoteCommunicationClient()
    : GDBRemoteClientBase("gdb-remote.client", "gdb-remote.client.rx_packet"),
      m_supports_not_sending_acks(eLazyBoolCalculate),
      m_supports_thread_suffix(eLazyBoolCalculate),
      m_supports_threads_in_stop_reply(eLazyBoolCalculate),
      m_supports_vCont_all(eLazyBoolCalculate),
      m_supports_vCont_any(eLazyBoolCalculate),
      m_supports_vCont_c(eLazyBoolCalculate),
      m_supports_vCont_C(eLazyBoolCalculate),
      m_supports_vCont_s(eLazyBoolCalculate),
      m_supports_vCont_S(eLazyBoolCalculate),
      m_qHostInfo_is_valid(eLazyBoolCalculate),
      m_curr_pid_is_valid(eLazyBoolCalculate),
      m_qProcessInfo_is_valid(eLazyBoolCalculate),
      m_qGDBServerVersion_is_valid(eLazyBoolCalculate),
      m_supports_alloc_dealloc_memory(eLazyBoolCalculate),
      m_supports_memory_region_info(eLazyBoolCalculate),
      m_supports_watchpoint_support_info(eLazyBoolCalculate),
      m_supports_detach_stay_stopped(eLazyBoolCalculate),
      m_watchpoints_trigger_after_instruction(eLazyBoolCalculate),
      m_attach_or_wait_reply(eLazyBoolCalculate),
      m_prepare_for_reg_writing_reply(eLazyBoolCalculate),
      m_supports_p(eLazyBoolCalculate), m_supports_x(eLazyBoolCalculate),
      m_avoid_g_packets(eLazyBoolCalculate),
      m_supports_QSaveRegisterState(eLazyBoolCalculate),
      m_supports_qXfer_auxv_read(eLazyBoolCalculate),
      m_supports_qXfer_libraries_read(eLazyBoolCalculate),
      m_supports_qXfer_libraries_svr4_read(eLazyBoolCalculate),
      m_supports_qXfer_features_read(eLazyBoolCalculate),
      m_supports_qXfer_memory_map_read(eLazyBoolCalculate),
      m_supports_augmented_libraries_svr4_read(eLazyBoolCalculate),
      m_supports_jThreadExtendedInfo(eLazyBoolCalculate),
      m_supports_jLoadedDynamicLibrariesInfos(eLazyBoolCalculate),
      m_supports_jGetSharedCacheInfo(eLazyBoolCalculate),
      m_supports_QPassSignals(eLazyBoolCalculate),
      m_supports_error_string_reply(eLazyBoolCalculate),
      m_supports_qProcessInfoPID(true), m_supports_qfProcessInfo(true),
      m_supports_qUserName(true), m_supports_qGroupName(true),
      m_supports_qThreadStopInfo(true), m_supports_z0(true),
      m_supports_z1(true), m_supports_z2(true), m_supports_z3(true),
      m_supports_z4(true), m_supports_QEnvironment(true),
      m_supports_QEnvironmentHexEncoded(true), m_supports_qSymbol(true),
      m_qSymbol_requests_done(false), m_supports_qModuleInfo(true),
      m_supports_jThreadsInfo(true), m_supports_jModulesInfo(true),
      m_curr_pid(LLDB_INVALID_PROCESS_ID), m_curr_tid(LLDB_INVALID_THREAD_ID),
      m_curr_tid_run(LLDB_INVALID_THREAD_ID),
      m_num_supported_hardware_watchpoints(0), m_host_arch(), m_process_arch(),
      m_os_build(), m_os_kernel(), m_hostname(), m_gdb_server_name(),
      m_gdb_server_version(UINT32_MAX), m_default_packet_timeout(0),
      m_max_packet_size(0), m_qSupported_response(),
      m_supported_async_json_packets_is_valid(false),
      m_supported_async_json_packets_sp(), m_qXfer_memory_map(),
      m_qXfer_memory_map_loaded(false) {}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
GDBRemoteCommunicationClient::~GDBRemoteCommunicationClient() {
  if (IsConnected())
    Disconnect();
}

bool GDBRemoteCommunicationClient::HandshakeWithServer(Status *error_ptr) {
  ResetDiscoverableSettings(false);

  // Start the read thread after we send the handshake ack since if we fail to
  // send the handshake ack, there is no reason to continue...
  if (SendAck()) {
    // Wait for any responses that might have been queued up in the remote
    // GDB server and flush them all
    StringExtractorGDBRemote response;
    PacketResult packet_result = PacketResult::Success;
    while (packet_result == PacketResult::Success)
      packet_result = ReadPacket(response, milliseconds(10), false);

    // The return value from QueryNoAckModeSupported() is true if the packet
    // was sent and _any_ response (including UNIMPLEMENTED) was received), or
    // false if no response was received. This quickly tells us if we have a
    // live connection to a remote GDB server...
    if (QueryNoAckModeSupported()) {
      return true;
    } else {
      if (error_ptr)
        error_ptr->SetErrorString("failed to get reply to handshake packet");
    }
  } else {
    if (error_ptr)
      error_ptr->SetErrorString("failed to send the handshake ack");
  }
  return false;
}

bool GDBRemoteCommunicationClient::GetEchoSupported() {
  if (m_supports_qEcho == eLazyBoolCalculate) {
    GetRemoteQSupported();
  }
  return m_supports_qEcho == eLazyBoolYes;
}

bool GDBRemoteCommunicationClient::GetQPassSignalsSupported() {
  if (m_supports_QPassSignals == eLazyBoolCalculate) {
    GetRemoteQSupported();
  }
  return m_supports_QPassSignals == eLazyBoolYes;
}

bool GDBRemoteCommunicationClient::GetAugmentedLibrariesSVR4ReadSupported() {
  if (m_supports_augmented_libraries_svr4_read == eLazyBoolCalculate) {
    GetRemoteQSupported();
  }
  return m_supports_augmented_libraries_svr4_read == eLazyBoolYes;
}

bool GDBRemoteCommunicationClient::GetQXferLibrariesSVR4ReadSupported() {
  if (m_supports_qXfer_libraries_svr4_read == eLazyBoolCalculate) {
    GetRemoteQSupported();
  }
  return m_supports_qXfer_libraries_svr4_read == eLazyBoolYes;
}

bool GDBRemoteCommunicationClient::GetQXferLibrariesReadSupported() {
  if (m_supports_qXfer_libraries_read == eLazyBoolCalculate) {
    GetRemoteQSupported();
  }
  return m_supports_qXfer_libraries_read == eLazyBoolYes;
}

bool GDBRemoteCommunicationClient::GetQXferAuxvReadSupported() {
  if (m_supports_qXfer_auxv_read == eLazyBoolCalculate) {
    GetRemoteQSupported();
  }
  return m_supports_qXfer_auxv_read == eLazyBoolYes;
}

bool GDBRemoteCommunicationClient::GetQXferFeaturesReadSupported() {
  if (m_supports_qXfer_features_read == eLazyBoolCalculate) {
    GetRemoteQSupported();
  }
  return m_supports_qXfer_features_read == eLazyBoolYes;
}

bool GDBRemoteCommunicationClient::GetQXferMemoryMapReadSupported() {
  if (m_supports_qXfer_memory_map_read == eLazyBoolCalculate) {
    GetRemoteQSupported();
  }
  return m_supports_qXfer_memory_map_read == eLazyBoolYes;
}

uint64_t GDBRemoteCommunicationClient::GetRemoteMaxPacketSize() {
  if (m_max_packet_size == 0) {
    GetRemoteQSupported();
  }
  return m_max_packet_size;
}

bool GDBRemoteCommunicationClient::QueryNoAckModeSupported() {
  if (m_supports_not_sending_acks == eLazyBoolCalculate) {
    m_send_acks = true;
    m_supports_not_sending_acks = eLazyBoolNo;

    // This is the first real packet that we'll send in a debug session and it
    // may take a little longer than normal to receive a reply.  Wait at least
    // 6 seconds for a reply to this packet.

    ScopedTimeout timeout(*this, std::max(GetPacketTimeout(), seconds(6)));

    StringExtractorGDBRemote response;
    if (SendPacketAndWaitForResponse("QStartNoAckMode", response, false) ==
        PacketResult::Success) {
      if (response.IsOKResponse()) {
        m_send_acks = false;
        m_supports_not_sending_acks = eLazyBoolYes;
      }
      return true;
    }
  }
  return false;
}

void GDBRemoteCommunicationClient::GetListThreadsInStopReplySupported() {
  if (m_supports_threads_in_stop_reply == eLazyBoolCalculate) {
    m_supports_threads_in_stop_reply = eLazyBoolNo;

    StringExtractorGDBRemote response;
    if (SendPacketAndWaitForResponse("QListThreadsInStopReply", response,
                                     false) == PacketResult::Success) {
      if (response.IsOKResponse())
        m_supports_threads_in_stop_reply = eLazyBoolYes;
    }
  }
}

bool GDBRemoteCommunicationClient::GetVAttachOrWaitSupported() {
  if (m_attach_or_wait_reply == eLazyBoolCalculate) {
    m_attach_or_wait_reply = eLazyBoolNo;

    StringExtractorGDBRemote response;
    if (SendPacketAndWaitForResponse("qVAttachOrWaitSupported", response,
                                     false) == PacketResult::Success) {
      if (response.IsOKResponse())
        m_attach_or_wait_reply = eLazyBoolYes;
    }
  }
  return m_attach_or_wait_reply == eLazyBoolYes;
}

bool GDBRemoteCommunicationClient::GetSyncThreadStateSupported() {
  if (m_prepare_for_reg_writing_reply == eLazyBoolCalculate) {
    m_prepare_for_reg_writing_reply = eLazyBoolNo;

    StringExtractorGDBRemote response;
    if (SendPacketAndWaitForResponse("qSyncThreadStateSupported", response,
                                     false) == PacketResult::Success) {
      if (response.IsOKResponse())
        m_prepare_for_reg_writing_reply = eLazyBoolYes;
    }
  }
  return m_prepare_for_reg_writing_reply == eLazyBoolYes;
}

void GDBRemoteCommunicationClient::ResetDiscoverableSettings(bool did_exec) {
  if (!did_exec) {
    // Hard reset everything, this is when we first connect to a GDB server
    m_supports_not_sending_acks = eLazyBoolCalculate;
    m_supports_thread_suffix = eLazyBoolCalculate;
    m_supports_threads_in_stop_reply = eLazyBoolCalculate;
    m_supports_vCont_c = eLazyBoolCalculate;
    m_supports_vCont_C = eLazyBoolCalculate;
    m_supports_vCont_s = eLazyBoolCalculate;
    m_supports_vCont_S = eLazyBoolCalculate;
    m_supports_p = eLazyBoolCalculate;
    m_supports_x = eLazyBoolCalculate;
    m_supports_QSaveRegisterState = eLazyBoolCalculate;
    m_qHostInfo_is_valid = eLazyBoolCalculate;
    m_curr_pid_is_valid = eLazyBoolCalculate;
    m_qGDBServerVersion_is_valid = eLazyBoolCalculate;
    m_supports_alloc_dealloc_memory = eLazyBoolCalculate;
    m_supports_memory_region_info = eLazyBoolCalculate;
    m_prepare_for_reg_writing_reply = eLazyBoolCalculate;
    m_attach_or_wait_reply = eLazyBoolCalculate;
    m_avoid_g_packets = eLazyBoolCalculate;
    m_supports_qXfer_auxv_read = eLazyBoolCalculate;
    m_supports_qXfer_libraries_read = eLazyBoolCalculate;
    m_supports_qXfer_libraries_svr4_read = eLazyBoolCalculate;
    m_supports_qXfer_features_read = eLazyBoolCalculate;
    m_supports_qXfer_memory_map_read = eLazyBoolCalculate;
    m_supports_augmented_libraries_svr4_read = eLazyBoolCalculate;
    m_supports_qProcessInfoPID = true;
    m_supports_qfProcessInfo = true;
    m_supports_qUserName = true;
    m_supports_qGroupName = true;
    m_supports_qThreadStopInfo = true;
    m_supports_z0 = true;
    m_supports_z1 = true;
    m_supports_z2 = true;
    m_supports_z3 = true;
    m_supports_z4 = true;
    m_supports_QEnvironment = true;
    m_supports_QEnvironmentHexEncoded = true;
    m_supports_qSymbol = true;
    m_qSymbol_requests_done = false;
    m_supports_qModuleInfo = true;
    m_host_arch.Clear();
    m_os_version = llvm::VersionTuple();
    m_os_build.clear();
    m_os_kernel.clear();
    m_hostname.clear();
    m_gdb_server_name.clear();
    m_gdb_server_version = UINT32_MAX;
    m_default_packet_timeout = seconds(0);
    m_max_packet_size = 0;
    m_qSupported_response.clear();
    m_supported_async_json_packets_is_valid = false;
    m_supported_async_json_packets_sp.reset();
    m_supports_jModulesInfo = true;
  }

  // These flags should be reset when we first connect to a GDB server and when
  // our inferior process execs
  m_qProcessInfo_is_valid = eLazyBoolCalculate;
  m_process_arch.Clear();
}

void GDBRemoteCommunicationClient::GetRemoteQSupported() {
  // Clear out any capabilities we expect to see in the qSupported response
  m_supports_qXfer_auxv_read = eLazyBoolNo;
  m_supports_qXfer_libraries_read = eLazyBoolNo;
  m_supports_qXfer_libraries_svr4_read = eLazyBoolNo;
  m_supports_augmented_libraries_svr4_read = eLazyBoolNo;
  m_supports_qXfer_features_read = eLazyBoolNo;
  m_supports_qXfer_memory_map_read = eLazyBoolNo;
  m_max_packet_size = UINT64_MAX; // It's supposed to always be there, but if
                                  // not, we assume no limit

  // build the qSupported packet
  std::vector<std::string> features = {"xmlRegisters=i386,arm,mips"};
  StreamString packet;
  packet.PutCString("qSupported");
  for (uint32_t i = 0; i < features.size(); ++i) {
    packet.PutCString(i == 0 ? ":" : ";");
    packet.PutCString(features[i]);
  }

  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse(packet.GetString(), response,
                                   /*send_async=*/false) ==
      PacketResult::Success) {
    const char *response_cstr = response.GetStringRef().c_str();

    // Hang on to the qSupported packet, so that platforms can do custom
    // configuration of the transport before attaching/launching the process.
    m_qSupported_response = response_cstr;

    if (::strstr(response_cstr, "qXfer:auxv:read+"))
      m_supports_qXfer_auxv_read = eLazyBoolYes;
    if (::strstr(response_cstr, "qXfer:libraries-svr4:read+"))
      m_supports_qXfer_libraries_svr4_read = eLazyBoolYes;
    if (::strstr(response_cstr, "augmented-libraries-svr4-read")) {
      m_supports_qXfer_libraries_svr4_read = eLazyBoolYes; // implied
      m_supports_augmented_libraries_svr4_read = eLazyBoolYes;
    }
    if (::strstr(response_cstr, "qXfer:libraries:read+"))
      m_supports_qXfer_libraries_read = eLazyBoolYes;
    if (::strstr(response_cstr, "qXfer:features:read+"))
      m_supports_qXfer_features_read = eLazyBoolYes;
    if (::strstr(response_cstr, "qXfer:memory-map:read+"))
      m_supports_qXfer_memory_map_read = eLazyBoolYes;

    // Look for a list of compressions in the features list e.g.
    // qXfer:features:read+;PacketSize=20000;qEcho+;SupportedCompressions=zlib-
    // deflate,lzma
    const char *features_list = ::strstr(response_cstr, "qXfer:features:");
    if (features_list) {
      const char *compressions =
          ::strstr(features_list, "SupportedCompressions=");
      if (compressions) {
        std::vector<std::string> supported_compressions;
        compressions += sizeof("SupportedCompressions=") - 1;
        const char *end_of_compressions = strchr(compressions, ';');
        if (end_of_compressions == NULL) {
          end_of_compressions = strchr(compressions, '\0');
        }
        const char *current_compression = compressions;
        while (current_compression < end_of_compressions) {
          const char *next_compression_name = strchr(current_compression, ',');
          const char *end_of_this_word = next_compression_name;
          if (next_compression_name == NULL ||
              end_of_compressions < next_compression_name) {
            end_of_this_word = end_of_compressions;
          }

          if (end_of_this_word) {
            if (end_of_this_word == current_compression) {
              current_compression++;
            } else {
              std::string this_compression(
                  current_compression, end_of_this_word - current_compression);
              supported_compressions.push_back(this_compression);
              current_compression = end_of_this_word + 1;
            }
          } else {
            supported_compressions.push_back(current_compression);
            current_compression = end_of_compressions;
          }
        }

        if (supported_compressions.size() > 0) {
          MaybeEnableCompression(supported_compressions);
        }
      }
    }

    if (::strstr(response_cstr, "qEcho"))
      m_supports_qEcho = eLazyBoolYes;
    else
      m_supports_qEcho = eLazyBoolNo;

    if (::strstr(response_cstr, "QPassSignals+"))
      m_supports_QPassSignals = eLazyBoolYes;
    else
      m_supports_QPassSignals = eLazyBoolNo;

    const char *packet_size_str = ::strstr(response_cstr, "PacketSize=");
    if (packet_size_str) {
      StringExtractorGDBRemote packet_response(packet_size_str +
                                               strlen("PacketSize="));
      m_max_packet_size =
          packet_response.GetHexMaxU64(/*little_endian=*/false, UINT64_MAX);
      if (m_max_packet_size == 0) {
        m_max_packet_size = UINT64_MAX; // Must have been a garbled response
        Log *log(
            ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
        if (log)
          log->Printf("Garbled PacketSize spec in qSupported response");
      }
    }
  }
}

bool GDBRemoteCommunicationClient::GetThreadSuffixSupported() {
  if (m_supports_thread_suffix == eLazyBoolCalculate) {
    StringExtractorGDBRemote response;
    m_supports_thread_suffix = eLazyBoolNo;
    if (SendPacketAndWaitForResponse("QThreadSuffixSupported", response,
                                     false) == PacketResult::Success) {
      if (response.IsOKResponse())
        m_supports_thread_suffix = eLazyBoolYes;
    }
  }
  return m_supports_thread_suffix;
}
bool GDBRemoteCommunicationClient::GetVContSupported(char flavor) {
  if (m_supports_vCont_c == eLazyBoolCalculate) {
    StringExtractorGDBRemote response;
    m_supports_vCont_any = eLazyBoolNo;
    m_supports_vCont_all = eLazyBoolNo;
    m_supports_vCont_c = eLazyBoolNo;
    m_supports_vCont_C = eLazyBoolNo;
    m_supports_vCont_s = eLazyBoolNo;
    m_supports_vCont_S = eLazyBoolNo;
    if (SendPacketAndWaitForResponse("vCont?", response, false) ==
        PacketResult::Success) {
      const char *response_cstr = response.GetStringRef().c_str();
      if (::strstr(response_cstr, ";c"))
        m_supports_vCont_c = eLazyBoolYes;

      if (::strstr(response_cstr, ";C"))
        m_supports_vCont_C = eLazyBoolYes;

      if (::strstr(response_cstr, ";s"))
        m_supports_vCont_s = eLazyBoolYes;

      if (::strstr(response_cstr, ";S"))
        m_supports_vCont_S = eLazyBoolYes;

      if (m_supports_vCont_c == eLazyBoolYes &&
          m_supports_vCont_C == eLazyBoolYes &&
          m_supports_vCont_s == eLazyBoolYes &&
          m_supports_vCont_S == eLazyBoolYes) {
        m_supports_vCont_all = eLazyBoolYes;
      }

      if (m_supports_vCont_c == eLazyBoolYes ||
          m_supports_vCont_C == eLazyBoolYes ||
          m_supports_vCont_s == eLazyBoolYes ||
          m_supports_vCont_S == eLazyBoolYes) {
        m_supports_vCont_any = eLazyBoolYes;
      }
    }
  }

  switch (flavor) {
  case 'a':
    return m_supports_vCont_any;
  case 'A':
    return m_supports_vCont_all;
  case 'c':
    return m_supports_vCont_c;
  case 'C':
    return m_supports_vCont_C;
  case 's':
    return m_supports_vCont_s;
  case 'S':
    return m_supports_vCont_S;
  default:
    break;
  }
  return false;
}

GDBRemoteCommunication::PacketResult
GDBRemoteCommunicationClient::SendThreadSpecificPacketAndWaitForResponse(
    lldb::tid_t tid, StreamString &&payload, StringExtractorGDBRemote &response,
    bool send_async) {
  Lock lock(*this, send_async);
  if (!lock) {
    if (Log *log = ProcessGDBRemoteLog::GetLogIfAnyCategoryIsSet(
            GDBR_LOG_PROCESS | GDBR_LOG_PACKETS))
      log->Printf("GDBRemoteCommunicationClient::%s: Didn't get sequence mutex "
                  "for %s packet.",
                  __FUNCTION__, payload.GetData());
    return PacketResult::ErrorNoSequenceLock;
  }

  if (GetThreadSuffixSupported())
    payload.Printf(";thread:%4.4" PRIx64 ";", tid);
  else {
    if (!SetCurrentThread(tid))
      return PacketResult::ErrorSendFailed;
  }

  return SendPacketAndWaitForResponseNoLock(payload.GetString(), response);
}

// Check if the target supports 'p' packet. It sends out a 'p' packet and
// checks the response. A normal packet will tell us that support is available.
//
// Takes a valid thread ID because p needs to apply to a thread.
bool GDBRemoteCommunicationClient::GetpPacketSupported(lldb::tid_t tid) {
  if (m_supports_p == eLazyBoolCalculate) {
    m_supports_p = eLazyBoolNo;
    StreamString payload;
    payload.PutCString("p0");
    StringExtractorGDBRemote response;
    if (SendThreadSpecificPacketAndWaitForResponse(tid, std::move(payload),
                                                   response, false) ==
            PacketResult::Success &&
        response.IsNormalResponse()) {
      m_supports_p = eLazyBoolYes;
    }
  }
  return m_supports_p;
}

StructuredData::ObjectSP GDBRemoteCommunicationClient::GetThreadsInfo() {
  // Get information on all threads at one using the "jThreadsInfo" packet
  StructuredData::ObjectSP object_sp;

  if (m_supports_jThreadsInfo) {
    StringExtractorGDBRemote response;
    response.SetResponseValidatorToJSON();
    if (SendPacketAndWaitForResponse("jThreadsInfo", response, false) ==
        PacketResult::Success) {
      if (response.IsUnsupportedResponse()) {
        m_supports_jThreadsInfo = false;
      } else if (!response.Empty()) {
        object_sp = StructuredData::ParseJSON(response.GetStringRef());
      }
    }
  }
  return object_sp;
}

bool GDBRemoteCommunicationClient::GetThreadExtendedInfoSupported() {
  if (m_supports_jThreadExtendedInfo == eLazyBoolCalculate) {
    StringExtractorGDBRemote response;
    m_supports_jThreadExtendedInfo = eLazyBoolNo;
    if (SendPacketAndWaitForResponse("jThreadExtendedInfo:", response, false) ==
        PacketResult::Success) {
      if (response.IsOKResponse()) {
        m_supports_jThreadExtendedInfo = eLazyBoolYes;
      }
    }
  }
  return m_supports_jThreadExtendedInfo;
}

void GDBRemoteCommunicationClient::EnableErrorStringInPacket() {
  if (m_supports_error_string_reply == eLazyBoolCalculate) {
    StringExtractorGDBRemote response;
    // We try to enable error strings in remote packets but if we fail, we just
    // work in the older way.
    m_supports_error_string_reply = eLazyBoolNo;
    if (SendPacketAndWaitForResponse("QEnableErrorStrings", response, false) ==
        PacketResult::Success) {
      if (response.IsOKResponse()) {
        m_supports_error_string_reply = eLazyBoolYes;
      }
    }
  }
}

bool GDBRemoteCommunicationClient::GetLoadedDynamicLibrariesInfosSupported() {
  if (m_supports_jLoadedDynamicLibrariesInfos == eLazyBoolCalculate) {
    StringExtractorGDBRemote response;
    m_supports_jLoadedDynamicLibrariesInfos = eLazyBoolNo;
    if (SendPacketAndWaitForResponse("jGetLoadedDynamicLibrariesInfos:",
                                     response,
                                     false) == PacketResult::Success) {
      if (response.IsOKResponse()) {
        m_supports_jLoadedDynamicLibrariesInfos = eLazyBoolYes;
      }
    }
  }
  return m_supports_jLoadedDynamicLibrariesInfos;
}

bool GDBRemoteCommunicationClient::GetSharedCacheInfoSupported() {
  if (m_supports_jGetSharedCacheInfo == eLazyBoolCalculate) {
    StringExtractorGDBRemote response;
    m_supports_jGetSharedCacheInfo = eLazyBoolNo;
    if (SendPacketAndWaitForResponse("jGetSharedCacheInfo:", response, false) ==
        PacketResult::Success) {
      if (response.IsOKResponse()) {
        m_supports_jGetSharedCacheInfo = eLazyBoolYes;
      }
    }
  }
  return m_supports_jGetSharedCacheInfo;
}

bool GDBRemoteCommunicationClient::GetxPacketSupported() {
  if (m_supports_x == eLazyBoolCalculate) {
    StringExtractorGDBRemote response;
    m_supports_x = eLazyBoolNo;
    char packet[256];
    snprintf(packet, sizeof(packet), "x0,0");
    if (SendPacketAndWaitForResponse(packet, response, false) ==
        PacketResult::Success) {
      if (response.IsOKResponse())
        m_supports_x = eLazyBoolYes;
    }
  }
  return m_supports_x;
}

GDBRemoteCommunicationClient::PacketResult
GDBRemoteCommunicationClient::SendPacketsAndConcatenateResponses(
    const char *payload_prefix, std::string &response_string) {
  Lock lock(*this, false);
  if (!lock) {
    Log *log(ProcessGDBRemoteLog::GetLogIfAnyCategoryIsSet(GDBR_LOG_PROCESS |
                                                           GDBR_LOG_PACKETS));
    if (log)
      log->Printf("error: failed to get packet sequence mutex, not sending "
                  "packets with prefix '%s'",
                  payload_prefix);
    return PacketResult::ErrorNoSequenceLock;
  }

  response_string = "";
  std::string payload_prefix_str(payload_prefix);
  unsigned int response_size = 0x1000;
  if (response_size > GetRemoteMaxPacketSize()) { // May send qSupported packet
    response_size = GetRemoteMaxPacketSize();
  }

  for (unsigned int offset = 0; true; offset += response_size) {
    StringExtractorGDBRemote this_response;
    // Construct payload
    char sizeDescriptor[128];
    snprintf(sizeDescriptor, sizeof(sizeDescriptor), "%x,%x", offset,
             response_size);
    PacketResult result = SendPacketAndWaitForResponseNoLock(
        payload_prefix_str + sizeDescriptor, this_response);
    if (result != PacketResult::Success)
      return result;

    const std::string &this_string = this_response.GetStringRef();

    // Check for m or l as first character; l seems to mean this is the last
    // chunk
    char first_char = *this_string.c_str();
    if (first_char != 'm' && first_char != 'l') {
      return PacketResult::ErrorReplyInvalid;
    }
    // Concatenate the result so far (skipping 'm' or 'l')
    response_string.append(this_string, 1, std::string::npos);
    if (first_char == 'l')
      // We're done
      return PacketResult::Success;
  }
}

lldb::pid_t GDBRemoteCommunicationClient::GetCurrentProcessID(bool allow_lazy) {
  if (allow_lazy && m_curr_pid_is_valid == eLazyBoolYes)
    return m_curr_pid;

  // First try to retrieve the pid via the qProcessInfo request.
  GetCurrentProcessInfo(allow_lazy);
  if (m_curr_pid_is_valid == eLazyBoolYes) {
    // We really got it.
    return m_curr_pid;
  } else {
    // If we don't get a response for qProcessInfo, check if $qC gives us a
    // result. $qC only returns a real process id on older debugserver and
    // lldb-platform stubs. The gdb remote protocol documents $qC as returning
    // the thread id, which newer debugserver and lldb-gdbserver stubs return
    // correctly.
    StringExtractorGDBRemote response;
    if (SendPacketAndWaitForResponse("qC", response, false) ==
        PacketResult::Success) {
      if (response.GetChar() == 'Q') {
        if (response.GetChar() == 'C') {
          m_curr_pid = response.GetHexMaxU32(false, LLDB_INVALID_PROCESS_ID);
          if (m_curr_pid != LLDB_INVALID_PROCESS_ID) {
            m_curr_pid_is_valid = eLazyBoolYes;
            return m_curr_pid;
          }
        }
      }
    }

    // If we don't get a response for $qC, check if $qfThreadID gives us a
    // result.
    if (m_curr_pid == LLDB_INVALID_PROCESS_ID) {
      std::vector<lldb::tid_t> thread_ids;
      bool sequence_mutex_unavailable;
      size_t size;
      size = GetCurrentThreadIDs(thread_ids, sequence_mutex_unavailable);
      if (size && !sequence_mutex_unavailable) {
        m_curr_pid = thread_ids.front();
        m_curr_pid_is_valid = eLazyBoolYes;
        return m_curr_pid;
      }
    }
  }

  return LLDB_INVALID_PROCESS_ID;
}

bool GDBRemoteCommunicationClient::GetLaunchSuccess(std::string &error_str) {
  error_str.clear();
  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse("qLaunchSuccess", response, false) ==
      PacketResult::Success) {
    if (response.IsOKResponse())
      return true;
    if (response.GetChar() == 'E') {
      // A string the describes what failed when launching...
      error_str = response.GetStringRef().substr(1);
    } else {
      error_str.assign("unknown error occurred launching process");
    }
  } else {
    error_str.assign("timed out waiting for app to launch");
  }
  return false;
}

int GDBRemoteCommunicationClient::SendArgumentsPacket(
    const ProcessLaunchInfo &launch_info) {
  // Since we don't get the send argv0 separate from the executable path, we
  // need to make sure to use the actual executable path found in the
  // launch_info...
  std::vector<const char *> argv;
  FileSpec exe_file = launch_info.GetExecutableFile();
  std::string exe_path;
  const char *arg = NULL;
  const Args &launch_args = launch_info.GetArguments();
  if (exe_file)
    exe_path = exe_file.GetPath(false);
  else {
    arg = launch_args.GetArgumentAtIndex(0);
    if (arg)
      exe_path = arg;
  }
  if (!exe_path.empty()) {
    argv.push_back(exe_path.c_str());
    for (uint32_t i = 1; (arg = launch_args.GetArgumentAtIndex(i)) != NULL;
         ++i) {
      if (arg)
        argv.push_back(arg);
    }
  }
  if (!argv.empty()) {
    StreamString packet;
    packet.PutChar('A');
    for (size_t i = 0, n = argv.size(); i < n; ++i) {
      arg = argv[i];
      const int arg_len = strlen(arg);
      if (i > 0)
        packet.PutChar(',');
      packet.Printf("%i,%i,", arg_len * 2, (int)i);
      packet.PutBytesAsRawHex8(arg, arg_len);
    }

    StringExtractorGDBRemote response;
    if (SendPacketAndWaitForResponse(packet.GetString(), response, false) ==
        PacketResult::Success) {
      if (response.IsOKResponse())
        return 0;
      uint8_t error = response.GetError();
      if (error)
        return error;
    }
  }
  return -1;
}

int GDBRemoteCommunicationClient::SendEnvironment(const Environment &env) {
  for (const auto &KV : env) {
    int r = SendEnvironmentPacket(Environment::compose(KV).c_str());
    if (r != 0)
      return r;
  }
  return 0;
}

int GDBRemoteCommunicationClient::SendEnvironmentPacket(
    char const *name_equal_value) {
  if (name_equal_value && name_equal_value[0]) {
    StreamString packet;
    bool send_hex_encoding = false;
    for (const char *p = name_equal_value; *p != '\0' && !send_hex_encoding;
         ++p) {
      if (isprint(*p)) {
        switch (*p) {
        case '$':
        case '#':
        case '*':
        case '}':
          send_hex_encoding = true;
          break;
        default:
          break;
        }
      } else {
        // We have non printable characters, lets hex encode this...
        send_hex_encoding = true;
      }
    }

    StringExtractorGDBRemote response;
    if (send_hex_encoding) {
      if (m_supports_QEnvironmentHexEncoded) {
        packet.PutCString("QEnvironmentHexEncoded:");
        packet.PutBytesAsRawHex8(name_equal_value, strlen(name_equal_value));
        if (SendPacketAndWaitForResponse(packet.GetString(), response, false) ==
            PacketResult::Success) {
          if (response.IsOKResponse())
            return 0;
          uint8_t error = response.GetError();
          if (error)
            return error;
          if (response.IsUnsupportedResponse())
            m_supports_QEnvironmentHexEncoded = false;
        }
      }

    } else if (m_supports_QEnvironment) {
      packet.Printf("QEnvironment:%s", name_equal_value);
      if (SendPacketAndWaitForResponse(packet.GetString(), response, false) ==
          PacketResult::Success) {
        if (response.IsOKResponse())
          return 0;
        uint8_t error = response.GetError();
        if (error)
          return error;
        if (response.IsUnsupportedResponse())
          m_supports_QEnvironment = false;
      }
    }
  }
  return -1;
}

int GDBRemoteCommunicationClient::SendLaunchArchPacket(char const *arch) {
  if (arch && arch[0]) {
    StreamString packet;
    packet.Printf("QLaunchArch:%s", arch);
    StringExtractorGDBRemote response;
    if (SendPacketAndWaitForResponse(packet.GetString(), response, false) ==
        PacketResult::Success) {
      if (response.IsOKResponse())
        return 0;
      uint8_t error = response.GetError();
      if (error)
        return error;
    }
  }
  return -1;
}

int GDBRemoteCommunicationClient::SendLaunchEventDataPacket(
    char const *data, bool *was_supported) {
  if (data && *data != '\0') {
    StreamString packet;
    packet.Printf("QSetProcessEvent:%s", data);
    StringExtractorGDBRemote response;
    if (SendPacketAndWaitForResponse(packet.GetString(), response, false) ==
        PacketResult::Success) {
      if (response.IsOKResponse()) {
        if (was_supported)
          *was_supported = true;
        return 0;
      } else if (response.IsUnsupportedResponse()) {
        if (was_supported)
          *was_supported = false;
        return -1;
      } else {
        uint8_t error = response.GetError();
        if (was_supported)
          *was_supported = true;
        if (error)
          return error;
      }
    }
  }
  return -1;
}

llvm::VersionTuple GDBRemoteCommunicationClient::GetOSVersion() {
  GetHostInfo();
  return m_os_version;
}

bool GDBRemoteCommunicationClient::GetOSBuildString(std::string &s) {
  if (GetHostInfo()) {
    if (!m_os_build.empty()) {
      s = m_os_build;
      return true;
    }
  }
  s.clear();
  return false;
}

bool GDBRemoteCommunicationClient::GetOSKernelDescription(std::string &s) {
  if (GetHostInfo()) {
    if (!m_os_kernel.empty()) {
      s = m_os_kernel;
      return true;
    }
  }
  s.clear();
  return false;
}

bool GDBRemoteCommunicationClient::GetHostname(std::string &s) {
  if (GetHostInfo()) {
    if (!m_hostname.empty()) {
      s = m_hostname;
      return true;
    }
  }
  s.clear();
  return false;
}

ArchSpec GDBRemoteCommunicationClient::GetSystemArchitecture() {
  if (GetHostInfo())
    return m_host_arch;
  return ArchSpec();
}

const lldb_private::ArchSpec &
GDBRemoteCommunicationClient::GetProcessArchitecture() {
  if (m_qProcessInfo_is_valid == eLazyBoolCalculate)
    GetCurrentProcessInfo();
  return m_process_arch;
}

bool GDBRemoteCommunicationClient::GetGDBServerVersion() {
  if (m_qGDBServerVersion_is_valid == eLazyBoolCalculate) {
    m_gdb_server_name.clear();
    m_gdb_server_version = 0;
    m_qGDBServerVersion_is_valid = eLazyBoolNo;

    StringExtractorGDBRemote response;
    if (SendPacketAndWaitForResponse("qGDBServerVersion", response, false) ==
        PacketResult::Success) {
      if (response.IsNormalResponse()) {
        llvm::StringRef name, value;
        bool success = false;
        while (response.GetNameColonValue(name, value)) {
          if (name.equals("name")) {
            success = true;
            m_gdb_server_name = value;
          } else if (name.equals("version")) {
            llvm::StringRef major, minor;
            std::tie(major, minor) = value.split('.');
            if (!major.getAsInteger(0, m_gdb_server_version))
              success = true;
          }
        }
        if (success)
          m_qGDBServerVersion_is_valid = eLazyBoolYes;
      }
    }
  }
  return m_qGDBServerVersion_is_valid == eLazyBoolYes;
}

void GDBRemoteCommunicationClient::MaybeEnableCompression(
    std::vector<std::string> supported_compressions) {
  CompressionType avail_type = CompressionType::None;
  std::string avail_name;

#if defined(HAVE_LIBCOMPRESSION)
  if (avail_type == CompressionType::None) {
    for (auto compression : supported_compressions) {
      if (compression == "lzfse") {
        avail_type = CompressionType::LZFSE;
        avail_name = compression;
        break;
      }
    }
  }
#endif

#if defined(HAVE_LIBCOMPRESSION)
  if (avail_type == CompressionType::None) {
    for (auto compression : supported_compressions) {
      if (compression == "zlib-deflate") {
        avail_type = CompressionType::ZlibDeflate;
        avail_name = compression;
        break;
      }
    }
  }
#endif

#if defined(HAVE_LIBZ)
  if (avail_type == CompressionType::None) {
    for (auto compression : supported_compressions) {
      if (compression == "zlib-deflate") {
        avail_type = CompressionType::ZlibDeflate;
        avail_name = compression;
        break;
      }
    }
  }
#endif

#if defined(HAVE_LIBCOMPRESSION)
  if (avail_type == CompressionType::None) {
    for (auto compression : supported_compressions) {
      if (compression == "lz4") {
        avail_type = CompressionType::LZ4;
        avail_name = compression;
        break;
      }
    }
  }
#endif

#if defined(HAVE_LIBCOMPRESSION)
  if (avail_type == CompressionType::None) {
    for (auto compression : supported_compressions) {
      if (compression == "lzma") {
        avail_type = CompressionType::LZMA;
        avail_name = compression;
        break;
      }
    }
  }
#endif

  if (avail_type != CompressionType::None) {
    StringExtractorGDBRemote response;
    std::string packet = "QEnableCompression:type:" + avail_name + ";";
    if (SendPacketAndWaitForResponse(packet, response, false) !=
        PacketResult::Success)
      return;

    if (response.IsOKResponse()) {
      m_compression_type = avail_type;
    }
  }
}

const char *GDBRemoteCommunicationClient::GetGDBServerProgramName() {
  if (GetGDBServerVersion()) {
    if (!m_gdb_server_name.empty())
      return m_gdb_server_name.c_str();
  }
  return NULL;
}

uint32_t GDBRemoteCommunicationClient::GetGDBServerProgramVersion() {
  if (GetGDBServerVersion())
    return m_gdb_server_version;
  return 0;
}

bool GDBRemoteCommunicationClient::GetDefaultThreadId(lldb::tid_t &tid) {
  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse("qC", response, false) !=
      PacketResult::Success)
    return false;

  if (!response.IsNormalResponse())
    return false;

  if (response.GetChar() == 'Q' && response.GetChar() == 'C')
    tid = response.GetHexMaxU32(true, -1);

  return true;
}

bool GDBRemoteCommunicationClient::GetHostInfo(bool force) {
  Log *log(ProcessGDBRemoteLog::GetLogIfAnyCategoryIsSet(GDBR_LOG_PROCESS));

  if (force || m_qHostInfo_is_valid == eLazyBoolCalculate) {
    // host info computation can require DNS traffic and shelling out to external processes.
    // Increase the timeout to account for that.
    ScopedTimeout timeout(*this, seconds(10));
    m_qHostInfo_is_valid = eLazyBoolNo;
    StringExtractorGDBRemote response;
    if (SendPacketAndWaitForResponse("qHostInfo", response, false) ==
        PacketResult::Success) {
      if (response.IsNormalResponse()) {
        llvm::StringRef name;
        llvm::StringRef value;
        uint32_t cpu = LLDB_INVALID_CPUTYPE;
        uint32_t sub = 0;
        std::string arch_name;
        std::string os_name;
        std::string vendor_name;
        std::string triple;
        std::string distribution_id;
        uint32_t pointer_byte_size = 0;
        ByteOrder byte_order = eByteOrderInvalid;
        uint32_t num_keys_decoded = 0;
        while (response.GetNameColonValue(name, value)) {
          if (name.equals("cputype")) {
            // exception type in big endian hex
            if (!value.getAsInteger(0, cpu))
              ++num_keys_decoded;
          } else if (name.equals("cpusubtype")) {
            // exception count in big endian hex
            if (!value.getAsInteger(0, sub))
              ++num_keys_decoded;
          } else if (name.equals("arch")) {
            arch_name = value;
            ++num_keys_decoded;
          } else if (name.equals("triple")) {
            StringExtractor extractor(value);
            extractor.GetHexByteString(triple);
            ++num_keys_decoded;
          } else if (name.equals("distribution_id")) {
            StringExtractor extractor(value);
            extractor.GetHexByteString(distribution_id);
            ++num_keys_decoded;
          } else if (name.equals("os_build")) {
            StringExtractor extractor(value);
            extractor.GetHexByteString(m_os_build);
            ++num_keys_decoded;
          } else if (name.equals("hostname")) {
            StringExtractor extractor(value);
            extractor.GetHexByteString(m_hostname);
            ++num_keys_decoded;
          } else if (name.equals("os_kernel")) {
            StringExtractor extractor(value);
            extractor.GetHexByteString(m_os_kernel);
            ++num_keys_decoded;
          } else if (name.equals("ostype")) {
            os_name = value;
            ++num_keys_decoded;
          } else if (name.equals("vendor")) {
            vendor_name = value;
            ++num_keys_decoded;
          } else if (name.equals("endian")) {
            byte_order = llvm::StringSwitch<lldb::ByteOrder>(value)
                             .Case("little", eByteOrderLittle)
                             .Case("big", eByteOrderBig)
                             .Case("pdp", eByteOrderPDP)
                             .Default(eByteOrderInvalid);
            if (byte_order != eByteOrderInvalid)
              ++num_keys_decoded;
          } else if (name.equals("ptrsize")) {
            if (!value.getAsInteger(0, pointer_byte_size))
              ++num_keys_decoded;
          } else if (name.equals("os_version") ||
                     name.equals(
                         "version")) // Older debugserver binaries used the
                                     // "version" key instead of
                                     // "os_version"...
          {
            if (!m_os_version.tryParse(value))
              ++num_keys_decoded;
          } else if (name.equals("watchpoint_exceptions_received")) {
            m_watchpoints_trigger_after_instruction =
                llvm::StringSwitch<LazyBool>(value)
                    .Case("before", eLazyBoolNo)
                    .Case("after", eLazyBoolYes)
                    .Default(eLazyBoolCalculate);
            if (m_watchpoints_trigger_after_instruction != eLazyBoolCalculate)
              ++num_keys_decoded;
          } else if (name.equals("default_packet_timeout")) {
            uint32_t timeout_seconds;
            if (!value.getAsInteger(0, timeout_seconds)) {
              m_default_packet_timeout = seconds(timeout_seconds);
              SetPacketTimeout(m_default_packet_timeout);
              ++num_keys_decoded;
            }
          }
        }

        if (num_keys_decoded > 0)
          m_qHostInfo_is_valid = eLazyBoolYes;

        if (triple.empty()) {
          if (arch_name.empty()) {
            if (cpu != LLDB_INVALID_CPUTYPE) {
              m_host_arch.SetArchitecture(eArchTypeMachO, cpu, sub);
              if (pointer_byte_size) {
                assert(pointer_byte_size == m_host_arch.GetAddressByteSize());
              }
              if (byte_order != eByteOrderInvalid) {
                assert(byte_order == m_host_arch.GetByteOrder());
              }

              if (!vendor_name.empty())
                m_host_arch.GetTriple().setVendorName(
                    llvm::StringRef(vendor_name));
              if (!os_name.empty())
                m_host_arch.GetTriple().setOSName(llvm::StringRef(os_name));
            }
          } else {
            std::string triple;
            triple += arch_name;
            if (!vendor_name.empty() || !os_name.empty()) {
              triple += '-';
              if (vendor_name.empty())
                triple += "unknown";
              else
                triple += vendor_name;
              triple += '-';
              if (os_name.empty())
                triple += "unknown";
              else
                triple += os_name;
            }
            m_host_arch.SetTriple(triple.c_str());

            llvm::Triple &host_triple = m_host_arch.GetTriple();
            if (host_triple.getVendor() == llvm::Triple::Apple &&
                host_triple.getOS() == llvm::Triple::Darwin) {
              switch (m_host_arch.GetMachine()) {
              case llvm::Triple::aarch64:
              case llvm::Triple::arm:
              case llvm::Triple::thumb:
                host_triple.setOS(llvm::Triple::IOS);
                break;
              default:
                host_triple.setOS(llvm::Triple::MacOSX);
                break;
              }
            }
            if (pointer_byte_size) {
              assert(pointer_byte_size == m_host_arch.GetAddressByteSize());
            }
            if (byte_order != eByteOrderInvalid) {
              assert(byte_order == m_host_arch.GetByteOrder());
            }
          }
        } else {
          m_host_arch.SetTriple(triple.c_str());
          if (pointer_byte_size) {
            assert(pointer_byte_size == m_host_arch.GetAddressByteSize());
          }
          if (byte_order != eByteOrderInvalid) {
            assert(byte_order == m_host_arch.GetByteOrder());
          }

          if (log)
            log->Printf("GDBRemoteCommunicationClient::%s parsed host "
                        "architecture as %s, triple as %s from triple text %s",
                        __FUNCTION__, m_host_arch.GetArchitectureName()
                                          ? m_host_arch.GetArchitectureName()
                                          : "<null-arch-name>",
                        m_host_arch.GetTriple().getTriple().c_str(),
                        triple.c_str());
        }
        if (!distribution_id.empty())
          m_host_arch.SetDistributionId(distribution_id.c_str());
      }
    }
  }
  return m_qHostInfo_is_valid == eLazyBoolYes;
}

int GDBRemoteCommunicationClient::SendAttach(
    lldb::pid_t pid, StringExtractorGDBRemote &response) {
  if (pid != LLDB_INVALID_PROCESS_ID) {
    char packet[64];
    const int packet_len =
        ::snprintf(packet, sizeof(packet), "vAttach;%" PRIx64, pid);
    UNUSED_IF_ASSERT_DISABLED(packet_len);
    assert(packet_len < (int)sizeof(packet));
    if (SendPacketAndWaitForResponse(packet, response, false) ==
        PacketResult::Success) {
      if (response.IsErrorResponse())
        return response.GetError();
      return 0;
    }
  }
  return -1;
}

int GDBRemoteCommunicationClient::SendStdinNotification(const char *data,
                                                        size_t data_len) {
  StreamString packet;
  packet.PutCString("I");
  packet.PutBytesAsRawHex8(data, data_len);
  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse(packet.GetString(), response, false) ==
      PacketResult::Success) {
    return 0;
  }
  return response.GetError();
}

const lldb_private::ArchSpec &
GDBRemoteCommunicationClient::GetHostArchitecture() {
  if (m_qHostInfo_is_valid == eLazyBoolCalculate)
    GetHostInfo();
  return m_host_arch;
}

seconds GDBRemoteCommunicationClient::GetHostDefaultPacketTimeout() {
  if (m_qHostInfo_is_valid == eLazyBoolCalculate)
    GetHostInfo();
  return m_default_packet_timeout;
}

addr_t GDBRemoteCommunicationClient::AllocateMemory(size_t size,
                                                    uint32_t permissions) {
  if (m_supports_alloc_dealloc_memory != eLazyBoolNo) {
    m_supports_alloc_dealloc_memory = eLazyBoolYes;
    char packet[64];
    const int packet_len = ::snprintf(
        packet, sizeof(packet), "_M%" PRIx64 ",%s%s%s", (uint64_t)size,
        permissions & lldb::ePermissionsReadable ? "r" : "",
        permissions & lldb::ePermissionsWritable ? "w" : "",
        permissions & lldb::ePermissionsExecutable ? "x" : "");
    assert(packet_len < (int)sizeof(packet));
    UNUSED_IF_ASSERT_DISABLED(packet_len);
    StringExtractorGDBRemote response;
    if (SendPacketAndWaitForResponse(packet, response, false) ==
        PacketResult::Success) {
      if (response.IsUnsupportedResponse())
        m_supports_alloc_dealloc_memory = eLazyBoolNo;
      else if (!response.IsErrorResponse())
        return response.GetHexMaxU64(false, LLDB_INVALID_ADDRESS);
    } else {
      m_supports_alloc_dealloc_memory = eLazyBoolNo;
    }
  }
  return LLDB_INVALID_ADDRESS;
}

bool GDBRemoteCommunicationClient::DeallocateMemory(addr_t addr) {
  if (m_supports_alloc_dealloc_memory != eLazyBoolNo) {
    m_supports_alloc_dealloc_memory = eLazyBoolYes;
    char packet[64];
    const int packet_len =
        ::snprintf(packet, sizeof(packet), "_m%" PRIx64, (uint64_t)addr);
    assert(packet_len < (int)sizeof(packet));
    UNUSED_IF_ASSERT_DISABLED(packet_len);
    StringExtractorGDBRemote response;
    if (SendPacketAndWaitForResponse(packet, response, false) ==
        PacketResult::Success) {
      if (response.IsUnsupportedResponse())
        m_supports_alloc_dealloc_memory = eLazyBoolNo;
      else if (response.IsOKResponse())
        return true;
    } else {
      m_supports_alloc_dealloc_memory = eLazyBoolNo;
    }
  }
  return false;
}

Status GDBRemoteCommunicationClient::Detach(bool keep_stopped) {
  Status error;

  if (keep_stopped) {
    if (m_supports_detach_stay_stopped == eLazyBoolCalculate) {
      char packet[64];
      const int packet_len =
          ::snprintf(packet, sizeof(packet), "qSupportsDetachAndStayStopped:");
      assert(packet_len < (int)sizeof(packet));
      UNUSED_IF_ASSERT_DISABLED(packet_len);
      StringExtractorGDBRemote response;
      if (SendPacketAndWaitForResponse(packet, response, false) ==
              PacketResult::Success &&
          response.IsOKResponse()) {
        m_supports_detach_stay_stopped = eLazyBoolYes;
      } else {
        m_supports_detach_stay_stopped = eLazyBoolNo;
      }
    }

    if (m_supports_detach_stay_stopped == eLazyBoolNo) {
      error.SetErrorString("Stays stopped not supported by this target.");
      return error;
    } else {
      StringExtractorGDBRemote response;
      PacketResult packet_result =
          SendPacketAndWaitForResponse("D1", response, false);
      if (packet_result != PacketResult::Success)
        error.SetErrorString("Sending extended disconnect packet failed.");
    }
  } else {
    StringExtractorGDBRemote response;
    PacketResult packet_result =
        SendPacketAndWaitForResponse("D", response, false);
    if (packet_result != PacketResult::Success)
      error.SetErrorString("Sending disconnect packet failed.");
  }
  return error;
}

Status GDBRemoteCommunicationClient::GetMemoryRegionInfo(
    lldb::addr_t addr, lldb_private::MemoryRegionInfo &region_info) {
  Status error;
  region_info.Clear();

  if (m_supports_memory_region_info != eLazyBoolNo) {
    m_supports_memory_region_info = eLazyBoolYes;
    char packet[64];
    const int packet_len = ::snprintf(
        packet, sizeof(packet), "qMemoryRegionInfo:%" PRIx64, (uint64_t)addr);
    assert(packet_len < (int)sizeof(packet));
    UNUSED_IF_ASSERT_DISABLED(packet_len);
    StringExtractorGDBRemote response;
    if (SendPacketAndWaitForResponse(packet, response, false) ==
            PacketResult::Success &&
        response.GetResponseType() == StringExtractorGDBRemote::eResponse) {
      llvm::StringRef name;
      llvm::StringRef value;
      addr_t addr_value = LLDB_INVALID_ADDRESS;
      bool success = true;
      bool saw_permissions = false;
      while (success && response.GetNameColonValue(name, value)) {
        if (name.equals("start")) {
          if (!value.getAsInteger(16, addr_value))
            region_info.GetRange().SetRangeBase(addr_value);
        } else if (name.equals("size")) {
          if (!value.getAsInteger(16, addr_value))
            region_info.GetRange().SetByteSize(addr_value);
        } else if (name.equals("permissions") &&
                   region_info.GetRange().IsValid()) {
          saw_permissions = true;
          if (region_info.GetRange().Contains(addr)) {
            if (value.find('r') != llvm::StringRef::npos)
              region_info.SetReadable(MemoryRegionInfo::eYes);
            else
              region_info.SetReadable(MemoryRegionInfo::eNo);

            if (value.find('w') != llvm::StringRef::npos)
              region_info.SetWritable(MemoryRegionInfo::eYes);
            else
              region_info.SetWritable(MemoryRegionInfo::eNo);

            if (value.find('x') != llvm::StringRef::npos)
              region_info.SetExecutable(MemoryRegionInfo::eYes);
            else
              region_info.SetExecutable(MemoryRegionInfo::eNo);

            region_info.SetMapped(MemoryRegionInfo::eYes);
          } else {
            // The reported region does not contain this address -- we're
            // looking at an unmapped page
            region_info.SetReadable(MemoryRegionInfo::eNo);
            region_info.SetWritable(MemoryRegionInfo::eNo);
            region_info.SetExecutable(MemoryRegionInfo::eNo);
            region_info.SetMapped(MemoryRegionInfo::eNo);
          }
        } else if (name.equals("name")) {
          StringExtractorGDBRemote name_extractor(value);
          std::string name;
          name_extractor.GetHexByteString(name);
          region_info.SetName(name.c_str());
        } else if (name.equals("error")) {
          StringExtractorGDBRemote error_extractor(value);
          std::string error_string;
          // Now convert the HEX bytes into a string value
          error_extractor.GetHexByteString(error_string);
          error.SetErrorString(error_string.c_str());
        }
      }

      if (region_info.GetRange().IsValid()) {
        // We got a valid address range back but no permissions -- which means
        // this is an unmapped page
        if (!saw_permissions) {
          region_info.SetReadable(MemoryRegionInfo::eNo);
          region_info.SetWritable(MemoryRegionInfo::eNo);
          region_info.SetExecutable(MemoryRegionInfo::eNo);
          region_info.SetMapped(MemoryRegionInfo::eNo);
        }
      } else {
        // We got an invalid address range back
        error.SetErrorString("Server returned invalid range");
      }
    } else {
      m_supports_memory_region_info = eLazyBoolNo;
    }
  }

  if (m_supports_memory_region_info == eLazyBoolNo) {
    error.SetErrorString("qMemoryRegionInfo is not supported");
  }

  // Try qXfer:memory-map:read to get region information not included in
  // qMemoryRegionInfo
  MemoryRegionInfo qXfer_region_info;
  Status qXfer_error = GetQXferMemoryMapRegionInfo(addr, qXfer_region_info);

  if (error.Fail()) {
    // If qMemoryRegionInfo failed, but qXfer:memory-map:read succeeded, use
    // the qXfer result as a fallback
    if (qXfer_error.Success()) {
      region_info = qXfer_region_info;
      error.Clear();
    } else {
      region_info.Clear();
    }
  } else if (qXfer_error.Success()) {
    // If both qMemoryRegionInfo and qXfer:memory-map:read succeeded, and if
    // both regions are the same range, update the result to include the flash-
    // memory information that is specific to the qXfer result.
    if (region_info.GetRange() == qXfer_region_info.GetRange()) {
      region_info.SetFlash(qXfer_region_info.GetFlash());
      region_info.SetBlocksize(qXfer_region_info.GetBlocksize());
    }
  }
  return error;
}

Status GDBRemoteCommunicationClient::GetQXferMemoryMapRegionInfo(
    lldb::addr_t addr, MemoryRegionInfo &region) {
  Status error = LoadQXferMemoryMap();
  if (!error.Success())
    return error;
  for (const auto &map_region : m_qXfer_memory_map) {
    if (map_region.GetRange().Contains(addr)) {
      region = map_region;
      return error;
    }
  }
  error.SetErrorString("Region not found");
  return error;
}

Status GDBRemoteCommunicationClient::LoadQXferMemoryMap() {

  Status error;

  if (m_qXfer_memory_map_loaded)
    // Already loaded, return success
    return error;

  if (!XMLDocument::XMLEnabled()) {
    error.SetErrorString("XML is not supported");
    return error;
  }

  if (!GetQXferMemoryMapReadSupported()) {
    error.SetErrorString("Memory map is not supported");
    return error;
  }

  std::string xml;
  lldb_private::Status lldberr;
  if (!ReadExtFeature(ConstString("memory-map"), ConstString(""), xml,
                      lldberr)) {
    error.SetErrorString("Failed to read memory map");
    return error;
  }

  XMLDocument xml_document;

  if (!xml_document.ParseMemory(xml.c_str(), xml.size())) {
    error.SetErrorString("Failed to parse memory map xml");
    return error;
  }

  XMLNode map_node = xml_document.GetRootElement("memory-map");
  if (!map_node) {
    error.SetErrorString("Invalid root node in memory map xml");
    return error;
  }

  m_qXfer_memory_map.clear();

  map_node.ForEachChildElement([this](const XMLNode &memory_node) -> bool {
    if (!memory_node.IsElement())
      return true;
    if (memory_node.GetName() != "memory")
      return true;
    auto type = memory_node.GetAttributeValue("type", "");
    uint64_t start;
    uint64_t length;
    if (!memory_node.GetAttributeValueAsUnsigned("start", start))
      return true;
    if (!memory_node.GetAttributeValueAsUnsigned("length", length))
      return true;
    MemoryRegionInfo region;
    region.GetRange().SetRangeBase(start);
    region.GetRange().SetByteSize(length);
    if (type == "rom") {
      region.SetReadable(MemoryRegionInfo::eYes);
      this->m_qXfer_memory_map.push_back(region);
    } else if (type == "ram") {
      region.SetReadable(MemoryRegionInfo::eYes);
      region.SetWritable(MemoryRegionInfo::eYes);
      this->m_qXfer_memory_map.push_back(region);
    } else if (type == "flash") {
      region.SetFlash(MemoryRegionInfo::eYes);
      memory_node.ForEachChildElement(
          [&region](const XMLNode &prop_node) -> bool {
            if (!prop_node.IsElement())
              return true;
            if (prop_node.GetName() != "property")
              return true;
            auto propname = prop_node.GetAttributeValue("name", "");
            if (propname == "blocksize") {
              uint64_t blocksize;
              if (prop_node.GetElementTextAsUnsigned(blocksize))
                region.SetBlocksize(blocksize);
            }
            return true;
          });
      this->m_qXfer_memory_map.push_back(region);
    }
    return true;
  });

  m_qXfer_memory_map_loaded = true;

  return error;
}

Status GDBRemoteCommunicationClient::GetWatchpointSupportInfo(uint32_t &num) {
  Status error;

  if (m_supports_watchpoint_support_info == eLazyBoolYes) {
    num = m_num_supported_hardware_watchpoints;
    return error;
  }

  // Set num to 0 first.
  num = 0;
  if (m_supports_watchpoint_support_info != eLazyBoolNo) {
    char packet[64];
    const int packet_len =
        ::snprintf(packet, sizeof(packet), "qWatchpointSupportInfo:");
    assert(packet_len < (int)sizeof(packet));
    UNUSED_IF_ASSERT_DISABLED(packet_len);
    StringExtractorGDBRemote response;
    if (SendPacketAndWaitForResponse(packet, response, false) ==
        PacketResult::Success) {
      m_supports_watchpoint_support_info = eLazyBoolYes;
      llvm::StringRef name;
      llvm::StringRef value;
      bool found_num_field = false;
      while (response.GetNameColonValue(name, value)) {
        if (name.equals("num")) {
          value.getAsInteger(0, m_num_supported_hardware_watchpoints);
          num = m_num_supported_hardware_watchpoints;
          found_num_field = true;
        }
      }
      if (!found_num_field) {
        m_supports_watchpoint_support_info = eLazyBoolNo;
      }
    } else {
      m_supports_watchpoint_support_info = eLazyBoolNo;
    }
  }

  if (m_supports_watchpoint_support_info == eLazyBoolNo) {
    error.SetErrorString("qWatchpointSupportInfo is not supported");
  }
  return error;
}

lldb_private::Status GDBRemoteCommunicationClient::GetWatchpointSupportInfo(
    uint32_t &num, bool &after, const ArchSpec &arch) {
  Status error(GetWatchpointSupportInfo(num));
  if (error.Success())
    error = GetWatchpointsTriggerAfterInstruction(after, arch);
  return error;
}

lldb_private::Status
GDBRemoteCommunicationClient::GetWatchpointsTriggerAfterInstruction(
    bool &after, const ArchSpec &arch) {
  Status error;
  llvm::Triple::ArchType atype = arch.GetMachine();

  // we assume watchpoints will happen after running the relevant opcode and we
  // only want to override this behavior if we have explicitly received a
  // qHostInfo telling us otherwise
  if (m_qHostInfo_is_valid != eLazyBoolYes) {
    // On targets like MIPS and ppc64le, watchpoint exceptions are always
    // generated before the instruction is executed. The connected target may
    // not support qHostInfo or qWatchpointSupportInfo packets.
    after =
        !(atype == llvm::Triple::mips || atype == llvm::Triple::mipsel ||
          atype == llvm::Triple::mips64 || atype == llvm::Triple::mips64el ||
          atype == llvm::Triple::ppc64le);
  } else {
    // For MIPS and ppc64le, set m_watchpoints_trigger_after_instruction to
    // eLazyBoolNo if it is not calculated before.
    if ((m_watchpoints_trigger_after_instruction == eLazyBoolCalculate &&
         (atype == llvm::Triple::mips || atype == llvm::Triple::mipsel ||
          atype == llvm::Triple::mips64 || atype == llvm::Triple::mips64el)) ||
        atype == llvm::Triple::ppc64le) {
      m_watchpoints_trigger_after_instruction = eLazyBoolNo;
    }

    after = (m_watchpoints_trigger_after_instruction != eLazyBoolNo);
  }
  return error;
}

int GDBRemoteCommunicationClient::SetSTDIN(const FileSpec &file_spec) {
  if (file_spec) {
    std::string path{file_spec.GetPath(false)};
    StreamString packet;
    packet.PutCString("QSetSTDIN:");
    packet.PutCStringAsRawHex8(path.c_str());

    StringExtractorGDBRemote response;
    if (SendPacketAndWaitForResponse(packet.GetString(), response, false) ==
        PacketResult::Success) {
      if (response.IsOKResponse())
        return 0;
      uint8_t error = response.GetError();
      if (error)
        return error;
    }
  }
  return -1;
}

int GDBRemoteCommunicationClient::SetSTDOUT(const FileSpec &file_spec) {
  if (file_spec) {
    std::string path{file_spec.GetPath(false)};
    StreamString packet;
    packet.PutCString("QSetSTDOUT:");
    packet.PutCStringAsRawHex8(path.c_str());

    StringExtractorGDBRemote response;
    if (SendPacketAndWaitForResponse(packet.GetString(), response, false) ==
        PacketResult::Success) {
      if (response.IsOKResponse())
        return 0;
      uint8_t error = response.GetError();
      if (error)
        return error;
    }
  }
  return -1;
}

int GDBRemoteCommunicationClient::SetSTDERR(const FileSpec &file_spec) {
  if (file_spec) {
    std::string path{file_spec.GetPath(false)};
    StreamString packet;
    packet.PutCString("QSetSTDERR:");
    packet.PutCStringAsRawHex8(path.c_str());

    StringExtractorGDBRemote response;
    if (SendPacketAndWaitForResponse(packet.GetString(), response, false) ==
        PacketResult::Success) {
      if (response.IsOKResponse())
        return 0;
      uint8_t error = response.GetError();
      if (error)
        return error;
    }
  }
  return -1;
}

bool GDBRemoteCommunicationClient::GetWorkingDir(FileSpec &working_dir) {
  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse("qGetWorkingDir", response, false) ==
      PacketResult::Success) {
    if (response.IsUnsupportedResponse())
      return false;
    if (response.IsErrorResponse())
      return false;
    std::string cwd;
    response.GetHexByteString(cwd);
    working_dir.SetFile(cwd, GetHostArchitecture().GetTriple());
    return !cwd.empty();
  }
  return false;
}

int GDBRemoteCommunicationClient::SetWorkingDir(const FileSpec &working_dir) {
  if (working_dir) {
    std::string path{working_dir.GetPath(false)};
    StreamString packet;
    packet.PutCString("QSetWorkingDir:");
    packet.PutCStringAsRawHex8(path.c_str());

    StringExtractorGDBRemote response;
    if (SendPacketAndWaitForResponse(packet.GetString(), response, false) ==
        PacketResult::Success) {
      if (response.IsOKResponse())
        return 0;
      uint8_t error = response.GetError();
      if (error)
        return error;
    }
  }
  return -1;
}

int GDBRemoteCommunicationClient::SetDisableASLR(bool enable) {
  char packet[32];
  const int packet_len =
      ::snprintf(packet, sizeof(packet), "QSetDisableASLR:%i", enable ? 1 : 0);
  assert(packet_len < (int)sizeof(packet));
  UNUSED_IF_ASSERT_DISABLED(packet_len);
  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse(packet, response, false) ==
      PacketResult::Success) {
    if (response.IsOKResponse())
      return 0;
    uint8_t error = response.GetError();
    if (error)
      return error;
  }
  return -1;
}

int GDBRemoteCommunicationClient::SetDetachOnError(bool enable) {
  char packet[32];
  const int packet_len = ::snprintf(packet, sizeof(packet),
                                    "QSetDetachOnError:%i", enable ? 1 : 0);
  assert(packet_len < (int)sizeof(packet));
  UNUSED_IF_ASSERT_DISABLED(packet_len);
  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse(packet, response, false) ==
      PacketResult::Success) {
    if (response.IsOKResponse())
      return 0;
    uint8_t error = response.GetError();
    if (error)
      return error;
  }
  return -1;
}

bool GDBRemoteCommunicationClient::DecodeProcessInfoResponse(
    StringExtractorGDBRemote &response, ProcessInstanceInfo &process_info) {
  if (response.IsNormalResponse()) {
    llvm::StringRef name;
    llvm::StringRef value;
    StringExtractor extractor;

    uint32_t cpu = LLDB_INVALID_CPUTYPE;
    uint32_t sub = 0;
    std::string vendor;
    std::string os_type;

    while (response.GetNameColonValue(name, value)) {
      if (name.equals("pid")) {
        lldb::pid_t pid = LLDB_INVALID_PROCESS_ID;
        value.getAsInteger(0, pid);
        process_info.SetProcessID(pid);
      } else if (name.equals("ppid")) {
        lldb::pid_t pid = LLDB_INVALID_PROCESS_ID;
        value.getAsInteger(0, pid);
        process_info.SetParentProcessID(pid);
      } else if (name.equals("uid")) {
        uint32_t uid = UINT32_MAX;
        value.getAsInteger(0, uid);
        process_info.SetUserID(uid);
      } else if (name.equals("euid")) {
        uint32_t uid = UINT32_MAX;
        value.getAsInteger(0, uid);
        process_info.SetEffectiveGroupID(uid);
      } else if (name.equals("gid")) {
        uint32_t gid = UINT32_MAX;
        value.getAsInteger(0, gid);
        process_info.SetGroupID(gid);
      } else if (name.equals("egid")) {
        uint32_t gid = UINT32_MAX;
        value.getAsInteger(0, gid);
        process_info.SetEffectiveGroupID(gid);
      } else if (name.equals("triple")) {
        StringExtractor extractor(value);
        std::string triple;
        extractor.GetHexByteString(triple);
        process_info.GetArchitecture().SetTriple(triple.c_str());
      } else if (name.equals("name")) {
        StringExtractor extractor(value);
        // The process name from ASCII hex bytes since we can't control the
        // characters in a process name
        std::string name;
        extractor.GetHexByteString(name);
        process_info.GetExecutableFile().SetFile(name, FileSpec::Style::native);
      } else if (name.equals("cputype")) {
        value.getAsInteger(0, cpu);
      } else if (name.equals("cpusubtype")) {
        value.getAsInteger(0, sub);
      } else if (name.equals("vendor")) {
        vendor = value;
      } else if (name.equals("ostype")) {
        os_type = value;
      }
    }

    if (cpu != LLDB_INVALID_CPUTYPE && !vendor.empty() && !os_type.empty()) {
      if (vendor == "apple") {
        process_info.GetArchitecture().SetArchitecture(eArchTypeMachO, cpu,
                                                       sub);
        process_info.GetArchitecture().GetTriple().setVendorName(
            llvm::StringRef(vendor));
        process_info.GetArchitecture().GetTriple().setOSName(
            llvm::StringRef(os_type));
      }
    }

    if (process_info.GetProcessID() != LLDB_INVALID_PROCESS_ID)
      return true;
  }
  return false;
}

bool GDBRemoteCommunicationClient::GetProcessInfo(
    lldb::pid_t pid, ProcessInstanceInfo &process_info) {
  process_info.Clear();

  if (m_supports_qProcessInfoPID) {
    char packet[32];
    const int packet_len =
        ::snprintf(packet, sizeof(packet), "qProcessInfoPID:%" PRIu64, pid);
    assert(packet_len < (int)sizeof(packet));
    UNUSED_IF_ASSERT_DISABLED(packet_len);
    StringExtractorGDBRemote response;
    if (SendPacketAndWaitForResponse(packet, response, false) ==
        PacketResult::Success) {
      return DecodeProcessInfoResponse(response, process_info);
    } else {
      m_supports_qProcessInfoPID = false;
      return false;
    }
  }
  return false;
}

bool GDBRemoteCommunicationClient::GetCurrentProcessInfo(bool allow_lazy) {
  Log *log(ProcessGDBRemoteLog::GetLogIfAnyCategoryIsSet(GDBR_LOG_PROCESS |
                                                         GDBR_LOG_PACKETS));

  if (allow_lazy) {
    if (m_qProcessInfo_is_valid == eLazyBoolYes)
      return true;
    if (m_qProcessInfo_is_valid == eLazyBoolNo)
      return false;
  }

  GetHostInfo();

  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse("qProcessInfo", response, false) ==
      PacketResult::Success) {
    if (response.IsNormalResponse()) {
      llvm::StringRef name;
      llvm::StringRef value;
      uint32_t cpu = LLDB_INVALID_CPUTYPE;
      uint32_t sub = 0;
      std::string arch_name;
      std::string os_name;
      std::string vendor_name;
      std::string triple;
      std::string elf_abi;
      uint32_t pointer_byte_size = 0;
      StringExtractor extractor;
      ByteOrder byte_order = eByteOrderInvalid;
      uint32_t num_keys_decoded = 0;
      lldb::pid_t pid = LLDB_INVALID_PROCESS_ID;
      while (response.GetNameColonValue(name, value)) {
        if (name.equals("cputype")) {
          if (!value.getAsInteger(16, cpu))
            ++num_keys_decoded;
        } else if (name.equals("cpusubtype")) {
          if (!value.getAsInteger(16, sub))
            ++num_keys_decoded;
        } else if (name.equals("triple")) {
          StringExtractor extractor(value);
          extractor.GetHexByteString(triple);
          ++num_keys_decoded;
        } else if (name.equals("ostype")) {
          os_name = value;
          ++num_keys_decoded;
        } else if (name.equals("vendor")) {
          vendor_name = value;
          ++num_keys_decoded;
        } else if (name.equals("endian")) {
          byte_order = llvm::StringSwitch<lldb::ByteOrder>(value)
                           .Case("little", eByteOrderLittle)
                           .Case("big", eByteOrderBig)
                           .Case("pdp", eByteOrderPDP)
                           .Default(eByteOrderInvalid);
          if (byte_order != eByteOrderInvalid)
            ++num_keys_decoded;
        } else if (name.equals("ptrsize")) {
          if (!value.getAsInteger(16, pointer_byte_size))
            ++num_keys_decoded;
        } else if (name.equals("pid")) {
          if (!value.getAsInteger(16, pid))
            ++num_keys_decoded;
        } else if (name.equals("elf_abi")) {
          elf_abi = value;
          ++num_keys_decoded;
        }
      }
      if (num_keys_decoded > 0)
        m_qProcessInfo_is_valid = eLazyBoolYes;
      if (pid != LLDB_INVALID_PROCESS_ID) {
        m_curr_pid_is_valid = eLazyBoolYes;
        m_curr_pid = pid;
      }

      // Set the ArchSpec from the triple if we have it.
      if (!triple.empty()) {
        m_process_arch.SetTriple(triple.c_str());
        m_process_arch.SetFlags(elf_abi);
        if (pointer_byte_size) {
          assert(pointer_byte_size == m_process_arch.GetAddressByteSize());
        }
      } else if (cpu != LLDB_INVALID_CPUTYPE && !os_name.empty() &&
                 !vendor_name.empty()) {
        llvm::Triple triple(llvm::Twine("-") + vendor_name + "-" + os_name);

        assert(triple.getObjectFormat() != llvm::Triple::UnknownObjectFormat);
        assert(triple.getObjectFormat() != llvm::Triple::Wasm);
        switch (triple.getObjectFormat()) {
        case llvm::Triple::MachO:
          m_process_arch.SetArchitecture(eArchTypeMachO, cpu, sub);
          break;
        case llvm::Triple::ELF:
          m_process_arch.SetArchitecture(eArchTypeELF, cpu, sub);
          break;
        case llvm::Triple::COFF:
          m_process_arch.SetArchitecture(eArchTypeCOFF, cpu, sub);
          break;
        case llvm::Triple::Wasm:
          if (log)
            log->Printf("error: not supported target architecture");
          return false;
        case llvm::Triple::UnknownObjectFormat:
          if (log)
            log->Printf("error: failed to determine target architecture");
          return false;
        }

        if (pointer_byte_size) {
          assert(pointer_byte_size == m_process_arch.GetAddressByteSize());
        }
        if (byte_order != eByteOrderInvalid) {
          assert(byte_order == m_process_arch.GetByteOrder());
        }
        m_process_arch.GetTriple().setVendorName(llvm::StringRef(vendor_name));
        m_process_arch.GetTriple().setOSName(llvm::StringRef(os_name));
        m_host_arch.GetTriple().setVendorName(llvm::StringRef(vendor_name));
        m_host_arch.GetTriple().setOSName(llvm::StringRef(os_name));
      }
      return true;
    }
  } else {
    m_qProcessInfo_is_valid = eLazyBoolNo;
  }

  return false;
}

uint32_t GDBRemoteCommunicationClient::FindProcesses(
    const ProcessInstanceInfoMatch &match_info,
    ProcessInstanceInfoList &process_infos) {
  process_infos.Clear();

  if (m_supports_qfProcessInfo) {
    StreamString packet;
    packet.PutCString("qfProcessInfo");
    if (!match_info.MatchAllProcesses()) {
      packet.PutChar(':');
      const char *name = match_info.GetProcessInfo().GetName();
      bool has_name_match = false;
      if (name && name[0]) {
        has_name_match = true;
        NameMatch name_match_type = match_info.GetNameMatchType();
        switch (name_match_type) {
        case NameMatch::Ignore:
          has_name_match = false;
          break;

        case NameMatch::Equals:
          packet.PutCString("name_match:equals;");
          break;

        case NameMatch::Contains:
          packet.PutCString("name_match:contains;");
          break;

        case NameMatch::StartsWith:
          packet.PutCString("name_match:starts_with;");
          break;

        case NameMatch::EndsWith:
          packet.PutCString("name_match:ends_with;");
          break;

        case NameMatch::RegularExpression:
          packet.PutCString("name_match:regex;");
          break;
        }
        if (has_name_match) {
          packet.PutCString("name:");
          packet.PutBytesAsRawHex8(name, ::strlen(name));
          packet.PutChar(';');
        }
      }

      if (match_info.GetProcessInfo().ProcessIDIsValid())
        packet.Printf("pid:%" PRIu64 ";",
                      match_info.GetProcessInfo().GetProcessID());
      if (match_info.GetProcessInfo().ParentProcessIDIsValid())
        packet.Printf("parent_pid:%" PRIu64 ";",
                      match_info.GetProcessInfo().GetParentProcessID());
      if (match_info.GetProcessInfo().UserIDIsValid())
        packet.Printf("uid:%u;", match_info.GetProcessInfo().GetUserID());
      if (match_info.GetProcessInfo().GroupIDIsValid())
        packet.Printf("gid:%u;", match_info.GetProcessInfo().GetGroupID());
      if (match_info.GetProcessInfo().EffectiveUserIDIsValid())
        packet.Printf("euid:%u;",
                      match_info.GetProcessInfo().GetEffectiveUserID());
      if (match_info.GetProcessInfo().EffectiveGroupIDIsValid())
        packet.Printf("egid:%u;",
                      match_info.GetProcessInfo().GetEffectiveGroupID());
      if (match_info.GetProcessInfo().EffectiveGroupIDIsValid())
        packet.Printf("all_users:%u;", match_info.GetMatchAllUsers() ? 1 : 0);
      if (match_info.GetProcessInfo().GetArchitecture().IsValid()) {
        const ArchSpec &match_arch =
            match_info.GetProcessInfo().GetArchitecture();
        const llvm::Triple &triple = match_arch.GetTriple();
        packet.PutCString("triple:");
        packet.PutCString(triple.getTriple());
        packet.PutChar(';');
      }
    }
    StringExtractorGDBRemote response;
    // Increase timeout as the first qfProcessInfo packet takes a long time on
    // Android. The value of 1min was arrived at empirically.
    ScopedTimeout timeout(*this, minutes(1));
    if (SendPacketAndWaitForResponse(packet.GetString(), response, false) ==
        PacketResult::Success) {
      do {
        ProcessInstanceInfo process_info;
        if (!DecodeProcessInfoResponse(response, process_info))
          break;
        process_infos.Append(process_info);
        response.GetStringRef().clear();
        response.SetFilePos(0);
      } while (SendPacketAndWaitForResponse("qsProcessInfo", response, false) ==
               PacketResult::Success);
    } else {
      m_supports_qfProcessInfo = false;
      return 0;
    }
  }
  return process_infos.GetSize();
}

bool GDBRemoteCommunicationClient::GetUserName(uint32_t uid,
                                               std::string &name) {
  if (m_supports_qUserName) {
    char packet[32];
    const int packet_len =
        ::snprintf(packet, sizeof(packet), "qUserName:%i", uid);
    assert(packet_len < (int)sizeof(packet));
    UNUSED_IF_ASSERT_DISABLED(packet_len);
    StringExtractorGDBRemote response;
    if (SendPacketAndWaitForResponse(packet, response, false) ==
        PacketResult::Success) {
      if (response.IsNormalResponse()) {
        // Make sure we parsed the right number of characters. The response is
        // the hex encoded user name and should make up the entire packet. If
        // there are any non-hex ASCII bytes, the length won't match below..
        if (response.GetHexByteString(name) * 2 ==
            response.GetStringRef().size())
          return true;
      }
    } else {
      m_supports_qUserName = false;
      return false;
    }
  }
  return false;
}

bool GDBRemoteCommunicationClient::GetGroupName(uint32_t gid,
                                                std::string &name) {
  if (m_supports_qGroupName) {
    char packet[32];
    const int packet_len =
        ::snprintf(packet, sizeof(packet), "qGroupName:%i", gid);
    assert(packet_len < (int)sizeof(packet));
    UNUSED_IF_ASSERT_DISABLED(packet_len);
    StringExtractorGDBRemote response;
    if (SendPacketAndWaitForResponse(packet, response, false) ==
        PacketResult::Success) {
      if (response.IsNormalResponse()) {
        // Make sure we parsed the right number of characters. The response is
        // the hex encoded group name and should make up the entire packet. If
        // there are any non-hex ASCII bytes, the length won't match below..
        if (response.GetHexByteString(name) * 2 ==
            response.GetStringRef().size())
          return true;
      }
    } else {
      m_supports_qGroupName = false;
      return false;
    }
  }
  return false;
}

bool GDBRemoteCommunicationClient::SetNonStopMode(const bool enable) {
  // Form non-stop packet request
  char packet[32];
  const int packet_len =
      ::snprintf(packet, sizeof(packet), "QNonStop:%1d", (int)enable);
  assert(packet_len < (int)sizeof(packet));
  UNUSED_IF_ASSERT_DISABLED(packet_len);

  StringExtractorGDBRemote response;
  // Send to target
  if (SendPacketAndWaitForResponse(packet, response, false) ==
      PacketResult::Success)
    if (response.IsOKResponse())
      return true;

  // Failed or not supported
  return false;
}

static void MakeSpeedTestPacket(StreamString &packet, uint32_t send_size,
                                uint32_t recv_size) {
  packet.Clear();
  packet.Printf("qSpeedTest:response_size:%i;data:", recv_size);
  uint32_t bytes_left = send_size;
  while (bytes_left > 0) {
    if (bytes_left >= 26) {
      packet.PutCString("abcdefghijklmnopqrstuvwxyz");
      bytes_left -= 26;
    } else {
      packet.Printf("%*.*s;", bytes_left, bytes_left,
                    "abcdefghijklmnopqrstuvwxyz");
      bytes_left = 0;
    }
  }
}

duration<float>
calculate_standard_deviation(const std::vector<duration<float>> &v) {
  using Dur = duration<float>;
  Dur sum = std::accumulate(std::begin(v), std::end(v), Dur());
  Dur mean = sum / v.size();
  float accum = 0;
  for (auto d : v) {
    float delta = (d - mean).count();
    accum += delta * delta;
  };

  return Dur(sqrtf(accum / (v.size() - 1)));
}

void GDBRemoteCommunicationClient::TestPacketSpeed(const uint32_t num_packets,
                                                   uint32_t max_send,
                                                   uint32_t max_recv,
                                                   uint64_t recv_amount,
                                                   bool json, Stream &strm) {
  uint32_t i;
  if (SendSpeedTestPacket(0, 0)) {
    StreamString packet;
    if (json)
      strm.Printf("{ \"packet_speeds\" : {\n    \"num_packets\" : %u,\n    "
                  "\"results\" : [",
                  num_packets);
    else
      strm.Printf("Testing sending %u packets of various sizes:\n",
                  num_packets);
    strm.Flush();

    uint32_t result_idx = 0;
    uint32_t send_size;
    std::vector<duration<float>> packet_times;

    for (send_size = 0; send_size <= max_send;
         send_size ? send_size *= 2 : send_size = 4) {
      for (uint32_t recv_size = 0; recv_size <= max_recv;
           recv_size ? recv_size *= 2 : recv_size = 4) {
        MakeSpeedTestPacket(packet, send_size, recv_size);

        packet_times.clear();
        // Test how long it takes to send 'num_packets' packets
        const auto start_time = steady_clock::now();
        for (i = 0; i < num_packets; ++i) {
          const auto packet_start_time = steady_clock::now();
          StringExtractorGDBRemote response;
          SendPacketAndWaitForResponse(packet.GetString(), response, false);
          const auto packet_end_time = steady_clock::now();
          packet_times.push_back(packet_end_time - packet_start_time);
        }
        const auto end_time = steady_clock::now();
        const auto total_time = end_time - start_time;

        float packets_per_second =
            ((float)num_packets) / duration<float>(total_time).count();
        auto average_per_packet = total_time / num_packets;
        const duration<float> standard_deviation =
            calculate_standard_deviation(packet_times);
        if (json) {
          strm.Format("{0}\n     {{\"send_size\" : {1,6}, \"recv_size\" : "
                      "{2,6}, \"total_time_nsec\" : {3,12:ns-}, "
                      "\"standard_deviation_nsec\" : {4,9:ns-f0}}",
                      result_idx > 0 ? "," : "", send_size, recv_size,
                      total_time, standard_deviation);
          ++result_idx;
        } else {
          strm.Format("qSpeedTest(send={0,7}, recv={1,7}) in {2:s+f9} for "
                      "{3,9:f2} packets/s ({4,10:ms+f6} per packet) with "
                      "standard deviation of {5,10:ms+f6}\n",
                      send_size, recv_size, duration<float>(total_time),
                      packets_per_second, duration<float>(average_per_packet),
                      standard_deviation);
        }
        strm.Flush();
      }
    }

    const float k_recv_amount_mb = (float)recv_amount / (1024.0f * 1024.0f);
    if (json)
      strm.Printf("\n    ]\n  },\n  \"download_speed\" : {\n    \"byte_size\" "
                  ": %" PRIu64 ",\n    \"results\" : [",
                  recv_amount);
    else
      strm.Printf("Testing receiving %2.1fMB of data using varying receive "
                  "packet sizes:\n",
                  k_recv_amount_mb);
    strm.Flush();
    send_size = 0;
    result_idx = 0;
    for (uint32_t recv_size = 32; recv_size <= max_recv; recv_size *= 2) {
      MakeSpeedTestPacket(packet, send_size, recv_size);

      // If we have a receive size, test how long it takes to receive 4MB of
      // data
      if (recv_size > 0) {
        const auto start_time = steady_clock::now();
        uint32_t bytes_read = 0;
        uint32_t packet_count = 0;
        while (bytes_read < recv_amount) {
          StringExtractorGDBRemote response;
          SendPacketAndWaitForResponse(packet.GetString(), response, false);
          bytes_read += recv_size;
          ++packet_count;
        }
        const auto end_time = steady_clock::now();
        const auto total_time = end_time - start_time;
        float mb_second = ((float)recv_amount) /
                          duration<float>(total_time).count() /
                          (1024.0 * 1024.0);
        float packets_per_second =
            ((float)packet_count) / duration<float>(total_time).count();
        const auto average_per_packet = total_time / packet_count;

        if (json) {
          strm.Format("{0}\n     {{\"send_size\" : {1,6}, \"recv_size\" : "
                      "{2,6}, \"total_time_nsec\" : {3,12:ns-}}",
                      result_idx > 0 ? "," : "", send_size, recv_size,
                      total_time);
          ++result_idx;
        } else {
          strm.Format("qSpeedTest(send={0,7}, recv={1,7}) {2,6} packets needed "
                      "to receive {3:f1}MB in {4:s+f9} for {5} MB/sec for "
                      "{6,9:f2} packets/sec ({7,10:ms+f6} per packet)\n",
                      send_size, recv_size, packet_count, k_recv_amount_mb,
                      duration<float>(total_time), mb_second,
                      packets_per_second, duration<float>(average_per_packet));
        }
        strm.Flush();
      }
    }
    if (json)
      strm.Printf("\n    ]\n  }\n}\n");
    else
      strm.EOL();
  }
}

bool GDBRemoteCommunicationClient::SendSpeedTestPacket(uint32_t send_size,
                                                       uint32_t recv_size) {
  StreamString packet;
  packet.Printf("qSpeedTest:response_size:%i;data:", recv_size);
  uint32_t bytes_left = send_size;
  while (bytes_left > 0) {
    if (bytes_left >= 26) {
      packet.PutCString("abcdefghijklmnopqrstuvwxyz");
      bytes_left -= 26;
    } else {
      packet.Printf("%*.*s;", bytes_left, bytes_left,
                    "abcdefghijklmnopqrstuvwxyz");
      bytes_left = 0;
    }
  }

  StringExtractorGDBRemote response;
  return SendPacketAndWaitForResponse(packet.GetString(), response, false) ==
         PacketResult::Success;
}

bool GDBRemoteCommunicationClient::LaunchGDBServer(
    const char *remote_accept_hostname, lldb::pid_t &pid, uint16_t &port,
    std::string &socket_name) {
  pid = LLDB_INVALID_PROCESS_ID;
  port = 0;
  socket_name.clear();

  StringExtractorGDBRemote response;
  StreamString stream;
  stream.PutCString("qLaunchGDBServer;");
  std::string hostname;
  if (remote_accept_hostname && remote_accept_hostname[0])
    hostname = remote_accept_hostname;
  else {
    if (HostInfo::GetHostname(hostname)) {
      // Make the GDB server we launch only accept connections from this host
      stream.Printf("host:%s;", hostname.c_str());
    } else {
      // Make the GDB server we launch accept connections from any host since
      // we can't figure out the hostname
      stream.Printf("host:*;");
    }
  }
  // give the process a few seconds to startup
  ScopedTimeout timeout(*this, seconds(10));

  if (SendPacketAndWaitForResponse(stream.GetString(), response, false) ==
      PacketResult::Success) {
    llvm::StringRef name;
    llvm::StringRef value;
    while (response.GetNameColonValue(name, value)) {
      if (name.equals("port"))
        value.getAsInteger(0, port);
      else if (name.equals("pid"))
        value.getAsInteger(0, pid);
      else if (name.compare("socket_name") == 0) {
        StringExtractor extractor(value);
        extractor.GetHexByteString(socket_name);
      }
    }
    return true;
  }
  return false;
}

size_t GDBRemoteCommunicationClient::QueryGDBServer(
    std::vector<std::pair<uint16_t, std::string>> &connection_urls) {
  connection_urls.clear();

  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse("qQueryGDBServer", response, false) !=
      PacketResult::Success)
    return 0;

  StructuredData::ObjectSP data =
      StructuredData::ParseJSON(response.GetStringRef());
  if (!data)
    return 0;

  StructuredData::Array *array = data->GetAsArray();
  if (!array)
    return 0;

  for (size_t i = 0, count = array->GetSize(); i < count; ++i) {
    StructuredData::Dictionary *element = nullptr;
    if (!array->GetItemAtIndexAsDictionary(i, element))
      continue;

    uint16_t port = 0;
    if (StructuredData::ObjectSP port_osp =
            element->GetValueForKey(llvm::StringRef("port")))
      port = port_osp->GetIntegerValue(0);

    std::string socket_name;
    if (StructuredData::ObjectSP socket_name_osp =
            element->GetValueForKey(llvm::StringRef("socket_name")))
      socket_name = socket_name_osp->GetStringValue();

    if (port != 0 || !socket_name.empty())
      connection_urls.emplace_back(port, socket_name);
  }
  return connection_urls.size();
}

bool GDBRemoteCommunicationClient::KillSpawnedProcess(lldb::pid_t pid) {
  StreamString stream;
  stream.Printf("qKillSpawnedProcess:%" PRId64, pid);

  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse(stream.GetString(), response, false) ==
      PacketResult::Success) {
    if (response.IsOKResponse())
      return true;
  }
  return false;
}

bool GDBRemoteCommunicationClient::SetCurrentThread(uint64_t tid) {
  if (m_curr_tid == tid)
    return true;

  char packet[32];
  int packet_len;
  if (tid == UINT64_MAX)
    packet_len = ::snprintf(packet, sizeof(packet), "Hg-1");
  else
    packet_len = ::snprintf(packet, sizeof(packet), "Hg%" PRIx64, tid);
  assert(packet_len + 1 < (int)sizeof(packet));
  UNUSED_IF_ASSERT_DISABLED(packet_len);
  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse(packet, response, false) ==
      PacketResult::Success) {
    if (response.IsOKResponse()) {
      m_curr_tid = tid;
      return true;
    }

    /*
     * Connected bare-iron target (like YAMON gdb-stub) may not have support for
     * Hg packet.
     * The reply from '?' packet could be as simple as 'S05'. There is no packet
     * which can
     * give us pid and/or tid. Assume pid=tid=1 in such cases.
    */
    if (response.IsUnsupportedResponse() && IsConnected()) {
      m_curr_tid = 1;
      return true;
    }
  }
  return false;
}

bool GDBRemoteCommunicationClient::SetCurrentThreadForRun(uint64_t tid) {
  if (m_curr_tid_run == tid)
    return true;

  char packet[32];
  int packet_len;
  if (tid == UINT64_MAX)
    packet_len = ::snprintf(packet, sizeof(packet), "Hc-1");
  else
    packet_len = ::snprintf(packet, sizeof(packet), "Hc%" PRIx64, tid);

  assert(packet_len + 1 < (int)sizeof(packet));
  UNUSED_IF_ASSERT_DISABLED(packet_len);
  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse(packet, response, false) ==
      PacketResult::Success) {
    if (response.IsOKResponse()) {
      m_curr_tid_run = tid;
      return true;
    }

    /*
     * Connected bare-iron target (like YAMON gdb-stub) may not have support for
     * Hc packet.
     * The reply from '?' packet could be as simple as 'S05'. There is no packet
     * which can
     * give us pid and/or tid. Assume pid=tid=1 in such cases.
    */
    if (response.IsUnsupportedResponse() && IsConnected()) {
      m_curr_tid_run = 1;
      return true;
    }
  }
  return false;
}

bool GDBRemoteCommunicationClient::GetStopReply(
    StringExtractorGDBRemote &response) {
  if (SendPacketAndWaitForResponse("?", response, false) ==
      PacketResult::Success)
    return response.IsNormalResponse();
  return false;
}

bool GDBRemoteCommunicationClient::GetThreadStopInfo(
    lldb::tid_t tid, StringExtractorGDBRemote &response) {
  if (m_supports_qThreadStopInfo) {
    char packet[256];
    int packet_len =
        ::snprintf(packet, sizeof(packet), "qThreadStopInfo%" PRIx64, tid);
    assert(packet_len < (int)sizeof(packet));
    UNUSED_IF_ASSERT_DISABLED(packet_len);
    if (SendPacketAndWaitForResponse(packet, response, false) ==
        PacketResult::Success) {
      if (response.IsUnsupportedResponse())
        m_supports_qThreadStopInfo = false;
      else if (response.IsNormalResponse())
        return true;
      else
        return false;
    } else {
      m_supports_qThreadStopInfo = false;
    }
  }
  return false;
}

uint8_t GDBRemoteCommunicationClient::SendGDBStoppointTypePacket(
    GDBStoppointType type, bool insert, addr_t addr, uint32_t length) {
  Log *log(GetLogIfAnyCategoriesSet(LIBLLDB_LOG_BREAKPOINTS));
  if (log)
    log->Printf("GDBRemoteCommunicationClient::%s() %s at addr = 0x%" PRIx64,
                __FUNCTION__, insert ? "add" : "remove", addr);

  // Check if the stub is known not to support this breakpoint type
  if (!SupportsGDBStoppointPacket(type))
    return UINT8_MAX;
  // Construct the breakpoint packet
  char packet[64];
  const int packet_len =
      ::snprintf(packet, sizeof(packet), "%c%i,%" PRIx64 ",%x",
                 insert ? 'Z' : 'z', type, addr, length);
  // Check we haven't overwritten the end of the packet buffer
  assert(packet_len + 1 < (int)sizeof(packet));
  UNUSED_IF_ASSERT_DISABLED(packet_len);
  StringExtractorGDBRemote response;
  // Make sure the response is either "OK", "EXX" where XX are two hex digits,
  // or "" (unsupported)
  response.SetResponseValidatorToOKErrorNotSupported();
  // Try to send the breakpoint packet, and check that it was correctly sent
  if (SendPacketAndWaitForResponse(packet, response, true) ==
      PacketResult::Success) {
    // Receive and OK packet when the breakpoint successfully placed
    if (response.IsOKResponse())
      return 0;

    // Status while setting breakpoint, send back specific error
    if (response.IsErrorResponse())
      return response.GetError();

    // Empty packet informs us that breakpoint is not supported
    if (response.IsUnsupportedResponse()) {
      // Disable this breakpoint type since it is unsupported
      switch (type) {
      case eBreakpointSoftware:
        m_supports_z0 = false;
        break;
      case eBreakpointHardware:
        m_supports_z1 = false;
        break;
      case eWatchpointWrite:
        m_supports_z2 = false;
        break;
      case eWatchpointRead:
        m_supports_z3 = false;
        break;
      case eWatchpointReadWrite:
        m_supports_z4 = false;
        break;
      case eStoppointInvalid:
        return UINT8_MAX;
      }
    }
  }
  // Signal generic failure
  return UINT8_MAX;
}

size_t GDBRemoteCommunicationClient::GetCurrentThreadIDs(
    std::vector<lldb::tid_t> &thread_ids, bool &sequence_mutex_unavailable) {
  thread_ids.clear();

  Lock lock(*this, false);
  if (lock) {
    sequence_mutex_unavailable = false;
    StringExtractorGDBRemote response;

    PacketResult packet_result;
    for (packet_result =
             SendPacketAndWaitForResponseNoLock("qfThreadInfo", response);
         packet_result == PacketResult::Success && response.IsNormalResponse();
         packet_result =
             SendPacketAndWaitForResponseNoLock("qsThreadInfo", response)) {
      char ch = response.GetChar();
      if (ch == 'l')
        break;
      if (ch == 'm') {
        do {
          tid_t tid = response.GetHexMaxU64(false, LLDB_INVALID_THREAD_ID);

          if (tid != LLDB_INVALID_THREAD_ID) {
            thread_ids.push_back(tid);
          }
          ch = response.GetChar(); // Skip the command separator
        } while (ch == ',');       // Make sure we got a comma separator
      }
    }

    /*
     * Connected bare-iron target (like YAMON gdb-stub) may not have support for
     * qProcessInfo, qC and qfThreadInfo packets. The reply from '?' packet
     * could
     * be as simple as 'S05'. There is no packet which can give us pid and/or
     * tid.
     * Assume pid=tid=1 in such cases.
    */
    if ((response.IsUnsupportedResponse() || response.IsNormalResponse()) &&
        thread_ids.size() == 0 && IsConnected()) {
      thread_ids.push_back(1);
    }
  } else {
#if !defined(LLDB_CONFIGURATION_DEBUG)
    Log *log(ProcessGDBRemoteLog::GetLogIfAnyCategoryIsSet(GDBR_LOG_PROCESS |
                                                           GDBR_LOG_PACKETS));
    if (log)
      log->Printf("error: failed to get packet sequence mutex, not sending "
                  "packet 'qfThreadInfo'");
#endif
    sequence_mutex_unavailable = true;
  }
  return thread_ids.size();
}

lldb::addr_t GDBRemoteCommunicationClient::GetShlibInfoAddr() {
  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse("qShlibInfoAddr", response, false) !=
          PacketResult::Success ||
      !response.IsNormalResponse())
    return LLDB_INVALID_ADDRESS;
  return response.GetHexMaxU64(false, LLDB_INVALID_ADDRESS);
}

lldb_private::Status GDBRemoteCommunicationClient::RunShellCommand(
    const char *command, // Shouldn't be NULL
    const FileSpec &
        working_dir, // Pass empty FileSpec to use the current working directory
    int *status_ptr, // Pass NULL if you don't want the process exit status
    int *signo_ptr,  // Pass NULL if you don't want the signal that caused the
                     // process to exit
    std::string
        *command_output, // Pass NULL if you don't want the command output
    const Timeout<std::micro> &timeout) {
  lldb_private::StreamString stream;
  stream.PutCString("qPlatform_shell:");
  stream.PutBytesAsRawHex8(command, strlen(command));
  stream.PutChar(',');
  uint32_t timeout_sec = UINT32_MAX;
  if (timeout) {
    // TODO: Use chrono version of std::ceil once c++17 is available.
    timeout_sec = std::ceil(std::chrono::duration<double>(*timeout).count());
  }
  stream.PutHex32(timeout_sec);
  if (working_dir) {
    std::string path{working_dir.GetPath(false)};
    stream.PutChar(',');
    stream.PutCStringAsRawHex8(path.c_str());
  }
  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse(stream.GetString(), response, false) ==
      PacketResult::Success) {
    if (response.GetChar() != 'F')
      return Status("malformed reply");
    if (response.GetChar() != ',')
      return Status("malformed reply");
    uint32_t exitcode = response.GetHexMaxU32(false, UINT32_MAX);
    if (exitcode == UINT32_MAX)
      return Status("unable to run remote process");
    else if (status_ptr)
      *status_ptr = exitcode;
    if (response.GetChar() != ',')
      return Status("malformed reply");
    uint32_t signo = response.GetHexMaxU32(false, UINT32_MAX);
    if (signo_ptr)
      *signo_ptr = signo;
    if (response.GetChar() != ',')
      return Status("malformed reply");
    std::string output;
    response.GetEscapedBinaryData(output);
    if (command_output)
      command_output->assign(output);
    return Status();
  }
  return Status("unable to send packet");
}

Status GDBRemoteCommunicationClient::MakeDirectory(const FileSpec &file_spec,
                                                   uint32_t file_permissions) {
  std::string path{file_spec.GetPath(false)};
  lldb_private::StreamString stream;
  stream.PutCString("qPlatform_mkdir:");
  stream.PutHex32(file_permissions);
  stream.PutChar(',');
  stream.PutCStringAsRawHex8(path.c_str());
  llvm::StringRef packet = stream.GetString();
  StringExtractorGDBRemote response;

  if (SendPacketAndWaitForResponse(packet, response, false) !=
      PacketResult::Success)
    return Status("failed to send '%s' packet", packet.str().c_str());

  if (response.GetChar() != 'F')
    return Status("invalid response to '%s' packet", packet.str().c_str());

  return Status(response.GetU32(UINT32_MAX), eErrorTypePOSIX);
}

Status
GDBRemoteCommunicationClient::SetFilePermissions(const FileSpec &file_spec,
                                                 uint32_t file_permissions) {
  std::string path{file_spec.GetPath(false)};
  lldb_private::StreamString stream;
  stream.PutCString("qPlatform_chmod:");
  stream.PutHex32(file_permissions);
  stream.PutChar(',');
  stream.PutCStringAsRawHex8(path.c_str());
  llvm::StringRef packet = stream.GetString();
  StringExtractorGDBRemote response;

  if (SendPacketAndWaitForResponse(packet, response, false) !=
      PacketResult::Success)
    return Status("failed to send '%s' packet", stream.GetData());

  if (response.GetChar() != 'F')
    return Status("invalid response to '%s' packet", stream.GetData());

  return Status(response.GetU32(UINT32_MAX), eErrorTypePOSIX);
}

static uint64_t ParseHostIOPacketResponse(StringExtractorGDBRemote &response,
                                          uint64_t fail_result, Status &error) {
  response.SetFilePos(0);
  if (response.GetChar() != 'F')
    return fail_result;
  int32_t result = response.GetS32(-2);
  if (result == -2)
    return fail_result;
  if (response.GetChar() == ',') {
    int result_errno = response.GetS32(-2);
    if (result_errno != -2)
      error.SetError(result_errno, eErrorTypePOSIX);
    else
      error.SetError(-1, eErrorTypeGeneric);
  } else
    error.Clear();
  return result;
}
lldb::user_id_t
GDBRemoteCommunicationClient::OpenFile(const lldb_private::FileSpec &file_spec,
                                       uint32_t flags, mode_t mode,
                                       Status &error) {
  std::string path(file_spec.GetPath(false));
  lldb_private::StreamString stream;
  stream.PutCString("vFile:open:");
  if (path.empty())
    return UINT64_MAX;
  stream.PutCStringAsRawHex8(path.c_str());
  stream.PutChar(',');
  stream.PutHex32(flags);
  stream.PutChar(',');
  stream.PutHex32(mode);
  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse(stream.GetString(), response, false) ==
      PacketResult::Success) {
    return ParseHostIOPacketResponse(response, UINT64_MAX, error);
  }
  return UINT64_MAX;
}

bool GDBRemoteCommunicationClient::CloseFile(lldb::user_id_t fd,
                                             Status &error) {
  lldb_private::StreamString stream;
  stream.Printf("vFile:close:%i", (int)fd);
  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse(stream.GetString(), response, false) ==
      PacketResult::Success) {
    return ParseHostIOPacketResponse(response, -1, error) == 0;
  }
  return false;
}

// Extension of host I/O packets to get the file size.
lldb::user_id_t GDBRemoteCommunicationClient::GetFileSize(
    const lldb_private::FileSpec &file_spec) {
  std::string path(file_spec.GetPath(false));
  lldb_private::StreamString stream;
  stream.PutCString("vFile:size:");
  stream.PutCStringAsRawHex8(path.c_str());
  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse(stream.GetString(), response, false) ==
      PacketResult::Success) {
    if (response.GetChar() != 'F')
      return UINT64_MAX;
    uint32_t retcode = response.GetHexMaxU64(false, UINT64_MAX);
    return retcode;
  }
  return UINT64_MAX;
}

Status
GDBRemoteCommunicationClient::GetFilePermissions(const FileSpec &file_spec,
                                                 uint32_t &file_permissions) {
  std::string path{file_spec.GetPath(false)};
  Status error;
  lldb_private::StreamString stream;
  stream.PutCString("vFile:mode:");
  stream.PutCStringAsRawHex8(path.c_str());
  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse(stream.GetString(), response, false) ==
      PacketResult::Success) {
    if (response.GetChar() != 'F') {
      error.SetErrorStringWithFormat("invalid response to '%s' packet",
                                     stream.GetData());
    } else {
      const uint32_t mode = response.GetS32(-1);
      if (static_cast<int32_t>(mode) == -1) {
        if (response.GetChar() == ',') {
          int response_errno = response.GetS32(-1);
          if (response_errno > 0)
            error.SetError(response_errno, lldb::eErrorTypePOSIX);
          else
            error.SetErrorToGenericError();
        } else
          error.SetErrorToGenericError();
      } else {
        file_permissions = mode & (S_IRWXU | S_IRWXG | S_IRWXO);
      }
    }
  } else {
    error.SetErrorStringWithFormat("failed to send '%s' packet",
                                   stream.GetData());
  }
  return error;
}

uint64_t GDBRemoteCommunicationClient::ReadFile(lldb::user_id_t fd,
                                                uint64_t offset, void *dst,
                                                uint64_t dst_len,
                                                Status &error) {
  lldb_private::StreamString stream;
  stream.Printf("vFile:pread:%i,%" PRId64 ",%" PRId64, (int)fd, dst_len,
                offset);
  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse(stream.GetString(), response, false) ==
      PacketResult::Success) {
    if (response.GetChar() != 'F')
      return 0;
    uint32_t retcode = response.GetHexMaxU32(false, UINT32_MAX);
    if (retcode == UINT32_MAX)
      return retcode;
    const char next = (response.Peek() ? *response.Peek() : 0);
    if (next == ',')
      return 0;
    if (next == ';') {
      response.GetChar(); // skip the semicolon
      std::string buffer;
      if (response.GetEscapedBinaryData(buffer)) {
        const uint64_t data_to_write =
            std::min<uint64_t>(dst_len, buffer.size());
        if (data_to_write > 0)
          memcpy(dst, &buffer[0], data_to_write);
        return data_to_write;
      }
    }
  }
  return 0;
}

uint64_t GDBRemoteCommunicationClient::WriteFile(lldb::user_id_t fd,
                                                 uint64_t offset,
                                                 const void *src,
                                                 uint64_t src_len,
                                                 Status &error) {
  lldb_private::StreamGDBRemote stream;
  stream.Printf("vFile:pwrite:%i,%" PRId64 ",", (int)fd, offset);
  stream.PutEscapedBytes(src, src_len);
  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse(stream.GetString(), response, false) ==
      PacketResult::Success) {
    if (response.GetChar() != 'F') {
      error.SetErrorStringWithFormat("write file failed");
      return 0;
    }
    uint64_t bytes_written = response.GetU64(UINT64_MAX);
    if (bytes_written == UINT64_MAX) {
      error.SetErrorToGenericError();
      if (response.GetChar() == ',') {
        int response_errno = response.GetS32(-1);
        if (response_errno > 0)
          error.SetError(response_errno, lldb::eErrorTypePOSIX);
      }
      return 0;
    }
    return bytes_written;
  } else {
    error.SetErrorString("failed to send vFile:pwrite packet");
  }
  return 0;
}

Status GDBRemoteCommunicationClient::CreateSymlink(const FileSpec &src,
                                                   const FileSpec &dst) {
  std::string src_path{src.GetPath(false)}, dst_path{dst.GetPath(false)};
  Status error;
  lldb_private::StreamGDBRemote stream;
  stream.PutCString("vFile:symlink:");
  // the unix symlink() command reverses its parameters where the dst if first,
  // so we follow suit here
  stream.PutCStringAsRawHex8(dst_path.c_str());
  stream.PutChar(',');
  stream.PutCStringAsRawHex8(src_path.c_str());
  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse(stream.GetString(), response, false) ==
      PacketResult::Success) {
    if (response.GetChar() == 'F') {
      uint32_t result = response.GetU32(UINT32_MAX);
      if (result != 0) {
        error.SetErrorToGenericError();
        if (response.GetChar() == ',') {
          int response_errno = response.GetS32(-1);
          if (response_errno > 0)
            error.SetError(response_errno, lldb::eErrorTypePOSIX);
        }
      }
    } else {
      // Should have returned with 'F<result>[,<errno>]'
      error.SetErrorStringWithFormat("symlink failed");
    }
  } else {
    error.SetErrorString("failed to send vFile:symlink packet");
  }
  return error;
}

Status GDBRemoteCommunicationClient::Unlink(const FileSpec &file_spec) {
  std::string path{file_spec.GetPath(false)};
  Status error;
  lldb_private::StreamGDBRemote stream;
  stream.PutCString("vFile:unlink:");
  // the unix symlink() command reverses its parameters where the dst if first,
  // so we follow suit here
  stream.PutCStringAsRawHex8(path.c_str());
  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse(stream.GetString(), response, false) ==
      PacketResult::Success) {
    if (response.GetChar() == 'F') {
      uint32_t result = response.GetU32(UINT32_MAX);
      if (result != 0) {
        error.SetErrorToGenericError();
        if (response.GetChar() == ',') {
          int response_errno = response.GetS32(-1);
          if (response_errno > 0)
            error.SetError(response_errno, lldb::eErrorTypePOSIX);
        }
      }
    } else {
      // Should have returned with 'F<result>[,<errno>]'
      error.SetErrorStringWithFormat("unlink failed");
    }
  } else {
    error.SetErrorString("failed to send vFile:unlink packet");
  }
  return error;
}

// Extension of host I/O packets to get whether a file exists.
bool GDBRemoteCommunicationClient::GetFileExists(
    const lldb_private::FileSpec &file_spec) {
  std::string path(file_spec.GetPath(false));
  lldb_private::StreamString stream;
  stream.PutCString("vFile:exists:");
  stream.PutCStringAsRawHex8(path.c_str());
  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse(stream.GetString(), response, false) ==
      PacketResult::Success) {
    if (response.GetChar() != 'F')
      return false;
    if (response.GetChar() != ',')
      return false;
    bool retcode = (response.GetChar() != '0');
    return retcode;
  }
  return false;
}

bool GDBRemoteCommunicationClient::CalculateMD5(
    const lldb_private::FileSpec &file_spec, uint64_t &high, uint64_t &low) {
  std::string path(file_spec.GetPath(false));
  lldb_private::StreamString stream;
  stream.PutCString("vFile:MD5:");
  stream.PutCStringAsRawHex8(path.c_str());
  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse(stream.GetString(), response, false) ==
      PacketResult::Success) {
    if (response.GetChar() != 'F')
      return false;
    if (response.GetChar() != ',')
      return false;
    if (response.Peek() && *response.Peek() == 'x')
      return false;
    low = response.GetHexMaxU64(false, UINT64_MAX);
    high = response.GetHexMaxU64(false, UINT64_MAX);
    return true;
  }
  return false;
}

bool GDBRemoteCommunicationClient::AvoidGPackets(ProcessGDBRemote *process) {
  // Some targets have issues with g/G packets and we need to avoid using them
  if (m_avoid_g_packets == eLazyBoolCalculate) {
    if (process) {
      m_avoid_g_packets = eLazyBoolNo;
      const ArchSpec &arch = process->GetTarget().GetArchitecture();
      if (arch.IsValid() &&
          arch.GetTriple().getVendor() == llvm::Triple::Apple &&
          arch.GetTriple().getOS() == llvm::Triple::IOS &&
          arch.GetTriple().getArch() == llvm::Triple::aarch64) {
        m_avoid_g_packets = eLazyBoolYes;
        uint32_t gdb_server_version = GetGDBServerProgramVersion();
        if (gdb_server_version != 0) {
          const char *gdb_server_name = GetGDBServerProgramName();
          if (gdb_server_name && strcmp(gdb_server_name, "debugserver") == 0) {
            if (gdb_server_version >= 310)
              m_avoid_g_packets = eLazyBoolNo;
          }
        }
      }
    }
  }
  return m_avoid_g_packets == eLazyBoolYes;
}

DataBufferSP GDBRemoteCommunicationClient::ReadRegister(lldb::tid_t tid,
                                                        uint32_t reg) {
  StreamString payload;
  payload.Printf("p%x", reg);
  StringExtractorGDBRemote response;
  if (SendThreadSpecificPacketAndWaitForResponse(
          tid, std::move(payload), response, false) != PacketResult::Success ||
      !response.IsNormalResponse())
    return nullptr;

  DataBufferSP buffer_sp(
      new DataBufferHeap(response.GetStringRef().size() / 2, 0));
  response.GetHexBytes(buffer_sp->GetData(), '\xcc');
  return buffer_sp;
}

DataBufferSP GDBRemoteCommunicationClient::ReadAllRegisters(lldb::tid_t tid) {
  StreamString payload;
  payload.PutChar('g');
  StringExtractorGDBRemote response;
  if (SendThreadSpecificPacketAndWaitForResponse(
          tid, std::move(payload), response, false) != PacketResult::Success ||
      !response.IsNormalResponse())
    return nullptr;

  DataBufferSP buffer_sp(
      new DataBufferHeap(response.GetStringRef().size() / 2, 0));
  response.GetHexBytes(buffer_sp->GetData(), '\xcc');
  return buffer_sp;
}

bool GDBRemoteCommunicationClient::WriteRegister(lldb::tid_t tid,
                                                 uint32_t reg_num,
                                                 llvm::ArrayRef<uint8_t> data) {
  StreamString payload;
  payload.Printf("P%x=", reg_num);
  payload.PutBytesAsRawHex8(data.data(), data.size(),
                            endian::InlHostByteOrder(),
                            endian::InlHostByteOrder());
  StringExtractorGDBRemote response;
  return SendThreadSpecificPacketAndWaitForResponse(tid, std::move(payload),
                                                    response, false) ==
             PacketResult::Success &&
         response.IsOKResponse();
}

bool GDBRemoteCommunicationClient::WriteAllRegisters(
    lldb::tid_t tid, llvm::ArrayRef<uint8_t> data) {
  StreamString payload;
  payload.PutChar('G');
  payload.PutBytesAsRawHex8(data.data(), data.size(),
                            endian::InlHostByteOrder(),
                            endian::InlHostByteOrder());
  StringExtractorGDBRemote response;
  return SendThreadSpecificPacketAndWaitForResponse(tid, std::move(payload),
                                                    response, false) ==
             PacketResult::Success &&
         response.IsOKResponse();
}

bool GDBRemoteCommunicationClient::SaveRegisterState(lldb::tid_t tid,
                                                     uint32_t &save_id) {
  save_id = 0; // Set to invalid save ID
  if (m_supports_QSaveRegisterState == eLazyBoolNo)
    return false;

  m_supports_QSaveRegisterState = eLazyBoolYes;
  StreamString payload;
  payload.PutCString("QSaveRegisterState");
  StringExtractorGDBRemote response;
  if (SendThreadSpecificPacketAndWaitForResponse(
          tid, std::move(payload), response, false) != PacketResult::Success)
    return false;

  if (response.IsUnsupportedResponse())
    m_supports_QSaveRegisterState = eLazyBoolNo;

  const uint32_t response_save_id = response.GetU32(0);
  if (response_save_id == 0)
    return false;

  save_id = response_save_id;
  return true;
}

bool GDBRemoteCommunicationClient::RestoreRegisterState(lldb::tid_t tid,
                                                        uint32_t save_id) {
  // We use the "m_supports_QSaveRegisterState" variable here because the
  // QSaveRegisterState and QRestoreRegisterState packets must both be
  // supported in order to be useful
  if (m_supports_QSaveRegisterState == eLazyBoolNo)
    return false;

  StreamString payload;
  payload.Printf("QRestoreRegisterState:%u", save_id);
  StringExtractorGDBRemote response;
  if (SendThreadSpecificPacketAndWaitForResponse(
          tid, std::move(payload), response, false) != PacketResult::Success)
    return false;

  if (response.IsOKResponse())
    return true;

  if (response.IsUnsupportedResponse())
    m_supports_QSaveRegisterState = eLazyBoolNo;
  return false;
}

bool GDBRemoteCommunicationClient::SyncThreadState(lldb::tid_t tid) {
  if (!GetSyncThreadStateSupported())
    return false;

  StreamString packet;
  StringExtractorGDBRemote response;
  packet.Printf("QSyncThreadState:%4.4" PRIx64 ";", tid);
  return SendPacketAndWaitForResponse(packet.GetString(), response, false) ==
             GDBRemoteCommunication::PacketResult::Success &&
         response.IsOKResponse();
}

lldb::user_id_t
GDBRemoteCommunicationClient::SendStartTracePacket(const TraceOptions &options,
                                                   Status &error) {
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
  lldb::user_id_t ret_uid = LLDB_INVALID_UID;

  StreamGDBRemote escaped_packet;
  escaped_packet.PutCString("jTraceStart:");

  StructuredData::Dictionary json_packet;
  json_packet.AddIntegerItem("type", options.getType());
  json_packet.AddIntegerItem("buffersize", options.getTraceBufferSize());
  json_packet.AddIntegerItem("metabuffersize", options.getMetaDataBufferSize());

  if (options.getThreadID() != LLDB_INVALID_THREAD_ID)
    json_packet.AddIntegerItem("threadid", options.getThreadID());

  StructuredData::DictionarySP custom_params = options.getTraceParams();
  if (custom_params)
    json_packet.AddItem("params", custom_params);

  StreamString json_string;
  json_packet.Dump(json_string, false);
  escaped_packet.PutEscapedBytes(json_string.GetData(), json_string.GetSize());

  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse(escaped_packet.GetString(), response,
                                   true) ==
      GDBRemoteCommunication::PacketResult::Success) {
    if (!response.IsNormalResponse()) {
      error = response.GetStatus();
      LLDB_LOG(log, "Target does not support Tracing , error {0}", error);
    } else {
      ret_uid = response.GetHexMaxU64(false, LLDB_INVALID_UID);
    }
  } else {
    LLDB_LOG(log, "failed to send packet");
    error.SetErrorStringWithFormat("failed to send packet: '%s'",
                                   escaped_packet.GetData());
  }
  return ret_uid;
}

Status
GDBRemoteCommunicationClient::SendStopTracePacket(lldb::user_id_t uid,
                                                  lldb::tid_t thread_id) {
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
  StringExtractorGDBRemote response;
  Status error;

  StructuredData::Dictionary json_packet;
  StreamGDBRemote escaped_packet;
  StreamString json_string;
  escaped_packet.PutCString("jTraceStop:");

  json_packet.AddIntegerItem("traceid", uid);

  if (thread_id != LLDB_INVALID_THREAD_ID)
    json_packet.AddIntegerItem("threadid", thread_id);

  json_packet.Dump(json_string, false);

  escaped_packet.PutEscapedBytes(json_string.GetData(), json_string.GetSize());

  if (SendPacketAndWaitForResponse(escaped_packet.GetString(), response,
                                   true) ==
      GDBRemoteCommunication::PacketResult::Success) {
    if (!response.IsOKResponse()) {
      error = response.GetStatus();
      LLDB_LOG(log, "stop tracing failed");
    }
  } else {
    LLDB_LOG(log, "failed to send packet");
    error.SetErrorStringWithFormat(
        "failed to send packet: '%s' with error '%d'", escaped_packet.GetData(),
        response.GetError());
  }
  return error;
}

Status GDBRemoteCommunicationClient::SendGetDataPacket(
    lldb::user_id_t uid, lldb::tid_t thread_id,
    llvm::MutableArrayRef<uint8_t> &buffer, size_t offset) {

  StreamGDBRemote escaped_packet;
  escaped_packet.PutCString("jTraceBufferRead:");
  return SendGetTraceDataPacket(escaped_packet, uid, thread_id, buffer, offset);
}

Status GDBRemoteCommunicationClient::SendGetMetaDataPacket(
    lldb::user_id_t uid, lldb::tid_t thread_id,
    llvm::MutableArrayRef<uint8_t> &buffer, size_t offset) {

  StreamGDBRemote escaped_packet;
  escaped_packet.PutCString("jTraceMetaRead:");
  return SendGetTraceDataPacket(escaped_packet, uid, thread_id, buffer, offset);
}

Status
GDBRemoteCommunicationClient::SendGetTraceConfigPacket(lldb::user_id_t uid,
                                                       TraceOptions &options) {
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
  StringExtractorGDBRemote response;
  Status error;

  StreamString json_string;
  StreamGDBRemote escaped_packet;
  escaped_packet.PutCString("jTraceConfigRead:");

  StructuredData::Dictionary json_packet;
  json_packet.AddIntegerItem("traceid", uid);

  if (options.getThreadID() != LLDB_INVALID_THREAD_ID)
    json_packet.AddIntegerItem("threadid", options.getThreadID());

  json_packet.Dump(json_string, false);
  escaped_packet.PutEscapedBytes(json_string.GetData(), json_string.GetSize());

  if (SendPacketAndWaitForResponse(escaped_packet.GetString(), response,
                                   true) ==
      GDBRemoteCommunication::PacketResult::Success) {
    if (response.IsNormalResponse()) {
      uint64_t type = std::numeric_limits<uint64_t>::max();
      uint64_t buffersize = std::numeric_limits<uint64_t>::max();
      uint64_t metabuffersize = std::numeric_limits<uint64_t>::max();

      auto json_object = StructuredData::ParseJSON(response.Peek());

      if (!json_object ||
          json_object->GetType() != lldb::eStructuredDataTypeDictionary) {
        error.SetErrorString("Invalid Configuration obtained");
        return error;
      }

      auto json_dict = json_object->GetAsDictionary();

      json_dict->GetValueForKeyAsInteger<uint64_t>("metabuffersize",
                                                   metabuffersize);
      options.setMetaDataBufferSize(metabuffersize);

      json_dict->GetValueForKeyAsInteger<uint64_t>("buffersize", buffersize);
      options.setTraceBufferSize(buffersize);

      json_dict->GetValueForKeyAsInteger<uint64_t>("type", type);
      options.setType(static_cast<lldb::TraceType>(type));

      StructuredData::ObjectSP custom_params_sp =
          json_dict->GetValueForKey("params");
      if (custom_params_sp) {
        if (custom_params_sp->GetType() !=
            lldb::eStructuredDataTypeDictionary) {
          error.SetErrorString("Invalid Configuration obtained");
          return error;
        } else
          options.setTraceParams(
              static_pointer_cast<StructuredData::Dictionary>(
                  custom_params_sp));
      }
    } else {
      error = response.GetStatus();
    }
  } else {
    LLDB_LOG(log, "failed to send packet");
    error.SetErrorStringWithFormat("failed to send packet: '%s'",
                                   escaped_packet.GetData());
  }
  return error;
}

Status GDBRemoteCommunicationClient::SendGetTraceDataPacket(
    StreamGDBRemote &packet, lldb::user_id_t uid, lldb::tid_t thread_id,
    llvm::MutableArrayRef<uint8_t> &buffer, size_t offset) {
  Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));
  Status error;

  StructuredData::Dictionary json_packet;

  json_packet.AddIntegerItem("traceid", uid);
  json_packet.AddIntegerItem("offset", offset);
  json_packet.AddIntegerItem("buffersize", buffer.size());

  if (thread_id != LLDB_INVALID_THREAD_ID)
    json_packet.AddIntegerItem("threadid", thread_id);

  StreamString json_string;
  json_packet.Dump(json_string, false);

  packet.PutEscapedBytes(json_string.GetData(), json_string.GetSize());
  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse(packet.GetString(), response, true) ==
      GDBRemoteCommunication::PacketResult::Success) {
    if (response.IsNormalResponse()) {
      size_t filled_size = response.GetHexBytesAvail(buffer);
      buffer = llvm::MutableArrayRef<uint8_t>(buffer.data(), filled_size);
    } else {
      error = response.GetStatus();
      buffer = buffer.slice(buffer.size());
    }
  } else {
    LLDB_LOG(log, "failed to send packet");
    error.SetErrorStringWithFormat("failed to send packet: '%s'",
                                   packet.GetData());
    buffer = buffer.slice(buffer.size());
  }
  return error;
}

bool GDBRemoteCommunicationClient::GetModuleInfo(
    const FileSpec &module_file_spec, const lldb_private::ArchSpec &arch_spec,
    ModuleSpec &module_spec) {
  if (!m_supports_qModuleInfo)
    return false;

  std::string module_path = module_file_spec.GetPath(false);
  if (module_path.empty())
    return false;

  StreamString packet;
  packet.PutCString("qModuleInfo:");
  packet.PutCStringAsRawHex8(module_path.c_str());
  packet.PutCString(";");
  const auto &triple = arch_spec.GetTriple().getTriple();
  packet.PutCStringAsRawHex8(triple.c_str());

  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse(packet.GetString(), response, false) !=
      PacketResult::Success)
    return false;

  if (response.IsErrorResponse())
    return false;

  if (response.IsUnsupportedResponse()) {
    m_supports_qModuleInfo = false;
    return false;
  }

  llvm::StringRef name;
  llvm::StringRef value;

  module_spec.Clear();
  module_spec.GetFileSpec() = module_file_spec;

  while (response.GetNameColonValue(name, value)) {
    if (name == "uuid" || name == "md5") {
      StringExtractor extractor(value);
      std::string uuid;
      extractor.GetHexByteString(uuid);
      module_spec.GetUUID().SetFromStringRef(uuid, uuid.size() / 2);
    } else if (name == "triple") {
      StringExtractor extractor(value);
      std::string triple;
      extractor.GetHexByteString(triple);
      module_spec.GetArchitecture().SetTriple(triple.c_str());
    } else if (name == "file_offset") {
      uint64_t ival = 0;
      if (!value.getAsInteger(16, ival))
        module_spec.SetObjectOffset(ival);
    } else if (name == "file_size") {
      uint64_t ival = 0;
      if (!value.getAsInteger(16, ival))
        module_spec.SetObjectSize(ival);
    } else if (name == "file_path") {
      StringExtractor extractor(value);
      std::string path;
      extractor.GetHexByteString(path);
      module_spec.GetFileSpec() = FileSpec(path, arch_spec.GetTriple());
    }
  }

  return true;
}

static llvm::Optional<ModuleSpec>
ParseModuleSpec(StructuredData::Dictionary *dict) {
  ModuleSpec result;
  if (!dict)
    return llvm::None;

  llvm::StringRef string;
  uint64_t integer;

  if (!dict->GetValueForKeyAsString("uuid", string))
    return llvm::None;
  if (result.GetUUID().SetFromStringRef(string, string.size() / 2) !=
      string.size())
    return llvm::None;

  if (!dict->GetValueForKeyAsInteger("file_offset", integer))
    return llvm::None;
  result.SetObjectOffset(integer);

  if (!dict->GetValueForKeyAsInteger("file_size", integer))
    return llvm::None;
  result.SetObjectSize(integer);

  if (!dict->GetValueForKeyAsString("triple", string))
    return llvm::None;
  result.GetArchitecture().SetTriple(string);

  if (!dict->GetValueForKeyAsString("file_path", string))
    return llvm::None;
  result.GetFileSpec() = FileSpec(string, result.GetArchitecture().GetTriple());

  return result;
}

llvm::Optional<std::vector<ModuleSpec>>
GDBRemoteCommunicationClient::GetModulesInfo(
    llvm::ArrayRef<FileSpec> module_file_specs, const llvm::Triple &triple) {
  if (!m_supports_jModulesInfo)
    return llvm::None;

  JSONArray::SP module_array_sp = std::make_shared<JSONArray>();
  for (const FileSpec &module_file_spec : module_file_specs) {
    JSONObject::SP module_sp = std::make_shared<JSONObject>();
    module_array_sp->AppendObject(module_sp);
    module_sp->SetObject(
        "file", std::make_shared<JSONString>(module_file_spec.GetPath(false)));
    module_sp->SetObject("triple",
                         std::make_shared<JSONString>(triple.getTriple()));
  }
  StreamString unescaped_payload;
  unescaped_payload.PutCString("jModulesInfo:");
  module_array_sp->Write(unescaped_payload);
  StreamGDBRemote payload;
  payload.PutEscapedBytes(unescaped_payload.GetString().data(),
                          unescaped_payload.GetSize());

  // Increase the timeout for jModulesInfo since this packet can take longer.
  ScopedTimeout timeout(*this, std::chrono::seconds(10));

  StringExtractorGDBRemote response;
  if (SendPacketAndWaitForResponse(payload.GetString(), response, false) !=
          PacketResult::Success ||
      response.IsErrorResponse())
    return llvm::None;

  if (response.IsUnsupportedResponse()) {
    m_supports_jModulesInfo = false;
    return llvm::None;
  }

  StructuredData::ObjectSP response_object_sp =
      StructuredData::ParseJSON(response.GetStringRef());
  if (!response_object_sp)
    return llvm::None;

  StructuredData::Array *response_array = response_object_sp->GetAsArray();
  if (!response_array)
    return llvm::None;

  std::vector<ModuleSpec> result;
  for (size_t i = 0; i < response_array->GetSize(); ++i) {
    if (llvm::Optional<ModuleSpec> module_spec = ParseModuleSpec(
            response_array->GetItemAtIndex(i)->GetAsDictionary()))
      result.push_back(*module_spec);
  }

  return result;
}

// query the target remote for extended information using the qXfer packet
//
// example: object='features', annex='target.xml', out=<xml output> return:
// 'true'  on success
//          'false' on failure (err set)
bool GDBRemoteCommunicationClient::ReadExtFeature(
    const lldb_private::ConstString object,
    const lldb_private::ConstString annex, std::string &out,
    lldb_private::Status &err) {

  std::stringstream output;
  StringExtractorGDBRemote chunk;

  uint64_t size = GetRemoteMaxPacketSize();
  if (size == 0)
    size = 0x1000;
  size = size - 1; // Leave space for the 'm' or 'l' character in the response
  int offset = 0;
  bool active = true;

  // loop until all data has been read
  while (active) {

    // send query extended feature packet
    std::stringstream packet;
    packet << "qXfer:" << object.AsCString("")
           << ":read:" << annex.AsCString("") << ":" << std::hex << offset
           << "," << std::hex << size;

    GDBRemoteCommunication::PacketResult res =
        SendPacketAndWaitForResponse(packet.str(), chunk, false);

    if (res != GDBRemoteCommunication::PacketResult::Success) {
      err.SetErrorString("Error sending $qXfer packet");
      return false;
    }

    const std::string &str = chunk.GetStringRef();
    if (str.length() == 0) {
      // should have some data in chunk
      err.SetErrorString("Empty response from $qXfer packet");
      return false;
    }

    // check packet code
    switch (str[0]) {
    // last chunk
    case ('l'):
      active = false;
      LLVM_FALLTHROUGH;

    // more chunks
    case ('m'):
      if (str.length() > 1)
        output << &str[1];
      offset += size;
      break;

    // unknown chunk
    default:
      err.SetErrorString("Invalid continuation code from $qXfer packet");
      return false;
    }
  }

  out = output.str();
  err.Success();
  return true;
}

// Notify the target that gdb is prepared to serve symbol lookup requests.
//  packet: "qSymbol::"
//  reply:
//  OK                  The target does not need to look up any (more) symbols.
//  qSymbol:<sym_name>  The target requests the value of symbol sym_name (hex
//  encoded).
//                      LLDB may provide the value by sending another qSymbol
//                      packet
//                      in the form of"qSymbol:<sym_value>:<sym_name>".
//
//  Three examples:
//
//  lldb sends:    qSymbol::
//  lldb receives: OK
//     Remote gdb stub does not need to know the addresses of any symbols, lldb
//     does not
//     need to ask again in this session.
//
//  lldb sends:    qSymbol::
//  lldb receives: qSymbol:64697370617463685f71756575655f6f666673657473
//  lldb sends:    qSymbol::64697370617463685f71756575655f6f666673657473
//  lldb receives: OK
//     Remote gdb stub asks for address of 'dispatch_queue_offsets'.  lldb does
//     not know
//     the address at this time.  lldb needs to send qSymbol:: again when it has
//     more
//     solibs loaded.
//
//  lldb sends:    qSymbol::
//  lldb receives: qSymbol:64697370617463685f71756575655f6f666673657473
//  lldb sends:    qSymbol:2bc97554:64697370617463685f71756575655f6f666673657473
//  lldb receives: OK
//     Remote gdb stub asks for address of 'dispatch_queue_offsets'.  lldb says
//     that it
//     is at address 0x2bc97554.  Remote gdb stub sends 'OK' indicating that it
//     does not
//     need any more symbols.  lldb does not need to ask again in this session.

void GDBRemoteCommunicationClient::ServeSymbolLookups(
    lldb_private::Process *process) {
  // Set to true once we've resolved a symbol to an address for the remote
  // stub. If we get an 'OK' response after this, the remote stub doesn't need
  // any more symbols and we can stop asking.
  bool symbol_response_provided = false;

  // Is this the initial qSymbol:: packet?
  bool first_qsymbol_query = true;

  if (m_supports_qSymbol && !m_qSymbol_requests_done) {
    Lock lock(*this, false);
    if (lock) {
      StreamString packet;
      packet.PutCString("qSymbol::");
      StringExtractorGDBRemote response;
      while (SendPacketAndWaitForResponseNoLock(packet.GetString(), response) ==
             PacketResult::Success) {
        if (response.IsOKResponse()) {
          if (symbol_response_provided || first_qsymbol_query) {
            m_qSymbol_requests_done = true;
          }

          // We are done serving symbols requests
          return;
        }
        first_qsymbol_query = false;

        if (response.IsUnsupportedResponse()) {
          // qSymbol is not supported by the current GDB server we are
          // connected to
          m_supports_qSymbol = false;
          return;
        } else {
          llvm::StringRef response_str(response.GetStringRef());
          if (response_str.startswith("qSymbol:")) {
            response.SetFilePos(strlen("qSymbol:"));
            std::string symbol_name;
            if (response.GetHexByteString(symbol_name)) {
              if (symbol_name.empty())
                return;

              addr_t symbol_load_addr = LLDB_INVALID_ADDRESS;
              lldb_private::SymbolContextList sc_list;
              if (process->GetTarget().GetImages().FindSymbolsWithNameAndType(
                      ConstString(symbol_name), eSymbolTypeAny, sc_list)) {
                const size_t num_scs = sc_list.GetSize();
                for (size_t sc_idx = 0;
                     sc_idx < num_scs &&
                     symbol_load_addr == LLDB_INVALID_ADDRESS;
                     ++sc_idx) {
                  SymbolContext sc;
                  if (sc_list.GetContextAtIndex(sc_idx, sc)) {
                    if (sc.symbol) {
                      switch (sc.symbol->GetType()) {
                      case eSymbolTypeInvalid:
                      case eSymbolTypeAbsolute:
                      case eSymbolTypeUndefined:
                      case eSymbolTypeSourceFile:
                      case eSymbolTypeHeaderFile:
                      case eSymbolTypeObjectFile:
                      case eSymbolTypeCommonBlock:
                      case eSymbolTypeBlock:
                      case eSymbolTypeLocal:
                      case eSymbolTypeParam:
                      case eSymbolTypeVariable:
                      case eSymbolTypeVariableType:
                      case eSymbolTypeLineEntry:
                      case eSymbolTypeLineHeader:
                      case eSymbolTypeScopeBegin:
                      case eSymbolTypeScopeEnd:
                      case eSymbolTypeAdditional:
                      case eSymbolTypeCompiler:
                      case eSymbolTypeInstrumentation:
                      case eSymbolTypeTrampoline:
                        break;

                      case eSymbolTypeCode:
                      case eSymbolTypeResolver:
                      case eSymbolTypeData:
                      case eSymbolTypeRuntime:
                      case eSymbolTypeException:
                      case eSymbolTypeObjCClass:
                      case eSymbolTypeObjCMetaClass:
                      case eSymbolTypeObjCIVar:
                      case eSymbolTypeReExported:
                        symbol_load_addr =
                            sc.symbol->GetLoadAddress(&process->GetTarget());
                        break;
                      }
                    }
                  }
                }
              }
              // This is the normal path where our symbol lookup was successful
              // and we want to send a packet with the new symbol value and see
              // if another lookup needs to be done.

              // Change "packet" to contain the requested symbol value and name
              packet.Clear();
              packet.PutCString("qSymbol:");
              if (symbol_load_addr != LLDB_INVALID_ADDRESS) {
                packet.Printf("%" PRIx64, symbol_load_addr);
                symbol_response_provided = true;
              } else {
                symbol_response_provided = false;
              }
              packet.PutCString(":");
              packet.PutBytesAsRawHex8(symbol_name.data(), symbol_name.size());
              continue; // go back to the while loop and send "packet" and wait
                        // for another response
            }
          }
        }
      }
      // If we make it here, the symbol request packet response wasn't valid or
      // our symbol lookup failed so we must abort
      return;

    } else if (Log *log = ProcessGDBRemoteLog::GetLogIfAnyCategoryIsSet(
                   GDBR_LOG_PROCESS | GDBR_LOG_PACKETS)) {
      log->Printf(
          "GDBRemoteCommunicationClient::%s: Didn't get sequence mutex.",
          __FUNCTION__);
    }
  }
}

StructuredData::Array *
GDBRemoteCommunicationClient::GetSupportedStructuredDataPlugins() {
  if (!m_supported_async_json_packets_is_valid) {
    // Query the server for the array of supported asynchronous JSON packets.
    m_supported_async_json_packets_is_valid = true;

    Log *log(ProcessGDBRemoteLog::GetLogIfAllCategoriesSet(GDBR_LOG_PROCESS));

    // Poll it now.
    StringExtractorGDBRemote response;
    const bool send_async = false;
    if (SendPacketAndWaitForResponse("qStructuredDataPlugins", response,
                                     send_async) == PacketResult::Success) {
      m_supported_async_json_packets_sp =
          StructuredData::ParseJSON(response.GetStringRef());
      if (m_supported_async_json_packets_sp &&
          !m_supported_async_json_packets_sp->GetAsArray()) {
        // We were returned something other than a JSON array.  This is
        // invalid.  Clear it out.
        if (log)
          log->Printf("GDBRemoteCommunicationClient::%s(): "
                      "QSupportedAsyncJSONPackets returned invalid "
                      "result: %s",
                      __FUNCTION__, response.GetStringRef().c_str());
        m_supported_async_json_packets_sp.reset();
      }
    } else {
      if (log)
        log->Printf("GDBRemoteCommunicationClient::%s(): "
                    "QSupportedAsyncJSONPackets unsupported",
                    __FUNCTION__);
    }

    if (log && m_supported_async_json_packets_sp) {
      StreamString stream;
      m_supported_async_json_packets_sp->Dump(stream);
      log->Printf("GDBRemoteCommunicationClient::%s(): supported async "
                  "JSON packets: %s",
                  __FUNCTION__, stream.GetData());
    }
  }

  return m_supported_async_json_packets_sp
             ? m_supported_async_json_packets_sp->GetAsArray()
             : nullptr;
}

Status GDBRemoteCommunicationClient::SendSignalsToIgnore(
    llvm::ArrayRef<int32_t> signals) {
  // Format packet:
  // QPassSignals:<hex_sig1>;<hex_sig2>...;<hex_sigN>
  auto range = llvm::make_range(signals.begin(), signals.end());
  std::string packet = formatv("QPassSignals:{0:$[;]@(x-2)}", range).str();

  StringExtractorGDBRemote response;
  auto send_status = SendPacketAndWaitForResponse(packet, response, false);

  if (send_status != GDBRemoteCommunication::PacketResult::Success)
    return Status("Sending QPassSignals packet failed");

  if (response.IsOKResponse()) {
    return Status();
  } else {
    return Status("Unknown error happened during sending QPassSignals packet.");
  }
}

Status GDBRemoteCommunicationClient::ConfigureRemoteStructuredData(
    const ConstString &type_name, const StructuredData::ObjectSP &config_sp) {
  Status error;

  if (type_name.GetLength() == 0) {
    error.SetErrorString("invalid type_name argument");
    return error;
  }

  // Build command: Configure{type_name}: serialized config data.
  StreamGDBRemote stream;
  stream.PutCString("QConfigure");
  stream.PutCString(type_name.AsCString());
  stream.PutChar(':');
  if (config_sp) {
    // Gather the plain-text version of the configuration data.
    StreamString unescaped_stream;
    config_sp->Dump(unescaped_stream);
    unescaped_stream.Flush();

    // Add it to the stream in escaped fashion.
    stream.PutEscapedBytes(unescaped_stream.GetString().data(),
                           unescaped_stream.GetSize());
  }
  stream.Flush();

  // Send the packet.
  const bool send_async = false;
  StringExtractorGDBRemote response;
  auto result =
      SendPacketAndWaitForResponse(stream.GetString(), response, send_async);
  if (result == PacketResult::Success) {
    // We failed if the config result comes back other than OK.
    if (strcmp(response.GetStringRef().c_str(), "OK") == 0) {
      // Okay!
      error.Clear();
    } else {
      error.SetErrorStringWithFormat("configuring StructuredData feature "
                                     "%s failed with error %s",
                                     type_name.AsCString(),
                                     response.GetStringRef().c_str());
    }
  } else {
    // Can we get more data here on the failure?
    error.SetErrorStringWithFormat("configuring StructuredData feature %s "
                                   "failed when sending packet: "
                                   "PacketResult=%d",
                                   type_name.AsCString(), (int)result);
  }
  return error;
}

void GDBRemoteCommunicationClient::OnRunPacketSent(bool first) {
  GDBRemoteClientBase::OnRunPacketSent(first);
  m_curr_tid = LLDB_INVALID_THREAD_ID;
}
