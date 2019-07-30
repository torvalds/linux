/* SPDX-License-Identifier: GPL-2.0 */
/*
 * System calls under the Sparc.
 *
 * Don't be scared by the ugly clobbers, it is the only way I can
 * think of right now to force the arguments into fixed registers
 * before the trap into the system call with gcc 'asm' statements.
 *
 * Copyright (C) 1995, 2007 David S. Miller (davem@davemloft.net)
 *
 * SunOS compatibility based upon preliminary work which is:
 *
 * Copyright (C) 1995 Adrian M. Rodriguez (adrian@remus.rutgers.edu)
 */
#ifndef _SPARC_UNISTD_H
#define _SPARC_UNISTD_H

#include <uapi/asm/unistd.h>

#define NR_syscalls	__NR_syscalls

#ifdef __32bit_syscall_numbers__
#else
#define __NR_time		231 /* Linux sparc32                               */
#endif
#define __ARCH_WANT_NEW_STAT
#define __ARCH_WANT_OLD_READDIR
#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SYS_ALARM
#define __ARCH_WANT_SYS_GETHOSTNAME
#define __ARCH_WANT_SYS_PAUSE
#define __ARCH_WANT_SYS_SIGNAL
#define __ARCH_WANT_SYS_TIME32
#define __ARCH_WANT_SYS_UTIME32
#define __ARCH_WANT_SYS_WAITPID
#define __ARCH_WANT_SYS_SOCKETCALL
#define __ARCH_WANT_SYS_FADVISE64
#define __ARCH_WANT_SYS_GETPGRP
#define __ARCH_WANT_SYS_NICE
#define __ARCH_WANT_SYS_OLDUMOUNT
#define __ARCH_WANT_SYS_SIGPENDING
#define __ARCH_WANT_SYS_SIGPROCMASK
#ifdef __32bit_syscall_numbers__
#define __ARCH_WANT_SYS_IPC
#else
#define __ARCH_WANT_SYS_TIME
#define __ARCH_WANT_SYS_UTIME
#define __ARCH_WANT_COMPAT_SYS_SENDFILE
#endif

#ifdef __32bit_syscall_numbers__
/* Sparc 32-bit only has the "setresuid32", "getresuid32" variants,
 * it never had the plain ones and there is no value to adding those
 * old versions into the syscall table.
 */
#define __IGNORE_setresuid
#define __IGNORE_getresuid
#define __IGNORE_setresgid
#define __IGNORE_getresgid
#endif

#endif /* _SPARC_UNISTD_H */
