// SPDX-License-Identifier: GPL-2.0-only
/*
 * ARMv8 single-step debug support and mdscr context switching.
 *
 * Copyright (C) 2012 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#include <linux/cpu.h>
#include <linux/debugfs.h>
#include <linux/hardirq.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/kprobes.h>
#include <linux/stat.h>
#include <linux/uaccess.h>
#include <linux/sched/task_stack.h>

#include <asm/cpufeature.h>
#include <asm/cputype.h>
#include <asm/daifflags.h>
#include <asm/debug-monitors.h>
#include <asm/system_misc.h>
#include <asm/traps.h>

/* Determine debug architecture. */
u8 debug_monitors_arch(void)
{
	return cpuid_feature_extract_unsigned_field(read_sanitised_ftr_reg(SYS_ID_AA64DFR0_EL1),
						ID_AA64DFR0_EL1_DebugVer_SHIFT);
}

/*
 * MDSCR access routines.
 */
static void mdscr_write(u32 mdscr)
{
	unsigned long flags;
	flags = local_daif_save();
	write_sysreg(mdscr, mdscr_el1);
	local_daif_restore(flags);
}
NOKPROBE_SYMBOL(mdscr_write);

static u32 mdscr_read(void)
{
	return read_sysreg(mdscr_el1);
}
NOKPROBE_SYMBOL(mdscr_read);

/*
 * Allow root to disable self-hosted debug from userspace.
 * This is useful if you want to connect an external JTAG debugger.
 */
static bool debug_enabled = true;

static int create_debug_debugfs_entry(void)
{
	debugfs_create_bool("debug_enabled", 0644, NULL, &debug_enabled);
	return 0;
}
fs_initcall(create_debug_debugfs_entry);

static int __init early_debug_disable(char *buf)
{
	debug_enabled = false;
	return 0;
}

early_param("nodebugmon", early_debug_disable);

/*
 * Keep track of debug users on each core.
 * The ref counts are per-cpu so we use a local_t type.
 */
static DEFINE_PER_CPU(int, mde_ref_count);
static DEFINE_PER_CPU(int, kde_ref_count);

void enable_debug_monitors(enum dbg_active_el el)
{
	u32 mdscr, enable = 0;

	WARN_ON(preemptible());

	if (this_cpu_inc_return(mde_ref_count) == 1)
		enable = DBG_MDSCR_MDE;

	if (el == DBG_ACTIVE_EL1 &&
	    this_cpu_inc_return(kde_ref_count) == 1)
		enable |= DBG_MDSCR_KDE;

	if (enable && debug_enabled) {
		mdscr = mdscr_read();
		mdscr |= enable;
		mdscr_write(mdscr);
	}
}
NOKPROBE_SYMBOL(enable_debug_monitors);

void disable_debug_monitors(enum dbg_active_el el)
{
	u32 mdscr, disable = 0;

	WARN_ON(preemptible());

	if (this_cpu_dec_return(mde_ref_count) == 0)
		disable = ~DBG_MDSCR_MDE;

	if (el == DBG_ACTIVE_EL1 &&
	    this_cpu_dec_return(kde_ref_count) == 0)
		disable &= ~DBG_MDSCR_KDE;

	if (disable) {
		mdscr = mdscr_read();
		mdscr &= disable;
		mdscr_write(mdscr);
	}
}
NOKPROBE_SYMBOL(disable_debug_monitors);

/*
 * OS lock clearing.
 */
static int clear_os_lock(unsigned int cpu)
{
	write_sysreg(0, osdlr_el1);
	write_sysreg(0, oslar_el1);
	isb();
	return 0;
}

static int __init debug_monitors_init(void)
{
	return cpuhp_setup_state(CPUHP_AP_ARM64_DEBUG_MONITORS_STARTING,
				 "arm64/debug_monitors:starting",
				 clear_os_lock, NULL);
}
postcore_initcall(debug_monitors_init);

/*
 * Single step API and exception handling.
 */
static void set_user_regs_spsr_ss(struct user_pt_regs *regs)
{
	regs->pstate |= DBG_SPSR_SS;
}
NOKPROBE_SYMBOL(set_user_regs_spsr_ss);

static void clear_user_regs_spsr_ss(struct user_pt_regs *regs)
{
	regs->pstate &= ~DBG_SPSR_SS;
}
NOKPROBE_SYMBOL(clear_user_regs_spsr_ss);

#define set_regs_spsr_ss(r)	set_user_regs_spsr_ss(&(r)->user_regs)
#define clear_regs_spsr_ss(r)	clear_user_regs_spsr_ss(&(r)->user_regs)

static DEFINE_SPINLOCK(debug_hook_lock);
static LIST_HEAD(user_step_hook);
static LIST_HEAD(kernel_step_hook);

static void register_debug_hook(struct list_head *node, struct list_head *list)
{
	spin_lock(&debug_hook_lock);
	list_add_rcu(node, list);
	spin_unlock(&debug_hook_lock);

}

static void unregister_debug_hook(struct list_head *node)
{
	spin_lock(&debug_hook_lock);
	list_del_rcu(node);
	spin_unlock(&debug_hook_lock);
	synchronize_rcu();
}

void register_user_step_hook(struct step_hook *hook)
{
	register_debug_hook(&hook->node, &user_step_hook);
}

void unregister_user_step_hook(struct step_hook *hook)
{
	unregister_debug_hook(&hook->node);
}

void register_kernel_step_hook(struct step_hook *hook)
{
	register_debug_hook(&hook->node, &kernel_step_hook);
}

void unregister_kernel_step_hook(struct step_hook *hook)
{
	unregister_debug_hook(&hook->node);
}

/*
 * Call registered single step handlers
 * There is no Syndrome info to check for determining the handler.
 * So we call all the registered handlers, until the right handler is
 * found which returns zero.
 */
static int call_step_hook(struct pt_regs *regs, unsigned long esr)
{
	struct step_hook *hook;
	struct list_head *list;
	int retval = DBG_HOOK_ERROR;

	list = user_mode(regs) ? &user_step_hook : &kernel_step_hook;

	/*
	 * Since single-step exception disables interrupt, this function is
	 * entirely not preemptible, and we can use rcu list safely here.
	 */
	list_for_each_entry_rcu(hook, list, node)	{
		retval = hook->fn(regs, esr);
		if (retval == DBG_HOOK_HANDLED)
			break;
	}

	return retval;
}
NOKPROBE_SYMBOL(call_step_hook);

static void send_user_sigtrap(int si_code)
{
	struct pt_regs *regs = current_pt_regs();

	if (WARN_ON(!user_mode(regs)))
		return;

	if (interrupts_enabled(regs))
		local_irq_enable();

	arm64_force_sig_fault(SIGTRAP, si_code, instruction_pointer(regs),
			      "User debug trap");
}

static int single_step_handler(unsigned long unused, unsigned long esr,
			       struct pt_regs *regs)
{
	bool handler_found = false;

	/*
	 * If we are stepping a pending breakpoint, call the hw_breakpoint
	 * handler first.
	 */
	if (!reinstall_suspended_bps(regs))
		return 0;

	if (!handler_found && call_step_hook(regs, esr) == DBG_HOOK_HANDLED)
		handler_found = true;

	if (!handler_found && user_mode(regs)) {
		send_user_sigtrap(TRAP_TRACE);

		/*
		 * ptrace will disable single step unless explicitly
		 * asked to re-enable it. For other clients, it makes
		 * sense to leave it enabled (i.e. rewind the controls
		 * to the active-not-pending state).
		 */
		user_rewind_single_step(current);
	} else if (!handler_found) {
		pr_warn("Unexpected kernel single-step exception at EL1\n");
		/*
		 * Re-enable stepping since we know that we will be
		 * returning to regs.
		 */
		set_regs_spsr_ss(regs);
	}

	return 0;
}
NOKPROBE_SYMBOL(single_step_handler);

static LIST_HEAD(user_break_hook);
static LIST_HEAD(kernel_break_hook);

void register_user_break_hook(struct break_hook *hook)
{
	register_debug_hook(&hook->node, &user_break_hook);
}
EXPORT_SYMBOL_GPL(register_user_break_hook);

void unregister_user_break_hook(struct break_hook *hook)
{
	unregister_debug_hook(&hook->node);
}
EXPORT_SYMBOL_GPL(unregister_user_break_hook);

void register_kernel_break_hook(struct break_hook *hook)
{
	register_debug_hook(&hook->node, &kernel_break_hook);
}
EXPORT_SYMBOL_GPL(register_kernel_break_hook);

void unregister_kernel_break_hook(struct break_hook *hook)
{
	unregister_debug_hook(&hook->node);
}
EXPORT_SYMBOL_GPL(unregister_kernel_break_hook);

static int call_break_hook(struct pt_regs *regs, unsigned long esr)
{
	struct break_hook *hook;
	struct list_head *list;
	int (*fn)(struct pt_regs *regs, unsigned long esr) = NULL;

	list = user_mode(regs) ? &user_break_hook : &kernel_break_hook;

	/*
	 * Since brk exception disables interrupt, this function is
	 * entirely not preemptible, and we can use rcu list safely here.
	 */
	list_for_each_entry_rcu(hook, list, node) {
		unsigned long comment = esr & ESR_ELx_BRK64_ISS_COMMENT_MASK;

		if ((comment & ~hook->mask) == hook->imm)
			fn = hook->fn;
	}

	return fn ? fn(regs, esr) : DBG_HOOK_ERROR;
}
NOKPROBE_SYMBOL(call_break_hook);

static int brk_handler(unsigned long unused, unsigned long esr,
		       struct pt_regs *regs)
{
	if (call_break_hook(regs, esr) == DBG_HOOK_HANDLED)
		return 0;

	if (user_mode(regs)) {
		send_user_sigtrap(TRAP_BRKPT);
	} else {
		pr_warn("Unexpected kernel BRK exception at EL1\n");
		return -EFAULT;
	}

	return 0;
}
NOKPROBE_SYMBOL(brk_handler);

int aarch32_break_handler(struct pt_regs *regs)
{
	u32 arm_instr;
	u16 thumb_instr;
	bool bp = false;
	void __user *pc = (void __user *)instruction_pointer(regs);

	if (!compat_user_mode(regs))
		return -EFAULT;

	if (compat_thumb_mode(regs)) {
		/* get 16-bit Thumb instruction */
		__le16 instr;
		get_user(instr, (__le16 __user *)pc);
		thumb_instr = le16_to_cpu(instr);
		if (thumb_instr == AARCH32_BREAK_THUMB2_LO) {
			/* get second half of 32-bit Thumb-2 instruction */
			get_user(instr, (__le16 __user *)(pc + 2));
			thumb_instr = le16_to_cpu(instr);
			bp = thumb_instr == AARCH32_BREAK_THUMB2_HI;
		} else {
			bp = thumb_instr == AARCH32_BREAK_THUMB;
		}
	} else {
		/* 32-bit ARM instruction */
		__le32 instr;
		get_user(instr, (__le32 __user *)pc);
		arm_instr = le32_to_cpu(instr);
		bp = (arm_instr & ~0xf0000000) == AARCH32_BREAK_ARM;
	}

	if (!bp)
		return -EFAULT;

	send_user_sigtrap(TRAP_BRKPT);
	return 0;
}
NOKPROBE_SYMBOL(aarch32_break_handler);

void __init debug_traps_init(void)
{
	hook_debug_fault_code(DBG_ESR_EVT_HWSS, single_step_handler, SIGTRAP,
			      TRAP_TRACE, "single-step handler");
	hook_debug_fault_code(DBG_ESR_EVT_BRK, brk_handler, SIGTRAP,
			      TRAP_BRKPT, "BRK handler");
}

/* Re-enable single step for syscall restarting. */
void user_rewind_single_step(struct task_struct *task)
{
	/*
	 * If single step is active for this thread, then set SPSR.SS
	 * to 1 to avoid returning to the active-pending state.
	 */
	if (test_tsk_thread_flag(task, TIF_SINGLESTEP))
		set_regs_spsr_ss(task_pt_regs(task));
}
NOKPROBE_SYMBOL(user_rewind_single_step);

void user_fastforward_single_step(struct task_struct *task)
{
	if (test_tsk_thread_flag(task, TIF_SINGLESTEP))
		clear_regs_spsr_ss(task_pt_regs(task));
}

void user_regs_reset_single_step(struct user_pt_regs *regs,
				 struct task_struct *task)
{
	if (test_tsk_thread_flag(task, TIF_SINGLESTEP))
		set_user_regs_spsr_ss(regs);
	else
		clear_user_regs_spsr_ss(regs);
}

/* Kernel API */
void kernel_enable_single_step(struct pt_regs *regs)
{
	WARN_ON(!irqs_disabled());
	set_regs_spsr_ss(regs);
	mdscr_write(mdscr_read() | DBG_MDSCR_SS);
	enable_debug_monitors(DBG_ACTIVE_EL1);
}
NOKPROBE_SYMBOL(kernel_enable_single_step);

void kernel_disable_single_step(void)
{
	WARN_ON(!irqs_disabled());
	mdscr_write(mdscr_read() & ~DBG_MDSCR_SS);
	disable_debug_monitors(DBG_ACTIVE_EL1);
}
NOKPROBE_SYMBOL(kernel_disable_single_step);

int kernel_active_single_step(void)
{
	WARN_ON(!irqs_disabled());
	return mdscr_read() & DBG_MDSCR_SS;
}
NOKPROBE_SYMBOL(kernel_active_single_step);

void kernel_rewind_single_step(struct pt_regs *regs)
{
	set_regs_spsr_ss(regs);
}

/* ptrace API */
void user_enable_single_step(struct task_struct *task)
{
	struct thread_info *ti = task_thread_info(task);

	if (!test_and_set_ti_thread_flag(ti, TIF_SINGLESTEP))
		set_regs_spsr_ss(task_pt_regs(task));
}
NOKPROBE_SYMBOL(user_enable_single_step);

void user_disable_single_step(struct task_struct *task)
{
	clear_ti_thread_flag(task_thread_info(task), TIF_SINGLESTEP);
}
NOKPROBE_SYMBOL(user_disable_single_step);
