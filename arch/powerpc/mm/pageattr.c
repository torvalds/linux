// SPDX-License-Identifier: GPL-2.0

/*
 * MMU-generic set_memory implementation for powerpc
 *
 * Copyright 2019-2021, IBM Corporation.
 */

#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/set_memory.h>

#include <asm/mmu.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#include <mm/mmu_decl.h>

static pte_basic_t pte_update_delta(pte_t *ptep, unsigned long addr,
				    unsigned long old, unsigned long new)
{
	return pte_update(&init_mm, addr, ptep, old & ~new, new & ~old, 0);
}

/*
 * Updates the attributes of a page atomically.
 *
 * This sequence is safe against concurrent updates, and also allows updating the
 * attributes of a page currently being executed or accessed.
 */
static int change_page_attr(pte_t *ptep, unsigned long addr, void *data)
{
	long action = (long)data;

	addr &= PAGE_MASK;
	/* modify the PTE bits as desired */
	switch (action) {
	case SET_MEMORY_RO:
		/* Don't clear DIRTY bit */
		pte_update_delta(ptep, addr, _PAGE_KERNEL_RW & ~_PAGE_DIRTY, _PAGE_KERNEL_RO);
		break;
	case SET_MEMORY_ROX:
		/* Don't clear DIRTY bit */
		pte_update_delta(ptep, addr, _PAGE_KERNEL_RW & ~_PAGE_DIRTY, _PAGE_KERNEL_ROX);
		break;
	case SET_MEMORY_RW:
		pte_update_delta(ptep, addr, _PAGE_KERNEL_RO, _PAGE_KERNEL_RW);
		break;
	case SET_MEMORY_NX:
		pte_update_delta(ptep, addr, _PAGE_KERNEL_ROX, _PAGE_KERNEL_RO);
		break;
	case SET_MEMORY_X:
		pte_update_delta(ptep, addr, _PAGE_KERNEL_RO, _PAGE_KERNEL_ROX);
		break;
	case SET_MEMORY_NP:
		pte_update(&init_mm, addr, ptep, _PAGE_PRESENT, 0, 0);
		break;
	case SET_MEMORY_P:
		pte_update(&init_mm, addr, ptep, 0, _PAGE_PRESENT, 0);
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}

	/* See ptesync comment in radix__set_pte_at() */
	if (radix_enabled())
		asm volatile("ptesync": : :"memory");

	flush_tlb_kernel_range(addr, addr + PAGE_SIZE);

	return 0;
}

int change_memory_attr(unsigned long addr, int numpages, long action)
{
	unsigned long start = ALIGN_DOWN(addr, PAGE_SIZE);
	unsigned long size = numpages * PAGE_SIZE;

	if (!numpages)
		return 0;

	if (WARN_ON_ONCE(is_vmalloc_or_module_addr((void *)addr) &&
			 is_vm_area_hugepages((void *)addr)))
		return -EINVAL;

#ifdef CONFIG_PPC_BOOK3S_64
	/*
	 * On hash, the linear mapping is not in the Linux page table so
	 * apply_to_existing_page_range() will have no effect. If in the future
	 * the set_memory_* functions are used on the linear map this will need
	 * to be updated.
	 */
	if (!radix_enabled()) {
		int region = get_region_id(addr);

		if (WARN_ON_ONCE(region != VMALLOC_REGION_ID && region != IO_REGION_ID))
			return -EINVAL;
	}
#endif

	return apply_to_existing_page_range(&init_mm, start, size,
					    change_page_attr, (void *)action);
}

#if defined(CONFIG_DEBUG_PAGEALLOC) || defined(CONFIG_KFENCE)
#ifdef CONFIG_ARCH_SUPPORTS_DEBUG_PAGEALLOC
void __kernel_map_pages(struct page *page, int numpages, int enable)
{
	int err;
	unsigned long addr = (unsigned long)page_address(page);

	if (PageHighMem(page))
		return;

	if (IS_ENABLED(CONFIG_PPC_BOOK3S_64) && !radix_enabled())
		err = hash__kernel_map_pages(page, numpages, enable);
	else if (enable)
		err = set_memory_p(addr, numpages);
	else
		err = set_memory_np(addr, numpages);

	if (err)
		panic("%s: changing memory protections failed\n", __func__);
}
#endif
#endif
