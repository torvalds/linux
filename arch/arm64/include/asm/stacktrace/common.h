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

#include <linux/types.h>

struct stack_info {
	unsigned long low;
	unsigned long high;
};

/**
 * struct unwind_state - state used for robust unwinding.
 *
 * @fp:          The fp value in the frame record (or the real fp)
 * @pc:          The lr value in the frame record (or the real lr)
 *
 * @stack:       The stack currently being unwound.
 * @stacks:      An array of stacks which can be unwound.
 * @nr_stacks:   The number of stacks in @stacks.
 */
struct unwind_state {
	unsigned long fp;
	unsigned long pc;

	struct stack_info stack;
	struct stack_info *stacks;
	int nr_stacks;
};

static inline struct stack_info stackinfo_get_unknown(void)
{
	return (struct stack_info) {
		.low = 0,
		.high = 0,
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

static inline void unwind_init_common(struct unwind_state *state)
{
	state->stack = stackinfo_get_unknown();
}

static struct stack_info *unwind_find_next_stack(const struct unwind_state *state,
						 unsigned long sp,
						 unsigned long size)
{
	for (int i = 0; i < state->nr_stacks; i++) {
		struct stack_info *info = &state->stacks[i];

		if (stackinfo_on_stack(info, sp, size))
			return info;
	}

	return NULL;
}

/**
 * unwind_consume_stack() - Check if an object is on an accessible stack,
 * updating stack boundaries so that future unwind steps cannot consume this
 * object again.
 *
 * @state: the current unwind state.
 * @sp:    the base address of the object.
 * @size:  the size of the object.
 *
 * Return: 0 upon success, an error code otherwise.
 */
static inline int unwind_consume_stack(struct unwind_state *state,
				       unsigned long sp,
				       unsigned long size)
{
	struct stack_info *next;

	if (stackinfo_on_stack(&state->stack, sp, size))
		goto found;

	next = unwind_find_next_stack(state, sp, size);
	if (!next)
		return -EINVAL;

	/*
	 * Stack transitions are strictly one-way, and once we've
	 * transitioned from one stack to another, it's never valid to
	 * unwind back to the old stack.
	 *
	 * Remove the current stack from the list of stacks so that it cannot
	 * be found on a subsequent transition.
	 *
	 * Note that stacks can nest in several valid orders, e.g.
	 *
	 *   TASK -> IRQ -> OVERFLOW -> SDEI_NORMAL
	 *   TASK -> SDEI_NORMAL -> SDEI_CRITICAL -> OVERFLOW
	 *   HYP -> OVERFLOW
	 *
	 * ... so we do not check the specific order of stack
	 * transitions.
	 */
	state->stack = *next;
	*next = stackinfo_get_unknown();

found:
	/*
	 * Future unwind steps can only consume stack above this frame record.
	 * Update the current stack to start immediately above it.
	 */
	state->stack.low = sp + size;
	return 0;
}

/**
 * unwind_next_frame_record() - Unwind to the next frame record.
 *
 * @state:        the current unwind state.
 *
 * Return: 0 upon success, an error code otherwise.
 */
static inline int
unwind_next_frame_record(struct unwind_state *state)
{
	unsigned long fp = state->fp;
	int err;

	if (fp & 0x7)
		return -EINVAL;

	err = unwind_consume_stack(state, fp, 16);
	if (err)
		return err;

	/*
	 * Record this frame record's values.
	 */
	state->fp = READ_ONCE(*(unsigned long *)(fp));
	state->pc = READ_ONCE(*(unsigned long *)(fp + 8));

	return 0;
}

#endif	/* __ASM_STACKTRACE_COMMON_H */
