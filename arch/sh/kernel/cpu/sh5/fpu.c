/*
 * arch/sh/kernel/cpu/sh5/fpu.c
 *
 * Copyright (C) 2001  Manuela Cirronis, Paolo Alberelli
 * Copyright (C) 2002  STMicroelectronics Limited
 *   Author : Stuart Menefy
 *
 * Started from SH4 version:
 *   Copyright (C) 1999, 2000  Kaz Kojima & Niibe Yutaka
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/sched.h>
#include <linux/signal.h>
#include <asm/processor.h>
#include <asm/user.h>
#include <asm/io.h>

/*
 * Initially load the FPU with signalling NANS.  This bit pattern
 * has the property that no matter whether considered as single or as
 * double precision, it still represents a signalling NAN.
 */
#define sNAN64		0xFFFFFFFFFFFFFFFFULL
#define sNAN32		0xFFFFFFFFUL

static union sh_fpu_union init_fpuregs = {
	.hard = {
		.fp_regs = { [0 ... 63] = sNAN32 },
		.fpscr = FPSCR_INIT
	}
};

void save_fpu(struct task_struct *tsk, struct pt_regs *regs)
{
	asm volatile("fst.p     %0, (0*8), fp0\n\t"
		     "fst.p     %0, (1*8), fp2\n\t"
		     "fst.p     %0, (2*8), fp4\n\t"
		     "fst.p     %0, (3*8), fp6\n\t"
		     "fst.p     %0, (4*8), fp8\n\t"
		     "fst.p     %0, (5*8), fp10\n\t"
		     "fst.p     %0, (6*8), fp12\n\t"
		     "fst.p     %0, (7*8), fp14\n\t"
		     "fst.p     %0, (8*8), fp16\n\t"
		     "fst.p     %0, (9*8), fp18\n\t"
		     "fst.p     %0, (10*8), fp20\n\t"
		     "fst.p     %0, (11*8), fp22\n\t"
		     "fst.p     %0, (12*8), fp24\n\t"
		     "fst.p     %0, (13*8), fp26\n\t"
		     "fst.p     %0, (14*8), fp28\n\t"
		     "fst.p     %0, (15*8), fp30\n\t"
		     "fst.p     %0, (16*8), fp32\n\t"
		     "fst.p     %0, (17*8), fp34\n\t"
		     "fst.p     %0, (18*8), fp36\n\t"
		     "fst.p     %0, (19*8), fp38\n\t"
		     "fst.p     %0, (20*8), fp40\n\t"
		     "fst.p     %0, (21*8), fp42\n\t"
		     "fst.p     %0, (22*8), fp44\n\t"
		     "fst.p     %0, (23*8), fp46\n\t"
		     "fst.p     %0, (24*8), fp48\n\t"
		     "fst.p     %0, (25*8), fp50\n\t"
		     "fst.p     %0, (26*8), fp52\n\t"
		     "fst.p     %0, (27*8), fp54\n\t"
		     "fst.p     %0, (28*8), fp56\n\t"
		     "fst.p     %0, (29*8), fp58\n\t"
		     "fst.p     %0, (30*8), fp60\n\t"
		     "fst.p     %0, (31*8), fp62\n\t"

		     "fgetscr   fr63\n\t"
		     "fst.s     %0, (32*8), fr63\n\t"
		: /* no output */
		: "r" (&tsk->thread.fpu.hard)
		: "memory");
}

static inline void
fpload(struct sh_fpu_hard_struct *fpregs)
{
	asm volatile("fld.p     %0, (0*8), fp0\n\t"
		     "fld.p     %0, (1*8), fp2\n\t"
		     "fld.p     %0, (2*8), fp4\n\t"
		     "fld.p     %0, (3*8), fp6\n\t"
		     "fld.p     %0, (4*8), fp8\n\t"
		     "fld.p     %0, (5*8), fp10\n\t"
		     "fld.p     %0, (6*8), fp12\n\t"
		     "fld.p     %0, (7*8), fp14\n\t"
		     "fld.p     %0, (8*8), fp16\n\t"
		     "fld.p     %0, (9*8), fp18\n\t"
		     "fld.p     %0, (10*8), fp20\n\t"
		     "fld.p     %0, (11*8), fp22\n\t"
		     "fld.p     %0, (12*8), fp24\n\t"
		     "fld.p     %0, (13*8), fp26\n\t"
		     "fld.p     %0, (14*8), fp28\n\t"
		     "fld.p     %0, (15*8), fp30\n\t"
		     "fld.p     %0, (16*8), fp32\n\t"
		     "fld.p     %0, (17*8), fp34\n\t"
		     "fld.p     %0, (18*8), fp36\n\t"
		     "fld.p     %0, (19*8), fp38\n\t"
		     "fld.p     %0, (20*8), fp40\n\t"
		     "fld.p     %0, (21*8), fp42\n\t"
		     "fld.p     %0, (22*8), fp44\n\t"
		     "fld.p     %0, (23*8), fp46\n\t"
		     "fld.p     %0, (24*8), fp48\n\t"
		     "fld.p     %0, (25*8), fp50\n\t"
		     "fld.p     %0, (26*8), fp52\n\t"
		     "fld.p     %0, (27*8), fp54\n\t"
		     "fld.p     %0, (28*8), fp56\n\t"
		     "fld.p     %0, (29*8), fp58\n\t"
		     "fld.p     %0, (30*8), fp60\n\t"

		     "fld.s     %0, (32*8), fr63\n\t"
		     "fputscr   fr63\n\t"

		     "fld.p     %0, (31*8), fp62\n\t"
		: /* no output */
		: "r" (fpregs) );
}

void fpinit(struct sh_fpu_hard_struct *fpregs)
{
	*fpregs = init_fpuregs.hard;
}

asmlinkage void
do_fpu_error(unsigned long ex, struct pt_regs *regs)
{
	struct task_struct *tsk = current;

	regs->pc += 4;

	tsk->thread.trap_no = 11;
	tsk->thread.error_code = 0;
	force_sig(SIGFPE, tsk);
}


asmlinkage void
do_fpu_state_restore(unsigned long ex, struct pt_regs *regs)
{
	void die(const char *str, struct pt_regs *regs, long err);

	if (! user_mode(regs))
		die("FPU used in kernel", regs, ex);

	regs->sr &= ~SR_FD;

	if (last_task_used_math == current)
		return;

	enable_fpu();
	if (last_task_used_math != NULL)
		/* Other processes fpu state, save away */
		save_fpu(last_task_used_math, regs);

        last_task_used_math = current;
        if (used_math()) {
                fpload(&current->thread.fpu.hard);
        } else {
		/* First time FPU user.  */
		fpload(&init_fpuregs.hard);
                set_used_math();
        }
	disable_fpu();
}
