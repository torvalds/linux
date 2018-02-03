/*
 * Dynamic Ftrace based Kprobes Optimization
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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

static nokprobe_inline
int __skip_singlestep(struct kprobe *p, struct pt_regs *regs,
		      struct kprobe_ctlblk *kcb, unsigned long orig_nip)
{
	/*
	 * Emulate singlestep (and also recover regs->nip)
	 * as if there is a nop
	 */
	regs->nip = (unsigned long)p->addr + MCOUNT_INSN_SIZE;
	if (unlikely(p->post_handler)) {
		kcb->kprobe_status = KPROBE_HIT_SSDONE;
		p->post_handler(p, regs, 0);
	}
	__this_cpu_write(current_kprobe, NULL);
	if (orig_nip)
		regs->nip = orig_nip;
	return 1;
}

int skip_singlestep(struct kprobe *p, struct pt_regs *regs,
		    struct kprobe_ctlblk *kcb)
{
	if (kprobe_ftrace(p))
		return __skip_singlestep(p, regs, kcb, 0);
	else
		return 0;
}
NOKPROBE_SYMBOL(skip_singlestep);

/* Ftrace callback handler for kprobes */
void kprobe_ftrace_handler(unsigned long nip, unsigned long parent_nip,
			   struct ftrace_ops *ops, struct pt_regs *regs)
{
	struct kprobe *p;
	struct kprobe_ctlblk *kcb;
	unsigned long flags;

	/* Disable irq for emulating a breakpoint and avoiding preempt */
	local_irq_save(flags);
	hard_irq_disable();
	preempt_disable();

	p = get_kprobe((kprobe_opcode_t *)nip);
	if (unlikely(!p) || kprobe_disabled(p))
		goto end;

	kcb = get_kprobe_ctlblk();
	if (kprobe_running()) {
		kprobes_inc_nmissed_count(p);
	} else {
		unsigned long orig_nip = regs->nip;

		/*
		 * On powerpc, NIP is *before* this instruction for the
		 * pre handler
		 */
		regs->nip -= MCOUNT_INSN_SIZE;

		__this_cpu_write(current_kprobe, p);
		kcb->kprobe_status = KPROBE_HIT_ACTIVE;
		if (!p->pre_handler || !p->pre_handler(p, regs))
			__skip_singlestep(p, regs, kcb, orig_nip);
		else {
			/*
			 * If pre_handler returns !0, it sets regs->nip and
			 * resets current kprobe. In this case, we still need
			 * to restore irq, but not preemption.
			 */
			local_irq_restore(flags);
			return;
		}
	}
end:
	preempt_enable_no_resched();
	local_irq_restore(flags);
}
NOKPROBE_SYMBOL(kprobe_ftrace_handler);

int arch_prepare_kprobe_ftrace(struct kprobe *p)
{
	p->ainsn.insn = NULL;
	p->ainsn.boostable = -1;
	return 0;
}
