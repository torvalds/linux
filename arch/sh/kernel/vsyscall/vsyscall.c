/*
 * arch/sh/kernel/vsyscall/vsyscall.c
 *
 *  Copyright (C) 2006 Paul Mundt
 *
 * vDSO randomization
 * Copyright(C) 2005-2006, Red Hat, Inc., Ingo Molnar
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/mm.h>
#include <linux/slab.h>
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
int arch_setup_additional_pages(struct linux_binprm *bprm,
				int executable_stack)
{
	struct mm_struct *mm = current->mm;
	unsigned long addr;
	int ret;

	down_write(&mm->mmap_sem);
	addr = get_unmapped_area(NULL, 0, PAGE_SIZE, 0, 0);
	if (IS_ERR_VALUE(addr)) {
		ret = addr;
		goto up_fail;
	}

	ret = install_special_mapping(mm, addr, PAGE_SIZE,
				      VM_READ | VM_EXEC |
				      VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC |
				      VM_ALWAYSDUMP,
				      syscall_pages);
	if (unlikely(ret))
		goto up_fail;

	current->mm->context.vdso = (void *)addr;

up_fail:
	up_write(&mm->mmap_sem);
	return ret;
}

const char *arch_vma_name(struct vm_area_struct *vma)
{
	if (vma->vm_mm && vma->vm_start == (long)vma->vm_mm->context.vdso)
		return "[vdso]";

	return NULL;
}

struct vm_area_struct *get_gate_vma(struct task_struct *task)
{
	return NULL;
}

int in_gate_area(struct task_struct *task, unsigned long address)
{
	return 0;
}

int in_gate_area_no_task(unsigned long address)
{
	return 0;
}
