#ifndef _METAG_PAGE_H
#define _METAG_PAGE_H

#include <linux/const.h>

#include <asm/metag_mem.h>

/* PAGE_SHIFT determines the page size */
#if defined(CONFIG_PAGE_SIZE_4K)
#define PAGE_SHIFT	12
#elif defined(CONFIG_PAGE_SIZE_8K)
#define PAGE_SHIFT	13
#elif defined(CONFIG_PAGE_SIZE_16K)
#define PAGE_SHIFT	14
#endif

#define PAGE_SIZE	(_AC(1, UL) << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#if defined(CONFIG_HUGETLB_PAGE_SIZE_8K)
# define HPAGE_SHIFT	13
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_16K)
# define HPAGE_SHIFT	14
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_32K)
# define HPAGE_SHIFT	15
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_64K)
# define HPAGE_SHIFT	16
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_128K)
# define HPAGE_SHIFT	17
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_256K)
# define HPAGE_SHIFT	18
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_512K)
# define HPAGE_SHIFT	19
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_1M)
# define HPAGE_SHIFT	20
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_2M)
# define HPAGE_SHIFT	21
#elif defined(CONFIG_HUGETLB_PAGE_SIZE_4M)
# define HPAGE_SHIFT	22
#endif

#ifdef CONFIG_HUGETLB_PAGE
# define HPAGE_SIZE		(1UL << HPAGE_SHIFT)
# define HPAGE_MASK		(~(HPAGE_SIZE-1))
# define HUGETLB_PAGE_ORDER	(HPAGE_SHIFT-PAGE_SHIFT)
/*
 * We define our own hugetlb_get_unmapped_area so we don't corrupt 2nd level
 * page tables with normal pages in them.
 */
# define HUGEPT_SHIFT		(22)
# define HUGEPT_ALIGN		(1 << HUGEPT_SHIFT)
# define HUGEPT_MASK		(HUGEPT_ALIGN - 1)
# define ALIGN_HUGEPT(x)	ALIGN(x, HUGEPT_ALIGN)
# define HAVE_ARCH_HUGETLB_UNMAPPED_AREA
#endif

#ifndef __ASSEMBLY__

/* On the Meta, we would like to know if the address (heap) we have is
 * in local or global space.
 */
#define is_global_space(addr)	((addr) > 0x7fffffff)
#define is_local_space(addr)	(!is_global_space(addr))

extern void clear_page(void *to);
extern void copy_page(void *to, void *from);

#define clear_user_page(page, vaddr, pg)        clear_page(page)
#define copy_user_page(to, from, vaddr, pg)     copy_page(to, from)

/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;
typedef struct page *pgtable_t;

#define pte_val(x)	((x).pte)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) })
#define __pgd(x)	((pgd_t) { (x) })
#define __pgprot(x)	((pgprot_t) { (x) })

/* The kernel must now ALWAYS live at either 0xC0000000 or 0x40000000 - that
 * being either global or local space.
 */
#define PAGE_OFFSET		(CONFIG_PAGE_OFFSET)

#if PAGE_OFFSET >= LINGLOBAL_BASE
#define META_MEMORY_BASE  LINGLOBAL_BASE
#define META_MEMORY_LIMIT LINGLOBAL_LIMIT
#else
#define META_MEMORY_BASE  LINLOCAL_BASE
#define META_MEMORY_LIMIT LINLOCAL_LIMIT
#endif

/* Offset between physical and virtual mapping of kernel memory. */
extern unsigned int meta_memoffset;

#define __pa(x) ((unsigned long)(((unsigned long)(x)) - meta_memoffset))
#define __va(x) ((void *)((unsigned long)(((unsigned long)(x)) + meta_memoffset)))

extern unsigned long pfn_base;
#define ARCH_PFN_OFFSET         (pfn_base)
#define virt_to_page(kaddr)     pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)
#define page_to_virt(page)      __va(page_to_pfn(page) << PAGE_SHIFT)
#define virt_addr_valid(kaddr)  pfn_valid(__pa(kaddr) >> PAGE_SHIFT)
#define page_to_phys(page)      (page_to_pfn(page) << PAGE_SHIFT)
#ifdef CONFIG_FLATMEM
extern unsigned long max_pfn;
extern unsigned long min_low_pfn;
#define pfn_valid(pfn)		((pfn) >= min_low_pfn && (pfn) < max_pfn)
#endif

#define pfn_to_kaddr(pfn)	__va((pfn) << PAGE_SHIFT)

#define VM_DATA_DEFAULT_FLAGS   (VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#include <asm-generic/memory_model.h>
#include <asm-generic/getorder.h>

#endif /* __ASSMEBLY__ */

#endif /* _METAG_PAGE_H */
