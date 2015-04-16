/*
 * arch/tile/kernel/kprobes.c
 * Kprobes on TILE-Gx
 *
 * Some portions copied from the MIPS version.
 *
 * Copyright (C) IBM Corporation, 2002, 2004
 * Copyright 2006 Sony Corp.
 * Copyright 2010 Cavium Networks
 *
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/kprobes.h>
#include <linux/kdebug.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>

#include <arch/opcode.h>

DEFINE_PER_CPU(struct kprobe *, current_kprobe) = NULL;
DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

tile_bundle_bits breakpoint_insn = TILEGX_BPT_BUNDLE;
tile_bundle_bits breakpoint2_insn = TILEGX_BPT_BUNDLE | DIE_SSTEPBP;

/*
 * Check whether instruction is branch or jump, or if executing it
 * has different results depending on where it is executed (e.g. lnk).
 */
static int __kprobes insn_has_control(kprobe_opcode_t insn)
{
	if (get_Mode(insn) != 0) {   /* Y-format bundle */
		if (get_Opcode_Y1(insn) != RRR_1_OPCODE_Y1 ||
		    get_RRROpcodeExtension_Y1(insn) != UNARY_RRR_1_OPCODE_Y1)
			return 0;

		switch (get_UnaryOpcodeExtension_Y1(insn)) {
		case JALRP_UNARY_OPCODE_Y1:
		case JALR_UNARY_OPCODE_Y1:
		case JRP_UNARY_OPCODE_Y1:
		case JR_UNARY_OPCODE_Y1:
		case LNK_UNARY_OPCODE_Y1:
			return 1;
		default:
			return 0;
		}
	}

	switch (get_Opcode_X1(insn)) {
	case BRANCH_OPCODE_X1:	/* branch instructions */
	case JUMP_OPCODE_X1:	/* jump instructions: j and jal */
		return 1;

	case RRR_0_OPCODE_X1:   /* other jump instructions */
		if (get_RRROpcodeExtension_X1(insn) != UNARY_RRR_0_OPCODE_X1)
			return 0;
		switch (get_UnaryOpcodeExtension_X1(insn)) {
		case JALRP_UNARY_OPCODE_X1:
		case JALR_UNARY_OPCODE_X1:
		case JRP_UNARY_OPCODE_X1:
		case JR_UNARY_OPCODE_X1:
		case LNK_UNARY_OPCODE_X1:
			return 1;
		default:
			return 0;
		}
	default:
		return 0;
	}
}

int __kprobes arch_prepare_kprobe(struct kprobe *p)
{
	unsigned long addr = (unsigned long)p->addr;

	if (addr & (sizeof(kprobe_opcode_t) - 1))
		return -EINVAL;

	if (insn_has_control(*p->addr)) {
		pr_notice("Kprobes for control instructions are not supported\n");
		return -EINVAL;
	}

	/* insn: must be on special executable page on tile. */
	p->ainsn.insn = get_insn_slot();
	if (!p->ainsn.insn)
		return -ENOMEM;

	/*
	 * In the kprobe->ainsn.insn[] array we store the original
	 * instruction at index zero and a break trap instruction at
	 * index one.
	 */
	memcpy(&p->ainsn.insn[0], p->addr, sizeof(kprobe_opcode_t));
	p->ainsn.insn[1] = breakpoint2_insn;
	p->opcode = *p->addr;

	return 0;
}

void __kprobes arch_arm_kprobe(struct kprobe *p)
{
	unsigned long addr_wr;

	/* Operate on writable kernel text mapping. */
	addr_wr = (unsigned long)p->addr - MEM_SV_START + PAGE_OFFSET;

	if (probe_kernel_write((void *)addr_wr, &breakpoint_insn,
		sizeof(breakpoint_insn)))
		pr_err("%s: failed to enable kprobe\n", __func__);

	smp_wmb();
	flush_insn_slot(p);
}

void __kprobes arch_disarm_kprobe(struct kprobe *kp)
{
	unsigned long addr_wr;

	/* Operate on writable kernel text mapping. */
	addr_wr = (unsigned long)kp->addr - MEM_SV_START + PAGE_OFFSET;

	if (probe_kernel_write((void *)addr_wr, &kp->opcode,
		sizeof(kp->opcode)))
		pr_err("%s: failed to enable kprobe\n", __func__);

	smp_wmb();
	flush_insn_slot(kp);
}

void __kprobes arch_remove_kprobe(struct kprobe *p)
{
	if (p->ainsn.insn) {
		free_insn_slot(p->ainsn.insn, 0);
		p->ainsn.insn = NULL;
	}
}

static void __kprobes save_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	kcb->prev_kprobe.kp = kprobe_running();
	kcb->prev_kprobe.status = kcb->kprobe_status;
	kcb->prev_kprobe.saved_pc = kcb->kprobe_saved_pc;
}

static void __kprobes restore_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	__this_cpu_write(current_kprobe, kcb->prev_kprobe.kp);
	kcb->kprobe_status = kcb->prev_kprobe.status;
	kcb->kprobe_saved_pc = kcb->prev_kprobe.saved_pc;
}

static void __kprobes set_current_kprobe(struct kprobe *p, struct pt_regs *regs,
			struct kprobe_ctlblk *kcb)
{
	__this_cpu_write(current_kprobe, p);
	kcb->kprobe_saved_pc = regs->pc;
}

static void __kprobes prepare_singlestep(struct kprobe *p, struct pt_regs *regs)
{
	/* Single step inline if the instruction is a break. */
	if (p->opcode == breakpoint_insn ||
	    p->opcode == breakpoint2_insn)
		regs->pc = (unsigned long)p->addr;
	else
		regs->pc = (unsigned long)&p->ainsn.insn[0];
}

static int __kprobes kprobe_handler(struct pt_regs *regs)
{
	struct kprobe *p;
	int ret = 0;
	kprobe_opcode_t *addr;
	struct kprobe_ctlblk *kcb;

	addr = (kprobe_opcode_t *)regs->pc;

	/*
	 * We don't want to be preempted for the entire
	 * duration of kprobe processing.
	 */
	preempt_disable();
	kcb = get_kprobe_ctlblk();

	/* Check we're not actually recursing. */
	if (kprobe_running()) {
		p = get_kprobe(addr);
		if (p) {
			if (kcb->kprobe_status == KPROBE_HIT_SS &&
			    p->ainsn.insn[0] == breakpoint_insn) {
				goto no_kprobe;
			}
			/*
			 * We have reentered the kprobe_handler(), since
			 * another probe was hit while within the handler.
			 * We here save the original kprobes variables and
			 * just single step on the instruction of the new probe
			 * without calling any user handlers.
			 */
			save_previous_kprobe(kcb);
			set_current_kprobe(p, regs, kcb);
			kprobes_inc_nmissed_count(p);
			prepare_singlestep(p, regs);
			kcb->kprobe_status = KPROBE_REENTER;
			return 1;
		} else {
			if (*addr != breakpoint_insn) {
				/*
				 * The breakpoint instruction was removed by
				 * another cpu right after we hit, no further
				 * handling of this interrupt is appropriate.
				 */
				ret = 1;
				goto no_kprobe;
			}
			p = __this_cpu_read(current_kprobe);
			if (p->break_handler && p->break_handler(p, regs))
				goto ss_probe;
		}
		goto no_kprobe;
	}

	p = get_kprobe(addr);
	if (!p) {
		if (*addr != breakpoint_insn) {
			/*
			 * The breakpoint instruction was removed right
			 * after we hit it.  Another cpu has removed
			 * either a probepoint or a debugger breakpoint
			 * at this address.  In either case, no further
			 * handling of this interrupt is appropriate.
			 */
			ret = 1;
		}
		/* Not one of ours: let kernel handle it. */
		goto no_kprobe;
	}

	set_current_kprobe(p, regs, kcb);
	kcb->kprobe_status = KPROBE_HIT_ACTIVE;

	if (p->pre_handler && p->pre_handler(p, regs)) {
		/* Handler has already set things up, so skip ss setup. */
		return 1;
	}

ss_probe:
	prepare_singlestep(p, regs);
	kcb->kprobe_status = KPROBE_HIT_SS;
	return 1;

no_kprobe:
	preempt_enable_no_resched();
	return ret;
}

/*
 * Called after single-stepping.  p->addr is the address of the
 * instruction that has been replaced by the breakpoint. To avoid the
 * SMP problems that can occur when we temporarily put back the
 * original opcode to single-step, we single-stepped a copy of the
 * instruction.  The address of this copy is p->ainsn.insn.
 *
 * This function prepares to return from the post-single-step
 * breakpoint trap.
 */
static void __kprobes resume_execution(struct kprobe *p,
				       struct pt_regs *regs,
				       struct kprobe_ctlblk *kcb)
{
	unsigned long orig_pc = kcb->kprobe_saved_pc;
	regs->pc = orig_pc + 8;
}

static inline int post_kprobe_handler(struct pt_regs *regs)
{
	struct kprobe *cur = kprobe_running();
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	if (!cur)
		return 0;

	if ((kcb->kprobe_status != KPROBE_REENTER) && cur->post_handler) {
		kcb->kprobe_status = KPROBE_HIT_SSDONE;
		cur->post_handler(cur, regs, 0);
	}

	resume_execution(cur, regs, kcb);

	/* Restore back the original saved kprobes variables and continue. */
	if (kcb->kprobe_status == KPROBE_REENTER) {
		restore_previous_kprobe(kcb);
		goto out;
	}
	reset_current_kprobe();
out:
	preempt_enable_no_resched();

	return 1;
}

static inline int kprobe_fault_handler(struct pt_regs *regs, int trapnr)
{
	struct kprobe *cur = kprobe_running();
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	if (cur->fault_handler && cur->fault_handler(cur, regs, trapnr))
		return 1;

	if (kcb->kprobe_status & KPROBE_HIT_SS) {
		/*
		 * We are here because the instruction being single
		 * stepped caused a page fault. We reset the current
		 * kprobe and the ip points back to the probe address
		 * and allow the page fault handler to continue as a
		 * normal page fault.
		 */
		resume_execution(cur, regs, kcb);
		reset_current_kprobe();
		preempt_enable_no_resched();
	}
	return 0;
}

/*
 * Wrapper routine for handling exceptions.
 */
int __kprobes kprobe_exceptions_notify(struct notifier_block *self,
				       unsigned long val, void *data)
{
	struct die_args *args = (struct die_args *)data;
	int ret = NOTIFY_DONE;

	switch (val) {
	case DIE_BREAK:
		if (kprobe_handler(args->regs))
			ret = NOTIFY_STOP;
		break;
	case DIE_SSTEPBP:
		if (post_kprobe_handler(args->regs))
			ret = NOTIFY_STOP;
		break;
	case DIE_PAGE_FAULT:
		/* kprobe_running() needs smp_processor_id(). */
		preempt_disable();

		if (kprobe_running()
		    && kprobe_fault_handler(args->regs, args->trapnr))
			ret = NOTIFY_STOP;
		preempt_enable();
		break;
	default:
		break;
	}
	return ret;
}

int __kprobes setjmp_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct jprobe *jp = container_of(p, struct jprobe, kp);
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	kcb->jprobe_saved_regs = *regs;
	kcb->jprobe_saved_sp = regs->sp;

	memcpy(kcb->jprobes_stack, (void *)kcb->jprobe_saved_sp,
	       MIN_JPROBES_STACK_SIZE(kcb->jprobe_saved_sp));

	regs->pc = (unsigned long)(jp->entry);

	return 1;
}

/* Defined in the inline asm below. */
void jprobe_return_end(void);

void __kprobes jprobe_return(void)
{
	asm volatile(
		"bpt\n\t"
		".globl jprobe_return_end\n"
		"jprobe_return_end:\n");
}

int __kprobes longjmp_break_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	if (regs->pc >= (unsigned long)jprobe_return &&
	    regs->pc <= (unsigned long)jprobe_return_end) {
		*regs = kcb->jprobe_saved_regs;
		memcpy((void *)kcb->jprobe_saved_sp, kcb->jprobes_stack,
		       MIN_JPROBES_STACK_SIZE(kcb->jprobe_saved_sp));
		preempt_enable_no_resched();

		return 1;
	}
	return 0;
}

/*
 * Function return probe trampoline:
 * - init_kprobes() establishes a probepoint here
 * - When the probed function returns, this probe causes the
 *   handlers to fire
 */
static void __used kretprobe_trampoline_holder(void)
{
	asm volatile(
		"nop\n\t"
		".global kretprobe_trampoline\n"
		"kretprobe_trampoline:\n\t"
		"nop\n\t"
		: : : "memory");
}

void kretprobe_trampoline(void);

void __kprobes arch_prepare_kretprobe(struct kretprobe_instance *ri,
				      struct pt_regs *regs)
{
	ri->ret_addr = (kprobe_opcode_t *) regs->lr;

	/* Replace the return addr with trampoline addr */
	regs->lr = (unsigned long)kretprobe_trampoline;
}

/*
 * Called when the probe at kretprobe trampoline is hit.
 */
static int __kprobes trampoline_probe_handler(struct kprobe *p,
						struct pt_regs *regs)
{
	struct kretprobe_instance *ri = NULL;
	struct hlist_head *head, empty_rp;
	struct hlist_node *tmp;
	unsigned long flags, orig_ret_address = 0;
	unsigned long trampoline_address = (unsigned long)kretprobe_trampoline;

	INIT_HLIST_HEAD(&empty_rp);
	kretprobe_hash_lock(current, &head, &flags);

	/*
	 * It is possible to have multiple instances associated with a given
	 * task either because multiple functions in the call path have
	 * a return probe installed on them, and/or more than one return
	 * return probe was registered for a target function.
	 *
	 * We can handle this because:
	 *     - instances are always inserted at the head of the list
	 *     - when multiple return probes are registered for the same
	 *       function, the first instance's ret_addr will point to the
	 *       real return address, and all the rest will point to
	 *       kretprobe_trampoline
	 */
	hlist_for_each_entry_safe(ri, tmp, head, hlist) {
		if (ri->task != current)
			/* another task is sharing our hash bucket */
			continue;

		if (ri->rp && ri->rp->handler)
			ri->rp->handler(ri, regs);

		orig_ret_address = (unsigned long)ri->ret_addr;
		recycle_rp_inst(ri, &empty_rp);

		if (orig_ret_address != trampoline_address) {
			/*
			 * This is the real return address. Any other
			 * instances associated with this task are for
			 * other calls deeper on the call stack
			 */
			break;
		}
	}

	kretprobe_assert(ri, orig_ret_address, trampoline_address);
	instruction_pointer(regs) = orig_ret_address;

	reset_current_kprobe();
	kretprobe_hash_unlock(current, &flags);
	preempt_enable_no_resched();

	hlist_for_each_entry_safe(ri, tmp, &empty_rp, hlist) {
		hlist_del(&ri->hlist);
		kfree(ri);
	}
	/*
	 * By returning a non-zero value, we are telling
	 * kprobe_handler() that we don't want the post_handler
	 * to run (and have re-enabled preemption)
	 */
	return 1;
}

int __kprobes arch_trampoline_kprobe(struct kprobe *p)
{
	if (p->addr == (kprobe_opcode_t *)kretprobe_trampoline)
		return 1;

	return 0;
}

static struct kprobe trampoline_p = {
	.addr = (kprobe_opcode_t *)kretprobe_trampoline,
	.pre_handler = trampoline_probe_handler
};

int __init arch_init_kprobes(void)
{
	register_kprobe(&trampoline_p);
	return 0;
}
