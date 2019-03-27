//===- llvm/Support/ErrorHandling.h - Fatal error handling ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines an API used to indicate fatal error conditions.  Non-fatal
// errors (most of them) should be handled through LLVMContext.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ERRORHANDLING_H
#define LLVM_SUPPORT_ERRORHANDLING_H

#include "llvm/Support/Compiler.h"
#include <string>

namespace llvm {
class StringRef;
  class Twine;

  /// An error handler callback.
  typedef void (*fatal_error_handler_t)(void *user_data,
                                        const std::string& reason,
                                        bool gen_crash_diag);

  /// install_fatal_error_handler - Installs a new error handler to be used
  /// whenever a serious (non-recoverable) error is encountered by LLVM.
  ///
  /// If no error handler is installed the default is to print the error message
  /// to stderr, and call exit(1).  If an error handler is installed then it is
  /// the handler's responsibility to log the message, it will no longer be
  /// printed to stderr.  If the error handler returns, then exit(1) will be
  /// called.
  ///
  /// It is dangerous to naively use an error handler which throws an exception.
  /// Even though some applications desire to gracefully recover from arbitrary
  /// faults, blindly throwing exceptions through unfamiliar code isn't a way to
  /// achieve this.
  ///
  /// \param user_data - An argument which will be passed to the install error
  /// handler.
  void install_fatal_error_handler(fatal_error_handler_t handler,
                                   void *user_data = nullptr);

  /// Restores default error handling behaviour.
  void remove_fatal_error_handler();

  /// ScopedFatalErrorHandler - This is a simple helper class which just
  /// calls install_fatal_error_handler in its constructor and
  /// remove_fatal_error_handler in its destructor.
  struct ScopedFatalErrorHandler {
    explicit ScopedFatalErrorHandler(fatal_error_handler_t handler,
                                     void *user_data = nullptr) {
      install_fatal_error_handler(handler, user_data);
    }

    ~ScopedFatalErrorHandler() { remove_fatal_error_handler(); }
  };

/// Reports a serious error, calling any installed error handler. These
/// functions are intended to be used for error conditions which are outside
/// the control of the compiler (I/O errors, invalid user input, etc.)
///
/// If no error handler is installed the default is to print the message to
/// standard error, followed by a newline.
/// After the error handler is called this function will call exit(1), it
/// does not return.
LLVM_ATTRIBUTE_NORETURN void report_fatal_error(const char *reason,
                                                bool gen_crash_diag = true);
LLVM_ATTRIBUTE_NORETURN void report_fatal_error(const std::string &reason,
                                                bool gen_crash_diag = true);
LLVM_ATTRIBUTE_NORETURN void report_fatal_error(StringRef reason,
                                                bool gen_crash_diag = true);
LLVM_ATTRIBUTE_NORETURN void report_fatal_error(const Twine &reason,
                                                bool gen_crash_diag = true);

/// Installs a new bad alloc error handler that should be used whenever a
/// bad alloc error, e.g. failing malloc/calloc, is encountered by LLVM.
///
/// The user can install a bad alloc handler, in order to define the behavior
/// in case of failing allocations, e.g. throwing an exception. Note that this
/// handler must not trigger any additional allocations itself.
///
/// If no error handler is installed the default is to print the error message
/// to stderr, and call exit(1).  If an error handler is installed then it is
/// the handler's responsibility to log the message, it will no longer be
/// printed to stderr.  If the error handler returns, then exit(1) will be
/// called.
///
///
/// \param user_data - An argument which will be passed to the installed error
/// handler.
void install_bad_alloc_error_handler(fatal_error_handler_t handler,
                                     void *user_data = nullptr);

/// Restores default bad alloc error handling behavior.
void remove_bad_alloc_error_handler();

void install_out_of_memory_new_handler();

/// Reports a bad alloc error, calling any user defined bad alloc
/// error handler. In contrast to the generic 'report_fatal_error'
/// functions, this function is expected to return, e.g. the user
/// defined error handler throws an exception.
///
/// Note: When throwing an exception in the bad alloc handler, make sure that
/// the following unwind succeeds, e.g. do not trigger additional allocations
/// in the unwind chain.
///
/// If no error handler is installed (default), then a bad_alloc exception
/// is thrown, if LLVM is compiled with exception support, otherwise an
/// assertion is called.
void report_bad_alloc_error(const char *Reason, bool GenCrashDiag = true);

/// This function calls abort(), and prints the optional message to stderr.
/// Use the llvm_unreachable macro (that adds location info), instead of
/// calling this function directly.
LLVM_ATTRIBUTE_NORETURN void
llvm_unreachable_internal(const char *msg = nullptr, const char *file = nullptr,
                          unsigned line = 0);
}

/// Marks that the current location is not supposed to be reachable.
/// In !NDEBUG builds, prints the message and location info to stderr.
/// In NDEBUG builds, becomes an optimizer hint that the current location
/// is not supposed to be reachable.  On compilers that don't support
/// such hints, prints a reduced message instead.
///
/// Use this instead of assert(0).  It conveys intent more clearly and
/// allows compilers to omit some unnecessary code.
#ifndef NDEBUG
#define llvm_unreachable(msg) \
  ::llvm::llvm_unreachable_internal(msg, __FILE__, __LINE__)
#elif defined(LLVM_BUILTIN_UNREACHABLE)
#define llvm_unreachable(msg) LLVM_BUILTIN_UNREACHABLE
#else
#define llvm_unreachable(msg) ::llvm::llvm_unreachable_internal()
#endif

#endif
