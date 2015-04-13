/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifdef CONFIG_COMPAT
#define __ARCH_WANT_COMPAT_SYS_GETDENTS64
#define __ARCH_WANT_COMPAT_STAT64
#define __ARCH_WANT_SYS_GETHOSTNAME
#define __ARCH_WANT_SYS_PAUSE
#define __ARCH_WANT_SYS_GETPGRP
#define __ARCH_WANT_SYS_LLSEEK
#define __ARCH_WANT_SYS_NICE
#define __ARCH_WANT_SYS_SIGPENDING
#define __ARCH_WANT_SYS_SIGPROCMASK
#define __ARCH_WANT_COMPAT_SYS_SENDFILE
#define __ARCH_WANT_SYS_FORK
#define __ARCH_WANT_SYS_VFORK

/*
 * Compat syscall numbers used by the AArch64 kernel.
 */
#define __NR_compat_restart_syscall	0
#define __NR_compat_exit		1
#define __NR_compat_read		3
#define __NR_compat_write		4
#define __NR_compat_sigreturn		119
#define __NR_compat_rt_sigreturn	173

/*
 * The following SVCs are ARM private.
 */
#define __ARM_NR_COMPAT_BASE		0x0f0000
#define __ARM_NR_compat_cacheflush	(__ARM_NR_COMPAT_BASE+2)
#define __ARM_NR_compat_set_tls		(__ARM_NR_COMPAT_BASE+5)

#define __NR_compat_syscalls		388
#endif

#define __ARCH_WANT_SYS_CLONE

#ifndef __COMPAT_SYSCALL_NR
#include <uapi/asm/unistd.h>
#endif

#define NR_syscalls (__NR_syscalls)
