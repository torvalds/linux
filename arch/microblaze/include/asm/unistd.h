/*
 * Copyright (C) 2007-2008 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef _ASM_MICROBLAZE_UNISTD_H
#define _ASM_MICROBLAZE_UNISTD_H

#include <uapi/asm/unistd.h>

#ifndef __ASSEMBLY__

/* #define __ARCH_WANT_OLD_READDIR */
/* #define __ARCH_WANT_OLD_STAT */
#define __ARCH_WANT_NEW_STAT
#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SYS_ALARM
#define __ARCH_WANT_SYS_GETHOSTNAME
#define __ARCH_WANT_SYS_PAUSE
#define __ARCH_WANT_SYS_SIGNAL
#define __ARCH_WANT_SYS_TIME
#define __ARCH_WANT_SYS_UTIME
#define __ARCH_WANT_SYS_WAITPID
#define __ARCH_WANT_SYS_SOCKETCALL
#define __ARCH_WANT_SYS_FADVISE64
#define __ARCH_WANT_SYS_GETPGRP
#define __ARCH_WANT_SYS_NICE
/* #define __ARCH_WANT_SYS_OLD_GETRLIMIT */
#define __ARCH_WANT_SYS_OLDUMOUNT
#define __ARCH_WANT_SYS_SIGPENDING
#define __ARCH_WANT_SYS_SIGPROCMASK
#define __ARCH_WANT_SYS_CLONE
#define __ARCH_WANT_SYS_VFORK
#define __ARCH_WANT_SYS_FORK

#endif /* __ASSEMBLY__ */

#define __NR_syscalls         401

#endif /* _ASM_MICROBLAZE_UNISTD_H */
