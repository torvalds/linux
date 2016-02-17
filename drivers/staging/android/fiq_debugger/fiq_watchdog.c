/*
 * Copyright (C) 2014 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/pstore_ram.h>

#include "fiq_watchdog.h"
#include "fiq_debugger_priv.h"

static DEFINE_RAW_SPINLOCK(fiq_watchdog_lock);

static void fiq_watchdog_printf(struct fiq_debugger_output *output,
				const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	int len;

	va_start(ap, fmt);
	len = vscnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	ramoops_console_write_buf(buf, len);
}

struct fiq_debugger_output fiq_watchdog_output = {
	.printf = fiq_watchdog_printf,
};

void fiq_watchdog_triggered(const struct pt_regs *regs, void *svc_sp)
{
	char msg[24];
	int len;

	raw_spin_lock(&fiq_watchdog_lock);

	len = scnprintf(msg, sizeof(msg), "watchdog fiq cpu %d\n",
			THREAD_INFO(svc_sp)->cpu);
	ramoops_console_write_buf(msg, len);

	fiq_debugger_dump_stacktrace(&fiq_watchdog_output, regs, 100, svc_sp);

	raw_spin_unlock(&fiq_watchdog_lock);
}
