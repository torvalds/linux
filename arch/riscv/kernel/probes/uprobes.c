// SPDX-License-Identifier: GPL-2.0-only

#include <linux/highmem.h>
#include <linux/ptrace.h>
#include <linux/uprobes.h>

#include "decode-insn.h"

#define UPROBE_TRAP_NR	UINT_MAX

bool is_swbp_insn(uprobe_opcode_t *insn)
{
#ifdef CONFIG_RISCV_ISA_C
	return (*insn & 0xffff) == UPROBE_SWBP_INSN;
#else
	return *insn == UPROBE_SWBP_INSN;
#endif
}

unsigned long uprobe_get_swbp_addr(struct pt_regs *regs)
{
	return instruction_pointer(regs);
}

int arch_uprobe_analyze_insn(struct arch_uprobe *auprobe, struct mm_struct *mm,
			     unsigned long addr)
{
	probe_opcode_t opcode;

	opcode = *(probe_opcode_t *)(&auprobe->insn[0]);

	auprobe->insn_size = GET_INSN_LENGTH(opcode);

	switch (riscv_probe_decode_insn(&opcode, &auprobe->api)) {
	case INSN_REJECTED:
		return -EINVAL;

	case INSN_GOOD_NO_SLOT:
		auprobe->simulate = true;
		break;

	case INSN_GOOD:
		auprobe->simulate = false;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

int arch_uprobe_pre_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	utask->autask.saved_cause = current->thread.bad_cause;
	current->thread.bad_cause = UPROBE_TRAP_NR;

	instruction_pointer_set(regs, utask->xol_vaddr);

	return 0;
}

int arch_uprobe_post_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	WARN_ON_ONCE(current->thread.bad_cause != UPROBE_TRAP_NR);
	current->thread.bad_cause = utask->autask.saved_cause;

	instruction_pointer_set(regs, utask->vaddr + auprobe->insn_size);

	return 0;
}

bool arch_uprobe_xol_was_trapped(struct task_struct *t)
{
	if (t->thread.bad_cause != UPROBE_TRAP_NR)
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

	current->thread.bad_cause = utask->autask.saved_cause;
	/*
	 * Task has received a fatal signal, so reset back to probbed
	 * address.
	 */
	instruction_pointer_set(regs, utask->vaddr);
}

bool arch_uretprobe_is_alive(struct return_instance *ret, enum rp_check ctx,
		struct pt_regs *regs)
{
	if (ctx == RP_CHECK_CHAIN_CALL)
		return regs->sp <= ret->stack;
	else
		return regs->sp < ret->stack;
}

unsigned long
arch_uretprobe_hijack_return_addr(unsigned long trampoline_vaddr,
				  struct pt_regs *regs)
{
	unsigned long ra;

	ra = regs->ra;

	regs->ra = trampoline_vaddr;

	return ra;
}

int arch_uprobe_exception_notify(struct notifier_block *self,
				 unsigned long val, void *data)
{
	return NOTIFY_DONE;
}

bool uprobe_breakpoint_handler(struct pt_regs *regs)
{
	if (uprobe_pre_sstep_notifier(regs))
		return true;

	return false;
}

bool uprobe_single_step_handler(struct pt_regs *regs)
{
	if (uprobe_post_sstep_notifier(regs))
		return true;

	return false;
}

void arch_uprobe_copy_ixol(struct page *page, unsigned long vaddr,
			   void *src, unsigned long len)
{
	/* Initialize the slot */
	void *kaddr = kmap_atomic(page);
	void *dst = kaddr + (vaddr & ~PAGE_MASK);

	memcpy(dst, src, len);

	/* Add ebreak behind opcode to simulate singlestep */
	if (vaddr) {
		dst += GET_INSN_LENGTH(*(probe_opcode_t *)src);
		*(uprobe_opcode_t *)dst = __BUG_INSN_32;
	}

	kunmap_atomic(kaddr);

	/*
	 * We probably need flush_icache_user_page() but it needs vma.
	 * This should work on most of architectures by default. If
	 * architecture needs to do something different it can define
	 * its own version of the function.
	 */
	flush_dcache_page(page);
}
