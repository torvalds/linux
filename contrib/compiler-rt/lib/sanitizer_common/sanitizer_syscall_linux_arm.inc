//===-- sanitizer_syscall_linux_arm.inc -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implementations of internal_syscall and internal_iserror for Linux/arm.
//
//===----------------------------------------------------------------------===//

#define SYSCALL(name) __NR_ ## name

static uptr __internal_syscall(u32 nr) {
  register u32 r8 asm("r7") = nr;
  register u32 r0 asm("r0");
  asm volatile("swi #0"
               : "=r"(r0)
               : "r"(r8)
               : "memory", "cc");
  return r0;
}
#define __internal_syscall0(n) \
  (__internal_syscall)(n)

static uptr __internal_syscall(u32 nr, u32 arg1) {
  register u32 r8 asm("r7") = nr;
  register u32 r0 asm("r0") = arg1;
  asm volatile("swi #0"
               : "=r"(r0)
               : "r"(r8), "0"(r0)
               : "memory", "cc");
  return r0;
}
#define __internal_syscall1(n, a1) \
  (__internal_syscall)(n, (u32)(a1))

static uptr __internal_syscall(u32 nr, u32 arg1, long arg2) {
  register u32 r8 asm("r7") = nr;
  register u32 r0 asm("r0") = arg1;
  register u32 r1 asm("r1") = arg2;
  asm volatile("swi #0"
               : "=r"(r0)
               : "r"(r8), "0"(r0), "r"(r1)
               : "memory", "cc");
  return r0;
}
#define __internal_syscall2(n, a1, a2) \
  (__internal_syscall)(n, (u32)(a1), (long)(a2))

static uptr __internal_syscall(u32 nr, u32 arg1, long arg2, long arg3) {
  register u32 r8 asm("r7") = nr;
  register u32 r0 asm("r0") = arg1;
  register u32 r1 asm("r1") = arg2;
  register u32 r2 asm("r2") = arg3;
  asm volatile("swi #0"
               : "=r"(r0)
               : "r"(r8), "0"(r0), "r"(r1), "r"(r2)
               : "memory", "cc");
  return r0;
}
#define __internal_syscall3(n, a1, a2, a3) \
  (__internal_syscall)(n, (u32)(a1), (long)(a2), (long)(a3))

static uptr __internal_syscall(u32 nr, u32 arg1, long arg2, long arg3,
                               u32 arg4) {
  register u32 r8 asm("r7") = nr;
  register u32 r0 asm("r0") = arg1;
  register u32 r1 asm("r1") = arg2;
  register u32 r2 asm("r2") = arg3;
  register u32 r3 asm("r3") = arg4;
  asm volatile("swi #0"
               : "=r"(r0)
               : "r"(r8), "0"(r0), "r"(r1), "r"(r2), "r"(r3)
               : "memory", "cc");
  return r0;
}
#define __internal_syscall4(n, a1, a2, a3, a4) \
  (__internal_syscall)(n, (u32)(a1), (long)(a2), (long)(a3), (long)(a4))

static uptr __internal_syscall(u32 nr, u32 arg1, long arg2, long arg3,
                               u32 arg4, long arg5) {
  register u32 r8 asm("r7") = nr;
  register u32 r0 asm("r0") = arg1;
  register u32 r1 asm("r1") = arg2;
  register u32 r2 asm("r2") = arg3;
  register u32 r3 asm("r3") = arg4;
  register u32 r4 asm("r4") = arg5;
  asm volatile("swi #0"
               : "=r"(r0)
               : "r"(r8), "0"(r0), "r"(r1), "r"(r2), "r"(r3), "r"(r4)
               : "memory", "cc");
  return r0;
}
#define __internal_syscall5(n, a1, a2, a3, a4, a5) \
  (__internal_syscall)(n, (u32)(a1), (long)(a2), (long)(a3), (long)(a4), \
                       (u32)(a5))

static uptr __internal_syscall(u32 nr, u32 arg1, long arg2, long arg3,
                               u32 arg4, long arg5, long arg6) {
  register u32 r8 asm("r7") = nr;
  register u32 r0 asm("r0") = arg1;
  register u32 r1 asm("r1") = arg2;
  register u32 r2 asm("r2") = arg3;
  register u32 r3 asm("r3") = arg4;
  register u32 r4 asm("r4") = arg5;
  register u32 r5 asm("r5") = arg6;
  asm volatile("swi #0"
               : "=r"(r0)
               : "r"(r8), "0"(r0), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5)
               : "memory", "cc");
  return r0;
}
#define __internal_syscall6(n, a1, a2, a3, a4, a5, a6) \
  (__internal_syscall)(n, (u32)(a1), (long)(a2), (long)(a3), (long)(a4), \
                       (u32)(a5), (long)(a6))

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
