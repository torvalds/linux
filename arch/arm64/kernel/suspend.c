// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/pgtable.h>
#include <asm/alternative.h>
#include <asm/cacheflush.h>
#include <asm/cpufeature.h>
#include <asm/cpuidle.h>
#include <asm/daifflags.h>
#include <asm/debug-monitors.h>
#include <asm/exec.h>
#include <asm/mte.h>
#include <asm/memory.h>
#include <asm/mmu_context.h>
#include <asm/smp_plat.h>
#include <asm/suspend.h>

/*
 * This is allocated by cpu_suspend_init(), and used to store a pointer to
 * the 'struct sleep_stack_data' the contains a particular CPUs state.
 */
unsigned long *sleep_save_stash;

/*
 * This hook is provided so that cpu_suspend code can restore HW
 * breakpoints as early as possible in the resume path, before reenabling
 * debug exceptions. Code cannot be run from a CPU PM notifier since by the
 * time the notifier runs debug exceptions might have been enabled already,
 * with HW breakpoints registers content still in an unknown state.
 */
static int (*hw_breakpoint_restore)(unsigned int);
void __init cpu_suspend_set_dbg_restorer(int (*hw_bp_restore)(unsigned int))
{
	/* Prevent multiple restore hook initializations */
	if (WARN_ON(hw_breakpoint_restore))
		return;
	hw_breakpoint_restore = hw_bp_restore;
}

void notrace __cpu_suspend_exit(void)
{
	unsigned int cpu = smp_processor_id();

	/*
	 * We are resuming from reset with the idmap active in TTBR0_EL1.
	 * We must uninstall the idmap and restore the expected MMU
	 * state before we can possibly return to userspace.
	 */
	cpu_uninstall_idmap();

	/* Restore CnP bit in TTBR1_EL1 */
	if (system_supports_cnp())
		cpu_replace_ttbr1(lm_alias(swapper_pg_dir), idmap_pg_dir);

	/*
	 * PSTATE was not saved over suspend/resume, re-enable any detected
	 * features that might not have been set correctly.
	 */
	__uaccess_enable_hw_pan();

	/*
	 * Restore HW breakpoint registers to sane values
	 * before debug exceptions are possibly reenabled
	 * by cpu_suspend()s local_daif_restore() call.
	 */
	if (hw_breakpoint_restore)
		hw_breakpoint_restore(cpu);

	/*
	 * On resume, firmware implementing dynamic mitigation will
	 * have turned the mitigation on. If the user has forcefully
	 * disabled it, make sure their wishes are obeyed.
	 */
	spectre_v4_enable_mitigation(NULL);

	/* Restore additional feature-specific configuration */
	ptrauth_suspend_exit();
}

/*
 * cpu_suspend
 *
 * arg: argument to pass to the finisher function
 * fn: finisher function pointer
 *
 */
int cpu_suspend(unsigned long arg, int (*fn)(unsigned long))
{
	int ret = 0;
	unsigned long flags;
	struct sleep_stack_data state;
	struct arm_cpuidle_irq_context context;

	/* Report any MTE async fault before going to suspend */
	mte_suspend_enter();

	/*
	 * From this point debug exceptions are disabled to prevent
	 * updates to mdscr register (saved and restored along with
	 * general purpose registers) from kernel debuggers.
	 */
	flags = local_daif_save();

	/*
	 * Function graph tracer state gets inconsistent when the kernel
	 * calls functions that never return (aka suspend finishers) hence
	 * disable graph tracing during their execution.
	 */
	pause_graph_tracing();

	/*
	 * Switch to using DAIF.IF instead of PMR in order to reliably
	 * resume if we're using pseudo-NMIs.
	 */
	arm_cpuidle_save_irq_context(&context);

	if (__cpu_suspend_enter(&state)) {
		/* Call the suspend finisher */
		ret = fn(arg);

		/*
		 * Never gets here, unless the suspend finisher fails.
		 * Successful cpu_suspend() should return from cpu_resume(),
		 * returning through this code path is considered an error
		 * If the return value is set to 0 force ret = -EOPNOTSUPP
		 * to make sure a proper error condition is propagated
		 */
		if (!ret)
			ret = -EOPNOTSUPP;
	} else {
		RCU_NONIDLE(__cpu_suspend_exit());
	}

	arm_cpuidle_restore_irq_context(&context);

	unpause_graph_tracing();

	/*
	 * Restore pstate flags. OS lock and mdscr have been already
	 * restored, so from this point onwards, debugging is fully
	 * reenabled if it was enabled when core started shutdown.
	 */
	local_daif_restore(flags);

	return ret;
}

static int __init cpu_suspend_init(void)
{
	/* ctx_ptr is an array of physical addresses */
	sleep_save_stash = kcalloc(mpidr_hash_size(), sizeof(*sleep_save_stash),
				   GFP_KERNEL);

	if (WARN_ON(!sleep_save_stash))
		return -ENOMEM;

	return 0;
}
early_initcall(cpu_suspend_init);
