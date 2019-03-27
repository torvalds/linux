//===-- sanitizer_syscall_generic.inc ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Generic implementations of internal_syscall* and internal_iserror.
//
//===----------------------------------------------------------------------===//

// NetBSD uses libc calls directly
#if !SANITIZER_NETBSD

#if SANITIZER_FREEBSD || SANITIZER_MAC || SANITIZER_OPENBSD || SANITIZER_SOLARIS
# define SYSCALL(name) SYS_ ## name
#else
# define SYSCALL(name) __NR_ ## name
#endif

#if defined(__x86_64__) && (SANITIZER_FREEBSD || SANITIZER_MAC)
# define internal_syscall __syscall
# else
# define internal_syscall syscall
#endif

#endif

bool internal_iserror(uptr retval, int *rverrno) {
  if (retval == (uptr)-1) {
    if (rverrno)
      *rverrno = errno;
    return true;
  } else {
    return false;
  }
}
