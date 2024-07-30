/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _XTENSA_UNISTD_H
#define _XTENSA_UNISTD_H

#define __ARCH_WANT_SYS_CLONE
#include <uapi/asm/unistd.h>

#define __ARCH_WANT_NEW_STAT
#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SYS_UTIME32
#define __ARCH_WANT_SYS_GETPGRP

#define NR_syscalls				__NR_syscalls

#endif /* _XTENSA_UNISTD_H */
