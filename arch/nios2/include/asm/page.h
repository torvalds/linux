/*
 * Copyright (C) 2011 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 *
 * MMU support based on asm/page.h from mips which is:
 *
 * Copyright (C) 1994 - 1999, 2000, 03 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 *
 * NOMMU support based on asm/page.h from m68knommu.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS2_PAGE_H
#define _ASM_NIOS2_PAGE_H

#include <linux/pfn.h>

/*
 * PAGE_SHIFT determines the page size
 */
#define PAGE_SHIFT	12
#define PAGE_SIZE	4096
#define PAGE_MASK	(~(PAGE_SIZE - 1))

/*
 * PAGE_OFFSET -- the first address of the first page of memory.
 */
#define PAGE_OFFSET	(CONFIG_MEM_BASE + CONFIG_KERNEL_REGION_BASE)

#ifndef __ASSEMBLY__

/*
 * This gives the physical RAM offset.
 */
#define PHYS_OFFSET		CONFIG_MEM_BASE

/*
 * It's normally defined only for FLATMEM config but it's
 * used in our early mem init code for all memory models.
 * So always define it.
 */
#define ARCH_PFN_OFFSET		PFN_UP(PHYS_OFFSET)

#ifndef CONFIG_MMU
# define get_user_page(vaddr)		__get_free_page(GFP_KERNEL)
# define free_user_page(page, addr)	free_page(addr)
#endif /* CONFIG_MMU */

#define clear_page(page)	memset((page), 0, PAGE_SIZE)
#define copy_page(to, from)	memcpy((to), (from), PAGE_SIZE)

#ifdef CONFIG_MMU
struct page;

extern void clear_user_page(void *addr, unsigned long vaddr, struct page *page);
extern void copy_user_page(void *vto, void *vfrom, unsigned long vaddr,
				struct page *to);
#else
# define clear_user_page(page, vaddr, pg)	clear_page(page)
# define copy_user_page(to, from, vaddr, pg)	copy_page(to, from)
#endif /* CONFIG_MMU */

extern unsigned long shm_align_mask;

/*
 * These are used to make use of C type-checking.
 */
typedef struct page *pgtable_t;
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)	((x).pte)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) })
#define __pgd(x)	((pgd_t) { (x) })
#define __pgprot(x)	((pgprot_t) { (x) })

#ifndef CONFIG_MMU
typedef struct { unsigned long pmd[16]; } pmd_t;

# define pmd_val(x)	((&x)->pmd[0])
# define __pmd(x)	((pmd_t) { (x) })
#endif /* CONFIG_MMU */

extern unsigned long memory_start;
extern unsigned long memory_end;
extern unsigned long memory_size;

extern struct page *mem_map;

#endif /* !__ASSEMBLY__ */

#ifdef CONFIG_MMU
# define __pa(x)		\
	((unsigned long)(x) - PAGE_OFFSET + PHYS_OFFSET)
# define __va(x)		\
	((void *)((unsigned long)(x) + PAGE_OFFSET - PHYS_OFFSET))
#else
# define __pa(x)		((unsigned long)(x))
# define __va(x)		((void *)(x))
#endif /* CONFIG_MMU */

#define page_to_virt(page)	\
	((((page) - mem_map) << PAGE_SHIFT) + PAGE_OFFSET)

#ifdef CONFIG_MMU
# define pfn_to_kaddr(pfn)	__va((pfn) << PAGE_SHIFT)
# define pfn_valid(pfn)		((pfn) >= ARCH_PFN_OFFSET &&	\
					(pfn) < (max_mapnr + ARCH_PFN_OFFSET))

# define virt_to_page(vaddr)	pfn_to_page(PFN_DOWN(virt_to_phys(vaddr)))
# define virt_addr_valid(vaddr)	pfn_valid(PFN_DOWN(virt_to_phys(vaddr)))
#else /* CONFIG_MMU */
# define pfn_valid(pfn)		((pfn) < max_mapnr)

# define virt_to_page(vaddr)	\
	((void *) vaddr < (void *) memory_end ? mem_map +	\
		(((unsigned long)(vaddr) - PAGE_OFFSET) >> PAGE_SHIFT) : 0UL)

# define virt_to_pfn(kaddr)	(__pa(kaddr) >> PAGE_SHIFT)
# define pfn_to_virt(pfn)	__va((pfn) << PAGE_SHIFT)

# define pfn_to_page(pfn)	virt_to_page(pfn_to_virt(pfn))
# define page_to_pfn(page)	virt_to_pfn(page_to_virt(page))

# define virt_addr_valid(kaddr)	(((void *)(kaddr) >= (void *)PAGE_OFFSET) && \
				 ((void *)(kaddr) < (void *)memory_end))
#endif /* CONFIG_MMU */

#ifdef CONFIG_MMU
# define VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

# define UNCAC_ADDR(addr)	\
	((void *)((unsigned)(addr) | CONFIG_IO_REGION_BASE))
# define CAC_ADDR(addr)		\
	((void *)(((unsigned)(addr) & ~CONFIG_IO_REGION_BASE) |		\
		CONFIG_KERNEL_REGION_BASE))

#include <asm-generic/memory_model.h>

#endif /* CONFIG_MMU */

#include <asm-generic/getorder.h>

#endif /* _ASM_NIOS2_PAGE_H */
