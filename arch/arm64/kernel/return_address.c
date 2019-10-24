// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm64/kernel/return_address.c
 *
 * Copyright (C) 2013 Linaro Limited
 * Author: AKASHI Takahiro <takahiro.akashi@linaro.org>
 */

#include <linux/export.h>
#include <linux/ftrace.h>
#include <linux/kprobes.h>

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
NOKPROBE_SYMBOL(save_return_addr);

void *return_address(unsigned int level)
{
	struct return_address_data data;
	struct stackframe frame;

	data.level = level + 2;
	data.addr = NULL;

	start_backtrace(&frame,
			(unsigned long)__builtin_frame_address(0),
			(unsigned long)return_address);
	walk_stackframe(current, &frame, save_return_addr, &data);

	if (!data.level)
		return data.addr;
	else
		return NULL;
}
EXPORT_SYMBOL_GPL(return_address);
NOKPROBE_SYMBOL(return_address);
