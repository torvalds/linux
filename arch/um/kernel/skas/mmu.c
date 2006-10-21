/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/sched.h"
#include "linux/list.h"
#include "linux/spinlock.h"
#include "linux/slab.h"
#include "linux/errno.h"
#include "linux/mm.h"
#include "asm/current.h"
#include "asm/segment.h"
#include "asm/mmu.h"
#include "asm/pgalloc.h"
#include "asm/pgtable.h"
#include "asm/ldt.h"
#include "os.h"
#include "skas.h"

extern int __syscall_stub_start;

static int init_stub_pte(struct mm_struct *mm, unsigned long proc,
			 unsigned long kernel)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pgd = pgd_offset(mm, proc);
	pud = pud_alloc(mm, pgd, proc);
	if (!pud)
		goto out;

	pmd = pmd_alloc(mm, pud, proc);
	if (!pmd)
		goto out_pmd;

	pte = pte_alloc_map(mm, pmd, proc);
	if (!pte)
		goto out_pte;

	/* There's an interaction between the skas0 stub pages, stack
	 * randomization, and the BUG at the end of exit_mmap.  exit_mmap
         * checks that the number of page tables freed is the same as had
         * been allocated.  If the stack is on the last page table page,
	 * then the stack pte page will be freed, and if not, it won't.  To
	 * avoid having to know where the stack is, or if the process mapped
	 * something at the top of its address space for some other reason,
	 * we set TASK_SIZE to end at the start of the last page table.
	 * This keeps exit_mmap off the last page, but introduces a leak
	 * of that page.  So, we hang onto it here and free it in
	 * destroy_context_skas.
	 */

        mm->context.skas.last_page_table = pmd_page_vaddr(*pmd);
#ifdef CONFIG_3_LEVEL_PGTABLES
        mm->context.skas.last_pmd = (unsigned long) __va(pud_val(*pud));
#endif

	*pte = mk_pte(virt_to_page(kernel), __pgprot(_PAGE_PRESENT));
	*pte = pte_mkread(*pte);
	return(0);

 out_pmd:
	pud_free(pud);
 out_pte:
	pmd_free(pmd);
 out:
	return(-ENOMEM);
}

int init_new_context_skas(struct task_struct *task, struct mm_struct *mm)
{
 	struct mmu_context_skas *from_mm = NULL;
	struct mmu_context_skas *to_mm = &mm->context.skas;
	unsigned long stack = 0;
	int ret = -ENOMEM;

	if(skas_needs_stub){
		stack = get_zeroed_page(GFP_KERNEL);
		if(stack == 0)
			goto out;

		/* This zeros the entry that pgd_alloc didn't, needed since
		 * we are about to reinitialize it, and want mm.nr_ptes to
		 * be accurate.
		 */
		mm->pgd[USER_PTRS_PER_PGD] = __pgd(0);

		ret = init_stub_pte(mm, CONFIG_STUB_CODE,
				    (unsigned long) &__syscall_stub_start);
		if(ret)
			goto out_free;

		ret = init_stub_pte(mm, CONFIG_STUB_DATA, stack);
		if(ret)
			goto out_free;

		mm->nr_ptes--;
	}

	to_mm->id.stack = stack;
	if(current->mm != NULL && current->mm != &init_mm)
		from_mm = &current->mm->context.skas;

	if(proc_mm){
		ret = new_mm(stack);
		if(ret < 0){
			printk("init_new_context_skas - new_mm failed, "
			       "errno = %d\n", ret);
			goto out_free;
		}
		to_mm->id.u.mm_fd = ret;
	}
	else {
		if(from_mm)
			to_mm->id.u.pid = copy_context_skas0(stack,
							     from_mm->id.u.pid);
		else to_mm->id.u.pid = start_userspace(stack);
	}

	ret = init_new_ldt(to_mm, from_mm);
	if(ret < 0){
		printk("init_new_context_skas - init_ldt"
		       " failed, errno = %d\n", ret);
		goto out_free;
	}

	return 0;

 out_free:
	if(to_mm->id.stack != 0)
		free_page(to_mm->id.stack);
 out:
	return ret;
}

void destroy_context_skas(struct mm_struct *mm)
{
	struct mmu_context_skas *mmu = &mm->context.skas;

	if(proc_mm)
		os_close_file(mmu->id.u.mm_fd);
	else
		os_kill_ptraced_process(mmu->id.u.pid, 1);

	if(!proc_mm || !ptrace_faultinfo){
		free_page(mmu->id.stack);
		pte_lock_deinit(virt_to_page(mmu->last_page_table));
		pte_free_kernel((pte_t *) mmu->last_page_table);
		dec_zone_page_state(virt_to_page(mmu->last_page_table), NR_PAGETABLE);
#ifdef CONFIG_3_LEVEL_PGTABLES
		pmd_free((pmd_t *) mmu->last_pmd);
#endif
	}
}
