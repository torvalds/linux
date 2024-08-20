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

static unsigned int __read_mostly vdso_enabled = 1;
unsigned long um_vdso_addr;

extern unsigned long task_size;
extern char vdso_start[], vdso_end[];

static struct page **vdsop;

static int __init init_vdso(void)
{
	struct page *um_vdso;

	BUG_ON(vdso_end - vdso_start > PAGE_SIZE);

	um_vdso_addr = task_size - PAGE_SIZE;

	vdsop = kmalloc(sizeof(struct page *), GFP_KERNEL);
	if (!vdsop)
		goto oom;

	um_vdso = alloc_page(GFP_KERNEL);
	if (!um_vdso) {
		kfree(vdsop);

		goto oom;
	}

	copy_page(page_address(um_vdso), vdso_start);
	*vdsop = um_vdso;

	return 0;

oom:
	printk(KERN_ERR "Cannot allocate vdso\n");
	vdso_enabled = 0;

	return -ENOMEM;
}
subsys_initcall(init_vdso);

int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;
	static struct vm_special_mapping vdso_mapping = {
		.name = "[vdso]",
	};

	if (!vdso_enabled)
		return 0;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	vdso_mapping.pages = vdsop;
	vma = _install_special_mapping(mm, um_vdso_addr, PAGE_SIZE,
		VM_READ|VM_EXEC|
		VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC,
		&vdso_mapping);

	mmap_write_unlock(mm);

	return IS_ERR(vma) ? PTR_ERR(vma) : 0;
}
