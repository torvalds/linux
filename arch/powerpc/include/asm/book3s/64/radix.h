#ifndef _ASM_POWERPC_PGTABLE_RADIX_H
#define _ASM_POWERPC_PGTABLE_RADIX_H

#ifndef __ASSEMBLY__
#include <asm/cmpxchg.h>
#endif

#ifdef CONFIG_PPC_64K_PAGES
#include <asm/book3s/64/radix-64k.h>
#else
#include <asm/book3s/64/radix-4k.h>
#endif

/*
 * For P9 DD1 only, we need to track whether the pte's huge.
 */
#define R_PAGE_LARGE	_RPAGE_RSV1


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
#define RADIX_PGD_BAD_BITS		0x60000000000000e0UL

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
 * +------------------------------+  Kernel IO map end (0xc010000000000000)
 * |                              |
 * |                              |
 * |      1/2 of virtual map      |
 * |                              |
 * |                              |
 * +------------------------------+  Kernel IO map start
 * |                              |
 * |      1/4 of virtual map      |
 * |                              |
 * +------------------------------+  Kernel vmemap start
 * |                              |
 * |     1/4 of virtual map       |
 * |                              |
 * +------------------------------+  Kernel virt start (0xc008000000000000)
 * |                              |
 * |                              |
 * |                              |
 * +------------------------------+  Kernel linear (0xc.....)
 */

#define RADIX_KERN_VIRT_START ASM_CONST(0xc008000000000000)
#define RADIX_KERN_VIRT_SIZE  ASM_CONST(0x0008000000000000)

/*
 * The vmalloc space starts at the beginning of that region, and
 * occupies a quarter of it on radix config.
 * (we keep a quarter for the virtual memmap)
 */
#define RADIX_VMALLOC_START	RADIX_KERN_VIRT_START
#define RADIX_VMALLOC_SIZE	(RADIX_KERN_VIRT_SIZE >> 2)
#define RADIX_VMALLOC_END	(RADIX_VMALLOC_START + RADIX_VMALLOC_SIZE)
/*
 * Defines the address of the vmemap area, in its own region on
 * hash table CPUs.
 */
#define RADIX_VMEMMAP_BASE		(RADIX_VMALLOC_END)

#ifndef __ASSEMBLY__
#define RADIX_PTE_TABLE_SIZE	(sizeof(pte_t) << RADIX_PTE_INDEX_SIZE)
#define RADIX_PMD_TABLE_SIZE	(sizeof(pmd_t) << RADIX_PMD_INDEX_SIZE)
#define RADIX_PUD_TABLE_SIZE	(sizeof(pud_t) << RADIX_PUD_INDEX_SIZE)
#define RADIX_PGD_TABLE_SIZE	(sizeof(pgd_t) << RADIX_PGD_INDEX_SIZE)

#ifdef CONFIG_STRICT_KERNEL_RWX
extern void radix__mark_rodata_ro(void);
extern void radix__mark_initmem_nx(void);
#endif

static inline unsigned long __radix_pte_update(pte_t *ptep, unsigned long clr,
					       unsigned long set)
{
	pte_t pte;
	unsigned long old_pte, new_pte;

	do {
		pte = READ_ONCE(*ptep);
		old_pte = pte_val(pte);
		new_pte = (old_pte | set) & ~clr;

	} while (!pte_xchg(ptep, __pte(old_pte), __pte(new_pte)));

	return old_pte;
}


static inline unsigned long radix__pte_update(struct mm_struct *mm,
					unsigned long addr,
					pte_t *ptep, unsigned long clr,
					unsigned long set,
					int huge)
{
	unsigned long old_pte;

	if (cpu_has_feature(CPU_FTR_POWER9_DD1)) {

		unsigned long new_pte;

		old_pte = __radix_pte_update(ptep, ~0ul, 0);
		/*
		 * new value of pte
		 */
		new_pte = (old_pte | set) & ~clr;
		radix__flush_tlb_pte_p9_dd1(old_pte, mm, addr);
		if (new_pte)
			__radix_pte_update(ptep, 0, new_pte);
	} else
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
		/*
		 * If we are trying to clear the pte, we can skip
		 * the DD1 pte update sequence and batch the tlb flush. The
		 * tlb flush batching is done by mmu gather code. We
		 * still keep the cmp_xchg update to make sure we get
		 * correct R/C bit which might be updated via Nest MMU.
		 */
		old_pte = __radix_pte_update(ptep, ~0ul, 0);
	} else
		old_pte = radix__pte_update(mm, addr, ptep, ~0ul, 0, 0);

	return __pte(old_pte);
}

/*
 * Set the dirty and/or accessed bits atomically in a linux PTE, this
 * function doesn't need to invalidate tlb.
 */
static inline void radix__ptep_set_access_flags(struct mm_struct *mm,
						pte_t *ptep, pte_t entry,
						unsigned long address)
{

	unsigned long set = pte_val(entry) & (_PAGE_DIRTY | _PAGE_ACCESSED |
					      _PAGE_RW | _PAGE_EXEC);

	if (cpu_has_feature(CPU_FTR_POWER9_DD1)) {

		unsigned long old_pte, new_pte;

		old_pte = __radix_pte_update(ptep, ~0, 0);
		/*
		 * new value of pte
		 */
		new_pte = old_pte | set;
		radix__flush_tlb_pte_p9_dd1(old_pte, mm, address);
		__radix_pte_update(ptep, 0, new_pte);
	} else
		__radix_pte_update(ptep, 0, set);
	asm volatile("ptesync" : : : "memory");
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
	asm volatile("ptesync" : : : "memory");
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


static inline int radix__pgd_bad(pgd_t pgd)
{
	return !!(pgd_val(pgd) & RADIX_PGD_BAD_BITS);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE

static inline int radix__pmd_trans_huge(pmd_t pmd)
{
	return (pmd_val(pmd) & (_PAGE_PTE | _PAGE_DEVMAP)) == _PAGE_PTE;
}

static inline pmd_t radix__pmd_mkhuge(pmd_t pmd)
{
	if (cpu_has_feature(CPU_FTR_POWER9_DD1))
		return __pmd(pmd_val(pmd) | _PAGE_PTE | R_PAGE_LARGE);
	return __pmd(pmd_val(pmd) | _PAGE_PTE);
}
static inline void radix__pmdp_huge_split_prepare(struct vm_area_struct *vma,
					    unsigned long address, pmd_t *pmdp)
{
	/* Nothing to do for radix. */
	return;
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
extern int radix__has_transparent_hugepage(void);
#endif

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
	 *  DD1    52-28 = 24, 0b11000
	 *  Others 52-31 = 21, 0b10101
	 * RTS encoding details
	 * bits 0 - 3 of rts -> bits 6 - 8 unsigned long
	 * bits 4 - 5 of rts -> bits 62 - 63 of unsigned long
	 */
	if (cpu_has_feature(CPU_FTR_POWER9_DD1))
		rts_field = (0x3UL << 61);
	else {
		rts_field = (0x5UL << 5); /* 6 - 8 bits */
		rts_field |= (0x2UL << 61);
	}
	return rts_field;
}

#ifdef CONFIG_MEMORY_HOTPLUG
int radix__create_section_mapping(unsigned long start, unsigned long end);
int radix__remove_section_mapping(unsigned long start, unsigned long end);
#endif /* CONFIG_MEMORY_HOTPLUG */
#endif /* __ASSEMBLY__ */
#endif
