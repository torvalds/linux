/*
 *  Kernel Probes (KProbes)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2002, 2004
 *
 * 2002-Oct	Created by Vamsi Krishna S <vamsi_krishna@in.ibm.com> Kernel
 *		Probes initial implementation ( includes contributions from
 *		Rusty Russell).
 * 2004-July	Suparna Bhattacharya <suparna@in.ibm.com> added jumper probes
 *		interface to access function arguments.
 * 2004-Nov	Ananth N Mavinakayanahalli <ananth@in.ibm.com> kprobes port
 *		for PPC64
 */

#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/preempt.h>
#include <linux/module.h>
#include <linux/kdebug.h>
#include <linux/slab.h>
#include <asm/cacheflush.h>
#include <asm/sstep.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#ifdef CONFIG_PPC_ADV_DEBUG_REGS
#define MSR_SINGLESTEP	(MSR_DE)
#else
#define MSR_SINGLESTEP	(MSR_SE)
#endif

DEFINE_PER_CPU(struct kprobe *, current_kprobe) = NULL;
DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

struct kretprobe_blackpoint kretprobe_blacklist[] = {{NULL, NULL}};

int __kprobes arch_prepare_kprobe(struct kprobe *p)
{
	int ret = 0;
	kprobe_opcode_t insn = *p->addr;

	if ((unsigned long)p->addr & 0x03) {
		printk("Attempt to register kprobe at an unaligned address\n");
		ret = -EINVAL;
	} else if (IS_MTMSRD(insn) || IS_RFID(insn) || IS_RFI(insn)) {
		printk("Cannot register a kprobe on rfi/rfid or mtmsr[d]\n");
		ret = -EINVAL;
	}

	/* insn must be on a special executable page on ppc64.  This is
	 * not explicitly required on ppc32 (right now), but it doesn't hurt */
	if (!ret) {
		p->ainsn.insn = get_insn_slot();
		if (!p->ainsn.insn)
			ret = -ENOMEM;
	}

	if (!ret) {
		memcpy(p->ainsn.insn, p->addr,
				MAX_INSN_SIZE * sizeof(kprobe_opcode_t));
		p->opcode = *p->addr;
		flush_icache_range((unsigned long)p->ainsn.insn,
			(unsigned long)p->ainsn.insn + sizeof(kprobe_opcode_t));
	}

	p->ainsn.boostable = 0;
	return ret;
}

void __kprobes arch_arm_kprobe(struct kprobe *p)
{
	*p->addr = BREAKPOINT_INSTRUCTION;
	flush_icache_range((unsigned long) p->addr,
			   (unsigned long) p->addr + sizeof(kprobe_opcode_t));
}

void __kprobes arch_disarm_kprobe(struct kprobe *p)
{
	*p->addr = p->opcode;
	flush_icache_range((unsigned long) p->addr,
			   (unsigned long) p->addr + sizeof(kprobe_opcode_t));
}

void __kprobes arch_remove_kprobe(struct kprobe *p)
{
	if (p->ainsn.insn) {
		free_insn_slot(p->ainsn.insn, 0);
		p->ainsn.insn = NULL;
	}
}

static void __kprobes prepare_singlestep(struct kprobe *p, struct pt_regs *regs)
{
	/* We turn off async exceptions to ensure that the single step will
	 * be for the instruction we have the kprobe on, if we dont its
	 * possible we'd get the single step reported for an exception handler
	 * like Decrementer or External Interrupt */
	regs->msr &= ~MSR_EE;
	regs->msr |= MSR_SINGLESTEP;
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
	regs->msr &= ~MSR_CE;
	mtspr(SPRN_DBCR0, mfspr(SPRN_DBCR0) | DBCR0_IC | DBCR0_IDM);
#endif

	/*
	 * On powerpc we should single step on the original
	 * instruction even if the probed insn is a trap
	 * variant as values in regs could play a part in
	 * if the trap is taken or not
	 */
	regs->nip = (unsigned long)p->ainsn.insn;
}

static void __kprobes save_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	kcb->prev_kprobe.kp = kprobe_running();
	kcb->prev_kprobe.status = kcb->kprobe_status;
	kcb->prev_kprobe.saved_msr = kcb->kprobe_saved_msr;
}

static void __kprobes restore_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	__get_cpu_var(current_kprobe) = kcb->prev_kprobe.kp;
	kcb->kprobe_status = kcb->prev_kprobe.status;
	kcb->kprobe_saved_msr = kcb->prev_kprobe.saved_msr;
}

static void __kprobes set_current_kprobe(struct kprobe *p, struct pt_regs *regs,
				struct kprobe_ctlblk *kcb)
{
	__get_cpu_var(current_kprobe) = p;
	kcb->kprobe_saved_msr = regs->msr;
}

void __kprobes arch_prepare_kretprobe(struct kretprobe_instance *ri,
				      struct pt_regs *regs)
{
	ri->ret_addr = (kprobe_opcode_t *)regs->link;

	/* Replace the return addr with trampoline addr */
	regs->link = (unsigned long)kretprobe_trampoline;
}

static int __kprobes kprobe_handler(struct pt_regs *regs)
{
	struct kprobe *p;
	int ret = 0;
	unsigned int *addr = (unsigned int *)regs->nip;
	struct kprobe_ctlblk *kcb;

	/*
	 * We don't want to be preempted for the entire
	 * duration of kprobe processing
	 */
	preempt_disable();
	kcb = get_kprobe_ctlblk();

	/* Check we're not actually recursing */
	if (kprobe_running()) {
		p = get_kprobe(addr);
		if (p) {
			kprobe_opcode_t insn = *p->ainsn.insn;
			if (kcb->kprobe_status == KPROBE_HIT_SS &&
					is_trap(insn)) {
				/* Turn off 'trace' bits */
				regs->msr &= ~MSR_SINGLESTEP;
				regs->msr |= kcb->kprobe_saved_msr;
				goto no_kprobe;
			}
			/* We have reentered the kprobe_handler(), since
			 * another probe was hit while within the handler.
			 * We here save the original kprobes variables and
			 * just single step on the instruction of the new probe
			 * without calling any user handlers.
			 */
			save_previous_kprobe(kcb);
			set_current_kprobe(p, regs, kcb);
			kcb->kprobe_saved_msr = regs->msr;
			kprobes_inc_nmissed_count(p);
			prepare_singlestep(p, regs);
			kcb->kprobe_status = KPROBE_REENTER;
			return 1;
		} else {
			if (*addr != BREAKPOINT_INSTRUCTION) {
				/* If trap variant, then it belongs not to us */
				kprobe_opcode_t cur_insn = *addr;
				if (is_trap(cur_insn))
		       			goto no_kprobe;
				/* The breakpoint instruction was removed by
				 * another cpu right after we hit, no further
				 * handling of this interrupt is appropriate
				 */
				ret = 1;
				goto no_kprobe;
			}
			p = __get_cpu_var(current_kprobe);
			if (p->break_handler && p->break_handler(p, regs)) {
				goto ss_probe;
			}
		}
		goto no_kprobe;
	}

	p = get_kprobe(addr);
	if (!p) {
		if (*addr != BREAKPOINT_INSTRUCTION) {
			/*
			 * PowerPC has multiple variants of the "trap"
			 * instruction. If the current instruction is a
			 * trap variant, it could belong to someone else
			 */
			kprobe_opcode_t cur_insn = *addr;
			if (is_trap(cur_insn))
		       		goto no_kprobe;
			/*
			 * The breakpoint instruction was removed right
			 * after we hit it.  Another cpu has removed
			 * either a probepoint or a debugger breakpoint
			 * at this address.  In either case, no further
			 * handling of this interrupt is appropriate.
			 */
			ret = 1;
		}
		/* Not one of ours: let kernel handle it */
		goto no_kprobe;
	}

	kcb->kprobe_status = KPROBE_HIT_ACTIVE;
	set_current_kprobe(p, regs, kcb);
	if (p->pre_handler && p->pre_handler(p, regs))
		/* handler has already set things up, so skip ss setup */
		return 1;

ss_probe:
	if (p->ainsn.boostable >= 0) {
		unsigned int insn = *p->ainsn.insn;

		/* regs->nip is also adjusted if emulate_step returns 1 */
		ret = emulate_step(regs, insn);
		if (ret > 0) {
			/*
			 * Once this instruction has been boosted
			 * successfully, set the boostable flag
			 */
			if (unlikely(p->ainsn.boostable == 0))
				p->ainsn.boostable = 1;

			if (p->post_handler)
				p->post_handler(p, regs, 0);

			kcb->kprobe_status = KPROBE_HIT_SSDONE;
			reset_current_kprobe();
			preempt_enable_no_resched();
			return 1;
		} else if (ret < 0) {
			/*
			 * We don't allow kprobes on mtmsr(d)/rfi(d), etc.
			 * So, we should never get here... but, its still
			 * good to catch them, just in case...
			 */
			printk("Can't step on instruction %x\n", insn);
			BUG();
		} else if (ret == 0)
			/* This instruction can't be boosted */
			p->ainsn.boostable = -1;
	}
	prepare_singlestep(p, regs);
	kcb->kprobe_status = KPROBE_HIT_SS;
	return 1;

no_kprobe:
	preempt_enable_no_resched();
	return ret;
}

/*
 * Function return probe trampoline:
 * 	- init_kprobes() establishes a probepoint here
 * 	- When the probed function returns, this probe
 * 		causes the handlers to fire
 */
static void __used kretprobe_trampoline_holder(void)
{
	asm volatile(".global kretprobe_trampoline\n"
			"kretprobe_trampoline:\n"
			"nop\n");
}

/*
 * Called when the probe at kretprobe trampoline is hit
 */
static int __kprobes trampoline_probe_handler(struct kprobe *p,
						struct pt_regs *regs)
{
	struct kretprobe_instance *ri = NULL;
	struct hlist_head *head, empty_rp;
	struct hlist_node *node, *tmp;
	unsigned long flags, orig_ret_address = 0;
	unsigned long trampoline_address =(unsigned long)&kretprobe_trampoline;

	INIT_HLIST_HEAD(&empty_rp);
	kretprobe_hash_lock(current, &head, &flags);

	/*
	 * It is possible to have multiple instances associated with a given
	 * task either because an multiple functions in the call path
	 * have a return probe installed on them, and/or more than one return
	 * return probe was registered for a target function.
	 *
	 * We can handle this because:
	 *     - instances are always inserted at the head of the list
	 *     - when multiple return probes are registered for the same
	 *       function, the first instance's ret_addr will point to the
	 *       real return address, and all the rest will point to
	 *       kretprobe_trampoline
	 */
	hlist_for_each_entry_safe(ri, node, tmp, head, hlist) {
		if (ri->task != current)
			/* another task is sharing our hash bucket */
			continue;

		if (ri->rp && ri->rp->handler)
			ri->rp->handler(ri, regs);

		orig_ret_address = (unsigned long)ri->ret_addr;
		recycle_rp_inst(ri, &empty_rp);

		if (orig_ret_address != trampoline_address)
			/*
			 * This is the real return address. Any other
			 * instances associated with this task are for
			 * other calls deeper on the call stack
			 */
			break;
	}

	kretprobe_assert(ri, orig_ret_address, trampoline_address);
	regs->nip = orig_ret_address;

	reset_current_kprobe();
	kretprobe_hash_unlock(current, &flags);
	preempt_enable_no_resched();

	hlist_for_each_entry_safe(ri, node, tmp, &empty_rp, hlist) {
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

/*
 * Called after single-stepping.  p->addr is the address of the
 * instruction whose first byte has been replaced by the "breakpoint"
 * instruction.  To avoid the SMP problems that can occur when we
 * temporarily put back the original opcode to single-step, we
 * single-stepped a copy of the instruction.  The address of this
 * copy is p->ainsn.insn.
 */
static void __kprobes resume_execution(struct kprobe *p, struct pt_regs *regs)
{
	int ret;
	unsigned int insn = *p->ainsn.insn;

	regs->nip = (unsigned long)p->addr;
	ret = emulate_step(regs, insn);
	if (ret == 0)
		regs->nip = (unsigned long)p->addr + 4;
}

static int __kprobes post_kprobe_handler(struct pt_regs *regs)
{
	struct kprobe *cur = kprobe_running();
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	if (!cur)
		return 0;

	/* make sure we got here for instruction we have a kprobe on */
	if (((unsigned long)cur->ainsn.insn + 4) != regs->nip)
		return 0;

	if ((kcb->kprobe_status != KPROBE_REENTER) && cur->post_handler) {
		kcb->kprobe_status = KPROBE_HIT_SSDONE;
		cur->post_handler(cur, regs, 0);
	}

	resume_execution(cur, regs);
	regs->msr |= kcb->kprobe_saved_msr;

	/*Restore back the original saved kprobes variables and continue. */
	if (kcb->kprobe_status == KPROBE_REENTER) {
		restore_previous_kprobe(kcb);
		goto out;
	}
	reset_current_kprobe();
out:
	preempt_enable_no_resched();

	/*
	 * if somebody else is singlestepping across a probe point, msr
	 * will have DE/SE set, in which case, continue the remaining processing
	 * of do_debug, as if this is not a probe hit.
	 */
	if (regs->msr & MSR_SINGLESTEP)
		return 0;

	return 1;
}

int __kprobes kprobe_fault_handler(struct pt_regs *regs, int trapnr)
{
	struct kprobe *cur = kprobe_running();
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	const struct exception_table_entry *entry;

	switch(kcb->kprobe_status) {
	case KPROBE_HIT_SS:
	case KPROBE_REENTER:
		/*
		 * We are here because the instruction being single
		 * stepped caused a page fault. We reset the current
		 * kprobe and the nip points back to the probe address
		 * and allow the page fault handler to continue as a
		 * normal page fault.
		 */
		regs->nip = (unsigned long)cur->addr;
		regs->msr &= ~MSR_SINGLESTEP; /* Turn off 'trace' bits */
		regs->msr |= kcb->kprobe_saved_msr;
		if (kcb->kprobe_status == KPROBE_REENTER)
			restore_previous_kprobe(kcb);
		else
			reset_current_kprobe();
		preempt_enable_no_resched();
		break;
	case KPROBE_HIT_ACTIVE:
	case KPROBE_HIT_SSDONE:
		/*
		 * We increment the nmissed count for accounting,
		 * we can also use npre/npostfault count for accouting
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
		if (cur->fault_handler && cur->fault_handler(cur, regs, trapnr))
			return 1;

		/*
		 * In case the user-specified fault handler returned
		 * zero, try to fix up.
		 */
		if ((entry = search_exception_tables(regs->nip)) != NULL) {
			regs->nip = entry->fixup;
			return 1;
		}

		/*
		 * fixup_exception() could not handle it,
		 * Let do_page_fault() fix it.
		 */
		break;
	default:
		break;
	}
	return 0;
}

/*
 * Wrapper routine to for handling exceptions.
 */
int __kprobes kprobe_exceptions_notify(struct notifier_block *self,
				       unsigned long val, void *data)
{
	struct die_args *args = (struct die_args *)data;
	int ret = NOTIFY_DONE;

	if (args->regs && user_mode(args->regs))
		return ret;

	switch (val) {
	case DIE_BPT:
		if (kprobe_handler(args->regs))
			ret = NOTIFY_STOP;
		break;
	case DIE_SSTEP:
		if (post_kprobe_handler(args->regs))
			ret = NOTIFY_STOP;
		break;
	default:
		break;
	}
	return ret;
}

#ifdef CONFIG_PPC64
unsigned long arch_deref_entry_point(void *entry)
{
	return ((func_descr_t *)entry)->entry;
}
#endif

int __kprobes setjmp_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct jprobe *jp = container_of(p, struct jprobe, kp);
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	memcpy(&kcb->jprobe_saved_regs, regs, sizeof(struct pt_regs));

	/* setup return addr to the jprobe handler routine */
	regs->nip = arch_deref_entry_point(jp->entry);
#ifdef CONFIG_PPC64
	regs->gpr[2] = (unsigned long)(((func_descr_t *)jp->entry)->toc);
#endif

	return 1;
}

void __used __kprobes jprobe_return(void)
{
	asm volatile("trap" ::: "memory");
}

static void __used __kprobes jprobe_return_end(void)
{
};

int __kprobes longjmp_break_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	/*
	 * FIXME - we should ideally be validating that we got here 'cos
	 * of the "trap" in jprobe_return() above, before restoring the
	 * saved regs...
	 */
	memcpy(regs, &kcb->jprobe_saved_regs, sizeof(struct pt_regs));
	preempt_enable_no_resched();
	return 1;
}

static struct kprobe trampoline_p = {
	.addr = (kprobe_opcode_t *) &kretprobe_trampoline,
	.pre_handler = trampoline_probe_handler
};

int __init arch_init_kprobes(void)
{
	return register_kprobe(&trampoline_p);
}

int __kprobes arch_trampoline_kprobe(struct kprobe *p)
{
	if (p->addr == (kprobe_opcode_t *)&kretprobe_trampoline)
		return 1;

	return 0;
}
