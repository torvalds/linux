// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/binfmts.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/unistd.h>
#include <linux/uaccess.h>

#include <asm/vdso.h>
#include <asm/cacheflush.h>

static struct page *vdso_page;

static int __init init_vdso(void)
{
	struct csky_vdso *vdso;
	int err = 0;

	vdso_page = alloc_page(GFP_KERNEL);
	if (!vdso_page)
		panic("Cannot allocate vdso");

	vdso = vmap(&vdso_page, 1, 0, PAGE_KERNEL);
	if (!vdso)
		panic("Cannot map vdso");

	clear_page(vdso);

	err = setup_vdso_page(vdso->rt_signal_retcode);
	if (err)
		panic("Cannot set signal return code, err: %x.", err);

	dcache_wb_range((unsigned long)vdso, (unsigned long)vdso + 16);

	vunmap(vdso);

	return 0;
}
subsys_initcall(init_vdso);

int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	int ret;
	unsigned long addr;
	struct mm_struct *mm = current->mm;

	mmap_write_lock(mm);

	addr = get_unmapped_area(NULL, STACK_TOP, PAGE_SIZE, 0, 0);
	if (IS_ERR_VALUE(addr)) {
		ret = addr;
		goto up_fail;
	}

	ret = install_special_mapping(
			mm,
			addr,
			PAGE_SIZE,
			VM_READ|VM_EXEC|VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC,
			&vdso_page);
	if (ret)
		goto up_fail;

	mm->context.vdso = (void *)addr;

up_fail:
	mmap_write_unlock(mm);
	return ret;
}

const char *arch_vma_name(struct vm_area_struct *vma)
{
	if (vma->vm_mm == NULL)
		return NULL;

	if (vma->vm_start == (long)vma->vm_mm->context.vdso)
		return "[vdso]";
	else
		return NULL;
}
