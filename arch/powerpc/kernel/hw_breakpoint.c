// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HW_breakpoint: a unified kernel/user-space hardware breakpoint facility,
 * using the CPU's debug registers. Derived from
 * "arch/x86/kernel/hw_breakpoint.c"
 *
 * Copyright 2010 IBM Corporation
 * Author: K.Prasad <prasad@linux.vnet.ibm.com>
 */

#include <linux/hw_breakpoint.h>
#include <linux/notifier.h>
#include <linux/kprobes.h>
#include <linux/percpu.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/debugfs.h>
#include <linux/init.h>

#include <asm/hw_breakpoint.h>
#include <asm/processor.h>
#include <asm/sstep.h>
#include <asm/debug.h>
#include <asm/debugfs.h>
#include <asm/hvcall.h>
#include <asm/inst.h>
#include <linux/uaccess.h>

/*
 * Stores the breakpoints currently in use on each breakpoint address
 * register for every cpu
 */
static DEFINE_PER_CPU(struct perf_event *, bp_per_reg[HBP_NUM_MAX]);

/*
 * Returns total number of data or instruction breakpoints available.
 */
int hw_breakpoint_slots(int type)
{
	if (type == TYPE_DATA)
		return nr_wp_slots();
	return 0;		/* no instruction breakpoints available */
}

static bool single_step_pending(void)
{
	int i;

	for (i = 0; i < nr_wp_slots(); i++) {
		if (current->thread.last_hit_ubp[i])
			return true;
	}
	return false;
}

/*
 * Install a perf counter breakpoint.
 *
 * We seek a free debug address register and use it for this
 * breakpoint.
 *
 * Atomic: we hold the counter->ctx->lock and we only handle variables
 * and registers local to this cpu.
 */
int arch_install_hw_breakpoint(struct perf_event *bp)
{
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);
	struct perf_event **slot;
	int i;

	for (i = 0; i < nr_wp_slots(); i++) {
		slot = this_cpu_ptr(&bp_per_reg[i]);
		if (!*slot) {
			*slot = bp;
			break;
		}
	}

	if (WARN_ONCE(i == nr_wp_slots(), "Can't find any breakpoint slot"))
		return -EBUSY;

	/*
	 * Do not install DABR values if the instruction must be single-stepped.
	 * If so, DABR will be populated in single_step_dabr_instruction().
	 */
	if (!single_step_pending())
		__set_breakpoint(i, info);

	return 0;
}

/*
 * Uninstall the breakpoint contained in the given counter.
 *
 * First we search the debug address register it uses and then we disable
 * it.
 *
 * Atomic: we hold the counter->ctx->lock and we only handle variables
 * and registers local to this cpu.
 */
void arch_uninstall_hw_breakpoint(struct perf_event *bp)
{
	struct arch_hw_breakpoint null_brk = {0};
	struct perf_event **slot;
	int i;

	for (i = 0; i < nr_wp_slots(); i++) {
		slot = this_cpu_ptr(&bp_per_reg[i]);
		if (*slot == bp) {
			*slot = NULL;
			break;
		}
	}

	if (WARN_ONCE(i == nr_wp_slots(), "Can't find any breakpoint slot"))
		return;

	__set_breakpoint(i, &null_brk);
}

static bool is_ptrace_bp(struct perf_event *bp)
{
	return bp->overflow_handler == ptrace_triggered;
}

struct breakpoint {
	struct list_head list;
	struct perf_event *bp;
	bool ptrace_bp;
};

static DEFINE_PER_CPU(struct breakpoint *, cpu_bps[HBP_NUM_MAX]);
static LIST_HEAD(task_bps);

static struct breakpoint *alloc_breakpoint(struct perf_event *bp)
{
	struct breakpoint *tmp;

	tmp = kzalloc(sizeof(*tmp), GFP_KERNEL);
	if (!tmp)
		return ERR_PTR(-ENOMEM);
	tmp->bp = bp;
	tmp->ptrace_bp = is_ptrace_bp(bp);
	return tmp;
}

static bool bp_addr_range_overlap(struct perf_event *bp1, struct perf_event *bp2)
{
	__u64 bp1_saddr, bp1_eaddr, bp2_saddr, bp2_eaddr;

	bp1_saddr = ALIGN_DOWN(bp1->attr.bp_addr, HW_BREAKPOINT_SIZE);
	bp1_eaddr = ALIGN(bp1->attr.bp_addr + bp1->attr.bp_len, HW_BREAKPOINT_SIZE);
	bp2_saddr = ALIGN_DOWN(bp2->attr.bp_addr, HW_BREAKPOINT_SIZE);
	bp2_eaddr = ALIGN(bp2->attr.bp_addr + bp2->attr.bp_len, HW_BREAKPOINT_SIZE);

	return (bp1_saddr < bp2_eaddr && bp1_eaddr > bp2_saddr);
}

static bool alternate_infra_bp(struct breakpoint *b, struct perf_event *bp)
{
	return is_ptrace_bp(bp) ? !b->ptrace_bp : b->ptrace_bp;
}

static bool can_co_exist(struct breakpoint *b, struct perf_event *bp)
{
	return !(alternate_infra_bp(b, bp) && bp_addr_range_overlap(b->bp, bp));
}

static int task_bps_add(struct perf_event *bp)
{
	struct breakpoint *tmp;

	tmp = alloc_breakpoint(bp);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);

	list_add(&tmp->list, &task_bps);
	return 0;
}

static void task_bps_remove(struct perf_event *bp)
{
	struct list_head *pos, *q;

	list_for_each_safe(pos, q, &task_bps) {
		struct breakpoint *tmp = list_entry(pos, struct breakpoint, list);

		if (tmp->bp == bp) {
			list_del(&tmp->list);
			kfree(tmp);
			break;
		}
	}
}

/*
 * If any task has breakpoint from alternate infrastructure,
 * return true. Otherwise return false.
 */
static bool all_task_bps_check(struct perf_event *bp)
{
	struct breakpoint *tmp;

	list_for_each_entry(tmp, &task_bps, list) {
		if (!can_co_exist(tmp, bp))
			return true;
	}
	return false;
}

/*
 * If same task has breakpoint from alternate infrastructure,
 * return true. Otherwise return false.
 */
static bool same_task_bps_check(struct perf_event *bp)
{
	struct breakpoint *tmp;

	list_for_each_entry(tmp, &task_bps, list) {
		if (tmp->bp->hw.target == bp->hw.target &&
		    !can_co_exist(tmp, bp))
			return true;
	}
	return false;
}

static int cpu_bps_add(struct perf_event *bp)
{
	struct breakpoint **cpu_bp;
	struct breakpoint *tmp;
	int i = 0;

	tmp = alloc_breakpoint(bp);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);

	cpu_bp = per_cpu_ptr(cpu_bps, bp->cpu);
	for (i = 0; i < nr_wp_slots(); i++) {
		if (!cpu_bp[i]) {
			cpu_bp[i] = tmp;
			break;
		}
	}
	return 0;
}

static void cpu_bps_remove(struct perf_event *bp)
{
	struct breakpoint **cpu_bp;
	int i = 0;

	cpu_bp = per_cpu_ptr(cpu_bps, bp->cpu);
	for (i = 0; i < nr_wp_slots(); i++) {
		if (!cpu_bp[i])
			continue;

		if (cpu_bp[i]->bp == bp) {
			kfree(cpu_bp[i]);
			cpu_bp[i] = NULL;
			break;
		}
	}
}

static bool cpu_bps_check(int cpu, struct perf_event *bp)
{
	struct breakpoint **cpu_bp;
	int i;

	cpu_bp = per_cpu_ptr(cpu_bps, cpu);
	for (i = 0; i < nr_wp_slots(); i++) {
		if (cpu_bp[i] && !can_co_exist(cpu_bp[i], bp))
			return true;
	}
	return false;
}

static bool all_cpu_bps_check(struct perf_event *bp)
{
	int cpu;

	for_each_online_cpu(cpu) {
		if (cpu_bps_check(cpu, bp))
			return true;
	}
	return false;
}

/*
 * We don't use any locks to serialize accesses to cpu_bps or task_bps
 * because are already inside nr_bp_mutex.
 */
int arch_reserve_bp_slot(struct perf_event *bp)
{
	int ret;

	/* ptrace breakpoint */
	if (is_ptrace_bp(bp)) {
		if (all_cpu_bps_check(bp))
			return -ENOSPC;

		if (same_task_bps_check(bp))
			return -ENOSPC;

		return task_bps_add(bp);
	}

	/* perf breakpoint */
	if (is_kernel_addr(bp->attr.bp_addr))
		return 0;

	if (bp->hw.target && bp->cpu == -1) {
		if (same_task_bps_check(bp))
			return -ENOSPC;

		return task_bps_add(bp);
	} else if (!bp->hw.target && bp->cpu != -1) {
		if (all_task_bps_check(bp))
			return -ENOSPC;

		return cpu_bps_add(bp);
	}

	if (same_task_bps_check(bp))
		return -ENOSPC;

	ret = cpu_bps_add(bp);
	if (ret)
		return ret;
	ret = task_bps_add(bp);
	if (ret)
		cpu_bps_remove(bp);

	return ret;
}

void arch_release_bp_slot(struct perf_event *bp)
{
	if (!is_kernel_addr(bp->attr.bp_addr)) {
		if (bp->hw.target)
			task_bps_remove(bp);
		if (bp->cpu != -1)
			cpu_bps_remove(bp);
	}
}

/*
 * Perform cleanup of arch-specific counters during unregistration
 * of the perf-event
 */
void arch_unregister_hw_breakpoint(struct perf_event *bp)
{
	/*
	 * If the breakpoint is unregistered between a hw_breakpoint_handler()
	 * and the single_step_dabr_instruction(), then cleanup the breakpoint
	 * restoration variables to prevent dangling pointers.
	 * FIXME, this should not be using bp->ctx at all! Sayeth peterz.
	 */
	if (bp->ctx && bp->ctx->task && bp->ctx->task != ((void *)-1L)) {
		int i;

		for (i = 0; i < nr_wp_slots(); i++) {
			if (bp->ctx->task->thread.last_hit_ubp[i] == bp)
				bp->ctx->task->thread.last_hit_ubp[i] = NULL;
		}
	}
}

/*
 * Check for virtual address in kernel space.
 */
int arch_check_bp_in_kernelspace(struct arch_hw_breakpoint *hw)
{
	return is_kernel_addr(hw->address);
}

int arch_bp_generic_fields(int type, int *gen_bp_type)
{
	*gen_bp_type = 0;
	if (type & HW_BRK_TYPE_READ)
		*gen_bp_type |= HW_BREAKPOINT_R;
	if (type & HW_BRK_TYPE_WRITE)
		*gen_bp_type |= HW_BREAKPOINT_W;
	if (*gen_bp_type == 0)
		return -EINVAL;
	return 0;
}

/*
 * Watchpoint match range is always doubleword(8 bytes) aligned on
 * powerpc. If the given range is crossing doubleword boundary, we
 * need to increase the length such that next doubleword also get
 * covered. Ex,
 *
 *          address   len = 6 bytes
 *                |=========.
 *   |------------v--|------v--------|
 *   | | | | | | | | | | | | | | | | |
 *   |---------------|---------------|
 *    <---8 bytes--->
 *
 * In this case, we should configure hw as:
 *   start_addr = address & ~(HW_BREAKPOINT_SIZE - 1)
 *   len = 16 bytes
 *
 * @start_addr is inclusive but @end_addr is exclusive.
 */
static int hw_breakpoint_validate_len(struct arch_hw_breakpoint *hw)
{
	u16 max_len = DABR_MAX_LEN;
	u16 hw_len;
	unsigned long start_addr, end_addr;

	start_addr = ALIGN_DOWN(hw->address, HW_BREAKPOINT_SIZE);
	end_addr = ALIGN(hw->address + hw->len, HW_BREAKPOINT_SIZE);
	hw_len = end_addr - start_addr;

	if (dawr_enabled()) {
		max_len = DAWR_MAX_LEN;
		/* DAWR region can't cross 512 bytes boundary on p10 predecessors */
		if (!cpu_has_feature(CPU_FTR_ARCH_31) &&
		    (ALIGN_DOWN(start_addr, SZ_512) != ALIGN_DOWN(end_addr - 1, SZ_512)))
			return -EINVAL;
	} else if (IS_ENABLED(CONFIG_PPC_8xx)) {
		/* 8xx can setup a range without limitation */
		max_len = U16_MAX;
	}

	if (hw_len > max_len)
		return -EINVAL;

	hw->hw_len = hw_len;
	return 0;
}

/*
 * Validate the arch-specific HW Breakpoint register settings
 */
int hw_breakpoint_arch_parse(struct perf_event *bp,
			     const struct perf_event_attr *attr,
			     struct arch_hw_breakpoint *hw)
{
	int ret = -EINVAL;

	if (!bp || !attr->bp_len)
		return ret;

	hw->type = HW_BRK_TYPE_TRANSLATE;
	if (attr->bp_type & HW_BREAKPOINT_R)
		hw->type |= HW_BRK_TYPE_READ;
	if (attr->bp_type & HW_BREAKPOINT_W)
		hw->type |= HW_BRK_TYPE_WRITE;
	if (hw->type == HW_BRK_TYPE_TRANSLATE)
		/* must set alteast read or write */
		return ret;
	if (!attr->exclude_user)
		hw->type |= HW_BRK_TYPE_USER;
	if (!attr->exclude_kernel)
		hw->type |= HW_BRK_TYPE_KERNEL;
	if (!attr->exclude_hv)
		hw->type |= HW_BRK_TYPE_HYP;
	hw->address = attr->bp_addr;
	hw->len = attr->bp_len;

	if (!ppc_breakpoint_available())
		return -ENODEV;

	return hw_breakpoint_validate_len(hw);
}

/*
 * Restores the breakpoint on the debug registers.
 * Invoke this function if it is known that the execution context is
 * about to change to cause loss of MSR_SE settings.
 */
void thread_change_pc(struct task_struct *tsk, struct pt_regs *regs)
{
	struct arch_hw_breakpoint *info;
	int i;

	preempt_disable();

	for (i = 0; i < nr_wp_slots(); i++) {
		if (unlikely(tsk->thread.last_hit_ubp[i]))
			goto reset;
	}
	goto out;

reset:
	regs->msr &= ~MSR_SE;
	for (i = 0; i < nr_wp_slots(); i++) {
		info = counter_arch_bp(__this_cpu_read(bp_per_reg[i]));
		__set_breakpoint(i, info);
		tsk->thread.last_hit_ubp[i] = NULL;
	}

out:
	preempt_enable();
}

static bool is_larx_stcx_instr(int type)
{
	return type == LARX || type == STCX;
}

/*
 * We've failed in reliably handling the hw-breakpoint. Unregister
 * it and throw a warning message to let the user know about it.
 */
static void handler_error(struct perf_event *bp, struct arch_hw_breakpoint *info)
{
	WARN(1, "Unable to handle hardware breakpoint. Breakpoint at 0x%lx will be disabled.",
	     info->address);
	perf_event_disable_inatomic(bp);
}

static void larx_stcx_err(struct perf_event *bp, struct arch_hw_breakpoint *info)
{
	printk_ratelimited("Breakpoint hit on instruction that can't be emulated. Breakpoint at 0x%lx will be disabled.\n",
			   info->address);
	perf_event_disable_inatomic(bp);
}

static bool stepping_handler(struct pt_regs *regs, struct perf_event **bp,
			     struct arch_hw_breakpoint **info, int *hit,
			     struct ppc_inst instr)
{
	int i;
	int stepped;

	/* Do not emulate user-space instructions, instead single-step them */
	if (user_mode(regs)) {
		for (i = 0; i < nr_wp_slots(); i++) {
			if (!hit[i])
				continue;
			current->thread.last_hit_ubp[i] = bp[i];
			info[i] = NULL;
		}
		regs->msr |= MSR_SE;
		return false;
	}

	stepped = emulate_step(regs, instr);
	if (!stepped) {
		for (i = 0; i < nr_wp_slots(); i++) {
			if (!hit[i])
				continue;
			handler_error(bp[i], info[i]);
			info[i] = NULL;
		}
		return false;
	}
	return true;
}

int hw_breakpoint_handler(struct die_args *args)
{
	bool err = false;
	int rc = NOTIFY_STOP;
	struct perf_event *bp[HBP_NUM_MAX] = { NULL };
	struct pt_regs *regs = args->regs;
	struct arch_hw_breakpoint *info[HBP_NUM_MAX] = { NULL };
	int i;
	int hit[HBP_NUM_MAX] = {0};
	int nr_hit = 0;
	bool ptrace_bp = false;
	struct ppc_inst instr = ppc_inst(0);
	int type = 0;
	int size = 0;
	unsigned long ea;

	/* Disable breakpoints during exception handling */
	hw_breakpoint_disable();

	/*
	 * The counter may be concurrently released but that can only
	 * occur from a call_rcu() path. We can then safely fetch
	 * the breakpoint, use its callback, touch its counter
	 * while we are in an rcu_read_lock() path.
	 */
	rcu_read_lock();

	if (!IS_ENABLED(CONFIG_PPC_8xx))
		wp_get_instr_detail(regs, &instr, &type, &size, &ea);

	for (i = 0; i < nr_wp_slots(); i++) {
		bp[i] = __this_cpu_read(bp_per_reg[i]);
		if (!bp[i])
			continue;

		info[i] = counter_arch_bp(bp[i]);
		info[i]->type &= ~HW_BRK_TYPE_EXTRANEOUS_IRQ;

		if (wp_check_constraints(regs, instr, ea, type, size, info[i])) {
			if (!IS_ENABLED(CONFIG_PPC_8xx) &&
			    ppc_inst_equal(instr, ppc_inst(0))) {
				handler_error(bp[i], info[i]);
				info[i] = NULL;
				err = 1;
				continue;
			}

			if (is_ptrace_bp(bp[i]))
				ptrace_bp = true;
			hit[i] = 1;
			nr_hit++;
		}
	}

	if (err)
		goto reset;

	if (!nr_hit) {
		rc = NOTIFY_DONE;
		goto out;
	}

	/*
	 * Return early after invoking user-callback function without restoring
	 * DABR if the breakpoint is from ptrace which always operates in
	 * one-shot mode. The ptrace-ed process will receive the SIGTRAP signal
	 * generated in do_dabr().
	 */
	if (ptrace_bp) {
		for (i = 0; i < nr_wp_slots(); i++) {
			if (!hit[i])
				continue;
			perf_bp_event(bp[i], regs);
			info[i] = NULL;
		}
		rc = NOTIFY_DONE;
		goto reset;
	}

	if (!IS_ENABLED(CONFIG_PPC_8xx)) {
		if (is_larx_stcx_instr(type)) {
			for (i = 0; i < nr_wp_slots(); i++) {
				if (!hit[i])
					continue;
				larx_stcx_err(bp[i], info[i]);
				info[i] = NULL;
			}
			goto reset;
		}

		if (!stepping_handler(regs, bp, info, hit, instr))
			goto reset;
	}

	/*
	 * As a policy, the callback is invoked in a 'trigger-after-execute'
	 * fashion
	 */
	for (i = 0; i < nr_wp_slots(); i++) {
		if (!hit[i])
			continue;
		if (!(info[i]->type & HW_BRK_TYPE_EXTRANEOUS_IRQ))
			perf_bp_event(bp[i], regs);
	}

reset:
	for (i = 0; i < nr_wp_slots(); i++) {
		if (!info[i])
			continue;
		__set_breakpoint(i, info[i]);
	}

out:
	rcu_read_unlock();
	return rc;
}
NOKPROBE_SYMBOL(hw_breakpoint_handler);

/*
 * Handle single-step exceptions following a DABR hit.
 */
static int single_step_dabr_instruction(struct die_args *args)
{
	struct pt_regs *regs = args->regs;
	struct perf_event *bp = NULL;
	struct arch_hw_breakpoint *info;
	int i;
	bool found = false;

	/*
	 * Check if we are single-stepping as a result of a
	 * previous HW Breakpoint exception
	 */
	for (i = 0; i < nr_wp_slots(); i++) {
		bp = current->thread.last_hit_ubp[i];

		if (!bp)
			continue;

		found = true;
		info = counter_arch_bp(bp);

		/*
		 * We shall invoke the user-defined callback function in the
		 * single stepping handler to confirm to 'trigger-after-execute'
		 * semantics
		 */
		if (!(info->type & HW_BRK_TYPE_EXTRANEOUS_IRQ))
			perf_bp_event(bp, regs);
		current->thread.last_hit_ubp[i] = NULL;
	}

	if (!found)
		return NOTIFY_DONE;

	for (i = 0; i < nr_wp_slots(); i++) {
		bp = __this_cpu_read(bp_per_reg[i]);
		if (!bp)
			continue;

		info = counter_arch_bp(bp);
		__set_breakpoint(i, info);
	}

	/*
	 * If the process was being single-stepped by ptrace, let the
	 * other single-step actions occur (e.g. generate SIGTRAP).
	 */
	if (test_thread_flag(TIF_SINGLESTEP))
		return NOTIFY_DONE;

	return NOTIFY_STOP;
}
NOKPROBE_SYMBOL(single_step_dabr_instruction);

/*
 * Handle debug exception notifications.
 */
int hw_breakpoint_exceptions_notify(
		struct notifier_block *unused, unsigned long val, void *data)
{
	int ret = NOTIFY_DONE;

	switch (val) {
	case DIE_DABR_MATCH:
		ret = hw_breakpoint_handler(data);
		break;
	case DIE_SSTEP:
		ret = single_step_dabr_instruction(data);
		break;
	}

	return ret;
}
NOKPROBE_SYMBOL(hw_breakpoint_exceptions_notify);

/*
 * Release the user breakpoints used by ptrace
 */
void flush_ptrace_hw_breakpoint(struct task_struct *tsk)
{
	int i;
	struct thread_struct *t = &tsk->thread;

	for (i = 0; i < nr_wp_slots(); i++) {
		unregister_hw_breakpoint(t->ptrace_bps[i]);
		t->ptrace_bps[i] = NULL;
	}
}

void hw_breakpoint_pmu_read(struct perf_event *bp)
{
	/* TODO */
}

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
