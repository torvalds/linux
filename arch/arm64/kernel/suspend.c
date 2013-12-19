#include <linux/slab.h>
#include <asm/cacheflush.h>
#include <asm/cpu_ops.h>
#include <asm/debug-monitors.h>
#include <asm/pgtable.h>
#include <asm/memory.h>
#include <asm/smp_plat.h>
#include <asm/suspend.h>
#include <asm/tlbflush.h>

extern int __cpu_suspend(unsigned long);
/*
 * This is called by __cpu_suspend() to save the state, and do whatever
 * flushing is required to ensure that when the CPU goes to sleep we have
 * the necessary data available when the caches are not searched.
 *
 * @arg: Argument to pass to suspend operations
 * @ptr: CPU context virtual address
 * @save_ptr: address of the location where the context physical address
 *            must be saved
 */
int __cpu_suspend_finisher(unsigned long arg, struct cpu_suspend_ctx *ptr,
			   phys_addr_t *save_ptr)
{
	int cpu = smp_processor_id();

	*save_ptr = virt_to_phys(ptr);

	cpu_do_suspend(ptr);
	/*
	 * Only flush the context that must be retrieved with the MMU
	 * off. VA primitives ensure the flush is applied to all
	 * cache levels so context is pushed to DRAM.
	 */
	__flush_dcache_area(ptr, sizeof(*ptr));
	__flush_dcache_area(save_ptr, sizeof(*save_ptr));

	return cpu_ops[cpu]->cpu_suspend(arg);
}

/**
 * cpu_suspend
 *
 * @arg: argument to pass to the finisher function
 */
int cpu_suspend(unsigned long arg)
{
	struct mm_struct *mm = current->active_mm;
	int ret, cpu = smp_processor_id();
	unsigned long flags;

	/*
	 * If cpu_ops have not been registered or suspend
	 * has not been initialized, cpu_suspend call fails early.
	 */
	if (!cpu_ops[cpu] || !cpu_ops[cpu]->cpu_suspend)
		return -EOPNOTSUPP;

	/*
	 * From this point debug exceptions are disabled to prevent
	 * updates to mdscr register (saved and restored along with
	 * general purpose registers) from kernel debuggers.
	 */
	local_dbg_save(flags);

	/*
	 * mm context saved on the stack, it will be restored when
	 * the cpu comes out of reset through the identity mapped
	 * page tables, so that the thread address space is properly
	 * set-up on function return.
	 */
	ret = __cpu_suspend(arg);
	if (ret == 0) {
		cpu_switch_mm(mm->pgd, mm);
		flush_tlb_all();
	}

	/*
	 * Restore pstate flags. OS lock and mdscr have been already
	 * restored, so from this point onwards, debugging is fully
	 * renabled if it was enabled when core started shutdown.
	 */
	local_dbg_restore(flags);

	return ret;
}

extern struct sleep_save_sp sleep_save_sp;
extern phys_addr_t sleep_idmap_phys;

static int cpu_suspend_init(void)
{
	void *ctx_ptr;

	/* ctx_ptr is an array of physical addresses */
	ctx_ptr = kcalloc(mpidr_hash_size(), sizeof(phys_addr_t), GFP_KERNEL);

	if (WARN_ON(!ctx_ptr))
		return -ENOMEM;

	sleep_save_sp.save_ptr_stash = ctx_ptr;
	sleep_save_sp.save_ptr_stash_phys = virt_to_phys(ctx_ptr);
	sleep_idmap_phys = virt_to_phys(idmap_pg_dir);
	__flush_dcache_area(&sleep_save_sp, sizeof(struct sleep_save_sp));
	__flush_dcache_area(&sleep_idmap_phys, sizeof(sleep_idmap_phys));

	return 0;
}
early_initcall(cpu_suspend_init);
