// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/kbuild.h>
#include <abi/regdef.h>

int main(void)
{
	/* offsets into the task struct */
	DEFINE(TASK_THREAD_INFO,  offsetof(struct task_struct, stack));
	DEFINE(TASK_FLAGS,        offsetof(struct task_struct, flags));
	DEFINE(TASK_PTRACE,       offsetof(struct task_struct, ptrace));
	DEFINE(TASK_THREAD,       offsetof(struct task_struct, thread));
	DEFINE(TASK_MM,           offsetof(struct task_struct, mm));
	DEFINE(TASK_ACTIVE_MM,    offsetof(struct task_struct, active_mm));

	/* offsets into the thread struct */
	DEFINE(THREAD_KSP,        offsetof(struct thread_struct, sp));
	DEFINE(THREAD_FESR,       offsetof(struct thread_struct, user_fp.fesr));
	DEFINE(THREAD_FCR,        offsetof(struct thread_struct, user_fp.fcr));
	DEFINE(THREAD_FPREG,      offsetof(struct thread_struct, user_fp.vr));

	/* offsets into the thread_info struct */
	DEFINE(TINFO_FLAGS,       offsetof(struct thread_info, flags));
	DEFINE(TINFO_PREEMPT,     offsetof(struct thread_info, preempt_count));
	DEFINE(TINFO_TP_VALUE,   offsetof(struct thread_info, tp_value));
	DEFINE(TINFO_TASK,        offsetof(struct thread_info, task));

	/* offsets into the pt_regs */
	DEFINE(PT_PC,             offsetof(struct pt_regs, pc));
	DEFINE(PT_ORIG_AO,        offsetof(struct pt_regs, orig_a0));
	DEFINE(PT_SR,             offsetof(struct pt_regs, sr));

	DEFINE(PT_A0,             offsetof(struct pt_regs, a0));
	DEFINE(PT_A1,             offsetof(struct pt_regs, a1));
	DEFINE(PT_A2,             offsetof(struct pt_regs, a2));
	DEFINE(PT_A3,             offsetof(struct pt_regs, a3));
	DEFINE(PT_REGS0,          offsetof(struct pt_regs, regs[0]));
	DEFINE(PT_REGS1,          offsetof(struct pt_regs, regs[1]));
	DEFINE(PT_REGS2,          offsetof(struct pt_regs, regs[2]));
	DEFINE(PT_REGS3,          offsetof(struct pt_regs, regs[3]));
	DEFINE(PT_REGS4,          offsetof(struct pt_regs, regs[4]));
	DEFINE(PT_REGS5,          offsetof(struct pt_regs, regs[5]));
	DEFINE(PT_REGS6,          offsetof(struct pt_regs, regs[6]));
	DEFINE(PT_REGS7,          offsetof(struct pt_regs, regs[7]));
	DEFINE(PT_REGS8,          offsetof(struct pt_regs, regs[8]));
	DEFINE(PT_REGS9,          offsetof(struct pt_regs, regs[9]));
	DEFINE(PT_R15,            offsetof(struct pt_regs, lr));
#if defined(__CSKYABIV2__)
	DEFINE(PT_R16,            offsetof(struct pt_regs, exregs[0]));
	DEFINE(PT_R17,            offsetof(struct pt_regs, exregs[1]));
	DEFINE(PT_R18,            offsetof(struct pt_regs, exregs[2]));
	DEFINE(PT_R19,            offsetof(struct pt_regs, exregs[3]));
	DEFINE(PT_R20,            offsetof(struct pt_regs, exregs[4]));
	DEFINE(PT_R21,            offsetof(struct pt_regs, exregs[5]));
	DEFINE(PT_R22,            offsetof(struct pt_regs, exregs[6]));
	DEFINE(PT_R23,            offsetof(struct pt_regs, exregs[7]));
	DEFINE(PT_R24,            offsetof(struct pt_regs, exregs[8]));
	DEFINE(PT_R25,            offsetof(struct pt_regs, exregs[9]));
	DEFINE(PT_R26,            offsetof(struct pt_regs, exregs[10]));
	DEFINE(PT_R27,            offsetof(struct pt_regs, exregs[11]));
	DEFINE(PT_R28,            offsetof(struct pt_regs, exregs[12]));
	DEFINE(PT_R29,            offsetof(struct pt_regs, exregs[13]));
	DEFINE(PT_R30,            offsetof(struct pt_regs, exregs[14]));
	DEFINE(PT_R31,            offsetof(struct pt_regs, exregs[15]));
	DEFINE(PT_RHI,            offsetof(struct pt_regs, rhi));
	DEFINE(PT_RLO,            offsetof(struct pt_regs, rlo));
#endif
	DEFINE(PT_USP,            offsetof(struct pt_regs, usp));
	DEFINE(PT_FRAME_SIZE,     sizeof(struct pt_regs));

	/* offsets into the irq_cpustat_t struct */
	DEFINE(CPUSTAT_SOFTIRQ_PENDING, offsetof(irq_cpustat_t,
						__softirq_pending));

	/* signal defines */
	DEFINE(SIGSEGV, SIGSEGV);
	DEFINE(SIGTRAP, SIGTRAP);

	return 0;
}
