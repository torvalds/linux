// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Dynamic Ftrace based Kprobes Optimization
 *
 * Copyright (C) Hitachi Ltd., 2012
 * Copyright 2016 Naveen N. Rao <naveen.n.rao@linux.vnet.ibm.com>
 *		  IBM Corporation
 */
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/hardirq.h>
#include <linux/preempt.h>
#include <linux/ftrace.h>

/* Ftrace callback handler for kprobes */
void kprobe_ftrace_handler(unsigned long nip, unsigned long parent_nip,
			   struct ftrace_ops *ops, struct ftrace_regs *fregs)
{
	struct kprobe *p;
	struct kprobe_ctlblk *kcb;
	struct pt_regs *regs;
	int bit;

	if (unlikely(kprobe_ftrace_disabled))
		return;

	bit = ftrace_test_recursion_trylock(nip, parent_nip);
	if (bit < 0)
		return;

	regs = ftrace_get_regs(fregs);
	p = get_kprobe((kprobe_opcode_t *)nip);
	if (unlikely(!p) || kprobe_disabled(p))
		goto out;

	kcb = get_kprobe_ctlblk();
	if (kprobe_running()) {
		kprobes_inc_nmissed_count(p);
	} else {
		/*
		 * On powerpc, NIP is *before* this instruction for the
		 * pre handler
		 */
		regs_add_return_ip(regs, -MCOUNT_INSN_SIZE);

		__this_cpu_write(current_kprobe, p);
		kcb->kprobe_status = KPROBE_HIT_ACTIVE;
		if (!p->pre_handler || !p->pre_handler(p, regs)) {
			/*
			 * Emulate singlestep (and also recover regs->nip)
			 * as if there is a nop
			 */
			regs_add_return_ip(regs, MCOUNT_INSN_SIZE);
			if (unlikely(p->post_handler)) {
				kcb->kprobe_status = KPROBE_HIT_SSDONE;
				p->post_handler(p, regs, 0);
			}
		}
		/*
		 * If pre_handler returns !0, it changes regs->nip. We have to
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
	p->ainsn.boostable = -1;
	return 0;
}
