#ifndef _ASM_IA64_PAGE_H
#define _ASM_IA64_PAGE_H
/*
 * Pagetable related stuff.
 *
 * Copyright (C) 1998, 1999, 2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <asm/intrinsics.h>
#include <asm/types.h>

/*
 * The top three bits of an IA64 address are its Region Number.
 * Different regions are assigned to different purposes.
 */
#define RGN_SHIFT	(61)
#define RGN_BASE(r)	(__IA64_UL_CONST(r)<<RGN_SHIFT)
#define RGN_BITS	(RGN_BASE(-1))

#define RGN_KERNEL	7	/* Identity mapped region */
#define RGN_UNCACHED    6	/* Identity mapped I/O region */
#define RGN_GATE	5	/* Gate page, Kernel text, etc */
#define RGN_HPAGE	4	/* For Huge TLB pages */

/*
 * PAGE_SHIFT determines the actual kernel page size.
 */
#if defined(CONFIG_IA64_PAGE_SIZE_4KB)
# define PAGE_SHIFT	12
#elif defined(CONFIG_IA64_PAGE_SIZE_8KB)
# define PAGE_SHIFT	13
#elif defined(CONFIG_IA64_PAGE_SIZE_16KB)
# define PAGE_SHIFT	14
#elif defined(CONFIG_IA64_PAGE_SIZE_64KB)
# define PAGE_SHIFT	16
#else
# error Unsupported page size!
#endif

#define PAGE_SIZE		(__IA64_UL_CONST(1) << PAGE_SHIFT)
#define PAGE_MASK		(~(PAGE_SIZE - 1))

#define PERCPU_PAGE_SHIFT	18	/* log2() of max. size of per-CPU area */
#define PERCPU_PAGE_SIZE	(__IA64_UL_CONST(1) << PERCPU_PAGE_SHIFT)


#ifdef CONFIG_HUGETLB_PAGE
# define HPAGE_REGION_BASE	RGN_BASE(RGN_HPAGE)
# define HPAGE_SHIFT		hpage_shift
# define HPAGE_SHIFT_DEFAULT	28	/* check ia64 SDM for architecture supported size */
# define HPAGE_SIZE		(__IA64_UL_CONST(1) << HPAGE_SHIFT)
# define HPAGE_MASK		(~(HPAGE_SIZE - 1))

# define HAVE_ARCH_HUGETLB_UNMAPPED_AREA
#endif /* CONFIG_HUGETLB_PAGE */

#ifdef __ASSEMBLY__
# define __pa(x)		((x) - PAGE_OFFSET)
# define __va(x)		((x) + PAGE_OFFSET)
#else /* !__ASSEMBLY */
#  define STRICT_MM_TYPECHECKS

extern void clear_page (void *page);
extern void copy_page (void *to, void *from);

/*
 * clear_user_page() and copy_user_page() can't be inline functions because
 * flush_dcache_page() can't be defined until later...
 */
#define clear_user_page(addr, vaddr, page)	\
do {						\
	clear_page(addr);			\
	flush_dcache_page(page);		\
} while (0)

#define copy_user_page(to, from, vaddr, page)	\
do {						\
	copy_page((to), (from));		\
	flush_dcache_page(page);		\
} while (0)


#define __alloc_zeroed_user_highpage(movableflags, vma, vaddr)		\
({									\
	struct page *page = alloc_page_vma(				\
		GFP_HIGHUSER | __GFP_ZERO | movableflags, vma, vaddr);	\
	if (page)							\
 		flush_dcache_page(page);				\
	page;								\
})

#define __HAVE_ARCH_ALLOC_ZEROED_USER_HIGHPAGE

#define virt_addr_valid(kaddr)	pfn_valid(__pa(kaddr) >> PAGE_SHIFT)

#ifdef CONFIG_VIRTUAL_MEM_MAP
extern int ia64_pfn_valid (unsigned long pfn);
#else
# define ia64_pfn_valid(pfn) 1
#endif

#ifdef CONFIG_VIRTUAL_MEM_MAP
extern struct page *vmem_map;
#ifdef CONFIG_DISCONTIGMEM
# define page_to_pfn(page)	((unsigned long) (page - vmem_map))
# define pfn_to_page(pfn)	(vmem_map + (pfn))
# define __pfn_to_phys(pfn)	PFN_PHYS(pfn)
#else
# include <asm-generic/memory_model.h>
#endif
#else
# include <asm-generic/memory_model.h>
#endif

#ifdef CONFIG_FLATMEM
# define pfn_valid(pfn)		(((pfn) < max_mapnr) && ia64_pfn_valid(pfn))
#elif defined(CONFIG_DISCONTIGMEM)
extern unsigned long min_low_pfn;
extern unsigned long max_low_pfn;
# define pfn_valid(pfn)		(((pfn) >= min_low_pfn) && ((pfn) < max_low_pfn) && ia64_pfn_valid(pfn))
#endif

#define page_to_phys(page)	(page_to_pfn(page) << PAGE_SHIFT)
#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)
#define pfn_to_kaddr(pfn)	__va((pfn) << PAGE_SHIFT)

typedef union ia64_va {
	struct {
		unsigned long off : 61;		/* intra-region offset */
		unsigned long reg :  3;		/* region number */
	} f;
	unsigned long l;
	void *p;
} ia64_va;

/*
 * Note: These macros depend on the fact that PAGE_OFFSET has all
 * region bits set to 1 and all other bits set to zero.  They are
 * expressed in this way to ensure they result in a single "dep"
 * instruction.
 */
#define __pa(x)		({ia64_va _v; _v.l = (long) (x); _v.f.reg = 0; _v.l;})
#define __va(x)		({ia64_va _v; _v.l = (long) (x); _v.f.reg = -1; _v.p;})

#define REGION_NUMBER(x)	({ia64_va _v; _v.l = (long) (x); _v.f.reg;})
#define REGION_OFFSET(x)	({ia64_va _v; _v.l = (long) (x); _v.f.off;})

#ifdef CONFIG_HUGETLB_PAGE
# define htlbpage_to_page(x)	(((unsigned long) REGION_NUMBER(x) << 61)			\
				 | (REGION_OFFSET(x) >> (HPAGE_SHIFT-PAGE_SHIFT)))
# define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)
extern unsigned int hpage_shift;
#endif

static __inline__ int
get_order (unsigned long size)
{
	long double d = size - 1;
	long order;

	order = ia64_getf_exp(d);
	order = order - PAGE_SHIFT - 0xffff + 1;
	if (order < 0)
		order = 0;
	return order;
}

#endif /* !__ASSEMBLY__ */

#ifdef STRICT_MM_TYPECHECKS
  /*
   * These are used to make use of C type-checking..
   */
  typedef struct { unsigned long pte; } pte_t;
  typedef struct { unsigned long pmd; } pmd_t;
#if CONFIG_PGTABLE_LEVELS == 4
  typedef struct { unsigned long pud; } pud_t;
#endif
  typedef struct { unsigned long pgd; } pgd_t;
  typedef struct { unsigned long pgprot; } pgprot_t;
  typedef struct page *pgtable_t;

# define pte_val(x)	((x).pte)
# define pmd_val(x)	((x).pmd)
#if CONFIG_PGTABLE_LEVELS == 4
# define pud_val(x)	((x).pud)
#endif
# define pgd_val(x)	((x).pgd)
# define pgprot_val(x)	((x).pgprot)

# define __pte(x)	((pte_t) { (x) } )
# define __pmd(x)	((pmd_t) { (x) } )
# define __pgprot(x)	((pgprot_t) { (x) } )

#else /* !STRICT_MM_TYPECHECKS */
  /*
   * .. while these make it easier on the compiler
   */
# ifndef __ASSEMBLY__
    typedef unsigned long pte_t;
    typedef unsigned long pmd_t;
    typedef unsigned long pgd_t;
    typedef unsigned long pgprot_t;
    typedef struct page *pgtable_t;
# endif

# define pte_val(x)	(x)
# define pmd_val(x)	(x)
# define pgd_val(x)	(x)
# define pgprot_val(x)	(x)

# define __pte(x)	(x)
# define __pgd(x)	(x)
# define __pgprot(x)	(x)
#endif /* !STRICT_MM_TYPECHECKS */

#define PAGE_OFFSET			RGN_BASE(RGN_KERNEL)

#define VM_DATA_DEFAULT_FLAGS		(VM_READ | VM_WRITE |					\
					 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC |		\
					 (((current->personality & READ_IMPLIES_EXEC) != 0)	\
					  ? VM_EXEC : 0))

#define GATE_ADDR		RGN_BASE(RGN_GATE)

/*
 * 0xa000000000000000+2*PERCPU_PAGE_SIZE
 * - 0xa000000000000000+3*PERCPU_PAGE_SIZE remain unmapped (guard page)
 */
#define KERNEL_START		 (GATE_ADDR+__IA64_UL_CONST(0x100000000))
#define PERCPU_ADDR		(-PERCPU_PAGE_SIZE)
#define LOAD_OFFSET		(KERNEL_START - KERNEL_TR_PAGE_SIZE)

#define __HAVE_ARCH_GATE_AREA	1

#endif /* _ASM_IA64_PAGE_H */
