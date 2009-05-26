/*
 * VM ops
 *
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 * Changes for MMU support:
 *    Copyright (C) 2007 Xilinx, Inc.  All rights reserved.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_PAGE_H
#define _ASM_MICROBLAZE_PAGE_H

#include <linux/pfn.h>
#include <asm/setup.h>
#include <linux/const.h>

#ifdef __KERNEL__

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT	(12)
#define PAGE_SIZE	(_AC(1, UL) << PAGE_SHIFT)
#define PAGE_MASK	(~(PAGE_SIZE-1))

#ifndef __ASSEMBLY__

#define PAGE_UP(addr)	(((addr)+((PAGE_SIZE)-1))&(~((PAGE_SIZE)-1)))
#define PAGE_DOWN(addr)	((addr)&(~((PAGE_SIZE)-1)))

/* align addr on a size boundary - adjust address up/down if needed */
#define _ALIGN_UP(addr, size)	(((addr)+((size)-1))&(~((size)-1)))
#define _ALIGN_DOWN(addr, size)	((addr)&(~((size)-1)))

/* align addr on a size boundary - adjust address up if needed */
#define _ALIGN(addr, size)	_ALIGN_UP(addr, size)

#ifndef CONFIG_MMU
/*
 * PAGE_OFFSET -- the first address of the first page of memory. When not
 * using MMU this corresponds to the first free page in physical memory (aligned
 * on a page boundary).
 */
extern unsigned int __page_offset;
#define PAGE_OFFSET __page_offset

#else /* CONFIG_MMU */

/*
 * PAGE_OFFSET -- the first address of the first page of memory. With MMU
 * it is set to the kernel start address (aligned on a page boundary).
 *
 * CONFIG_KERNEL_START is defined in arch/microblaze/config.in and used
 * in arch/microblaze/Makefile.
 */
#define PAGE_OFFSET	CONFIG_KERNEL_START

/*
 * MAP_NR -- given an address, calculate the index of the page struct which
 * points to the address's page.
 */
#define MAP_NR(addr) (((unsigned long)(addr) - PAGE_OFFSET) >> PAGE_SHIFT)

/*
 * The basic type of a PTE - 32 bit physical addressing.
 */
typedef unsigned long pte_basic_t;
#define PTE_SHIFT	(PAGE_SHIFT - 2)	/* 1024 ptes per page */
#define PTE_FMT		"%.8lx"

#endif /* CONFIG_MMU */

#  ifndef CONFIG_MMU
#  define copy_page(to, from)			memcpy((to), (from), PAGE_SIZE)
#  define get_user_page(vaddr)			__get_free_page(GFP_KERNEL)
#  define free_user_page(page, addr)		free_page(addr)
#  else /* CONFIG_MMU */
extern void copy_page(void *to, void *from);
#  endif /* CONFIG_MMU */

# define clear_page(pgaddr)			memset((pgaddr), 0, PAGE_SIZE)

# define clear_user_page(pgaddr, vaddr, page)	memset((pgaddr), 0, PAGE_SIZE)
# define copy_user_page(vto, vfrom, vaddr, topg) \
			memcpy((vto), (vfrom), PAGE_SIZE)

/*
 * These are used to make use of C type-checking..
 */
typedef struct page *pgtable_t;
typedef struct { unsigned long	pte; }		pte_t;
typedef struct { unsigned long	pgprot; }	pgprot_t;
/* FIXME this can depend on linux kernel version */
#   ifdef CONFIG_MMU
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
#   else /* CONFIG_MMU */
typedef struct { unsigned long	ste[64]; }	pmd_t;
typedef struct { pmd_t		pue[1]; }	pud_t;
typedef struct { pud_t		pge[1]; }	pgd_t;
#   endif /* CONFIG_MMU */

# define pte_val(x)	((x).pte)
# define pgprot_val(x)	((x).pgprot)

#   ifdef CONFIG_MMU
#   define pmd_val(x)      ((x).pmd)
#   define pgd_val(x)      ((x).pgd)
#   else  /* CONFIG_MMU */
#   define pmd_val(x)	((x).ste[0])
#   define pud_val(x)	((x).pue[0])
#   define pgd_val(x)	((x).pge[0])
#   endif  /* CONFIG_MMU */

# define __pte(x)	((pte_t) { (x) })
# define __pmd(x)	((pmd_t) { (x) })
# define __pgd(x)	((pgd_t) { (x) })
# define __pgprot(x)	((pgprot_t) { (x) })

/**
 * Conversions for virtual address, physical address, pfn, and struct
 * page are defined in the following files.
 *
 * virt -+
 *	 | asm-microblaze/page.h
 * phys -+
 *	 | linux/pfn.h
 *  pfn -+
 *	 | asm-generic/memory_model.h
 * page -+
 *
 */

extern unsigned long max_low_pfn;
extern unsigned long min_low_pfn;
extern unsigned long max_pfn;

extern unsigned long memory_start;
extern unsigned long memory_end;
extern unsigned long memory_size;

extern int page_is_ram(unsigned long pfn);

# define phys_to_pfn(phys)	(PFN_DOWN(phys))
# define pfn_to_phys(pfn)	(PFN_PHYS(pfn))

# define virt_to_pfn(vaddr)	(phys_to_pfn((__pa(vaddr))))
# define pfn_to_virt(pfn)	__va(pfn_to_phys((pfn)))

#  ifdef CONFIG_MMU
#  define virt_to_page(kaddr) 	(mem_map +  MAP_NR(kaddr))
#  else /* CONFIG_MMU */
#  define virt_to_page(vaddr)	(pfn_to_page(virt_to_pfn(vaddr)))
#  define page_to_virt(page)	(pfn_to_virt(page_to_pfn(page)))
#  define page_to_phys(page)	(pfn_to_phys(page_to_pfn(page)))
#  define page_to_bus(page)	(page_to_phys(page))
#  define phys_to_page(paddr)	(pfn_to_page(phys_to_pfn(paddr)))
#  endif /* CONFIG_MMU */

#  ifndef CONFIG_MMU
#  define pfn_valid(pfn)	((pfn) >= min_low_pfn && (pfn) <= max_mapnr)
#  define ARCH_PFN_OFFSET	(PAGE_OFFSET >> PAGE_SHIFT)
#  else /* CONFIG_MMU */
#  define ARCH_PFN_OFFSET	(memory_start >> PAGE_SHIFT)
#  define pfn_valid(pfn)	((pfn) < (max_mapnr + ARCH_PFN_OFFSET))
#  define VALID_PAGE(page) 	((page - mem_map) < max_mapnr)
#  endif /* CONFIG_MMU */

# endif /* __ASSEMBLY__ */

#define	virt_addr_valid(vaddr)	(pfn_valid(virt_to_pfn(vaddr)))


#  ifndef CONFIG_MMU
#  define __pa(vaddr)	((unsigned long) (vaddr))
#  define __va(paddr)	((void *) (paddr))
#  else /* CONFIG_MMU */
#  define __pa(x)	__virt_to_phys((unsigned long)(x))
#  define __va(x)	((void *)__phys_to_virt((unsigned long)(x)))
#  endif /* CONFIG_MMU */


/* Convert between virtual and physical address for MMU. */
/* Handle MicroBlaze processor with virtual memory. */
#ifndef CONFIG_MMU
#define __virt_to_phys(addr)	addr
#define __phys_to_virt(addr)	addr
#define tophys(rd, rs)	addik rd, rs, 0
#define tovirt(rd, rs)	addik rd, rs, 0
#else
#define __virt_to_phys(addr) \
	((addr) + CONFIG_KERNEL_BASE_ADDR - CONFIG_KERNEL_START)
#define __phys_to_virt(addr) \
	((addr) + CONFIG_KERNEL_START - CONFIG_KERNEL_BASE_ADDR)
#define tophys(rd, rs) \
	addik rd, rs, (CONFIG_KERNEL_BASE_ADDR - CONFIG_KERNEL_START)
#define tovirt(rd, rs) \
	addik rd, rs, (CONFIG_KERNEL_START - CONFIG_KERNEL_BASE_ADDR)
#endif /* CONFIG_MMU */

#define TOPHYS(addr)  __virt_to_phys(addr)

#ifdef CONFIG_MMU
#ifdef CONFIG_CONTIGUOUS_PAGE_ALLOC
#define WANT_PAGE_VIRTUAL 1 /* page alloc 2 relies on this */
#endif

#define VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)
#endif /* CONFIG_MMU */

#endif /* __KERNEL__ */

#include <asm-generic/memory_model.h>
#include <asm-generic/page.h>

#endif /* _ASM_MICROBLAZE_PAGE_H */
