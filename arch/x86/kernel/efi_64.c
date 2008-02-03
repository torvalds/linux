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

static pgd_t save_pgd __initdata;
static unsigned long efi_flags __initdata;

static void __init early_mapping_set_exec(unsigned long start,
					  unsigned long end,
					  int executable)
{
	pte_t *kpte;
	unsigned int level;

	while (start < end) {
		kpte = lookup_address((unsigned long)__va(start), &level);
		BUG_ON(!kpte);
		if (executable)
			set_pte(kpte, pte_mkexec(*kpte));
		else
			set_pte(kpte, __pte((pte_val(*kpte) | _PAGE_NX) & \
					    __supported_pte_mask));
		if (level == 4)
			start = (start + PMD_SIZE) & PMD_MASK;
		else
			start = (start + PAGE_SIZE) & PAGE_MASK;
	}
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
			end = md->phys_addr + (md->num_pages << PAGE_SHIFT);
			early_mapping_set_exec(md->phys_addr, end, executable);
		}
	}
}

void __init efi_call_phys_prelog(void)
{
	unsigned long vaddress;

	local_irq_save(efi_flags);
	early_runtime_code_mapping_set_exec(1);
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
	early_runtime_code_mapping_set_exec(0);
	__flush_tlb_all();
	local_irq_restore(efi_flags);
}

void __init efi_reserve_bootmem(void)
{
	reserve_bootmem_generic((unsigned long)memmap.phys_map,
				memmap.nr_map * memmap.desc_size);
}

void __iomem * __init efi_ioremap(unsigned long offset,
				  unsigned long size)
{
	static unsigned pages_mapped;
	unsigned long last_addr;
	unsigned i, pages;

	last_addr = offset + size - 1;
	offset &= PAGE_MASK;
	pages = (PAGE_ALIGN(last_addr) - offset) >> PAGE_SHIFT;
	if (pages_mapped + pages > MAX_EFI_IO_PAGES)
		return NULL;

	for (i = 0; i < pages; i++) {
		__set_fixmap(FIX_EFI_IO_MAP_FIRST_PAGE - pages_mapped,
			     offset, PAGE_KERNEL_EXEC_NOCACHE);
		offset += PAGE_SIZE;
		pages_mapped++;
	}

	return (void __iomem *)__fix_to_virt(FIX_EFI_IO_MAP_FIRST_PAGE - \
					     (pages_mapped - pages));
}
