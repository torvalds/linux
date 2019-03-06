// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SYNC_FILE_RANGE2

/* Use the standard ABI for syscalls */
#include <asm-generic/unistd.h>

/* Additional NDS32 specific syscalls. */
#define __NR_cacheflush		(__NR_arch_specific_syscall)
#define __NR_udftrap		(__NR_arch_specific_syscall + 1)
__SYSCALL(__NR_cacheflush, sys_cacheflush)
__SYSCALL(__NR_udftrap, sys_udftrap)
