// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Dynamic Ftrace based Kprobes Optimization
 *
 * Copyright (C) Hitachi Ltd., 2012
 */
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/hardirq.h>
#include <linux/preempt.h>
#include <linux/ftrace.h>
#include <asm/text-patching.h>

#include "common.h"

/* Ftrace callback handler for kprobes -- called under preempt disabled */
void kprobe_ftrace_handler(unsigned long ip, unsigned long parent_ip,
			   struct ftrace_ops *ops, struct ftrace_regs *fregs)
{
	struct pt_regs *regs = ftrace_get_regs(fregs);
	struct kprobe *p;
	struct kprobe_ctlblk *kcb;
	int bit;

	if (unlikely(kprobe_ftrace_disabled))
		return;

	bit = ftrace_test_recursion_trylock(ip, parent_ip);
	if (bit < 0)
		return;

	p = get_kprobe((kprobe_opcode_t *)ip);
	if (unlikely(!p) || kprobe_disabled(p))
		goto out;

	kcb = get_kprobe_ctlblk();
	if (kprobe_running()) {
		kprobes_inc_nmissed_count(p);
	} else {
		unsigned long orig_ip = instruction_pointer(regs);

		/* Kprobe handler expects regs->ip = ip + 1 as breakpoint hit */
		instruction_pointer_set(regs, ip + INT3_INSN_SIZE);

		__this_cpu_write(current_kprobe, p);
		kcb->kprobe_status = KPROBE_HIT_ACTIVE;
		if (!p->pre_handler || !p->pre_handler(p, regs)) {
			if (unlikely(p->post_handler)) {
				/*
				 * Emulate singlestep (and also recover regs->ip)
				 * as if there is a 5byte nop
				 */
				instruction_pointer_set(regs, ip + MCOUNT_INSN_SIZE);
				kcb->kprobe_status = KPROBE_HIT_SSDONE;
				p->post_handler(p, regs, 0);
			}
			/* Recover IP address */
			instruction_pointer_set(regs, orig_ip);
		}
		/*
		 * If pre_handler returns !0, it changes regs->ip. We have to
		 * skip emulating post_handler.
		 */
		__this_cpu_write(current_kprobe, NULL);
	}
out:
	ftrace_test_recursion_unlock(bit);
}
NOKPROBE_SYMBOL(kprobe_ftrace_handler);

int arch_prepare_kprobe_ftrace(struct kprobe *p)
{
	p->ainsn.insn = NULL;
	p->ainsn.boostable = false;
	return 0;
}
