#include <linux/ftrace.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <asm/cacheflush.h>
#include <asm/debug-monitors.h>
#include <asm/pgtable.h>
#include <asm/memory.h>
#include <asm/mmu_context.h>
#include <asm/smp_plat.h>
#include <asm/suspend.h>
#include <asm/tlbflush.h>


/*
 * This is called by __cpu_suspend_enter() to save the state, and do whatever
 * flushing is required to ensure that when the CPU goes to sleep we have
 * the necessary data available when the caches are not searched.
 *
 * ptr: sleep_stack_data containing cpu state virtual address.
 * save_ptr: address of the location where the context physical address
 *           must be saved
 */
void notrace __cpu_suspend_save(struct sleep_stack_data *ptr,
				phys_addr_t *save_ptr)
{
	*save_ptr = virt_to_phys(ptr);

	cpu_do_suspend(&ptr->system_regs);
	/*
	 * Only flush the context that must be retrieved with the MMU
	 * off. VA primitives ensure the flush is applied to all
	 * cache levels so context is pushed to DRAM.
	 */
	__flush_dcache_area(ptr, sizeof(*ptr));
	__flush_dcache_area(save_ptr, sizeof(*save_ptr));
}

/*
 * This hook is provided so that cpu_suspend code can restore HW
 * breakpoints as early as possible in the resume path, before reenabling
 * debug exceptions. Code cannot be run from a CPU PM notifier since by the
 * time the notifier runs debug exceptions might have been enabled already,
 * with HW breakpoints registers content still in an unknown state.
 */
static void (*hw_breakpoint_restore)(void *);
void __init cpu_suspend_set_dbg_restorer(void (*hw_bp_restore)(void *))
{
	/* Prevent multiple restore hook initializations */
	if (WARN_ON(hw_breakpoint_restore))
		return;
	hw_breakpoint_restore = hw_bp_restore;
}

void notrace __cpu_suspend_exit(void)
{
	/*
	 * We are resuming from reset with the idmap active in TTBR0_EL1.
	 * We must uninstall the idmap and restore the expected MMU
	 * state before we can possibly return to userspace.
	 */
	cpu_uninstall_idmap();

	/*
	 * Restore per-cpu offset before any kernel
	 * subsystem relying on it has a chance to run.
	 */
	set_my_cpu_offset(per_cpu_offset(smp_processor_id()));

	/*
	 * Restore HW breakpoint registers to sane values
	 * before debug exceptions are possibly reenabled
	 * through local_dbg_restore.
	 */
	if (hw_breakpoint_restore)
		hw_breakpoint_restore(NULL);
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

struct sleep_save_sp sleep_save_sp;

static int __init cpu_suspend_init(void)
{
	void *ctx_ptr;

	/* ctx_ptr is an array of physical addresses */
	ctx_ptr = kcalloc(mpidr_hash_size(), sizeof(phys_addr_t), GFP_KERNEL);

	if (WARN_ON(!ctx_ptr))
		return -ENOMEM;

	sleep_save_sp.save_ptr_stash = ctx_ptr;
	sleep_save_sp.save_ptr_stash_phys = virt_to_phys(ctx_ptr);
	__flush_dcache_area(&sleep_save_sp, sizeof(struct sleep_save_sp));

	return 0;
}
early_initcall(cpu_suspend_init);
