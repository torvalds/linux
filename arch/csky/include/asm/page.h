/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_PAGE_H
#define __ASM_CSKY_PAGE_H

#include <asm/setup.h>
#include <asm/cache.h>
#include <linux/const.h>

/*
 * PAGE_SHIFT determines the page size: 4KB
 */
#define PAGE_SHIFT	12
#define PAGE_SIZE	(_AC(1, UL) << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE - 1))
#define THREAD_SIZE	(PAGE_SIZE * 2)
#define THREAD_MASK	(~(THREAD_SIZE - 1))
#define THREAD_SHIFT	(PAGE_SHIFT + 1)


/*
 * For C-SKY "User-space:Kernel-space" is "2GB:2GB" fixed by hardware and there
 * are two segment registers (MSA0 + MSA1) to mapping 512MB + 512MB physical
 * address region. We use them mapping kernel 1GB direct-map address area and
 * for more than 1GB of memory we use highmem.
 */
#define PAGE_OFFSET	0x80000000
#define SSEG_SIZE	0x20000000
#define LOWMEM_LIMIT	(SSEG_SIZE * 2)

#define PHYS_OFFSET_OFFSET (CONFIG_RAM_BASE & (SSEG_SIZE - 1))

#ifndef __ASSEMBLY__

#include <linux/pfn.h>

#define virt_to_pfn(kaddr)      (__pa(kaddr) >> PAGE_SHIFT)
#define pfn_to_virt(pfn)        __va((pfn) << PAGE_SHIFT)

#define virt_addr_valid(kaddr)  ((void *)(kaddr) >= (void *)PAGE_OFFSET && \
			(void *)(kaddr) < high_memory)
#define pfn_valid(pfn)		((pfn) >= ARCH_PFN_OFFSET && ((pfn) - ARCH_PFN_OFFSET) < max_mapnr)

extern void *memset(void *dest, int c, size_t l);
extern void *memcpy(void *to, const void *from, size_t l);

#define clear_page(page)	memset((page), 0, PAGE_SIZE)
#define copy_page(to, from)	memcpy((to), (from), PAGE_SIZE)

#define page_to_phys(page)	(page_to_pfn(page) << PAGE_SHIFT)
#define phys_to_page(paddr)	(pfn_to_page(PFN_DOWN(paddr)))

struct page;

#include <abi/page.h>

struct vm_area_struct;

typedef struct { unsigned long pte_low; } pte_t;
#define pte_val(x)	((x).pte_low)

typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;
typedef struct page *pgtable_t;

#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define ptep_buddy(x)	((pte_t *)((unsigned long)(x) ^ sizeof(pte_t)))

#define __pte(x)	((pte_t) { (x) })
#define __pgd(x)	((pgd_t) { (x) })
#define __pgprot(x)	((pgprot_t) { (x) })

extern unsigned long va_pa_offset;

#define ARCH_PFN_OFFSET	PFN_DOWN(va_pa_offset + PHYS_OFFSET_OFFSET)

#define __pa(x)		 ((unsigned long)(x) - PAGE_OFFSET + va_pa_offset)
#define __va(x) ((void *)((unsigned long)(x) + PAGE_OFFSET - va_pa_offset))

#define __pa_symbol(x)	__pa(RELOC_HIDE((unsigned long)(x), 0))

#define MAP_NR(x)	PFN_DOWN((unsigned long)(x) - PAGE_OFFSET - \
				 PHYS_OFFSET_OFFSET)
#define virt_to_page(x)	(mem_map + MAP_NR(x))

#define pfn_to_kaddr(x)	__va(PFN_PHYS(x))

#include <asm-generic/memory_model.h>
#include <asm-generic/getorder.h>

#endif /* !__ASSEMBLY__ */
#endif /* __ASM_CSKY_PAGE_H */
