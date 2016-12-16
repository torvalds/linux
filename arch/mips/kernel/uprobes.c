#include <linux/highmem.h>
#include <linux/kdebug.h>
#include <linux/types.h>
#include <linux/notifier.h>
#include <linux/sched.h>
#include <linux/uprobes.h>

#include <asm/branch.h>
#include <asm/cpu-features.h>
#include <asm/ptrace.h>
#include <asm/inst.h>

static inline int insn_has_delay_slot(const union mips_instruction insn)
{
	switch (insn.i_format.opcode) {
	/*
	 * jr and jalr are in r_format format.
	 */
	case spec_op:
		switch (insn.r_format.func) {
		case jalr_op:
		case jr_op:
			return 1;
		}
		break;

	/*
	 * This group contains:
	 * bltz_op, bgez_op, bltzl_op, bgezl_op,
	 * bltzal_op, bgezal_op, bltzall_op, bgezall_op.
	 */
	case bcond_op:
		switch (insn.i_format.rt) {
		case bltz_op:
		case bltzl_op:
		case bgez_op:
		case bgezl_op:
		case bltzal_op:
		case bltzall_op:
		case bgezal_op:
		case bgezall_op:
		case bposge32_op:
			return 1;
		}
		break;

	/*
	 * These are unconditional and in j_format.
	 */
	case jal_op:
	case j_op:
	case beq_op:
	case beql_op:
	case bne_op:
	case bnel_op:
	case blez_op: /* not really i_format */
	case blezl_op:
	case bgtz_op:
	case bgtzl_op:
		return 1;

	/*
	 * And now the FPA/cp1 branch instructions.
	 */
	case cop1_op:
#ifdef CONFIG_CPU_CAVIUM_OCTEON
	case lwc2_op: /* This is bbit0 on Octeon */
	case ldc2_op: /* This is bbit032 on Octeon */
	case swc2_op: /* This is bbit1 on Octeon */
	case sdc2_op: /* This is bbit132 on Octeon */
#endif
		return 1;
	}

	return 0;
}

/**
 * arch_uprobe_analyze_insn - instruction analysis including validity and fixups.
 * @mm: the probed address space.
 * @arch_uprobe: the probepoint information.
 * @addr: virtual address at which to install the probepoint
 * Return 0 on success or a -ve number on error.
 */
int arch_uprobe_analyze_insn(struct arch_uprobe *aup,
	struct mm_struct *mm, unsigned long addr)
{
	union mips_instruction inst;

	/*
	 * For the time being this also blocks attempts to use uprobes with
	 * MIPS16 and microMIPS.
	 */
	if (addr & 0x03)
		return -EINVAL;

	inst.word = aup->insn[0];
	aup->ixol[0] = aup->insn[insn_has_delay_slot(inst)];
	aup->ixol[1] = UPROBE_BRK_UPROBE_XOL;		/* NOP  */

	return 0;
}

/**
 * is_trap_insn - check if the instruction is a trap variant
 * @insn: instruction to be checked.
 * Returns true if @insn is a trap variant.
 *
 * This definition overrides the weak definition in kernel/events/uprobes.c.
 * and is needed for the case where an architecture has multiple trap
 * instructions (like PowerPC or MIPS).  We treat BREAK just like the more
 * modern conditional trap instructions.
 */
bool is_trap_insn(uprobe_opcode_t *insn)
{
	union mips_instruction inst;

	inst.word = *insn;

	switch (inst.i_format.opcode) {
	case spec_op:
		switch (inst.r_format.func) {
		case break_op:
		case teq_op:
		case tge_op:
		case tgeu_op:
		case tlt_op:
		case tltu_op:
		case tne_op:
			return 1;
		}
		break;

	case bcond_op:	/* Yes, really ...  */
		switch (inst.u_format.rt) {
		case teqi_op:
		case tgei_op:
		case tgeiu_op:
		case tlti_op:
		case tltiu_op:
		case tnei_op:
			return 1;
		}
		break;
	}

	return 0;
}

#define UPROBE_TRAP_NR	ULONG_MAX

/*
 * arch_uprobe_pre_xol - prepare to execute out of line.
 * @auprobe: the probepoint information.
 * @regs: reflects the saved user state of current task.
 */
int arch_uprobe_pre_xol(struct arch_uprobe *aup, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	/*
	 * Now find the EPC where to resume after the breakpoint has been
	 * dealt with.  This may require emulation of a branch.
	 */
	aup->resume_epc = regs->cp0_epc + 4;
	if (insn_has_delay_slot((union mips_instruction) aup->insn[0])) {
		unsigned long epc;

		epc = regs->cp0_epc;
		__compute_return_epc_for_insn(regs,
			(union mips_instruction) aup->insn[0]);
		aup->resume_epc = regs->cp0_epc;
	}
	utask->autask.saved_trap_nr = current->thread.trap_nr;
	current->thread.trap_nr = UPROBE_TRAP_NR;
	regs->cp0_epc = current->utask->xol_vaddr;

	return 0;
}

int arch_uprobe_post_xol(struct arch_uprobe *aup, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	current->thread.trap_nr = utask->autask.saved_trap_nr;
	regs->cp0_epc = aup->resume_epc;

	return 0;
}

/*
 * If xol insn itself traps and generates a signal(Say,
 * SIGILL/SIGSEGV/etc), then detect the case where a singlestepped
 * instruction jumps back to its own address. It is assumed that anything
 * like do_page_fault/do_trap/etc sets thread.trap_nr != -1.
 *
 * arch_uprobe_pre_xol/arch_uprobe_post_xol save/restore thread.trap_nr,
 * arch_uprobe_xol_was_trapped() simply checks that ->trap_nr is not equal to
 * UPROBE_TRAP_NR == -1 set by arch_uprobe_pre_xol().
 */
bool arch_uprobe_xol_was_trapped(struct task_struct *tsk)
{
	if (tsk->thread.trap_nr != UPROBE_TRAP_NR)
		return true;

	return false;
}

int arch_uprobe_exception_notify(struct notifier_block *self,
	unsigned long val, void *data)
{
	struct die_args *args = data;
	struct pt_regs *regs = args->regs;

	/* regs == NULL is a kernel bug */
	if (WARN_ON(!regs))
		return NOTIFY_DONE;

	/* We are only interested in userspace traps */
	if (!user_mode(regs))
		return NOTIFY_DONE;

	switch (val) {
	case DIE_UPROBE:
		if (uprobe_pre_sstep_notifier(regs))
			return NOTIFY_STOP;
		break;
	case DIE_UPROBE_XOL:
		if (uprobe_post_sstep_notifier(regs))
			return NOTIFY_STOP;
	default:
		break;
	}

	return 0;
}

/*
 * This function gets called when XOL instruction either gets trapped or
 * the thread has a fatal signal. Reset the instruction pointer to its
 * probed address for the potential restart or for post mortem analysis.
 */
void arch_uprobe_abort_xol(struct arch_uprobe *aup,
	struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	instruction_pointer_set(regs, utask->vaddr);
}

unsigned long arch_uretprobe_hijack_return_addr(
	unsigned long trampoline_vaddr, struct pt_regs *regs)
{
	unsigned long ra;

	ra = regs->regs[31];

	/* Replace the return address with the trampoline address */
	regs->regs[31] = trampoline_vaddr;

	return ra;
}

/**
 * set_swbp - store breakpoint at a given address.
 * @auprobe: arch specific probepoint information.
 * @mm: the probed process address space.
 * @vaddr: the virtual address to insert the opcode.
 *
 * For mm @mm, store the breakpoint instruction at @vaddr.
 * Return 0 (success) or a negative errno.
 *
 * This version overrides the weak version in kernel/events/uprobes.c.
 * It is required to handle MIPS16 and microMIPS.
 */
int __weak set_swbp(struct arch_uprobe *auprobe, struct mm_struct *mm,
	unsigned long vaddr)
{
	return uprobe_write_opcode(mm, vaddr, UPROBE_SWBP_INSN);
}

void __weak arch_uprobe_copy_ixol(struct page *page, unsigned long vaddr,
				  void *src, unsigned long len)
{
	void *kaddr;

	/* Initialize the slot */
	kaddr = kmap_atomic(page);
	memcpy(kaddr + (vaddr & ~PAGE_MASK), src, len);
	kunmap_atomic(kaddr);

	/*
	 * The MIPS version of flush_icache_range will operate safely on
	 * user space addresses and more importantly, it doesn't require a
	 * VMA argument.
	 */
	flush_icache_range(vaddr, vaddr + len);
}

/**
 * uprobe_get_swbp_addr - compute address of swbp given post-swbp regs
 * @regs: Reflects the saved state of the task after it has hit a breakpoint
 * instruction.
 * Return the address of the breakpoint instruction.
 *
 * This overrides the weak version in kernel/events/uprobes.c.
 */
unsigned long uprobe_get_swbp_addr(struct pt_regs *regs)
{
	return instruction_pointer(regs);
}

/*
 * See if the instruction can be emulated.
 * Returns true if instruction was emulated, false otherwise.
 *
 * For now we always emulate so this function just returns 0.
 */
bool arch_uprobe_skip_sstep(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	return 0;
}
