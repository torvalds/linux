// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/binfmts.h>
#include <linux/elf.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include <asm/page.h>
#include <vdso/datapage.h>

extern char vdso_start[], vdso_end[];

static unsigned int vdso_pages;
static struct page **vdso_pagelist;

static union vdso_data_store vdso_data_store __page_aligned_data;
struct vdso_data *vdso_data = vdso_data_store.data;

static int __init vdso_init(void)
{
	unsigned int i;

	vdso_pages = (vdso_end - vdso_start) >> PAGE_SHIFT;
	vdso_pagelist =
		kcalloc(vdso_pages + 1, sizeof(struct page *), GFP_KERNEL);
	if (unlikely(vdso_pagelist == NULL)) {
		pr_err("vdso: pagelist allocation failed\n");
		return -ENOMEM;
	}

	for (i = 0; i < vdso_pages; i++) {
		struct page *pg;

		pg = virt_to_page(vdso_start + (i << PAGE_SHIFT));
		vdso_pagelist[i] = pg;
	}
	vdso_pagelist[i] = virt_to_page(vdso_data);

	return 0;
}
arch_initcall(vdso_init);

int arch_setup_additional_pages(struct linux_binprm *bprm,
	int uses_interp)
{
	struct mm_struct *mm = current->mm;
	unsigned long vdso_base, vdso_len;
	int ret;

	vdso_len = (vdso_pages + 1) << PAGE_SHIFT;

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

	ret =
	   install_special_mapping(mm, vdso_base, vdso_pages << PAGE_SHIFT,
		(VM_READ | VM_EXEC | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC),
		vdso_pagelist);

	if (unlikely(ret)) {
		mm->context.vdso = NULL;
		goto end;
	}

	vdso_base += (vdso_pages << PAGE_SHIFT);
	ret = install_special_mapping(mm, vdso_base, PAGE_SIZE,
		(VM_READ | VM_MAYREAD), &vdso_pagelist[vdso_pages]);

	if (unlikely(ret))
		mm->context.vdso = NULL;
end:
	mmap_write_unlock(mm);
	return ret;
}

const char *arch_vma_name(struct vm_area_struct *vma)
{
	if (vma->vm_mm && (vma->vm_start == (long)vma->vm_mm->context.vdso))
		return "[vdso]";
	if (vma->vm_mm && (vma->vm_start ==
			   (long)vma->vm_mm->context.vdso + PAGE_SIZE))
		return "[vdso_data]";
	return NULL;
}
