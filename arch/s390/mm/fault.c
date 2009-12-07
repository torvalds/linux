/*
 *  arch/s390/mm/fault.c
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 *               Ulrich Weigand (uweigand@de.ibm.com)
 *
 *  Derived from "arch/i386/mm/fault.c"
 *    Copyright (C) 1995  Linus Torvalds
 */

#include <linux/perf_event.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/compat.h>
#include <linux/smp.h>
#include <linux/kdebug.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/module.h>
#include <linux/hardirq.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/hugetlb.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/s390_ext.h>
#include <asm/mmu_context.h>
#include "../kernel/entry.h"

#ifndef CONFIG_64BIT
#define __FAIL_ADDR_MASK 0x7ffff000
#define __FIXUP_MASK 0x7fffffff
#define __SUBCODE_MASK 0x0200
#define __PF_RES_FIELD 0ULL
#else /* CONFIG_64BIT */
#define __FAIL_ADDR_MASK -4096L
#define __FIXUP_MASK ~0L
#define __SUBCODE_MASK 0x0600
#define __PF_RES_FIELD 0x8000000000000000ULL
#endif /* CONFIG_64BIT */

#ifdef CONFIG_SYSCTL
extern int sysctl_userprocess_debug;
#endif

static inline int notify_page_fault(struct pt_regs *regs)
{
	int ret = 0;

#ifdef CONFIG_KPROBES
	/* kprobe_running() needs smp_processor_id() */
	if (!user_mode(regs)) {
		preempt_disable();
		if (kprobe_running() && kprobe_fault_handler(regs, 14))
			ret = 1;
		preempt_enable();
	}
#endif
	return ret;
}


/*
 * Unlock any spinlocks which will prevent us from getting the
 * message out.
 */
void bust_spinlocks(int yes)
{
	if (yes) {
		oops_in_progress = 1;
	} else {
		int loglevel_save = console_loglevel;
		console_unblank();
		oops_in_progress = 0;
		/*
		 * OK, the message is on the console.  Now we call printk()
		 * without oops_in_progress set so that printk will give klogd
		 * a poke.  Hold onto your hats...
		 */
		console_loglevel = 15;
		printk(" ");
		console_loglevel = loglevel_save;
	}
}

/*
 * Returns the address space associated with the fault.
 * Returns 0 for kernel space and 1 for user space.
 */
static inline int user_space_fault(unsigned long trans_exc_code)
{
	/*
	 * The lowest two bits of the translation exception
	 * identification indicate which paging table was used.
	 */
	trans_exc_code &= 3;
	if (trans_exc_code == 2)
		/* Access via secondary space, set_fs setting decides */
		return current->thread.mm_segment.ar4;
	if (user_mode == HOME_SPACE_MODE)
		/* User space if the access has been done via home space. */
		return trans_exc_code == 3;
	/*
	 * If the user space is not the home space the kernel runs in home
	 * space. Access via secondary space has already been covered,
	 * access via primary space or access register is from user space
	 * and access via home space is from the kernel.
	 */
	return trans_exc_code != 3;
}

/*
 * Send SIGSEGV to task.  This is an external routine
 * to keep the stack usage of do_page_fault small.
 */
static void do_sigsegv(struct pt_regs *regs, unsigned long error_code,
		       int si_code, unsigned long address)
{
	struct siginfo si;

#if defined(CONFIG_SYSCTL) || defined(CONFIG_PROCESS_DEBUG)
#if defined(CONFIG_SYSCTL)
	if (sysctl_userprocess_debug)
#endif
	{
		printk("User process fault: interruption code 0x%lX\n",
		       error_code);
		printk("failing address: %lX\n", address);
		show_regs(regs);
	}
#endif
	si.si_signo = SIGSEGV;
	si.si_code = si_code;
	si.si_addr = (void __user *) address;
	force_sig_info(SIGSEGV, &si, current);
}

static void do_no_context(struct pt_regs *regs, unsigned long error_code,
			  unsigned long trans_exc_code)
{
	const struct exception_table_entry *fixup;
	unsigned long address;

	/* Are we prepared to handle this kernel fault?  */
	fixup = search_exception_tables(regs->psw.addr & __FIXUP_MASK);
	if (fixup) {
		regs->psw.addr = fixup->fixup | PSW_ADDR_AMODE;
		return;
	}

	/*
	 * Oops. The kernel tried to access some bad page. We'll have to
	 * terminate things with extreme prejudice.
	 */
	address = trans_exc_code & __FAIL_ADDR_MASK;
	if (!user_space_fault(trans_exc_code))
		printk(KERN_ALERT "Unable to handle kernel pointer dereference"
		       " at virtual kernel address %p\n", (void *)address);
	else
		printk(KERN_ALERT "Unable to handle kernel paging request"
		       " at virtual user address %p\n", (void *)address);

	die("Oops", regs, error_code);
	do_exit(SIGKILL);
}

static void do_low_address(struct pt_regs *regs, unsigned long error_code,
			   unsigned long trans_exc_code)
{
	/* Low-address protection hit in kernel mode means
	   NULL pointer write access in kernel mode.  */
	if (regs->psw.mask & PSW_MASK_PSTATE) {
		/* Low-address protection hit in user mode 'cannot happen'. */
		die ("Low-address protection", regs, error_code);
		do_exit(SIGKILL);
	}

	do_no_context(regs, error_code, trans_exc_code);
}

static void do_sigbus(struct pt_regs *regs, unsigned long error_code,
		      unsigned long trans_exc_code)
{
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;

	up_read(&mm->mmap_sem);
	/*
	 * Send a sigbus, regardless of whether we were in kernel
	 * or user mode.
	 */
	tsk->thread.prot_addr = trans_exc_code & __FAIL_ADDR_MASK;
	tsk->thread.trap_no = error_code;
	force_sig(SIGBUS, tsk);

	/* Kernel mode? Handle exceptions or die */
	if (!(regs->psw.mask & PSW_MASK_PSTATE))
		do_no_context(regs, error_code, trans_exc_code);
}

#ifdef CONFIG_S390_EXEC_PROTECT
static int signal_return(struct mm_struct *mm, struct pt_regs *regs,
			 unsigned long address, unsigned long error_code)
{
	u16 instruction;
	int rc;
#ifdef CONFIG_COMPAT
	int compat;
#endif

	pagefault_disable();
	rc = __get_user(instruction, (u16 __user *) regs->psw.addr);
	pagefault_enable();
	if (rc)
		return -EFAULT;

	up_read(&mm->mmap_sem);
	clear_tsk_thread_flag(current, TIF_SINGLE_STEP);
#ifdef CONFIG_COMPAT
	compat = is_compat_task();
	if (compat && instruction == 0x0a77)
		sys32_sigreturn();
	else if (compat && instruction == 0x0aad)
		sys32_rt_sigreturn();
	else
#endif
	if (instruction == 0x0a77)
		sys_sigreturn();
	else if (instruction == 0x0aad)
		sys_rt_sigreturn();
	else {
		current->thread.prot_addr = address;
		current->thread.trap_no = error_code;
		do_sigsegv(regs, error_code, SEGV_MAPERR, address);
	}
	return 0;
}
#endif /* CONFIG_S390_EXEC_PROTECT */

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 *
 * error_code:
 *   04       Protection           ->  Write-Protection  (suprression)
 *   10       Segment translation  ->  Not present       (nullification)
 *   11       Page translation     ->  Not present       (nullification)
 *   3b       Region third trans.  ->  Not present       (nullification)
 */
static inline void
do_exception(struct pt_regs *regs, unsigned long error_code, int write,
	     unsigned long trans_exc_code)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	unsigned long address;
	int si_code;
	int fault;

	if (notify_page_fault(regs))
		return;

	tsk = current;
	mm = tsk->mm;

	/*
	 * Verify that the fault happened in user space, that
	 * we are not in an interrupt and that there is a 
	 * user context.
	 */
	if (unlikely(!user_space_fault(trans_exc_code) || in_atomic() || !mm))
		goto no_context;

	address = trans_exc_code & __FAIL_ADDR_MASK;
	/*
	 * When we get here, the fault happened in the current
	 * task's user address space, so we can switch on the
	 * interrupts again and then search the VMAs
	 */
	local_irq_enable();
	perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS, 1, 0, regs, address);
	down_read(&mm->mmap_sem);

	si_code = SEGV_MAPERR;
	vma = find_vma(mm, address);
	if (!vma)
		goto bad_area;

#ifdef CONFIG_S390_EXEC_PROTECT
	if (unlikely((regs->psw.mask & PSW_MASK_ASC) == PSW_ASC_SECONDARY &&
		     (trans_exc_code & 3) == 0 && !(vma->vm_flags & VM_EXEC)))
		if (!signal_return(mm, regs, address, error_code))
			/*
			 * signal_return() has done an up_read(&mm->mmap_sem)
			 * if it returns 0.
			 */
			return;
#endif

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
	if (!write) {
		/* page not present, check vm flags */
		if (!(vma->vm_flags & (VM_READ | VM_EXEC | VM_WRITE)))
			goto bad_area;
	} else {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	}

	if (is_vm_hugetlb_page(vma))
		address &= HPAGE_MASK;
	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	fault = handle_mm_fault(mm, vma, address, write ? FAULT_FLAG_WRITE : 0);
	if (unlikely(fault & VM_FAULT_ERROR)) {
		if (fault & VM_FAULT_OOM) {
			up_read(&mm->mmap_sem);
			pagefault_out_of_memory();
			return;
		} else if (fault & VM_FAULT_SIGBUS) {
			do_sigbus(regs, error_code, address);
			return;
		}
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
        up_read(&mm->mmap_sem);
	/*
	 * The instruction that caused the program check will
	 * be repeated. Don't signal single step via SIGTRAP.
	 */
	clear_tsk_thread_flag(tsk, TIF_SINGLE_STEP);
        return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
	up_read(&mm->mmap_sem);

	/* User mode accesses just cause a SIGSEGV */
	if (regs->psw.mask & PSW_MASK_PSTATE) {
		tsk->thread.prot_addr = address;
		tsk->thread.trap_no = error_code;
		do_sigsegv(regs, error_code, si_code, address);
		return;
	}

no_context:
	do_no_context(regs, error_code, trans_exc_code);
}

void __kprobes do_protection_exception(struct pt_regs *regs,
				       long error_code)
{
	unsigned long trans_exc_code = S390_lowcore.trans_exc_code;

	/* Protection exception is supressing, decrement psw address. */
	regs->psw.addr -= (error_code >> 16);
	/*
	 * Check for low-address protection.  This needs to be treated
	 * as a special case because the translation exception code
	 * field is not guaranteed to contain valid data in this case.
	 */
	if (unlikely(!(trans_exc_code & 4))) {
		do_low_address(regs, error_code, trans_exc_code);
		return;
	}
	do_exception(regs, 4, 1, trans_exc_code);
}

void __kprobes do_dat_exception(struct pt_regs *regs, long error_code)
{
	do_exception(regs, error_code & 0xff, 0, S390_lowcore.trans_exc_code);
}

#ifdef CONFIG_64BIT
void __kprobes do_asce_exception(struct pt_regs *regs, unsigned long error_code)
{
	unsigned long trans_exc_code = S390_lowcore.trans_exc_code;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	unsigned long address;

	mm = current->mm;
	address = trans_exc_code & __FAIL_ADDR_MASK;

	if (unlikely(!user_space_fault(trans_exc_code) || in_atomic() || !mm))
		goto no_context;

	local_irq_enable();

	down_read(&mm->mmap_sem);
	vma = find_vma(mm, address);
	up_read(&mm->mmap_sem);

	if (vma) {
		update_mm(mm, current);
		return;
	}

	/* User mode accesses just cause a SIGSEGV */
	if (regs->psw.mask & PSW_MASK_PSTATE) {
		current->thread.prot_addr = address;
		current->thread.trap_no = error_code;
		do_sigsegv(regs, error_code, SEGV_MAPERR, address);
		return;
	}

no_context:
	do_no_context(regs, error_code, trans_exc_code);
}
#endif

#ifdef CONFIG_PFAULT 
/*
 * 'pfault' pseudo page faults routines.
 */
static ext_int_info_t ext_int_pfault;
static int pfault_disable = 0;

static int __init nopfault(char *str)
{
	pfault_disable = 1;
	return 1;
}

__setup("nopfault", nopfault);

typedef struct {
	__u16 refdiagc;
	__u16 reffcode;
	__u16 refdwlen;
	__u16 refversn;
	__u64 refgaddr;
	__u64 refselmk;
	__u64 refcmpmk;
	__u64 reserved;
} __attribute__ ((packed, aligned(8))) pfault_refbk_t;

int pfault_init(void)
{
	pfault_refbk_t refbk =
		{ 0x258, 0, 5, 2, __LC_CURRENT, 1ULL << 48, 1ULL << 48,
		  __PF_RES_FIELD };
        int rc;

	if (!MACHINE_IS_VM || pfault_disable)
		return -1;
	asm volatile(
		"	diag	%1,%0,0x258\n"
		"0:	j	2f\n"
		"1:	la	%0,8\n"
		"2:\n"
		EX_TABLE(0b,1b)
		: "=d" (rc) : "a" (&refbk), "m" (refbk) : "cc");
        __ctl_set_bit(0, 9);
        return rc;
}

void pfault_fini(void)
{
	pfault_refbk_t refbk =
	{ 0x258, 1, 5, 2, 0ULL, 0ULL, 0ULL, 0ULL };

	if (!MACHINE_IS_VM || pfault_disable)
		return;
	__ctl_clear_bit(0,9);
	asm volatile(
		"	diag	%0,0,0x258\n"
		"0:\n"
		EX_TABLE(0b,0b)
		: : "a" (&refbk), "m" (refbk) : "cc");
}

static void pfault_interrupt(__u16 error_code)
{
	struct task_struct *tsk;
	__u16 subcode;

	/*
	 * Get the external interruption subcode & pfault
	 * initial/completion signal bit. VM stores this 
	 * in the 'cpu address' field associated with the
         * external interrupt. 
	 */
	subcode = S390_lowcore.cpu_addr;
	if ((subcode & 0xff00) != __SUBCODE_MASK)
		return;

	/*
	 * Get the token (= address of the task structure of the affected task).
	 */
	tsk = *(struct task_struct **) __LC_PFAULT_INTPARM;

	if (subcode & 0x0080) {
		/* signal bit is set -> a page has been swapped in by VM */
		if (xchg(&tsk->thread.pfault_wait, -1) != 0) {
			/* Initial interrupt was faster than the completion
			 * interrupt. pfault_wait is valid. Set pfault_wait
			 * back to zero and wake up the process. This can
			 * safely be done because the task is still sleeping
			 * and can't produce new pfaults. */
			tsk->thread.pfault_wait = 0;
			wake_up_process(tsk);
			put_task_struct(tsk);
		}
	} else {
		/* signal bit not set -> a real page is missing. */
		get_task_struct(tsk);
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
		if (xchg(&tsk->thread.pfault_wait, 1) != 0) {
			/* Completion interrupt was faster than the initial
			 * interrupt (swapped in a -1 for pfault_wait). Set
			 * pfault_wait back to zero and exit. This can be
			 * done safely because tsk is running in kernel 
			 * mode and can't produce new pfaults. */
			tsk->thread.pfault_wait = 0;
			set_task_state(tsk, TASK_RUNNING);
			put_task_struct(tsk);
		} else
			set_tsk_need_resched(tsk);
	}
}

void __init pfault_irq_init(void)
{
	if (!MACHINE_IS_VM)
		return;

	/*
	 * Try to get pfault pseudo page faults going.
	 */
	if (register_early_external_interrupt(0x2603, pfault_interrupt,
					      &ext_int_pfault) != 0)
		panic("Couldn't request external interrupt 0x2603");

	if (pfault_init() == 0)
		return;

	/* Tough luck, no pfault. */
	pfault_disable = 1;
	unregister_early_external_interrupt(0x2603, pfault_interrupt,
					    &ext_int_pfault);
}
#endif
