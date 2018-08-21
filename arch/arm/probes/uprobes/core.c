/*
 * Copyright (C) 2012 Rabin Vincent <rabin at rab.in>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/errno.h>
#include <linux/highmem.h>
#include <linux/sched.h>
#include <linux/uprobes.h>
#include <linux/notifier.h>

#include <asm/opcodes.h>
#include <asm/traps.h>

#include "../decode.h"
#include "../decode-arm.h"
#include "core.h"

#define UPROBE_TRAP_NR	UINT_MAX

bool is_swbp_insn(uprobe_opcode_t *insn)
{
	return (__mem_to_opcode_arm(*insn) & 0x0fffffff) ==
		(UPROBE_SWBP_ARM_INSN & 0x0fffffff);
}

int set_swbp(struct arch_uprobe *auprobe, struct mm_struct *mm,
	     unsigned long vaddr)
{
	return uprobe_write_opcode(auprobe, mm, vaddr,
		   __opcode_to_mem_arm(auprobe->bpinsn));
}

bool arch_uprobe_ignore(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	if (!auprobe->asi.insn_check_cc(regs->ARM_cpsr)) {
		regs->ARM_pc += 4;
		return true;
	}

	return false;
}

bool arch_uprobe_skip_sstep(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	probes_opcode_t opcode;

	if (!auprobe->simulate)
		return false;

	opcode = __mem_to_opcode_arm(*(unsigned int *) auprobe->insn);

	auprobe->asi.insn_singlestep(opcode, &auprobe->asi, regs);

	return true;
}

unsigned long
arch_uretprobe_hijack_return_addr(unsigned long trampoline_vaddr,
				  struct pt_regs *regs)
{
	unsigned long orig_ret_vaddr;

	orig_ret_vaddr = regs->ARM_lr;
	/* Replace the return addr with trampoline addr */
	regs->ARM_lr = trampoline_vaddr;
	return orig_ret_vaddr;
}

int arch_uprobe_analyze_insn(struct arch_uprobe *auprobe, struct mm_struct *mm,
			     unsigned long addr)
{
	unsigned int insn;
	unsigned int bpinsn;
	enum probes_insn ret;

	/* Thumb not yet support */
	if (addr & 0x3)
		return -EINVAL;

	insn = __mem_to_opcode_arm(*(unsigned int *)auprobe->insn);
	auprobe->ixol[0] = __opcode_to_mem_arm(insn);
	auprobe->ixol[1] = __opcode_to_mem_arm(UPROBE_SS_ARM_INSN);

	ret = arm_probes_decode_insn(insn, &auprobe->asi, false,
				     uprobes_probes_actions, NULL);
	switch (ret) {
	case INSN_REJECTED:
		return -EINVAL;

	case INSN_GOOD_NO_SLOT:
		auprobe->simulate = true;
		break;

	case INSN_GOOD:
	default:
		break;
	}

	bpinsn = UPROBE_SWBP_ARM_INSN & 0x0fffffff;
	if (insn >= 0xe0000000)
		bpinsn |= 0xe0000000;  /* Unconditional instruction */
	else
		bpinsn |= insn & 0xf0000000;  /* Copy condition from insn */

	auprobe->bpinsn = bpinsn;

	return 0;
}

void arch_uprobe_copy_ixol(struct page *page, unsigned long vaddr,
			   void *src, unsigned long len)
{
	void *xol_page_kaddr = kmap_atomic(page);
	void *dst = xol_page_kaddr + (vaddr & ~PAGE_MASK);

	preempt_disable();

	/* Initialize the slot */
	memcpy(dst, src, len);

	/* flush caches (dcache/icache) */
	flush_uprobe_xol_access(page, vaddr, dst, len);

	preempt_enable();

	kunmap_atomic(xol_page_kaddr);
}


int arch_uprobe_pre_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	if (auprobe->prehandler)
		auprobe->prehandler(auprobe, &utask->autask, regs);

	utask->autask.saved_trap_no = current->thread.trap_no;
	current->thread.trap_no = UPROBE_TRAP_NR;
	regs->ARM_pc = utask->xol_vaddr;

	return 0;
}

int arch_uprobe_post_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	WARN_ON_ONCE(current->thread.trap_no != UPROBE_TRAP_NR);

	current->thread.trap_no = utask->autask.saved_trap_no;
	regs->ARM_pc = utask->vaddr + 4;

	if (auprobe->posthandler)
		auprobe->posthandler(auprobe, &utask->autask, regs);

	return 0;
}

bool arch_uprobe_xol_was_trapped(struct task_struct *t)
{
	if (t->thread.trap_no != UPROBE_TRAP_NR)
		return true;

	return false;
}

void arch_uprobe_abort_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	current->thread.trap_no = utask->autask.saved_trap_no;
	instruction_pointer_set(regs, utask->vaddr);
}

int arch_uprobe_exception_notify(struct notifier_block *self,
				 unsigned long val, void *data)
{
	return NOTIFY_DONE;
}

static int uprobe_trap_handler(struct pt_regs *regs, unsigned int instr)
{
	unsigned long flags;

	local_irq_save(flags);
	instr &= 0x0fffffff;
	if (instr == (UPROBE_SWBP_ARM_INSN & 0x0fffffff))
		uprobe_pre_sstep_notifier(regs);
	else if (instr == (UPROBE_SS_ARM_INSN & 0x0fffffff))
		uprobe_post_sstep_notifier(regs);
	local_irq_restore(flags);

	return 0;
}

unsigned long uprobe_get_swbp_addr(struct pt_regs *regs)
{
	return instruction_pointer(regs);
}

static struct undef_hook uprobes_arm_break_hook = {
	.instr_mask	= 0x0fffffff,
	.instr_val	= (UPROBE_SWBP_ARM_INSN & 0x0fffffff),
	.cpsr_mask	= MODE_MASK,
	.cpsr_val	= USR_MODE,
	.fn		= uprobe_trap_handler,
};

static struct undef_hook uprobes_arm_ss_hook = {
	.instr_mask	= 0x0fffffff,
	.instr_val	= (UPROBE_SS_ARM_INSN & 0x0fffffff),
	.cpsr_mask	= MODE_MASK,
	.cpsr_val	= USR_MODE,
	.fn		= uprobe_trap_handler,
};

static int arch_uprobes_init(void)
{
	register_undef_hook(&uprobes_arm_break_hook);
	register_undef_hook(&uprobes_arm_ss_hook);

	return 0;
}
device_initcall(arch_uprobes_init);
