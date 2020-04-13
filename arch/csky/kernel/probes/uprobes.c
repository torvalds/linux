// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014-2016 Pratyush Anand <panand@redhat.com>
 */
#include <linux/highmem.h>
#include <linux/ptrace.h>
#include <linux/uprobes.h>
#include <asm/cacheflush.h>

#include "decode-insn.h"

#define UPROBE_TRAP_NR	UINT_MAX

unsigned long uprobe_get_swbp_addr(struct pt_regs *regs)
{
	return instruction_pointer(regs);
}

int arch_uprobe_analyze_insn(struct arch_uprobe *auprobe, struct mm_struct *mm,
		unsigned long addr)
{
	probe_opcode_t insn;

	insn = *(probe_opcode_t *)(&auprobe->insn[0]);

	auprobe->insn_size = is_insn32(insn) ? 4 : 2;

	switch (csky_probe_decode_insn(&insn, &auprobe->api)) {
	case INSN_REJECTED:
		return -EINVAL;

	case INSN_GOOD_NO_SLOT:
		auprobe->simulate = true;
		break;

	default:
		break;
	}

	return 0;
}

int arch_uprobe_pre_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	utask->autask.saved_trap_no = current->thread.trap_no;
	current->thread.trap_no = UPROBE_TRAP_NR;

	instruction_pointer_set(regs, utask->xol_vaddr);

	user_enable_single_step(current);

	return 0;
}

int arch_uprobe_post_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	WARN_ON_ONCE(current->thread.trap_no != UPROBE_TRAP_NR);

	instruction_pointer_set(regs, utask->vaddr + auprobe->insn_size);

	user_disable_single_step(current);

	return 0;
}

bool arch_uprobe_xol_was_trapped(struct task_struct *t)
{
	if (t->thread.trap_no != UPROBE_TRAP_NR)
		return true;

	return false;
}

bool arch_uprobe_skip_sstep(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	probe_opcode_t insn;
	unsigned long addr;

	if (!auprobe->simulate)
		return false;

	insn = *(probe_opcode_t *)(&auprobe->insn[0]);
	addr = instruction_pointer(regs);

	if (auprobe->api.handler)
		auprobe->api.handler(insn, addr, regs);

	return true;
}

void arch_uprobe_abort_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	/*
	 * Task has received a fatal signal, so reset back to probbed
	 * address.
	 */
	instruction_pointer_set(regs, utask->vaddr);

	user_disable_single_step(current);
}

bool arch_uretprobe_is_alive(struct return_instance *ret, enum rp_check ctx,
		struct pt_regs *regs)
{
	if (ctx == RP_CHECK_CHAIN_CALL)
		return regs->usp <= ret->stack;
	else
		return regs->usp < ret->stack;
}

unsigned long
arch_uretprobe_hijack_return_addr(unsigned long trampoline_vaddr,
				  struct pt_regs *regs)
{
	unsigned long ra;

	ra = regs->lr;

	regs->lr = trampoline_vaddr;

	return ra;
}

int arch_uprobe_exception_notify(struct notifier_block *self,
				 unsigned long val, void *data)
{
	return NOTIFY_DONE;
}

int uprobe_breakpoint_handler(struct pt_regs *regs)
{
	if (uprobe_pre_sstep_notifier(regs))
		return 1;

	return 0;
}

int uprobe_single_step_handler(struct pt_regs *regs)
{
	if (uprobe_post_sstep_notifier(regs))
		return 1;

	return 0;
}
