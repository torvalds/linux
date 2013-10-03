/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 96, 97, 98, 99, 2000 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 *
 * Changed system calls macros _syscall5 - _syscall7 to push args 5 to 7 onto
 * the stack. Robin Farine for ACN S.A, Copyright (C) 1996 by ACN S.A
 */
#ifndef _ASM_UNISTD_H
#define _ASM_UNISTD_H

#include <uapi/asm/unistd.h>


#ifndef __ASSEMBLY__

#define __ARCH_OMIT_COMPAT_SYS_GETDENTS64
#define __ARCH_WANT_OLD_READDIR
#define __ARCH_WANT_SYS_ALARM
#define __ARCH_WANT_SYS_GETHOSTNAME
#define __ARCH_WANT_SYS_IPC
#define __ARCH_WANT_SYS_PAUSE
#define __ARCH_WANT_SYS_SGETMASK
#define __ARCH_WANT_SYS_UTIME
#define __ARCH_WANT_SYS_WAITPID
#define __ARCH_WANT_SYS_SOCKETCALL
#define __ARCH_WANT_SYS_GETPGRP
#define __ARCH_WANT_SYS_LLSEEK
#define __ARCH_WANT_SYS_NICE
#define __ARCH_WANT_SYS_OLD_GETRLIMIT
#define __ARCH_WANT_SYS_OLD_UNAME
#define __ARCH_WANT_SYS_OLDUMOUNT
#define __ARCH_WANT_SYS_SIGPENDING
#define __ARCH_WANT_SYS_SIGPROCMASK
# ifdef CONFIG_32BIT
#  define __ARCH_WANT_STAT64
#  define __ARCH_WANT_SYS_TIME
# endif
# ifdef CONFIG_MIPS32_O32
#  define __ARCH_WANT_COMPAT_SYS_TIME
# endif
#define __ARCH_WANT_SYS_FORK
#define __ARCH_WANT_SYS_CLONE

/* whitelists for checksyscalls */
#define __IGNORE_select
#define __IGNORE_vfork
#define __IGNORE_time
#define __IGNORE_uselib
#define __IGNORE_fadvise64_64
#define __IGNORE_getdents64
#if _MIPS_SIM == _MIPS_SIM_NABI32
#define __IGNORE_truncate64
#define __IGNORE_ftruncate64
#define __IGNORE_stat64
#define __IGNORE_lstat64
#define __IGNORE_fstat64
#define __IGNORE_fstatat64
#endif

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_UNISTD_H */
