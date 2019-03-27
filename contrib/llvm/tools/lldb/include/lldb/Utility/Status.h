//===-- Status.h ------------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_STATUS_H
#define LLDB_UTILITY_STATUS_H

#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include <cstdarg>
#include <stdint.h>
#include <string>
#include <system_error>
#include <type_traits>

namespace llvm {
class raw_ostream;
}

namespace lldb_private {

//----------------------------------------------------------------------
/// @class Status Status.h "lldb/Utility/Status.h" An error handling class.
///
/// This class is designed to be able to hold any error code that can be
/// encountered on a given platform. The errors are stored as a value of type
/// Status::ValueType. This value should be large enough to hold any and all
/// errors that the class supports. Each error has an associated type that is
/// of type lldb::ErrorType. New types can be added to support new error
/// types, and architecture specific types can be enabled. In the future we
/// may wish to switch to a registration mechanism where new error types can
/// be registered at runtime instead of a hard coded scheme.
///
/// All errors in this class also know how to generate a string representation
/// of themselves for printing results and error codes. The string value will
/// be fetched on demand and its string value will be cached until the error
/// is cleared of the value of the error changes.
//----------------------------------------------------------------------
class Status {
public:
  //------------------------------------------------------------------
  /// Every error value that this object can contain needs to be able to fit
  /// into ValueType.
  //------------------------------------------------------------------
  typedef uint32_t ValueType;

  //------------------------------------------------------------------
  /// Default constructor.
  ///
  /// Initialize the error object with a generic success value.
  ///
  /// @param[in] err
  ///     An error code.
  ///
  /// @param[in] type
  ///     The type for \a err.
  //------------------------------------------------------------------
  Status();

  explicit Status(ValueType err,
                  lldb::ErrorType type = lldb::eErrorTypeGeneric);

  /* implicit */ Status(std::error_code EC);

  explicit Status(const char *format, ...)
      __attribute__((format(printf, 2, 3)));

  Status(const Status &rhs);
  //------------------------------------------------------------------
  /// Assignment operator.
  ///
  /// @param[in] err
  ///     An error code.
  ///
  /// @return
  ///     A const reference to this object.
  //------------------------------------------------------------------
  const Status &operator=(const Status &rhs);

  ~Status();

  // llvm::Error support
  explicit Status(llvm::Error error) { *this = std::move(error); }
  const Status &operator=(llvm::Error error);
  llvm::Error ToError() const;

  //------------------------------------------------------------------
  /// Get the error string associated with the current error.
  //
  /// Gets the error value as a NULL terminated C string. The error string
  /// will be fetched and cached on demand. The error string will be retrieved
  /// from a callback that is appropriate for the type of the error and will
  /// be cached until the error value is changed or cleared.
  ///
  /// @return
  ///     The error as a NULL terminated C string value if the error
  ///     is valid and is able to be converted to a string value,
  ///     NULL otherwise.
  //------------------------------------------------------------------
  const char *AsCString(const char *default_error_str = "unknown error") const;

  //------------------------------------------------------------------
  /// Clear the object state.
  ///
  /// Reverts the state of this object to contain a generic success value and
  /// frees any cached error string value.
  //------------------------------------------------------------------
  void Clear();

  //------------------------------------------------------------------
  /// Test for error condition.
  ///
  /// @return
  ///     \b true if this object contains an error, \b false
  ///     otherwise.
  //------------------------------------------------------------------
  bool Fail() const;

  //------------------------------------------------------------------
  /// Access the error value.
  ///
  /// @return
  ///     The error value.
  //------------------------------------------------------------------
  ValueType GetError() const;

  //------------------------------------------------------------------
  /// Access the error type.
  ///
  /// @return
  ///     The error type enumeration value.
  //------------------------------------------------------------------
  lldb::ErrorType GetType() const;

  //------------------------------------------------------------------
  /// Set accessor from a kern_return_t.
  ///
  /// Set accesssor for the error value to \a err and the error type to \c
  /// MachKernel.
  ///
  /// @param[in] err
  ///     A mach error code.
  //------------------------------------------------------------------
  void SetMachError(uint32_t err);

  void SetExpressionError(lldb::ExpressionResults, const char *mssg);

  int SetExpressionErrorWithFormat(lldb::ExpressionResults, const char *format,
                                   ...) __attribute__((format(printf, 3, 4)));

  //------------------------------------------------------------------
  /// Set accesssor with an error value and type.
  ///
  /// Set accesssor for the error value to \a err and the error type to \a
  /// type.
  ///
  /// @param[in] err
  ///     A mach error code.
  ///
  /// @param[in] type
  ///     The type for \a err.
  //------------------------------------------------------------------
  void SetError(ValueType err, lldb::ErrorType type);

  //------------------------------------------------------------------
  /// Set the current error to errno.
  ///
  /// Update the error value to be \c errno and update the type to be \c
  /// Status::POSIX.
  //------------------------------------------------------------------
  void SetErrorToErrno();

  //------------------------------------------------------------------
  /// Set the current error to a generic error.
  ///
  /// Update the error value to be \c LLDB_GENERIC_ERROR and update the type
  /// to be \c Status::Generic.
  //------------------------------------------------------------------
  void SetErrorToGenericError();

  //------------------------------------------------------------------
  /// Set the current error string to \a err_str.
  ///
  /// Set accessor for the error string value for a generic errors, or to
  /// supply additional details above and beyond the standard error strings
  /// that the standard type callbacks typically provide. This allows custom
  /// strings to be supplied as an error explanation. The error string value
  /// will remain until the error value is cleared or a new error value/type
  /// is assigned.
  ///
  /// @param err_str
  ///     The new custom error string to copy and cache.
  //------------------------------------------------------------------
  void SetErrorString(llvm::StringRef err_str);

  //------------------------------------------------------------------
  /// Set the current error string to a formatted error string.
  ///
  /// @param format
  ///     A printf style format string
  //------------------------------------------------------------------
  int SetErrorStringWithFormat(const char *format, ...)
      __attribute__((format(printf, 2, 3)));

  int SetErrorStringWithVarArg(const char *format, va_list args);

  template <typename... Args>
  void SetErrorStringWithFormatv(const char *format, Args &&... args) {
    SetErrorString(llvm::formatv(format, std::forward<Args>(args)...).str());
  }

  //------------------------------------------------------------------
  /// Test for success condition.
  ///
  /// Returns true if the error code in this object is considered a successful
  /// return value.
  ///
  /// @return
  ///     \b true if this object contains an value that describes
  ///     success (non-erro), \b false otherwise.
  //------------------------------------------------------------------
  bool Success() const;

  //------------------------------------------------------------------
  /// Test for a failure due to a generic interrupt.
  ///
  /// Returns true if the error code in this object was caused by an
  /// interrupt. At present only supports Posix EINTR.
  ///
  /// @return
  ///     \b true if this object contains an value that describes
  ///     failure due to interrupt, \b false otherwise.
  //------------------------------------------------------------------
  bool WasInterrupted() const;

protected:
  //------------------------------------------------------------------
  /// Member variables
  //------------------------------------------------------------------
  ValueType m_code;             ///< Status code as an integer value.
  lldb::ErrorType m_type;       ///< The type of the above error code.
  mutable std::string m_string; ///< A string representation of the error code.
};

} // namespace lldb_private

namespace llvm {
template <> struct format_provider<lldb_private::Status> {
  static void format(const lldb_private::Status &error, llvm::raw_ostream &OS,
                     llvm::StringRef Options);
};
}

#endif // #ifndef LLDB_UTILITY_STATUS_H
