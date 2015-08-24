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
#include <linux/file.h>
#include <linux/elf.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/sections.h>
#include <asm/vdso.h>
#include <arch/sim.h>

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
	int ret = 0;
	char *buf, *path;
	struct vm_area_struct *vma;
	struct file *exe_file;

	if (!sim_is_simulator())
		return 1;

	buf = (char *) __get_free_page(GFP_KERNEL);
	if (buf == NULL)
		return 0;

	exe_file = get_mm_exe_file(mm);
	if (exe_file == NULL)
		goto done_free;

	path = file_path(exe_file, buf, PAGE_SIZE);
	if (IS_ERR(path))
		goto done_put;

	down_read(&mm->mmap_sem);
	for (vma = current->mm->mmap; ; vma = vma->vm_next) {
		if (vma == NULL) {
			up_read(&mm->mmap_sem);
			goto done_put;
		}
		if (vma->vm_file == exe_file)
			break;
	}

	/*
	 * Notify simulator of an ET_DYN object so we know the load address.
	 * The somewhat cryptic overuse of SIM_CONTROL_DLOPEN allows us
	 * to be backward-compatible with older simulator releases.
	 */
	if (vma->vm_start == (ELF_ET_DYN_BASE & PAGE_MASK)) {
		char buf[64];
		int i;

		snprintf(buf, sizeof(buf), "0x%lx:@", vma->vm_start);
		for (i = 0; ; ++i) {
			char c = buf[i];
			__insn_mtspr(SPR_SIM_CONTROL,
				     (SIM_CONTROL_DLOPEN
				      | (c << _SIM_CONTROL_OPERATOR_BITS)));
			if (c == '\0') {
				ret = 1; /* success */
				break;
			}
		}
	}
	up_read(&mm->mmap_sem);

	sim_notify_exec(path);
done_put:
	fput(exe_file);
done_free:
	free_page((unsigned long)buf);
	return ret;
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


int arch_setup_additional_pages(struct linux_binprm *bprm,
				int executable_stack)
{
	struct mm_struct *mm = current->mm;
	int retval = 0;

	/*
	 * Notify the simulator that an exec just occurred.
	 * If we can't find the filename of the mapping, just use
	 * whatever was passed as the linux_binprm filename.
	 */
	if (!notify_exec(mm))
		sim_notify_exec(bprm->filename);

	down_write(&mm->mmap_sem);

	retval = setup_vdso_pages();

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
