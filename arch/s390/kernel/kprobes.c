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
 * Copyright (C) IBM Corporation, 2002, 2006
 *
 * s390 port, used ppc64 as template. Mike Grundy <grundym@us.ibm.com>
 */

#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/preempt.h>
#include <linux/stop_machine.h>
#include <linux/kdebug.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/sections.h>
#include <linux/module.h>

DEFINE_PER_CPU(struct kprobe *, current_kprobe) = NULL;
DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

struct kretprobe_blackpoint kretprobe_blacklist[] = {{NULL, NULL}};

int __kprobes arch_prepare_kprobe(struct kprobe *p)
{
	/* Make sure the probe isn't going on a difficult instruction */
	if (is_prohibited_opcode((kprobe_opcode_t *) p->addr))
		return -EINVAL;

	if ((unsigned long)p->addr & 0x01)
		return -EINVAL;

	/* Use the get_insn_slot() facility for correctness */
	if (!(p->ainsn.insn = get_insn_slot()))
		return -ENOMEM;

	memcpy(p->ainsn.insn, p->addr, MAX_INSN_SIZE * sizeof(kprobe_opcode_t));

	get_instruction_type(&p->ainsn);
	p->opcode = *p->addr;
	return 0;
}

int __kprobes is_prohibited_opcode(kprobe_opcode_t *instruction)
{
	switch (*(__u8 *) instruction) {
	case 0x0c:	/* bassm */
	case 0x0b:	/* bsm	 */
	case 0x83:	/* diag  */
	case 0x44:	/* ex	 */
		return -EINVAL;
	}
	switch (*(__u16 *) instruction) {
	case 0x0101:	/* pr	 */
	case 0xb25a:	/* bsa	 */
	case 0xb240:	/* bakr  */
	case 0xb258:	/* bsg	 */
	case 0xb218:	/* pc	 */
	case 0xb228:	/* pt	 */
		return -EINVAL;
	}
	return 0;
}

void __kprobes get_instruction_type(struct arch_specific_insn *ainsn)
{
	/* default fixup method */
	ainsn->fixup = FIXUP_PSW_NORMAL;

	/* save r1 operand */
	ainsn->reg = (*ainsn->insn & 0xf0) >> 4;

	/* save the instruction length (pop 5-5) in bytes */
	switch (*(__u8 *) (ainsn->insn) >> 6) {
	case 0:
		ainsn->ilen = 2;
		break;
	case 1:
	case 2:
		ainsn->ilen = 4;
		break;
	case 3:
		ainsn->ilen = 6;
		break;
	}

	switch (*(__u8 *) ainsn->insn) {
	case 0x05:	/* balr	*/
	case 0x0d:	/* basr */
		ainsn->fixup = FIXUP_RETURN_REGISTER;
		/* if r2 = 0, no branch will be taken */
		if ((*ainsn->insn & 0x0f) == 0)
			ainsn->fixup |= FIXUP_BRANCH_NOT_TAKEN;
		break;
	case 0x06:	/* bctr	*/
	case 0x07:	/* bcr	*/
		ainsn->fixup = FIXUP_BRANCH_NOT_TAKEN;
		break;
	case 0x45:	/* bal	*/
	case 0x4d:	/* bas	*/
		ainsn->fixup = FIXUP_RETURN_REGISTER;
		break;
	case 0x47:	/* bc	*/
	case 0x46:	/* bct	*/
	case 0x86:	/* bxh	*/
	case 0x87:	/* bxle	*/
		ainsn->fixup = FIXUP_BRANCH_NOT_TAKEN;
		break;
	case 0x82:	/* lpsw	*/
		ainsn->fixup = FIXUP_NOT_REQUIRED;
		break;
	case 0xb2:	/* lpswe */
		if (*(((__u8 *) ainsn->insn) + 1) == 0xb2) {
			ainsn->fixup = FIXUP_NOT_REQUIRED;
		}
		break;
	case 0xa7:	/* bras	*/
		if ((*ainsn->insn & 0x0f) == 0x05) {
			ainsn->fixup |= FIXUP_RETURN_REGISTER;
		}
		break;
	case 0xc0:
		if ((*ainsn->insn & 0x0f) == 0x00  /* larl  */
			|| (*ainsn->insn & 0x0f) == 0x05) /* brasl */
		ainsn->fixup |= FIXUP_RETURN_REGISTER;
		break;
	case 0xeb:
		if (*(((__u8 *) ainsn->insn) + 5 ) == 0x44 ||	/* bxhg  */
			*(((__u8 *) ainsn->insn) + 5) == 0x45) {/* bxleg */
			ainsn->fixup = FIXUP_BRANCH_NOT_TAKEN;
		}
		break;
	case 0xe3:	/* bctg	*/
		if (*(((__u8 *) ainsn->insn) + 5) == 0x46) {
			ainsn->fixup = FIXUP_BRANCH_NOT_TAKEN;
		}
		break;
	}
}

static int __kprobes swap_instruction(void *aref)
{
	struct ins_replace_args *args = aref;

	return probe_kernel_write(args->ptr, &args->new, sizeof(args->new));
}

void __kprobes arch_arm_kprobe(struct kprobe *p)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	unsigned long status = kcb->kprobe_status;
	struct ins_replace_args args;

	args.ptr = p->addr;
	args.old = p->opcode;
	args.new = BREAKPOINT_INSTRUCTION;

	kcb->kprobe_status = KPROBE_SWAP_INST;
	stop_machine(swap_instruction, &args, NULL);
	kcb->kprobe_status = status;
}

void __kprobes arch_disarm_kprobe(struct kprobe *p)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	unsigned long status = kcb->kprobe_status;
	struct ins_replace_args args;

	args.ptr = p->addr;
	args.old = BREAKPOINT_INSTRUCTION;
	args.new = p->opcode;

	kcb->kprobe_status = KPROBE_SWAP_INST;
	stop_machine(swap_instruction, &args, NULL);
	kcb->kprobe_status = status;
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
	per_cr_bits kprobe_per_regs[1];

	memset(kprobe_per_regs, 0, sizeof(per_cr_bits));
	regs->psw.addr = (unsigned long)p->ainsn.insn | PSW_ADDR_AMODE;

	/* Set up the per control reg info, will pass to lctl */
	kprobe_per_regs[0].em_instruction_fetch = 1;
	kprobe_per_regs[0].starting_addr = (unsigned long)p->ainsn.insn;
	kprobe_per_regs[0].ending_addr = (unsigned long)p->ainsn.insn + 1;

	/* Set the PER control regs, turns on single step for this address */
	__ctl_load(kprobe_per_regs, 9, 11);
	regs->psw.mask |= PSW_MASK_PER;
	regs->psw.mask &= ~(PSW_MASK_IO | PSW_MASK_EXT | PSW_MASK_MCHECK);
}

static void __kprobes save_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	kcb->prev_kprobe.kp = kprobe_running();
	kcb->prev_kprobe.status = kcb->kprobe_status;
	kcb->prev_kprobe.kprobe_saved_imask = kcb->kprobe_saved_imask;
	memcpy(kcb->prev_kprobe.kprobe_saved_ctl, kcb->kprobe_saved_ctl,
					sizeof(kcb->kprobe_saved_ctl));
}

static void __kprobes restore_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	__get_cpu_var(current_kprobe) = kcb->prev_kprobe.kp;
	kcb->kprobe_status = kcb->prev_kprobe.status;
	kcb->kprobe_saved_imask = kcb->prev_kprobe.kprobe_saved_imask;
	memcpy(kcb->kprobe_saved_ctl, kcb->prev_kprobe.kprobe_saved_ctl,
					sizeof(kcb->kprobe_saved_ctl));
}

static void __kprobes set_current_kprobe(struct kprobe *p, struct pt_regs *regs,
						struct kprobe_ctlblk *kcb)
{
	__get_cpu_var(current_kprobe) = p;
	/* Save the interrupt and per flags */
	kcb->kprobe_saved_imask = regs->psw.mask &
	    (PSW_MASK_PER | PSW_MASK_IO | PSW_MASK_EXT | PSW_MASK_MCHECK);
	/* Save the control regs that govern PER */
	__ctl_store(kcb->kprobe_saved_ctl, 9, 11);
}

void __kprobes arch_prepare_kretprobe(struct kretprobe_instance *ri,
					struct pt_regs *regs)
{
	ri->ret_addr = (kprobe_opcode_t *) regs->gprs[14];

	/* Replace the return addr with trampoline addr */
	regs->gprs[14] = (unsigned long)&kretprobe_trampoline;
}

static int __kprobes kprobe_handler(struct pt_regs *regs)
{
	struct kprobe *p;
	int ret = 0;
	unsigned long *addr = (unsigned long *)
		((regs->psw.addr & PSW_ADDR_INSN) - 2);
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
				regs->psw.mask &= ~PSW_MASK_PER;
				regs->psw.mask |= kcb->kprobe_saved_imask;
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
			p = __get_cpu_var(current_kprobe);
			if (p->break_handler && p->break_handler(p, regs)) {
				goto ss_probe;
			}
		}
		goto no_kprobe;
	}

	p = get_kprobe(addr);
	if (!p)
		/*
		 * No kprobe at this address. The fault has not been
		 * caused by a kprobe breakpoint. The race of breakpoint
		 * vs. kprobe remove does not exist because on s390 we
		 * use stop_machine to arm/disarm the breakpoints.
		 */
		goto no_kprobe;

	kcb->kprobe_status = KPROBE_HIT_ACTIVE;
	set_current_kprobe(p, regs, kcb);
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
 * Function return probe trampoline:
 *	- init_kprobes() establishes a probepoint here
 *	- When the probed function returns, this probe
 *		causes the handlers to fire
 */
static void __used kretprobe_trampoline_holder(void)
{
	asm volatile(".global kretprobe_trampoline\n"
		     "kretprobe_trampoline: bcr 0,0\n");
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
	unsigned long trampoline_address = (unsigned long)&kretprobe_trampoline;

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
	 *	 function, the first instance's ret_addr will point to the
	 *	 real return address, and all the rest will point to
	 *	 kretprobe_trampoline
	 */
	hlist_for_each_entry_safe(ri, node, tmp, head, hlist) {
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
	regs->psw.addr = orig_ret_address | PSW_ADDR_AMODE;

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
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	regs->psw.addr &= PSW_ADDR_INSN;

	if (p->ainsn.fixup & FIXUP_PSW_NORMAL)
		regs->psw.addr = (unsigned long)p->addr +
				((unsigned long)regs->psw.addr -
				 (unsigned long)p->ainsn.insn);

	if (p->ainsn.fixup & FIXUP_BRANCH_NOT_TAKEN)
		if ((unsigned long)regs->psw.addr -
		    (unsigned long)p->ainsn.insn == p->ainsn.ilen)
			regs->psw.addr = (unsigned long)p->addr + p->ainsn.ilen;

	if (p->ainsn.fixup & FIXUP_RETURN_REGISTER)
		regs->gprs[p->ainsn.reg] = ((unsigned long)p->addr +
						(regs->gprs[p->ainsn.reg] -
						(unsigned long)p->ainsn.insn))
						| PSW_ADDR_AMODE;

	regs->psw.addr |= PSW_ADDR_AMODE;
	/* turn off PER mode */
	regs->psw.mask &= ~PSW_MASK_PER;
	/* Restore the original per control regs */
	__ctl_load(kcb->kprobe_saved_ctl, 9, 11);
	regs->psw.mask |= kcb->kprobe_saved_imask;
}

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

	resume_execution(cur, regs);

	/*Restore back the original saved kprobes variables and continue. */
	if (kcb->kprobe_status == KPROBE_REENTER) {
		restore_previous_kprobe(kcb);
		goto out;
	}
	reset_current_kprobe();
out:
	preempt_enable_no_resched();

	/*
	 * if somebody else is singlestepping across a probe point, psw mask
	 * will have PER set, in which case, continue the remaining processing
	 * of do_single_step, as if this is not a probe hit.
	 */
	if (regs->psw.mask & PSW_MASK_PER) {
		return 0;
	}

	return 1;
}

int __kprobes kprobe_fault_handler(struct pt_regs *regs, int trapnr)
{
	struct kprobe *cur = kprobe_running();
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	const struct exception_table_entry *entry;

	switch(kcb->kprobe_status) {
	case KPROBE_SWAP_INST:
		/* We are here because the instruction replacement failed */
		return 0;
	case KPROBE_HIT_SS:
	case KPROBE_REENTER:
		/*
		 * We are here because the instruction being single
		 * stepped caused a page fault. We reset the current
		 * kprobe and the nip points back to the probe address
		 * and allow the page fault handler to continue as a
		 * normal page fault.
		 */
		regs->psw.addr = (unsigned long)cur->addr | PSW_ADDR_AMODE;
		regs->psw.mask &= ~PSW_MASK_PER;
		regs->psw.mask |= kcb->kprobe_saved_imask;
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
		entry = search_exception_tables(regs->psw.addr & PSW_ADDR_INSN);
		if (entry) {
			regs->psw.addr = entry->fixup | PSW_ADDR_AMODE;
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

	switch (val) {
	case DIE_BPT:
		if (kprobe_handler(args->regs))
			ret = NOTIFY_STOP;
		break;
	case DIE_SSTEP:
		if (post_kprobe_handler(args->regs))
			ret = NOTIFY_STOP;
		break;
	case DIE_TRAP:
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

	memcpy(&kcb->jprobe_saved_regs, regs, sizeof(struct pt_regs));

	/* setup return addr to the jprobe handler routine */
	regs->psw.addr = (unsigned long)(jp->entry) | PSW_ADDR_AMODE;

	/* r14 is the function return address */
	kcb->jprobe_saved_r14 = (unsigned long)regs->gprs[14];
	/* r15 is the stack pointer */
	kcb->jprobe_saved_r15 = (unsigned long)regs->gprs[15];
	addr = (unsigned long)kcb->jprobe_saved_r15;

	memcpy(kcb->jprobes_stack, (kprobe_opcode_t *) addr,
	       MIN_STACK_SIZE(addr));
	return 1;
}

void __kprobes jprobe_return(void)
{
	asm volatile(".word 0x0002");
}

void __kprobes jprobe_return_end(void)
{
	asm volatile("bcr 0,0");
}

int __kprobes longjmp_break_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	unsigned long stack_addr = (unsigned long)(kcb->jprobe_saved_r15);

	/* Put the regs back */
	memcpy(regs, &kcb->jprobe_saved_regs, sizeof(struct pt_regs));
	/* put the stack back */
	memcpy((kprobe_opcode_t *) stack_addr, kcb->jprobes_stack,
	       MIN_STACK_SIZE(stack_addr));
	preempt_enable_no_resched();
	return 1;
}

static struct kprobe trampoline_p = {
	.addr = (kprobe_opcode_t *) & kretprobe_trampoline,
	.pre_handler = trampoline_probe_handler
};

int __init arch_init_kprobes(void)
{
	return register_kprobe(&trampoline_p);
}

int __kprobes arch_trampoline_kprobe(struct kprobe *p)
{
	if (p->addr == (kprobe_opcode_t *) & kretprobe_trampoline)
		return 1;
	return 0;
}
