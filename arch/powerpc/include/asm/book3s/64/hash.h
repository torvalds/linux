#ifndef _ASM_POWERPC_BOOK3S_64_HASH_H
#define _ASM_POWERPC_BOOK3S_64_HASH_H
#ifdef __KERNEL__

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
/*
 * Common bits between 4K and 64K pages in a linux-style PTE.
 * These match the bits in the (hardware-defined) PowerPC PTE as closely
 * as possible. Additional bits may be defined in pgtable-hash64-*.h
 *
 * Note: We only support user read/write permissions. Supervisor always
 * have full read/write to pages above PAGE_OFFSET (pages below that
 * always use the user access permissions).
 *
 * We could create separate kernel read-only if we used the 3 PP bits
 * combinations that newer processors provide but we currently don't.
 */
#define _PAGE_PRESENT		0x0001 /* software: pte contains a translation */
#define _PAGE_USER		0x0002 /* matches one of the PP bits */
#define _PAGE_BIT_SWAP_TYPE	2
#define _PAGE_EXEC		0x0004 /* No execute on POWER4 and newer (we invert) */
#define _PAGE_GUARDED		0x0008
/* We can derive Memory coherence from _PAGE_NO_CACHE */
#define _PAGE_COHERENT		0x0
#define _PAGE_NO_CACHE		0x0020 /* I: cache inhibit */
#define _PAGE_WRITETHRU		0x0040 /* W: cache write-through */
#define _PAGE_DIRTY		0x0080 /* C: page changed */
#define _PAGE_ACCESSED		0x0100 /* R: page referenced */
#define _PAGE_RW		0x0200 /* software: user write access allowed */
#define _PAGE_BUSY		0x0800 /* software: PTE & hash are busy */

/* No separate kernel read-only */
#define _PAGE_KERNEL_RW		(_PAGE_RW | _PAGE_DIRTY) /* user access blocked by key */
#define _PAGE_KERNEL_RO		 _PAGE_KERNEL_RW
#define _PAGE_KERNEL_RWX	(_PAGE_DIRTY | _PAGE_RW | _PAGE_EXEC)

/* Strong Access Ordering */
#define _PAGE_SAO		(_PAGE_WRITETHRU | _PAGE_NO_CACHE | _PAGE_COHERENT)

/* No page size encoding in the linux PTE */
#define _PAGE_PSIZE		0

/* PTEIDX nibble */
#define _PTEIDX_SECONDARY	0x8
#define _PTEIDX_GROUP_IX	0x7

/* Hash table based platforms need atomic updates of the linux PTE */
#define PTE_ATOMIC_UPDATES	1

/*
 * THP pages can't be special. So use the _PAGE_SPECIAL
 */
#define _PAGE_SPLITTING _PAGE_SPECIAL

/*
 * We need to differentiate between explicit huge page and THP huge
 * page, since THP huge page also need to track real subpage details
 */
#define _PAGE_THP_HUGE  _PAGE_4K_PFN

/*
 * set of bits not changed in pmd_modify.
 */
#define _HPAGE_CHG_MASK (PTE_RPN_MASK | _PAGE_HPTEFLAGS |		\
			 _PAGE_DIRTY | _PAGE_ACCESSED | _PAGE_SPLITTING | \
			 _PAGE_THP_HUGE)
#define _PTE_NONE_MASK	_PAGE_HPTEFLAGS
/*
 * The mask convered by the RPN must be a ULL on 32-bit platforms with
 * 64-bit PTEs
 */
#define PTE_RPN_MASK	(~((1UL << PTE_RPN_SHIFT) - 1))
/*
 * _PAGE_CHG_MASK masks of bits that are to be preserved across
 * pgprot changes
 */
#define _PAGE_CHG_MASK	(PTE_RPN_MASK | _PAGE_HPTEFLAGS | _PAGE_DIRTY | \
			 _PAGE_ACCESSED | _PAGE_SPECIAL)
/*
 * Mask of bits returned by pte_pgprot()
 */
#define PAGE_PROT_BITS	(_PAGE_GUARDED | _PAGE_COHERENT | _PAGE_NO_CACHE | \
			 _PAGE_WRITETHRU | _PAGE_4K_PFN | \
			 _PAGE_USER | _PAGE_ACCESSED |  \
			 _PAGE_RW |  _PAGE_DIRTY | _PAGE_EXEC)
/*
 * We define 2 sets of base prot bits, one for basic pages (ie,
 * cacheable kernel and user pages) and one for non cacheable
 * pages. We always set _PAGE_COHERENT when SMP is enabled or
 * the processor might need it for DMA coherency.
 */
#define _PAGE_BASE_NC	(_PAGE_PRESENT | _PAGE_ACCESSED | _PAGE_PSIZE)
#define _PAGE_BASE	(_PAGE_BASE_NC | _PAGE_COHERENT)

/* Permission masks used to generate the __P and __S table,
 *
 * Note:__pgprot is defined in arch/powerpc/include/asm/page.h
 *
 * Write permissions imply read permissions for now (we could make write-only
 * pages on BookE but we don't bother for now). Execute permission control is
 * possible on platforms that define _PAGE_EXEC
 *
 * Note due to the way vm flags are laid out, the bits are XWR
 */
#define PAGE_NONE	__pgprot(_PAGE_BASE)
#define PAGE_SHARED	__pgprot(_PAGE_BASE | _PAGE_USER | _PAGE_RW)
#define PAGE_SHARED_X	__pgprot(_PAGE_BASE | _PAGE_USER | _PAGE_RW | \
				 _PAGE_EXEC)
#define PAGE_COPY	__pgprot(_PAGE_BASE | _PAGE_USER )
#define PAGE_COPY_X	__pgprot(_PAGE_BASE | _PAGE_USER | _PAGE_EXEC)
#define PAGE_READONLY	__pgprot(_PAGE_BASE | _PAGE_USER )
#define PAGE_READONLY_X	__pgprot(_PAGE_BASE | _PAGE_USER | _PAGE_EXEC)

#define __P000	PAGE_NONE
#define __P001	PAGE_READONLY
#define __P010	PAGE_COPY
#define __P011	PAGE_COPY
#define __P100	PAGE_READONLY_X
#define __P101	PAGE_READONLY_X
#define __P110	PAGE_COPY_X
#define __P111	PAGE_COPY_X

#define __S000	PAGE_NONE
#define __S001	PAGE_READONLY
#define __S010	PAGE_SHARED
#define __S011	PAGE_SHARED
#define __S100	PAGE_READONLY_X
#define __S101	PAGE_READONLY_X
#define __S110	PAGE_SHARED_X
#define __S111	PAGE_SHARED_X

/* Permission masks used for kernel mappings */
#define PAGE_KERNEL	__pgprot(_PAGE_BASE | _PAGE_KERNEL_RW)
#define PAGE_KERNEL_NC	__pgprot(_PAGE_BASE_NC | _PAGE_KERNEL_RW | \
				 _PAGE_NO_CACHE)
#define PAGE_KERNEL_NCG	__pgprot(_PAGE_BASE_NC | _PAGE_KERNEL_RW | \
				 _PAGE_NO_CACHE | _PAGE_GUARDED)
#define PAGE_KERNEL_X	__pgprot(_PAGE_BASE | _PAGE_KERNEL_RWX)
#define PAGE_KERNEL_RO	__pgprot(_PAGE_BASE | _PAGE_KERNEL_RO)
#define PAGE_KERNEL_ROX	__pgprot(_PAGE_BASE | _PAGE_KERNEL_ROX)

/* Protection used for kernel text. We want the debuggers to be able to
 * set breakpoints anywhere, so don't write protect the kernel text
 * on platforms where such control is possible.
 */
#if defined(CONFIG_KGDB) || defined(CONFIG_XMON) || defined(CONFIG_BDI_SWITCH) ||\
	defined(CONFIG_KPROBES) || defined(CONFIG_DYNAMIC_FTRACE)
#define PAGE_KERNEL_TEXT	PAGE_KERNEL_X
#else
#define PAGE_KERNEL_TEXT	PAGE_KERNEL_ROX
#endif

/* Make modules code happy. We don't set RO yet */
#define PAGE_KERNEL_EXEC	PAGE_KERNEL_X
#define PAGE_AGP		(PAGE_KERNEL_NC)

#define PMD_BAD_BITS		(PTE_TABLE_SIZE-1)
#define PUD_BAD_BITS		(PMD_TABLE_SIZE-1)
/*
 * We save the slot number & secondary bit in the second half of the
 * PTE page. We use the 8 bytes per each pte entry.
 */
#define PTE_PAGE_HIDX_OFFSET (PTRS_PER_PTE * 8)

#ifndef __ASSEMBLY__
#define	pmd_bad(pmd)		(!is_kernel_addr(pmd_val(pmd)) \
				 || (pmd_val(pmd) & PMD_BAD_BITS))
#define pmd_page_vaddr(pmd)	(pmd_val(pmd) & ~PMD_MASKED_BITS)

#define	pud_bad(pud)		(!is_kernel_addr(pud_val(pud)) \
				 || (pud_val(pud) & PUD_BAD_BITS))
#define pud_page_vaddr(pud)	(pud_val(pud) & ~PUD_MASKED_BITS)

#define pgd_index(address) (((address) >> (PGDIR_SHIFT)) & (PTRS_PER_PGD - 1))
#define pmd_index(address) (((address) >> (PMD_SHIFT)) & (PTRS_PER_PMD - 1))
#define pte_index(address) (((address) >> (PAGE_SHIFT)) & (PTRS_PER_PTE - 1))

extern void hpte_need_flush(struct mm_struct *mm, unsigned long addr,
			    pte_t *ptep, unsigned long pte, int huge);
extern unsigned long pmd_hugepage_update(struct mm_struct *mm,
					 unsigned long addr,
					 pmd_t *pmdp,
					 unsigned long clr,
					 unsigned long set);
/* Atomic PTE updates */
static inline unsigned long pte_update(struct mm_struct *mm,
				       unsigned long addr,
				       pte_t *ptep, unsigned long clr,
				       unsigned long set,
				       int huge)
{
	unsigned long old, tmp;

	__asm__ __volatile__(
	"1:	ldarx	%0,0,%3		# pte_update\n\
	andi.	%1,%0,%6\n\
	bne-	1b \n\
	andc	%1,%0,%4 \n\
	or	%1,%1,%7\n\
	stdcx.	%1,0,%3 \n\
	bne-	1b"
	: "=&r" (old), "=&r" (tmp), "=m" (*ptep)
	: "r" (ptep), "r" (clr), "m" (*ptep), "i" (_PAGE_BUSY), "r" (set)
	: "cc" );
	/* huge pages use the old page table lock */
	if (!huge)
		assert_pte_locked(mm, addr);

	if (old & _PAGE_HASHPTE)
		hpte_need_flush(mm, addr, ptep, old, huge);

	return old;
}

static inline int __ptep_test_and_clear_young(struct mm_struct *mm,
					      unsigned long addr, pte_t *ptep)
{
	unsigned long old;

	if ((pte_val(*ptep) & (_PAGE_ACCESSED | _PAGE_HASHPTE)) == 0)
		return 0;
	old = pte_update(mm, addr, ptep, _PAGE_ACCESSED, 0, 0);
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

	if ((pte_val(*ptep) & _PAGE_RW) == 0)
		return;

	pte_update(mm, addr, ptep, _PAGE_RW, 0, 0);
}

static inline void huge_ptep_set_wrprotect(struct mm_struct *mm,
					   unsigned long addr, pte_t *ptep)
{
	if ((pte_val(*ptep) & _PAGE_RW) == 0)
		return;

	pte_update(mm, addr, ptep, _PAGE_RW, 0, 1);
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
	unsigned long old = pte_update(mm, addr, ptep, ~0UL, 0, 0);
	return __pte(old);
}

static inline void pte_clear(struct mm_struct *mm, unsigned long addr,
			     pte_t * ptep)
{
	pte_update(mm, addr, ptep, ~0UL, 0, 0);
}


/* Set the dirty and/or accessed bits atomically in a linux PTE, this
 * function doesn't need to flush the hash entry
 */
static inline void __ptep_set_access_flags(pte_t *ptep, pte_t entry)
{
	unsigned long bits = pte_val(entry) &
		(_PAGE_DIRTY | _PAGE_ACCESSED | _PAGE_RW | _PAGE_EXEC);

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
}

#define __HAVE_ARCH_PTE_SAME
#define pte_same(A,B)	(((pte_val(A) ^ pte_val(B)) & ~_PAGE_HPTEFLAGS) == 0)

static inline char *get_hpte_slot_array(pmd_t *pmdp)
{
	/*
	 * The hpte hindex is stored in the pgtable whose address is in the
	 * second half of the PMD
	 *
	 * Order this load with the test for pmd_trans_huge in the caller
	 */
	smp_rmb();
	return *(char **)(pmdp + PTRS_PER_PMD);


}
/*
 * The linux hugepage PMD now include the pmd entries followed by the address
 * to the stashed pgtable_t. The stashed pgtable_t contains the hpte bits.
 * [ 1 bit secondary | 3 bit hidx | 1 bit valid | 000]. We use one byte per
 * each HPTE entry. With 16MB hugepage and 64K HPTE we need 256 entries and
 * with 4K HPTE we need 4096 entries. Both will fit in a 4K pgtable_t.
 *
 * The last three bits are intentionally left to zero. This memory location
 * are also used as normal page PTE pointers. So if we have any pointers
 * left around while we collapse a hugepage, we need to make sure
 * _PAGE_PRESENT bit of that is zero when we look at them
 */
static inline unsigned int hpte_valid(unsigned char *hpte_slot_array, int index)
{
	return (hpte_slot_array[index] >> 3) & 0x1;
}

static inline unsigned int hpte_hash_index(unsigned char *hpte_slot_array,
					   int index)
{
	return hpte_slot_array[index] >> 4;
}

static inline void mark_hpte_slot_valid(unsigned char *hpte_slot_array,
					unsigned int index, unsigned int hidx)
{
	hpte_slot_array[index] = hidx << 4 | 0x1 << 3;
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
/*
 *
 * For core kernel code by design pmd_trans_huge is never run on any hugetlbfs
 * page. The hugetlbfs page table walking and mangling paths are totally
 * separated form the core VM paths and they're differentiated by
 *  VM_HUGETLB being set on vm_flags well before any pmd_trans_huge could run.
 *
 * pmd_trans_huge() is defined as false at build time if
 * CONFIG_TRANSPARENT_HUGEPAGE=n to optimize away code blocks at build
 * time in such case.
 *
 * For ppc64 we need to differntiate from explicit hugepages from THP, because
 * for THP we also track the subpage details at the pmd level. We don't do
 * that for explicit huge pages.
 *
 */
static inline int pmd_trans_huge(pmd_t pmd)
{
	/*
	 * leaf pte for huge page, bottom two bits != 00
	 */
	return (pmd_val(pmd) & 0x3) && (pmd_val(pmd) & _PAGE_THP_HUGE);
}

static inline int pmd_trans_splitting(pmd_t pmd)
{
	if (pmd_trans_huge(pmd))
		return pmd_val(pmd) & _PAGE_SPLITTING;
	return 0;
}

#endif
static inline int pmd_large(pmd_t pmd)
{
	/*
	 * leaf pte for huge page, bottom two bits != 00
	 */
	return ((pmd_val(pmd) & 0x3) != 0x0);
}

static inline pmd_t pmd_mknotpresent(pmd_t pmd)
{
	return __pmd(pmd_val(pmd) & ~_PAGE_PRESENT);
}

static inline pmd_t pmd_mksplitting(pmd_t pmd)
{
	return __pmd(pmd_val(pmd) | _PAGE_SPLITTING);
}

#define __HAVE_ARCH_PMD_SAME
static inline int pmd_same(pmd_t pmd_a, pmd_t pmd_b)
{
	return (((pmd_val(pmd_a) ^ pmd_val(pmd_b)) & ~_PAGE_HPTEFLAGS) == 0);
}

static inline int __pmdp_test_and_clear_young(struct mm_struct *mm,
					      unsigned long addr, pmd_t *pmdp)
{
	unsigned long old;

	if ((pmd_val(*pmdp) & (_PAGE_ACCESSED | _PAGE_HASHPTE)) == 0)
		return 0;
	old = pmd_hugepage_update(mm, addr, pmdp, _PAGE_ACCESSED, 0);
	return ((old & _PAGE_ACCESSED) != 0);
}

#define __HAVE_ARCH_PMDP_SET_WRPROTECT
static inline void pmdp_set_wrprotect(struct mm_struct *mm, unsigned long addr,
				      pmd_t *pmdp)
{

	if ((pmd_val(*pmdp) & _PAGE_RW) == 0)
		return;

	pmd_hugepage_update(mm, addr, pmdp, _PAGE_RW, 0);
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
