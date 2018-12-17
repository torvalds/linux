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
#include <linux/io.h>
#include <linux/memblock.h>
#include <linux/mm_types.h>
#include <linux/preempt.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <asm/cacheflush.h>
#include <asm/efi.h>
#include <asm/mmu.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>

extern u64 efi_system_table;

#ifdef CONFIG_ARM64_PTDUMP_DEBUGFS
#include <asm/ptdump.h>

static struct ptdump_info efi_ptdump_info = {
	.mm		= &efi_mm,
	.markers	= (struct addr_marker[]){
		{ 0,		"UEFI runtime start" },
		{ TASK_SIZE_64,	"UEFI runtime end" }
	},
	.base_addr	= 0,
};

static int __init ptdump_init(void)
{
	if (!efi_enabled(EFI_RUNTIME_SERVICES))
		return 0;

	return ptdump_debugfs_register(&efi_ptdump_info, "efi_page_tables");
}
device_initcall(ptdump_init);

#endif

static bool __init efi_virtmap_init(void)
{
	efi_memory_desc_t *md;
	bool systab_found;

	efi_mm.pgd = pgd_alloc(&efi_mm);
	mm_init_cpumask(&efi_mm);
	init_new_context(NULL, &efi_mm);

	systab_found = false;
	for_each_efi_memory_desc(md) {
		phys_addr_t phys = md->phys_addr;
		int ret;

		if (!(md->attribute & EFI_MEMORY_RUNTIME))
			continue;
		if (md->virt_addr == 0)
			return false;

		ret = efi_create_mapping(&efi_mm, md);
		if (ret) {
			pr_warn("  EFI remap %pa: failed to create mapping (%d)\n",
				&phys, ret);
			return false;
		}
		/*
		 * If this entry covers the address of the UEFI system table,
		 * calculate and record its virtual address.
		 */
		if (efi_system_table >= phys &&
		    efi_system_table < phys + (md->num_pages * EFI_PAGE_SIZE)) {
			efi.systab = (void *)(unsigned long)(efi_system_table -
							     phys + md->virt_addr);
			systab_found = true;
		}
	}
	if (!systab_found) {
		pr_err("No virtual mapping found for the UEFI System Table\n");
		return false;
	}

	if (efi_memattr_apply_permissions(&efi_mm, efi_set_mapping_permissions))
		return false;

	return true;
}

/*
 * Enable the UEFI Runtime Services if all prerequisites are in place, i.e.,
 * non-early mapping of the UEFI system table and virtual mappings for all
 * EFI_MEMORY_RUNTIME regions.
 */
static int __init arm_enable_runtime_services(void)
{
	u64 mapsize;

	if (!efi_enabled(EFI_BOOT)) {
		pr_info("EFI services will not be available.\n");
		return 0;
	}

	efi_memmap_unmap();

	mapsize = efi.memmap.desc_size * efi.memmap.nr_map;

	if (efi_memmap_init_late(efi.memmap.phys_map, mapsize)) {
		pr_err("Failed to remap EFI memory map\n");
		return 0;
	}

	if (efi_runtime_disabled()) {
		pr_info("EFI runtime services will be disabled.\n");
		return 0;
	}

	if (efi_enabled(EFI_RUNTIME_SERVICES)) {
		pr_info("EFI runtime services access via paravirt.\n");
		return 0;
	}

	pr_info("Remapping and enabling EFI services.\n");

	if (!efi_virtmap_init()) {
		pr_err("UEFI virtual mapping missing or invalid -- runtime services will not be available\n");
		return -ENOMEM;
	}

	/* Set up runtime services function pointers */
	efi_native_runtime_setup();
	set_bit(EFI_RUNTIME_SERVICES, &efi.flags);

	return 0;
}
early_initcall(arm_enable_runtime_services);

void efi_virtmap_load(void)
{
	preempt_disable();
	efi_set_pgd(&efi_mm);
}

void efi_virtmap_unload(void)
{
	efi_set_pgd(current->active_mm);
	preempt_enable();
}


static int __init arm_dmi_init(void)
{
	/*
	 * On arm64/ARM, DMI depends on UEFI, and dmi_scan_machine() needs to
	 * be called early because dmi_id_init(), which is an arch_initcall
	 * itself, depends on dmi_scan_machine() having been called already.
	 */
	dmi_scan_machine();
	if (dmi_available)
		dmi_set_dump_stack_arch_desc();
	return 0;
}
core_initcall(arm_dmi_init);
