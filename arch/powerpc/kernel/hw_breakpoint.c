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
#include <linux/spinlock.h>
#include <linux/debugfs.h>
#include <linux/init.h>

#include <asm/hw_breakpoint.h>
#include <asm/processor.h>
#include <asm/sstep.h>
#include <asm/debug.h>
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
	if (!info->perf_single_step)
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
 *
 * The perf watchpoint will simply re-trigger once the thread is started again,
 * and the watchpoint handler will set up MSR_SE and perf_single_step as
 * needed.
 */
void thread_change_pc(struct task_struct *tsk, struct pt_regs *regs)
{
	struct arch_hw_breakpoint *info;
	int i;

	preempt_disable();

	for (i = 0; i < nr_wp_slots(); i++) {
		struct perf_event *bp = __this_cpu_read(bp_per_reg[i]);

		if (unlikely(bp && counter_arch_bp(bp)->perf_single_step))
			goto reset;
	}
	goto out;

reset:
	regs_set_return_msr(regs, regs->msr & ~MSR_SE);
	for (i = 0; i < nr_wp_slots(); i++) {
		info = counter_arch_bp(__this_cpu_read(bp_per_reg[i]));
		__set_breakpoint(i, info);
		info->perf_single_step = false;
	}

out:
	preempt_enable();
}

static bool is_larx_stcx_instr(int type)
{
	return type == LARX || type == STCX;
}

static bool is_octword_vsx_instr(int type, int size)
{
	return ((type == LOAD_VSX || type == STORE_VSX) && size == 32);
}

/*
 * We've failed in reliably handling the hw-breakpoint. Unregister
 * it and throw a warning message to let the user know about it.
 */
static void handler_error(struct perf_event *bp)
{
	WARN(1, "Unable to handle hardware breakpoint. Breakpoint at 0x%lx will be disabled.",
	     counter_arch_bp(bp)->address);
	perf_event_disable_inatomic(bp);
}

static void larx_stcx_err(struct perf_event *bp)
{
	printk_ratelimited("Breakpoint hit on instruction that can't be emulated. Breakpoint at 0x%lx will be disabled.\n",
			   counter_arch_bp(bp)->address);
	perf_event_disable_inatomic(bp);
}

static bool stepping_handler(struct pt_regs *regs, struct perf_event **bp,
			     int *hit, ppc_inst_t instr)
{
	int i;
	int stepped;

	/* Do not emulate user-space instructions, instead single-step them */
	if (user_mode(regs)) {
		for (i = 0; i < nr_wp_slots(); i++) {
			if (!hit[i])
				continue;

			counter_arch_bp(bp[i])->perf_single_step = true;
			bp[i] = NULL;
		}
		regs_set_return_msr(regs, regs->msr | MSR_SE);
		return false;
	}

	stepped = emulate_step(regs, instr);
	if (!stepped) {
		for (i = 0; i < nr_wp_slots(); i++) {
			if (!hit[i])
				continue;
			handler_error(bp[i]);
			bp[i] = NULL;
		}
		return false;
	}
	return true;
}

static void handle_p10dd1_spurious_exception(struct perf_event **bp,
					     int *hit, unsigned long ea)
{
	int i;
	unsigned long hw_end_addr;

	/*
	 * Handle spurious exception only when any bp_per_reg is set.
	 * Otherwise this might be created by xmon and not actually a
	 * spurious exception.
	 */
	for (i = 0; i < nr_wp_slots(); i++) {
		struct arch_hw_breakpoint *info;

		if (!bp[i])
			continue;

		info = counter_arch_bp(bp[i]);

		hw_end_addr = ALIGN(info->address + info->len, HW_BREAKPOINT_SIZE);

		/*
		 * Ending address of DAWR range is less than starting
		 * address of op.
		 */
		if ((hw_end_addr - 1) >= ea)
			continue;

		/*
		 * Those addresses need to be in the same or in two
		 * consecutive 512B blocks;
		 */
		if (((hw_end_addr - 1) >> 10) != (ea >> 10))
			continue;

		/*
		 * 'op address + 64B' generates an address that has a
		 * carry into bit 52 (crosses 2K boundary).
		 */
		if ((ea & 0x800) == ((ea + 64) & 0x800))
			continue;

		break;
	}

	if (i == nr_wp_slots())
		return;

	for (i = 0; i < nr_wp_slots(); i++) {
		if (bp[i]) {
			hit[i] = 1;
			counter_arch_bp(bp[i])->type |= HW_BRK_TYPE_EXTRANEOUS_IRQ;
		}
	}
}

/*
 * Handle a DABR or DAWR exception.
 *
 * Called in atomic context.
 */
int hw_breakpoint_handler(struct die_args *args)
{
	bool err = false;
	int rc = NOTIFY_STOP;
	struct perf_event *bp[HBP_NUM_MAX] = { NULL };
	struct pt_regs *regs = args->regs;
	int i;
	int hit[HBP_NUM_MAX] = {0};
	int nr_hit = 0;
	bool ptrace_bp = false;
	ppc_inst_t instr = ppc_inst(0);
	int type = 0;
	int size = 0;
	unsigned long ea = 0;

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
		struct arch_hw_breakpoint *info;

		bp[i] = __this_cpu_read(bp_per_reg[i]);
		if (!bp[i])
			continue;

		info = counter_arch_bp(bp[i]);
		info->type &= ~HW_BRK_TYPE_EXTRANEOUS_IRQ;

		if (wp_check_constraints(regs, instr, ea, type, size, info)) {
			if (!IS_ENABLED(CONFIG_PPC_8xx) &&
			    ppc_inst_equal(instr, ppc_inst(0))) {
				handler_error(bp[i]);
				bp[i] = NULL;
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
		/* Workaround for Power10 DD1 */
		if (!IS_ENABLED(CONFIG_PPC_8xx) && mfspr(SPRN_PVR) == 0x800100 &&
		    is_octword_vsx_instr(type, size)) {
			handle_p10dd1_spurious_exception(bp, hit, ea);
		} else {
			rc = NOTIFY_DONE;
			goto out;
		}
	}

	/*
	 * Return early after invoking user-callback function without restoring
	 * DABR if the breakpoint is from ptrace which always operates in
	 * one-shot mode. The ptrace-ed process will receive the SIGTRAP signal
	 * generated in do_dabr().
	 */
	if (ptrace_bp) {
		for (i = 0; i < nr_wp_slots(); i++) {
			if (!hit[i] || !is_ptrace_bp(bp[i]))
				continue;
			perf_bp_event(bp[i], regs);
			bp[i] = NULL;
		}
		rc = NOTIFY_DONE;
		goto reset;
	}

	if (!IS_ENABLED(CONFIG_PPC_8xx)) {
		if (is_larx_stcx_instr(type)) {
			for (i = 0; i < nr_wp_slots(); i++) {
				if (!hit[i])
					continue;
				larx_stcx_err(bp[i]);
				bp[i] = NULL;
			}
			goto reset;
		}

		if (!stepping_handler(regs, bp, hit, instr))
			goto reset;
	}

	/*
	 * As a policy, the callback is invoked in a 'trigger-after-execute'
	 * fashion
	 */
	for (i = 0; i < nr_wp_slots(); i++) {
		if (!hit[i])
			continue;
		if (!(counter_arch_bp(bp[i])->type & HW_BRK_TYPE_EXTRANEOUS_IRQ))
			perf_bp_event(bp[i], regs);
	}

reset:
	for (i = 0; i < nr_wp_slots(); i++) {
		if (!bp[i])
			continue;
		__set_breakpoint(i, counter_arch_bp(bp[i]));
	}

out:
	rcu_read_unlock();
	return rc;
}
NOKPROBE_SYMBOL(hw_breakpoint_handler);

/*
 * Handle single-step exceptions following a DABR hit.
 *
 * Called in atomic context.
 */
static int single_step_dabr_instruction(struct die_args *args)
{
	struct pt_regs *regs = args->regs;
	bool found = false;

	/*
	 * Check if we are single-stepping as a result of a
	 * previous HW Breakpoint exception
	 */
	for (int i = 0; i < nr_wp_slots(); i++) {
		struct perf_event *bp;
		struct arch_hw_breakpoint *info;

		bp = __this_cpu_read(bp_per_reg[i]);

		if (!bp)
			continue;

		info = counter_arch_bp(bp);

		if (!info->perf_single_step)
			continue;

		found = true;

		/*
		 * We shall invoke the user-defined callback function in the
		 * single stepping handler to confirm to 'trigger-after-execute'
		 * semantics
		 */
		if (!(info->type & HW_BRK_TYPE_EXTRANEOUS_IRQ))
			perf_bp_event(bp, regs);

		info->perf_single_step = false;
		__set_breakpoint(i, counter_arch_bp(bp));
	}

	/*
	 * If the process was being single-stepped by ptrace, let the
	 * other single-step actions occur (e.g. generate SIGTRAP).
	 */
	if (!found || test_thread_flag(TIF_SINGLESTEP))
		return NOTIFY_DONE;

	return NOTIFY_STOP;
}
NOKPROBE_SYMBOL(single_step_dabr_instruction);

/*
 * Handle debug exception notifications.
 *
 * Called in atomic context.
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
