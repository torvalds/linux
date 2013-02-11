/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARC_PAGE_H
#define __ASM_ARC_PAGE_H

#include <uapi/asm/page.h>


#ifndef __ASSEMBLY__

#define get_user_page(vaddr)		__get_free_page(GFP_KERNEL)
#define free_user_page(page, addr)	free_page(addr)

/* TBD: for now don't worry about VIPT D$ aliasing */
#define clear_page(paddr)		memset((paddr), 0, PAGE_SIZE)
#define copy_page(to, from)		memcpy((to), (from), PAGE_SIZE)

#define clear_user_page(addr, vaddr, pg)	clear_page(addr)
#define copy_user_page(vto, vfrom, vaddr, pg)	copy_page(vto, vfrom)

#undef STRICT_MM_TYPECHECKS

#ifdef STRICT_MM_TYPECHECKS
/*
 * These are used to make use of C type-checking..
 */
typedef struct {
	unsigned long pte;
} pte_t;
typedef struct {
	unsigned long pgd;
} pgd_t;
typedef struct {
	unsigned long pgprot;
} pgprot_t;
typedef unsigned long pgtable_t;

#define pte_val(x)      ((x).pte)
#define pgd_val(x)      ((x).pgd)
#define pgprot_val(x)   ((x).pgprot)

#define __pte(x)        ((pte_t) { (x) })
#define __pgd(x)        ((pgd_t) { (x) })
#define __pgprot(x)     ((pgprot_t) { (x) })

#else /* !STRICT_MM_TYPECHECKS */

typedef unsigned long pte_t;
typedef unsigned long pgd_t;
typedef unsigned long pgprot_t;
typedef unsigned long pgtable_t;

#define pte_val(x)	(x)
#define pgd_val(x)	(x)
#define pgprot_val(x)	(x)
#define __pte(x)	(x)
#define __pgprot(x)	(x)

#endif

#define ARCH_PFN_OFFSET     (CONFIG_LINUX_LINK_BASE >> PAGE_SHIFT)

#define pfn_valid(pfn)      (((pfn) - ARCH_PFN_OFFSET) < max_mapnr)

/*
 * __pa, __va, virt_to_page (ALERT: deprecated, don't use them)
 *
 * These macros have historically been misnamed
 * virt here means link-address/program-address as embedded in object code.
 * So if kernel img is linked at 0x8000_0000 onwards, 0x8010_0000 will be
 * 128th page, and virt_to_page( ) will return the struct page corresp to it.
 * mem_map[ ] is an array of struct page for each page frame in the system
 *
 * Independent of where linux is linked at, link-addr = physical address
 * So the old macro  __pa = vaddr + PAGE_OFFSET - CONFIG_LINUX_LINK_BASE
 * would have been wrong in case kernel is not at 0x8zs
 */
#define __pa(vaddr)  ((unsigned long)vaddr)
#define __va(paddr)  ((void *)((unsigned long)(paddr)))

#define virt_to_page(kaddr)	\
	(mem_map + ((__pa(kaddr) - CONFIG_LINUX_LINK_BASE) >> PAGE_SHIFT))

#define virt_addr_valid(kaddr)  pfn_valid(__pa(kaddr) >> PAGE_SHIFT)

/* Default Permissions for page, used in mmap.c */
#ifdef CONFIG_ARC_STACK_NONEXEC
#define VM_DATA_DEFAULT_FLAGS   (VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE)
#else
#define VM_DATA_DEFAULT_FLAGS   (VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)
#endif

#define WANT_PAGE_VIRTUAL   1

#include <asm-generic/memory_model.h>   /* page_to_pfn, pfn_to_page */
#include <asm-generic/getorder.h>

#endif /* !__ASSEMBLY__ */

#endif
