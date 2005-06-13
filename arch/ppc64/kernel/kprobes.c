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
#include <asm/kdebug.h>
#include <asm/sstep.h>

/* kprobe_status settings */
#define KPROBE_HIT_ACTIVE	0x00000001
#define KPROBE_HIT_SS		0x00000002

static struct kprobe *current_kprobe;
static unsigned long kprobe_status, kprobe_saved_msr;
static struct pt_regs jprobe_saved_regs;

int arch_prepare_kprobe(struct kprobe *p)
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
	return ret;
}

void arch_copy_kprobe(struct kprobe *p)
{
	memcpy(p->ainsn.insn, p->addr, MAX_INSN_SIZE * sizeof(kprobe_opcode_t));
}

void arch_remove_kprobe(struct kprobe *p)
{
}

static inline void disarm_kprobe(struct kprobe *p, struct pt_regs *regs)
{
	*p->addr = p->opcode;
	regs->nip = (unsigned long)p->addr;
}

static inline void prepare_singlestep(struct kprobe *p, struct pt_regs *regs)
{
	regs->msr |= MSR_SE;
	/*single step inline if it a breakpoint instruction*/
	if (p->opcode == BREAKPOINT_INSTRUCTION)
		regs->nip = (unsigned long)p->addr;
	else
		regs->nip = (unsigned long)&p->ainsn.insn;
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
			if (kprobe_status == KPROBE_HIT_SS) {
				regs->msr &= ~MSR_SE;
				regs->msr |= kprobe_saved_msr;
				unlock_kprobes();
				goto no_kprobe;
			}
			disarm_kprobe(p, regs);
			ret = 1;
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
			if (IS_TW(cur_insn) || IS_TD(cur_insn) ||
					IS_TWI(cur_insn) || IS_TDI(cur_insn))
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
 * Called after single-stepping.  p->addr is the address of the
 * instruction whose first byte has been replaced by the "breakpoint"
 * instruction.  To avoid the SMP problems that can occur when we
 * temporarily put back the original opcode to single-step, we
 * single-stepped a copy of the instruction.  The address of this
 * copy is p->ainsn.insn.
 */
static void resume_execution(struct kprobe *p, struct pt_regs *regs)
{
	int ret;

	regs->nip = (unsigned long)p->addr;
	ret = emulate_step(regs, p->ainsn.insn[0]);
	if (ret == 0)
		regs->nip = (unsigned long)p->addr + 4;
}

static inline int post_kprobe_handler(struct pt_regs *regs)
{
	if (!kprobe_running())
		return 0;

	if (current_kprobe->post_handler)
		current_kprobe->post_handler(current_kprobe, regs, 0);

	resume_execution(current_kprobe, regs);
	regs->msr |= kprobe_saved_msr;

	unlock_kprobes();
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
int kprobe_exceptions_notify(struct notifier_block *self, unsigned long val,
			     void *data)
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
	preempt_enable();
	return ret;
}

int setjmp_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct jprobe *jp = container_of(p, struct jprobe, kp);

	memcpy(&jprobe_saved_regs, regs, sizeof(struct pt_regs));

	/* setup return addr to the jprobe handler routine */
	regs->nip = (unsigned long)(((func_descr_t *)jp->entry)->entry);
	regs->gpr[2] = (unsigned long)(((func_descr_t *)jp->entry)->toc);

	return 1;
}

void jprobe_return(void)
{
	asm volatile("trap" ::: "memory");
}

void jprobe_return_end(void)
{
};

int longjmp_break_handler(struct kprobe *p, struct pt_regs *regs)
{
	/*
	 * FIXME - we should ideally be validating that we got here 'cos
	 * of the "trap" in jprobe_return() above, before restoring the
	 * saved regs...
	 */
	memcpy(regs, &jprobe_saved_regs, sizeof(struct pt_regs));
	return 1;
}
