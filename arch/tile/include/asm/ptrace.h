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

#include <arch/chip.h>
#include <arch/abi.h>

/* These must match struct pt_regs, below. */
#if CHIP_WORD_SIZE() == 32
#define PTREGS_OFFSET_REG(n)    ((n)*4)
#else
#define PTREGS_OFFSET_REG(n)    ((n)*8)
#endif
#define PTREGS_OFFSET_BASE      0
#define PTREGS_OFFSET_TP        PTREGS_OFFSET_REG(53)
#define PTREGS_OFFSET_SP        PTREGS_OFFSET_REG(54)
#define PTREGS_OFFSET_LR        PTREGS_OFFSET_REG(55)
#define PTREGS_NR_GPRS          56
#define PTREGS_OFFSET_PC        PTREGS_OFFSET_REG(56)
#define PTREGS_OFFSET_EX1       PTREGS_OFFSET_REG(57)
#define PTREGS_OFFSET_FAULTNUM  PTREGS_OFFSET_REG(58)
#define PTREGS_OFFSET_ORIG_R0   PTREGS_OFFSET_REG(59)
#define PTREGS_OFFSET_FLAGS     PTREGS_OFFSET_REG(60)
#if CHIP_HAS_CMPEXCH()
#define PTREGS_OFFSET_CMPEXCH   PTREGS_OFFSET_REG(61)
#endif
#define PTREGS_SIZE             PTREGS_OFFSET_REG(64)

#ifndef __ASSEMBLY__

#ifdef __KERNEL__
/* Benefit from consistent use of "long" on all chips. */
typedef unsigned long pt_reg_t;
#else
/* Provide appropriate length type to userspace regardless of -m32/-m64. */
typedef uint_reg_t pt_reg_t;
#endif

/*
 * This struct defines the way the registers are stored on the stack during a
 * system call or exception.  "struct sigcontext" has the same shape.
 */
struct pt_regs {
	/* Saved main processor registers; 56..63 are special. */
	/* tp, sp, and lr must immediately follow regs[] for aliasing. */
	pt_reg_t regs[53];
	pt_reg_t tp;		/* aliases regs[TREG_TP] */
	pt_reg_t sp;		/* aliases regs[TREG_SP] */
	pt_reg_t lr;		/* aliases regs[TREG_LR] */

	/* Saved special registers. */
	pt_reg_t pc;		/* stored in EX_CONTEXT_K_0 */
	pt_reg_t ex1;		/* stored in EX_CONTEXT_K_1 (PL and ICS bit) */
	pt_reg_t faultnum;	/* fault number (INT_SWINT_1 for syscall) */
	pt_reg_t orig_r0;	/* r0 at syscall entry, else zero */
	pt_reg_t flags;		/* flags (see below) */
#if !CHIP_HAS_CMPEXCH()
	pt_reg_t pad[3];
#else
	pt_reg_t cmpexch;	/* value of CMPEXCH_VALUE SPR at interrupt */
	pt_reg_t pad[2];
#endif
};

#endif /* __ASSEMBLY__ */

#define PTRACE_GETREGS		12
#define PTRACE_SETREGS		13
#define PTRACE_GETFPREGS	14
#define PTRACE_SETFPREGS	15

/* Support TILE-specific ptrace options, with events starting at 16. */
#define PTRACE_O_TRACEMIGRATE	0x00010000
#define PTRACE_EVENT_MIGRATE	16
#ifdef __KERNEL__
#define PTRACE_O_MASK_TILE	(PTRACE_O_TRACEMIGRATE)
#define PT_TRACE_MIGRATE	0x00080000
#define PT_TRACE_MASK_TILE	(PT_TRACE_MIGRATE)
#endif

#ifdef __KERNEL__

/* Flag bits in pt_regs.flags */
#define PT_FLAGS_DISABLE_IRQ    1  /* on return to kernel, disable irqs */
#define PT_FLAGS_CALLER_SAVES   2  /* caller-save registers are valid */
#define PT_FLAGS_RESTORE_REGS   4  /* restore callee-save regs on return */

#ifndef __ASSEMBLY__

#define instruction_pointer(regs) ((regs)->pc)
#define profile_pc(regs) instruction_pointer(regs)

/* Does the process account for user or for system time? */
#define user_mode(regs) (EX1_PL((regs)->ex1) == USER_PL)

/* Fill in a struct pt_regs with the current kernel registers. */
struct pt_regs *get_pt_regs(struct pt_regs *);

/* Trace the current syscall. */
extern void do_syscall_trace(void);

extern void show_regs(struct pt_regs *);

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

#endif /* !__KERNEL__ */

#endif /* _ASM_TILE_PTRACE_H */
