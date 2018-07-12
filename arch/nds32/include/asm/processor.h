// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef __ASM_NDS32_PROCESSOR_H
#define __ASM_NDS32_PROCESSOR_H

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ __label__ _l; _l: &&_l;})

#ifdef __KERNEL__

#include <asm/ptrace.h>
#include <asm/types.h>
#include <asm/sigcontext.h>

#define KERNEL_STACK_SIZE	PAGE_SIZE
#define STACK_TOP	TASK_SIZE
#define STACK_TOP_MAX   TASK_SIZE

struct cpu_context {
	unsigned long r6;
	unsigned long r7;
	unsigned long r8;
	unsigned long r9;
	unsigned long r10;
	unsigned long r11;
	unsigned long r12;
	unsigned long r13;
	unsigned long r14;
	unsigned long fp;
	unsigned long pc;
	unsigned long sp;
};

struct thread_struct {
	struct cpu_context cpu_context;	/* cpu context */
	/* fault info     */
	unsigned long address;
	unsigned long trap_no;
	unsigned long error_code;
};

#define INIT_THREAD  {	}

#ifdef __NDS32_EB__
#define PSW_DE	PSW_mskBE
#else
#define PSW_DE	0x0
#endif

#ifdef CONFIG_WBNA
#define PSW_valWBNA	PSW_mskWBNA
#else
#define PSW_valWBNA	0x0
#endif

#ifdef CONFIG_HWZOL
#define	PSW_valINIT (PSW_CPL_ANY | PSW_mskAEN | PSW_valWBNA | PSW_mskDT | PSW_mskIT | PSW_DE | PSW_mskGIE)
#else
#define	PSW_valINIT (PSW_CPL_ANY | PSW_valWBNA | PSW_mskDT | PSW_mskIT | PSW_DE | PSW_mskGIE)
#endif

#define start_thread(regs,pc,stack)			\
({							\
	memzero(regs, sizeof(struct pt_regs));		\
	forget_syscall(regs);				\
	regs->ipsw = PSW_valINIT;			\
	regs->ir0 = (PSW_CPL_ANY | PSW_valWBNA | PSW_mskDT | PSW_mskIT | PSW_DE | PSW_SYSTEM | PSW_INTL_1);	\
	regs->ipc = pc;					\
	regs->sp = stack;				\
})

/* Forward declaration, a strange C thing */
struct task_struct;

/* Free all resources held by a thread. */
#define release_thread(thread) do { } while(0)

/* Prepare to copy thread state - unlazy all lazy status */
#define prepare_to_copy(tsk)	do { } while (0)

unsigned long get_wchan(struct task_struct *p);

#define cpu_relax()			barrier()

#define task_pt_regs(task) \
	((struct pt_regs *) (task_stack_page(task) + THREAD_SIZE \
		- 8) - 1)

/*
 * Create a new kernel thread
 */
extern int kernel_thread(int (*fn) (void *), void *arg, unsigned long flags);

#define KSTK_EIP(tsk)	instruction_pointer(task_pt_regs(tsk))
#define KSTK_ESP(tsk)	user_stack_pointer(task_pt_regs(tsk))

#endif

#endif /* __ASM_NDS32_PROCESSOR_H */
