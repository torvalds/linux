/*
 * arch/sh/mm/cache-sh2.c
 *
 * Copyright (C) 2002 Paul Mundt
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

	begin = (unsigned long)start & ~(L1_CACHE_BYTES-1);
	end = ((unsigned long)start + size + L1_CACHE_BYTES-1)
		& ~(L1_CACHE_BYTES-1);
	for (v = begin; v < end; v+=L1_CACHE_BYTES) {
		unsigned long addr = CACHE_OC_ADDRESS_ARRAY | (v & 0x00000ff0);
		int way;
		for (way = 0; way < 4; way++) {
			unsigned long data =  ctrl_inl(addr | (way << 12));
			if ((data & CACHE_PHYSADDR_MASK) == (v & CACHE_PHYSADDR_MASK)) {
				data &= ~SH_CACHE_UPDATED;
				ctrl_outl(data, addr | (way << 12));
			}
		}
	}
}

void __flush_purge_region(void *start, int size)
{
	unsigned long v;
	unsigned long begin, end;

	begin = (unsigned long)start & ~(L1_CACHE_BYTES-1);
	end = ((unsigned long)start + size + L1_CACHE_BYTES-1)
		& ~(L1_CACHE_BYTES-1);

	for (v = begin; v < end; v+=L1_CACHE_BYTES)
		ctrl_outl((v & CACHE_PHYSADDR_MASK),
			  CACHE_OC_ADDRESS_ARRAY | (v & 0x00000ff0) | 0x00000008);
}

void __flush_invalidate_region(void *start, int size)
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

	ccr = ctrl_inl(CCR);
	ccr |= CCR_CACHE_INVALIDATE;
	ctrl_outl(ccr, CCR);

	back_to_cached();
	local_irq_restore(flags);
#else
	unsigned long v;
	unsigned long begin, end;

	begin = (unsigned long)start & ~(L1_CACHE_BYTES-1);
	end = ((unsigned long)start + size + L1_CACHE_BYTES-1)
		& ~(L1_CACHE_BYTES-1);

	for (v = begin; v < end; v+=L1_CACHE_BYTES)
		ctrl_outl((v & CACHE_PHYSADDR_MASK),
			  CACHE_OC_ADDRESS_ARRAY | (v & 0x00000ff0) | 0x00000008);
#endif
}
