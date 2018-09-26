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

#define pr_fmt(fmt)	"efi: " fmt

#include <linux/efi.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/mm_types.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/platform_device.h>
#include <linux/screen_info.h>

#include <asm/efi.h>

u64 efi_system_table;

static int __init is_memory(efi_memory_desc_t *md)
{
	if (md->attribute & (EFI_MEMORY_WB|EFI_MEMORY_WT|EFI_MEMORY_WC))
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

	for_each_efi_memory_desc(md) {
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

static __initdata unsigned long screen_info_table = EFI_INVALID_TABLE_ADDR;

static __initdata efi_config_table_type_t arch_tables[] = {
	{LINUX_EFI_ARM_SCREEN_INFO_TABLE_GUID, NULL, &screen_info_table},
	{NULL_GUID, NULL, NULL}
};

static void __init init_screen_info(void)
{
	struct screen_info *si;

	if (screen_info_table != EFI_INVALID_TABLE_ADDR) {
		si = early_memremap_ro(screen_info_table, sizeof(*si));
		if (!si) {
			pr_err("Could not map screen_info config table\n");
			return;
		}
		screen_info = *si;
		early_memunmap(si, sizeof(*si));

		/* dummycon on ARM needs non-zero values for columns/lines */
		screen_info.orig_video_cols = 80;
		screen_info.orig_video_lines = 25;
	}

	if (screen_info.orig_video_isVGA == VIDEO_TYPE_EFI &&
	    memblock_is_map_memory(screen_info.lfb_base))
		memblock_mark_nomap(screen_info.lfb_base, screen_info.lfb_size);
}

static int __init uefi_init(void)
{
	efi_char16_t *c16;
	void *config_tables;
	size_t table_size;
	char vendor[100] = "unknown";
	int i, retval;

	efi.systab = early_memremap_ro(efi_system_table,
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

	efi.runtime_version = efi.systab->hdr.revision;

	/* Show what we know for posterity */
	c16 = early_memremap_ro(efi_to_phys(efi.systab->fw_vendor),
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
	config_tables = early_memremap_ro(efi_to_phys(efi.systab->tables),
					  table_size);
	if (config_tables == NULL) {
		pr_warn("Unable to map EFI config table array.\n");
		retval = -ENOMEM;
		goto out;
	}
	retval = efi_config_parse_tables(config_tables, efi.systab->nr_tables,
					 sizeof(efi_config_table_t),
					 arch_tables);

	if (!retval)
		efi.config_table = (unsigned long)efi.systab->tables;

	early_memunmap(config_tables, table_size);
out:
	early_memunmap(efi.systab,  sizeof(efi_system_table_t));
	return retval;
}

/*
 * Return true for regions that can be used as System RAM.
 */
static __init int is_usable_memory(efi_memory_desc_t *md)
{
	switch (md->type) {
	case EFI_LOADER_CODE:
	case EFI_LOADER_DATA:
	case EFI_ACPI_RECLAIM_MEMORY:
	case EFI_BOOT_SERVICES_CODE:
	case EFI_BOOT_SERVICES_DATA:
	case EFI_CONVENTIONAL_MEMORY:
	case EFI_PERSISTENT_MEMORY:
		/*
		 * According to the spec, these regions are no longer reserved
		 * after calling ExitBootServices(). However, we can only use
		 * them as System RAM if they can be mapped writeback cacheable.
		 */
		return (md->attribute & EFI_MEMORY_WB);
	default:
		break;
	}
	return false;
}

static __init void reserve_regions(void)
{
	efi_memory_desc_t *md;
	u64 paddr, npages, size;

	if (efi_enabled(EFI_DBG))
		pr_info("Processing EFI memory map:\n");

	/*
	 * Discard memblocks discovered so far: if there are any at this
	 * point, they originate from memory nodes in the DT, and UEFI
	 * uses its own memory map instead.
	 */
	memblock_dump_all();
	memblock_remove(0, PHYS_ADDR_MAX);

	for_each_efi_memory_desc(md) {
		paddr = md->phys_addr;
		npages = md->num_pages;

		if (efi_enabled(EFI_DBG)) {
			char buf[64];

			pr_info("  0x%012llx-0x%012llx %s\n",
				paddr, paddr + (npages << EFI_PAGE_SHIFT) - 1,
				efi_md_typeattr_format(buf, sizeof(buf), md));
		}

		memrange_efi_to_native(&paddr, &npages);
		size = npages << PAGE_SHIFT;

		if (is_memory(md)) {
			early_init_dt_add_memory_arch(paddr, size);

			if (!is_usable_memory(md))
				memblock_mark_nomap(paddr, size);

			/* keep ACPI reclaim memory intact for kexec etc. */
			if (md->type == EFI_ACPI_RECLAIM_MEMORY)
				memblock_reserve(paddr, size);
		}
	}
}

void __init efi_init(void)
{
	struct efi_memory_map_data data;
	struct efi_fdt_params params;

	/* Grab UEFI information placed in FDT by stub */
	if (!efi_get_fdt_params(&params))
		return;

	efi_system_table = params.system_table;

	data.desc_version = params.desc_ver;
	data.desc_size = params.desc_size;
	data.size = params.mmap_size;
	data.phys_map = params.mmap;

	if (efi_memmap_init_early(&data) < 0) {
		/*
		* If we are booting via UEFI, the UEFI memory map is the only
		* description of memory we have, so there is little point in
		* proceeding if we cannot access it.
		*/
		panic("Unable to map EFI memory map.\n");
	}

	WARN(efi.memmap.desc_version != 1,
	     "Unexpected EFI_MEMORY_DESCRIPTOR version %ld",
	      efi.memmap.desc_version);

	if (uefi_init() < 0) {
		efi_memmap_unmap();
		return;
	}

	reserve_regions();
	efi_esrt_init();

	memblock_reserve(params.mmap & PAGE_MASK,
			 PAGE_ALIGN(params.mmap_size +
				    (params.mmap & ~PAGE_MASK)));

	init_screen_info();
}

static int __init register_gop_device(void)
{
	void *pd;

	if (screen_info.orig_video_isVGA != VIDEO_TYPE_EFI)
		return 0;

	pd = platform_device_register_data(NULL, "efi-framebuffer", 0,
					   &screen_info, sizeof(screen_info));
	return PTR_ERR_OR_ZERO(pd);
}
subsys_initcall(register_gop_device);
