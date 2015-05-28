/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 95, 96, 97, 98, 99, 2000, 2003 Ralf Baechle
 * Copyright (C) 1999, 2000, 2001 Silicon Graphics, Inc.
 */
#ifndef _ASM_PGTABLE_64_H
#define _ASM_PGTABLE_64_H

#include <linux/compiler.h>
#include <linux/linkage.h>

#include <asm/addrspace.h>
#include <asm/page.h>
#include <asm/cachectl.h>
#include <asm/fixmap.h>

#ifdef CONFIG_PAGE_SIZE_64KB
#include <asm-generic/pgtable-nopmd.h>
#else
#include <asm-generic/pgtable-nopud.h>
#endif

/*
 * Each address space has 2 4K pages as its page directory, giving 1024
 * (== PTRS_PER_PGD) 8 byte pointers to pmd tables. Each pmd table is a
 * single 4K page, giving 512 (== PTRS_PER_PMD) 8 byte pointers to page
 * tables. Each page table is also a single 4K page, giving 512 (==
 * PTRS_PER_PTE) 8 byte ptes. Each pud entry is initialized to point to
 * invalid_pmd_table, each pmd entry is initialized to point to
 * invalid_pte_table, each pte is initialized to 0. When memory is low,
 * and a pmd table or a page table allocation fails, empty_bad_pmd_table
 * and empty_bad_page_table is returned back to higher layer code, so
 * that the failure is recognized later on. Linux does not seem to
 * handle these failures very well though. The empty_bad_page_table has
 * invalid pte entries in it, to force page faults.
 *
 * Kernel mappings: kernel mappings are held in the swapper_pg_table.
 * The layout is identical to userspace except it's indexed with the
 * fault address - VMALLOC_START.
 */


/* PGDIR_SHIFT determines what a third-level page table entry can map */
#ifdef __PAGETABLE_PMD_FOLDED
#define PGDIR_SHIFT	(PAGE_SHIFT + PAGE_SHIFT + PTE_ORDER - 3)
#else

/* PMD_SHIFT determines the size of the area a second-level page table can map */
#define PMD_SHIFT	(PAGE_SHIFT + (PAGE_SHIFT + PTE_ORDER - 3))
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))


#define PGDIR_SHIFT	(PMD_SHIFT + (PAGE_SHIFT + PMD_ORDER - 3))
#endif
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * For 4kB page size we use a 3 level page tree and an 8kB pud, which
 * permits us mapping 40 bits of virtual address space.
 *
 * We used to implement 41 bits by having an order 1 pmd level but that seemed
 * rather pointless.
 *
 * For 8kB page size we use a 3 level page tree which permits a total of
 * 8TB of address space.  Alternatively a 33-bit / 8GB organization using
 * two levels would be easy to implement.
 *
 * For 16kB page size we use a 2 level page tree which permits a total of
 * 36 bits of virtual address space.  We could add a third level but it seems
 * like at the moment there's no need for this.
 *
 * For 64kB page size we use a 2 level page table tree for a total of 42 bits
 * of virtual address space.
 */
#ifdef CONFIG_PAGE_SIZE_4KB
#define PGD_ORDER		1
#define PUD_ORDER		aieeee_attempt_to_allocate_pud
#define PMD_ORDER		0
#define PTE_ORDER		0
#endif
#ifdef CONFIG_PAGE_SIZE_8KB
#define PGD_ORDER		0
#define PUD_ORDER		aieeee_attempt_to_allocate_pud
#define PMD_ORDER		0
#define PTE_ORDER		0
#endif
#ifdef CONFIG_PAGE_SIZE_16KB
#define PGD_ORDER		0
#define PUD_ORDER		aieeee_attempt_to_allocate_pud
#define PMD_ORDER		0
#define PTE_ORDER		0
#endif
#ifdef CONFIG_PAGE_SIZE_32KB
#define PGD_ORDER		0
#define PUD_ORDER		aieeee_attempt_to_allocate_pud
#define PMD_ORDER		0
#define PTE_ORDER		0
#endif
#ifdef CONFIG_PAGE_SIZE_64KB
#define PGD_ORDER		0
#define PUD_ORDER		aieeee_attempt_to_allocate_pud
#define PMD_ORDER		aieeee_attempt_to_allocate_pmd
#define PTE_ORDER		0
#endif

#define PTRS_PER_PGD	((PAGE_SIZE << PGD_ORDER) / sizeof(pgd_t))
#ifndef __PAGETABLE_PMD_FOLDED
#define PTRS_PER_PMD	((PAGE_SIZE << PMD_ORDER) / sizeof(pmd_t))
#endif
#define PTRS_PER_PTE	((PAGE_SIZE << PTE_ORDER) / sizeof(pte_t))

#if PGDIR_SIZE >= TASK_SIZE64
#define USER_PTRS_PER_PGD	(1)
#else
#define USER_PTRS_PER_PGD	(TASK_SIZE64 / PGDIR_SIZE)
#endif
#define FIRST_USER_ADDRESS	0UL

/*
 * TLB refill handlers also map the vmalloc area into xuseg.  Avoid
 * the first couple of pages so NULL pointer dereferences will still
 * reliably trap.
 */
#define VMALLOC_START		(MAP_BASE + (2 * PAGE_SIZE))
#define VMALLOC_END	\
	(MAP_BASE + \
	 min(PTRS_PER_PGD * PTRS_PER_PMD * PTRS_PER_PTE * PAGE_SIZE, \
	     (1UL << cpu_vmbits)) - (1UL << 32))

#if defined(CONFIG_MODULES) && defined(KBUILD_64BIT_SYM32) && \
	VMALLOC_START != CKSSEG
/* Load modules into 32bit-compatible segment. */
#define MODULE_START	CKSSEG
#define MODULE_END	(FIXADDR_START-2*PAGE_SIZE)
#endif

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %016lx.\n", __FILE__, __LINE__, pte_val(e))
#ifndef __PAGETABLE_PMD_FOLDED
#define pmd_ERROR(e) \
	printk("%s:%d: bad pmd %016lx.\n", __FILE__, __LINE__, pmd_val(e))
#endif
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %016lx.\n", __FILE__, __LINE__, pgd_val(e))

extern pte_t invalid_pte_table[PTRS_PER_PTE];
extern pte_t empty_bad_page_table[PTRS_PER_PTE];


#ifndef __PAGETABLE_PMD_FOLDED
/*
 * For 3-level pagetables we defines these ourselves, for 2-level the
 * definitions are supplied by <asm-generic/pgtable-nopmd.h>.
 */
typedef struct { unsigned long pmd; } pmd_t;
#define pmd_val(x)	((x).pmd)
#define __pmd(x)	((pmd_t) { (x) } )


extern pmd_t invalid_pmd_table[PTRS_PER_PMD];
#endif

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
#ifndef __PAGETABLE_PMD_FOLDED

/*
 * Empty pud entries point to the invalid_pmd_table.
 */
static inline int pud_none(pud_t pud)
{
	return pud_val(pud) == (unsigned long) invalid_pmd_table;
}

static inline int pud_bad(pud_t pud)
{
	return pud_val(pud) & ~PAGE_MASK;
}

static inline int pud_present(pud_t pud)
{
	return pud_val(pud) != (unsigned long) invalid_pmd_table;
}

static inline void pud_clear(pud_t *pudp)
{
	pud_val(*pudp) = ((unsigned long) invalid_pmd_table);
}
#endif

#define pte_page(x)		pfn_to_page(pte_pfn(x))

#ifdef CONFIG_CPU_VR41XX
#define pte_pfn(x)		((unsigned long)((x).pte >> (PAGE_SHIFT + 2)))
#define pfn_pte(pfn, prot)	__pte(((pfn) << (PAGE_SHIFT + 2)) | pgprot_val(prot))
#else
#define pte_pfn(x)		((unsigned long)((x).pte >> _PFN_SHIFT))
#define pfn_pte(pfn, prot)	__pte(((pfn) << _PFN_SHIFT) | pgprot_val(prot))
#define pfn_pmd(pfn, prot)	__pmd(((pfn) << _PFN_SHIFT) | pgprot_val(prot))
#endif

#define __pgd_offset(address)	pgd_index(address)
#define __pud_offset(address)	(((address) >> PUD_SHIFT) & (PTRS_PER_PUD-1))
#define __pmd_offset(address)	pmd_index(address)

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)

#define pgd_index(address)	(((address) >> PGDIR_SHIFT) & (PTRS_PER_PGD-1))
#define pmd_index(address)	(((address) >> PMD_SHIFT) & (PTRS_PER_PMD-1))

/* to find an entry in a page-table-directory */
#define pgd_offset(mm, addr)	((mm)->pgd + pgd_index(addr))

#ifndef __PAGETABLE_PMD_FOLDED
static inline unsigned long pud_page_vaddr(pud_t pud)
{
	return pud_val(pud);
}
#define pud_phys(pud)		virt_to_phys((void *)pud_val(pud))
#define pud_page(pud)		(pfn_to_page(pud_phys(pud) >> PAGE_SHIFT))

/* Find an entry in the second-level page table.. */
static inline pmd_t *pmd_offset(pud_t * pud, unsigned long address)
{
	return (pmd_t *) pud_page_vaddr(*pud) + pmd_index(address);
}
#endif

/* Find an entry in the third-level page table.. */
#define __pte_offset(address)						\
	(((address) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pte_offset(dir, address)					\
	((pte_t *) pmd_page_vaddr(*(dir)) + __pte_offset(address))
#define pte_offset_kernel(dir, address)					\
	((pte_t *) pmd_page_vaddr(*(dir)) + __pte_offset(address))
#define pte_offset_map(dir, address)					\
	((pte_t *)page_address(pmd_page(*(dir))) + __pte_offset(address))
#define pte_unmap(pte) ((void)(pte))

/*
 * Initialize a new pgd / pmd table with invalid pointers.
 */
extern void pgd_init(unsigned long page);
extern void pmd_init(unsigned long page, unsigned long pagetable);

/*
 * Non-present pages:  high 40 bits are offset, next 8 bits type,
 * low 16 bits zero.
 */
static inline pte_t mk_swap_pte(unsigned long type, unsigned long offset)
{ pte_t pte; pte_val(pte) = (type << 16) | (offset << 24); return pte; }

#define __swp_type(x)		(((x).val >> 16) & 0xff)
#define __swp_offset(x)		((x).val >> 24)
#define __swp_entry(type, offset) ((swp_entry_t) { pte_val(mk_swap_pte((type), (offset))) })
#define __pte_to_swp_entry(pte) ((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val })

#endif /* _ASM_PGTABLE_64_H */
