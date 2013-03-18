/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
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

#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/binfmts.h>
#include <linux/compat.h>
#include <linux/mman.h>
#include <linux/elf.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/sections.h>
#include <arch/sim_def.h>

/* Notify a running simulator, if any, that an exec just occurred. */
static void sim_notify_exec(const char *binary_name)
{
	unsigned char c;
	do {
		c = *binary_name++;
		__insn_mtspr(SPR_SIM_CONTROL,
			     (SIM_CONTROL_OS_EXEC
			      | (c << _SIM_CONTROL_OPERATOR_BITS)));

	} while (c);
}

static int notify_exec(struct mm_struct *mm)
{
	int retval = 0;  /* failure */

	if (mm->exe_file) {
		char *buf = (char *) __get_free_page(GFP_KERNEL);
		if (buf) {
			char *path = d_path(&mm->exe_file->f_path,
					    buf, PAGE_SIZE);
			if (!IS_ERR(path)) {
				sim_notify_exec(path);
				retval = 1;
			}
			free_page((unsigned long)buf);
		}
	}
	return retval;
}

/* Notify a running simulator, if any, that we loaded an interpreter. */
static void sim_notify_interp(unsigned long load_addr)
{
	size_t i;
	for (i = 0; i < sizeof(load_addr); i++) {
		unsigned char c = load_addr >> (i * 8);
		__insn_mtspr(SPR_SIM_CONTROL,
			     (SIM_CONTROL_OS_INTERP
			      | (c << _SIM_CONTROL_OPERATOR_BITS)));
	}
}


/* Kernel address of page used to map read-only kernel data into userspace. */
static void *vdso_page;

/* One-entry array used for install_special_mapping. */
static struct page *vdso_pages[1];

static int __init vdso_setup(void)
{
	vdso_page = (void *)get_zeroed_page(GFP_ATOMIC);
	memcpy(vdso_page, __rt_sigreturn, __rt_sigreturn_end - __rt_sigreturn);
	vdso_pages[0] = virt_to_page(vdso_page);
	return 0;
}
device_initcall(vdso_setup);

const char *arch_vma_name(struct vm_area_struct *vma)
{
	if (vma->vm_private_data == vdso_pages)
		return "[vdso]";
#ifndef __tilegx__
	if (vma->vm_start == MEM_USER_INTRPT)
		return "[intrpt]";
#endif
	return NULL;
}

int arch_setup_additional_pages(struct linux_binprm *bprm,
				int executable_stack)
{
	struct mm_struct *mm = current->mm;
	unsigned long vdso_base;
	int retval = 0;

	down_write(&mm->mmap_sem);

	/*
	 * Notify the simulator that an exec just occurred.
	 * If we can't find the filename of the mapping, just use
	 * whatever was passed as the linux_binprm filename.
	 */
	if (!notify_exec(mm))
		sim_notify_exec(bprm->filename);

	/*
	 * MAYWRITE to allow gdb to COW and set breakpoints
	 */
	vdso_base = VDSO_BASE;
	retval = install_special_mapping(mm, vdso_base, PAGE_SIZE,
					 VM_READ|VM_EXEC|
					 VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC,
					 vdso_pages);

#ifndef __tilegx__
	/*
	 * Set up a user-interrupt mapping here; the user can't
	 * create one themselves since it is above TASK_SIZE.
	 * We make it unwritable by default, so the model for adding
	 * interrupt vectors always involves an mprotect.
	 */
	if (!retval) {
		unsigned long addr = MEM_USER_INTRPT;
		addr = mmap_region(NULL, addr, INTRPT_SIZE,
				   VM_READ|VM_EXEC|
				   VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC, 0);
		if (addr > (unsigned long) -PAGE_SIZE)
			retval = (int) addr;
	}
#endif

	up_write(&mm->mmap_sem);

	return retval;
}


void elf_plat_init(struct pt_regs *regs, unsigned long load_addr)
{
	/* Zero all registers. */
	memset(regs, 0, sizeof(*regs));

	/* Report the interpreter's load address. */
	sim_notify_interp(load_addr);
}
