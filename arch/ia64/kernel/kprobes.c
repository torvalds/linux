/*
 *  Kernel Probes (KProbes)
 *  arch/ia64/kernel/kprobes.c
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
 * Copyright (C) Intel Corporation, 2005
 *
 * 2005-Apr     Rusty Lynch <rusty.lynch@intel.com> and Anil S Keshavamurthy
 *              <anil.s.keshavamurthy@intel.com> adapted from i386
 */

#include <linux/config.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/preempt.h>
#include <linux/moduleloader.h>

#include <asm/pgtable.h>
#include <asm/kdebug.h>
#include <asm/sections.h>

extern void jprobe_inst_return(void);

DEFINE_PER_CPU(struct kprobe *, current_kprobe) = NULL;
DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

enum instruction_type {A, I, M, F, B, L, X, u};
static enum instruction_type bundle_encoding[32][3] = {
  { M, I, I },				/* 00 */
  { M, I, I },				/* 01 */
  { M, I, I },				/* 02 */
  { M, I, I },				/* 03 */
  { M, L, X },				/* 04 */
  { M, L, X },				/* 05 */
  { u, u, u },  			/* 06 */
  { u, u, u },  			/* 07 */
  { M, M, I },				/* 08 */
  { M, M, I },				/* 09 */
  { M, M, I },				/* 0A */
  { M, M, I },				/* 0B */
  { M, F, I },				/* 0C */
  { M, F, I },				/* 0D */
  { M, M, F },				/* 0E */
  { M, M, F },				/* 0F */
  { M, I, B },				/* 10 */
  { M, I, B },				/* 11 */
  { M, B, B },				/* 12 */
  { M, B, B },				/* 13 */
  { u, u, u },  			/* 14 */
  { u, u, u },  			/* 15 */
  { B, B, B },				/* 16 */
  { B, B, B },				/* 17 */
  { M, M, B },				/* 18 */
  { M, M, B },				/* 19 */
  { u, u, u },  			/* 1A */
  { u, u, u },  			/* 1B */
  { M, F, B },				/* 1C */
  { M, F, B },				/* 1D */
  { u, u, u },  			/* 1E */
  { u, u, u },  			/* 1F */
};

/*
 * In this function we check to see if the instruction
 * is IP relative instruction and update the kprobe
 * inst flag accordingly
 */
static void __kprobes update_kprobe_inst_flag(uint template, uint  slot,
					      uint major_opcode,
					      unsigned long kprobe_inst,
					      struct kprobe *p)
{
	p->ainsn.inst_flag = 0;
	p->ainsn.target_br_reg = 0;

	/* Check for Break instruction
 	 * Bits 37:40 Major opcode to be zero
	 * Bits 27:32 X6 to be zero
	 * Bits 32:35 X3 to be zero
	 */
	if ((!major_opcode) && (!((kprobe_inst >> 27) & 0x1FF)) ) {
		/* is a break instruction */
	 	p->ainsn.inst_flag |= INST_FLAG_BREAK_INST;
		return;
	}

	if (bundle_encoding[template][slot] == B) {
		switch (major_opcode) {
		  case INDIRECT_CALL_OPCODE:
	 		p->ainsn.inst_flag |= INST_FLAG_FIX_BRANCH_REG;
 			p->ainsn.target_br_reg = ((kprobe_inst >> 6) & 0x7);
 			break;
		  case IP_RELATIVE_PREDICT_OPCODE:
		  case IP_RELATIVE_BRANCH_OPCODE:
			p->ainsn.inst_flag |= INST_FLAG_FIX_RELATIVE_IP_ADDR;
 			break;
		  case IP_RELATIVE_CALL_OPCODE:
 			p->ainsn.inst_flag |= INST_FLAG_FIX_RELATIVE_IP_ADDR;
 			p->ainsn.inst_flag |= INST_FLAG_FIX_BRANCH_REG;
 			p->ainsn.target_br_reg = ((kprobe_inst >> 6) & 0x7);
 			break;
		}
 	} else if (bundle_encoding[template][slot] == X) {
		switch (major_opcode) {
		  case LONG_CALL_OPCODE:
			p->ainsn.inst_flag |= INST_FLAG_FIX_BRANCH_REG;
			p->ainsn.target_br_reg = ((kprobe_inst >> 6) & 0x7);
		  break;
		}
	}
	return;
}

/*
 * In this function we check to see if the instruction
 * on which we are inserting kprobe is supported.
 * Returns 0 if supported
 * Returns -EINVAL if unsupported
 */
static int __kprobes unsupported_inst(uint template, uint  slot,
				      uint major_opcode,
				      unsigned long kprobe_inst,
				      struct kprobe *p)
{
	unsigned long addr = (unsigned long)p->addr;

	if (bundle_encoding[template][slot] == I) {
		switch (major_opcode) {
			case 0x0: //I_UNIT_MISC_OPCODE:
			/*
			 * Check for Integer speculation instruction
			 * - Bit 33-35 to be equal to 0x1
			 */
			if (((kprobe_inst >> 33) & 0x7) == 1) {
				printk(KERN_WARNING
					"Kprobes on speculation inst at <0x%lx> not supported\n",
					addr);
				return -EINVAL;
			}

			/*
			 * IP relative mov instruction
			 *  - Bit 27-35 to be equal to 0x30
			 */
			if (((kprobe_inst >> 27) & 0x1FF) == 0x30) {
				printk(KERN_WARNING
					"Kprobes on \"mov r1=ip\" at <0x%lx> not supported\n",
					addr);
				return -EINVAL;

			}
		}
	}
	return 0;
}


/*
 * In this function we check to see if the instruction
 * (qp) cmpx.crel.ctype p1,p2=r2,r3
 * on which we are inserting kprobe is cmp instruction
 * with ctype as unc.
 */
static uint __kprobes is_cmp_ctype_unc_inst(uint template, uint slot,
					    uint major_opcode,
					    unsigned long kprobe_inst)
{
	cmp_inst_t cmp_inst;
	uint ctype_unc = 0;

	if (!((bundle_encoding[template][slot] == I) ||
		(bundle_encoding[template][slot] == M)))
		goto out;

	if (!((major_opcode == 0xC) || (major_opcode == 0xD) ||
		(major_opcode == 0xE)))
		goto out;

	cmp_inst.l = kprobe_inst;
	if ((cmp_inst.f.x2 == 0) || (cmp_inst.f.x2 == 1)) {
		/* Integere compare - Register Register (A6 type)*/
		if ((cmp_inst.f.tb == 0) && (cmp_inst.f.ta == 0)
				&&(cmp_inst.f.c == 1))
			ctype_unc = 1;
	} else if ((cmp_inst.f.x2 == 2)||(cmp_inst.f.x2 == 3)) {
		/* Integere compare - Immediate Register (A8 type)*/
		if ((cmp_inst.f.ta == 0) &&(cmp_inst.f.c == 1))
			ctype_unc = 1;
	}
out:
	return ctype_unc;
}

/*
 * In this function we override the bundle with
 * the break instruction at the given slot.
 */
static void __kprobes prepare_break_inst(uint template, uint  slot,
					 uint major_opcode,
					 unsigned long kprobe_inst,
					 struct kprobe *p)
{
	unsigned long break_inst = BREAK_INST;
	bundle_t *bundle = &p->ainsn.insn.bundle;

	/*
	 * Copy the original kprobe_inst qualifying predicate(qp)
	 * to the break instruction iff !is_cmp_ctype_unc_inst
	 * because for cmp instruction with ctype equal to unc,
	 * which is a special instruction always needs to be
	 * executed regradless of qp
	 */
	if (!is_cmp_ctype_unc_inst(template, slot, major_opcode, kprobe_inst))
		break_inst |= (0x3f & kprobe_inst);

	switch (slot) {
	  case 0:
		bundle->quad0.slot0 = break_inst;
		break;
	  case 1:
		bundle->quad0.slot1_p0 = break_inst;
		bundle->quad1.slot1_p1 = break_inst >> (64-46);
		break;
	  case 2:
		bundle->quad1.slot2 = break_inst;
		break;
	}

	/*
	 * Update the instruction flag, so that we can
	 * emulate the instruction properly after we
	 * single step on original instruction
	 */
	update_kprobe_inst_flag(template, slot, major_opcode, kprobe_inst, p);
}

static inline void get_kprobe_inst(bundle_t *bundle, uint slot,
	       	unsigned long *kprobe_inst, uint *major_opcode)
{
	unsigned long kprobe_inst_p0, kprobe_inst_p1;
	unsigned int template;

	template = bundle->quad0.template;

	switch (slot) {
	  case 0:
 		*major_opcode = (bundle->quad0.slot0 >> SLOT0_OPCODE_SHIFT);
 		*kprobe_inst = bundle->quad0.slot0;
		break;
	  case 1:
 		*major_opcode = (bundle->quad1.slot1_p1 >> SLOT1_p1_OPCODE_SHIFT);
  		kprobe_inst_p0 = bundle->quad0.slot1_p0;
  		kprobe_inst_p1 = bundle->quad1.slot1_p1;
  		*kprobe_inst = kprobe_inst_p0 | (kprobe_inst_p1 << (64-46));
		break;
	  case 2:
 		*major_opcode = (bundle->quad1.slot2 >> SLOT2_OPCODE_SHIFT);
 		*kprobe_inst = bundle->quad1.slot2;
		break;
	}
}

/* Returns non-zero if the addr is in the Interrupt Vector Table */
static inline int in_ivt_functions(unsigned long addr)
{
	return (addr >= (unsigned long)__start_ivt_text
		&& addr < (unsigned long)__end_ivt_text);
}

static int __kprobes valid_kprobe_addr(int template, int slot,
				       unsigned long addr)
{
	if ((slot > 2) || ((bundle_encoding[template][1] == L) && slot > 1)) {
		printk(KERN_WARNING "Attempting to insert unaligned kprobe "
				"at 0x%lx\n", addr);
		return -EINVAL;
	}

 	if (in_ivt_functions(addr)) {
 		printk(KERN_WARNING "Kprobes can't be inserted inside "
				"IVT functions at 0x%lx\n", addr);
 		return -EINVAL;
 	}

	if (slot == 1 && bundle_encoding[template][1] != L) {
		printk(KERN_WARNING "Inserting kprobes on slot #1 "
		       "is not supported\n");
		return -EINVAL;
	}

	return 0;
}

static inline void save_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	kcb->prev_kprobe.kp = kprobe_running();
	kcb->prev_kprobe.status = kcb->kprobe_status;
}

static inline void restore_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	__get_cpu_var(current_kprobe) = kcb->prev_kprobe.kp;
	kcb->kprobe_status = kcb->prev_kprobe.status;
}

static inline void set_current_kprobe(struct kprobe *p,
			struct kprobe_ctlblk *kcb)
{
	__get_cpu_var(current_kprobe) = p;
}

static void kretprobe_trampoline(void)
{
}

/*
 * At this point the target function has been tricked into
 * returning into our trampoline.  Lookup the associated instance
 * and then:
 *    - call the handler function
 *    - cleanup by marking the instance as unused
 *    - long jump back to the original return address
 */
int __kprobes trampoline_probe_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct kretprobe_instance *ri = NULL;
	struct hlist_head *head;
	struct hlist_node *node, *tmp;
	unsigned long flags, orig_ret_address = 0;
	unsigned long trampoline_address =
		((struct fnptr *)kretprobe_trampoline)->ip;

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
	regs->cr_iip = orig_ret_address;

	reset_current_kprobe();
	spin_unlock_irqrestore(&kretprobe_lock, flags);
	preempt_enable_no_resched();

	/*
	 * By returning a non-zero value, we are telling
	 * kprobe_handler() that we don't want the post_handler
	 * to run (and have re-enabled preemption)
	 */
	return 1;
}

/* Called with kretprobe_lock held */
void __kprobes arch_prepare_kretprobe(struct kretprobe *rp,
				      struct pt_regs *regs)
{
	struct kretprobe_instance *ri;

	if ((ri = get_free_rp_inst(rp)) != NULL) {
		ri->rp = rp;
		ri->task = current;
		ri->ret_addr = (kprobe_opcode_t *)regs->b0;

		/* Replace the return addr with trampoline addr */
		regs->b0 = ((struct fnptr *)kretprobe_trampoline)->ip;

		add_rp_inst(ri);
	} else {
		rp->nmissed++;
	}
}

int __kprobes arch_prepare_kprobe(struct kprobe *p)
{
	unsigned long addr = (unsigned long) p->addr;
	unsigned long *kprobe_addr = (unsigned long *)(addr & ~0xFULL);
	unsigned long kprobe_inst=0;
	unsigned int slot = addr & 0xf, template, major_opcode = 0;
	bundle_t *bundle = &p->ainsn.insn.bundle;

	memcpy(&p->opcode.bundle, kprobe_addr, sizeof(bundle_t));
	memcpy(&p->ainsn.insn.bundle, kprobe_addr, sizeof(bundle_t));

 	template = bundle->quad0.template;

	if(valid_kprobe_addr(template, slot, addr))
		return -EINVAL;

	/* Move to slot 2, if bundle is MLX type and kprobe slot is 1 */
 	if (slot == 1 && bundle_encoding[template][1] == L)
  		slot++;

	/* Get kprobe_inst and major_opcode from the bundle */
	get_kprobe_inst(bundle, slot, &kprobe_inst, &major_opcode);

	if (unsupported_inst(template, slot, major_opcode, kprobe_inst, p))
			return -EINVAL;

	prepare_break_inst(template, slot, major_opcode, kprobe_inst, p);

	return 0;
}

void __kprobes arch_arm_kprobe(struct kprobe *p)
{
	unsigned long addr = (unsigned long)p->addr;
	unsigned long arm_addr = addr & ~0xFULL;

	memcpy((char *)arm_addr, &p->ainsn.insn.bundle, sizeof(bundle_t));
	flush_icache_range(arm_addr, arm_addr + sizeof(bundle_t));
}

void __kprobes arch_disarm_kprobe(struct kprobe *p)
{
	unsigned long addr = (unsigned long)p->addr;
	unsigned long arm_addr = addr & ~0xFULL;

	/* p->opcode contains the original unaltered bundle */
	memcpy((char *) arm_addr, (char *) &p->opcode.bundle, sizeof(bundle_t));
	flush_icache_range(arm_addr, arm_addr + sizeof(bundle_t));
}

void __kprobes arch_remove_kprobe(struct kprobe *p)
{
}

/*
 * We are resuming execution after a single step fault, so the pt_regs
 * structure reflects the register state after we executed the instruction
 * located in the kprobe (p->ainsn.insn.bundle).  We still need to adjust
 * the ip to point back to the original stack address. To set the IP address
 * to original stack address, handle the case where we need to fixup the
 * relative IP address and/or fixup branch register.
 */
static void __kprobes resume_execution(struct kprobe *p, struct pt_regs *regs)
{
  	unsigned long bundle_addr = ((unsigned long) (&p->opcode.bundle)) & ~0xFULL;
  	unsigned long resume_addr = (unsigned long)p->addr & ~0xFULL;
 	unsigned long template;
 	int slot = ((unsigned long)p->addr & 0xf);

	template = p->opcode.bundle.quad0.template;

 	if (slot == 1 && bundle_encoding[template][1] == L)
 		slot = 2;

	if (p->ainsn.inst_flag) {

		if (p->ainsn.inst_flag & INST_FLAG_FIX_RELATIVE_IP_ADDR) {
			/* Fix relative IP address */
 			regs->cr_iip = (regs->cr_iip - bundle_addr) + resume_addr;
		}

		if (p->ainsn.inst_flag & INST_FLAG_FIX_BRANCH_REG) {
		/*
		 * Fix target branch register, software convention is
		 * to use either b0 or b6 or b7, so just checking
		 * only those registers
		 */
			switch (p->ainsn.target_br_reg) {
			case 0:
				if ((regs->b0 == bundle_addr) ||
					(regs->b0 == bundle_addr + 0x10)) {
					regs->b0 = (regs->b0 - bundle_addr) +
						resume_addr;
				}
				break;
			case 6:
				if ((regs->b6 == bundle_addr) ||
					(regs->b6 == bundle_addr + 0x10)) {
					regs->b6 = (regs->b6 - bundle_addr) +
						resume_addr;
				}
				break;
			case 7:
				if ((regs->b7 == bundle_addr) ||
					(regs->b7 == bundle_addr + 0x10)) {
					regs->b7 = (regs->b7 - bundle_addr) +
						resume_addr;
				}
				break;
			} /* end switch */
		}
		goto turn_ss_off;
	}

	if (slot == 2) {
 		if (regs->cr_iip == bundle_addr + 0x10) {
 			regs->cr_iip = resume_addr + 0x10;
 		}
 	} else {
 		if (regs->cr_iip == bundle_addr) {
 			regs->cr_iip = resume_addr;
 		}
	}

turn_ss_off:
  	/* Turn off Single Step bit */
  	ia64_psr(regs)->ss = 0;
}

static void __kprobes prepare_ss(struct kprobe *p, struct pt_regs *regs)
{
	unsigned long bundle_addr = (unsigned long) &p->opcode.bundle;
	unsigned long slot = (unsigned long)p->addr & 0xf;

	/* single step inline if break instruction */
	if (p->ainsn.inst_flag == INST_FLAG_BREAK_INST)
		regs->cr_iip = (unsigned long)p->addr & ~0xFULL;
	else
		regs->cr_iip = bundle_addr & ~0xFULL;

	if (slot > 2)
		slot = 0;

	ia64_psr(regs)->ri = slot;

	/* turn on single stepping */
	ia64_psr(regs)->ss = 1;
}

static int __kprobes is_ia64_break_inst(struct pt_regs *regs)
{
	unsigned int slot = ia64_psr(regs)->ri;
	unsigned int template, major_opcode;
	unsigned long kprobe_inst;
	unsigned long *kprobe_addr = (unsigned long *)regs->cr_iip;
	bundle_t bundle;

	memcpy(&bundle, kprobe_addr, sizeof(bundle_t));
	template = bundle.quad0.template;

	/* Move to slot 2, if bundle is MLX type and kprobe slot is 1 */
	if (slot == 1 && bundle_encoding[template][1] == L)
  		slot++;

	/* Get Kprobe probe instruction at given slot*/
	get_kprobe_inst(&bundle, slot, &kprobe_inst, &major_opcode);

	/* For break instruction,
	 * Bits 37:40 Major opcode to be zero
	 * Bits 27:32 X6 to be zero
	 * Bits 32:35 X3 to be zero
	 */
	if (major_opcode || ((kprobe_inst >> 27) & 0x1FF) ) {
		/* Not a break instruction */
		return 0;
	}

	/* Is a break instruction */
	return 1;
}

static int __kprobes pre_kprobes_handler(struct die_args *args)
{
	struct kprobe *p;
	int ret = 0;
	struct pt_regs *regs = args->regs;
	kprobe_opcode_t *addr = (kprobe_opcode_t *)instruction_pointer(regs);
	struct kprobe_ctlblk *kcb;

	/*
	 * We don't want to be preempted for the entire
	 * duration of kprobe processing
	 */
	preempt_disable();
	kcb = get_kprobe_ctlblk();

	/* Handle recursion cases */
	if (kprobe_running()) {
		p = get_kprobe(addr);
		if (p) {
			if ((kcb->kprobe_status == KPROBE_HIT_SS) &&
	 		     (p->ainsn.inst_flag == INST_FLAG_BREAK_INST)) {
  				ia64_psr(regs)->ss = 0;
				goto no_kprobe;
			}
			/* We have reentered the pre_kprobe_handler(), since
			 * another probe was hit while within the handler.
			 * We here save the original kprobes variables and
			 * just single step on the instruction of the new probe
			 * without calling any user handlers.
			 */
			save_previous_kprobe(kcb);
			set_current_kprobe(p, kcb);
			p->nmissed++;
			prepare_ss(p, regs);
			kcb->kprobe_status = KPROBE_REENTER;
			return 1;
		} else if (args->err == __IA64_BREAK_JPROBE) {
			/*
			 * jprobe instrumented function just completed
			 */
			p = __get_cpu_var(current_kprobe);
			if (p->break_handler && p->break_handler(p, regs)) {
				goto ss_probe;
			}
		} else {
			/* Not our break */
			goto no_kprobe;
		}
	}

	p = get_kprobe(addr);
	if (!p) {
		if (!is_ia64_break_inst(regs)) {
			/*
			 * The breakpoint instruction was removed right
			 * after we hit it.  Another cpu has removed
			 * either a probepoint or a debugger breakpoint
			 * at this address.  In either case, no further
			 * handling of this interrupt is appropriate.
			 */
			ret = 1;

		}

		/* Not one of our break, let kernel handle it */
		goto no_kprobe;
	}

	set_current_kprobe(p, kcb);
	kcb->kprobe_status = KPROBE_HIT_ACTIVE;

	if (p->pre_handler && p->pre_handler(p, regs))
		/*
		 * Our pre-handler is specifically requesting that we just
		 * do a return.  This is used for both the jprobe pre-handler
		 * and the kretprobe trampoline
		 */
		return 1;

ss_probe:
	prepare_ss(p, regs);
	kcb->kprobe_status = KPROBE_HIT_SS;
	return 1;

no_kprobe:
	preempt_enable_no_resched();
	return ret;
}

static int __kprobes post_kprobes_handler(struct pt_regs *regs)
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
	return 1;
}

static int __kprobes kprobes_fault_handler(struct pt_regs *regs, int trapnr)
{
	struct kprobe *cur = kprobe_running();
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	if (cur->fault_handler && cur->fault_handler(cur, regs, trapnr))
		return 1;

	if (kcb->kprobe_status & KPROBE_HIT_SS) {
		resume_execution(cur, regs);
		reset_current_kprobe();
		preempt_enable_no_resched();
	}

	return 0;
}

int __kprobes kprobe_exceptions_notify(struct notifier_block *self,
				       unsigned long val, void *data)
{
	struct die_args *args = (struct die_args *)data;
	int ret = NOTIFY_DONE;

	switch(val) {
	case DIE_BREAK:
		/* err is break number from ia64_bad_break() */
		if (args->err == 0x80200 || args->err == 0x80300)
			if (pre_kprobes_handler(args))
				ret = NOTIFY_STOP;
		break;
	case DIE_FAULT:
		/* err is vector number from ia64_fault() */
		if (args->err == 36)
			if (post_kprobes_handler(args->regs))
				ret = NOTIFY_STOP;
		break;
	case DIE_PAGE_FAULT:
		/* kprobe_running() needs smp_processor_id() */
		preempt_disable();
		if (kprobe_running() &&
			kprobes_fault_handler(args->regs, args->trapnr))
			ret = NOTIFY_STOP;
		preempt_enable();
	default:
		break;
	}
	return ret;
}

int __kprobes setjmp_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct jprobe *jp = container_of(p, struct jprobe, kp);
	unsigned long addr = ((struct fnptr *)(jp->entry))->ip;
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	/* save architectural state */
	kcb->jprobe_saved_regs = *regs;

	/* after rfi, execute the jprobe instrumented function */
	regs->cr_iip = addr & ~0xFULL;
	ia64_psr(regs)->ri = addr & 0xf;
	regs->r1 = ((struct fnptr *)(jp->entry))->gp;

	/*
	 * fix the return address to our jprobe_inst_return() function
	 * in the jprobes.S file
	 */
 	regs->b0 = ((struct fnptr *)(jprobe_inst_return))->ip;

	return 1;
}

int __kprobes longjmp_break_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();

	*regs = kcb->jprobe_saved_regs;
	preempt_enable_no_resched();
	return 1;
}

static struct kprobe trampoline_p = {
	.pre_handler = trampoline_probe_handler
};

int __init arch_init_kprobes(void)
{
	trampoline_p.addr =
		(kprobe_opcode_t *)((struct fnptr *)kretprobe_trampoline)->ip;
	return register_kprobe(&trampoline_p);
}
