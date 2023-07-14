// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 */
#include <asm/unwind.h>

unsigned long unwind_get_return_address(struct unwind_state *state)
{
	return __unwind_get_return_address(state);
}
EXPORT_SYMBOL_GPL(unwind_get_return_address);

void unwind_start(struct unwind_state *state, struct task_struct *task,
		    struct pt_regs *regs)
{
	__unwind_start(state, task, regs);
	if (!unwind_done(state) && !__kernel_text_address(state->pc))
		unwind_next_frame(state);
}
EXPORT_SYMBOL_GPL(unwind_start);

bool unwind_next_frame(struct unwind_state *state)
{
	return default_next_frame(state);
}
EXPORT_SYMBOL_GPL(unwind_next_frame);
