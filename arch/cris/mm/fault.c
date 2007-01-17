/*
 *  linux/arch/cris/mm/fault.c
 *
 *  Copyright (C) 2000, 2001  Axis Communications AB
 *
 *  Authors:  Bjorn Wesen 
 * 
 *  $Log: fault.c,v $
 *  Revision 1.20  2005/03/04 08:16:18  starvik
 *  Merge of Linux 2.6.11.
 *
 *  Revision 1.19  2005/01/14 10:07:59  starvik
 *  Fixed warning.
 *
 *  Revision 1.18  2005/01/12 08:10:14  starvik
 *  Readded the change of frametype when handling kernel page fault fixup
 *  for v10. This is necessary to avoid that the CPU remakes the faulting
 *  access.
 *
 *  Revision 1.17  2005/01/11 13:53:05  starvik
 *  Use raw_printk.
 *
 *  Revision 1.16  2004/12/17 11:39:41  starvik
 *  SMP support.
 *
 *  Revision 1.15  2004/11/23 18:36:18  starvik
 *  Stack is now non-executable.
 *  Signal handler trampolines are placed in a reserved page mapped into all
 *  processes.
 *
 *  Revision 1.14  2004/11/23 07:10:21  starvik
 *  Moved find_fixup_code to generic code.
 *
 *  Revision 1.13  2004/11/23 07:00:54  starvik
 *  Actually use the execute permission bit in the MMU. This makes it possible
 *  to prevent e.g. attacks where executable code is put on the stack.
 *
 *  Revision 1.12  2004/09/29 06:16:04  starvik
 *  Use instruction_pointer
 *
 *  Revision 1.11  2004/05/14 07:58:05  starvik
 *  Merge of changes from 2.4
 *
 *  Revision 1.10  2003/10/27 14:51:24  starvik
 *  Removed debugcode
 *
 *  Revision 1.9  2003/10/27 14:50:42  starvik
 *  Changed do_page_fault signature
 *
 *  Revision 1.8  2003/07/04 13:02:48  tobiasa
 *  Moved code snippet from arch/cris/mm/fault.c that searches for fixup code
 *  to seperate function in arch-specific files.
 *
 *  Revision 1.7  2003/01/22 06:48:38  starvik
 *  Fixed warnings issued by GCC 3.2.1
 *
 *  Revision 1.6  2003/01/09 14:42:52  starvik
 *  Merge of Linux 2.5.55
 *
 *  Revision 1.5  2002/12/11 14:44:48  starvik
 *  Extracted v10 (ETRAX 100LX) specific stuff to arch/cris/arch-v10/mm
 *
 *  Revision 1.4  2002/11/13 15:10:28  starvik
 *  pte_offset has been renamed to pte_offset_kernel
 *
 *  Revision 1.3  2002/11/05 06:45:13  starvik
 *  Merge of Linux 2.5.45
 *
 *  Revision 1.2  2001/12/18 13:35:22  bjornw
 *  Applied the 2.4.13->2.4.16 CRIS patch to 2.5.1 (is a copy of 2.4.15).
 *
 *  Revision 1.20  2001/11/22 13:34:06  bjornw
 *  * Bug workaround (LX TR89): force a rerun of the whole of an interrupted
 *    unaligned write, because the second half of the write will be corrupted
 *    otherwise. Affected unaligned writes spanning not-yet mapped pages.
 *  * Optimization: use the wr_rd bit in R_MMU_CAUSE to know whether a miss
 *    was due to a read or a write (before we didn't know this until the next
 *    restart of the interrupted instruction, thus wasting one fault-irq)
 *
 *  Revision 1.19  2001/11/12 19:02:10  pkj
 *  Fixed compiler warnings.
 *
 *  Revision 1.18  2001/07/18 22:14:32  bjornw
 *  Enable interrupts in the bulk of do_page_fault
 *
 *  Revision 1.17  2001/07/18 13:07:23  bjornw
 *  * Detect non-existant PTE's in vmalloc pmd synchronization
 *  * Remove comment about fast-paths for VMALLOC_START etc, because all that
 *    was totally bogus anyway it turned out :)
 *  * Fix detection of vmalloc-area synchronization
 *  * Add some comments
 *
 *  Revision 1.16  2001/06/13 00:06:08  bjornw
 *  current_pgd should be volatile
 *
 *  Revision 1.15  2001/06/13 00:02:23  bjornw
 *  Use a separate variable to store the current pgd to avoid races in schedule
 *
 *  Revision 1.14  2001/05/16 17:41:07  hp
 *  Last comment tweak further tweaked.
 *
 *  Revision 1.13  2001/05/15 00:58:44  hp
 *  Expand a bit on the comment why we compare address >= TASK_SIZE rather
 *  than >= VMALLOC_START.
 *
 *  Revision 1.12  2001/04/04 10:51:14  bjornw
 *  mmap_sem is grabbed for reading
 *
 *  Revision 1.11  2001/03/23 07:36:07  starvik
 *  Corrected according to review remarks
 *
 *  Revision 1.10  2001/03/21 16:10:11  bjornw
 *  CRIS_FRAME_FIXUP not needed anymore, use FRAME_NORMAL
 *
 *  Revision 1.9  2001/03/05 13:22:20  bjornw
 *  Spell-fix and fix in vmalloc_fault handling
 *
 *  Revision 1.8  2000/11/22 14:45:31  bjornw
 *  * 2.4.0-test10 removed the set_pgdir instantaneous kernel global mapping
 *    into all processes. Instead we fill in the missing PTE entries on demand.
 *
 *  Revision 1.7  2000/11/21 16:39:09  bjornw
 *  fixup switches frametype
 *
 *  Revision 1.6  2000/11/17 16:54:08  bjornw
 *  More detailed siginfo reporting
 *
 *
 */

#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <asm/uaccess.h>

extern int find_fixup_code(struct pt_regs *);
extern void die_if_kernel(const char *, struct pt_regs *, long);
extern int raw_printk(const char *fmt, ...);

/* debug of low-level TLB reload */
#undef DEBUG

#ifdef DEBUG
#define D(x) x
#else
#define D(x)
#endif

/* debug of higher-level faults */
#define DPG(x)

/* current active page directory */

volatile DEFINE_PER_CPU(pgd_t *,current_pgd);
unsigned long cris_signal_return_page;

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 *
 * Notice that the address we're given is aligned to the page the fault
 * occurred in, since we only get the PFN in R_MMU_CAUSE not the complete
 * address.
 *
 * error_code:
 *	bit 0 == 0 means no page found, 1 means protection fault
 *	bit 1 == 0 means read, 1 means write
 *
 * If this routine detects a bad access, it returns 1, otherwise it
 * returns 0.
 */

asmlinkage void
do_page_fault(unsigned long address, struct pt_regs *regs,
	      int protection, int writeaccess)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	struct vm_area_struct * vma;
	siginfo_t info;

        D(printk("Page fault for %lX on %X at %lX, prot %d write %d\n",
                 address, smp_processor_id(), instruction_pointer(regs),
                 protection, writeaccess));

	tsk = current;

	/*
	 * We fault-in kernel-space virtual memory on-demand. The
	 * 'reference' page table is init_mm.pgd.
	 *
	 * NOTE! We MUST NOT take any locks for this case. We may
	 * be in an interrupt or a critical region, and should
	 * only copy the information from the master page table,
	 * nothing more.
	 *
	 * NOTE2: This is done so that, when updating the vmalloc
	 * mappings we don't have to walk all processes pgdirs and
	 * add the high mappings all at once. Instead we do it as they
	 * are used. However vmalloc'ed page entries have the PAGE_GLOBAL
	 * bit set so sometimes the TLB can use a lingering entry.
	 *
	 * This verifies that the fault happens in kernel space
	 * and that the fault was not a protection error (error_code & 1).
	 */

	if (address >= VMALLOC_START &&
	    !protection &&
	    !user_mode(regs))
		goto vmalloc_fault;

	/* When stack execution is not allowed we store the signal
	 * trampolines in the reserved cris_signal_return_page.
	 * Handle this in the exact same way as vmalloc (we know
	 * that the mapping is there and is valid so no need to
	 * call handle_mm_fault).
	 */
	if (cris_signal_return_page &&
	    address == cris_signal_return_page &&
	    !protection && user_mode(regs))
		goto vmalloc_fault;

	/* we can and should enable interrupts at this point */
	local_irq_enable();

	mm = tsk->mm;
	info.si_code = SEGV_MAPERR;

	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */

	if (in_atomic() || !mm)
		goto no_context;

	down_read(&mm->mmap_sem);
	vma = find_vma(mm, address);
	if (!vma)
		goto bad_area;
	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (user_mode(regs)) {
		/*
		 * accessing the stack below usp is always a bug.
		 * we get page-aligned addresses so we can only check
		 * if we're within a page from usp, but that might be
		 * enough to catch brutal errors at least.
		 */
		if (address + PAGE_SIZE < rdusp())
			goto bad_area;
	}
	if (expand_stack(vma, address))
		goto bad_area;

	/*
	 * Ok, we have a good vm_area for this memory access, so
	 * we can handle it..
	 */

 good_area:
	info.si_code = SEGV_ACCERR;

	/* first do some preliminary protection checks */

	if (writeaccess == 2){
		if (!(vma->vm_flags & VM_EXEC))
			goto bad_area;
	} else if (writeaccess == 1) {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	} else {
		if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
			goto bad_area;
	}

	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */

	switch (handle_mm_fault(mm, vma, address, writeaccess & 1)) {
	case VM_FAULT_MINOR:
		tsk->min_flt++;
		break;
	case VM_FAULT_MAJOR:
		tsk->maj_flt++;
		break;
	case VM_FAULT_SIGBUS:
		goto do_sigbus;
	default:
		goto out_of_memory;
	}

	up_read(&mm->mmap_sem);
	return;

	/*
	 * Something tried to access memory that isn't in our memory map..
	 * Fix it, but check if it's kernel or user first..
	 */

 bad_area:
	up_read(&mm->mmap_sem);

 bad_area_nosemaphore:
	DPG(show_registers(regs));

	/* User mode accesses just cause a SIGSEGV */

	if (user_mode(regs)) {
		info.si_signo = SIGSEGV;
		info.si_errno = 0;
		/* info.si_code has been set above */
		info.si_addr = (void *)address;
		force_sig_info(SIGSEGV, &info, tsk);
		return;
	}

 no_context:

	/* Are we prepared to handle this kernel fault?
	 *
	 * (The kernel has valid exception-points in the source 
	 *  when it acesses user-memory. When it fails in one
	 *  of those points, we find it in a table and do a jump
	 *  to some fixup code that loads an appropriate error
	 *  code)
	 */

	if (find_fixup_code(regs))
		return;

	/*
	 * Oops. The kernel tried to access some bad page. We'll have to
	 * terminate things with extreme prejudice.
	 */

	if ((unsigned long) (address) < PAGE_SIZE)
		raw_printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
	else
		raw_printk(KERN_ALERT "Unable to handle kernel access");
	raw_printk(" at virtual address %08lx\n",address);

	die_if_kernel("Oops", regs, (writeaccess << 1) | protection);

	do_exit(SIGKILL);

	/*
	 * We ran out of memory, or some other thing happened to us that made
	 * us unable to handle the page fault gracefully.
	 */

 out_of_memory:
	up_read(&mm->mmap_sem);
	printk("VM: killing process %s\n", tsk->comm);
	if (user_mode(regs))
		do_exit(SIGKILL);
	goto no_context;

 do_sigbus:
	up_read(&mm->mmap_sem);

	/*
	 * Send a sigbus, regardless of whether we were in kernel
	 * or user mode.
	 */
	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_ADRERR;
	info.si_addr = (void *)address;
	force_sig_info(SIGBUS, &info, tsk);

	/* Kernel mode? Handle exceptions or die */
	if (!user_mode(regs))
		goto no_context;
	return;

vmalloc_fault:
	{
		/*
		 * Synchronize this task's top level page-table
		 * with the 'reference' page table.
		 *
		 * Use current_pgd instead of tsk->active_mm->pgd
		 * since the latter might be unavailable if this
		 * code is executed in a misfortunately run irq
		 * (like inside schedule() between switch_mm and
		 *  switch_to...).
		 */

		int offset = pgd_index(address);
		pgd_t *pgd, *pgd_k;
		pud_t *pud, *pud_k;
		pmd_t *pmd, *pmd_k;
		pte_t *pte_k;

		pgd = (pgd_t *)per_cpu(current_pgd, smp_processor_id()) + offset;
		pgd_k = init_mm.pgd + offset;

		/* Since we're two-level, we don't need to do both
		 * set_pgd and set_pmd (they do the same thing). If
		 * we go three-level at some point, do the right thing
		 * with pgd_present and set_pgd here. 
		 * 
		 * Also, since the vmalloc area is global, we don't
		 * need to copy individual PTE's, it is enough to
		 * copy the pgd pointer into the pte page of the
		 * root task. If that is there, we'll find our pte if
		 * it exists.
		 */

		pud = pud_offset(pgd, address);
		pud_k = pud_offset(pgd_k, address);
		if (!pud_present(*pud_k))
			goto no_context;

		pmd = pmd_offset(pud, address);
		pmd_k = pmd_offset(pud_k, address);

		if (!pmd_present(*pmd_k))
			goto bad_area_nosemaphore;

		set_pmd(pmd, *pmd_k);

		/* Make sure the actual PTE exists as well to
		 * catch kernel vmalloc-area accesses to non-mapped
		 * addresses. If we don't do this, this will just
		 * silently loop forever.
		 */

		pte_k = pte_offset_kernel(pmd_k, address);
		if (!pte_present(*pte_k))
			goto no_context;

		return;
	}
}

/* Find fixup code. */
int
find_fixup_code(struct pt_regs *regs)
{
	const struct exception_table_entry *fixup;

	if ((fixup = search_exception_tables(instruction_pointer(regs))) != 0) {
		/* Adjust the instruction pointer in the stackframe. */
		instruction_pointer(regs) = fixup->fixup;
		arch_fixup(regs);
		return 1;
	}

	return 0;
}
