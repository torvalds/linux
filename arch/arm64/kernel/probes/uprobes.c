// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014-2016 Pratyush Anand <panand@redhat.com>
 */
#include <linux/highmem.h>
#include <linux/ptrace.h>
#include <linux/uprobes.h>
#include <asm/cacheflush.h>

#include "decode-insn.h"

#define UPROBE_INV_FAULT_CODE	UINT_MAX

void arch_uprobe_copy_ixol(struct page *page, unsigned long vaddr,
		void *src, unsigned long len)
{
	void *xol_page_kaddr = kmap_atomic(page);
	void *dst = xol_page_kaddr + (vaddr & ~PAGE_MASK);

	/*
	 * Initial cache maintenance of the xol page done via set_pte_at().
	 * Subsequent CMOs only needed if the xol slot changes.
	 */
	if (!memcmp(dst, src, len))
		goto done;

	/* Initialize the slot */
	memcpy(dst, src, len);

	/* flush caches (dcache/icache) */
	sync_icache_aliases((unsigned long)dst, (unsigned long)dst + len);

done:
	kunmap_atomic(xol_page_kaddr);
}

unsigned long uprobe_get_swbp_addr(struct pt_regs *regs)
{
	return instruction_pointer(regs);
}

int arch_uprobe_analyze_insn(struct arch_uprobe *auprobe, struct mm_struct *mm,
		unsigned long addr)
{
	u32 insn;

	/* TODO: Currently we do not support AARCH32 instruction probing */
	if (mm->context.flags & MMCF_AARCH32)
		return -EOPNOTSUPP;
	else if (!IS_ALIGNED(addr, AARCH64_INSN_SIZE))
		return -EINVAL;

	insn = le32_to_cpu(auprobe->insn);

	switch (arm_probe_decode_insn(insn, &auprobe->api)) {
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

	/* Initialize with an invalid fault code to detect if ol insn trapped */
	current->thread.fault_code = UPROBE_INV_FAULT_CODE;

	/* Instruction points to execute ol */
	instruction_pointer_set(regs, utask->xol_vaddr);

	user_enable_single_step(current);

	return 0;
}

int arch_uprobe_post_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	WARN_ON_ONCE(current->thread.fault_code != UPROBE_INV_FAULT_CODE);

	/* Instruction points to execute next to breakpoint address */
	instruction_pointer_set(regs, utask->vaddr + 4);

	user_disable_single_step(current);

	return 0;
}
bool arch_uprobe_xol_was_trapped(struct task_struct *t)
{
	/*
	 * Between arch_uprobe_pre_xol and arch_uprobe_post_xol, if an xol
	 * insn itself is trapped, then detect the case with the help of
	 * invalid fault code which is being set in arch_uprobe_pre_xol
	 */
	if (t->thread.fault_code != UPROBE_INV_FAULT_CODE)
		return true;

	return false;
}

bool arch_uprobe_skip_sstep(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	u32 insn;
	unsigned long addr;

	if (!auprobe->simulate)
		return false;

	insn = le32_to_cpu(auprobe->insn);
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
	/*
	 * If a simple branch instruction (B) was called for retprobed
	 * assembly label then return true even when regs->sp and ret->stack
	 * are same. It will ensure that cleanup and reporting of return
	 * instances corresponding to callee label is done when
	 * handle_trampoline for called function is executed.
	 */
	if (ctx == RP_CHECK_CHAIN_CALL)
		return regs->sp <= ret->stack;
	else
		return regs->sp < ret->stack;
}

unsigned long
arch_uretprobe_hijack_return_addr(unsigned long trampoline_vaddr,
				  struct pt_regs *regs)
{
	unsigned long orig_ret_vaddr;

	orig_ret_vaddr = procedure_link_pointer(regs);
	/* Replace the return addr with trampoline addr */
	procedure_link_pointer_set(regs, trampoline_vaddr);

	return orig_ret_vaddr;
}

int arch_uprobe_exception_notify(struct notifier_block *self,
				 unsigned long val, void *data)
{
	return NOTIFY_DONE;
}

static int uprobe_breakpoint_handler(struct pt_regs *regs,
				     unsigned long esr)
{
	if (uprobe_pre_sstep_notifier(regs))
		return DBG_HOOK_HANDLED;

	return DBG_HOOK_ERROR;
}

static int uprobe_single_step_handler(struct pt_regs *regs,
				      unsigned long esr)
{
	struct uprobe_task *utask = current->utask;

	WARN_ON(utask && (instruction_pointer(regs) != utask->xol_vaddr + 4));
	if (uprobe_post_sstep_notifier(regs))
		return DBG_HOOK_HANDLED;

	return DBG_HOOK_ERROR;
}

/* uprobe breakpoint handler hook */
static struct break_hook uprobes_break_hook = {
	.imm = UPROBES_BRK_IMM,
	.fn = uprobe_breakpoint_handler,
};

/* uprobe single step handler hook */
static struct step_hook uprobes_step_hook = {
	.fn = uprobe_single_step_handler,
};

static int __init arch_init_uprobes(void)
{
	register_user_break_hook(&uprobes_break_hook);
	register_user_step_hook(&uprobes_step_hook);

	return 0;
}

device_initcall(arch_init_uprobes);
