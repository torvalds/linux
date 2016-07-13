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

#include <linux/dmi.h>
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

	create_pgd_mapping(mm, md->phys_addr, md->virt_addr,
			   md->num_pages << EFI_PAGE_SHIFT,
			   __pgprot(prot_val | PTE_NG));
	return 0;
}

static int __init arm64_dmi_init(void)
{
	/*
	 * On arm64, DMI depends on UEFI, and dmi_scan_machine() needs to
	 * be called early because dmi_id_init(), which is an arch_initcall
	 * itself, depends on dmi_scan_machine() having been called already.
	 */
	dmi_scan_machine();
	if (dmi_available)
		dmi_set_dump_stack_arch_desc();
	return 0;
}
core_initcall(arm64_dmi_init);

/*
 * UpdateCapsule() depends on the system being shutdown via
 * ResetSystem().
 */
bool efi_poweroff_required(void)
{
	return efi_enabled(EFI_RUNTIME_SERVICES);
}
