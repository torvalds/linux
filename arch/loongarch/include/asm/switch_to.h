/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_SWITCH_TO_H
#define _ASM_SWITCH_TO_H

#include <asm/cpu-features.h>
#include <asm/fpu.h>
#include <asm/lbt.h>

struct task_struct;

/**
 * __switch_to - switch execution of a task
 * @prev:	The task previously executed.
 * @next:	The task to begin executing.
 * @next_ti:	task_thread_info(next).
 * @sched_ra:	__schedule return address.
 * @sched_cfa:	__schedule call frame address.
 *
 * This function is used whilst scheduling to save the context of prev & load
 * the context of next. Returns prev.
 */
extern asmlinkage struct task_struct *__switch_to(struct task_struct *prev,
			struct task_struct *next, struct thread_info *next_ti,
			void *sched_ra, void *sched_cfa);

/*
 * For newly created kernel threads switch_to() will return to
 * ret_from_kernel_thread, newly created user threads to ret_from_fork.
 * That is, everything following __switch_to() will be skipped for new threads.
 * So everything that matters to new threads should be placed before __switch_to().
 */
#define switch_to(prev, next, last)						\
do {										\
	lose_fpu_inatomic(1, prev);						\
	lose_lbt_inatomic(1, prev);						\
	hw_breakpoint_thread_switch(next);					\
	(last) = __switch_to(prev, next, task_thread_info(next),		\
		 __builtin_return_address(0), __builtin_frame_address(0));	\
} while (0)

#endif /* _ASM_SWITCH_TO_H */
