// SPDX-License-Identifier: GPL-2.0-only
#include <linux/highmem.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/uprobes.h>
#include <asm/cacheflush.h>

#define UPROBE_TRAP_NR	UINT_MAX

int arch_uprobe_analyze_insn(struct arch_uprobe *auprobe,
			     struct mm_struct *mm, unsigned long addr)
{
	int idx;
	union loongarch_instruction insn;

	if (addr & 0x3)
		return -EILSEQ;

	for (idx = ARRAY_SIZE(auprobe->insn) - 1; idx >= 0; idx--) {
		insn.word = auprobe->insn[idx];
		if (insns_not_supported(insn))
			return -EINVAL;
	}

	if (insns_need_simulation(insn)) {
		auprobe->ixol[0] = larch_insn_gen_nop();
		auprobe->simulate = true;
	} else {
		auprobe->ixol[0] = auprobe->insn[0];
		auprobe->simulate = false;
	}

	auprobe->ixol[1] = UPROBE_XOLBP_INSN;

	return 0;
}

int arch_uprobe_pre_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	utask->autask.saved_trap_nr = current->thread.trap_nr;
	current->thread.trap_nr = UPROBE_TRAP_NR;
	instruction_pointer_set(regs, utask->xol_vaddr);
	user_enable_single_step(current);

	return 0;
}

int arch_uprobe_post_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	WARN_ON_ONCE(current->thread.trap_nr != UPROBE_TRAP_NR);
	current->thread.trap_nr = utask->autask.saved_trap_nr;

	if (auprobe->simulate)
		instruction_pointer_set(regs, auprobe->resume_era);
	else
		instruction_pointer_set(regs, utask->vaddr + LOONGARCH_INSN_SIZE);

	user_disable_single_step(current);

	return 0;
}

void arch_uprobe_abort_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	current->thread.trap_nr = utask->autask.saved_trap_nr;
	instruction_pointer_set(regs, utask->vaddr);
	user_disable_single_step(current);
}

bool arch_uprobe_xol_was_trapped(struct task_struct *t)
{
	if (t->thread.trap_nr != UPROBE_TRAP_NR)
		return true;

	return false;
}

bool arch_uprobe_skip_sstep(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	union loongarch_instruction insn;

	if (!auprobe->simulate)
		return false;

	insn.word = auprobe->insn[0];
	arch_simulate_insn(insn, regs);
	auprobe->resume_era = regs->csr_era;

	return true;
}

unsigned long arch_uretprobe_hijack_return_addr(unsigned long trampoline_vaddr,
						struct pt_regs *regs)
{
	unsigned long ra = regs->regs[1];

	regs->regs[1] = trampoline_vaddr;

	return ra;
}

bool arch_uretprobe_is_alive(struct return_instance *ret,
			     enum rp_check ctx, struct pt_regs *regs)
{
	if (ctx == RP_CHECK_CHAIN_CALL)
		return regs->regs[3] <= ret->stack;
	else
		return regs->regs[3] < ret->stack;
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

bool uprobe_singlestep_handler(struct pt_regs *regs)
{
	if (uprobe_post_sstep_notifier(regs))
		return true;

	return false;
}

unsigned long uprobe_get_swbp_addr(struct pt_regs *regs)
{
	return instruction_pointer(regs);
}

void arch_uprobe_copy_ixol(struct page *page, unsigned long vaddr,
			   void *src, unsigned long len)
{
	void *kaddr = kmap_local_page(page);
	void *dst = kaddr + (vaddr & ~PAGE_MASK);

	memcpy(dst, src, len);
	flush_icache_range((unsigned long)dst, (unsigned long)dst + len);
	kunmap_local(kaddr);
}
