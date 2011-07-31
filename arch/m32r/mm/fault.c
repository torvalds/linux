/*
 *  linux/arch/m32r/mm/fault.c
 *
 *  Copyright (c) 2001, 2002  Hitoshi Yamamoto, and H. Kondo
 *  Copyright (c) 2004  Naoto Sugai, NIIBE Yutaka
 *
 *  Some code taken from i386 version.
 *    Copyright (C) 1995  Linus Torvalds
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
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/vt_kern.h>		/* For unblank_screen() */
#include <linux/highmem.h>
#include <linux/module.h>

#include <asm/m32r.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/hardirq.h>
#include <asm/mmu_context.h>
#include <asm/tlbflush.h>

extern void die(const char *, struct pt_regs *, long);

#ifndef CONFIG_SMP
asmlinkage unsigned int tlb_entry_i_dat;
asmlinkage unsigned int tlb_entry_d_dat;
#define tlb_entry_i tlb_entry_i_dat
#define tlb_entry_d tlb_entry_d_dat
#else
unsigned int tlb_entry_i_dat[NR_CPUS];
unsigned int tlb_entry_d_dat[NR_CPUS];
#define tlb_entry_i tlb_entry_i_dat[smp_processor_id()]
#define tlb_entry_d tlb_entry_d_dat[smp_processor_id()]
#endif

extern void init_tlb(void);

/*======================================================================*
 * do_page_fault()
 *======================================================================*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 *
 * ARGUMENT:
 *  regs       : M32R SP reg.
 *  error_code : See below
 *  address    : M32R MMU MDEVA reg. (Operand ACE)
 *             : M32R BPC reg. (Instruction ACE)
 *
 * error_code :
 *  bit 0 == 0 means no page found, 1 means protection fault
 *  bit 1 == 0 means read, 1 means write
 *  bit 2 == 0 means kernel, 1 means user-mode
 *  bit 3 == 0 means data, 1 means instruction
 *======================================================================*/
#define ACE_PROTECTION		1
#define ACE_WRITE		2
#define ACE_USERMODE		4
#define ACE_INSTRUCTION		8

asmlinkage void do_page_fault(struct pt_regs *regs, unsigned long error_code,
  unsigned long address)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	struct vm_area_struct * vma;
	unsigned long page, addr;
	int write;
	int fault;
	siginfo_t info;

	/*
	 * If BPSW IE bit enable --> set PSW IE bit
	 */
	if (regs->psw & M32R_PSW_BIE)
		local_irq_enable();

	tsk = current;

	info.si_code = SEGV_MAPERR;

	/*
	 * We fault-in kernel-space virtual memory on-demand. The
	 * 'reference' page table is init_mm.pgd.
	 *
	 * NOTE! We MUST NOT take any locks for this case. We may
	 * be in an interrupt or a critical region, and should
	 * only copy the information from the master page table,
	 * nothing more.
	 *
	 * This verifies that the fault happens in kernel space
	 * (error_code & ACE_USERMODE) == 0, and that the fault was not a
	 * protection error (error_code & ACE_PROTECTION) == 0.
	 */
	if (address >= TASK_SIZE && !(error_code & ACE_USERMODE))
		goto vmalloc_fault;

	mm = tsk->mm;

	/*
	 * If we're in an interrupt or have no user context or are running in an
	 * atomic region then we must not take the fault..
	 */
	if (in_atomic() || !mm)
		goto bad_area_nosemaphore;

	/* When running in the kernel we expect faults to occur only to
	 * addresses in user space.  All other faults represent errors in the
	 * kernel and should generate an OOPS.  Unfortunatly, in the case of an
	 * erroneous fault occurring in a code path which already holds mmap_sem
	 * we will deadlock attempting to validate the fault against the
	 * address space.  Luckily the kernel only validly references user
	 * space from well defined areas of code, which are listed in the
	 * exceptions table.
	 *
	 * As the vast majority of faults will be valid we will only perform
	 * the source reference check when there is a possibilty of a deadlock.
	 * Attempt to lock the address space, if we cannot we then validate the
	 * source.  If this is invalid we can skip the address space check,
	 * thus avoiding the deadlock.
	 */
	if (!down_read_trylock(&mm->mmap_sem)) {
		if ((error_code & ACE_USERMODE) == 0 &&
		    !search_exception_tables(regs->psw))
			goto bad_area_nosemaphore;
		down_read(&mm->mmap_sem);
	}

	vma = find_vma(mm, address);
	if (!vma)
		goto bad_area;
	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;

	if (error_code & ACE_USERMODE) {
		/*
		 * accessing the stack below "spu" is always a bug.
		 * The "+ 4" is there due to the push instruction
		 * doing pre-decrement on the stack and that
		 * doesn't show up until later..
		 */
		if (address + 4 < regs->spu)
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
	write = 0;
	switch (error_code & (ACE_WRITE|ACE_PROTECTION)) {
		default:	/* 3: write, present */
			/* fall through */
		case ACE_WRITE:	/* write, not present */
			if (!(vma->vm_flags & VM_WRITE))
				goto bad_area;
			write++;
			break;
		case ACE_PROTECTION:	/* read, present */
		case 0:		/* read, not present */
			if (!(vma->vm_flags & (VM_READ | VM_EXEC)))
				goto bad_area;
	}

	/*
	 * For instruction access exception, check if the area is executable
	 */
	if ((error_code & ACE_INSTRUCTION) && !(vma->vm_flags & VM_EXEC))
	  goto bad_area;

	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	addr = (address & PAGE_MASK);
	set_thread_fault_code(error_code);
	fault = handle_mm_fault(mm, vma, addr, write ? FAULT_FLAG_WRITE : 0);
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
	set_thread_fault_code(0);
	up_read(&mm->mmap_sem);
	return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
	up_read(&mm->mmap_sem);

bad_area_nosemaphore:
	/* User mode accesses just cause a SIGSEGV */
	if (error_code & ACE_USERMODE) {
		tsk->thread.address = address;
		tsk->thread.error_code = error_code | (address >= TASK_SIZE);
		tsk->thread.trap_no = 14;
		info.si_signo = SIGSEGV;
		info.si_errno = 0;
		/* info.si_code has been set above */
		info.si_addr = (void __user *)address;
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
 */

	bust_spinlocks(1);

	if (address < PAGE_SIZE)
		printk(KERN_ALERT "Unable to handle kernel NULL pointer dereference");
	else
		printk(KERN_ALERT "Unable to handle kernel paging request");
	printk(" at virtual address %08lx\n",address);
	printk(KERN_ALERT " printing bpc:\n");
	printk("%08lx\n", regs->bpc);
	page = *(unsigned long *)MPTB;
	page = ((unsigned long *) page)[address >> PGDIR_SHIFT];
	printk(KERN_ALERT "*pde = %08lx\n", page);
	if (page & _PAGE_PRESENT) {
		page &= PAGE_MASK;
		address &= 0x003ff000;
		page = ((unsigned long *) __va(page))[address >> PAGE_SHIFT];
		printk(KERN_ALERT "*pte = %08lx\n", page);
	}
	die("Oops", regs, error_code);
	bust_spinlocks(0);
	do_exit(SIGKILL);

/*
 * We ran out of memory, or some other thing happened to us that made
 * us unable to handle the page fault gracefully.
 */
out_of_memory:
	up_read(&mm->mmap_sem);
	if (!(error_code & ACE_USERMODE))
		goto no_context;
	pagefault_out_of_memory();
	return;

do_sigbus:
	up_read(&mm->mmap_sem);

	/* Kernel mode? Handle exception or die */
	if (!(error_code & ACE_USERMODE))
		goto no_context;

	tsk->thread.address = address;
	tsk->thread.error_code = error_code;
	tsk->thread.trap_no = 14;
	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_ADRERR;
	info.si_addr = (void __user *)address;
	force_sig_info(SIGBUS, &info, tsk);
	return;

vmalloc_fault:
	{
		/*
		 * Synchronize this task's top level page-table
		 * with the 'reference' page table.
		 *
		 * Do _not_ use "tsk" here. We might be inside
		 * an interrupt in the middle of a task switch..
		 */
		int offset = pgd_index(address);
		pgd_t *pgd, *pgd_k;
		pmd_t *pmd, *pmd_k;
		pte_t *pte_k;

		pgd = (pgd_t *)*(unsigned long *)MPTB;
		pgd = offset + (pgd_t *)pgd;
		pgd_k = init_mm.pgd + offset;

		if (!pgd_present(*pgd_k))
			goto no_context;

		/*
		 * set_pgd(pgd, *pgd_k); here would be useless on PAE
		 * and redundant with the set_pmd() on non-PAE.
		 */

		pmd = pmd_offset(pgd, address);
		pmd_k = pmd_offset(pgd_k, address);
		if (!pmd_present(*pmd_k))
			goto no_context;
		set_pmd(pmd, *pmd_k);

		pte_k = pte_offset_kernel(pmd_k, address);
		if (!pte_present(*pte_k))
			goto no_context;

		addr = (address & PAGE_MASK);
		set_thread_fault_code(error_code);
		update_mmu_cache(NULL, addr, pte_k);
		set_thread_fault_code(0);
		return;
	}
}

/*======================================================================*
 * update_mmu_cache()
 *======================================================================*/
#define TLB_MASK	(NR_TLB_ENTRIES - 1)
#define ITLB_END	(unsigned long *)(ITLB_BASE + (NR_TLB_ENTRIES * 8))
#define DTLB_END	(unsigned long *)(DTLB_BASE + (NR_TLB_ENTRIES * 8))
void update_mmu_cache(struct vm_area_struct *vma, unsigned long vaddr,
	pte_t *ptep)
{
	volatile unsigned long *entry1, *entry2;
	unsigned long pte_data, flags;
	unsigned int *entry_dat;
	int inst = get_thread_fault_code() & ACE_INSTRUCTION;
	int i;

	/* Ptrace may call this routine. */
	if (vma && current->active_mm != vma->vm_mm)
		return;

	local_irq_save(flags);

	vaddr = (vaddr & PAGE_MASK) | get_asid();

	pte_data = pte_val(*ptep);

#ifdef CONFIG_CHIP_OPSP
	entry1 = (unsigned long *)ITLB_BASE;
	for (i = 0; i < NR_TLB_ENTRIES; i++) {
		if (*entry1++ == vaddr) {
			set_tlb_data(entry1, pte_data);
			break;
		}
		entry1++;
	}
	entry2 = (unsigned long *)DTLB_BASE;
	for (i = 0; i < NR_TLB_ENTRIES; i++) {
		if (*entry2++ == vaddr) {
			set_tlb_data(entry2, pte_data);
			break;
		}
		entry2++;
	}
#else
	/*
	 * Update TLB entries
	 *  entry1: ITLB entry address
	 *  entry2: DTLB entry address
	 */
	__asm__ __volatile__ (
		"seth	%0, #high(%4)	\n\t"
		"st	%2, @(%5, %0)	\n\t"
		"ldi	%1, #1		\n\t"
		"st	%1, @(%6, %0)	\n\t"
		"add3	r4, %0, %7	\n\t"
		".fillinsn		\n"
		"1:			\n\t"
		"ld	%1, @(%6, %0)	\n\t"
		"bnez	%1, 1b		\n\t"
		"ld	%0, @r4+	\n\t"
		"ld	%1, @r4		\n\t"
		"st	%3, @+%0	\n\t"
		"st	%3, @+%1	\n\t"
		: "=&r" (entry1), "=&r" (entry2)
		: "r" (vaddr), "r" (pte_data), "i" (MMU_REG_BASE),
		"i" (MSVA_offset), "i" (MTOP_offset), "i" (MIDXI_offset)
		: "r4", "memory"
	);
#endif

	if ((!inst && entry2 >= DTLB_END) || (inst && entry1 >= ITLB_END))
		goto notfound;

found:
	local_irq_restore(flags);

	return;

	/* Valid entry not found */
notfound:
	/*
	 * Update ITLB or DTLB entry
	 *  entry1: TLB entry address
	 *  entry2: TLB base address
	 */
	if (!inst) {
		entry2 = (unsigned long *)DTLB_BASE;
		entry_dat = &tlb_entry_d;
	} else {
		entry2 = (unsigned long *)ITLB_BASE;
		entry_dat = &tlb_entry_i;
	}
	entry1 = entry2 + (((*entry_dat - 1) & TLB_MASK) << 1);

	for (i = 0 ; i < NR_TLB_ENTRIES ; i++) {
		if (!(entry1[1] & 2))	/* Valid bit check */
			break;

		if (entry1 != entry2)
			entry1 -= 2;
		else
			entry1 += TLB_MASK << 1;
	}

	if (i >= NR_TLB_ENTRIES) {	/* Empty entry not found */
		entry1 = entry2 + (*entry_dat << 1);
		*entry_dat = (*entry_dat + 1) & TLB_MASK;
	}
	*entry1++ = vaddr;	/* Set TLB tag */
	set_tlb_data(entry1, pte_data);

	goto found;
}

/*======================================================================*
 * flush_tlb_page() : flushes one page
 *======================================================================*/
void local_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	if (vma->vm_mm && mm_context(vma->vm_mm) != NO_CONTEXT) {
		unsigned long flags;

		local_irq_save(flags);
		page &= PAGE_MASK;
		page |= (mm_context(vma->vm_mm) & MMU_CONTEXT_ASID_MASK);
		__flush_tlb_page(page);
		local_irq_restore(flags);
	}
}

/*======================================================================*
 * flush_tlb_range() : flushes a range of pages
 *======================================================================*/
void local_flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
	unsigned long end)
{
	struct mm_struct *mm;

	mm = vma->vm_mm;
	if (mm_context(mm) != NO_CONTEXT) {
		unsigned long flags;
		int size;

		local_irq_save(flags);
		size = (end - start + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
		if (size > (NR_TLB_ENTRIES / 4)) { /* Too many TLB to flush */
			mm_context(mm) = NO_CONTEXT;
			if (mm == current->mm)
				activate_context(mm);
		} else {
			unsigned long asid;

			asid = mm_context(mm) & MMU_CONTEXT_ASID_MASK;
			start &= PAGE_MASK;
			end += (PAGE_SIZE - 1);
			end &= PAGE_MASK;

			start |= asid;
			end   |= asid;
			while (start < end) {
				__flush_tlb_page(start);
				start += PAGE_SIZE;
			}
		}
		local_irq_restore(flags);
	}
}

/*======================================================================*
 * flush_tlb_mm() : flushes the specified mm context TLB's
 *======================================================================*/
void local_flush_tlb_mm(struct mm_struct *mm)
{
	/* Invalidate all TLB of this process. */
	/* Instead of invalidating each TLB, we get new MMU context. */
	if (mm_context(mm) != NO_CONTEXT) {
		unsigned long flags;

		local_irq_save(flags);
		mm_context(mm) = NO_CONTEXT;
		if (mm == current->mm)
			activate_context(mm);
		local_irq_restore(flags);
	}
}

/*======================================================================*
 * flush_tlb_all() : flushes all processes TLBs
 *======================================================================*/
void local_flush_tlb_all(void)
{
	unsigned long flags;

	local_irq_save(flags);
	__flush_tlb_all();
	local_irq_restore(flags);
}

/*======================================================================*
 * init_mmu()
 *======================================================================*/
void __init init_mmu(void)
{
	tlb_entry_i = 0;
	tlb_entry_d = 0;
	mmu_context_cache = MMU_CONTEXT_FIRST_VERSION;
	set_asid(mmu_context_cache & MMU_CONTEXT_ASID_MASK);
	*(volatile unsigned long *)MPTB = (unsigned long)swapper_pg_dir;
}
