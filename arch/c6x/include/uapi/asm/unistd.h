/*
 * Copyright (C) 2011 Texas Instruments Incorporated
 *
 * Based on arch/tile version.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.	See the GNU General Public License for
 *   more details.
 */

#define __ARCH_WANT_RENAMEAT
#define __ARCH_WANT_SYS_CLONE

/* Use the standard ABI for syscalls. */
#include <asm-generic/unistd.h>

/* C6X-specific syscalls. */
#define __NR_cache_sync	(__NR_arch_specific_syscall + 0)
__SYSCALL(__NR_cache_sync, sys_cache_sync)
