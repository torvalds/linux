/* MN10300 System call number list
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_UNISTD_H
#define _ASM_UNISTD_H

#include <uapi/asm/unistd.h>


#define NR_syscalls 340

/*
 * specify the deprecated syscalls we want to support on this arch
 */
#define __ARCH_WANT_OLD_READDIR
#define __ARCH_WANT_OLD_STAT
#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SYS_ALARM
#define __ARCH_WANT_SYS_GETHOSTNAME
#define __ARCH_WANT_SYS_IPC
#define __ARCH_WANT_SYS_PAUSE
#define __ARCH_WANT_SYS_SGETMASK
#define __ARCH_WANT_SYS_SIGNAL
#define __ARCH_WANT_SYS_TIME
#define __ARCH_WANT_SYS_UTIME
#define __ARCH_WANT_SYS_WAITPID
#define __ARCH_WANT_SYS_SOCKETCALL
#define __ARCH_WANT_SYS_FADVISE64
#define __ARCH_WANT_SYS_GETPGRP
#define __ARCH_WANT_SYS_LLSEEK
#define __ARCH_WANT_SYS_NICE
#define __ARCH_WANT_SYS_OLD_GETRLIMIT
#define __ARCH_WANT_SYS_OLD_SELECT
#define __ARCH_WANT_SYS_OLDUMOUNT
#define __ARCH_WANT_SYS_SIGPENDING
#define __ARCH_WANT_SYS_SIGPROCMASK
#define __ARCH_WANT_SYS_FORK
#define __ARCH_WANT_SYS_VFORK
#define __ARCH_WANT_SYS_CLONE

#endif /* _ASM_UNISTD_H */
