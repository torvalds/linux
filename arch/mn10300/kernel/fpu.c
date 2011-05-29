/* MN10300 FPU management
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <asm/uaccess.h>
#include <asm/fpu.h>
#include <asm/elf.h>
#include <asm/exceptions.h>
#include <asm/system.h>

#ifdef CONFIG_LAZY_SAVE_FPU
struct task_struct *fpu_state_owner;
#endif

/*
 * error functions in FPU disabled exception
 */
asmlinkage void fpu_disabled_in_kernel(struct pt_regs *regs)
{
	die_if_no_fixup("An FPU Disabled exception happened in kernel space\n",
			regs, EXCEP_FPU_DISABLED);
}

/*
 * handle an FPU operational exception
 * - there's a possibility that if the FPU is asynchronous, the signal might
 *   be meant for a process other than the current one
 */
asmlinkage void fpu_exception(struct pt_regs *regs, enum exception_code code)
{
	struct task_struct *tsk = current;
	siginfo_t info;
	u32 fpcr;

	if (!user_mode(regs))
		die_if_no_fixup("An FPU Operation exception happened in"
				" kernel space\n",
				regs, code);

	if (!is_using_fpu(tsk))
		die_if_no_fixup("An FPU Operation exception happened,"
				" but the FPU is not in use",
				regs, code);

	info.si_signo = SIGFPE;
	info.si_errno = 0;
	info.si_addr = (void *) tsk->thread.uregs->pc;
	info.si_code = FPE_FLTINV;

	unlazy_fpu(tsk);

	fpcr = tsk->thread.fpu_state.fpcr;

	if (fpcr & FPCR_EC_Z)
		info.si_code = FPE_FLTDIV;
	else if	(fpcr & FPCR_EC_O)
		info.si_code = FPE_FLTOVF;
	else if	(fpcr & FPCR_EC_U)
		info.si_code = FPE_FLTUND;
	else if	(fpcr & FPCR_EC_I)
		info.si_code = FPE_FLTRES;

	force_sig_info(SIGFPE, &info, tsk);
}

/*
 * save the FPU state to a signal context
 */
int fpu_setup_sigcontext(struct fpucontext *fpucontext)
{
	struct task_struct *tsk = current;

	if (!is_using_fpu(tsk))
		return 0;

	/* transfer the current FPU state to memory and cause fpu_init() to be
	 * triggered by the next attempted FPU operation by the current
	 * process.
	 */
	preempt_disable();

#ifndef CONFIG_LAZY_SAVE_FPU
	if (tsk->thread.fpu_flags & THREAD_HAS_FPU) {
		fpu_save(&tsk->thread.fpu_state);
		tsk->thread.uregs->epsw &= ~EPSW_FE;
		tsk->thread.fpu_flags &= ~THREAD_HAS_FPU;
	}
#else /* !CONFIG_LAZY_SAVE_FPU */
	if (fpu_state_owner == tsk) {
		fpu_save(&tsk->thread.fpu_state);
		fpu_state_owner->thread.uregs->epsw &= ~EPSW_FE;
		fpu_state_owner = NULL;
	}
#endif /* !CONFIG_LAZY_SAVE_FPU */

	preempt_enable();

	/* we no longer have a valid current FPU state */
	clear_using_fpu(tsk);

	/* transfer the saved FPU state onto the userspace stack */
	if (copy_to_user(fpucontext,
			 &tsk->thread.fpu_state,
			 min(sizeof(struct fpu_state_struct),
			     sizeof(struct fpucontext))))
		return -1;

	return 1;
}

/*
 * kill a process's FPU state during restoration after signal handling
 */
void fpu_kill_state(struct task_struct *tsk)
{
	/* disown anything left in the FPU */
	preempt_disable();

#ifndef CONFIG_LAZY_SAVE_FPU
	if (tsk->thread.fpu_flags & THREAD_HAS_FPU) {
		tsk->thread.uregs->epsw &= ~EPSW_FE;
		tsk->thread.fpu_flags &= ~THREAD_HAS_FPU;
	}
#else /* !CONFIG_LAZY_SAVE_FPU */
	if (fpu_state_owner == tsk) {
		fpu_state_owner->thread.uregs->epsw &= ~EPSW_FE;
		fpu_state_owner = NULL;
	}
#endif /* !CONFIG_LAZY_SAVE_FPU */

	preempt_enable();

	/* we no longer have a valid current FPU state */
	clear_using_fpu(tsk);
}

/*
 * restore the FPU state from a signal context
 */
int fpu_restore_sigcontext(struct fpucontext *fpucontext)
{
	struct task_struct *tsk = current;
	int ret;

	/* load up the old FPU state */
	ret = copy_from_user(&tsk->thread.fpu_state, fpucontext,
			     min(sizeof(struct fpu_state_struct),
				 sizeof(struct fpucontext)));
	if (!ret)
		set_using_fpu(tsk);

	return ret;
}

/*
 * fill in the FPU structure for a core dump
 */
int dump_fpu(struct pt_regs *regs, elf_fpregset_t *fpreg)
{
	struct task_struct *tsk = current;
	int fpvalid;

	fpvalid = is_using_fpu(tsk);
	if (fpvalid) {
		unlazy_fpu(tsk);
		memcpy(fpreg, &tsk->thread.fpu_state, sizeof(*fpreg));
	}

	return fpvalid;
}
