/*
 * Copyright (C) 2015 Regents of the University of California
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#ifndef _ASM_RISCV_CACHEFLUSH_H
#define _ASM_RISCV_CACHEFLUSH_H

#include <asm-generic/cacheflush.h>

#undef flush_icache_range
#undef flush_icache_user_range

static inline void local_flush_icache_all(void)
{
	asm volatile ("fence.i" ::: "memory");
}

#ifndef CONFIG_SMP

#define flush_icache_range(start, end) local_flush_icache_all()
#define flush_icache_user_range(vma, pg, addr, len) local_flush_icache_all()

#else /* CONFIG_SMP */

#define flush_icache_range(start, end) sbi_remote_fence_i(0)
#define flush_icache_user_range(vma, pg, addr, len) sbi_remote_fence_i(0)

#endif /* CONFIG_SMP */

#endif /* _ASM_RISCV_CACHEFLUSH_H */
