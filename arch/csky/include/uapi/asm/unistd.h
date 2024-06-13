/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#define __ARCH_WANT_STAT64
#define __ARCH_WANT_NEW_STAT
#define __ARCH_WANT_SYS_CLONE
#define __ARCH_WANT_SYS_CLONE3
#define __ARCH_WANT_SET_GET_RLIMIT
#define __ARCH_WANT_TIME32_SYSCALLS
#include <asm-generic/unistd.h>

#define __NR_set_thread_area	(__NR_arch_specific_syscall + 0)
__SYSCALL(__NR_set_thread_area, sys_set_thread_area)
#define __NR_cacheflush		(__NR_arch_specific_syscall + 1)
__SYSCALL(__NR_cacheflush, sys_cacheflush)
