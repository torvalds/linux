/*
 * arch/sh/mm/cache-sh3.c
 *
 * Copyright (C) 1999, 2000  Niibe Yutaka
 * Copyright (C) 2002 Paul Mundt
 *
 * Released under the terms of the GNU GPL v2.0.
 */

#include <linux/init.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/threads.h>
#include <asm/addrspace.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/cache.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>

/*
 * Write back the dirty D-caches, but not invalidate them.
 *
 * Is this really worth it, or should we just alias this routine
 * to __flush_purge_region too?
 *
 * START: Virtual Address (U0, P1, or P3)
 * SIZE: Size of the region.
 */

void __flush_wback_region(void *start, int size)
{
	unsigned long v, j;
	unsigned long begin, end;
	unsigned long flags;

	begin = (unsigned long)start & ~(L1_CACHE_BYTES-1);
	end = ((unsigned long)start + size + L1_CACHE_BYTES-1)
		& ~(L1_CACHE_BYTES-1);

	for (v = begin; v < end; v+=L1_CACHE_BYTES) {
		unsigned long addrstart = CACHE_OC_ADDRESS_ARRAY;
		for (j = 0; j < cpu_data->dcache.ways; j++) {
			unsigned long data, addr, p;

			p = __pa(v);
			addr = addrstart | (v & cpu_data->dcache.entry_mask);
			local_irq_save(flags);
			data = ctrl_inl(addr);

			if ((data & CACHE_PHYSADDR_MASK) ==
			    (p & CACHE_PHYSADDR_MASK)) {
				data &= ~SH_CACHE_UPDATED;
				ctrl_outl(data, addr);
				local_irq_restore(flags);
				break;
			}
			local_irq_restore(flags);
			addrstart += cpu_data->dcache.way_incr;
		}
	}
}

/*
 * Write back the dirty D-caches and invalidate them.
 *
 * START: Virtual Address (U0, P1, or P3)
 * SIZE: Size of the region.
 */
void __flush_purge_region(void *start, int size)
{
	unsigned long v;
	unsigned long begin, end;

	begin = (unsigned long)start & ~(L1_CACHE_BYTES-1);
	end = ((unsigned long)start + size + L1_CACHE_BYTES-1)
		& ~(L1_CACHE_BYTES-1);

	for (v = begin; v < end; v+=L1_CACHE_BYTES) {
		unsigned long data, addr;

		data = (v & 0xfffffc00); /* _Virtual_ address, ~U, ~V */
		addr = CACHE_OC_ADDRESS_ARRAY |
			(v & cpu_data->dcache.entry_mask) | SH_CACHE_ASSOC;
		ctrl_outl(data, addr);
	}
}

/*
 * No write back please
 *
 * Except I don't think there's any way to avoid the writeback. So we
 * just alias it to __flush_purge_region(). dwmw2.
 */
void __flush_invalidate_region(void *start, int size)
	__attribute__((alias("__flush_purge_region")));
