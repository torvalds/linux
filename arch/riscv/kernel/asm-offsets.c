// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#define GENERATING_ASM_OFFSETS

#include <linux/kbuild.h>
#include <linux/sched.h>
#include <asm/thread_info.h>
#include <asm/ptrace.h>

void asm_offsets(void)
{
	OFFSET(TASK_THREAD_RA, task_struct, thread.ra);
	OFFSET(TASK_THREAD_SP, task_struct, thread.sp);
	OFFSET(TASK_THREAD_S0, task_struct, thread.s[0]);
	OFFSET(TASK_THREAD_S1, task_struct, thread.s[1]);
	OFFSET(TASK_THREAD_S2, task_struct, thread.s[2]);
	OFFSET(TASK_THREAD_S3, task_struct, thread.s[3]);
	OFFSET(TASK_THREAD_S4, task_struct, thread.s[4]);
	OFFSET(TASK_THREAD_S5, task_struct, thread.s[5]);
	OFFSET(TASK_THREAD_S6, task_struct, thread.s[6]);
	OFFSET(TASK_THREAD_S7, task_struct, thread.s[7]);
	OFFSET(TASK_THREAD_S8, task_struct, thread.s[8]);
	OFFSET(TASK_THREAD_S9, task_struct, thread.s[9]);
	OFFSET(TASK_THREAD_S10, task_struct, thread.s[10]);
	OFFSET(TASK_THREAD_S11, task_struct, thread.s[11]);
	OFFSET(TASK_THREAD_SP, task_struct, thread.sp);
	OFFSET(TASK_STACK, task_struct, stack);
	OFFSET(TASK_TI, task_struct, thread_info);
	OFFSET(TASK_TI_FLAGS, task_struct, thread_info.flags);
	OFFSET(TASK_TI_PREEMPT_COUNT, task_struct, thread_info.preempt_count);
	OFFSET(TASK_TI_KERNEL_SP, task_struct, thread_info.kernel_sp);
	OFFSET(TASK_TI_USER_SP, task_struct, thread_info.user_sp);
	OFFSET(TASK_TI_CPU, task_struct, thread_info.cpu);

	OFFSET(TASK_THREAD_F0,  task_struct, thread.fstate.f[0]);
	OFFSET(TASK_THREAD_F1,  task_struct, thread.fstate.f[1]);
	OFFSET(TASK_THREAD_F2,  task_struct, thread.fstate.f[2]);
	OFFSET(TASK_THREAD_F3,  task_struct, thread.fstate.f[3]);
	OFFSET(TASK_THREAD_F4,  task_struct, thread.fstate.f[4]);
	OFFSET(TASK_THREAD_F5,  task_struct, thread.fstate.f[5]);
	OFFSET(TASK_THREAD_F6,  task_struct, thread.fstate.f[6]);
	OFFSET(TASK_THREAD_F7,  task_struct, thread.fstate.f[7]);
	OFFSET(TASK_THREAD_F8,  task_struct, thread.fstate.f[8]);
	OFFSET(TASK_THREAD_F9,  task_struct, thread.fstate.f[9]);
	OFFSET(TASK_THREAD_F10, task_struct, thread.fstate.f[10]);
	OFFSET(TASK_THREAD_F11, task_struct, thread.fstate.f[11]);
	OFFSET(TASK_THREAD_F12, task_struct, thread.fstate.f[12]);
	OFFSET(TASK_THREAD_F13, task_struct, thread.fstate.f[13]);
	OFFSET(TASK_THREAD_F14, task_struct, thread.fstate.f[14]);
	OFFSET(TASK_THREAD_F15, task_struct, thread.fstate.f[15]);
	OFFSET(TASK_THREAD_F16, task_struct, thread.fstate.f[16]);
	OFFSET(TASK_THREAD_F17, task_struct, thread.fstate.f[17]);
	OFFSET(TASK_THREAD_F18, task_struct, thread.fstate.f[18]);
	OFFSET(TASK_THREAD_F19, task_struct, thread.fstate.f[19]);
	OFFSET(TASK_THREAD_F20, task_struct, thread.fstate.f[20]);
	OFFSET(TASK_THREAD_F21, task_struct, thread.fstate.f[21]);
	OFFSET(TASK_THREAD_F22, task_struct, thread.fstate.f[22]);
	OFFSET(TASK_THREAD_F23, task_struct, thread.fstate.f[23]);
	OFFSET(TASK_THREAD_F24, task_struct, thread.fstate.f[24]);
	OFFSET(TASK_THREAD_F25, task_struct, thread.fstate.f[25]);
	OFFSET(TASK_THREAD_F26, task_struct, thread.fstate.f[26]);
	OFFSET(TASK_THREAD_F27, task_struct, thread.fstate.f[27]);
	OFFSET(TASK_THREAD_F28, task_struct, thread.fstate.f[28]);
	OFFSET(TASK_THREAD_F29, task_struct, thread.fstate.f[29]);
	OFFSET(TASK_THREAD_F30, task_struct, thread.fstate.f[30]);
	OFFSET(TASK_THREAD_F31, task_struct, thread.fstate.f[31]);
	OFFSET(TASK_THREAD_FCSR, task_struct, thread.fstate.fcsr);

	DEFINE(PT_SIZE, sizeof(struct pt_regs));
	OFFSET(PT_SEPC, pt_regs, sepc);
	OFFSET(PT_RA, pt_regs, ra);
	OFFSET(PT_FP, pt_regs, s0);
	OFFSET(PT_S0, pt_regs, s0);
	OFFSET(PT_S1, pt_regs, s1);
	OFFSET(PT_S2, pt_regs, s2);
	OFFSET(PT_S3, pt_regs, s3);
	OFFSET(PT_S4, pt_regs, s4);
	OFFSET(PT_S5, pt_regs, s5);
	OFFSET(PT_S6, pt_regs, s6);
	OFFSET(PT_S7, pt_regs, s7);
	OFFSET(PT_S8, pt_regs, s8);
	OFFSET(PT_S9, pt_regs, s9);
	OFFSET(PT_S10, pt_regs, s10);
	OFFSET(PT_S11, pt_regs, s11);
	OFFSET(PT_SP, pt_regs, sp);
	OFFSET(PT_TP, pt_regs, tp);
	OFFSET(PT_A0, pt_regs, a0);
	OFFSET(PT_A1, pt_regs, a1);
	OFFSET(PT_A2, pt_regs, a2);
	OFFSET(PT_A3, pt_regs, a3);
	OFFSET(PT_A4, pt_regs, a4);
	OFFSET(PT_A5, pt_regs, a5);
	OFFSET(PT_A6, pt_regs, a6);
	OFFSET(PT_A7, pt_regs, a7);
	OFFSET(PT_T0, pt_regs, t0);
	OFFSET(PT_T1, pt_regs, t1);
	OFFSET(PT_T2, pt_regs, t2);
	OFFSET(PT_T3, pt_regs, t3);
	OFFSET(PT_T4, pt_regs, t4);
	OFFSET(PT_T5, pt_regs, t5);
	OFFSET(PT_T6, pt_regs, t6);
	OFFSET(PT_GP, pt_regs, gp);
	OFFSET(PT_ORIG_A0, pt_regs, orig_a0);
	OFFSET(PT_SSTATUS, pt_regs, sstatus);
	OFFSET(PT_SBADADDR, pt_regs, sbadaddr);
	OFFSET(PT_SCAUSE, pt_regs, scause);

	/*
	 * THREAD_{F,X}* might be larger than a S-type offset can handle, but
	 * these are used in performance-sensitive assembly so we can't resort
	 * to loading the long immediate every time.
	 */
	DEFINE(TASK_THREAD_RA_RA,
		  offsetof(struct task_struct, thread.ra)
		- offsetof(struct task_struct, thread.ra)
	);
	DEFINE(TASK_THREAD_SP_RA,
		  offsetof(struct task_struct, thread.sp)
		- offsetof(struct task_struct, thread.ra)
	);
	DEFINE(TASK_THREAD_S0_RA,
		  offsetof(struct task_struct, thread.s[0])
		- offsetof(struct task_struct, thread.ra)
	);
	DEFINE(TASK_THREAD_S1_RA,
		  offsetof(struct task_struct, thread.s[1])
		- offsetof(struct task_struct, thread.ra)
	);
	DEFINE(TASK_THREAD_S2_RA,
		  offsetof(struct task_struct, thread.s[2])
		- offsetof(struct task_struct, thread.ra)
	);
	DEFINE(TASK_THREAD_S3_RA,
		  offsetof(struct task_struct, thread.s[3])
		- offsetof(struct task_struct, thread.ra)
	);
	DEFINE(TASK_THREAD_S4_RA,
		  offsetof(struct task_struct, thread.s[4])
		- offsetof(struct task_struct, thread.ra)
	);
	DEFINE(TASK_THREAD_S5_RA,
		  offsetof(struct task_struct, thread.s[5])
		- offsetof(struct task_struct, thread.ra)
	);
	DEFINE(TASK_THREAD_S6_RA,
		  offsetof(struct task_struct, thread.s[6])
		- offsetof(struct task_struct, thread.ra)
	);
	DEFINE(TASK_THREAD_S7_RA,
		  offsetof(struct task_struct, thread.s[7])
		- offsetof(struct task_struct, thread.ra)
	);
	DEFINE(TASK_THREAD_S8_RA,
		  offsetof(struct task_struct, thread.s[8])
		- offsetof(struct task_struct, thread.ra)
	);
	DEFINE(TASK_THREAD_S9_RA,
		  offsetof(struct task_struct, thread.s[9])
		- offsetof(struct task_struct, thread.ra)
	);
	DEFINE(TASK_THREAD_S10_RA,
		  offsetof(struct task_struct, thread.s[10])
		- offsetof(struct task_struct, thread.ra)
	);
	DEFINE(TASK_THREAD_S11_RA,
		  offsetof(struct task_struct, thread.s[11])
		- offsetof(struct task_struct, thread.ra)
	);

	DEFINE(TASK_THREAD_F0_F0,
		  offsetof(struct task_struct, thread.fstate.f[0])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F1_F0,
		  offsetof(struct task_struct, thread.fstate.f[1])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F2_F0,
		  offsetof(struct task_struct, thread.fstate.f[2])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F3_F0,
		  offsetof(struct task_struct, thread.fstate.f[3])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F4_F0,
		  offsetof(struct task_struct, thread.fstate.f[4])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F5_F0,
		  offsetof(struct task_struct, thread.fstate.f[5])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F6_F0,
		  offsetof(struct task_struct, thread.fstate.f[6])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F7_F0,
		  offsetof(struct task_struct, thread.fstate.f[7])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F8_F0,
		  offsetof(struct task_struct, thread.fstate.f[8])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F9_F0,
		  offsetof(struct task_struct, thread.fstate.f[9])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F10_F0,
		  offsetof(struct task_struct, thread.fstate.f[10])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F11_F0,
		  offsetof(struct task_struct, thread.fstate.f[11])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F12_F0,
		  offsetof(struct task_struct, thread.fstate.f[12])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F13_F0,
		  offsetof(struct task_struct, thread.fstate.f[13])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F14_F0,
		  offsetof(struct task_struct, thread.fstate.f[14])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F15_F0,
		  offsetof(struct task_struct, thread.fstate.f[15])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F16_F0,
		  offsetof(struct task_struct, thread.fstate.f[16])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F17_F0,
		  offsetof(struct task_struct, thread.fstate.f[17])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F18_F0,
		  offsetof(struct task_struct, thread.fstate.f[18])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F19_F0,
		  offsetof(struct task_struct, thread.fstate.f[19])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F20_F0,
		  offsetof(struct task_struct, thread.fstate.f[20])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F21_F0,
		  offsetof(struct task_struct, thread.fstate.f[21])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F22_F0,
		  offsetof(struct task_struct, thread.fstate.f[22])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F23_F0,
		  offsetof(struct task_struct, thread.fstate.f[23])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F24_F0,
		  offsetof(struct task_struct, thread.fstate.f[24])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F25_F0,
		  offsetof(struct task_struct, thread.fstate.f[25])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F26_F0,
		  offsetof(struct task_struct, thread.fstate.f[26])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F27_F0,
		  offsetof(struct task_struct, thread.fstate.f[27])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F28_F0,
		  offsetof(struct task_struct, thread.fstate.f[28])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F29_F0,
		  offsetof(struct task_struct, thread.fstate.f[29])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F30_F0,
		  offsetof(struct task_struct, thread.fstate.f[30])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_F31_F0,
		  offsetof(struct task_struct, thread.fstate.f[31])
		- offsetof(struct task_struct, thread.fstate.f[0])
	);
	DEFINE(TASK_THREAD_FCSR_F0,
		  offsetof(struct task_struct, thread.fstate.fcsr)
		- offsetof(struct task_struct, thread.fstate.f[0])
	);

	/*
	 * We allocate a pt_regs on the stack when entering the kernel.  This
	 * ensures the alignment is sane.
	 */
	DEFINE(PT_SIZE_ON_STACK, ALIGN(sizeof(struct pt_regs), STACK_ALIGN));
}
