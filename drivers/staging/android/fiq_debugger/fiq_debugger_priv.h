/*
 * Copyright (C) 2014 Google, Inc.
 * Author: Colin Cross <ccross@android.com>
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

#ifndef _FIQ_DEBUGGER_PRIV_H_
#define _FIQ_DEBUGGER_PRIV_H_

#define THREAD_INFO(sp) ((struct thread_info *) \
		((unsigned long)(sp) & ~(THREAD_SIZE - 1)))

struct fiq_debugger_output {
	void (*printf)(struct fiq_debugger_output *output, const char *fmt, ...);
};

struct pt_regs;

void fiq_debugger_dump_pc(struct fiq_debugger_output *output,
		const struct pt_regs *regs);
void fiq_debugger_dump_regs(struct fiq_debugger_output *output,
		const struct pt_regs *regs);
void fiq_debugger_dump_allregs(struct fiq_debugger_output *output,
		const struct pt_regs *regs);
void fiq_debugger_dump_stacktrace(struct fiq_debugger_output *output,
		const struct pt_regs *regs, unsigned int depth, void *ssp);

#endif
