// SPDX-License-Identifier: GPL-2.0
/*
 * arch/parisc/kernel/kprobes.c
 *
 * PA-RISC kprobes implementation
 *
 * Copyright (c) 2019 Sven Schnelle <svens@stackframe.org>
 */

#include <linux/types.h>
#include <linux/kprobes.h>
#include <linux/slab.h>
#include <asm/cacheflush.h>
#include <asm/patch.h>

DEFINE_PER_CPU(struct kprobe *, current_kprobe) = NULL;
DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

int __kprobes arch_prepare_kprobe(struct kprobe *p)
{
	if ((unsigned long)p->addr & 3UL)
		return -EINVAL;

	p->ainsn.insn = get_insn_slot();
	if (!p->ainsn.insn)
		return -ENOMEM;

	memcpy(p->ainsn.insn, p->addr,
		MAX_INSN_SIZE * sizeof(kprobe_opcode_t));
	p->opcode = *p->addr;
	flush_insn_slot(p);
	return 0;
}

void __kprobes arch_remove_kprobe(struct kprobe *p)
{
	if (!p->ainsn.insn)
		return;

	free_insn_slot(p->ainsn.insn, 0);
	p->ainsn.insn = NULL;
}

void __kprobes arch_arm_kprobe(struct kprobe *p)
{
	patch_text(p->addr, PARISC_KPROBES_BREAK_INSN);
}

void __kprobes arch_disarm_kprobe(struct kprobe *p)
{
	patch_text(p->addr, p->opcode);
}

static void __kprobes save_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	kcb->prev_kprobe.kp = kprobe_running();
	kcb->prev_kprobe.status = kcb->kprobe_status;
}

static void __kprobes restore_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	__this_cpu_write(current_kprobe, kcb->prev_kprobe.kp);
	kcb->kprobe_status = kcb->prev_kprobe.status;
}

static inline void __kprobes set_current_kprobe(struct kprobe *p)
{
	__this_cpu_write(current_kprobe, p);
}

static void __kprobes setup_singlestep(struct kprobe *p,
		struct kprobe_ctlblk *kcb, struct pt_regs *regs)
{
	kcb->iaoq[0] = regs->iaoq[0];
	kcb->iaoq[1] = regs->iaoq[1];
	regs->iaoq[0] = (unsigned long)p->ainsn.insn;
	mtctl(0, 0);
	regs->gr[0] |= PSW_R;
}

int __kprobes parisc_kprobe_break_handler(struct pt_regs *regs)
{
	struct kprobe *p;
	struct kprobe_ctlblk *kcb;

	preempt_disable();

	kcb = get_kprobe_ctlblk();
	p = get_kprobe((unsigned long *)regs->iaoq[0]);

	if (!p) {
		preempt_enable_no_resched();
		return 0;
	}

	if (kprobe_running()) {
		/*
		 * We have reentered the kprobe_handler, since another kprobe
		 * was hit while within the handler, we save the original
		 * kprobes and single step on the instruction of the new probe
		 * without calling any user handlers to avoid recursive
		 * kprobes.
		 */
		save_previous_kprobe(kcb);
		set_current_kprobe(p);
		kprobes_inc_nmissed_count(p);
		setup_singlestep(p, kcb, regs);
		kcb->kprobe_status = KPROBE_REENTER;
		return 1;
	}

	set_current_kprobe(p);
	kcb->kprobe_status = KPROBE_HIT_ACTIVE;

	/* If we have no pre-handler or it returned 0, we continue with
	 * normal processing. If we have a pre-handler and it returned
	 * non-zero - which means user handler setup registers to exit
	 * to another instruction, we must skip the single stepping.
	 */

	if (!p->pre_handler || !p->pre_handler(p, regs)) {
		setup_singlestep(p, kcb, regs);
		kcb->kprobe_status = KPROBE_HIT_SS;
	} else {
		reset_current_kprobe();
		preempt_enable_no_resched();
	}
	return 1;
}

int __kprobes parisc_kprobe_ss_handler(struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	struct kprobe *p = kprobe_running();

	if (!p)
		return 0;

	if (regs->iaoq[0] != (unsigned long)p->ainsn.insn+4)
		return 0;

	/* restore back original saved kprobe variables and continue */
	if (kcb->kprobe_status == KPROBE_REENTER) {
		restore_previous_kprobe(kcb);
		return 1;
	}

	/* for absolute branch instructions we can copy iaoq_b. for relative
	 * branch instructions we need to calculate the new address based on the
	 * difference between iaoq_f and iaoq_b. We cannot use iaoq_b without
	 * modificationt because it's based on our ainsn.insn address.
	 */

	if (p->post_handler)
		p->post_handler(p, regs, 0);

	switch (regs->iir >> 26) {
	case 0x38: /* BE */
	case 0x39: /* BE,L */
	case 0x3a: /* BV */
	case 0x3b: /* BVE */
		/* for absolute branches, regs->iaoq[1] has already the right
		 * address
		 */
		regs->iaoq[0] = kcb->iaoq[1];
		break;
	default:
		regs->iaoq[1] = kcb->iaoq[0];
		regs->iaoq[1] += (regs->iaoq[1] - regs->iaoq[0]) + 4;
		regs->iaoq[0] = kcb->iaoq[1];
		break;
	}
	kcb->kprobe_status = KPROBE_HIT_SSDONE;
	reset_current_kprobe();
	return 1;
}

static inline void kretprobe_trampoline(void)
{
	asm volatile("nop");
	asm volatile("nop");
}

static int __kprobes trampoline_probe_handler(struct kprobe *p,
					      struct pt_regs *regs);

static struct kprobe trampoline_p = {
	.pre_handler = trampoline_probe_handler
};

static int __kprobes trampoline_probe_handler(struct kprobe *p,
					      struct pt_regs *regs)
{
	unsigned long orig_ret_address;

	orig_ret_address = __kretprobe_trampoline_handler(regs, trampoline_p.addr, NULL);
	instruction_pointer_set(regs, orig_ret_address);

	return 1;
}

void __kprobes arch_prepare_kretprobe(struct kretprobe_instance *ri,
				      struct pt_regs *regs)
{
	ri->ret_addr = (kprobe_opcode_t *)regs->gr[2];
	ri->fp = NULL;

	/* Replace the return addr with trampoline addr. */
	regs->gr[2] = (unsigned long)trampoline_p.addr;
}

int __kprobes arch_trampoline_kprobe(struct kprobe *p)
{
	return p->addr == trampoline_p.addr;
}

int __init arch_init_kprobes(void)
{
	trampoline_p.addr = (kprobe_opcode_t *)
		dereference_function_descriptor(kretprobe_trampoline);
	return register_kprobe(&trampoline_p);
}
