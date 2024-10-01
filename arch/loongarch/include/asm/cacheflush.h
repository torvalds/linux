/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_CACHEFLUSH_H
#define _ASM_CACHEFLUSH_H

#include <linux/mm.h>
#include <asm/cpu-info.h>
#include <asm/cacheops.h>

static inline bool cache_present(struct cache_desc *cdesc)
{
	return cdesc->flags & CACHE_PRESENT;
}

static inline bool cache_private(struct cache_desc *cdesc)
{
	return cdesc->flags & CACHE_PRIVATE;
}

static inline bool cache_inclusive(struct cache_desc *cdesc)
{
	return cdesc->flags & CACHE_INCLUSIVE;
}

static inline unsigned int cpu_last_level_cache_line_size(void)
{
	int cache_present = boot_cpu_data.cache_leaves_present;

	return boot_cpu_data.cache_leaves[cache_present - 1].linesz;
}

asmlinkage void __flush_cache_all(void);
void local_flush_icache_range(unsigned long start, unsigned long end);

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

static inline void flush_cache_line(int leaf, unsigned long addr)
{
	switch (leaf) {
	case Cache_LEAF0:
		cache_op(Index_Writeback_Inv_LEAF0, addr);
		break;
	case Cache_LEAF1:
		cache_op(Index_Writeback_Inv_LEAF1, addr);
		break;
	case Cache_LEAF2:
		cache_op(Index_Writeback_Inv_LEAF2, addr);
		break;
	case Cache_LEAF3:
		cache_op(Index_Writeback_Inv_LEAF3, addr);
		break;
	case Cache_LEAF4:
		cache_op(Index_Writeback_Inv_LEAF4, addr);
		break;
	case Cache_LEAF5:
		cache_op(Index_Writeback_Inv_LEAF5, addr);
		break;
	default:
		break;
	}
}

#include <asm-generic/cacheflush.h>

#endif /* _ASM_CACHEFLUSH_H */
