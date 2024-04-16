/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/include/asm/pgtable-nommu.h
 *
 *  Copyright (C) 1995-2002 Russell King
 *  Copyright (C) 2004  Hyok S. Choi
 */
#ifndef _ASMARM_PGTABLE_NOMMU_H
#define _ASMARM_PGTABLE_NOMMU_H

#ifndef __ASSEMBLY__

#include <linux/slab.h>
#include <asm/processor.h>
#include <asm/page.h>

/*
 * Trivial page table functions.
 */
#define pgd_present(pgd)	(1)
#define pgd_none(pgd)		(0)
#define pgd_bad(pgd)		(0)
#define pgd_clear(pgdp)
#define kern_addr_valid(addr)	(1)
/* FIXME */
/*
 * PMD_SHIFT determines the size of the area a second-level page table can map
 * PGDIR_SHIFT determines what a third-level page table entry can map
 */
#define PGDIR_SHIFT		21

#define PGDIR_SIZE		(1UL << PGDIR_SHIFT)
#define PGDIR_MASK		(~(PGDIR_SIZE-1))
/* FIXME */

#define PAGE_NONE	__pgprot(0)
#define PAGE_SHARED	__pgprot(0)
#define PAGE_COPY	__pgprot(0)
#define PAGE_READONLY	__pgprot(0)
#define PAGE_KERNEL	__pgprot(0)

#define swapper_pg_dir ((pgd_t *) 0)


typedef pte_t *pte_addr_t;

/*
 * Mark the prot value as uncacheable and unbufferable.
 */
#define pgprot_noncached(prot)	(prot)
#define pgprot_writecombine(prot) (prot)
#define pgprot_device(prot)	(prot)


/*
 * These would be in other places but having them here reduces the diffs.
 */
extern unsigned int kobjsize(const void *objp);

/*
 * All 32bit addresses are effectively valid for vmalloc...
 * Sort of meaningless for non-VM targets.
 */
#define	VMALLOC_START	0UL
#define	VMALLOC_END	0xffffffffUL

#define FIRST_USER_ADDRESS      0UL

#else 

/*
 * dummy tlb and user structures.
 */
#define v3_tlb_fns	(0)
#define v4_tlb_fns	(0)
#define v4wb_tlb_fns	(0)
#define v4wbi_tlb_fns	(0)
#define v6wbi_tlb_fns	(0)
#define v7wbi_tlb_fns	(0)

#define v3_user_fns	(0)
#define v4_user_fns	(0)
#define v4_mc_user_fns	(0)
#define v4wb_user_fns	(0)
#define v4wt_user_fns	(0)
#define v6_user_fns	(0)
#define xscale_mc_user_fns (0)

#endif /*__ASSEMBLY__*/

#endif /* _ASMARM_PGTABLE_H */
