// SPDX-License-Identifier: GPL-2.0
/*
 * MMU fault handling support.
 *
 * Copyright (C) 1998-2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */
#include <linux/sched/signal.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/extable.h>
#include <linux/interrupt.h>
#include <linux/kprobes.h>
#include <linux/kdebug.h>
#include <linux/prefetch.h>
#include <linux/uaccess.h>

#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/exception.h>

extern int die(char *, struct pt_regs *, long);

#ifdef CONFIG_KPROBES
static inline int notify_page_fault(struct pt_regs *regs, int trap)
{
	int ret = 0;

	if (!user_mode(regs)) {
		/* kprobe_running() needs smp_processor_id() */
		preempt_disable();
		if (kprobe_running() && kprobe_fault_handler(regs, trap))
			ret = 1;
		preempt_enable();
	}

	return ret;
}
#else
static inline int notify_page_fault(struct pt_regs *regs, int trap)
{
	return 0;
}
#endif

/*
 * Return TRUE if ADDRESS points at a page in the kernel's mapped segment
 * (inside region 5, on ia64) and that page is present.
 */
static int
mapped_kernel_page_is_present (unsigned long address)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep, pte;

	pgd = pgd_offset_k(address);
	if (pgd_none(*pgd) || pgd_bad(*pgd))
		return 0;

	pud = pud_offset(pgd, address);
	if (pud_none(*pud) || pud_bad(*pud))
		return 0;

	pmd = pmd_offset(pud, address);
	if (pmd_none(*pmd) || pmd_bad(*pmd))
		return 0;

	ptep = pte_offset_kernel(pmd, address);
	if (!ptep)
		return 0;

	pte = *ptep;
	return pte_present(pte);
}

#	define VM_READ_BIT	0
#	define VM_WRITE_BIT	1
#	define VM_EXEC_BIT	2

void __kprobes
ia64_do_page_fault (unsigned long address, unsigned long isr, struct pt_regs *regs)
{
	int signal = SIGSEGV, code = SEGV_MAPERR;
	struct vm_area_struct *vma, *prev_vma;
	struct mm_struct *mm = current->mm;
	struct siginfo si;
	unsigned long mask;
	int fault;
	unsigned int flags = FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_KILLABLE;

	mask = ((((isr >> IA64_ISR_X_BIT) & 1UL) << VM_EXEC_BIT)
		| (((isr >> IA64_ISR_W_BIT) & 1UL) << VM_WRITE_BIT));

	/* mmap_sem is performance critical.... */
	prefetchw(&mm->mmap_sem);

	/*
	 * If we're in an interrupt or have no user context, we must not take the fault..
	 */
	if (faulthandler_disabled() || !mm)
		goto no_context;

#ifdef CONFIG_VIRTUAL_MEM_MAP
	/*
	 * If fault is in region 5 and we are in the kernel, we may already
	 * have the mmap_sem (pfn_valid macro is called during mmap). There
	 * is no vma for region 5 addr's anyway, so skip getting the semaphore
	 * and go directly to the exception handling code.
	 */

	if ((REGION_NUMBER(address) == 5) && !user_mode(regs))
		goto bad_area_no_up;
#endif

	/*
	 * This is to handle the kprobes on user space access instructions
	 */
	if (notify_page_fault(regs, TRAP_BRKPT))
		return;

	if (user_mode(regs))
		flags |= FAULT_FLAG_USER;
	if (mask & VM_WRITE)
		flags |= FAULT_FLAG_WRITE;
retry:
	down_read(&mm->mmap_sem);

	vma = find_vma_prev(mm, address, &prev_vma);
	if (!vma && !prev_vma )
		goto bad_area;

        /*
         * find_vma_prev() returns vma such that address < vma->vm_end or NULL
         *
         * May find no vma, but could be that the last vm area is the
         * register backing store that needs to expand upwards, in
         * this case vma will be null, but prev_vma will ne non-null
         */
        if (( !vma && prev_vma ) || (address < vma->vm_start) )
		goto check_expansion;

  good_area:
	code = SEGV_ACCERR;

	/* OK, we've got a good vm_area for this memory area.  Check the access permissions: */

#	if (((1 << VM_READ_BIT) != VM_READ || (1 << VM_WRITE_BIT) != VM_WRITE) \
	    || (1 << VM_EXEC_BIT) != VM_EXEC)
#		error File is out of sync with <linux/mm.h>.  Please update.
#	endif

	if (((isr >> IA64_ISR_R_BIT) & 1UL) && (!(vma->vm_flags & (VM_READ | VM_WRITE))))
		goto bad_area;

	if ((vma->vm_flags & mask) != mask)
		goto bad_area;

	/*
	 * If for any reason at all we couldn't handle the fault, make
	 * sure we exit gracefully rather than endlessly redo the
	 * fault.
	 */
	fault = handle_mm_fault(vma, address, flags);

	if ((fault & VM_FAULT_RETRY) && fatal_signal_pending(current))
		return;

	if (unlikely(fault & VM_FAULT_ERROR)) {
		/*
		 * We ran out of memory, or some other thing happened
		 * to us that made us unable to handle the page fault
		 * gracefully.
		 */
		if (fault & VM_FAULT_OOM) {
			goto out_of_memory;
		} else if (fault & VM_FAULT_SIGSEGV) {
			goto bad_area;
		} else if (fault & VM_FAULT_SIGBUS) {
			signal = SIGBUS;
			goto bad_area;
		}
		BUG();
	}

	if (flags & FAULT_FLAG_ALLOW_RETRY) {
		if (fault & VM_FAULT_MAJOR)
			current->maj_flt++;
		else
			current->min_flt++;
		if (fault & VM_FAULT_RETRY) {
			flags &= ~FAULT_FLAG_ALLOW_RETRY;
			flags |= FAULT_FLAG_TRIED;

			 /* No need to up_read(&mm->mmap_sem) as we would
			 * have already released it in __lock_page_or_retry
			 * in mm/filemap.c.
			 */

			goto retry;
		}
	}

	up_read(&mm->mmap_sem);
	return;

  check_expansion:
	if (!(prev_vma && (prev_vma->vm_flags & VM_GROWSUP) && (address == prev_vma->vm_end))) {
		if (!vma)
			goto bad_area;
		if (!(vma->vm_flags & VM_GROWSDOWN))
			goto bad_area;
		if (REGION_NUMBER(address) != REGION_NUMBER(vma->vm_start)
		    || REGION_OFFSET(address) >= RGN_MAP_LIMIT)
			goto bad_area;
		if (expand_stack(vma, address))
			goto bad_area;
	} else {
		vma = prev_vma;
		if (REGION_NUMBER(address) != REGION_NUMBER(vma->vm_start)
		    || REGION_OFFSET(address) >= RGN_MAP_LIMIT)
			goto bad_area;
		/*
		 * Since the register backing store is accessed sequentially,
		 * we disallow growing it by more than a page at a time.
		 */
		if (address > vma->vm_end + PAGE_SIZE - sizeof(long))
			goto bad_area;
		if (expand_upwards(vma, address))
			goto bad_area;
	}
	goto good_area;

  bad_area:
	up_read(&mm->mmap_sem);
#ifdef CONFIG_VIRTUAL_MEM_MAP
  bad_area_no_up:
#endif
	if ((isr & IA64_ISR_SP)
	    || ((isr & IA64_ISR_NA) && (isr & IA64_ISR_CODE_MASK) == IA64_ISR_CODE_LFETCH))
	{
		/*
		 * This fault was due to a speculative load or lfetch.fault, set the "ed"
		 * bit in the psr to ensure forward progress.  (Target register will get a
		 * NaT for ld.s, lfetch will be canceled.)
		 */
		ia64_psr(regs)->ed = 1;
		return;
	}
	if (user_mode(regs)) {
		si.si_signo = signal;
		si.si_errno = 0;
		si.si_code = code;
		si.si_addr = (void __user *) address;
		si.si_isr = isr;
		si.si_flags = __ISR_VALID;
		force_sig_info(signal, &si, current);
		return;
	}

  no_context:
	if ((isr & IA64_ISR_SP)
	    || ((isr & IA64_ISR_NA) && (isr & IA64_ISR_CODE_MASK) == IA64_ISR_CODE_LFETCH))
	{
		/*
		 * This fault was due to a speculative load or lfetch.fault, set the "ed"
		 * bit in the psr to ensure forward progress.  (Target register will get a
		 * NaT for ld.s, lfetch will be canceled.)
		 */
		ia64_psr(regs)->ed = 1;
		return;
	}

	/*
	 * Since we have no vma's for region 5, we might get here even if the address is
	 * valid, due to the VHPT walker inserting a non present translation that becomes
	 * stale. If that happens, the non present fault handler already purged the stale
	 * translation, which fixed the problem. So, we check to see if the translation is
	 * valid, and return if it is.
	 */
	if (REGION_NUMBER(address) == 5 && mapped_kernel_page_is_present(address))
		return;

	if (ia64_done_with_exception(regs))
		return;

	/*
	 * Oops. The kernel tried to access some bad page. We'll have to terminate things
	 * with extreme prejudice.
	 */
	bust_spinlocks(1);

	if (address < PAGE_SIZE)
		printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference (address %016lx)\n", address);
	else
		printk(KERN_ALERT "Unable to handle kernel paging request at "
		       "virtual address %016lx\n", address);
	if (die("Oops", regs, isr))
		regs = NULL;
	bust_spinlocks(0);
	if (regs)
		do_exit(SIGKILL);
	return;

  out_of_memory:
	up_read(&mm->mmap_sem);
	if (!user_mode(regs))
		goto no_context;
	pagefault_out_of_memory();
}
