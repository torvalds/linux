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

			data = ctrl_inl(addr);

			if ((data & v) == v)
				ctrl_outl(data & ~v, addr);

		}

		addrstart += current_cpu_data.dcache.way_incr;
	} while (--ways);
}

/*
 * Write back the range of D-cache, and purge the I-cache.
 *
 * Called from kernel/module.c:sys_init_module and routine for a.out format.
 */
void flush_icache_range(unsigned long start, unsigned long end)
{
	__flush_wback_region((void *)start, end - start);
}

/*
 * Writeback&Invalidate the D-cache of the page
 */
static void __uses_jump_to_uncached __flush_dcache_page(unsigned long phys)
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

			data = ctrl_inl(addr) & (0x1ffffC00 | SH_CACHE_VALID);
		        if (data == phys) {
				data &= ~(SH_CACHE_VALID | SH_CACHE_UPDATED);
				ctrl_outl(data, addr);
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
void flush_dcache_page(struct page *page)
{
	if (test_bit(PG_mapped, &page->flags))
		__flush_dcache_page(PHYSADDR(page_address(page)));
}

void __uses_jump_to_uncached flush_cache_all(void)
{
	unsigned long flags;

	local_irq_save(flags);
	jump_to_uncached();

	cache_wback_all();
	back_to_cached();
	local_irq_restore(flags);
}

void flush_cache_mm(struct mm_struct *mm)
{
	/* Is there any good way? */
	/* XXX: possibly call flush_cache_range for each vm area */
	flush_cache_all();
}

/*
 * Write back and invalidate D-caches.
 *
 * START, END: Virtual Address (U0 address)
 *
 * NOTE: We need to flush the _physical_ page entry.
 * Flushing the cache lines for U0 only isn't enough.
 * We need to flush for P1 too, which may contain aliases.
 */
void flush_cache_range(struct vm_area_struct *vma, unsigned long start,
		       unsigned long end)
{

	/*
	 * We could call flush_cache_page for the pages of these range,
	 * but it's not efficient (scan the caches all the time...).
	 *
	 * We can't use A-bit magic, as there's the case we don't have
	 * valid entry on TLB.
	 */
	flush_cache_all();
}

/*
 * Write back and invalidate I/D-caches for the page.
 *
 * ADDRESS: Virtual Address (U0 address)
 */
void flush_cache_page(struct vm_area_struct *vma, unsigned long address,
		      unsigned long pfn)
{
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
void flush_icache_page(struct vm_area_struct *vma, struct page *page)
{
	__flush_purge_region(page_address(page), PAGE_SIZE);
}
