/*
 * Copyright (C) 2011 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 *
 * MMU support based on asm/page.h from mips which is:
 *
 * Copyright (C) 1994 - 1999, 2000, 03 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS2_PAGE_H
#define _ASM_NIOS2_PAGE_H

#include <linux/pfn.h>
#include <linux/const.h>

/*
 * PAGE_SHIFT determines the page size
 */
#define PAGE_SHIFT	12
#define PAGE_SIZE	(_AC(1, UL) << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE - 1))

/*
 * PAGE_OFFSET -- the first address of the first page of memory.
 */
#define PAGE_OFFSET	\
	(CONFIG_NIOS2_MEM_BASE + CONFIG_NIOS2_KERNEL_REGION_BASE)

#ifndef __ASSEMBLY__

/*
 * This gives the physical RAM offset.
 */
#define PHYS_OFFSET		CONFIG_NIOS2_MEM_BASE

/*
 * It's normally defined only for FLATMEM config but it's
 * used in our early mem init code for all memory models.
 * So always define it.
 */
#define ARCH_PFN_OFFSET		PFN_UP(PHYS_OFFSET)

#define clear_page(page)	memset((page), 0, PAGE_SIZE)
#define copy_page(to, from)	memcpy((to), (from), PAGE_SIZE)

struct page;

extern void clear_user_page(void *addr, unsigned long vaddr, struct page *page);
extern void copy_user_page(void *vto, void *vfrom, unsigned long vaddr,
				struct page *to);

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

extern unsigned long memory_start;
extern unsigned long memory_end;
extern unsigned long memory_size;

extern struct page *mem_map;

# define __pa(x)		\
	((unsigned long)(x) - PAGE_OFFSET + PHYS_OFFSET)
# define __va(x)		\
	((void *)((unsigned long)(x) + PAGE_OFFSET - PHYS_OFFSET))

#define page_to_virt(page)	\
	((void *)(((page) - mem_map) << PAGE_SHIFT) + PAGE_OFFSET)

# define pfn_to_kaddr(pfn)	__va((pfn) << PAGE_SHIFT)

static inline bool pfn_valid(unsigned long pfn)
{
	/* avoid <linux/mm.h> include hell */
	extern unsigned long max_mapnr;
	unsigned long pfn_offset = ARCH_PFN_OFFSET;

	return pfn >= pfn_offset && pfn < max_mapnr;
}

# define virt_to_page(vaddr)	pfn_to_page(PFN_DOWN(virt_to_phys(vaddr)))
# define virt_addr_valid(vaddr)	pfn_valid(PFN_DOWN(virt_to_phys(vaddr)))

# define VM_DATA_DEFAULT_FLAGS	VM_DATA_FLAGS_NON_EXEC

#include <asm-generic/memory_model.h>

#include <asm-generic/getorder.h>

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_NIOS2_PAGE_H */
