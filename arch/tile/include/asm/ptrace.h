/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */
#ifndef _ASM_TILE_PTRACE_H
#define _ASM_TILE_PTRACE_H

#include <linux/compiler.h>

#ifndef __ASSEMBLY__
/* Benefit from consistent use of "long" on all chips. */
typedef unsigned long pt_reg_t;
#endif

#include <uapi/asm/ptrace.h>

#define PTRACE_O_MASK_TILE	(PTRACE_O_TRACEMIGRATE)
#define PT_TRACE_MIGRATE	PT_EVENT_FLAG(PTRACE_EVENT_MIGRATE)

/* Flag bits in pt_regs.flags */
#define PT_FLAGS_DISABLE_IRQ    1  /* on return to kernel, disable irqs */
#define PT_FLAGS_CALLER_SAVES   2  /* caller-save registers are valid */
#define PT_FLAGS_RESTORE_REGS   4  /* restore callee-save regs on return */

#ifndef __ASSEMBLY__

#define instruction_pointer(regs) ((regs)->pc)
#define profile_pc(regs) instruction_pointer(regs)
#define user_stack_pointer(regs) ((regs)->sp)

/* Does the process account for user or for system time? */
#define user_mode(regs) (EX1_PL((regs)->ex1) == USER_PL)

/* Fill in a struct pt_regs with the current kernel registers. */
struct pt_regs *get_pt_regs(struct pt_regs *);

/* Trace the current syscall. */
extern void do_syscall_trace(void);

#define arch_has_single_step()	(1)

/*
 * A structure for all single-stepper state.
 *
 * Also update defines in assembler section if it changes
 */
struct single_step_state {
	/* the page to which we will write hacked-up bundles */
	void __user *buffer;

	union {
		int flags;
		struct {
			unsigned long is_enabled:1, update:1, update_reg:6;
		};
	};

	unsigned long orig_pc;		/* the original PC */
	unsigned long next_pc;		/* return PC if no branch (PC + 1) */
	unsigned long branch_next_pc;	/* return PC if we did branch/jump */
	unsigned long update_value;	/* value to restore to update_target */
};

/* Single-step the instruction at regs->pc */
extern void single_step_once(struct pt_regs *regs);

/* Clean up after execve(). */
extern void single_step_execve(void);

struct task_struct;

extern void send_sigtrap(struct task_struct *tsk, struct pt_regs *regs,
			 int error_code);

#ifdef __tilegx__
/* We need this since sigval_t has a user pointer in it, for GETSIGINFO etc. */
#define __ARCH_WANT_COMPAT_SYS_PTRACE
#endif

#endif /* !__ASSEMBLY__ */

#define SINGLESTEP_STATE_MASK_IS_ENABLED      0x1
#define SINGLESTEP_STATE_MASK_UPDATE          0x2
#define SINGLESTEP_STATE_TARGET_LB              2
#define SINGLESTEP_STATE_TARGET_UB              7

#endif /* _ASM_TILE_PTRACE_H */
