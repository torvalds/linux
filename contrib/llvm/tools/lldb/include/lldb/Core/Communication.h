//===-- Communication.h -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Communication_h_
#define liblldb_Communication_h_

#include "lldb/Host/HostThread.h"
#include "lldb/Utility/Broadcaster.h"
#include "lldb/Utility/Timeout.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"

#include <atomic>
#include <mutex>
#include <ratio>
#include <string>

#include <stddef.h>
#include <stdint.h>

namespace lldb_private {
class Connection;
}
namespace lldb_private {
class ConstString;
}
namespace lldb_private {
class Status;
}

namespace lldb_private {

//----------------------------------------------------------------------
/// @class Communication Communication.h "lldb/Core/Communication.h" An
/// abstract communications class.
///
/// Communication is an class that handles data communication between two data
/// sources. It uses a Connection class to do the real communication. This
/// approach has a couple of advantages: it allows a single instance of this
/// class to be used even though its connection can change. Connections could
/// negotiate for different connections based on abilities like starting with
/// Bluetooth and negotiating up to WiFi if available. It also allows this
/// class to be subclassed by any interfaces that don't want to give bytes but
/// want to validate and give out packets. This can be done by overriding:
///
/// AppendBytesToCache (const uint8_t *src, size_t src_len, bool broadcast);
///
/// Communication inherits from Broadcaster which means it can be used in
/// conjunction with Listener to wait for multiple broadcaster objects and
/// multiple events from each of those objects. Communication defines a set of
/// pre-defined event bits (see enumerations definitions that start with
/// "eBroadcastBit" below).
///
/// There are two modes in which communications can occur:
///     @li single-threaded
///     @li multi-threaded
///
/// In single-threaded mode, all reads and writes happen synchronously on the
/// calling thread.
///
/// In multi-threaded mode, a read thread is spawned that continually reads
/// data and caches any received bytes. To start the read thread clients call:
///
///     bool Communication::StartReadThread (Status *);
///
/// If true is returned a read thread has been spawned that will continually
/// execute a call to the pure virtual DoRead function:
///
///     size_t Communication::ReadFromConnection (void *, size_t, uint32_t);
///
/// When bytes are received the data gets cached in \a m_bytes and this class
/// will broadcast a \b eBroadcastBitReadThreadGotBytes event. Clients that
/// want packet based communication should override AppendBytesToCache. The
/// subclasses can choose to call the built in AppendBytesToCache with the \a
/// broadcast parameter set to false. This will cause the \b
/// eBroadcastBitReadThreadGotBytes event not get broadcast, and then the
/// subclass can post a \b eBroadcastBitPacketAvailable event when a full
/// packet of data has been received.
///
/// If the connection is disconnected a \b eBroadcastBitDisconnected event
/// gets broadcast. If the read thread exits a \b
/// eBroadcastBitReadThreadDidExit event will be broadcast. Clients can also
/// post a \b eBroadcastBitReadThreadShouldExit event to this object which
/// will cause the read thread to exit.
//----------------------------------------------------------------------
class Communication : public Broadcaster {
public:
  FLAGS_ANONYMOUS_ENUM(){
      eBroadcastBitDisconnected =
          (1u << 0), ///< Sent when the communications connection is lost.
      eBroadcastBitReadThreadGotBytes =
          (1u << 1), ///< Sent by the read thread when bytes become available.
      eBroadcastBitReadThreadDidExit =
          (1u
           << 2), ///< Sent by the read thread when it exits to inform clients.
      eBroadcastBitReadThreadShouldExit =
          (1u << 3), ///< Sent by clients that need to cancel the read thread.
      eBroadcastBitPacketAvailable =
          (1u << 4), ///< Sent when data received makes a complete packet.
      eBroadcastBitNoMorePendingInput = (1u << 5), ///< Sent by the read thread
                                                   ///to indicate all pending
                                                   ///input has been processed.
      kLoUserBroadcastBit =
          (1u << 16), ///< Subclasses can used bits 31:16 for any needed events.
      kHiUserBroadcastBit = (1u << 31),
      eAllEventBits = 0xffffffff};

  typedef void (*ReadThreadBytesReceived)(void *baton, const void *src,
                                          size_t src_len);

  //------------------------------------------------------------------
  /// Construct the Communication object with the specified name for the
  /// Broadcaster that this object inherits from.
  ///
  /// @param[in] broadcaster_name
  ///     The name of the broadcaster object.  This name should be as
  ///     complete as possible to uniquely identify this object. The
  ///     broadcaster name can be updated after the connect function
  ///     is called.
  //------------------------------------------------------------------
  Communication(const char *broadcaster_name);

  //------------------------------------------------------------------
  /// Destructor.
  ///
  /// The destructor is virtual since this class gets subclassed.
  //------------------------------------------------------------------
  ~Communication() override;

  void Clear();

  //------------------------------------------------------------------
  /// Connect using the current connection by passing \a url to its connect
  /// function. string.
  ///
  /// @param[in] url
  ///     A string that contains all information needed by the
  ///     subclass to connect to another client.
  ///
  /// @return
  ///     \b True if the connect succeeded, \b false otherwise. The
  ///     internal error object should be filled in with an
  ///     appropriate value based on the result of this function.
  ///
  /// @see Status& Communication::GetError ();
  /// @see bool Connection::Connect (const char *url);
  //------------------------------------------------------------------
  lldb::ConnectionStatus Connect(const char *url, Status *error_ptr);

  //------------------------------------------------------------------
  /// Disconnect the communications connection if one is currently connected.
  ///
  /// @return
  ///     \b True if the disconnect succeeded, \b false otherwise. The
  ///     internal error object should be filled in with an
  ///     appropriate value based on the result of this function.
  ///
  /// @see Status& Communication::GetError ();
  /// @see bool Connection::Disconnect ();
  //------------------------------------------------------------------
  lldb::ConnectionStatus Disconnect(Status *error_ptr = nullptr);

  //------------------------------------------------------------------
  /// Check if the connection is valid.
  ///
  /// @return
  ///     \b True if this object is currently connected, \b false
  ///     otherwise.
  //------------------------------------------------------------------
  bool IsConnected() const;

  bool HasConnection() const;

  lldb_private::Connection *GetConnection() { return m_connection_sp.get(); }

  //------------------------------------------------------------------
  /// Read bytes from the current connection.
  ///
  /// If no read thread is running, this function call the connection's
  /// Connection::Read(...) function to get any available.
  ///
  /// If a read thread has been started, this function will check for any
  /// cached bytes that have already been read and return any currently
  /// available bytes. If no bytes are cached, it will wait for the bytes to
  /// become available by listening for the \a eBroadcastBitReadThreadGotBytes
  /// event. If this function consumes all of the bytes in the cache, it will
  /// reset the \a eBroadcastBitReadThreadGotBytes event bit.
  ///
  /// @param[in] dst
  ///     A destination buffer that must be at least \a dst_len bytes
  ///     long.
  ///
  /// @param[in] dst_len
  ///     The number of bytes to attempt to read, and also the max
  ///     number of bytes that can be placed into \a dst.
  ///
  /// @param[in] timeout
  ///     A timeout value or llvm::None for no timeout.
  ///
  /// @return
  ///     The number of bytes actually read.
  ///
  /// @see size_t Connection::Read (void *, size_t);
  //------------------------------------------------------------------
  size_t Read(void *dst, size_t dst_len, const Timeout<std::micro> &timeout,
              lldb::ConnectionStatus &status, Status *error_ptr);

  //------------------------------------------------------------------
  /// The actual write function that attempts to write to the communications
  /// protocol.
  ///
  /// Subclasses must override this function.
  ///
  /// @param[in] src
  ///     A source buffer that must be at least \a src_len bytes
  ///     long.
  ///
  /// @param[in] src_len
  ///     The number of bytes to attempt to write, and also the
  ///     number of bytes are currently available in \a src.
  ///
  /// @return
  ///     The number of bytes actually Written.
  //------------------------------------------------------------------
  size_t Write(const void *src, size_t src_len, lldb::ConnectionStatus &status,
               Status *error_ptr);

  //------------------------------------------------------------------
  /// Sets the connection that it to be used by this class.
  ///
  /// By making a communication class that uses different connections it
  /// allows a single communication interface to negotiate and change its
  /// connection without any interruption to the client. It also allows the
  /// Communication class to be subclassed for packet based communication.
  ///
  /// @param[in] connection
  ///     A connection that this class will own and destroy.
  ///
  /// @see
  ///     class Connection
  //------------------------------------------------------------------
  void SetConnection(Connection *connection);

  //------------------------------------------------------------------
  /// Starts a read thread whose sole purpose it to read bytes from the
  /// current connection. This function will call connection's read function:
  ///
  /// size_t Connection::Read (void *, size_t);
  ///
  /// When bytes are read and cached, this function will call:
  ///
  /// Communication::AppendBytesToCache (const uint8_t * bytes, size_t len,
  /// bool
  /// broadcast);
  ///
  /// Subclasses should override this function if they wish to override the
  /// default action of caching the bytes and broadcasting a \b
  /// eBroadcastBitReadThreadGotBytes event.
  ///
  /// @return
  ///     \b True if the read thread was successfully started, \b
  ///     false otherwise.
  ///
  /// @see size_t Connection::Read (void *, size_t);
  /// @see void Communication::AppendBytesToCache (const uint8_t * bytes,
  ///                                              size_t len, bool broadcast);
  //------------------------------------------------------------------
  virtual bool StartReadThread(Status *error_ptr = nullptr);

  //------------------------------------------------------------------
  /// Stops the read thread by cancelling it.
  ///
  /// @return
  ///     \b True if the read thread was successfully canceled, \b
  ///     false otherwise.
  //------------------------------------------------------------------
  virtual bool StopReadThread(Status *error_ptr = nullptr);

  virtual bool JoinReadThread(Status *error_ptr = nullptr);
  //------------------------------------------------------------------
  /// Checks if there is a currently running read thread.
  ///
  /// @return
  ///     \b True if the read thread is running, \b false otherwise.
  //------------------------------------------------------------------
  bool ReadThreadIsRunning();

  //------------------------------------------------------------------
  /// The static read thread function. This function will call the "DoRead"
  /// function continuously and wait for data to become available. When data
  /// is received it will append the available data to the internal cache and
  /// broadcast a \b eBroadcastBitReadThreadGotBytes event.
  ///
  /// @param[in] comm_ptr
  ///     A pointer to an instance of this class.
  ///
  /// @return
  ///     \b NULL.
  ///
  /// @see void Communication::ReadThreadGotBytes (const uint8_t *, size_t);
  //------------------------------------------------------------------
  static lldb::thread_result_t ReadThread(lldb::thread_arg_t comm_ptr);

  void SetReadThreadBytesReceivedCallback(ReadThreadBytesReceived callback,
                                          void *callback_baton);

  //------------------------------------------------------------------
  /// Wait for the read thread to process all outstanding data.
  ///
  /// After this function returns, the read thread has processed all data that
  /// has been waiting in the Connection queue.
  ///
  //------------------------------------------------------------------
  void SynchronizeWithReadThread();

  static const char *ConnectionStatusAsCString(lldb::ConnectionStatus status);

  bool GetCloseOnEOF() const { return m_close_on_eof; }

  void SetCloseOnEOF(bool b) { m_close_on_eof = b; }

  static ConstString &GetStaticBroadcasterClass();

  ConstString &GetBroadcasterClass() const override {
    return GetStaticBroadcasterClass();
  }

protected:
  lldb::ConnectionSP m_connection_sp; ///< The connection that is current in use
                                      ///by this communications class.
  HostThread m_read_thread; ///< The read thread handle in case we need to
                            ///cancel the thread.
  std::atomic<bool> m_read_thread_enabled;
  std::atomic<bool> m_read_thread_did_exit;
  std::string
      m_bytes; ///< A buffer to cache bytes read in the ReadThread function.
  std::recursive_mutex m_bytes_mutex; ///< A mutex to protect multi-threaded
                                      ///access to the cached bytes.
  std::mutex
      m_write_mutex; ///< Don't let multiple threads write at the same time...
  std::mutex m_synchronize_mutex;
  ReadThreadBytesReceived m_callback;
  void *m_callback_baton;
  bool m_close_on_eof;

  size_t ReadFromConnection(void *dst, size_t dst_len,
                            const Timeout<std::micro> &timeout,
                            lldb::ConnectionStatus &status, Status *error_ptr);

  //------------------------------------------------------------------
  /// Append new bytes that get read from the read thread into the internal
  /// object byte cache. This will cause a \b eBroadcastBitReadThreadGotBytes
  /// event to be broadcast if \a broadcast is true.
  ///
  /// Subclasses can override this function in order to inspect the received
  /// data and check if a packet is available.
  ///
  /// Subclasses can also still call this function from the overridden method
  /// to allow the caching to correctly happen and suppress the broadcasting
  /// of the \a eBroadcastBitReadThreadGotBytes event by setting \a broadcast
  /// to false.
  ///
  /// @param[in] src
  ///     A source buffer that must be at least \a src_len bytes
  ///     long.
  ///
  /// @param[in] src_len
  ///     The number of bytes to append to the cache.
  //------------------------------------------------------------------
  virtual void AppendBytesToCache(const uint8_t *src, size_t src_len,
                                  bool broadcast,
                                  lldb::ConnectionStatus status);

  //------------------------------------------------------------------
  /// Get any available bytes from our data cache. If this call empties the
  /// data cache, the \b eBroadcastBitReadThreadGotBytes event will be reset
  /// to signify no more bytes are available.
  ///
  /// @param[in] dst
  ///     A destination buffer that must be at least \a dst_len bytes
  ///     long.
  ///
  /// @param[in] dst_len
  ///     The number of bytes to attempt to read from the cache,
  ///     and also the max number of bytes that can be placed into
  ///     \a dst.
  ///
  /// @return
  ///     The number of bytes extracted from the data cache.
  //------------------------------------------------------------------
  size_t GetCachedBytes(void *dst, size_t dst_len);

private:
  DISALLOW_COPY_AND_ASSIGN(Communication);
};

} // namespace lldb_private

#endif // liblldb_Communication_h_
