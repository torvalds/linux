/*
 *  Kernel Probes (KProbes)
 *  arch/i386/kernel/kprobes.c
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
 */

#include <linux/config.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/spinlock.h>
#include <linux/preempt.h>
#include <asm/kdebug.h>
#include <asm/desc.h>

/* kprobe_status settings */
#define KPROBE_HIT_ACTIVE	0x00000001
#define KPROBE_HIT_SS		0x00000002

static struct kprobe *current_kprobe;
static unsigned long kprobe_status, kprobe_old_eflags, kprobe_saved_eflags;
static struct pt_regs jprobe_saved_regs;
static long *jprobe_saved_esp;
/* copy of the kernel stack at the probe fire time */
static kprobe_opcode_t jprobes_stack[MAX_STACK_SIZE];
void jprobe_return_end(void);

/*
 * returns non-zero if opcode modifies the interrupt flag.
 */
static inline int is_IF_modifier(kprobe_opcode_t opcode)
{
	switch (opcode) {
	case 0xfa:		/* cli */
	case 0xfb:		/* sti */
	case 0xcf:		/* iret/iretd */
	case 0x9d:		/* popf/popfd */
		return 1;
	}
	return 0;
}

int arch_prepare_kprobe(struct kprobe *p)
{
	return 0;
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
	regs->eip = (unsigned long)p->addr;
}

static inline void prepare_singlestep(struct kprobe *p, struct pt_regs *regs)
{
	regs->eflags |= TF_MASK;
	regs->eflags &= ~IF_MASK;
	/*single step inline if the instruction is an int3*/
	if (p->opcode == BREAKPOINT_INSTRUCTION)
		regs->eip = (unsigned long)p->addr;
	else
		regs->eip = (unsigned long)&p->ainsn.insn;
}

/*
 * Interrupts are disabled on entry as trap3 is an interrupt gate and they
 * remain disabled thorough out this function.
 */
static int kprobe_handler(struct pt_regs *regs)
{
	struct kprobe *p;
	int ret = 0;
	kprobe_opcode_t *addr = NULL;
	unsigned long *lp;

	/* We're in an interrupt, but this is clear and BUG()-safe. */
	preempt_disable();
	/* Check if the application is using LDT entry for its code segment and
	 * calculate the address by reading the base address from the LDT entry.
	 */
	if ((regs->xcs & 4) && (current->mm)) {
		lp = (unsigned long *) ((unsigned long)((regs->xcs >> 3) * 8)
					+ (char *) current->mm->context.ldt);
		addr = (kprobe_opcode_t *) (get_desc_base(lp) + regs->eip -
						sizeof(kprobe_opcode_t));
	} else {
		addr = (kprobe_opcode_t *)(regs->eip - sizeof(kprobe_opcode_t));
	}
	/* Check we're not actually recursing */
	if (kprobe_running()) {
		/* We *are* holding lock here, so this is safe.
		   Disarm the probe we just hit, and ignore it. */
		p = get_kprobe(addr);
		if (p) {
			if (kprobe_status == KPROBE_HIT_SS) {
				regs->eflags &= ~TF_MASK;
				regs->eflags |= kprobe_saved_eflags;
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
		if (regs->eflags & VM_MASK) {
			/* We are in virtual-8086 mode. Return 0 */
			goto no_kprobe;
		}

		if (*addr != BREAKPOINT_INSTRUCTION) {
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
	kprobe_saved_eflags = kprobe_old_eflags
	    = (regs->eflags & (TF_MASK | IF_MASK));
	if (is_IF_modifier(p->opcode))
		kprobe_saved_eflags &= ~IF_MASK;

	if (p->pre_handler && p->pre_handler(p, regs))
		/* handler has already set things up, so skip ss setup */
		return 1;

ss_probe:
	prepare_singlestep(p, regs);
	kprobe_status = KPROBE_HIT_SS;
	return 1;

no_kprobe:
	preempt_enable_no_resched();
	return ret;
}

/*
 * Called after single-stepping.  p->addr is the address of the
 * instruction whose first byte has been replaced by the "int 3"
 * instruction.  To avoid the SMP problems that can occur when we
 * temporarily put back the original opcode to single-step, we
 * single-stepped a copy of the instruction.  The address of this
 * copy is p->ainsn.insn.
 *
 * This function prepares to return from the post-single-step
 * interrupt.  We have to fix up the stack as follows:
 *
 * 0) Except in the case of absolute or indirect jump or call instructions,
 * the new eip is relative to the copied instruction.  We need to make
 * it relative to the original instruction.
 *
 * 1) If the single-stepped instruction was pushfl, then the TF and IF
 * flags are set in the just-pushed eflags, and may need to be cleared.
 *
 * 2) If the single-stepped instruction was a call, the return address
 * that is atop the stack is the address following the copied instruction.
 * We need to make it the address following the original instruction.
 */
static void resume_execution(struct kprobe *p, struct pt_regs *regs)
{
	unsigned long *tos = (unsigned long *)&regs->esp;
	unsigned long next_eip = 0;
	unsigned long copy_eip = (unsigned long)&p->ainsn.insn;
	unsigned long orig_eip = (unsigned long)p->addr;

	switch (p->ainsn.insn[0]) {
	case 0x9c:		/* pushfl */
		*tos &= ~(TF_MASK | IF_MASK);
		*tos |= kprobe_old_eflags;
		break;
	case 0xc3:		/* ret/lret */
	case 0xcb:
	case 0xc2:
	case 0xca:
		regs->eflags &= ~TF_MASK;
		/* eip is already adjusted, no more changes required*/
		return;
	case 0xe8:		/* call relative - Fix return addr */
		*tos = orig_eip + (*tos - copy_eip);
		break;
	case 0xff:
		if ((p->ainsn.insn[1] & 0x30) == 0x10) {
			/* call absolute, indirect */
			/* Fix return addr; eip is correct. */
			next_eip = regs->eip;
			*tos = orig_eip + (*tos - copy_eip);
		} else if (((p->ainsn.insn[1] & 0x31) == 0x20) ||	/* jmp near, absolute indirect */
			   ((p->ainsn.insn[1] & 0x31) == 0x21)) {	/* jmp far, absolute indirect */
			/* eip is correct. */
			next_eip = regs->eip;
		}
		break;
	case 0xea:		/* jmp absolute -- eip is correct */
		next_eip = regs->eip;
		break;
	default:
		break;
	}

	regs->eflags &= ~TF_MASK;
	if (next_eip) {
		regs->eip = next_eip;
	} else {
		regs->eip = orig_eip + (regs->eip - copy_eip);
	}
}

/*
 * Interrupts are disabled on entry as trap1 is an interrupt gate and they
 * remain disabled thoroughout this function.  And we hold kprobe lock.
 */
static inline int post_kprobe_handler(struct pt_regs *regs)
{
	if (!kprobe_running())
		return 0;

	if (current_kprobe->post_handler)
		current_kprobe->post_handler(current_kprobe, regs, 0);

	resume_execution(current_kprobe, regs);
	regs->eflags |= kprobe_saved_eflags;

	unlock_kprobes();
	preempt_enable_no_resched();

	/*
	 * if somebody else is singlestepping across a probe point, eflags
	 * will have TF set, in which case, continue the remaining processing
	 * of do_debug, as if this is not a probe hit.
	 */
	if (regs->eflags & TF_MASK)
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
		regs->eflags |= kprobe_old_eflags;

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
	switch (val) {
	case DIE_INT3:
		if (kprobe_handler(args->regs))
			return NOTIFY_STOP;
		break;
	case DIE_DEBUG:
		if (post_kprobe_handler(args->regs))
			return NOTIFY_STOP;
		break;
	case DIE_GPF:
		if (kprobe_running() &&
		    kprobe_fault_handler(args->regs, args->trapnr))
			return NOTIFY_STOP;
		break;
	case DIE_PAGE_FAULT:
		if (kprobe_running() &&
		    kprobe_fault_handler(args->regs, args->trapnr))
			return NOTIFY_STOP;
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

int setjmp_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct jprobe *jp = container_of(p, struct jprobe, kp);
	unsigned long addr;

	jprobe_saved_regs = *regs;
	jprobe_saved_esp = &regs->esp;
	addr = (unsigned long)jprobe_saved_esp;

	/*
	 * TBD: As Linus pointed out, gcc assumes that the callee
	 * owns the argument space and could overwrite it, e.g.
	 * tailcall optimization. So, to be absolutely safe
	 * we also save and restore enough stack bytes to cover
	 * the argument area.
	 */
	memcpy(jprobes_stack, (kprobe_opcode_t *) addr, MIN_STACK_SIZE(addr));
	regs->eflags &= ~IF_MASK;
	regs->eip = (unsigned long)(jp->entry);
	return 1;
}

void jprobe_return(void)
{
	preempt_enable_no_resched();
	asm volatile ("       xchgl   %%ebx,%%esp     \n"
		      "       int3			\n"
		      "       .globl jprobe_return_end	\n"
		      "       jprobe_return_end:	\n"
		      "       nop			\n"::"b"
		      (jprobe_saved_esp):"memory");
}

int longjmp_break_handler(struct kprobe *p, struct pt_regs *regs)
{
	u8 *addr = (u8 *) (regs->eip - 1);
	unsigned long stack_addr = (unsigned long)jprobe_saved_esp;
	struct jprobe *jp = container_of(p, struct jprobe, kp);

	if ((addr > (u8 *) jprobe_return) && (addr < (u8 *) jprobe_return_end)) {
		if (&regs->esp != jprobe_saved_esp) {
			struct pt_regs *saved_regs =
			    container_of(jprobe_saved_esp, struct pt_regs, esp);
			printk("current esp %p does not match saved esp %p\n",
			       &regs->esp, jprobe_saved_esp);
			printk("Saved registers for jprobe %p\n", jp);
			show_registers(saved_regs);
			printk("Current registers\n");
			show_registers(regs);
			BUG();
		}
		*regs = jprobe_saved_regs;
		memcpy((kprobe_opcode_t *) stack_addr, jprobes_stack,
		       MIN_STACK_SIZE(stack_addr));
		return 1;
	}
	return 0;
}
