/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Most of this ideas comes from x86.
 *
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_UNWIND_H
#define _ASM_UNWIND_H

#include <linux/sched.h>

#include <asm/stacktrace.h>

enum unwinder_type {
	UNWINDER_GUESS,
	UNWINDER_PROLOGUE,
};

struct unwind_state {
	char type; /* UNWINDER_XXX */
	struct stack_info stack_info;
	struct task_struct *task;
	bool first, error;
	unsigned long sp, pc, ra;
};

void unwind_start(struct unwind_state *state,
		  struct task_struct *task, struct pt_regs *regs);
bool unwind_next_frame(struct unwind_state *state);
unsigned long unwind_get_return_address(struct unwind_state *state);

static inline bool unwind_done(struct unwind_state *state)
{
	return state->stack_info.type == STACK_TYPE_UNKNOWN;
}

static inline bool unwind_error(struct unwind_state *state)
{
	return state->error;
}

#endif /* _ASM_UNWIND_H */
