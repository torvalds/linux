// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kdebug.h>
#include <linux/kprobes.h>
#include <linux/preempt.h>
#include <asm/break.h>

static const union loongarch_instruction breakpoint_insn = {
	.reg0i15_format = {
		.opcode = break_op,
		.immediate = BRK_KPROBE_BP,
	}
};

static const union loongarch_instruction singlestep_insn = {
	.reg0i15_format = {
		.opcode = break_op,
		.immediate = BRK_KPROBE_SSTEPBP,
	}
};

DEFINE_PER_CPU(struct kprobe *, current_kprobe);
DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

static bool insns_not_supported(union loongarch_instruction insn)
{
	switch (insn.reg2i14_format.opcode) {
	case llw_op:
	case lld_op:
	case scw_op:
	case scd_op:
		pr_notice("kprobe: ll and sc instructions are not supported\n");
		return true;
	}

	switch (insn.reg1i21_format.opcode) {
	case bceqz_op:
		pr_notice("kprobe: bceqz and bcnez instructions are not supported\n");
		return true;
	}

	return false;
}
NOKPROBE_SYMBOL(insns_not_supported);

static bool insns_need_simulation(struct kprobe *p)
{
	if (is_pc_ins(&p->opcode))
		return true;

	if (is_branch_ins(&p->opcode))
		return true;

	return false;
}
NOKPROBE_SYMBOL(insns_need_simulation);

static void arch_simulate_insn(struct kprobe *p, struct pt_regs *regs)
{
	if (is_pc_ins(&p->opcode))
		simu_pc(regs, p->opcode);
	else if (is_branch_ins(&p->opcode))
		simu_branch(regs, p->opcode);
}
NOKPROBE_SYMBOL(arch_simulate_insn);

static void arch_prepare_ss_slot(struct kprobe *p)
{
	p->ainsn.insn[0] = *p->addr;
	p->ainsn.insn[1] = singlestep_insn;
	p->ainsn.restore = (unsigned long)p->addr + LOONGARCH_INSN_SIZE;
}
NOKPROBE_SYMBOL(arch_prepare_ss_slot);

static void arch_prepare_simulate(struct kprobe *p)
{
	p->ainsn.restore = 0;
}
NOKPROBE_SYMBOL(arch_prepare_simulate);

int arch_prepare_kprobe(struct kprobe *p)
{
	if ((unsigned long)p->addr & 0x3)
		return -EILSEQ;

	/* copy instruction */
	p->opcode = *p->addr;

	/* decode instruction */
	if (insns_not_supported(p->opcode))
		return -EINVAL;

	if (insns_need_simulation(p)) {
		p->ainsn.insn = NULL;
	} else {
		p->ainsn.insn = get_insn_slot();
		if (!p->ainsn.insn)
			return -ENOMEM;
	}

	/* prepare the instruction */
	if (p->ainsn.insn)
		arch_prepare_ss_slot(p);
	else
		arch_prepare_simulate(p);

	return 0;
}
NOKPROBE_SYMBOL(arch_prepare_kprobe);

/* Install breakpoint in text */
void arch_arm_kprobe(struct kprobe *p)
{
	*p->addr = breakpoint_insn;
	flush_insn_slot(p);
}
NOKPROBE_SYMBOL(arch_arm_kprobe);

/* Remove breakpoint from text */
void arch_disarm_kprobe(struct kprobe *p)
{
	*p->addr = p->opcode;
	flush_insn_slot(p);
}
NOKPROBE_SYMBOL(arch_disarm_kprobe);

void arch_remove_kprobe(struct kprobe *p)
{
	if (p->ainsn.insn) {
		free_insn_slot(p->ainsn.insn, 0);
		p->ainsn.insn = NULL;
	}
}
NOKPROBE_SYMBOL(arch_remove_kprobe);

static void save_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	kcb->prev_kprobe.kp = kprobe_running();
	kcb->prev_kprobe.status = kcb->kprobe_status;
}
NOKPROBE_SYMBOL(save_previous_kprobe);

static void restore_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	__this_cpu_write(current_kprobe, kcb->prev_kprobe.kp);
	kcb->kprobe_status = kcb->prev_kprobe.status;
}
NOKPROBE_SYMBOL(restore_previous_kprobe);

static void set_current_kprobe(struct kprobe *p)
{
	__this_cpu_write(current_kprobe, p);
}
NOKPROBE_SYMBOL(set_current_kprobe);

/*
 * Interrupts need to be disabled before single-step mode is set,
 * and not reenabled until after single-step mode ends.
 * Without disabling interrupt on local CPU, there is a chance of
 * interrupt occurrence in the period of exception return and start
 * of out-of-line single-step, that result in wrongly single stepping
 * into the interrupt handler.
 */
static void save_local_irqflag(struct kprobe_ctlblk *kcb,
			       struct pt_regs *regs)
{
	kcb->saved_status = regs->csr_prmd;
	regs->csr_prmd &= ~CSR_PRMD_PIE;
}
NOKPROBE_SYMBOL(save_local_irqflag);

static void restore_local_irqflag(struct kprobe_ctlblk *kcb,
				  struct pt_regs *regs)
{
	regs->csr_prmd = kcb->saved_status;
}
NOKPROBE_SYMBOL(restore_local_irqflag);

static void post_kprobe_handler(struct kprobe *cur, struct kprobe_ctlblk *kcb,
				struct pt_regs *regs)
{
	/* return addr restore if non-branching insn */
	if (cur->ainsn.restore != 0)
		instruction_pointer_set(regs, cur->ainsn.restore);

	/* restore back original saved kprobe variables and continue */
	if (kcb->kprobe_status == KPROBE_REENTER) {
		restore_previous_kprobe(kcb);
		preempt_enable_no_resched();
		return;
	}

	/*
	 * update the kcb status even if the cur->post_handler is
	 * not set because reset_curent_kprobe() doesn't update kcb.
	 */
	kcb->kprobe_status = KPROBE_HIT_SSDONE;
	if (cur->post_handler)
		cur->post_handler(cur, regs, 0);

	reset_current_kprobe();
	preempt_enable_no_resched();
}
NOKPROBE_SYMBOL(post_kprobe_handler);

static void setup_singlestep(struct kprobe *p, struct pt_regs *regs,
			     struct kprobe_ctlblk *kcb, int reenter)
{
	if (reenter) {
		save_previous_kprobe(kcb);
		set_current_kprobe(p);
		kcb->kprobe_status = KPROBE_REENTER;
	} else {
		kcb->kprobe_status = KPROBE_HIT_SS;
	}

	if (p->ainsn.insn) {
		/* IRQs and single stepping do not mix well */
		save_local_irqflag(kcb, regs);
		/* set ip register to prepare for single stepping */
		regs->csr_era = (unsigned long)p->ainsn.insn;
	} else {
		/* simulate single steping */
		arch_simulate_insn(p, regs);
		/* now go for post processing */
		post_kprobe_handler(p, kcb, regs);
	}
}
NOKPROBE_SYMBOL(setup_singlestep);

static bool reenter_kprobe(struct kprobe *p, struct pt_regs *regs,
			   struct kprobe_ctlblk *kcb)
{
	switch (kcb->kprobe_status) {
	case KPROBE_HIT_SS:
	case KPROBE_HIT_SSDONE:
	case KPROBE_HIT_ACTIVE:
		kprobes_inc_nmissed_count(p);
		setup_singlestep(p, regs, kcb, 1);
		break;
	case KPROBE_REENTER:
		pr_warn("Failed to recover from reentered kprobes.\n");
		dump_kprobe(p);
		WARN_ON_ONCE(1);
		break;
	default:
		WARN_ON(1);
		return false;
	}

	return true;
}
NOKPROBE_SYMBOL(reenter_kprobe);

bool kprobe_breakpoint_handler(struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb;
	struct kprobe *p, *cur_kprobe;
	kprobe_opcode_t *addr = (kprobe_opcode_t *)regs->csr_era;

	/*
	 * We don't want to be preempted for the entire
	 * duration of kprobe processing.
	 */
	preempt_disable();
	kcb = get_kprobe_ctlblk();
	cur_kprobe = kprobe_running();

	p = get_kprobe(addr);
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
			if (!p->pre_handler || !p->pre_handler(p, regs)) {
				setup_singlestep(p, regs, kcb, 0);
			} else {
				reset_current_kprobe();
				preempt_enable_no_resched();
			}
			return true;
		}
	}

	if (addr->word != breakpoint_insn.word) {
		/*
		 * The breakpoint instruction was removed right
		 * after we hit it.  Another cpu has removed
		 * either a probepoint or a debugger breakpoint
		 * at this address.  In either case, no further
		 * handling of this interrupt is appropriate.
		 * Return back to original instruction, and continue.
		 */
		regs->csr_era = (unsigned long)addr;
		preempt_enable_no_resched();
		return true;
	}

	preempt_enable_no_resched();
	return false;
}
NOKPROBE_SYMBOL(kprobe_breakpoint_handler);

bool kprobe_singlestep_handler(struct pt_regs *regs)
{
	struct kprobe *cur = kprobe_running();
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	unsigned long addr = instruction_pointer(regs);

	if (cur && (kcb->kprobe_status & (KPROBE_HIT_SS | KPROBE_REENTER)) &&
	    ((unsigned long)&cur->ainsn.insn[1] == addr)) {
		restore_local_irqflag(kcb, regs);
		post_kprobe_handler(cur, kcb, regs);
		return true;
	}

	preempt_enable_no_resched();
	return false;
}
NOKPROBE_SYMBOL(kprobe_singlestep_handler);

bool kprobe_fault_handler(struct pt_regs *regs, int trapnr)
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
		regs->csr_era = (unsigned long)cur->addr;
		WARN_ON_ONCE(!instruction_pointer(regs));

		if (kcb->kprobe_status == KPROBE_REENTER) {
			restore_previous_kprobe(kcb);
		} else {
			restore_local_irqflag(kcb, regs);
			reset_current_kprobe();
		}
		preempt_enable_no_resched();
		break;
	}
	return false;
}
NOKPROBE_SYMBOL(kprobe_fault_handler);

/*
 * Provide a blacklist of symbols identifying ranges which cannot be kprobed.
 * This blacklist is exposed to userspace via debugfs (kprobes/blacklist).
 */
int __init arch_populate_kprobe_blacklist(void)
{
	return kprobe_add_area_blacklist((unsigned long)__irqentry_text_start,
					 (unsigned long)__irqentry_text_end);
}

int __init arch_init_kprobes(void)
{
	return 0;
}

/* ASM function that handles the kretprobes must not be probed */
NOKPROBE_SYMBOL(__kretprobe_trampoline);

/* Called from __kretprobe_trampoline */
void __used *trampoline_probe_handler(struct pt_regs *regs)
{
	return (void *)kretprobe_trampoline_handler(regs, NULL);
}
NOKPROBE_SYMBOL(trampoline_probe_handler);

void arch_prepare_kretprobe(struct kretprobe_instance *ri,
			    struct pt_regs *regs)
{
	ri->ret_addr = (kprobe_opcode_t *)regs->regs[1];
	ri->fp = NULL;

	/* Replace the return addr with trampoline addr */
	regs->regs[1] = (unsigned long)&__kretprobe_trampoline;
}
NOKPROBE_SYMBOL(arch_prepare_kretprobe);

int arch_trampoline_kprobe(struct kprobe *p)
{
	return 0;
}
NOKPROBE_SYMBOL(arch_trampoline_kprobe);
