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

static __initdata unsigned long screen_info_table = EFI_INVALID_TABLE_ADDR;

static const efi_config_table_type_t arch_tables[] __initconst = {
	{LINUX_EFI_ARM_SCREEN_INFO_TABLE_GUID, &screen_info_table},
	{}
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

	retval = efi_systab_check_header(&systab->hdr, 2);
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
					 arch_tables);

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
	efi_esrt_init();

	memblock_reserve(data.phys_map & PAGE_MASK,
			 PAGE_ALIGN(data.size + (data.phys_map & ~PAGE_MASK)));

	init_screen_info();

	/* ARM does not permit early mappings to persist across paging_init() */
	if (IS_ENABLED(CONFIG_ARM))
		efi_memmap_unmap();
}

static bool efifb_overlaps_pci_range(const struct of_pci_range *range)
{
	u64 fb_base = screen_info.lfb_base;

	if (screen_info.capabilities & VIDEO_CAPABILITY_64BIT_BASE)
		fb_base |= (u64)(unsigned long)screen_info.ext_lfb_base << 32;

	return fb_base >= range->cpu_addr &&
	       fb_base < (range->cpu_addr + range->size);
}

static struct device_node *find_pci_overlap_node(void)
{
	struct device_node *np;

	for_each_node_by_type(np, "pci") {
		struct of_pci_range_parser parser;
		struct of_pci_range range;
		int err;

		err = of_pci_range_parser_init(&parser, np);
		if (err) {
			pr_warn("of_pci_range_parser_init() failed: %d\n", err);
			continue;
		}

		for_each_of_pci_range(&parser, &range)
			if (efifb_overlaps_pci_range(&range))
				return np;
	}
	return NULL;
}

/*
 * If the efifb framebuffer is backed by a PCI graphics controller, we have
 * to ensure that this relation is expressed using a device link when
 * running in DT mode, or the probe order may be reversed, resulting in a
 * resource reservation conflict on the memory window that the efifb
 * framebuffer steals from the PCIe host bridge.
 */
static int efifb_add_links(const struct fwnode_handle *fwnode,
			   struct device *dev)
{
	struct device_node *sup_np;
	struct device *sup_dev;

	sup_np = find_pci_overlap_node();

	/*
	 * If there's no PCI graphics controller backing the efifb, we are
	 * done here.
	 */
	if (!sup_np)
		return 0;

	sup_dev = get_dev_from_fwnode(&sup_np->fwnode);
	of_node_put(sup_np);

	/*
	 * Return -ENODEV if the PCI graphics controller device hasn't been
	 * registered yet.  This ensures that efifb isn't allowed to probe
	 * and this function is retried again when new devices are
	 * registered.
	 */
	if (!sup_dev)
		return -ENODEV;

	/*
	 * If this fails, retrying this function at a later point won't
	 * change anything. So, don't return an error after this.
	 */
	if (!device_link_add(dev, sup_dev, fw_devlink_get_flags()))
		dev_warn(dev, "device_link_add() failed\n");

	put_device(sup_dev);

	return 0;
}

static const struct fwnode_operations efifb_fwnode_ops = {
	.add_links = efifb_add_links,
};

static struct fwnode_handle efifb_fwnode = {
	.ops = &efifb_fwnode_ops,
};

static int __init register_gop_device(void)
{
	struct platform_device *pd;
	int err;

	if (screen_info.orig_video_isVGA != VIDEO_TYPE_EFI)
		return 0;

	pd = platform_device_alloc("efi-framebuffer", 0);
	if (!pd)
		return -ENOMEM;

	if (IS_ENABLED(CONFIG_PCI))
		pd->dev.fwnode = &efifb_fwnode;

	err = platform_device_add_data(pd, &screen_info, sizeof(screen_info));
	if (err)
		return err;

	return platform_device_add(pd);
}
subsys_initcall(register_gop_device);
