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

int __init efi_create_mapping(struct mm_struct *mm, efi_memory_desc_t *md)
{
	pteval_t prot_val;

	/*
	 * Only regions of type EFI_RUNTIME_SERVICES_CODE need to be
	 * executable, everything else can be mapped with the XN bits
	 * set.
	 */
	if ((md->attribute & EFI_MEMORY_WB) == 0)
		prot_val = PROT_DEVICE_nGnRE;
	else if (md->type == EFI_RUNTIME_SERVICES_CODE ||
		 !PAGE_ALIGNED(md->phys_addr))
		prot_val = pgprot_val(PAGE_KERNEL_EXEC);
	else
		prot_val = pgprot_val(PAGE_KERNEL);

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
