/*
 * Extensible Firmware Interface
 *
 * Based on Extensible Firmware Interface Specification version 2.4
 *
 * Copyright (C) 2013 - 2015 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/efi.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/mm_types.h>
#include <linux/of.h>
#include <linux/of_fdt.h>

#include <asm/efi.h>

struct efi_memory_map memmap;

u64 efi_system_table;

static int __init is_normal_ram(efi_memory_desc_t *md)
{
	if (md->attribute & EFI_MEMORY_WB)
		return 1;
	return 0;
}

/*
 * Translate a EFI virtual address into a physical address: this is necessary,
 * as some data members of the EFI system table are virtually remapped after
 * SetVirtualAddressMap() has been called.
 */
static phys_addr_t efi_to_phys(unsigned long addr)
{
	efi_memory_desc_t *md;

	for_each_efi_memory_desc(&memmap, md) {
		if (!(md->attribute & EFI_MEMORY_RUNTIME))
			continue;
		if (md->virt_addr == 0)
			/* no virtual mapping has been installed by the stub */
			break;
		if (md->virt_addr <= addr &&
		    (addr - md->virt_addr) < (md->num_pages << EFI_PAGE_SHIFT))
			return md->phys_addr + addr - md->virt_addr;
	}
	return addr;
}

static int __init uefi_init(void)
{
	efi_char16_t *c16;
	void *config_tables;
	size_t table_size;
	char vendor[100] = "unknown";
	int i, retval;

	efi.systab = early_memremap(efi_system_table,
				    sizeof(efi_system_table_t));
	if (efi.systab == NULL) {
		pr_warn("Unable to map EFI system table.\n");
		return -ENOMEM;
	}

	set_bit(EFI_BOOT, &efi.flags);
	if (IS_ENABLED(CONFIG_64BIT))
		set_bit(EFI_64BIT, &efi.flags);

	/*
	 * Verify the EFI Table
	 */
	if (efi.systab->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE) {
		pr_err("System table signature incorrect\n");
		retval = -EINVAL;
		goto out;
	}
	if ((efi.systab->hdr.revision >> 16) < 2)
		pr_warn("Warning: EFI system table version %d.%02d, expected 2.00 or greater\n",
			efi.systab->hdr.revision >> 16,
			efi.systab->hdr.revision & 0xffff);

	/* Show what we know for posterity */
	c16 = early_memremap(efi_to_phys(efi.systab->fw_vendor),
			     sizeof(vendor) * sizeof(efi_char16_t));
	if (c16) {
		for (i = 0; i < (int) sizeof(vendor) - 1 && *c16; ++i)
			vendor[i] = c16[i];
		vendor[i] = '\0';
		early_memunmap(c16, sizeof(vendor) * sizeof(efi_char16_t));
	}

	pr_info("EFI v%u.%.02u by %s\n",
		efi.systab->hdr.revision >> 16,
		efi.systab->hdr.revision & 0xffff, vendor);

	table_size = sizeof(efi_config_table_64_t) * efi.systab->nr_tables;
	config_tables = early_memremap(efi_to_phys(efi.systab->tables),
				       table_size);
	if (config_tables == NULL) {
		pr_warn("Unable to map EFI config table array.\n");
		retval = -ENOMEM;
		goto out;
	}
	retval = efi_config_parse_tables(config_tables, efi.systab->nr_tables,
					 sizeof(efi_config_table_t), NULL);

	early_memunmap(config_tables, table_size);
out:
	early_memunmap(efi.systab,  sizeof(efi_system_table_t));
	return retval;
}

/*
 * Return true for RAM regions we want to permanently reserve.
 */
static __init int is_reserve_region(efi_memory_desc_t *md)
{
	switch (md->type) {
	case EFI_LOADER_CODE:
	case EFI_LOADER_DATA:
	case EFI_BOOT_SERVICES_CODE:
	case EFI_BOOT_SERVICES_DATA:
	case EFI_CONVENTIONAL_MEMORY:
	case EFI_PERSISTENT_MEMORY:
		return 0;
	default:
		break;
	}
	return is_normal_ram(md);
}

static __init void reserve_regions(void)
{
	efi_memory_desc_t *md;
	u64 paddr, npages, size;

	if (efi_enabled(EFI_DBG))
		pr_info("Processing EFI memory map:\n");

	for_each_efi_memory_desc(&memmap, md) {
		paddr = md->phys_addr;
		npages = md->num_pages;

		if (efi_enabled(EFI_DBG)) {
			char buf[64];

			pr_info("  0x%012llx-0x%012llx %s",
				paddr, paddr + (npages << EFI_PAGE_SHIFT) - 1,
				efi_md_typeattr_format(buf, sizeof(buf), md));
		}

		memrange_efi_to_native(&paddr, &npages);
		size = npages << PAGE_SHIFT;

		if (is_normal_ram(md))
			early_init_dt_add_memory_arch(paddr, size);

		if (is_reserve_region(md)) {
			memblock_mark_nomap(paddr, size);
			if (efi_enabled(EFI_DBG))
				pr_cont("*");
		}

		if (efi_enabled(EFI_DBG))
			pr_cont("\n");
	}

	set_bit(EFI_MEMMAP, &efi.flags);
}

void __init efi_init(void)
{
	struct efi_fdt_params params;

	/* Grab UEFI information placed in FDT by stub */
	if (!efi_get_fdt_params(&params))
		return;

	efi_system_table = params.system_table;

	memmap.phys_map = params.mmap;
	memmap.map = early_memremap(params.mmap, params.mmap_size);
	if (memmap.map == NULL) {
		/*
		* If we are booting via UEFI, the UEFI memory map is the only
		* description of memory we have, so there is little point in
		* proceeding if we cannot access it.
		*/
		panic("Unable to map EFI memory map.\n");
	}
	memmap.map_end = memmap.map + params.mmap_size;
	memmap.desc_size = params.desc_size;
	memmap.desc_version = params.desc_ver;

	if (uefi_init() < 0)
		return;

	reserve_regions();
	early_memunmap(memmap.map, params.mmap_size);
	memblock_mark_nomap(params.mmap & PAGE_MASK,
			    PAGE_ALIGN(params.mmap_size +
				       (params.mmap & ~PAGE_MASK)));
}
