#ifndef _ASM_POWERPC_BOOK3S_64_HASH_H
#define _ASM_POWERPC_BOOK3S_64_HASH_H
#ifdef __KERNEL__

/*
 * Common bits between 4K and 64K pages in a linux-style PTE.
 * Additional bits may be defined in pgtable-hash64-*.h
 *
 * Note: We only support user read/write permissions. Supervisor always
 * have full read/write to pages above PAGE_OFFSET (pages below that
 * always use the user access permissions).
 *
 * We could create separate kernel read-only if we used the 3 PP bits
 * combinations that newer processors provide but we currently don't.
 */
#define H_PAGE_BUSY		0x00800 /* software: PTE & hash are busy */
#define H_PTE_NONE_MASK		_PAGE_HPTEFLAGS
#define H_PAGE_F_GIX_SHIFT	57
#define H_PAGE_F_GIX		(7ul << 57)	/* HPTE index within HPTEG */
#define H_PAGE_F_SECOND		(1ul << 60)	/* HPTE is in 2ndary HPTEG */
#define H_PAGE_HASHPTE		(1ul << 61)	/* PTE has associated HPTE */

#ifdef CONFIG_PPC_64K_PAGES
#include <asm/book3s/64/hash-64k.h>
#else
#include <asm/book3s/64/hash-4k.h>
#endif

/*
 * Size of EA range mapped by our pagetables.
 */
#define PGTABLE_EADDR_SIZE	(PTE_INDEX_SIZE + PMD_INDEX_SIZE + \
				 PUD_INDEX_SIZE + PGD_INDEX_SIZE + PAGE_SHIFT)
#define PGTABLE_RANGE		(ASM_CONST(1) << PGTABLE_EADDR_SIZE)

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
#define PMD_CACHE_INDEX	(PMD_INDEX_SIZE + 1)
#else
#define PMD_CACHE_INDEX	PMD_INDEX_SIZE
#endif
/*
 * Define the address range of the kernel non-linear virtual area
 */
#define KERN_VIRT_START ASM_CONST(0xD000000000000000)
#define KERN_VIRT_SIZE	ASM_CONST(0x0000100000000000)

/*
 * The vmalloc space starts at the beginning of that region, and
 * occupies half of it on hash CPUs and a quarter of it on Book3E
 * (we keep a quarter for the virtual memmap)
 */
#define VMALLOC_START	KERN_VIRT_START
#define VMALLOC_SIZE	(KERN_VIRT_SIZE >> 1)
#define VMALLOC_END	(VMALLOC_START + VMALLOC_SIZE)

/*
 * Region IDs
 */
#define REGION_SHIFT		60UL
#define REGION_MASK		(0xfUL << REGION_SHIFT)
#define REGION_ID(ea)		(((unsigned long)(ea)) >> REGION_SHIFT)

#define VMALLOC_REGION_ID	(REGION_ID(VMALLOC_START))
#define KERNEL_REGION_ID	(REGION_ID(PAGE_OFFSET))
#define VMEMMAP_REGION_ID	(0xfUL)	/* Server only */
#define USER_REGION_ID		(0UL)

/*
 * Defines the address of the vmemap area, in its own region on
 * hash table CPUs.
 */
#define VMEMMAP_BASE		(VMEMMAP_REGION_ID << REGION_SHIFT)

#ifdef CONFIG_PPC_MM_SLICES
#define HAVE_ARCH_UNMAPPED_AREA
#define HAVE_ARCH_UNMAPPED_AREA_TOPDOWN
#endif /* CONFIG_PPC_MM_SLICES */


/* PTEIDX nibble */
#define _PTEIDX_SECONDARY	0x8
#define _PTEIDX_GROUP_IX	0x7

#define PMD_BAD_BITS		(PTE_TABLE_SIZE-1)
#define PUD_BAD_BITS		(PMD_TABLE_SIZE-1)

#ifndef __ASSEMBLY__
#define	pmd_bad(pmd)		(pmd_val(pmd) & PMD_BAD_BITS)
#define pmd_page_vaddr(pmd)	__va(pmd_val(pmd) & ~PMD_MASKED_BITS)

#define	pud_bad(pud)		(pud_val(pud) & PUD_BAD_BITS)
#define pud_page_vaddr(pud)	__va(pud_val(pud) & ~PUD_MASKED_BITS)

/* Pointers in the page table tree are physical addresses */
#define __pgtable_ptr_val(ptr)	__pa(ptr)

#define pgd_index(address) (((address) >> (PGDIR_SHIFT)) & (PTRS_PER_PGD - 1))
#define pud_index(address) (((address) >> (PUD_SHIFT)) & (PTRS_PER_PUD - 1))
#define pmd_index(address) (((address) >> (PMD_SHIFT)) & (PTRS_PER_PMD - 1))
#define pte_index(address) (((address) >> (PAGE_SHIFT)) & (PTRS_PER_PTE - 1))

extern void hpte_need_flush(struct mm_struct *mm, unsigned long addr,
			    pte_t *ptep, unsigned long pte, int huge);
extern unsigned long htab_convert_pte_flags(unsigned long pteflags);
/* Atomic PTE updates */
static inline unsigned long pte_update(struct mm_struct *mm,
				       unsigned long addr,
				       pte_t *ptep, unsigned long clr,
				       unsigned long set,
				       int huge)
{
	__be64 old_be, tmp_be;
	unsigned long old;

	__asm__ __volatile__(
	"1:	ldarx	%0,0,%3		# pte_update\n\
	and.	%1,%0,%6\n\
	bne-	1b \n\
	andc	%1,%0,%4 \n\
	or	%1,%1,%7\n\
	stdcx.	%1,0,%3 \n\
	bne-	1b"
	: "=&r" (old_be), "=&r" (tmp_be), "=m" (*ptep)
	: "r" (ptep), "r" (cpu_to_be64(clr)), "m" (*ptep),
	  "r" (cpu_to_be64(H_PAGE_BUSY)), "r" (cpu_to_be64(set))
	: "cc" );
	/* huge pages use the old page table lock */
	if (!huge)
		assert_pte_locked(mm, addr);

	old = be64_to_cpu(old_be);
	if (old & H_PAGE_HASHPTE)
		hpte_need_flush(mm, addr, ptep, old, huge);

	return old;
}

/* Set the dirty and/or accessed bits atomically in a linux PTE, this
 * function doesn't need to flush the hash entry
 */
static inline void __ptep_set_access_flags(pte_t *ptep, pte_t entry)
{
	__be64 old, tmp, val, mask;

	mask = cpu_to_be64(_PAGE_DIRTY | _PAGE_ACCESSED | _PAGE_READ | _PAGE_WRITE |
			   _PAGE_EXEC | _PAGE_SOFT_DIRTY);

	val = pte_raw(entry) & mask;

	__asm__ __volatile__(
	"1:	ldarx	%0,0,%4\n\
		and.	%1,%0,%6\n\
		bne-	1b \n\
		or	%0,%3,%0\n\
		stdcx.	%0,0,%4\n\
		bne-	1b"
	:"=&r" (old), "=&r" (tmp), "=m" (*ptep)
	:"r" (val), "r" (ptep), "m" (*ptep), "r" (cpu_to_be64(H_PAGE_BUSY))
	:"cc");
}

static inline int pgd_bad(pgd_t pgd)
{
	return (pgd_val(pgd) == 0);
}

#define __HAVE_ARCH_PTE_SAME
static inline int pte_same(pte_t pte_a, pte_t pte_b)
{
	return (((pte_raw(pte_a) ^ pte_raw(pte_b)) & ~cpu_to_be64(_PAGE_HPTEFLAGS)) == 0);
}

static inline unsigned long pgd_page_vaddr(pgd_t pgd)
{
	return (unsigned long)__va(pgd_val(pgd) & ~PGD_MASKED_BITS);
}


/* Generic accessors to PTE bits */
static inline int pte_none(pte_t pte)		{ return (pte_val(pte) & ~H_PTE_NONE_MASK) == 0; }

/* This low level function performs the actual PTE insertion
 * Setting the PTE depends on the MMU type and other factors. It's
 * an horrible mess that I'm not going to try to clean up now but
 * I'm keeping it in one place rather than spread around
 */
static inline void __set_pte_at(struct mm_struct *mm, unsigned long addr,
				pte_t *ptep, pte_t pte, int percpu)
{
	/*
	 * Anything else just stores the PTE normally. That covers all 64-bit
	 * cases, and 32-bit non-hash with 32-bit PTEs.
	 */
	*ptep = pte;
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
extern void hpte_do_hugepage_flush(struct mm_struct *mm, unsigned long addr,
				   pmd_t *pmdp, unsigned long old_pmd);
#else
static inline void hpte_do_hugepage_flush(struct mm_struct *mm,
					  unsigned long addr, pmd_t *pmdp,
					  unsigned long old_pmd)
{
	WARN(1, "%s called with THP disabled\n", __func__);
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

#endif /* !__ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_BOOK3S_64_HASH_H */
