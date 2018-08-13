/*
 * Xtensa hardware breakpoints/watchpoints handling functions
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2016 Cadence Design Systems Inc.
 */

#include <linux/hw_breakpoint.h>
#include <linux/log2.h>
#include <linux/percpu.h>
#include <linux/perf_event.h>
#include <variant/core.h>

/* Breakpoint currently in use for each IBREAKA. */
static DEFINE_PER_CPU(struct perf_event *, bp_on_reg[XCHAL_NUM_IBREAK]);

/* Watchpoint currently in use for each DBREAKA. */
static DEFINE_PER_CPU(struct perf_event *, wp_on_reg[XCHAL_NUM_DBREAK]);

int hw_breakpoint_slots(int type)
{
	switch (type) {
	case TYPE_INST:
		return XCHAL_NUM_IBREAK;
	case TYPE_DATA:
		return XCHAL_NUM_DBREAK;
	default:
		pr_warn("unknown slot type: %d\n", type);
		return 0;
	}
}

int arch_check_bp_in_kernelspace(struct arch_hw_breakpoint *hw)
{
	unsigned int len;
	unsigned long va;

	va = hw->address;
	len = hw->len;

	return (va >= TASK_SIZE) && ((va + len - 1) >= TASK_SIZE);
}

/*
 * Construct an arch_hw_breakpoint from a perf_event.
 */
int hw_breakpoint_arch_parse(struct perf_event *bp,
			     const struct perf_event_attr *attr,
			     struct arch_hw_breakpoint *hw)
{
	/* Type */
	switch (attr->bp_type) {
	case HW_BREAKPOINT_X:
		hw->type = XTENSA_BREAKPOINT_EXECUTE;
		break;
	case HW_BREAKPOINT_R:
		hw->type = XTENSA_BREAKPOINT_LOAD;
		break;
	case HW_BREAKPOINT_W:
		hw->type = XTENSA_BREAKPOINT_STORE;
		break;
	case HW_BREAKPOINT_RW:
		hw->type = XTENSA_BREAKPOINT_LOAD | XTENSA_BREAKPOINT_STORE;
		break;
	default:
		return -EINVAL;
	}

	/* Len */
	hw->len = attr->bp_len;
	if (hw->len < 1 || hw->len > 64 || !is_power_of_2(hw->len))
		return -EINVAL;

	/* Address */
	hw->address = attr->bp_addr;
	if (hw->address & (hw->len - 1))
		return -EINVAL;

	return 0;
}

int hw_breakpoint_exceptions_notify(struct notifier_block *unused,
				    unsigned long val, void *data)
{
	return NOTIFY_DONE;
}

static void xtensa_wsr(unsigned long v, u8 sr)
{
	/* We don't have indexed wsr and creating instruction dynamically
	 * doesn't seem worth it given how small XCHAL_NUM_IBREAK and
	 * XCHAL_NUM_DBREAK are. Thus the switch. In case build breaks here
	 * the switch below needs to be extended.
	 */
	BUILD_BUG_ON(XCHAL_NUM_IBREAK > 2);
	BUILD_BUG_ON(XCHAL_NUM_DBREAK > 2);

	switch (sr) {
#if XCHAL_NUM_IBREAK > 0
	case SREG_IBREAKA + 0:
		WSR(v, SREG_IBREAKA + 0);
		break;
#endif
#if XCHAL_NUM_IBREAK > 1
	case SREG_IBREAKA + 1:
		WSR(v, SREG_IBREAKA + 1);
		break;
#endif

#if XCHAL_NUM_DBREAK > 0
	case SREG_DBREAKA + 0:
		WSR(v, SREG_DBREAKA + 0);
		break;
	case SREG_DBREAKC + 0:
		WSR(v, SREG_DBREAKC + 0);
		break;
#endif
#if XCHAL_NUM_DBREAK > 1
	case SREG_DBREAKA + 1:
		WSR(v, SREG_DBREAKA + 1);
		break;

	case SREG_DBREAKC + 1:
		WSR(v, SREG_DBREAKC + 1);
		break;
#endif
	}
}

static int alloc_slot(struct perf_event **slot, size_t n,
		      struct perf_event *bp)
{
	size_t i;

	for (i = 0; i < n; ++i) {
		if (!slot[i]) {
			slot[i] = bp;
			return i;
		}
	}
	return -EBUSY;
}

static void set_ibreak_regs(int reg, struct perf_event *bp)
{
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);
	unsigned long ibreakenable;

	xtensa_wsr(info->address, SREG_IBREAKA + reg);
	RSR(ibreakenable, SREG_IBREAKENABLE);
	WSR(ibreakenable | (1 << reg), SREG_IBREAKENABLE);
}

static void set_dbreak_regs(int reg, struct perf_event *bp)
{
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);
	unsigned long dbreakc = DBREAKC_MASK_MASK & -info->len;

	if (info->type & XTENSA_BREAKPOINT_LOAD)
		dbreakc |= DBREAKC_LOAD_MASK;
	if (info->type & XTENSA_BREAKPOINT_STORE)
		dbreakc |= DBREAKC_STOR_MASK;

	xtensa_wsr(info->address, SREG_DBREAKA + reg);
	xtensa_wsr(dbreakc, SREG_DBREAKC + reg);
}

int arch_install_hw_breakpoint(struct perf_event *bp)
{
	int i;

	if (counter_arch_bp(bp)->type == XTENSA_BREAKPOINT_EXECUTE) {
		/* Breakpoint */
		i = alloc_slot(this_cpu_ptr(bp_on_reg), XCHAL_NUM_IBREAK, bp);
		if (i < 0)
			return i;
		set_ibreak_regs(i, bp);

	} else {
		/* Watchpoint */
		i = alloc_slot(this_cpu_ptr(wp_on_reg), XCHAL_NUM_DBREAK, bp);
		if (i < 0)
			return i;
		set_dbreak_regs(i, bp);
	}
	return 0;
}

static int free_slot(struct perf_event **slot, size_t n,
		     struct perf_event *bp)
{
	size_t i;

	for (i = 0; i < n; ++i) {
		if (slot[i] == bp) {
			slot[i] = NULL;
			return i;
		}
	}
	return -EBUSY;
}

void arch_uninstall_hw_breakpoint(struct perf_event *bp)
{
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);
	int i;

	if (info->type == XTENSA_BREAKPOINT_EXECUTE) {
		unsigned long ibreakenable;

		/* Breakpoint */
		i = free_slot(this_cpu_ptr(bp_on_reg), XCHAL_NUM_IBREAK, bp);
		if (i >= 0) {
			RSR(ibreakenable, SREG_IBREAKENABLE);
			WSR(ibreakenable & ~(1 << i), SREG_IBREAKENABLE);
		}
	} else {
		/* Watchpoint */
		i = free_slot(this_cpu_ptr(wp_on_reg), XCHAL_NUM_DBREAK, bp);
		if (i >= 0)
			xtensa_wsr(0, SREG_DBREAKC + i);
	}
}

void hw_breakpoint_pmu_read(struct perf_event *bp)
{
}

void flush_ptrace_hw_breakpoint(struct task_struct *tsk)
{
	int i;
	struct thread_struct *t = &tsk->thread;

	for (i = 0; i < XCHAL_NUM_IBREAK; ++i) {
		if (t->ptrace_bp[i]) {
			unregister_hw_breakpoint(t->ptrace_bp[i]);
			t->ptrace_bp[i] = NULL;
		}
	}
	for (i = 0; i < XCHAL_NUM_DBREAK; ++i) {
		if (t->ptrace_wp[i]) {
			unregister_hw_breakpoint(t->ptrace_wp[i]);
			t->ptrace_wp[i] = NULL;
		}
	}
}

/*
 * Set ptrace breakpoint pointers to zero for this task.
 * This is required in order to prevent child processes from unregistering
 * breakpoints held by their parent.
 */
void clear_ptrace_hw_breakpoint(struct task_struct *tsk)
{
	memset(tsk->thread.ptrace_bp, 0, sizeof(tsk->thread.ptrace_bp));
	memset(tsk->thread.ptrace_wp, 0, sizeof(tsk->thread.ptrace_wp));
}

void restore_dbreak(void)
{
	int i;

	for (i = 0; i < XCHAL_NUM_DBREAK; ++i) {
		struct perf_event *bp = this_cpu_ptr(wp_on_reg)[i];

		if (bp)
			set_dbreak_regs(i, bp);
	}
	clear_thread_flag(TIF_DB_DISABLED);
}

int check_hw_breakpoint(struct pt_regs *regs)
{
	if (regs->debugcause & BIT(DEBUGCAUSE_IBREAK_BIT)) {
		int i;
		struct perf_event **bp = this_cpu_ptr(bp_on_reg);

		for (i = 0; i < XCHAL_NUM_IBREAK; ++i) {
			if (bp[i] && !bp[i]->attr.disabled &&
			    regs->pc == bp[i]->attr.bp_addr)
				perf_bp_event(bp[i], regs);
		}
		return 0;
	} else if (regs->debugcause & BIT(DEBUGCAUSE_DBREAK_BIT)) {
		struct perf_event **bp = this_cpu_ptr(wp_on_reg);
		int dbnum = (regs->debugcause & DEBUGCAUSE_DBNUM_MASK) >>
			DEBUGCAUSE_DBNUM_SHIFT;

		if (dbnum < XCHAL_NUM_DBREAK && bp[dbnum]) {
			if (user_mode(regs)) {
				perf_bp_event(bp[dbnum], regs);
			} else {
				set_thread_flag(TIF_DB_DISABLED);
				xtensa_wsr(0, SREG_DBREAKC + dbnum);
			}
		} else {
			WARN_ONCE(1,
				  "Wrong/unconfigured DBNUM reported in DEBUGCAUSE: %d\n",
				  dbnum);
		}
		return 0;
	}
	return -ENOENT;
}
