//===-- Communication.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Communication.h"

#include "lldb/Host/HostThread.h"
#include "lldb/Host/ThreadLauncher.h"
#include "lldb/Utility/Connection.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Event.h"
#include "lldb/Utility/Listener.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Logging.h"
#include "lldb/Utility/Status.h"

#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/Support/Compiler.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>

using namespace lldb;
using namespace lldb_private;

ConstString &Communication::GetStaticBroadcasterClass() {
  static ConstString class_name("lldb.communication");
  return class_name;
}

Communication::Communication(const char *name)
    : Broadcaster(nullptr, name), m_connection_sp(),
      m_read_thread_enabled(false), m_read_thread_did_exit(false), m_bytes(),
      m_bytes_mutex(), m_write_mutex(), m_synchronize_mutex(),
      m_callback(nullptr), m_callback_baton(nullptr), m_close_on_eof(true)

{
  lldb_private::LogIfAnyCategoriesSet(
      LIBLLDB_LOG_OBJECT | LIBLLDB_LOG_COMMUNICATION,
      "%p Communication::Communication (name = %s)", this, name);

  SetEventName(eBroadcastBitDisconnected, "disconnected");
  SetEventName(eBroadcastBitReadThreadGotBytes, "got bytes");
  SetEventName(eBroadcastBitReadThreadDidExit, "read thread did exit");
  SetEventName(eBroadcastBitReadThreadShouldExit, "read thread should exit");
  SetEventName(eBroadcastBitPacketAvailable, "packet available");
  SetEventName(eBroadcastBitNoMorePendingInput, "no more pending input");

  CheckInWithManager();
}

Communication::~Communication() {
  lldb_private::LogIfAnyCategoriesSet(
      LIBLLDB_LOG_OBJECT | LIBLLDB_LOG_COMMUNICATION,
      "%p Communication::~Communication (name = %s)", this,
      GetBroadcasterName().AsCString());
  Clear();
}

void Communication::Clear() {
  SetReadThreadBytesReceivedCallback(nullptr, nullptr);
  Disconnect(nullptr);
  StopReadThread(nullptr);
}

ConnectionStatus Communication::Connect(const char *url, Status *error_ptr) {
  Clear();

  lldb_private::LogIfAnyCategoriesSet(LIBLLDB_LOG_COMMUNICATION,
                                      "%p Communication::Connect (url = %s)",
                                      this, url);

  lldb::ConnectionSP connection_sp(m_connection_sp);
  if (connection_sp)
    return connection_sp->Connect(url, error_ptr);
  if (error_ptr)
    error_ptr->SetErrorString("Invalid connection.");
  return eConnectionStatusNoConnection;
}

ConnectionStatus Communication::Disconnect(Status *error_ptr) {
  lldb_private::LogIfAnyCategoriesSet(LIBLLDB_LOG_COMMUNICATION,
                                      "%p Communication::Disconnect ()", this);

  lldb::ConnectionSP connection_sp(m_connection_sp);
  if (connection_sp) {
    ConnectionStatus status = connection_sp->Disconnect(error_ptr);
    // We currently don't protect connection_sp with any mutex for multi-
    // threaded environments. So lets not nuke our connection class without
    // putting some multi-threaded protections in. We also probably don't want
    // to pay for the overhead it might cause if every time we access the
    // connection we have to take a lock.
    //
    // This unique pointer will cleanup after itself when this object goes
    // away, so there is no need to currently have it destroy itself
    // immediately upon disconnect.
    // connection_sp.reset();
    return status;
  }
  return eConnectionStatusNoConnection;
}

bool Communication::IsConnected() const {
  lldb::ConnectionSP connection_sp(m_connection_sp);
  return (connection_sp ? connection_sp->IsConnected() : false);
}

bool Communication::HasConnection() const {
  return m_connection_sp.get() != nullptr;
}

size_t Communication::Read(void *dst, size_t dst_len,
                           const Timeout<std::micro> &timeout,
                           ConnectionStatus &status, Status *error_ptr) {
  Log *log = GetLogIfAllCategoriesSet(LIBLLDB_LOG_COMMUNICATION);
  LLDB_LOG(
      log,
      "this = {0}, dst = {1}, dst_len = {2}, timeout = {3}, connection = {4}",
      this, dst, dst_len, timeout, m_connection_sp.get());

  if (m_read_thread_enabled) {
    // We have a dedicated read thread that is getting data for us
    size_t cached_bytes = GetCachedBytes(dst, dst_len);
    if (cached_bytes > 0 || (timeout && timeout->count() == 0)) {
      status = eConnectionStatusSuccess;
      return cached_bytes;
    }

    if (!m_connection_sp) {
      if (error_ptr)
        error_ptr->SetErrorString("Invalid connection.");
      status = eConnectionStatusNoConnection;
      return 0;
    }

    ListenerSP listener_sp(Listener::MakeListener("Communication::Read"));
    listener_sp->StartListeningForEvents(
        this, eBroadcastBitReadThreadGotBytes | eBroadcastBitReadThreadDidExit);
    EventSP event_sp;
    while (listener_sp->GetEvent(event_sp, timeout)) {
      const uint32_t event_type = event_sp->GetType();
      if (event_type & eBroadcastBitReadThreadGotBytes) {
        return GetCachedBytes(dst, dst_len);
      }

      if (event_type & eBroadcastBitReadThreadDidExit) {
        if (GetCloseOnEOF())
          Disconnect(nullptr);
        break;
      }
    }
    return 0;
  }

  // We aren't using a read thread, just read the data synchronously in this
  // thread.
  return ReadFromConnection(dst, dst_len, timeout, status, error_ptr);
}

size_t Communication::Write(const void *src, size_t src_len,
                            ConnectionStatus &status, Status *error_ptr) {
  lldb::ConnectionSP connection_sp(m_connection_sp);

  std::lock_guard<std::mutex> guard(m_write_mutex);
  lldb_private::LogIfAnyCategoriesSet(
      LIBLLDB_LOG_COMMUNICATION,
      "%p Communication::Write (src = %p, src_len = %" PRIu64
      ") connection = %p",
      this, src, (uint64_t)src_len, connection_sp.get());

  if (connection_sp)
    return connection_sp->Write(src, src_len, status, error_ptr);

  if (error_ptr)
    error_ptr->SetErrorString("Invalid connection.");
  status = eConnectionStatusNoConnection;
  return 0;
}

bool Communication::StartReadThread(Status *error_ptr) {
  if (error_ptr)
    error_ptr->Clear();

  if (m_read_thread.IsJoinable())
    return true;

  lldb_private::LogIfAnyCategoriesSet(
      LIBLLDB_LOG_COMMUNICATION, "%p Communication::StartReadThread ()", this);

  char thread_name[1024];
  snprintf(thread_name, sizeof(thread_name), "<lldb.comm.%s>",
           GetBroadcasterName().AsCString());

  m_read_thread_enabled = true;
  m_read_thread_did_exit = false;
  m_read_thread = ThreadLauncher::LaunchThread(
      thread_name, Communication::ReadThread, this, error_ptr);
  if (!m_read_thread.IsJoinable())
    m_read_thread_enabled = false;
  return m_read_thread_enabled;
}

bool Communication::StopReadThread(Status *error_ptr) {
  if (!m_read_thread.IsJoinable())
    return true;

  lldb_private::LogIfAnyCategoriesSet(
      LIBLLDB_LOG_COMMUNICATION, "%p Communication::StopReadThread ()", this);

  m_read_thread_enabled = false;

  BroadcastEvent(eBroadcastBitReadThreadShouldExit, nullptr);

  // error = m_read_thread.Cancel();

  Status error = m_read_thread.Join(nullptr);
  return error.Success();
}

bool Communication::JoinReadThread(Status *error_ptr) {
  if (!m_read_thread.IsJoinable())
    return true;

  Status error = m_read_thread.Join(nullptr);
  return error.Success();
}

size_t Communication::GetCachedBytes(void *dst, size_t dst_len) {
  std::lock_guard<std::recursive_mutex> guard(m_bytes_mutex);
  if (!m_bytes.empty()) {
    // If DST is nullptr and we have a thread, then return the number of bytes
    // that are available so the caller can call again
    if (dst == nullptr)
      return m_bytes.size();

    const size_t len = std::min<size_t>(dst_len, m_bytes.size());

    ::memcpy(dst, m_bytes.c_str(), len);
    m_bytes.erase(m_bytes.begin(), m_bytes.begin() + len);

    return len;
  }
  return 0;
}

void Communication::AppendBytesToCache(const uint8_t *bytes, size_t len,
                                       bool broadcast,
                                       ConnectionStatus status) {
  lldb_private::LogIfAnyCategoriesSet(
      LIBLLDB_LOG_COMMUNICATION,
      "%p Communication::AppendBytesToCache (src = %p, src_len = %" PRIu64
      ", broadcast = %i)",
      this, bytes, (uint64_t)len, broadcast);
  if ((bytes == nullptr || len == 0) &&
      (status != lldb::eConnectionStatusEndOfFile))
    return;
  if (m_callback) {
    // If the user registered a callback, then call it and do not broadcast
    m_callback(m_callback_baton, bytes, len);
  } else if (bytes != nullptr && len > 0) {
    std::lock_guard<std::recursive_mutex> guard(m_bytes_mutex);
    m_bytes.append((const char *)bytes, len);
    if (broadcast)
      BroadcastEventIfUnique(eBroadcastBitReadThreadGotBytes);
  }
}

size_t Communication::ReadFromConnection(void *dst, size_t dst_len,
                                         const Timeout<std::micro> &timeout,
                                         ConnectionStatus &status,
                                         Status *error_ptr) {
  lldb::ConnectionSP connection_sp(m_connection_sp);
  if (connection_sp)
    return connection_sp->Read(dst, dst_len, timeout, status, error_ptr);

  if (error_ptr)
    error_ptr->SetErrorString("Invalid connection.");
  status = eConnectionStatusNoConnection;
  return 0;
}

bool Communication::ReadThreadIsRunning() { return m_read_thread_enabled; }

lldb::thread_result_t Communication::ReadThread(lldb::thread_arg_t p) {
  Communication *comm = (Communication *)p;

  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_COMMUNICATION));

  if (log)
    log->Printf("%p Communication::ReadThread () thread starting...", p);

  uint8_t buf[1024];

  Status error;
  ConnectionStatus status = eConnectionStatusSuccess;
  bool done = false;
  while (!done && comm->m_read_thread_enabled) {
    size_t bytes_read = comm->ReadFromConnection(
        buf, sizeof(buf), std::chrono::seconds(5), status, &error);
    if (bytes_read > 0)
      comm->AppendBytesToCache(buf, bytes_read, true, status);
    else if ((bytes_read == 0) && status == eConnectionStatusEndOfFile) {
      if (comm->GetCloseOnEOF())
        comm->Disconnect();
      comm->AppendBytesToCache(buf, bytes_read, true, status);
    }

    switch (status) {
    case eConnectionStatusSuccess:
      break;

    case eConnectionStatusEndOfFile:
      done = true;
      break;
    case eConnectionStatusError: // Check GetError() for details
      if (error.GetType() == eErrorTypePOSIX && error.GetError() == EIO) {
        // EIO on a pipe is usually caused by remote shutdown
        comm->Disconnect();
        done = true;
      }
      if (error.Fail())
        LLDB_LOG(log, "error: {0}, status = {1}", error,
                 Communication::ConnectionStatusAsCString(status));
      break;
    case eConnectionStatusInterrupted: // Synchronization signal from
                                       // SynchronizeWithReadThread()
      // The connection returns eConnectionStatusInterrupted only when there is
      // no input pending to be read, so we can signal that.
      comm->BroadcastEvent(eBroadcastBitNoMorePendingInput);
      break;
    case eConnectionStatusNoConnection:   // No connection
    case eConnectionStatusLostConnection: // Lost connection while connected to
                                          // a valid connection
      done = true;
      LLVM_FALLTHROUGH;
    case eConnectionStatusTimedOut: // Request timed out
      if (error.Fail())
        LLDB_LOG(log, "error: {0}, status = {1}", error,
                 Communication::ConnectionStatusAsCString(status));
      break;
    }
  }
  log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_COMMUNICATION);
  if (log)
    log->Printf("%p Communication::ReadThread () thread exiting...", p);

  comm->m_read_thread_did_exit = true;
  // Let clients know that this thread is exiting
  comm->BroadcastEvent(eBroadcastBitNoMorePendingInput);
  comm->BroadcastEvent(eBroadcastBitReadThreadDidExit);
  return NULL;
}

void Communication::SetReadThreadBytesReceivedCallback(
    ReadThreadBytesReceived callback, void *callback_baton) {
  m_callback = callback;
  m_callback_baton = callback_baton;
}

void Communication::SynchronizeWithReadThread() {
  // Only one thread can do the synchronization dance at a time.
  std::lock_guard<std::mutex> guard(m_synchronize_mutex);

  // First start listening for the synchronization event.
  ListenerSP listener_sp(
      Listener::MakeListener("Communication::SyncronizeWithReadThread"));
  listener_sp->StartListeningForEvents(this, eBroadcastBitNoMorePendingInput);

  // If the thread is not running, there is no point in synchronizing.
  if (!m_read_thread_enabled || m_read_thread_did_exit)
    return;

  // Notify the read thread.
  m_connection_sp->InterruptRead();

  // Wait for the synchronization event.
  EventSP event_sp;
  listener_sp->GetEvent(event_sp, llvm::None);
}

void Communication::SetConnection(Connection *connection) {
  Disconnect(nullptr);
  StopReadThread(nullptr);
  m_connection_sp.reset(connection);
}

const char *
Communication::ConnectionStatusAsCString(lldb::ConnectionStatus status) {
  switch (status) {
  case eConnectionStatusSuccess:
    return "success";
  case eConnectionStatusError:
    return "error";
  case eConnectionStatusTimedOut:
    return "timed out";
  case eConnectionStatusNoConnection:
    return "no connection";
  case eConnectionStatusLostConnection:
    return "lost connection";
  case eConnectionStatusEndOfFile:
    return "end of file";
  case eConnectionStatusInterrupted:
    return "interrupted";
  }

  static char unknown_state_string[64];
  snprintf(unknown_state_string, sizeof(unknown_state_string),
           "ConnectionStatus = %i", status);
  return unknown_state_string;
}
