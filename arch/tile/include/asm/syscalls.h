/*
 * syscalls.h - Linux syscall interfaces (arch-specific)
 *
 * Copyright (c) 2008 Jaswinder Singh Rajput
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

#ifndef _ASM_TILE_SYSCALLS_H
#define _ASM_TILE_SYSCALLS_H

#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/signal.h>
#include <linux/types.h>
#include <asm-generic/syscalls.h>

/* kernel/sys.c */
ssize_t sys32_readahead(int fd, u32 offset_lo, u32 offset_hi, u32 count);
long sys32_fadvise64(int fd, u32 offset_lo, u32 offset_hi,
		     u32 len, int advice);
int sys32_fadvise64_64(int fd, u32 offset_lo, u32 offset_hi,
		       u32 len_lo, u32 len_hi, int advice);
long sys_flush_cache(void);

#ifndef __tilegx__
/* mm/fault.c */
int sys_cmpxchg_badaddr(unsigned long address, struct pt_regs *);
#endif

#endif /* _ASM_TILE_SYSCALLS_H */
