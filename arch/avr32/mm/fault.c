/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * Based on linux/arch/sh/mm/fault.c:
 *   Copyright (C) 1999  Niibe Yutaka
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pagemap.h>

#include <asm/kdebug.h>
#include <asm/mmu_context.h>
#include <asm/sysreg.h>
#include <asm/uaccess.h>
#include <asm/tlb.h>

#ifdef DEBUG
static void dump_code(unsigned long pc)
{
	char *p = (char *)pc;
	char val;
	int i;


	printk(KERN_DEBUG "Code:");
	for (i = 0; i < 16; i++) {
		if (__get_user(val, p + i))
			break;
		printk(" %02x", val);
	}
	printk("\n");
}
#endif

#ifdef CONFIG_KPROBES
ATOMIC_NOTIFIER_HEAD(notify_page_fault_chain);

/* Hook to register for page fault notifications */
int register_page_fault_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&notify_page_fault_chain, nb);
}

int unregister_page_fault_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&notify_page_fault_chain, nb);
}

static inline int notify_page_fault(enum die_val val, struct pt_regs *regs,
				    int trap, int sig)
{
	struct die_args args = {
		.regs = regs,
		.trapnr = trap,
	};
	return atomic_notifier_call_chain(&notify_page_fault_chain, val, &args);
}
#else
static inline int notify_page_fault(enum die_val val, struct pt_regs *regs,
				    int trap, int sig)
{
	return NOTIFY_DONE;
}
#endif

/*
 * This routine handles page faults. It determines the address and the
 * problem, and then passes it off to one of the appropriate routines.
 *
 * ecr is the Exception Cause Register. Possible values are:
 *   5:  Page not found (instruction access)
 *   6:  Protection fault (instruction access)
 *   12: Page not found (read access)
 *   13: Page not found (write access)
 *   14: Protection fault (read access)
 *   15: Protection fault (write access)
 */
asmlinkage void do_page_fault(unsigned long ecr, struct pt_regs *regs)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	const struct exception_table_entry *fixup;
	unsigned long address;
	unsigned long page;
	int writeaccess = 0;

	if (notify_page_fault(DIE_PAGE_FAULT, regs,
			      ecr, SIGSEGV) == NOTIFY_STOP)
		return;

	address = sysreg_read(TLBEAR);

	tsk = current;
	mm = tsk->mm;

	/*
	 * If we're in an interrupt or have no user context, we must
	 * not take the fault...
	 */
	if (in_atomic() || !mm || regs->sr & SYSREG_BIT(GM))
		goto no_context;

	local_irq_enable();

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

	/*
	 * Ok, we have a good vm_area for this memory access, so we
	 * can handle it...
	 */
good_area:
	//pr_debug("good area: vm_flags = 0x%lx\n", vma->vm_flags);
	switch (ecr) {
	case ECR_PROTECTION_X:
	case ECR_TLB_MISS_X:
		if (!(vma->vm_flags & VM_EXEC))
			goto bad_area;
		break;
	case ECR_PROTECTION_R:
	case ECR_TLB_MISS_R:
		if (!(vma->vm_flags & (VM_READ | VM_WRITE | VM_EXEC)))
			goto bad_area;
		break;
	case ECR_PROTECTION_W:
	case ECR_TLB_MISS_W:
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
		writeaccess = 1;
		break;
	default:
		panic("Unhandled case %lu in do_page_fault!", ecr);
	}

	/*
	 * If for any reason at all we couldn't handle the fault, make
	 * sure we exit gracefully rather than endlessly redo the
	 * fault.
	 */
survive:
	switch (handle_mm_fault(mm, vma, address, writeaccess)) {
	case VM_FAULT_MINOR:
		tsk->min_flt++;
		break;
	case VM_FAULT_MAJOR:
		tsk->maj_flt++;
		break;
	case VM_FAULT_SIGBUS:
		goto do_sigbus;
	case VM_FAULT_OOM:
		goto out_of_memory;
	default:
		BUG();
	}

	up_read(&mm->mmap_sem);
	return;

	/*
	 * Something tried to access memory that isn't in our memory
	 * map. Fix it, but check if it's kernel or user first...
	 */
bad_area:
	pr_debug("Bad area [%s:%u]: addr %08lx, ecr %lu\n",
		 tsk->comm, tsk->pid, address, ecr);

	up_read(&mm->mmap_sem);

	if (user_mode(regs)) {
		/* Hmm...we have to pass address and ecr somehow... */
		/* tsk->thread.address = address;
		   tsk->thread.error_code = ecr; */
#ifdef DEBUG
		show_regs(regs);
		dump_code(regs->pc);

		page = sysreg_read(PTBR);
		printk("ptbr = %08lx", page);
		if (page) {
			page = ((unsigned long *)page)[address >> 22];
			printk(" pgd = %08lx", page);
			if (page & _PAGE_PRESENT) {
				page &= PAGE_MASK;
				address &= 0x003ff000;
				page = ((unsigned long *)__va(page))[address >> PAGE_SHIFT];
				printk(" pte = %08lx\n", page);
			}
		}
#endif
		pr_debug("Sending SIGSEGV to PID %d...\n",
			tsk->pid);
		force_sig(SIGSEGV, tsk);
		return;
	}

no_context:
	pr_debug("No context\n");

	/* Are we prepared to handle this kernel fault? */
	fixup = search_exception_tables(regs->pc);
	if (fixup) {
		regs->pc = fixup->fixup;
		pr_debug("Found fixup at %08lx\n", fixup->fixup);
		return;
	}

	/*
	 * Oops. The kernel tried to access some bad page. We'll have
	 * to terminate things with extreme prejudice.
	 */
	if (address < PAGE_SIZE)
		printk(KERN_ALERT
		       "Unable to handle kernel NULL pointer dereference");
	else
		printk(KERN_ALERT
		       "Unable to handle kernel paging request");
	printk(" at virtual address %08lx\n", address);
	printk(KERN_ALERT "pc = %08lx\n", regs->pc);

	page = sysreg_read(PTBR);
	printk(KERN_ALERT "ptbr = %08lx", page);
	if (page) {
		page = ((unsigned long *)page)[address >> 22];
		printk(" pgd = %08lx", page);
		if (page & _PAGE_PRESENT) {
			page &= PAGE_MASK;
			address &= 0x003ff000;
			page = ((unsigned long *)__va(page))[address >> PAGE_SHIFT];
			printk(" pte = %08lx\n", page);
		}
	}
	die("\nOops", regs, ecr);
	do_exit(SIGKILL);

	/*
	 * We ran out of memory, or some other thing happened to us
	 * that made us unable to handle the page fault gracefully.
	 */
out_of_memory:
	printk("Out of memory\n");
	up_read(&mm->mmap_sem);
	if (current->pid == 1) {
		yield();
		down_read(&mm->mmap_sem);
		goto survive;
	}
	printk("VM: Killing process %s\n", tsk->comm);
	if (user_mode(regs))
		do_exit(SIGKILL);
	goto no_context;

do_sigbus:
	up_read(&mm->mmap_sem);

	/*
	 * Send a sigbus, regardless of whether we were in kernel or
	 * user mode.
	 */
	/* address, error_code, trap_no, ... */
#ifdef DEBUG
	show_regs(regs);
	dump_code(regs->pc);
#endif
	pr_debug("Sending SIGBUS to PID %d...\n", tsk->pid);
	force_sig(SIGBUS, tsk);

	/* Kernel mode? Handle exceptions or die */
	if (!user_mode(regs))
		goto no_context;
}

asmlinkage void do_bus_error(unsigned long addr, int write_access,
			     struct pt_regs *regs)
{
	printk(KERN_ALERT
	       "Bus error at physical address 0x%08lx (%s access)\n",
	       addr, write_access ? "write" : "read");
	printk(KERN_INFO "DTLB dump:\n");
	dump_dtlb();
	die("Bus Error", regs, write_access);
	do_exit(SIGKILL);
}

/*
 * This functionality is currently not possible to implement because
 * we're using segmentation to ensure a fixed mapping of the kernel
 * virtual address space.
 *
 * It would be possible to implement this, but it would require us to
 * disable segmentation at startup and load the kernel mappings into
 * the TLB like any other pages. There will be lots of trickery to
 * avoid recursive invocation of the TLB miss handler, though...
 */
#ifdef CONFIG_DEBUG_PAGEALLOC
void kernel_map_pages(struct page *page, int numpages, int enable)
{

}
EXPORT_SYMBOL(kernel_map_pages);
#endif
