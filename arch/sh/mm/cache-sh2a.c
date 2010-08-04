/*
 * arch/sh/mm/cache-sh2a.c
 *
 * Copyright (C) 2008 Yoshinori Sato
 *
 * Released under the terms of the GNU GPL v2.0.
 */

#include <linux/init.h>
#include <linux/mm.h>

#include <asm/cache.h>
#include <asm/addrspace.h>
#include <asm/processor.h>
#include <asm/cacheflush.h>
#include <asm/io.h>

static void sh2a__flush_wback_region(void *start, int size)
{
	unsigned long v;
	unsigned long begin, end;
	unsigned long flags;

	begin = (unsigned long)start & ~(L1_CACHE_BYTES-1);
	end = ((unsigned long)start + size + L1_CACHE_BYTES-1)
		& ~(L1_CACHE_BYTES-1);

	local_irq_save(flags);
	jump_to_uncached();

	for (v = begin; v < end; v+=L1_CACHE_BYTES) {
		unsigned long addr = CACHE_OC_ADDRESS_ARRAY | (v & 0x000007f0);
		int way;
		for (way = 0; way < 4; way++) {
			unsigned long data =  __raw_readl(addr | (way << 11));
			if ((data & CACHE_PHYSADDR_MASK) == (v & CACHE_PHYSADDR_MASK)) {
				data &= ~SH_CACHE_UPDATED;
				__raw_writel(data, addr | (way << 11));
			}
		}
	}

	back_to_cached();
	local_irq_restore(flags);
}

static void sh2a__flush_purge_region(void *start, int size)
{
	unsigned long v;
	unsigned long begin, end;
	unsigned long flags;

	begin = (unsigned long)start & ~(L1_CACHE_BYTES-1);
	end = ((unsigned long)start + size + L1_CACHE_BYTES-1)
		& ~(L1_CACHE_BYTES-1);

	local_irq_save(flags);
	jump_to_uncached();

	for (v = begin; v < end; v+=L1_CACHE_BYTES) {
		__raw_writel((v & CACHE_PHYSADDR_MASK),
			  CACHE_OC_ADDRESS_ARRAY | (v & 0x000007f0) | 0x00000008);
	}
	back_to_cached();
	local_irq_restore(flags);
}

static void sh2a__flush_invalidate_region(void *start, int size)
{
	unsigned long v;
	unsigned long begin, end;
	unsigned long flags;

	begin = (unsigned long)start & ~(L1_CACHE_BYTES-1);
	end = ((unsigned long)start + size + L1_CACHE_BYTES-1)
		& ~(L1_CACHE_BYTES-1);
	local_irq_save(flags);
	jump_to_uncached();

#ifdef CONFIG_CACHE_WRITEBACK
	__raw_writel(__raw_readl(CCR) | CCR_OCACHE_INVALIDATE, CCR);
	/* I-cache invalidate */
	for (v = begin; v < end; v+=L1_CACHE_BYTES) {
		__raw_writel((v & CACHE_PHYSADDR_MASK),
			  CACHE_IC_ADDRESS_ARRAY | (v & 0x000007f0) | 0x00000008);
	}
#else
	for (v = begin; v < end; v+=L1_CACHE_BYTES) {
		__raw_writel((v & CACHE_PHYSADDR_MASK),
			  CACHE_IC_ADDRESS_ARRAY | (v & 0x000007f0) | 0x00000008);
		__raw_writel((v & CACHE_PHYSADDR_MASK),
			  CACHE_OC_ADDRESS_ARRAY | (v & 0x000007f0) | 0x00000008);
	}
#endif
	back_to_cached();
	local_irq_restore(flags);
}

/* WBack O-Cache and flush I-Cache */
static void sh2a_flush_icache_range(void *args)
{
	struct flusher_data *data = args;
	unsigned long start, end;
	unsigned long v;
	unsigned long flags;

	start = data->addr1 & ~(L1_CACHE_BYTES-1);
	end = (data->addr2 + L1_CACHE_BYTES-1) & ~(L1_CACHE_BYTES-1);

	local_irq_save(flags);
	jump_to_uncached();

	for (v = start; v < end; v+=L1_CACHE_BYTES) {
		unsigned long addr = (v & 0x000007f0);
		int way;
		/* O-Cache writeback */
		for (way = 0; way < 4; way++) {
			unsigned long data =  __raw_readl(CACHE_OC_ADDRESS_ARRAY | addr | (way << 11));
			if ((data & CACHE_PHYSADDR_MASK) == (v & CACHE_PHYSADDR_MASK)) {
				data &= ~SH_CACHE_UPDATED;
				__raw_writel(data, CACHE_OC_ADDRESS_ARRAY | addr | (way << 11));
			}
		}
		/* I-Cache invalidate */
		__raw_writel(addr,
			  CACHE_IC_ADDRESS_ARRAY | addr | 0x00000008);
	}

	back_to_cached();
	local_irq_restore(flags);
}

void __init sh2a_cache_init(void)
{
	local_flush_icache_range	= sh2a_flush_icache_range;

	__flush_wback_region		= sh2a__flush_wback_region;
	__flush_purge_region		= sh2a__flush_purge_region;
	__flush_invalidate_region	= sh2a__flush_invalidate_region;
}
