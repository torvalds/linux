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

static struct mm_struct efi_mm = {
	.mm_rb			= RB_ROOT,
	.mm_users		= ATOMIC_INIT(2),
	.mm_count		= ATOMIC_INIT(1),
	.mmap_sem		= __RWSEM_INITIALIZER(efi_mm.mmap_sem),
	.page_table_lock	= __SPIN_LOCK_UNLOCKED(efi_mm.page_table_lock),
	.mmlist			= LIST_HEAD_INIT(efi_mm.mmlist),
};

static bool __init efi_virtmap_init(void)
{
	efi_memory_desc_t *md;

	efi_mm.pgd = pgd_alloc(&efi_mm);
	init_new_context(NULL, &efi_mm);

	for_each_efi_memory_desc(&memmap, md) {
		phys_addr_t phys = md->phys_addr;
		int ret;

		if (!(md->attribute & EFI_MEMORY_RUNTIME))
			continue;
		if (md->virt_addr == 0)
			return false;

		ret = efi_create_mapping(&efi_mm, md);
		if  (!ret) {
			pr_info("  EFI remap %pa => %p\n",
				&phys, (void *)(unsigned long)md->virt_addr);
		} else {
			pr_warn("  EFI remap %pa: failed to create mapping (%d)\n",
				&phys, ret);
			return false;
		}
	}
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

	if (efi_runtime_disabled()) {
		pr_info("EFI runtime services will be disabled.\n");
		return 0;
	}

	pr_info("Remapping and enabling EFI services.\n");

	mapsize = memmap.map_end - memmap.map;
	memmap.map = (__force void *)ioremap_cache(memmap.phys_map,
						   mapsize);
	if (!memmap.map) {
		pr_err("Failed to remap EFI memory map\n");
		return -ENOMEM;
	}
	memmap.map_end = memmap.map + mapsize;
	efi.memmap = &memmap;

	efi.systab = (__force void *)ioremap_cache(efi_system_table,
						   sizeof(efi_system_table_t));
	if (!efi.systab) {
		pr_err("Failed to remap EFI System Table\n");
		return -ENOMEM;
	}

	if (!efi_virtmap_init()) {
		pr_err("No UEFI virtual mapping was installed -- runtime services will not be available\n");
		return -ENOMEM;
	}

	/* Set up runtime services function pointers */
	efi_native_runtime_setup();
	set_bit(EFI_RUNTIME_SERVICES, &efi.flags);

	efi.runtime_version = efi.systab->hdr.revision;

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
