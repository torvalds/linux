// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/regset.h>
#include <linux/hw_breakpoint.h>

#include "ptrace-decl.h"

void user_enable_single_step(struct task_struct *task)
{
	struct pt_regs *regs = task->thread.regs;

	if (regs != NULL) {
		task->thread.debug.dbcr0 &= ~DBCR0_BT;
		task->thread.debug.dbcr0 |= DBCR0_IDM | DBCR0_IC;
		regs->msr |= MSR_DE;
	}
	set_tsk_thread_flag(task, TIF_SINGLESTEP);
}

void user_enable_block_step(struct task_struct *task)
{
	struct pt_regs *regs = task->thread.regs;

	if (regs != NULL) {
		task->thread.debug.dbcr0 &= ~DBCR0_IC;
		task->thread.debug.dbcr0 = DBCR0_IDM | DBCR0_BT;
		regs->msr |= MSR_DE;
	}
	set_tsk_thread_flag(task, TIF_SINGLESTEP);
}

void user_disable_single_step(struct task_struct *task)
{
	struct pt_regs *regs = task->thread.regs;

	if (regs != NULL) {
		/*
		 * The logic to disable single stepping should be as
		 * simple as turning off the Instruction Complete flag.
		 * And, after doing so, if all debug flags are off, turn
		 * off DBCR0(IDM) and MSR(DE) .... Torez
		 */
		task->thread.debug.dbcr0 &= ~(DBCR0_IC | DBCR0_BT);
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
	}
	clear_tsk_thread_flag(task, TIF_SINGLESTEP);
}

void ppc_gethwdinfo(struct ppc_debug_info *dbginfo)
{
	dbginfo->version = 1;
	dbginfo->num_instruction_bps = CONFIG_PPC_ADV_DEBUG_IACS;
	dbginfo->num_data_bps = CONFIG_PPC_ADV_DEBUG_DACS;
	dbginfo->num_condition_regs = CONFIG_PPC_ADV_DEBUG_DVCS;
	dbginfo->data_bp_alignment = 4;
	dbginfo->sizeof_condition = 4;
	dbginfo->features = PPC_DEBUG_FEATURE_INSN_BP_RANGE |
			    PPC_DEBUG_FEATURE_INSN_BP_MASK;
	if (IS_ENABLED(CONFIG_PPC_ADV_DEBUG_DAC_RANGE))
		dbginfo->features |= PPC_DEBUG_FEATURE_DATA_BP_RANGE |
				     PPC_DEBUG_FEATURE_DATA_BP_MASK;
}

int ptrace_get_debugreg(struct task_struct *child, unsigned long addr,
			unsigned long __user *datalp)
{
	/* We only support one DABR and no IABRS at the moment */
	if (addr > 0)
		return -EINVAL;
	return put_user(child->thread.debug.dac1, datalp);
}

int ptrace_set_debugreg(struct task_struct *task, unsigned long addr, unsigned long data)
{
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	int ret;
	struct thread_struct *thread = &task->thread;
	struct perf_event *bp;
	struct perf_event_attr attr;
#endif /* CONFIG_HAVE_HW_BREAKPOINT */

	/* For ppc64 we support one DABR and no IABR's at the moment (ppc64).
	 *  For embedded processors we support one DAC and no IAC's at the
	 *  moment.
	 */
	if (addr > 0)
		return -EINVAL;

	/* The bottom 3 bits in dabr are flags */
	if ((data & ~0x7UL) >= TASK_SIZE)
		return -EIO;

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

	/* Set the Internal Debugging flag (IDM bit 1) for the DBCR0 register */
	task->thread.debug.dbcr0 |= DBCR0_IDM;

	/* Check for write and read flags and set DBCR0 accordingly */
	dbcr_dac(task) &= ~(DBCR_DAC1R | DBCR_DAC1W);
	if (data & 0x1UL)
		dbcr_dac(task) |= DBCR_DAC1R;
	if (data & 0x2UL)
		dbcr_dac(task) |= DBCR_DAC1W;
	task->thread.regs->msr |= MSR_DE;
	return 0;
}

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
		if (!slot1_in_use && !slot2_in_use) {
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
		} else {
			return -ENOSPC;
		}
	} else {
		/* We only need one.  If possible leave a pair free in
		 * case a range is needed later
		 */
		if (!slot1_in_use) {
			/*
			 * Don't use iac1 if iac1-iac2 are free and either
			 * iac3 or iac4 (but not both) are free
			 */
			if (slot2_in_use || slot3_in_use == slot4_in_use) {
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
		} else {
			return -ENOSPC;
		}
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

	if (byte_enable && condition_mode == 0)
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
	} else {
		return -ENOSPC;
	}
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
	} else {
		return -EINVAL;
	}

	return 0;
}

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

long ppc_set_hwdebug(struct task_struct *child, struct ppc_hw_breakpoint *bp_info)
{
	if (bp_info->version != 1)
		return -ENOTSUPP;
	/*
	 * Check for invalid flags and combinations
	 */
	if (bp_info->trigger_type == 0 ||
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
		if (bp_info->trigger_type != PPC_BREAKPOINT_TRIGGER_EXECUTE ||
		    bp_info->condition_mode != PPC_BREAKPOINT_CONDITION_NONE)
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
}

long ppc_del_hwdebug(struct task_struct *child, long data)
{
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
}
