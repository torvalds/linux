/*
 * File:         arch/blackfin/kernel/asm-offsets.c
 * Based on:
 * Author:
 *
 * Created:
 * Description:  generate definitions needed by assembly language modules.
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/ptrace.h>
#include <linux/hardirq.h>
#include <asm/irq.h>
#include <asm/thread_info.h>

#define DEFINE(sym, val) \
        asm volatile("\n->" #sym " %0 " #val : : "i" (val))

int main(void)
{
	/* offsets into the task struct */
	DEFINE(TASK_STATE, offsetof(struct task_struct, state));
	DEFINE(TASK_FLAGS, offsetof(struct task_struct, flags));
	DEFINE(TASK_PTRACE, offsetof(struct task_struct, ptrace));
	DEFINE(TASK_BLOCKED, offsetof(struct task_struct, blocked));
	DEFINE(TASK_THREAD, offsetof(struct task_struct, thread));
	DEFINE(TASK_THREAD_INFO, offsetof(struct task_struct, stack));
	DEFINE(TASK_MM, offsetof(struct task_struct, mm));
	DEFINE(TASK_ACTIVE_MM, offsetof(struct task_struct, active_mm));
	DEFINE(TASK_SIGPENDING, offsetof(struct task_struct, pending));

	/* offsets into the irq_cpustat_t struct */
	DEFINE(CPUSTAT_SOFTIRQ_PENDING,
	       offsetof(irq_cpustat_t, __softirq_pending));

	/* offsets into the thread struct */
	DEFINE(THREAD_KSP, offsetof(struct thread_struct, ksp));
	DEFINE(THREAD_USP, offsetof(struct thread_struct, usp));
	DEFINE(THREAD_SR, offsetof(struct thread_struct, seqstat));
	DEFINE(PT_SR, offsetof(struct thread_struct, seqstat));
	DEFINE(THREAD_ESP0, offsetof(struct thread_struct, esp0));
	DEFINE(THREAD_PC, offsetof(struct thread_struct, pc));
	DEFINE(KERNEL_STACK_SIZE, THREAD_SIZE);

	/* offsets into the pt_regs */
	DEFINE(PT_ORIG_P0, offsetof(struct pt_regs, orig_p0));
	DEFINE(PT_ORIG_PC, offsetof(struct pt_regs, orig_pc));
	DEFINE(PT_R0, offsetof(struct pt_regs, r0));
	DEFINE(PT_R1, offsetof(struct pt_regs, r1));
	DEFINE(PT_R2, offsetof(struct pt_regs, r2));
	DEFINE(PT_R3, offsetof(struct pt_regs, r3));
	DEFINE(PT_R4, offsetof(struct pt_regs, r4));
	DEFINE(PT_R5, offsetof(struct pt_regs, r5));
	DEFINE(PT_R6, offsetof(struct pt_regs, r6));
	DEFINE(PT_R7, offsetof(struct pt_regs, r7));

	DEFINE(PT_P0, offsetof(struct pt_regs, p0));
	DEFINE(PT_P1, offsetof(struct pt_regs, p1));
	DEFINE(PT_P2, offsetof(struct pt_regs, p2));
	DEFINE(PT_P3, offsetof(struct pt_regs, p3));
	DEFINE(PT_P4, offsetof(struct pt_regs, p4));
	DEFINE(PT_P5, offsetof(struct pt_regs, p5));

	DEFINE(PT_FP, offsetof(struct pt_regs, fp));
	DEFINE(PT_USP, offsetof(struct pt_regs, usp));
	DEFINE(PT_I0, offsetof(struct pt_regs, i0));
	DEFINE(PT_I1, offsetof(struct pt_regs, i1));
	DEFINE(PT_I2, offsetof(struct pt_regs, i2));
	DEFINE(PT_I3, offsetof(struct pt_regs, i3));
	DEFINE(PT_M0, offsetof(struct pt_regs, m0));
	DEFINE(PT_M1, offsetof(struct pt_regs, m1));
	DEFINE(PT_M2, offsetof(struct pt_regs, m2));
	DEFINE(PT_M3, offsetof(struct pt_regs, m3));
	DEFINE(PT_L0, offsetof(struct pt_regs, l0));
	DEFINE(PT_L1, offsetof(struct pt_regs, l1));
	DEFINE(PT_L2, offsetof(struct pt_regs, l2));
	DEFINE(PT_L3, offsetof(struct pt_regs, l3));
	DEFINE(PT_B0, offsetof(struct pt_regs, b0));
	DEFINE(PT_B1, offsetof(struct pt_regs, b1));
	DEFINE(PT_B2, offsetof(struct pt_regs, b2));
	DEFINE(PT_B3, offsetof(struct pt_regs, b3));
	DEFINE(PT_A0X, offsetof(struct pt_regs, a0x));
	DEFINE(PT_A0W, offsetof(struct pt_regs, a0w));
	DEFINE(PT_A1X, offsetof(struct pt_regs, a1x));
	DEFINE(PT_A1W, offsetof(struct pt_regs, a1w));
	DEFINE(PT_LC0, offsetof(struct pt_regs, lc0));
	DEFINE(PT_LC1, offsetof(struct pt_regs, lc1));
	DEFINE(PT_LT0, offsetof(struct pt_regs, lt0));
	DEFINE(PT_LT1, offsetof(struct pt_regs, lt1));
	DEFINE(PT_LB0, offsetof(struct pt_regs, lb0));
	DEFINE(PT_LB1, offsetof(struct pt_regs, lb1));
	DEFINE(PT_ASTAT, offsetof(struct pt_regs, astat));
	DEFINE(PT_RESERVED, offsetof(struct pt_regs, reserved));
	DEFINE(PT_RETS, offsetof(struct pt_regs, rets));
	DEFINE(PT_PC, offsetof(struct pt_regs, pc));
	DEFINE(PT_RETX, offsetof(struct pt_regs, retx));
	DEFINE(PT_RETN, offsetof(struct pt_regs, retn));
	DEFINE(PT_RETE, offsetof(struct pt_regs, rete));
	DEFINE(PT_SEQSTAT, offsetof(struct pt_regs, seqstat));
	DEFINE(PT_SYSCFG, offsetof(struct pt_regs, syscfg));
	DEFINE(PT_IPEND, offsetof(struct pt_regs, ipend));
	DEFINE(SIZEOF_PTREGS, sizeof(struct pt_regs));
	DEFINE(PT_TEXT_ADDR, sizeof(struct pt_regs));        /* Needed by gdb */
	DEFINE(PT_TEXT_END_ADDR, 4 + sizeof(struct pt_regs));/* Needed by gdb */
	DEFINE(PT_DATA_ADDR, 8 + sizeof(struct pt_regs));    /* Needed by gdb */
	DEFINE(PT_FDPIC_EXEC, 12 + sizeof(struct pt_regs));  /* Needed by gdb */
	DEFINE(PT_FDPIC_INTERP, 16 + sizeof(struct pt_regs));/* Needed by gdb */

	/* signal defines */
	DEFINE(SIGSEGV, SIGSEGV);
	DEFINE(SIGTRAP, SIGTRAP);

	return 0;
}
