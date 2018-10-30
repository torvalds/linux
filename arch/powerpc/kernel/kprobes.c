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
#include <linux/extable.h>
#include <linux/kdebug.h>
#include <linux/slab.h>
#include <asm/code-patching.h>
#include <asm/cacheflush.h>
#include <asm/sstep.h>
#include <asm/sections.h>
#include <linux/uaccess.h>

DEFINE_PER_CPU(struct kprobe *, current_kprobe) = NULL;
DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

struct kretprobe_blackpoint kretprobe_blacklist[] = {{NULL, NULL}};

bool arch_within_kprobe_blacklist(unsigned long addr)
{
	return  (addr >= (unsigned long)__kprobes_text_start &&
		 addr < (unsigned long)__kprobes_text_end) ||
		(addr >= (unsigned long)_stext &&
		 addr < (unsigned long)__head_end);
}

kprobe_opcode_t *kprobe_lookup_name(const char *name, unsigned int offset)
{
	kprobe_opcode_t *addr = NULL;

#ifdef PPC64_ELF_ABI_v2
	/* PPC64 ABIv2 needs local entry point */
	addr = (kprobe_opcode_t *)kallsyms_lookup_name(name);
	if (addr && !offset) {
#ifdef CONFIG_KPROBES_ON_FTRACE
		unsigned long faddr;
		/*
		 * Per livepatch.h, ftrace location is always within the first
		 * 16 bytes of a function on powerpc with -mprofile-kernel.
		 */
		faddr = ftrace_location_range((unsigned long)addr,
					      (unsigned long)addr + 16);
		if (faddr)
			addr = (kprobe_opcode_t *)faddr;
		else
#endif
			addr = (kprobe_opcode_t *)ppc_function_entry(addr);
	}
#elif defined(PPC64_ELF_ABI_v1)
	/*
	 * 64bit powerpc ABIv1 uses function descriptors:
	 * - Check for the dot variant of the symbol first.
	 * - If that fails, try looking up the symbol provided.
	 *
	 * This ensures we always get to the actual symbol and not
	 * the descriptor.
	 *
	 * Also handle <module:symbol> format.
	 */
	char dot_name[MODULE_NAME_LEN + 1 + KSYM_NAME_LEN];
	bool dot_appended = false;
	const char *c;
	ssize_t ret = 0;
	int len = 0;

	if ((c = strnchr(name, MODULE_NAME_LEN, ':')) != NULL) {
		c++;
		len = c - name;
		memcpy(dot_name, name, len);
	} else
		c = name;

	if (*c != '\0' && *c != '.') {
		dot_name[len++] = '.';
		dot_appended = true;
	}
	ret = strscpy(dot_name + len, c, KSYM_NAME_LEN);
	if (ret > 0)
		addr = (kprobe_opcode_t *)kallsyms_lookup_name(dot_name);

	/* Fallback to the original non-dot symbol lookup */
	if (!addr && dot_appended)
		addr = (kprobe_opcode_t *)kallsyms_lookup_name(name);
#else
	addr = (kprobe_opcode_t *)kallsyms_lookup_name(name);
#endif

	return addr;
}

int arch_prepare_kprobe(struct kprobe *p)
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
NOKPROBE_SYMBOL(arch_prepare_kprobe);

void arch_arm_kprobe(struct kprobe *p)
{
	patch_instruction(p->addr, BREAKPOINT_INSTRUCTION);
}
NOKPROBE_SYMBOL(arch_arm_kprobe);

void arch_disarm_kprobe(struct kprobe *p)
{
	patch_instruction(p->addr, p->opcode);
}
NOKPROBE_SYMBOL(arch_disarm_kprobe);

void arch_remove_kprobe(struct kprobe *p)
{
	if (p->ainsn.insn) {
		free_insn_slot(p->ainsn.insn, 0);
		p->ainsn.insn = NULL;
	}
}
NOKPROBE_SYMBOL(arch_remove_kprobe);

static nokprobe_inline void prepare_singlestep(struct kprobe *p, struct pt_regs *regs)
{
	enable_single_step(regs);

	/*
	 * On powerpc we should single step on the original
	 * instruction even if the probed insn is a trap
	 * variant as values in regs could play a part in
	 * if the trap is taken or not
	 */
	regs->nip = (unsigned long)p->ainsn.insn;
}

static nokprobe_inline void save_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	kcb->prev_kprobe.kp = kprobe_running();
	kcb->prev_kprobe.status = kcb->kprobe_status;
	kcb->prev_kprobe.saved_msr = kcb->kprobe_saved_msr;
}

static nokprobe_inline void restore_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	__this_cpu_write(current_kprobe, kcb->prev_kprobe.kp);
	kcb->kprobe_status = kcb->prev_kprobe.status;
	kcb->kprobe_saved_msr = kcb->prev_kprobe.saved_msr;
}

static nokprobe_inline void set_current_kprobe(struct kprobe *p, struct pt_regs *regs,
				struct kprobe_ctlblk *kcb)
{
	__this_cpu_write(current_kprobe, p);
	kcb->kprobe_saved_msr = regs->msr;
}

bool arch_kprobe_on_func_entry(unsigned long offset)
{
#ifdef PPC64_ELF_ABI_v2
#ifdef CONFIG_KPROBES_ON_FTRACE
	return offset <= 16;
#else
	return offset <= 8;
#endif
#else
	return !offset;
#endif
}

void arch_prepare_kretprobe(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	ri->ret_addr = (kprobe_opcode_t *)regs->link;

	/* Replace the return addr with trampoline addr */
	regs->link = (unsigned long)kretprobe_trampoline;
}
NOKPROBE_SYMBOL(arch_prepare_kretprobe);

static int try_to_emulate(struct kprobe *p, struct pt_regs *regs)
{
	int ret;
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
	} else if (ret < 0) {
		/*
		 * We don't allow kprobes on mtmsr(d)/rfi(d), etc.
		 * So, we should never get here... but, its still
		 * good to catch them, just in case...
		 */
		printk("Can't step on instruction %x\n", insn);
		BUG();
	} else {
		/*
		 * If we haven't previously emulated this instruction, then it
		 * can't be boosted. Note it down so we don't try to do so again.
		 *
		 * If, however, we had emulated this instruction in the past,
		 * then this is just an error with the current run (for
		 * instance, exceptions due to a load/store). We return 0 so
		 * that this is now single-stepped, but continue to try
		 * emulating it in subsequent probe hits.
		 */
		if (unlikely(p->ainsn.boostable != 1))
			p->ainsn.boostable = -1;
	}

	return ret;
}
NOKPROBE_SYMBOL(try_to_emulate);

int kprobe_handler(struct pt_regs *regs)
{
	struct kprobe *p;
	int ret = 0;
	unsigned int *addr = (unsigned int *)regs->nip;
	struct kprobe_ctlblk *kcb;

	if (user_mode(regs))
		return 0;

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
			kprobes_inc_nmissed_count(p);
			kcb->kprobe_status = KPROBE_REENTER;
			if (p->ainsn.boostable >= 0) {
				ret = try_to_emulate(p, regs);

				if (ret > 0) {
					restore_previous_kprobe(kcb);
					preempt_enable_no_resched();
					return 1;
				}
			}
			prepare_singlestep(p, regs);
			return 1;
		} else if (*addr != BREAKPOINT_INSTRUCTION) {
			/* If trap variant, then it belongs not to us */
			kprobe_opcode_t cur_insn = *addr;

			if (is_trap(cur_insn))
				goto no_kprobe;
			/* The breakpoint instruction was removed by
			 * another cpu right after we hit, no further
			 * handling of this interrupt is appropriate
			 */
			ret = 1;
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
	if (p->pre_handler && p->pre_handler(p, regs)) {
		/* handler changed execution path, so skip ss setup */
		reset_current_kprobe();
		preempt_enable_no_resched();
		return 1;
	}

	if (p->ainsn.boostable >= 0) {
		ret = try_to_emulate(p, regs);

		if (ret > 0) {
			if (p->post_handler)
				p->post_handler(p, regs, 0);

			kcb->kprobe_status = KPROBE_HIT_SSDONE;
			reset_current_kprobe();
			preempt_enable_no_resched();
			return 1;
		}
	}
	prepare_singlestep(p, regs);
	kcb->kprobe_status = KPROBE_HIT_SS;
	return 1;

no_kprobe:
	preempt_enable_no_resched();
	return ret;
}
NOKPROBE_SYMBOL(kprobe_handler);

/*
 * Function return probe trampoline:
 * 	- init_kprobes() establishes a probepoint here
 * 	- When the probed function returns, this probe
 * 		causes the handlers to fire
 */
asm(".global kretprobe_trampoline\n"
	".type kretprobe_trampoline, @function\n"
	"kretprobe_trampoline:\n"
	"nop\n"
	"blr\n"
	".size kretprobe_trampoline, .-kretprobe_trampoline\n");

/*
 * Called when the probe at kretprobe trampoline is hit
 */
static int trampoline_probe_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct kretprobe_instance *ri = NULL;
	struct hlist_head *head, empty_rp;
	struct hlist_node *tmp;
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
	hlist_for_each_entry_safe(ri, tmp, head, hlist) {
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

	/*
	 * We get here through one of two paths:
	 * 1. by taking a trap -> kprobe_handler() -> here
	 * 2. by optprobe branch -> optimized_callback() -> opt_pre_handler() -> here
	 *
	 * When going back through (1), we need regs->nip to be setup properly
	 * as it is used to determine the return address from the trap.
	 * For (2), since nip is not honoured with optprobes, we instead setup
	 * the link register properly so that the subsequent 'blr' in
	 * kretprobe_trampoline jumps back to the right instruction.
	 *
	 * For nip, we should set the address to the previous instruction since
	 * we end up emulating it in kprobe_handler(), which increments the nip
	 * again.
	 */
	regs->nip = orig_ret_address - 4;
	regs->link = orig_ret_address;

	kretprobe_hash_unlock(current, &flags);

	hlist_for_each_entry_safe(ri, tmp, &empty_rp, hlist) {
		hlist_del(&ri->hlist);
		kfree(ri);
	}

	return 0;
}
NOKPROBE_SYMBOL(trampoline_probe_handler);

/*
 * Called after single-stepping.  p->addr is the address of the
 * instruction whose first byte has been replaced by the "breakpoint"
 * instruction.  To avoid the SMP problems that can occur when we
 * temporarily put back the original opcode to single-step, we
 * single-stepped a copy of the instruction.  The address of this
 * copy is p->ainsn.insn.
 */
int kprobe_post_handler(struct pt_regs *regs)
{
	struct kprobe *cur = kprobe_running();
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	if (!cur || user_mode(regs))
		return 0;

	/* make sure we got here for instruction we have a kprobe on */
	if (((unsigned long)cur->ainsn.insn + 4) != regs->nip)
		return 0;

	if ((kcb->kprobe_status != KPROBE_REENTER) && cur->post_handler) {
		kcb->kprobe_status = KPROBE_HIT_SSDONE;
		cur->post_handler(cur, regs, 0);
	}

	/* Adjust nip to after the single-stepped instruction */
	regs->nip = (unsigned long)cur->addr + 4;
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
NOKPROBE_SYMBOL(kprobe_post_handler);

int kprobe_fault_handler(struct pt_regs *regs, int trapnr)
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
		if (cur->fault_handler && cur->fault_handler(cur, regs, trapnr))
			return 1;

		/*
		 * In case the user-specified fault handler returned
		 * zero, try to fix up.
		 */
		if ((entry = search_exception_tables(regs->nip)) != NULL) {
			regs->nip = extable_fixup(entry);
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
NOKPROBE_SYMBOL(kprobe_fault_handler);

unsigned long arch_deref_entry_point(void *entry)
{
#ifdef PPC64_ELF_ABI_v1
	if (!kernel_text_address((unsigned long)entry))
		return ppc_global_function_entry(entry);
	else
#endif
		return (unsigned long)entry;
}
NOKPROBE_SYMBOL(arch_deref_entry_point);

static struct kprobe trampoline_p = {
	.addr = (kprobe_opcode_t *) &kretprobe_trampoline,
	.pre_handler = trampoline_probe_handler
};

int __init arch_init_kprobes(void)
{
	return register_kprobe(&trampoline_p);
}

int arch_trampoline_kprobe(struct kprobe *p)
{
	if (p->addr == (kprobe_opcode_t *)&kretprobe_trampoline)
		return 1;

	return 0;
}
NOKPROBE_SYMBOL(arch_trampoline_kprobe);
