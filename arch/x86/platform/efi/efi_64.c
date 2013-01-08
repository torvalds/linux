/*
 * x86_64 specific EFI support functions
 * Based on Extensible Firmware Interface Specification version 1.0
 *
 * Copyright (C) 2005-2008 Intel Co.
 *	Fenghua Yu <fenghua.yu@intel.com>
 *	Bibo Mao <bibo.mao@intel.com>
 *	Chandramouli Narayanan <mouli@linux.intel.com>
 *	Huang Ying <ying.huang@intel.com>
 *
 * Code to convert EFI to E820 map has been implemented in elilo bootloader
 * based on a EFI patch by Edgar Hucek. Based on the E820 map, the page table
 * is setup appropriately for EFI runtime code.
 * - mouli 06/14/2007.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/bootmem.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/efi.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/reboot.h>

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/e820.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/proto.h>
#include <asm/efi.h>
#include <asm/cacheflush.h>
#include <asm/fixmap.h>

static pgd_t *save_pgd __initdata;
static unsigned long efi_flags __initdata;

static void __init early_code_mapping_set_exec(int executable)
{
	efi_memory_desc_t *md;
	void *p;

	if (!(__supported_pte_mask & _PAGE_NX))
		return;

	/* Make EFI service code area executable */
	for (p = memmap.map; p < memmap.map_end; p += memmap.desc_size) {
		md = p;
		if (md->type == EFI_RUNTIME_SERVICES_CODE ||
		    md->type == EFI_BOOT_SERVICES_CODE)
			efi_set_executable(md, executable);
	}
}

void __init efi_call_phys_prelog(void)
{
	unsigned long vaddress;
	int pgd;
	int n_pgds;

	early_code_mapping_set_exec(1);
	local_irq_save(efi_flags);

	n_pgds = DIV_ROUND_UP((max_pfn << PAGE_SHIFT), PGDIR_SIZE);
	save_pgd = kmalloc(n_pgds * sizeof(pgd_t), GFP_KERNEL);

	for (pgd = 0; pgd < n_pgds; pgd++) {
		save_pgd[pgd] = *pgd_offset_k(pgd * PGDIR_SIZE);
		vaddress = (unsigned long)__va(pgd * PGDIR_SIZE);
		set_pgd(pgd_offset_k(pgd * PGDIR_SIZE), *pgd_offset_k(vaddress));
	}
	__flush_tlb_all();
}

void __init efi_call_phys_epilog(void)
{
	/*
	 * After the lock is released, the original page table is restored.
	 */
	int pgd;
	int n_pgds = DIV_ROUND_UP((max_pfn << PAGE_SHIFT) , PGDIR_SIZE);
	for (pgd = 0; pgd < n_pgds; pgd++)
		set_pgd(pgd_offset_k(pgd * PGDIR_SIZE), save_pgd[pgd]);
	kfree(save_pgd);
	__flush_tlb_all();
	local_irq_restore(efi_flags);
	early_code_mapping_set_exec(0);
}

void __iomem *__init efi_ioremap(unsigned long phys_addr, unsigned long size,
				 u32 type, u64 attribute)
{
	unsigned long last_map_pfn;

	if (type == EFI_MEMORY_MAPPED_IO)
		return ioremap(phys_addr, size);

	last_map_pfn = init_memory_mapping(phys_addr, phys_addr + size);
	if ((last_map_pfn << PAGE_SHIFT) < phys_addr + size) {
		unsigned long top = last_map_pfn << PAGE_SHIFT;
		efi_ioremap(top, size - (top - phys_addr), type, attribute);
	}

	if (!(attribute & EFI_MEMORY_WB))
		efi_memory_uc((u64)(unsigned long)__va(phys_addr), size);

	return (void __iomem *)__va(phys_addr);
}
