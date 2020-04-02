// TODO VM_EXEC flag work-around, cache aliasing
/*
 * arch/xtensa/mm/fault.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2010 Tensilica Inc.
 *
 * Chris Zankel <chris@zankel.net>
 * Joe Taylor	<joe@tensilica.com, joetylr@yahoo.com>
 */

#include <linux/mm.h>
#include <linux/extable.h>
#include <linux/hardirq.h>
#include <linux/perf_event.h>
#include <linux/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>
#include <asm/hardirq.h>
#include <asm/pgalloc.h>

DEFINE_PER_CPU(unsigned long, asid_cache) = ASID_USER_FIRST;
void bad_page_fault(struct pt_regs*, unsigned long, int);

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 *
 * Note: does not handle Miss and MultiHit.
 */

void do_page_fault(struct pt_regs *regs)
{
	struct vm_area_struct * vma;
	struct mm_struct *mm = current->mm;
	unsigned int exccause = regs->exccause;
	unsigned int address = regs->excvaddr;
	int code;

	int is_write, is_exec;
	vm_fault_t fault;
	unsigned int flags = FAULT_FLAG_DEFAULT;

	code = SEGV_MAPERR;

	/* We fault-in kernel-space virtual memory on-demand. The
	 * 'reference' page table is init_mm.pgd.
	 */
	if (address >= TASK_SIZE && !user_mode(regs))
		goto vmalloc_fault;

	/* If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
	if (faulthandler_disabled() || !mm) {
		bad_page_fault(regs, address, SIGSEGV);
		return;
	}

	is_write = (exccause == EXCCAUSE_STORE_CACHE_ATTRIBUTE) ? 1 : 0;
	is_exec =  (exccause == EXCCAUSE_ITLB_PRIVILEGE ||
		    exccause == EXCCAUSE_ITLB_MISS ||
		    exccause == EXCCAUSE_FETCH_CACHE_ATTRIBUTE) ? 1 : 0;

	pr_debug("[%s:%d:%08x:%d:%08lx:%s%s]\n",
		 current->comm, current->pid,
		 address, exccause, regs->pc,
		 is_write ? "w" : "", is_exec ? "x" : "");

	if (user_mode(regs))
		flags |= FAULT_FLAG_USER;
retry:
	down_read(&mm->mmap_sem);
	vma = find_vma(mm, address);

	if (!vma)
		goto bad_area;
	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (expand_stack(vma, address))
		goto bad_area;

	/* Ok, we have a good vm_area for this memory access, so
	 * we can handle it..
	 */

good_area:
	code = SEGV_ACCERR;

	if (is_write) {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
		flags |= FAULT_FLAG_WRITE;
	} else if (is_exec) {
		if (!(vma->vm_flags & VM_EXEC))
			goto bad_area;
	} else	/* Allow read even from write-only pages. */
		if (!(vma->vm_flags & (VM_READ | VM_WRITE)))
			goto bad_area;

	/* If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	fault = handle_mm_fault(vma, address, flags);

	if (fault_signal_pending(fault, regs))
		return;

	if (unlikely(fault & VM_FAULT_ERROR)) {
		if (fault & VM_FAULT_OOM)
			goto out_of_memory;
		else if (fault & VM_FAULT_SIGSEGV)
			goto bad_area;
		else if (fault & VM_FAULT_SIGBUS)
			goto do_sigbus;
		BUG();
	}
	if (flags & FAULT_FLAG_ALLOW_RETRY) {
		if (fault & VM_FAULT_MAJOR)
			current->maj_flt++;
		else
			current->min_flt++;
		if (fault & VM_FAULT_RETRY) {
			flags |= FAULT_FLAG_TRIED;

			 /* No need to up_read(&mm->mmap_sem) as we would
			 * have already released it in __lock_page_or_retry
			 * in mm/filemap.c.
			 */

			goto retry;
		}
	}

	up_read(&mm->mmap_sem);
	perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS, 1, regs, address);
	if (flags & VM_FAULT_MAJOR)
		perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MAJ, 1, regs, address);
	else
		perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MIN, 1, regs, address);

	return;

	/* Something tried to access memory that isn't in our memory map..
	 * Fix it, but check if it's kernel or user first..
	 */
bad_area:
	up_read(&mm->mmap_sem);
	if (user_mode(regs)) {
		current->thread.bad_vaddr = address;
		current->thread.error_code = is_write;
		force_sig_fault(SIGSEGV, code, (void *) address);
		return;
	}
	bad_page_fault(regs, address, SIGSEGV);
	return;


	/* We ran out of memory, or some other thing happened to us that made
	 * us unable to handle the page fault gracefully.
	 */
out_of_memory:
	up_read(&mm->mmap_sem);
	if (!user_mode(regs))
		bad_page_fault(regs, address, SIGKILL);
	else
		pagefault_out_of_memory();
	return;

do_sigbus:
	up_read(&mm->mmap_sem);

	/* Send a sigbus, regardless of whether we were in kernel
	 * or user mode.
	 */
	current->thread.bad_vaddr = address;
	force_sig_fault(SIGBUS, BUS_ADRERR, (void *) address);

	/* Kernel mode? Handle exceptions or die */
	if (!user_mode(regs))
		bad_page_fault(regs, address, SIGBUS);
	return;

vmalloc_fault:
	{
		/* Synchronize this task's top level page-table
		 * with the 'reference' page table.
		 */
		struct mm_struct *act_mm = current->active_mm;
		int index = pgd_index(address);
		pgd_t *pgd, *pgd_k;
		p4d_t *p4d, *p4d_k;
		pud_t *pud, *pud_k;
		pmd_t *pmd, *pmd_k;
		pte_t *pte_k;

		if (act_mm == NULL)
			goto bad_page_fault;

		pgd = act_mm->pgd + index;
		pgd_k = init_mm.pgd + index;

		if (!pgd_present(*pgd_k))
			goto bad_page_fault;

		pgd_val(*pgd) = pgd_val(*pgd_k);

		p4d = p4d_offset(pgd, address);
		p4d_k = p4d_offset(pgd_k, address);
		if (!p4d_present(*p4d) || !p4d_present(*p4d_k))
			goto bad_page_fault;

		pud = pud_offset(p4d, address);
		pud_k = pud_offset(p4d_k, address);
		if (!pud_present(*pud) || !pud_present(*pud_k))
			goto bad_page_fault;

		pmd = pmd_offset(pud, address);
		pmd_k = pmd_offset(pud_k, address);
		if (!pmd_present(*pmd) || !pmd_present(*pmd_k))
			goto bad_page_fault;

		pmd_val(*pmd) = pmd_val(*pmd_k);
		pte_k = pte_offset_kernel(pmd_k, address);

		if (!pte_present(*pte_k))
			goto bad_page_fault;
		return;
	}
bad_page_fault:
	bad_page_fault(regs, address, SIGKILL);
	return;
}


void
bad_page_fault(struct pt_regs *regs, unsigned long address, int sig)
{
	extern void die(const char*, struct pt_regs*, long);
	const struct exception_table_entry *entry;

	/* Are we prepared to handle this kernel fault?  */
	if ((entry = search_exception_tables(regs->pc)) != NULL) {
		pr_debug("%s: Exception at pc=%#010lx (%lx)\n",
			 current->comm, regs->pc, entry->fixup);
		current->thread.bad_uaddr = address;
		regs->pc = entry->fixup;
		return;
	}

	/* Oops. The kernel tried to access some bad page. We'll have to
	 * terminate things with extreme prejudice.
	 */
	pr_alert("Unable to handle kernel paging request at virtual "
		 "address %08lx\n pc = %08lx, ra = %08lx\n",
		 address, regs->pc, regs->areg[0]);
	die("Oops", regs, sig);
	do_exit(sig);
}
