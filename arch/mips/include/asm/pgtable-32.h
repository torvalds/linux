/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 95, 96, 97, 98, 99, 2000, 2003 Ralf Baechle
 * Copyright (C) 1999, 2000, 2001 Silicon Graphics, Inc.
 */
#ifndef _ASM_PGTABLE_32_H
#define _ASM_PGTABLE_32_H

#include <asm/addrspace.h>
#include <asm/page.h>

#include <linux/linkage.h>
#include <asm/cachectl.h>
#include <asm/fixmap.h>

#include <asm-generic/pgtable-nopmd.h>

#ifdef CONFIG_HIGHMEM
#include <asm/highmem.h>
#endif

/*
 * Regarding 32-bit MIPS huge page support (and the tradeoff it entails):
 *
 *  We use the same huge page sizes as 64-bit MIPS. Assuming a 4KB page size,
 * our 2-level table layout would normally have a PGD entry cover a contiguous
 * 4MB virtual address region (pointing to a 4KB PTE page of 1,024 32-bit pte_t
 * pointers, each pointing to a 4KB physical page). The problem is that 4MB,
 * spanning both halves of a TLB EntryLo0,1 pair, requires 2MB hardware page
 * support, not one of the standard supported sizes (1MB,4MB,16MB,...).
 *  To correct for this, when huge pages are enabled, we halve the number of
 * pointers a PTE page holds, making its last half go to waste. Correspondingly,
 * we double the number of PGD pages. Overall, page table memory overhead
 * increases to match 64-bit MIPS, but PTE lookups remain CPU cache-friendly.
 *
 * NOTE: We don't yet support huge pages if extended-addressing is enabled
 *       (i.e. EVA, XPA, 36-bit Alchemy/Netlogic).
 */

extern int temp_tlb_entry;

/*
 * - add_temporary_entry() add a temporary TLB entry. We use TLB entries
 *	starting at the top and working down. This is for populating the
 *	TLB before trap_init() puts the TLB miss handler in place. It
 *	should be used only for entries matching the actual page tables,
 *	to prevent inconsistencies.
 */
extern int add_temporary_entry(unsigned long entrylo0, unsigned long entrylo1,
			       unsigned long entryhi, unsigned long pagemask);

/*
 * Basically we have the same two-level (which is the logical three level
 * Linux page table layout folded) page tables as the i386.  Some day
 * when we have proper page coloring support we can have a 1% quicker
 * tlb refill handling mechanism, but for now it is a bit slower but
 * works even with the cache aliasing problem the R4k and above have.
 */

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#if defined(CONFIG_MIPS_HUGE_TLB_SUPPORT) && !defined(CONFIG_PHYS_ADDR_T_64BIT)
# define PGDIR_SHIFT	(2 * PAGE_SHIFT - PTE_T_LOG2 - 1)
#else
# define PGDIR_SHIFT	(2 * PAGE_SHIFT - PTE_T_LOG2)
#endif

#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * Entries per page directory level: we use two-level, so
 * we don't really have any PUD/PMD directory physically.
 */
#if defined(CONFIG_MIPS_HUGE_TLB_SUPPORT) && !defined(CONFIG_PHYS_ADDR_T_64BIT)
# define __PGD_TABLE_ORDER (32 - 3 * PAGE_SHIFT + PGD_T_LOG2 + PTE_T_LOG2 + 1)
#else
# define __PGD_TABLE_ORDER (32 - 3 * PAGE_SHIFT + PGD_T_LOG2 + PTE_T_LOG2)
#endif

#define PGD_TABLE_ORDER	(__PGD_TABLE_ORDER >= 0 ? __PGD_TABLE_ORDER : 0)
#define PUD_TABLE_ORDER	aieeee_attempt_to_allocate_pud
#define PMD_TABLE_ORDER	aieeee_attempt_to_allocate_pmd

#define PTRS_PER_PGD	(USER_PTRS_PER_PGD * 2)
#if defined(CONFIG_MIPS_HUGE_TLB_SUPPORT) && !defined(CONFIG_PHYS_ADDR_T_64BIT)
# define PTRS_PER_PTE	(PAGE_SIZE / sizeof(pte_t) / 2)
#else
# define PTRS_PER_PTE	(PAGE_SIZE / sizeof(pte_t))
#endif

#define USER_PTRS_PER_PGD	(0x80000000UL/PGDIR_SIZE)

#define VMALLOC_START	  MAP_BASE

#define PKMAP_END	((FIXADDR_START) & ~((LAST_PKMAP << PAGE_SHIFT)-1))
#define PKMAP_BASE	(PKMAP_END - PAGE_SIZE * LAST_PKMAP)

#ifdef CONFIG_HIGHMEM
# define VMALLOC_END	(PKMAP_BASE-2*PAGE_SIZE)
#else
# define VMALLOC_END	(FIXADDR_START-2*PAGE_SIZE)
#endif

#ifdef CONFIG_PHYS_ADDR_T_64BIT
#define pte_ERROR(e) \
	printk("%s:%d: bad pte %016Lx.\n", __FILE__, __LINE__, pte_val(e))
#else
#define pte_ERROR(e) \
	printk("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))
#endif
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

extern void load_pgd(unsigned long pg_dir);

extern pte_t invalid_pte_table[PTRS_PER_PTE];

/*
 * Empty pgd/pmd entries point to the invalid_pte_table.
 */
static inline int pmd_none(pmd_t pmd)
{
	return pmd_val(pmd) == (unsigned long) invalid_pte_table;
}

static inline int pmd_bad(pmd_t pmd)
{
#ifdef CONFIG_MIPS_HUGE_TLB_SUPPORT
	/* pmd_huge(pmd) but inline */
	if (unlikely(pmd_val(pmd) & _PAGE_HUGE))
		return 0;
#endif

	if (unlikely(pmd_val(pmd) & ~PAGE_MASK))
		return 1;

	return 0;
}

static inline int pmd_present(pmd_t pmd)
{
	return pmd_val(pmd) != (unsigned long) invalid_pte_table;
}

static inline void pmd_clear(pmd_t *pmdp)
{
	pmd_val(*pmdp) = ((unsigned long) invalid_pte_table);
}

#if defined(CONFIG_XPA)

#define MAX_POSSIBLE_PHYSMEM_BITS 40
#define pte_pfn(x)		(((unsigned long)((x).pte_high >> PFN_PTE_SHIFT)) | (unsigned long)((x).pte_low << _PAGE_PRESENT_SHIFT))
static inline pte_t
pfn_pte(unsigned long pfn, pgprot_t prot)
{
	pte_t pte;

	pte.pte_low = (pfn >> _PAGE_PRESENT_SHIFT) |
				(pgprot_val(prot) & ~_PFNX_MASK);
	pte.pte_high = (pfn << PFN_PTE_SHIFT) |
				(pgprot_val(prot) & ~_PFN_MASK);
	return pte;
}

#elif defined(CONFIG_PHYS_ADDR_T_64BIT) && defined(CONFIG_CPU_MIPS32)

#define MAX_POSSIBLE_PHYSMEM_BITS 36
#define pte_pfn(x)		((unsigned long)((x).pte_high >> 6))

static inline pte_t pfn_pte(unsigned long pfn, pgprot_t prot)
{
	pte_t pte;

	pte.pte_high = (pfn << 6) | (pgprot_val(prot) & 0x3f);
	pte.pte_low = pgprot_val(prot);

	return pte;
}

#else

#define MAX_POSSIBLE_PHYSMEM_BITS 32
#define pte_pfn(x)		((unsigned long)((x).pte >> PFN_PTE_SHIFT))
#define pfn_pte(pfn, prot)	__pte(((unsigned long long)(pfn) << PFN_PTE_SHIFT) | pgprot_val(prot))
#define pfn_pmd(pfn, prot)	__pmd(((unsigned long long)(pfn) << PFN_PTE_SHIFT) | pgprot_val(prot))
#endif /* defined(CONFIG_PHYS_ADDR_T_64BIT) && defined(CONFIG_CPU_MIPS32) */

#define pte_page(x)		pfn_to_page(pte_pfn(x))

/*
 * Encode/decode swap entries and swap PTEs. Swap PTEs are all PTEs that
 * are !pte_none() && !pte_present().
 */
#if defined(CONFIG_CPU_R3K_TLB)

/*
 * Format of swap PTEs:
 *
 *   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
 *   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *   <----------- offset ------------> < type -> V G E 0 0 0 0 0 0 P
 *
 *   E is the exclusive marker that is not stored in swap entries.
 *   _PAGE_PRESENT (P), _PAGE_VALID (V) and_PAGE_GLOBAL (G) have to remain
 *   unused.
 */
#define __swp_type(x)			(((x).val >> 10) & 0x1f)
#define __swp_offset(x)			((x).val >> 15)
#define __swp_entry(type, offset)	((swp_entry_t) { (((type) & 0x1f) << 10) | ((offset) << 15) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val })

/* We borrow bit 7 to store the exclusive marker in swap PTEs. */
#define _PAGE_SWP_EXCLUSIVE	(1 << 7)

#else

#if defined(CONFIG_XPA)

/*
 * Format of swap PTEs:
 *
 *   6 6 6 6 5 5 5 5 5 5 5 5 5 5 4 4 4 4 4 4 4 4 4 4 3 3 3 3 3 3 3 3
 *   3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2
 *   0 0 0 0 0 0 E P <------------------ zeroes ------------------->
 *
 *   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
 *   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *   <----------------- offset ------------------> < type -> V G 0 0
 *
 *   E is the exclusive marker that is not stored in swap entries.
 *   _PAGE_PRESENT (P), _PAGE_VALID (V) and_PAGE_GLOBAL (G) have to remain
 *   unused.
 */
#define __swp_type(x)			(((x).val >> 4) & 0x1f)
#define __swp_offset(x)			 ((x).val >> 9)
#define __swp_entry(type, offset)	((swp_entry_t)  { (((type) & 0x1f) << 4) | ((offset) << 9) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { (pte).pte_high })
#define __swp_entry_to_pte(x)		((pte_t) { 0, (x).val })

/*
 * We borrow bit 57 (bit 25 in the low PTE) to store the exclusive marker in
 * swap PTEs.
 */
#define _PAGE_SWP_EXCLUSIVE	(1 << 25)

#elif defined(CONFIG_PHYS_ADDR_T_64BIT) && defined(CONFIG_CPU_MIPS32)

/*
 * Format of swap PTEs:
 *
 *   6 6 6 6 5 5 5 5 5 5 5 5 5 5 4 4 4 4 4 4 4 4 4 4 3 3 3 3 3 3 3 3
 *   3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2
 *   <------------------ zeroes -------------------> E P 0 0 0 0 0 0
 *
 *   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
 *   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *   <------------------- offset --------------------> < type -> V G
 *
 *   E is the exclusive marker that is not stored in swap entries.
 *   _PAGE_PRESENT (P), _PAGE_VALID (V) and_PAGE_GLOBAL (G) have to remain
 *   unused.
 */
#define __swp_type(x)			(((x).val >> 2) & 0x1f)
#define __swp_offset(x)			 ((x).val >> 7)
#define __swp_entry(type, offset)	((swp_entry_t)  { (((type) & 0x1f) << 2) | ((offset) << 7) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { (pte).pte_high })
#define __swp_entry_to_pte(x)		((pte_t) { 0, (x).val })

/*
 * We borrow bit 39 (bit 7 in the low PTE) to store the exclusive marker in swap
 * PTEs.
 */
#define _PAGE_SWP_EXCLUSIVE	(1 << 7)

#else
/*
 * Format of swap PTEs:
 *
 *   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
 *   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
 *   <------------- offset --------------> < type -> 0 0 0 0 0 0 E P
 *
 *   E is the exclusive marker that is not stored in swap entries.
 *   _PAGE_PRESENT (P), _PAGE_VALID (V) and_PAGE_GLOBAL (G) have to remain
 *   unused. The location of V and G varies.
 */
#define __swp_type(x)			(((x).val >> 8) & 0x1f)
#define __swp_offset(x)			 ((x).val >> 13)
#define __swp_entry(type, offset)	((swp_entry_t)	{ ((type) << 8) | ((offset) << 13) })
#define __pte_to_swp_entry(pte)		((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)		((pte_t) { (x).val })

/* We borrow bit 1 to store the exclusive marker in swap PTEs. */
#define _PAGE_SWP_EXCLUSIVE	(1 << 1)

#endif /* defined(CONFIG_PHYS_ADDR_T_64BIT) && defined(CONFIG_CPU_MIPS32) */

#endif /* defined(CONFIG_CPU_R3K_TLB) */

#endif /* _ASM_PGTABLE_32_H */
