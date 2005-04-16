/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Copyright 2003 PathScale, Inc.
 * Licensed under the GPL
 */

#include "linux/stddef.h"
#include "linux/sched.h"
#include "linux/mm.h"
#include "asm/page.h"
#include "asm/pgtable.h"
#include "asm/mmu.h"
#include "user_util.h"
#include "mem_user.h"
#include "mem.h"
#include "skas.h"
#include "os.h"
#include "tlb.h"

static void do_ops(int fd, struct host_vm_op *ops, int last)
{
	struct host_vm_op *op;
	int i;

	for(i = 0; i <= last; i++){
		op = &ops[i];
		switch(op->type){
		case MMAP:
			map(fd, op->u.mmap.addr, op->u.mmap.len,
			    op->u.mmap.r, op->u.mmap.w, op->u.mmap.x,
			    op->u.mmap.fd, op->u.mmap.offset);
			break;
		case MUNMAP:
			unmap(fd, (void *) op->u.munmap.addr,
			      op->u.munmap.len);
			break;
		case MPROTECT:
			protect(fd, op->u.mprotect.addr, op->u.mprotect.len,
				op->u.mprotect.r, op->u.mprotect.w,
				op->u.mprotect.x);
			break;
		default:
			printk("Unknown op type %d in do_ops\n", op->type);
			break;
		}
	}
}

static void fix_range(struct mm_struct *mm, unsigned long start_addr,
		      unsigned long end_addr, int force)
{
        int fd = mm->context.skas.mm_fd;

        fix_range_common(mm, start_addr, end_addr, force, fd, do_ops);
}

void __flush_tlb_one_skas(unsigned long addr)
{
        flush_tlb_kernel_range_common(addr, addr + PAGE_SIZE);
}

void flush_tlb_range_skas(struct vm_area_struct *vma, unsigned long start, 
		     unsigned long end)
{
        if(vma->vm_mm == NULL)
                flush_tlb_kernel_range_common(start, end);
        else fix_range(vma->vm_mm, start, end, 0);
}

void flush_tlb_mm_skas(struct mm_struct *mm)
{
	/* Don't bother flushing if this address space is about to be
         * destroyed.
         */
        if(atomic_read(&mm->mm_users) == 0)
                return;

        fix_range(mm, 0, host_task_size, 0);
        flush_tlb_kernel_range_common(start_vm, end_vm);
}

void force_flush_all_skas(void)
{
        fix_range(current->mm, 0, host_task_size, 1);
}
