// SPDX-License-Identifier: GPL-2.0
/*
 * arch/sh/kernel/vsyscall/vsyscall.c
 *
 *  Copyright (C) 2006 Paul Mundt
 *
 * vDSO randomization
 * Copyright(C) 2005-2006, Red Hat, Inc., Ingo Molnar
 */
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/module.h>
#include <linux/elf.h>
#include <linux/sched.h>
#include <linux/err.h>

/*
 * Should the kernel map a VDSO page into processes and pass its
 * address down to glibc upon exec()?
 */
unsigned int __read_mostly vdso_enabled = 1;
EXPORT_SYMBOL_GPL(vdso_enabled);

static int __init vdso_setup(char *s)
{
	vdso_enabled = simple_strtoul(s, NULL, 0);
	return 1;
}
__setup("vdso=", vdso_setup);

/*
 * These symbols are defined by vsyscall.o to mark the bounds
 * of the ELF DSO images included therein.
 */
extern const char vsyscall_trapa_start, vsyscall_trapa_end;
static struct page *syscall_pages[1];
static struct vm_special_mapping vdso_mapping = {
	.name = "[vdso]",
	.pages = syscall_pages,
};

int __init vsyscall_init(void)
{
	void *syscall_page = (void *)get_zeroed_page(GFP_ATOMIC);
	syscall_pages[0] = virt_to_page(syscall_page);

	/*
	 * XXX: Map this page to a fixmap entry if we get around
	 * to adding the page to ELF core dumps
	 */

	memcpy(syscall_page,
	       &vsyscall_trapa_start,
	       &vsyscall_trapa_end - &vsyscall_trapa_start);

	return 0;
}

/* Setup a VMA at program startup for the vsyscall page */
int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long addr;
	int ret;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	addr = get_unmapped_area(NULL, 0, PAGE_SIZE, 0, 0);
	if (IS_ERR_VALUE(addr)) {
		ret = addr;
		goto up_fail;
	}

	vdso_mapping.pages = syscall_pages;
	vma = _install_special_mapping(mm, addr, PAGE_SIZE,
				      VM_READ | VM_EXEC |
				      VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC,
				      &vdso_mapping);
	ret = PTR_ERR(vma);
	if (IS_ERR(vma))
		goto up_fail;

	current->mm->context.vdso = (void *)addr;
	ret = 0;

up_fail:
	mmap_write_unlock(mm);
	return ret;
}

const char *arch_vma_name(struct vm_area_struct *vma)
{
	if (vma->vm_mm && vma->vm_start == (long)vma->vm_mm->context.vdso)
		return "[vdso]";

	return NULL;
}
