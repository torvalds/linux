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
 * Copyright IBM Corp. 2002, 2006
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
#include <linux/slab.h>
#include <linux/hardirq.h>

DEFINE_PER_CPU(struct kprobe *, current_kprobe);
DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

struct kretprobe_blackpoint kretprobe_blacklist[] = { };

DEFINE_INSN_CACHE_OPS(dmainsn);

static void *alloc_dmainsn_page(void)
{
	return (void *)__get_free_page(GFP_KERNEL | GFP_DMA);
}

static void free_dmainsn_page(void *page)
{
	free_page((unsigned long)page);
}

struct kprobe_insn_cache kprobe_dmainsn_slots = {
	.mutex = __MUTEX_INITIALIZER(kprobe_dmainsn_slots.mutex),
	.alloc = alloc_dmainsn_page,
	.free = free_dmainsn_page,
	.pages = LIST_HEAD_INIT(kprobe_dmainsn_slots.pages),
	.insn_size = MAX_INSN_SIZE,
};

static int __kprobes is_prohibited_opcode(kprobe_opcode_t *insn)
{
	switch (insn[0] >> 8) {
	case 0x0c:	/* bassm */
	case 0x0b:	/* bsm	 */
	case 0x83:	/* diag  */
	case 0x44:	/* ex	 */
	case 0xac:	/* stnsm */
	case 0xad:	/* stosm */
		return -EINVAL;
	}
	switch (insn[0]) {
	case 0x0101:	/* pr	 */
	case 0xb25a:	/* bsa	 */
	case 0xb240:	/* bakr  */
	case 0xb258:	/* bsg	 */
	case 0xb218:	/* pc	 */
	case 0xb228:	/* pt	 */
	case 0xb98d:	/* epsw	 */
		return -EINVAL;
	}
	return 0;
}

static int __kprobes get_fixup_type(kprobe_opcode_t *insn)
{
	/* default fixup method */
	int fixup = FIXUP_PSW_NORMAL;

	switch (insn[0] >> 8) {
	case 0x05:	/* balr	*/
	case 0x0d:	/* basr */
		fixup = FIXUP_RETURN_REGISTER;
		/* if r2 = 0, no branch will be taken */
		if ((insn[0] & 0x0f) == 0)
			fixup |= FIXUP_BRANCH_NOT_TAKEN;
		break;
	case 0x06:	/* bctr	*/
	case 0x07:	/* bcr	*/
		fixup = FIXUP_BRANCH_NOT_TAKEN;
		break;
	case 0x45:	/* bal	*/
	case 0x4d:	/* bas	*/
		fixup = FIXUP_RETURN_REGISTER;
		break;
	case 0x47:	/* bc	*/
	case 0x46:	/* bct	*/
	case 0x86:	/* bxh	*/
	case 0x87:	/* bxle	*/
		fixup = FIXUP_BRANCH_NOT_TAKEN;
		break;
	case 0x82:	/* lpsw	*/
		fixup = FIXUP_NOT_REQUIRED;
		break;
	case 0xb2:	/* lpswe */
		if ((insn[0] & 0xff) == 0xb2)
			fixup = FIXUP_NOT_REQUIRED;
		break;
	case 0xa7:	/* bras	*/
		if ((insn[0] & 0x0f) == 0x05)
			fixup |= FIXUP_RETURN_REGISTER;
		break;
	case 0xc0:
		if ((insn[0] & 0x0f) == 0x05)	/* brasl */
			fixup |= FIXUP_RETURN_REGISTER;
		break;
	case 0xeb:
		switch (insn[2] & 0xff) {
		case 0x44: /* bxhg  */
		case 0x45: /* bxleg */
			fixup = FIXUP_BRANCH_NOT_TAKEN;
			break;
		}
		break;
	case 0xe3:	/* bctg	*/
		if ((insn[2] & 0xff) == 0x46)
			fixup = FIXUP_BRANCH_NOT_TAKEN;
		break;
	case 0xec:
		switch (insn[2] & 0xff) {
		case 0xe5: /* clgrb */
		case 0xe6: /* cgrb  */
		case 0xf6: /* crb   */
		case 0xf7: /* clrb  */
		case 0xfc: /* cgib  */
		case 0xfd: /* cglib */
		case 0xfe: /* cib   */
		case 0xff: /* clib  */
			fixup = FIXUP_BRANCH_NOT_TAKEN;
			break;
		}
		break;
	}
	return fixup;
}

static int __kprobes is_insn_relative_long(kprobe_opcode_t *insn)
{
	/* Check if we have a RIL-b or RIL-c format instruction which
	 * we need to modify in order to avoid instruction emulation. */
	switch (insn[0] >> 8) {
	case 0xc0:
		if ((insn[0] & 0x0f) == 0x00) /* larl */
			return true;
		break;
	case 0xc4:
		switch (insn[0] & 0x0f) {
		case 0x02: /* llhrl  */
		case 0x04: /* lghrl  */
		case 0x05: /* lhrl   */
		case 0x06: /* llghrl */
		case 0x07: /* sthrl  */
		case 0x08: /* lgrl   */
		case 0x0b: /* stgrl  */
		case 0x0c: /* lgfrl  */
		case 0x0d: /* lrl    */
		case 0x0e: /* llgfrl */
		case 0x0f: /* strl   */
			return true;
		}
		break;
	case 0xc6:
		switch (insn[0] & 0x0f) {
		case 0x00: /* exrl   */
		case 0x02: /* pfdrl  */
		case 0x04: /* cghrl  */
		case 0x05: /* chrl   */
		case 0x06: /* clghrl */
		case 0x07: /* clhrl  */
		case 0x08: /* cgrl   */
		case 0x0a: /* clgrl  */
		case 0x0c: /* cgfrl  */
		case 0x0d: /* crl    */
		case 0x0e: /* clgfrl */
		case 0x0f: /* clrl   */
			return true;
		}
		break;
	}
	return false;
}

static void __kprobes copy_instruction(struct kprobe *p)
{
	s64 disp, new_disp;
	u64 addr, new_addr;

	memcpy(p->ainsn.insn, p->addr, ((p->opcode >> 14) + 3) & -2);
	if (!is_insn_relative_long(p->ainsn.insn))
		return;
	/*
	 * For pc-relative instructions in RIL-b or RIL-c format patch the
	 * RI2 displacement field. We have already made sure that the insn
	 * slot for the patched instruction is within the same 2GB area
	 * as the original instruction (either kernel image or module area).
	 * Therefore the new displacement will always fit.
	 */
	disp = *(s32 *)&p->ainsn.insn[1];
	addr = (u64)(unsigned long)p->addr;
	new_addr = (u64)(unsigned long)p->ainsn.insn;
	new_disp = ((addr + (disp * 2)) - new_addr) / 2;
	*(s32 *)&p->ainsn.insn[1] = new_disp;
}

static inline int is_kernel_addr(void *addr)
{
	return addr < (void *)_end;
}

static inline int is_module_addr(void *addr)
{
#ifdef CONFIG_64BIT
	BUILD_BUG_ON(MODULES_LEN > (1UL << 31));
	if (addr < (void *)MODULES_VADDR)
		return 0;
	if (addr > (void *)MODULES_END)
		return 0;
#endif
	return 1;
}

static int __kprobes s390_get_insn_slot(struct kprobe *p)
{
	/*
	 * Get an insn slot that is within the same 2GB area like the original
	 * instruction. That way instructions with a 32bit signed displacement
	 * field can be patched and executed within the insn slot.
	 */
	p->ainsn.insn = NULL;
	if (is_kernel_addr(p->addr))
		p->ainsn.insn = get_dmainsn_slot();
	if (is_module_addr(p->addr))
		p->ainsn.insn = get_insn_slot();
	return p->ainsn.insn ? 0 : -ENOMEM;
}

static void __kprobes s390_free_insn_slot(struct kprobe *p)
{
	if (!p->ainsn.insn)
		return;
	if (is_kernel_addr(p->addr))
		free_dmainsn_slot(p->ainsn.insn, 0);
	else
		free_insn_slot(p->ainsn.insn, 0);
	p->ainsn.insn = NULL;
}

int __kprobes arch_prepare_kprobe(struct kprobe *p)
{
	if ((unsigned long) p->addr & 0x01)
		return -EINVAL;
	/* Make sure the probe isn't going on a difficult instruction */
	if (is_prohibited_opcode(p->addr))
		return -EINVAL;
	if (s390_get_insn_slot(p))
		return -ENOMEM;
	p->opcode = *p->addr;
	copy_instruction(p);
	return 0;
}

struct ins_replace_args {
	kprobe_opcode_t *ptr;
	kprobe_opcode_t opcode;
};

static int __kprobes swap_instruction(void *aref)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	unsigned long status = kcb->kprobe_status;
	struct ins_replace_args *args = aref;

	kcb->kprobe_status = KPROBE_SWAP_INST;
	probe_kernel_write(args->ptr, &args->opcode, sizeof(args->opcode));
	kcb->kprobe_status = status;
	return 0;
}

void __kprobes arch_arm_kprobe(struct kprobe *p)
{
	struct ins_replace_args args;

	args.ptr = p->addr;
	args.opcode = BREAKPOINT_INSTRUCTION;
	stop_machine(swap_instruction, &args, NULL);
}

void __kprobes arch_disarm_kprobe(struct kprobe *p)
{
	struct ins_replace_args args;

	args.ptr = p->addr;
	args.opcode = p->opcode;
	stop_machine(swap_instruction, &args, NULL);
}

void __kprobes arch_remove_kprobe(struct kprobe *p)
{
	s390_free_insn_slot(p);
}

static void __kprobes enable_singlestep(struct kprobe_ctlblk *kcb,
					struct pt_regs *regs,
					unsigned long ip)
{
	struct per_regs per_kprobe;

	/* Set up the PER control registers %cr9-%cr11 */
	per_kprobe.control = PER_EVENT_IFETCH;
	per_kprobe.start = ip;
	per_kprobe.end = ip;

	/* Save control regs and psw mask */
	__ctl_store(kcb->kprobe_saved_ctl, 9, 11);
	kcb->kprobe_saved_imask = regs->psw.mask &
		(PSW_MASK_PER | PSW_MASK_IO | PSW_MASK_EXT);

	/* Set PER control regs, turns on single step for the given address */
	__ctl_load(per_kprobe, 9, 11);
	regs->psw.mask |= PSW_MASK_PER;
	regs->psw.mask &= ~(PSW_MASK_IO | PSW_MASK_EXT);
	regs->psw.addr = ip | PSW_ADDR_AMODE;
}

static void __kprobes disable_singlestep(struct kprobe_ctlblk *kcb,
					 struct pt_regs *regs,
					 unsigned long ip)
{
	/* Restore control regs and psw mask, set new psw address */
	__ctl_load(kcb->kprobe_saved_ctl, 9, 11);
	regs->psw.mask &= ~PSW_MASK_PER;
	regs->psw.mask |= kcb->kprobe_saved_imask;
	regs->psw.addr = ip | PSW_ADDR_AMODE;
}

/*
 * Activate a kprobe by storing its pointer to current_kprobe. The
 * previous kprobe is stored in kcb->prev_kprobe. A stack of up to
 * two kprobes can be active, see KPROBE_REENTER.
 */
static void __kprobes push_kprobe(struct kprobe_ctlblk *kcb, struct kprobe *p)
{
	kcb->prev_kprobe.kp = __get_cpu_var(current_kprobe);
	kcb->prev_kprobe.status = kcb->kprobe_status;
	__get_cpu_var(current_kprobe) = p;
}

/*
 * Deactivate a kprobe by backing up to the previous state. If the
 * current state is KPROBE_REENTER prev_kprobe.kp will be non-NULL,
 * for any other state prev_kprobe.kp will be NULL.
 */
static void __kprobes pop_kprobe(struct kprobe_ctlblk *kcb)
{
	__get_cpu_var(current_kprobe) = kcb->prev_kprobe.kp;
	kcb->kprobe_status = kcb->prev_kprobe.status;
}

void __kprobes arch_prepare_kretprobe(struct kretprobe_instance *ri,
					struct pt_regs *regs)
{
	ri->ret_addr = (kprobe_opcode_t *) regs->gprs[14];

	/* Replace the return addr with trampoline addr */
	regs->gprs[14] = (unsigned long) &kretprobe_trampoline;
}

static void __kprobes kprobe_reenter_check(struct kprobe_ctlblk *kcb,
					   struct kprobe *p)
{
	switch (kcb->kprobe_status) {
	case KPROBE_HIT_SSDONE:
	case KPROBE_HIT_ACTIVE:
		kprobes_inc_nmissed_count(p);
		break;
	case KPROBE_HIT_SS:
	case KPROBE_REENTER:
	default:
		/*
		 * A kprobe on the code path to single step an instruction
		 * is a BUG. The code path resides in the .kprobes.text
		 * section and is executed with interrupts disabled.
		 */
		printk(KERN_EMERG "Invalid kprobe detected at %p.\n", p->addr);
		dump_kprobe(p);
		BUG();
	}
}

static int __kprobes kprobe_handler(struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb;
	struct kprobe *p;

	/*
	 * We want to disable preemption for the entire duration of kprobe
	 * processing. That includes the calls to the pre/post handlers
	 * and single stepping the kprobe instruction.
	 */
	preempt_disable();
	kcb = get_kprobe_ctlblk();
	p = get_kprobe((void *)((regs->psw.addr & PSW_ADDR_INSN) - 2));

	if (p) {
		if (kprobe_running()) {
			/*
			 * We have hit a kprobe while another is still
			 * active. This can happen in the pre and post
			 * handler. Single step the instruction of the
			 * new probe but do not call any handler function
			 * of this secondary kprobe.
			 * push_kprobe and pop_kprobe saves and restores
			 * the currently active kprobe.
			 */
			kprobe_reenter_check(kcb, p);
			push_kprobe(kcb, p);
			kcb->kprobe_status = KPROBE_REENTER;
		} else {
			/*
			 * If we have no pre-handler or it returned 0, we
			 * continue with single stepping. If we have a
			 * pre-handler and it returned non-zero, it prepped
			 * for calling the break_handler below on re-entry
			 * for jprobe processing, so get out doing nothing
			 * more here.
			 */
			push_kprobe(kcb, p);
			kcb->kprobe_status = KPROBE_HIT_ACTIVE;
			if (p->pre_handler && p->pre_handler(p, regs))
				return 1;
			kcb->kprobe_status = KPROBE_HIT_SS;
		}
		enable_singlestep(kcb, regs, (unsigned long) p->ainsn.insn);
		return 1;
	} else if (kprobe_running()) {
		p = __get_cpu_var(current_kprobe);
		if (p->break_handler && p->break_handler(p, regs)) {
			/*
			 * Continuation after the jprobe completed and
			 * caused the jprobe_return trap. The jprobe
			 * break_handler "returns" to the original
			 * function that still has the kprobe breakpoint
			 * installed. We continue with single stepping.
			 */
			kcb->kprobe_status = KPROBE_HIT_SS;
			enable_singlestep(kcb, regs,
					  (unsigned long) p->ainsn.insn);
			return 1;
		} /* else:
		   * No kprobe at this address and the current kprobe
		   * has no break handler (no jprobe!). The kernel just
		   * exploded, let the standard trap handler pick up the
		   * pieces.
		   */
	} /* else:
	   * No kprobe at this address and no active kprobe. The trap has
	   * not been caused by a kprobe breakpoint. The race of breakpoint
	   * vs. kprobe remove does not exist because on s390 as we use
	   * stop_machine to arm/disarm the breakpoints.
	   */
	preempt_enable_no_resched();
	return 0;
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
	struct kretprobe_instance *ri;
	struct hlist_head *head, empty_rp;
	struct hlist_node *tmp;
	unsigned long flags, orig_ret_address;
	unsigned long trampoline_address;
	kprobe_opcode_t *correct_ret_addr;

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
	ri = NULL;
	orig_ret_address = 0;
	correct_ret_addr = NULL;
	trampoline_address = (unsigned long) &kretprobe_trampoline;
	hlist_for_each_entry_safe(ri, tmp, head, hlist) {
		if (ri->task != current)
			/* another task is sharing our hash bucket */
			continue;

		orig_ret_address = (unsigned long) ri->ret_addr;

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

		orig_ret_address = (unsigned long) ri->ret_addr;

		if (ri->rp && ri->rp->handler) {
			ri->ret_addr = correct_ret_addr;
			ri->rp->handler(ri, regs);
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

	regs->psw.addr = orig_ret_address | PSW_ADDR_AMODE;

	pop_kprobe(get_kprobe_ctlblk());
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
	unsigned long ip = regs->psw.addr & PSW_ADDR_INSN;
	int fixup = get_fixup_type(p->ainsn.insn);

	if (fixup & FIXUP_PSW_NORMAL)
		ip += (unsigned long) p->addr - (unsigned long) p->ainsn.insn;

	if (fixup & FIXUP_BRANCH_NOT_TAKEN) {
		int ilen = ((p->ainsn.insn[0] >> 14) + 3) & -2;
		if (ip - (unsigned long) p->ainsn.insn == ilen)
			ip = (unsigned long) p->addr + ilen;
	}

	if (fixup & FIXUP_RETURN_REGISTER) {
		int reg = (p->ainsn.insn[0] & 0xf0) >> 4;
		regs->gprs[reg] += (unsigned long) p->addr -
				   (unsigned long) p->ainsn.insn;
	}

	disable_singlestep(kcb, regs, ip);
}

static int __kprobes post_kprobe_handler(struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	struct kprobe *p = kprobe_running();

	if (!p)
		return 0;

	if (kcb->kprobe_status != KPROBE_REENTER && p->post_handler) {
		kcb->kprobe_status = KPROBE_HIT_SSDONE;
		p->post_handler(p, regs, 0);
	}

	resume_execution(p, regs);
	pop_kprobe(kcb);
	preempt_enable_no_resched();

	/*
	 * if somebody else is singlestepping across a probe point, psw mask
	 * will have PER set, in which case, continue the remaining processing
	 * of do_single_step, as if this is not a probe hit.
	 */
	if (regs->psw.mask & PSW_MASK_PER)
		return 0;

	return 1;
}

static int __kprobes kprobe_trap_handler(struct pt_regs *regs, int trapnr)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	struct kprobe *p = kprobe_running();
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
		disable_singlestep(kcb, regs, (unsigned long) p->addr);
		pop_kprobe(kcb);
		preempt_enable_no_resched();
		break;
	case KPROBE_HIT_ACTIVE:
	case KPROBE_HIT_SSDONE:
		/*
		 * We increment the nmissed count for accounting,
		 * we can also use npre/npostfault count for accouting
		 * these specific fault cases.
		 */
		kprobes_inc_nmissed_count(p);

		/*
		 * We come here because instructions in the pre/post
		 * handler caused the page_fault, this could happen
		 * if handler tries to access user space by
		 * copy_from_user(), get_user() etc. Let the
		 * user-specified handler try to fix it first.
		 */
		if (p->fault_handler && p->fault_handler(p, regs, trapnr))
			return 1;

		/*
		 * In case the user-specified fault handler returned
		 * zero, try to fix up.
		 */
		entry = search_exception_tables(regs->psw.addr & PSW_ADDR_INSN);
		if (entry) {
			regs->psw.addr = extable_fixup(entry) | PSW_ADDR_AMODE;
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

int __kprobes kprobe_fault_handler(struct pt_regs *regs, int trapnr)
{
	int ret;

	if (regs->psw.mask & (PSW_MASK_IO | PSW_MASK_EXT))
		local_irq_disable();
	ret = kprobe_trap_handler(regs, trapnr);
	if (regs->psw.mask & (PSW_MASK_IO | PSW_MASK_EXT))
		local_irq_restore(regs->psw.mask & ~PSW_MASK_PER);
	return ret;
}

/*
 * Wrapper routine to for handling exceptions.
 */
int __kprobes kprobe_exceptions_notify(struct notifier_block *self,
				       unsigned long val, void *data)
{
	struct die_args *args = (struct die_args *) data;
	struct pt_regs *regs = args->regs;
	int ret = NOTIFY_DONE;

	if (regs->psw.mask & (PSW_MASK_IO | PSW_MASK_EXT))
		local_irq_disable();

	switch (val) {
	case DIE_BPT:
		if (kprobe_handler(regs))
			ret = NOTIFY_STOP;
		break;
	case DIE_SSTEP:
		if (post_kprobe_handler(regs))
			ret = NOTIFY_STOP;
		break;
	case DIE_TRAP:
		if (!preemptible() && kprobe_running() &&
		    kprobe_trap_handler(regs, args->trapnr))
			ret = NOTIFY_STOP;
		break;
	default:
		break;
	}

	if (regs->psw.mask & (PSW_MASK_IO | PSW_MASK_EXT))
		local_irq_restore(regs->psw.mask & ~PSW_MASK_PER);

	return ret;
}

int __kprobes setjmp_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct jprobe *jp = container_of(p, struct jprobe, kp);
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	unsigned long stack;

	memcpy(&kcb->jprobe_saved_regs, regs, sizeof(struct pt_regs));

	/* setup return addr to the jprobe handler routine */
	regs->psw.addr = (unsigned long) jp->entry | PSW_ADDR_AMODE;
	regs->psw.mask &= ~(PSW_MASK_IO | PSW_MASK_EXT);

	/* r15 is the stack pointer */
	stack = (unsigned long) regs->gprs[15];

	memcpy(kcb->jprobes_stack, (void *) stack, MIN_STACK_SIZE(stack));
	return 1;
}

void __kprobes jprobe_return(void)
{
	asm volatile(".word 0x0002");
}

static void __used __kprobes jprobe_return_end(void)
{
	asm volatile("bcr 0,0");
}

int __kprobes longjmp_break_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	unsigned long stack;

	stack = (unsigned long) kcb->jprobe_saved_regs.gprs[15];

	/* Put the regs back */
	memcpy(regs, &kcb->jprobe_saved_regs, sizeof(struct pt_regs));
	/* put the stack back */
	memcpy((void *) stack, kcb->jprobes_stack, MIN_STACK_SIZE(stack));
	preempt_enable_no_resched();
	return 1;
}

static struct kprobe trampoline = {
	.addr = (kprobe_opcode_t *) &kretprobe_trampoline,
	.pre_handler = trampoline_probe_handler
};

int __init arch_init_kprobes(void)
{
	return register_kprobe(&trampoline);
}

int __kprobes arch_trampoline_kprobe(struct kprobe *p)
{
	return p->addr == (kprobe_opcode_t *) &kretprobe_trampoline;
}
