// SPDX-License-Identifier: GPL-2.0

#include <linux/ptrace.h>
#include <asm/bugs.h>
#include <asm/msr.h>
#include <asm/traps.h>

enum cp_error_code {
	CP_EC        = (1 << 15) - 1,

	CP_RET       = 1,
	CP_IRET      = 2,
	CP_ENDBR     = 3,
	CP_RSTRORSSP = 4,
	CP_SETSSBSY  = 5,

	CP_ENCL	     = 1 << 15,
};

static const char cp_err[][10] = {
	[0] = "unknown",
	[1] = "near ret",
	[2] = "far/iret",
	[3] = "endbranch",
	[4] = "rstorssp",
	[5] = "setssbsy",
};

static const char *cp_err_string(unsigned long error_code)
{
	unsigned int cpec = error_code & CP_EC;

	if (cpec >= ARRAY_SIZE(cp_err))
		cpec = 0;
	return cp_err[cpec];
}

static void do_unexpected_cp(struct pt_regs *regs, unsigned long error_code)
{
	WARN_ONCE(1, "Unexpected %s #CP, error_code: %s\n",
		  user_mode(regs) ? "user mode" : "kernel mode",
		  cp_err_string(error_code));
}

static DEFINE_RATELIMIT_STATE(cpf_rate, DEFAULT_RATELIMIT_INTERVAL,
			      DEFAULT_RATELIMIT_BURST);

static void do_user_cp_fault(struct pt_regs *regs, unsigned long error_code)
{
	struct task_struct *tsk;
	unsigned long ssp;

	/*
	 * An exception was just taken from userspace. Since interrupts are disabled
	 * here, no scheduling should have messed with the registers yet and they
	 * will be whatever is live in userspace. So read the SSP before enabling
	 * interrupts so locking the fpregs to do it later is not required.
	 */
	rdmsrq(MSR_IA32_PL3_SSP, ssp);

	cond_local_irq_enable(regs);

	tsk = current;
	tsk->thread.error_code = error_code;
	tsk->thread.trap_nr = X86_TRAP_CP;

	/* Ratelimit to prevent log spamming. */
	if (show_unhandled_signals && unhandled_signal(tsk, SIGSEGV) &&
	    __ratelimit(&cpf_rate)) {
		pr_emerg("%s[%d] control protection ip:%lx sp:%lx ssp:%lx error:%lx(%s)%s",
			 tsk->comm, task_pid_nr(tsk),
			 regs->ip, regs->sp, ssp, error_code,
			 cp_err_string(error_code),
			 error_code & CP_ENCL ? " in enclave" : "");
		print_vma_addr(KERN_CONT " in ", regs->ip);
		pr_cont("\n");
	}

	force_sig_fault(SIGSEGV, SEGV_CPERR, (void __user *)0);
	cond_local_irq_disable(regs);
}

static __ro_after_init bool ibt_fatal = true;

/*
 * By definition, all missing-ENDBRANCH #CPs are a result of WFE && !ENDBR.
 *
 * For the kernel IBT no ENDBR selftest where #CPs are deliberately triggered,
 * the WFE state of the interrupted context needs to be cleared to let execution
 * continue.  Otherwise when the CPU resumes from the instruction that just
 * caused the previous #CP, another missing-ENDBRANCH #CP is raised and the CPU
 * enters a dead loop.
 *
 * This is not a problem with IDT because it doesn't preserve WFE and IRET doesn't
 * set WFE.  But FRED provides space on the entry stack (in an expanded CS area)
 * to save and restore the WFE state, thus the WFE state is no longer clobbered,
 * so software must clear it.
 */
static void ibt_clear_fred_wfe(struct pt_regs *regs)
{
	/*
	 * No need to do any FRED checks.
	 *
	 * For IDT event delivery, the high-order 48 bits of CS are pushed
	 * as 0s into the stack, and later IRET ignores these bits.
	 *
	 * For FRED, a test to check if fred_cs.wfe is set would be dropped
	 * by compilers.
	 */
	regs->fred_cs.wfe = 0;
}

static void do_kernel_cp_fault(struct pt_regs *regs, unsigned long error_code)
{
	if ((error_code & CP_EC) != CP_ENDBR) {
		do_unexpected_cp(regs, error_code);
		return;
	}

	if (unlikely(regs->ip == (unsigned long)&ibt_selftest_noendbr)) {
		regs->ax = 0;
		ibt_clear_fred_wfe(regs);
		return;
	}

	pr_err("Missing ENDBR: %pS\n", (void *)instruction_pointer(regs));
	if (!ibt_fatal) {
		printk(KERN_DEFAULT CUT_HERE);
		__warn(__FILE__, __LINE__, (void *)regs->ip, TAINT_WARN, regs, NULL);
		ibt_clear_fred_wfe(regs);
		return;
	}
	BUG();
}

static int __init ibt_setup(char *str)
{
	if (!strcmp(str, "off"))
		setup_clear_cpu_cap(X86_FEATURE_IBT);

	if (!strcmp(str, "warn"))
		ibt_fatal = false;

	return 1;
}

__setup("ibt=", ibt_setup);

DEFINE_IDTENTRY_ERRORCODE(exc_control_protection)
{
	if (user_mode(regs)) {
		if (cpu_feature_enabled(X86_FEATURE_USER_SHSTK))
			do_user_cp_fault(regs, error_code);
		else
			do_unexpected_cp(regs, error_code);
	} else {
		if (cpu_feature_enabled(X86_FEATURE_IBT))
			do_kernel_cp_fault(regs, error_code);
		else
			do_unexpected_cp(regs, error_code);
	}
}
