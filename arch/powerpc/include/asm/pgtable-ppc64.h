#ifndef _ASM_POWERPC_PGTABLE_PPC64_H_
#define _ASM_POWERPC_PGTABLE_PPC64_H_
/*
 * This file contains the functions and defines necessary to modify and use
 * the ppc64 hashed page table.
 */

#ifndef __ASSEMBLY__
#include <linux/stddef.h>
#include <asm/tlbflush.h>
#endif /* __ASSEMBLY__ */

#ifdef CONFIG_PPC_64K_PAGES
#include <asm/pgtable-ppc64-64k.h>
#else
#include <asm/pgtable-ppc64-4k.h>
#endif

#define FIRST_USER_ADDRESS	0

/*
 * Size of EA range mapped by our pagetables.
 */
#define PGTABLE_EADDR_SIZE (PTE_INDEX_SIZE + PMD_INDEX_SIZE + \
                	    PUD_INDEX_SIZE + PGD_INDEX_SIZE + PAGE_SHIFT)
#define PGTABLE_RANGE (ASM_CONST(1) << PGTABLE_EADDR_SIZE)


/* Some sanity checking */
#if TASK_SIZE_USER64 > PGTABLE_RANGE
#error TASK_SIZE_USER64 exceeds pagetable range
#endif

#ifdef CONFIG_PPC_STD_MMU_64
#if TASK_SIZE_USER64 > (1UL << (USER_ESID_BITS + SID_SHIFT))
#error TASK_SIZE_USER64 exceeds user VSID range
#endif
#endif

/*
 * Define the address range of the vmalloc VM area.
 */
#define VMALLOC_START ASM_CONST(0xD000000000000000)
#define VMALLOC_SIZE  (PGTABLE_RANGE >> 1)
#define VMALLOC_END   (VMALLOC_START + VMALLOC_SIZE)

/*
 * Define the address ranges for MMIO and IO space :
 *
 *  ISA_IO_BASE = VMALLOC_END, 64K reserved area
 *  PHB_IO_BASE = ISA_IO_BASE + 64K to ISA_IO_BASE + 2G, PHB IO spaces
 * IOREMAP_BASE = ISA_IO_BASE + 2G to VMALLOC_START + PGTABLE_RANGE
 */
#define FULL_IO_SIZE	0x80000000ul
#define  ISA_IO_BASE	(VMALLOC_END)
#define  ISA_IO_END	(VMALLOC_END + 0x10000ul)
#define  PHB_IO_BASE	(ISA_IO_END)
#define  PHB_IO_END	(VMALLOC_END + FULL_IO_SIZE)
#define IOREMAP_BASE	(PHB_IO_END)
#define IOREMAP_END	(VMALLOC_START + PGTABLE_RANGE)

/*
 * Region IDs
 */
#define REGION_SHIFT		60UL
#define REGION_MASK		(0xfUL << REGION_SHIFT)
#define REGION_ID(ea)		(((unsigned long)(ea)) >> REGION_SHIFT)

#define VMALLOC_REGION_ID	(REGION_ID(VMALLOC_START))
#define KERNEL_REGION_ID	(REGION_ID(PAGE_OFFSET))
#define VMEMMAP_REGION_ID	(0xfUL)
#define USER_REGION_ID		(0UL)

/*
 * Defines the address of the vmemap area, in its own region
 */
#define VMEMMAP_BASE		(VMEMMAP_REGION_ID << REGION_SHIFT)
#define vmemmap			((struct page *)VMEMMAP_BASE)


/*
 * Include the PTE bits definitions
 */
#include <asm/pte-hash64.h>
#include <asm/pte-common.h>


#ifdef CONFIG_PPC_MM_SLICES
#define HAVE_ARCH_UNMAPPED_AREA
#define HAVE_ARCH_UNMAPPED_AREA_TOPDOWN
#endif /* CONFIG_PPC_MM_SLICES */

#ifndef __ASSEMBLY__

/*
 * This is the default implementation of various PTE accessors, it's
 * used in all cases except Book3S with 64K pages where we have a
 * concept of sub-pages
 */
#ifndef __real_pte

#ifdef STRICT_MM_TYPECHECKS
#define __real_pte(e,p)		((real_pte_t){(e)})
#define __rpte_to_pte(r)	((r).pte)
#else
#define __real_pte(e,p)		(e)
#define __rpte_to_pte(r)	(__pte(r))
#endif
#define __rpte_to_hidx(r,index)	(pte_val(__rpte_to_pte(r)) >> 12)

#define pte_iterate_hashed_subpages(rpte, psize, va, index, shift)       \
	do {							         \
		index = 0;					         \
		shift = mmu_psize_defs[psize].shift;		         \

#define pte_iterate_hashed_end() } while(0)

#ifdef CONFIG_PPC_HAS_HASH_64K
#define pte_pagesize_index(mm, addr, pte)	get_slice_psize(mm, addr)
#else
#define pte_pagesize_index(mm, addr, pte)	MMU_PAGE_4K
#endif

#endif /* __real_pte */


/* pte_clear moved to later in this file */

#define PMD_BAD_BITS		(PTE_TABLE_SIZE-1)
#define PUD_BAD_BITS		(PMD_TABLE_SIZE-1)

#define pmd_set(pmdp, pmdval) 	(pmd_val(*(pmdp)) = (pmdval))
#define pmd_none(pmd)		(!pmd_val(pmd))
#define	pmd_bad(pmd)		(!is_kernel_addr(pmd_val(pmd)) \
				 || (pmd_val(pmd) & PMD_BAD_BITS))
#define	pmd_present(pmd)	(pmd_val(pmd) != 0)
#define	pmd_clear(pmdp)		(pmd_val(*(pmdp)) = 0)
#define pmd_page_vaddr(pmd)	(pmd_val(pmd) & ~PMD_MASKED_BITS)
#define pmd_page(pmd)		virt_to_page(pmd_page_vaddr(pmd))

#define pud_set(pudp, pudval)	(pud_val(*(pudp)) = (pudval))
#define pud_none(pud)		(!pud_val(pud))
#define	pud_bad(pud)		(!is_kernel_addr(pud_val(pud)) \
				 || (pud_val(pud) & PUD_BAD_BITS))
#define pud_present(pud)	(pud_val(pud) != 0)
#define pud_clear(pudp)		(pud_val(*(pudp)) = 0)
#define pud_page_vaddr(pud)	(pud_val(pud) & ~PUD_MASKED_BITS)
#define pud_page(pud)		virt_to_page(pud_page_vaddr(pud))

#define pgd_set(pgdp, pudp)	({pgd_val(*(pgdp)) = (unsigned long)(pudp);})

/*
 * Find an entry in a page-table-directory.  We combine the address region
 * (the high order N bits) and the pgd portion of the address.
 */
/* to avoid overflow in free_pgtables we don't use PTRS_PER_PGD here */
#define pgd_index(address) (((address) >> (PGDIR_SHIFT)) & 0x1ff)

#define pgd_offset(mm, address)	 ((mm)->pgd + pgd_index(address))

#define pmd_offset(pudp,addr) \
  (((pmd_t *) pud_page_vaddr(*(pudp))) + (((addr) >> PMD_SHIFT) & (PTRS_PER_PMD - 1)))

#define pte_offset_kernel(dir,addr) \
  (((pte_t *) pmd_page_vaddr(*(dir))) + (((addr) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1)))

#define pte_offset_map(dir,addr)	pte_offset_kernel((dir), (addr))
#define pte_offset_map_nested(dir,addr)	pte_offset_kernel((dir), (addr))
#define pte_unmap(pte)			do { } while(0)
#define pte_unmap_nested(pte)		do { } while(0)

/* to find an entry in a kernel page-table-directory */
/* This now only contains the vmalloc pages */
#define pgd_offset_k(address) pgd_offset(&init_mm, address)


/* Atomic PTE updates */
static inline unsigned long pte_update(struct mm_struct *mm,
				       unsigned long addr,
				       pte_t *ptep, unsigned long clr,
				       int huge)
{
#ifdef PTE_ATOMIC_UPDATES
	unsigned long old, tmp;

	__asm__ __volatile__(
	"1:	ldarx	%0,0,%3		# pte_update\n\
	andi.	%1,%0,%6\n\
	bne-	1b \n\
	andc	%1,%0,%4 \n\
	stdcx.	%1,0,%3 \n\
	bne-	1b"
	: "=&r" (old), "=&r" (tmp), "=m" (*ptep)
	: "r" (ptep), "r" (clr), "m" (*ptep), "i" (_PAGE_BUSY)
	: "cc" );
#else
	unsigned long old = pte_val(*ptep);
	*ptep = __pte(old & ~clr);
#endif
	/* huge pages use the old page table lock */
	if (!huge)
		assert_pte_locked(mm, addr);

#ifdef CONFIG_PPC_STD_MMU_64
	if (old & _PAGE_HASHPTE)
		hpte_need_flush(mm, addr, ptep, old, huge);
#endif

	return old;
}

static inline int __ptep_test_and_clear_young(struct mm_struct *mm,
					      unsigned long addr, pte_t *ptep)
{
	unsigned long old;

       	if ((pte_val(*ptep) & (_PAGE_ACCESSED | _PAGE_HASHPTE)) == 0)
		return 0;
	old = pte_update(mm, addr, ptep, _PAGE_ACCESSED, 0);
	return (old & _PAGE_ACCESSED) != 0;
}
#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
#define ptep_test_and_clear_young(__vma, __addr, __ptep)		   \
({									   \
	int __r;							   \
	__r = __ptep_test_and_clear_young((__vma)->vm_mm, __addr, __ptep); \
	__r;								   \
})

#define __HAVE_ARCH_PTEP_SET_WRPROTECT
static inline void ptep_set_wrprotect(struct mm_struct *mm, unsigned long addr,
				      pte_t *ptep)
{
	unsigned long old;

       	if ((pte_val(*ptep) & _PAGE_RW) == 0)
       		return;
	old = pte_update(mm, addr, ptep, _PAGE_RW, 0);
}

static inline void huge_ptep_set_wrprotect(struct mm_struct *mm,
					   unsigned long addr, pte_t *ptep)
{
	unsigned long old;

	if ((pte_val(*ptep) & _PAGE_RW) == 0)
		return;
	old = pte_update(mm, addr, ptep, _PAGE_RW, 1);
}

/*
 * We currently remove entries from the hashtable regardless of whether
 * the entry was young or dirty. The generic routines only flush if the
 * entry was young or dirty which is not good enough.
 *
 * We should be more intelligent about this but for the moment we override
 * these functions and force a tlb flush unconditionally
 */
#define __HAVE_ARCH_PTEP_CLEAR_YOUNG_FLUSH
#define ptep_clear_flush_young(__vma, __address, __ptep)		\
({									\
	int __young = __ptep_test_and_clear_young((__vma)->vm_mm, __address, \
						  __ptep);		\
	__young;							\
})

#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
static inline pte_t ptep_get_and_clear(struct mm_struct *mm,
				       unsigned long addr, pte_t *ptep)
{
	unsigned long old = pte_update(mm, addr, ptep, ~0UL, 0);
	return __pte(old);
}

static inline void pte_clear(struct mm_struct *mm, unsigned long addr,
			     pte_t * ptep)
{
	pte_update(mm, addr, ptep, ~0UL, 0);
}


/* Set the dirty and/or accessed bits atomically in a linux PTE, this
 * function doesn't need to flush the hash entry
 */
static inline void __ptep_set_access_flags(pte_t *ptep, pte_t entry)
{
	unsigned long bits = pte_val(entry) &
		(_PAGE_DIRTY | _PAGE_ACCESSED | _PAGE_RW |
		 _PAGE_EXEC | _PAGE_HWEXEC);

#ifdef PTE_ATOMIC_UPDATES
	unsigned long old, tmp;

	__asm__ __volatile__(
	"1:	ldarx	%0,0,%4\n\
		andi.	%1,%0,%6\n\
		bne-	1b \n\
		or	%0,%3,%0\n\
		stdcx.	%0,0,%4\n\
		bne-	1b"
	:"=&r" (old), "=&r" (tmp), "=m" (*ptep)
	:"r" (bits), "r" (ptep), "m" (*ptep), "i" (_PAGE_BUSY)
	:"cc");
#else
	unsigned long old = pte_val(*ptep);
	*ptep = __pte(old | bits);
#endif
}

#define __HAVE_ARCH_PTE_SAME
#define pte_same(A,B)	(((pte_val(A) ^ pte_val(B)) & ~_PAGE_HPTEFLAGS) == 0)

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))
#define pmd_ERROR(e) \
	printk("%s:%d: bad pmd %08lx.\n", __FILE__, __LINE__, pmd_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__, pgd_val(e))

/* Encode and de-code a swap entry */
#define __swp_type(entry)	(((entry).val >> 1) & 0x3f)
#define __swp_offset(entry)	((entry).val >> 8)
#define __swp_entry(type, offset) ((swp_entry_t){((type)<< 1)|((offset)<<8)})
#define __pte_to_swp_entry(pte)	((swp_entry_t){pte_val(pte) >> PTE_RPN_SHIFT})
#define __swp_entry_to_pte(x)	((pte_t) { (x).val << PTE_RPN_SHIFT })
#define pte_to_pgoff(pte)	(pte_val(pte) >> PTE_RPN_SHIFT)
#define pgoff_to_pte(off)	((pte_t) {((off) << PTE_RPN_SHIFT)|_PAGE_FILE})
#define PTE_FILE_MAX_BITS	(BITS_PER_LONG - PTE_RPN_SHIFT)

void pgtable_cache_init(void);

/*
 * find_linux_pte returns the address of a linux pte for a given
 * effective address and directory.  If not found, it returns zero.
 */static inline pte_t *find_linux_pte(pgd_t *pgdir, unsigned long ea)
{
	pgd_t *pg;
	pud_t *pu;
	pmd_t *pm;
	pte_t *pt = NULL;

	pg = pgdir + pgd_index(ea);
	if (!pgd_none(*pg)) {
		pu = pud_offset(pg, ea);
		if (!pud_none(*pu)) {
			pm = pmd_offset(pu, ea);
			if (pmd_present(*pm))
				pt = pte_offset_kernel(pm, ea);
		}
	}
	return pt;
}

pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long address);

#endif /* __ASSEMBLY__ */

#endif /* _ASM_POWERPC_PGTABLE_PPC64_H_ */
