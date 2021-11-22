/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_PGTABLE_RADIX_H
#define _ASM_POWERPC_PGTABLE_RADIX_H

#include <asm/asm-const.h>

#ifndef __ASSEMBLY__
#include <asm/cmpxchg.h>
#endif

#ifdef CONFIG_PPC_64K_PAGES
#include <asm/book3s/64/radix-64k.h>
#else
#include <asm/book3s/64/radix-4k.h>
#endif

#ifndef __ASSEMBLY__
#include <asm/book3s/64/tlbflush-radix.h>
#include <asm/cpu_has_feature.h>
#endif

/* An empty PTE can still have a R or C writeback */
#define RADIX_PTE_NONE_MASK		(_PAGE_DIRTY | _PAGE_ACCESSED)

/* Bits to set in a RPMD/RPUD/RPGD */
#define RADIX_PMD_VAL_BITS		(0x8000000000000000UL | RADIX_PTE_INDEX_SIZE)
#define RADIX_PUD_VAL_BITS		(0x8000000000000000UL | RADIX_PMD_INDEX_SIZE)
#define RADIX_PGD_VAL_BITS		(0x8000000000000000UL | RADIX_PUD_INDEX_SIZE)

/* Don't have anything in the reserved bits and leaf bits */
#define RADIX_PMD_BAD_BITS		0x60000000000000e0UL
#define RADIX_PUD_BAD_BITS		0x60000000000000e0UL
#define RADIX_P4D_BAD_BITS		0x60000000000000e0UL

#define RADIX_PMD_SHIFT		(PAGE_SHIFT + RADIX_PTE_INDEX_SIZE)
#define RADIX_PUD_SHIFT		(RADIX_PMD_SHIFT + RADIX_PMD_INDEX_SIZE)
#define RADIX_PGD_SHIFT		(RADIX_PUD_SHIFT + RADIX_PUD_INDEX_SIZE)
/*
 * Size of EA range mapped by our pagetables.
 */
#define RADIX_PGTABLE_EADDR_SIZE (RADIX_PTE_INDEX_SIZE + RADIX_PMD_INDEX_SIZE +	\
			      RADIX_PUD_INDEX_SIZE + RADIX_PGD_INDEX_SIZE + PAGE_SHIFT)
#define RADIX_PGTABLE_RANGE (ASM_CONST(1) << RADIX_PGTABLE_EADDR_SIZE)

/*
 * We support 52 bit address space, Use top bit for kernel
 * virtual mapping. Also make sure kernel fit in the top
 * quadrant.
 *
 *           +------------------+
 *           +------------------+  Kernel virtual map (0xc008000000000000)
 *           |                  |
 *           |                  |
 *           |                  |
 * 0b11......+------------------+  Kernel linear map (0xc....)
 *           |                  |
 *           |     2 quadrant   |
 *           |                  |
 * 0b10......+------------------+
 *           |                  |
 *           |    1 quadrant    |
 *           |                  |
 * 0b01......+------------------+
 *           |                  |
 *           |    0 quadrant    |
 *           |                  |
 * 0b00......+------------------+
 *
 *
 * 3rd quadrant expanded:
 * +------------------------------+
 * |                              |
 * |                              |
 * |                              |
 * +------------------------------+  Kernel vmemmap end (0xc010000000000000)
 * |                              |
 * |           512TB		  |
 * |                              |
 * +------------------------------+  Kernel IO map end/vmemap start
 * |                              |
 * |           512TB		  |
 * |                              |
 * +------------------------------+  Kernel vmap end/ IO map start
 * |                              |
 * |           512TB		  |
 * |                              |
 * +------------------------------+  Kernel virt start (0xc008000000000000)
 * |                              |
 * |                              |
 * |                              |
 * +------------------------------+  Kernel linear (0xc.....)
 */


/*
 * If we store section details in page->flags we can't increase the MAX_PHYSMEM_BITS
 * if we increase SECTIONS_WIDTH we will not store node details in page->flags and
 * page_to_nid does a page->section->node lookup
 * Hence only increase for VMEMMAP. Further depending on SPARSEMEM_EXTREME reduce
 * memory requirements with large number of sections.
 * 51 bits is the max physical real address on POWER9
 */

#if defined(CONFIG_SPARSEMEM_VMEMMAP) && defined(CONFIG_SPARSEMEM_EXTREME)
#define R_MAX_PHYSMEM_BITS	51
#else
#define R_MAX_PHYSMEM_BITS	46
#endif

#define RADIX_KERN_VIRT_START	ASM_CONST(0xc008000000000000)
/*
 * 49 =  MAX_EA_BITS_PER_CONTEXT (hash specific). To make sure we pick
 * the same value as hash.
 */
#define RADIX_KERN_MAP_SIZE	(1UL << 49)

#define RADIX_VMALLOC_START	RADIX_KERN_VIRT_START
#define RADIX_VMALLOC_SIZE	RADIX_KERN_MAP_SIZE
#define RADIX_VMALLOC_END	(RADIX_VMALLOC_START + RADIX_VMALLOC_SIZE)

#define RADIX_KERN_IO_START	RADIX_VMALLOC_END
#define RADIX_KERN_IO_SIZE	RADIX_KERN_MAP_SIZE
#define RADIX_KERN_IO_END	(RADIX_KERN_IO_START + RADIX_KERN_IO_SIZE)

#define RADIX_VMEMMAP_START	RADIX_KERN_IO_END
#define RADIX_VMEMMAP_SIZE	RADIX_KERN_MAP_SIZE
#define RADIX_VMEMMAP_END	(RADIX_VMEMMAP_START + RADIX_VMEMMAP_SIZE)

#ifndef __ASSEMBLY__
#define RADIX_PTE_TABLE_SIZE	(sizeof(pte_t) << RADIX_PTE_INDEX_SIZE)
#define RADIX_PMD_TABLE_SIZE	(sizeof(pmd_t) << RADIX_PMD_INDEX_SIZE)
#define RADIX_PUD_TABLE_SIZE	(sizeof(pud_t) << RADIX_PUD_INDEX_SIZE)
#define RADIX_PGD_TABLE_SIZE	(sizeof(pgd_t) << RADIX_PGD_INDEX_SIZE)

#ifdef CONFIG_STRICT_KERNEL_RWX
extern void radix__mark_rodata_ro(void);
extern void radix__mark_initmem_nx(void);
#endif

extern void radix__ptep_set_access_flags(struct vm_area_struct *vma, pte_t *ptep,
					 pte_t entry, unsigned long address,
					 int psize);

extern void radix__ptep_modify_prot_commit(struct vm_area_struct *vma,
					   unsigned long addr, pte_t *ptep,
					   pte_t old_pte, pte_t pte);

static inline unsigned long __radix_pte_update(pte_t *ptep, unsigned long clr,
					       unsigned long set)
{
	__be64 old_be, tmp_be;

	__asm__ __volatile__(
	"1:	ldarx	%0,0,%3		# pte_update\n"
	"	andc	%1,%0,%5	\n"
	"	or	%1,%1,%4	\n"
	"	stdcx.	%1,0,%3		\n"
	"	bne-	1b"
	: "=&r" (old_be), "=&r" (tmp_be), "=m" (*ptep)
	: "r" (ptep), "r" (cpu_to_be64(set)), "r" (cpu_to_be64(clr))
	: "cc" );

	return be64_to_cpu(old_be);
}

static inline unsigned long radix__pte_update(struct mm_struct *mm,
					unsigned long addr,
					pte_t *ptep, unsigned long clr,
					unsigned long set,
					int huge)
{
	unsigned long old_pte;

	old_pte = __radix_pte_update(ptep, clr, set);
	if (!huge)
		assert_pte_locked(mm, addr);

	return old_pte;
}

static inline pte_t radix__ptep_get_and_clear_full(struct mm_struct *mm,
						   unsigned long addr,
						   pte_t *ptep, int full)
{
	unsigned long old_pte;

	if (full) {
		old_pte = pte_val(*ptep);
		*ptep = __pte(0);
	} else
		old_pte = radix__pte_update(mm, addr, ptep, ~0ul, 0, 0);

	return __pte(old_pte);
}

static inline int radix__pte_same(pte_t pte_a, pte_t pte_b)
{
	return ((pte_raw(pte_a) ^ pte_raw(pte_b)) == 0);
}

static inline int radix__pte_none(pte_t pte)
{
	return (pte_val(pte) & ~RADIX_PTE_NONE_MASK) == 0;
}

static inline void radix__set_pte_at(struct mm_struct *mm, unsigned long addr,
				 pte_t *ptep, pte_t pte, int percpu)
{
	*ptep = pte;

	/*
	 * The architecture suggests a ptesync after setting the pte, which
	 * orders the store that updates the pte with subsequent page table
	 * walk accesses which may load the pte. Without this it may be
	 * possible for a subsequent access to result in spurious fault.
	 *
	 * This is not necessary for correctness, because a spurious fault
	 * is tolerated by the page fault handler, and this store will
	 * eventually be seen. In testing, there was no noticable increase
	 * in user faults on POWER9. Avoiding ptesync here is a significant
	 * win for things like fork. If a future microarchitecture benefits
	 * from ptesync, it should probably go into update_mmu_cache, rather
	 * than set_pte_at (which is used to set ptes unrelated to faults).
	 *
	 * Spurious faults from the kernel memory are not tolerated, so there
	 * is a ptesync in flush_cache_vmap, and __map_kernel_page() follows
	 * the pte update sequence from ISA Book III 6.10 Translation Table
	 * Update Synchronization Requirements.
	 */
}

static inline int radix__pmd_bad(pmd_t pmd)
{
	return !!(pmd_val(pmd) & RADIX_PMD_BAD_BITS);
}

static inline int radix__pmd_same(pmd_t pmd_a, pmd_t pmd_b)
{
	return ((pmd_raw(pmd_a) ^ pmd_raw(pmd_b)) == 0);
}

static inline int radix__pud_bad(pud_t pud)
{
	return !!(pud_val(pud) & RADIX_PUD_BAD_BITS);
}


static inline int radix__p4d_bad(p4d_t p4d)
{
	return !!(p4d_val(p4d) & RADIX_P4D_BAD_BITS);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE

static inline int radix__pmd_trans_huge(pmd_t pmd)
{
	return (pmd_val(pmd) & (_PAGE_PTE | _PAGE_DEVMAP)) == _PAGE_PTE;
}

static inline pmd_t radix__pmd_mkhuge(pmd_t pmd)
{
	return __pmd(pmd_val(pmd) | _PAGE_PTE);
}

extern unsigned long radix__pmd_hugepage_update(struct mm_struct *mm, unsigned long addr,
					  pmd_t *pmdp, unsigned long clr,
					  unsigned long set);
extern pmd_t radix__pmdp_collapse_flush(struct vm_area_struct *vma,
				  unsigned long address, pmd_t *pmdp);
extern void radix__pgtable_trans_huge_deposit(struct mm_struct *mm, pmd_t *pmdp,
					pgtable_t pgtable);
extern pgtable_t radix__pgtable_trans_huge_withdraw(struct mm_struct *mm, pmd_t *pmdp);
extern pmd_t radix__pmdp_huge_get_and_clear(struct mm_struct *mm,
				      unsigned long addr, pmd_t *pmdp);
static inline int radix__has_transparent_hugepage(void)
{
	/* For radix 2M at PMD level means thp */
	if (mmu_psize_defs[MMU_PAGE_2M].shift == PMD_SHIFT)
		return 1;
	return 0;
}
#endif

static inline pmd_t radix__pmd_mkdevmap(pmd_t pmd)
{
	return __pmd(pmd_val(pmd) | (_PAGE_PTE | _PAGE_DEVMAP));
}

extern int __meminit radix__vmemmap_create_mapping(unsigned long start,
					     unsigned long page_size,
					     unsigned long phys);
extern void radix__vmemmap_remove_mapping(unsigned long start,
				    unsigned long page_size);

extern int radix__map_kernel_page(unsigned long ea, unsigned long pa,
				 pgprot_t flags, unsigned int psz);

static inline unsigned long radix__get_tree_size(void)
{
	unsigned long rts_field;
	/*
	 * We support 52 bits, hence:
	 * bits 52 - 31 = 21, 0b10101
	 * RTS encoding details
	 * bits 0 - 3 of rts -> bits 6 - 8 unsigned long
	 * bits 4 - 5 of rts -> bits 62 - 63 of unsigned long
	 */
	rts_field = (0x5UL << 5); /* 6 - 8 bits */
	rts_field |= (0x2UL << 61);

	return rts_field;
}

#ifdef CONFIG_MEMORY_HOTPLUG
int radix__create_section_mapping(unsigned long start, unsigned long end,
				  int nid, pgprot_t prot);
int radix__remove_section_mapping(unsigned long start, unsigned long end);
#endif /* CONFIG_MEMORY_HOTPLUG */

void radix__kernel_map_pages(struct page *page, int numpages, int enable);

#endif /* __ASSEMBLY__ */
#endif
