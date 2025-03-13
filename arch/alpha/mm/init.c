// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/arch/alpha/mm/init.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

/* 2.3.x zone allocator, 1999 Andrea Arcangeli <andrea@suse.de> */

#include <linux/pagemap.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/init.h>
#include <linux/memblock.h> /* max_low_pfn */
#include <linux/vmalloc.h>
#include <linux/gfp.h>

#include <linux/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/hwrpb.h>
#include <asm/dma.h>
#include <asm/mmu_context.h>
#include <asm/console.h>
#include <asm/tlb.h>
#include <asm/setup.h>
#include <asm/sections.h>

#include "../kernel/proto.h"

static struct pcb_struct original_pcb;

pgd_t *
pgd_alloc(struct mm_struct *mm)
{
	pgd_t *ret, *init;

	ret = __pgd_alloc(mm, 0);
	init = pgd_offset(&init_mm, 0UL);
	if (ret) {
#ifdef CONFIG_ALPHA_LARGE_VMALLOC
		memcpy (ret + USER_PTRS_PER_PGD, init + USER_PTRS_PER_PGD,
			(PTRS_PER_PGD - USER_PTRS_PER_PGD - 1)*sizeof(pgd_t));
#else
		pgd_val(ret[PTRS_PER_PGD-2]) = pgd_val(init[PTRS_PER_PGD-2]);
#endif

		/* The last PGD entry is the VPTB self-map.  */
		pgd_val(ret[PTRS_PER_PGD-1])
		  = pte_val(mk_pte(virt_to_page(ret), PAGE_KERNEL));
	}
	return ret;
}


/*
 * BAD_PAGE is the page that is used for page faults when linux
 * is out-of-memory. Older versions of linux just did a
 * do_exit(), but using this instead means there is less risk
 * for a process dying in kernel mode, possibly leaving an inode
 * unused etc..
 *
 * BAD_PAGETABLE is the accompanying page-table: it is initialized
 * to point to BAD_PAGE entries.
 *
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
pmd_t *
__bad_pagetable(void)
{
	memset(absolute_pointer(EMPTY_PGT), 0, PAGE_SIZE);
	return (pmd_t *) EMPTY_PGT;
}

pte_t
__bad_page(void)
{
	memset(absolute_pointer(EMPTY_PGE), 0, PAGE_SIZE);
	return pte_mkdirty(mk_pte(virt_to_page(EMPTY_PGE), PAGE_SHARED));
}

static inline unsigned long
load_PCB(struct pcb_struct *pcb)
{
	register unsigned long sp __asm__("$30");
	pcb->ksp = sp;
	return __reload_thread(pcb);
}

/* Set up initial PCB, VPTB, and other such nicities.  */

static inline void
switch_to_system_map(void)
{
	unsigned long newptbr;
	unsigned long original_pcb_ptr;

	/* Initialize the kernel's page tables.  Linux puts the vptb in
	   the last slot of the L1 page table.  */
	memset(swapper_pg_dir, 0, PAGE_SIZE);
	newptbr = ((unsigned long) swapper_pg_dir - PAGE_OFFSET) >> PAGE_SHIFT;
	pgd_val(swapper_pg_dir[1023]) =
		(newptbr << 32) | pgprot_val(PAGE_KERNEL);

	/* Set the vptb.  This is often done by the bootloader, but 
	   shouldn't be required.  */
	if (hwrpb->vptb != 0xfffffffe00000000UL) {
		wrvptptr(0xfffffffe00000000UL);
		hwrpb->vptb = 0xfffffffe00000000UL;
		hwrpb_update_checksum(hwrpb);
	}

	/* Also set up the real kernel PCB while we're at it.  */
	init_thread_info.pcb.ptbr = newptbr;
	init_thread_info.pcb.flags = 1;	/* set FEN, clear everything else */
	original_pcb_ptr = load_PCB(&init_thread_info.pcb);
	tbia();

	/* Save off the contents of the original PCB so that we can
	   restore the original console's page tables for a clean reboot.

	   Note that the PCB is supposed to be a physical address, but
	   since KSEG values also happen to work, folks get confused.
	   Check this here.  */

	if (original_pcb_ptr < PAGE_OFFSET) {
		original_pcb_ptr = (unsigned long)
			phys_to_virt(original_pcb_ptr);
	}
	original_pcb = *(struct pcb_struct *) original_pcb_ptr;
}

int callback_init_done;

void * __init
callback_init(void * kernel_end)
{
	struct crb_struct * crb;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	void *two_pages;

	/* Starting at the HWRPB, locate the CRB. */
	crb = (struct crb_struct *)((char *)hwrpb + hwrpb->crb_offset);

	if (alpha_using_srm) {
		/* Tell the console whither it is to be remapped. */
		if (srm_fixup(VMALLOC_START, (unsigned long)hwrpb))
			__halt();		/* "We're boned."  --Bender */

		/* Edit the procedure descriptors for DISPATCH and FIXUP. */
		crb->dispatch_va = (struct procdesc_struct *)
			(VMALLOC_START + (unsigned long)crb->dispatch_va
			 - crb->map[0].va);
		crb->fixup_va = (struct procdesc_struct *)
			(VMALLOC_START + (unsigned long)crb->fixup_va
			 - crb->map[0].va);
	}

	switch_to_system_map();

	/* Allocate one PGD and one PMD.  In the case of SRM, we'll need
	   these to actually remap the console.  There is an assumption
	   here that only one of each is needed, and this allows for 8MB.
	   On systems with larger consoles, additional pages will be
	   allocated as needed during the mapping process.

	   In the case of not SRM, but not CONFIG_ALPHA_LARGE_VMALLOC,
	   we need to allocate the PGD we use for vmalloc before we start
	   forking other tasks.  */

	two_pages = (void *)
	  (((unsigned long)kernel_end + ~PAGE_MASK) & PAGE_MASK);
	kernel_end = two_pages + 2*PAGE_SIZE;
	memset(two_pages, 0, 2*PAGE_SIZE);

	pgd = pgd_offset_k(VMALLOC_START);
	p4d = p4d_offset(pgd, VMALLOC_START);
	pud = pud_offset(p4d, VMALLOC_START);
	pud_set(pud, (pmd_t *)two_pages);
	pmd = pmd_offset(pud, VMALLOC_START);
	pmd_set(pmd, (pte_t *)(two_pages + PAGE_SIZE));

	if (alpha_using_srm) {
		static struct vm_struct console_remap_vm;
		unsigned long nr_pages = 0;
		unsigned long vaddr;
		unsigned long i, j;

		/* calculate needed size */
		for (i = 0; i < crb->map_entries; ++i)
			nr_pages += crb->map[i].count;

		/* register the vm area */
		console_remap_vm.flags = VM_ALLOC;
		console_remap_vm.size = nr_pages << PAGE_SHIFT;
		vm_area_register_early(&console_remap_vm, PAGE_SIZE);

		vaddr = (unsigned long)console_remap_vm.addr;

		/* Set up the third level PTEs and update the virtual
		   addresses of the CRB entries.  */
		for (i = 0; i < crb->map_entries; ++i) {
			unsigned long pfn = crb->map[i].pa >> PAGE_SHIFT;
			crb->map[i].va = vaddr;
			for (j = 0; j < crb->map[i].count; ++j) {
				/* Newer consoles (especially on larger
				   systems) may require more pages of
				   PTEs. Grab additional pages as needed. */
				if (pmd != pmd_offset(pud, vaddr)) {
					memset(kernel_end, 0, PAGE_SIZE);
					pmd = pmd_offset(pud, vaddr);
					pmd_set(pmd, (pte_t *)kernel_end);
					kernel_end += PAGE_SIZE;
				}
				set_pte(pte_offset_kernel(pmd, vaddr),
					pfn_pte(pfn, PAGE_KERNEL));
				pfn++;
				vaddr += PAGE_SIZE;
			}
		}
	}

	callback_init_done = 1;
	return kernel_end;
}

/*
 * paging_init() sets up the memory map.
 */
void __init paging_init(void)
{
	unsigned long max_zone_pfn[MAX_NR_ZONES] = {0, };
	unsigned long dma_pfn;

	dma_pfn = virt_to_phys((char *)MAX_DMA_ADDRESS) >> PAGE_SHIFT;
	max_pfn = max_low_pfn;

	max_zone_pfn[ZONE_DMA] = dma_pfn;
	max_zone_pfn[ZONE_NORMAL] = max_pfn;

	/* Initialize mem_map[].  */
	free_area_init(max_zone_pfn);

	/* Initialize the kernel's ZERO_PGE. */
	memset(absolute_pointer(ZERO_PGE), 0, PAGE_SIZE);
}

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_SRM)
void
srm_paging_stop (void)
{
	/* Move the vptb back to where the SRM console expects it.  */
	swapper_pg_dir[1] = swapper_pg_dir[1023];
	tbia();
	wrvptptr(0x200000000UL);
	hwrpb->vptb = 0x200000000UL;
	hwrpb_update_checksum(hwrpb);

	/* Reload the page tables that the console had in use.  */
	load_PCB(&original_pcb);
	tbia();
}
#endif

static const pgprot_t protection_map[16] = {
	[VM_NONE]					= _PAGE_P(_PAGE_FOE | _PAGE_FOW |
								  _PAGE_FOR),
	[VM_READ]					= _PAGE_P(_PAGE_FOE | _PAGE_FOW),
	[VM_WRITE]					= _PAGE_P(_PAGE_FOE),
	[VM_WRITE | VM_READ]				= _PAGE_P(_PAGE_FOE),
	[VM_EXEC]					= _PAGE_P(_PAGE_FOW | _PAGE_FOR),
	[VM_EXEC | VM_READ]				= _PAGE_P(_PAGE_FOW),
	[VM_EXEC | VM_WRITE]				= _PAGE_P(0),
	[VM_EXEC | VM_WRITE | VM_READ]			= _PAGE_P(0),
	[VM_SHARED]					= _PAGE_S(_PAGE_FOE | _PAGE_FOW |
								  _PAGE_FOR),
	[VM_SHARED | VM_READ]				= _PAGE_S(_PAGE_FOE | _PAGE_FOW),
	[VM_SHARED | VM_WRITE]				= _PAGE_S(_PAGE_FOE),
	[VM_SHARED | VM_WRITE | VM_READ]		= _PAGE_S(_PAGE_FOE),
	[VM_SHARED | VM_EXEC]				= _PAGE_S(_PAGE_FOW | _PAGE_FOR),
	[VM_SHARED | VM_EXEC | VM_READ]			= _PAGE_S(_PAGE_FOW),
	[VM_SHARED | VM_EXEC | VM_WRITE]		= _PAGE_S(0),
	[VM_SHARED | VM_EXEC | VM_WRITE | VM_READ]	= _PAGE_S(0)
};
DECLARE_VM_GET_PAGE_PROT
