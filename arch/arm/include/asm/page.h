/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/include/asm/page.h
 *
 *  Copyright (C) 1995-2003 Russell King
 */
#ifndef _ASMARM_PAGE_H
#define _ASMARM_PAGE_H

/* PAGE_SHIFT determines the page size */
#define PAGE_SHIFT		12
#define PAGE_SIZE		(_AC(1,UL) << PAGE_SHIFT)
#define PAGE_MASK		(~((1 << PAGE_SHIFT) - 1))

#ifndef __ASSEMBLY__

#ifndef CONFIG_MMU

#include <asm/page-nommu.h>

#else

#include <asm/glue.h>

/*
 *	User Space Model
 *	================
 *
 *	This section selects the correct set of functions for dealing with
 *	page-based copying and clearing for user space for the particular
 *	processor(s) we're building for.
 *
 *	We have the following to choose from:
 *	  v4wt		- ARMv4 with writethrough cache, without minicache
 *	  v4wb		- ARMv4 with writeback cache, without minicache
 *	  v4_mc		- ARMv4 with minicache
 *	  xscale	- Xscale
 *	  xsc3		- XScalev3
 */
#undef _USER
#undef MULTI_USER

#ifdef CONFIG_CPU_COPY_V4WT
# ifdef _USER
#  define MULTI_USER 1
# else
#  define _USER v4wt
# endif
#endif

#ifdef CONFIG_CPU_COPY_V4WB
# ifdef _USER
#  define MULTI_USER 1
# else
#  define _USER v4wb
# endif
#endif

#ifdef CONFIG_CPU_COPY_FEROCEON
# ifdef _USER
#  define MULTI_USER 1
# else
#  define _USER feroceon
# endif
#endif

#ifdef CONFIG_CPU_COPY_FA
# ifdef _USER
#  define MULTI_USER 1
# else
#  define _USER fa
# endif
#endif

#ifdef CONFIG_CPU_SA1100
# ifdef _USER
#  define MULTI_USER 1
# else
#  define _USER v4_mc
# endif
#endif

#ifdef CONFIG_CPU_XSCALE
# ifdef _USER
#  define MULTI_USER 1
# else
#  define _USER xscale_mc
# endif
#endif

#ifdef CONFIG_CPU_XSC3
# ifdef _USER
#  define MULTI_USER 1
# else
#  define _USER xsc3_mc
# endif
#endif

#ifdef CONFIG_CPU_COPY_V6
# define MULTI_USER 1
#endif

#if !defined(_USER) && !defined(MULTI_USER)
#error Unknown user operations model
#endif

struct page;
struct vm_area_struct;

struct cpu_user_fns {
	void (*cpu_clear_user_highpage)(struct page *page, unsigned long vaddr);
	void (*cpu_copy_user_highpage)(struct page *to, struct page *from,
			unsigned long vaddr, struct vm_area_struct *vma);
};

void fa_copy_user_highpage(struct page *to, struct page *from,
	unsigned long vaddr, struct vm_area_struct *vma);
void fa_clear_user_highpage(struct page *page, unsigned long vaddr);
void feroceon_copy_user_highpage(struct page *to, struct page *from,
	unsigned long vaddr, struct vm_area_struct *vma);
void feroceon_clear_user_highpage(struct page *page, unsigned long vaddr);
void v4_mc_copy_user_highpage(struct page *to, struct page *from,
	unsigned long vaddr, struct vm_area_struct *vma);
void v4_mc_clear_user_highpage(struct page *page, unsigned long vaddr);
void v4wb_copy_user_highpage(struct page *to, struct page *from,
	unsigned long vaddr, struct vm_area_struct *vma);
void v4wb_clear_user_highpage(struct page *page, unsigned long vaddr);
void v4wt_copy_user_highpage(struct page *to, struct page *from,
	unsigned long vaddr, struct vm_area_struct *vma);
void v4wt_clear_user_highpage(struct page *page, unsigned long vaddr);
void xsc3_mc_copy_user_highpage(struct page *to, struct page *from,
	unsigned long vaddr, struct vm_area_struct *vma);
void xsc3_mc_clear_user_highpage(struct page *page, unsigned long vaddr);
void xscale_mc_copy_user_highpage(struct page *to, struct page *from,
	unsigned long vaddr, struct vm_area_struct *vma);
void xscale_mc_clear_user_highpage(struct page *page, unsigned long vaddr);

#ifdef MULTI_USER
extern struct cpu_user_fns cpu_user;

#define __cpu_clear_user_highpage	cpu_user.cpu_clear_user_highpage
#define __cpu_copy_user_highpage	cpu_user.cpu_copy_user_highpage

#else

#define __cpu_clear_user_highpage	__glue(_USER,_clear_user_highpage)
#define __cpu_copy_user_highpage	__glue(_USER,_copy_user_highpage)

extern void __cpu_clear_user_highpage(struct page *page, unsigned long vaddr);
extern void __cpu_copy_user_highpage(struct page *to, struct page *from,
			unsigned long vaddr, struct vm_area_struct *vma);
#endif

#define clear_user_highpage(page,vaddr)		\
	 __cpu_clear_user_highpage(page, vaddr)

#define __HAVE_ARCH_COPY_USER_HIGHPAGE
#define copy_user_highpage(to,from,vaddr,vma)	\
	__cpu_copy_user_highpage(to, from, vaddr, vma)

#define clear_page(page)	memset((void *)(page), 0, PAGE_SIZE)
extern void copy_page(void *to, const void *from);

#ifdef CONFIG_KUSER_HELPERS
#define __HAVE_ARCH_GATE_AREA 1
#endif

#ifdef CONFIG_ARM_LPAE
#include <asm/pgtable-3level-types.h>
#else
#include <asm/pgtable-2level-types.h>
#ifdef CONFIG_VMAP_STACK
#define ARCH_PAGE_TABLE_SYNC_MASK	PGTBL_PMD_MODIFIED
#endif
#endif

#endif /* CONFIG_MMU */

typedef struct page *pgtable_t;

#ifdef CONFIG_HAVE_ARCH_PFN_VALID
extern int pfn_valid(unsigned long);
#define pfn_valid pfn_valid
#endif

#endif /* !__ASSEMBLY__ */

#include <asm/memory.h>

#define VM_DATA_DEFAULT_FLAGS	VM_DATA_FLAGS_TSK_EXEC

#include <asm-generic/getorder.h>
#include <asm-generic/memory_model.h>

#endif
