/* SPDX-License-Identifier: GPL-2.0 */
/*
 * VM ops
 *
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 * Changes for MMU support:
 *    Copyright (C) 2007 Xilinx, Inc.  All rights reserved.
 */

#ifndef _ASM_MICROBLAZE_PAGE_H
#define _ASM_MICROBLAZE_PAGE_H

#include <linux/pfn.h>
#include <asm/setup.h>
#include <asm/asm-compat.h>
#include <linux/const.h>

#ifdef __KERNEL__

#include <vdso/page.h>

#define LOAD_OFFSET	ASM_CONST((CONFIG_KERNEL_START-CONFIG_KERNEL_BASE_ADDR))

#define PTE_SHIFT	(PAGE_SHIFT - 2)	/* 1024 ptes per page */

#ifndef __ASSEMBLER__

/*
 * PAGE_OFFSET -- the first address of the first page of memory. With MMU
 * it is set to the kernel start address (aligned on a page boundary).
 *
 * CONFIG_KERNEL_START is defined in arch/microblaze/config.in and used
 * in arch/microblaze/Makefile.
 */
#define PAGE_OFFSET	CONFIG_KERNEL_START

/*
 * The basic type of a PTE - 32 bit physical addressing.
 */
typedef unsigned long pte_basic_t;
#define PTE_FMT		"%.8lx"

# define copy_page(to, from)			memcpy((to), (from), PAGE_SIZE)
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
typedef struct { unsigned long pgd; } pgd_t;

# define pte_val(x)	((x).pte)
# define pgprot_val(x)	((x).pgprot)

#   define pgd_val(x)      ((x).pgd)

# define __pte(x)	((pte_t) { (x) })
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
extern unsigned long memory_size;
extern unsigned long lowmem_size;

extern unsigned long kernel_tlb;

extern int page_is_ram(unsigned long pfn);

# define phys_to_pfn(phys)	(PFN_DOWN(phys))
# define pfn_to_phys(pfn)	(PFN_PHYS(pfn))

#  define virt_to_page(kaddr)	(pfn_to_page(__pa(kaddr) >> PAGE_SHIFT))
#  define page_to_virt(page)   __va(page_to_pfn(page) << PAGE_SHIFT)

#  define ARCH_PFN_OFFSET	(memory_start >> PAGE_SHIFT)
# endif /* __ASSEMBLER__ */

/* Convert between virtual and physical address for MMU. */
/* Handle MicroBlaze processor with virtual memory. */
#define __virt_to_phys(addr) \
	((addr) + CONFIG_KERNEL_BASE_ADDR - CONFIG_KERNEL_START)
#define __phys_to_virt(addr) \
	((addr) + CONFIG_KERNEL_START - CONFIG_KERNEL_BASE_ADDR)
#define tophys(rd, rs) \
	addik rd, rs, (CONFIG_KERNEL_BASE_ADDR - CONFIG_KERNEL_START)
#define tovirt(rd, rs) \
	addik rd, rs, (CONFIG_KERNEL_START - CONFIG_KERNEL_BASE_ADDR)

#ifndef __ASSEMBLER__

# define __pa(x)	__virt_to_phys((unsigned long)(x))
# define __va(x)	((void *)__phys_to_virt((unsigned long)(x)))

static inline unsigned long virt_to_pfn(const void *vaddr)
{
	return phys_to_pfn(__pa(vaddr));
}

static inline const void *pfn_to_virt(unsigned long pfn)
{
	return __va(pfn_to_phys((pfn)));
}

#define	virt_addr_valid(vaddr)	(pfn_valid(virt_to_pfn(vaddr)))

#endif /* __ASSEMBLER__ */

#define TOPHYS(addr)  __virt_to_phys(addr)

#endif /* __KERNEL__ */

#include <asm-generic/memory_model.h>
#include <asm-generic/getorder.h>

#endif /* _ASM_MICROBLAZE_PAGE_H */
