/*
 * arch/score/mm/cache.c
 *
 * Score Processor version.
 *
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Lennox Wu <lennox.wu@sunplusct.com>
 *  Chen Liqin <liqin.chen@sunplusct.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>

#include <asm/mmu_context.h>

/*
Just flush entire Dcache!!
You must ensure the page doesn't include instructions, because
the function will not flush the Icache.
The addr must be cache aligned.
*/
static void flush_data_cache_page(unsigned long addr)
{
	unsigned int i;
	for (i = 0; i < (PAGE_SIZE / L1_CACHE_BYTES); i += L1_CACHE_BYTES) {
		__asm__ __volatile__(
		"cache 0x0e, [%0, 0]\n"
		"cache 0x1a, [%0, 0]\n"
		"nop\n"
		: : "r" (addr));
		addr += L1_CACHE_BYTES;
	}
}

/* called by update_mmu_cache. */
void __update_cache(struct vm_area_struct *vma, unsigned long address,
		pte_t pte)
{
	struct page *page;
	unsigned long pfn, addr;
	int exec = (vma->vm_flags & VM_EXEC);

	pfn = pte_pfn(pte);
	if (unlikely(!pfn_valid(pfn)))
		return;
	page = pfn_to_page(pfn);
	if (page_mapping(page) && test_bit(PG_arch_1, &page->flags)) {
		addr = (unsigned long) page_address(page);
		if (exec)
			flush_data_cache_page(addr);
		clear_bit(PG_arch_1, &page->flags);
	}
}

static inline void setup_protection_map(void)
{
	protection_map[0] = PAGE_NONE;
	protection_map[1] = PAGE_READONLY;
	protection_map[2] = PAGE_COPY;
	protection_map[3] = PAGE_COPY;
	protection_map[4] = PAGE_READONLY;
	protection_map[5] = PAGE_READONLY;
	protection_map[6] = PAGE_COPY;
	protection_map[7] = PAGE_COPY;
	protection_map[8] = PAGE_NONE;
	protection_map[9] = PAGE_READONLY;
	protection_map[10] = PAGE_SHARED;
	protection_map[11] = PAGE_SHARED;
	protection_map[12] = PAGE_READONLY;
	protection_map[13] = PAGE_READONLY;
	protection_map[14] = PAGE_SHARED;
	protection_map[15] = PAGE_SHARED;
}

void __devinit cpu_cache_init(void)
{
	setup_protection_map();
}

void flush_icache_all(void)
{
	__asm__ __volatile__(
	"la r8, flush_icache_all\n"
	"cache 0x10, [r8, 0]\n"
	"nop\nnop\nnop\nnop\nnop\nnop\n"
	: : : "r8");
}

void flush_dcache_all(void)
{
	__asm__ __volatile__(
	"la r8, flush_dcache_all\n"
	"cache 0x1f, [r8, 0]\n"
	"nop\nnop\nnop\nnop\nnop\nnop\n"
	"cache 0x1a, [r8, 0]\n"
	"nop\nnop\nnop\nnop\nnop\nnop\n"
	: : : "r8");
}

void flush_cache_all(void)
{
	__asm__ __volatile__(
	"la r8, flush_cache_all\n"
	"cache 0x10, [r8, 0]\n"
	"nop\nnop\nnop\nnop\nnop\nnop\n"
	"cache 0x1f, [r8, 0]\n"
	"nop\nnop\nnop\nnop\nnop\nnop\n"
	"cache 0x1a, [r8, 0]\n"
	"nop\nnop\nnop\nnop\nnop\nnop\n"
	: : : "r8");
}

void flush_cache_mm(struct mm_struct *mm)
{
	if (!(mm->context))
		return;
	flush_cache_all();
}

/*if we flush a range precisely , the processing may be very long.
We must check each page in the range whether present. If the page is present,
we can flush the range in the page. Be careful, the range may be cross two
page, a page is present and another is not present.
*/
/*
The interface is provided in hopes that the port can find
a suitably efficient method for removing multiple page
sized regions from the cache.
*/
void flush_cache_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;
	int exec = vma->vm_flags & VM_EXEC;
	pgd_t *pgdp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;

	if (!(mm->context))
		return;

	pgdp = pgd_offset(mm, start);
	pudp = pud_offset(pgdp, start);
	pmdp = pmd_offset(pudp, start);
	ptep = pte_offset(pmdp, start);

	while (start <= end) {
		unsigned long tmpend;
		pgdp = pgd_offset(mm, start);
		pudp = pud_offset(pgdp, start);
		pmdp = pmd_offset(pudp, start);
		ptep = pte_offset(pmdp, start);

		if (!(pte_val(*ptep) & _PAGE_PRESENT)) {
			start = (start + PAGE_SIZE) & ~(PAGE_SIZE - 1);
			continue;
		}
		tmpend = (start | (PAGE_SIZE-1)) > end ?
				 end : (start | (PAGE_SIZE-1));

		flush_dcache_range(start, tmpend);
		if (exec)
			flush_icache_range(start, tmpend);
		start = (start + PAGE_SIZE) & ~(PAGE_SIZE - 1);
	}
}

void flush_cache_page(struct vm_area_struct *vma,
		unsigned long addr, unsigned long pfn)
{
	int exec = vma->vm_flags & VM_EXEC;
	unsigned long kaddr = 0xa0000000 | (pfn << PAGE_SHIFT);

	flush_dcache_range(kaddr, kaddr + PAGE_SIZE);

	if (exec)
		flush_icache_range(kaddr, kaddr + PAGE_SIZE);
}

void flush_cache_sigtramp(unsigned long addr)
{
	__asm__ __volatile__(
	"cache 0x02, [%0, 0]\n"
	"nop\nnop\nnop\nnop\nnop\n"
	"cache 0x02, [%0, 0x4]\n"
	"nop\nnop\nnop\nnop\nnop\n"

	"cache 0x0d, [%0, 0]\n"
	"nop\nnop\nnop\nnop\nnop\n"
	"cache 0x0d, [%0, 0x4]\n"
	"nop\nnop\nnop\nnop\nnop\n"

	"cache 0x1a, [%0, 0]\n"
	"nop\nnop\nnop\nnop\nnop\n"
	: : "r" (addr));
}

/*
1. WB and invalid a cache line of Dcache
2. Drain Write Buffer
the range must be smaller than PAGE_SIZE
*/
void flush_dcache_range(unsigned long start, unsigned long end)
{
	int size, i;

	start = start & ~(L1_CACHE_BYTES - 1);
	end = end & ~(L1_CACHE_BYTES - 1);
	size = end - start;
	/* flush dcache to ram, and invalidate dcache lines. */
	for (i = 0; i < size; i += L1_CACHE_BYTES) {
		__asm__ __volatile__(
		"cache 0x0e, [%0, 0]\n"
		"nop\nnop\nnop\nnop\nnop\n"
		"cache 0x1a, [%0, 0]\n"
		"nop\nnop\nnop\nnop\nnop\n"
		: : "r" (start));
		start += L1_CACHE_BYTES;
	}
}

void flush_icache_range(unsigned long start, unsigned long end)
{
	int size, i;
	start = start & ~(L1_CACHE_BYTES - 1);
	end = end & ~(L1_CACHE_BYTES - 1);

	size = end - start;
	/* invalidate icache lines. */
	for (i = 0; i < size; i += L1_CACHE_BYTES) {
		__asm__ __volatile__(
		"cache 0x02, [%0, 0]\n"
		"nop\nnop\nnop\nnop\nnop\n"
		: : "r" (start));
		start += L1_CACHE_BYTES;
	}
}
