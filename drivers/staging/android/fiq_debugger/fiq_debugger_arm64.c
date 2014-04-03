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

#include <linux/ptrace.h>
#include <asm/stacktrace.h>

#include "fiq_debugger_priv.h"

static char *mode_name(const struct pt_regs *regs)
{
	if (compat_user_mode(regs)) {
		return "USR";
	} else {
		switch (processor_mode(regs)) {
		case PSR_MODE_EL0t: return "EL0t";
		case PSR_MODE_EL1t: return "EL1t";
		case PSR_MODE_EL1h: return "EL1h";
		case PSR_MODE_EL2t: return "EL2t";
		case PSR_MODE_EL2h: return "EL2h";
		default: return "???";
		}
	}
}

void fiq_debugger_dump_pc(struct fiq_debugger_state *state,
		const struct pt_regs *regs)
{
	fiq_debugger_printf(state, " pc %016lx cpsr %08lx mode %s\n",
		regs->pc, regs->pstate, mode_name(regs));
}

void fiq_debugger_dump_regs_aarch32(struct fiq_debugger_state *state,
		const struct pt_regs *regs)
{
	fiq_debugger_printf(state, " r0 %08x  r1 %08x  r2 %08x  r3 %08x\n",
			regs->compat_usr(0), regs->compat_usr(1),
			regs->compat_usr(2), regs->compat_usr(3));
	fiq_debugger_printf(state, " r4 %08x  r5 %08x  r6 %08x  r7 %08x\n",
			regs->compat_usr(4), regs->compat_usr(5),
			regs->compat_usr(6), regs->compat_usr(7));
	fiq_debugger_printf(state, " r8 %08x  r9 %08x r10 %08x r11 %08x\n",
			regs->compat_usr(8), regs->compat_usr(9),
			regs->compat_usr(10), regs->compat_usr(11));
	fiq_debugger_printf(state, " ip %08x  sp %08x  lr %08x  pc %08x\n",
			regs->compat_usr(12), regs->compat_sp,
			regs->compat_lr, regs->pc);
	fiq_debugger_printf(state, " cpsr %08x (%s)\n",
			regs->pstate, mode_name(regs));
}

void fiq_debugger_dump_regs_aarch64(struct fiq_debugger_state *state,
		const struct pt_regs *regs)
{

	fiq_debugger_printf(state, "  x0 %016lx   x1 %016lx\n",
			regs->regs[0], regs->regs[1]);
	fiq_debugger_printf(state, "  x2 %016lx   x3 %016lx\n",
			regs->regs[2], regs->regs[3]);
	fiq_debugger_printf(state, "  x4 %016lx   x5 %016lx\n",
			regs->regs[4], regs->regs[5]);
	fiq_debugger_printf(state, "  x6 %016lx   x7 %016lx\n",
			regs->regs[6], regs->regs[7]);
	fiq_debugger_printf(state, "  x8 %016lx   x9 %016lx\n",
			regs->regs[8], regs->regs[9]);
	fiq_debugger_printf(state, " x10 %016lx  x11 %016lx\n",
			regs->regs[10], regs->regs[11]);
	fiq_debugger_printf(state, " x12 %016lx  x13 %016lx\n",
			regs->regs[12], regs->regs[13]);
	fiq_debugger_printf(state, " x14 %016lx  x15 %016lx\n",
			regs->regs[14], regs->regs[15]);
	fiq_debugger_printf(state, " x16 %016lx  x17 %016lx\n",
			regs->regs[16], regs->regs[17]);
	fiq_debugger_printf(state, " x18 %016lx  x19 %016lx\n",
			regs->regs[18], regs->regs[19]);
	fiq_debugger_printf(state, " x20 %016lx  x21 %016lx\n",
			regs->regs[20], regs->regs[21]);
	fiq_debugger_printf(state, " x22 %016lx  x23 %016lx\n",
			regs->regs[22], regs->regs[23]);
	fiq_debugger_printf(state, " x24 %016lx  x25 %016lx\n",
			regs->regs[24], regs->regs[25]);
	fiq_debugger_printf(state, " x26 %016lx  x27 %016lx\n",
			regs->regs[26], regs->regs[27]);
	fiq_debugger_printf(state, " x28 %016lx  x29 %016lx\n",
			regs->regs[28], regs->regs[29]);
	fiq_debugger_printf(state, " x30 %016lx   sp %016lx\n",
			regs->regs[30], regs->sp);
	fiq_debugger_printf(state, "  pc %016lx cpsr %08x (%s)\n",
			regs->pc, regs->pstate, mode_name(regs));
}

void fiq_debugger_dump_regs(struct fiq_debugger_state *state,
		const struct pt_regs *regs)
{
	if (compat_user_mode(regs))
		fiq_debugger_dump_regs_aarch32(state, regs);
	else
		fiq_debugger_dump_regs_aarch64(state, regs);
}

#define READ_SPECIAL_REG(x) ({ \
	u64 val; \
	asm volatile ("mrs %0, " # x : "=r"(val)); \
	val; \
})

void fiq_debugger_dump_allregs(struct fiq_debugger_state *state,
		const struct pt_regs *regs)
{
	u32 pstate = READ_SPECIAL_REG(CurrentEl);
	bool in_el2 = (pstate & PSR_MODE_MASK) >= PSR_MODE_EL2t;

	fiq_debugger_dump_regs(state, regs);

	fiq_debugger_printf(state, " sp_el0   %016lx\n",
			READ_SPECIAL_REG(sp_el0));

	if (in_el2)
		fiq_debugger_printf(state, " sp_el1   %016lx\n",
				READ_SPECIAL_REG(sp_el1));

	fiq_debugger_printf(state, " elr_el1  %016lx\n",
			READ_SPECIAL_REG(elr_el1));

	fiq_debugger_printf(state, " spsr_el1 %08lx\n",
			READ_SPECIAL_REG(spsr_el1));

	if (in_el2) {
		fiq_debugger_printf(state, " spsr_irq %08lx\n",
				READ_SPECIAL_REG(spsr_irq));
		fiq_debugger_printf(state, " spsr_abt %08lx\n",
				READ_SPECIAL_REG(spsr_abt));
		fiq_debugger_printf(state, " spsr_und %08lx\n",
				READ_SPECIAL_REG(spsr_und));
		fiq_debugger_printf(state, " spsr_fiq %08lx\n",
				READ_SPECIAL_REG(spsr_fiq));
		fiq_debugger_printf(state, " spsr_el2 %08lx\n",
				READ_SPECIAL_REG(elr_el2));
		fiq_debugger_printf(state, " spsr_el2 %08lx\n",
				READ_SPECIAL_REG(spsr_el2));
	}
}

struct stacktrace_state {
	struct fiq_debugger_state *state;
	unsigned int depth;
};

static int report_trace(struct stackframe *frame, void *d)
{
	struct stacktrace_state *sts = d;

	if (sts->depth) {
		fiq_debugger_printf(sts->state, "%pF:\n", frame->pc);
		fiq_debugger_printf(sts->state,
				"  pc %016lx   sp %016lx   fp %016lx\n",
				frame->pc, frame->sp, frame->fp);
		sts->depth--;
		return 0;
	}
	fiq_debugger_printf(sts->state, "  ...\n");

	return sts->depth == 0;
}

void fiq_debugger_dump_stacktrace(struct fiq_debugger_state *state,
		const struct pt_regs *regs, unsigned int depth, void *ssp)
{
	struct thread_info *real_thread_info = THREAD_INFO(ssp);
	struct stacktrace_state sts;

	sts.depth = depth;
	sts.state = state;
	*current_thread_info() = *real_thread_info;

	if (!current)
		fiq_debugger_printf(state, "current NULL\n");
	else
		fiq_debugger_printf(state, "pid: %d  comm: %s\n",
			current->pid, current->comm);
	fiq_debugger_dump_regs(state, regs);

	if (!user_mode(regs)) {
		struct stackframe frame;
		frame.fp = regs->regs[29];
		frame.sp = regs->sp;
		frame.pc = regs->pc;
		fiq_debugger_printf(state, "\n");
		walk_stackframe(&frame, report_trace, &sts);
	}
}
