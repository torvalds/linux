//===-- sanitizer_syscall_linux_aarch64.inc --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implementations of internal_syscall and internal_iserror for Linux/aarch64.
//
//===----------------------------------------------------------------------===//

#define SYSCALL(name) __NR_ ## name

static uptr __internal_syscall(u64 nr) {
  register u64 x8 asm("x8") = nr;
  register u64 x0 asm("x0");
  asm volatile("svc 0"
               : "=r"(x0)
               : "r"(x8)
               : "memory", "cc");
  return x0;
}
#define __internal_syscall0(n) \
  (__internal_syscall)(n)

static uptr __internal_syscall(u64 nr, u64 arg1) {
  register u64 x8 asm("x8") = nr;
  register u64 x0 asm("x0") = arg1;
  asm volatile("svc 0"
               : "=r"(x0)
               : "r"(x8), "0"(x0)
               : "memory", "cc");
  return x0;
}
#define __internal_syscall1(n, a1) \
  (__internal_syscall)(n, (u64)(a1))

static uptr __internal_syscall(u64 nr, u64 arg1, long arg2) {
  register u64 x8 asm("x8") = nr;
  register u64 x0 asm("x0") = arg1;
  register u64 x1 asm("x1") = arg2;
  asm volatile("svc 0"
               : "=r"(x0)
               : "r"(x8), "0"(x0), "r"(x1)
               : "memory", "cc");
  return x0;
}
#define __internal_syscall2(n, a1, a2) \
  (__internal_syscall)(n, (u64)(a1), (long)(a2))

static uptr __internal_syscall(u64 nr, u64 arg1, long arg2, long arg3) {
  register u64 x8 asm("x8") = nr;
  register u64 x0 asm("x0") = arg1;
  register u64 x1 asm("x1") = arg2;
  register u64 x2 asm("x2") = arg3;
  asm volatile("svc 0"
               : "=r"(x0)
               : "r"(x8), "0"(x0), "r"(x1), "r"(x2)
               : "memory", "cc");
  return x0;
}
#define __internal_syscall3(n, a1, a2, a3) \
  (__internal_syscall)(n, (u64)(a1), (long)(a2), (long)(a3))

static uptr __internal_syscall(u64 nr, u64 arg1, long arg2, long arg3,
                               u64 arg4) {
  register u64 x8 asm("x8") = nr;
  register u64 x0 asm("x0") = arg1;
  register u64 x1 asm("x1") = arg2;
  register u64 x2 asm("x2") = arg3;
  register u64 x3 asm("x3") = arg4;
  asm volatile("svc 0"
               : "=r"(x0)
               : "r"(x8), "0"(x0), "r"(x1), "r"(x2), "r"(x3)
               : "memory", "cc");
  return x0;
}
#define __internal_syscall4(n, a1, a2, a3, a4) \
  (__internal_syscall)(n, (u64)(a1), (long)(a2), (long)(a3), (long)(a4))

static uptr __internal_syscall(u64 nr, u64 arg1, long arg2, long arg3,
                               u64 arg4, long arg5) {
  register u64 x8 asm("x8") = nr;
  register u64 x0 asm("x0") = arg1;
  register u64 x1 asm("x1") = arg2;
  register u64 x2 asm("x2") = arg3;
  register u64 x3 asm("x3") = arg4;
  register u64 x4 asm("x4") = arg5;
  asm volatile("svc 0"
               : "=r"(x0)
               : "r"(x8), "0"(x0), "r"(x1), "r"(x2), "r"(x3), "r"(x4)
               : "memory", "cc");
  return x0;
}
#define __internal_syscall5(n, a1, a2, a3, a4, a5) \
  (__internal_syscall)(n, (u64)(a1), (long)(a2), (long)(a3), (long)(a4), \
                       (u64)(a5))

static uptr __internal_syscall(u64 nr, u64 arg1, long arg2, long arg3,
                               u64 arg4, long arg5, long arg6) {
  register u64 x8 asm("x8") = nr;
  register u64 x0 asm("x0") = arg1;
  register u64 x1 asm("x1") = arg2;
  register u64 x2 asm("x2") = arg3;
  register u64 x3 asm("x3") = arg4;
  register u64 x4 asm("x4") = arg5;
  register u64 x5 asm("x5") = arg6;
  asm volatile("svc 0"
               : "=r"(x0)
               : "r"(x8), "0"(x0), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
               : "memory", "cc");
  return x0;
}
#define __internal_syscall6(n, a1, a2, a3, a4, a5, a6) \
  (__internal_syscall)(n, (u64)(a1), (long)(a2), (long)(a3), (long)(a4), \
                       (u64)(a5), (long)(a6))

#define __SYSCALL_NARGS_X(a1, a2, a3, a4, a5, a6, a7, a8, n, ...) n
#define __SYSCALL_NARGS(...) \
  __SYSCALL_NARGS_X(__VA_ARGS__, 7, 6, 5, 4, 3, 2, 1, 0, )
#define __SYSCALL_CONCAT_X(a, b) a##b
#define __SYSCALL_CONCAT(a, b) __SYSCALL_CONCAT_X(a, b)
#define __SYSCALL_DISP(b, ...) \
  __SYSCALL_CONCAT(b, __SYSCALL_NARGS(__VA_ARGS__))(__VA_ARGS__)

#define internal_syscall(...) __SYSCALL_DISP(__internal_syscall, __VA_ARGS__)

// Helper function used to avoid cobbler errno.
bool internal_iserror(uptr retval, int *rverrno) {
  if (retval >= (uptr)-4095) {
    if (rverrno)
      *rverrno = -retval;
    return true;
  }
  return false;
}
