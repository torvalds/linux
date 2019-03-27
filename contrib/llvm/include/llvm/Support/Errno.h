//===- llvm/Support/Errno.h - Portable+convenient errno handling -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares some portable and convenient functions to deal with errno.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ERRNO_H
#define LLVM_SUPPORT_ERRNO_H

#include <cerrno>
#include <string>
#include <type_traits>

namespace llvm {
namespace sys {

/// Returns a string representation of the errno value, using whatever
/// thread-safe variant of strerror() is available.  Be sure to call this
/// immediately after the function that set errno, or errno may have been
/// overwritten by an intervening call.
std::string StrError();

/// Like the no-argument version above, but uses \p errnum instead of errno.
std::string StrError(int errnum);

template <typename FailT, typename Fun, typename... Args>
inline auto RetryAfterSignal(const FailT &Fail, const Fun &F,
                             const Args &... As) -> decltype(F(As...)) {
  decltype(F(As...)) Res;
  do {
    errno = 0;
    Res = F(As...);
  } while (Res == Fail && errno == EINTR);
  return Res;
}

}  // namespace sys
}  // namespace llvm

#endif  // LLVM_SYSTEM_ERRNO_H
