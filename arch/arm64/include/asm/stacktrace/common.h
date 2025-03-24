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

/**
 * unwind_find_stack() - Find the accessible stack which entirely contains an
 * object.
 *
 * @state: the current unwind state.
 * @sp:    the base address of the object.
 * @size:  the size of the object.
 *
 * Return: a pointer to the relevant stack_info if found; NULL otherwise.
 */
static struct stack_info *unwind_find_stack(struct unwind_state *state,
					    unsigned long sp,
					    unsigned long size)
{
	struct stack_info *info = &state->stack;

	if (stackinfo_on_stack(info, sp, size))
		return info;

	for (int i = 0; i < state->nr_stacks; i++) {
		info = &state->stacks[i];
		if (stackinfo_on_stack(info, sp, size))
			return info;
	}

	return NULL;
}

/**
 * unwind_consume_stack() - Update stack boundaries so that future unwind steps
 * cannot consume this object again.
 *
 * @state: the current unwind state.
 * @info:  the stack_info of the stack containing the object.
 * @sp:    the base address of the object.
 * @size:  the size of the object.
 *
 * Return: 0 upon success, an error code otherwise.
 */
static inline void unwind_consume_stack(struct unwind_state *state,
					struct stack_info *info,
					unsigned long sp,
					unsigned long size)
{
	struct stack_info tmp;

	/*
	 * Stack transitions are strictly one-way, and once we've
	 * transitioned from one stack to another, it's never valid to
	 * unwind back to the old stack.
	 *
	 * Destroy the old stack info so that it cannot be found upon a
	 * subsequent transition. If the stack has not changed, we'll
	 * immediately restore the current stack info.
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
	tmp = *info;
	*info = stackinfo_get_unknown();
	state->stack = tmp;

	/*
	 * Future unwind steps can only consume stack above this frame record.
	 * Update the current stack to start immediately above it.
	 */
	state->stack.low = sp + size;
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
	struct stack_info *info;
	struct frame_record *record;
	unsigned long fp = state->fp;

	if (fp & 0x7)
		return -EINVAL;

	info = unwind_find_stack(state, fp, sizeof(*record));
	if (!info)
		return -EINVAL;

	unwind_consume_stack(state, info, fp, sizeof(*record));

	/*
	 * Record this frame record's values.
	 */
	record = (struct frame_record *)fp;
	state->fp = READ_ONCE(record->fp);
	state->pc = READ_ONCE(record->lr);

	return 0;
}

#endif	/* __ASM_STACKTRACE_COMMON_H */
