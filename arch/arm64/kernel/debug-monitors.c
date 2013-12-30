/*
 * ARMv8 single-step debug support and mdscr context switching.
 *
 * Copyright (C) 2012 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#include <linux/cpu.h>
#include <linux/debugfs.h>
#include <linux/hardirq.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/stat.h>
#include <linux/uaccess.h>

#include <asm/debug-monitors.h>
#include <asm/cputype.h>
#include <asm/system_misc.h>

/* Low-level stepping controls. */
#define DBG_MDSCR_SS		(1 << 0)
#define DBG_SPSR_SS		(1 << 21)

/* MDSCR_EL1 enabling bits */
#define DBG_MDSCR_KDE		(1 << 13)
#define DBG_MDSCR_MDE		(1 << 15)
#define DBG_MDSCR_MASK		~(DBG_MDSCR_KDE | DBG_MDSCR_MDE)

/* Determine debug architecture. */
u8 debug_monitors_arch(void)
{
	return read_cpuid(ID_AA64DFR0_EL1) & 0xf;
}

/*
 * MDSCR access routines.
 */
static void mdscr_write(u32 mdscr)
{
	unsigned long flags;
	local_dbg_save(flags);
	asm volatile("msr mdscr_el1, %0" :: "r" (mdscr));
	local_dbg_restore(flags);
}

static u32 mdscr_read(void)
{
	u32 mdscr;
	asm volatile("mrs %0, mdscr_el1" : "=r" (mdscr));
	return mdscr;
}

/*
 * Allow root to disable self-hosted debug from userspace.
 * This is useful if you want to connect an external JTAG debugger.
 */
static u32 debug_enabled = 1;

static int create_debug_debugfs_entry(void)
{
	debugfs_create_bool("debug_enabled", 0644, NULL, &debug_enabled);
	return 0;
}
fs_initcall(create_debug_debugfs_entry);

static int __init early_debug_disable(char *buf)
{
	debug_enabled = 0;
	return 0;
}

early_param("nodebugmon", early_debug_disable);

/*
 * Keep track of debug users on each core.
 * The ref counts are per-cpu so we use a local_t type.
 */
static DEFINE_PER_CPU(int, mde_ref_count);
static DEFINE_PER_CPU(int, kde_ref_count);

void enable_debug_monitors(enum debug_el el)
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

void disable_debug_monitors(enum debug_el el)
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

/*
 * OS lock clearing.
 */
static void clear_os_lock(void *unused)
{
	asm volatile("msr oslar_el1, %0" : : "r" (0));
	isb();
}

static int os_lock_notify(struct notifier_block *self,
				    unsigned long action, void *data)
{
	int cpu = (unsigned long)data;
	if (action == CPU_ONLINE)
		smp_call_function_single(cpu, clear_os_lock, NULL, 1);
	return NOTIFY_OK;
}

static struct notifier_block os_lock_nb = {
	.notifier_call = os_lock_notify,
};

static int debug_monitors_init(void)
{
	/* Clear the OS lock. */
	smp_call_function(clear_os_lock, NULL, 1);
	clear_os_lock(NULL);

	/* Register hotplug handler. */
	register_cpu_notifier(&os_lock_nb);
	return 0;
}
postcore_initcall(debug_monitors_init);

/*
 * Single step API and exception handling.
 */
static void set_regs_spsr_ss(struct pt_regs *regs)
{
	unsigned long spsr;

	spsr = regs->pstate;
	spsr &= ~DBG_SPSR_SS;
	spsr |= DBG_SPSR_SS;
	regs->pstate = spsr;
}

static void clear_regs_spsr_ss(struct pt_regs *regs)
{
	unsigned long spsr;

	spsr = regs->pstate;
	spsr &= ~DBG_SPSR_SS;
	regs->pstate = spsr;
}

static int single_step_handler(unsigned long addr, unsigned int esr,
			       struct pt_regs *regs)
{
	siginfo_t info;

	/*
	 * If we are stepping a pending breakpoint, call the hw_breakpoint
	 * handler first.
	 */
	if (!reinstall_suspended_bps(regs))
		return 0;

	if (user_mode(regs)) {
		info.si_signo = SIGTRAP;
		info.si_errno = 0;
		info.si_code  = TRAP_HWBKPT;
		info.si_addr  = (void __user *)instruction_pointer(regs);
		force_sig_info(SIGTRAP, &info, current);

		/*
		 * ptrace will disable single step unless explicitly
		 * asked to re-enable it. For other clients, it makes
		 * sense to leave it enabled (i.e. rewind the controls
		 * to the active-not-pending state).
		 */
		user_rewind_single_step(current);
	} else {
		/* TODO: route to KGDB */
		pr_warning("Unexpected kernel single-step exception at EL1\n");
		/*
		 * Re-enable stepping since we know that we will be
		 * returning to regs.
		 */
		set_regs_spsr_ss(regs);
	}

	return 0;
}

static int brk_handler(unsigned long addr, unsigned int esr,
		       struct pt_regs *regs)
{
	siginfo_t info;

	if (!user_mode(regs))
		return -EFAULT;

	info = (siginfo_t) {
		.si_signo = SIGTRAP,
		.si_errno = 0,
		.si_code  = TRAP_BRKPT,
		.si_addr  = (void __user *)instruction_pointer(regs),
	};

	force_sig_info(SIGTRAP, &info, current);
	return 0;
}

int aarch32_break_handler(struct pt_regs *regs)
{
	siginfo_t info;
	u32 arm_instr;
	u16 thumb_instr;
	bool bp = false;
	void __user *pc = (void __user *)instruction_pointer(regs);

	if (!compat_user_mode(regs))
		return -EFAULT;

	if (compat_thumb_mode(regs)) {
		/* get 16-bit Thumb instruction */
		get_user(thumb_instr, (u16 __user *)pc);
		thumb_instr = le16_to_cpu(thumb_instr);
		if (thumb_instr == AARCH32_BREAK_THUMB2_LO) {
			/* get second half of 32-bit Thumb-2 instruction */
			get_user(thumb_instr, (u16 __user *)(pc + 2));
			thumb_instr = le16_to_cpu(thumb_instr);
			bp = thumb_instr == AARCH32_BREAK_THUMB2_HI;
		} else {
			bp = thumb_instr == AARCH32_BREAK_THUMB;
		}
	} else {
		/* 32-bit ARM instruction */
		get_user(arm_instr, (u32 __user *)pc);
		arm_instr = le32_to_cpu(arm_instr);
		bp = (arm_instr & ~0xf0000000) == AARCH32_BREAK_ARM;
	}

	if (!bp)
		return -EFAULT;

	info = (siginfo_t) {
		.si_signo = SIGTRAP,
		.si_errno = 0,
		.si_code  = TRAP_BRKPT,
		.si_addr  = pc,
	};

	force_sig_info(SIGTRAP, &info, current);
	return 0;
}

static int __init debug_traps_init(void)
{
	hook_debug_fault_code(DBG_ESR_EVT_HWSS, single_step_handler, SIGTRAP,
			      TRAP_HWBKPT, "single-step handler");
	hook_debug_fault_code(DBG_ESR_EVT_BRK, brk_handler, SIGTRAP,
			      TRAP_BRKPT, "ptrace BRK handler");
	return 0;
}
arch_initcall(debug_traps_init);

/* Re-enable single step for syscall restarting. */
void user_rewind_single_step(struct task_struct *task)
{
	/*
	 * If single step is active for this thread, then set SPSR.SS
	 * to 1 to avoid returning to the active-pending state.
	 */
	if (test_ti_thread_flag(task_thread_info(task), TIF_SINGLESTEP))
		set_regs_spsr_ss(task_pt_regs(task));
}

void user_fastforward_single_step(struct task_struct *task)
{
	if (test_ti_thread_flag(task_thread_info(task), TIF_SINGLESTEP))
		clear_regs_spsr_ss(task_pt_regs(task));
}

/* Kernel API */
void kernel_enable_single_step(struct pt_regs *regs)
{
	WARN_ON(!irqs_disabled());
	set_regs_spsr_ss(regs);
	mdscr_write(mdscr_read() | DBG_MDSCR_SS);
	enable_debug_monitors(DBG_ACTIVE_EL1);
}

void kernel_disable_single_step(void)
{
	WARN_ON(!irqs_disabled());
	mdscr_write(mdscr_read() & ~DBG_MDSCR_SS);
	disable_debug_monitors(DBG_ACTIVE_EL1);
}

int kernel_active_single_step(void)
{
	WARN_ON(!irqs_disabled());
	return mdscr_read() & DBG_MDSCR_SS;
}

/* ptrace API */
void user_enable_single_step(struct task_struct *task)
{
	set_ti_thread_flag(task_thread_info(task), TIF_SINGLESTEP);
	set_regs_spsr_ss(task_pt_regs(task));
}

void user_disable_single_step(struct task_struct *task)
{
	clear_ti_thread_flag(task_thread_info(task), TIF_SINGLESTEP);
}
