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
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/stop_machine.h>
#include <linux/stringify.h>
#include <asm/traps.h>
#include <asm/ptrace.h>
#include <asm/cacheflush.h>
#include <asm/debug-monitors.h>
#include <asm/system_misc.h>
#include <asm/insn.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm-generic/sections.h>

#include "decode-insn.h"

#define MIN_STACK_SIZE(addr)	(on_irq_stack(addr, raw_smp_processor_id()) ? \
	min((unsigned long)IRQ_STACK_SIZE,	\
	IRQ_STACK_PTR(raw_smp_processor_id()) - (addr)) : \
	min((unsigned long)MAX_STACK_SIZE,	\
	(unsigned long)current_thread_info() + THREAD_START_SP - (addr)))

void jprobe_return_break(void);

DEFINE_PER_CPU(struct kprobe *, current_kprobe) = NULL;
DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

static void __kprobes arch_prepare_ss_slot(struct kprobe *p)
{
	/* prepare insn slot */
	p->ainsn.insn[0] = cpu_to_le32(p->opcode);

	flush_icache_range((uintptr_t) (p->ainsn.insn),
			   (uintptr_t) (p->ainsn.insn) +
			   MAX_INSN_SIZE * sizeof(kprobe_opcode_t));

	/*
	 * Needs restoring of return address after stepping xol.
	 */
	p->ainsn.restore = (unsigned long) p->addr +
	  sizeof(kprobe_opcode_t);
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

	case INSN_GOOD:	/* instruction uses slot */
		p->ainsn.insn = get_insn_slot();
		if (!p->ainsn.insn)
			return -ENOMEM;
		break;
	};

	/* prepare the instruction */
	arch_prepare_ss_slot(p);

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
	if (p->ainsn.insn) {
		free_insn_slot(p->ainsn.insn, 0);
		p->ainsn.insn = NULL;
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
 * The D-flag (Debug mask) is set (masked) upon debug exception entry.
 * Kprobes needs to clear (unmask) D-flag -ONLY- in case of recursive
 * probe i.e. when probe hit from kprobe handler context upon
 * executing the pre/post handlers. In this case we return with
 * D-flag clear so that single-stepping can be carried-out.
 *
 * Leave D-flag set in all other cases.
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

	BUG_ON(!p->ainsn.insn);

	/* prepare for single stepping */
	slot = (unsigned long)p->ainsn.insn;

	set_ss_context(kcb, slot);	/* mark pending ss */

	if (kcb->kprobe_status == KPROBE_REENTER)
		spsr_set_debug_flag(regs, 0);

	/* IRQs and single stepping do not mix well. */
	kprobes_save_local_irqflag(kcb, regs);
	kernel_enable_single_step(regs);
	instruction_pointer_set(regs, slot);
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
		pr_warn("Unrecoverable kprobe detected at %p.\n", p->addr);
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
	if (cur->ainsn.restore != 0)
		instruction_pointer_set(regs, cur->ainsn.restore);

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
			spsr_set_debug_flag(regs, 1);

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

int __kprobes kprobe_exceptions_notify(struct notifier_block *self,
				       unsigned long val, void *data)
{
	return NOTIFY_DONE;
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

		if (kcb->kprobe_status == KPROBE_REENTER)
			spsr_set_debug_flag(regs, 1);

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
	long stack_ptr = kernel_stack_pointer(regs);

	kcb->jprobe_saved_regs = *regs;
	/*
	 * As Linus pointed out, gcc assumes that the callee
	 * owns the argument space and could overwrite it, e.g.
	 * tailcall optimization. So, to be absolutely safe
	 * we also save and restore enough stack bytes to cover
	 * the argument area.
	 */
	memcpy(kcb->jprobes_stack, (void *)stack_ptr,
	       MIN_STACK_SIZE(stack_ptr));

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
	 * -a magic number in x0 to identify from rest of other kprobes.
	 * -restore stack addr to original saved pt_regs
	 */
	asm volatile ("ldr x0, [%0]\n\t"
		      "mov sp, x0\n\t"
		      ".globl jprobe_return_break\n\t"
		      "jprobe_return_break:\n\t"
		      "brk %1\n\t"
		      :
		      : "r"(&kcb->jprobe_saved_regs.sp),
		      "I"(BRK64_ESR_KPROBES)
		      : "memory");
}

int __kprobes longjmp_break_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	long stack_addr = kcb->jprobe_saved_regs.sp;
	long orig_sp = kernel_stack_pointer(regs);
	struct jprobe *jp = container_of(p, struct jprobe, kp);

	if (instruction_pointer(regs) != (u64) jprobe_return_break)
		return 0;

	if (orig_sp != stack_addr) {
		struct pt_regs *saved_regs =
		    (struct pt_regs *)kcb->jprobe_saved_regs.sp;
		pr_err("current sp %lx does not match saved sp %lx\n",
		       orig_sp, stack_addr);
		pr_err("Saved registers for jprobe %p\n", jp);
		show_regs(saved_regs);
		pr_err("Current registers\n");
		show_regs(regs);
		BUG();
	}
	unpause_graph_tracing();
	*regs = kcb->jprobe_saved_regs;
	memcpy((void *)stack_addr, kcb->jprobes_stack,
	       MIN_STACK_SIZE(stack_addr));
	preempt_enable_no_resched();
	return 1;
}

bool arch_within_kprobe_blacklist(unsigned long addr)
{
	extern char __idmap_text_start[], __idmap_text_end[];
	extern char __hyp_idmap_text_start[], __hyp_idmap_text_end[];

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

int __init arch_init_kprobes(void)
{
	return 0;
}
