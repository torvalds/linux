/*
 * Copyright (C) 2015 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/efi.h>
#include <asm/efi.h>
#include <asm/mach/map.h>
#include <asm/mmu_context.h>

int __init efi_create_mapping(struct mm_struct *mm, efi_memory_desc_t *md)
{
	struct map_desc desc = {
		.virtual	= md->virt_addr,
		.pfn		= __phys_to_pfn(md->phys_addr),
		.length		= md->num_pages * EFI_PAGE_SIZE,
	};

	/*
	 * Order is important here: memory regions may have all of the
	 * bits below set (and usually do), so we check them in order of
	 * preference.
	 */
	if (md->attribute & EFI_MEMORY_WB)
		desc.type = MT_MEMORY_RWX;
	else if (md->attribute & EFI_MEMORY_WT)
		desc.type = MT_MEMORY_RWX_NONCACHED;
	else if (md->attribute & EFI_MEMORY_WC)
		desc.type = MT_DEVICE_WC;
	else
		desc.type = MT_DEVICE;

	create_mapping_late(mm, &desc, true);
	return 0;
}
