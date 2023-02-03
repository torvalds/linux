// SPDX-License-Identifier: GPL-2.0
/*
 * Extensible Firmware Interface
 *
 * Based on Extensible Firmware Interface Specification version 2.4
 *
 * Copyright (C) 2013 - 2015 Linaro Ltd.
 */

#define pr_fmt(fmt)	"efi: " fmt

#include <linux/efi.h>
#include <linux/fwnode.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/mm_types.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/platform_device.h>
#include <linux/screen_info.h>

#include <asm/efi.h>

unsigned long __initdata screen_info_table = EFI_INVALID_TABLE_ADDR;

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
static phys_addr_t __init efi_to_phys(unsigned long addr)
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

extern __weak const efi_config_table_type_t efi_arch_tables[];

static void __init init_screen_info(void)
{
	struct screen_info *si;

	if (screen_info_table != EFI_INVALID_TABLE_ADDR) {
		si = early_memremap(screen_info_table, sizeof(*si));
		if (!si) {
			pr_err("Could not map screen_info config table\n");
			return;
		}
		screen_info = *si;
		memset(si, 0, sizeof(*si));
		early_memunmap(si, sizeof(*si));

		if (memblock_is_map_memory(screen_info.lfb_base))
			memblock_mark_nomap(screen_info.lfb_base,
					    screen_info.lfb_size);
	}
}

static int __init uefi_init(u64 efi_system_table)
{
	efi_config_table_t *config_tables;
	efi_system_table_t *systab;
	size_t table_size;
	int retval;

	systab = early_memremap_ro(efi_system_table, sizeof(efi_system_table_t));
	if (systab == NULL) {
		pr_warn("Unable to map EFI system table.\n");
		return -ENOMEM;
	}

	set_bit(EFI_BOOT, &efi.flags);
	if (IS_ENABLED(CONFIG_64BIT))
		set_bit(EFI_64BIT, &efi.flags);

	retval = efi_systab_check_header(&systab->hdr);
	if (retval)
		goto out;

	efi.runtime = systab->runtime;
	efi.runtime_version = systab->hdr.revision;

	efi_systab_report_header(&systab->hdr, efi_to_phys(systab->fw_vendor));

	table_size = sizeof(efi_config_table_t) * systab->nr_tables;
	config_tables = early_memremap_ro(efi_to_phys(systab->tables),
					  table_size);
	if (config_tables == NULL) {
		pr_warn("Unable to map EFI config table array.\n");
		retval = -ENOMEM;
		goto out;
	}
	retval = efi_config_parse_tables(config_tables, systab->nr_tables,
					 efi_arch_tables);

	early_memunmap(config_tables, table_size);
out:
	early_memunmap(systab, sizeof(efi_system_table_t));
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
		 * Special purpose memory is 'soft reserved', which means it
		 * is set aside initially, but can be hotplugged back in or
		 * be assigned to the dax driver after boot.
		 */
		if (efi_soft_reserve_enabled() &&
		    (md->attribute & EFI_MEMORY_SP))
			return false;

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
	u64 efi_system_table;

	/* Grab UEFI information placed in FDT by stub */
	efi_system_table = efi_get_fdt_params(&data);
	if (!efi_system_table)
		return;

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

	if (uefi_init(efi_system_table) < 0) {
		efi_memmap_unmap();
		return;
	}

	reserve_regions();
	/*
	 * For memblock manipulation, the cap should come after the memblock_add().
	 * And now, memblock is fully populated, it is time to do capping.
	 */
	early_init_dt_check_for_usable_mem_range();
	efi_find_mirror();
	efi_esrt_init();
	efi_mokvar_table_init();

	memblock_reserve(data.phys_map & PAGE_MASK,
			 PAGE_ALIGN(data.size + (data.phys_map & ~PAGE_MASK)));

	init_screen_info();
}
