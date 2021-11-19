// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Kernel Probes (KProbes)
 *  arch/ia64/kernel/kprobes.c
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
#include <linux/extable.h>
#include <linux/kdebug.h>
#include <linux/pgtable.h>

#include <asm/sections.h>
#include <asm/exception.h>

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

/* Insert a long branch code */
static void __kprobes set_brl_inst(void *from, void *to)
{
	s64 rel = ((s64) to - (s64) from) >> 4;
	bundle_t *brl;
	brl = (bundle_t *) ((u64) from & ~0xf);
	brl->quad0.template = 0x05;	/* [MLX](stop) */
	brl->quad0.slot0 = NOP_M_INST;	/* nop.m 0x0 */
	brl->quad0.slot1_p0 = ((rel >> 20) & 0x7fffffffff) << 2;
	brl->quad1.slot1_p1 = (((rel >> 20) & 0x7fffffffff) << 2) >> (64 - 46);
	/* brl.cond.sptk.many.clr rel<<4 (qp=0) */
	brl->quad1.slot2 = BRL_INST(rel >> 59, rel & 0xfffff);
}

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
	i = atomic_read(&kcb->prev_kprobe_index);
	__this_cpu_write(current_kprobe, kcb->prev_kprobe[i-1].kp);
	kcb->kprobe_status = kcb->prev_kprobe[i-1].status;
	atomic_sub(1, &kcb->prev_kprobe_index);
}

static void __kprobes set_current_kprobe(struct kprobe *p,
			struct kprobe_ctlblk *kcb)
{
	__this_cpu_write(current_kprobe, p);
}

static void kretprobe_trampoline(void)
{
}

int __kprobes trampoline_probe_handler(struct kprobe *p, struct pt_regs *regs)
{
	regs->cr_iip = __kretprobe_trampoline_handler(regs,
		dereference_function_descriptor(kretprobe_trampoline), NULL);
	/*
	 * By returning a non-zero value, we are telling
	 * kprobe_handler() that we don't want the post_handler
	 * to run (and have re-enabled preemption)
	 */
	return 1;
}

void __kprobes arch_prepare_kretprobe(struct kretprobe_instance *ri,
				      struct pt_regs *regs)
{
	ri->ret_addr = (kprobe_opcode_t *)regs->b0;
	ri->fp = NULL;

	/* Replace the return addr with trampoline addr */
	regs->b0 = (unsigned long)dereference_function_descriptor(kretprobe_trampoline);
}

/* Check the instruction in the slot is break */
static int __kprobes __is_ia64_break_inst(bundle_t *bundle, uint slot)
{
	unsigned int major_opcode;
	unsigned int template = bundle->quad0.template;
	unsigned long kprobe_inst;

	/* Move to slot 2, if bundle is MLX type and kprobe slot is 1 */
	if (slot == 1 && bundle_encoding[template][1] == L)
		slot++;

	/* Get Kprobe probe instruction at given slot*/
	get_kprobe_inst(bundle, slot, &kprobe_inst, &major_opcode);

	/* For break instruction,
	 * Bits 37:40 Major opcode to be zero
	 * Bits 27:32 X6 to be zero
	 * Bits 32:35 X3 to be zero
	 */
	if (major_opcode || ((kprobe_inst >> 27) & 0x1FF)) {
		/* Not a break instruction */
		return 0;
	}

	/* Is a break instruction */
	return 1;
}

/*
 * In this function, we check whether the target bundle modifies IP or
 * it triggers an exception. If so, it cannot be boostable.
 */
static int __kprobes can_boost(bundle_t *bundle, uint slot,
			       unsigned long bundle_addr)
{
	unsigned int template = bundle->quad0.template;

	do {
		if (search_exception_tables(bundle_addr + slot) ||
		    __is_ia64_break_inst(bundle, slot))
			return 0;	/* exception may occur in this bundle*/
	} while ((++slot) < 3);
	template &= 0x1e;
	if (template >= 0x10 /* including B unit */ ||
	    template == 0x04 /* including X unit */ ||
	    template == 0x06) /* undefined */
		return 0;

	return 1;
}

/* Prepare long jump bundle and disables other boosters if need */
static void __kprobes prepare_booster(struct kprobe *p)
{
	unsigned long addr = (unsigned long)p->addr & ~0xFULL;
	unsigned int slot = (unsigned long)p->addr & 0xf;
	struct kprobe *other_kp;

	if (can_boost(&p->ainsn.insn[0].bundle, slot, addr)) {
		set_brl_inst(&p->ainsn.insn[1].bundle, (bundle_t *)addr + 1);
		p->ainsn.inst_flag |= INST_FLAG_BOOSTABLE;
	}

	/* disables boosters in previous slots */
	for (; addr < (unsigned long)p->addr; addr++) {
		other_kp = get_kprobe((void *)addr);
		if (other_kp)
			other_kp->ainsn.inst_flag &= ~INST_FLAG_BOOSTABLE;
	}
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

	prepare_booster(p);

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
			   (unsigned long)p->ainsn.insn +
			   sizeof(kprobe_opcode_t) * MAX_INSN_SIZE);

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
	if (p->ainsn.insn) {
		free_insn_slot(p->ainsn.insn,
			       p->ainsn.inst_flag & INST_FLAG_BOOSTABLE);
		p->ainsn.insn = NULL;
	}
}
/*
 * We are resuming execution after a single step fault, so the pt_regs
 * structure reflects the register state after we executed the instruction
 * located in the kprobe (p->ainsn.insn->bundle).  We still need to adjust
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

	if (p->ainsn.inst_flag & ~INST_FLAG_BOOSTABLE) {

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
	unsigned long *kprobe_addr = (unsigned long *)regs->cr_iip;
	bundle_t bundle;

	memcpy(&bundle, kprobe_addr, sizeof(bundle_t));

	return __is_ia64_break_inst(&bundle, slot);
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

	if (p->pre_handler && p->pre_handler(p, regs)) {
		reset_current_kprobe();
		preempt_enable_no_resched();
		return 1;
	}

#if !defined(CONFIG_PREEMPTION)
	if (p->ainsn.inst_flag == INST_FLAG_BOOSTABLE && !p->post_handler) {
		/* Boost up -- we can execute copied instructions directly */
		ia64_psr(regs)->ri = p->ainsn.slot;
		regs->cr_iip = (unsigned long)&p->ainsn.insn->bundle & ~0xFULL;
		/* turn single stepping off */
		ia64_psr(regs)->ss = 0;

		reset_current_kprobe();
		preempt_enable_no_resched();
		return 1;
	}
#endif
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

unsigned long arch_deref_entry_point(void *entry)
{
	return ((struct fnptr *)entry)->ip;
}

static struct kprobe trampoline_p = {
	.pre_handler = trampoline_probe_handler
};

int __init arch_init_kprobes(void)
{
	trampoline_p.addr =
		dereference_function_descriptor(kretprobe_trampoline);
	return register_kprobe(&trampoline_p);
}

int __kprobes arch_trampoline_kprobe(struct kprobe *p)
{
	if (p->addr ==
		dereference_function_descriptor(kretprobe_trampoline))
		return 1;

	return 0;
}
