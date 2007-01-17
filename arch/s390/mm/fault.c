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
#include <linux/init.h>
#include <linux/console.h>
#include <linux/module.h>
#include <linux/hardirq.h>
#include <linux/kprobes.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/kdebug.h>
#include <asm/s390_ext.h>

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

extern void die(const char *,struct pt_regs *,long);

#ifdef CONFIG_KPROBES
ATOMIC_NOTIFIER_HEAD(notify_page_fault_chain);
int register_page_fault_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&notify_page_fault_chain, nb);
}

int unregister_page_fault_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&notify_page_fault_chain, nb);
}

static inline int notify_page_fault(enum die_val val, const char *str,
			struct pt_regs *regs, long err, int trap, int sig)
{
	struct die_args args = {
		.regs = regs,
		.str = str,
		.err = err,
		.trapnr = trap,
		.signr = sig
	};
	return atomic_notifier_call_chain(&notify_page_fault_chain, val, &args);
}
#else
static inline int notify_page_fault(enum die_val val, const char *str,
			struct pt_regs *regs, long err, int trap, int sig)
{
	return NOTIFY_DONE;
}
#endif

extern spinlock_t timerlist_lock;

/*
 * Unlock any spinlocks which will prevent us from getting the
 * message out (timerlist_lock is acquired through the
 * console unblank code)
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
 * Check which address space is addressed by the access
 * register in S390_lowcore.exc_access_id.
 * Returns 1 for user space and 0 for kernel space.
 */
static int __check_access_register(struct pt_regs *regs, int error_code)
{
	int areg = S390_lowcore.exc_access_id;

	if (areg == 0)
		/* Access via access register 0 -> kernel address */
		return 0;
	save_access_regs(current->thread.acrs);
	if (regs && areg < NUM_ACRS && current->thread.acrs[areg] <= 1)
		/*
		 * access register contains 0 -> kernel address,
		 * access register contains 1 -> user space address
		 */
		return current->thread.acrs[areg];

	/* Something unhealthy was done with the access registers... */
	die("page fault via unknown access register", regs, error_code);
	do_exit(SIGKILL);
	return 0;
}

/*
 * Check which address space the address belongs to.
 * Returns 1 for user space and 0 for kernel space.
 */
static inline int check_user_space(struct pt_regs *regs, int error_code)
{
	/*
	 * The lowest two bits of S390_lowcore.trans_exc_code indicate
	 * which paging table was used:
	 *   0: Primary Segment Table Descriptor
	 *   1: STD determined via access register
	 *   2: Secondary Segment Table Descriptor
	 *   3: Home Segment Table Descriptor
	 */
	int descriptor = S390_lowcore.trans_exc_code & 3;
	if (unlikely(descriptor == 1))
		return __check_access_register(regs, error_code);
	if (descriptor == 2)
		return current->thread.mm_segment.ar4;
	return descriptor != 0;
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
static inline void __kprobes
do_exception(struct pt_regs *regs, unsigned long error_code, int is_protection)
{
        struct task_struct *tsk;
        struct mm_struct *mm;
        struct vm_area_struct * vma;
        unsigned long address;
	int user_address;
	const struct exception_table_entry *fixup;
	int si_code = SEGV_MAPERR;

        tsk = current;
        mm = tsk->mm;
	
	if (notify_page_fault(DIE_PAGE_FAULT, "page fault", regs, error_code, 14,
					SIGSEGV) == NOTIFY_STOP)
		return;

	/* 
         * Check for low-address protection.  This needs to be treated
	 * as a special case because the translation exception code 
	 * field is not guaranteed to contain valid data in this case.
	 */
	if (is_protection && !(S390_lowcore.trans_exc_code & 4)) {

		/* Low-address protection hit in kernel mode means 
		   NULL pointer write access in kernel mode.  */
 		if (!(regs->psw.mask & PSW_MASK_PSTATE)) {
			address = 0;
			user_address = 0;
			goto no_context;
		}

		/* Low-address protection hit in user mode 'cannot happen'.  */
		die ("Low-address protection", regs, error_code);
        	do_exit(SIGKILL);
	}

        /* 
         * get the failing address 
         * more specific the segment and page table portion of 
         * the address 
         */
        address = S390_lowcore.trans_exc_code & __FAIL_ADDR_MASK;
	user_address = check_user_space(regs, error_code);

	/*
	 * Verify that the fault happened in user space, that
	 * we are not in an interrupt and that there is a 
	 * user context.
	 */
        if (user_address == 0 || in_atomic() || !mm)
                goto no_context;

	/*
	 * When we get here, the fault happened in the current
	 * task's user address space, so we can switch on the
	 * interrupts again and then search the VMAs
	 */
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
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
	si_code = SEGV_ACCERR;
	if (!is_protection) {
		/* page not present, check vm flags */
		if (!(vma->vm_flags & (VM_READ | VM_EXEC | VM_WRITE)))
			goto bad_area;
	} else {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	}

survive:
	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	switch (handle_mm_fault(mm, vma, address, is_protection)) {
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
	/*
	 * The instruction that caused the program check will
	 * be repeated. Don't signal single step via SIGTRAP.
	 */
	clear_tsk_thread_flag(current, TIF_SINGLE_STEP);
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
        if (user_address == 0)
                printk(KERN_ALERT "Unable to handle kernel pointer dereference"
        	       " at virtual kernel address %p\n", (void *)address);
        else
                printk(KERN_ALERT "Unable to handle kernel paging request"
		       " at virtual user address %p\n", (void *)address);

        die("Oops", regs, error_code);
        do_exit(SIGKILL);


/*
 * We ran out of memory, or some other thing happened to us that made
 * us unable to handle the page fault gracefully.
*/
out_of_memory:
	up_read(&mm->mmap_sem);
	if (is_init(tsk)) {
		yield();
		down_read(&mm->mmap_sem);
		goto survive;
	}
	printk("VM: killing process %s\n", tsk->comm);
	if (regs->psw.mask & PSW_MASK_PSTATE)
		do_exit(SIGKILL);
	goto no_context;

do_sigbus:
	up_read(&mm->mmap_sem);

	/*
	 * Send a sigbus, regardless of whether we were in kernel
	 * or user mode.
	 */
        tsk->thread.prot_addr = address;
        tsk->thread.trap_no = error_code;
	force_sig(SIGBUS, tsk);

	/* Kernel mode? Handle exceptions or die */
	if (!(regs->psw.mask & PSW_MASK_PSTATE))
		goto no_context;
}

void do_protection_exception(struct pt_regs *regs, unsigned long error_code)
{
	regs->psw.addr -= (error_code >> 16);
	do_exception(regs, 4, 1);
}

void do_dat_exception(struct pt_regs *regs, unsigned long error_code)
{
	do_exception(regs, error_code & 0xff, 0);
}

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
} __attribute__ ((packed)) pfault_refbk_t;

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

asmlinkage void
pfault_interrupt(__u16 error_code)
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
