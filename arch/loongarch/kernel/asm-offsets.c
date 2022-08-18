// SPDX-License-Identifier: GPL-2.0
/*
 * asm-offsets.c: Calculate pt_regs and task_struct offsets.
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kbuild.h>
#include <linux/suspend.h>
#include <asm/cpu-info.h>
#include <asm/ptrace.h>
#include <asm/processor.h>

void output_ptreg_defines(void)
{
	COMMENT("LoongArch pt_regs offsets.");
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
	OFFSET(PT_CRMD, pt_regs, csr_crmd);
	OFFSET(PT_PRMD, pt_regs, csr_prmd);
	OFFSET(PT_EUEN, pt_regs, csr_euen);
	OFFSET(PT_ECFG, pt_regs, csr_ecfg);
	OFFSET(PT_ESTAT, pt_regs, csr_estat);
	OFFSET(PT_ERA, pt_regs, csr_era);
	OFFSET(PT_BVADDR, pt_regs, csr_badvaddr);
	OFFSET(PT_ORIG_A0, pt_regs, orig_a0);
	DEFINE(PT_SIZE, sizeof(struct pt_regs));
	BLANK();
}

void output_task_defines(void)
{
	COMMENT("LoongArch task_struct offsets.");
	OFFSET(TASK_STATE, task_struct, __state);
	OFFSET(TASK_THREAD_INFO, task_struct, stack);
	OFFSET(TASK_FLAGS, task_struct, flags);
	OFFSET(TASK_MM, task_struct, mm);
	OFFSET(TASK_PID, task_struct, pid);
	DEFINE(TASK_STRUCT_SIZE, sizeof(struct task_struct));
	BLANK();
}

void output_thread_info_defines(void)
{
	COMMENT("LoongArch thread_info offsets.");
	OFFSET(TI_TASK, thread_info, task);
	OFFSET(TI_FLAGS, thread_info, flags);
	OFFSET(TI_TP_VALUE, thread_info, tp_value);
	OFFSET(TI_CPU, thread_info, cpu);
	OFFSET(TI_PRE_COUNT, thread_info, preempt_count);
	OFFSET(TI_REGS, thread_info, regs);
	DEFINE(_THREAD_SIZE, THREAD_SIZE);
	DEFINE(_THREAD_MASK, THREAD_MASK);
	DEFINE(_IRQ_STACK_SIZE, IRQ_STACK_SIZE);
	DEFINE(_IRQ_STACK_START, IRQ_STACK_START);
	BLANK();
}

void output_thread_defines(void)
{
	COMMENT("LoongArch specific thread_struct offsets.");
	OFFSET(THREAD_REG01, task_struct, thread.reg01);
	OFFSET(THREAD_REG03, task_struct, thread.reg03);
	OFFSET(THREAD_REG22, task_struct, thread.reg22);
	OFFSET(THREAD_REG23, task_struct, thread.reg23);
	OFFSET(THREAD_REG24, task_struct, thread.reg24);
	OFFSET(THREAD_REG25, task_struct, thread.reg25);
	OFFSET(THREAD_REG26, task_struct, thread.reg26);
	OFFSET(THREAD_REG27, task_struct, thread.reg27);
	OFFSET(THREAD_REG28, task_struct, thread.reg28);
	OFFSET(THREAD_REG29, task_struct, thread.reg29);
	OFFSET(THREAD_REG30, task_struct, thread.reg30);
	OFFSET(THREAD_REG31, task_struct, thread.reg31);
	OFFSET(THREAD_SCHED_RA, task_struct, thread.sched_ra);
	OFFSET(THREAD_SCHED_CFA, task_struct, thread.sched_cfa);
	OFFSET(THREAD_CSRCRMD, task_struct,
	       thread.csr_crmd);
	OFFSET(THREAD_CSRPRMD, task_struct,
	       thread.csr_prmd);
	OFFSET(THREAD_CSREUEN, task_struct,
	       thread.csr_euen);
	OFFSET(THREAD_CSRECFG, task_struct,
	       thread.csr_ecfg);

	OFFSET(THREAD_SCR0, task_struct, thread.scr0);
	OFFSET(THREAD_SCR1, task_struct, thread.scr1);
	OFFSET(THREAD_SCR2, task_struct, thread.scr2);
	OFFSET(THREAD_SCR3, task_struct, thread.scr3);

	OFFSET(THREAD_EFLAGS, task_struct, thread.eflags);

	OFFSET(THREAD_FPU, task_struct, thread.fpu);

	OFFSET(THREAD_BVADDR, task_struct, \
	       thread.csr_badvaddr);
	OFFSET(THREAD_ECODE, task_struct, \
	       thread.error_code);
	OFFSET(THREAD_TRAPNO, task_struct, thread.trap_nr);
	BLANK();
}

void output_thread_fpu_defines(void)
{
	OFFSET(THREAD_FPR0, loongarch_fpu, fpr[0]);
	OFFSET(THREAD_FPR1, loongarch_fpu, fpr[1]);
	OFFSET(THREAD_FPR2, loongarch_fpu, fpr[2]);
	OFFSET(THREAD_FPR3, loongarch_fpu, fpr[3]);
	OFFSET(THREAD_FPR4, loongarch_fpu, fpr[4]);
	OFFSET(THREAD_FPR5, loongarch_fpu, fpr[5]);
	OFFSET(THREAD_FPR6, loongarch_fpu, fpr[6]);
	OFFSET(THREAD_FPR7, loongarch_fpu, fpr[7]);
	OFFSET(THREAD_FPR8, loongarch_fpu, fpr[8]);
	OFFSET(THREAD_FPR9, loongarch_fpu, fpr[9]);
	OFFSET(THREAD_FPR10, loongarch_fpu, fpr[10]);
	OFFSET(THREAD_FPR11, loongarch_fpu, fpr[11]);
	OFFSET(THREAD_FPR12, loongarch_fpu, fpr[12]);
	OFFSET(THREAD_FPR13, loongarch_fpu, fpr[13]);
	OFFSET(THREAD_FPR14, loongarch_fpu, fpr[14]);
	OFFSET(THREAD_FPR15, loongarch_fpu, fpr[15]);
	OFFSET(THREAD_FPR16, loongarch_fpu, fpr[16]);
	OFFSET(THREAD_FPR17, loongarch_fpu, fpr[17]);
	OFFSET(THREAD_FPR18, loongarch_fpu, fpr[18]);
	OFFSET(THREAD_FPR19, loongarch_fpu, fpr[19]);
	OFFSET(THREAD_FPR20, loongarch_fpu, fpr[20]);
	OFFSET(THREAD_FPR21, loongarch_fpu, fpr[21]);
	OFFSET(THREAD_FPR22, loongarch_fpu, fpr[22]);
	OFFSET(THREAD_FPR23, loongarch_fpu, fpr[23]);
	OFFSET(THREAD_FPR24, loongarch_fpu, fpr[24]);
	OFFSET(THREAD_FPR25, loongarch_fpu, fpr[25]);
	OFFSET(THREAD_FPR26, loongarch_fpu, fpr[26]);
	OFFSET(THREAD_FPR27, loongarch_fpu, fpr[27]);
	OFFSET(THREAD_FPR28, loongarch_fpu, fpr[28]);
	OFFSET(THREAD_FPR29, loongarch_fpu, fpr[29]);
	OFFSET(THREAD_FPR30, loongarch_fpu, fpr[30]);
	OFFSET(THREAD_FPR31, loongarch_fpu, fpr[31]);

	OFFSET(THREAD_FCSR, loongarch_fpu, fcsr);
	OFFSET(THREAD_FCC,  loongarch_fpu, fcc);
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
	DEFINE(_PGD_T_SIZE, sizeof(pgd_t));
	DEFINE(_PMD_T_SIZE, sizeof(pmd_t));
	DEFINE(_PTE_T_SIZE, sizeof(pte_t));
	BLANK();
	DEFINE(_PGD_T_LOG2, PGD_T_LOG2);
#ifndef __PAGETABLE_PMD_FOLDED
	DEFINE(_PMD_T_LOG2, PMD_T_LOG2);
#endif
	DEFINE(_PTE_T_LOG2, PTE_T_LOG2);
	BLANK();
	DEFINE(_PMD_SHIFT, PMD_SHIFT);
	DEFINE(_PGDIR_SHIFT, PGDIR_SHIFT);
	BLANK();
	DEFINE(_PTRS_PER_PGD, PTRS_PER_PGD);
	DEFINE(_PTRS_PER_PMD, PTRS_PER_PMD);
	DEFINE(_PTRS_PER_PTE, PTRS_PER_PTE);
	BLANK();
	DEFINE(_PAGE_SHIFT, PAGE_SHIFT);
	DEFINE(_PAGE_SIZE, PAGE_SIZE);
	BLANK();
}

void output_sc_defines(void)
{
	COMMENT("Linux sigcontext offsets.");
	OFFSET(SC_REGS, sigcontext, sc_regs);
	OFFSET(SC_PC, sigcontext, sc_pc);
	BLANK();
}

void output_signal_defines(void)
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

#ifdef CONFIG_SMP
void output_smpboot_defines(void)
{
	COMMENT("Linux smp cpu boot offsets.");
	OFFSET(CPU_BOOT_STACK, secondary_data, stack);
	OFFSET(CPU_BOOT_TINFO, secondary_data, thread_info);
	BLANK();
}
#endif
