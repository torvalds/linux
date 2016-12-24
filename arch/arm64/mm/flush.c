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

void flush_cache_range(struct vm_area_struct *vma, unsigned long start,
		       unsigned long end)
{
	if (vma->vm_flags & VM_EXEC)
		__flush_icache_all();
}

static void sync_icache_aliases(void *kaddr, unsigned long len)
{
	unsigned long addr = (unsigned long)kaddr;

	if (icache_is_aliasing()) {
		__clean_dcache_area_pou(kaddr, len);
		__flush_icache_all();
	} else {
		flush_icache_range(addr, addr + len);
	}
}

static void flush_ptrace_access(struct vm_area_struct *vma, struct page *page,
				unsigned long uaddr, void *kaddr,
				unsigned long len)
{
	if (vma->vm_flags & VM_EXEC)
		sync_icache_aliases(kaddr, len);
}

/*
 * Copy user data from/to a page which is mapped into a different processes
 * address space.  Really, we want to allow our "user space" model to handle
 * this.
 */
void copy_to_user_page(struct vm_area_struct *vma, struct page *page,
		       unsigned long uaddr, void *dst, const void *src,
		       unsigned long len)
{
	memcpy(dst, src, len);
	flush_ptrace_access(vma, page, uaddr, dst, len);
}

void __sync_icache_dcache(pte_t pte, unsigned long addr)
{
	struct page *page = pte_page(pte);

	if (!test_and_set_bit(PG_dcache_clean, &page->flags))
		sync_icache_aliases(page_address(page),
				    PAGE_SIZE << compound_order(page));
	else if (icache_is_aivivt())
		__flush_icache_all();
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
EXPORT_SYMBOL(flush_icache_range);
