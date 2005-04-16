/* $Id: fault.c,v 1.14 2004/01/13 05:52:11 kkojima Exp $
 *
 *  linux/arch/sh/mm/fault.c
 *  Copyright (C) 1999  Niibe Yutaka
 *  Copyright (C) 2003  Paul Mundt
 *
 *  Based on linux/arch/i386/mm/fault.c:
 *   Copyright (C) 1995  Linus Torvalds
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/module.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>
#include <asm/kgdb.h>

extern void die(const char *,struct pt_regs *,long);

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 */
asmlinkage void do_page_fault(struct pt_regs *regs, unsigned long writeaccess,
			      unsigned long address)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	struct vm_area_struct * vma;
	unsigned long page;

#ifdef CONFIG_SH_KGDB
	if (kgdb_nofault && kgdb_bus_err_hook)
		kgdb_bus_err_hook();
#endif

	tsk = current;
	mm = tsk->mm;

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
	if (writeaccess) {
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

	if (user_mode(regs)) {
		tsk->thread.address = address;
		tsk->thread.error_code = writeaccess;
		force_sig(SIGSEGV, tsk);
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
	if (current->pid == 1) {
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
	tsk->thread.address = address;
	tsk->thread.error_code = writeaccess;
	tsk->thread.trap_no = 14;
	force_sig(SIGBUS, tsk);

	/* Kernel mode? Handle exceptions or die */
	if (!user_mode(regs))
		goto no_context;
}

/*
 * Called with interrupt disabled.
 */
asmlinkage int __do_page_fault(struct pt_regs *regs, unsigned long writeaccess,
			       unsigned long address)
{
	unsigned long addrmax = P4SEG;
	pgd_t *dir;
	pmd_t *pmd;
	pte_t *pte;
	pte_t entry;

#ifdef CONFIG_SH_KGDB
	if (kgdb_nofault && kgdb_bus_err_hook)
		kgdb_bus_err_hook();
#endif

#ifdef CONFIG_SH_STORE_QUEUES
	addrmax = P4SEG_STORE_QUE + 0x04000000;
#endif

	if (address >= P3SEG && address < addrmax)
		dir = pgd_offset_k(address);
	else if (address >= TASK_SIZE)
		return 1;
	else if (!current->mm)
		return 1;
	else
		dir = pgd_offset(current->mm, address);

	pmd = pmd_offset(dir, address);
	if (pmd_none(*pmd))
		return 1;
	if (pmd_bad(*pmd)) {
		pmd_ERROR(*pmd);
		pmd_clear(pmd);
		return 1;
	}
	pte = pte_offset_kernel(pmd, address);
	entry = *pte;
	if (pte_none(entry) || pte_not_present(entry)
	    || (writeaccess && !pte_write(entry)))
		return 1;

	if (writeaccess)
		entry = pte_mkdirty(entry);
	entry = pte_mkyoung(entry);

#ifdef CONFIG_CPU_SH4
	/*
	 * ITLB is not affected by "ldtlb" instruction.
	 * So, we need to flush the entry by ourselves.
	 */

	{
		unsigned long flags;
		local_irq_save(flags);
		__flush_tlb_page(get_asid(), address&PAGE_MASK);
		local_irq_restore(flags);
	}
#endif

	set_pte(pte, entry);
	update_mmu_cache(NULL, address, entry);

	return 0;
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	if (vma->vm_mm && vma->vm_mm->context != NO_CONTEXT) {
		unsigned long flags;
		unsigned long asid;
		unsigned long saved_asid = MMU_NO_ASID;

		asid = vma->vm_mm->context & MMU_CONTEXT_ASID_MASK;
		page &= PAGE_MASK;

		local_irq_save(flags);
		if (vma->vm_mm != current->mm) {
			saved_asid = get_asid();
			set_asid(asid);
		}
		__flush_tlb_page(asid, page);
		if (saved_asid != MMU_NO_ASID)
			set_asid(saved_asid);
		local_irq_restore(flags);
	}
}

void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		     unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;

	if (mm->context != NO_CONTEXT) {
		unsigned long flags;
		int size;

		local_irq_save(flags);
		size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		if (size > (MMU_NTLB_ENTRIES/4)) { /* Too many TLB to flush */
			mm->context = NO_CONTEXT;
			if (mm == current->mm)
				activate_context(mm);
		} else {
			unsigned long asid = mm->context&MMU_CONTEXT_ASID_MASK;
			unsigned long saved_asid = MMU_NO_ASID;

			start &= PAGE_MASK;
			end += (PAGE_SIZE - 1);
			end &= PAGE_MASK;
			if (mm != current->mm) {
				saved_asid = get_asid();
				set_asid(asid);
			}
			while (start < end) {
				__flush_tlb_page(asid, start);
				start += PAGE_SIZE;
			}
			if (saved_asid != MMU_NO_ASID)
				set_asid(saved_asid);
		}
		local_irq_restore(flags);
	}
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	unsigned long flags;
	int size;

	local_irq_save(flags);
	size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
	if (size > (MMU_NTLB_ENTRIES/4)) { /* Too many TLB to flush */
		flush_tlb_all();
	} else {
		unsigned long asid = init_mm.context&MMU_CONTEXT_ASID_MASK;
		unsigned long saved_asid = get_asid();

		start &= PAGE_MASK;
		end += (PAGE_SIZE - 1);
		end &= PAGE_MASK;
		set_asid(asid);
		while (start < end) {
			__flush_tlb_page(asid, start);
			start += PAGE_SIZE;
		}
		set_asid(saved_asid);
	}
	local_irq_restore(flags);
}

void flush_tlb_mm(struct mm_struct *mm)
{
	/* Invalidate all TLB of this process. */
	/* Instead of invalidating each TLB, we get new MMU context. */
	if (mm->context != NO_CONTEXT) {
		unsigned long flags;

		local_irq_save(flags);
		mm->context = NO_CONTEXT;
		if (mm == current->mm)
			activate_context(mm);
		local_irq_restore(flags);
	}
}

void flush_tlb_all(void)
{
	unsigned long flags, status;

	/*
	 * Flush all the TLB.
	 *
	 * Write to the MMU control register's bit:
	 * 	TF-bit for SH-3, TI-bit for SH-4.
	 *      It's same position, bit #2.
	 */
	local_irq_save(flags);
	status = ctrl_inl(MMUCR);
	status |= 0x04;		
	ctrl_outl(status, MMUCR);
	local_irq_restore(flags);
}
