// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Performance counter callchain support - powerpc architecture code
 *
 * Copyright © 2009 Paul Mackerras, IBM Corporation.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/perf_event.h>
#include <linux/percpu.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <asm/ptrace.h>
#include <asm/sigcontext.h>
#include <asm/ucontext.h>
#include <asm/vdso.h>
#include <asm/pte-walk.h>

#include "callchain.h"

/*
 * Is sp valid as the address of the next kernel stack frame after prev_sp?
 * The next frame may be in a different stack area but should not go
 * back down in the same stack area.
 */
static int valid_next_sp(unsigned long sp, unsigned long prev_sp)
{
	if (sp & 0xf)
		return 0;		/* must be 16-byte aligned */
	if (!validate_sp(sp, current, STACK_FRAME_OVERHEAD))
		return 0;
	if (sp >= prev_sp + STACK_FRAME_MIN_SIZE)
		return 1;
	/*
	 * sp could decrease when we jump off an interrupt stack
	 * back to the regular process stack.
	 */
	if ((sp & ~(THREAD_SIZE - 1)) != (prev_sp & ~(THREAD_SIZE - 1)))
		return 1;
	return 0;
}

void __no_sanitize_address
perf_callchain_kernel(struct perf_callchain_entry_ctx *entry, struct pt_regs *regs)
{
	unsigned long sp, next_sp;
	unsigned long next_ip;
	unsigned long lr;
	long level = 0;
	unsigned long *fp;

	lr = regs->link;
	sp = regs->gpr[1];
	perf_callchain_store(entry, perf_instruction_pointer(regs));

	if (!validate_sp(sp, current, STACK_FRAME_OVERHEAD))
		return;

	for (;;) {
		fp = (unsigned long *) sp;
		next_sp = fp[0];

		if (next_sp == sp + STACK_INT_FRAME_SIZE &&
		    validate_sp(sp, current, STACK_INT_FRAME_SIZE) &&
		    fp[STACK_FRAME_MARKER] == STACK_FRAME_REGS_MARKER) {
			/*
			 * This looks like an interrupt frame for an
			 * interrupt that occurred in the kernel
			 */
			regs = (struct pt_regs *)(sp + STACK_FRAME_OVERHEAD);
			next_ip = regs->nip;
			lr = regs->link;
			level = 0;
			perf_callchain_store_context(entry, PERF_CONTEXT_KERNEL);

		} else {
			if (level == 0)
				next_ip = lr;
			else
				next_ip = fp[STACK_FRAME_LR_SAVE];

			/*
			 * We can't tell which of the first two addresses
			 * we get are valid, but we can filter out the
			 * obviously bogus ones here.  We replace them
			 * with 0 rather than removing them entirely so
			 * that userspace can tell which is which.
			 */
			if ((level == 1 && next_ip == lr) ||
			    (level <= 1 && !kernel_text_address(next_ip)))
				next_ip = 0;

			++level;
		}

		perf_callchain_store(entry, next_ip);
		if (!valid_next_sp(next_sp, sp))
			return;
		sp = next_sp;
	}
}

void
perf_callchain_user(struct perf_callchain_entry_ctx *entry, struct pt_regs *regs)
{
	if (!is_32bit_task())
		perf_callchain_user_64(entry, regs);
	else
		perf_callchain_user_32(entry, regs);
}
