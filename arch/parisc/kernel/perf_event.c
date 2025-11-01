// SPDX-License-Identifier: GPL-2.0
/*
 * Performance event support for parisc
 *
 * Copyright (C) 2025 by Helge Deller <deller@gmx.de>
 */

#include <linux/kernel.h>
#include <linux/perf_event.h>
#include <asm/unwind.h>

void perf_callchain_kernel(struct perf_callchain_entry_ctx *entry,
			   struct pt_regs *regs)
{

	struct unwind_frame_info info;

	unwind_frame_init_task(&info, current, NULL);
	while (1) {
		if (unwind_once(&info) < 0 || info.ip == 0)
			break;

		if (!__kernel_text_address(info.ip) ||
			perf_callchain_store(entry, info.ip))
				return;
	}
}
