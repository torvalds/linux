//===-- Status.cpp -----------------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Status.h"

#include "lldb/Utility/VASPrintf.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Errno.h"
#include "llvm/Support/FormatProviders.h"

#include <cerrno>
#include <cstdarg>
#include <string>
#include <system_error>

#ifdef __APPLE__
#include <mach/mach.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif
#include <stdint.h>

namespace llvm {
class raw_ostream;
}

using namespace lldb;
using namespace lldb_private;

Status::Status() : m_code(0), m_type(eErrorTypeInvalid), m_string() {}

Status::Status(ValueType err, ErrorType type)
    : m_code(err), m_type(type), m_string() {}

Status::Status(std::error_code EC)
    : m_code(EC.value()), m_type(ErrorType::eErrorTypeGeneric),
      m_string(EC.message()) {}

Status::Status(const Status &rhs) = default;

Status::Status(const char *format, ...)
    : m_code(0), m_type(eErrorTypeInvalid), m_string() {
  va_list args;
  va_start(args, format);
  SetErrorToGenericError();
  SetErrorStringWithVarArg(format, args);
  va_end(args);
}

const Status &Status::operator=(llvm::Error error) {
  if (!error) {
    Clear();
    return *this;
  }

  // if the error happens to be a errno error, preserve the error code
  error = llvm::handleErrors(
      std::move(error), [&](std::unique_ptr<llvm::ECError> e) -> llvm::Error {
        std::error_code ec = e->convertToErrorCode();
        if (ec.category() == std::generic_category()) {
          m_code = ec.value();
          m_type = ErrorType::eErrorTypePOSIX;
          return llvm::Error::success();
        }
        return llvm::Error(std::move(e));
      });

  // Otherwise, just preserve the message
  if (error) {
    SetErrorToGenericError();
    SetErrorString(llvm::toString(std::move(error)));
  }

  return *this;
}

llvm::Error Status::ToError() const {
  if (Success())
    return llvm::Error::success();
  if (m_type == ErrorType::eErrorTypePOSIX)
    return llvm::errorCodeToError(
        std::error_code(m_code, std::generic_category()));
  return llvm::make_error<llvm::StringError>(AsCString(),
                                             llvm::inconvertibleErrorCode());
}

//----------------------------------------------------------------------
// Assignment operator
//----------------------------------------------------------------------
const Status &Status::operator=(const Status &rhs) {
  if (this != &rhs) {
    m_code = rhs.m_code;
    m_type = rhs.m_type;
    m_string = rhs.m_string;
  }
  return *this;
}

Status::~Status() = default;

#ifdef _WIN32
static std::string RetrieveWin32ErrorString(uint32_t error_code) {
  char *buffer = nullptr;
  std::string message;
  // Retrieve win32 system error.
  if (::FormatMessageA(
          FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
              FORMAT_MESSAGE_MAX_WIDTH_MASK,
          NULL, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
          (LPSTR)&buffer, 0, NULL)) {
    message.assign(buffer);
    ::LocalFree(buffer);
  }
  return message;
}
#endif

//----------------------------------------------------------------------
// Get the error value as a NULL C string. The error string will be fetched and
// cached on demand. The cached error string value will remain until the error
// value is changed or cleared.
//----------------------------------------------------------------------
const char *Status::AsCString(const char *default_error_str) const {
  if (Success())
    return nullptr;

  if (m_string.empty()) {
    switch (m_type) {
    case eErrorTypeMachKernel:
#if defined(__APPLE__)
      if (const char *s = ::mach_error_string(m_code))
        m_string.assign(s);
#endif
      break;

    case eErrorTypePOSIX:
      m_string = llvm::sys::StrError(m_code);
      break;

    case eErrorTypeWin32:
#if defined(_WIN32)
      m_string = RetrieveWin32ErrorString(m_code);
#endif
      break;

    default:
      break;
    }
  }
  if (m_string.empty()) {
    if (default_error_str)
      m_string.assign(default_error_str);
    else
      return nullptr; // User wanted a nullptr string back...
  }
  return m_string.c_str();
}

//----------------------------------------------------------------------
// Clear the error and any cached error string that it might contain.
//----------------------------------------------------------------------
void Status::Clear() {
  m_code = 0;
  m_type = eErrorTypeInvalid;
  m_string.clear();
}

//----------------------------------------------------------------------
// Access the error value.
//----------------------------------------------------------------------
Status::ValueType Status::GetError() const { return m_code; }

//----------------------------------------------------------------------
// Access the error type.
//----------------------------------------------------------------------
ErrorType Status::GetType() const { return m_type; }

//----------------------------------------------------------------------
// Returns true if this object contains a value that describes an error or
// otherwise non-success result.
//----------------------------------------------------------------------
bool Status::Fail() const { return m_code != 0; }

//----------------------------------------------------------------------
// Set accessor for the error value to "err" and the type to
// "eErrorTypeMachKernel"
//----------------------------------------------------------------------
void Status::SetMachError(uint32_t err) {
  m_code = err;
  m_type = eErrorTypeMachKernel;
  m_string.clear();
}

void Status::SetExpressionError(lldb::ExpressionResults result,
                                const char *mssg) {
  m_code = result;
  m_type = eErrorTypeExpression;
  m_string = mssg;
}

int Status::SetExpressionErrorWithFormat(lldb::ExpressionResults result,
                                         const char *format, ...) {
  int length = 0;

  if (format != nullptr && format[0]) {
    va_list args;
    va_start(args, format);
    length = SetErrorStringWithVarArg(format, args);
    va_end(args);
  } else {
    m_string.clear();
  }
  m_code = result;
  m_type = eErrorTypeExpression;
  return length;
}

//----------------------------------------------------------------------
// Set accessor for the error value and type.
//----------------------------------------------------------------------
void Status::SetError(ValueType err, ErrorType type) {
  m_code = err;
  m_type = type;
  m_string.clear();
}

//----------------------------------------------------------------------
// Update the error value to be "errno" and update the type to be "POSIX".
//----------------------------------------------------------------------
void Status::SetErrorToErrno() {
  m_code = errno;
  m_type = eErrorTypePOSIX;
  m_string.clear();
}

//----------------------------------------------------------------------
// Update the error value to be LLDB_GENERIC_ERROR and update the type to be
// "Generic".
//----------------------------------------------------------------------
void Status::SetErrorToGenericError() {
  m_code = LLDB_GENERIC_ERROR;
  m_type = eErrorTypeGeneric;
  m_string.clear();
}

//----------------------------------------------------------------------
// Set accessor for the error string value for a specific error. This allows
// any string to be supplied as an error explanation. The error string value
// will remain until the error value is cleared or a new error value/type is
// assigned.
//----------------------------------------------------------------------
void Status::SetErrorString(llvm::StringRef err_str) {
  if (!err_str.empty()) {
    // If we have an error string, we should always at least have an error set
    // to a generic value.
    if (Success())
      SetErrorToGenericError();
  }
  m_string = err_str;
}

//------------------------------------------------------------------
/// Set the current error string to a formatted error string.
///
/// @param format
///     A printf style format string
//------------------------------------------------------------------
int Status::SetErrorStringWithFormat(const char *format, ...) {
  if (format != nullptr && format[0]) {
    va_list args;
    va_start(args, format);
    int length = SetErrorStringWithVarArg(format, args);
    va_end(args);
    return length;
  } else {
    m_string.clear();
  }
  return 0;
}

int Status::SetErrorStringWithVarArg(const char *format, va_list args) {
  if (format != nullptr && format[0]) {
    // If we have an error string, we should always at least have an error set
    // to a generic value.
    if (Success())
      SetErrorToGenericError();

    llvm::SmallString<1024> buf;
    VASprintf(buf, format, args);
    m_string = buf.str();
    return buf.size();
  } else {
    m_string.clear();
  }
  return 0;
}

//----------------------------------------------------------------------
// Returns true if the error code in this object is considered a successful
// return value.
//----------------------------------------------------------------------
bool Status::Success() const { return m_code == 0; }

bool Status::WasInterrupted() const {
  return (m_type == eErrorTypePOSIX && m_code == EINTR);
}

void llvm::format_provider<lldb_private::Status>::format(
    const lldb_private::Status &error, llvm::raw_ostream &OS,
    llvm::StringRef Options) {
  llvm::format_provider<llvm::StringRef>::format(error.AsCString(), OS,
                                                 Options);
}
