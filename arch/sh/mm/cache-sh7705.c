/*
 * arch/sh/mm/cache-sh7705.c
 *
 * Copyright (C) 1999, 2000  Niibe Yutaka
 * Copyright (C) 2004  Alex Song
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */
#include <linux/init.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/threads.h>
#include <asm/addrspace.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/cache.h>
#include <asm/io.h>
#include <linux/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>

/*
 * The 32KB cache on the SH7705 suffers from the same synonym problem
 * as SH4 CPUs
 */
static inline void cache_wback_all(void)
{
	unsigned long ways, waysize, addrstart;

	ways = current_cpu_data.dcache.ways;
	waysize = current_cpu_data.dcache.sets;
	waysize <<= current_cpu_data.dcache.entry_shift;

	addrstart = CACHE_OC_ADDRESS_ARRAY;

	do {
		unsigned long addr;

		for (addr = addrstart;
		     addr < addrstart + waysize;
		     addr += current_cpu_data.dcache.linesz) {
			unsigned long data;
			int v = SH_CACHE_UPDATED | SH_CACHE_VALID;

			data = __raw_readl(addr);

			if ((data & v) == v)
				__raw_writel(data & ~v, addr);

		}

		addrstart += current_cpu_data.dcache.way_incr;
	} while (--ways);
}

/*
 * Write back the range of D-cache, and purge the I-cache.
 *
 * Called from kernel/module.c:sys_init_module and routine for a.out format.
 */
static void sh7705_flush_icache_range(void *args)
{
	struct flusher_data *data = args;
	unsigned long start, end;

	start = data->addr1;
	end = data->addr2;

	__flush_wback_region((void *)start, end - start);
}

/*
 * Writeback&Invalidate the D-cache of the page
 */
static void __flush_dcache_page(unsigned long phys)
{
	unsigned long ways, waysize, addrstart;
	unsigned long flags;

	phys |= SH_CACHE_VALID;

	/*
	 * Here, phys is the physical address of the page. We check all the
	 * tags in the cache for those with the same page number as this page
	 * (by masking off the lowest 2 bits of the 19-bit tag; these bits are
	 * derived from the offset within in the 4k page). Matching valid
	 * entries are invalidated.
	 *
	 * Since 2 bits of the cache index are derived from the virtual page
	 * number, knowing this would reduce the number of cache entries to be
	 * searched by a factor of 4. However this function exists to deal with
	 * potential cache aliasing, therefore the optimisation is probably not
	 * possible.
	 */
	local_irq_save(flags);
	jump_to_uncached();

	ways = current_cpu_data.dcache.ways;
	waysize = current_cpu_data.dcache.sets;
	waysize <<= current_cpu_data.dcache.entry_shift;

	addrstart = CACHE_OC_ADDRESS_ARRAY;

	do {
		unsigned long addr;

		for (addr = addrstart;
		     addr < addrstart + waysize;
		     addr += current_cpu_data.dcache.linesz) {
			unsigned long data;

			data = __raw_readl(addr) & (0x1ffffC00 | SH_CACHE_VALID);
		        if (data == phys) {
				data &= ~(SH_CACHE_VALID | SH_CACHE_UPDATED);
				__raw_writel(data, addr);
			}
		}

		addrstart += current_cpu_data.dcache.way_incr;
	} while (--ways);

	back_to_cached();
	local_irq_restore(flags);
}

/*
 * Write back & invalidate the D-cache of the page.
 * (To avoid "alias" issues)
 */
static void sh7705_flush_dcache_folio(void *arg)
{
	struct folio *folio = arg;
	struct address_space *mapping = folio_flush_mapping(folio);

	if (mapping && !mapping_mapped(mapping))
		clear_bit(PG_dcache_clean, &folio->flags);
	else {
		unsigned long pfn = folio_pfn(folio);
		unsigned int i, nr = folio_nr_pages(folio);

		for (i = 0; i < nr; i++)
			__flush_dcache_page((pfn + i) * PAGE_SIZE);
	}
}

static void sh7705_flush_cache_all(void *args)
{
	unsigned long flags;

	local_irq_save(flags);
	jump_to_uncached();

	cache_wback_all();
	back_to_cached();
	local_irq_restore(flags);
}

/*
 * Write back and invalidate I/D-caches for the page.
 *
 * ADDRESS: Virtual Address (U0 address)
 */
static void sh7705_flush_cache_page(void *args)
{
	struct flusher_data *data = args;
	unsigned long pfn = data->addr2;

	__flush_dcache_page(pfn << PAGE_SHIFT);
}

/*
 * This is called when a page-cache page is about to be mapped into a
 * user process' address space.  It offers an opportunity for a
 * port to ensure d-cache/i-cache coherency if necessary.
 *
 * Not entirely sure why this is necessary on SH3 with 32K cache but
 * without it we get occasional "Memory fault" when loading a program.
 */
static void sh7705_flush_icache_folio(void *arg)
{
	struct folio *folio = arg;
	__flush_purge_region(folio_address(folio), folio_size(folio));
}

void __init sh7705_cache_init(void)
{
	local_flush_icache_range	= sh7705_flush_icache_range;
	local_flush_dcache_folio	= sh7705_flush_dcache_folio;
	local_flush_cache_all		= sh7705_flush_cache_all;
	local_flush_cache_mm		= sh7705_flush_cache_all;
	local_flush_cache_dup_mm	= sh7705_flush_cache_all;
	local_flush_cache_range		= sh7705_flush_cache_all;
	local_flush_cache_page		= sh7705_flush_cache_page;
	local_flush_icache_folio	= sh7705_flush_icache_folio;
}
