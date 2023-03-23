// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/user.h>
#include <linux/string.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/kallsyms.h>
#include <linux/rtc.h>
#include <linux/uaccess.h>
#include <linux/kprobes.h>
#include <linux/kdebug.h>
#include <linux/sched/debug.h>

#include <asm/setup.h>
#include <asm/traps.h>
#include <asm/pgalloc.h>
#include <asm/siginfo.h>

#include <asm/mmu_context.h>

#ifdef CONFIG_CPU_HAS_FPU
#include <abi/fpu.h>
#endif

int show_unhandled_signals = 1;

/* Defined in entry.S */
asmlinkage void csky_trap(void);

asmlinkage void csky_systemcall(void);
asmlinkage void csky_cmpxchg(void);
asmlinkage void csky_get_tls(void);
asmlinkage void csky_irq(void);

asmlinkage void csky_tlbinvalidl(void);
asmlinkage void csky_tlbinvalids(void);
asmlinkage void csky_tlbmodified(void);

/* Defined in head.S */
asmlinkage void _start_smp_secondary(void);

void __init pre_trap_init(void)
{
	int i;

	mtcr("vbr", vec_base);

	for (i = 1; i < 128; i++)
		VEC_INIT(i, csky_trap);
}

void __init trap_init(void)
{
	VEC_INIT(VEC_AUTOVEC, csky_irq);

	/* setup trap0 trap2 trap3 */
	VEC_INIT(VEC_TRAP0, csky_systemcall);
	VEC_INIT(VEC_TRAP2, csky_cmpxchg);
	VEC_INIT(VEC_TRAP3, csky_get_tls);

	/* setup MMU TLB exception */
	VEC_INIT(VEC_TLBINVALIDL, csky_tlbinvalidl);
	VEC_INIT(VEC_TLBINVALIDS, csky_tlbinvalids);
	VEC_INIT(VEC_TLBMODIFIED, csky_tlbmodified);

#ifdef CONFIG_CPU_HAS_FPU
	init_fpu();
#endif

#ifdef CONFIG_SMP
	mtcr("cr<28, 0>", virt_to_phys(vec_base));

	VEC_INIT(VEC_RESET, (void *)virt_to_phys(_start_smp_secondary));
#endif
}

static DEFINE_SPINLOCK(die_lock);

void die(struct pt_regs *regs, const char *str)
{
	static int die_counter;
	int ret;

	oops_enter();

	spin_lock_irq(&die_lock);
	console_verbose();
	bust_spinlocks(1);

	pr_emerg("%s [#%d]\n", str, ++die_counter);
	print_modules();
	show_regs(regs);
	show_stack(current, (unsigned long *)regs->regs[4], KERN_INFO);

	ret = notify_die(DIE_OOPS, str, regs, 0, trap_no(regs), SIGSEGV);

	bust_spinlocks(0);
	add_taint(TAINT_DIE, LOCKDEP_NOW_UNRELIABLE);
	spin_unlock_irq(&die_lock);
	oops_exit();

	if (in_interrupt())
		panic("Fatal exception in interrupt");
	if (panic_on_oops)
		panic("Fatal exception");
	if (ret != NOTIFY_STOP)
		make_task_dead(SIGSEGV);
}

void do_trap(struct pt_regs *regs, int signo, int code, unsigned long addr)
{
	struct task_struct *tsk = current;

	if (show_unhandled_signals && unhandled_signal(tsk, signo)
	    && printk_ratelimit()) {
		pr_info("%s[%d]: unhandled signal %d code 0x%x at 0x%08lx",
			tsk->comm, task_pid_nr(tsk), signo, code, addr);
		print_vma_addr(KERN_CONT " in ", instruction_pointer(regs));
		pr_cont("\n");
		show_regs(regs);
	}

	force_sig_fault(signo, code, (void __user *)addr);
}

static void do_trap_error(struct pt_regs *regs, int signo, int code,
	unsigned long addr, const char *str)
{
	current->thread.trap_no = trap_no(regs);

	if (user_mode(regs)) {
		do_trap(regs, signo, code, addr);
	} else {
		if (!fixup_exception(regs))
			die(regs, str);
	}
}

#define DO_ERROR_INFO(name, signo, code, str)				\
asmlinkage __visible void name(struct pt_regs *regs)			\
{									\
	do_trap_error(regs, signo, code, regs->pc, "Oops - " str);	\
}

DO_ERROR_INFO(do_trap_unknown,
	SIGILL, ILL_ILLTRP, "unknown exception");
DO_ERROR_INFO(do_trap_zdiv,
	SIGFPE, FPE_INTDIV, "error zero div exception");
DO_ERROR_INFO(do_trap_buserr,
	SIGSEGV, ILL_ILLADR, "error bus error exception");

asmlinkage void do_trap_misaligned(struct pt_regs *regs)
{
#ifdef CONFIG_CPU_NEED_SOFTALIGN
	csky_alignment(regs);
#else
	current->thread.trap_no = trap_no(regs);
	do_trap_error(regs, SIGBUS, BUS_ADRALN, regs->pc,
		      "Oops - load/store address misaligned");
#endif
}

asmlinkage void do_trap_bkpt(struct pt_regs *regs)
{
#ifdef CONFIG_KPROBES
	if (kprobe_single_step_handler(regs))
		return;
#endif
#ifdef CONFIG_UPROBES
	if (uprobe_single_step_handler(regs))
		return;
#endif
	if (user_mode(regs)) {
		send_sig(SIGTRAP, current, 0);
		return;
	}

	do_trap_error(regs, SIGILL, ILL_ILLTRP, regs->pc,
		      "Oops - illegal trap exception");
}

asmlinkage void do_trap_illinsn(struct pt_regs *regs)
{
	current->thread.trap_no = trap_no(regs);

#ifdef CONFIG_KPROBES
	if (kprobe_breakpoint_handler(regs))
		return;
#endif
#ifdef CONFIG_UPROBES
	if (uprobe_breakpoint_handler(regs))
		return;
#endif
#ifndef CONFIG_CPU_NO_USER_BKPT
	if (*(uint16_t *)instruction_pointer(regs) != USR_BKPT) {
		send_sig(SIGTRAP, current, 0);
		return;
	}
#endif

	do_trap_error(regs, SIGILL, ILL_ILLOPC, regs->pc,
		      "Oops - illegal instruction exception");
}

asmlinkage void do_trap_fpe(struct pt_regs *regs)
{
#ifdef CONFIG_CPU_HAS_FPU
	return fpu_fpe(regs);
#else
	do_trap_error(regs, SIGILL, ILL_ILLOPC, regs->pc,
		      "Oops - fpu instruction exception");
#endif
}

asmlinkage void do_trap_priv(struct pt_regs *regs)
{
#ifdef CONFIG_CPU_HAS_FPU
	if (user_mode(regs) && fpu_libc_helper(regs))
		return;
#endif
	do_trap_error(regs, SIGILL, ILL_PRVOPC, regs->pc,
		      "Oops - illegal privileged exception");
}

asmlinkage void trap_c(struct pt_regs *regs)
{
	switch (trap_no(regs)) {
	case VEC_ZERODIV:
		do_trap_zdiv(regs);
		break;
	case VEC_TRACE:
		do_trap_bkpt(regs);
		break;
	case VEC_ILLEGAL:
		do_trap_illinsn(regs);
		break;
	case VEC_TRAP1:
	case VEC_BREAKPOINT:
		do_trap_bkpt(regs);
		break;
	case VEC_ACCESS:
		do_trap_buserr(regs);
		break;
	case VEC_ALIGN:
		do_trap_misaligned(regs);
		break;
	case VEC_FPE:
		do_trap_fpe(regs);
		break;
	case VEC_PRIV:
		do_trap_priv(regs);
		break;
	default:
		do_trap_unknown(regs);
		break;
	}
}
