/*
 * Copyright (C) 2011 Richard Weinberger <richrd@nod.at>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/mm.h>
#include <asm/elf.h>

static struct vm_area_struct gate_vma;

static int __init gate_vma_init(void)
{
	if (!FIXADDR_USER_START)
		return 0;

	gate_vma.vm_mm = NULL;
	gate_vma.vm_start = FIXADDR_USER_START;
	gate_vma.vm_end = FIXADDR_USER_END;
	gate_vma.vm_flags = VM_READ | VM_MAYREAD | VM_EXEC | VM_MAYEXEC;
	gate_vma.vm_page_prot = __P101;

	return 0;
}
__initcall(gate_vma_init);

struct vm_area_struct *get_gate_vma(struct mm_struct *mm)
{
	return FIXADDR_USER_START ? &gate_vma : NULL;
}

int in_gate_area_no_mm(unsigned long addr)
{
	if (!FIXADDR_USER_START)
		return 0;

	if ((addr >= FIXADDR_USER_START) && (addr < FIXADDR_USER_END))
		return 1;

	return 0;
}

int in_gate_area(struct mm_struct *mm, unsigned long addr)
{
	struct vm_area_struct *vma = get_gate_vma(mm);

	if (!vma)
		return 0;

	return (addr >= vma->vm_start) && (addr < vma->vm_end);
}
