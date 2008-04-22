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
#include <linux/kdebug.h>
#include <linux/kprobes.h>

#include <asm/mmu_context.h>
#include <asm/sysreg.h>
#include <asm/tlb.h>
#include <asm/uaccess.h>

#ifdef CONFIG_KPROBES
static inline int notify_page_fault(struct pt_regs *regs, int trap)
{
	int ret = 0;

	if (!user_mode(regs)) {
		if (kprobe_running() && kprobe_fault_handler(regs, trap))
			ret = 1;
	}

	return ret;
}
#else
static inline int notify_page_fault(struct pt_regs *regs, int trap)
{
	return 0;
}
#endif

int exception_trace = 1;

/*
 * This routine handles page faults. It determines the address and the
 * problem, and then passes it off to one of the appropriate routines.
 *
 * ecr is the Exception Cause Register. Possible values are:
 *   6:  Protection fault (instruction access)
 *   15: Protection fault (read access)
 *   16: Protection fault (write access)
 *   20: Page not found (instruction access)
 *   24: Page not found (read access)
 *   28: Page not found (write access)
 */
asmlinkage void do_page_fault(unsigned long ecr, struct pt_regs *regs)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	const struct exception_table_entry *fixup;
	unsigned long address;
	unsigned long page;
	int writeaccess;
	long signr;
	int code;
	int fault;

	if (notify_page_fault(regs, ecr))
		return;

	address = sysreg_read(TLBEAR);

	tsk = current;
	mm = tsk->mm;

	signr = SIGSEGV;
	code = SEGV_MAPERR;

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
	code = SEGV_ACCERR;
	writeaccess = 0;

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
	fault = handle_mm_fault(mm, vma, address, writeaccess);
	if (unlikely(fault & VM_FAULT_ERROR)) {
		if (fault & VM_FAULT_OOM)
			goto out_of_memory;
		else if (fault & VM_FAULT_SIGBUS)
			goto do_sigbus;
		BUG();
	}
	if (fault & VM_FAULT_MAJOR)
		tsk->maj_flt++;
	else
		tsk->min_flt++;

	up_read(&mm->mmap_sem);
	return;

	/*
	 * Something tried to access memory that isn't in our memory
	 * map. Fix it, but check if it's kernel or user first...
	 */
bad_area:
	up_read(&mm->mmap_sem);

	if (user_mode(regs)) {
		if (exception_trace && printk_ratelimit())
			printk("%s%s[%d]: segfault at %08lx pc %08lx "
			       "sp %08lx ecr %lu\n",
			       is_global_init(tsk) ? KERN_EMERG : KERN_INFO,
			       tsk->comm, tsk->pid, address, regs->pc,
			       regs->sp, ecr);
		_exception(SIGSEGV, regs, code, address);
		return;
	}

no_context:
	/* Are we prepared to handle this kernel fault? */
	fixup = search_exception_tables(regs->pc);
	if (fixup) {
		regs->pc = fixup->fixup;
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

	page = sysreg_read(PTBR);
	printk(KERN_ALERT "ptbr = %08lx", page);
	if (address >= TASK_SIZE)
		page = (unsigned long)swapper_pg_dir;
	if (page) {
		page = ((unsigned long *)page)[address >> 22];
		printk(" pgd = %08lx", page);
		if (page & _PAGE_PRESENT) {
			page &= PAGE_MASK;
			address &= 0x003ff000;
			page = ((unsigned long *)__va(page))[address >> PAGE_SHIFT];
			printk(" pte = %08lx", page);
		}
	}
	printk("\n");
	die("Kernel access of bad area", regs, signr);
	return;

	/*
	 * We ran out of memory, or some other thing happened to us
	 * that made us unable to handle the page fault gracefully.
	 */
out_of_memory:
	up_read(&mm->mmap_sem);
	if (is_global_init(current)) {
		yield();
		down_read(&mm->mmap_sem);
		goto survive;
	}
	printk("VM: Killing process %s\n", tsk->comm);
	if (user_mode(regs))
		do_group_exit(SIGKILL);
	goto no_context;

do_sigbus:
	up_read(&mm->mmap_sem);

	/* Kernel mode? Handle exceptions or die */
	signr = SIGBUS;
	code = BUS_ADRERR;
	if (!user_mode(regs))
		goto no_context;

	if (exception_trace)
		printk("%s%s[%d]: bus error at %08lx pc %08lx "
		       "sp %08lx ecr %lu\n",
		       is_global_init(tsk) ? KERN_EMERG : KERN_INFO,
		       tsk->comm, tsk->pid, address, regs->pc,
		       regs->sp, ecr);

	_exception(SIGBUS, regs, BUS_ADRERR, address);
}

asmlinkage void do_bus_error(unsigned long addr, int write_access,
			     struct pt_regs *regs)
{
	printk(KERN_ALERT
	       "Bus error at physical address 0x%08lx (%s access)\n",
	       addr, write_access ? "write" : "read");
	printk(KERN_INFO "DTLB dump:\n");
	dump_dtlb();
	die("Bus Error", regs, SIGKILL);
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
