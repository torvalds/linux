// SPDX-License-Identifier: GPL-2.0+

#include <linux/kprobes.h>
#include <linux/extable.h>
#include <linux/slab.h>
#include <linux/stop_machine.h>
#include <asm/ptrace.h>
#include <linux/uaccess.h>
#include <asm/sections.h>
#include <asm/cacheflush.h>
#include <asm/bug.h>
#include <asm/patch.h>

#include "decode-insn.h"

DEFINE_PER_CPU(struct kprobe *, current_kprobe) = NULL;
DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

static void __kprobes
post_kprobe_handler(struct kprobe *, struct kprobe_ctlblk *, struct pt_regs *);

static void __kprobes arch_prepare_ss_slot(struct kprobe *p)
{
	unsigned long offset = GET_INSN_LENGTH(p->opcode);

	p->ainsn.api.restore = (unsigned long)p->addr + offset;

	patch_text(p->ainsn.api.insn, p->opcode);
	patch_text((void *)((unsigned long)(p->ainsn.api.insn) + offset),
		   __BUG_INSN_32);
}

static void __kprobes arch_prepare_simulate(struct kprobe *p)
{
	p->ainsn.api.restore = 0;
}

static void __kprobes arch_simulate_insn(struct kprobe *p, struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	if (p->ainsn.api.handler)
		p->ainsn.api.handler((u32)p->opcode,
					(unsigned long)p->addr, regs);

	post_kprobe_handler(p, kcb, regs);
}

int __kprobes arch_prepare_kprobe(struct kprobe *p)
{
	unsigned long probe_addr = (unsigned long)p->addr;

	if (probe_addr & 0x1) {
		pr_warn("Address not aligned.\n");

		return -EINVAL;
	}

	/* copy instruction */
	p->opcode = *p->addr;

	/* decode instruction */
	switch (riscv_probe_decode_insn(p->addr, &p->ainsn.api)) {
	case INSN_REJECTED:	/* insn not supported */
		return -EINVAL;

	case INSN_GOOD_NO_SLOT:	/* insn need simulation */
		p->ainsn.api.insn = NULL;
		break;

	case INSN_GOOD:	/* instruction uses slot */
		p->ainsn.api.insn = get_insn_slot();
		if (!p->ainsn.api.insn)
			return -ENOMEM;
		break;
	}

	/* prepare the instruction */
	if (p->ainsn.api.insn)
		arch_prepare_ss_slot(p);
	else
		arch_prepare_simulate(p);

	return 0;
}

#ifdef CONFIG_MMU
void *alloc_insn_page(void)
{
	return  __vmalloc_node_range(PAGE_SIZE, 1, VMALLOC_START, VMALLOC_END,
				     GFP_KERNEL, PAGE_KERNEL_READ_EXEC,
				     VM_FLUSH_RESET_PERMS, NUMA_NO_NODE,
				     __builtin_return_address(0));
}
#endif

/* install breakpoint in text */
void __kprobes arch_arm_kprobe(struct kprobe *p)
{
	if ((p->opcode & __INSN_LENGTH_MASK) == __INSN_LENGTH_32)
		patch_text(p->addr, __BUG_INSN_32);
	else
		patch_text(p->addr, __BUG_INSN_16);
}

/* remove breakpoint from text */
void __kprobes arch_disarm_kprobe(struct kprobe *p)
{
	patch_text(p->addr, p->opcode);
}

void __kprobes arch_remove_kprobe(struct kprobe *p)
{
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

static void __kprobes set_current_kprobe(struct kprobe *p)
{
	__this_cpu_write(current_kprobe, p);
}

/*
 * Interrupts need to be disabled before single-step mode is set, and not
 * reenabled until after single-step mode ends.
 * Without disabling interrupt on local CPU, there is a chance of
 * interrupt occurrence in the period of exception return and  start of
 * out-of-line single-step, that result in wrongly single stepping
 * into the interrupt handler.
 */
static void __kprobes kprobes_save_local_irqflag(struct kprobe_ctlblk *kcb,
						struct pt_regs *regs)
{
	kcb->saved_status = regs->status;
	regs->status &= ~SR_SPIE;
}

static void __kprobes kprobes_restore_local_irqflag(struct kprobe_ctlblk *kcb,
						struct pt_regs *regs)
{
	regs->status = kcb->saved_status;
}

static void __kprobes setup_singlestep(struct kprobe *p,
				       struct pt_regs *regs,
				       struct kprobe_ctlblk *kcb, int reenter)
{
	unsigned long slot;

	if (reenter) {
		save_previous_kprobe(kcb);
		set_current_kprobe(p);
		kcb->kprobe_status = KPROBE_REENTER;
	} else {
		kcb->kprobe_status = KPROBE_HIT_SS;
	}

	if (p->ainsn.api.insn) {
		/* prepare for single stepping */
		slot = (unsigned long)p->ainsn.api.insn;

		/* IRQs and single stepping do not mix well. */
		kprobes_save_local_irqflag(kcb, regs);

		instruction_pointer_set(regs, slot);
	} else {
		/* insn simulation */
		arch_simulate_insn(p, regs);
	}
}

static int __kprobes reenter_kprobe(struct kprobe *p,
				    struct pt_regs *regs,
				    struct kprobe_ctlblk *kcb)
{
	switch (kcb->kprobe_status) {
	case KPROBE_HIT_SSDONE:
	case KPROBE_HIT_ACTIVE:
		kprobes_inc_nmissed_count(p);
		setup_singlestep(p, regs, kcb, 1);
		break;
	case KPROBE_HIT_SS:
	case KPROBE_REENTER:
		pr_warn("Unrecoverable kprobe detected.\n");
		dump_kprobe(p);
		BUG();
		break;
	default:
		WARN_ON(1);
		return 0;
	}

	return 1;
}

static void __kprobes
post_kprobe_handler(struct kprobe *cur, struct kprobe_ctlblk *kcb, struct pt_regs *regs)
{
	/* return addr restore if non-branching insn */
	if (cur->ainsn.api.restore != 0)
		regs->epc = cur->ainsn.api.restore;

	/* restore back original saved kprobe variables and continue */
	if (kcb->kprobe_status == KPROBE_REENTER) {
		restore_previous_kprobe(kcb);
		return;
	}

	/* call post handler */
	kcb->kprobe_status = KPROBE_HIT_SSDONE;
	if (cur->post_handler)	{
		/* post_handler can hit breakpoint and single step
		 * again, so we enable D-flag for recursive exception.
		 */
		cur->post_handler(cur, regs, 0);
	}

	reset_current_kprobe();
}

int __kprobes kprobe_fault_handler(struct pt_regs *regs, unsigned int trapnr)
{
	struct kprobe *cur = kprobe_running();
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	switch (kcb->kprobe_status) {
	case KPROBE_HIT_SS:
	case KPROBE_REENTER:
		/*
		 * We are here because the instruction being single
		 * stepped caused a page fault. We reset the current
		 * kprobe and the ip points back to the probe address
		 * and allow the page fault handler to continue as a
		 * normal page fault.
		 */
		regs->epc = (unsigned long) cur->addr;
		BUG_ON(!instruction_pointer(regs));

		if (kcb->kprobe_status == KPROBE_REENTER)
			restore_previous_kprobe(kcb);
		else {
			kprobes_restore_local_irqflag(kcb, regs);
			reset_current_kprobe();
		}

		break;
	case KPROBE_HIT_ACTIVE:
	case KPROBE_HIT_SSDONE:
		/*
		 * In case the user-specified fault handler returned
		 * zero, try to fix up.
		 */
		if (fixup_exception(regs))
			return 1;
	}
	return 0;
}

bool __kprobes
kprobe_breakpoint_handler(struct pt_regs *regs)
{
	struct kprobe *p, *cur_kprobe;
	struct kprobe_ctlblk *kcb;
	unsigned long addr = instruction_pointer(regs);

	kcb = get_kprobe_ctlblk();
	cur_kprobe = kprobe_running();

	p = get_kprobe((kprobe_opcode_t *) addr);

	if (p) {
		if (cur_kprobe) {
			if (reenter_kprobe(p, regs, kcb))
				return true;
		} else {
			/* Probe hit */
			set_current_kprobe(p);
			kcb->kprobe_status = KPROBE_HIT_ACTIVE;

			/*
			 * If we have no pre-handler or it returned 0, we
			 * continue with normal processing.  If we have a
			 * pre-handler and it returned non-zero, it will
			 * modify the execution path and no need to single
			 * stepping. Let's just reset current kprobe and exit.
			 *
			 * pre_handler can hit a breakpoint and can step thru
			 * before return.
			 */
			if (!p->pre_handler || !p->pre_handler(p, regs))
				setup_singlestep(p, regs, kcb, 0);
			else
				reset_current_kprobe();
		}
		return true;
	}

	/*
	 * The breakpoint instruction was removed right
	 * after we hit it.  Another cpu has removed
	 * either a probepoint or a debugger breakpoint
	 * at this address.  In either case, no further
	 * handling of this interrupt is appropriate.
	 * Return back to original instruction, and continue.
	 */
	return false;
}

bool __kprobes
kprobe_single_step_handler(struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	unsigned long addr = instruction_pointer(regs);
	struct kprobe *cur = kprobe_running();

	if (cur && (kcb->kprobe_status & (KPROBE_HIT_SS | KPROBE_REENTER)) &&
	    ((unsigned long)&cur->ainsn.api.insn[0] + GET_INSN_LENGTH(cur->opcode) == addr)) {
		kprobes_restore_local_irqflag(kcb, regs);
		post_kprobe_handler(cur, kcb, regs);
		return true;
	}
	/* not ours, kprobes should ignore it */
	return false;
}

/*
 * Provide a blacklist of symbols identifying ranges which cannot be kprobed.
 * This blacklist is exposed to userspace via debugfs (kprobes/blacklist).
 */
int __init arch_populate_kprobe_blacklist(void)
{
	int ret;

	ret = kprobe_add_area_blacklist((unsigned long)__irqentry_text_start,
					(unsigned long)__irqentry_text_end);
	return ret;
}

void __kprobes __used *trampoline_probe_handler(struct pt_regs *regs)
{
	return (void *)kretprobe_trampoline_handler(regs, &kretprobe_trampoline, NULL);
}

void __kprobes arch_prepare_kretprobe(struct kretprobe_instance *ri,
				      struct pt_regs *regs)
{
	ri->ret_addr = (kprobe_opcode_t *)regs->ra;
	ri->fp = NULL;
	regs->ra = (unsigned long) &kretprobe_trampoline;
}

int __kprobes arch_trampoline_kprobe(struct kprobe *p)
{
	return 0;
}

int __init arch_init_kprobes(void)
{
	return 0;
}
