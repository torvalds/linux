/*
 * arch/sh/mm/tlb-flush_64.c
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003  Richard Curnow (/proc/tlb, bug fixes)
 * Copyright (C) 2003 - 2009 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/signal.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/perf_event.h>
#include <linux/interrupt.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/tlb.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>

extern void die(const char *,struct pt_regs *,long);

#define PFLAG(val,flag)   (( (val) & (flag) ) ? #flag : "" )
#define PPROT(flag) PFLAG(pgprot_val(prot),flag)

static inline void print_prots(pgprot_t prot)
{
	printk("prot is 0x%08lx\n",pgprot_val(prot));

	printk("%s %s %s %s %s\n",PPROT(_PAGE_SHARED),PPROT(_PAGE_READ),
	       PPROT(_PAGE_EXECUTE),PPROT(_PAGE_WRITE),PPROT(_PAGE_USER));
}

static inline void print_vma(struct vm_area_struct *vma)
{
	printk("vma start 0x%08lx\n", vma->vm_start);
	printk("vma end   0x%08lx\n", vma->vm_end);

	print_prots(vma->vm_page_prot);
	printk("vm_flags 0x%08lx\n", vma->vm_flags);
}

static inline void print_task(struct task_struct *tsk)
{
	printk("Task pid %d\n", task_pid_nr(tsk));
}

static pte_t *lookup_pte(struct mm_struct *mm, unsigned long address)
{
	pgd_t *dir;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	pte_t entry;

	dir = pgd_offset(mm, address);
	if (pgd_none(*dir))
		return NULL;

	pud = pud_offset(dir, address);
	if (pud_none(*pud))
		return NULL;

	pmd = pmd_offset(pud, address);
	if (pmd_none(*pmd))
		return NULL;

	pte = pte_offset_kernel(pmd, address);
	entry = *pte;
	if (pte_none(entry) || !pte_present(entry))
		return NULL;

	return pte;
}

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 */
asmlinkage void do_page_fault(struct pt_regs *regs, unsigned long writeaccess,
			      unsigned long textaccess, unsigned long address)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	struct vm_area_struct * vma;
	const struct exception_table_entry *fixup;
	pte_t *pte;
	int fault;

	/* SIM
	 * Note this is now called with interrupts still disabled
	 * This is to cope with being called for a missing IO port
	 * address with interrupts disabled. This should be fixed as
	 * soon as we have a better 'fast path' miss handler.
	 *
	 * Plus take care how you try and debug this stuff.
	 * For example, writing debug data to a port which you
	 * have just faulted on is not going to work.
	 */

	tsk = current;
	mm = tsk->mm;

	/* Not an IO address, so reenable interrupts */
	local_irq_enable();

	perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS, 1, 0, regs, address);

	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
	if (in_atomic() || !mm)
		goto no_context;

	/* TLB misses upon some cache flushes get done under cli() */
	down_read(&mm->mmap_sem);

	vma = find_vma(mm, address);

	if (!vma) {
#ifdef DEBUG_FAULT
		print_task(tsk);
		printk("%s:%d fault, address is 0x%08x PC %016Lx textaccess %d writeaccess %d\n",
		       __func__, __LINE__,
		       address,regs->pc,textaccess,writeaccess);
		show_regs(regs);
#endif
		goto bad_area;
	}
	if (vma->vm_start <= address) {
		goto good_area;
	}

	if (!(vma->vm_flags & VM_GROWSDOWN)) {
#ifdef DEBUG_FAULT
		print_task(tsk);
		printk("%s:%d fault, address is 0x%08x PC %016Lx textaccess %d writeaccess %d\n",
		       __func__, __LINE__,
		       address,regs->pc,textaccess,writeaccess);
		show_regs(regs);

		print_vma(vma);
#endif
		goto bad_area;
	}
	if (expand_stack(vma, address)) {
#ifdef DEBUG_FAULT
		print_task(tsk);
		printk("%s:%d fault, address is 0x%08x PC %016Lx textaccess %d writeaccess %d\n",
		       __func__, __LINE__,
		       address,regs->pc,textaccess,writeaccess);
		show_regs(regs);
#endif
		goto bad_area;
	}
/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
	if (textaccess) {
		if (!(vma->vm_flags & VM_EXEC))
			goto bad_area;
	} else {
		if (writeaccess) {
			if (!(vma->vm_flags & VM_WRITE))
				goto bad_area;
		} else {
			if (!(vma->vm_flags & VM_READ))
				goto bad_area;
		}
	}

	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
survive:
	fault = handle_mm_fault(mm, vma, address, writeaccess ? FAULT_FLAG_WRITE : 0);
	if (unlikely(fault & VM_FAULT_ERROR)) {
		if (fault & VM_FAULT_OOM)
			goto out_of_memory;
		else if (fault & VM_FAULT_SIGBUS)
			goto do_sigbus;
		BUG();
	}

	if (fault & VM_FAULT_MAJOR) {
		tsk->maj_flt++;
		perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MAJ, 1, 0,
				     regs, address);
	} else {
		tsk->min_flt++;
		perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MIN, 1, 0,
				     regs, address);
	}

	/* If we get here, the page fault has been handled.  Do the TLB refill
	   now from the newly-setup PTE, to avoid having to fault again right
	   away on the same instruction. */
	pte = lookup_pte (mm, address);
	if (!pte) {
		/* From empirical evidence, we can get here, due to
		   !pte_present(pte).  (e.g. if a swap-in occurs, and the page
		   is swapped back out again before the process that wanted it
		   gets rescheduled?) */
		goto no_pte;
	}

	__do_tlb_refill(address, textaccess, pte);

no_pte:

	up_read(&mm->mmap_sem);
	return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
#ifdef DEBUG_FAULT
	printk("fault:bad area\n");
#endif
	up_read(&mm->mmap_sem);

	if (user_mode(regs)) {
		static int count=0;
		siginfo_t info;
		if (count < 4) {
			/* This is really to help debug faults when starting
			 * usermode, so only need a few */
			count++;
			printk("user mode bad_area address=%08lx pid=%d (%s) pc=%08lx\n",
				address, task_pid_nr(current), current->comm,
				(unsigned long) regs->pc);
#if 0
			show_regs(regs);
#endif
		}
		if (is_global_init(tsk)) {
			panic("INIT had user mode bad_area\n");
		}
		tsk->thread.address = address;
		tsk->thread.error_code = writeaccess;
		info.si_signo = SIGSEGV;
		info.si_errno = 0;
		info.si_addr = (void *) address;
		force_sig_info(SIGSEGV, &info, tsk);
		return;
	}

no_context:
#ifdef DEBUG_FAULT
	printk("fault:No context\n");
#endif
	/* Are we prepared to handle this kernel fault?  */
	fixup = search_exception_tables(regs->pc);
	if (fixup) {
		regs->pc = fixup->fixup;
		return;
	}

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
	printk(KERN_ALERT "pc = %08Lx%08Lx\n", regs->pc >> 32, regs->pc & 0xffffffff);
	die("Oops", regs, writeaccess);
	do_exit(SIGKILL);

/*
 * We ran out of memory, or some other thing happened to us that made
 * us unable to handle the page fault gracefully.
 */
out_of_memory:
	if (is_global_init(current)) {
		panic("INIT out of memory\n");
		yield();
		goto survive;
	}
	printk("fault:Out of memory\n");
	up_read(&mm->mmap_sem);
	if (is_global_init(current)) {
		yield();
		down_read(&mm->mmap_sem);
		goto survive;
	}
	printk("VM: killing process %s\n", tsk->comm);
	if (user_mode(regs))
		do_group_exit(SIGKILL);
	goto no_context;

do_sigbus:
	printk("fault:Do sigbus\n");
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

void local_flush_tlb_one(unsigned long asid, unsigned long page)
{
	unsigned long long match, pteh=0, lpage;
	unsigned long tlb;

	/*
	 * Sign-extend based on neff.
	 */
	lpage = neff_sign_extend(page);
	match = (asid << PTEH_ASID_SHIFT) | PTEH_VALID;
	match |= lpage;

	for_each_itlb_entry(tlb) {
		asm volatile ("getcfg	%1, 0, %0"
			      : "=r" (pteh)
			      : "r" (tlb) );

		if (pteh == match) {
			__flush_tlb_slot(tlb);
			break;
		}
	}

	for_each_dtlb_entry(tlb) {
		asm volatile ("getcfg	%1, 0, %0"
			      : "=r" (pteh)
			      : "r" (tlb) );

		if (pteh == match) {
			__flush_tlb_slot(tlb);
			break;
		}

	}
}

void local_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	unsigned long flags;

	if (vma->vm_mm) {
		page &= PAGE_MASK;
		local_irq_save(flags);
		local_flush_tlb_one(get_asid(), page);
		local_irq_restore(flags);
	}
}

void local_flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
			   unsigned long end)
{
	unsigned long flags;
	unsigned long long match, pteh=0, pteh_epn, pteh_low;
	unsigned long tlb;
	unsigned int cpu = smp_processor_id();
	struct mm_struct *mm;

	mm = vma->vm_mm;
	if (cpu_context(cpu, mm) == NO_CONTEXT)
		return;

	local_irq_save(flags);

	start &= PAGE_MASK;
	end &= PAGE_MASK;

	match = (cpu_asid(cpu, mm) << PTEH_ASID_SHIFT) | PTEH_VALID;

	/* Flush ITLB */
	for_each_itlb_entry(tlb) {
		asm volatile ("getcfg	%1, 0, %0"
			      : "=r" (pteh)
			      : "r" (tlb) );

		pteh_epn = pteh & PAGE_MASK;
		pteh_low = pteh & ~PAGE_MASK;

		if (pteh_low == match && pteh_epn >= start && pteh_epn <= end)
			__flush_tlb_slot(tlb);
	}

	/* Flush DTLB */
	for_each_dtlb_entry(tlb) {
		asm volatile ("getcfg	%1, 0, %0"
			      : "=r" (pteh)
			      : "r" (tlb) );

		pteh_epn = pteh & PAGE_MASK;
		pteh_low = pteh & ~PAGE_MASK;

		if (pteh_low == match && pteh_epn >= start && pteh_epn <= end)
			__flush_tlb_slot(tlb);
	}

	local_irq_restore(flags);
}

void local_flush_tlb_mm(struct mm_struct *mm)
{
	unsigned long flags;
	unsigned int cpu = smp_processor_id();

	if (cpu_context(cpu, mm) == NO_CONTEXT)
		return;

	local_irq_save(flags);

	cpu_context(cpu, mm) = NO_CONTEXT;
	if (mm == current->mm)
		activate_context(mm, cpu);

	local_irq_restore(flags);
}

void local_flush_tlb_all(void)
{
	/* Invalidate all, including shared pages, excluding fixed TLBs */
	unsigned long flags, tlb;

	local_irq_save(flags);

	/* Flush each ITLB entry */
	for_each_itlb_entry(tlb)
		__flush_tlb_slot(tlb);

	/* Flush each DTLB entry */
	for_each_dtlb_entry(tlb)
		__flush_tlb_slot(tlb);

	local_irq_restore(flags);
}

void local_flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
        /* FIXME: Optimize this later.. */
        flush_tlb_all();
}

void __update_tlb(struct vm_area_struct *vma, unsigned long address, pte_t pte)
{
}
