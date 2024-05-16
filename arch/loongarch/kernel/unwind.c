// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022-2023 Loongson Technology Corporation Limited
 */
#include <linux/kernel.h>
#include <linux/ftrace.h>

#include <asm/unwind.h>

bool default_next_frame(struct unwind_state *state)
{
	struct stack_info *info = &state->stack_info;
	unsigned long addr;

	if (unwind_done(state))
		return false;

	do {
		for (state->sp += sizeof(unsigned long);
		     state->sp < info->end; state->sp += sizeof(unsigned long)) {
			addr = *(unsigned long *)(state->sp);
			state->pc = unwind_graph_addr(state, addr, state->sp + 8);
			if (__kernel_text_address(state->pc))
				return true;
		}

		state->sp = info->next_sp;

	} while (!get_stack_info(state->sp, state->task, info));

	return false;
}
