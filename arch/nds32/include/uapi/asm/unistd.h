/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
// Copyright (C) 2005-2017 Andes Technology Corporation

#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SYNC_FILE_RANGE2
#define __ARCH_WANT_SET_GET_RLIMIT
#define __ARCH_WANT_TIME32_SYSCALLS

/* Use the standard ABI for syscalls */
#include <asm-generic/unistd.h>

/* Additional NDS32 specific syscalls. */
#define __NR_cacheflush		(__NR_arch_specific_syscall)
#define __NR_fp_udfiex_crtl	(__NR_arch_specific_syscall + 1)
__SYSCALL(__NR_cacheflush, sys_cacheflush)
__SYSCALL(__NR_fp_udfiex_crtl, sys_fp_udfiex_crtl)
