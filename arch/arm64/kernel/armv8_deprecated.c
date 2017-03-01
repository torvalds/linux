/*
 *  Copyright (C) 2014 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/perf_event.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sysctl.h>

#include <asm/cpufeature.h>
#include <asm/insn.h>
#include <asm/opcodes.h>
#include <asm/sysreg.h>
#include <asm/system_misc.h>
#include <asm/traps.h>
#include <asm/uaccess.h>
#include <asm/cpufeature.h>

#define CREATE_TRACE_POINTS
#include "trace-events-emulation.h"

/*
 * The runtime support for deprecated instruction support can be in one of
 * following three states -
 *
 * 0 = undef
 * 1 = emulate (software emulation)
 * 2 = hw (supported in hardware)
 */
enum insn_emulation_mode {
	INSN_UNDEF,
	INSN_EMULATE,
	INSN_HW,
};

enum legacy_insn_status {
	INSN_DEPRECATED,
	INSN_OBSOLETE,
};

struct insn_emulation_ops {
	const char		*name;
	enum legacy_insn_status	status;
	struct undef_hook	*hooks;
	int			(*set_hw_mode)(bool enable);
};

struct insn_emulation {
	struct list_head node;
	struct insn_emulation_ops *ops;
	int current_mode;
	int min;
	int max;
};

static LIST_HEAD(insn_emulation);
static int nr_insn_emulated __initdata;
static DEFINE_RAW_SPINLOCK(insn_emulation_lock);

static void register_emulation_hooks(struct insn_emulation_ops *ops)
{
	struct undef_hook *hook;

	BUG_ON(!ops->hooks);

	for (hook = ops->hooks; hook->instr_mask; hook++)
		register_undef_hook(hook);

	pr_notice("Registered %s emulation handler\n", ops->name);
}

static void remove_emulation_hooks(struct insn_emulation_ops *ops)
{
	struct undef_hook *hook;

	BUG_ON(!ops->hooks);

	for (hook = ops->hooks; hook->instr_mask; hook++)
		unregister_undef_hook(hook);

	pr_notice("Removed %s emulation handler\n", ops->name);
}

static void enable_insn_hw_mode(void *data)
{
	struct insn_emulation *insn = (struct insn_emulation *)data;
	if (insn->ops->set_hw_mode)
		insn->ops->set_hw_mode(true);
}

static void disable_insn_hw_mode(void *data)
{
	struct insn_emulation *insn = (struct insn_emulation *)data;
	if (insn->ops->set_hw_mode)
		insn->ops->set_hw_mode(false);
}

/* Run set_hw_mode(mode) on all active CPUs */
static int run_all_cpu_set_hw_mode(struct insn_emulation *insn, bool enable)
{
	if (!insn->ops->set_hw_mode)
		return -EINVAL;
	if (enable)
		on_each_cpu(enable_insn_hw_mode, (void *)insn, true);
	else
		on_each_cpu(disable_insn_hw_mode, (void *)insn, true);
	return 0;
}

/*
 * Run set_hw_mode for all insns on a starting CPU.
 * Returns:
 *  0 		- If all the hooks ran successfully.
 * -EINVAL	- At least one hook is not supported by the CPU.
 */
static int run_all_insn_set_hw_mode(unsigned long cpu)
{
	int rc = 0;
	unsigned long flags;
	struct insn_emulation *insn;

	raw_spin_lock_irqsave(&insn_emulation_lock, flags);
	list_for_each_entry(insn, &insn_emulation, node) {
		bool enable = (insn->current_mode == INSN_HW);
		if (insn->ops->set_hw_mode && insn->ops->set_hw_mode(enable)) {
			pr_warn("CPU[%ld] cannot support the emulation of %s",
				cpu, insn->ops->name);
			rc = -EINVAL;
		}
	}
	raw_spin_unlock_irqrestore(&insn_emulation_lock, flags);
	return rc;
}

static int update_insn_emulation_mode(struct insn_emulation *insn,
				       enum insn_emulation_mode prev)
{
	int ret = 0;

	switch (prev) {
	case INSN_UNDEF: /* Nothing to be done */
		break;
	case INSN_EMULATE:
		remove_emulation_hooks(insn->ops);
		break;
	case INSN_HW:
		if (!run_all_cpu_set_hw_mode(insn, false))
			pr_notice("Disabled %s support\n", insn->ops->name);
		break;
	}

	switch (insn->current_mode) {
	case INSN_UNDEF:
		break;
	case INSN_EMULATE:
		register_emulation_hooks(insn->ops);
		break;
	case INSN_HW:
		ret = run_all_cpu_set_hw_mode(insn, true);
		if (!ret)
			pr_notice("Enabled %s support\n", insn->ops->name);
		break;
	}

	return ret;
}

static void __init register_insn_emulation(struct insn_emulation_ops *ops)
{
	unsigned long flags;
	struct insn_emulation *insn;

	insn = kzalloc(sizeof(*insn), GFP_KERNEL);
	insn->ops = ops;
	insn->min = INSN_UNDEF;

	switch (ops->status) {
	case INSN_DEPRECATED:
		insn->current_mode = INSN_EMULATE;
		/* Disable the HW mode if it was turned on at early boot time */
		run_all_cpu_set_hw_mode(insn, false);
		insn->max = INSN_HW;
		break;
	case INSN_OBSOLETE:
		insn->current_mode = INSN_UNDEF;
		insn->max = INSN_EMULATE;
		break;
	}

	raw_spin_lock_irqsave(&insn_emulation_lock, flags);
	list_add(&insn->node, &insn_emulation);
	nr_insn_emulated++;
	raw_spin_unlock_irqrestore(&insn_emulation_lock, flags);

	/* Register any handlers if required */
	update_insn_emulation_mode(insn, INSN_UNDEF);
}

static int emulation_proc_handler(struct ctl_table *table, int write,
				  void __user *buffer, size_t *lenp,
				  loff_t *ppos)
{
	int ret = 0;
	struct insn_emulation *insn = (struct insn_emulation *) table->data;
	enum insn_emulation_mode prev_mode = insn->current_mode;

	table->data = &insn->current_mode;
	ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);

	if (ret || !write || prev_mode == insn->current_mode)
		goto ret;

	ret = update_insn_emulation_mode(insn, prev_mode);
	if (ret) {
		/* Mode change failed, revert to previous mode. */
		insn->current_mode = prev_mode;
		update_insn_emulation_mode(insn, INSN_UNDEF);
	}
ret:
	table->data = insn;
	return ret;
}

static struct ctl_table ctl_abi[] = {
	{
		.procname = "abi",
		.mode = 0555,
	},
	{ }
};

static void __init register_insn_emulation_sysctl(struct ctl_table *table)
{
	unsigned long flags;
	int i = 0;
	struct insn_emulation *insn;
	struct ctl_table *insns_sysctl, *sysctl;

	insns_sysctl = kzalloc(sizeof(*sysctl) * (nr_insn_emulated + 1),
			      GFP_KERNEL);

	raw_spin_lock_irqsave(&insn_emulation_lock, flags);
	list_for_each_entry(insn, &insn_emulation, node) {
		sysctl = &insns_sysctl[i];

		sysctl->mode = 0644;
		sysctl->maxlen = sizeof(int);

		sysctl->procname = insn->ops->name;
		sysctl->data = insn;
		sysctl->extra1 = &insn->min;
		sysctl->extra2 = &insn->max;
		sysctl->proc_handler = emulation_proc_handler;
		i++;
	}
	raw_spin_unlock_irqrestore(&insn_emulation_lock, flags);

	table->child = insns_sysctl;
	register_sysctl_table(table);
}

/*
 *  Implement emulation of the SWP/SWPB instructions using load-exclusive and
 *  store-exclusive.
 *
 *  Syntax of SWP{B} instruction: SWP{B}<c> <Rt>, <Rt2>, [<Rn>]
 *  Where: Rt  = destination
 *	   Rt2 = source
 *	   Rn  = address
 */

/*
 * Error-checking SWP macros implemented using ldxr{b}/stxr{b}
 */
#define __user_swpX_asm(data, addr, res, temp, B)		\
do {								\
	uaccess_enable();					\
	__asm__ __volatile__(					\
	"0:	ldxr"B"		%w2, [%3]\n"			\
	"1:	stxr"B"		%w0, %w1, [%3]\n"		\
	"	cbz		%w0, 2f\n"			\
	"	mov		%w0, %w4\n"			\
	"	b		3f\n"				\
	"2:\n"							\
	"	mov		%w1, %w2\n"			\
	"3:\n"							\
	"	.pushsection	 .fixup,\"ax\"\n"		\
	"	.align		2\n"				\
	"4:	mov		%w0, %w5\n"			\
	"	b		3b\n"				\
	"	.popsection"					\
	_ASM_EXTABLE(0b, 4b)					\
	_ASM_EXTABLE(1b, 4b)					\
	: "=&r" (res), "+r" (data), "=&r" (temp)		\
	: "r" (addr), "i" (-EAGAIN), "i" (-EFAULT)		\
	: "memory");						\
	uaccess_disable();					\
} while (0)

#define __user_swp_asm(data, addr, res, temp) \
	__user_swpX_asm(data, addr, res, temp, "")
#define __user_swpb_asm(data, addr, res, temp) \
	__user_swpX_asm(data, addr, res, temp, "b")

/*
 * Bit 22 of the instruction encoding distinguishes between
 * the SWP and SWPB variants (bit set means SWPB).
 */
#define TYPE_SWPB (1 << 22)

/*
 * Set up process info to signal segmentation fault - called on access error.
 */
static void set_segfault(struct pt_regs *regs, unsigned long addr)
{
	siginfo_t info;

	down_read(&current->mm->mmap_sem);
	if (find_vma(current->mm, addr) == NULL)
		info.si_code = SEGV_MAPERR;
	else
		info.si_code = SEGV_ACCERR;
	up_read(&current->mm->mmap_sem);

	info.si_signo = SIGSEGV;
	info.si_errno = 0;
	info.si_addr  = (void *) instruction_pointer(regs);

	pr_debug("SWP{B} emulation: access caused memory abort!\n");
	arm64_notify_die("Illegal memory access", regs, &info, 0);
}

static int emulate_swpX(unsigned int address, unsigned int *data,
			unsigned int type)
{
	unsigned int res = 0;

	if ((type != TYPE_SWPB) && (address & 0x3)) {
		/* SWP to unaligned address not permitted */
		pr_debug("SWP instruction on unaligned pointer!\n");
		return -EFAULT;
	}

	while (1) {
		unsigned long temp;

		if (type == TYPE_SWPB)
			__user_swpb_asm(*data, address, res, temp);
		else
			__user_swp_asm(*data, address, res, temp);

		if (likely(res != -EAGAIN) || signal_pending(current))
			break;

		cond_resched();
	}

	return res;
}

#define	ARM_OPCODE_CONDITION_UNCOND	0xf

static unsigned int __kprobes aarch32_check_condition(u32 opcode, u32 psr)
{
	u32 cc_bits  = opcode >> 28;

	if (cc_bits != ARM_OPCODE_CONDITION_UNCOND) {
		if ((*aarch32_opcode_cond_checks[cc_bits])(psr))
			return ARM_OPCODE_CONDTEST_PASS;
		else
			return ARM_OPCODE_CONDTEST_FAIL;
	}
	return ARM_OPCODE_CONDTEST_UNCOND;
}

/*
 * swp_handler logs the id of calling process, dissects the instruction, sanity
 * checks the memory location, calls emulate_swpX for the actual operation and
 * deals with fixup/error handling before returning
 */
static int swp_handler(struct pt_regs *regs, u32 instr)
{
	u32 destreg, data, type, address = 0;
	int rn, rt2, res = 0;

	perf_sw_event(PERF_COUNT_SW_EMULATION_FAULTS, 1, regs, regs->pc);

	type = instr & TYPE_SWPB;

	switch (aarch32_check_condition(instr, regs->pstate)) {
	case ARM_OPCODE_CONDTEST_PASS:
		break;
	case ARM_OPCODE_CONDTEST_FAIL:
		/* Condition failed - return to next instruction */
		goto ret;
	case ARM_OPCODE_CONDTEST_UNCOND:
		/* If unconditional encoding - not a SWP, undef */
		return -EFAULT;
	default:
		return -EINVAL;
	}

	rn = aarch32_insn_extract_reg_num(instr, A32_RN_OFFSET);
	rt2 = aarch32_insn_extract_reg_num(instr, A32_RT2_OFFSET);

	address = (u32)regs->user_regs.regs[rn];
	data	= (u32)regs->user_regs.regs[rt2];
	destreg = aarch32_insn_extract_reg_num(instr, A32_RT_OFFSET);

	pr_debug("addr in r%d->0x%08x, dest is r%d, source in r%d->0x%08x)\n",
		rn, address, destreg,
		aarch32_insn_extract_reg_num(instr, A32_RT2_OFFSET), data);

	/* Check access in reasonable access range for both SWP and SWPB */
	if (!access_ok(VERIFY_WRITE, (address & ~3), 4)) {
		pr_debug("SWP{B} emulation: access to 0x%08x not allowed!\n",
			address);
		goto fault;
	}

	res = emulate_swpX(address, &data, type);
	if (res == -EFAULT)
		goto fault;
	else if (res == 0)
		regs->user_regs.regs[destreg] = data;

ret:
	if (type == TYPE_SWPB)
		trace_instruction_emulation("swpb", regs->pc);
	else
		trace_instruction_emulation("swp", regs->pc);

	pr_warn_ratelimited("\"%s\" (%ld) uses obsolete SWP{B} instruction at 0x%llx\n",
			current->comm, (unsigned long)current->pid, regs->pc);

	regs->pc += 4;
	return 0;

fault:
	set_segfault(regs, address);

	return 0;
}

/*
 * Only emulate SWP/SWPB executed in ARM state/User mode.
 * The kernel must be SWP free and SWP{B} does not exist in Thumb.
 */
static struct undef_hook swp_hooks[] = {
	{
		.instr_mask	= 0x0fb00ff0,
		.instr_val	= 0x01000090,
		.pstate_mask	= COMPAT_PSR_MODE_MASK,
		.pstate_val	= COMPAT_PSR_MODE_USR,
		.fn		= swp_handler
	},
	{ }
};

static struct insn_emulation_ops swp_ops = {
	.name = "swp",
	.status = INSN_OBSOLETE,
	.hooks = swp_hooks,
	.set_hw_mode = NULL,
};

static int cp15barrier_handler(struct pt_regs *regs, u32 instr)
{
	perf_sw_event(PERF_COUNT_SW_EMULATION_FAULTS, 1, regs, regs->pc);

	switch (aarch32_check_condition(instr, regs->pstate)) {
	case ARM_OPCODE_CONDTEST_PASS:
		break;
	case ARM_OPCODE_CONDTEST_FAIL:
		/* Condition failed - return to next instruction */
		goto ret;
	case ARM_OPCODE_CONDTEST_UNCOND:
		/* If unconditional encoding - not a barrier instruction */
		return -EFAULT;
	default:
		return -EINVAL;
	}

	switch (aarch32_insn_mcr_extract_crm(instr)) {
	case 10:
		/*
		 * dmb - mcr p15, 0, Rt, c7, c10, 5
		 * dsb - mcr p15, 0, Rt, c7, c10, 4
		 */
		if (aarch32_insn_mcr_extract_opc2(instr) == 5) {
			dmb(sy);
			trace_instruction_emulation(
				"mcr p15, 0, Rt, c7, c10, 5 ; dmb", regs->pc);
		} else {
			dsb(sy);
			trace_instruction_emulation(
				"mcr p15, 0, Rt, c7, c10, 4 ; dsb", regs->pc);
		}
		break;
	case 5:
		/*
		 * isb - mcr p15, 0, Rt, c7, c5, 4
		 *
		 * Taking an exception or returning from one acts as an
		 * instruction barrier. So no explicit barrier needed here.
		 */
		trace_instruction_emulation(
			"mcr p15, 0, Rt, c7, c5, 4 ; isb", regs->pc);
		break;
	}

ret:
	pr_warn_ratelimited("\"%s\" (%ld) uses deprecated CP15 Barrier instruction at 0x%llx\n",
			current->comm, (unsigned long)current->pid, regs->pc);

	regs->pc += 4;
	return 0;
}

static int cp15_barrier_set_hw_mode(bool enable)
{
	if (enable)
		config_sctlr_el1(0, SCTLR_EL1_CP15BEN);
	else
		config_sctlr_el1(SCTLR_EL1_CP15BEN, 0);
	return 0;
}

static struct undef_hook cp15_barrier_hooks[] = {
	{
		.instr_mask	= 0x0fff0fdf,
		.instr_val	= 0x0e070f9a,
		.pstate_mask	= COMPAT_PSR_MODE_MASK,
		.pstate_val	= COMPAT_PSR_MODE_USR,
		.fn		= cp15barrier_handler,
	},
	{
		.instr_mask	= 0x0fff0fff,
		.instr_val	= 0x0e070f95,
		.pstate_mask	= COMPAT_PSR_MODE_MASK,
		.pstate_val	= COMPAT_PSR_MODE_USR,
		.fn		= cp15barrier_handler,
	},
	{ }
};

static struct insn_emulation_ops cp15_barrier_ops = {
	.name = "cp15_barrier",
	.status = INSN_DEPRECATED,
	.hooks = cp15_barrier_hooks,
	.set_hw_mode = cp15_barrier_set_hw_mode,
};

static int setend_set_hw_mode(bool enable)
{
	if (!cpu_supports_mixed_endian_el0())
		return -EINVAL;

	if (enable)
		config_sctlr_el1(SCTLR_EL1_SED, 0);
	else
		config_sctlr_el1(0, SCTLR_EL1_SED);
	return 0;
}

static int compat_setend_handler(struct pt_regs *regs, u32 big_endian)
{
	char *insn;

	perf_sw_event(PERF_COUNT_SW_EMULATION_FAULTS, 1, regs, regs->pc);

	if (big_endian) {
		insn = "setend be";
		regs->pstate |= COMPAT_PSR_E_BIT;
	} else {
		insn = "setend le";
		regs->pstate &= ~COMPAT_PSR_E_BIT;
	}

	trace_instruction_emulation(insn, regs->pc);
	pr_warn_ratelimited("\"%s\" (%ld) uses deprecated setend instruction at 0x%llx\n",
			current->comm, (unsigned long)current->pid, regs->pc);

	return 0;
}

static int a32_setend_handler(struct pt_regs *regs, u32 instr)
{
	int rc = compat_setend_handler(regs, (instr >> 9) & 1);
	regs->pc += 4;
	return rc;
}

static int t16_setend_handler(struct pt_regs *regs, u32 instr)
{
	int rc = compat_setend_handler(regs, (instr >> 3) & 1);
	regs->pc += 2;
	return rc;
}

static struct undef_hook setend_hooks[] = {
	{
		.instr_mask	= 0xfffffdff,
		.instr_val	= 0xf1010000,
		.pstate_mask	= COMPAT_PSR_MODE_MASK,
		.pstate_val	= COMPAT_PSR_MODE_USR,
		.fn		= a32_setend_handler,
	},
	{
		/* Thumb mode */
		.instr_mask	= 0x0000fff7,
		.instr_val	= 0x0000b650,
		.pstate_mask	= (COMPAT_PSR_T_BIT | COMPAT_PSR_MODE_MASK),
		.pstate_val	= (COMPAT_PSR_T_BIT | COMPAT_PSR_MODE_USR),
		.fn		= t16_setend_handler,
	},
	{}
};

static struct insn_emulation_ops setend_ops = {
	.name = "setend",
	.status = INSN_DEPRECATED,
	.hooks = setend_hooks,
	.set_hw_mode = setend_set_hw_mode,
};

static int insn_cpu_hotplug_notify(struct notifier_block *b,
			      unsigned long action, void *hcpu)
{
	int rc = 0;
	if ((action & ~CPU_TASKS_FROZEN) == CPU_STARTING)
		rc = run_all_insn_set_hw_mode((unsigned long)hcpu);

	return notifier_from_errno(rc);
}

static struct notifier_block insn_cpu_hotplug_notifier = {
	.notifier_call = insn_cpu_hotplug_notify,
};

/*
 * Invoked as late_initcall, since not needed before init spawned.
 */
static int __init armv8_deprecated_init(void)
{
	if (IS_ENABLED(CONFIG_SWP_EMULATION))
		register_insn_emulation(&swp_ops);

	if (IS_ENABLED(CONFIG_CP15_BARRIER_EMULATION))
		register_insn_emulation(&cp15_barrier_ops);

	if (IS_ENABLED(CONFIG_SETEND_EMULATION)) {
		if(system_supports_mixed_endian_el0())
			register_insn_emulation(&setend_ops);
		else
			pr_info("setend instruction emulation is not supported on the system");
	}

	register_cpu_notifier(&insn_cpu_hotplug_notifier);
	register_insn_emulation_sysctl(ctl_abi);

	return 0;
}

late_initcall(armv8_deprecated_init);
