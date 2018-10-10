// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/arch/i386/kernel/head32.c -- prepare to run common code
 *
 *  Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 *  Copyright (C) 2007 Eric Biederman <ebiederm@xmission.com>
 */

#include <linux/init.h>
#include <linux/start_kernel.h>
#include <linux/mm.h>
#include <linux/memblock.h>

#include <asm/desc.h>
#include <asm/setup.h>
#include <asm/sections.h>
#include <asm/e820/api.h>
#include <asm/page.h>
#include <asm/apic.h>
#include <asm/io_apic.h>
#include <asm/bios_ebda.h>
#include <asm/tlbflush.h>
#include <asm/bootparam_utils.h>

static void __init i386_default_early_setup(void)
{
	/* Initialize 32bit specific setup functions */
	x86_init.resources.reserve_resources = i386_reserve_resources;
	x86_init.mpparse.setup_ioapic_ids = setup_ioapic_ids_from_mpc;
}

asmlinkage __visible void __init i386_start_kernel(void)
{
	/* Make sure IDT is set up before any exception happens */
	idt_setup_early_handler();

	cr4_init_shadow();

	sanitize_boot_params(&boot_params);
	x86_verify_bootdata_version();

	x86_early_init_platform_quirks();

	/* Call the subarch specific early setup function */
	switch (boot_params.hdr.hardware_subarch) {
	case X86_SUBARCH_INTEL_MID:
		x86_intel_mid_early_setup();
		break;
	case X86_SUBARCH_CE4100:
		x86_ce4100_early_setup();
		break;
	default:
		i386_default_early_setup();
		break;
	}

	start_kernel();
}

/*
 * Initialize page tables.  This creates a PDE and a set of page
 * tables, which are located immediately beyond __brk_base.  The variable
 * _brk_end is set up to point to the first "safe" location.
 * Mappings are created both at virtual address 0 (identity mapping)
 * and PAGE_OFFSET for up to _end.
 *
 * In PAE mode initial_page_table is statically defined to contain
 * enough entries to cover the VMSPLIT option (that is the top 1, 2 or 3
 * entries). The identity mapping is handled by pointing two PGD entries
 * to the first kernel PMD. Note the upper half of each PMD or PTE are
 * always zero at this stage.
 */
void __init mk_early_pgtbl_32(void)
{
#ifdef __pa
#undef __pa
#endif
#define __pa(x)  ((unsigned long)(x) - PAGE_OFFSET)
	pte_t pte, *ptep;
	int i;
	unsigned long *ptr;
	/* Enough space to fit pagetables for the low memory linear map */
	const unsigned long limit = __pa(_end) +
		(PAGE_TABLE_SIZE(LOWMEM_PAGES) << PAGE_SHIFT);
#ifdef CONFIG_X86_PAE
	pmd_t pl2, *pl2p = (pmd_t *)__pa(initial_pg_pmd);
#define SET_PL2(pl2, val)    { (pl2).pmd = (val); }
#else
	pgd_t pl2, *pl2p = (pgd_t *)__pa(initial_page_table);
#define SET_PL2(pl2, val)   { (pl2).pgd = (val); }
#endif

	ptep = (pte_t *)__pa(__brk_base);
	pte.pte = PTE_IDENT_ATTR;

	while ((pte.pte & PTE_PFN_MASK) < limit) {

		SET_PL2(pl2, (unsigned long)ptep | PDE_IDENT_ATTR);
		*pl2p = pl2;
#ifndef CONFIG_X86_PAE
		/* Kernel PDE entry */
		*(pl2p +  ((PAGE_OFFSET >> PGDIR_SHIFT))) = pl2;
#endif
		for (i = 0; i < PTRS_PER_PTE; i++) {
			*ptep = pte;
			pte.pte += PAGE_SIZE;
			ptep++;
		}

		pl2p++;
	}

	ptr = (unsigned long *)__pa(&max_pfn_mapped);
	/* Can't use pte_pfn() since it's a call with CONFIG_PARAVIRT */
	*ptr = (pte.pte & PTE_PFN_MASK) >> PAGE_SHIFT;

	ptr = (unsigned long *)__pa(&_brk_end);
	*ptr = (unsigned long)ptep + PAGE_OFFSET;
}

