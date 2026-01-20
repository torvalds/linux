// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/efi.h>
#include <linux/initrd.h>
#include <linux/memblock.h>

#include <asm/bootinfo.h>
#include <asm/loongson.h>
#include <asm/sections.h>

void __init memblock_init(void)
{
	u32 mem_type;
	u64 mem_start, mem_size;
	efi_memory_desc_t *md;

	/* Parse memory information */
	for_each_efi_memory_desc(md) {
		mem_type = md->type;
		mem_start = md->phys_addr;
		mem_size = md->num_pages << EFI_PAGE_SHIFT;

		switch (mem_type) {
		case EFI_LOADER_CODE:
		case EFI_LOADER_DATA:
		case EFI_BOOT_SERVICES_CODE:
		case EFI_BOOT_SERVICES_DATA:
		case EFI_PERSISTENT_MEMORY:
		case EFI_CONVENTIONAL_MEMORY:
			memblock_add(mem_start, mem_size);
			break;
		case EFI_PAL_CODE:
		case EFI_UNUSABLE_MEMORY:
		case EFI_ACPI_RECLAIM_MEMORY:
			memblock_add(mem_start, mem_size);
			fallthrough;
		case EFI_RESERVED_TYPE:
		case EFI_RUNTIME_SERVICES_CODE:
		case EFI_RUNTIME_SERVICES_DATA:
		case EFI_MEMORY_MAPPED_IO:
		case EFI_MEMORY_MAPPED_IO_PORT_SPACE:
			memblock_reserve(mem_start, mem_size);
			break;
		}
	}

	max_pfn = PFN_DOWN(memblock_end_of_DRAM());
	max_low_pfn = min(PFN_DOWN(HIGHMEM_START), max_pfn);
	memblock_set_current_limit(PFN_PHYS(max_low_pfn));

	/* Reserve the first 2MB */
	memblock_reserve(PHYS_OFFSET, 0x200000);

	/* Reserve the kernel text/data/bss */
	memblock_reserve(__pa_symbol(&_text),
			 __pa_symbol(&_end) - __pa_symbol(&_text));

	memblock_set_node(0, PHYS_ADDR_MAX, &memblock.memory, 0);
	memblock_set_node(0, PHYS_ADDR_MAX, &memblock.reserved, 0);
}
