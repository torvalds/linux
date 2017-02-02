#ifndef _SPARC64_PAGE_H
#define _SPARC64_PAGE_H

#include <linux/const.h>

#define PAGE_SHIFT   13

#define PAGE_SIZE    (_AC(1,UL) << PAGE_SHIFT)
#define PAGE_MASK    (~(PAGE_SIZE-1))

/* Flushing for D-cache alias handling is only needed if
 * the page size is smaller than 16K.
 */
#if PAGE_SHIFT < 14
#define DCACHE_ALIASING_POSSIBLE
#endif

#define HPAGE_SHIFT		23
#define REAL_HPAGE_SHIFT	22
#define HPAGE_256MB_SHIFT	28
#define REAL_HPAGE_SIZE		(_AC(1,UL) << REAL_HPAGE_SHIFT)

#if defined(CONFIG_HUGETLB_PAGE) || defined(CONFIG_TRANSPARENT_HUGEPAGE)
#define HPAGE_SIZE		(_AC(1,UL) << HPAGE_SHIFT)
#define HPAGE_MASK		(~(HPAGE_SIZE - 1UL))
#define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT - PAGE_SHIFT)
#define HAVE_ARCH_HUGETLB_UNMAPPED_AREA
#define REAL_HPAGE_PER_HPAGE	(_AC(1,UL) << (HPAGE_SHIFT - REAL_HPAGE_SHIFT))
#define HUGE_MAX_HSTATE		2
#endif

#ifndef __ASSEMBLY__

#if defined(CONFIG_HUGETLB_PAGE) || defined(CONFIG_TRANSPARENT_HUGEPAGE)
struct pt_regs;
void hugetlb_setup(struct pt_regs *regs);
#endif

#define WANT_PAGE_VIRTUAL

void _clear_page(void *page);
#define clear_page(X)	_clear_page((void *)(X))
struct page;
void clear_user_page(void *addr, unsigned long vaddr, struct page *page);
#define copy_page(X,Y)	memcpy((void *)(X), (void *)(Y), PAGE_SIZE)
void copy_user_page(void *to, void *from, unsigned long vaddr, struct page *topage);

/* Unlike sparc32, sparc64's parameter passing API is more
 * sane in that structures which as small enough are passed
 * in registers instead of on the stack.  Thus, setting
 * STRICT_MM_TYPECHECKS does not generate worse code so
 * let's enable it to get the type checking.
 */

#define STRICT_MM_TYPECHECKS

#ifdef STRICT_MM_TYPECHECKS
/* These are used to make use of C type-checking.. */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long iopte; } iopte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pud; } pud_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)	((x).pte)
#define iopte_val(x)	((x).iopte)
#define pmd_val(x)      ((x).pmd)
#define pud_val(x)      ((x).pud)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) } )
#define __iopte(x)	((iopte_t) { (x) } )
#define __pmd(x)        ((pmd_t) { (x) } )
#define __pud(x)        ((pud_t) { (x) } )
#define __pgd(x)	((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

#else
/* .. while these make it easier on the compiler */
typedef unsigned long pte_t;
typedef unsigned long iopte_t;
typedef unsigned long pmd_t;
typedef unsigned long pud_t;
typedef unsigned long pgd_t;
typedef unsigned long pgprot_t;

#define pte_val(x)	(x)
#define iopte_val(x)	(x)
#define pmd_val(x)      (x)
#define pud_val(x)      (x)
#define pgd_val(x)	(x)
#define pgprot_val(x)	(x)

#define __pte(x)	(x)
#define __iopte(x)	(x)
#define __pmd(x)        (x)
#define __pud(x)        (x)
#define __pgd(x)	(x)
#define __pgprot(x)	(x)

#endif /* (STRICT_MM_TYPECHECKS) */

typedef pte_t *pgtable_t;

extern unsigned long sparc64_va_hole_top;
extern unsigned long sparc64_va_hole_bottom;

/* The next two defines specify the actual exclusion region we
 * enforce, wherein we use a 4GB red zone on each side of the VA hole.
 */
#define VA_EXCLUDE_START (sparc64_va_hole_bottom - (1UL << 32UL))
#define VA_EXCLUDE_END   (sparc64_va_hole_top + (1UL << 32UL))

#define TASK_UNMAPPED_BASE	(test_thread_flag(TIF_32BIT) ? \
				 _AC(0x0000000070000000,UL) : \
				 VA_EXCLUDE_END)

#include <asm-generic/memory_model.h>

extern unsigned long PAGE_OFFSET;

#endif /* !(__ASSEMBLY__) */

/* The maximum number of physical memory address bits we support.  The
 * largest value we can support is whatever "KPGD_SHIFT + KPTE_BITS"
 * evaluates to.
 */
#define MAX_PHYS_ADDRESS_BITS	53

#define ILOG2_4MB		22
#define ILOG2_256MB		28

#ifndef __ASSEMBLY__

#define __pa(x)			((unsigned long)(x) - PAGE_OFFSET)
#define __va(x)			((void *)((unsigned long) (x) + PAGE_OFFSET))

#define pfn_to_kaddr(pfn)	__va((pfn) << PAGE_SHIFT)

#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr)>>PAGE_SHIFT)

#define virt_addr_valid(kaddr)	pfn_valid(__pa(kaddr) >> PAGE_SHIFT)

#define virt_to_phys __pa
#define phys_to_virt __va

#endif /* !(__ASSEMBLY__) */

#define VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#include <asm-generic/getorder.h>

#endif /* _SPARC64_PAGE_H */
