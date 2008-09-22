#ifndef _BLACKFIN_PAGE_H
#define _BLACKFIN_PAGE_H

/* PAGE_SHIFT determines the page size */

#define PAGE_SHIFT	12
#ifdef __ASSEMBLY__
#define PAGE_SIZE	(1 << PAGE_SHIFT)
#else
#define PAGE_SIZE	(1UL << PAGE_SHIFT)
#endif
#define PAGE_MASK	(~(PAGE_SIZE-1))

#include <asm/setup.h>

#ifndef __ASSEMBLY__

#define get_user_page(vaddr)		__get_free_page(GFP_KERNEL)
#define free_user_page(page, addr)	free_page(addr)

#define clear_page(page)	memset((page), 0, PAGE_SIZE)
#define copy_page(to,from)	memcpy((to), (from), PAGE_SIZE)

#define clear_user_page(page, vaddr,pg)	clear_page(page)
#define copy_user_page(to, from, vaddr,pg)	copy_page(to, from)

/*
 * These are used to make use of C type-checking..
 */
typedef struct {
	unsigned long pte;
} pte_t;
typedef struct {
	unsigned long pmd[16];
} pmd_t;
typedef struct {
	unsigned long pgd;
} pgd_t;
typedef struct {
	unsigned long pgprot;
} pgprot_t;
typedef struct page *pgtable_t;

#define pte_val(x)	((x).pte)
#define pmd_val(x)	((&x)->pmd[0])
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) } )
#define __pmd(x)	((pmd_t) { (x) } )
#define __pgd(x)	((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

extern unsigned long memory_start;
extern unsigned long memory_end;

#endif				/* !__ASSEMBLY__ */

#include <asm/page_offset.h>
#include <asm/io.h>

#define PAGE_OFFSET		(PAGE_OFFSET_RAW)

#ifndef __ASSEMBLY__

#define __pa(vaddr)		virt_to_phys((void *)(vaddr))
#define __va(paddr)		phys_to_virt((unsigned long)(paddr))

#define MAP_NR(addr)		(((unsigned long)(addr)-PAGE_OFFSET) >> PAGE_SHIFT)

#define virt_to_pfn(kaddr)	(__pa(kaddr) >> PAGE_SHIFT)
#define pfn_to_virt(pfn)	__va((pfn) << PAGE_SHIFT)
#define virt_to_page(addr)	(mem_map + (((unsigned long)(addr)-PAGE_OFFSET) >> PAGE_SHIFT))
#define page_to_virt(page)	((((page) - mem_map) << PAGE_SHIFT) + PAGE_OFFSET)
#define VALID_PAGE(page)	((page - mem_map) < max_mapnr)

#define pfn_to_page(pfn)	virt_to_page(pfn_to_virt(pfn))
#define page_to_pfn(page)	virt_to_pfn(page_to_virt(page))
#define pfn_valid(pfn)	        ((pfn) < max_mapnr)

#define	virt_addr_valid(kaddr)	(((void *)(kaddr) >= (void *)PAGE_OFFSET) && \
				((void *)(kaddr) < (void *)memory_end))

#include <asm-generic/page.h>

#endif				/* __ASSEMBLY__ */

#endif				/* _BLACKFIN_PAGE_H */
