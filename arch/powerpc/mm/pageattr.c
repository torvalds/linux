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


/*
 * Updates the attributes of a page in three steps:
 *
 * 1. take the page_table_lock
 * 2. install the new entry with the updated attributes
 * 3. flush the TLB
 *
 * This sequence is safe against concurrent updates, and also allows updating the
 * attributes of a page currently being executed or accessed.
 */
static int change_page_attr(pte_t *ptep, unsigned long addr, void *data)
{
	long action = (long)data;
	pte_t pte;

	spin_lock(&init_mm.page_table_lock);

	pte = ptep_get(ptep);

	/* modify the PTE bits as desired, then apply */
	switch (action) {
	case SET_MEMORY_RO:
		pte = pte_wrprotect(pte);
		break;
	case SET_MEMORY_RW:
		pte = pte_mkwrite(pte_mkdirty(pte));
		break;
	case SET_MEMORY_NX:
		pte = pte_exprotect(pte);
		break;
	case SET_MEMORY_X:
		pte = pte_mkexec(pte);
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}

	pte_update(&init_mm, addr, ptep, ~0UL, pte_val(pte), 0);

	/* See ptesync comment in radix__set_pte_at() */
	if (radix_enabled())
		asm volatile("ptesync": : :"memory");

	flush_tlb_kernel_range(addr, addr + PAGE_SIZE);

	spin_unlock(&init_mm.page_table_lock);

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

/*
 * Set the attributes of a page:
 *
 * This function is used by PPC32 at the end of init to set final kernel memory
 * protection. It includes changing the maping of the page it is executing from
 * and data pages it is using.
 */
static int set_page_attr(pte_t *ptep, unsigned long addr, void *data)
{
	pgprot_t prot = __pgprot((unsigned long)data);

	spin_lock(&init_mm.page_table_lock);

	set_pte_at(&init_mm, addr, ptep, pte_modify(*ptep, prot));
	flush_tlb_kernel_range(addr, addr + PAGE_SIZE);

	spin_unlock(&init_mm.page_table_lock);

	return 0;
}

int set_memory_attr(unsigned long addr, int numpages, pgprot_t prot)
{
	unsigned long start = ALIGN_DOWN(addr, PAGE_SIZE);
	unsigned long sz = numpages * PAGE_SIZE;

	if (numpages <= 0)
		return 0;

	return apply_to_existing_page_range(&init_mm, start, sz, set_page_attr,
					    (void *)pgprot_val(prot));
}
