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
#include "mem_user.h"
#include "mem.h"
#include "skas.h"
#include "os.h"
#include "tlb.h"

static int do_ops(union mm_context *mmu, struct host_vm_op *ops, int last,
		  int finished, void **flush)
{
	struct host_vm_op *op;
        int i, ret = 0;

        for(i = 0; i <= last && !ret; i++){
		op = &ops[i];
		switch(op->type){
		case MMAP:
			ret = map(&mmu->skas.id, op->u.mmap.addr,
				  op->u.mmap.len, op->u.mmap.r, op->u.mmap.w,
				  op->u.mmap.x, op->u.mmap.fd,
				  op->u.mmap.offset, finished, flush);
			break;
		case MUNMAP:
			ret = unmap(&mmu->skas.id, op->u.munmap.addr,
				    op->u.munmap.len, finished, flush);
			break;
		case MPROTECT:
			ret = protect(&mmu->skas.id, op->u.mprotect.addr,
				      op->u.mprotect.len, op->u.mprotect.r,
				      op->u.mprotect.w, op->u.mprotect.x,
				      finished, flush);
			break;
		default:
			printk("Unknown op type %d in do_ops\n", op->type);
			break;
		}
	}

	return ret;
}

extern int proc_mm;

static void fix_range(struct mm_struct *mm, unsigned long start_addr,
		      unsigned long end_addr, int force)
{
        if(!proc_mm && (end_addr > CONFIG_STUB_START))
                end_addr = CONFIG_STUB_START;

        fix_range_common(mm, start_addr, end_addr, force, do_ops);
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
	unsigned long end;

	/* Don't bother flushing if this address space is about to be
         * destroyed.
         */
        if(atomic_read(&mm->mm_users) == 0)
                return;

	end = proc_mm ? task_size : CONFIG_STUB_START;
        fix_range(mm, 0, end, 0);
}

void force_flush_all_skas(void)
{
	unsigned long end = proc_mm ? task_size : CONFIG_STUB_START;
        fix_range(current->mm, 0, end, 1);
}

void flush_tlb_page_skas(struct vm_area_struct *vma, unsigned long address)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	struct mm_struct *mm = vma->vm_mm;
	void *flush = NULL;
	int r, w, x, err = 0;
	struct mm_id *mm_id;

	pgd = pgd_offset(vma->vm_mm, address);
	if(!pgd_present(*pgd))
		goto kill;

	pud = pud_offset(pgd, address);
	if(!pud_present(*pud))
		goto kill;

	pmd = pmd_offset(pud, address);
	if(!pmd_present(*pmd))
		goto kill;

	pte = pte_offset_kernel(pmd, address);

	r = pte_read(*pte);
	w = pte_write(*pte);
	x = pte_exec(*pte);
	if (!pte_young(*pte)) {
		r = 0;
		w = 0;
	} else if (!pte_dirty(*pte)) {
		w = 0;
	}

	mm_id = &mm->context.skas.id;
	if(pte_newpage(*pte)){
		if(pte_present(*pte)){
			unsigned long long offset;
			int fd;

			fd = phys_mapping(pte_val(*pte) & PAGE_MASK, &offset);
			err = map(mm_id, address, PAGE_SIZE, r, w, x, fd,
				  offset, 1, &flush);
		}
		else err = unmap(mm_id, address, PAGE_SIZE, 1, &flush);
	}
	else if(pte_newprot(*pte))
		err = protect(mm_id, address, PAGE_SIZE, r, w, x, 1, &flush);

	if(err)
		goto kill;

	*pte = pte_mkuptodate(*pte);

	return;

kill:
	printk("Failed to flush page for address 0x%lx\n", address);
	force_sig(SIGKILL, current);
}

