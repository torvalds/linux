/*
 * Based on arch/arm/mm/flush.c
 *
 * Copyright (C) 1995-2002 Russell King
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/export.h>
#include <linux/mm.h>
#include <linux/pagemap.h>

#include <asm/cacheflush.h>
#include <asm/cachetype.h>
#include <asm/tlbflush.h>

#include "mm.h"

void flush_cache_range(struct vm_area_struct *vma, unsigned long start,
		       unsigned long end)
{
	if (vma->vm_flags & VM_EXEC)
		__flush_icache_all();
}

static void flush_ptrace_access(struct vm_area_struct *vma, struct page *page,
				unsigned long uaddr, void *kaddr,
				unsigned long len)
{
	if (vma->vm_flags & VM_EXEC) {
		unsigned long addr = (unsigned long)kaddr;
		if (icache_is_aliasing()) {
			__flush_dcache_area(kaddr, len);
			__flush_icache_all();
		} else {
			flush_icache_range(addr, addr + len);
		}
	}
}

/*
 * Copy user data from/to a page which is mapped into a different processes
 * address space.  Really, we want to allow our "user space" model to handle
 * this.
 *
 * Note that this code needs to run on the current CPU.
 */
void copy_to_user_page(struct vm_area_struct *vma, struct page *page,
		       unsigned long uaddr, void *dst, const void *src,
		       unsigned long len)
{
#ifdef CONFIG_SMP
	preempt_disable();
#endif
	memcpy(dst, src, len);
	flush_ptrace_access(vma, page, uaddr, dst, len);
#ifdef CONFIG_SMP
	preempt_enable();
#endif
}

void __flush_dcache_page(struct page *page)
{
	__flush_dcache_area(page_address(page), PAGE_SIZE);
}

void __sync_icache_dcache(pte_t pte, unsigned long addr)
{
	unsigned long pfn;
	struct page *page;

	pfn = pte_pfn(pte);
	if (!pfn_valid(pfn))
		return;

	page = pfn_to_page(pfn);
	if (!test_and_set_bit(PG_dcache_clean, &page->flags)) {
		__flush_dcache_page(page);
		__flush_icache_all();
	} else if (icache_is_aivivt()) {
		__flush_icache_all();
	}
}

/*
 * This function is called when a page has been modified by the kernel. Mark
 * it as dirty for later flushing when mapped in user space (if executable,
 * see __sync_icache_dcache).
 */
void flush_dcache_page(struct page *page)
{
	if (test_bit(PG_dcache_clean, &page->flags))
		clear_bit(PG_dcache_clean, &page->flags);
}
EXPORT_SYMBOL(flush_dcache_page);

/*
 * Additional functions defined in assembly.
 */
EXPORT_SYMBOL(flush_cache_all);
EXPORT_SYMBOL(flush_icache_range);
