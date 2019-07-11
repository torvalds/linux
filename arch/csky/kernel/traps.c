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

#include <asm/setup.h>
#include <asm/traps.h>
#include <asm/pgalloc.h>
#include <asm/siginfo.h>

#include <asm/mmu_context.h>

#ifdef CONFIG_CPU_HAS_FPU
#include <abi/fpu.h>
#endif

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

void die_if_kernel(char *str, struct pt_regs *regs, int nr)
{
	if (user_mode(regs))
		return;

	console_verbose();
	pr_err("%s: %08x\n", str, nr);
	show_regs(regs);
	add_taint(TAINT_DIE, LOCKDEP_NOW_UNRELIABLE);
	do_exit(SIGSEGV);
}

void buserr(struct pt_regs *regs)
{
#ifdef CONFIG_CPU_CK810
	static unsigned long prev_pc;

	if ((regs->pc == prev_pc) && prev_pc != 0) {
		prev_pc = 0;
	} else {
		prev_pc = regs->pc;
		return;
	}
#endif

	die_if_kernel("Kernel mode BUS error", regs, 0);

	pr_err("User mode Bus Error\n");
	show_regs(regs);

	force_sig_fault(SIGSEGV, 0, (void __user *)regs->pc, current);
}

#define USR_BKPT 0x1464
asmlinkage void trap_c(struct pt_regs *regs)
{
	int sig;
	unsigned long vector;
	siginfo_t info;

	vector = (mfcr("psr") >> 16) & 0xff;

	switch (vector) {
	case VEC_ZERODIV:
		sig = SIGFPE;
		break;
	/* ptrace */
	case VEC_TRACE:
		info.si_code = TRAP_TRACE;
		sig = SIGTRAP;
		break;
	case VEC_ILLEGAL:
#ifndef CONFIG_CPU_NO_USER_BKPT
		if (*(uint16_t *)instruction_pointer(regs) != USR_BKPT)
#endif
		{
			sig = SIGILL;
			break;
		}
	/* gdbserver  breakpoint */
	case VEC_TRAP1:
	/* jtagserver breakpoint */
	case VEC_BREAKPOINT:
		info.si_code = TRAP_BRKPT;
		sig = SIGTRAP;
		break;
	case VEC_ACCESS:
		return buserr(regs);
#ifdef CONFIG_CPU_NEED_SOFTALIGN
	case VEC_ALIGN:
		return csky_alignment(regs);
#endif
#ifdef CONFIG_CPU_HAS_FPU
	case VEC_FPE:
		return fpu_fpe(regs);
	case VEC_PRIV:
		if (fpu_libc_helper(regs))
			return;
#endif
	default:
		sig = SIGSEGV;
		break;
	}
	send_sig(sig, current, 0);
}
