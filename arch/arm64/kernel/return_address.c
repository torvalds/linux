/*
 * arch/arm64/kernel/return_address.c
 *
 * Copyright (C) 2013 Linaro Limited
 * Author: AKASHI Takahiro <takahiro.akashi@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/export.h>
#include <linux/ftrace.h>

#include <asm/stack_pointer.h>
#include <asm/stacktrace.h>

struct return_address_data {
	unsigned int level;
	void *addr;
};

static int save_return_addr(struct stackframe *frame, void *d)
{
	struct return_address_data *data = d;

	if (!data->level) {
		data->addr = (void *)frame->pc;
		return 1;
	} else {
		--data->level;
		return 0;
	}
}

void *return_address(unsigned int level)
{
	struct return_address_data data;
	struct stackframe frame;

	data.level = level + 2;
	data.addr = NULL;

	frame.fp = (unsigned long)__builtin_frame_address(0);
	frame.pc = (unsigned long)return_address; /* dummy */
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	frame.graph = current->curr_ret_stack;
#endif

	walk_stackframe(current, &frame, save_return_addr, &data);

	if (!data.level)
		return data.addr;
	else
		return NULL;
}
EXPORT_SYMBOL_GPL(return_address);
