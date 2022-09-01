/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Common arm64 stack unwinder code.
 *
 * See: arch/arm64/kernel/stacktrace.c for the reference implementation.
 *
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_STACKTRACE_COMMON_H
#define __ASM_STACKTRACE_COMMON_H

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/kprobes.h>
#include <linux/types.h>

enum stack_type {
	STACK_TYPE_UNKNOWN,
	STACK_TYPE_TASK,
	STACK_TYPE_IRQ,
	STACK_TYPE_OVERFLOW,
	STACK_TYPE_SDEI_NORMAL,
	STACK_TYPE_SDEI_CRITICAL,
	STACK_TYPE_HYP,
	__NR_STACK_TYPES
};

struct stack_info {
	unsigned long low;
	unsigned long high;
	enum stack_type type;
};

/**
 * struct unwind_state - state used for robust unwinding.
 *
 * @fp:          The fp value in the frame record (or the real fp)
 * @pc:          The lr value in the frame record (or the real lr)
 *
 * @stacks_done: Stacks which have been entirely unwound, for which it is no
 *               longer valid to unwind to.
 *
 * @prev_fp:     The fp that pointed to this frame record, or a synthetic value
 *               of 0. This is used to ensure that within a stack, each
 *               subsequent frame record is at an increasing address.
 * @prev_type:   The type of stack this frame record was on, or a synthetic
 *               value of STACK_TYPE_UNKNOWN. This is used to detect a
 *               transition from one stack to another.
 *
 * @kr_cur:      When KRETPROBES is selected, holds the kretprobe instance
 *               associated with the most recently encountered replacement lr
 *               value.
 *
 * @task:        The task being unwound.
 */
struct unwind_state {
	unsigned long fp;
	unsigned long pc;
	DECLARE_BITMAP(stacks_done, __NR_STACK_TYPES);
	unsigned long prev_fp;
	enum stack_type prev_type;
#ifdef CONFIG_KRETPROBES
	struct llist_node *kr_cur;
#endif
	struct task_struct *task;
};

static inline struct stack_info stackinfo_get_unknown(void)
{
	return (struct stack_info) {
		.low = 0,
		.high = 0,
		.type = STACK_TYPE_UNKNOWN,
	};
}

static inline bool stackinfo_on_stack(const struct stack_info *info,
				      unsigned long sp, unsigned long size)
{
	if (!info->low)
		return false;

	if (sp < info->low || sp + size < sp || sp + size > info->high)
		return false;

	return true;
}

static inline void unwind_init_common(struct unwind_state *state,
				      struct task_struct *task)
{
	state->task = task;
#ifdef CONFIG_KRETPROBES
	state->kr_cur = NULL;
#endif

	/*
	 * Prime the first unwind.
	 *
	 * In unwind_next() we'll check that the FP points to a valid stack,
	 * which can't be STACK_TYPE_UNKNOWN, and the first unwind will be
	 * treated as a transition to whichever stack that happens to be. The
	 * prev_fp value won't be used, but we set it to 0 such that it is
	 * definitely not an accessible stack address.
	 */
	bitmap_zero(state->stacks_done, __NR_STACK_TYPES);
	state->prev_fp = 0;
	state->prev_type = STACK_TYPE_UNKNOWN;
}

/**
 * typedef stack_trace_translate_fp_fn() - Translates a non-kernel frame
 * pointer to a kernel address.
 *
 * @fp:   the frame pointer to be updated to its kernel address.
 *
 * Return: true if the VA can be translated, false otherwise.
 *
 * Upon success @fp is updated to the corresponding kernel virtual address.
 */
typedef bool (*stack_trace_translate_fp_fn)(unsigned long *fp);

/**
 * typedef on_accessible_stack_fn() - Check whether a stack range is on any of
 * the possible stacks.
 *
 * @tsk:  task whose stack is being unwound
 * @sp:   stack address being checked
 * @size: size of the stack range being checked
 * @info: stack unwinding context
 *
 * Return: true if the stack range is accessible, false otherwise.
 *
 * Upon success @info is updated with information for the relevant stack.
 *
 * Upon failure @info is updated with the UNKNOWN stack.
 */
typedef bool (*on_accessible_stack_fn)(const struct task_struct *tsk,
				       unsigned long sp, unsigned long size,
				       struct stack_info *info);

/**
 * unwind_next_frame_record() - Unwind to the next frame record.
 *
 * @state:        the current unwind state.
 * @accessible:   determines whether the frame record is accessible
 * @translate_fp: translates the fp prior to access (may be NULL)
 *
 * Return: 0 upon success, an error code otherwise.
 */
static inline int
unwind_next_frame_record(struct unwind_state *state,
			 on_accessible_stack_fn accessible,
			 stack_trace_translate_fp_fn translate_fp)
{
	struct stack_info info;
	unsigned long fp = state->fp, kern_fp = fp;
	struct task_struct *tsk = state->task;

	if (fp & 0x7)
		return -EINVAL;

	if (!accessible(tsk, fp, 16, &info))
		return -EINVAL;

	if (test_bit(info.type, state->stacks_done))
		return -EINVAL;

	/*
	 * If fp is not from the current address space perform the necessary
	 * translation before dereferencing it to get the next fp.
	 */
	if (translate_fp && !translate_fp(&kern_fp))
		return -EINVAL;

	/*
	 * As stacks grow downward, any valid record on the same stack must be
	 * at a strictly higher address than the prior record.
	 *
	 * Stacks can nest in several valid orders, e.g.
	 *
	 * TASK -> IRQ -> OVERFLOW -> SDEI_NORMAL
	 * TASK -> SDEI_NORMAL -> SDEI_CRITICAL -> OVERFLOW
	 * HYP -> OVERFLOW
	 *
	 * ... but the nesting itself is strict. Once we transition from one
	 * stack to another, it's never valid to unwind back to that first
	 * stack.
	 */
	if (info.type == state->prev_type) {
		if (fp <= state->prev_fp)
			return -EINVAL;
	} else {
		__set_bit(state->prev_type, state->stacks_done);
	}

	/*
	 * Record this frame record's values and location. The prev_fp and
	 * prev_type are only meaningful to the next unwind_next() invocation.
	 */
	state->fp = READ_ONCE(*(unsigned long *)(kern_fp));
	state->pc = READ_ONCE(*(unsigned long *)(kern_fp + 8));
	state->prev_fp = fp;
	state->prev_type = info.type;

	return 0;
}

#endif	/* __ASM_STACKTRACE_COMMON_H */
