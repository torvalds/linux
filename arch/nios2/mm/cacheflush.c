/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2009, Wind River Systems Inc
 * Implemented by fredrik.markstrom@gmail.com and ivarholmqvist@gmail.com
 */

#include <linux/export.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>

#include <asm/cacheflush.h>
#include <asm/cpuinfo.h>

static void __flush_dcache(unsigned long start, unsigned long end)
{
	unsigned long addr;

	start &= ~(cpuinfo.dcache_line_size - 1);
	end += (cpuinfo.dcache_line_size - 1);
	end &= ~(cpuinfo.dcache_line_size - 1);

	for (addr = start; addr < end; addr += cpuinfo.dcache_line_size) {
		__asm__ __volatile__ ("   flushda 0(%0)\n"
					: /* Outputs */
					: /* Inputs  */ "r"(addr)
					/* : No clobber */);
	}
}

static void __flush_dcache_all(unsigned long start, unsigned long end)
{
	unsigned long addr;

	start &= ~(cpuinfo.dcache_line_size - 1);
	end += (cpuinfo.dcache_line_size - 1);
	end &= ~(cpuinfo.dcache_line_size - 1);

	if (end > start + cpuinfo.dcache_size)
		end = start + cpuinfo.dcache_size;

	for (addr = start; addr < end; addr += cpuinfo.dcache_line_size) {
		__asm__ __volatile__ ("   flushd 0(%0)\n"
					: /* Outputs */
					: /* Inputs  */ "r"(addr)
					/* : No clobber */);
	}
}

static void __invalidate_dcache(unsigned long start, unsigned long end)
{
	unsigned long addr;

	start &= ~(cpuinfo.dcache_line_size - 1);
	end += (cpuinfo.dcache_line_size - 1);
	end &= ~(cpuinfo.dcache_line_size - 1);

	if (end > start + cpuinfo.dcache_size)
		end = start + cpuinfo.dcache_size;

	for (addr = start; addr < end; addr += cpuinfo.dcache_line_size) {
		__asm__ __volatile__ ("   initda 0(%0)\n"
					: /* Outputs */
					: /* Inputs  */ "r"(addr)
					/* : No clobber */);
	}
}

static void __flush_icache(unsigned long start, unsigned long end)
{
	unsigned long addr;

	start &= ~(cpuinfo.icache_line_size - 1);
	end += (cpuinfo.icache_line_size - 1);
	end &= ~(cpuinfo.icache_line_size - 1);

	if (end > start + cpuinfo.icache_size)
		end = start + cpuinfo.icache_size;

	for (addr = start; addr < end; addr += cpuinfo.icache_line_size) {
		__asm__ __volatile__ ("   flushi %0\n"
					: /* Outputs */
					: /* Inputs  */ "r"(addr)
					/* : No clobber */);
	}
	__asm__ __volatile(" flushp\n");
}

static void flush_aliases(struct address_space *mapping, struct page *page)
{
	struct mm_struct *mm = current->active_mm;
	struct vm_area_struct *mpnt;
	pgoff_t pgoff;

	pgoff = page->index;

	flush_dcache_mmap_lock(mapping);
	vma_interval_tree_foreach(mpnt, &mapping->i_mmap, pgoff, pgoff) {
		unsigned long offset;

		if (mpnt->vm_mm != mm)
			continue;
		if (!(mpnt->vm_flags & VM_MAYSHARE))
			continue;

		offset = (pgoff - mpnt->vm_pgoff) << PAGE_SHIFT;
		flush_cache_page(mpnt, mpnt->vm_start + offset,
			page_to_pfn(page));
	}
	flush_dcache_mmap_unlock(mapping);
}

void flush_cache_all(void)
{
	__flush_dcache_all(0, cpuinfo.dcache_size);
	__flush_icache(0, cpuinfo.icache_size);
}

void flush_cache_mm(struct mm_struct *mm)
{
	flush_cache_all();
}

void flush_cache_dup_mm(struct mm_struct *mm)
{
	flush_cache_all();
}

void flush_icache_range(unsigned long start, unsigned long end)
{
	__flush_icache(start, end);
}

void flush_dcache_range(unsigned long start, unsigned long end)
{
	__flush_dcache(start, end);
}
EXPORT_SYMBOL(flush_dcache_range);

void invalidate_dcache_range(unsigned long start, unsigned long end)
{
	__invalidate_dcache(start, end);
}
EXPORT_SYMBOL(invalidate_dcache_range);

void flush_cache_range(struct vm_area_struct *vma, unsigned long start,
			unsigned long end)
{
	__flush_dcache(start, end);
	if (vma == NULL || (vma->vm_flags & VM_EXEC))
		__flush_icache(start, end);
}

void flush_icache_page(struct vm_area_struct *vma, struct page *page)
{
	unsigned long start = (unsigned long) page_address(page);
	unsigned long end = start + PAGE_SIZE;

	__flush_icache(start, end);
}

void flush_cache_page(struct vm_area_struct *vma, unsigned long vmaddr,
			unsigned long pfn)
{
	unsigned long start = vmaddr;
	unsigned long end = start + PAGE_SIZE;

	__flush_dcache(start, end);
	if (vma->vm_flags & VM_EXEC)
		__flush_icache(start, end);
}

void flush_dcache_page(struct page *page)
{
	struct address_space *mapping;

	/*
	 * The zero page is never written to, so never has any dirty
	 * cache lines, and therefore never needs to be flushed.
	 */
	if (page == ZERO_PAGE(0))
		return;

	mapping = page_mapping(page);

	/* Flush this page if there are aliases. */
	if (mapping && !mapping_mapped(mapping)) {
		clear_bit(PG_dcache_clean, &page->flags);
	} else {
		unsigned long start = (unsigned long)page_address(page);

		__flush_dcache_all(start, start + PAGE_SIZE);
		if (mapping)
			flush_aliases(mapping,  page);
		set_bit(PG_dcache_clean, &page->flags);
	}
}
EXPORT_SYMBOL(flush_dcache_page);

void update_mmu_cache(struct vm_area_struct *vma,
		      unsigned long address, pte_t *pte)
{
	unsigned long pfn = pte_pfn(*pte);
	struct page *page;

	if (!pfn_valid(pfn))
		return;

	/*
	* The zero page is never written to, so never has any dirty
	* cache lines, and therefore never needs to be flushed.
	*/
	page = pfn_to_page(pfn);
	if (page == ZERO_PAGE(0))
		return;

	if (!PageReserved(page) &&
	     !test_and_set_bit(PG_dcache_clean, &page->flags)) {
		unsigned long start = page_to_virt(page);
		struct address_space *mapping;

		__flush_dcache(start, start + PAGE_SIZE);

		mapping = page_mapping(page);
		if (mapping)
			flush_aliases(mapping, page);
	}
}

void copy_user_page(void *vto, void *vfrom, unsigned long vaddr,
		    struct page *to)
{
	__flush_dcache(vaddr, vaddr + PAGE_SIZE);
	copy_page(vto, vfrom);
	__flush_dcache((unsigned long)vto, (unsigned long)vto + PAGE_SIZE);
}

void clear_user_page(void *addr, unsigned long vaddr, struct page *page)
{
	__flush_dcache(vaddr, vaddr + PAGE_SIZE);
	clear_page(addr);
	__flush_dcache((unsigned long)addr, (unsigned long)addr + PAGE_SIZE);
}

void copy_from_user_page(struct vm_area_struct *vma, struct page *page,
			unsigned long user_vaddr,
			void *dst, void *src, int len)
{
	flush_cache_page(vma, user_vaddr, page_to_pfn(page));
	memcpy(dst, src, len);
	__flush_dcache((unsigned long)src, (unsigned long)src + len);
	if (vma->vm_flags & VM_EXEC)
		__flush_icache((unsigned long)src, (unsigned long)src + len);
}

void copy_to_user_page(struct vm_area_struct *vma, struct page *page,
			unsigned long user_vaddr,
			void *dst, void *src, int len)
{
	flush_cache_page(vma, user_vaddr, page_to_pfn(page));
	memcpy(dst, src, len);
	__flush_dcache((unsigned long)dst, (unsigned long)dst + len);
	if (vma->vm_flags & VM_EXEC)
		__flush_icache((unsigned long)dst, (unsigned long)dst + len);
}
