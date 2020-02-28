/*
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Derived from "arch/m68k/kernel/ptrace.c"
 *  Copyright (C) 1994 by Hamish Macdonald
 *  Taken from linux/kernel/ptrace.c and modified for M680x0.
 *  linux/kernel/ptrace.c is by Ross Biro 1/23/92, edited by Linus Torvalds
 *
 * Modified by Cort Dougan (cort@hq.fsmlabs.com)
 * and Paul Mackerras (paulus@samba.org).
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file README.legal in the main directory of
 * this archive for more details.
 */

#include <linux/regset.h>
#include <linux/tracehook.h>
#include <linux/audit.h>
#include <linux/hw_breakpoint.h>
#include <linux/context_tracking.h>
#include <linux/syscalls.h>

#include <asm/switch_to.h>
#include <asm/asm-prototypes.h>
#include <asm/debug.h>

#define CREATE_TRACE_POINTS
#include <trace/events/syscalls.h>

#include "ptrace-decl.h"

void user_enable_single_step(struct task_struct *task)
{
	struct pt_regs *regs = task->thread.regs;

	if (regs != NULL) {
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
		task->thread.debug.dbcr0 &= ~DBCR0_BT;
		task->thread.debug.dbcr0 |= DBCR0_IDM | DBCR0_IC;
		regs->msr |= MSR_DE;
#else
		regs->msr &= ~MSR_BE;
		regs->msr |= MSR_SE;
#endif
	}
	set_tsk_thread_flag(task, TIF_SINGLESTEP);
}

void user_enable_block_step(struct task_struct *task)
{
	struct pt_regs *regs = task->thread.regs;

	if (regs != NULL) {
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
		task->thread.debug.dbcr0 &= ~DBCR0_IC;
		task->thread.debug.dbcr0 = DBCR0_IDM | DBCR0_BT;
		regs->msr |= MSR_DE;
#else
		regs->msr &= ~MSR_SE;
		regs->msr |= MSR_BE;
#endif
	}
	set_tsk_thread_flag(task, TIF_SINGLESTEP);
}

void user_disable_single_step(struct task_struct *task)
{
	struct pt_regs *regs = task->thread.regs;

	if (regs != NULL) {
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
		/*
		 * The logic to disable single stepping should be as
		 * simple as turning off the Instruction Complete flag.
		 * And, after doing so, if all debug flags are off, turn
		 * off DBCR0(IDM) and MSR(DE) .... Torez
		 */
		task->thread.debug.dbcr0 &= ~(DBCR0_IC|DBCR0_BT);
		/*
		 * Test to see if any of the DBCR_ACTIVE_EVENTS bits are set.
		 */
		if (!DBCR_ACTIVE_EVENTS(task->thread.debug.dbcr0,
					task->thread.debug.dbcr1)) {
			/*
			 * All debug events were off.....
			 */
			task->thread.debug.dbcr0 &= ~DBCR0_IDM;
			regs->msr &= ~MSR_DE;
		}
#else
		regs->msr &= ~(MSR_SE | MSR_BE);
#endif
	}
	clear_tsk_thread_flag(task, TIF_SINGLESTEP);
}

#ifdef CONFIG_HAVE_HW_BREAKPOINT
void ptrace_triggered(struct perf_event *bp,
		      struct perf_sample_data *data, struct pt_regs *regs)
{
	struct perf_event_attr attr;

	/*
	 * Disable the breakpoint request here since ptrace has defined a
	 * one-shot behaviour for breakpoint exceptions in PPC64.
	 * The SIGTRAP signal is generated automatically for us in do_dabr().
	 * We don't have to do anything about that here
	 */
	attr = bp->attr;
	attr.disabled = true;
	modify_user_hw_breakpoint(bp, &attr);
}
#endif /* CONFIG_HAVE_HW_BREAKPOINT */

static int ptrace_set_debugreg(struct task_struct *task, unsigned long addr,
			       unsigned long data)
{
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	int ret;
	struct thread_struct *thread = &(task->thread);
	struct perf_event *bp;
	struct perf_event_attr attr;
#endif /* CONFIG_HAVE_HW_BREAKPOINT */
#ifndef CONFIG_PPC_ADV_DEBUG_REGS
	bool set_bp = true;
	struct arch_hw_breakpoint hw_brk;
#endif

	/* For ppc64 we support one DABR and no IABR's at the moment (ppc64).
	 *  For embedded processors we support one DAC and no IAC's at the
	 *  moment.
	 */
	if (addr > 0)
		return -EINVAL;

	/* The bottom 3 bits in dabr are flags */
	if ((data & ~0x7UL) >= TASK_SIZE)
		return -EIO;

#ifndef CONFIG_PPC_ADV_DEBUG_REGS
	/* For processors using DABR (i.e. 970), the bottom 3 bits are flags.
	 *  It was assumed, on previous implementations, that 3 bits were
	 *  passed together with the data address, fitting the design of the
	 *  DABR register, as follows:
	 *
	 *  bit 0: Read flag
	 *  bit 1: Write flag
	 *  bit 2: Breakpoint translation
	 *
	 *  Thus, we use them here as so.
	 */

	/* Ensure breakpoint translation bit is set */
	if (data && !(data & HW_BRK_TYPE_TRANSLATE))
		return -EIO;
	hw_brk.address = data & (~HW_BRK_TYPE_DABR);
	hw_brk.type = (data & HW_BRK_TYPE_DABR) | HW_BRK_TYPE_PRIV_ALL;
	hw_brk.len = DABR_MAX_LEN;
	hw_brk.hw_len = DABR_MAX_LEN;
	set_bp = (data) && (hw_brk.type & HW_BRK_TYPE_RDWR);
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	bp = thread->ptrace_bps[0];
	if (!set_bp) {
		if (bp) {
			unregister_hw_breakpoint(bp);
			thread->ptrace_bps[0] = NULL;
		}
		return 0;
	}
	if (bp) {
		attr = bp->attr;
		attr.bp_addr = hw_brk.address;
		attr.bp_len = DABR_MAX_LEN;
		arch_bp_generic_fields(hw_brk.type, &attr.bp_type);

		/* Enable breakpoint */
		attr.disabled = false;

		ret =  modify_user_hw_breakpoint(bp, &attr);
		if (ret) {
			return ret;
		}
		thread->ptrace_bps[0] = bp;
		thread->hw_brk = hw_brk;
		return 0;
	}

	/* Create a new breakpoint request if one doesn't exist already */
	hw_breakpoint_init(&attr);
	attr.bp_addr = hw_brk.address;
	attr.bp_len = DABR_MAX_LEN;
	arch_bp_generic_fields(hw_brk.type,
			       &attr.bp_type);

	thread->ptrace_bps[0] = bp = register_user_hw_breakpoint(&attr,
					       ptrace_triggered, NULL, task);
	if (IS_ERR(bp)) {
		thread->ptrace_bps[0] = NULL;
		return PTR_ERR(bp);
	}

#else /* !CONFIG_HAVE_HW_BREAKPOINT */
	if (set_bp && (!ppc_breakpoint_available()))
		return -ENODEV;
#endif /* CONFIG_HAVE_HW_BREAKPOINT */
	task->thread.hw_brk = hw_brk;
#else /* CONFIG_PPC_ADV_DEBUG_REGS */
	/* As described above, it was assumed 3 bits were passed with the data
	 *  address, but we will assume only the mode bits will be passed
	 *  as to not cause alignment restrictions for DAC-based processors.
	 */

	/* DAC's hold the whole address without any mode flags */
	task->thread.debug.dac1 = data & ~0x3UL;

	if (task->thread.debug.dac1 == 0) {
		dbcr_dac(task) &= ~(DBCR_DAC1R | DBCR_DAC1W);
		if (!DBCR_ACTIVE_EVENTS(task->thread.debug.dbcr0,
					task->thread.debug.dbcr1)) {
			task->thread.regs->msr &= ~MSR_DE;
			task->thread.debug.dbcr0 &= ~DBCR0_IDM;
		}
		return 0;
	}

	/* Read or Write bits must be set */

	if (!(data & 0x3UL))
		return -EINVAL;

	/* Set the Internal Debugging flag (IDM bit 1) for the DBCR0
	   register */
	task->thread.debug.dbcr0 |= DBCR0_IDM;

	/* Check for write and read flags and set DBCR0
	   accordingly */
	dbcr_dac(task) &= ~(DBCR_DAC1R|DBCR_DAC1W);
	if (data & 0x1UL)
		dbcr_dac(task) |= DBCR_DAC1R;
	if (data & 0x2UL)
		dbcr_dac(task) |= DBCR_DAC1W;
	task->thread.regs->msr |= MSR_DE;
#endif /* CONFIG_PPC_ADV_DEBUG_REGS */
	return 0;
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure single step bits etc are not set.
 */
void ptrace_disable(struct task_struct *child)
{
	/* make sure the single step bit is not set. */
	user_disable_single_step(child);
}

#ifdef CONFIG_PPC_ADV_DEBUG_REGS
static long set_instruction_bp(struct task_struct *child,
			      struct ppc_hw_breakpoint *bp_info)
{
	int slot;
	int slot1_in_use = ((child->thread.debug.dbcr0 & DBCR0_IAC1) != 0);
	int slot2_in_use = ((child->thread.debug.dbcr0 & DBCR0_IAC2) != 0);
	int slot3_in_use = ((child->thread.debug.dbcr0 & DBCR0_IAC3) != 0);
	int slot4_in_use = ((child->thread.debug.dbcr0 & DBCR0_IAC4) != 0);

	if (dbcr_iac_range(child) & DBCR_IAC12MODE)
		slot2_in_use = 1;
	if (dbcr_iac_range(child) & DBCR_IAC34MODE)
		slot4_in_use = 1;

	if (bp_info->addr >= TASK_SIZE)
		return -EIO;

	if (bp_info->addr_mode != PPC_BREAKPOINT_MODE_EXACT) {

		/* Make sure range is valid. */
		if (bp_info->addr2 >= TASK_SIZE)
			return -EIO;

		/* We need a pair of IAC regsisters */
		if ((!slot1_in_use) && (!slot2_in_use)) {
			slot = 1;
			child->thread.debug.iac1 = bp_info->addr;
			child->thread.debug.iac2 = bp_info->addr2;
			child->thread.debug.dbcr0 |= DBCR0_IAC1;
			if (bp_info->addr_mode ==
					PPC_BREAKPOINT_MODE_RANGE_EXCLUSIVE)
				dbcr_iac_range(child) |= DBCR_IAC12X;
			else
				dbcr_iac_range(child) |= DBCR_IAC12I;
#if CONFIG_PPC_ADV_DEBUG_IACS > 2
		} else if ((!slot3_in_use) && (!slot4_in_use)) {
			slot = 3;
			child->thread.debug.iac3 = bp_info->addr;
			child->thread.debug.iac4 = bp_info->addr2;
			child->thread.debug.dbcr0 |= DBCR0_IAC3;
			if (bp_info->addr_mode ==
					PPC_BREAKPOINT_MODE_RANGE_EXCLUSIVE)
				dbcr_iac_range(child) |= DBCR_IAC34X;
			else
				dbcr_iac_range(child) |= DBCR_IAC34I;
#endif
		} else
			return -ENOSPC;
	} else {
		/* We only need one.  If possible leave a pair free in
		 * case a range is needed later
		 */
		if (!slot1_in_use) {
			/*
			 * Don't use iac1 if iac1-iac2 are free and either
			 * iac3 or iac4 (but not both) are free
			 */
			if (slot2_in_use || (slot3_in_use == slot4_in_use)) {
				slot = 1;
				child->thread.debug.iac1 = bp_info->addr;
				child->thread.debug.dbcr0 |= DBCR0_IAC1;
				goto out;
			}
		}
		if (!slot2_in_use) {
			slot = 2;
			child->thread.debug.iac2 = bp_info->addr;
			child->thread.debug.dbcr0 |= DBCR0_IAC2;
#if CONFIG_PPC_ADV_DEBUG_IACS > 2
		} else if (!slot3_in_use) {
			slot = 3;
			child->thread.debug.iac3 = bp_info->addr;
			child->thread.debug.dbcr0 |= DBCR0_IAC3;
		} else if (!slot4_in_use) {
			slot = 4;
			child->thread.debug.iac4 = bp_info->addr;
			child->thread.debug.dbcr0 |= DBCR0_IAC4;
#endif
		} else
			return -ENOSPC;
	}
out:
	child->thread.debug.dbcr0 |= DBCR0_IDM;
	child->thread.regs->msr |= MSR_DE;

	return slot;
}

static int del_instruction_bp(struct task_struct *child, int slot)
{
	switch (slot) {
	case 1:
		if ((child->thread.debug.dbcr0 & DBCR0_IAC1) == 0)
			return -ENOENT;

		if (dbcr_iac_range(child) & DBCR_IAC12MODE) {
			/* address range - clear slots 1 & 2 */
			child->thread.debug.iac2 = 0;
			dbcr_iac_range(child) &= ~DBCR_IAC12MODE;
		}
		child->thread.debug.iac1 = 0;
		child->thread.debug.dbcr0 &= ~DBCR0_IAC1;
		break;
	case 2:
		if ((child->thread.debug.dbcr0 & DBCR0_IAC2) == 0)
			return -ENOENT;

		if (dbcr_iac_range(child) & DBCR_IAC12MODE)
			/* used in a range */
			return -EINVAL;
		child->thread.debug.iac2 = 0;
		child->thread.debug.dbcr0 &= ~DBCR0_IAC2;
		break;
#if CONFIG_PPC_ADV_DEBUG_IACS > 2
	case 3:
		if ((child->thread.debug.dbcr0 & DBCR0_IAC3) == 0)
			return -ENOENT;

		if (dbcr_iac_range(child) & DBCR_IAC34MODE) {
			/* address range - clear slots 3 & 4 */
			child->thread.debug.iac4 = 0;
			dbcr_iac_range(child) &= ~DBCR_IAC34MODE;
		}
		child->thread.debug.iac3 = 0;
		child->thread.debug.dbcr0 &= ~DBCR0_IAC3;
		break;
	case 4:
		if ((child->thread.debug.dbcr0 & DBCR0_IAC4) == 0)
			return -ENOENT;

		if (dbcr_iac_range(child) & DBCR_IAC34MODE)
			/* Used in a range */
			return -EINVAL;
		child->thread.debug.iac4 = 0;
		child->thread.debug.dbcr0 &= ~DBCR0_IAC4;
		break;
#endif
	default:
		return -EINVAL;
	}
	return 0;
}

static int set_dac(struct task_struct *child, struct ppc_hw_breakpoint *bp_info)
{
	int byte_enable =
		(bp_info->condition_mode >> PPC_BREAKPOINT_CONDITION_BE_SHIFT)
		& 0xf;
	int condition_mode =
		bp_info->condition_mode & PPC_BREAKPOINT_CONDITION_MODE;
	int slot;

	if (byte_enable && (condition_mode == 0))
		return -EINVAL;

	if (bp_info->addr >= TASK_SIZE)
		return -EIO;

	if ((dbcr_dac(child) & (DBCR_DAC1R | DBCR_DAC1W)) == 0) {
		slot = 1;
		if (bp_info->trigger_type & PPC_BREAKPOINT_TRIGGER_READ)
			dbcr_dac(child) |= DBCR_DAC1R;
		if (bp_info->trigger_type & PPC_BREAKPOINT_TRIGGER_WRITE)
			dbcr_dac(child) |= DBCR_DAC1W;
		child->thread.debug.dac1 = (unsigned long)bp_info->addr;
#if CONFIG_PPC_ADV_DEBUG_DVCS > 0
		if (byte_enable) {
			child->thread.debug.dvc1 =
				(unsigned long)bp_info->condition_value;
			child->thread.debug.dbcr2 |=
				((byte_enable << DBCR2_DVC1BE_SHIFT) |
				 (condition_mode << DBCR2_DVC1M_SHIFT));
		}
#endif
#ifdef CONFIG_PPC_ADV_DEBUG_DAC_RANGE
	} else if (child->thread.debug.dbcr2 & DBCR2_DAC12MODE) {
		/* Both dac1 and dac2 are part of a range */
		return -ENOSPC;
#endif
	} else if ((dbcr_dac(child) & (DBCR_DAC2R | DBCR_DAC2W)) == 0) {
		slot = 2;
		if (bp_info->trigger_type & PPC_BREAKPOINT_TRIGGER_READ)
			dbcr_dac(child) |= DBCR_DAC2R;
		if (bp_info->trigger_type & PPC_BREAKPOINT_TRIGGER_WRITE)
			dbcr_dac(child) |= DBCR_DAC2W;
		child->thread.debug.dac2 = (unsigned long)bp_info->addr;
#if CONFIG_PPC_ADV_DEBUG_DVCS > 0
		if (byte_enable) {
			child->thread.debug.dvc2 =
				(unsigned long)bp_info->condition_value;
			child->thread.debug.dbcr2 |=
				((byte_enable << DBCR2_DVC2BE_SHIFT) |
				 (condition_mode << DBCR2_DVC2M_SHIFT));
		}
#endif
	} else
		return -ENOSPC;
	child->thread.debug.dbcr0 |= DBCR0_IDM;
	child->thread.regs->msr |= MSR_DE;

	return slot + 4;
}

static int del_dac(struct task_struct *child, int slot)
{
	if (slot == 1) {
		if ((dbcr_dac(child) & (DBCR_DAC1R | DBCR_DAC1W)) == 0)
			return -ENOENT;

		child->thread.debug.dac1 = 0;
		dbcr_dac(child) &= ~(DBCR_DAC1R | DBCR_DAC1W);
#ifdef CONFIG_PPC_ADV_DEBUG_DAC_RANGE
		if (child->thread.debug.dbcr2 & DBCR2_DAC12MODE) {
			child->thread.debug.dac2 = 0;
			child->thread.debug.dbcr2 &= ~DBCR2_DAC12MODE;
		}
		child->thread.debug.dbcr2 &= ~(DBCR2_DVC1M | DBCR2_DVC1BE);
#endif
#if CONFIG_PPC_ADV_DEBUG_DVCS > 0
		child->thread.debug.dvc1 = 0;
#endif
	} else if (slot == 2) {
		if ((dbcr_dac(child) & (DBCR_DAC2R | DBCR_DAC2W)) == 0)
			return -ENOENT;

#ifdef CONFIG_PPC_ADV_DEBUG_DAC_RANGE
		if (child->thread.debug.dbcr2 & DBCR2_DAC12MODE)
			/* Part of a range */
			return -EINVAL;
		child->thread.debug.dbcr2 &= ~(DBCR2_DVC2M | DBCR2_DVC2BE);
#endif
#if CONFIG_PPC_ADV_DEBUG_DVCS > 0
		child->thread.debug.dvc2 = 0;
#endif
		child->thread.debug.dac2 = 0;
		dbcr_dac(child) &= ~(DBCR_DAC2R | DBCR_DAC2W);
	} else
		return -EINVAL;

	return 0;
}
#endif /* CONFIG_PPC_ADV_DEBUG_REGS */

#ifdef CONFIG_PPC_ADV_DEBUG_DAC_RANGE
static int set_dac_range(struct task_struct *child,
			 struct ppc_hw_breakpoint *bp_info)
{
	int mode = bp_info->addr_mode & PPC_BREAKPOINT_MODE_MASK;

	/* We don't allow range watchpoints to be used with DVC */
	if (bp_info->condition_mode)
		return -EINVAL;

	/*
	 * Best effort to verify the address range.  The user/supervisor bits
	 * prevent trapping in kernel space, but let's fail on an obvious bad
	 * range.  The simple test on the mask is not fool-proof, and any
	 * exclusive range will spill over into kernel space.
	 */
	if (bp_info->addr >= TASK_SIZE)
		return -EIO;
	if (mode == PPC_BREAKPOINT_MODE_MASK) {
		/*
		 * dac2 is a bitmask.  Don't allow a mask that makes a
		 * kernel space address from a valid dac1 value
		 */
		if (~((unsigned long)bp_info->addr2) >= TASK_SIZE)
			return -EIO;
	} else {
		/*
		 * For range breakpoints, addr2 must also be a valid address
		 */
		if (bp_info->addr2 >= TASK_SIZE)
			return -EIO;
	}

	if (child->thread.debug.dbcr0 &
	    (DBCR0_DAC1R | DBCR0_DAC1W | DBCR0_DAC2R | DBCR0_DAC2W))
		return -ENOSPC;

	if (bp_info->trigger_type & PPC_BREAKPOINT_TRIGGER_READ)
		child->thread.debug.dbcr0 |= (DBCR0_DAC1R | DBCR0_IDM);
	if (bp_info->trigger_type & PPC_BREAKPOINT_TRIGGER_WRITE)
		child->thread.debug.dbcr0 |= (DBCR0_DAC1W | DBCR0_IDM);
	child->thread.debug.dac1 = bp_info->addr;
	child->thread.debug.dac2 = bp_info->addr2;
	if (mode == PPC_BREAKPOINT_MODE_RANGE_INCLUSIVE)
		child->thread.debug.dbcr2  |= DBCR2_DAC12M;
	else if (mode == PPC_BREAKPOINT_MODE_RANGE_EXCLUSIVE)
		child->thread.debug.dbcr2  |= DBCR2_DAC12MX;
	else	/* PPC_BREAKPOINT_MODE_MASK */
		child->thread.debug.dbcr2  |= DBCR2_DAC12MM;
	child->thread.regs->msr |= MSR_DE;

	return 5;
}
#endif /* CONFIG_PPC_ADV_DEBUG_DAC_RANGE */

static long ppc_set_hwdebug(struct task_struct *child,
		     struct ppc_hw_breakpoint *bp_info)
{
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	int len = 0;
	struct thread_struct *thread = &(child->thread);
	struct perf_event *bp;
	struct perf_event_attr attr;
#endif /* CONFIG_HAVE_HW_BREAKPOINT */
#ifndef CONFIG_PPC_ADV_DEBUG_REGS
	struct arch_hw_breakpoint brk;
#endif

	if (bp_info->version != 1)
		return -ENOTSUPP;
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
	/*
	 * Check for invalid flags and combinations
	 */
	if ((bp_info->trigger_type == 0) ||
	    (bp_info->trigger_type & ~(PPC_BREAKPOINT_TRIGGER_EXECUTE |
				       PPC_BREAKPOINT_TRIGGER_RW)) ||
	    (bp_info->addr_mode & ~PPC_BREAKPOINT_MODE_MASK) ||
	    (bp_info->condition_mode &
	     ~(PPC_BREAKPOINT_CONDITION_MODE |
	       PPC_BREAKPOINT_CONDITION_BE_ALL)))
		return -EINVAL;
#if CONFIG_PPC_ADV_DEBUG_DVCS == 0
	if (bp_info->condition_mode != PPC_BREAKPOINT_CONDITION_NONE)
		return -EINVAL;
#endif

	if (bp_info->trigger_type & PPC_BREAKPOINT_TRIGGER_EXECUTE) {
		if ((bp_info->trigger_type != PPC_BREAKPOINT_TRIGGER_EXECUTE) ||
		    (bp_info->condition_mode != PPC_BREAKPOINT_CONDITION_NONE))
			return -EINVAL;
		return set_instruction_bp(child, bp_info);
	}
	if (bp_info->addr_mode == PPC_BREAKPOINT_MODE_EXACT)
		return set_dac(child, bp_info);

#ifdef CONFIG_PPC_ADV_DEBUG_DAC_RANGE
	return set_dac_range(child, bp_info);
#else
	return -EINVAL;
#endif
#else /* !CONFIG_PPC_ADV_DEBUG_DVCS */
	/*
	 * We only support one data breakpoint
	 */
	if ((bp_info->trigger_type & PPC_BREAKPOINT_TRIGGER_RW) == 0 ||
	    (bp_info->trigger_type & ~PPC_BREAKPOINT_TRIGGER_RW) != 0 ||
	    bp_info->condition_mode != PPC_BREAKPOINT_CONDITION_NONE)
		return -EINVAL;

	if ((unsigned long)bp_info->addr >= TASK_SIZE)
		return -EIO;

	brk.address = bp_info->addr & ~HW_BREAKPOINT_ALIGN;
	brk.type = HW_BRK_TYPE_TRANSLATE;
	brk.len = DABR_MAX_LEN;
	if (bp_info->trigger_type & PPC_BREAKPOINT_TRIGGER_READ)
		brk.type |= HW_BRK_TYPE_READ;
	if (bp_info->trigger_type & PPC_BREAKPOINT_TRIGGER_WRITE)
		brk.type |= HW_BRK_TYPE_WRITE;
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	if (bp_info->addr_mode == PPC_BREAKPOINT_MODE_RANGE_INCLUSIVE)
		len = bp_info->addr2 - bp_info->addr;
	else if (bp_info->addr_mode == PPC_BREAKPOINT_MODE_EXACT)
		len = 1;
	else
		return -EINVAL;
	bp = thread->ptrace_bps[0];
	if (bp)
		return -ENOSPC;

	/* Create a new breakpoint request if one doesn't exist already */
	hw_breakpoint_init(&attr);
	attr.bp_addr = (unsigned long)bp_info->addr;
	attr.bp_len = len;
	arch_bp_generic_fields(brk.type, &attr.bp_type);

	thread->ptrace_bps[0] = bp = register_user_hw_breakpoint(&attr,
					       ptrace_triggered, NULL, child);
	if (IS_ERR(bp)) {
		thread->ptrace_bps[0] = NULL;
		return PTR_ERR(bp);
	}

	return 1;
#endif /* CONFIG_HAVE_HW_BREAKPOINT */

	if (bp_info->addr_mode != PPC_BREAKPOINT_MODE_EXACT)
		return -EINVAL;

	if (child->thread.hw_brk.address)
		return -ENOSPC;

	if (!ppc_breakpoint_available())
		return -ENODEV;

	child->thread.hw_brk = brk;

	return 1;
#endif /* !CONFIG_PPC_ADV_DEBUG_DVCS */
}

static long ppc_del_hwdebug(struct task_struct *child, long data)
{
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	int ret = 0;
	struct thread_struct *thread = &(child->thread);
	struct perf_event *bp;
#endif /* CONFIG_HAVE_HW_BREAKPOINT */
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
	int rc;

	if (data <= 4)
		rc = del_instruction_bp(child, (int)data);
	else
		rc = del_dac(child, (int)data - 4);

	if (!rc) {
		if (!DBCR_ACTIVE_EVENTS(child->thread.debug.dbcr0,
					child->thread.debug.dbcr1)) {
			child->thread.debug.dbcr0 &= ~DBCR0_IDM;
			child->thread.regs->msr &= ~MSR_DE;
		}
	}
	return rc;
#else
	if (data != 1)
		return -EINVAL;

#ifdef CONFIG_HAVE_HW_BREAKPOINT
	bp = thread->ptrace_bps[0];
	if (bp) {
		unregister_hw_breakpoint(bp);
		thread->ptrace_bps[0] = NULL;
	} else
		ret = -ENOENT;
	return ret;
#else /* CONFIG_HAVE_HW_BREAKPOINT */
	if (child->thread.hw_brk.address == 0)
		return -ENOENT;

	child->thread.hw_brk.address = 0;
	child->thread.hw_brk.type = 0;
#endif /* CONFIG_HAVE_HW_BREAKPOINT */

	return 0;
#endif
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	int ret = -EPERM;
	void __user *datavp = (void __user *) data;
	unsigned long __user *datalp = datavp;

	switch (request) {
	/* read the word at location addr in the USER area. */
	case PTRACE_PEEKUSR: {
		unsigned long index, tmp;

		ret = -EIO;
		/* convert to index and check */
#ifdef CONFIG_PPC32
		index = addr >> 2;
		if ((addr & 3) || (index > PT_FPSCR)
		    || (child->thread.regs == NULL))
#else
		index = addr >> 3;
		if ((addr & 7) || (index > PT_FPSCR))
#endif
			break;

		CHECK_FULL_REGS(child->thread.regs);
		if (index < PT_FPR0) {
			ret = ptrace_get_reg(child, (int) index, &tmp);
			if (ret)
				break;
		} else {
			unsigned int fpidx = index - PT_FPR0;

			flush_fp_to_thread(child);
			if (fpidx < (PT_FPSCR - PT_FPR0))
				memcpy(&tmp, &child->thread.TS_FPR(fpidx),
				       sizeof(long));
			else
				tmp = child->thread.fp_state.fpscr;
		}
		ret = put_user(tmp, datalp);
		break;
	}

	/* write the word at location addr in the USER area */
	case PTRACE_POKEUSR: {
		unsigned long index;

		ret = -EIO;
		/* convert to index and check */
#ifdef CONFIG_PPC32
		index = addr >> 2;
		if ((addr & 3) || (index > PT_FPSCR)
		    || (child->thread.regs == NULL))
#else
		index = addr >> 3;
		if ((addr & 7) || (index > PT_FPSCR))
#endif
			break;

		CHECK_FULL_REGS(child->thread.regs);
		if (index < PT_FPR0) {
			ret = ptrace_put_reg(child, index, data);
		} else {
			unsigned int fpidx = index - PT_FPR0;

			flush_fp_to_thread(child);
			if (fpidx < (PT_FPSCR - PT_FPR0))
				memcpy(&child->thread.TS_FPR(fpidx), &data,
				       sizeof(long));
			else
				child->thread.fp_state.fpscr = data;
			ret = 0;
		}
		break;
	}

	case PPC_PTRACE_GETHWDBGINFO: {
		struct ppc_debug_info dbginfo;

		dbginfo.version = 1;
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
		dbginfo.num_instruction_bps = CONFIG_PPC_ADV_DEBUG_IACS;
		dbginfo.num_data_bps = CONFIG_PPC_ADV_DEBUG_DACS;
		dbginfo.num_condition_regs = CONFIG_PPC_ADV_DEBUG_DVCS;
		dbginfo.data_bp_alignment = 4;
		dbginfo.sizeof_condition = 4;
		dbginfo.features = PPC_DEBUG_FEATURE_INSN_BP_RANGE |
				   PPC_DEBUG_FEATURE_INSN_BP_MASK;
#ifdef CONFIG_PPC_ADV_DEBUG_DAC_RANGE
		dbginfo.features |=
				   PPC_DEBUG_FEATURE_DATA_BP_RANGE |
				   PPC_DEBUG_FEATURE_DATA_BP_MASK;
#endif
#else /* !CONFIG_PPC_ADV_DEBUG_REGS */
		dbginfo.num_instruction_bps = 0;
		if (ppc_breakpoint_available())
			dbginfo.num_data_bps = 1;
		else
			dbginfo.num_data_bps = 0;
		dbginfo.num_condition_regs = 0;
		dbginfo.data_bp_alignment = sizeof(long);
		dbginfo.sizeof_condition = 0;
#ifdef CONFIG_HAVE_HW_BREAKPOINT
		dbginfo.features = PPC_DEBUG_FEATURE_DATA_BP_RANGE;
		if (dawr_enabled())
			dbginfo.features |= PPC_DEBUG_FEATURE_DATA_BP_DAWR;
#else
		dbginfo.features = 0;
#endif /* CONFIG_HAVE_HW_BREAKPOINT */
#endif /* CONFIG_PPC_ADV_DEBUG_REGS */

		if (copy_to_user(datavp, &dbginfo,
				 sizeof(struct ppc_debug_info)))
			return -EFAULT;
		return 0;
	}

	case PPC_PTRACE_SETHWDEBUG: {
		struct ppc_hw_breakpoint bp_info;

		if (copy_from_user(&bp_info, datavp,
				   sizeof(struct ppc_hw_breakpoint)))
			return -EFAULT;
		return ppc_set_hwdebug(child, &bp_info);
	}

	case PPC_PTRACE_DELHWDEBUG: {
		ret = ppc_del_hwdebug(child, data);
		break;
	}

	case PTRACE_GET_DEBUGREG: {
#ifndef CONFIG_PPC_ADV_DEBUG_REGS
		unsigned long dabr_fake;
#endif
		ret = -EINVAL;
		/* We only support one DABR and no IABRS at the moment */
		if (addr > 0)
			break;
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
		ret = put_user(child->thread.debug.dac1, datalp);
#else
		dabr_fake = ((child->thread.hw_brk.address & (~HW_BRK_TYPE_DABR)) |
			     (child->thread.hw_brk.type & HW_BRK_TYPE_DABR));
		ret = put_user(dabr_fake, datalp);
#endif
		break;
	}

	case PTRACE_SET_DEBUGREG:
		ret = ptrace_set_debugreg(child, addr, data);
		break;

#ifdef CONFIG_PPC64
	case PTRACE_GETREGS64:
#endif
	case PTRACE_GETREGS:	/* Get all pt_regs from the child. */
		return copy_regset_to_user(child, &user_ppc_native_view,
					   REGSET_GPR,
					   0, sizeof(struct user_pt_regs),
					   datavp);

#ifdef CONFIG_PPC64
	case PTRACE_SETREGS64:
#endif
	case PTRACE_SETREGS:	/* Set all gp regs in the child. */
		return copy_regset_from_user(child, &user_ppc_native_view,
					     REGSET_GPR,
					     0, sizeof(struct user_pt_regs),
					     datavp);

	case PTRACE_GETFPREGS: /* Get the child FPU state (FPR0...31 + FPSCR) */
		return copy_regset_to_user(child, &user_ppc_native_view,
					   REGSET_FPR,
					   0, sizeof(elf_fpregset_t),
					   datavp);

	case PTRACE_SETFPREGS: /* Set the child FPU state (FPR0...31 + FPSCR) */
		return copy_regset_from_user(child, &user_ppc_native_view,
					     REGSET_FPR,
					     0, sizeof(elf_fpregset_t),
					     datavp);

#ifdef CONFIG_ALTIVEC
	case PTRACE_GETVRREGS:
		return copy_regset_to_user(child, &user_ppc_native_view,
					   REGSET_VMX,
					   0, (33 * sizeof(vector128) +
					       sizeof(u32)),
					   datavp);

	case PTRACE_SETVRREGS:
		return copy_regset_from_user(child, &user_ppc_native_view,
					     REGSET_VMX,
					     0, (33 * sizeof(vector128) +
						 sizeof(u32)),
					     datavp);
#endif
#ifdef CONFIG_VSX
	case PTRACE_GETVSRREGS:
		return copy_regset_to_user(child, &user_ppc_native_view,
					   REGSET_VSX,
					   0, 32 * sizeof(double),
					   datavp);

	case PTRACE_SETVSRREGS:
		return copy_regset_from_user(child, &user_ppc_native_view,
					     REGSET_VSX,
					     0, 32 * sizeof(double),
					     datavp);
#endif
#ifdef CONFIG_SPE
	case PTRACE_GETEVRREGS:
		/* Get the child spe register state. */
		return copy_regset_to_user(child, &user_ppc_native_view,
					   REGSET_SPE, 0, 35 * sizeof(u32),
					   datavp);

	case PTRACE_SETEVRREGS:
		/* Set the child spe register state. */
		return copy_regset_from_user(child, &user_ppc_native_view,
					     REGSET_SPE, 0, 35 * sizeof(u32),
					     datavp);
#endif

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}
	return ret;
}

#ifdef CONFIG_SECCOMP
static int do_seccomp(struct pt_regs *regs)
{
	if (!test_thread_flag(TIF_SECCOMP))
		return 0;

	/*
	 * The ABI we present to seccomp tracers is that r3 contains
	 * the syscall return value and orig_gpr3 contains the first
	 * syscall parameter. This is different to the ptrace ABI where
	 * both r3 and orig_gpr3 contain the first syscall parameter.
	 */
	regs->gpr[3] = -ENOSYS;

	/*
	 * We use the __ version here because we have already checked
	 * TIF_SECCOMP. If this fails, there is nothing left to do, we
	 * have already loaded -ENOSYS into r3, or seccomp has put
	 * something else in r3 (via SECCOMP_RET_ERRNO/TRACE).
	 */
	if (__secure_computing(NULL))
		return -1;

	/*
	 * The syscall was allowed by seccomp, restore the register
	 * state to what audit expects.
	 * Note that we use orig_gpr3, which means a seccomp tracer can
	 * modify the first syscall parameter (in orig_gpr3) and also
	 * allow the syscall to proceed.
	 */
	regs->gpr[3] = regs->orig_gpr3;

	return 0;
}
#else
static inline int do_seccomp(struct pt_regs *regs) { return 0; }
#endif /* CONFIG_SECCOMP */

/**
 * do_syscall_trace_enter() - Do syscall tracing on kernel entry.
 * @regs: the pt_regs of the task to trace (current)
 *
 * Performs various types of tracing on syscall entry. This includes seccomp,
 * ptrace, syscall tracepoints and audit.
 *
 * The pt_regs are potentially visible to userspace via ptrace, so their
 * contents is ABI.
 *
 * One or more of the tracers may modify the contents of pt_regs, in particular
 * to modify arguments or even the syscall number itself.
 *
 * It's also possible that a tracer can choose to reject the system call. In
 * that case this function will return an illegal syscall number, and will put
 * an appropriate return value in regs->r3.
 *
 * Return: the (possibly changed) syscall number.
 */
long do_syscall_trace_enter(struct pt_regs *regs)
{
	u32 flags;

	user_exit();

	flags = READ_ONCE(current_thread_info()->flags) &
		(_TIF_SYSCALL_EMU | _TIF_SYSCALL_TRACE);

	if (flags) {
		int rc = tracehook_report_syscall_entry(regs);

		if (unlikely(flags & _TIF_SYSCALL_EMU)) {
			/*
			 * A nonzero return code from
			 * tracehook_report_syscall_entry() tells us to prevent
			 * the syscall execution, but we are not going to
			 * execute it anyway.
			 *
			 * Returning -1 will skip the syscall execution. We want
			 * to avoid clobbering any registers, so we don't goto
			 * the skip label below.
			 */
			return -1;
		}

		if (rc) {
			/*
			 * The tracer decided to abort the syscall. Note that
			 * the tracer may also just change regs->gpr[0] to an
			 * invalid syscall number, that is handled below on the
			 * exit path.
			 */
			goto skip;
		}
	}

	/* Run seccomp after ptrace; allow it to set gpr[3]. */
	if (do_seccomp(regs))
		return -1;

	/* Avoid trace and audit when syscall is invalid. */
	if (regs->gpr[0] >= NR_syscalls)
		goto skip;

	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_enter(regs, regs->gpr[0]);

	if (!is_32bit_task())
		audit_syscall_entry(regs->gpr[0], regs->gpr[3], regs->gpr[4],
				    regs->gpr[5], regs->gpr[6]);
	else
		audit_syscall_entry(regs->gpr[0],
				    regs->gpr[3] & 0xffffffff,
				    regs->gpr[4] & 0xffffffff,
				    regs->gpr[5] & 0xffffffff,
				    regs->gpr[6] & 0xffffffff);

	/* Return the possibly modified but valid syscall number */
	return regs->gpr[0];

skip:
	/*
	 * If we are aborting explicitly, or if the syscall number is
	 * now invalid, set the return value to -ENOSYS.
	 */
	regs->gpr[3] = -ENOSYS;
	return -1;
}

void do_syscall_trace_leave(struct pt_regs *regs)
{
	int step;

	audit_syscall_exit(regs);

	if (unlikely(test_thread_flag(TIF_SYSCALL_TRACEPOINT)))
		trace_sys_exit(regs, regs->result);

	step = test_thread_flag(TIF_SINGLESTEP);
	if (step || test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, step);

	user_enter();
}

void __init pt_regs_check(void);

/*
 * Dummy function, its purpose is to break the build if struct pt_regs and
 * struct user_pt_regs don't match.
 */
void __init pt_regs_check(void)
{
	BUILD_BUG_ON(offsetof(struct pt_regs, gpr) !=
		     offsetof(struct user_pt_regs, gpr));
	BUILD_BUG_ON(offsetof(struct pt_regs, nip) !=
		     offsetof(struct user_pt_regs, nip));
	BUILD_BUG_ON(offsetof(struct pt_regs, msr) !=
		     offsetof(struct user_pt_regs, msr));
	BUILD_BUG_ON(offsetof(struct pt_regs, msr) !=
		     offsetof(struct user_pt_regs, msr));
	BUILD_BUG_ON(offsetof(struct pt_regs, orig_gpr3) !=
		     offsetof(struct user_pt_regs, orig_gpr3));
	BUILD_BUG_ON(offsetof(struct pt_regs, ctr) !=
		     offsetof(struct user_pt_regs, ctr));
	BUILD_BUG_ON(offsetof(struct pt_regs, link) !=
		     offsetof(struct user_pt_regs, link));
	BUILD_BUG_ON(offsetof(struct pt_regs, xer) !=
		     offsetof(struct user_pt_regs, xer));
	BUILD_BUG_ON(offsetof(struct pt_regs, ccr) !=
		     offsetof(struct user_pt_regs, ccr));
#ifdef __powerpc64__
	BUILD_BUG_ON(offsetof(struct pt_regs, softe) !=
		     offsetof(struct user_pt_regs, softe));
#else
	BUILD_BUG_ON(offsetof(struct pt_regs, mq) !=
		     offsetof(struct user_pt_regs, mq));
#endif
	BUILD_BUG_ON(offsetof(struct pt_regs, trap) !=
		     offsetof(struct user_pt_regs, trap));
	BUILD_BUG_ON(offsetof(struct pt_regs, dar) !=
		     offsetof(struct user_pt_regs, dar));
	BUILD_BUG_ON(offsetof(struct pt_regs, dsisr) !=
		     offsetof(struct user_pt_regs, dsisr));
	BUILD_BUG_ON(offsetof(struct pt_regs, result) !=
		     offsetof(struct user_pt_regs, result));

	BUILD_BUG_ON(sizeof(struct user_pt_regs) > sizeof(struct pt_regs));

	// Now check that the pt_regs offsets match the uapi #defines
	#define CHECK_REG(_pt, _reg) \
		BUILD_BUG_ON(_pt != (offsetof(struct user_pt_regs, _reg) / \
				     sizeof(unsigned long)));

	CHECK_REG(PT_R0,  gpr[0]);
	CHECK_REG(PT_R1,  gpr[1]);
	CHECK_REG(PT_R2,  gpr[2]);
	CHECK_REG(PT_R3,  gpr[3]);
	CHECK_REG(PT_R4,  gpr[4]);
	CHECK_REG(PT_R5,  gpr[5]);
	CHECK_REG(PT_R6,  gpr[6]);
	CHECK_REG(PT_R7,  gpr[7]);
	CHECK_REG(PT_R8,  gpr[8]);
	CHECK_REG(PT_R9,  gpr[9]);
	CHECK_REG(PT_R10, gpr[10]);
	CHECK_REG(PT_R11, gpr[11]);
	CHECK_REG(PT_R12, gpr[12]);
	CHECK_REG(PT_R13, gpr[13]);
	CHECK_REG(PT_R14, gpr[14]);
	CHECK_REG(PT_R15, gpr[15]);
	CHECK_REG(PT_R16, gpr[16]);
	CHECK_REG(PT_R17, gpr[17]);
	CHECK_REG(PT_R18, gpr[18]);
	CHECK_REG(PT_R19, gpr[19]);
	CHECK_REG(PT_R20, gpr[20]);
	CHECK_REG(PT_R21, gpr[21]);
	CHECK_REG(PT_R22, gpr[22]);
	CHECK_REG(PT_R23, gpr[23]);
	CHECK_REG(PT_R24, gpr[24]);
	CHECK_REG(PT_R25, gpr[25]);
	CHECK_REG(PT_R26, gpr[26]);
	CHECK_REG(PT_R27, gpr[27]);
	CHECK_REG(PT_R28, gpr[28]);
	CHECK_REG(PT_R29, gpr[29]);
	CHECK_REG(PT_R30, gpr[30]);
	CHECK_REG(PT_R31, gpr[31]);
	CHECK_REG(PT_NIP, nip);
	CHECK_REG(PT_MSR, msr);
	CHECK_REG(PT_ORIG_R3, orig_gpr3);
	CHECK_REG(PT_CTR, ctr);
	CHECK_REG(PT_LNK, link);
	CHECK_REG(PT_XER, xer);
	CHECK_REG(PT_CCR, ccr);
#ifdef CONFIG_PPC64
	CHECK_REG(PT_SOFTE, softe);
#else
	CHECK_REG(PT_MQ, mq);
#endif
	CHECK_REG(PT_TRAP, trap);
	CHECK_REG(PT_DAR, dar);
	CHECK_REG(PT_DSISR, dsisr);
	CHECK_REG(PT_RESULT, result);
	#undef CHECK_REG

	BUILD_BUG_ON(PT_REGS_COUNT != sizeof(struct user_pt_regs) / sizeof(unsigned long));

	/*
	 * PT_DSCR isn't a real reg, but it's important that it doesn't overlap the
	 * real registers.
	 */
	BUILD_BUG_ON(PT_DSCR < sizeof(struct user_pt_regs) / sizeof(unsigned long));
}
