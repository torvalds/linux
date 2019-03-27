//===-- GDBRemoteCommunicationReplayServer.h --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_GDBRemoteCommunicationReplayServer_h_
#define liblldb_GDBRemoteCommunicationReplayServer_h_

// Other libraries and framework includes
#include "GDBRemoteCommunication.h"
#include "GDBRemoteCommunicationHistory.h"

// Project includes
#include "lldb/Host/HostThread.h"
#include "lldb/Utility/Broadcaster.h"
#include "lldb/lldb-private-forward.h"
#include "llvm/Support/Error.h"

// C Includes
// C++ Includes
#include <functional>
#include <map>
#include <thread>

class StringExtractorGDBRemote;

namespace lldb_private {
namespace process_gdb_remote {

class ProcessGDBRemote;

/// Dummy GDB server that replays packets from the GDB Remote Communication
/// history. This is used to replay GDB packets.
class GDBRemoteCommunicationReplayServer : public GDBRemoteCommunication {
public:
  GDBRemoteCommunicationReplayServer();

  ~GDBRemoteCommunicationReplayServer() override;

  PacketResult GetPacketAndSendResponse(Timeout<std::micro> timeout,
                                        Status &error, bool &interrupt,
                                        bool &quit);

  bool HandshakeWithClient() { return GetAck() == PacketResult::Success; }

  llvm::Error LoadReplayHistory(const FileSpec &path);

  bool StartAsyncThread();
  void StopAsyncThread();

protected:
  enum {
    eBroadcastBitAsyncContinue = (1 << 0),
    eBroadcastBitAsyncThreadShouldExit = (1 << 1),
  };

  static void ReceivePacket(GDBRemoteCommunicationReplayServer &server,
                            bool &done);
  static lldb::thread_result_t AsyncThread(void *arg);

  /// Replay history with the oldest packet at the end.
  std::vector<GDBRemoteCommunicationHistory::Entry> m_packet_history;

  /// Server thread.
  Broadcaster m_async_broadcaster;
  lldb::ListenerSP m_async_listener_sp;
  HostThread m_async_thread;
  std::recursive_mutex m_async_thread_state_mutex;

  bool m_skip_acks;

private:
  DISALLOW_COPY_AND_ASSIGN(GDBRemoteCommunicationReplayServer);
};

} // namespace process_gdb_remote
} // namespace lldb_private

#endif // liblldb_GDBRemoteCommunicationReplayServer_h_
