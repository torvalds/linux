/*
 * arch/arm64/kernel/probes/kprobes.c
 *
 * Kprobes support for ARM64
 *
 * Copyright (C) 2013 Linaro Limited.
 * Author: Sandeepa Prabhu <sandeepa.prabhu@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */
#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/extable.h>
#include <linux/slab.h>
#include <linux/stop_machine.h>
#include <linux/sched/debug.h>
#include <linux/stringify.h>
#include <asm/traps.h>
#include <asm/ptrace.h>
#include <asm/cacheflush.h>
#include <asm/debug-monitors.h>
#include <asm/system_misc.h>
#include <asm/insn.h>
#include <linux/uaccess.h>
#include <asm/irq.h>
#include <asm/sections.h>

#include "decode-insn.h"

DEFINE_PER_CPU(struct kprobe *, current_kprobe) = NULL;
DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

static void __kprobes
post_kprobe_handler(struct kprobe_ctlblk *, struct pt_regs *);

static void __kprobes arch_prepare_ss_slot(struct kprobe *p)
{
	/* prepare insn slot */
	p->ainsn.api.insn[0] = cpu_to_le32(p->opcode);

	flush_icache_range((uintptr_t) (p->ainsn.api.insn),
			   (uintptr_t) (p->ainsn.api.insn) +
			   MAX_INSN_SIZE * sizeof(kprobe_opcode_t));

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
	post_kprobe_handler(kcb, regs);
}

int __kprobes arch_prepare_kprobe(struct kprobe *p)
{
	unsigned long probe_addr = (unsigned long)p->addr;
	extern char __start_rodata[];
	extern char __end_rodata[];

	if (probe_addr & 0x3)
		return -EINVAL;

	/* copy instruction */
	p->opcode = le32_to_cpu(*p->addr);

	if (in_exception_text(probe_addr))
		return -EINVAL;
	if (probe_addr >= (unsigned long) __start_rodata &&
	    probe_addr <= (unsigned long) __end_rodata)
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
	};

	/* prepare the instruction */
	if (p->ainsn.api.insn)
		arch_prepare_ss_slot(p);
	else
		arch_prepare_simulate(p);

	return 0;
}

static int __kprobes patch_text(kprobe_opcode_t *addr, u32 opcode)
{
	void *addrs[1];
	u32 insns[1];

	addrs[0] = (void *)addr;
	insns[0] = (u32)opcode;

	return aarch64_insn_patch_text(addrs, insns, 1);
}

/* arm kprobe: install breakpoint in text */
void __kprobes arch_arm_kprobe(struct kprobe *p)
{
	patch_text(p->addr, BRK64_OPCODE_KPROBES);
}

/* disarm kprobe: remove breakpoint from text */
void __kprobes arch_disarm_kprobe(struct kprobe *p)
{
	patch_text(p->addr, p->opcode);
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
 * When PSTATE.D is set (masked), then software step exceptions can not be
 * generated.
 * SPSR's D bit shows the value of PSTATE.D immediately before the
 * exception was taken. PSTATE.D is set while entering into any exception
 * mode, however software clears it for any normal (none-debug-exception)
 * mode in the exception entry. Therefore, when we are entering into kprobe
 * breakpoint handler from any normal mode then SPSR.D bit is already
 * cleared, however it is set when we are entering from any debug exception
 * mode.
 * Since we always need to generate single step exception after a kprobe
 * breakpoint exception therefore we need to clear it unconditionally, when
 * we become sure that the current breakpoint exception is for kprobe.
 */
static void __kprobes
spsr_set_debug_flag(struct pt_regs *regs, int mask)
{
	unsigned long spsr = regs->pstate;

	if (mask)
		spsr |= PSR_D_BIT;
	else
		spsr &= ~PSR_D_BIT;

	regs->pstate = spsr;
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
	kcb->saved_irqflag = regs->pstate;
	regs->pstate |= PSR_I_BIT;
}

static void __kprobes kprobes_restore_local_irqflag(struct kprobe_ctlblk *kcb,
						struct pt_regs *regs)
{
	if (kcb->saved_irqflag & PSR_I_BIT)
		regs->pstate |= PSR_I_BIT;
	else
		regs->pstate &= ~PSR_I_BIT;
}

static void __kprobes
set_ss_context(struct kprobe_ctlblk *kcb, unsigned long addr)
{
	kcb->ss_ctx.ss_pending = true;
	kcb->ss_ctx.match_addr = addr + sizeof(kprobe_opcode_t);
}

static void __kprobes clear_ss_context(struct kprobe_ctlblk *kcb)
{
	kcb->ss_ctx.ss_pending = false;
	kcb->ss_ctx.match_addr = 0;
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

		set_ss_context(kcb, slot);	/* mark pending ss */

		spsr_set_debug_flag(regs, 0);

		/* IRQs and single stepping do not mix well. */
		kprobes_save_local_irqflag(kcb, regs);
		kernel_enable_single_step(regs);
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
		instruction_pointer_set(regs, cur->ainsn.api.restore);

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
		if (!instruction_pointer(regs))
			BUG();

		kernel_disable_single_step();

		if (kcb->kprobe_status == KPROBE_REENTER)
			restore_previous_kprobe(kcb);
		else
			reset_current_kprobe();

		break;
	case KPROBE_HIT_ACTIVE:
	case KPROBE_HIT_SSDONE:
		/*
		 * We increment the nmissed count for accounting,
		 * we can also use npre/npostfault count for accounting
		 * these specific fault cases.
		 */
		kprobes_inc_nmissed_count(cur);

		/*
		 * We come here because instructions in the pre/post
		 * handler caused the page_fault, this could happen
		 * if handler tries to access user space by
		 * copy_from_user(), get_user() etc. Let the
		 * user-specified handler try to fix it first.
		 */
		if (cur->fault_handler && cur->fault_handler(cur, regs, fsr))
			return 1;

		/*
		 * In case the user-specified fault handler returned
		 * zero, try to fix up.
		 */
		if (fixup_exception(regs))
			return 1;
	}
	return 0;
}

static void __kprobes kprobe_handler(struct pt_regs *regs)
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
				return;
		} else {
			/* Probe hit */
			set_current_kprobe(p);
			kcb->kprobe_status = KPROBE_HIT_ACTIVE;

			/*
			 * If we have no pre-handler or it returned 0, we
			 * continue with normal processing.  If we have a
			 * pre-handler and it returned non-zero, it prepped
			 * for calling the break_handler below on re-entry,
			 * so get out doing nothing more here.
			 *
			 * pre_handler can hit a breakpoint and can step thru
			 * before return, keep PSTATE D-flag enabled until
			 * pre_handler return back.
			 */
			if (!p->pre_handler || !p->pre_handler(p, regs)) {
				setup_singlestep(p, regs, kcb, 0);
				return;
			}
		}
	} else if ((le32_to_cpu(*(kprobe_opcode_t *) addr) ==
	    BRK64_OPCODE_KPROBES) && cur_kprobe) {
		/* We probably hit a jprobe.  Call its break handler. */
		if (cur_kprobe->break_handler  &&
		     cur_kprobe->break_handler(cur_kprobe, regs)) {
			setup_singlestep(cur_kprobe, regs, kcb, 0);
			return;
		}
	}
	/*
	 * The breakpoint instruction was removed right
	 * after we hit it.  Another cpu has removed
	 * either a probepoint or a debugger breakpoint
	 * at this address.  In either case, no further
	 * handling of this interrupt is appropriate.
	 * Return back to original instruction, and continue.
	 */
}

static int __kprobes
kprobe_ss_hit(struct kprobe_ctlblk *kcb, unsigned long addr)
{
	if ((kcb->ss_ctx.ss_pending)
	    && (kcb->ss_ctx.match_addr == addr)) {
		clear_ss_context(kcb);	/* clear pending ss */
		return DBG_HOOK_HANDLED;
	}
	/* not ours, kprobes should ignore it */
	return DBG_HOOK_ERROR;
}

int __kprobes
kprobe_single_step_handler(struct pt_regs *regs, unsigned int esr)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	int retval;

	/* return error if this is not our step */
	retval = kprobe_ss_hit(kcb, instruction_pointer(regs));

	if (retval == DBG_HOOK_HANDLED) {
		kprobes_restore_local_irqflag(kcb, regs);
		kernel_disable_single_step();

		post_kprobe_handler(kcb, regs);
	}

	return retval;
}

int __kprobes
kprobe_breakpoint_handler(struct pt_regs *regs, unsigned int esr)
{
	kprobe_handler(regs);
	return DBG_HOOK_HANDLED;
}

int __kprobes setjmp_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct jprobe *jp = container_of(p, struct jprobe, kp);
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	kcb->jprobe_saved_regs = *regs;
	/*
	 * Since we can't be sure where in the stack frame "stacked"
	 * pass-by-value arguments are stored we just don't try to
	 * duplicate any of the stack. Do not use jprobes on functions that
	 * use more than 64 bytes (after padding each to an 8 byte boundary)
	 * of arguments, or pass individual arguments larger than 16 bytes.
	 */

	instruction_pointer_set(regs, (unsigned long) jp->entry);
	preempt_disable();
	pause_graph_tracing();
	return 1;
}

void __kprobes jprobe_return(void)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	/*
	 * Jprobe handler return by entering break exception,
	 * encoded same as kprobe, but with following conditions
	 * -a special PC to identify it from the other kprobes.
	 * -restore stack addr to original saved pt_regs
	 */
	asm volatile("				mov sp, %0	\n"
		     "jprobe_return_break:	brk %1		\n"
		     :
		     : "r" (kcb->jprobe_saved_regs.sp),
		       "I" (BRK64_ESR_KPROBES)
		     : "memory");

	unreachable();
}

int __kprobes longjmp_break_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	long stack_addr = kcb->jprobe_saved_regs.sp;
	long orig_sp = kernel_stack_pointer(regs);
	struct jprobe *jp = container_of(p, struct jprobe, kp);
	extern const char jprobe_return_break[];

	if (instruction_pointer(regs) != (u64) jprobe_return_break)
		return 0;

	if (orig_sp != stack_addr) {
		struct pt_regs *saved_regs =
		    (struct pt_regs *)kcb->jprobe_saved_regs.sp;
		pr_err("current sp %lx does not match saved sp %lx\n",
		       orig_sp, stack_addr);
		pr_err("Saved registers for jprobe %p\n", jp);
		__show_regs(saved_regs);
		pr_err("Current registers\n");
		__show_regs(regs);
		BUG();
	}
	unpause_graph_tracing();
	*regs = kcb->jprobe_saved_regs;
	preempt_enable_no_resched();
	return 1;
}

bool arch_within_kprobe_blacklist(unsigned long addr)
{
	if ((addr >= (unsigned long)__kprobes_text_start &&
	    addr < (unsigned long)__kprobes_text_end) ||
	    (addr >= (unsigned long)__entry_text_start &&
	    addr < (unsigned long)__entry_text_end) ||
	    (addr >= (unsigned long)__idmap_text_start &&
	    addr < (unsigned long)__idmap_text_end) ||
	    !!search_exception_tables(addr))
		return true;

	if (!is_kernel_in_hyp_mode()) {
		if ((addr >= (unsigned long)__hyp_text_start &&
		    addr < (unsigned long)__hyp_text_end) ||
		    (addr >= (unsigned long)__hyp_idmap_text_start &&
		    addr < (unsigned long)__hyp_idmap_text_end))
			return true;
	}

	return false;
}

void __kprobes __used *trampoline_probe_handler(struct pt_regs *regs)
{
	struct kretprobe_instance *ri = NULL;
	struct hlist_head *head, empty_rp;
	struct hlist_node *tmp;
	unsigned long flags, orig_ret_address = 0;
	unsigned long trampoline_address =
		(unsigned long)&kretprobe_trampoline;
	kprobe_opcode_t *correct_ret_addr = NULL;

	INIT_HLIST_HEAD(&empty_rp);
	kretprobe_hash_lock(current, &head, &flags);

	/*
	 * It is possible to have multiple instances associated with a given
	 * task either because multiple functions in the call path have
	 * return probes installed on them, and/or more than one
	 * return probe was registered for a target function.
	 *
	 * We can handle this because:
	 *     - instances are always pushed into the head of the list
	 *     - when multiple return probes are registered for the same
	 *	 function, the (chronologically) first instance's ret_addr
	 *	 will be the real return address, and all the rest will
	 *	 point to kretprobe_trampoline.
	 */
	hlist_for_each_entry_safe(ri, tmp, head, hlist) {
		if (ri->task != current)
			/* another task is sharing our hash bucket */
			continue;

		orig_ret_address = (unsigned long)ri->ret_addr;

		if (orig_ret_address != trampoline_address)
			/*
			 * This is the real return address. Any other
			 * instances associated with this task are for
			 * other calls deeper on the call stack
			 */
			break;
	}

	kretprobe_assert(ri, orig_ret_address, trampoline_address);

	correct_ret_addr = ri->ret_addr;
	hlist_for_each_entry_safe(ri, tmp, head, hlist) {
		if (ri->task != current)
			/* another task is sharing our hash bucket */
			continue;

		orig_ret_address = (unsigned long)ri->ret_addr;
		if (ri->rp && ri->rp->handler) {
			__this_cpu_write(current_kprobe, &ri->rp->kp);
			get_kprobe_ctlblk()->kprobe_status = KPROBE_HIT_ACTIVE;
			ri->ret_addr = correct_ret_addr;
			ri->rp->handler(ri, regs);
			__this_cpu_write(current_kprobe, NULL);
		}

		recycle_rp_inst(ri, &empty_rp);

		if (orig_ret_address != trampoline_address)
			/*
			 * This is the real return address. Any other
			 * instances associated with this task are for
			 * other calls deeper on the call stack
			 */
			break;
	}

	kretprobe_hash_unlock(current, &flags);

	hlist_for_each_entry_safe(ri, tmp, &empty_rp, hlist) {
		hlist_del(&ri->hlist);
		kfree(ri);
	}
	return (void *)orig_ret_address;
}

void __kprobes arch_prepare_kretprobe(struct kretprobe_instance *ri,
				      struct pt_regs *regs)
{
	ri->ret_addr = (kprobe_opcode_t *)regs->regs[30];

	/* replace return addr (x30) with trampoline */
	regs->regs[30] = (long)&kretprobe_trampoline;
}

int __kprobes arch_trampoline_kprobe(struct kprobe *p)
{
	return 0;
}

int __init arch_init_kprobes(void)
{
	return 0;
}
