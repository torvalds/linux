// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/binfmts.h>
#include <linux/elf.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include <asm/page.h>

extern char vdso_start[], vdso_end[];

static unsigned int vdso_pages;
static struct page **vdso_pagelist;

static int __init vdso_init(void)
{
	unsigned int i;

	vdso_pages = (vdso_end - vdso_start) >> PAGE_SHIFT;
	vdso_pagelist =
		kcalloc(vdso_pages, sizeof(struct page *), GFP_KERNEL);
	if (unlikely(vdso_pagelist == NULL)) {
		pr_err("vdso: pagelist allocation failed\n");
		return -ENOMEM;
	}

	for (i = 0; i < vdso_pages; i++) {
		struct page *pg;

		pg = virt_to_page(vdso_start + (i << PAGE_SHIFT));
		vdso_pagelist[i] = pg;
	}

	return 0;
}
arch_initcall(vdso_init);

int arch_setup_additional_pages(struct linux_binprm *bprm,
	int uses_interp)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;
	unsigned long vdso_base, vdso_len;
	int ret;
	static struct vm_special_mapping vdso_mapping = {
		.name = "[vdso]",
	};

	vdso_len = vdso_pages << PAGE_SHIFT;

	mmap_write_lock(mm);
	vdso_base = get_unmapped_area(NULL, 0, vdso_len, 0, 0);
	if (IS_ERR_VALUE(vdso_base)) {
		ret = vdso_base;
		goto end;
	}

	/*
	 * Put vDSO base into mm struct. We need to do this before calling
	 * install_special_mapping or the perf counter mmap tracking code
	 * will fail to recognise it as a vDSO (since arch_vma_name fails).
	 */
	mm->context.vdso = (void *)vdso_base;

	vdso_mapping.pages = vdso_pagelist;
	vma =
	   _install_special_mapping(mm, vdso_base, vdso_pages << PAGE_SHIFT,
		(VM_READ | VM_EXEC | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC),
		&vdso_mapping);

	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		mm->context.vdso = NULL;
		goto end;
	}

	vdso_base += (vdso_pages << PAGE_SHIFT);
	ret = 0;
end:
	mmap_write_unlock(mm);
	return ret;
}
