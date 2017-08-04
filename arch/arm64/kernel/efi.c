/*
 * Extensible Firmware Interface
 *
 * Based on Extensible Firmware Interface Specification version 2.4
 *
 * Copyright (C) 2013, 2014 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/efi.h>
#include <linux/init.h>

#include <asm/efi.h>

/*
 * Only regions of type EFI_RUNTIME_SERVICES_CODE need to be
 * executable, everything else can be mapped with the XN bits
 * set. Also take the new (optional) RO/XP bits into account.
 */
static __init pteval_t create_mapping_protection(efi_memory_desc_t *md)
{
	u64 attr = md->attribute;
	u32 type = md->type;

	if (type == EFI_MEMORY_MAPPED_IO)
		return PROT_DEVICE_nGnRE;

	if (WARN_ONCE(!PAGE_ALIGNED(md->phys_addr),
		      "UEFI Runtime regions are not aligned to 64 KB -- buggy firmware?"))
		/*
		 * If the region is not aligned to the page size of the OS, we
		 * can not use strict permissions, since that would also affect
		 * the mapping attributes of the adjacent regions.
		 */
		return pgprot_val(PAGE_KERNEL_EXEC);

	/* R-- */
	if ((attr & (EFI_MEMORY_XP | EFI_MEMORY_RO)) ==
	    (EFI_MEMORY_XP | EFI_MEMORY_RO))
		return pgprot_val(PAGE_KERNEL_RO);

	/* R-X */
	if (attr & EFI_MEMORY_RO)
		return pgprot_val(PAGE_KERNEL_ROX);

	/* RW- */
	if (attr & EFI_MEMORY_XP || type != EFI_RUNTIME_SERVICES_CODE)
		return pgprot_val(PAGE_KERNEL);

	/* RWX */
	return pgprot_val(PAGE_KERNEL_EXEC);
}

/* we will fill this structure from the stub, so don't put it in .bss */
struct screen_info screen_info __section(.data);

int __init efi_create_mapping(struct mm_struct *mm, efi_memory_desc_t *md)
{
	pteval_t prot_val = create_mapping_protection(md);
	bool page_mappings_only = (md->type == EFI_RUNTIME_SERVICES_CODE ||
				   md->type == EFI_RUNTIME_SERVICES_DATA);

	if (!PAGE_ALIGNED(md->phys_addr) ||
	    !PAGE_ALIGNED(md->num_pages << EFI_PAGE_SHIFT)) {
		/*
		 * If the end address of this region is not aligned to page
		 * size, the mapping is rounded up, and may end up sharing a
		 * page frame with the next UEFI memory region. If we create
		 * a block entry now, we may need to split it again when mapping
		 * the next region, and support for that is going to be removed
		 * from the MMU routines. So avoid block mappings altogether in
		 * that case.
		 */
		page_mappings_only = true;
	}

	create_pgd_mapping(mm, md->phys_addr, md->virt_addr,
			   md->num_pages << EFI_PAGE_SHIFT,
			   __pgprot(prot_val | PTE_NG), page_mappings_only);
	return 0;
}

static int __init set_permissions(pte_t *ptep, pgtable_t token,
				  unsigned long addr, void *data)
{
	efi_memory_desc_t *md = data;
	pte_t pte = *ptep;

	if (md->attribute & EFI_MEMORY_RO)
		pte = set_pte_bit(pte, __pgprot(PTE_RDONLY));
	if (md->attribute & EFI_MEMORY_XP)
		pte = set_pte_bit(pte, __pgprot(PTE_PXN));
	set_pte(ptep, pte);
	return 0;
}

int __init efi_set_mapping_permissions(struct mm_struct *mm,
				       efi_memory_desc_t *md)
{
	BUG_ON(md->type != EFI_RUNTIME_SERVICES_CODE &&
	       md->type != EFI_RUNTIME_SERVICES_DATA);

	/*
	 * Calling apply_to_page_range() is only safe on regions that are
	 * guaranteed to be mapped down to pages. Since we are only called
	 * for regions that have been mapped using efi_create_mapping() above
	 * (and this is checked by the generic Memory Attributes table parsing
	 * routines), there is no need to check that again here.
	 */
	return apply_to_page_range(mm, md->virt_addr,
				   md->num_pages << EFI_PAGE_SHIFT,
				   set_permissions, md);
}

/*
 * UpdateCapsule() depends on the system being shutdown via
 * ResetSystem().
 */
bool efi_poweroff_required(void)
{
	return efi_enabled(EFI_RUNTIME_SERVICES);
}
