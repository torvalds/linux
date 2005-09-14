/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Copyright 2003 PathScale, Inc.
 * Licensed under the GPL
 */

#include "linux/stddef.h"
#include "linux/kernel.h"
#include "linux/sched.h"
#include "linux/mm.h"
#include "asm/page.h"
#include "asm/pgtable.h"
#include "asm/uaccess.h"
#include "asm/tlbflush.h"
#include "user_util.h"
#include "mem_user.h"
#include "os.h"
#include "tlb.h"

static int do_ops(union mm_context *mmu, struct host_vm_op *ops, int last,
		    int finished, void **flush)
{
	struct host_vm_op *op;
        int i, ret=0;

        for(i = 0; i <= last && !ret; i++){
		op = &ops[i];
		switch(op->type){
		case MMAP:
                        ret = os_map_memory((void *) op->u.mmap.addr,
                                            op->u.mmap.fd, op->u.mmap.offset,
                                            op->u.mmap.len, op->u.mmap.r,
                                            op->u.mmap.w, op->u.mmap.x);
			break;
		case MUNMAP:
                        ret = os_unmap_memory((void *) op->u.munmap.addr,
                                              op->u.munmap.len);
			break;
		case MPROTECT:
                        ret = protect_memory(op->u.mprotect.addr,
                                             op->u.munmap.len,
                                             op->u.mprotect.r,
                                             op->u.mprotect.w,
                                             op->u.mprotect.x, 1);
			protect_memory(op->u.mprotect.addr, op->u.munmap.len,
				       op->u.mprotect.r, op->u.mprotect.w,
				       op->u.mprotect.x, 1);
			break;
		default:
			printk("Unknown op type %d in do_ops\n", op->type);
			break;
		}
	}

	return ret;
}

static void fix_range(struct mm_struct *mm, unsigned long start_addr, 
		      unsigned long end_addr, int force)
{
        if((current->thread.mode.tt.extern_pid != -1) &&
           (current->thread.mode.tt.extern_pid != os_getpid()))
                panic("fix_range fixing wrong address space, current = 0x%p",
                      current);

        fix_range_common(mm, start_addr, end_addr, force, do_ops);
}

atomic_t vmchange_seq = ATOMIC_INIT(1);

void flush_tlb_kernel_range_tt(unsigned long start, unsigned long end)
{
        if(flush_tlb_kernel_range_common(start, end))
                atomic_inc(&vmchange_seq);
}

static void protect_vm_page(unsigned long addr, int w, int must_succeed)
{
	int err;

	err = protect_memory(addr, PAGE_SIZE, 1, w, 1, must_succeed);
	if(err == 0) return;
	else if((err == -EFAULT) || (err == -ENOMEM)){
		flush_tlb_kernel_range(addr, addr + PAGE_SIZE);
		protect_vm_page(addr, w, 1);
	}
	else panic("protect_vm_page : protect failed, errno = %d\n", err);
}

void mprotect_kernel_vm(int w)
{
	struct mm_struct *mm;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long addr;
	
	mm = &init_mm;
	for(addr = start_vm; addr < end_vm;){
		pgd = pgd_offset(mm, addr);
		pud = pud_offset(pgd, addr);
		pmd = pmd_offset(pud, addr);
		if(pmd_present(*pmd)){
			pte = pte_offset_kernel(pmd, addr);
			if(pte_present(*pte)) protect_vm_page(addr, w, 0);
			addr += PAGE_SIZE;
		}
		else addr += PMD_SIZE;
	}
}

void flush_tlb_kernel_vm_tt(void)
{
        flush_tlb_kernel_range(start_vm, end_vm);
}

void __flush_tlb_one_tt(unsigned long addr)
{
        flush_tlb_kernel_range(addr, addr + PAGE_SIZE);
}
  
void flush_tlb_range_tt(struct vm_area_struct *vma, unsigned long start, 
		     unsigned long end)
{
	if(vma->vm_mm != current->mm) return;

	/* Assumes that the range start ... end is entirely within
	 * either process memory or kernel vm
	 */
	if((start >= start_vm) && (start < end_vm)){
		if(flush_tlb_kernel_range_common(start, end))
			atomic_inc(&vmchange_seq);
	}
	else fix_range(vma->vm_mm, start, end, 0);
}

void flush_tlb_mm_tt(struct mm_struct *mm)
{
	unsigned long seq;

	if(mm != current->mm) return;

	fix_range(mm, 0, STACK_TOP, 0);

	seq = atomic_read(&vmchange_seq);
	if(current->thread.mode.tt.vm_seq == seq)
		return;
	current->thread.mode.tt.vm_seq = seq;
	flush_tlb_kernel_range_common(start_vm, end_vm);
}

void force_flush_all_tt(void)
{
	fix_range(current->mm, 0, STACK_TOP, 1);
	flush_tlb_kernel_range_common(start_vm, end_vm);
}
