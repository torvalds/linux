// SPDX-License-Identifier: GPL-2.0

#include <linux/kprobes.h>

/* Ftrace callback handler for kprobes -- called under preepmt disabled */
void kprobe_ftrace_handler(unsigned long ip, unsigned long parent_ip,
			   struct ftrace_ops *ops, struct ftrace_regs *fregs)
{
	int bit;
	bool lr_saver = false;
	struct kprobe *p;
	struct kprobe_ctlblk *kcb;
	struct pt_regs *regs;

	if (unlikely(kprobe_ftrace_disabled))
		return;

	bit = ftrace_test_recursion_trylock(ip, parent_ip);
	if (bit < 0)
		return;

	regs = ftrace_get_regs(fregs);
	p = get_kprobe((kprobe_opcode_t *)ip);
	if (!p) {
		p = get_kprobe((kprobe_opcode_t *)(ip - MCOUNT_INSN_SIZE));
		if (unlikely(!p) || kprobe_disabled(p))
			goto out;
		lr_saver = true;
	}

	kcb = get_kprobe_ctlblk();
	if (kprobe_running()) {
		kprobes_inc_nmissed_count(p);
	} else {
		unsigned long orig_ip = instruction_pointer(regs);

		if (lr_saver)
			ip -= MCOUNT_INSN_SIZE;
		instruction_pointer_set(regs, ip);
		__this_cpu_write(current_kprobe, p);
		kcb->kprobe_status = KPROBE_HIT_ACTIVE;
		if (!p->pre_handler || !p->pre_handler(p, regs)) {
			/*
			 * Emulate singlestep (and also recover regs->pc)
			 * as if there is a nop
			 */
			instruction_pointer_set(regs,
				(unsigned long)p->addr + MCOUNT_INSN_SIZE);
			if (unlikely(p->post_handler)) {
				kcb->kprobe_status = KPROBE_HIT_SSDONE;
				p->post_handler(p, regs, 0);
			}
			instruction_pointer_set(regs, orig_ip);
		}
		/*
		 * If pre_handler returns !0, it changes regs->pc. We have to
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
	p->ainsn.api.insn = NULL;
	return 0;
}
