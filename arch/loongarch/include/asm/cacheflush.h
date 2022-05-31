/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_CACHEFLUSH_H
#define _ASM_CACHEFLUSH_H

#include <linux/mm.h>
#include <asm/cpu-features.h>
#include <asm/cacheops.h>

extern void local_flush_icache_range(unsigned long start, unsigned long end);

#define flush_icache_range	local_flush_icache_range
#define flush_icache_user_range	local_flush_icache_range

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 0

#define flush_cache_all()				do { } while (0)
#define flush_cache_mm(mm)				do { } while (0)
#define flush_cache_dup_mm(mm)				do { } while (0)
#define flush_cache_range(vma, start, end)		do { } while (0)
#define flush_cache_page(vma, vmaddr, pfn)		do { } while (0)
#define flush_cache_vmap(start, end)			do { } while (0)
#define flush_cache_vunmap(start, end)			do { } while (0)
#define flush_icache_page(vma, page)			do { } while (0)
#define flush_icache_user_page(vma, page, addr, len)	do { } while (0)
#define flush_dcache_page(page)				do { } while (0)
#define flush_dcache_mmap_lock(mapping)			do { } while (0)
#define flush_dcache_mmap_unlock(mapping)		do { } while (0)

#define cache_op(op, addr)						\
	__asm__ __volatile__(						\
	"	cacop	%0, %1					\n"	\
	:								\
	: "i" (op), "ZC" (*(unsigned char *)(addr)))

static inline void flush_icache_line_indexed(unsigned long addr)
{
	cache_op(Index_Invalidate_I, addr);
}

static inline void flush_dcache_line_indexed(unsigned long addr)
{
	cache_op(Index_Writeback_Inv_D, addr);
}

static inline void flush_vcache_line_indexed(unsigned long addr)
{
	cache_op(Index_Writeback_Inv_V, addr);
}

static inline void flush_scache_line_indexed(unsigned long addr)
{
	cache_op(Index_Writeback_Inv_S, addr);
}

static inline void flush_icache_line(unsigned long addr)
{
	cache_op(Hit_Invalidate_I, addr);
}

static inline void flush_dcache_line(unsigned long addr)
{
	cache_op(Hit_Writeback_Inv_D, addr);
}

static inline void flush_vcache_line(unsigned long addr)
{
	cache_op(Hit_Writeback_Inv_V, addr);
}

static inline void flush_scache_line(unsigned long addr)
{
	cache_op(Hit_Writeback_Inv_S, addr);
}

#include <asm-generic/cacheflush.h>

#endif /* _ASM_CACHEFLUSH_H */
