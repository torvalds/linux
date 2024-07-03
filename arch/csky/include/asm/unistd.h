/* SPDX-License-Identifier: GPL-2.0 */

#include <uapi/asm/unistd.h>

#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SYS_CLONE

#define NR_syscalls (__NR_syscalls)
