/*
 * Architecture-specific trap handling.
 *
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * 05/12/00 grao <goutham.rao@intel.com> : added isr in siginfo for SIGFPE
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/vt_kern.h>		/* For unblank_screen() */
#include <linux/module.h>       /* for EXPORT_SYMBOL */
#include <linux/hardirq.h>
#include <linux/kprobes.h>
#include <linux/delay.h>		/* for ssleep() */
#include <linux/kdebug.h>

#include <asm/fpswa.h>
#include <asm/ia32.h>
#include <asm/intrinsics.h>
#include <asm/processor.h>
#include <asm/uaccess.h>

fpswa_interface_t *fpswa_interface;
EXPORT_SYMBOL(fpswa_interface);

void __init
trap_init (void)
{
	if (ia64_boot_param->fpswa)
		/* FPSWA fixup: make the interface pointer a kernel virtual address: */
		fpswa_interface = __va(ia64_boot_param->fpswa);
}

void
die (const char *str, struct pt_regs *regs, long err)
{
	static struct {
		spinlock_t lock;
		u32 lock_owner;
		int lock_owner_depth;
	} die = {
		.lock =			SPIN_LOCK_UNLOCKED,
		.lock_owner =		-1,
		.lock_owner_depth =	0
	};
	static int die_counter;
	int cpu = get_cpu();

	if (die.lock_owner != cpu) {
		console_verbose();
		spin_lock_irq(&die.lock);
		die.lock_owner = cpu;
		die.lock_owner_depth = 0;
		bust_spinlocks(1);
	}
	put_cpu();

	if (++die.lock_owner_depth < 3) {
		printk("%s[%d]: %s %ld [%d]\n",
			current->comm, current->pid, str, err, ++die_counter);
		(void) notify_die(DIE_OOPS, (char *)str, regs, err, 255, SIGSEGV);
		show_regs(regs);
  	} else
		printk(KERN_ERR "Recursive die() failure, output suppressed\n");

	bust_spinlocks(0);
	die.lock_owner = -1;
	spin_unlock_irq(&die.lock);

	if (panic_on_oops)
		panic("Fatal exception");

  	do_exit(SIGSEGV);
}

void
die_if_kernel (char *str, struct pt_regs *regs, long err)
{
	if (!user_mode(regs))
		die(str, regs, err);
}

void
__kprobes ia64_bad_break (unsigned long break_num, struct pt_regs *regs)
{
	siginfo_t siginfo;
	int sig, code;

	/* SIGILL, SIGFPE, SIGSEGV, and SIGBUS want these field initialized: */
	siginfo.si_addr = (void __user *) (regs->cr_iip + ia64_psr(regs)->ri);
	siginfo.si_imm = break_num;
	siginfo.si_flags = 0;		/* clear __ISR_VALID */
	siginfo.si_isr = 0;

	switch (break_num) {
	      case 0: /* unknown error (used by GCC for __builtin_abort()) */
		if (notify_die(DIE_BREAK, "break 0", regs, break_num, TRAP_BRKPT, SIGTRAP)
			       	== NOTIFY_STOP)
			return;
		die_if_kernel("bugcheck!", regs, break_num);
		sig = SIGILL; code = ILL_ILLOPC;
		break;

	      case 1: /* integer divide by zero */
		sig = SIGFPE; code = FPE_INTDIV;
		break;

	      case 2: /* integer overflow */
		sig = SIGFPE; code = FPE_INTOVF;
		break;

	      case 3: /* range check/bounds check */
		sig = SIGFPE; code = FPE_FLTSUB;
		break;

	      case 4: /* null pointer dereference */
		sig = SIGSEGV; code = SEGV_MAPERR;
		break;

	      case 5: /* misaligned data */
		sig = SIGSEGV; code = BUS_ADRALN;
		break;

	      case 6: /* decimal overflow */
		sig = SIGFPE; code = __FPE_DECOVF;
		break;

	      case 7: /* decimal divide by zero */
		sig = SIGFPE; code = __FPE_DECDIV;
		break;

	      case 8: /* packed decimal error */
		sig = SIGFPE; code = __FPE_DECERR;
		break;

	      case 9: /* invalid ASCII digit */
		sig = SIGFPE; code = __FPE_INVASC;
		break;

	      case 10: /* invalid decimal digit */
		sig = SIGFPE; code = __FPE_INVDEC;
		break;

	      case 11: /* paragraph stack overflow */
		sig = SIGSEGV; code = __SEGV_PSTKOVF;
		break;

	      case 0x3f000 ... 0x3ffff:	/* bundle-update in progress */
		sig = SIGILL; code = __ILL_BNDMOD;
		break;

	      default:
		if (break_num < 0x40000 || break_num > 0x100000)
			die_if_kernel("Bad break", regs, break_num);

		if (break_num < 0x80000) {
			sig = SIGILL; code = __ILL_BREAK;
		} else {
			if (notify_die(DIE_BREAK, "bad break", regs, break_num, TRAP_BRKPT, SIGTRAP)
					== NOTIFY_STOP)
				return;
			sig = SIGTRAP; code = TRAP_BRKPT;
		}
	}
	siginfo.si_signo = sig;
	siginfo.si_errno = 0;
	siginfo.si_code = code;
	force_sig_info(sig, &siginfo, current);
}

/*
 * disabled_fph_fault() is called when a user-level process attempts to access f32..f127
 * and it doesn't own the fp-high register partition.  When this happens, we save the
 * current fph partition in the task_struct of the fpu-owner (if necessary) and then load
 * the fp-high partition of the current task (if necessary).  Note that the kernel has
 * access to fph by the time we get here, as the IVT's "Disabled FP-Register" handler takes
 * care of clearing psr.dfh.
 */
static inline void
disabled_fph_fault (struct pt_regs *regs)
{
	struct ia64_psr *psr = ia64_psr(regs);

	/* first, grant user-level access to fph partition: */
	psr->dfh = 0;

	/*
	 * Make sure that no other task gets in on this processor
	 * while we're claiming the FPU
	 */
	preempt_disable();
#ifndef CONFIG_SMP
	{
		struct task_struct *fpu_owner
			= (struct task_struct *)ia64_get_kr(IA64_KR_FPU_OWNER);

		if (ia64_is_local_fpu_owner(current)) {
			preempt_enable_no_resched();
			return;
		}

		if (fpu_owner)
			ia64_flush_fph(fpu_owner);
	}
#endif /* !CONFIG_SMP */
	ia64_set_local_fpu_owner(current);
	if ((current->thread.flags & IA64_THREAD_FPH_VALID) != 0) {
		__ia64_load_fpu(current->thread.fph);
		psr->mfh = 0;
	} else {
		__ia64_init_fpu();
		/*
		 * Set mfh because the state in thread.fph does not match the state in
		 * the fph partition.
		 */
		psr->mfh = 1;
	}
	preempt_enable_no_resched();
}

static inline int
fp_emulate (int fp_fault, void *bundle, long *ipsr, long *fpsr, long *isr, long *pr, long *ifs,
	    struct pt_regs *regs)
{
	fp_state_t fp_state;
	fpswa_ret_t ret;

	if (!fpswa_interface)
		return -1;

	memset(&fp_state, 0, sizeof(fp_state_t));

	/*
	 * compute fp_state.  only FP registers f6 - f11 are used by the
	 * kernel, so set those bits in the mask and set the low volatile
	 * pointer to point to these registers.
	 */
	fp_state.bitmask_low64 = 0xfc0;  /* bit6..bit11 */

	fp_state.fp_state_low_volatile = (fp_state_low_volatile_t *) &regs->f6;
	/*
	 * unsigned long (*EFI_FPSWA) (
	 *      unsigned long    trap_type,
	 *	void             *Bundle,
	 *	unsigned long    *pipsr,
	 *	unsigned long    *pfsr,
	 *	unsigned long    *pisr,
	 *	unsigned long    *ppreds,
	 *	unsigned long    *pifs,
	 *	void             *fp_state);
	 */
	ret = (*fpswa_interface->fpswa)((unsigned long) fp_fault, bundle,
					(unsigned long *) ipsr, (unsigned long *) fpsr,
					(unsigned long *) isr, (unsigned long *) pr,
					(unsigned long *) ifs, &fp_state);

	return ret.status;
}

struct fpu_swa_msg {
	unsigned long count;
	unsigned long time;
};
static DEFINE_PER_CPU(struct fpu_swa_msg, cpulast);
DECLARE_PER_CPU(struct fpu_swa_msg, cpulast);
static struct fpu_swa_msg last __cacheline_aligned;


/*
 * Handle floating-point assist faults and traps.
 */
static int
handle_fpu_swa (int fp_fault, struct pt_regs *regs, unsigned long isr)
{
	long exception, bundle[2];
	unsigned long fault_ip;
	struct siginfo siginfo;

	fault_ip = regs->cr_iip;
	if (!fp_fault && (ia64_psr(regs)->ri == 0))
		fault_ip -= 16;
	if (copy_from_user(bundle, (void __user *) fault_ip, sizeof(bundle)))
		return -1;

	if (!(current->thread.flags & IA64_THREAD_FPEMU_NOPRINT))  {
		unsigned long count, current_jiffies = jiffies;
		struct fpu_swa_msg *cp = &__get_cpu_var(cpulast);

		if (unlikely(current_jiffies > cp->time))
			cp->count = 0;
		if (unlikely(cp->count < 5)) {
			cp->count++;
			cp->time = current_jiffies + 5 * HZ;

			/* minimize races by grabbing a copy of count BEFORE checking last.time. */
			count = last.count;
			barrier();

			/*
			 * Lower 4 bits are used as a count. Upper bits are a sequence
			 * number that is updated when count is reset. The cmpxchg will
			 * fail is seqno has changed. This minimizes mutiple cpus
			 * reseting the count.
			 */
			if (current_jiffies > last.time)
				(void) cmpxchg_acq(&last.count, count, 16 + (count & ~15));

			/* used fetchadd to atomically update the count */
			if ((last.count & 15) < 5 && (ia64_fetchadd(1, &last.count, acq) & 15) < 5) {
				last.time = current_jiffies + 5 * HZ;
				printk(KERN_WARNING
		       			"%s(%d): floating-point assist fault at ip %016lx, isr %016lx\n",
		       			current->comm, current->pid, regs->cr_iip + ia64_psr(regs)->ri, isr);
			}
		}
	}

	exception = fp_emulate(fp_fault, bundle, &regs->cr_ipsr, &regs->ar_fpsr, &isr, &regs->pr,
			       &regs->cr_ifs, regs);
	if (fp_fault) {
		if (exception == 0) {
			/* emulation was successful */
			ia64_increment_ip(regs);
		} else if (exception == -1) {
			printk(KERN_ERR "handle_fpu_swa: fp_emulate() returned -1\n");
			return -1;
		} else {
			/* is next instruction a trap? */
			if (exception & 2) {
				ia64_increment_ip(regs);
			}
			siginfo.si_signo = SIGFPE;
			siginfo.si_errno = 0;
			siginfo.si_code = __SI_FAULT;	/* default code */
			siginfo.si_addr = (void __user *) (regs->cr_iip + ia64_psr(regs)->ri);
			if (isr & 0x11) {
				siginfo.si_code = FPE_FLTINV;
			} else if (isr & 0x22) {
				/* denormal operand gets the same si_code as underflow 
				* see arch/i386/kernel/traps.c:math_error()  */
				siginfo.si_code = FPE_FLTUND;
			} else if (isr & 0x44) {
				siginfo.si_code = FPE_FLTDIV;
			}
			siginfo.si_isr = isr;
			siginfo.si_flags = __ISR_VALID;
			siginfo.si_imm = 0;
			force_sig_info(SIGFPE, &siginfo, current);
		}
	} else {
		if (exception == -1) {
			printk(KERN_ERR "handle_fpu_swa: fp_emulate() returned -1\n");
			return -1;
		} else if (exception != 0) {
			/* raise exception */
			siginfo.si_signo = SIGFPE;
			siginfo.si_errno = 0;
			siginfo.si_code = __SI_FAULT;	/* default code */
			siginfo.si_addr = (void __user *) (regs->cr_iip + ia64_psr(regs)->ri);
			if (isr & 0x880) {
				siginfo.si_code = FPE_FLTOVF;
			} else if (isr & 0x1100) {
				siginfo.si_code = FPE_FLTUND;
			} else if (isr & 0x2200) {
				siginfo.si_code = FPE_FLTRES;
			}
			siginfo.si_isr = isr;
			siginfo.si_flags = __ISR_VALID;
			siginfo.si_imm = 0;
			force_sig_info(SIGFPE, &siginfo, current);
		}
	}
	return 0;
}

struct illegal_op_return {
	unsigned long fkt, arg1, arg2, arg3;
};

struct illegal_op_return
ia64_illegal_op_fault (unsigned long ec, long arg1, long arg2, long arg3,
		       long arg4, long arg5, long arg6, long arg7,
		       struct pt_regs regs)
{
	struct illegal_op_return rv;
	struct siginfo si;
	char buf[128];

#ifdef CONFIG_IA64_BRL_EMU
	{
		extern struct illegal_op_return ia64_emulate_brl (struct pt_regs *, unsigned long);

		rv = ia64_emulate_brl(&regs, ec);
		if (rv.fkt != (unsigned long) -1)
			return rv;
	}
#endif

	sprintf(buf, "IA-64 Illegal operation fault");
	die_if_kernel(buf, &regs, 0);

	memset(&si, 0, sizeof(si));
	si.si_signo = SIGILL;
	si.si_code = ILL_ILLOPC;
	si.si_addr = (void __user *) (regs.cr_iip + ia64_psr(&regs)->ri);
	force_sig_info(SIGILL, &si, current);
	rv.fkt = 0;
	return rv;
}

void __kprobes
ia64_fault (unsigned long vector, unsigned long isr, unsigned long ifa,
	    unsigned long iim, unsigned long itir, long arg5, long arg6,
	    long arg7, struct pt_regs regs)
{
	unsigned long code, error = isr, iip;
	struct siginfo siginfo;
	char buf[128];
	int result, sig;
	static const char *reason[] = {
		"IA-64 Illegal Operation fault",
		"IA-64 Privileged Operation fault",
		"IA-64 Privileged Register fault",
		"IA-64 Reserved Register/Field fault",
		"Disabled Instruction Set Transition fault",
		"Unknown fault 5", "Unknown fault 6", "Unknown fault 7", "Illegal Hazard fault",
		"Unknown fault 9", "Unknown fault 10", "Unknown fault 11", "Unknown fault 12",
		"Unknown fault 13", "Unknown fault 14", "Unknown fault 15"
	};

	if ((isr & IA64_ISR_NA) && ((isr & IA64_ISR_CODE_MASK) == IA64_ISR_CODE_LFETCH)) {
		/*
		 * This fault was due to lfetch.fault, set "ed" bit in the psr to cancel
		 * the lfetch.
		 */
		ia64_psr(&regs)->ed = 1;
		return;
	}

	iip = regs.cr_iip + ia64_psr(&regs)->ri;

	switch (vector) {
	      case 24: /* General Exception */
		code = (isr >> 4) & 0xf;
		sprintf(buf, "General Exception: %s%s", reason[code],
			(code == 3) ? ((isr & (1UL << 37))
				       ? " (RSE access)" : " (data access)") : "");
		if (code == 8) {
# ifdef CONFIG_IA64_PRINT_HAZARDS
			printk("%s[%d]: possible hazard @ ip=%016lx (pr = %016lx)\n",
			       current->comm, current->pid,
			       regs.cr_iip + ia64_psr(&regs)->ri, regs.pr);
# endif
			return;
		}
		break;

	      case 25: /* Disabled FP-Register */
		if (isr & 2) {
			disabled_fph_fault(&regs);
			return;
		}
		sprintf(buf, "Disabled FPL fault---not supposed to happen!");
		break;

	      case 26: /* NaT Consumption */
		if (user_mode(&regs)) {
			void __user *addr;

			if (((isr >> 4) & 0xf) == 2) {
				/* NaT page consumption */
				sig = SIGSEGV;
				code = SEGV_ACCERR;
				addr = (void __user *) ifa;
			} else {
				/* register NaT consumption */
				sig = SIGILL;
				code = ILL_ILLOPN;
				addr = (void __user *) (regs.cr_iip
							+ ia64_psr(&regs)->ri);
			}
			siginfo.si_signo = sig;
			siginfo.si_code = code;
			siginfo.si_errno = 0;
			siginfo.si_addr = addr;
			siginfo.si_imm = vector;
			siginfo.si_flags = __ISR_VALID;
			siginfo.si_isr = isr;
			force_sig_info(sig, &siginfo, current);
			return;
		} else if (ia64_done_with_exception(&regs))
			return;
		sprintf(buf, "NaT consumption");
		break;

	      case 31: /* Unsupported Data Reference */
		if (user_mode(&regs)) {
			siginfo.si_signo = SIGILL;
			siginfo.si_code = ILL_ILLOPN;
			siginfo.si_errno = 0;
			siginfo.si_addr = (void __user *) iip;
			siginfo.si_imm = vector;
			siginfo.si_flags = __ISR_VALID;
			siginfo.si_isr = isr;
			force_sig_info(SIGILL, &siginfo, current);
			return;
		}
		sprintf(buf, "Unsupported data reference");
		break;

	      case 29: /* Debug */
	      case 35: /* Taken Branch Trap */
	      case 36: /* Single Step Trap */
		if (fsys_mode(current, &regs)) {
			extern char __kernel_syscall_via_break[];
			/*
			 * Got a trap in fsys-mode: Taken Branch Trap
			 * and Single Step trap need special handling;
			 * Debug trap is ignored (we disable it here
			 * and re-enable it in the lower-privilege trap).
			 */
			if (unlikely(vector == 29)) {
				set_thread_flag(TIF_DB_DISABLED);
				ia64_psr(&regs)->db = 0;
				ia64_psr(&regs)->lp = 1;
				return;
			}
			/* re-do the system call via break 0x100000: */
			regs.cr_iip = (unsigned long) __kernel_syscall_via_break;
			ia64_psr(&regs)->ri = 0;
			ia64_psr(&regs)->cpl = 3;
			return;
		}
		switch (vector) {
		      case 29:
			siginfo.si_code = TRAP_HWBKPT;
#ifdef CONFIG_ITANIUM
			/*
			 * Erratum 10 (IFA may contain incorrect address) now has
			 * "NoFix" status.  There are no plans for fixing this.
			 */
			if (ia64_psr(&regs)->is == 0)
			  ifa = regs.cr_iip;
#endif
			break;
		      case 35: siginfo.si_code = TRAP_BRANCH; ifa = 0; break;
		      case 36: siginfo.si_code = TRAP_TRACE; ifa = 0; break;
		}
		if (notify_die(DIE_FAULT, "ia64_fault", &regs, vector, siginfo.si_code, SIGTRAP)
			       	== NOTIFY_STOP)
			return;
		siginfo.si_signo = SIGTRAP;
		siginfo.si_errno = 0;
		siginfo.si_addr  = (void __user *) ifa;
		siginfo.si_imm   = 0;
		siginfo.si_flags = __ISR_VALID;
		siginfo.si_isr   = isr;
		force_sig_info(SIGTRAP, &siginfo, current);
		return;

	      case 32: /* fp fault */
	      case 33: /* fp trap */
		result = handle_fpu_swa((vector == 32) ? 1 : 0, &regs, isr);
		if ((result < 0) || (current->thread.flags & IA64_THREAD_FPEMU_SIGFPE)) {
			siginfo.si_signo = SIGFPE;
			siginfo.si_errno = 0;
			siginfo.si_code = FPE_FLTINV;
			siginfo.si_addr = (void __user *) iip;
			siginfo.si_flags = __ISR_VALID;
			siginfo.si_isr = isr;
			siginfo.si_imm = 0;
			force_sig_info(SIGFPE, &siginfo, current);
		}
		return;

	      case 34:
		if (isr & 0x2) {
			/* Lower-Privilege Transfer Trap */

			/* If we disabled debug traps during an fsyscall,
			 * re-enable them here.
			 */
			if (test_thread_flag(TIF_DB_DISABLED)) {
				clear_thread_flag(TIF_DB_DISABLED);
				ia64_psr(&regs)->db = 1;
			}

			/*
			 * Just clear PSR.lp and then return immediately:
			 * all the interesting work (e.g., signal delivery)
			 * is done in the kernel exit path.
			 */
			ia64_psr(&regs)->lp = 0;
			return;
		} else {
			/* Unimplemented Instr. Address Trap */
			if (user_mode(&regs)) {
				siginfo.si_signo = SIGILL;
				siginfo.si_code = ILL_BADIADDR;
				siginfo.si_errno = 0;
				siginfo.si_flags = 0;
				siginfo.si_isr = 0;
				siginfo.si_imm = 0;
				siginfo.si_addr = (void __user *) iip;
				force_sig_info(SIGILL, &siginfo, current);
				return;
			}
			sprintf(buf, "Unimplemented Instruction Address fault");
		}
		break;

	      case 45:
#ifdef CONFIG_IA32_SUPPORT
		if (ia32_exception(&regs, isr) == 0)
			return;
#endif
		printk(KERN_ERR "Unexpected IA-32 exception (Trap 45)\n");
		printk(KERN_ERR "  iip - 0x%lx, ifa - 0x%lx, isr - 0x%lx\n",
		       iip, ifa, isr);
		force_sig(SIGSEGV, current);
		break;

	      case 46:
#ifdef CONFIG_IA32_SUPPORT
		if (ia32_intercept(&regs, isr) == 0)
			return;
#endif
		printk(KERN_ERR "Unexpected IA-32 intercept trap (Trap 46)\n");
		printk(KERN_ERR "  iip - 0x%lx, ifa - 0x%lx, isr - 0x%lx, iim - 0x%lx\n",
		       iip, ifa, isr, iim);
		force_sig(SIGSEGV, current);
		return;

	      case 47:
		sprintf(buf, "IA-32 Interruption Fault (int 0x%lx)", isr >> 16);
		break;

	      default:
		sprintf(buf, "Fault %lu", vector);
		break;
	}
	die_if_kernel(buf, &regs, error);
	force_sig(SIGILL, current);
}
