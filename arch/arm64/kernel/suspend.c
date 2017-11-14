// SPDX-License-Identifier: GPL-2.0
#include <linux/ftrace.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <asm/alternative.h>
#include <asm/cacheflush.h>
#include <asm/cpufeature.h>
#include <asm/debug-monitors.h>
#include <asm/exec.h>
#include <asm/pgtable.h>
#include <asm/memory.h>
#include <asm/mmu_context.h>
#include <asm/smp_plat.h>
#include <asm/suspend.h>
#include <asm/tlbflush.h>

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

	/*
	 * PSTATE was not saved over suspend/resume, re-enable any detected
	 * features that might not have been set correctly.
	 */
	asm(ALTERNATIVE("nop", SET_PSTATE_PAN(1), ARM64_HAS_PAN,
			CONFIG_ARM64_PAN));
	uao_thread_switch(current);

	/*
	 * Restore HW breakpoint registers to sane values
	 * before debug exceptions are possibly reenabled
	 * through local_dbg_restore.
	 */
	if (hw_breakpoint_restore)
		hw_breakpoint_restore(cpu);
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

	/*
	 * From this point debug exceptions are disabled to prevent
	 * updates to mdscr register (saved and restored along with
	 * general purpose registers) from kernel debuggers.
	 */
	local_dbg_save(flags);

	/*
	 * Function graph tracer state gets incosistent when the kernel
	 * calls functions that never return (aka suspend finishers) hence
	 * disable graph tracing during their execution.
	 */
	pause_graph_tracing();

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
		__cpu_suspend_exit();
	}

	unpause_graph_tracing();

	/*
	 * Restore pstate flags. OS lock and mdscr have been already
	 * restored, so from this point onwards, debugging is fully
	 * renabled if it was enabled when core started shutdown.
	 */
	local_dbg_restore(flags);

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
