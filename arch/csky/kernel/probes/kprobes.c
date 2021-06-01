// SPDX-License-Identifier: GPL-2.0+

#include <linux/kprobes.h>
#include <linux/extable.h>
#include <linux/slab.h>
#include <linux/stop_machine.h>
#include <asm/ptrace.h>
#include <linux/uaccess.h>
#include <asm/sections.h>
#include <asm/cacheflush.h>

#include "decode-insn.h"

DEFINE_PER_CPU(struct kprobe *, current_kprobe) = NULL;
DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

static void __kprobes
post_kprobe_handler(struct kprobe_ctlblk *, struct pt_regs *);

struct csky_insn_patch {
	kprobe_opcode_t	*addr;
	u32		opcode;
	atomic_t	cpu_count;
};

static int __kprobes patch_text_cb(void *priv)
{
	struct csky_insn_patch *param = priv;
	unsigned int addr = (unsigned int)param->addr;

	if (atomic_inc_return(&param->cpu_count) == 1) {
		*(u16 *) addr = cpu_to_le16(param->opcode);
		dcache_wb_range(addr, addr + 2);
		atomic_inc(&param->cpu_count);
	} else {
		while (atomic_read(&param->cpu_count) <= num_online_cpus())
			cpu_relax();
	}

	icache_inv_range(addr, addr + 2);

	return 0;
}

static int __kprobes patch_text(kprobe_opcode_t *addr, u32 opcode)
{
	struct csky_insn_patch param = { addr, opcode, ATOMIC_INIT(0) };

	return stop_machine_cpuslocked(patch_text_cb, &param, cpu_online_mask);
}

static void __kprobes arch_prepare_ss_slot(struct kprobe *p)
{
	unsigned long offset = is_insn32(p->opcode) ? 4 : 2;

	p->ainsn.api.restore = (unsigned long)p->addr + offset;

	patch_text(p->ainsn.api.insn, p->opcode);
}

static void __kprobes arch_prepare_simulate(struct kprobe *p)
{
	p->ainsn.api.restore = 0;
}

static void __kprobes arch_simulate_insn(struct kprobe *p, struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	if (p->ainsn.api.handler)
		p->ainsn.api.handler((u32)p->opcode, (long)p->addr, regs);

	post_kprobe_handler(kcb, regs);
}

int __kprobes arch_prepare_kprobe(struct kprobe *p)
{
	unsigned long probe_addr = (unsigned long)p->addr;

	if (probe_addr & 0x1) {
		pr_warn("Address not aligned.\n");
		return -EINVAL;
	}

	/* copy instruction */
	p->opcode = le32_to_cpu(*p->addr);

	/* decode instruction */
	switch (csky_probe_decode_insn(p->addr, &p->ainsn.api)) {
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

/* install breakpoint in text */
void __kprobes arch_arm_kprobe(struct kprobe *p)
{
	patch_text(p->addr, USR_BKPT);
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
	kcb->saved_sr = regs->sr;
	regs->sr &= ~BIT(6);
}

static void __kprobes kprobes_restore_local_irqflag(struct kprobe_ctlblk *kcb,
						struct pt_regs *regs)
{
	regs->sr = kcb->saved_sr;
}

static void __kprobes
set_ss_context(struct kprobe_ctlblk *kcb, unsigned long addr, struct kprobe *p)
{
	unsigned long offset = is_insn32(p->opcode) ? 4 : 2;

	kcb->ss_ctx.ss_pending = true;
	kcb->ss_ctx.match_addr = addr + offset;
}

static void __kprobes clear_ss_context(struct kprobe_ctlblk *kcb)
{
	kcb->ss_ctx.ss_pending = false;
	kcb->ss_ctx.match_addr = 0;
}

#define TRACE_MODE_SI		BIT(14)
#define TRACE_MODE_MASK		~(0x3 << 14)
#define TRACE_MODE_RUN		0

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

		set_ss_context(kcb, slot, p);	/* mark pending ss */

		/* IRQs and single stepping do not mix well. */
		kprobes_save_local_irqflag(kcb, regs);
		regs->sr = (regs->sr & TRACE_MODE_MASK) | TRACE_MODE_SI;
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
post_kprobe_handler(struct kprobe_ctlblk *kcb, struct pt_regs *regs)
{
	struct kprobe *cur = kprobe_running();

	if (!cur)
		return;

	/* return addr restore if non-branching insn */
	if (cur->ainsn.api.restore != 0)
		regs->pc = cur->ainsn.api.restore;

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
		regs->pc = (unsigned long) cur->addr;
		if (!instruction_pointer(regs))
			BUG();

		if (kcb->kprobe_status == KPROBE_REENTER)
			restore_previous_kprobe(kcb);
		else
			reset_current_kprobe();

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

int __kprobes
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
				return 1;
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
		return 1;
	}

	/*
	 * The breakpoint instruction was removed right
	 * after we hit it.  Another cpu has removed
	 * either a probepoint or a debugger breakpoint
	 * at this address.  In either case, no further
	 * handling of this interrupt is appropriate.
	 * Return back to original instruction, and continue.
	 */
	return 0;
}

int __kprobes
kprobe_single_step_handler(struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	if ((kcb->ss_ctx.ss_pending)
	    && (kcb->ss_ctx.match_addr == instruction_pointer(regs))) {
		clear_ss_context(kcb);	/* clear pending ss */

		kprobes_restore_local_irqflag(kcb, regs);
		regs->sr = (regs->sr & TRACE_MODE_MASK) | TRACE_MODE_RUN;

		post_kprobe_handler(kcb, regs);
		return 1;
	}
	return 0;
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
	ri->ret_addr = (kprobe_opcode_t *)regs->lr;
	ri->fp = NULL;
	regs->lr = (unsigned long) &kretprobe_trampoline;
}

int __kprobes arch_trampoline_kprobe(struct kprobe *p)
{
	return 0;
}

int __init arch_init_kprobes(void)
{
	return 0;
}
