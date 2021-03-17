// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/sh/mm/cache-sh2.c
 *
 * Copyright (C) 2002 Paul Mundt
 * Copyright (C) 2008 Yoshinori Sato
 */

#include <linux/init.h>
#include <linux/mm.h>

#include <asm/cache.h>
#include <asm/addrspace.h>
#include <asm/processor.h>
#include <asm/cacheflush.h>
#include <asm/io.h>

static void sh2__flush_wback_region(void *start, int size)
{
	unsigned long v;
	unsigned long begin, end;

	begin = (unsigned long)start & ~(L1_CACHE_BYTES-1);
	end = ((unsigned long)start + size + L1_CACHE_BYTES-1)
		& ~(L1_CACHE_BYTES-1);
	for (v = begin; v < end; v+=L1_CACHE_BYTES) {
		unsigned long addr = CACHE_OC_ADDRESS_ARRAY | (v & 0x00000ff0);
		int way;
		for (way = 0; way < 4; way++) {
			unsigned long data =  __raw_readl(addr | (way << 12));
			if ((data & CACHE_PHYSADDR_MASK) == (v & CACHE_PHYSADDR_MASK)) {
				data &= ~SH_CACHE_UPDATED;
				__raw_writel(data, addr | (way << 12));
			}
		}
	}
}

static void sh2__flush_purge_region(void *start, int size)
{
	unsigned long v;
	unsigned long begin, end;

	begin = (unsigned long)start & ~(L1_CACHE_BYTES-1);
	end = ((unsigned long)start + size + L1_CACHE_BYTES-1)
		& ~(L1_CACHE_BYTES-1);

	for (v = begin; v < end; v+=L1_CACHE_BYTES)
		__raw_writel((v & CACHE_PHYSADDR_MASK),
			  CACHE_OC_ADDRESS_ARRAY | (v & 0x00000ff0) | 0x00000008);
}

static void sh2__flush_invalidate_region(void *start, int size)
{
#ifdef CONFIG_CACHE_WRITEBACK
	/*
	 * SH-2 does not support individual line invalidation, only a
	 * global invalidate.
	 */
	unsigned long ccr;
	unsigned long flags;
	local_irq_save(flags);
	jump_to_uncached();

	ccr = __raw_readl(SH_CCR);
	ccr |= CCR_CACHE_INVALIDATE;
	__raw_writel(ccr, SH_CCR);

	back_to_cached();
	local_irq_restore(flags);
#else
	unsigned long v;
	unsigned long begin, end;

	begin = (unsigned long)start & ~(L1_CACHE_BYTES-1);
	end = ((unsigned long)start + size + L1_CACHE_BYTES-1)
		& ~(L1_CACHE_BYTES-1);

	for (v = begin; v < end; v+=L1_CACHE_BYTES)
		__raw_writel((v & CACHE_PHYSADDR_MASK),
			  CACHE_OC_ADDRESS_ARRAY | (v & 0x00000ff0) | 0x00000008);
#endif
}

void __init sh2_cache_init(void)
{
	__flush_wback_region		= sh2__flush_wback_region;
	__flush_purge_region		= sh2__flush_purge_region;
	__flush_invalidate_region	= sh2__flush_invalidate_region;
}
