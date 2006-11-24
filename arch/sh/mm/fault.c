/*
 * Page fault handler for SH with an MMU.
 *
 *  Copyright (C) 1999  Niibe Yutaka
 *  Copyright (C) 2003  Paul Mundt
 *
 *  Based on linux/arch/i386/mm/fault.c:
 *   Copyright (C) 1995  Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/hardirq.h>
#include <linux/kprobes.h>
#include <asm/system.h>
#include <asm/mmu_context.h>
#include <asm/kgdb.h>

extern void die(const char *,struct pt_regs *,long);

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 */
asmlinkage void __kprobes do_page_fault(struct pt_regs *regs,
					unsigned long writeaccess,
					unsigned long address)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	struct vm_area_struct * vma;
	unsigned long page;
	int si_code;
	siginfo_t info;

#ifdef CONFIG_SH_KGDB
	if (kgdb_nofault && kgdb_bus_err_hook)
		kgdb_bus_err_hook();
#endif

	tsk = current;
	mm = tsk->mm;
	si_code = SEGV_MAPERR;

	if (unlikely(address >= TASK_SIZE)) {
		/*
		 * Synchronize this task's top level page-table
		 * with the 'reference' page table.
		 *
		 * Do _not_ use "tsk" here. We might be inside
		 * an interrupt in the middle of a task switch..
		 */
		int offset = pgd_index(address);
		pgd_t *pgd, *pgd_k;
		pud_t *pud, *pud_k;
		pmd_t *pmd, *pmd_k;

		pgd = get_TTB() + offset;
		pgd_k = swapper_pg_dir + offset;

		/* This will never happen with the folded page table. */
		if (!pgd_present(*pgd)) {
			if (!pgd_present(*pgd_k))
				goto bad_area_nosemaphore;
			set_pgd(pgd, *pgd_k);
			return;
		}

		pud = pud_offset(pgd, address);
		pud_k = pud_offset(pgd_k, address);
		if (pud_present(*pud) || !pud_present(*pud_k))
			goto bad_area_nosemaphore;
		set_pud(pud, *pud_k);

		pmd = pmd_offset(pud, address);
		pmd_k = pmd_offset(pud_k, address);
		if (pmd_present(*pmd) || !pmd_present(*pmd_k))
			goto bad_area_nosemaphore;
		set_pmd(pmd, *pmd_k);

		return;
	}

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
	if (expand_stack(vma, address))
		goto bad_area;
/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
	si_code = SEGV_ACCERR;
	if (writeaccess) {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	} else {
		if (!(vma->vm_flags & (VM_READ | VM_EXEC | VM_WRITE)))
			goto bad_area;
	}

	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
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
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
	up_read(&mm->mmap_sem);

bad_area_nosemaphore:
	if (user_mode(regs)) {
		info.si_signo = SIGSEGV;
		info.si_errno = 0;
		info.si_code = si_code;
		info.si_addr = (void *) address;
		force_sig_info(SIGSEGV, &info, tsk);
		return;
	}

no_context:
	/* Are we prepared to handle this kernel fault?  */
	if (fixup_exception(regs))
		return;

/*
 * Oops. The kernel tried to access some bad page. We'll have to
 * terminate things with extreme prejudice.
 *
 */
	if (address < PAGE_SIZE)
		printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
	else
		printk(KERN_ALERT "Unable to handle kernel paging request");
	printk(" at virtual address %08lx\n", address);
	printk(KERN_ALERT "pc = %08lx\n", regs->pc);
	asm volatile("mov.l	%1, %0"
		     : "=r" (page)
		     : "m" (__m(MMU_TTB)));
	if (page) {
		page = ((unsigned long *) page)[address >> 22];
		printk(KERN_ALERT "*pde = %08lx\n", page);
		if (page & _PAGE_PRESENT) {
			page &= PAGE_MASK;
			address &= 0x003ff000;
			page = ((unsigned long *) __va(page))[address >> PAGE_SHIFT];
			printk(KERN_ALERT "*pte = %08lx\n", page);
		}
	}
	die("Oops", regs, writeaccess);
	do_exit(SIGKILL);

/*
 * We ran out of memory, or some other thing happened to us that made
 * us unable to handle the page fault gracefully.
 */
out_of_memory:
	up_read(&mm->mmap_sem);
	if (is_init(current)) {
		yield();
		down_read(&mm->mmap_sem);
		goto survive;
	}
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
}
