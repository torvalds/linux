//===-- GDBRemoteClientBase.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_GDBRemoteClientBase_h_
#define liblldb_GDBRemoteClientBase_h_

#include "GDBRemoteCommunication.h"

#include <condition_variable>

namespace lldb_private {
namespace process_gdb_remote {

class GDBRemoteClientBase : public GDBRemoteCommunication {
public:
  struct ContinueDelegate {
    virtual ~ContinueDelegate();
    virtual void HandleAsyncStdout(llvm::StringRef out) = 0;
    virtual void HandleAsyncMisc(llvm::StringRef data) = 0;
    virtual void HandleStopReply() = 0;

    // =========================================================================
    /// Process asynchronously-received structured data.
    ///
    /// @param[in] data
    ///   The complete data packet, expected to start with JSON-async.
    // =========================================================================
    virtual void HandleAsyncStructuredDataPacket(llvm::StringRef data) = 0;
  };

  GDBRemoteClientBase(const char *comm_name, const char *listener_name);

  bool SendAsyncSignal(int signo);

  bool Interrupt();

  lldb::StateType SendContinuePacketAndWaitForResponse(
      ContinueDelegate &delegate, const UnixSignals &signals,
      llvm::StringRef payload, StringExtractorGDBRemote &response);

  PacketResult SendPacketAndWaitForResponse(llvm::StringRef payload,
                                            StringExtractorGDBRemote &response,
                                            bool send_async);

  PacketResult SendPacketAndReceiveResponseWithOutputSupport(
      llvm::StringRef payload, StringExtractorGDBRemote &response,
      bool send_async,
      llvm::function_ref<void(llvm::StringRef)> output_callback);

  bool SendvContPacket(llvm::StringRef payload,
                       StringExtractorGDBRemote &response);

  class Lock {
  public:
    Lock(GDBRemoteClientBase &comm, bool interrupt);
    ~Lock();

    explicit operator bool() { return m_acquired; }

    // Whether we had to interrupt the continue thread to acquire the
    // connection.
    bool DidInterrupt() const { return m_did_interrupt; }

  private:
    std::unique_lock<std::recursive_mutex> m_async_lock;
    GDBRemoteClientBase &m_comm;
    bool m_acquired;
    bool m_did_interrupt;

    void SyncWithContinueThread(bool interrupt);
  };

protected:
  PacketResult
  SendPacketAndWaitForResponseNoLock(llvm::StringRef payload,
                                     StringExtractorGDBRemote &response);

  virtual void OnRunPacketSent(bool first);

private:
  // Variables handling synchronization between the Continue thread and any
  // other threads
  // wishing to send packets over the connection. Either the continue thread has
  // control over
  // the connection (m_is_running == true) or the connection is free for an
  // arbitrary number of
  // other senders to take which indicate their interest by incrementing
  // m_async_count.
  // Semantics of individual states:
  // - m_continue_packet == false, m_async_count == 0: connection is free
  // - m_continue_packet == true, m_async_count == 0: only continue thread is
  // present
  // - m_continue_packet == true, m_async_count > 0: continue thread has
  // control, async threads
  //   should interrupt it and wait for it to set m_continue_packet to false
  // - m_continue_packet == false, m_async_count > 0: async threads have
  // control, continue
  //   thread needs to wait for them to finish (m_async_count goes down to 0).
  std::mutex m_mutex;
  std::condition_variable m_cv;
  // Packet with which to resume after an async interrupt. Can be changed by an
  // async thread
  // e.g. to inject a signal.
  std::string m_continue_packet;
  // When was the interrupt packet sent. Used to make sure we time out if the
  // stub does not
  // respond to interrupt requests.
  std::chrono::time_point<std::chrono::steady_clock> m_interrupt_time;
  uint32_t m_async_count;
  bool m_is_running;
  bool m_should_stop; // Whether we should resume after a stop.
  // end of continue thread synchronization block

  // This handles the synchronization between individual async threads. For now
  // they just use a
  // simple mutex.
  std::recursive_mutex m_async_mutex;

  bool ShouldStop(const UnixSignals &signals,
                  StringExtractorGDBRemote &response);

  class ContinueLock {
  public:
    enum class LockResult { Success, Cancelled, Failed };

    explicit ContinueLock(GDBRemoteClientBase &comm);
    ~ContinueLock();
    explicit operator bool() { return m_acquired; }

    LockResult lock();

    void unlock();

  private:
    GDBRemoteClientBase &m_comm;
    bool m_acquired;
  };
};

} // namespace process_gdb_remote
} // namespace lldb_private

#endif // liblldb_GDBRemoteCommunicationClient_h_
