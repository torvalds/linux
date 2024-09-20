// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * User-space Probes (UProbes) for sparc
 *
 * Copyright (C) 2013 Oracle Inc.
 *
 * Authors:
 *	Jose E. Marchesi <jose.marchesi@oracle.com>
 *	Eric Saint Etienne <eric.saint.etienne@oracle.com>
 */

#include <linux/kernel.h>
#include <linux/highmem.h>
#include <linux/uprobes.h>
#include <linux/uaccess.h>
#include <linux/sched.h> /* For struct task_struct */
#include <linux/kdebug.h>

#include <asm/cacheflush.h>

#include "kernel.h"

/* Compute the address of the breakpoint instruction and return it.
 *
 * Note that uprobe_get_swbp_addr is defined as a weak symbol in
 * kernel/events/uprobe.c.
 */
unsigned long uprobe_get_swbp_addr(struct pt_regs *regs)
{
	return instruction_pointer(regs);
}

static void copy_to_page(struct page *page, unsigned long vaddr,
			 const void *src, int len)
{
	void *kaddr = kmap_atomic(page);

	memcpy(kaddr + (vaddr & ~PAGE_MASK), src, len);
	kunmap_atomic(kaddr);
}

/* Fill in the xol area with the probed instruction followed by the
 * single-step trap.  Some fixups in the copied instruction are
 * performed at this point.
 *
 * Note that uprobe_xol_copy is defined as a weak symbol in
 * kernel/events/uprobe.c.
 */
void arch_uprobe_copy_ixol(struct page *page, unsigned long vaddr,
			   void *src, unsigned long len)
{
	const u32 stp_insn = UPROBE_STP_INSN;
	u32 insn = *(u32 *) src;

	/* Branches annulling their delay slot must be fixed to not do
	 * so.  Clearing the annul bit on these instructions we can be
	 * sure the single-step breakpoint in the XOL slot will be
	 * executed.
	 */

	u32 op = (insn >> 30) & 0x3;
	u32 op2 = (insn >> 22) & 0x7;

	if (op == 0 &&
	    (op2 == 1 || op2 == 2 || op2 == 3 || op2 == 5 || op2 == 6) &&
	    (insn & ANNUL_BIT) == ANNUL_BIT)
		insn &= ~ANNUL_BIT;

	copy_to_page(page, vaddr, &insn, len);
	copy_to_page(page, vaddr+len, &stp_insn, 4);
}


/* Instruction analysis/validity.
 *
 * This function returns 0 on success or a -ve number on error.
 */
int arch_uprobe_analyze_insn(struct arch_uprobe *auprobe,
			     struct mm_struct *mm, unsigned long addr)
{
	/* Any unsupported instruction?  Then return -EINVAL  */
	return 0;
}

/* If INSN is a relative control transfer instruction, return the
 * corrected branch destination value.
 *
 * Note that regs->tpc and regs->tnpc still hold the values of the
 * program counters at the time of the single-step trap due to the
 * execution of the UPROBE_STP_INSN at utask->xol_vaddr + 4.
 *
 */
static unsigned long relbranch_fixup(u32 insn, struct uprobe_task *utask,
				     struct pt_regs *regs)
{
	/* Branch not taken, no mods necessary.  */
	if (regs->tnpc == regs->tpc + 0x4UL)
		return utask->autask.saved_tnpc + 0x4UL;

	/* The three cases are call, branch w/prediction,
	 * and traditional branch.
	 */
	if ((insn & 0xc0000000) == 0x40000000 ||
	    (insn & 0xc1c00000) == 0x00400000 ||
	    (insn & 0xc1c00000) == 0x00800000) {
		unsigned long real_pc = (unsigned long) utask->vaddr;
		unsigned long ixol_addr = utask->xol_vaddr;

		/* The instruction did all the work for us
		 * already, just apply the offset to the correct
		 * instruction location.
		 */
		return (real_pc + (regs->tnpc - ixol_addr));
	}

	/* It is jmpl or some other absolute PC modification instruction,
	 * leave NPC as-is.
	 */
	return regs->tnpc;
}

/* If INSN is an instruction which writes its PC location
 * into a destination register, fix that up.
 */
static int retpc_fixup(struct pt_regs *regs, u32 insn,
		       unsigned long real_pc)
{
	unsigned long *slot = NULL;
	int rc = 0;

	/* Simplest case is 'call', which always uses %o7 */
	if ((insn & 0xc0000000) == 0x40000000)
		slot = &regs->u_regs[UREG_I7];

	/* 'jmpl' encodes the register inside of the opcode */
	if ((insn & 0xc1f80000) == 0x81c00000) {
		unsigned long rd = ((insn >> 25) & 0x1f);

		if (rd <= 15) {
			slot = &regs->u_regs[rd];
		} else {
			unsigned long fp = regs->u_regs[UREG_FP];
			/* Hard case, it goes onto the stack. */
			flushw_all();

			rd -= 16;
			if (test_thread_64bit_stack(fp)) {
				unsigned long __user *uslot =
			(unsigned long __user *) (fp + STACK_BIAS) + rd;
				rc = __put_user(real_pc, uslot);
			} else {
				unsigned int __user *uslot = (unsigned int
						__user *) fp + rd;
				rc = __put_user((u32) real_pc, uslot);
			}
		}
	}
	if (slot != NULL)
		*slot = real_pc;
	return rc;
}

/* Single-stepping can be avoided for certain instructions: NOPs and
 * instructions that can be emulated.  This function determines
 * whether the instruction where the uprobe is installed falls in one
 * of these cases and emulates it.
 *
 * This function returns true if the single-stepping can be skipped,
 * false otherwise.
 */
bool arch_uprobe_skip_sstep(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	/* We currently only emulate NOP instructions.
	 */

	if (auprobe->ixol == (1 << 24)) {
		regs->tnpc += 4;
		regs->tpc += 4;
		return true;
	}

	return false;
}

/* Prepare to execute out of line.  At this point
 * current->utask->xol_vaddr points to an allocated XOL slot properly
 * initialized with the original instruction and the single-stepping
 * trap instruction.
 *
 * This function returns 0 on success, any other number on error.
 */
int arch_uprobe_pre_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;
	struct arch_uprobe_task *autask = &current->utask->autask;

	/* Save the current program counters so they can be restored
	 * later.
	 */
	autask->saved_tpc = regs->tpc;
	autask->saved_tnpc = regs->tnpc;

	/* Adjust PC and NPC so the first instruction in the XOL slot
	 * will be executed by the user task.
	 */
	instruction_pointer_set(regs, utask->xol_vaddr);

	return 0;
}

/* Prepare to resume execution after the single-step.  Called after
 * single-stepping. To avoid the SMP problems that can occur when we
 * temporarily put back the original opcode to single-step, we
 * single-stepped a copy of the instruction.
 *
 * This function returns 0 on success, any other number on error.
 */
int arch_uprobe_post_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;
	struct arch_uprobe_task *autask = &utask->autask;
	u32 insn = auprobe->ixol;
	int rc = 0;

	if (utask->state == UTASK_SSTEP_ACK) {
		regs->tnpc = relbranch_fixup(insn, utask, regs);
		regs->tpc = autask->saved_tnpc;
		rc =  retpc_fixup(regs, insn, (unsigned long) utask->vaddr);
	} else {
		regs->tnpc = utask->vaddr+4;
		regs->tpc = autask->saved_tnpc+4;
	}
	return rc;
}

/* Handler for uprobe traps.  This is called from the traps table and
 * triggers the proper die notification.
 */
asmlinkage void uprobe_trap(struct pt_regs *regs,
			    unsigned long trap_level)
{
	BUG_ON(trap_level != 0x173 && trap_level != 0x174);

	/* We are only interested in user-mode code.  Uprobe traps
	 * shall not be present in kernel code.
	 */
	if (!user_mode(regs)) {
		local_irq_enable();
		bad_trap(regs, trap_level);
		return;
	}

	/* trap_level == 0x173 --> ta 0x73
	 * trap_level == 0x174 --> ta 0x74
	 */
	if (notify_die((trap_level == 0x173) ? DIE_BPT : DIE_SSTEP,
				(trap_level == 0x173) ? "bpt" : "sstep",
				regs, 0, trap_level, SIGTRAP) != NOTIFY_STOP)
		bad_trap(regs, trap_level);
}

/* Callback routine for handling die notifications.
*/
int arch_uprobe_exception_notify(struct notifier_block *self,
				 unsigned long val, void *data)
{
	int ret = NOTIFY_DONE;
	struct die_args *args = (struct die_args *)data;

	/* We are only interested in userspace traps */
	if (args->regs && !user_mode(args->regs))
		return NOTIFY_DONE;

	switch (val) {
	case DIE_BPT:
		if (uprobe_pre_sstep_notifier(args->regs))
			ret = NOTIFY_STOP;
		break;

	case DIE_SSTEP:
		if (uprobe_post_sstep_notifier(args->regs))
			ret = NOTIFY_STOP;

	default:
		break;
	}

	return ret;
}

/* This function gets called when a XOL instruction either gets
 * trapped or the thread has a fatal signal, so reset the instruction
 * pointer to its probed address.
 */
void arch_uprobe_abort_xol(struct arch_uprobe *auprobe, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	instruction_pointer_set(regs, utask->vaddr);
}

/* If xol insn itself traps and generates a signal(Say,
 * SIGILL/SIGSEGV/etc), then detect the case where a singlestepped
 * instruction jumps back to its own address.
 */
bool arch_uprobe_xol_was_trapped(struct task_struct *t)
{
	return false;
}

unsigned long
arch_uretprobe_hijack_return_addr(unsigned long trampoline_vaddr,
				  struct pt_regs *regs)
{
	unsigned long orig_ret_vaddr = regs->u_regs[UREG_I7];

	regs->u_regs[UREG_I7] = trampoline_vaddr-8;

	return orig_ret_vaddr + 8;
}
