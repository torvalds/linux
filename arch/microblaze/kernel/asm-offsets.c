/*
 * Copyright (C) 2007-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#define COMPILE_OFFSETS

#include <linux/init.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/ptrace.h>
#include <linux/hardirq.h>
#include <linux/thread_info.h>
#include <linux/kbuild.h>
#include <asm/cpuinfo.h>

int main(int argc, char *argv[])
{
	/* struct pt_regs */
	DEFINE(PT_SIZE, sizeof(struct pt_regs));
	DEFINE(PT_MSR, offsetof(struct pt_regs, msr));
	DEFINE(PT_EAR, offsetof(struct pt_regs, ear));
	DEFINE(PT_ESR, offsetof(struct pt_regs, esr));
	DEFINE(PT_FSR, offsetof(struct pt_regs, fsr));
	DEFINE(PT_PC, offsetof(struct pt_regs, pc));
	DEFINE(PT_R0, offsetof(struct pt_regs, r0));
	DEFINE(PT_R1, offsetof(struct pt_regs, r1));
	DEFINE(PT_R2, offsetof(struct pt_regs, r2));
	DEFINE(PT_R3, offsetof(struct pt_regs, r3));
	DEFINE(PT_R4, offsetof(struct pt_regs, r4));
	DEFINE(PT_R5, offsetof(struct pt_regs, r5));
	DEFINE(PT_R6, offsetof(struct pt_regs, r6));
	DEFINE(PT_R7, offsetof(struct pt_regs, r7));
	DEFINE(PT_R8, offsetof(struct pt_regs, r8));
	DEFINE(PT_R9, offsetof(struct pt_regs, r9));
	DEFINE(PT_R10, offsetof(struct pt_regs, r10));
	DEFINE(PT_R11, offsetof(struct pt_regs, r11));
	DEFINE(PT_R12, offsetof(struct pt_regs, r12));
	DEFINE(PT_R13, offsetof(struct pt_regs, r13));
	DEFINE(PT_R14, offsetof(struct pt_regs, r14));
	DEFINE(PT_R15, offsetof(struct pt_regs, r15));
	DEFINE(PT_R16, offsetof(struct pt_regs, r16));
	DEFINE(PT_R17, offsetof(struct pt_regs, r17));
	DEFINE(PT_R18, offsetof(struct pt_regs, r18));
	DEFINE(PT_R19, offsetof(struct pt_regs, r19));
	DEFINE(PT_R20, offsetof(struct pt_regs, r20));
	DEFINE(PT_R21, offsetof(struct pt_regs, r21));
	DEFINE(PT_R22, offsetof(struct pt_regs, r22));
	DEFINE(PT_R23, offsetof(struct pt_regs, r23));
	DEFINE(PT_R24, offsetof(struct pt_regs, r24));
	DEFINE(PT_R25, offsetof(struct pt_regs, r25));
	DEFINE(PT_R26, offsetof(struct pt_regs, r26));
	DEFINE(PT_R27, offsetof(struct pt_regs, r27));
	DEFINE(PT_R28, offsetof(struct pt_regs, r28));
	DEFINE(PT_R29, offsetof(struct pt_regs, r29));
	DEFINE(PT_R30, offsetof(struct pt_regs, r30));
	DEFINE(PT_R31, offsetof(struct pt_regs, r31));
	DEFINE(PT_MODE, offsetof(struct pt_regs, pt_mode));
	BLANK();

	/* Magic offsets for PTRACE PEEK/POKE etc */
	DEFINE(PT_TEXT_ADDR, sizeof(struct pt_regs) + 1);
	DEFINE(PT_TEXT_LEN, sizeof(struct pt_regs) + 2);
	DEFINE(PT_DATA_ADDR, sizeof(struct pt_regs) + 3);
	BLANK();

	/* struct task_struct */
	DEFINE(TS_THREAD_INFO, offsetof(struct task_struct, stack));
	DEFINE(TASK_FLAGS, offsetof(struct task_struct, flags));
	DEFINE(TASK_PTRACE, offsetof(struct task_struct, ptrace));
	DEFINE(TASK_BLOCKED, offsetof(struct task_struct, blocked));
	DEFINE(TASK_MM, offsetof(struct task_struct, mm));
	DEFINE(TASK_ACTIVE_MM, offsetof(struct task_struct, active_mm));
	DEFINE(TASK_PID, offsetof(struct task_struct, pid));
	DEFINE(TASK_THREAD, offsetof(struct task_struct, thread));
	DEFINE(THREAD_KSP, offsetof(struct thread_struct, ksp));
	BLANK();

	DEFINE(PGDIR, offsetof(struct thread_struct, pgdir));
	BLANK();

	/* struct thread_info */
	DEFINE(TI_TASK, offsetof(struct thread_info, task));
	DEFINE(TI_FLAGS, offsetof(struct thread_info, flags));
	DEFINE(TI_CPU_CONTEXT, offsetof(struct thread_info, cpu_context));
	DEFINE(TI_PREEMPT_COUNT, offsetof(struct thread_info, preempt_count));
	BLANK();

	/* struct cpu_context */
	DEFINE(CC_R1, offsetof(struct cpu_context, r1)); /* r1 */
	DEFINE(CC_R2, offsetof(struct cpu_context, r2));
	/* dedicated registers */
	DEFINE(CC_R13, offsetof(struct cpu_context, r13));
	DEFINE(CC_R14, offsetof(struct cpu_context, r14));
	DEFINE(CC_R15, offsetof(struct cpu_context, r15));
	DEFINE(CC_R16, offsetof(struct cpu_context, r16));
	DEFINE(CC_R17, offsetof(struct cpu_context, r17));
	DEFINE(CC_R18, offsetof(struct cpu_context, r18));
	/* non-volatile registers */
	DEFINE(CC_R19, offsetof(struct cpu_context, r19));
	DEFINE(CC_R20, offsetof(struct cpu_context, r20));
	DEFINE(CC_R21, offsetof(struct cpu_context, r21));
	DEFINE(CC_R22, offsetof(struct cpu_context, r22));
	DEFINE(CC_R23, offsetof(struct cpu_context, r23));
	DEFINE(CC_R24, offsetof(struct cpu_context, r24));
	DEFINE(CC_R25, offsetof(struct cpu_context, r25));
	DEFINE(CC_R26, offsetof(struct cpu_context, r26));
	DEFINE(CC_R27, offsetof(struct cpu_context, r27));
	DEFINE(CC_R28, offsetof(struct cpu_context, r28));
	DEFINE(CC_R29, offsetof(struct cpu_context, r29));
	DEFINE(CC_R30, offsetof(struct cpu_context, r30));
	/* special purpose registers */
	DEFINE(CC_MSR, offsetof(struct cpu_context, msr));
	DEFINE(CC_EAR, offsetof(struct cpu_context, ear));
	DEFINE(CC_ESR, offsetof(struct cpu_context, esr));
	DEFINE(CC_FSR, offsetof(struct cpu_context, fsr));
	BLANK();

	/* struct cpuinfo */
	DEFINE(CI_DCS, offsetof(struct cpuinfo, dcache_size));
	DEFINE(CI_DCL, offsetof(struct cpuinfo, dcache_line_length));
	DEFINE(CI_ICS, offsetof(struct cpuinfo, icache_size));
	DEFINE(CI_ICL, offsetof(struct cpuinfo, icache_line_length));
	BLANK();

	return 0;
}
