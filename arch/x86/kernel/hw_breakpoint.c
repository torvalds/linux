/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) 2007 Alan Stern
 * Copyright (C) 2009 IBM Corporation
 * Copyright (C) 2009 Frederic Weisbecker <fweisbec@gmail.com>
 *
 * Authors: Alan Stern <stern@rowland.harvard.edu>
 *          K.Prasad <prasad@linux.vnet.ibm.com>
 *          Frederic Weisbecker <fweisbec@gmail.com>
 */

/*
 * HW_breakpoint: a unified kernel/user-space hardware breakpoint facility,
 * using the CPU's debug registers.
 */

#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <linux/irqflags.h>
#include <linux/notifier.h>
#include <linux/kallsyms.h>
#include <linux/kprobes.h>
#include <linux/percpu.h>
#include <linux/kdebug.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/smp.h>

#include <asm/hw_breakpoint.h>
#include <asm/processor.h>
#include <asm/debugreg.h>
#include <asm/user.h>

/* Per cpu debug control register value */
DEFINE_PER_CPU(unsigned long, cpu_dr7);
EXPORT_PER_CPU_SYMBOL(cpu_dr7);

/* Per cpu debug address registers values */
static DEFINE_PER_CPU(unsigned long, cpu_debugreg[HBP_NUM]);

/*
 * Stores the breakpoints currently in use on each breakpoint address
 * register for each cpus
 */
static DEFINE_PER_CPU(struct perf_event *, bp_per_reg[HBP_NUM]);


static inline unsigned long
__encode_dr7(int drnum, unsigned int len, unsigned int type)
{
	unsigned long bp_info;

	bp_info = (len | type) & 0xf;
	bp_info <<= (DR_CONTROL_SHIFT + drnum * DR_CONTROL_SIZE);
	bp_info |= (DR_GLOBAL_ENABLE << (drnum * DR_ENABLE_SIZE));

	return bp_info;
}

/*
 * Encode the length, type, Exact, and Enable bits for a particular breakpoint
 * as stored in debug register 7.
 */
unsigned long encode_dr7(int drnum, unsigned int len, unsigned int type)
{
	return __encode_dr7(drnum, len, type) | DR_GLOBAL_SLOWDOWN;
}

/*
 * Decode the length and type bits for a particular breakpoint as
 * stored in debug register 7.  Return the "enabled" status.
 */
int decode_dr7(unsigned long dr7, int bpnum, unsigned *len, unsigned *type)
{
	int bp_info = dr7 >> (DR_CONTROL_SHIFT + bpnum * DR_CONTROL_SIZE);

	*len = (bp_info & 0xc) | 0x40;
	*type = (bp_info & 0x3) | 0x80;

	return (dr7 >> (bpnum * DR_ENABLE_SIZE)) & 0x3;
}

/*
 * Install a perf counter breakpoint.
 *
 * We seek a free debug address register and use it for this
 * breakpoint. Eventually we enable it in the debug control register.
 *
 * Atomic: we hold the counter->ctx->lock and we only handle variables
 * and registers local to this cpu.
 */
int arch_install_hw_breakpoint(struct perf_event *bp)
{
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);
	unsigned long *dr7;
	int i;

	for (i = 0; i < HBP_NUM; i++) {
		struct perf_event **slot = this_cpu_ptr(&bp_per_reg[i]);

		if (!*slot) {
			*slot = bp;
			break;
		}
	}

	if (WARN_ONCE(i == HBP_NUM, "Can't find any breakpoint slot"))
		return -EBUSY;

	set_debugreg(info->address, i);
	__this_cpu_write(cpu_debugreg[i], info->address);

	dr7 = this_cpu_ptr(&cpu_dr7);
	*dr7 |= encode_dr7(i, info->len, info->type);

	set_debugreg(*dr7, 7);
	if (info->mask)
		set_dr_addr_mask(info->mask, i);

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
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);
	unsigned long *dr7;
	int i;

	for (i = 0; i < HBP_NUM; i++) {
		struct perf_event **slot = this_cpu_ptr(&bp_per_reg[i]);

		if (*slot == bp) {
			*slot = NULL;
			break;
		}
	}

	if (WARN_ONCE(i == HBP_NUM, "Can't find any breakpoint slot"))
		return;

	dr7 = this_cpu_ptr(&cpu_dr7);
	*dr7 &= ~__encode_dr7(i, info->len, info->type);

	set_debugreg(*dr7, 7);
	if (info->mask)
		set_dr_addr_mask(0, i);
}

/*
 * Check for virtual address in kernel space.
 */
int arch_check_bp_in_kernelspace(struct perf_event *bp)
{
	unsigned int len;
	unsigned long va;
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);

	va = info->address;
	len = bp->attr.bp_len;

	/*
	 * We don't need to worry about va + len - 1 overflowing:
	 * we already require that va is aligned to a multiple of len.
	 */
	return (va >= TASK_SIZE_MAX) || ((va + len - 1) >= TASK_SIZE_MAX);
}

int arch_bp_generic_fields(int x86_len, int x86_type,
			   int *gen_len, int *gen_type)
{
	/* Type */
	switch (x86_type) {
	case X86_BREAKPOINT_EXECUTE:
		if (x86_len != X86_BREAKPOINT_LEN_X)
			return -EINVAL;

		*gen_type = HW_BREAKPOINT_X;
		*gen_len = sizeof(long);
		return 0;
	case X86_BREAKPOINT_WRITE:
		*gen_type = HW_BREAKPOINT_W;
		break;
	case X86_BREAKPOINT_RW:
		*gen_type = HW_BREAKPOINT_W | HW_BREAKPOINT_R;
		break;
	default:
		return -EINVAL;
	}

	/* Len */
	switch (x86_len) {
	case X86_BREAKPOINT_LEN_1:
		*gen_len = HW_BREAKPOINT_LEN_1;
		break;
	case X86_BREAKPOINT_LEN_2:
		*gen_len = HW_BREAKPOINT_LEN_2;
		break;
	case X86_BREAKPOINT_LEN_4:
		*gen_len = HW_BREAKPOINT_LEN_4;
		break;
#ifdef CONFIG_X86_64
	case X86_BREAKPOINT_LEN_8:
		*gen_len = HW_BREAKPOINT_LEN_8;
		break;
#endif
	default:
		return -EINVAL;
	}

	return 0;
}


static int arch_build_bp_info(struct perf_event *bp)
{
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);

	info->address = bp->attr.bp_addr;

	/* Type */
	switch (bp->attr.bp_type) {
	case HW_BREAKPOINT_W:
		info->type = X86_BREAKPOINT_WRITE;
		break;
	case HW_BREAKPOINT_W | HW_BREAKPOINT_R:
		info->type = X86_BREAKPOINT_RW;
		break;
	case HW_BREAKPOINT_X:
		/*
		 * We don't allow kernel breakpoints in places that are not
		 * acceptable for kprobes.  On non-kprobes kernels, we don't
		 * allow kernel breakpoints at all.
		 */
		if (bp->attr.bp_addr >= TASK_SIZE_MAX) {
#ifdef CONFIG_KPROBES
			if (within_kprobe_blacklist(bp->attr.bp_addr))
				return -EINVAL;
#else
			return -EINVAL;
#endif
		}

		info->type = X86_BREAKPOINT_EXECUTE;
		/*
		 * x86 inst breakpoints need to have a specific undefined len.
		 * But we still need to check userspace is not trying to setup
		 * an unsupported length, to get a range breakpoint for example.
		 */
		if (bp->attr.bp_len == sizeof(long)) {
			info->len = X86_BREAKPOINT_LEN_X;
			return 0;
		}
	default:
		return -EINVAL;
	}

	/* Len */
	info->mask = 0;

	switch (bp->attr.bp_len) {
	case HW_BREAKPOINT_LEN_1:
		info->len = X86_BREAKPOINT_LEN_1;
		break;
	case HW_BREAKPOINT_LEN_2:
		info->len = X86_BREAKPOINT_LEN_2;
		break;
	case HW_BREAKPOINT_LEN_4:
		info->len = X86_BREAKPOINT_LEN_4;
		break;
#ifdef CONFIG_X86_64
	case HW_BREAKPOINT_LEN_8:
		info->len = X86_BREAKPOINT_LEN_8;
		break;
#endif
	default:
		/* AMD range breakpoint */
		if (!is_power_of_2(bp->attr.bp_len))
			return -EINVAL;
		if (bp->attr.bp_addr & (bp->attr.bp_len - 1))
			return -EINVAL;

		if (!boot_cpu_has(X86_FEATURE_BPEXT))
			return -EOPNOTSUPP;

		/*
		 * It's impossible to use a range breakpoint to fake out
		 * user vs kernel detection because bp_len - 1 can't
		 * have the high bit set.  If we ever allow range instruction
		 * breakpoints, then we'll have to check for kprobe-blacklisted
		 * addresses anywhere in the range.
		 */
		info->mask = bp->attr.bp_len - 1;
		info->len = X86_BREAKPOINT_LEN_1;
	}

	return 0;
}

/*
 * Validate the arch-specific HW Breakpoint register settings
 */
int arch_validate_hwbkpt_settings(struct perf_event *bp)
{
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);
	unsigned int align;
	int ret;


	ret = arch_build_bp_info(bp);
	if (ret)
		return ret;

	switch (info->len) {
	case X86_BREAKPOINT_LEN_1:
		align = 0;
		if (info->mask)
			align = info->mask;
		break;
	case X86_BREAKPOINT_LEN_2:
		align = 1;
		break;
	case X86_BREAKPOINT_LEN_4:
		align = 3;
		break;
#ifdef CONFIG_X86_64
	case X86_BREAKPOINT_LEN_8:
		align = 7;
		break;
#endif
	default:
		WARN_ON_ONCE(1);
	}

	/*
	 * Check that the low-order bits of the address are appropriate
	 * for the alignment implied by len.
	 */
	if (info->address & align)
		return -EINVAL;

	return 0;
}

/*
 * Dump the debug register contents to the user.
 * We can't dump our per cpu values because it
 * may contain cpu wide breakpoint, something that
 * doesn't belong to the current task.
 *
 * TODO: include non-ptrace user breakpoints (perf)
 */
void aout_dump_debugregs(struct user *dump)
{
	int i;
	int dr7 = 0;
	struct perf_event *bp;
	struct arch_hw_breakpoint *info;
	struct thread_struct *thread = &current->thread;

	for (i = 0; i < HBP_NUM; i++) {
		bp = thread->ptrace_bps[i];

		if (bp && !bp->attr.disabled) {
			dump->u_debugreg[i] = bp->attr.bp_addr;
			info = counter_arch_bp(bp);
			dr7 |= encode_dr7(i, info->len, info->type);
		} else {
			dump->u_debugreg[i] = 0;
		}
	}

	dump->u_debugreg[4] = 0;
	dump->u_debugreg[5] = 0;
	dump->u_debugreg[6] = current->thread.debugreg6;

	dump->u_debugreg[7] = dr7;
}
EXPORT_SYMBOL_GPL(aout_dump_debugregs);

/*
 * Release the user breakpoints used by ptrace
 */
void flush_ptrace_hw_breakpoint(struct task_struct *tsk)
{
	int i;
	struct thread_struct *t = &tsk->thread;

	for (i = 0; i < HBP_NUM; i++) {
		unregister_hw_breakpoint(t->ptrace_bps[i]);
		t->ptrace_bps[i] = NULL;
	}

	t->debugreg6 = 0;
	t->ptrace_dr7 = 0;
}

void hw_breakpoint_restore(void)
{
	set_debugreg(__this_cpu_read(cpu_debugreg[0]), 0);
	set_debugreg(__this_cpu_read(cpu_debugreg[1]), 1);
	set_debugreg(__this_cpu_read(cpu_debugreg[2]), 2);
	set_debugreg(__this_cpu_read(cpu_debugreg[3]), 3);
	set_debugreg(current->thread.debugreg6, 6);
	set_debugreg(__this_cpu_read(cpu_dr7), 7);
}
EXPORT_SYMBOL_GPL(hw_breakpoint_restore);

/*
 * Handle debug exception notifications.
 *
 * Return value is either NOTIFY_STOP or NOTIFY_DONE as explained below.
 *
 * NOTIFY_DONE returned if one of the following conditions is true.
 * i) When the causative address is from user-space and the exception
 * is a valid one, i.e. not triggered as a result of lazy debug register
 * switching
 * ii) When there are more bits than trap<n> set in DR6 register (such
 * as BD, BS or BT) indicating that more than one debug condition is
 * met and requires some more action in do_debug().
 *
 * NOTIFY_STOP returned for all other cases
 *
 */
static int hw_breakpoint_handler(struct die_args *args)
{
	int i, cpu, rc = NOTIFY_STOP;
	struct perf_event *bp;
	unsigned long dr7, dr6;
	unsigned long *dr6_p;

	/* The DR6 value is pointed by args->err */
	dr6_p = (unsigned long *)ERR_PTR(args->err);
	dr6 = *dr6_p;

	/* If it's a single step, TRAP bits are random */
	if (dr6 & DR_STEP)
		return NOTIFY_DONE;

	/* Do an early return if no trap bits are set in DR6 */
	if ((dr6 & DR_TRAP_BITS) == 0)
		return NOTIFY_DONE;

	get_debugreg(dr7, 7);
	/* Disable breakpoints during exception handling */
	set_debugreg(0UL, 7);
	/*
	 * Assert that local interrupts are disabled
	 * Reset the DRn bits in the virtualized register value.
	 * The ptrace trigger routine will add in whatever is needed.
	 */
	current->thread.debugreg6 &= ~DR_TRAP_BITS;
	cpu = get_cpu();

	/* Handle all the breakpoints that were triggered */
	for (i = 0; i < HBP_NUM; ++i) {
		if (likely(!(dr6 & (DR_TRAP0 << i))))
			continue;

		/*
		 * The counter may be concurrently released but that can only
		 * occur from a call_rcu() path. We can then safely fetch
		 * the breakpoint, use its callback, touch its counter
		 * while we are in an rcu_read_lock() path.
		 */
		rcu_read_lock();

		bp = per_cpu(bp_per_reg[i], cpu);
		/*
		 * Reset the 'i'th TRAP bit in dr6 to denote completion of
		 * exception handling
		 */
		(*dr6_p) &= ~(DR_TRAP0 << i);
		/*
		 * bp can be NULL due to lazy debug register switching
		 * or due to concurrent perf counter removing.
		 */
		if (!bp) {
			rcu_read_unlock();
			break;
		}

		perf_bp_event(bp, args->regs);

		/*
		 * Set up resume flag to avoid breakpoint recursion when
		 * returning back to origin.
		 */
		if (bp->hw.info.type == X86_BREAKPOINT_EXECUTE)
			args->regs->flags |= X86_EFLAGS_RF;

		rcu_read_unlock();
	}
	/*
	 * Further processing in do_debug() is needed for a) user-space
	 * breakpoints (to generate signals) and b) when the system has
	 * taken exception due to multiple causes
	 */
	if ((current->thread.debugreg6 & DR_TRAP_BITS) ||
	    (dr6 & (~DR_TRAP_BITS)))
		rc = NOTIFY_DONE;

	set_debugreg(dr7, 7);
	put_cpu();

	return rc;
}

/*
 * Handle debug exception notifications.
 */
int hw_breakpoint_exceptions_notify(
		struct notifier_block *unused, unsigned long val, void *data)
{
	if (val != DIE_DEBUG)
		return NOTIFY_DONE;

	return hw_breakpoint_handler(data);
}

void hw_breakpoint_pmu_read(struct perf_event *bp)
{
	/* TODO */
}
