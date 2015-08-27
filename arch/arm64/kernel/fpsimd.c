/*
 * FP/SIMD context switching and fault handling
 *
 * Copyright (C) 2012 ARM Ltd.
 * Author: Catalin Marinas <catalin.marinas@arm.com>
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
 */

#include <linux/cpu_pm.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/hardirq.h>

#include <asm/fpsimd.h>
#include <asm/cputype.h>

#define FPEXC_IOF	(1 << 0)
#define FPEXC_DZF	(1 << 1)
#define FPEXC_OFF	(1 << 2)
#define FPEXC_UFF	(1 << 3)
#define FPEXC_IXF	(1 << 4)
#define FPEXC_IDF	(1 << 7)

/*
 * In order to reduce the number of times the FPSIMD state is needlessly saved
 * and restored, we need to keep track of two things:
 * (a) for each task, we need to remember which CPU was the last one to have
 *     the task's FPSIMD state loaded into its FPSIMD registers;
 * (b) for each CPU, we need to remember which task's userland FPSIMD state has
 *     been loaded into its FPSIMD registers most recently, or whether it has
 *     been used to perform kernel mode NEON in the meantime.
 *
 * For (a), we add a 'cpu' field to struct fpsimd_state, which gets updated to
 * the id of the current CPU everytime the state is loaded onto a CPU. For (b),
 * we add the per-cpu variable 'fpsimd_last_state' (below), which contains the
 * address of the userland FPSIMD state of the task that was loaded onto the CPU
 * the most recently, or NULL if kernel mode NEON has been performed after that.
 *
 * With this in place, we no longer have to restore the next FPSIMD state right
 * when switching between tasks. Instead, we can defer this check to userland
 * resume, at which time we verify whether the CPU's fpsimd_last_state and the
 * task's fpsimd_state.cpu are still mutually in sync. If this is the case, we
 * can omit the FPSIMD restore.
 *
 * As an optimization, we use the thread_info flag TIF_FOREIGN_FPSTATE to
 * indicate whether or not the userland FPSIMD state of the current task is
 * present in the registers. The flag is set unless the FPSIMD registers of this
 * CPU currently contain the most recent userland FPSIMD state of the current
 * task.
 *
 * For a certain task, the sequence may look something like this:
 * - the task gets scheduled in; if both the task's fpsimd_state.cpu field
 *   contains the id of the current CPU, and the CPU's fpsimd_last_state per-cpu
 *   variable points to the task's fpsimd_state, the TIF_FOREIGN_FPSTATE flag is
 *   cleared, otherwise it is set;
 *
 * - the task returns to userland; if TIF_FOREIGN_FPSTATE is set, the task's
 *   userland FPSIMD state is copied from memory to the registers, the task's
 *   fpsimd_state.cpu field is set to the id of the current CPU, the current
 *   CPU's fpsimd_last_state pointer is set to this task's fpsimd_state and the
 *   TIF_FOREIGN_FPSTATE flag is cleared;
 *
 * - the task executes an ordinary syscall; upon return to userland, the
 *   TIF_FOREIGN_FPSTATE flag will still be cleared, so no FPSIMD state is
 *   restored;
 *
 * - the task executes a syscall which executes some NEON instructions; this is
 *   preceded by a call to kernel_neon_begin(), which copies the task's FPSIMD
 *   register contents to memory, clears the fpsimd_last_state per-cpu variable
 *   and sets the TIF_FOREIGN_FPSTATE flag;
 *
 * - the task gets preempted after kernel_neon_end() is called; as we have not
 *   returned from the 2nd syscall yet, TIF_FOREIGN_FPSTATE is still set so
 *   whatever is in the FPSIMD registers is not saved to memory, but discarded.
 */
static DEFINE_PER_CPU(struct fpsimd_state *, fpsimd_last_state);

/*
 * Trapped FP/ASIMD access.
 */
void do_fpsimd_acc(unsigned int esr, struct pt_regs *regs)
{
	/* TODO: implement lazy context saving/restoring */
	WARN_ON(1);
}

/*
 * Raise a SIGFPE for the current process.
 */
void do_fpsimd_exc(unsigned int esr, struct pt_regs *regs)
{
	siginfo_t info;
	unsigned int si_code = 0;

	if (esr & FPEXC_IOF)
		si_code = FPE_FLTINV;
	else if (esr & FPEXC_DZF)
		si_code = FPE_FLTDIV;
	else if (esr & FPEXC_OFF)
		si_code = FPE_FLTOVF;
	else if (esr & FPEXC_UFF)
		si_code = FPE_FLTUND;
	else if (esr & FPEXC_IXF)
		si_code = FPE_FLTRES;

	memset(&info, 0, sizeof(info));
	info.si_signo = SIGFPE;
	info.si_code = si_code;
	info.si_addr = (void __user *)instruction_pointer(regs);

	send_sig_info(SIGFPE, &info, current);
}

void fpsimd_thread_switch(struct task_struct *next)
{
	/*
	 * Save the current FPSIMD state to memory, but only if whatever is in
	 * the registers is in fact the most recent userland FPSIMD state of
	 * 'current'.
	 */
	if (current->mm && !test_thread_flag(TIF_FOREIGN_FPSTATE))
		fpsimd_save_state(&current->thread.fpsimd_state);

	if (next->mm) {
		/*
		 * If we are switching to a task whose most recent userland
		 * FPSIMD state is already in the registers of *this* cpu,
		 * we can skip loading the state from memory. Otherwise, set
		 * the TIF_FOREIGN_FPSTATE flag so the state will be loaded
		 * upon the next return to userland.
		 */
		struct fpsimd_state *st = &next->thread.fpsimd_state;

		if (__this_cpu_read(fpsimd_last_state) == st
		    && st->cpu == smp_processor_id())
			clear_ti_thread_flag(task_thread_info(next),
					     TIF_FOREIGN_FPSTATE);
		else
			set_ti_thread_flag(task_thread_info(next),
					   TIF_FOREIGN_FPSTATE);
	}
}

void fpsimd_flush_thread(void)
{
	memset(&current->thread.fpsimd_state, 0, sizeof(struct fpsimd_state));
	fpsimd_flush_task_state(current);
	set_thread_flag(TIF_FOREIGN_FPSTATE);
}

/*
 * Save the userland FPSIMD state of 'current' to memory, but only if the state
 * currently held in the registers does in fact belong to 'current'
 */
void fpsimd_preserve_current_state(void)
{
	preempt_disable();
	if (!test_thread_flag(TIF_FOREIGN_FPSTATE))
		fpsimd_save_state(&current->thread.fpsimd_state);
	preempt_enable();
}

/*
 * Load the userland FPSIMD state of 'current' from memory, but only if the
 * FPSIMD state already held in the registers is /not/ the most recent FPSIMD
 * state of 'current'
 */
void fpsimd_restore_current_state(void)
{
	preempt_disable();
	if (test_and_clear_thread_flag(TIF_FOREIGN_FPSTATE)) {
		struct fpsimd_state *st = &current->thread.fpsimd_state;

		fpsimd_load_state(st);
		this_cpu_write(fpsimd_last_state, st);
		st->cpu = smp_processor_id();
	}
	preempt_enable();
}

/*
 * Load an updated userland FPSIMD state for 'current' from memory and set the
 * flag that indicates that the FPSIMD register contents are the most recent
 * FPSIMD state of 'current'
 */
void fpsimd_update_current_state(struct fpsimd_state *state)
{
	preempt_disable();
	fpsimd_load_state(state);
	if (test_and_clear_thread_flag(TIF_FOREIGN_FPSTATE)) {
		struct fpsimd_state *st = &current->thread.fpsimd_state;

		this_cpu_write(fpsimd_last_state, st);
		st->cpu = smp_processor_id();
	}
	preempt_enable();
}

/*
 * Invalidate live CPU copies of task t's FPSIMD state
 */
void fpsimd_flush_task_state(struct task_struct *t)
{
	t->thread.fpsimd_state.cpu = NR_CPUS;
}

#ifdef CONFIG_KERNEL_MODE_NEON

static DEFINE_PER_CPU(struct fpsimd_partial_state, hardirq_fpsimdstate);
static DEFINE_PER_CPU(struct fpsimd_partial_state, softirq_fpsimdstate);

/*
 * Kernel-side NEON support functions
 */
void kernel_neon_begin_partial(u32 num_regs)
{
	if (in_interrupt()) {
		struct fpsimd_partial_state *s = this_cpu_ptr(
			in_irq() ? &hardirq_fpsimdstate : &softirq_fpsimdstate);

		BUG_ON(num_regs > 32);
		fpsimd_save_partial_state(s, roundup(num_regs, 2));
	} else {
		/*
		 * Save the userland FPSIMD state if we have one and if we
		 * haven't done so already. Clear fpsimd_last_state to indicate
		 * that there is no longer userland FPSIMD state in the
		 * registers.
		 */
		preempt_disable();
		if (current->mm &&
		    !test_and_set_thread_flag(TIF_FOREIGN_FPSTATE))
			fpsimd_save_state(&current->thread.fpsimd_state);
		this_cpu_write(fpsimd_last_state, NULL);
	}
}
EXPORT_SYMBOL(kernel_neon_begin_partial);

void kernel_neon_end(void)
{
	if (in_interrupt()) {
		struct fpsimd_partial_state *s = this_cpu_ptr(
			in_irq() ? &hardirq_fpsimdstate : &softirq_fpsimdstate);
		fpsimd_load_partial_state(s);
	} else {
		preempt_enable();
	}
}
EXPORT_SYMBOL(kernel_neon_end);

#endif /* CONFIG_KERNEL_MODE_NEON */

#ifdef CONFIG_CPU_PM
static int fpsimd_cpu_pm_notifier(struct notifier_block *self,
				  unsigned long cmd, void *v)
{
	switch (cmd) {
	case CPU_PM_ENTER:
		if (current->mm && !test_thread_flag(TIF_FOREIGN_FPSTATE))
			fpsimd_save_state(&current->thread.fpsimd_state);
		this_cpu_write(fpsimd_last_state, NULL);
		break;
	case CPU_PM_EXIT:
		if (current->mm)
			set_thread_flag(TIF_FOREIGN_FPSTATE);
		break;
	case CPU_PM_ENTER_FAILED:
	default:
		return NOTIFY_DONE;
	}
	return NOTIFY_OK;
}

static struct notifier_block fpsimd_cpu_pm_notifier_block = {
	.notifier_call = fpsimd_cpu_pm_notifier,
};

static void fpsimd_pm_init(void)
{
	cpu_pm_register_notifier(&fpsimd_cpu_pm_notifier_block);
}

#else
static inline void fpsimd_pm_init(void) { }
#endif /* CONFIG_CPU_PM */

/*
 * FP/SIMD support code initialisation.
 */
static int __init fpsimd_init(void)
{
	u64 pfr = read_cpuid(ID_AA64PFR0_EL1);

	if (pfr & (0xf << 16)) {
		pr_notice("Floating-point is not implemented\n");
		return 0;
	}
	elf_hwcap |= HWCAP_FP;

	if (pfr & (0xf << 20))
		pr_notice("Advanced SIMD is not implemented\n");
	else
		elf_hwcap |= HWCAP_ASIMD;

	fpsimd_pm_init();

	return 0;
}
late_initcall(fpsimd_init);
