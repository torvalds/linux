/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * This file contains the system call numbers.
 */
#ifndef _ASM_POWERPC_UNISTD_H_
#define _ASM_POWERPC_UNISTD_H_

#include <uapi/asm/unistd.h>

#define NR_syscalls	__NR_syscalls

#ifndef __ASSEMBLER__

#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/linkage.h>

#define __ARCH_WANT_NEW_STAT
#define __ARCH_WANT_OLD_READDIR
#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SYS_ALARM
#define __ARCH_WANT_SYS_GETHOSTNAME
#define __ARCH_WANT_SYS_IPC
#define __ARCH_WANT_SYS_PAUSE
#define __ARCH_WANT_SYS_SIGNAL
#define __ARCH_WANT_SYS_TIME32
#define __ARCH_WANT_SYS_UTIME32
#define __ARCH_WANT_SYS_WAITPID
#define __ARCH_WANT_SYS_SOCKETCALL
#define __ARCH_WANT_SYS_FADVISE64
#define __ARCH_WANT_SYS_GETPGRP
#define __ARCH_WANT_SYS_LLSEEK
#define __ARCH_WANT_SYS_NICE
#define __ARCH_WANT_SYS_OLD_GETRLIMIT
#define __ARCH_WANT_SYS_OLD_UNAME
#define __ARCH_WANT_SYS_OLDUMOUNT
#define __ARCH_WANT_SYS_SIGPENDING
#define __ARCH_WANT_SYS_SIGPROCMASK
#ifdef CONFIG_PPC32
#define __ARCH_WANT_OLD_STAT
#define __ARCH_WANT_SYS_OLD_SELECT
#endif
#ifdef CONFIG_PPC64
#define __ARCH_WANT_SYS_TIME
#define __ARCH_WANT_SYS_UTIME
#define __ARCH_WANT_SYS_NEWFSTATAT
#define __ARCH_WANT_COMPAT_STAT
#define __ARCH_WANT_COMPAT_FALLOCATE
#define __ARCH_WANT_COMPAT_SYS_SENDFILE
#endif
#define __ARCH_WANT_SYS_FORK
#define __ARCH_WANT_SYS_VFORK
#define __ARCH_WANT_SYS_CLONE

#endif		/* __ASSEMBLER__ */
#endif /* _ASM_POWERPC_UNISTD_H_ */
