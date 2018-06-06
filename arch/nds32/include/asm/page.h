/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (C) 2005-2017 Andes Technology Corporation
 */

#ifndef _ASMNDS32_PAGE_H
#define _ASMNDS32_PAGE_H

#ifdef CONFIG_ANDES_PAGE_SIZE_4KB
#define PAGE_SHIFT      12
#endif
#ifdef CONFIG_ANDES_PAGE_SIZE_8KB
#define PAGE_SHIFT      13
#endif
#include <linux/const.h>
#define PAGE_SIZE       (_AC(1,UL) << PAGE_SHIFT)
#define PAGE_MASK       (~(PAGE_SIZE-1))

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

struct page;
struct vm_area_struct;
#ifdef CONFIG_CPU_CACHE_ALIASING
extern void copy_user_highpage(struct page *to, struct page *from,
			       unsigned long vaddr, struct vm_area_struct *vma);
extern void clear_user_highpage(struct page *page, unsigned long vaddr);

void copy_user_page(void *vto, void *vfrom, unsigned long vaddr,
		    struct page *to);
void clear_user_page(void *addr, unsigned long vaddr, struct page *page);
#define __HAVE_ARCH_COPY_USER_HIGHPAGE
#define clear_user_highpage	clear_user_highpage
#else
#define clear_user_page(page, vaddr, pg)        clear_page(page)
#define copy_user_page(to, from, vaddr, pg)     copy_page(to, from)
#endif

void clear_page(void *page);
void copy_page(void *to, void *from);

typedef unsigned long pte_t;
typedef unsigned long pmd_t;
typedef unsigned long pgd_t;
typedef unsigned long pgprot_t;

#define pte_val(x)      (x)
#define pmd_val(x)      (x)
#define pgd_val(x)	(x)
#define pgprot_val(x)   (x)

#define __pte(x)        (x)
#define __pmd(x)        (x)
#define __pgd(x)        (x)
#define __pgprot(x)     (x)

typedef struct page *pgtable_t;

#include <asm/memory.h>
#include <asm-generic/getorder.h>

#endif /* !__ASSEMBLY__ */

#define VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#endif /* __KERNEL__ */

#endif
