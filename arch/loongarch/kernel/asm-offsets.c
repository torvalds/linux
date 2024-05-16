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
#include <linux/kvm_host.h>
#include <asm/cpu-info.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/ftrace.h>

static void __used output_ptreg_defines(void)
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

static void __used output_task_defines(void)
{
	COMMENT("LoongArch task_struct offsets.");
	OFFSET(TASK_STATE, task_struct, __state);
	OFFSET(TASK_THREAD_INFO, task_struct, stack);
	OFFSET(TASK_FLAGS, task_struct, flags);
	OFFSET(TASK_MM, task_struct, mm);
	OFFSET(TASK_PID, task_struct, pid);
#if defined(CONFIG_STACKPROTECTOR)
	OFFSET(TASK_STACK_CANARY, task_struct, stack_canary);
#endif
	DEFINE(TASK_STRUCT_SIZE, sizeof(struct task_struct));
	BLANK();
}

static void __used output_thread_info_defines(void)
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

static void __used output_thread_defines(void)
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

	OFFSET(THREAD_FPU, task_struct, thread.fpu);

	OFFSET(THREAD_BVADDR, task_struct, \
	       thread.csr_badvaddr);
	OFFSET(THREAD_ECODE, task_struct, \
	       thread.error_code);
	OFFSET(THREAD_TRAPNO, task_struct, thread.trap_nr);
	BLANK();
}

static void __used output_thread_fpu_defines(void)
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
	OFFSET(THREAD_FTOP, loongarch_fpu, ftop);
	BLANK();
}

static void __used output_thread_lbt_defines(void)
{
	OFFSET(THREAD_SCR0,  loongarch_lbt, scr0);
	OFFSET(THREAD_SCR1,  loongarch_lbt, scr1);
	OFFSET(THREAD_SCR2,  loongarch_lbt, scr2);
	OFFSET(THREAD_SCR3,  loongarch_lbt, scr3);
	OFFSET(THREAD_EFLAGS, loongarch_lbt, eflags);
	BLANK();
}

static void __used output_mm_defines(void)
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

static void __used output_sc_defines(void)
{
	COMMENT("Linux sigcontext offsets.");
	OFFSET(SC_REGS, sigcontext, sc_regs);
	OFFSET(SC_PC, sigcontext, sc_pc);
	BLANK();
}

static void __used output_signal_defines(void)
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
static void __used output_smpboot_defines(void)
{
	COMMENT("Linux smp cpu boot offsets.");
	OFFSET(CPU_BOOT_STACK, secondary_data, stack);
	OFFSET(CPU_BOOT_TINFO, secondary_data, thread_info);
	BLANK();
}
#endif

#ifdef CONFIG_HIBERNATION
static void __used output_pbe_defines(void)
{
	COMMENT("Linux struct pbe offsets.");
	OFFSET(PBE_ADDRESS, pbe, address);
	OFFSET(PBE_ORIG_ADDRESS, pbe, orig_address);
	OFFSET(PBE_NEXT, pbe, next);
	DEFINE(PBE_SIZE, sizeof(struct pbe));
	BLANK();
}
#endif

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
static void __used output_fgraph_ret_regs_defines(void)
{
	COMMENT("LoongArch fgraph_ret_regs offsets.");
	OFFSET(FGRET_REGS_A0, fgraph_ret_regs, regs[0]);
	OFFSET(FGRET_REGS_A1, fgraph_ret_regs, regs[1]);
	OFFSET(FGRET_REGS_FP, fgraph_ret_regs, fp);
	DEFINE(FGRET_REGS_SIZE, sizeof(struct fgraph_ret_regs));
	BLANK();
}
#endif

static void __used output_kvm_defines(void)
{
	COMMENT("KVM/LoongArch Specific offsets.");

	OFFSET(VCPU_FCC, kvm_vcpu_arch, fpu.fcc);
	OFFSET(VCPU_FCSR0, kvm_vcpu_arch, fpu.fcsr);
	BLANK();

	OFFSET(KVM_VCPU_ARCH, kvm_vcpu, arch);
	OFFSET(KVM_VCPU_KVM, kvm_vcpu, kvm);
	OFFSET(KVM_VCPU_RUN, kvm_vcpu, run);
	BLANK();

	OFFSET(KVM_ARCH_HSP, kvm_vcpu_arch, host_sp);
	OFFSET(KVM_ARCH_HTP, kvm_vcpu_arch, host_tp);
	OFFSET(KVM_ARCH_HPGD, kvm_vcpu_arch, host_pgd);
	OFFSET(KVM_ARCH_HANDLE_EXIT, kvm_vcpu_arch, handle_exit);
	OFFSET(KVM_ARCH_HEENTRY, kvm_vcpu_arch, host_eentry);
	OFFSET(KVM_ARCH_GEENTRY, kvm_vcpu_arch, guest_eentry);
	OFFSET(KVM_ARCH_GPC, kvm_vcpu_arch, pc);
	OFFSET(KVM_ARCH_GGPR, kvm_vcpu_arch, gprs);
	OFFSET(KVM_ARCH_HBADI, kvm_vcpu_arch, badi);
	OFFSET(KVM_ARCH_HBADV, kvm_vcpu_arch, badv);
	OFFSET(KVM_ARCH_HECFG, kvm_vcpu_arch, host_ecfg);
	OFFSET(KVM_ARCH_HESTAT, kvm_vcpu_arch, host_estat);
	OFFSET(KVM_ARCH_HPERCPU, kvm_vcpu_arch, host_percpu);

	OFFSET(KVM_GPGD, kvm, arch.pgd);
	BLANK();
}
