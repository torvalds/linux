//===- ErrorHandler.h -------------------------------------------*- C++ -*-===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// We designed lld's error handlers with the following goals in mind:
//
//  - Errors can occur at any place where we handle user input, but we don't
//    want them to affect the normal execution path too much. Ideally,
//    handling errors should be as simple as reporting them and exit (but
//    without actually doing exit).
//
//    In particular, the design to wrap all functions that could fail with
//    ErrorOr<T> is rejected because otherwise we would have to wrap a large
//    number of functions in lld with ErrorOr. With that approach, if some
//    function F can fail, not only F but all functions that transitively call
//    F have to be wrapped with ErrorOr. That seemed too much.
//
//  - Finding only one error at a time is not sufficient. We want to find as
//    many errors as possible with one execution of the linker. That means the
//    linker needs to keep running after a first error and give up at some
//    checkpoint (beyond which it would find cascading, false errors caused by
//    the previous errors).
//
//  - We want a simple interface to report errors. Unlike Clang, the data we
//    handle is compiled binary, so we don't need an error reporting mechanism
//    that's as sophisticated as the one that Clang has.
//
// The current lld's error handling mechanism is simple:
//
//  - When you find an error, report it using error() and continue as far as
//    you can. An internal error counter is incremented by one every time you
//    call error().
//
//    A common idiom to handle an error is calling error() and then returning
//    a reasonable default value. For example, if your function handles a
//    user-supplied alignment value, and if you find an invalid alignment
//    (e.g. 17 which is not 2^n), you may report it using error() and continue
//    as if it were alignment 1 (which is the simplest reasonable value).
//
//    Note that you should not continue with an invalid value; that breaks the
//    internal consistency. You need to maintain all variables have some sane
//    value even after an error occurred. So, when you have to continue with
//    some value, always use a dummy value.
//
//  - Find a reasonable checkpoint at where you want to stop the linker, and
//    add code to return from the function if errorCount() > 0. In most cases,
//    a checkpoint already exists, so you don't need to do anything for this.
//
// This interface satisfies all the goals that we mentioned above.
//
// You should never call fatal() except for reporting a corrupted input file.
// fatal() immediately terminates the linker, so the function is not desirable
// if you are using lld as a subroutine in other program, and with that you
// can find only one error at a time.
//
// warn() doesn't do anything but printing out a given message.
//
// It is not recommended to use llvm::outs() or llvm::errs() directly in lld
// because they are not thread-safe. The functions declared in this file are
// thread-safe.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COMMON_ERRORHANDLER_H
#define LLD_COMMON_ERRORHANDLER_H

#include "lld/Common/LLVM.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileOutputBuffer.h"

namespace llvm {
class DiagnosticInfo;
}

namespace lld {

class ErrorHandler {
public:
  uint64_t ErrorCount = 0;
  uint64_t ErrorLimit = 20;
  StringRef ErrorLimitExceededMsg = "too many errors emitted, stopping now";
  StringRef LogName = "lld";
  llvm::raw_ostream *ErrorOS = &llvm::errs();
  bool ColorDiagnostics = llvm::errs().has_colors();
  bool ExitEarly = true;
  bool FatalWarnings = false;
  bool Verbose = false;

  void error(const Twine &Msg);
  LLVM_ATTRIBUTE_NORETURN void fatal(const Twine &Msg);
  void log(const Twine &Msg);
  void message(const Twine &Msg);
  void warn(const Twine &Msg);

  std::unique_ptr<llvm::FileOutputBuffer> OutputBuffer;

private:
  void print(StringRef S, raw_ostream::Colors C);
};

/// Returns the default error handler.
ErrorHandler &errorHandler();

inline void error(const Twine &Msg) { errorHandler().error(Msg); }
inline LLVM_ATTRIBUTE_NORETURN void fatal(const Twine &Msg) {
  errorHandler().fatal(Msg);
}
inline void log(const Twine &Msg) { errorHandler().log(Msg); }
inline void message(const Twine &Msg) { errorHandler().message(Msg); }
inline void warn(const Twine &Msg) { errorHandler().warn(Msg); }
inline uint64_t errorCount() { return errorHandler().ErrorCount; }

LLVM_ATTRIBUTE_NORETURN void exitLld(int Val);

void diagnosticHandler(const llvm::DiagnosticInfo &DI);
void checkError(Error E);

// check functions are convenient functions to strip errors
// from error-or-value objects.
template <class T> T check(ErrorOr<T> E) {
  if (auto EC = E.getError())
    fatal(EC.message());
  return std::move(*E);
}

template <class T> T check(Expected<T> E) {
  if (!E)
    fatal(llvm::toString(E.takeError()));
  return std::move(*E);
}

template <class T>
T check2(ErrorOr<T> E, llvm::function_ref<std::string()> Prefix) {
  if (auto EC = E.getError())
    fatal(Prefix() + ": " + EC.message());
  return std::move(*E);
}

template <class T>
T check2(Expected<T> E, llvm::function_ref<std::string()> Prefix) {
  if (!E)
    fatal(Prefix() + ": " + toString(E.takeError()));
  return std::move(*E);
}

inline std::string toString(const Twine &S) { return S.str(); }

// To evaluate the second argument lazily, we use C macro.
#define CHECK(E, S) check2((E), [&] { return toString(S); })

} // namespace lld

#endif
