// SPDX-License-Identifier: GPL-2.0

#include <linux/kprobes.h>

/* Ftrace callback handler for kprobes -- called under preepmt disabed */
void kprobe_ftrace_handler(unsigned long ip, unsigned long parent_ip,
			   struct ftrace_ops *ops, struct ftrace_regs *regs)
{
	struct kprobe *p;
	struct kprobe_ctlblk *kcb;

	p = get_kprobe((kprobe_opcode_t *)ip);
	if (unlikely(!p) || kprobe_disabled(p))
		return;

	kcb = get_kprobe_ctlblk();
	if (kprobe_running()) {
		kprobes_inc_nmissed_count(p);
	} else {
		unsigned long orig_ip = instruction_pointer(&(regs->regs));

		instruction_pointer_set(&(regs->regs), ip);

		__this_cpu_write(current_kprobe, p);
		kcb->kprobe_status = KPROBE_HIT_ACTIVE;
		if (!p->pre_handler || !p->pre_handler(p, &(regs->regs))) {
			/*
			 * Emulate singlestep (and also recover regs->pc)
			 * as if there is a nop
			 */
			instruction_pointer_set(&(regs->regs),
				(unsigned long)p->addr + MCOUNT_INSN_SIZE);
			if (unlikely(p->post_handler)) {
				kcb->kprobe_status = KPROBE_HIT_SSDONE;
				p->post_handler(p, &(regs->regs), 0);
			}
			instruction_pointer_set(&(regs->regs), orig_ip);
		}

		/*
		 * If pre_handler returns !0, it changes regs->pc. We have to
		 * skip emulating post_handler.
		 */
		__this_cpu_write(current_kprobe, NULL);
	}
}
NOKPROBE_SYMBOL(kprobe_ftrace_handler);

int arch_prepare_kprobe_ftrace(struct kprobe *p)
{
	p->ainsn.api.insn = NULL;
	return 0;
}
