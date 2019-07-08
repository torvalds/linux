// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>

#include <asm/pgtable.h>
#include <asm/set_memory.h>
#include <asm/tlbflush.h>

struct page_change_data {
	pgprot_t set_mask;
	pgprot_t clear_mask;
};

bool rodata_full __ro_after_init = IS_ENABLED(CONFIG_RODATA_FULL_DEFAULT_ENABLED);

static int change_page_range(pte_t *ptep, pgtable_t token, unsigned long addr,
			void *data)
{
	struct page_change_data *cdata = data;
	pte_t pte = READ_ONCE(*ptep);

	pte = clear_pte_bit(pte, cdata->clear_mask);
	pte = set_pte_bit(pte, cdata->set_mask);

	set_pte(ptep, pte);
	return 0;
}

/*
 * This function assumes that the range is mapped with PAGE_SIZE pages.
 */
static int __change_memory_common(unsigned long start, unsigned long size,
				pgprot_t set_mask, pgprot_t clear_mask)
{
	struct page_change_data data;
	int ret;

	data.set_mask = set_mask;
	data.clear_mask = clear_mask;

	ret = apply_to_page_range(&init_mm, start, size, change_page_range,
					&data);

	flush_tlb_kernel_range(start, start + size);
	return ret;
}

static int change_memory_common(unsigned long addr, int numpages,
				pgprot_t set_mask, pgprot_t clear_mask)
{
	unsigned long start = addr;
	unsigned long size = PAGE_SIZE*numpages;
	unsigned long end = start + size;
	struct vm_struct *area;
	int i;

	if (!PAGE_ALIGNED(addr)) {
		start &= PAGE_MASK;
		end = start + size;
		WARN_ON_ONCE(1);
	}

	/*
	 * Kernel VA mappings are always live, and splitting live section
	 * mappings into page mappings may cause TLB conflicts. This means
	 * we have to ensure that changing the permission bits of the range
	 * we are operating on does not result in such splitting.
	 *
	 * Let's restrict ourselves to mappings created by vmalloc (or vmap).
	 * Those are guaranteed to consist entirely of page mappings, and
	 * splitting is never needed.
	 *
	 * So check whether the [addr, addr + size) interval is entirely
	 * covered by precisely one VM area that has the VM_ALLOC flag set.
	 */
	area = find_vm_area((void *)addr);
	if (!area ||
	    end > (unsigned long)area->addr + area->size ||
	    !(area->flags & VM_ALLOC))
		return -EINVAL;

	if (!numpages)
		return 0;

	/*
	 * If we are manipulating read-only permissions, apply the same
	 * change to the linear mapping of the pages that back this VM area.
	 */
	if (rodata_full && (pgprot_val(set_mask) == PTE_RDONLY ||
			    pgprot_val(clear_mask) == PTE_RDONLY)) {
		for (i = 0; i < area->nr_pages; i++) {
			__change_memory_common((u64)page_address(area->pages[i]),
					       PAGE_SIZE, set_mask, clear_mask);
		}
	}

	/*
	 * Get rid of potentially aliasing lazily unmapped vm areas that may
	 * have permissions set that deviate from the ones we are setting here.
	 */
	vm_unmap_aliases();

	return __change_memory_common(start, size, set_mask, clear_mask);
}

int set_memory_ro(unsigned long addr, int numpages)
{
	return change_memory_common(addr, numpages,
					__pgprot(PTE_RDONLY),
					__pgprot(PTE_WRITE));
}

int set_memory_rw(unsigned long addr, int numpages)
{
	return change_memory_common(addr, numpages,
					__pgprot(PTE_WRITE),
					__pgprot(PTE_RDONLY));
}

int set_memory_nx(unsigned long addr, int numpages)
{
	return change_memory_common(addr, numpages,
					__pgprot(PTE_PXN),
					__pgprot(0));
}
EXPORT_SYMBOL_GPL(set_memory_nx);

int set_memory_x(unsigned long addr, int numpages)
{
	return change_memory_common(addr, numpages,
					__pgprot(0),
					__pgprot(PTE_PXN));
}
EXPORT_SYMBOL_GPL(set_memory_x);

int set_memory_valid(unsigned long addr, int numpages, int enable)
{
	if (enable)
		return __change_memory_common(addr, PAGE_SIZE * numpages,
					__pgprot(PTE_VALID),
					__pgprot(0));
	else
		return __change_memory_common(addr, PAGE_SIZE * numpages,
					__pgprot(0),
					__pgprot(PTE_VALID));
}

int set_direct_map_invalid_noflush(struct page *page)
{
	struct page_change_data data = {
		.set_mask = __pgprot(0),
		.clear_mask = __pgprot(PTE_VALID),
	};

	if (!rodata_full)
		return 0;

	return apply_to_page_range(&init_mm,
				   (unsigned long)page_address(page),
				   PAGE_SIZE, change_page_range, &data);
}

int set_direct_map_default_noflush(struct page *page)
{
	struct page_change_data data = {
		.set_mask = __pgprot(PTE_VALID | PTE_WRITE),
		.clear_mask = __pgprot(PTE_RDONLY),
	};

	if (!rodata_full)
		return 0;

	return apply_to_page_range(&init_mm,
				   (unsigned long)page_address(page),
				   PAGE_SIZE, change_page_range, &data);
}

void __kernel_map_pages(struct page *page, int numpages, int enable)
{
	if (!debug_pagealloc_enabled() && !rodata_full)
		return;

	set_memory_valid((unsigned long)page_address(page), numpages, enable);
}

/*
 * This function is used to determine if a linear map page has been marked as
 * not-valid. Walk the page table and check the PTE_VALID bit. This is based
 * on kern_addr_valid(), which almost does what we need.
 *
 * Because this is only called on the kernel linear map,  p?d_sect() implies
 * p?d_present(). When debug_pagealloc is enabled, sections mappings are
 * disabled.
 */
bool kernel_page_present(struct page *page)
{
	pgd_t *pgdp;
	pud_t *pudp, pud;
	pmd_t *pmdp, pmd;
	pte_t *ptep;
	unsigned long addr = (unsigned long)page_address(page);

	if (!debug_pagealloc_enabled() && !rodata_full)
		return true;

	pgdp = pgd_offset_k(addr);
	if (pgd_none(READ_ONCE(*pgdp)))
		return false;

	pudp = pud_offset(pgdp, addr);
	pud = READ_ONCE(*pudp);
	if (pud_none(pud))
		return false;
	if (pud_sect(pud))
		return true;

	pmdp = pmd_offset(pudp, addr);
	pmd = READ_ONCE(*pmdp);
	if (pmd_none(pmd))
		return false;
	if (pmd_sect(pmd))
		return true;

	ptep = pte_offset_kernel(pmdp, addr);
	return pte_valid(READ_ONCE(*ptep));
}
