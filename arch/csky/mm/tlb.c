// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>

#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/setup.h>

void flush_tlb_all(void)
{
	tlb_invalid_all();
}

void flush_tlb_mm(struct mm_struct *mm)
{
	tlb_invalid_all();
}

void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
			unsigned long end)
{
	tlb_invalid_all();
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	tlb_invalid_all();
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long addr)
{
	tlb_invalid_all();
}

void flush_tlb_one(unsigned long addr)
{
	tlb_invalid_all();
}
EXPORT_SYMBOL(flush_tlb_one);
