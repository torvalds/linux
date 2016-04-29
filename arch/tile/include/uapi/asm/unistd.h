/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#define __ARCH_WANT_RENAMEAT
#if !defined(__LP64__) || defined(__SYSCALL_COMPAT)
/* Use the flavor of this syscall that matches the 32-bit API better. */
#define __ARCH_WANT_SYNC_FILE_RANGE2
#endif

/* Use the standard ABI for syscalls. */
#include <asm-generic/unistd.h>

#define NR_syscalls __NR_syscalls

/* Additional Tilera-specific syscalls. */
#define __NR_cacheflush	(__NR_arch_specific_syscall + 1)
__SYSCALL(__NR_cacheflush, sys_cacheflush)

#ifndef __tilegx__
/* "Fast" syscalls provide atomic support for 32-bit chips. */
#define __NR_FAST_cmpxchg	-1
#define __NR_FAST_atomic_update	-2
#define __NR_FAST_cmpxchg64	-3
#define __NR_cmpxchg_badaddr	(__NR_arch_specific_syscall + 0)
__SYSCALL(__NR_cmpxchg_badaddr, sys_cmpxchg_badaddr)
#endif
