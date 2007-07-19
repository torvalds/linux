/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * arch/sh64/mm/fault.c
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003  Richard Curnow (/proc/tlb, bug fixes)
 * Copyright (C) 2003  Paul Mundt
 *
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
#include <linux/interrupt.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/tlb.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/registers.h>		/* required by inline asm statements */

#if defined(CONFIG_SH64_PROC_TLB)
#include <linux/init.h>
#include <linux/proc_fs.h>
/* Count numbers of tlb refills in each region */
static unsigned long long calls_to_update_mmu_cache = 0ULL;
static unsigned long long calls_to_flush_tlb_page   = 0ULL;
static unsigned long long calls_to_flush_tlb_range  = 0ULL;
static unsigned long long calls_to_flush_tlb_mm     = 0ULL;
static unsigned long long calls_to_flush_tlb_all    = 0ULL;
unsigned long long calls_to_do_slow_page_fault = 0ULL;
unsigned long long calls_to_do_fast_page_fault = 0ULL;

/* Count size of ranges for flush_tlb_range */
static unsigned long long flush_tlb_range_1         = 0ULL;
static unsigned long long flush_tlb_range_2         = 0ULL;
static unsigned long long flush_tlb_range_3_4       = 0ULL;
static unsigned long long flush_tlb_range_5_7       = 0ULL;
static unsigned long long flush_tlb_range_8_11      = 0ULL;
static unsigned long long flush_tlb_range_12_15     = 0ULL;
static unsigned long long flush_tlb_range_16_up     = 0ULL;

static unsigned long long page_not_present          = 0ULL;

#endif

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
	printk("Task pid %d\n", tsk->pid);
}

static pte_t *lookup_pte(struct mm_struct *mm, unsigned long address)
{
	pgd_t *dir;
	pmd_t *pmd;
	pte_t *pte;
	pte_t entry;

	dir = pgd_offset(mm, address);
	if (pgd_none(*dir)) {
		return NULL;
	}

	pmd = pmd_offset(dir, address);
	if (pmd_none(*pmd)) {
		return NULL;
	}

	pte = pte_offset_kernel(pmd, address);
	entry = *pte;

	if (pte_none(entry)) {
		return NULL;
	}
	if (!pte_present(entry)) {
		return NULL;
	}

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

#if defined(CONFIG_SH64_PROC_TLB)
        ++calls_to_do_slow_page_fault;
#endif

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
		       __FUNCTION__,__LINE__,
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
		       __FUNCTION__,__LINE__,
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
		       __FUNCTION__,__LINE__,
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
				address, current->pid, current->comm,
				(unsigned long) regs->pc);
#if 0
			show_regs(regs);
#endif
		}
		if (is_init(tsk)) {
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
	if (is_init(current)) {
		panic("INIT out of memory\n");
		yield();
		goto survive;
	}
	printk("fault:Out of memory\n");
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


void flush_tlb_all(void);

void update_mmu_cache(struct vm_area_struct * vma,
			unsigned long address, pte_t pte)
{
#if defined(CONFIG_SH64_PROC_TLB)
	++calls_to_update_mmu_cache;
#endif

	/*
	 * This appears to get called once for every pte entry that gets
	 * established => I don't think it's efficient to try refilling the
	 * TLBs with the pages - some may not get accessed even.  Also, for
	 * executable pages, it is impossible to determine reliably here which
	 * TLB they should be mapped into (or both even).
	 *
	 * So, just do nothing here and handle faults on demand.  In the
	 * TLBMISS handling case, the refill is now done anyway after the pte
	 * has been fixed up, so that deals with most useful cases.
	 */
}

static void __flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	unsigned long long match, pteh=0, lpage;
	unsigned long tlb;
	struct mm_struct *mm;

	mm = vma->vm_mm;

	if (mm->context == NO_CONTEXT)
		return;

	/*
	 * Sign-extend based on neff.
	 */
	lpage = (page & NEFF_SIGN) ? (page | NEFF_MASK) : page;
	match = ((mm->context & MMU_CONTEXT_ASID_MASK) << PTEH_ASID_SHIFT) | PTEH_VALID;
	match |= lpage;

        /* Do ITLB : don't bother for pages in non-exectutable VMAs */
	if (vma->vm_flags & VM_EXEC) {
		for_each_itlb_entry(tlb) {
			asm volatile ("getcfg	%1, 0, %0"
				      : "=r" (pteh)
				      : "r" (tlb) );

			if (pteh == match) {
				__flush_tlb_slot(tlb);
				break;
			}

		}
	}

        /* Do DTLB : any page could potentially be in here. */
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

void flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	unsigned long flags;

#if defined(CONFIG_SH64_PROC_TLB)
        ++calls_to_flush_tlb_page;
#endif

	if (vma->vm_mm) {
		page &= PAGE_MASK;
		local_irq_save(flags);
		__flush_tlb_page(vma, page);
		local_irq_restore(flags);
	}
}

void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		     unsigned long end)
{
	unsigned long flags;
	unsigned long long match, pteh=0, pteh_epn, pteh_low;
	unsigned long tlb;
	struct mm_struct *mm;

	mm = vma->vm_mm;

#if defined(CONFIG_SH64_PROC_TLB)
	++calls_to_flush_tlb_range;

	{
		unsigned long size = (end - 1) - start;
		size >>= 12; /* divide by PAGE_SIZE */
		size++; /* end=start+4096 => 1 page */
		switch (size) {
		  case  1        : flush_tlb_range_1++;     break;
		  case  2        : flush_tlb_range_2++;     break;
		  case  3 ...  4 : flush_tlb_range_3_4++;   break;
		  case  5 ...  7 : flush_tlb_range_5_7++;   break;
		  case  8 ... 11 : flush_tlb_range_8_11++;  break;
		  case 12 ... 15 : flush_tlb_range_12_15++; break;
		  default        : flush_tlb_range_16_up++; break;
		}
	}
#endif

	if (mm->context == NO_CONTEXT)
		return;

	local_irq_save(flags);

	start &= PAGE_MASK;
	end &= PAGE_MASK;

	match = ((mm->context & MMU_CONTEXT_ASID_MASK) << PTEH_ASID_SHIFT) | PTEH_VALID;

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

void flush_tlb_mm(struct mm_struct *mm)
{
	unsigned long flags;

#if defined(CONFIG_SH64_PROC_TLB)
	++calls_to_flush_tlb_mm;
#endif

	if (mm->context == NO_CONTEXT)
		return;

	local_irq_save(flags);

	mm->context=NO_CONTEXT;
	if(mm==current->mm)
		activate_context(mm);

	local_irq_restore(flags);

}

void flush_tlb_all(void)
{
	/* Invalidate all, including shared pages, excluding fixed TLBs */

	unsigned long flags, tlb;

#if defined(CONFIG_SH64_PROC_TLB)
	++calls_to_flush_tlb_all;
#endif

	local_irq_save(flags);

	/* Flush each ITLB entry */
	for_each_itlb_entry(tlb) {
		__flush_tlb_slot(tlb);
	}

	/* Flush each DTLB entry */
	for_each_dtlb_entry(tlb) {
		__flush_tlb_slot(tlb);
	}

	local_irq_restore(flags);
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
        /* FIXME: Optimize this later.. */
        flush_tlb_all();
}

#if defined(CONFIG_SH64_PROC_TLB)
/* Procfs interface to read the performance information */

static int
tlb_proc_info(char *buf, char **start, off_t fpos, int length, int *eof, void *data)
{
  int len=0;
  len += sprintf(buf+len, "do_fast_page_fault   called %12lld times\n", calls_to_do_fast_page_fault);
  len += sprintf(buf+len, "do_slow_page_fault   called %12lld times\n", calls_to_do_slow_page_fault);
  len += sprintf(buf+len, "update_mmu_cache     called %12lld times\n", calls_to_update_mmu_cache);
  len += sprintf(buf+len, "flush_tlb_page       called %12lld times\n", calls_to_flush_tlb_page);
  len += sprintf(buf+len, "flush_tlb_range      called %12lld times\n", calls_to_flush_tlb_range);
  len += sprintf(buf+len, "flush_tlb_mm         called %12lld times\n", calls_to_flush_tlb_mm);
  len += sprintf(buf+len, "flush_tlb_all        called %12lld times\n", calls_to_flush_tlb_all);
  len += sprintf(buf+len, "flush_tlb_range_sizes\n"
                          " 1      : %12lld\n"
                          " 2      : %12lld\n"
                          " 3 -  4 : %12lld\n"
                          " 5 -  7 : %12lld\n"
                          " 8 - 11 : %12lld\n"
                          "12 - 15 : %12lld\n"
                          "16+     : %12lld\n",
                          flush_tlb_range_1, flush_tlb_range_2, flush_tlb_range_3_4,
                          flush_tlb_range_5_7, flush_tlb_range_8_11, flush_tlb_range_12_15,
                          flush_tlb_range_16_up);
  len += sprintf(buf+len, "page not present           %12lld times\n", page_not_present);
  *eof = 1;
  return len;
}

static int __init register_proc_tlb(void)
{
  create_proc_read_entry("tlb", 0, NULL, tlb_proc_info, NULL);
  return 0;
}

__initcall(register_proc_tlb);

#endif
