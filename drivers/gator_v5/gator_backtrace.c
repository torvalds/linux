/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

/*
 * EABI backtrace stores {fp,lr} on the stack.
 */
struct frame_tail_eabi {
	unsigned long fp; // points to prev_lr
	unsigned long lr;
};

static void arm_backtrace_eabi(int cpu, int buftype, struct pt_regs * const regs, unsigned int depth)
{
#if defined(__arm__)
	struct frame_tail_eabi *tail;
	struct frame_tail_eabi *next;
	struct frame_tail_eabi *ptrtail;
	struct frame_tail_eabi buftail;
	unsigned long fp = regs->ARM_fp;
	unsigned long sp = regs->ARM_sp;
	unsigned long lr = regs->ARM_lr;
	int is_user_mode = user_mode(regs);

	if (!is_user_mode) {
		return;
	}

	/* entry preamble may not have executed */
	gator_add_trace(cpu, buftype, lr);

	/* check tail is valid */
	if (fp == 0 || fp < sp) {
		return;
	}

	tail = (struct frame_tail_eabi *)(fp - 4);

	while (depth-- && tail && !((unsigned long) tail & 3)) {
		/* Also check accessibility of one struct frame_tail beyond */
		if (!access_ok(VERIFY_READ, tail, sizeof(struct frame_tail_eabi)))
			return;
		if (__copy_from_user_inatomic(&buftail, tail, sizeof(struct frame_tail_eabi)))
			return;
		ptrtail = &buftail;

		lr = ptrtail[0].lr;
		gator_add_trace(cpu, buftype, lr);

		/* frame pointers should progress back up the stack, towards higher addresses */
		next = (struct frame_tail_eabi *)(lr - 4);
		if (tail >= next || lr == 0) {
			fp = ptrtail[0].fp;
			next = (struct frame_tail_eabi *)(fp - 4);
			/* check tail is valid */
			if (tail >= next || fp == 0) {
				return;
			}
		}

		tail = next;
	}
#endif
}

#if defined(__arm__)
static DEFINE_PER_CPU(int, backtrace_buffer);
static int report_trace(struct stackframe *frame, void *d)
{
	struct module *mod;
	unsigned int *depth = d, addr = frame->pc, cookie = NO_COOKIE, cpu = smp_processor_id();

	if (*depth) {
		mod = __module_address(addr);
		if (mod) {
			cookie = get_cookie(cpu, per_cpu(backtrace_buffer, cpu), current, NULL, mod, true);
			addr = addr - (unsigned long)mod->module_core;
		}
		marshal_backtrace(addr & ~1, cookie);
		(*depth)--;
	}

	return *depth == 0;
}
#endif

// Uncomment the following line to enable kernel stack unwinding within gator, note it can also be defined from the Makefile
// #define GATOR_KERNEL_STACK_UNWINDING
static void kernel_backtrace(int cpu, int buftype, struct pt_regs * const regs)
{
#if defined(__arm__)
#ifdef GATOR_KERNEL_STACK_UNWINDING
	int depth = gator_backtrace_depth;
#else
	int depth = 1;
#endif
	struct stackframe frame;
	if (depth == 0)
		depth = 1;
	frame.fp = regs->ARM_fp;
	frame.sp = regs->ARM_sp;
	frame.lr = regs->ARM_lr;
	frame.pc = regs->ARM_pc;
	per_cpu(backtrace_buffer, cpu) = buftype;
	walk_stackframe(&frame, report_trace, &depth);
#else
	marshal_backtrace(PC_REG & ~1, NO_COOKIE);
#endif
}
