/**
 * Copyright (C) ARM Limited 2010-2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

/*
 * EABI backtrace stores {fp,lr} on the stack.
 */
struct stack_frame_eabi {
	union {
		struct {
			unsigned long fp;
			// May be the fp in the case of a leaf function or clang
			unsigned long lr;
			// If lr is really the fp, lr2 is the corresponding lr
			unsigned long lr2;
		};
		// Used to read 32 bit fp/lr from a 64 bit kernel
		struct {
			u32 fp_32;
			// same as lr above
			u32 lr_32;
			// same as lr2 above
			u32 lr2_32;
		};
	};
};

static void arm_backtrace_eabi(int cpu, struct pt_regs *const regs, unsigned int depth)
{
#if defined(__arm__) || defined(__aarch64__)
	struct stack_frame_eabi *curr;
	struct stack_frame_eabi bufcurr;
#if defined(__arm__)
	const bool is_compat = false;
	unsigned long fp = regs->ARM_fp;
	unsigned long sp = regs->ARM_sp;
	unsigned long lr = regs->ARM_lr;
	const int gcc_frame_offset = sizeof(unsigned long);
#else
	// Is userspace aarch32 (32 bit)
	const bool is_compat = compat_user_mode(regs);
	unsigned long fp = (is_compat ? regs->regs[11] : regs->regs[29]);
	unsigned long sp = (is_compat ? regs->compat_sp : regs->sp);
	unsigned long lr = (is_compat ? regs->compat_lr : regs->regs[30]);
	const int gcc_frame_offset = (is_compat ? sizeof(u32) : 0);
#endif
	// clang frame offset is always zero
	int is_user_mode = user_mode(regs);

	// pc (current function) has already been added

	if (!is_user_mode) {
		return;
	}

	// Add the lr (parent function)
	// entry preamble may not have executed
	gator_add_trace(cpu, lr);

	// check fp is valid
	if (fp == 0 || fp < sp) {
		return;
	}

	// Get the current stack frame
	curr = (struct stack_frame_eabi *)(fp - gcc_frame_offset);
	if ((unsigned long)curr & 3) {
		return;
	}

	while (depth-- && curr) {
		if (!access_ok(VERIFY_READ, curr, sizeof(struct stack_frame_eabi)) ||
				__copy_from_user_inatomic(&bufcurr, curr, sizeof(struct stack_frame_eabi))) {
			return;
		}

		fp = (is_compat ? bufcurr.fp_32 : bufcurr.fp);
		lr = (is_compat ? bufcurr.lr_32 : bufcurr.lr);

#define calc_next(reg) ((reg) - gcc_frame_offset)
		// Returns true if reg is a valid fp
#define validate_next(reg, curr) \
		((reg) != 0 && (calc_next(reg) & 3) == 0 && (unsigned long)(curr) < calc_next(reg))

		// Try lr from the stack as the fp because gcc leaf functions do not push lr
		// If gcc_frame_offset is non-zero, the lr will also be the clang fp
		// This assumes code is at a lower address than the stack
		if (validate_next(lr, curr)) {
			fp = lr;
			lr = (is_compat ? bufcurr.lr2_32 : bufcurr.lr2);
		}

		gator_add_trace(cpu, lr);

		if (!validate_next(fp, curr)) {
			return;
		}

		// Move to the next stack frame
		curr = (struct stack_frame_eabi *)calc_next(fp);
	}
#endif
}

#if defined(__arm__) || defined(__aarch64__)
static int report_trace(struct stackframe *frame, void *d)
{
	unsigned int *depth = d, cookie = NO_COOKIE;
	unsigned long addr = frame->pc;

	if (*depth) {
#if defined(MODULE)
		unsigned int cpu = get_physical_cpu();
		struct module *mod = __module_address(addr);
		if (mod) {
			cookie = get_cookie(cpu, current, mod->name, false);
			addr = addr - (unsigned long)mod->module_core;
		}
#endif
		marshal_backtrace(addr & ~1, cookie);
		(*depth)--;
	}

	return *depth == 0;
}
#endif

// Uncomment the following line to enable kernel stack unwinding within gator, note it can also be defined from the Makefile
// #define GATOR_KERNEL_STACK_UNWINDING
static void kernel_backtrace(int cpu, struct pt_regs *const regs)
{
#if defined(__arm__) || defined(__aarch64__)
#ifdef GATOR_KERNEL_STACK_UNWINDING
	int depth = gator_backtrace_depth;
#else
	int depth = 1;
#endif
	struct stackframe frame;
	if (depth == 0)
		depth = 1;
#if defined(__arm__)
	frame.fp = regs->ARM_fp;
	frame.sp = regs->ARM_sp;
	frame.lr = regs->ARM_lr;
	frame.pc = regs->ARM_pc;
#else
	frame.fp = regs->regs[29];
	frame.sp = regs->sp;
	frame.pc = regs->pc;
#endif
	walk_stackframe(&frame, report_trace, &depth);
#else
	marshal_backtrace(PC_REG & ~1, NO_COOKIE);
#endif
}
