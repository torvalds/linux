// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/regset.h>
#include <linux/hw_breakpoint.h>

#include <asm/debug.h>

#include "ptrace-decl.h"

void user_enable_single_step(struct task_struct *task)
{
	struct pt_regs *regs = task->thread.regs;

	if (regs != NULL) {
		regs->msr &= ~MSR_BE;
		regs->msr |= MSR_SE;
	}
	set_tsk_thread_flag(task, TIF_SINGLESTEP);
}

void user_enable_block_step(struct task_struct *task)
{
	struct pt_regs *regs = task->thread.regs;

	if (regs != NULL) {
		regs->msr &= ~MSR_SE;
		regs->msr |= MSR_BE;
	}
	set_tsk_thread_flag(task, TIF_SINGLESTEP);
}

void user_disable_single_step(struct task_struct *task)
{
	struct pt_regs *regs = task->thread.regs;

	if (regs != NULL)
		regs->msr &= ~(MSR_SE | MSR_BE);

	clear_tsk_thread_flag(task, TIF_SINGLESTEP);
}

void ppc_gethwdinfo(struct ppc_debug_info *dbginfo)
{
	dbginfo->version = 1;
	dbginfo->num_instruction_bps = 0;
	if (ppc_breakpoint_available())
		dbginfo->num_data_bps = nr_wp_slots();
	else
		dbginfo->num_data_bps = 0;
	dbginfo->num_condition_regs = 0;
	dbginfo->data_bp_alignment = sizeof(long);
	dbginfo->sizeof_condition = 0;
	if (IS_ENABLED(CONFIG_HAVE_HW_BREAKPOINT)) {
		dbginfo->features = PPC_DEBUG_FEATURE_DATA_BP_RANGE;
		if (dawr_enabled())
			dbginfo->features |= PPC_DEBUG_FEATURE_DATA_BP_DAWR;
	} else {
		dbginfo->features = 0;
	}
}

int ptrace_get_debugreg(struct task_struct *child, unsigned long addr,
			unsigned long __user *datalp)
{
	unsigned long dabr_fake;

	/* We only support one DABR and no IABRS at the moment */
	if (addr > 0)
		return -EINVAL;
	dabr_fake = ((child->thread.hw_brk[0].address & (~HW_BRK_TYPE_DABR)) |
		     (child->thread.hw_brk[0].type & HW_BRK_TYPE_DABR));
	return put_user(dabr_fake, datalp);
}

/*
 * ptrace_set_debugreg() fakes DABR and DABR is only one. So even if
 * internal hw supports more than one watchpoint, we support only one
 * watchpoint with this interface.
 */
int ptrace_set_debugreg(struct task_struct *task, unsigned long addr, unsigned long data)
{
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	int ret;
	struct thread_struct *thread = &task->thread;
	struct perf_event *bp;
	struct perf_event_attr attr;
#endif /* CONFIG_HAVE_HW_BREAKPOINT */
	bool set_bp = true;
	struct arch_hw_breakpoint hw_brk;

	/* For ppc64 we support one DABR and no IABR's at the moment (ppc64).
	 *  For embedded processors we support one DAC and no IAC's at the
	 *  moment.
	 */
	if (addr > 0)
		return -EINVAL;

	/* The bottom 3 bits in dabr are flags */
	if ((data & ~0x7UL) >= TASK_SIZE)
		return -EIO;

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
		if (ret)
			return ret;

		thread->ptrace_bps[0] = bp;
		thread->hw_brk[0] = hw_brk;
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
	task->thread.hw_brk[0] = hw_brk;
	return 0;
}

#ifdef CONFIG_HAVE_HW_BREAKPOINT
static int find_empty_ptrace_bp(struct thread_struct *thread)
{
	int i;

	for (i = 0; i < nr_wp_slots(); i++) {
		if (!thread->ptrace_bps[i])
			return i;
	}
	return -1;
}
#endif

static int find_empty_hw_brk(struct thread_struct *thread)
{
	int i;

	for (i = 0; i < nr_wp_slots(); i++) {
		if (!thread->hw_brk[i].address)
			return i;
	}
	return -1;
}

long ppc_set_hwdebug(struct task_struct *child, struct ppc_hw_breakpoint *bp_info)
{
	int i;
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	int len = 0;
	struct thread_struct *thread = &child->thread;
	struct perf_event *bp;
	struct perf_event_attr attr;
#endif /* CONFIG_HAVE_HW_BREAKPOINT */
	struct arch_hw_breakpoint brk;

	if (bp_info->version != 1)
		return -ENOTSUPP;
	/*
	 * We only support one data breakpoint
	 */
	if ((bp_info->trigger_type & PPC_BREAKPOINT_TRIGGER_RW) == 0 ||
	    (bp_info->trigger_type & ~PPC_BREAKPOINT_TRIGGER_RW) != 0 ||
	    bp_info->condition_mode != PPC_BREAKPOINT_CONDITION_NONE)
		return -EINVAL;

	if ((unsigned long)bp_info->addr >= TASK_SIZE)
		return -EIO;

	brk.address = ALIGN_DOWN(bp_info->addr, HW_BREAKPOINT_SIZE);
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

	i = find_empty_ptrace_bp(thread);
	if (i < 0)
		return -ENOSPC;

	/* Create a new breakpoint request if one doesn't exist already */
	hw_breakpoint_init(&attr);
	attr.bp_addr = (unsigned long)bp_info->addr;
	attr.bp_len = len;
	arch_bp_generic_fields(brk.type, &attr.bp_type);

	bp = register_user_hw_breakpoint(&attr, ptrace_triggered, NULL, child);
	thread->ptrace_bps[i] = bp;
	if (IS_ERR(bp)) {
		thread->ptrace_bps[i] = NULL;
		return PTR_ERR(bp);
	}

	return i + 1;
#endif /* CONFIG_HAVE_HW_BREAKPOINT */

	if (bp_info->addr_mode != PPC_BREAKPOINT_MODE_EXACT)
		return -EINVAL;

	i = find_empty_hw_brk(&child->thread);
	if (i < 0)
		return -ENOSPC;

	if (!ppc_breakpoint_available())
		return -ENODEV;

	child->thread.hw_brk[i] = brk;

	return i + 1;
}

long ppc_del_hwdebug(struct task_struct *child, long data)
{
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	int ret = 0;
	struct thread_struct *thread = &child->thread;
	struct perf_event *bp;
#endif /* CONFIG_HAVE_HW_BREAKPOINT */
	if (data < 1 || data > nr_wp_slots())
		return -EINVAL;

#ifdef CONFIG_HAVE_HW_BREAKPOINT
	bp = thread->ptrace_bps[data - 1];
	if (bp) {
		unregister_hw_breakpoint(bp);
		thread->ptrace_bps[data - 1] = NULL;
	} else {
		ret = -ENOENT;
	}
	return ret;
#else /* CONFIG_HAVE_HW_BREAKPOINT */
	if (child->thread.hw_brk[data - 1].address == 0)
		return -ENOENT;

	child->thread.hw_brk[data - 1].address = 0;
	child->thread.hw_brk[data - 1].type = 0;
#endif /* CONFIG_HAVE_HW_BREAKPOINT */

	return 0;
}
