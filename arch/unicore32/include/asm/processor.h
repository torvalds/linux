/*
 * linux/arch/unicore32/include/asm/processor.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __UNICORE_PROCESSOR_H__
#define __UNICORE_PROCESSOR_H__

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ __label__ _l; _l: &&_l; })

#ifdef __KERNEL__

#include <asm/ptrace.h>
#include <asm/types.h>

#ifdef __KERNEL__
#define STACK_TOP	TASK_SIZE
#define STACK_TOP_MAX	TASK_SIZE
#endif

struct debug_entry {
	u32			address;
	u32			insn;
};

struct debug_info {
	int			nsaved;
	struct debug_entry	bp[2];
};

struct thread_struct {
							/* fault info	  */
	unsigned long		address;
	unsigned long		trap_no;
	unsigned long		error_code;
							/* debugging	  */
	struct debug_info	debug;
};

#define INIT_THREAD  {	}

#define start_thread(regs, pc, sp)					\
({									\
	unsigned long *stack = (unsigned long *)sp;			\
	set_fs(USER_DS);						\
	memset(regs->uregs, 0, sizeof(regs->uregs));			\
	regs->UCreg_asr = USER_MODE;					\
	regs->UCreg_pc = pc & ~1;	/* pc */                        \
	regs->UCreg_sp = sp;		/* sp */                        \
	regs->UCreg_02 = stack[2];	/* r2 (envp) */                 \
	regs->UCreg_01 = stack[1];	/* r1 (argv) */                 \
	regs->UCreg_00 = stack[0];	/* r0 (argc) */                 \
})

/* Forward declaration, a strange C thing */
struct task_struct;

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

/* Prepare to copy thread state - unlazy all lazy status */
#define prepare_to_copy(tsk)	do { } while (0)

unsigned long get_wchan(struct task_struct *p);

#define cpu_relax()			barrier()

/*
 * Create a new kernel thread
 */
extern int kernel_thread(int (*fn)(void *), void *arg, unsigned long flags);

#define task_pt_regs(p) \
	((struct pt_regs *)(THREAD_START_SP + task_stack_page(p)) - 1)

#define KSTK_EIP(tsk)	(task_pt_regs(tsk)->UCreg_pc)
#define KSTK_ESP(tsk)	(task_pt_regs(tsk)->UCreg_sp)

#endif

#endif /* __UNICORE_PROCESSOR_H__ */
