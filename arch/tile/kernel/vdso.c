/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/binfmts.h>
#include <linux/compat.h>
#include <linux/elf.h>
#include <linux/mm.h>
#include <linux/pagemap.h>

#include <asm/vdso.h>
#include <asm/mman.h>
#include <asm/sections.h>

#include <arch/sim.h>

/* The alignment of the vDSO. */
#define VDSO_ALIGNMENT  PAGE_SIZE


static unsigned int vdso_pages;
static struct page **vdso_pagelist;

#ifdef CONFIG_COMPAT
static unsigned int vdso32_pages;
static struct page **vdso32_pagelist;
#endif
static int vdso_ready;

/*
 * The vdso data page.
 */
static union {
	struct vdso_data	data;
	u8			page[PAGE_SIZE];
} vdso_data_store __page_aligned_data;

struct vdso_data *vdso_data = &vdso_data_store.data;

static unsigned int __read_mostly vdso_enabled = 1;

static struct page **vdso_setup(void *vdso_kbase, unsigned int pages)
{
	int i;
	struct page **pagelist;

	pagelist = kzalloc(sizeof(struct page *) * (pages + 1), GFP_KERNEL);
	BUG_ON(pagelist == NULL);
	for (i = 0; i < pages - 1; i++) {
		struct page *pg = virt_to_page(vdso_kbase + i*PAGE_SIZE);
		ClearPageReserved(pg);
		pagelist[i] = pg;
	}
	pagelist[pages - 1] = virt_to_page(vdso_data);
	pagelist[pages] = NULL;

	return pagelist;
}

static int __init vdso_init(void)
{
	int data_pages = sizeof(vdso_data_store) >> PAGE_SHIFT;

	/*
	 * We can disable vDSO support generally, but we need to retain
	 * one page to support the two-bundle (16-byte) rt_sigreturn path.
	 */
	if (!vdso_enabled) {
		size_t offset = (unsigned long)&__vdso_rt_sigreturn;
		static struct page *sigret_page;
		sigret_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
		BUG_ON(sigret_page == NULL);
		vdso_pagelist = &sigret_page;
		vdso_pages = 1;
		BUG_ON(offset >= PAGE_SIZE);
		memcpy(page_address(sigret_page) + offset,
		       vdso_start + offset, 16);
#ifdef CONFIG_COMPAT
		vdso32_pages = vdso_pages;
		vdso32_pagelist = vdso_pagelist;
#endif
		vdso_ready = 1;
		return 0;
	}

	vdso_pages = (vdso_end - vdso_start) >> PAGE_SHIFT;
	vdso_pages += data_pages;
	vdso_pagelist = vdso_setup(vdso_start, vdso_pages);

#ifdef CONFIG_COMPAT
	vdso32_pages = (vdso32_end - vdso32_start) >> PAGE_SHIFT;
	vdso32_pages += data_pages;
	vdso32_pagelist = vdso_setup(vdso32_start, vdso32_pages);
#endif

	smp_wmb();
	vdso_ready = 1;

	return 0;
}
arch_initcall(vdso_init);

const char *arch_vma_name(struct vm_area_struct *vma)
{
	if (vma->vm_mm && vma->vm_start == VDSO_BASE)
		return "[vdso]";
#ifndef __tilegx__
	if (vma->vm_start == MEM_USER_INTRPT)
		return "[intrpt]";
#endif
	return NULL;
}

int setup_vdso_pages(void)
{
	struct page **pagelist;
	unsigned long pages;
	struct mm_struct *mm = current->mm;
	unsigned long vdso_base = 0;
	int retval = 0;

	if (!vdso_ready)
		return 0;

	mm->context.vdso_base = 0;

	pagelist = vdso_pagelist;
	pages = vdso_pages;
#ifdef CONFIG_COMPAT
	if (is_compat_task()) {
		pagelist = vdso32_pagelist;
		pages = vdso32_pages;
	}
#endif

	/*
	 * vDSO has a problem and was disabled, just don't "enable" it for the
	 * process.
	 */
	if (pages == 0)
		return 0;

	vdso_base = get_unmapped_area(NULL, vdso_base,
				      (pages << PAGE_SHIFT) +
				      ((VDSO_ALIGNMENT - 1) & PAGE_MASK),
				      0, 0);
	if (IS_ERR_VALUE(vdso_base)) {
		retval = vdso_base;
		return retval;
	}

	/* Add required alignment. */
	vdso_base = ALIGN(vdso_base, VDSO_ALIGNMENT);

	/*
	 * Put vDSO base into mm struct. We need to do this before calling
	 * install_special_mapping or the perf counter mmap tracking code
	 * will fail to recognise it as a vDSO (since arch_vma_name fails).
	 */
	mm->context.vdso_base = vdso_base;

	/*
	 * our vma flags don't have VM_WRITE so by default, the process isn't
	 * allowed to write those pages.
	 * gdb can break that with ptrace interface, and thus trigger COW on
	 * those pages but it's then your responsibility to never do that on
	 * the "data" page of the vDSO or you'll stop getting kernel updates
	 * and your nice userland gettimeofday will be totally dead.
	 * It's fine to use that for setting breakpoints in the vDSO code
	 * pages though
	 */
	retval = install_special_mapping(mm, vdso_base,
					 pages << PAGE_SHIFT,
					 VM_READ|VM_EXEC |
					 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC,
					 pagelist);
	if (retval)
		mm->context.vdso_base = 0;

	return retval;
}

static __init int vdso_func(char *s)
{
	return kstrtouint(s, 0, &vdso_enabled);
}
__setup("vdso=", vdso_func);
