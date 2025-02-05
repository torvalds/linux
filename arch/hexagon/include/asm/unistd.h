/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SYS_CLONE
#define __ARCH_WANT_SYS_VFORK
#define __ARCH_WANT_SYS_FORK

#define __ARCH_BROKEN_SYS_CLONE3

#include <uapi/asm/unistd.h>
