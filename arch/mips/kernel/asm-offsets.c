/*
 * offset.c: Calculate pt_regs and task_struct offsets.
 *
 * Copyright (C) 1996 David S. Miller
 * Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 *
 * Kevin Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.
 */
#include <linux/config.h>
#include <linux/compat.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

#include <asm/ptrace.h>
#include <asm/processor.h>

#define text(t) __asm__("\n@@@" t)
#define _offset(type, member) (&(((type *)NULL)->member))
#define offset(string, ptr, member) \
	__asm__("\n@@@" string "%0" : : "i" (_offset(ptr, member)))
#define constant(string, member) \
	__asm__("\n@@@" string "%x0" : : "ri" (member))
#define size(string, size) \
	__asm__("\n@@@" string "%0" : : "i" (sizeof(size)))
#define linefeed text("")

void output_ptreg_defines(void)
{
	text("/* MIPS pt_regs offsets. */");
	offset("#define PT_R0     ", struct pt_regs, regs[0]);
	offset("#define PT_R1     ", struct pt_regs, regs[1]);
	offset("#define PT_R2     ", struct pt_regs, regs[2]);
	offset("#define PT_R3     ", struct pt_regs, regs[3]);
	offset("#define PT_R4     ", struct pt_regs, regs[4]);
	offset("#define PT_R5     ", struct pt_regs, regs[5]);
	offset("#define PT_R6     ", struct pt_regs, regs[6]);
	offset("#define PT_R7     ", struct pt_regs, regs[7]);
	offset("#define PT_R8     ", struct pt_regs, regs[8]);
	offset("#define PT_R9     ", struct pt_regs, regs[9]);
	offset("#define PT_R10    ", struct pt_regs, regs[10]);
	offset("#define PT_R11    ", struct pt_regs, regs[11]);
	offset("#define PT_R12    ", struct pt_regs, regs[12]);
	offset("#define PT_R13    ", struct pt_regs, regs[13]);
	offset("#define PT_R14    ", struct pt_regs, regs[14]);
	offset("#define PT_R15    ", struct pt_regs, regs[15]);
	offset("#define PT_R16    ", struct pt_regs, regs[16]);
	offset("#define PT_R17    ", struct pt_regs, regs[17]);
	offset("#define PT_R18    ", struct pt_regs, regs[18]);
	offset("#define PT_R19    ", struct pt_regs, regs[19]);
	offset("#define PT_R20    ", struct pt_regs, regs[20]);
	offset("#define PT_R21    ", struct pt_regs, regs[21]);
	offset("#define PT_R22    ", struct pt_regs, regs[22]);
	offset("#define PT_R23    ", struct pt_regs, regs[23]);
	offset("#define PT_R24    ", struct pt_regs, regs[24]);
	offset("#define PT_R25    ", struct pt_regs, regs[25]);
	offset("#define PT_R26    ", struct pt_regs, regs[26]);
	offset("#define PT_R27    ", struct pt_regs, regs[27]);
	offset("#define PT_R28    ", struct pt_regs, regs[28]);
	offset("#define PT_R29    ", struct pt_regs, regs[29]);
	offset("#define PT_R30    ", struct pt_regs, regs[30]);
	offset("#define PT_R31    ", struct pt_regs, regs[31]);
	offset("#define PT_LO     ", struct pt_regs, lo);
	offset("#define PT_HI     ", struct pt_regs, hi);
	offset("#define PT_EPC    ", struct pt_regs, cp0_epc);
	offset("#define PT_BVADDR ", struct pt_regs, cp0_badvaddr);
	offset("#define PT_STATUS ", struct pt_regs, cp0_status);
	offset("#define PT_CAUSE  ", struct pt_regs, cp0_cause);
#ifdef CONFIG_MIPS_MT_SMTC
	offset("#define PT_TCSTATUS  ", struct pt_regs, cp0_tcstatus);
#endif /* CONFIG_MIPS_MT_SMTC */
	size("#define PT_SIZE   ", struct pt_regs);
	linefeed;
}

void output_task_defines(void)
{
	text("/* MIPS task_struct offsets. */");
	offset("#define TASK_STATE         ", struct task_struct, state);
	offset("#define TASK_THREAD_INFO   ", struct task_struct, thread_info);
	offset("#define TASK_FLAGS         ", struct task_struct, flags);
	offset("#define TASK_MM            ", struct task_struct, mm);
	offset("#define TASK_PID           ", struct task_struct, pid);
	size(  "#define TASK_STRUCT_SIZE   ", struct task_struct);
	linefeed;
}

void output_thread_info_defines(void)
{
	text("/* MIPS thread_info offsets. */");
	offset("#define TI_TASK            ", struct thread_info, task);
	offset("#define TI_EXEC_DOMAIN     ", struct thread_info, exec_domain);
	offset("#define TI_FLAGS           ", struct thread_info, flags);
	offset("#define TI_CPU             ", struct thread_info, cpu);
	offset("#define TI_PRE_COUNT       ", struct thread_info, preempt_count);
	offset("#define TI_ADDR_LIMIT      ", struct thread_info, addr_limit);
	offset("#define TI_RESTART_BLOCK   ", struct thread_info, restart_block);
	offset("#define TI_TP_VALUE	   ", struct thread_info, tp_value);
	constant("#define _THREAD_SIZE_ORDER ", THREAD_SIZE_ORDER);
	constant("#define _THREAD_SIZE       ", THREAD_SIZE);
	constant("#define _THREAD_MASK       ", THREAD_MASK);
	linefeed;
}

void output_thread_defines(void)
{
	text("/* MIPS specific thread_struct offsets. */");
	offset("#define THREAD_REG16   ", struct task_struct, thread.reg16);
	offset("#define THREAD_REG17   ", struct task_struct, thread.reg17);
	offset("#define THREAD_REG18   ", struct task_struct, thread.reg18);
	offset("#define THREAD_REG19   ", struct task_struct, thread.reg19);
	offset("#define THREAD_REG20   ", struct task_struct, thread.reg20);
	offset("#define THREAD_REG21   ", struct task_struct, thread.reg21);
	offset("#define THREAD_REG22   ", struct task_struct, thread.reg22);
	offset("#define THREAD_REG23   ", struct task_struct, thread.reg23);
	offset("#define THREAD_REG29   ", struct task_struct, thread.reg29);
	offset("#define THREAD_REG30   ", struct task_struct, thread.reg30);
	offset("#define THREAD_REG31   ", struct task_struct, thread.reg31);
	offset("#define THREAD_STATUS  ", struct task_struct,
	       thread.cp0_status);
	offset("#define THREAD_FPU     ", struct task_struct, thread.fpu);

	offset("#define THREAD_BVADDR  ", struct task_struct, \
	       thread.cp0_badvaddr);
	offset("#define THREAD_BUADDR  ", struct task_struct, \
	       thread.cp0_baduaddr);
	offset("#define THREAD_ECODE   ", struct task_struct, \
	       thread.error_code);
	offset("#define THREAD_TRAPNO  ", struct task_struct, thread.trap_no);
	offset("#define THREAD_MFLAGS  ", struct task_struct, thread.mflags);
	offset("#define THREAD_TRAMP   ", struct task_struct, \
	       thread.irix_trampoline);
	offset("#define THREAD_OLDCTX  ", struct task_struct, \
	       thread.irix_oldctx);
	linefeed;
}

void output_thread_fpu_defines(void)
{
	offset("#define THREAD_FPR0    ",
	       struct task_struct, thread.fpu.fpr[0]);
	offset("#define THREAD_FPR1    ",
	       struct task_struct, thread.fpu.fpr[1]);
	offset("#define THREAD_FPR2    ",
	       struct task_struct, thread.fpu.fpr[2]);
	offset("#define THREAD_FPR3    ",
	       struct task_struct, thread.fpu.fpr[3]);
	offset("#define THREAD_FPR4    ",
	       struct task_struct, thread.fpu.fpr[4]);
	offset("#define THREAD_FPR5    ",
	       struct task_struct, thread.fpu.fpr[5]);
	offset("#define THREAD_FPR6    ",
	       struct task_struct, thread.fpu.fpr[6]);
	offset("#define THREAD_FPR7    ",
	       struct task_struct, thread.fpu.fpr[7]);
	offset("#define THREAD_FPR8    ",
	       struct task_struct, thread.fpu.fpr[8]);
	offset("#define THREAD_FPR9    ",
	       struct task_struct, thread.fpu.fpr[9]);
	offset("#define THREAD_FPR10   ",
	       struct task_struct, thread.fpu.fpr[10]);
	offset("#define THREAD_FPR11   ",
	       struct task_struct, thread.fpu.fpr[11]);
	offset("#define THREAD_FPR12   ",
	       struct task_struct, thread.fpu.fpr[12]);
	offset("#define THREAD_FPR13   ",
	       struct task_struct, thread.fpu.fpr[13]);
	offset("#define THREAD_FPR14   ",
	       struct task_struct, thread.fpu.fpr[14]);
	offset("#define THREAD_FPR15   ",
	       struct task_struct, thread.fpu.fpr[15]);
	offset("#define THREAD_FPR16   ",
	       struct task_struct, thread.fpu.fpr[16]);
	offset("#define THREAD_FPR17   ",
	       struct task_struct, thread.fpu.fpr[17]);
	offset("#define THREAD_FPR18   ",
	       struct task_struct, thread.fpu.fpr[18]);
	offset("#define THREAD_FPR19   ",
	       struct task_struct, thread.fpu.fpr[19]);
	offset("#define THREAD_FPR20   ",
	       struct task_struct, thread.fpu.fpr[20]);
	offset("#define THREAD_FPR21   ",
	       struct task_struct, thread.fpu.fpr[21]);
	offset("#define THREAD_FPR22   ",
	       struct task_struct, thread.fpu.fpr[22]);
	offset("#define THREAD_FPR23   ",
	       struct task_struct, thread.fpu.fpr[23]);
	offset("#define THREAD_FPR24   ",
	       struct task_struct, thread.fpu.fpr[24]);
	offset("#define THREAD_FPR25   ",
	       struct task_struct, thread.fpu.fpr[25]);
	offset("#define THREAD_FPR26   ",
	       struct task_struct, thread.fpu.fpr[26]);
	offset("#define THREAD_FPR27   ",
	       struct task_struct, thread.fpu.fpr[27]);
	offset("#define THREAD_FPR28   ",
	       struct task_struct, thread.fpu.fpr[28]);
	offset("#define THREAD_FPR29   ",
	       struct task_struct, thread.fpu.fpr[29]);
	offset("#define THREAD_FPR30   ",
	       struct task_struct, thread.fpu.fpr[30]);
	offset("#define THREAD_FPR31   ",
	       struct task_struct, thread.fpu.fpr[31]);

	offset("#define THREAD_FCR31   ",
	       struct task_struct, thread.fpu.fcr31);
	linefeed;
}

void output_mm_defines(void)
{
	text("/* Size of struct page  */");
	size("#define STRUCT_PAGE_SIZE   ", struct page);
	linefeed;
	text("/* Linux mm_struct offsets. */");
	offset("#define MM_USERS      ", struct mm_struct, mm_users);
	offset("#define MM_PGD        ", struct mm_struct, pgd);
	offset("#define MM_CONTEXT    ", struct mm_struct, context);
	linefeed;
	constant("#define _PAGE_SIZE     ", PAGE_SIZE);
	constant("#define _PAGE_SHIFT    ", PAGE_SHIFT);
	linefeed;
	constant("#define _PGD_T_SIZE    ", sizeof(pgd_t));
	constant("#define _PMD_T_SIZE    ", sizeof(pmd_t));
	constant("#define _PTE_T_SIZE    ", sizeof(pte_t));
	linefeed;
	constant("#define _PGD_T_LOG2    ", PGD_T_LOG2);
	constant("#define _PMD_T_LOG2    ", PMD_T_LOG2);
	constant("#define _PTE_T_LOG2    ", PTE_T_LOG2);
	linefeed;
	constant("#define _PMD_SHIFT     ", PMD_SHIFT);
	constant("#define _PGDIR_SHIFT   ", PGDIR_SHIFT);
	linefeed;
	constant("#define _PGD_ORDER     ", PGD_ORDER);
	constant("#define _PMD_ORDER     ", PMD_ORDER);
	constant("#define _PTE_ORDER     ", PTE_ORDER);
	linefeed;
	constant("#define _PTRS_PER_PGD  ", PTRS_PER_PGD);
	constant("#define _PTRS_PER_PMD  ", PTRS_PER_PMD);
	constant("#define _PTRS_PER_PTE  ", PTRS_PER_PTE);
	linefeed;
}

#ifdef CONFIG_32BIT
void output_sc_defines(void)
{
	text("/* Linux sigcontext offsets. */");
	offset("#define SC_REGS       ", struct sigcontext, sc_regs);
	offset("#define SC_FPREGS     ", struct sigcontext, sc_fpregs);
	offset("#define SC_MDHI       ", struct sigcontext, sc_mdhi);
	offset("#define SC_MDLO       ", struct sigcontext, sc_mdlo);
	offset("#define SC_PC         ", struct sigcontext, sc_pc);
	offset("#define SC_STATUS     ", struct sigcontext, sc_status);
	offset("#define SC_FPC_CSR    ", struct sigcontext, sc_fpc_csr);
	offset("#define SC_FPC_EIR    ", struct sigcontext, sc_fpc_eir);
	offset("#define SC_HI1        ", struct sigcontext, sc_hi1);
	offset("#define SC_LO1        ", struct sigcontext, sc_lo1);
	offset("#define SC_HI2        ", struct sigcontext, sc_hi2);
	offset("#define SC_LO2        ", struct sigcontext, sc_lo2);
	offset("#define SC_HI3        ", struct sigcontext, sc_hi3);
	offset("#define SC_LO3        ", struct sigcontext, sc_lo3);
	linefeed;
}
#endif

#ifdef CONFIG_64BIT
void output_sc_defines(void)
{
	text("/* Linux sigcontext offsets. */");
	offset("#define SC_REGS       ", struct sigcontext, sc_regs);
	offset("#define SC_FPREGS     ", struct sigcontext, sc_fpregs);
	offset("#define SC_MDHI       ", struct sigcontext, sc_mdhi);
	offset("#define SC_MDLO       ", struct sigcontext, sc_mdlo);
	offset("#define SC_PC         ", struct sigcontext, sc_pc);
	offset("#define SC_FPC_CSR    ", struct sigcontext, sc_fpc_csr);
	linefeed;
}
#endif

#ifdef CONFIG_MIPS32_COMPAT
void output_sc32_defines(void)
{
	text("/* Linux 32-bit sigcontext offsets. */");
	offset("#define SC32_FPREGS     ", struct sigcontext32, sc_fpregs);
	offset("#define SC32_FPC_CSR    ", struct sigcontext32, sc_fpc_csr);
	offset("#define SC32_FPC_EIR    ", struct sigcontext32, sc_fpc_eir);
	linefeed;
}
#endif

void output_signal_defined(void)
{
	text("/* Linux signal numbers. */");
	constant("#define _SIGHUP     ", SIGHUP);
	constant("#define _SIGINT     ", SIGINT);
	constant("#define _SIGQUIT    ", SIGQUIT);
	constant("#define _SIGILL     ", SIGILL);
	constant("#define _SIGTRAP    ", SIGTRAP);
	constant("#define _SIGIOT     ", SIGIOT);
	constant("#define _SIGABRT    ", SIGABRT);
	constant("#define _SIGEMT     ", SIGEMT);
	constant("#define _SIGFPE     ", SIGFPE);
	constant("#define _SIGKILL    ", SIGKILL);
	constant("#define _SIGBUS     ", SIGBUS);
	constant("#define _SIGSEGV    ", SIGSEGV);
	constant("#define _SIGSYS     ", SIGSYS);
	constant("#define _SIGPIPE    ", SIGPIPE);
	constant("#define _SIGALRM    ", SIGALRM);
	constant("#define _SIGTERM    ", SIGTERM);
	constant("#define _SIGUSR1    ", SIGUSR1);
	constant("#define _SIGUSR2    ", SIGUSR2);
	constant("#define _SIGCHLD    ", SIGCHLD);
	constant("#define _SIGPWR     ", SIGPWR);
	constant("#define _SIGWINCH   ", SIGWINCH);
	constant("#define _SIGURG     ", SIGURG);
	constant("#define _SIGIO      ", SIGIO);
	constant("#define _SIGSTOP    ", SIGSTOP);
	constant("#define _SIGTSTP    ", SIGTSTP);
	constant("#define _SIGCONT    ", SIGCONT);
	constant("#define _SIGTTIN    ", SIGTTIN);
	constant("#define _SIGTTOU    ", SIGTTOU);
	constant("#define _SIGVTALRM  ", SIGVTALRM);
	constant("#define _SIGPROF    ", SIGPROF);
	constant("#define _SIGXCPU    ", SIGXCPU);
	constant("#define _SIGXFSZ    ", SIGXFSZ);
	linefeed;
}

void output_irq_cpustat_t_defines(void)
{
	text("/* Linux irq_cpustat_t offsets. */");
	offset("#define IC_SOFTIRQ_PENDING ", irq_cpustat_t, __softirq_pending);
	size("#define IC_IRQ_CPUSTAT_T   ", irq_cpustat_t);
	linefeed;
}
