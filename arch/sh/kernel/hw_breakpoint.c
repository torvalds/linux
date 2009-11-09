/*
 * arch/sh/kernel/hw_breakpoint.c
 *
 * Unified kernel/user-space hardware breakpoint facility for the on-chip UBC.
 *
 * Copyright (C) 2009  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <linux/percpu.h>
#include <linux/kallsyms.h>
#include <linux/notifier.h>
#include <linux/kprobes.h>
#include <linux/kdebug.h>
#include <linux/io.h>
#include <asm/hw_breakpoint.h>
#include <asm/mmu_context.h>

struct ubc_context {
	unsigned long pc;
	unsigned long state;
};

/* Per cpu ubc channel state */
static DEFINE_PER_CPU(struct ubc_context, ubc_ctx[HBP_NUM]);

/*
 * Stores the breakpoints currently in use on each breakpoint address
 * register for each cpus
 */
static DEFINE_PER_CPU(struct perf_event *, bp_per_reg[HBP_NUM]);

static int __init ubc_init(void)
{
	__raw_writel(0, UBC_CAMR0);
	__raw_writel(0, UBC_CBR0);
	__raw_writel(0, UBC_CBCR);

	__raw_writel(UBC_CRR_BIE | UBC_CRR_PCB, UBC_CRR0);

	/* dummy read for write posting */
	(void)__raw_readl(UBC_CRR0);

	return 0;
}
arch_initcall(ubc_init);

/*
 * Install a perf counter breakpoint.
 *
 * We seek a free UBC channel and use it for this breakpoint.
 *
 * Atomic: we hold the counter->ctx->lock and we only handle variables
 * and registers local to this cpu.
 */
int arch_install_hw_breakpoint(struct perf_event *bp)
{
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);
	struct ubc_context *ubc_ctx;
	int i;

	for (i = 0; i < HBP_NUM; i++) {
		struct perf_event **slot = &__get_cpu_var(bp_per_reg[i]);

		if (!*slot) {
			*slot = bp;
			break;
		}
	}

	if (WARN_ONCE(i == HBP_NUM, "Can't find any breakpoint slot"))
		return -EBUSY;

	ubc_ctx = &__get_cpu_var(ubc_ctx[i]);

	ubc_ctx->pc = info->address;
	ubc_ctx->state = info->len | info->type;

	__raw_writel(UBC_CBR_CE | ubc_ctx->state, UBC_CBR0);
	__raw_writel(ubc_ctx->pc, UBC_CAR0);

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
	struct ubc_context *ubc_ctx;
	int i;

	for (i = 0; i < HBP_NUM; i++) {
		struct perf_event **slot = &__get_cpu_var(bp_per_reg[i]);

		if (*slot == bp) {
			*slot = NULL;
			break;
		}
	}

	if (WARN_ONCE(i == HBP_NUM, "Can't find any breakpoint slot"))
		return;

	ubc_ctx = &__get_cpu_var(ubc_ctx[i]);
	ubc_ctx->pc = 0;
	ubc_ctx->state &= ~(info->len | info->type);

	__raw_writel(ubc_ctx->pc, UBC_CBR0);
	__raw_writel(ubc_ctx->state, UBC_CAR0);
}

static int get_hbp_len(u16 hbp_len)
{
	unsigned int len_in_bytes = 0;

	switch (hbp_len) {
	case SH_BREAKPOINT_LEN_1:
		len_in_bytes = 1;
		break;
	case SH_BREAKPOINT_LEN_2:
		len_in_bytes = 2;
		break;
	case SH_BREAKPOINT_LEN_4:
		len_in_bytes = 4;
		break;
	case SH_BREAKPOINT_LEN_8:
		len_in_bytes = 8;
		break;
	}
	return len_in_bytes;
}

/*
 * Check for virtual address in user space.
 */
int arch_check_va_in_userspace(unsigned long va, u16 hbp_len)
{
	unsigned int len;

	len = get_hbp_len(hbp_len);

	return (va <= TASK_SIZE - len);
}

/*
 * Check for virtual address in kernel space.
 */
static int arch_check_va_in_kernelspace(unsigned long va, u8 hbp_len)
{
	unsigned int len;

	len = get_hbp_len(hbp_len);

	return (va >= TASK_SIZE) && ((va + len - 1) >= TASK_SIZE);
}

/*
 * Store a breakpoint's encoded address, length, and type.
 */
static int arch_store_info(struct perf_event *bp)
{
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);

	/*
	 * User-space requests will always have the address field populated
	 * For kernel-addresses, either the address or symbol name can be
	 * specified.
	 */
	if (info->name)
		info->address = (unsigned long)kallsyms_lookup_name(info->name);
	if (info->address) {
		info->asid = get_asid();
		return 0;
	}

	return -EINVAL;
}

int arch_bp_generic_fields(int sh_len, int sh_type,
			   int *gen_len, int *gen_type)
{
	/* Len */
	switch (sh_len) {
	case SH_BREAKPOINT_LEN_1:
		*gen_len = HW_BREAKPOINT_LEN_1;
		break;
	case SH_BREAKPOINT_LEN_2:
		*gen_len = HW_BREAKPOINT_LEN_2;
		break;
	case SH_BREAKPOINT_LEN_4:
		*gen_len = HW_BREAKPOINT_LEN_4;
		break;
	case SH_BREAKPOINT_LEN_8:
		*gen_len = HW_BREAKPOINT_LEN_8;
		break;
	default:
		return -EINVAL;
	}

	/* Type */
	switch (sh_type) {
	case SH_BREAKPOINT_READ:
		*gen_type = HW_BREAKPOINT_R;
	case SH_BREAKPOINT_WRITE:
		*gen_type = HW_BREAKPOINT_W;
		break;
	case SH_BREAKPOINT_RW:
		*gen_type = HW_BREAKPOINT_W | HW_BREAKPOINT_R;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int arch_build_bp_info(struct perf_event *bp)
{
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);

	info->address = bp->attr.bp_addr;

	/* Len */
	switch (bp->attr.bp_len) {
	case HW_BREAKPOINT_LEN_1:
		info->len = SH_BREAKPOINT_LEN_1;
		break;
	case HW_BREAKPOINT_LEN_2:
		info->len = SH_BREAKPOINT_LEN_2;
		break;
	case HW_BREAKPOINT_LEN_4:
		info->len = SH_BREAKPOINT_LEN_4;
		break;
	case HW_BREAKPOINT_LEN_8:
		info->len = SH_BREAKPOINT_LEN_8;
		break;
	default:
		return -EINVAL;
	}

	/* Type */
	switch (bp->attr.bp_type) {
	case HW_BREAKPOINT_R:
		info->type = SH_BREAKPOINT_READ;
		break;
	case HW_BREAKPOINT_W:
		info->type = SH_BREAKPOINT_WRITE;
		break;
	case HW_BREAKPOINT_W | HW_BREAKPOINT_R:
		info->type = SH_BREAKPOINT_RW;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * Validate the arch-specific HW Breakpoint register settings
 */
int arch_validate_hwbkpt_settings(struct perf_event *bp,
				  struct task_struct *tsk)
{
	struct arch_hw_breakpoint *info = counter_arch_bp(bp);
	unsigned int align;
	int ret;

	ret = arch_build_bp_info(bp);
	if (ret)
		return ret;

	ret = -EINVAL;

	switch (info->len) {
	case SH_BREAKPOINT_LEN_1:
		align = 0;
		break;
	case SH_BREAKPOINT_LEN_2:
		align = 1;
		break;
	case SH_BREAKPOINT_LEN_4:
		align = 3;
		break;
	case SH_BREAKPOINT_LEN_8:
		align = 7;
		break;
	default:
		return ret;
	}

	if (bp->callback)
		ret = arch_store_info(bp);

	if (ret < 0)
		return ret;

	/*
	 * Check that the low-order bits of the address are appropriate
	 * for the alignment implied by len.
	 */
	if (info->address & align)
		return -EINVAL;

	/* Check that the virtual address is in the proper range */
	if (tsk) {
		if (!arch_check_va_in_userspace(info->address, info->len))
			return -EFAULT;
	} else {
		if (!arch_check_va_in_kernelspace(info->address, info->len))
			return -EFAULT;
	}

	return 0;
}

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
}

static int __kprobes hw_breakpoint_handler(struct die_args *args)
{
	int cpu, i, rc = NOTIFY_STOP;
	struct perf_event *bp;
	unsigned long val;

	val = __raw_readl(UBC_CBR0);
	__raw_writel(val & ~UBC_CBR_CE, UBC_CBR0);

	cpu = get_cpu();
	for (i = 0; i < HBP_NUM; i++) {
		/*
		 * The counter may be concurrently released but that can only
		 * occur from a call_rcu() path. We can then safely fetch
		 * the breakpoint, use its callback, touch its counter
		 * while we are in an rcu_read_lock() path.
		 */
		rcu_read_lock();

		bp = per_cpu(bp_per_reg[i], cpu);
		if (bp) {
			rc = NOTIFY_DONE;
		} else {
			rcu_read_unlock();
			break;
		}

		(bp->callback)(bp, args->regs);

		rcu_read_unlock();
	}

	if (bp) {
		struct arch_hw_breakpoint *info = counter_arch_bp(bp);

		__raw_writel(UBC_CBR_CE | info->len | info->type, UBC_CBR0);
		__raw_writel(info->address, UBC_CAR0);
	}

	put_cpu();

	return rc;
}

BUILD_TRAP_HANDLER(breakpoint)
{
	unsigned long ex = lookup_exception_vector();
	TRAP_HANDLER_DECL;

	notify_die(DIE_BREAKPOINT, "breakpoint", regs, 0, ex, SIGTRAP);
}

/*
 * Handle debug exception notifications.
 */
int __kprobes hw_breakpoint_exceptions_notify(struct notifier_block *unused,
				    unsigned long val, void *data)
{
	if (val != DIE_BREAKPOINT)
		return NOTIFY_DONE;

	return hw_breakpoint_handler(data);
}

void hw_breakpoint_pmu_read(struct perf_event *bp)
{
	/* TODO */
}

void hw_breakpoint_pmu_unthrottle(struct perf_event *bp)
{
	/* TODO */
}
