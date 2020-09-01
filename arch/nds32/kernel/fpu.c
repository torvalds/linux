// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2018 Andes Technology Corporation

#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/sched/signal.h>
#include <asm/processor.h>
#include <asm/user.h>
#include <asm/io.h>
#include <asm/bitfield.h>
#include <asm/fpu.h>

const struct fpu_struct init_fpuregs = {
	.fd_regs = {[0 ... 31] = sNAN64},
	.fpcsr = FPCSR_INIT,
#if IS_ENABLED(CONFIG_SUPPORT_DENORMAL_ARITHMETIC)
	.UDF_IEX_trap = 0
#endif
};

void save_fpu(struct task_struct *tsk)
{
	unsigned int fpcfg, fpcsr;

	enable_fpu();
	fpcfg = ((__nds32__fmfcfg() & FPCFG_mskFREG) >> FPCFG_offFREG);
	switch (fpcfg) {
	case SP32_DP32_reg:
		asm volatile ("fsdi $fd31, [%0+0xf8]\n\t"
			      "fsdi $fd30, [%0+0xf0]\n\t"
			      "fsdi $fd29, [%0+0xe8]\n\t"
			      "fsdi $fd28, [%0+0xe0]\n\t"
			      "fsdi $fd27, [%0+0xd8]\n\t"
			      "fsdi $fd26, [%0+0xd0]\n\t"
			      "fsdi $fd25, [%0+0xc8]\n\t"
			      "fsdi $fd24, [%0+0xc0]\n\t"
			      "fsdi $fd23, [%0+0xb8]\n\t"
			      "fsdi $fd22, [%0+0xb0]\n\t"
			      "fsdi $fd21, [%0+0xa8]\n\t"
			      "fsdi $fd20, [%0+0xa0]\n\t"
			      "fsdi $fd19, [%0+0x98]\n\t"
			      "fsdi $fd18, [%0+0x90]\n\t"
			      "fsdi $fd17, [%0+0x88]\n\t"
			      "fsdi $fd16, [%0+0x80]\n\t"
			      :	/* no output */
			      : "r" (&tsk->thread.fpu)
			      : "memory");
		fallthrough;
	case SP32_DP16_reg:
		asm volatile ("fsdi $fd15, [%0+0x78]\n\t"
			      "fsdi $fd14, [%0+0x70]\n\t"
			      "fsdi $fd13, [%0+0x68]\n\t"
			      "fsdi $fd12, [%0+0x60]\n\t"
			      "fsdi $fd11, [%0+0x58]\n\t"
			      "fsdi $fd10, [%0+0x50]\n\t"
			      "fsdi $fd9,  [%0+0x48]\n\t"
			      "fsdi $fd8,  [%0+0x40]\n\t"
			      :	/* no output */
			      : "r" (&tsk->thread.fpu)
			      : "memory");
		fallthrough;
	case SP16_DP8_reg:
		asm volatile ("fsdi $fd7,  [%0+0x38]\n\t"
			      "fsdi $fd6,  [%0+0x30]\n\t"
			      "fsdi $fd5,  [%0+0x28]\n\t"
			      "fsdi $fd4,  [%0+0x20]\n\t"
			      :	/* no output */
			      : "r" (&tsk->thread.fpu)
			      : "memory");
		fallthrough;
	case SP8_DP4_reg:
		asm volatile ("fsdi $fd3,  [%1+0x18]\n\t"
			      "fsdi $fd2,  [%1+0x10]\n\t"
			      "fsdi $fd1,  [%1+0x8]\n\t"
			      "fsdi $fd0,  [%1+0x0]\n\t"
			      "fmfcsr	%0\n\t"
			      "swi  %0, [%1+0x100]\n\t"
			      : "=&r" (fpcsr)
			      : "r"(&tsk->thread.fpu)
			      : "memory");
	}
	disable_fpu();
}

void load_fpu(const struct fpu_struct *fpregs)
{
	unsigned int fpcfg, fpcsr;

	enable_fpu();
	fpcfg = ((__nds32__fmfcfg() & FPCFG_mskFREG) >> FPCFG_offFREG);
	switch (fpcfg) {
	case SP32_DP32_reg:
		asm volatile ("fldi $fd31, [%0+0xf8]\n\t"
			      "fldi $fd30, [%0+0xf0]\n\t"
			      "fldi $fd29, [%0+0xe8]\n\t"
			      "fldi $fd28, [%0+0xe0]\n\t"
			      "fldi $fd27, [%0+0xd8]\n\t"
			      "fldi $fd26, [%0+0xd0]\n\t"
			      "fldi $fd25, [%0+0xc8]\n\t"
			      "fldi $fd24, [%0+0xc0]\n\t"
			      "fldi $fd23, [%0+0xb8]\n\t"
			      "fldi $fd22, [%0+0xb0]\n\t"
			      "fldi $fd21, [%0+0xa8]\n\t"
			      "fldi $fd20, [%0+0xa0]\n\t"
			      "fldi $fd19, [%0+0x98]\n\t"
			      "fldi $fd18, [%0+0x90]\n\t"
			      "fldi $fd17, [%0+0x88]\n\t"
			      "fldi $fd16, [%0+0x80]\n\t"
			      :	/* no output */
			      : "r" (fpregs));
		fallthrough;
	case SP32_DP16_reg:
		asm volatile ("fldi $fd15, [%0+0x78]\n\t"
			      "fldi $fd14, [%0+0x70]\n\t"
			      "fldi $fd13, [%0+0x68]\n\t"
			      "fldi $fd12, [%0+0x60]\n\t"
			      "fldi $fd11, [%0+0x58]\n\t"
			      "fldi $fd10, [%0+0x50]\n\t"
			      "fldi $fd9,  [%0+0x48]\n\t"
			      "fldi $fd8,  [%0+0x40]\n\t"
			      :	/* no output */
			      : "r" (fpregs));
		fallthrough;
	case SP16_DP8_reg:
		asm volatile ("fldi $fd7,  [%0+0x38]\n\t"
			      "fldi $fd6,  [%0+0x30]\n\t"
			      "fldi $fd5,  [%0+0x28]\n\t"
			      "fldi $fd4,  [%0+0x20]\n\t"
			      :	/* no output */
			      : "r" (fpregs));
		fallthrough;
	case SP8_DP4_reg:
		asm volatile ("fldi $fd3,  [%1+0x18]\n\t"
			      "fldi $fd2,  [%1+0x10]\n\t"
			      "fldi $fd1,  [%1+0x8]\n\t"
			      "fldi $fd0,  [%1+0x0]\n\t"
			      "lwi  %0, [%1+0x100]\n\t"
			      "fmtcsr	%0\n\t":"=&r" (fpcsr)
			      : "r"(fpregs));
	}
	disable_fpu();
}
void store_fpu_for_suspend(void)
{
#ifdef CONFIG_LAZY_FPU
	if (last_task_used_math != NULL)
		save_fpu(last_task_used_math);
	last_task_used_math = NULL;
#else
	if (!used_math())
		return;
	unlazy_fpu(current);
#endif
	clear_fpu(task_pt_regs(current));
}
inline void do_fpu_context_switch(struct pt_regs *regs)
{
	/* Enable to use FPU. */

	if (!user_mode(regs)) {
		pr_err("BUG: FPU is used in kernel mode.\n");
		BUG();
		return;
	}

	enable_ptreg_fpu(regs);
#ifdef CONFIG_LAZY_FPU	//Lazy FPU is used
	if (last_task_used_math == current)
		return;
	if (last_task_used_math != NULL)
		/* Other processes fpu state, save away */
		save_fpu(last_task_used_math);
	last_task_used_math = current;
#endif
	if (used_math()) {
		load_fpu(&current->thread.fpu);
	} else {
		/* First time FPU user.  */
		load_fpu(&init_fpuregs);
#if IS_ENABLED(CONFIG_SUPPORT_DENORMAL_ARITHMETIC)
		current->thread.fpu.UDF_IEX_trap = init_fpuregs.UDF_IEX_trap;
#endif
		set_used_math();
	}

}

inline void fill_sigfpe_signo(unsigned int fpcsr, int *signo)
{
	if (fpcsr & FPCSR_mskOVFT)
		*signo = FPE_FLTOVF;
#ifndef CONFIG_SUPPORT_DENORMAL_ARITHMETIC
	else if (fpcsr & FPCSR_mskUDFT)
		*signo = FPE_FLTUND;
#endif
	else if (fpcsr & FPCSR_mskIVOT)
		*signo = FPE_FLTINV;
	else if (fpcsr & FPCSR_mskDBZT)
		*signo = FPE_FLTDIV;
	else if (fpcsr & FPCSR_mskIEXT)
		*signo = FPE_FLTRES;
}

inline void handle_fpu_exception(struct pt_regs *regs)
{
	unsigned int fpcsr;
	int si_code = 0, si_signo = SIGFPE;
#if IS_ENABLED(CONFIG_SUPPORT_DENORMAL_ARITHMETIC)
	unsigned long redo_except = FPCSR_mskDNIT|FPCSR_mskUDFT|FPCSR_mskIEXT;
#else
	unsigned long redo_except = FPCSR_mskDNIT;
#endif

	lose_fpu();
	fpcsr = current->thread.fpu.fpcsr;

	if (fpcsr & redo_except) {
		si_signo = do_fpuemu(regs, &current->thread.fpu);
		fpcsr = current->thread.fpu.fpcsr;
		if (!si_signo) {
			current->thread.fpu.fpcsr &= ~(redo_except);
			goto done;
		}
	} else if (fpcsr & FPCSR_mskRIT) {
		if (!user_mode(regs))
			do_exit(SIGILL);
		si_signo = SIGILL;
	}

	switch (si_signo) {
	case SIGFPE:
		fill_sigfpe_signo(fpcsr, &si_code);
		break;
	case SIGILL:
		show_regs(regs);
		si_code = ILL_COPROC;
		break;
	case SIGBUS:
		si_code = BUS_ADRERR;
		break;
	default:
		break;
	}

	force_sig_fault(si_signo, si_code,
			(void __user *)instruction_pointer(regs));
done:
	own_fpu();
}

bool do_fpu_exception(unsigned int subtype, struct pt_regs *regs)
{
	int done = true;
	/* Coprocessor disabled exception */
	if (subtype == FPU_DISABLE_EXCEPTION) {
		preempt_disable();
		do_fpu_context_switch(regs);
		preempt_enable();
	}
	/* Coprocessor exception such as underflow and overflow */
	else if (subtype == FPU_EXCEPTION)
		handle_fpu_exception(regs);
	else
		done = false;
	return done;
}
