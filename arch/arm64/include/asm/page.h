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

struct folio *vma_alloc_zeroed_movable_folio(struct vm_area_struct *vma,
						unsigned long vaddr);
#define vma_alloc_zeroed_movable_folio vma_alloc_zeroed_movable_folio

void tag_clear_highpage(struct page *to);
#define __HAVE_ARCH_TAG_CLEAR_HIGHPAGE

#define clear_user_page(page, vaddr, pg)	clear_page(page)
#define copy_user_page(to, from, vaddr, pg)	copy_page(to, from)

typedef struct page *pgtable_t;

int pfn_is_map_memory(unsigned long pfn);

#include <asm/memory.h>

#endif /* !__ASSEMBLY__ */

#define VM_DATA_DEFAULT_FLAGS	(VM_DATA_FLAGS_TSK_EXEC | VM_MTE_ALLOWED)

#include <asm-generic/getorder.h>

#endif
