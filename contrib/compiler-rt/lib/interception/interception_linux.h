//===-- interception_linux.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Linux-specific interception methods.
//===----------------------------------------------------------------------===//

#if SANITIZER_LINUX || SANITIZER_FREEBSD || SANITIZER_NETBSD || \
    SANITIZER_OPENBSD || SANITIZER_SOLARIS

#if !defined(INCLUDED_FROM_INTERCEPTION_LIB)
# error "interception_linux.h should be included from interception library only"
#endif

#ifndef INTERCEPTION_LINUX_H
#define INTERCEPTION_LINUX_H

namespace __interception {
// returns true if a function with the given name was found.
bool GetRealFunctionAddress(const char *func_name, uptr *func_addr,
    uptr real, uptr wrapper);
void *GetFuncAddrVer(const char *func_name, const char *ver);
}  // namespace __interception

#define INTERCEPT_FUNCTION_LINUX_OR_FREEBSD(func)                          \
  ::__interception::GetRealFunctionAddress(                                \
      #func, (::__interception::uptr *)&__interception::PTR_TO_REAL(func), \
      (::__interception::uptr) & (func),                                   \
      (::__interception::uptr) & WRAP(func))

// Android,  Solaris and OpenBSD do not have dlvsym
#if !SANITIZER_ANDROID && !SANITIZER_SOLARIS && !SANITIZER_OPENBSD
#define INTERCEPT_FUNCTION_VER_LINUX_OR_FREEBSD(func, symver) \
  (::__interception::real_##func = (func##_type)(                \
       unsigned long)::__interception::GetFuncAddrVer(#func, symver))
#else
#define INTERCEPT_FUNCTION_VER_LINUX_OR_FREEBSD(func, symver) \
  INTERCEPT_FUNCTION_LINUX_OR_FREEBSD(func)
#endif  // !SANITIZER_ANDROID && !SANITIZER_SOLARIS

#endif  // INTERCEPTION_LINUX_H
#endif  // SANITIZER_LINUX || SANITIZER_FREEBSD || SANITIZER_NETBSD ||
        // SANITIZER_OPENBSD || SANITIZER_SOLARIS
