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
 * 2005-May	Hien Nguyen <hien@us.ibm.com>, Jim Keniston
 *		<jkenisto@us.ibm.com> and Prasanna S Panchamukhi
 *		<prasanna@in.ibm.com> added function-return probes.
 */

#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/preempt.h>
#include <linux/kdebug.h>
#include <asm/cacheflush.h>
#include <asm/desc.h>
#include <asm/uaccess.h>
#include <asm/alternative.h>

void jprobe_return_end(void);

DEFINE_PER_CPU(struct kprobe *, current_kprobe) = NULL;
DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

struct kretprobe_blackpoint kretprobe_blacklist[] = {
	{"__switch_to", }, /* This function switches only current task, but
			     doesn't switch kernel stack.*/
	{NULL, NULL}	/* Terminator */
};
const int kretprobe_blacklist_size = ARRAY_SIZE(kretprobe_blacklist);

/* insert a jmp code */
static __always_inline void set_jmp_op(void *from, void *to)
{
	struct __arch_jmp_op {
		char op;
		long raddr;
	} __attribute__((packed)) *jop;
	jop = (struct __arch_jmp_op *)from;
	jop->raddr = (long)(to) - ((long)(from) + 5);
	jop->op = RELATIVEJUMP_INSTRUCTION;
}

/*
 * returns non-zero if opcodes can be boosted.
 */
static __always_inline int can_boost(kprobe_opcode_t *opcodes)
{
#define W(row,b0,b1,b2,b3,b4,b5,b6,b7,b8,b9,ba,bb,bc,bd,be,bf)		      \
	(((b0##UL << 0x0)|(b1##UL << 0x1)|(b2##UL << 0x2)|(b3##UL << 0x3) |   \
	  (b4##UL << 0x4)|(b5##UL << 0x5)|(b6##UL << 0x6)|(b7##UL << 0x7) |   \
	  (b8##UL << 0x8)|(b9##UL << 0x9)|(ba##UL << 0xa)|(bb##UL << 0xb) |   \
	  (bc##UL << 0xc)|(bd##UL << 0xd)|(be##UL << 0xe)|(bf##UL << 0xf))    \
	 << (row % 32))
	/*
	 * Undefined/reserved opcodes, conditional jump, Opcode Extension
	 * Groups, and some special opcodes can not be boost.
	 */
	static const unsigned long twobyte_is_boostable[256 / 32] = {
		/*      0 1 2 3 4 5 6 7 8 9 a b c d e f         */
		/*      -------------------------------         */
		W(0x00, 0,0,1,1,0,0,1,0,1,1,0,0,0,0,0,0)| /* 00 */
		W(0x10, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0), /* 10 */
		W(0x20, 1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0)| /* 20 */
		W(0x30, 0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0), /* 30 */
		W(0x40, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1)| /* 40 */
		W(0x50, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0), /* 50 */
		W(0x60, 1,1,1,1,1,1,1,1,1,1,1,1,0,0,1,1)| /* 60 */
		W(0x70, 0,0,0,0,1,1,1,1,0,0,0,0,0,0,1,1), /* 70 */
		W(0x80, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)| /* 80 */
		W(0x90, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1), /* 90 */
		W(0xa0, 1,1,0,1,1,1,0,0,1,1,0,1,1,1,0,1)| /* a0 */
		W(0xb0, 1,1,1,1,1,1,1,1,0,0,0,1,1,1,1,1), /* b0 */
		W(0xc0, 1,1,0,0,0,0,0,0,1,1,1,1,1,1,1,1)| /* c0 */
		W(0xd0, 0,1,1,1,0,1,0,0,1,1,0,1,1,1,0,1), /* d0 */
		W(0xe0, 0,1,1,0,0,1,0,0,1,1,0,1,1,1,0,1)| /* e0 */
		W(0xf0, 0,1,1,1,0,1,0,0,1,1,1,0,1,1,1,0)  /* f0 */
		/*      -------------------------------         */
		/*      0 1 2 3 4 5 6 7 8 9 a b c d e f         */
	};
#undef W
	kprobe_opcode_t opcode;
	kprobe_opcode_t *orig_opcodes = opcodes;
retry:
	if (opcodes - orig_opcodes > MAX_INSN_SIZE - 1)
		return 0;
	opcode = *(opcodes++);

	/* 2nd-byte opcode */
	if (opcode == 0x0f) {
		if (opcodes - orig_opcodes > MAX_INSN_SIZE - 1)
			return 0;
		return test_bit(*opcodes, twobyte_is_boostable);
	}

	switch (opcode & 0xf0) {
	case 0x60:
		if (0x63 < opcode && opcode < 0x67)
			goto retry; /* prefixes */
		/* can't boost Address-size override and bound */
		return (opcode != 0x62 && opcode != 0x67);
	case 0x70:
		return 0; /* can't boost conditional jump */
	case 0xc0:
		/* can't boost software-interruptions */
		return (0xc1 < opcode && opcode < 0xcc) || opcode == 0xcf;
	case 0xd0:
		/* can boost AA* and XLAT */
		return (opcode == 0xd4 || opcode == 0xd5 || opcode == 0xd7);
	case 0xe0:
		/* can boost in/out and absolute jmps */
		return ((opcode & 0x04) || opcode == 0xea);
	case 0xf0:
		if ((opcode & 0x0c) == 0 && opcode != 0xf1)
			goto retry; /* lock/rep(ne) prefix */
		/* clear and set flags can be boost */
		return (opcode == 0xf5 || (0xf7 < opcode && opcode < 0xfe));
	default:
		if (opcode == 0x26 || opcode == 0x36 || opcode == 0x3e)
			goto retry; /* prefixes */
		/* can't boost CS override and call */
		return (opcode != 0x2e && opcode != 0x9a);
	}
}

/*
 * returns non-zero if opcode modifies the interrupt flag.
 */
static int __kprobes is_IF_modifier(kprobe_opcode_t opcode)
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

int __kprobes arch_prepare_kprobe(struct kprobe *p)
{
	/* insn: must be on special executable page on i386. */
	p->ainsn.insn = get_insn_slot();
	if (!p->ainsn.insn)
		return -ENOMEM;

	memcpy(p->ainsn.insn, p->addr, MAX_INSN_SIZE * sizeof(kprobe_opcode_t));
	p->opcode = *p->addr;
	if (can_boost(p->addr)) {
		p->ainsn.boostable = 0;
	} else {
		p->ainsn.boostable = -1;
	}
	return 0;
}

void __kprobes arch_arm_kprobe(struct kprobe *p)
{
	text_poke(p->addr, ((unsigned char []){BREAKPOINT_INSTRUCTION}), 1);
}

void __kprobes arch_disarm_kprobe(struct kprobe *p)
{
	text_poke(p->addr, &p->opcode, 1);
}

void __kprobes arch_remove_kprobe(struct kprobe *p)
{
	mutex_lock(&kprobe_mutex);
	free_insn_slot(p->ainsn.insn, (p->ainsn.boostable == 1));
	mutex_unlock(&kprobe_mutex);
}

static void __kprobes save_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	kcb->prev_kprobe.kp = kprobe_running();
	kcb->prev_kprobe.status = kcb->kprobe_status;
	kcb->prev_kprobe.old_eflags = kcb->kprobe_old_eflags;
	kcb->prev_kprobe.saved_eflags = kcb->kprobe_saved_eflags;
}

static void __kprobes restore_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	__get_cpu_var(current_kprobe) = kcb->prev_kprobe.kp;
	kcb->kprobe_status = kcb->prev_kprobe.status;
	kcb->kprobe_old_eflags = kcb->prev_kprobe.old_eflags;
	kcb->kprobe_saved_eflags = kcb->prev_kprobe.saved_eflags;
}

static void __kprobes set_current_kprobe(struct kprobe *p, struct pt_regs *regs,
				struct kprobe_ctlblk *kcb)
{
	__get_cpu_var(current_kprobe) = p;
	kcb->kprobe_saved_eflags = kcb->kprobe_old_eflags
		= (regs->eflags & (TF_MASK | IF_MASK));
	if (is_IF_modifier(p->opcode))
		kcb->kprobe_saved_eflags &= ~IF_MASK;
}

static void __kprobes prepare_singlestep(struct kprobe *p, struct pt_regs *regs)
{
	regs->eflags |= TF_MASK;
	regs->eflags &= ~IF_MASK;
	/*single step inline if the instruction is an int3*/
	if (p->opcode == BREAKPOINT_INSTRUCTION)
		regs->eip = (unsigned long)p->addr;
	else
		regs->eip = (unsigned long)p->ainsn.insn;
}

/* Called with kretprobe_lock held */
void __kprobes arch_prepare_kretprobe(struct kretprobe_instance *ri,
				      struct pt_regs *regs)
{
	unsigned long *sara = (unsigned long *)&regs->esp;

	ri->ret_addr = (kprobe_opcode_t *) *sara;

	/* Replace the return addr with trampoline addr */
	*sara = (unsigned long) &kretprobe_trampoline;
}

/*
 * Interrupts are disabled on entry as trap3 is an interrupt gate and they
 * remain disabled thorough out this function.
 */
static int __kprobes kprobe_handler(struct pt_regs *regs)
{
	struct kprobe *p;
	int ret = 0;
	kprobe_opcode_t *addr;
	struct kprobe_ctlblk *kcb;

	addr = (kprobe_opcode_t *)(regs->eip - sizeof(kprobe_opcode_t));

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
			if (kcb->kprobe_status == KPROBE_HIT_SS &&
				*p->ainsn.insn == BREAKPOINT_INSTRUCTION) {
				regs->eflags &= ~TF_MASK;
				regs->eflags |= kcb->kprobe_saved_eflags;
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
			kprobes_inc_nmissed_count(p);
			prepare_singlestep(p, regs);
			kcb->kprobe_status = KPROBE_REENTER;
			return 1;
		} else {
			if (*addr != BREAKPOINT_INSTRUCTION) {
			/* The breakpoint instruction was removed by
			 * another cpu right after we hit, no further
			 * handling of this interrupt is appropriate
			 */
				regs->eip -= sizeof(kprobe_opcode_t);
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
			 * The breakpoint instruction was removed right
			 * after we hit it.  Another cpu has removed
			 * either a probepoint or a debugger breakpoint
			 * at this address.  In either case, no further
			 * handling of this interrupt is appropriate.
			 * Back up over the (now missing) int3 and run
			 * the original instruction.
			 */
			regs->eip -= sizeof(kprobe_opcode_t);
			ret = 1;
		}
		/* Not one of ours: let kernel handle it */
		goto no_kprobe;
	}

	set_current_kprobe(p, regs, kcb);
	kcb->kprobe_status = KPROBE_HIT_ACTIVE;

	if (p->pre_handler && p->pre_handler(p, regs))
		/* handler has already set things up, so skip ss setup */
		return 1;

ss_probe:
#if !defined(CONFIG_PREEMPT) || defined(CONFIG_PM)
	if (p->ainsn.boostable == 1 && !p->post_handler){
		/* Boost up -- we can execute copied instructions directly */
		reset_current_kprobe();
		regs->eip = (unsigned long)p->ainsn.insn;
		preempt_enable_no_resched();
		return 1;
	}
#endif
	prepare_singlestep(p, regs);
	kcb->kprobe_status = KPROBE_HIT_SS;
	return 1;

no_kprobe:
	preempt_enable_no_resched();
	return ret;
}

/*
 * For function-return probes, init_kprobes() establishes a probepoint
 * here. When a retprobed function returns, this probe is hit and
 * trampoline_probe_handler() runs, calling the kretprobe's handler.
 */
 void __kprobes kretprobe_trampoline_holder(void)
 {
	asm volatile ( ".global kretprobe_trampoline\n"
			"kretprobe_trampoline: \n"
			"	pushf\n"
			/* skip cs, eip, orig_eax */
			"	subl $12, %esp\n"
			"	pushl %fs\n"
			"	pushl %ds\n"
			"	pushl %es\n"
			"	pushl %eax\n"
			"	pushl %ebp\n"
			"	pushl %edi\n"
			"	pushl %esi\n"
			"	pushl %edx\n"
			"	pushl %ecx\n"
			"	pushl %ebx\n"
			"	movl %esp, %eax\n"
			"	call trampoline_handler\n"
			/* move eflags to cs */
			"	movl 52(%esp), %edx\n"
			"	movl %edx, 48(%esp)\n"
			/* save true return address on eflags */
			"	movl %eax, 52(%esp)\n"
			"	popl %ebx\n"
			"	popl %ecx\n"
			"	popl %edx\n"
			"	popl %esi\n"
			"	popl %edi\n"
			"	popl %ebp\n"
			"	popl %eax\n"
			/* skip eip, orig_eax, es, ds, fs */
			"	addl $20, %esp\n"
			"	popf\n"
			"	ret\n");
}

/*
 * Called from kretprobe_trampoline
 */
fastcall void *__kprobes trampoline_handler(struct pt_regs *regs)
{
	struct kretprobe_instance *ri = NULL;
	struct hlist_head *head, empty_rp;
	struct hlist_node *node, *tmp;
	unsigned long flags, orig_ret_address = 0;
	unsigned long trampoline_address =(unsigned long)&kretprobe_trampoline;

	INIT_HLIST_HEAD(&empty_rp);
	spin_lock_irqsave(&kretprobe_lock, flags);
	head = kretprobe_inst_table_head(current);
	/* fixup registers */
	regs->xcs = __KERNEL_CS | get_kernel_rpl();
	regs->eip = trampoline_address;
	regs->orig_eax = 0xffffffff;

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

		if (ri->rp && ri->rp->handler){
			__get_cpu_var(current_kprobe) = &ri->rp->kp;
			get_kprobe_ctlblk()->kprobe_status = KPROBE_HIT_ACTIVE;
			ri->rp->handler(ri, regs);
			__get_cpu_var(current_kprobe) = NULL;
		}

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
	spin_unlock_irqrestore(&kretprobe_lock, flags);

	hlist_for_each_entry_safe(ri, node, tmp, &empty_rp, hlist) {
		hlist_del(&ri->hlist);
		kfree(ri);
	}
	return (void*)orig_ret_address;
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
 *
 * This function also checks instruction size for preparing direct execution.
 */
static void __kprobes resume_execution(struct kprobe *p,
		struct pt_regs *regs, struct kprobe_ctlblk *kcb)
{
	unsigned long *tos = (unsigned long *)&regs->esp;
	unsigned long copy_eip = (unsigned long)p->ainsn.insn;
	unsigned long orig_eip = (unsigned long)p->addr;

	regs->eflags &= ~TF_MASK;
	switch (p->ainsn.insn[0]) {
	case 0x9c:		/* pushfl */
		*tos &= ~(TF_MASK | IF_MASK);
		*tos |= kcb->kprobe_old_eflags;
		break;
	case 0xc2:		/* iret/ret/lret */
	case 0xc3:
	case 0xca:
	case 0xcb:
	case 0xcf:
	case 0xea:		/* jmp absolute -- eip is correct */
		/* eip is already adjusted, no more changes required */
		p->ainsn.boostable = 1;
		goto no_change;
	case 0xe8:		/* call relative - Fix return addr */
		*tos = orig_eip + (*tos - copy_eip);
		break;
	case 0x9a:		/* call absolute -- same as call absolute, indirect */
		*tos = orig_eip + (*tos - copy_eip);
		goto no_change;
	case 0xff:
		if ((p->ainsn.insn[1] & 0x30) == 0x10) {
			/*
			 * call absolute, indirect
			 * Fix return addr; eip is correct.
			 * But this is not boostable
			 */
			*tos = orig_eip + (*tos - copy_eip);
			goto no_change;
		} else if (((p->ainsn.insn[1] & 0x31) == 0x20) ||	/* jmp near, absolute indirect */
			   ((p->ainsn.insn[1] & 0x31) == 0x21)) {	/* jmp far, absolute indirect */
			/* eip is correct. And this is boostable */
			p->ainsn.boostable = 1;
			goto no_change;
		}
	default:
		break;
	}

	if (p->ainsn.boostable == 0) {
		if ((regs->eip > copy_eip) &&
		    (regs->eip - copy_eip) + 5 < MAX_INSN_SIZE) {
			/*
			 * These instructions can be executed directly if it
			 * jumps back to correct address.
			 */
			set_jmp_op((void *)regs->eip,
				   (void *)orig_eip + (regs->eip - copy_eip));
			p->ainsn.boostable = 1;
		} else {
			p->ainsn.boostable = -1;
		}
	}

	regs->eip = orig_eip + (regs->eip - copy_eip);

no_change:
	return;
}

/*
 * Interrupts are disabled on entry as trap1 is an interrupt gate and they
 * remain disabled thoroughout this function.
 */
static int __kprobes post_kprobe_handler(struct pt_regs *regs)
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
	regs->eflags |= kcb->kprobe_saved_eflags;
	trace_hardirqs_fixup_flags(regs->eflags);

	/*Restore back the original saved kprobes variables and continue. */
	if (kcb->kprobe_status == KPROBE_REENTER) {
		restore_previous_kprobe(kcb);
		goto out;
	}
	reset_current_kprobe();
out:
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

int __kprobes kprobe_fault_handler(struct pt_regs *regs, int trapnr)
{
	struct kprobe *cur = kprobe_running();
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	switch(kcb->kprobe_status) {
	case KPROBE_HIT_SS:
	case KPROBE_REENTER:
		/*
		 * We are here because the instruction being single
		 * stepped caused a page fault. We reset the current
		 * kprobe and the eip points back to the probe address
		 * and allow the page fault handler to continue as a
		 * normal page fault.
		 */
		regs->eip = (unsigned long)cur->addr;
		regs->eflags |= kcb->kprobe_old_eflags;
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
		if (fixup_exception(regs))
			return 1;

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

	if (args->regs && user_mode_vm(args->regs))
		return ret;

	switch (val) {
	case DIE_INT3:
		if (kprobe_handler(args->regs))
			ret = NOTIFY_STOP;
		break;
	case DIE_DEBUG:
		if (post_kprobe_handler(args->regs))
			ret = NOTIFY_STOP;
		break;
	case DIE_GPF:
		/* kprobe_running() needs smp_processor_id() */
		preempt_disable();
		if (kprobe_running() &&
		    kprobe_fault_handler(args->regs, args->trapnr))
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
	unsigned long addr;
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	kcb->jprobe_saved_regs = *regs;
	kcb->jprobe_saved_esp = &regs->esp;
	addr = (unsigned long)(kcb->jprobe_saved_esp);

	/*
	 * TBD: As Linus pointed out, gcc assumes that the callee
	 * owns the argument space and could overwrite it, e.g.
	 * tailcall optimization. So, to be absolutely safe
	 * we also save and restore enough stack bytes to cover
	 * the argument area.
	 */
	memcpy(kcb->jprobes_stack, (kprobe_opcode_t *)addr,
			MIN_STACK_SIZE(addr));
	regs->eflags &= ~IF_MASK;
	trace_hardirqs_off();
	regs->eip = (unsigned long)(jp->entry);
	return 1;
}

void __kprobes jprobe_return(void)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	asm volatile ("       xchgl   %%ebx,%%esp     \n"
		      "       int3			\n"
		      "       .globl jprobe_return_end	\n"
		      "       jprobe_return_end:	\n"
		      "       nop			\n"::"b"
		      (kcb->jprobe_saved_esp):"memory");
}

int __kprobes longjmp_break_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	u8 *addr = (u8 *) (regs->eip - 1);
	unsigned long stack_addr = (unsigned long)(kcb->jprobe_saved_esp);
	struct jprobe *jp = container_of(p, struct jprobe, kp);

	if ((addr > (u8 *) jprobe_return) && (addr < (u8 *) jprobe_return_end)) {
		if (&regs->esp != kcb->jprobe_saved_esp) {
			struct pt_regs *saved_regs =
			    container_of(kcb->jprobe_saved_esp,
					    struct pt_regs, esp);
			printk("current esp %p does not match saved esp %p\n",
			       &regs->esp, kcb->jprobe_saved_esp);
			printk("Saved registers for jprobe %p\n", jp);
			show_registers(saved_regs);
			printk("Current registers\n");
			show_registers(regs);
			BUG();
		}
		*regs = kcb->jprobe_saved_regs;
		memcpy((kprobe_opcode_t *) stack_addr, kcb->jprobes_stack,
		       MIN_STACK_SIZE(stack_addr));
		preempt_enable_no_resched();
		return 1;
	}
	return 0;
}

int __kprobes arch_trampoline_kprobe(struct kprobe *p)
{
	return 0;
}

int __init arch_init_kprobes(void)
{
	return 0;
}
