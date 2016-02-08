#ifndef _ASM_POWERPC_BOOK3S_64_HASH_H
#define _ASM_POWERPC_BOOK3S_64_HASH_H
#ifdef __KERNEL__

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
#define _PAGE_PTE		0x00001
#define _PAGE_PRESENT		0x00002 /* software: pte contains a translation */
#define _PAGE_BIT_SWAP_TYPE	2
#define _PAGE_USER		0x00004 /* matches one of the PP bits */
#define _PAGE_EXEC		0x00008 /* No execute on POWER4 and newer (we invert) */
#define _PAGE_GUARDED		0x00010
/* We can derive Memory coherence from _PAGE_NO_CACHE */
#define _PAGE_COHERENT		0x0
#define _PAGE_NO_CACHE		0x00020 /* I: cache inhibit */
#define _PAGE_WRITETHRU		0x00040 /* W: cache write-through */
#define _PAGE_DIRTY		0x00080 /* C: page changed */
#define _PAGE_ACCESSED		0x00100 /* R: page referenced */
#define _PAGE_RW		0x00200 /* software: user write access allowed */
#define _PAGE_HASHPTE		0x00400 /* software: pte has an associated HPTE */
#define _PAGE_BUSY		0x00800 /* software: PTE & hash are busy */
#define _PAGE_F_GIX		0x07000 /* full page: hidx bits */
#define _PAGE_F_GIX_SHIFT	12
#define _PAGE_F_SECOND		0x08000 /* Whether to use secondary hash or not */
#define _PAGE_SPECIAL		0x10000 /* software: special page */

#ifdef CONFIG_MEM_SOFT_DIRTY
#define _PAGE_SOFT_DIRTY	0x20000 /* software: software dirty tracking */
#else
#define _PAGE_SOFT_DIRTY	0x00000
#endif

/*
 * We need to differentiate between explicit huge page and THP huge
 * page, since THP huge page also need to track real subpage details
 */
#define _PAGE_THP_HUGE  _PAGE_4K_PFN

/*
 * set of bits not changed in pmd_modify.
 */
#define _HPAGE_CHG_MASK (PTE_RPN_MASK | _PAGE_HPTEFLAGS | _PAGE_DIRTY | \
			 _PAGE_ACCESSED | _PAGE_THP_HUGE | _PAGE_PTE | \
			 _PAGE_SOFT_DIRTY)


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
			 _PAGE_ACCESSED | _PAGE_SPECIAL | _PAGE_PTE | \
			 _PAGE_SOFT_DIRTY)
/*
 * Mask of bits returned by pte_pgprot()
 */
#define PAGE_PROT_BITS	(_PAGE_GUARDED | _PAGE_COHERENT | _PAGE_NO_CACHE | \
			 _PAGE_WRITETHRU | _PAGE_4K_PFN | \
			 _PAGE_USER | _PAGE_ACCESSED |  \
			 _PAGE_RW |  _PAGE_DIRTY | _PAGE_EXEC | \
			 _PAGE_SOFT_DIRTY)
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
extern unsigned long htab_convert_pte_flags(unsigned long pteflags);
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
		(_PAGE_DIRTY | _PAGE_ACCESSED | _PAGE_RW | _PAGE_EXEC |
		 _PAGE_SOFT_DIRTY);

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

/* Generic accessors to PTE bits */
static inline int pte_write(pte_t pte)		{ return !!(pte_val(pte) & _PAGE_RW);}
static inline int pte_dirty(pte_t pte)		{ return !!(pte_val(pte) & _PAGE_DIRTY); }
static inline int pte_young(pte_t pte)		{ return !!(pte_val(pte) & _PAGE_ACCESSED); }
static inline int pte_special(pte_t pte)	{ return !!(pte_val(pte) & _PAGE_SPECIAL); }
static inline int pte_none(pte_t pte)		{ return (pte_val(pte) & ~_PTE_NONE_MASK) == 0; }
static inline pgprot_t pte_pgprot(pte_t pte)	{ return __pgprot(pte_val(pte) & PAGE_PROT_BITS); }

#ifdef CONFIG_HAVE_ARCH_SOFT_DIRTY
static inline bool pte_soft_dirty(pte_t pte)
{
	return !!(pte_val(pte) & _PAGE_SOFT_DIRTY);
}
static inline pte_t pte_mksoft_dirty(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_SOFT_DIRTY);
}

static inline pte_t pte_clear_soft_dirty(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_SOFT_DIRTY);
}
#endif /* CONFIG_HAVE_ARCH_SOFT_DIRTY */

#ifdef CONFIG_NUMA_BALANCING
/*
 * These work without NUMA balancing but the kernel does not care. See the
 * comment in include/asm-generic/pgtable.h . On powerpc, this will only
 * work for user pages and always return true for kernel pages.
 */
static inline int pte_protnone(pte_t pte)
{
	return (pte_val(pte) &
		(_PAGE_PRESENT | _PAGE_USER)) == _PAGE_PRESENT;
}
#endif /* CONFIG_NUMA_BALANCING */

static inline int pte_present(pte_t pte)
{
	return pte_val(pte) & _PAGE_PRESENT;
}

/* Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 *
 * Even if PTEs can be unsigned long long, a PFN is always an unsigned
 * long for now.
 */
static inline pte_t pfn_pte(unsigned long pfn, pgprot_t pgprot)
{
	return __pte(((pte_basic_t)(pfn) << PTE_RPN_SHIFT) |
		     pgprot_val(pgprot));
}

static inline unsigned long pte_pfn(pte_t pte)
{
	return pte_val(pte) >> PTE_RPN_SHIFT;
}

/* Generic modifiers for PTE bits */
static inline pte_t pte_wrprotect(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_RW);
}

static inline pte_t pte_mkclean(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_DIRTY);
}

static inline pte_t pte_mkold(pte_t pte)
{
	return __pte(pte_val(pte) & ~_PAGE_ACCESSED);
}

static inline pte_t pte_mkwrite(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_RW);
}

static inline pte_t pte_mkdirty(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_DIRTY | _PAGE_SOFT_DIRTY);
}

static inline pte_t pte_mkyoung(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_ACCESSED);
}

static inline pte_t pte_mkspecial(pte_t pte)
{
	return __pte(pte_val(pte) | _PAGE_SPECIAL);
}

static inline pte_t pte_mkhuge(pte_t pte)
{
	return pte;
}

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot));
}

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

/*
 * Macro to mark a page protection value as "uncacheable".
 */

#define _PAGE_CACHE_CTL	(_PAGE_COHERENT | _PAGE_GUARDED | _PAGE_NO_CACHE | \
			 _PAGE_WRITETHRU)

#define pgprot_noncached pgprot_noncached
static inline pgprot_t pgprot_noncached(pgprot_t prot)
{
	return __pgprot((pgprot_val(prot) & ~_PAGE_CACHE_CTL) |
			_PAGE_NO_CACHE | _PAGE_GUARDED);
}

#define pgprot_noncached_wc pgprot_noncached_wc
static inline pgprot_t pgprot_noncached_wc(pgprot_t prot)
{
	return __pgprot((pgprot_val(prot) & ~_PAGE_CACHE_CTL) |
			_PAGE_NO_CACHE);
}

#define pgprot_cached pgprot_cached
static inline pgprot_t pgprot_cached(pgprot_t prot)
{
	return __pgprot((pgprot_val(prot) & ~_PAGE_CACHE_CTL) |
			_PAGE_COHERENT);
}

#define pgprot_cached_wthru pgprot_cached_wthru
static inline pgprot_t pgprot_cached_wthru(pgprot_t prot)
{
	return __pgprot((pgprot_val(prot) & ~_PAGE_CACHE_CTL) |
			_PAGE_COHERENT | _PAGE_WRITETHRU);
}

#define pgprot_cached_noncoherent pgprot_cached_noncoherent
static inline pgprot_t pgprot_cached_noncoherent(pgprot_t prot)
{
	return __pgprot(pgprot_val(prot) & ~_PAGE_CACHE_CTL);
}

#define pgprot_writecombine pgprot_writecombine
static inline pgprot_t pgprot_writecombine(pgprot_t prot)
{
	return pgprot_noncached_wc(prot);
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
