/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#define __ARCH_WANT_SYS_CLONE

#ifdef CONFIG_COMPAT
#define __ARCH_WANT_COMPAT_TRUNCATE64
#define __ARCH_WANT_COMPAT_FTRUNCATE64
#define __ARCH_WANT_COMPAT_FALLOCATE
#define __ARCH_WANT_COMPAT_PREAD64
#define __ARCH_WANT_COMPAT_PWRITE64
#define __ARCH_WANT_COMPAT_SYNC_FILE_RANGE
#define __ARCH_WANT_COMPAT_READAHEAD
#define __ARCH_WANT_COMPAT_FADVISE64_64
#endif

#if defined(__LP64__) && !defined(__SYSCALL_COMPAT)
#define __ARCH_WANT_NEW_STAT
#define __ARCH_WANT_SET_GET_RLIMIT
#endif /* __LP64__ */

#define __ARCH_WANT_MEMFD_SECRET


#include <uapi/asm/unistd.h>

#define NR_syscalls (__NR_syscalls)
