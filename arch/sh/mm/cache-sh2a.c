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

void __flush_wback_region(void *start, int size)
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
			unsigned long data =  ctrl_inl(addr | (way << 11));
			if ((data & CACHE_PHYSADDR_MASK) == (v & CACHE_PHYSADDR_MASK)) {
				data &= ~SH_CACHE_UPDATED;
				ctrl_outl(data, addr | (way << 11));
			}
		}
	}

	back_to_cached();
	local_irq_restore(flags);
}

void __flush_purge_region(void *start, int size)
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
		ctrl_outl((v & CACHE_PHYSADDR_MASK),
			  CACHE_OC_ADDRESS_ARRAY | (v & 0x000007f0) | 0x00000008);
	}
	back_to_cached();
	local_irq_restore(flags);
}

void __flush_invalidate_region(void *start, int size)
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
	ctrl_outl(ctrl_inl(CCR) | CCR_OCACHE_INVALIDATE, CCR);
	/* I-cache invalidate */
	for (v = begin; v < end; v+=L1_CACHE_BYTES) {
		ctrl_outl((v & CACHE_PHYSADDR_MASK),
			  CACHE_IC_ADDRESS_ARRAY | (v & 0x000007f0) | 0x00000008);
	}
#else
	for (v = begin; v < end; v+=L1_CACHE_BYTES) {
		ctrl_outl((v & CACHE_PHYSADDR_MASK),
			  CACHE_IC_ADDRESS_ARRAY | (v & 0x000007f0) | 0x00000008);
		ctrl_outl((v & CACHE_PHYSADDR_MASK),
			  CACHE_OC_ADDRESS_ARRAY | (v & 0x000007f0) | 0x00000008);
	}
#endif
	back_to_cached();
	local_irq_restore(flags);
}

/* WBack O-Cache and flush I-Cache */
void flush_icache_range(unsigned long start, unsigned long end)
{
	unsigned long v;
	unsigned long flags;

	start = start & ~(L1_CACHE_BYTES-1);
	end = (end + L1_CACHE_BYTES-1) & ~(L1_CACHE_BYTES-1);

	local_irq_save(flags);
	jump_to_uncached();

	for (v = start; v < end; v+=L1_CACHE_BYTES) {
		unsigned long addr = (v & 0x000007f0);
		int way;
		/* O-Cache writeback */
		for (way = 0; way < 4; way++) {
			unsigned long data =  ctrl_inl(CACHE_OC_ADDRESS_ARRAY | addr | (way << 11));
			if ((data & CACHE_PHYSADDR_MASK) == (v & CACHE_PHYSADDR_MASK)) {
				data &= ~SH_CACHE_UPDATED;
				ctrl_outl(data, CACHE_OC_ADDRESS_ARRAY | addr | (way << 11));
			}
		}
		/* I-Cache invalidate */
		ctrl_outl(addr,
			  CACHE_IC_ADDRESS_ARRAY | addr | 0x00000008);
	}

	back_to_cached();
	local_irq_restore(flags);
}
