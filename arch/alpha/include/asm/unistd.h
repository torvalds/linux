/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ALPHA_UNISTD_H
#define _ALPHA_UNISTD_H

#include <uapi/asm/unistd.h>

#define NR_SYSCALLS	__NR_syscalls

#define __ARCH_WANT_NEW_STAT
#define __ARCH_WANT_OLD_READDIR
#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SYS_GETHOSTNAME
#define __ARCH_WANT_SYS_FADVISE64
#define __ARCH_WANT_SYS_GETPGRP
#define __ARCH_WANT_SYS_OLDUMOUNT
#define __ARCH_WANT_SYS_SIGPENDING
#define __ARCH_WANT_SYS_UTIME
#define __ARCH_WANT_SYS_FORK
#define __ARCH_WANT_SYS_VFORK
#define __ARCH_WANT_SYS_CLONE

/*
 * Ignore legacy syscalls that we don't use.
 */
#define __IGNORE_alarm
#define __IGNORE_creat
#define __IGNORE_getegid
#define __IGNORE_geteuid
#define __IGNORE_getgid
#define __IGNORE_getpid
#define __IGNORE_getppid
#define __IGNORE_getuid
#define __IGNORE_pause
#define __IGNORE_time
#define __IGNORE_utime
#define __IGNORE_umount2

/* Alpha doesn't have protection keys. */
#define __IGNORE_pkey_mprotect
#define __IGNORE_pkey_alloc
#define __IGNORE_pkey_free

#endif /* _ALPHA_UNISTD_H */
