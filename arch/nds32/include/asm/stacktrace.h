/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2008-2018 Andes Technology Corporation */

#ifndef __ASM_STACKTRACE_H
#define __ASM_STACKTRACE_H

/* Kernel callchain */
struct stackframe {
	unsigned long fp;
	unsigned long sp;
	unsigned long lp;
};

/*
 * struct frame_tail: User callchain
 * IMPORTANT:
 * This struct is used for call-stack walking,
 * the order and types matters.
 * Do not use array, it only stores sizeof(pointer)
 *
 * The details can refer to arch/arm/kernel/perf_event.c
 */
struct frame_tail {
	unsigned long stack_fp;
	unsigned long stack_lp;
};

/* For User callchain with optimize for size */
struct frame_tail_opt_size {
	unsigned long stack_r6;
	unsigned long stack_fp;
	unsigned long stack_gp;
	unsigned long stack_lp;
};

extern void
get_real_ret_addr(unsigned long *addr, struct task_struct *tsk, int *graph);

#endif /* __ASM_STACKTRACE_H */
