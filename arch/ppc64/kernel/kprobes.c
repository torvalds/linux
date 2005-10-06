/*
 *  Kernel Probes (KProbes)
 *  arch/ppc64/kernel/kprobes.c
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

#include <linux/config.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/spinlock.h>
#include <linux/preempt.h>
#include <asm/cacheflush.h>
#include <asm/kdebug.h>
#include <asm/sstep.h>

static DECLARE_MUTEX(kprobe_mutex);

static struct kprobe *current_kprobe;
static unsigned long kprobe_status, kprobe_saved_msr;
static struct kprobe *kprobe_prev;
static unsigned long kprobe_status_prev, kprobe_saved_msr_prev;
static struct pt_regs jprobe_saved_regs;

int __kprobes arch_prepare_kprobe(struct kprobe *p)
{
	int ret = 0;
	kprobe_opcode_t insn = *p->addr;

	if ((unsigned long)p->addr & 0x03) {
		printk("Attempt to register kprobe at an unaligned address\n");
		ret = -EINVAL;
	} else if (IS_MTMSRD(insn) || IS_RFID(insn)) {
		printk("Cannot register a kprobe on rfid or mtmsrd\n");
		ret = -EINVAL;
	}

	/* insn must be on a special executable page on ppc64 */
	if (!ret) {
		down(&kprobe_mutex);
		p->ainsn.insn = get_insn_slot();
		up(&kprobe_mutex);
		if (!p->ainsn.insn)
			ret = -ENOMEM;
	}
	return ret;
}

void __kprobes arch_copy_kprobe(struct kprobe *p)
{
	memcpy(p->ainsn.insn, p->addr, MAX_INSN_SIZE * sizeof(kprobe_opcode_t));
	p->opcode = *p->addr;
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
	down(&kprobe_mutex);
	free_insn_slot(p->ainsn.insn);
	up(&kprobe_mutex);
}

static inline void prepare_singlestep(struct kprobe *p, struct pt_regs *regs)
{
	kprobe_opcode_t insn = *p->ainsn.insn;

	regs->msr |= MSR_SE;

	/* single step inline if it is a trap variant */
	if (is_trap(insn))
		regs->nip = (unsigned long)p->addr;
	else
		regs->nip = (unsigned long)p->ainsn.insn;
}

static inline void save_previous_kprobe(void)
{
	kprobe_prev = current_kprobe;
	kprobe_status_prev = kprobe_status;
	kprobe_saved_msr_prev = kprobe_saved_msr;
}

static inline void restore_previous_kprobe(void)
{
	current_kprobe = kprobe_prev;
	kprobe_status = kprobe_status_prev;
	kprobe_saved_msr = kprobe_saved_msr_prev;
}

void __kprobes arch_prepare_kretprobe(struct kretprobe *rp,
				      struct pt_regs *regs)
{
	struct kretprobe_instance *ri;

	if ((ri = get_free_rp_inst(rp)) != NULL) {
		ri->rp = rp;
		ri->task = current;
		ri->ret_addr = (kprobe_opcode_t *)regs->link;

		/* Replace the return addr with trampoline addr */
		regs->link = (unsigned long)kretprobe_trampoline;
		add_rp_inst(ri);
	} else {
		rp->nmissed++;
	}
}

static inline int kprobe_handler(struct pt_regs *regs)
{
	struct kprobe *p;
	int ret = 0;
	unsigned int *addr = (unsigned int *)regs->nip;

	/* Check we're not actually recursing */
	if (kprobe_running()) {
		/* We *are* holding lock here, so this is safe.
		   Disarm the probe we just hit, and ignore it. */
		p = get_kprobe(addr);
		if (p) {
			kprobe_opcode_t insn = *p->ainsn.insn;
			if (kprobe_status == KPROBE_HIT_SS &&
					is_trap(insn)) {
				regs->msr &= ~MSR_SE;
				regs->msr |= kprobe_saved_msr;
				unlock_kprobes();
				goto no_kprobe;
			}
			/* We have reentered the kprobe_handler(), since
			 * another probe was hit while within the handler.
			 * We here save the original kprobes variables and
			 * just single step on the instruction of the new probe
			 * without calling any user handlers.
			 */
			save_previous_kprobe();
			current_kprobe = p;
			kprobe_saved_msr = regs->msr;
			p->nmissed++;
			prepare_singlestep(p, regs);
			kprobe_status = KPROBE_REENTER;
			return 1;
		} else {
			p = current_kprobe;
			if (p->break_handler && p->break_handler(p, regs)) {
				goto ss_probe;
			}
		}
		/* If it's not ours, can't be delete race, (we hold lock). */
		goto no_kprobe;
	}

	lock_kprobes();
	p = get_kprobe(addr);
	if (!p) {
		unlock_kprobes();
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

	kprobe_status = KPROBE_HIT_ACTIVE;
	current_kprobe = p;
	kprobe_saved_msr = regs->msr;
	if (p->pre_handler && p->pre_handler(p, regs))
		/* handler has already set things up, so skip ss setup */
		return 1;

ss_probe:
	prepare_singlestep(p, regs);
	kprobe_status = KPROBE_HIT_SS;
	/*
	 * This preempt_disable() matches the preempt_enable_no_resched()
	 * in post_kprobe_handler().
	 */
	preempt_disable();
	return 1;

no_kprobe:
	return ret;
}

/*
 * Function return probe trampoline:
 * 	- init_kprobes() establishes a probepoint here
 * 	- When the probed function returns, this probe
 * 		causes the handlers to fire
 */
void kretprobe_trampoline_holder(void)
{
	asm volatile(".global kretprobe_trampoline\n"
			"kretprobe_trampoline:\n"
			"nop\n");
}

/*
 * Called when the probe at kretprobe trampoline is hit
 */
int __kprobes trampoline_probe_handler(struct kprobe *p, struct pt_regs *regs)
{
        struct kretprobe_instance *ri = NULL;
        struct hlist_head *head;
        struct hlist_node *node, *tmp;
	unsigned long orig_ret_address = 0;
	unsigned long trampoline_address =(unsigned long)&kretprobe_trampoline;

        head = kretprobe_inst_table_head(current);

	/*
	 * It is possible to have multiple instances associated with a given
	 * task either because an multiple functions in the call path
	 * have a return probe installed on them, and/or more then one return
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
		recycle_rp_inst(ri);

		if (orig_ret_address != trampoline_address)
			/*
			 * This is the real return address. Any other
			 * instances associated with this task are for
			 * other calls deeper on the call stack
			 */
			break;
	}

	BUG_ON(!orig_ret_address || (orig_ret_address == trampoline_address));
	regs->nip = orig_ret_address;

	unlock_kprobes();

        /*
         * By returning a non-zero value, we are telling
         * kprobe_handler() that we have handled unlocking
         * and re-enabling preemption.
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

static inline int post_kprobe_handler(struct pt_regs *regs)
{
	if (!kprobe_running())
		return 0;

	if ((kprobe_status != KPROBE_REENTER) && current_kprobe->post_handler) {
		kprobe_status = KPROBE_HIT_SSDONE;
		current_kprobe->post_handler(current_kprobe, regs, 0);
	}

	resume_execution(current_kprobe, regs);
	regs->msr |= kprobe_saved_msr;

	/*Restore back the original saved kprobes variables and continue. */
	if (kprobe_status == KPROBE_REENTER) {
		restore_previous_kprobe();
		goto out;
	}
	unlock_kprobes();
out:
	preempt_enable_no_resched();

	/*
	 * if somebody else is singlestepping across a probe point, msr
	 * will have SE set, in which case, continue the remaining processing
	 * of do_debug, as if this is not a probe hit.
	 */
	if (regs->msr & MSR_SE)
		return 0;

	return 1;
}

/* Interrupts disabled, kprobe_lock held. */
static inline int kprobe_fault_handler(struct pt_regs *regs, int trapnr)
{
	if (current_kprobe->fault_handler
	    && current_kprobe->fault_handler(current_kprobe, regs, trapnr))
		return 1;

	if (kprobe_status & KPROBE_HIT_SS) {
		resume_execution(current_kprobe, regs);
		regs->msr &= ~MSR_SE;
		regs->msr |= kprobe_saved_msr;

		unlock_kprobes();
		preempt_enable_no_resched();
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

	/*
	 * Interrupts are not disabled here.  We need to disable
	 * preemption, because kprobe_running() uses smp_processor_id().
	 */
	preempt_disable();
	switch (val) {
	case DIE_BPT:
		if (kprobe_handler(args->regs))
			ret = NOTIFY_STOP;
		break;
	case DIE_SSTEP:
		if (post_kprobe_handler(args->regs))
			ret = NOTIFY_STOP;
		break;
	case DIE_GPF:
	case DIE_PAGE_FAULT:
		if (kprobe_running() &&
		    kprobe_fault_handler(args->regs, args->trapnr))
			ret = NOTIFY_STOP;
		break;
	default:
		break;
	}
	preempt_enable_no_resched();
	return ret;
}

int __kprobes setjmp_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct jprobe *jp = container_of(p, struct jprobe, kp);

	memcpy(&jprobe_saved_regs, regs, sizeof(struct pt_regs));

	/* setup return addr to the jprobe handler routine */
	regs->nip = (unsigned long)(((func_descr_t *)jp->entry)->entry);
	regs->gpr[2] = (unsigned long)(((func_descr_t *)jp->entry)->toc);

	return 1;
}

void __kprobes jprobe_return(void)
{
	asm volatile("trap" ::: "memory");
}

void __kprobes jprobe_return_end(void)
{
};

int __kprobes longjmp_break_handler(struct kprobe *p, struct pt_regs *regs)
{
	/*
	 * FIXME - we should ideally be validating that we got here 'cos
	 * of the "trap" in jprobe_return() above, before restoring the
	 * saved regs...
	 */
	memcpy(regs, &jprobe_saved_regs, sizeof(struct pt_regs));
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
