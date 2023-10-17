// SPDX-License-Identifier: GPL-2.0
/*
 * Extensible Firmware Interface
 *
 * Copyright (C) 2020 Western Digital Corporation or its affiliates.
 *
 * Based on Extensible Firmware Interface Specification version 2.4
 * Adapted from drivers/firmware/efi/arm-runtime.c
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
#include <linux/pgtable.h>

#include <asm/cacheflush.h>
#include <asm/efi.h>
#include <asm/mmu.h>
#include <asm/pgalloc.h>

static bool __init efi_virtmap_init(void)
{
	efi_memory_desc_t *md;

	efi_mm.pgd = pgd_alloc(&efi_mm);
	mm_init_cpumask(&efi_mm);
	init_new_context(NULL, &efi_mm);

	for_each_efi_memory_desc(md) {
		phys_addr_t phys = md->phys_addr;
		int ret;

		if (!(md->attribute & EFI_MEMORY_RUNTIME))
			continue;
		if (md->virt_addr == U64_MAX)
			return false;

		ret = efi_create_mapping(&efi_mm, md);
		if (ret) {
			pr_warn("  EFI remap %pa: failed to create mapping (%d)\n",
				&phys, ret);
			return false;
		}
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
static int __init riscv_enable_runtime_services(void)
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

	if (efi_soft_reserve_enabled()) {
		efi_memory_desc_t *md;

		for_each_efi_memory_desc(md) {
			int md_size = md->num_pages << EFI_PAGE_SHIFT;
			struct resource *res;

			if (!(md->attribute & EFI_MEMORY_SP))
				continue;

			res = kzalloc(sizeof(*res), GFP_KERNEL);
			if (WARN_ON(!res))
				break;

			res->start	= md->phys_addr;
			res->end	= md->phys_addr + md_size - 1;
			res->name	= "Soft Reserved";
			res->flags	= IORESOURCE_MEM;
			res->desc	= IORES_DESC_SOFT_RESERVED;

			insert_resource(&iomem_resource, res);
		}
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
early_initcall(riscv_enable_runtime_services);

static void efi_virtmap_load(void)
{
	preempt_disable();
	switch_mm(current->active_mm, &efi_mm, NULL);
}

static void efi_virtmap_unload(void)
{
	switch_mm(&efi_mm, current->active_mm, NULL);
	preempt_enable();
}

void arch_efi_call_virt_setup(void)
{
	sync_kernel_mappings(efi_mm.pgd);
	efi_virtmap_load();
}

void arch_efi_call_virt_teardown(void)
{
	efi_virtmap_unload();
}
