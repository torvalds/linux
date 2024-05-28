// SPDX-License-Identifier: GPL-2.0
/*
 * Extensible Firmware Interface
 *
 * Based on Extensible Firmware Interface Specification version 1.0
 *
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999-2002 Hewlett-Packard Co.
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Stephane Eranian <eranian@hpl.hp.com>
 *
 * All EFI Runtime Services are not implemented yet as EFI only
 * supports physical mode addressing on SoftSDV. This is to be fixed
 * in a future version.  --drummond 1999-07-20
 *
 * Implemented EFI runtime services and virtual mode calls.  --davidm
 *
 * Goutham Rao: <goutham.rao@intel.com>
 *	Skip non-WB memory and ignore empty memory ranges.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/efi.h>
#include <linux/pgtable.h>

#include <asm/io.h>
#include <asm/desc.h>
#include <asm/page.h>
#include <asm/set_memory.h>
#include <asm/tlbflush.h>
#include <asm/efi.h>

void __init efi_map_region(efi_memory_desc_t *md)
{
	u64 start_pfn, end_pfn, end;
	unsigned long size;
	void *va;

	start_pfn	= PFN_DOWN(md->phys_addr);
	size		= md->num_pages << PAGE_SHIFT;
	end		= md->phys_addr + size;
	end_pfn 	= PFN_UP(end);

	if (pfn_range_is_mapped(start_pfn, end_pfn)) {
		va = __va(md->phys_addr);

		if (!(md->attribute & EFI_MEMORY_WB))
			set_memory_uc((unsigned long)va, md->num_pages);
	} else {
		va = ioremap_cache(md->phys_addr, size);
	}

	md->virt_addr = (unsigned long)va;
	if (!va)
		pr_err("ioremap of 0x%llX failed!\n", md->phys_addr);
}

/*
 * To make EFI call EFI runtime service in physical addressing mode we need
 * prolog/epilog before/after the invocation to claim the EFI runtime service
 * handler exclusively and to duplicate a memory mapping in low memory space,
 * say 0 - 3G.
 */

int __init efi_alloc_page_tables(void)
{
	return 0;
}

void efi_sync_low_kernel_mappings(void) {}

void __init efi_dump_pagetable(void)
{
#ifdef CONFIG_EFI_PGT_DUMP
	ptdump_walk_pgd_level(NULL, &init_mm);
#endif
}

int __init efi_setup_page_tables(unsigned long pa_memmap, unsigned num_pages)
{
	return 0;
}

void __init efi_map_region_fixed(efi_memory_desc_t *md) {}
void __init parse_efi_setup(u64 phys_addr, u32 data_len) {}

efi_status_t efi_call_svam(efi_runtime_services_t * const *,
			   u32, u32, u32, void *, u32);

efi_status_t __init efi_set_virtual_address_map(unsigned long memory_map_size,
						unsigned long descriptor_size,
						u32 descriptor_version,
						efi_memory_desc_t *virtual_map,
						unsigned long systab_phys)
{
	const efi_system_table_t *systab = (efi_system_table_t *)systab_phys;
	struct desc_ptr gdt_descr;
	efi_status_t status;
	unsigned long flags;
	pgd_t *save_pgd;

	/* Current pgd is swapper_pg_dir, we'll restore it later: */
	save_pgd = swapper_pg_dir;
	load_cr3(initial_page_table);
	__flush_tlb_all();

	gdt_descr.address = get_cpu_gdt_paddr(0);
	gdt_descr.size = GDT_SIZE - 1;
	load_gdt(&gdt_descr);

	/* Disable interrupts around EFI calls: */
	local_irq_save(flags);
	status = efi_call_svam(&systab->runtime,
			       memory_map_size, descriptor_size,
			       descriptor_version, virtual_map,
			       __pa(&efi.runtime));
	local_irq_restore(flags);

	load_fixmap_gdt(0);
	load_cr3(save_pgd);
	__flush_tlb_all();

	return status;
}

void __init efi_runtime_update_mappings(void)
{
	if (__supported_pte_mask & _PAGE_NX) {
		efi_memory_desc_t *md;

		/* Make EFI runtime service code area executable */
		for_each_efi_memory_desc(md) {
			if (md->type != EFI_RUNTIME_SERVICES_CODE)
				continue;

			set_memory_x(md->virt_addr, md->num_pages);
		}
	}
}

void arch_efi_call_virt_setup(void)
{
	efi_fpu_begin();
	firmware_restrict_branch_speculation_start();
}

void arch_efi_call_virt_teardown(void)
{
	firmware_restrict_branch_speculation_end();
	efi_fpu_end();
}
