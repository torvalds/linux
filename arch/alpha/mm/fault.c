// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/arch/alpha/mm/fault.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

#include <linux/sched/signal.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/io.h>

#define __EXTERN_INLINE inline
#include <asm/mmu_context.h>
#include <asm/tlbflush.h>
#undef  __EXTERN_INLINE

#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/extable.h>
#include <linux/uaccess.h>
#include <linux/perf_event.h>

extern void die_if_kernel(char *,struct pt_regs *,long, unsigned long *);


/*
 * Force a new ASN for a task.
 */

#ifndef CONFIG_SMP
unsigned long last_asn = ASN_FIRST_VERSION;
#endif

void
__load_new_mm_context(struct mm_struct *next_mm)
{
	unsigned long mmc;
	struct pcb_struct *pcb;

	mmc = __get_new_mm_context(next_mm, smp_processor_id());
	next_mm->context[smp_processor_id()] = mmc;

	pcb = &current_thread_info()->pcb;
	pcb->asn = mmc & HARDWARE_ASN_MASK;
	pcb->ptbr = ((unsigned long) next_mm->pgd - IDENT_ADDR) >> PAGE_SHIFT;

	__reload_thread(pcb);
}


/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to handle_mm_fault().
 *
 * mmcsr:
 *	0 = translation not valid
 *	1 = access violation
 *	2 = fault-on-read
 *	3 = fault-on-execute
 *	4 = fault-on-write
 *
 * cause:
 *	-1 = instruction fetch
 *	0 = load
 *	1 = store
 *
 * Registers $9 through $15 are saved in a block just prior to `regs' and
 * are saved and restored around the call to allow exception code to
 * modify them.
 */

/* Macro for exception fixup code to access integer registers.  */
#define dpf_reg(r)							\
	(((unsigned long *)regs)[(r) <= 8 ? (r) : (r) <= 15 ? (r)-16 :	\
				 (r) <= 18 ? (r)+10 : (r)-10])

asmlinkage void
do_page_fault(unsigned long address, unsigned long mmcsr,
	      long cause, struct pt_regs *regs)
{
	struct vm_area_struct * vma;
	struct mm_struct *mm = current->mm;
	const struct exception_table_entry *fixup;
	int si_code = SEGV_MAPERR;
	vm_fault_t fault;
	unsigned int flags = FAULT_FLAG_DEFAULT;

	/* As of EV6, a load into $31/$f31 is a prefetch, and never faults
	   (or is suppressed by the PALcode).  Support that for older CPUs
	   by ignoring such an instruction.  */
	if (cause == 0) {
		unsigned int insn;
		__get_user(insn, (unsigned int __user *)regs->pc);
		if ((insn >> 21 & 0x1f) == 0x1f &&
		    /* ldq ldl ldt lds ldg ldf ldwu ldbu */
		    (1ul << (insn >> 26) & 0x30f00001400ul)) {
			regs->pc += 4;
			return;
		}
	}

	/* If we're in an interrupt context, or have no user context,
	   we must not take the fault.  */
	if (!mm || faulthandler_disabled())
		goto no_context;

#ifdef CONFIG_ALPHA_LARGE_VMALLOC
	if (address >= TASK_SIZE)
		goto vmalloc_fault;
#endif
	if (user_mode(regs))
		flags |= FAULT_FLAG_USER;
	perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS, 1, regs, address);
retry:
	mmap_read_lock(mm);
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
	   we can handle it.  */
 good_area:
	si_code = SEGV_ACCERR;
	if (cause < 0) {
		if (!(vma->vm_flags & VM_EXEC))
			goto bad_area;
	} else if (!cause) {
		/* Allow reads even for write-only mappings */
		if (!(vma->vm_flags & (VM_READ | VM_WRITE)))
			goto bad_area;
	} else {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
		flags |= FAULT_FLAG_WRITE;
	}

	/* If for any reason at all we couldn't handle the fault,
	   make sure we exit gracefully rather than endlessly redo
	   the fault.  */
	fault = handle_mm_fault(vma, address, flags, regs);

	if (fault_signal_pending(fault, regs)) {
		if (!user_mode(regs))
			goto no_context;
		return;
	}

	/* The fault is fully completed (including releasing mmap lock) */
	if (fault & VM_FAULT_COMPLETED)
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

	if (fault & VM_FAULT_RETRY) {
		flags |= FAULT_FLAG_TRIED;

		/* No need to mmap_read_unlock(mm) as we would
		 * have already released it in __lock_page_or_retry
		 * in mm/filemap.c.
		 */

		goto retry;
	}

	mmap_read_unlock(mm);

	return;

	/* Something tried to access memory that isn't in our memory map.
	   Fix it, but check if it's kernel or user first.  */
 bad_area:
	mmap_read_unlock(mm);

	if (user_mode(regs))
		goto do_sigsegv;

 no_context:
	/* Are we prepared to handle this fault as an exception?  */
	if ((fixup = search_exception_tables(regs->pc)) != 0) {
		unsigned long newpc;
		newpc = fixup_exception(dpf_reg, fixup, regs->pc);
		regs->pc = newpc;
		return;
	}

	/* Oops. The kernel tried to access some bad page. We'll have to
	   terminate things with extreme prejudice.  */
	printk(KERN_ALERT "Unable to handle kernel paging request at "
	       "virtual address %016lx\n", address);
	die_if_kernel("Oops", regs, cause, (unsigned long*)regs - 16);
	make_task_dead(SIGKILL);

	/* We ran out of memory, or some other thing happened to us that
	   made us unable to handle the page fault gracefully.  */
 out_of_memory:
	mmap_read_unlock(mm);
	if (!user_mode(regs))
		goto no_context;
	pagefault_out_of_memory();
	return;

 do_sigbus:
	mmap_read_unlock(mm);
	/* Send a sigbus, regardless of whether we were in kernel
	   or user mode.  */
	force_sig_fault(SIGBUS, BUS_ADRERR, (void __user *) address);
	if (!user_mode(regs))
		goto no_context;
	return;

 do_sigsegv:
	force_sig_fault(SIGSEGV, si_code, (void __user *) address);
	return;

#ifdef CONFIG_ALPHA_LARGE_VMALLOC
 vmalloc_fault:
	if (user_mode(regs))
		goto do_sigsegv;
	else {
		/* Synchronize this task's top level page-table
		   with the "reference" page table from init.  */
		long index = pgd_index(address);
		pgd_t *pgd, *pgd_k;

		pgd = current->active_mm->pgd + index;
		pgd_k = swapper_pg_dir + index;
		if (!pgd_present(*pgd) && pgd_present(*pgd_k)) {
			pgd_val(*pgd) = pgd_val(*pgd_k);
			return;
		}
		goto no_context;
	}
#endif
}
