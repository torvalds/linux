//===-- Connection.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Connection_h_
#define liblldb_Connection_h_

#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"

#include "llvm/ADT/StringRef.h"

#include <ratio>
#include <string>

#include <stddef.h>

namespace lldb_private {
class Status;
}
namespace lldb_private {
template <typename Ratio> class Timeout;
}

namespace lldb_private {

//----------------------------------------------------------------------
/// @class Connection Connection.h "lldb/Utility/Connection.h"
/// A communication connection class.
///
/// A class that implements that actual communication functions for
/// connecting/disconnecting, reading/writing, and waiting for bytes to become
/// available from a two way communication connection.
///
/// This class is designed to only do very simple communication functions.
/// Instances can be instantiated and given to a Communication class to
/// perform communications where clients can listen for broadcasts, and
/// perform other higher level communications.
//----------------------------------------------------------------------
class Connection {
public:
  //------------------------------------------------------------------
  /// Default constructor
  //------------------------------------------------------------------
  Connection() = default;

  //------------------------------------------------------------------
  /// Virtual destructor since this class gets subclassed and handed to a
  /// Communication object.
  //------------------------------------------------------------------
  virtual ~Connection();

  //------------------------------------------------------------------
  /// Connect using the connect string \a url.
  ///
  /// @param[in] url
  ///     A string that contains all information needed by the
  ///     subclass to connect to another client.
  ///
  /// @param[out] error_ptr
  ///     A pointer to an error object that should be given an
  ///     appropriate error value if this method returns false. This
  ///     value can be NULL if the error value should be ignored.
  ///
  /// @return
  ///     \b True if the connect succeeded, \b false otherwise. The
  ///     internal error object should be filled in with an
  ///     appropriate value based on the result of this function.
  ///
  /// @see Status& Communication::GetError ();
  //------------------------------------------------------------------
  virtual lldb::ConnectionStatus Connect(llvm::StringRef url,
                                         Status *error_ptr) = 0;

  //------------------------------------------------------------------
  /// Disconnect the communications connection if one is currently connected.
  ///
  /// @param[out] error_ptr
  ///     A pointer to an error object that should be given an
  ///     appropriate error value if this method returns false. This
  ///     value can be NULL if the error value should be ignored.
  ///
  /// @return
  ///     \b True if the disconnect succeeded, \b false otherwise. The
  ///     internal error object should be filled in with an
  ///     appropriate value based on the result of this function.
  ///
  /// @see Status& Communication::GetError ();
  //------------------------------------------------------------------
  virtual lldb::ConnectionStatus Disconnect(Status *error_ptr) = 0;

  //------------------------------------------------------------------
  /// Check if the connection is valid.
  ///
  /// @return
  ///     \b True if this object is currently connected, \b false
  ///     otherwise.
  //------------------------------------------------------------------
  virtual bool IsConnected() const = 0;

  //------------------------------------------------------------------
  /// The read function that attempts to read from the connection.
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
  ///     The number of microseconds to wait for the data.
  ///
  /// @param[out] status
  ///     On return, indicates whether the call was successful or terminated
  ///     due to some error condition.
  ///
  /// @param[out] error_ptr
  ///     A pointer to an error object that should be given an
  ///     appropriate error value if this method returns zero. This
  ///     value can be NULL if the error value should be ignored.
  ///
  /// @return
  ///     The number of bytes actually read.
  ///
  /// @see size_t Communication::Read (void *, size_t, uint32_t);
  //------------------------------------------------------------------
  virtual size_t Read(void *dst, size_t dst_len,
                      const Timeout<std::micro> &timeout,
                      lldb::ConnectionStatus &status, Status *error_ptr) = 0;

  //------------------------------------------------------------------
  /// The actual write function that attempts to write to the communications
  /// protocol.
  ///
  /// Subclasses must override this function.
  ///
  /// @param[in] dst
  ///     A desination buffer that must be at least \a dst_len bytes
  ///     long.
  ///
  /// @param[in] dst_len
  ///     The number of bytes to attempt to write, and also the
  ///     number of bytes are currently available in \a dst.
  ///
  /// @param[out] error_ptr
  ///     A pointer to an error object that should be given an
  ///     appropriate error value if this method returns zero. This
  ///     value can be NULL if the error value should be ignored.
  ///
  /// @return
  ///     The number of bytes actually Written.
  //------------------------------------------------------------------
  virtual size_t Write(const void *dst, size_t dst_len,
                       lldb::ConnectionStatus &status, Status *error_ptr) = 0;

  //------------------------------------------------------------------
  /// Returns a URI that describes this connection object
  ///
  /// Subclasses may override this function.
  ///
  /// @return
  ///     Returns URI or an empty string if disconnecteds
  //------------------------------------------------------------------
  virtual std::string GetURI() = 0;

  //------------------------------------------------------------------
  /// Interrupts an ongoing Read() operation.
  ///
  /// If there is an ongoing read operation in another thread, this operation
  /// return with status == eConnectionStatusInterrupted. Note that if there
  /// data waiting to be read and an interrupt request is issued, the Read()
  /// function will return the data immediately without processing the
  /// interrupt request (which will remain queued for the next Read()
  /// operation).
  ///
  /// @return
  ///     Returns true is the interrupt request was successful.
  //------------------------------------------------------------------
  virtual bool InterruptRead() = 0;

  //------------------------------------------------------------------
  /// Returns the underlying IOObject used by the Connection.
  ///
  /// The IOObject can be used to wait for data to become available on the
  /// connection. If the Connection does not use IOObjects (and hence does not
  /// support waiting) this function should return a null pointer.
  ///
  /// @return
  ///     The underlying IOObject used for reading.
  //------------------------------------------------------------------
  virtual lldb::IOObjectSP GetReadObject() { return lldb::IOObjectSP(); }

private:
  //------------------------------------------------------------------
  // For Connection only
  //------------------------------------------------------------------
  DISALLOW_COPY_AND_ASSIGN(Connection);
};

} // namespace lldb_private

#endif // liblldb_Connection_h_
