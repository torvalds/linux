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

static pgd_t save_pgd __initdata;
static unsigned long efi_flags __initdata;

static void __init early_mapping_set_exec(unsigned long start,
					  unsigned long end,
					  int executable)
{
	unsigned long num_pages;

	start &= PMD_MASK;
	end = (end + PMD_SIZE - 1) & PMD_MASK;
	num_pages = (end - start) >> PAGE_SHIFT;
	if (executable)
		set_memory_x((unsigned long)__va(start), num_pages);
	else
		set_memory_nx((unsigned long)__va(start), num_pages);
}

static void __init early_runtime_code_mapping_set_exec(int executable)
{
	efi_memory_desc_t *md;
	void *p;

	if (!(__supported_pte_mask & _PAGE_NX))
		return;

	/* Make EFI runtime service code area executable */
	for (p = memmap.map; p < memmap.map_end; p += memmap.desc_size) {
		md = p;
		if (md->type == EFI_RUNTIME_SERVICES_CODE) {
			unsigned long end;
			end = md->phys_addr + (md->num_pages << EFI_PAGE_SHIFT);
			early_mapping_set_exec(md->phys_addr, end, executable);
		}
	}
}

void __init efi_call_phys_prelog(void)
{
	unsigned long vaddress;

	early_runtime_code_mapping_set_exec(1);
	local_irq_save(efi_flags);
	vaddress = (unsigned long)__va(0x0UL);
	save_pgd = *pgd_offset_k(0x0UL);
	set_pgd(pgd_offset_k(0x0UL), *pgd_offset_k(vaddress));
	__flush_tlb_all();
}

void __init efi_call_phys_epilog(void)
{
	/*
	 * After the lock is released, the original page table is restored.
	 */
	set_pgd(pgd_offset_k(0x0UL), save_pgd);
	__flush_tlb_all();
	local_irq_restore(efi_flags);
	early_runtime_code_mapping_set_exec(0);
}

void __iomem *__init efi_ioremap(unsigned long phys_addr, unsigned long size)
{
	unsigned long last_map_pfn;

	last_map_pfn = init_memory_mapping(phys_addr, phys_addr + size);
	if ((last_map_pfn << PAGE_SHIFT) < phys_addr + size)
		return NULL;

	return (void __iomem *)__va(phys_addr);
}
