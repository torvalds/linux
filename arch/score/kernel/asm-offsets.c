/*
 * arch/score/kernel/asm-offsets.c
 *
 * Score Processor version.
 *
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Chen Liqin <liqin.chen@sunplusct.com>
 *  Lennox Wu <lennox.wu@sunplusct.com>
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

#include <linux/kbuild.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/sched.h>

#include <asm-generic/cmpxchg-local.h>

void output_ptreg_defines(void)
{
	COMMENT("SCORE pt_regs offsets.");
	OFFSET(PT_R0, pt_regs, regs[0]);
	OFFSET(PT_R1, pt_regs, regs[1]);
	OFFSET(PT_R2, pt_regs, regs[2]);
	OFFSET(PT_R3, pt_regs, regs[3]);
	OFFSET(PT_R4, pt_regs, regs[4]);
	OFFSET(PT_R5, pt_regs, regs[5]);
	OFFSET(PT_R6, pt_regs, regs[6]);
	OFFSET(PT_R7, pt_regs, regs[7]);
	OFFSET(PT_R8, pt_regs, regs[8]);
	OFFSET(PT_R9, pt_regs, regs[9]);
	OFFSET(PT_R10, pt_regs, regs[10]);
	OFFSET(PT_R11, pt_regs, regs[11]);
	OFFSET(PT_R12, pt_regs, regs[12]);
	OFFSET(PT_R13, pt_regs, regs[13]);
	OFFSET(PT_R14, pt_regs, regs[14]);
	OFFSET(PT_R15, pt_regs, regs[15]);
	OFFSET(PT_R16, pt_regs, regs[16]);
	OFFSET(PT_R17, pt_regs, regs[17]);
	OFFSET(PT_R18, pt_regs, regs[18]);
	OFFSET(PT_R19, pt_regs, regs[19]);
	OFFSET(PT_R20, pt_regs, regs[20]);
	OFFSET(PT_R21, pt_regs, regs[21]);
	OFFSET(PT_R22, pt_regs, regs[22]);
	OFFSET(PT_R23, pt_regs, regs[23]);
	OFFSET(PT_R24, pt_regs, regs[24]);
	OFFSET(PT_R25, pt_regs, regs[25]);
	OFFSET(PT_R26, pt_regs, regs[26]);
	OFFSET(PT_R27, pt_regs, regs[27]);
	OFFSET(PT_R28, pt_regs, regs[28]);
	OFFSET(PT_R29, pt_regs, regs[29]);
	OFFSET(PT_R30, pt_regs, regs[30]);
	OFFSET(PT_R31, pt_regs, regs[31]);

	OFFSET(PT_ORIG_R4, pt_regs, orig_r4);
	OFFSET(PT_ORIG_R7, pt_regs, orig_r7);
	OFFSET(PT_CEL, pt_regs, cel);
	OFFSET(PT_CEH, pt_regs, ceh);
	OFFSET(PT_SR0, pt_regs, sr0);
	OFFSET(PT_SR1, pt_regs, sr1);
	OFFSET(PT_SR2, pt_regs, sr2);
	OFFSET(PT_EPC, pt_regs, cp0_epc);
	OFFSET(PT_EMA, pt_regs, cp0_ema);
	OFFSET(PT_PSR, pt_regs, cp0_psr);
	OFFSET(PT_ECR, pt_regs, cp0_ecr);
	OFFSET(PT_CONDITION, pt_regs, cp0_condition);
	OFFSET(PT_IS_SYSCALL, pt_regs, is_syscall);

	DEFINE(PT_SIZE, sizeof(struct pt_regs));
	BLANK();
}

void output_task_defines(void)
{
	COMMENT("SCORE task_struct offsets.");
	OFFSET(TASK_STATE, task_struct, state);
	OFFSET(TASK_THREAD_INFO, task_struct, stack);
	OFFSET(TASK_FLAGS, task_struct, flags);
	OFFSET(TASK_MM, task_struct, mm);
	OFFSET(TASK_PID, task_struct, pid);
	DEFINE(TASK_STRUCT_SIZE, sizeof(struct task_struct));
	BLANK();
}

void output_thread_info_defines(void)
{
	COMMENT("SCORE thread_info offsets.");
	OFFSET(TI_TASK, thread_info, task);
	OFFSET(TI_EXEC_DOMAIN, thread_info, exec_domain);
	OFFSET(TI_FLAGS, thread_info, flags);
	OFFSET(TI_TP_VALUE, thread_info, tp_value);
	OFFSET(TI_CPU, thread_info, cpu);
	OFFSET(TI_PRE_COUNT, thread_info, preempt_count);
	OFFSET(TI_ADDR_LIMIT, thread_info, addr_limit);
	OFFSET(TI_RESTART_BLOCK, thread_info, restart_block);
	OFFSET(TI_REGS, thread_info, regs);
	DEFINE(KERNEL_STACK_SIZE, THREAD_SIZE);
	DEFINE(KERNEL_STACK_MASK, THREAD_MASK);
	BLANK();
}

void output_thread_defines(void)
{
	COMMENT("SCORE specific thread_struct offsets.");
	OFFSET(THREAD_REG0, task_struct, thread.reg0);
	OFFSET(THREAD_REG2, task_struct, thread.reg2);
	OFFSET(THREAD_REG3, task_struct, thread.reg3);
	OFFSET(THREAD_REG12, task_struct, thread.reg12);
	OFFSET(THREAD_REG13, task_struct, thread.reg13);
	OFFSET(THREAD_REG14, task_struct, thread.reg14);
	OFFSET(THREAD_REG15, task_struct, thread.reg15);
	OFFSET(THREAD_REG16, task_struct, thread.reg16);
	OFFSET(THREAD_REG17, task_struct, thread.reg17);
	OFFSET(THREAD_REG18, task_struct, thread.reg18);
	OFFSET(THREAD_REG19, task_struct, thread.reg19);
	OFFSET(THREAD_REG20, task_struct, thread.reg20);
	OFFSET(THREAD_REG21, task_struct, thread.reg21);
	OFFSET(THREAD_REG29, task_struct, thread.reg29);

	OFFSET(THREAD_PSR, task_struct, thread.cp0_psr);
	OFFSET(THREAD_EMA, task_struct, thread.cp0_ema);
	OFFSET(THREAD_BADUADDR, task_struct, thread.cp0_baduaddr);
	OFFSET(THREAD_ECODE, task_struct, thread.error_code);
	OFFSET(THREAD_TRAPNO, task_struct, thread.trap_no);
	BLANK();
}

void output_mm_defines(void)
{
	COMMENT("Size of struct page");
	DEFINE(STRUCT_PAGE_SIZE, sizeof(struct page));
	BLANK();
	COMMENT("Linux mm_struct offsets.");
	OFFSET(MM_USERS, mm_struct, mm_users);
	OFFSET(MM_PGD, mm_struct, pgd);
	OFFSET(MM_CONTEXT, mm_struct, context);
	BLANK();
	DEFINE(_PAGE_SIZE, PAGE_SIZE);
	DEFINE(_PAGE_SHIFT, PAGE_SHIFT);
	BLANK();
	DEFINE(_PGD_T_SIZE, sizeof(pgd_t));
	DEFINE(_PTE_T_SIZE, sizeof(pte_t));
	BLANK();
	DEFINE(_PGD_ORDER, PGD_ORDER);
	DEFINE(_PTE_ORDER, PTE_ORDER);
	BLANK();
	DEFINE(_PGDIR_SHIFT, PGDIR_SHIFT);
	BLANK();
	DEFINE(_PTRS_PER_PGD, PTRS_PER_PGD);
	DEFINE(_PTRS_PER_PTE, PTRS_PER_PTE);
	BLANK();
}

void output_sc_defines(void)
{
	COMMENT("Linux sigcontext offsets.");
	OFFSET(SC_REGS, sigcontext, sc_regs);
	OFFSET(SC_MDCEH, sigcontext, sc_mdceh);
	OFFSET(SC_MDCEL, sigcontext, sc_mdcel);
	OFFSET(SC_PC, sigcontext, sc_pc);
	OFFSET(SC_PSR, sigcontext, sc_psr);
	OFFSET(SC_ECR, sigcontext, sc_ecr);
	OFFSET(SC_EMA, sigcontext, sc_ema);
	BLANK();
}

void output_signal_defined(void)
{
	COMMENT("Linux signal numbers.");
	DEFINE(_SIGHUP, SIGHUP);
	DEFINE(_SIGINT, SIGINT);
	DEFINE(_SIGQUIT, SIGQUIT);
	DEFINE(_SIGILL, SIGILL);
	DEFINE(_SIGTRAP, SIGTRAP);
	DEFINE(_SIGIOT, SIGIOT);
	DEFINE(_SIGABRT, SIGABRT);
	DEFINE(_SIGFPE, SIGFPE);
	DEFINE(_SIGKILL, SIGKILL);
	DEFINE(_SIGBUS, SIGBUS);
	DEFINE(_SIGSEGV, SIGSEGV);
	DEFINE(_SIGSYS, SIGSYS);
	DEFINE(_SIGPIPE, SIGPIPE);
	DEFINE(_SIGALRM, SIGALRM);
	DEFINE(_SIGTERM, SIGTERM);
	DEFINE(_SIGUSR1, SIGUSR1);
	DEFINE(_SIGUSR2, SIGUSR2);
	DEFINE(_SIGCHLD, SIGCHLD);
	DEFINE(_SIGPWR, SIGPWR);
	DEFINE(_SIGWINCH, SIGWINCH);
	DEFINE(_SIGURG, SIGURG);
	DEFINE(_SIGIO, SIGIO);
	DEFINE(_SIGSTOP, SIGSTOP);
	DEFINE(_SIGTSTP, SIGTSTP);
	DEFINE(_SIGCONT, SIGCONT);
	DEFINE(_SIGTTIN, SIGTTIN);
	DEFINE(_SIGTTOU, SIGTTOU);
	DEFINE(_SIGVTALRM, SIGVTALRM);
	DEFINE(_SIGPROF, SIGPROF);
	DEFINE(_SIGXCPU, SIGXCPU);
	DEFINE(_SIGXFSZ, SIGXFSZ);
	BLANK();
}
