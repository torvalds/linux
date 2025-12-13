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
#include <asm/exception.h>
#include <asm/kgdb.h>
#include <asm/kprobes.h>
#include <asm/system_misc.h>
#include <asm/traps.h>
#include <asm/uprobes.h>

/* Determine debug architecture. */
u8 debug_monitors_arch(void)
{
	return cpuid_feature_extract_unsigned_field(read_sanitised_ftr_reg(SYS_ID_AA64DFR0_EL1),
						ID_AA64DFR0_EL1_DebugVer_SHIFT);
}

/*
 * MDSCR access routines.
 */
static void mdscr_write(u64 mdscr)
{
	unsigned long flags;
	flags = local_daif_save();
	write_sysreg(mdscr, mdscr_el1);
	local_daif_restore(flags);
}
NOKPROBE_SYMBOL(mdscr_write);

static u64 mdscr_read(void)
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
	u64 mdscr, enable = 0;

	WARN_ON(preemptible());

	if (this_cpu_inc_return(mde_ref_count) == 1)
		enable = MDSCR_EL1_MDE;

	if (el == DBG_ACTIVE_EL1 &&
	    this_cpu_inc_return(kde_ref_count) == 1)
		enable |= MDSCR_EL1_KDE;

	if (enable && debug_enabled) {
		mdscr = mdscr_read();
		mdscr |= enable;
		mdscr_write(mdscr);
	}
}
NOKPROBE_SYMBOL(enable_debug_monitors);

void disable_debug_monitors(enum dbg_active_el el)
{
	u64 mdscr, disable = 0;

	WARN_ON(preemptible());

	if (this_cpu_dec_return(mde_ref_count) == 0)
		disable = ~MDSCR_EL1_MDE;

	if (el == DBG_ACTIVE_EL1 &&
	    this_cpu_dec_return(kde_ref_count) == 0)
		disable &= ~MDSCR_EL1_KDE;

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

static void send_user_sigtrap(int si_code)
{
	struct pt_regs *regs = current_pt_regs();

	if (WARN_ON(!user_mode(regs)))
		return;

	if (!regs_irqs_disabled(regs))
		local_irq_enable();

	arm64_force_sig_fault(SIGTRAP, si_code, instruction_pointer(regs),
			      "User debug trap");
}

/*
 * We have already unmasked interrupts and enabled preemption
 * when calling do_el0_softstep() from entry-common.c.
 */
void do_el0_softstep(unsigned long esr, struct pt_regs *regs)
{
	if (uprobe_single_step_handler(regs, esr) == DBG_HOOK_HANDLED)
		return;

	send_user_sigtrap(TRAP_TRACE);
	/*
	 * ptrace will disable single step unless explicitly
	 * asked to re-enable it. For other clients, it makes
	 * sense to leave it enabled (i.e. rewind the controls
	 * to the active-not-pending state).
	 */
	user_rewind_single_step(current);
}

void do_el1_softstep(unsigned long esr, struct pt_regs *regs)
{
	if (kgdb_single_step_handler(regs, esr) == DBG_HOOK_HANDLED)
		return;

	pr_warn("Unexpected kernel single-step exception at EL1\n");
	/*
	 * Re-enable stepping since we know that we will be
	 * returning to regs.
	 */
	set_regs_spsr_ss(regs);
}
NOKPROBE_SYMBOL(do_el1_softstep);

static int call_el1_break_hook(struct pt_regs *regs, unsigned long esr)
{
	if (esr_brk_comment(esr) == BUG_BRK_IMM)
		return bug_brk_handler(regs, esr);

	if (IS_ENABLED(CONFIG_CFI) && esr_is_cfi_brk(esr))
		return cfi_brk_handler(regs, esr);

	if (esr_brk_comment(esr) == FAULT_BRK_IMM)
		return reserved_fault_brk_handler(regs, esr);

	if (IS_ENABLED(CONFIG_KASAN_SW_TAGS) &&
		(esr_brk_comment(esr) & ~KASAN_BRK_MASK) == KASAN_BRK_IMM)
		return kasan_brk_handler(regs, esr);

	if (IS_ENABLED(CONFIG_UBSAN_TRAP) && esr_is_ubsan_brk(esr))
		return ubsan_brk_handler(regs, esr);

	if (IS_ENABLED(CONFIG_KGDB)) {
		if (esr_brk_comment(esr) == KGDB_DYN_DBG_BRK_IMM)
			return kgdb_brk_handler(regs, esr);
		if (esr_brk_comment(esr) == KGDB_COMPILED_DBG_BRK_IMM)
			return kgdb_compiled_brk_handler(regs, esr);
	}

	if (IS_ENABLED(CONFIG_KPROBES)) {
		if (esr_brk_comment(esr) == KPROBES_BRK_IMM)
			return kprobe_brk_handler(regs, esr);
		if (esr_brk_comment(esr) == KPROBES_BRK_SS_IMM)
			return kprobe_ss_brk_handler(regs, esr);
	}

	if (IS_ENABLED(CONFIG_KRETPROBES) &&
		esr_brk_comment(esr) == KRETPROBES_BRK_IMM)
		return kretprobe_brk_handler(regs, esr);

	return DBG_HOOK_ERROR;
}
NOKPROBE_SYMBOL(call_el1_break_hook);

/*
 * We have already unmasked interrupts and enabled preemption
 * when calling do_el0_brk64() from entry-common.c.
 */
void do_el0_brk64(unsigned long esr, struct pt_regs *regs)
{
	if (IS_ENABLED(CONFIG_UPROBES) &&
		esr_brk_comment(esr) == UPROBES_BRK_IMM &&
		uprobe_brk_handler(regs, esr) == DBG_HOOK_HANDLED)
		return;

	send_user_sigtrap(TRAP_BRKPT);
}

void do_el1_brk64(unsigned long esr, struct pt_regs *regs)
{
	if (call_el1_break_hook(regs, esr) == DBG_HOOK_HANDLED)
		return;

	die("Oops - BRK", regs, esr);
}
NOKPROBE_SYMBOL(do_el1_brk64);

#ifdef CONFIG_COMPAT
void do_bkpt32(unsigned long esr, struct pt_regs *regs)
{
	arm64_notify_die("aarch32 BKPT", regs, SIGTRAP, TRAP_BRKPT, regs->pc, esr);
}
#endif /* CONFIG_COMPAT */

bool try_handle_aarch32_break(struct pt_regs *regs)
{
	u32 arm_instr;
	u16 thumb_instr;
	bool bp = false;
	void __user *pc = (void __user *)instruction_pointer(regs);

	if (!compat_user_mode(regs))
		return false;

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
		return false;

	send_user_sigtrap(TRAP_BRKPT);
	return true;
}
NOKPROBE_SYMBOL(try_handle_aarch32_break);

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
	mdscr_write(mdscr_read() | MDSCR_EL1_SS);
	enable_debug_monitors(DBG_ACTIVE_EL1);
}
NOKPROBE_SYMBOL(kernel_enable_single_step);

void kernel_disable_single_step(void)
{
	WARN_ON(!irqs_disabled());
	mdscr_write(mdscr_read() & ~MDSCR_EL1_SS);
	disable_debug_monitors(DBG_ACTIVE_EL1);
}
NOKPROBE_SYMBOL(kernel_disable_single_step);

int kernel_active_single_step(void)
{
	WARN_ON(!irqs_disabled());
	return mdscr_read() & MDSCR_EL1_SS;
}
NOKPROBE_SYMBOL(kernel_active_single_step);

void kernel_rewind_single_step(struct pt_regs *regs)
{
	set_regs_spsr_ss(regs);
}

void kernel_fastforward_single_step(struct pt_regs *regs)
{
	clear_regs_spsr_ss(regs);
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
