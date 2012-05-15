/*
 * vDSO implementation for Hexagon
 *
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/err.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/binfmts.h>

#include <asm/vdso.h>

static struct page *vdso_page;

/* Create a vDSO page holding the signal trampoline.
 * We want this for a non-executable stack.
 */
static int __init vdso_init(void)
{
	struct hexagon_vdso *vdso;

	vdso_page = alloc_page(GFP_KERNEL);
	if (!vdso_page)
		panic("Cannot allocate vdso");

	vdso = vmap(&vdso_page, 1, 0, PAGE_KERNEL);
	if (!vdso)
		panic("Cannot map vdso");
	clear_page(vdso);

	/* Install the signal trampoline; currently looks like this:
	 *	r6 = #__NR_rt_sigreturn;
	 *	trap0(#1);
	 */
	vdso->rt_signal_trampoline[0] = __rt_sigtramp_template[0];
	vdso->rt_signal_trampoline[1] = __rt_sigtramp_template[1];

	vunmap(vdso);

	return 0;
}
arch_initcall(vdso_init);

/*
 * Called from binfmt_elf.  Create a VMA for the vDSO page.
 */
int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	int ret;
	unsigned long vdso_base;
	struct mm_struct *mm = current->mm;

	down_write(&mm->mmap_sem);

	/* Try to get it loaded right near ld.so/glibc. */
	vdso_base = STACK_TOP;

	vdso_base = get_unmapped_area(NULL, vdso_base, PAGE_SIZE, 0, 0);
	if (IS_ERR_VALUE(vdso_base)) {
		ret = vdso_base;
		goto up_fail;
	}

	/* MAYWRITE to allow gdb to COW and set breakpoints. */
	ret = install_special_mapping(mm, vdso_base, PAGE_SIZE,
				      VM_READ|VM_EXEC|
				      VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC,
				      &vdso_page);

	if (ret)
		goto up_fail;

	mm->context.vdso = (void *)vdso_base;

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
