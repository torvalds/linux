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
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/preempt.h>
#include <linux/moduleloader.h>

#include <asm/pgtable.h>
#include <asm/kdebug.h>

extern void jprobe_inst_return(void);

/* kprobe_status settings */
#define KPROBE_HIT_ACTIVE	0x00000001
#define KPROBE_HIT_SS		0x00000002

static struct kprobe *current_kprobe;
static unsigned long kprobe_status;
static struct pt_regs jprobe_saved_regs;

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

int arch_prepare_kprobe(struct kprobe *p)
{
	unsigned long addr = (unsigned long) p->addr;
	unsigned long *bundle_addr = (unsigned long *)(addr & ~0xFULL);
	unsigned long slot = addr & 0xf;
	unsigned long template;
 	unsigned long major_opcode = 0;
 	unsigned long lx_type_inst = 0;
 	unsigned long kprobe_inst = 0;
	bundle_t *bundle = &p->ainsn.insn.bundle;

	memcpy(&p->opcode.bundle, bundle_addr, sizeof(bundle_t));
	memcpy(&p->ainsn.insn.bundle, bundle_addr, sizeof(bundle_t));

	p->ainsn.inst_flag = 0;
	p->ainsn.target_br_reg = 0;

 	template = bundle->quad0.template;

	if (((bundle_encoding[template][1] == L) && slot > 1) || (slot > 2)) {
		printk(KERN_WARNING "Attempting to insert unaligned kprobe at 0x%lx\n",
				addr);
		return -EINVAL;
	}

 	if (slot == 1 && bundle_encoding[template][1] == L) {
 		lx_type_inst = 1;
  		slot = 2;
 	}

	switch (slot) {
	case 0:
 		major_opcode = (bundle->quad0.slot0 >> SLOT0_OPCODE_SHIFT);
 		kprobe_inst = bundle->quad0.slot0;
		bundle->quad0.slot0 = BREAK_INST;
		break;
	case 1:
 		major_opcode = (bundle->quad1.slot1_p1 >> SLOT1_p1_OPCODE_SHIFT);
 		kprobe_inst = (bundle->quad0.slot1_p0 |
 				(bundle->quad1.slot1_p1 << (64-46)));
		bundle->quad0.slot1_p0 = BREAK_INST;
		bundle->quad1.slot1_p1 = (BREAK_INST >> (64-46));
		break;
	case 2:
 		major_opcode = (bundle->quad1.slot2 >> SLOT2_OPCODE_SHIFT);
 		kprobe_inst = bundle->quad1.slot2;
		bundle->quad1.slot2 = BREAK_INST;
		break;
	}

 	/*
 	 * Look for IP relative Branches, IP relative call or
 	 * IP relative predicate instructions
 	 */
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
 			default:
 				/* Do nothing */
 				break;
 		}
 	} else if (lx_type_inst) {
 		switch (major_opcode) {
 			case LONG_CALL_OPCODE:
 				p->ainsn.inst_flag |= INST_FLAG_FIX_BRANCH_REG;
 				p->ainsn.target_br_reg = ((kprobe_inst >> 6) & 0x7);
				break;
 			default:
 				/* Do nothing */
 				break;
 		}
	}

	return 0;
}

void arch_arm_kprobe(struct kprobe *p)
{
	unsigned long addr = (unsigned long)p->addr;
	unsigned long arm_addr = addr & ~0xFULL;

	memcpy((char *)arm_addr, &p->ainsn.insn.bundle, sizeof(bundle_t));
	flush_icache_range(arm_addr, arm_addr + sizeof(bundle_t));
}

void arch_disarm_kprobe(struct kprobe *p)
{
	unsigned long addr = (unsigned long)p->addr;
	unsigned long arm_addr = addr & ~0xFULL;

	/* p->opcode contains the original unaltered bundle */
	memcpy((char *) arm_addr, (char *) &p->opcode.bundle, sizeof(bundle_t));
	flush_icache_range(arm_addr, arm_addr + sizeof(bundle_t));
}

void arch_remove_kprobe(struct kprobe *p)
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
static void resume_execution(struct kprobe *p, struct pt_regs *regs)
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

static void prepare_ss(struct kprobe *p, struct pt_regs *regs)
{
	unsigned long bundle_addr = (unsigned long) &p->opcode.bundle;
	unsigned long slot = (unsigned long)p->addr & 0xf;

	/* Update instruction pointer (IIP) and slot number (IPSR.ri) */
	regs->cr_iip = bundle_addr & ~0xFULL;

	if (slot > 2)
		slot = 0;

	ia64_psr(regs)->ri = slot;

	/* turn on single stepping */
	ia64_psr(regs)->ss = 1;
}

static int pre_kprobes_handler(struct pt_regs *regs)
{
	struct kprobe *p;
	int ret = 0;
	kprobe_opcode_t *addr = (kprobe_opcode_t *)instruction_pointer(regs);

	preempt_disable();

	/* Handle recursion cases */
	if (kprobe_running()) {
		p = get_kprobe(addr);
		if (p) {
			if (kprobe_status == KPROBE_HIT_SS) {
				unlock_kprobes();
				goto no_kprobe;
			}
			arch_disarm_kprobe(p);
			ret = 1;
		} else {
			/*
			 * jprobe instrumented function just completed
			 */
			p = current_kprobe;
			if (p->break_handler && p->break_handler(p, regs)) {
				goto ss_probe;
			}
		}
	}

	lock_kprobes();
	p = get_kprobe(addr);
	if (!p) {
		unlock_kprobes();
		goto no_kprobe;
	}

	kprobe_status = KPROBE_HIT_ACTIVE;
	current_kprobe = p;

	if (p->pre_handler && p->pre_handler(p, regs))
		/*
		 * Our pre-handler is specifically requesting that we just
		 * do a return.  This is handling the case where the
		 * pre-handler is really our special jprobe pre-handler.
		 */
		return 1;

ss_probe:
	prepare_ss(p, regs);
	kprobe_status = KPROBE_HIT_SS;
	return 1;

no_kprobe:
	preempt_enable_no_resched();
	return ret;
}

static int post_kprobes_handler(struct pt_regs *regs)
{
	if (!kprobe_running())
		return 0;

	if (current_kprobe->post_handler)
		current_kprobe->post_handler(current_kprobe, regs, 0);

	resume_execution(current_kprobe, regs);

	unlock_kprobes();
	preempt_enable_no_resched();
	return 1;
}

static int kprobes_fault_handler(struct pt_regs *regs, int trapnr)
{
	if (!kprobe_running())
		return 0;

	if (current_kprobe->fault_handler &&
	    current_kprobe->fault_handler(current_kprobe, regs, trapnr))
		return 1;

	if (kprobe_status & KPROBE_HIT_SS) {
		resume_execution(current_kprobe, regs);
		unlock_kprobes();
		preempt_enable_no_resched();
	}

	return 0;
}

int kprobe_exceptions_notify(struct notifier_block *self, unsigned long val,
			     void *data)
{
	struct die_args *args = (struct die_args *)data;
	switch(val) {
	case DIE_BREAK:
		if (pre_kprobes_handler(args->regs))
			return NOTIFY_STOP;
		break;
	case DIE_SS:
		if (post_kprobes_handler(args->regs))
			return NOTIFY_STOP;
		break;
	case DIE_PAGE_FAULT:
		if (kprobes_fault_handler(args->regs, args->trapnr))
			return NOTIFY_STOP;
	default:
		break;
	}
	return NOTIFY_DONE;
}

int setjmp_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
	struct jprobe *jp = container_of(p, struct jprobe, kp);
	unsigned long addr = ((struct fnptr *)(jp->entry))->ip;

	/* save architectural state */
	jprobe_saved_regs = *regs;

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

int longjmp_break_handler(struct kprobe *p, struct pt_regs *regs)
{
	*regs = jprobe_saved_regs;
	return 1;
}
