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

#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/preempt.h>
#include <linux/moduleloader.h>
#include <linux/kdebug.h>

#include <asm/pgtable.h>
#include <asm/sections.h>
#include <asm/uaccess.h>

extern void jprobe_inst_return(void);

DEFINE_PER_CPU(struct kprobe *, current_kprobe) = NULL;
DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

struct kretprobe_blackpoint kretprobe_blacklist[] = {{NULL, NULL}};

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
	p->ainsn.slot = slot;

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
		/* Integer compare - Register Register (A6 type)*/
		if ((cmp_inst.f.tb == 0) && (cmp_inst.f.ta == 0)
				&&(cmp_inst.f.c == 1))
			ctype_unc = 1;
	} else if ((cmp_inst.f.x2 == 2)||(cmp_inst.f.x2 == 3)) {
		/* Integer compare - Immediate Register (A8 type)*/
		if ((cmp_inst.f.ta == 0) &&(cmp_inst.f.c == 1))
			ctype_unc = 1;
	}
out:
	return ctype_unc;
}

/*
 * In this function we check to see if the instruction
 * on which we are inserting kprobe is supported.
 * Returns qp value if supported
 * Returns -EINVAL if unsupported
 */
static int __kprobes unsupported_inst(uint template, uint  slot,
				      uint major_opcode,
				      unsigned long kprobe_inst,
				      unsigned long addr)
{
	int qp;

	qp = kprobe_inst & 0x3f;
	if (is_cmp_ctype_unc_inst(template, slot, major_opcode, kprobe_inst)) {
		if (slot == 1 && qp)  {
			printk(KERN_WARNING "Kprobes on cmp unc "
					"instruction on slot 1 at <0x%lx> "
					"is not supported\n", addr);
			return -EINVAL;

		}
		qp = 0;
	}
	else if (bundle_encoding[template][slot] == I) {
		if (major_opcode == 0) {
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
		else if ((major_opcode == 5) &&	!(kprobe_inst & (0xFUl << 33)) &&
				(kprobe_inst & (0x1UL << 12))) {
			/* test bit instructions, tbit,tnat,tf
			 * bit 33-36 to be equal to 0
			 * bit 12 to be equal to 1
			 */
			if (slot == 1 && qp) {
				printk(KERN_WARNING "Kprobes on test bit "
						"instruction on slot at <0x%lx> "
						"is not supported\n", addr);
				return -EINVAL;
			}
			qp = 0;
		}
	}
	else if (bundle_encoding[template][slot] == B) {
		if (major_opcode == 7) {
			/* IP-Relative Predict major code is 7 */
			printk(KERN_WARNING "Kprobes on IP-Relative"
					"Predict is not supported\n");
			return -EINVAL;
		}
		else if (major_opcode == 2) {
			/* Indirect Predict, major code is 2
			 * bit 27-32 to be equal to 10 or 11
			 */
			int x6=(kprobe_inst >> 27) & 0x3F;
			if ((x6 == 0x10) || (x6 == 0x11)) {
				printk(KERN_WARNING "Kprobes on "
					"Indirect Predict is not supported\n");
				return -EINVAL;
			}
		}
	}
	/* kernel does not use float instruction, here for safety kprobe
	 * will judge whether it is fcmp/flass/float approximation instruction
	 */
	else if (unlikely(bundle_encoding[template][slot] == F)) {
		if ((major_opcode == 4 || major_opcode == 5) &&
				(kprobe_inst  & (0x1 << 12))) {
			/* fcmp/fclass unc instruction */
			if (slot == 1 && qp) {
				printk(KERN_WARNING "Kprobes on fcmp/fclass "
					"instruction on slot at <0x%lx> "
					"is not supported\n", addr);
				return -EINVAL;

			}
			qp = 0;
		}
		if ((major_opcode == 0 || major_opcode == 1) &&
			(kprobe_inst & (0x1UL << 33))) {
			/* float Approximation instruction */
			if (slot == 1 && qp) {
				printk(KERN_WARNING "Kprobes on float Approx "
					"instr at <0x%lx> is not supported\n",
						addr);
				return -EINVAL;
			}
			qp = 0;
		}
	}
	return qp;
}

/*
 * In this function we override the bundle with
 * the break instruction at the given slot.
 */
static void __kprobes prepare_break_inst(uint template, uint  slot,
					 uint major_opcode,
					 unsigned long kprobe_inst,
					 struct kprobe *p,
					 int qp)
{
	unsigned long break_inst = BREAK_INST;
	bundle_t *bundle = &p->opcode.bundle;

	/*
	 * Copy the original kprobe_inst qualifying predicate(qp)
	 * to the break instruction
	 */
	break_inst |= qp;

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

static void __kprobes get_kprobe_inst(bundle_t *bundle, uint slot,
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
static int __kprobes in_ivt_functions(unsigned long addr)
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

	return 0;
}

static void __kprobes save_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	unsigned int i;
	i = atomic_add_return(1, &kcb->prev_kprobe_index);
	kcb->prev_kprobe[i-1].kp = kprobe_running();
	kcb->prev_kprobe[i-1].status = kcb->kprobe_status;
}

static void __kprobes restore_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	unsigned int i;
	i = atomic_sub_return(1, &kcb->prev_kprobe_index);
	__get_cpu_var(current_kprobe) = kcb->prev_kprobe[i].kp;
	kcb->kprobe_status = kcb->prev_kprobe[i].status;
}

static void __kprobes set_current_kprobe(struct kprobe *p,
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
	struct hlist_head *head, empty_rp;
	struct hlist_node *node, *tmp;
	unsigned long flags, orig_ret_address = 0;
	unsigned long trampoline_address =
		((struct fnptr *)kretprobe_trampoline)->ip;

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

		orig_ret_address = (unsigned long)ri->ret_addr;
		if (orig_ret_address != trampoline_address)
			/*
			 * This is the real return address. Any other
			 * instances associated with this task are for
			 * other calls deeper on the call stack
			 */
			break;
	}

	regs->cr_iip = orig_ret_address;

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

/* Called with kretprobe_lock held */
void __kprobes arch_prepare_kretprobe(struct kretprobe_instance *ri,
				      struct pt_regs *regs)
{
	ri->ret_addr = (kprobe_opcode_t *)regs->b0;

	/* Replace the return addr with trampoline addr */
	regs->b0 = ((struct fnptr *)kretprobe_trampoline)->ip;
}

int __kprobes arch_prepare_kprobe(struct kprobe *p)
{
	unsigned long addr = (unsigned long) p->addr;
	unsigned long *kprobe_addr = (unsigned long *)(addr & ~0xFULL);
	unsigned long kprobe_inst=0;
	unsigned int slot = addr & 0xf, template, major_opcode = 0;
	bundle_t *bundle;
	int qp;

	bundle = &((kprobe_opcode_t *)kprobe_addr)->bundle;
	template = bundle->quad0.template;

	if(valid_kprobe_addr(template, slot, addr))
		return -EINVAL;

	/* Move to slot 2, if bundle is MLX type and kprobe slot is 1 */
	if (slot == 1 && bundle_encoding[template][1] == L)
		slot++;

	/* Get kprobe_inst and major_opcode from the bundle */
	get_kprobe_inst(bundle, slot, &kprobe_inst, &major_opcode);

	qp = unsupported_inst(template, slot, major_opcode, kprobe_inst, addr);
	if (qp < 0)
		return -EINVAL;

	p->ainsn.insn = get_insn_slot();
	if (!p->ainsn.insn)
		return -ENOMEM;
	memcpy(&p->opcode, kprobe_addr, sizeof(kprobe_opcode_t));
	memcpy(p->ainsn.insn, kprobe_addr, sizeof(kprobe_opcode_t));

	prepare_break_inst(template, slot, major_opcode, kprobe_inst, p, qp);

	return 0;
}

void __kprobes arch_arm_kprobe(struct kprobe *p)
{
	unsigned long arm_addr;
	bundle_t *src, *dest;

	arm_addr = ((unsigned long)p->addr) & ~0xFUL;
	dest = &((kprobe_opcode_t *)arm_addr)->bundle;
	src = &p->opcode.bundle;

	flush_icache_range((unsigned long)p->ainsn.insn,
			(unsigned long)p->ainsn.insn + sizeof(kprobe_opcode_t));
	switch (p->ainsn.slot) {
		case 0:
			dest->quad0.slot0 = src->quad0.slot0;
			break;
		case 1:
			dest->quad1.slot1_p1 = src->quad1.slot1_p1;
			break;
		case 2:
			dest->quad1.slot2 = src->quad1.slot2;
			break;
	}
	flush_icache_range(arm_addr, arm_addr + sizeof(kprobe_opcode_t));
}

void __kprobes arch_disarm_kprobe(struct kprobe *p)
{
	unsigned long arm_addr;
	bundle_t *src, *dest;

	arm_addr = ((unsigned long)p->addr) & ~0xFUL;
	dest = &((kprobe_opcode_t *)arm_addr)->bundle;
	/* p->ainsn.insn contains the original unaltered kprobe_opcode_t */
	src = &p->ainsn.insn->bundle;
	switch (p->ainsn.slot) {
		case 0:
			dest->quad0.slot0 = src->quad0.slot0;
			break;
		case 1:
			dest->quad1.slot1_p1 = src->quad1.slot1_p1;
			break;
		case 2:
			dest->quad1.slot2 = src->quad1.slot2;
			break;
	}
	flush_icache_range(arm_addr, arm_addr + sizeof(kprobe_opcode_t));
}

void __kprobes arch_remove_kprobe(struct kprobe *p)
{
	mutex_lock(&kprobe_mutex);
	free_insn_slot(p->ainsn.insn, 0);
	mutex_unlock(&kprobe_mutex);
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
	unsigned long bundle_addr = (unsigned long) (&p->ainsn.insn->bundle);
	unsigned long resume_addr = (unsigned long)p->addr & ~0xFULL;
	unsigned long template;
	int slot = ((unsigned long)p->addr & 0xf);

	template = p->ainsn.insn->bundle.quad0.template;

	if (slot == 1 && bundle_encoding[template][1] == L)
		slot = 2;

	if (p->ainsn.inst_flag) {

		if (p->ainsn.inst_flag & INST_FLAG_FIX_RELATIVE_IP_ADDR) {
			/* Fix relative IP address */
			regs->cr_iip = (regs->cr_iip - bundle_addr) +
					resume_addr;
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
	unsigned long bundle_addr = (unsigned long) &p->ainsn.insn->bundle;
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
			kprobes_inc_nmissed_count(p);
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
		} else if (!is_ia64_break_inst(regs)) {
			/* The breakpoint instruction was removed by
			 * another cpu right after we hit, no further
			 * handling of this interrupt is appropriate
			 */
			ret = 1;
			goto no_kprobe;
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

int __kprobes kprobes_fault_handler(struct pt_regs *regs, int trapnr)
{
	struct kprobe *cur = kprobe_running();
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();


	switch(kcb->kprobe_status) {
	case KPROBE_HIT_SS:
	case KPROBE_REENTER:
		/*
		 * We are here because the instruction being single
		 * stepped caused a page fault. We reset the current
		 * kprobe and the instruction pointer points back to
		 * the probe address and allow the page fault handler
		 * to continue as a normal page fault.
		 */
		regs->cr_iip = ((unsigned long)cur->addr) & ~0xFULL;
		ia64_psr(regs)->ri = ((unsigned long)cur->addr) & 0xf;
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
		if (ia64_done_with_exception(regs))
			return 1;

		/*
		 * Let ia64_do_page_fault() fix it.
		 */
		break;
	default:
		break;
	}

	return 0;
}

int __kprobes kprobe_exceptions_notify(struct notifier_block *self,
				       unsigned long val, void *data)
{
	struct die_args *args = (struct die_args *)data;
	int ret = NOTIFY_DONE;

	if (args->regs && user_mode(args->regs))
		return ret;

	switch(val) {
	case DIE_BREAK:
		/* err is break number from ia64_bad_break() */
		if ((args->err >> 12) == (__IA64_BREAK_KPROBE >> 12)
			|| args->err == __IA64_BREAK_JPROBE
			|| args->err == 0)
			if (pre_kprobes_handler(args))
				ret = NOTIFY_STOP;
		break;
	case DIE_FAULT:
		/* err is vector number from ia64_fault() */
		if (args->err == 36)
			if (post_kprobes_handler(args->regs))
				ret = NOTIFY_STOP;
		break;
	default:
		break;
	}
	return ret;
}

struct param_bsp_cfm {
	unsigned long ip;
	unsigned long *bsp;
	unsigned long cfm;
};

static void ia64_get_bsp_cfm(struct unw_frame_info *info, void *arg)
{
	unsigned long ip;
	struct param_bsp_cfm *lp = arg;

	do {
		unw_get_ip(info, &ip);
		if (ip == 0)
			break;
		if (ip == lp->ip) {
			unw_get_bsp(info, (unsigned long*)&lp->bsp);
			unw_get_cfm(info, (unsigned long*)&lp->cfm);
			return;
		}
	} while (unw_unwind(info) >= 0);
	lp->bsp = NULL;
	lp->cfm = 0;
	return;
}

unsigned long arch_deref_entry_point(void *entry)
{
	return ((struct fnptr *)entry)->ip;
}

int __kprobes setjmp_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct jprobe *jp = container_of(p, struct jprobe, kp);
	unsigned long addr = arch_deref_entry_point(jp->entry);
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	struct param_bsp_cfm pa;
	int bytes;

	/*
	 * Callee owns the argument space and could overwrite it, eg
	 * tail call optimization. So to be absolutely safe
	 * we save the argument space before transferring the control
	 * to instrumented jprobe function which runs in
	 * the process context
	 */
	pa.ip = regs->cr_iip;
	unw_init_running(ia64_get_bsp_cfm, &pa);
	bytes = (char *)ia64_rse_skip_regs(pa.bsp, pa.cfm & 0x3f)
				- (char *)pa.bsp;
	memcpy( kcb->jprobes_saved_stacked_regs,
		pa.bsp,
		bytes );
	kcb->bsp = pa.bsp;
	kcb->cfm = pa.cfm;

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
	int bytes;

	/* restoring architectural state */
	*regs = kcb->jprobe_saved_regs;

	/* restoring the original argument space */
	flush_register_stack();
	bytes = (char *)ia64_rse_skip_regs(kcb->bsp, kcb->cfm & 0x3f)
				- (char *)kcb->bsp;
	memcpy( kcb->bsp,
		kcb->jprobes_saved_stacked_regs,
		bytes );
	invalidate_stacked_regs();

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

int __kprobes arch_trampoline_kprobe(struct kprobe *p)
{
	if (p->addr ==
		(kprobe_opcode_t *)((struct fnptr *)kretprobe_trampoline)->ip)
		return 1;

	return 0;
}
