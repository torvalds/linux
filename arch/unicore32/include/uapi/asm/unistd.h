/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * linux/arch/unicore32/include/asm/unistd.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define __ARCH_WANT_RENAMEAT
#define __ARCH_WANT_SET_GET_RLIMIT
#define __ARCH_WANT_STAT64
#define __ARCH_WANT_TIME32_SYSCALLS

/* Use the standard ABI for syscalls. */
#include <asm-generic/unistd.h>
#define __ARCH_WANT_SYS_CLONE
