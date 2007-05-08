/*
 *  Kernel Probes (KProbes)
 *  arch/x86_64/kernel/kprobes.c
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
 * 2004-Oct	Jim Keniston <kenistoj@us.ibm.com> and Prasanna S Panchamukhi
 *		<prasanna@in.ibm.com> adapted for x86_64
 * 2005-Mar	Roland McGrath <roland@redhat.com>
 *		Fixed to handle %rip-relative addressing mode correctly.
 * 2005-May     Rusty Lynch <rusty.lynch@intel.com>
 *              Added function return probes functionality
 */

#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/preempt.h>
#include <linux/module.h>
#include <linux/kdebug.h>

#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>

void jprobe_return_end(void);
static void __kprobes arch_copy_kprobe(struct kprobe *p);

DEFINE_PER_CPU(struct kprobe *, current_kprobe) = NULL;
DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

/*
 * returns non-zero if opcode modifies the interrupt flag.
 */
static __always_inline int is_IF_modifier(kprobe_opcode_t *insn)
{
	switch (*insn) {
	case 0xfa:		/* cli */
	case 0xfb:		/* sti */
	case 0xcf:		/* iret/iretd */
	case 0x9d:		/* popf/popfd */
		return 1;
	}

	if (*insn  >= 0x40 && *insn <= 0x4f && *++insn == 0xcf)
		return 1;
	return 0;
}

int __kprobes arch_prepare_kprobe(struct kprobe *p)
{
	/* insn: must be on special executable page on x86_64. */
	p->ainsn.insn = get_insn_slot();
	if (!p->ainsn.insn) {
		return -ENOMEM;
	}
	arch_copy_kprobe(p);
	return 0;
}

/*
 * Determine if the instruction uses the %rip-relative addressing mode.
 * If it does, return the address of the 32-bit displacement word.
 * If not, return null.
 */
static s32 __kprobes *is_riprel(u8 *insn)
{
#define W(row,b0,b1,b2,b3,b4,b5,b6,b7,b8,b9,ba,bb,bc,bd,be,bf)		      \
	(((b0##UL << 0x0)|(b1##UL << 0x1)|(b2##UL << 0x2)|(b3##UL << 0x3) |   \
	  (b4##UL << 0x4)|(b5##UL << 0x5)|(b6##UL << 0x6)|(b7##UL << 0x7) |   \
	  (b8##UL << 0x8)|(b9##UL << 0x9)|(ba##UL << 0xa)|(bb##UL << 0xb) |   \
	  (bc##UL << 0xc)|(bd##UL << 0xd)|(be##UL << 0xe)|(bf##UL << 0xf))    \
	 << (row % 64))
	static const u64 onebyte_has_modrm[256 / 64] = {
		/*      0 1 2 3 4 5 6 7 8 9 a b c d e f         */
		/*      -------------------------------         */
		W(0x00, 1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0)| /* 00 */
		W(0x10, 1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0)| /* 10 */
		W(0x20, 1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0)| /* 20 */
		W(0x30, 1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0), /* 30 */
		W(0x40, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)| /* 40 */
		W(0x50, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)| /* 50 */
		W(0x60, 0,0,1,1,0,0,0,0,0,1,0,1,0,0,0,0)| /* 60 */
		W(0x70, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0), /* 70 */
		W(0x80, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1)| /* 80 */
		W(0x90, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)| /* 90 */
		W(0xa0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)| /* a0 */
		W(0xb0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0), /* b0 */
		W(0xc0, 1,1,0,0,1,1,1,1,0,0,0,0,0,0,0,0)| /* c0 */
		W(0xd0, 1,1,1,1,0,0,0,0,1,1,1,1,1,1,1,1)| /* d0 */
		W(0xe0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)| /* e0 */
		W(0xf0, 0,0,0,0,0,0,1,1,0,0,0,0,0,0,1,1)  /* f0 */
		/*      -------------------------------         */
		/*      0 1 2 3 4 5 6 7 8 9 a b c d e f         */
	};
	static const u64 twobyte_has_modrm[256 / 64] = {
		/*      0 1 2 3 4 5 6 7 8 9 a b c d e f         */
		/*      -------------------------------         */
		W(0x00, 1,1,1,1,0,0,0,0,0,0,0,0,0,1,0,1)| /* 0f */
		W(0x10, 1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0)| /* 1f */
		W(0x20, 1,1,1,1,1,0,1,0,1,1,1,1,1,1,1,1)| /* 2f */
		W(0x30, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0), /* 3f */
		W(0x40, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1)| /* 4f */
		W(0x50, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1)| /* 5f */
		W(0x60, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1)| /* 6f */
		W(0x70, 1,1,1,1,1,1,1,0,0,0,0,0,1,1,1,1), /* 7f */
		W(0x80, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)| /* 8f */
		W(0x90, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1)| /* 9f */
		W(0xa0, 0,0,0,1,1,1,1,1,0,0,0,1,1,1,1,1)| /* af */
		W(0xb0, 1,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1), /* bf */
		W(0xc0, 1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0)| /* cf */
		W(0xd0, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1)| /* df */
		W(0xe0, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1)| /* ef */
		W(0xf0, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0)  /* ff */
		/*      -------------------------------         */
		/*      0 1 2 3 4 5 6 7 8 9 a b c d e f         */
	};
#undef	W
	int need_modrm;

	/* Skip legacy instruction prefixes.  */
	while (1) {
		switch (*insn) {
		case 0x66:
		case 0x67:
		case 0x2e:
		case 0x3e:
		case 0x26:
		case 0x64:
		case 0x65:
		case 0x36:
		case 0xf0:
		case 0xf3:
		case 0xf2:
			++insn;
			continue;
		}
		break;
	}

	/* Skip REX instruction prefix.  */
	if ((*insn & 0xf0) == 0x40)
		++insn;

	if (*insn == 0x0f) {	/* Two-byte opcode.  */
		++insn;
		need_modrm = test_bit(*insn, twobyte_has_modrm);
	} else {		/* One-byte opcode.  */
		need_modrm = test_bit(*insn, onebyte_has_modrm);
	}

	if (need_modrm) {
		u8 modrm = *++insn;
		if ((modrm & 0xc7) == 0x05) { /* %rip+disp32 addressing mode */
			/* Displacement follows ModRM byte.  */
			return (s32 *) ++insn;
		}
	}

	/* No %rip-relative addressing mode here.  */
	return NULL;
}

static void __kprobes arch_copy_kprobe(struct kprobe *p)
{
	s32 *ripdisp;
	memcpy(p->ainsn.insn, p->addr, MAX_INSN_SIZE);
	ripdisp = is_riprel(p->ainsn.insn);
	if (ripdisp) {
		/*
		 * The copied instruction uses the %rip-relative
		 * addressing mode.  Adjust the displacement for the
		 * difference between the original location of this
		 * instruction and the location of the copy that will
		 * actually be run.  The tricky bit here is making sure
		 * that the sign extension happens correctly in this
		 * calculation, since we need a signed 32-bit result to
		 * be sign-extended to 64 bits when it's added to the
		 * %rip value and yield the same 64-bit result that the
		 * sign-extension of the original signed 32-bit
		 * displacement would have given.
		 */
		s64 disp = (u8 *) p->addr + *ripdisp - (u8 *) p->ainsn.insn;
		BUG_ON((s64) (s32) disp != disp); /* Sanity check.  */
		*ripdisp = disp;
	}
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
	mutex_lock(&kprobe_mutex);
	free_insn_slot(p->ainsn.insn, 0);
	mutex_unlock(&kprobe_mutex);
}

static void __kprobes save_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	kcb->prev_kprobe.kp = kprobe_running();
	kcb->prev_kprobe.status = kcb->kprobe_status;
	kcb->prev_kprobe.old_rflags = kcb->kprobe_old_rflags;
	kcb->prev_kprobe.saved_rflags = kcb->kprobe_saved_rflags;
}

static void __kprobes restore_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	__get_cpu_var(current_kprobe) = kcb->prev_kprobe.kp;
	kcb->kprobe_status = kcb->prev_kprobe.status;
	kcb->kprobe_old_rflags = kcb->prev_kprobe.old_rflags;
	kcb->kprobe_saved_rflags = kcb->prev_kprobe.saved_rflags;
}

static void __kprobes set_current_kprobe(struct kprobe *p, struct pt_regs *regs,
				struct kprobe_ctlblk *kcb)
{
	__get_cpu_var(current_kprobe) = p;
	kcb->kprobe_saved_rflags = kcb->kprobe_old_rflags
		= (regs->eflags & (TF_MASK | IF_MASK));
	if (is_IF_modifier(p->ainsn.insn))
		kcb->kprobe_saved_rflags &= ~IF_MASK;
}

static void __kprobes prepare_singlestep(struct kprobe *p, struct pt_regs *regs)
{
	regs->eflags |= TF_MASK;
	regs->eflags &= ~IF_MASK;
	/*single step inline if the instruction is an int3*/
	if (p->opcode == BREAKPOINT_INSTRUCTION)
		regs->rip = (unsigned long)p->addr;
	else
		regs->rip = (unsigned long)p->ainsn.insn;
}

/* Called with kretprobe_lock held */
void __kprobes arch_prepare_kretprobe(struct kretprobe_instance *ri,
				      struct pt_regs *regs)
{
	unsigned long *sara = (unsigned long *)regs->rsp;

	ri->ret_addr = (kprobe_opcode_t *) *sara;
	/* Replace the return addr with trampoline addr */
	*sara = (unsigned long) &kretprobe_trampoline;
}

int __kprobes kprobe_handler(struct pt_regs *regs)
{
	struct kprobe *p;
	int ret = 0;
	kprobe_opcode_t *addr = (kprobe_opcode_t *)(regs->rip - sizeof(kprobe_opcode_t));
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
			if (kcb->kprobe_status == KPROBE_HIT_SS &&
				*p->ainsn.insn == BREAKPOINT_INSTRUCTION) {
				regs->eflags &= ~TF_MASK;
				regs->eflags |= kcb->kprobe_saved_rflags;
				goto no_kprobe;
			} else if (kcb->kprobe_status == KPROBE_HIT_SSDONE) {
				/* TODO: Provide re-entrancy from
				 * post_kprobes_handler() and avoid exception
				 * stack corruption while single-stepping on
				 * the instruction of the new probe.
				 */
				arch_disarm_kprobe(p);
				regs->rip = (unsigned long)p->addr;
				reset_current_kprobe();
				ret = 1;
			} else {
				/* We have reentered the kprobe_handler(), since
				 * another probe was hit while within the
				 * handler. We here save the original kprobe
				 * variables and just single step on instruction
				 * of the new probe without calling any user
				 * handlers.
				 */
				save_previous_kprobe(kcb);
				set_current_kprobe(p, regs, kcb);
				kprobes_inc_nmissed_count(p);
				prepare_singlestep(p, regs);
				kcb->kprobe_status = KPROBE_REENTER;
				return 1;
			}
		} else {
			if (*addr != BREAKPOINT_INSTRUCTION) {
			/* The breakpoint instruction was removed by
			 * another cpu right after we hit, no further
			 * handling of this interrupt is appropriate
			 */
				regs->rip = (unsigned long)addr;
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
			regs->rip = (unsigned long)addr;
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
 void kretprobe_trampoline_holder(void)
 {
 	asm volatile (  ".global kretprobe_trampoline\n"
 			"kretprobe_trampoline: \n"
 			"nop\n");
 }

/*
 * Called when we hit the probe point at kretprobe_trampoline
 */
int __kprobes trampoline_probe_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct kretprobe_instance *ri = NULL;
	struct hlist_head *head, empty_rp;
	struct hlist_node *node, *tmp;
	unsigned long flags, orig_ret_address = 0;
	unsigned long trampoline_address =(unsigned long)&kretprobe_trampoline;

	INIT_HLIST_HEAD(&empty_rp);
	spin_lock_irqsave(&kretprobe_lock, flags);
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
	regs->rip = orig_ret_address;

	reset_current_kprobe();
	spin_unlock_irqrestore(&kretprobe_lock, flags);
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
 * the new rip is relative to the copied instruction.  We need to make
 * it relative to the original instruction.
 *
 * 1) If the single-stepped instruction was pushfl, then the TF and IF
 * flags are set in the just-pushed eflags, and may need to be cleared.
 *
 * 2) If the single-stepped instruction was a call, the return address
 * that is atop the stack is the address following the copied instruction.
 * We need to make it the address following the original instruction.
 */
static void __kprobes resume_execution(struct kprobe *p,
		struct pt_regs *regs, struct kprobe_ctlblk *kcb)
{
	unsigned long *tos = (unsigned long *)regs->rsp;
	unsigned long next_rip = 0;
	unsigned long copy_rip = (unsigned long)p->ainsn.insn;
	unsigned long orig_rip = (unsigned long)p->addr;
	kprobe_opcode_t *insn = p->ainsn.insn;

	/*skip the REX prefix*/
	if (*insn >= 0x40 && *insn <= 0x4f)
		insn++;

	switch (*insn) {
	case 0x9c:		/* pushfl */
		*tos &= ~(TF_MASK | IF_MASK);
		*tos |= kcb->kprobe_old_rflags;
		break;
	case 0xc3:		/* ret/lret */
	case 0xcb:
	case 0xc2:
	case 0xca:
		regs->eflags &= ~TF_MASK;
		/* rip is already adjusted, no more changes required*/
		return;
	case 0xe8:		/* call relative - Fix return addr */
		*tos = orig_rip + (*tos - copy_rip);
		break;
	case 0xff:
		if ((insn[1] & 0x30) == 0x10) {
			/* call absolute, indirect */
			/* Fix return addr; rip is correct. */
			next_rip = regs->rip;
			*tos = orig_rip + (*tos - copy_rip);
		} else if (((insn[1] & 0x31) == 0x20) ||	/* jmp near, absolute indirect */
			   ((insn[1] & 0x31) == 0x21)) {	/* jmp far, absolute indirect */
			/* rip is correct. */
			next_rip = regs->rip;
		}
		break;
	case 0xea:		/* jmp absolute -- rip is correct */
		next_rip = regs->rip;
		break;
	default:
		break;
	}

	regs->eflags &= ~TF_MASK;
	if (next_rip) {
		regs->rip = next_rip;
	} else {
		regs->rip = orig_rip + (regs->rip - copy_rip);
	}
}

int __kprobes post_kprobe_handler(struct pt_regs *regs)
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
	regs->eflags |= kcb->kprobe_saved_rflags;

	/* Restore the original saved kprobes variables and continue. */
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
	const struct exception_table_entry *fixup;

	switch(kcb->kprobe_status) {
	case KPROBE_HIT_SS:
	case KPROBE_REENTER:
		/*
		 * We are here because the instruction being single
		 * stepped caused a page fault. We reset the current
		 * kprobe and the rip points back to the probe address
		 * and allow the page fault handler to continue as a
		 * normal page fault.
		 */
		regs->rip = (unsigned long)cur->addr;
		regs->eflags |= kcb->kprobe_old_rflags;
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
		fixup = search_exception_tables(regs->rip);
		if (fixup) {
			regs->rip = fixup->fixup;
			return 1;
		}

		/*
		 * fixup() could not handle it,
		 * Let do_page_fault() fix it.
		 */
		break;
	default:
		break;
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

	if (args->regs && user_mode(args->regs))
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
	case DIE_PAGE_FAULT:
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
	kcb->jprobe_saved_rsp = (long *) regs->rsp;
	addr = (unsigned long)(kcb->jprobe_saved_rsp);
	/*
	 * As Linus pointed out, gcc assumes that the callee
	 * owns the argument space and could overwrite it, e.g.
	 * tailcall optimization. So, to be absolutely safe
	 * we also save and restore enough stack bytes to cover
	 * the argument area.
	 */
	memcpy(kcb->jprobes_stack, (kprobe_opcode_t *)addr,
			MIN_STACK_SIZE(addr));
	regs->eflags &= ~IF_MASK;
	regs->rip = (unsigned long)(jp->entry);
	return 1;
}

void __kprobes jprobe_return(void)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	asm volatile ("       xchg   %%rbx,%%rsp     \n"
		      "       int3			\n"
		      "       .globl jprobe_return_end	\n"
		      "       jprobe_return_end:	\n"
		      "       nop			\n"::"b"
		      (kcb->jprobe_saved_rsp):"memory");
}

int __kprobes longjmp_break_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	u8 *addr = (u8 *) (regs->rip - 1);
	unsigned long stack_addr = (unsigned long)(kcb->jprobe_saved_rsp);
	struct jprobe *jp = container_of(p, struct jprobe, kp);

	if ((addr > (u8 *) jprobe_return) && (addr < (u8 *) jprobe_return_end)) {
		if ((long *)regs->rsp != kcb->jprobe_saved_rsp) {
			struct pt_regs *saved_regs =
			    container_of(kcb->jprobe_saved_rsp,
					    struct pt_regs, rsp);
			printk("current rsp %p does not match saved rsp %p\n",
			       (long *)regs->rsp, kcb->jprobe_saved_rsp);
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

static struct kprobe trampoline_p = {
	.addr = (kprobe_opcode_t *) &kretprobe_trampoline,
	.pre_handler = trampoline_probe_handler
};

int __init arch_init_kprobes(void)
{
	return register_kprobe(&trampoline_p);
}
