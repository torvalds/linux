/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifdef CONFIG_COMPAT
#define __ARCH_WANT_COMPAT_STAT
#define __ARCH_WANT_COMPAT_STAT64
#define __ARCH_WANT_SYS_GETHOSTNAME
#define __ARCH_WANT_SYS_PAUSE
#define __ARCH_WANT_SYS_GETPGRP
#define __ARCH_WANT_SYS_NICE
#define __ARCH_WANT_SYS_SIGPENDING
#define __ARCH_WANT_SYS_SIGPROCMASK
#define __ARCH_WANT_COMPAT_SYS_SENDFILE
#define __ARCH_WANT_SYS_UTIME32
#define __ARCH_WANT_SYS_FORK
#define __ARCH_WANT_SYS_VFORK

/*
 * The following SVCs are ARM private.
 */
#define __ARM_NR_COMPAT_BASE		0x0f0000
#define __ARM_NR_compat_cacheflush	(__ARM_NR_COMPAT_BASE + 2)
#define __ARM_NR_compat_set_tls		(__ARM_NR_COMPAT_BASE + 5)
#define __ARM_NR_COMPAT_END		(__ARM_NR_COMPAT_BASE + 0x800)
#endif

#define __ARCH_WANT_SYS_CLONE
#define __ARCH_WANT_NEW_STAT

#include <asm/unistd_64.h>

#define NR_syscalls (__NR_syscalls)
