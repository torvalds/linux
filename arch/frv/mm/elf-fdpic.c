/* elf-fdpic.c: ELF FDPIC memory layout management
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/elf-fdpic.h>
#include <asm/mman.h>

/*****************************************************************************/
/*
 * lay out the userspace VM according to our grand design
 */
#ifdef CONFIG_MMU
void elf_fdpic_arch_lay_out_mm(struct elf_fdpic_params *exec_params,
			       struct elf_fdpic_params *interp_params,
			       unsigned long *start_stack,
			       unsigned long *start_brk)
{
	*start_stack = 0x02200000UL;

	/* if the only executable is a shared object, assume that it is an interpreter rather than
	 * a true executable, and map it such that "ld.so --list" comes out right
	 */
	if (!(interp_params->flags & ELF_FDPIC_FLAG_PRESENT) &&
	    exec_params->hdr.e_type != ET_EXEC
	    ) {
		exec_params->load_addr = PAGE_SIZE;

		*start_brk = 0x80000000UL;
	}
	else {
		exec_params->load_addr = 0x02200000UL;

		if ((exec_params->flags & ELF_FDPIC_FLAG_ARRANGEMENT) ==
		    ELF_FDPIC_FLAG_INDEPENDENT
		    ) {
			exec_params->flags &= ~ELF_FDPIC_FLAG_ARRANGEMENT;
			exec_params->flags |= ELF_FDPIC_FLAG_CONSTDISP;
		}
	}

} /* end elf_fdpic_arch_lay_out_mm() */
#endif

/*****************************************************************************/
/*
 * place non-fixed mmaps firstly in the bottom part of memory, working up, and then in the top part
 * of memory, working down
 */
unsigned long arch_get_unmapped_area(struct file *filp, unsigned long addr, unsigned long len,
				     unsigned long pgoff, unsigned long flags)
{
	struct vm_area_struct *vma;
	unsigned long limit;

	if (len > TASK_SIZE)
		return -ENOMEM;

	/* handle MAP_FIXED */
	if (flags & MAP_FIXED)
		return addr;

	/* only honour a hint if we're not going to clobber something doing so */
	if (addr) {
		addr = PAGE_ALIGN(addr);
		vma = find_vma(current->mm, addr);
		if (TASK_SIZE - len >= addr &&
		    (!vma || addr + len <= vma->vm_start))
			goto success;
	}

	/* search between the bottom of user VM and the stack grow area */
	addr = PAGE_SIZE;
	limit = (current->mm->start_stack - 0x00200000);
	if (addr + len <= limit) {
		limit -= len;

		if (addr <= limit) {
			vma = find_vma(current->mm, PAGE_SIZE);
			for (; vma; vma = vma->vm_next) {
				if (addr > limit)
					break;
				if (addr + len <= vma->vm_start)
					goto success;
				addr = vma->vm_end;
			}
		}
	}

	/* search from just above the WorkRAM area to the top of memory */
	addr = PAGE_ALIGN(0x80000000);
	limit = TASK_SIZE - len;
	if (addr <= limit) {
		vma = find_vma(current->mm, addr);
		for (; vma; vma = vma->vm_next) {
			if (addr > limit)
				break;
			if (addr + len <= vma->vm_start)
				goto success;
			addr = vma->vm_end;
		}

		if (!vma && addr <= limit)
			goto success;
	}

#if 0
	printk("[area] l=%lx (ENOMEM) f='%s'\n",
	       len, filp ? filp->f_path.dentry->d_name.name : "");
#endif
	return -ENOMEM;

 success:
#if 0
	printk("[area] l=%lx ad=%lx f='%s'\n",
	       len, addr, filp ? filp->f_path.dentry->d_name.name : "");
#endif
	return addr;
} /* end arch_get_unmapped_area() */
