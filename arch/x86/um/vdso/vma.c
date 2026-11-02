// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011 Richard Weinberger <richrd@nod.at>
 */

#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/page.h>
#include <asm/elf.h>
#include <linux/init.h>
#include <os.h>

unsigned long um_vdso_addr;
static struct page *um_vdso;

extern unsigned long task_size;
extern char vdso_start[], vdso_end[];

static int __init init_vdso(void)
{
	BUG_ON(vdso_end - vdso_start > PAGE_SIZE);

	um_vdso = alloc_page(GFP_KERNEL);
	if (!um_vdso)
		panic("Cannot allocate vdso\n");

	copy_page(page_address(um_vdso), vdso_start);

#ifdef CONFIG_MMU
	um_vdso_addr = task_size - PAGE_SIZE;
#else
	/* this is fine with NOMMU as everything is accessible */
	um_vdso_addr = (unsigned long)page_address(um_vdso);
	os_protect_memory((void *)um_vdso_addr, vdso_end - vdso_start, 1, 0, 1);
#endif

	return 0;
}
subsys_initcall(init_vdso);

#ifdef CONFIG_MMU
int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;
	static struct vm_special_mapping vdso_mapping = {
		.name = "[vdso]",
		.pages = &um_vdso,
	};

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	vma = _install_special_mapping(mm, um_vdso_addr, PAGE_SIZE,
		VM_READ|VM_EXEC|
		VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC,
		&vdso_mapping);

	mmap_write_unlock(mm);

	return IS_ERR(vma) ? PTR_ERR(vma) : 0;
}
#endif /* CONFIG_MMU */
