// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015 Thomas Meyer (thomas@m3y3r.de)
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>

#include <asm/pgalloc.h>
#include <asm/sections.h>
#include <as-layout.h>
#include <os.h>
#include <skas.h>

static int init_stub_pte(struct mm_struct *mm, unsigned long proc,
			 unsigned long kernel)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pgd = pgd_offset(mm, proc);

	p4d = p4d_alloc(mm, pgd, proc);
	if (!p4d)
		goto out;

	pud = pud_alloc(mm, p4d, proc);
	if (!pud)
		goto out_pud;

	pmd = pmd_alloc(mm, pud, proc);
	if (!pmd)
		goto out_pmd;

	pte = pte_alloc_map(mm, pmd, proc);
	if (!pte)
		goto out_pte;

	*pte = mk_pte(virt_to_page(kernel), __pgprot(_PAGE_PRESENT));
	*pte = pte_mkread(*pte);
	return 0;

 out_pte:
	pmd_free(mm, pmd);
 out_pmd:
	pud_free(mm, pud);
 out_pud:
	p4d_free(mm, p4d);
 out:
	return -ENOMEM;
}

int init_new_context(struct task_struct *task, struct mm_struct *mm)
{
 	struct mm_context *from_mm = NULL;
	struct mm_context *to_mm = &mm->context;
	unsigned long stack = 0;
	int ret = -ENOMEM;

	stack = get_zeroed_page(GFP_KERNEL);
	if (stack == 0)
		goto out;

	to_mm->id.stack = stack;
	if (current->mm != NULL && current->mm != &init_mm)
		from_mm = &current->mm->context;

	block_signals_trace();
	if (from_mm)
		to_mm->id.u.pid = copy_context_skas0(stack,
						     from_mm->id.u.pid);
	else to_mm->id.u.pid = start_userspace(stack);
	unblock_signals_trace();

	if (to_mm->id.u.pid < 0) {
		ret = to_mm->id.u.pid;
		goto out_free;
	}

	ret = init_new_ldt(to_mm, from_mm);
	if (ret < 0) {
		printk(KERN_ERR "init_new_context_skas - init_ldt"
		       " failed, errno = %d\n", ret);
		goto out_free;
	}

	return 0;

 out_free:
	if (to_mm->id.stack != 0)
		free_page(to_mm->id.stack);
 out:
	return ret;
}

void uml_setup_stubs(struct mm_struct *mm)
{
	int err, ret;

	ret = init_stub_pte(mm, STUB_CODE,
			    (unsigned long) __syscall_stub_start);
	if (ret)
		goto out;

	ret = init_stub_pte(mm, STUB_DATA, mm->context.id.stack);
	if (ret)
		goto out;

	mm->context.stub_pages[0] = virt_to_page(__syscall_stub_start);
	mm->context.stub_pages[1] = virt_to_page(mm->context.id.stack);

	/* dup_mmap already holds mmap_lock */
	err = install_special_mapping(mm, STUB_START, STUB_END - STUB_START,
				      VM_READ | VM_MAYREAD | VM_EXEC |
				      VM_MAYEXEC | VM_DONTCOPY | VM_PFNMAP,
				      mm->context.stub_pages);
	if (err) {
		printk(KERN_ERR "install_special_mapping returned %d\n", err);
		goto out;
	}
	return;

out:
	force_sigsegv(SIGSEGV);
}

void arch_exit_mmap(struct mm_struct *mm)
{
	pte_t *pte;

	pte = virt_to_pte(mm, STUB_CODE);
	if (pte != NULL)
		pte_clear(mm, STUB_CODE, pte);

	pte = virt_to_pte(mm, STUB_DATA);
	if (pte == NULL)
		return;

	pte_clear(mm, STUB_DATA, pte);
}

void destroy_context(struct mm_struct *mm)
{
	struct mm_context *mmu = &mm->context;

	/*
	 * If init_new_context wasn't called, this will be
	 * zero, resulting in a kill(0), which will result in the
	 * whole UML suddenly dying.  Also, cover negative and
	 * 1 cases, since they shouldn't happen either.
	 */
	if (mmu->id.u.pid < 2) {
		printk(KERN_ERR "corrupt mm_context - pid = %d\n",
		       mmu->id.u.pid);
		return;
	}
	os_kill_ptraced_process(mmu->id.u.pid, 1);

	free_page(mmu->id.stack);
	free_ldt(mmu);
}
