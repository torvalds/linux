/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/page.h
 *
 * Copyright (C) 1995-2003 Russell King
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_PAGE_H
#define __ASM_PAGE_H

#include <asm/page-def.h>

#ifndef __ASSEMBLY__

#include <linux/personality.h> /* for READ_IMPLIES_EXEC */
#include <linux/types.h> /* for gfp_t */
#include <asm/pgtable-types.h>

struct page;
struct vm_area_struct;

extern void copy_page(void *to, const void *from);
extern void clear_page(void *to);

void copy_user_highpage(struct page *to, struct page *from,
			unsigned long vaddr, struct vm_area_struct *vma);
#define __HAVE_ARCH_COPY_USER_HIGHPAGE

void copy_highpage(struct page *to, struct page *from);
#define __HAVE_ARCH_COPY_HIGHPAGE

struct page *alloc_zeroed_user_highpage_movable(struct vm_area_struct *vma,
						unsigned long vaddr);
#define __HAVE_ARCH_ALLOC_ZEROED_USER_HIGHPAGE_MOVABLE

void tag_clear_highpage(struct page *to);
#define __HAVE_ARCH_TAG_CLEAR_HIGHPAGE

#define clear_user_page(page, vaddr, pg)	clear_page(page)
#define copy_user_page(to, from, vaddr, pg)	copy_page(to, from)

typedef struct page *pgtable_t;

extern int pfn_valid(unsigned long);

#ifdef CONFIG_ARM64_ERRATUM_2454944_DEBUG
#include <asm/cpufeature.h>

void page_check_nc(struct page *page, int order);

static inline void arch_free_page(struct page *page, int order)
{
	if (cpus_have_const_cap(ARM64_WORKAROUND_NO_DMA_ALIAS))
		page_check_nc(page, order);
}
#define HAVE_ARCH_FREE_PAGE
#endif

#include <asm/memory.h>

#endif /* !__ASSEMBLY__ */

#define VM_DATA_DEFAULT_FLAGS	(VM_DATA_FLAGS_TSK_EXEC | VM_MTE_ALLOWED)

#include <asm-generic/getorder.h>

#endif
