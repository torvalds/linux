// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm64/kernel/probes/kprobes.c
 *
 * Kprobes support for ARM64
 *
 * Copyright (C) 2013 Linaro Limited.
 * Author: Sandeepa Prabhu <sandeepa.prabhu@linaro.org>
 */

#define pr_fmt(fmt) "kprobes: " fmt

#include <linux/extable.h>
#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/sched/debug.h>
#include <linux/set_memory.h>
#include <linux/slab.h>
#include <linux/stop_machine.h>
#include <linux/stringify.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include <asm/cacheflush.h>
#include <asm/daifflags.h>
#include <asm/debug-monitors.h>
#include <asm/insn.h>
#include <asm/irq.h>
#include <asm/patching.h>
#include <asm/ptrace.h>
#include <asm/sections.h>
#include <asm/system_misc.h>
#include <asm/traps.h>

#include "decode-insn.h"

DEFINE_PER_CPU(struct kprobe *, current_kprobe) = NULL;
DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

static void __kprobes
post_kprobe_handler(struct kprobe *, struct kprobe_ctlblk *, struct pt_regs *);

static void __kprobes arch_prepare_ss_slot(struct kprobe *p)
{
	kprobe_opcode_t *addr = p->ainsn.api.insn;

	/*
	 * Prepare insn slot, Mark Rutland points out it depends on a coupe of
	 * subtleties:
	 *
	 * - That the I-cache maintenance for these instructions is complete
	 *   *before* the kprobe BRK is written (and aarch64_insn_patch_text_nosync()
	 *   ensures this, but just omits causing a Context-Synchronization-Event
	 *   on all CPUS).
	 *
	 * - That the kprobe BRK results in an exception (and consequently a
	 *   Context-Synchronoization-Event), which ensures that the CPU will
	 *   fetch thesingle-step slot instructions *after* this, ensuring that
	 *   the new instructions are used
	 *
	 * It supposes to place ISB after patching to guarantee I-cache maintenance
	 * is observed on all CPUS, however, single-step slot is installed in
	 * the BRK exception handler, so it is unnecessary to generate
	 * Contex-Synchronization-Event via ISB again.
	 */
	aarch64_insn_patch_text_nosync(addr, p->opcode);
	aarch64_insn_patch_text_nosync(addr + 1, BRK64_OPCODE_KPROBES_SS);

	/*
	 * Needs restoring of return address after stepping xol.
	 */
	p->ainsn.api.restore = (unsigned long) p->addr +
	  sizeof(kprobe_opcode_t);
}

static void __kprobes arch_prepare_simulate(struct kprobe *p)
{
	/* This instructions is not executed xol. No need to adjust the PC */
	p->ainsn.api.restore = 0;
}

static void __kprobes arch_simulate_insn(struct kprobe *p, struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	if (p->ainsn.api.handler)
		p->ainsn.api.handler((u32)p->opcode, (long)p->addr, regs);

	/* single step simulated, now go for post processing */
	post_kprobe_handler(p, kcb, regs);
}

int __kprobes arch_prepare_kprobe(struct kprobe *p)
{
	unsigned long probe_addr = (unsigned long)p->addr;

	if (probe_addr & 0x3)
		return -EINVAL;

	/* copy instruction */
	p->opcode = le32_to_cpu(*p->addr);

	if (search_exception_tables(probe_addr))
		return -EINVAL;

	/* decode instruction */
	switch (arm_kprobe_decode_insn(p->addr, &p->ainsn)) {
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

void *alloc_insn_page(void)
{
	return __vmalloc_node_range(PAGE_SIZE, 1, VMALLOC_START, VMALLOC_END,
			GFP_KERNEL, PAGE_KERNEL_ROX, VM_FLUSH_RESET_PERMS,
			NUMA_NO_NODE, __builtin_return_address(0));
}

/* arm kprobe: install breakpoint in text */
void __kprobes arch_arm_kprobe(struct kprobe *p)
{
	void *addr = p->addr;
	u32 insn = BRK64_OPCODE_KPROBES;

	aarch64_insn_patch_text(&addr, &insn, 1);
}

/* disarm kprobe: remove breakpoint from text */
void __kprobes arch_disarm_kprobe(struct kprobe *p)
{
	void *addr = p->addr;

	aarch64_insn_patch_text(&addr, &p->opcode, 1);
}

void __kprobes arch_remove_kprobe(struct kprobe *p)
{
	if (p->ainsn.api.insn) {
		free_insn_slot(p->ainsn.api.insn, 0);
		p->ainsn.api.insn = NULL;
	}
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
 * Mask all of DAIF while executing the instruction out-of-line, to keep things
 * simple and avoid nesting exceptions. Interrupts do have to be disabled since
 * the kprobe state is per-CPU and doesn't get migrated.
 */
static void __kprobes kprobes_save_local_irqflag(struct kprobe_ctlblk *kcb,
						struct pt_regs *regs)
{
	kcb->saved_irqflag = regs->pstate & DAIF_MASK;
	regs->pstate |= DAIF_MASK;
}

static void __kprobes kprobes_restore_local_irqflag(struct kprobe_ctlblk *kcb,
						struct pt_regs *regs)
{
	regs->pstate &= ~DAIF_MASK;
	regs->pstate |= kcb->saved_irqflag;
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
		pr_warn("Failed to recover from reentered kprobes.\n");
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
		instruction_pointer_set(regs, cur->ainsn.api.restore);

	/* restore back original saved kprobe variables and continue */
	if (kcb->kprobe_status == KPROBE_REENTER) {
		restore_previous_kprobe(kcb);
		return;
	}
	/* call post handler */
	kcb->kprobe_status = KPROBE_HIT_SSDONE;
	if (cur->post_handler)
		cur->post_handler(cur, regs, 0);

	reset_current_kprobe();
}

int __kprobes kprobe_fault_handler(struct pt_regs *regs, unsigned int fsr)
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
		instruction_pointer_set(regs, (unsigned long) cur->addr);
		BUG_ON(!instruction_pointer(regs));

		if (kcb->kprobe_status == KPROBE_REENTER) {
			restore_previous_kprobe(kcb);
		} else {
			kprobes_restore_local_irqflag(kcb, regs);
			reset_current_kprobe();
		}

		break;
	}
	return 0;
}

static int __kprobes
kprobe_breakpoint_handler(struct pt_regs *regs, unsigned long esr)
{
	struct kprobe *p, *cur_kprobe;
	struct kprobe_ctlblk *kcb;
	unsigned long addr = instruction_pointer(regs);

	kcb = get_kprobe_ctlblk();
	cur_kprobe = kprobe_running();

	p = get_kprobe((kprobe_opcode_t *) addr);
	if (WARN_ON_ONCE(!p)) {
		/*
		 * Something went wrong. This BRK used an immediate reserved
		 * for kprobes, but we couldn't find any corresponding probe.
		 */
		return DBG_HOOK_ERROR;
	}

	if (cur_kprobe) {
		/* Hit a kprobe inside another kprobe */
		if (!reenter_kprobe(p, regs, kcb))
			return DBG_HOOK_ERROR;
	} else {
		/* Probe hit */
		set_current_kprobe(p);
		kcb->kprobe_status = KPROBE_HIT_ACTIVE;

		/*
		 * If we have no pre-handler or it returned 0, we
		 * continue with normal processing.  If we have a
		 * pre-handler and it returned non-zero, it will
		 * modify the execution path and not need to single-step
		 * Let's just reset current kprobe and exit.
		 */
		if (!p->pre_handler || !p->pre_handler(p, regs))
			setup_singlestep(p, regs, kcb, 0);
		else
			reset_current_kprobe();
	}

	return DBG_HOOK_HANDLED;
}

static struct break_hook kprobes_break_hook = {
	.imm = KPROBES_BRK_IMM,
	.fn = kprobe_breakpoint_handler,
};

static int __kprobes
kprobe_breakpoint_ss_handler(struct pt_regs *regs, unsigned long esr)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	unsigned long addr = instruction_pointer(regs);
	struct kprobe *cur = kprobe_running();

	if (cur && (kcb->kprobe_status & (KPROBE_HIT_SS | KPROBE_REENTER)) &&
	    ((unsigned long)&cur->ainsn.api.insn[1] == addr)) {
		kprobes_restore_local_irqflag(kcb, regs);
		post_kprobe_handler(cur, kcb, regs);

		return DBG_HOOK_HANDLED;
	}

	/* not ours, kprobes should ignore it */
	return DBG_HOOK_ERROR;
}

static struct break_hook kprobes_break_ss_hook = {
	.imm = KPROBES_BRK_SS_IMM,
	.fn = kprobe_breakpoint_ss_handler,
};

static int __kprobes
kretprobe_breakpoint_handler(struct pt_regs *regs, unsigned long esr)
{
	if (regs->pc != (unsigned long)__kretprobe_trampoline)
		return DBG_HOOK_ERROR;

	regs->pc = kretprobe_trampoline_handler(regs, (void *)regs->regs[29]);
	return DBG_HOOK_HANDLED;
}

static struct break_hook kretprobes_break_hook = {
	.imm = KRETPROBES_BRK_IMM,
	.fn = kretprobe_breakpoint_handler,
};

/*
 * Provide a blacklist of symbols identifying ranges which cannot be kprobed.
 * This blacklist is exposed to userspace via debugfs (kprobes/blacklist).
 */
int __init arch_populate_kprobe_blacklist(void)
{
	int ret;

	ret = kprobe_add_area_blacklist((unsigned long)__entry_text_start,
					(unsigned long)__entry_text_end);
	if (ret)
		return ret;
	ret = kprobe_add_area_blacklist((unsigned long)__irqentry_text_start,
					(unsigned long)__irqentry_text_end);
	if (ret)
		return ret;
	ret = kprobe_add_area_blacklist((unsigned long)__hyp_text_start,
					(unsigned long)__hyp_text_end);
	if (ret || is_kernel_in_hyp_mode())
		return ret;
	ret = kprobe_add_area_blacklist((unsigned long)__hyp_idmap_text_start,
					(unsigned long)__hyp_idmap_text_end);
	return ret;
}

void __kprobes arch_prepare_kretprobe(struct kretprobe_instance *ri,
				      struct pt_regs *regs)
{
	ri->ret_addr = (kprobe_opcode_t *)regs->regs[30];
	ri->fp = (void *)regs->regs[29];

	/* replace return addr (x30) with trampoline */
	regs->regs[30] = (long)&__kretprobe_trampoline;
}

int __kprobes arch_trampoline_kprobe(struct kprobe *p)
{
	return 0;
}

int __init arch_init_kprobes(void)
{
	register_kernel_break_hook(&kprobes_break_hook);
	register_kernel_break_hook(&kprobes_break_ss_hook);
	register_kernel_break_hook(&kretprobes_break_hook);

	return 0;
}
