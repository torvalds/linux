/*
 * arch/sh/mm/pg-sh7705.c
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

static inline void __flush_purge_virtual_region(void *p1, void *virt, int size)
{
	unsigned long v;
	unsigned long begin, end;
	unsigned long p1_begin;


	begin = L1_CACHE_ALIGN((unsigned long)virt);
	end = L1_CACHE_ALIGN((unsigned long)virt + size);

	p1_begin = (unsigned long)p1 & ~(L1_CACHE_BYTES - 1);

	/* do this the slow way as we may not have TLB entries
	 * for virt yet. */
	for (v = begin; v < end; v += L1_CACHE_BYTES) {
		unsigned long p;
	        unsigned long ways, addr;

		p = __pa(p1_begin);

	        ways = cpu_data->dcache.ways;
		addr = CACHE_OC_ADDRESS_ARRAY;

		do {
			unsigned long data;

			addr |= (v & cpu_data->dcache.entry_mask);

			data = ctrl_inl(addr);
			if ((data & CACHE_PHYSADDR_MASK) ==
			       (p & CACHE_PHYSADDR_MASK)) {
				data &= ~(SH_CACHE_UPDATED|SH_CACHE_VALID);
				ctrl_outl(data, addr);
			}

			addr += cpu_data->dcache.way_incr;
		} while (--ways);

		p1_begin += L1_CACHE_BYTES;
	}
}

/*
 * clear_user_page
 * @to: P1 address
 * @address: U0 address to be mapped
 */
void clear_user_page(void *to, unsigned long address, struct page *pg)
{
	struct page *page = virt_to_page(to);

	__set_bit(PG_mapped, &page->flags);
	if (((address ^ (unsigned long)to) & CACHE_ALIAS) == 0) {
		clear_page(to);
		__flush_wback_region(to, PAGE_SIZE);
	} else {
		__flush_purge_virtual_region(to,
					     (void *)(address & 0xfffff000),
					     PAGE_SIZE);
		clear_page(to);
		__flush_wback_region(to, PAGE_SIZE);
	}
}

/*
 * copy_user_page
 * @to: P1 address
 * @from: P1 address
 * @address: U0 address to be mapped
 */
void copy_user_page(void *to, void *from, unsigned long address, struct page *pg)
{
	struct page *page = virt_to_page(to);


	__set_bit(PG_mapped, &page->flags);
	if (((address ^ (unsigned long)to) & CACHE_ALIAS) == 0) {
		copy_page(to, from);
		__flush_wback_region(to, PAGE_SIZE);
	} else {
		__flush_purge_virtual_region(to,
					     (void *)(address & 0xfffff000),
					     PAGE_SIZE);
		copy_page(to, from);
		__flush_wback_region(to, PAGE_SIZE);
	}
}

/*
 * For SH7705, we have our own implementation for ptep_get_and_clear
 * Copied from pg-sh4.c
 */
inline pte_t ptep_get_and_clear(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	pte_t pte = *ptep;

	pte_clear(mm, addr, ptep);
	if (!pte_not_present(pte)) {
		unsigned long pfn = pte_pfn(pte);
		if (pfn_valid(pfn)) {
			struct page *page = pfn_to_page(pfn);
			struct address_space *mapping = page_mapping(page);
			if (!mapping || !mapping_writably_mapped(mapping))
				__clear_bit(PG_mapped, &page->flags);
		}
	}

	return pte;
}

