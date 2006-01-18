/* Copyright 2005 Andi Kleen, SuSE Labs.
 * Licensed under GPL, v.2
 */
#include <linux/config.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/random.h>
#include <asm/ia32.h>

/* Notebook: move the mmap code from sys_x86_64.c over here. */

void arch_pick_mmap_layout(struct mm_struct *mm)
{
#ifdef CONFIG_IA32_EMULATION
	if (current_thread_info()->flags & _TIF_IA32)
		return ia32_pick_mmap_layout(mm);
#endif
	mm->mmap_base = TASK_UNMAPPED_BASE;
	if (current->flags & PF_RANDOMIZE) {
		/* Add 28bit randomness which is about 40bits of address space
		   because mmap base has to be page aligned.
 		   or ~1/128 of the total user VM
	   	   (total user address space is 47bits) */
		unsigned rnd = get_random_int() & 0xfffffff;
		mm->mmap_base += ((unsigned long)rnd) << PAGE_SHIFT;
	}
	mm->get_unmapped_area = arch_get_unmapped_area;
	mm->unmap_area = arch_unmap_area;
}

