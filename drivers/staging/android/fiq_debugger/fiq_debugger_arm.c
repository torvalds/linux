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
#include <linux/uaccess.h>

#include <asm/stacktrace.h>

#include "fiq_debugger_priv.h"

static char *mode_name(unsigned cpsr)
{
	switch (cpsr & MODE_MASK) {
	case USR_MODE: return "USR";
	case FIQ_MODE: return "FIQ";
	case IRQ_MODE: return "IRQ";
	case SVC_MODE: return "SVC";
	case ABT_MODE: return "ABT";
	case UND_MODE: return "UND";
	case SYSTEM_MODE: return "SYS";
	default: return "???";
	}
}

void fiq_debugger_dump_pc(struct fiq_debugger_output *output,
		const struct pt_regs *regs)
{
	output->printf(output, " pc %08x cpsr %08x mode %s\n",
		regs->ARM_pc, regs->ARM_cpsr, mode_name(regs->ARM_cpsr));
}

void fiq_debugger_dump_regs(struct fiq_debugger_output *output,
		const struct pt_regs *regs)
{
	output->printf(output,
			" r0 %08x  r1 %08x  r2 %08x  r3 %08x\n",
			regs->ARM_r0, regs->ARM_r1, regs->ARM_r2, regs->ARM_r3);
	output->printf(output,
			" r4 %08x  r5 %08x  r6 %08x  r7 %08x\n",
			regs->ARM_r4, regs->ARM_r5, regs->ARM_r6, regs->ARM_r7);
	output->printf(output,
			" r8 %08x  r9 %08x r10 %08x r11 %08x  mode %s\n",
			regs->ARM_r8, regs->ARM_r9, regs->ARM_r10, regs->ARM_fp,
			mode_name(regs->ARM_cpsr));
	output->printf(output,
			" ip %08x  sp %08x  lr %08x  pc %08x cpsr %08x\n",
			regs->ARM_ip, regs->ARM_sp, regs->ARM_lr, regs->ARM_pc,
			regs->ARM_cpsr);
}

struct mode_regs {
	unsigned long sp_svc;
	unsigned long lr_svc;
	unsigned long spsr_svc;

	unsigned long sp_abt;
	unsigned long lr_abt;
	unsigned long spsr_abt;

	unsigned long sp_und;
	unsigned long lr_und;
	unsigned long spsr_und;

	unsigned long sp_irq;
	unsigned long lr_irq;
	unsigned long spsr_irq;

	unsigned long r8_fiq;
	unsigned long r9_fiq;
	unsigned long r10_fiq;
	unsigned long r11_fiq;
	unsigned long r12_fiq;
	unsigned long sp_fiq;
	unsigned long lr_fiq;
	unsigned long spsr_fiq;
};

static void __naked get_mode_regs(struct mode_regs *regs)
{
	asm volatile (
	"mrs	r1, cpsr\n"
#ifdef CONFIG_THUMB2_KERNEL
	"mov	r3, #0xd3 @(SVC_MODE | PSR_I_BIT | PSR_F_BIT)\n"
	"msr	cpsr_c, r3\n"
	"str	r13, [r0], 4\n"
	"str	r14, [r0], 4\n"
	"mrs	r2, spsr\n"
	"mov	r3, #0xd7 @(ABT_MODE | PSR_I_BIT | PSR_F_BIT)\n"
	"msr	cpsr_c, r3\n"
	"str	r2, [r0], 4\n"
	"str	r13, [r0], 4\n"
	"str	r14, [r0], 4\n"
	"mrs	r2, spsr\n"
	"mov	r3, #0xdb @(UND_MODE | PSR_I_BIT | PSR_F_BIT)\n"
	"msr	cpsr_c, r3\n"
	"str	r2, [r0], 4\n"
	"str	r13, [r0], 4\n"
	"str	r14, [r0], 4\n"
	"mrs	r2, spsr\n"
	"mov	r3, #0xd2 @(IRQ_MODE | PSR_I_BIT | PSR_F_BIT)\n"
	"msr	cpsr_c, r3\n"
	"str	r2, [r0], 4\n"
	"str	r13, [r0], 4\n"
	"str	r14, [r0], 4\n"
	"mrs	r2, spsr\n"
	"mov	r3, #0xd1 @(FIQ_MODE | PSR_I_BIT | PSR_F_BIT)\n"
	"msr	cpsr_c, r3\n"
	"stmia	r0!, {r2, r8 - r12}\n"
	"str	r13, [r0], 4\n"
	"str	r14, [r0], 4\n"
#else
	"msr	cpsr_c, #0xd3 @(SVC_MODE | PSR_I_BIT | PSR_F_BIT)\n"
	"stmia	r0!, {r13 - r14}\n"
	"mrs	r2, spsr\n"
	"msr	cpsr_c, #0xd7 @(ABT_MODE | PSR_I_BIT | PSR_F_BIT)\n"
	"stmia	r0!, {r2, r13 - r14}\n"
	"mrs	r2, spsr\n"
	"msr	cpsr_c, #0xdb @(UND_MODE | PSR_I_BIT | PSR_F_BIT)\n"
	"stmia	r0!, {r2, r13 - r14}\n"
	"mrs	r2, spsr\n"
	"msr	cpsr_c, #0xd2 @(IRQ_MODE | PSR_I_BIT | PSR_F_BIT)\n"
	"stmia	r0!, {r2, r13 - r14}\n"
	"mrs	r2, spsr\n"
	"msr	cpsr_c, #0xd1 @(FIQ_MODE | PSR_I_BIT | PSR_F_BIT)\n"
	"stmia	r0!, {r2, r8 - r14}\n"
#endif
	"mrs	r2, spsr\n"
	"stmia	r0!, {r2}\n"
	"msr	cpsr_c, r1\n"
	"bx	lr\n");
}


void fiq_debugger_dump_allregs(struct fiq_debugger_output *output,
		const struct pt_regs *regs)
{
	struct mode_regs mode_regs;
	unsigned long mode = regs->ARM_cpsr & MODE_MASK;

	fiq_debugger_dump_regs(output, regs);
	get_mode_regs(&mode_regs);

	output->printf(output,
			"%csvc: sp %08x  lr %08x  spsr %08x\n",
			mode == SVC_MODE ? '*' : ' ',
			mode_regs.sp_svc, mode_regs.lr_svc, mode_regs.spsr_svc);
	output->printf(output,
			"%cabt: sp %08x  lr %08x  spsr %08x\n",
			mode == ABT_MODE ? '*' : ' ',
			mode_regs.sp_abt, mode_regs.lr_abt, mode_regs.spsr_abt);
	output->printf(output,
			"%cund: sp %08x  lr %08x  spsr %08x\n",
			mode == UND_MODE ? '*' : ' ',
			mode_regs.sp_und, mode_regs.lr_und, mode_regs.spsr_und);
	output->printf(output,
			"%cirq: sp %08x  lr %08x  spsr %08x\n",
			mode == IRQ_MODE ? '*' : ' ',
			mode_regs.sp_irq, mode_regs.lr_irq, mode_regs.spsr_irq);
	output->printf(output,
			"%cfiq: r8 %08x  r9 %08x  r10 %08x  r11 %08x  r12 %08x\n",
			mode == FIQ_MODE ? '*' : ' ',
			mode_regs.r8_fiq, mode_regs.r9_fiq, mode_regs.r10_fiq,
			mode_regs.r11_fiq, mode_regs.r12_fiq);
	output->printf(output,
			" fiq: sp %08x  lr %08x  spsr %08x\n",
			mode_regs.sp_fiq, mode_regs.lr_fiq, mode_regs.spsr_fiq);
}

struct stacktrace_state {
	struct fiq_debugger_output *output;
	unsigned int depth;
};

static int report_trace(struct stackframe *frame, void *d)
{
	struct stacktrace_state *sts = d;

	if (sts->depth) {
		sts->output->printf(sts->output,
			"  pc: %p (%pF), lr %p (%pF), sp %p, fp %p\n",
			frame->pc, frame->pc, frame->lr, frame->lr,
			frame->sp, frame->fp);
		sts->depth--;
		return 0;
	}
	sts->output->printf(sts->output, "  ...\n");

	return sts->depth == 0;
}

struct frame_tail {
	struct frame_tail *fp;
	unsigned long sp;
	unsigned long lr;
} __attribute__((packed));

static struct frame_tail *user_backtrace(struct fiq_debugger_output *output,
					struct frame_tail *tail)
{
	struct frame_tail buftail[2];

	/* Also check accessibility of one struct frame_tail beyond */
	if (!access_ok(VERIFY_READ, tail, sizeof(buftail))) {
		output->printf(output, "  invalid frame pointer %p\n",
				tail);
		return NULL;
	}
	if (__copy_from_user_inatomic(buftail, tail, sizeof(buftail))) {
		output->printf(output,
			"  failed to copy frame pointer %p\n", tail);
		return NULL;
	}

	output->printf(output, "  %p\n", buftail[0].lr);

	/* frame pointers should strictly progress back up the stack
	 * (towards higher addresses) */
	if (tail >= buftail[0].fp)
		return NULL;

	return buftail[0].fp-1;
}

void fiq_debugger_dump_stacktrace(struct fiq_debugger_output *output,
		const struct pt_regs *regs, unsigned int depth, void *ssp)
{
	struct frame_tail *tail;
	struct thread_info *real_thread_info = THREAD_INFO(ssp);
	struct stacktrace_state sts;

	sts.depth = depth;
	sts.output = output;
	*current_thread_info() = *real_thread_info;

	if (!current)
		output->printf(output, "current NULL\n");
	else
		output->printf(output, "pid: %d  comm: %s\n",
			current->pid, current->comm);
	fiq_debugger_dump_regs(output, regs);

	if (!user_mode(regs)) {
		struct stackframe frame;
		frame.fp = regs->ARM_fp;
		frame.sp = regs->ARM_sp;
		frame.lr = regs->ARM_lr;
		frame.pc = regs->ARM_pc;
		output->printf(output,
			"  pc: %p (%pF), lr %p (%pF), sp %p, fp %p\n",
			regs->ARM_pc, regs->ARM_pc, regs->ARM_lr, regs->ARM_lr,
			regs->ARM_sp, regs->ARM_fp);
		walk_stackframe(&frame, report_trace, &sts);
		return;
	}

	tail = ((struct frame_tail *) regs->ARM_fp) - 1;
	while (depth-- && tail && !((unsigned long) tail & 3))
		tail = user_backtrace(output, tail);
}
