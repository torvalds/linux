/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/config.h"
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

        mm->context.skas.last_page_table = pmd_page_kernel(*pmd);
#ifdef CONFIG_3_LEVEL_PGTABLES
        mm->context.skas.last_pmd = (unsigned long) __va(pud_val(*pud));
#endif

	*pte = mk_pte(virt_to_page(kernel), __pgprot(_PAGE_PRESENT));
	*pte = pte_mkexec(*pte);
	*pte = pte_wrprotect(*pte);
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
	struct mm_struct *cur_mm = current->mm;
	struct mm_id *cur_mm_id = &cur_mm->context.skas.id;
	struct mm_id *mm_id = &mm->context.skas.id;
	unsigned long stack = 0;
	int from, ret = -ENOMEM;

	if(!proc_mm || !ptrace_faultinfo){
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
	mm_id->stack = stack;

	if(proc_mm){
		if((cur_mm != NULL) && (cur_mm != &init_mm))
			from = cur_mm_id->u.mm_fd;
		else from = -1;

		ret = new_mm(from, stack);
		if(ret < 0){
			printk("init_new_context_skas - new_mm failed, "
			       "errno = %d\n", ret);
			goto out_free;
		}
		mm_id->u.mm_fd = ret;
	}
	else {
		if((cur_mm != NULL) && (cur_mm != &init_mm))
			mm_id->u.pid = copy_context_skas0(stack,
							  cur_mm_id->u.pid);
		else mm_id->u.pid = start_userspace(stack);
	}

	return 0;

 out_free:
	if(mm_id->stack != 0)
		free_page(mm_id->stack);
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
                dec_page_state(nr_page_table_pages);
#ifdef CONFIG_3_LEVEL_PGTABLES
		pmd_free((pmd_t *) mmu->last_pmd);
#endif
	}
}
