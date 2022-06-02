/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _M68K_PAGE_NO_H
#define _M68K_PAGE_NO_H

#ifndef __ASSEMBLY__
 
extern unsigned long memory_start;
extern unsigned long memory_end;

#define clear_page(page)	memset((page), 0, PAGE_SIZE)
#define copy_page(to,from)	memcpy((to), (from), PAGE_SIZE)

#define clear_user_page(page, vaddr, pg)	clear_page(page)
#define copy_user_page(to, from, vaddr, pg)	copy_page(to, from)

#define vma_alloc_zeroed_movable_folio(vma, vaddr) \
	vma_alloc_folio(GFP_HIGHUSER_MOVABLE | __GFP_ZERO, 0, vma, vaddr, false)

#define __pa(vaddr)		((unsigned long)(vaddr))
#define __va(paddr)		((void *)((unsigned long)(paddr)))

static inline unsigned long virt_to_pfn(const void *kaddr)
{
	return __pa(kaddr) >> PAGE_SHIFT;
}

static inline void *pfn_to_virt(unsigned long pfn)
{
	return __va(pfn << PAGE_SHIFT);
}

#define virt_to_page(addr)	(mem_map + (((unsigned long)(addr)-PAGE_OFFSET) >> PAGE_SHIFT))
#define page_to_virt(page)	__va(((((page) - mem_map) << PAGE_SHIFT) + PAGE_OFFSET))

#define	virt_addr_valid(kaddr)	(((unsigned long)(kaddr) >= PAGE_OFFSET) && \
				((unsigned long)(kaddr) < memory_end))

#define ARCH_PFN_OFFSET PHYS_PFN(PAGE_OFFSET_RAW)

#endif /* __ASSEMBLY__ */

#endif /* _M68K_PAGE_NO_H */
