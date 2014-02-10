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
#include <linux/slab.h>

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/e820.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/proto.h>
#include <asm/efi.h>
#include <asm/cacheflush.h>
#include <asm/fixmap.h>
#include <asm/realmode.h>

static pgd_t *save_pgd __initdata;
static unsigned long efi_flags __initdata;

/*
 * We allocate runtime services regions bottom-up, starting from -4G, i.e.
 * 0xffff_ffff_0000_0000 and limit EFI VA mapping space to 64G.
 */
static u64 efi_va	= -4 * (1UL << 30);
#define EFI_VA_END	(-68 * (1UL << 30))

/*
 * Scratch space used for switching the pagetable in the EFI stub
 */
struct efi_scratch {
	u64 r15;
	u64 prev_cr3;
	pgd_t *efi_pgt;
	bool use_pgd;
};

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

	if (!efi_enabled(EFI_OLD_MEMMAP))
		return;

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

	if (!efi_enabled(EFI_OLD_MEMMAP))
		return;

	for (pgd = 0; pgd < n_pgds; pgd++)
		set_pgd(pgd_offset_k(pgd * PGDIR_SIZE), save_pgd[pgd]);
	kfree(save_pgd);
	__flush_tlb_all();
	local_irq_restore(efi_flags);
	early_code_mapping_set_exec(0);
}

/*
 * Add low kernel mappings for passing arguments to EFI functions.
 */
void efi_sync_low_kernel_mappings(void)
{
	unsigned num_pgds;
	pgd_t *pgd = (pgd_t *)__va(real_mode_header->trampoline_pgd);

	if (efi_enabled(EFI_OLD_MEMMAP))
		return;

	num_pgds = pgd_index(MODULES_END - 1) - pgd_index(PAGE_OFFSET);

	memcpy(pgd + pgd_index(PAGE_OFFSET),
		init_mm.pgd + pgd_index(PAGE_OFFSET),
		sizeof(pgd_t) * num_pgds);
}

void efi_setup_page_tables(void)
{
	efi_scratch.efi_pgt = (pgd_t *)(unsigned long)real_mode_header->trampoline_pgd;

	if (!efi_enabled(EFI_OLD_MEMMAP))
		efi_scratch.use_pgd = true;
}

static void __init __map_region(efi_memory_desc_t *md, u64 va)
{
	pgd_t *pgd = (pgd_t *)__va(real_mode_header->trampoline_pgd);
	unsigned long pf = 0;

	if (!(md->attribute & EFI_MEMORY_WB))
		pf |= _PAGE_PCD;

	if (kernel_map_pages_in_pgd(pgd, md->phys_addr, va, md->num_pages, pf))
		pr_warn("Error mapping PA 0x%llx -> VA 0x%llx!\n",
			   md->phys_addr, va);
}

void __init efi_map_region(efi_memory_desc_t *md)
{
	unsigned long size = md->num_pages << PAGE_SHIFT;
	u64 pa = md->phys_addr;

	if (efi_enabled(EFI_OLD_MEMMAP))
		return old_map_region(md);

	/*
	 * Make sure the 1:1 mappings are present as a catch-all for b0rked
	 * firmware which doesn't update all internal pointers after switching
	 * to virtual mode and would otherwise crap on us.
	 */
	__map_region(md, md->phys_addr);

	efi_va -= size;

	/* Is PA 2M-aligned? */
	if (!(pa & (PMD_SIZE - 1))) {
		efi_va &= PMD_MASK;
	} else {
		u64 pa_offset = pa & (PMD_SIZE - 1);
		u64 prev_va = efi_va;

		/* get us the same offset within this 2M page */
		efi_va = (efi_va & PMD_MASK) + pa_offset;

		if (efi_va > prev_va)
			efi_va -= PMD_SIZE;
	}

	if (efi_va < EFI_VA_END) {
		pr_warn(FW_WARN "VA address range overflow!\n");
		return;
	}

	/* Do the VA map */
	__map_region(md, efi_va);
	md->virt_addr = efi_va;
}

/*
 * kexec kernel will use efi_map_region_fixed to map efi runtime memory ranges.
 * md->virt_addr is the original virtual address which had been mapped in kexec
 * 1st kernel.
 */
void __init efi_map_region_fixed(efi_memory_desc_t *md)
{
	__map_region(md, md->virt_addr);
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

void __init parse_efi_setup(u64 phys_addr, u32 data_len)
{
	efi_setup = phys_addr + sizeof(struct setup_data);
}
