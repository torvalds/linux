// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 SiFive
 */

#include <linux/acpi.h>
#include <linux/of.h>
#include <linux/prctl.h>
#include <asm/acpi.h>
#include <asm/cacheflush.h>

#ifdef CONFIG_SMP

#include <asm/sbi.h>

static void ipi_remote_fence_i(void *info)
{
	return local_flush_icache_all();
}

void flush_icache_all(void)
{
	local_flush_icache_all();

	if (num_online_cpus() < 2)
		return;
	else if (riscv_use_sbi_for_rfence())
		sbi_remote_fence_i(NULL);
	else
		on_each_cpu(ipi_remote_fence_i, NULL, 1);
}
EXPORT_SYMBOL(flush_icache_all);

/*
 * Performs an icache flush for the given MM context.  RISC-V has no direct
 * mechanism for instruction cache shoot downs, so instead we send an IPI that
 * informs the remote harts they need to flush their local instruction caches.
 * To avoid pathologically slow behavior in a common case (a bunch of
 * single-hart processes on a many-hart machine, ie 'make -j') we avoid the
 * IPIs for harts that are not currently executing a MM context and instead
 * schedule a deferred local instruction cache flush to be performed before
 * execution resumes on each hart.
 */
void flush_icache_mm(struct mm_struct *mm, bool local)
{
	unsigned int cpu;
	cpumask_t others, *mask;

	preempt_disable();

	/* Mark every hart's icache as needing a flush for this MM. */
	mask = &mm->context.icache_stale_mask;
	cpumask_setall(mask);
	/* Flush this hart's I$ now, and mark it as flushed. */
	cpu = smp_processor_id();
	cpumask_clear_cpu(cpu, mask);
	local_flush_icache_all();

	/*
	 * Flush the I$ of other harts concurrently executing, and mark them as
	 * flushed.
	 */
	cpumask_andnot(&others, mm_cpumask(mm), cpumask_of(cpu));
	local |= cpumask_empty(&others);
	if (mm == current->active_mm && local) {
		/*
		 * It's assumed that at least one strongly ordered operation is
		 * performed on this hart between setting a hart's cpumask bit
		 * and scheduling this MM context on that hart.  Sending an SBI
		 * remote message will do this, but in the case where no
		 * messages are sent we still need to order this hart's writes
		 * with flush_icache_deferred().
		 */
		smp_mb();
	} else if (riscv_use_sbi_for_rfence()) {
		sbi_remote_fence_i(&others);
	} else {
		on_each_cpu_mask(&others, ipi_remote_fence_i, NULL, 1);
	}

	preempt_enable();
}

#endif /* CONFIG_SMP */

#ifdef CONFIG_MMU
void flush_icache_pte(struct mm_struct *mm, pte_t pte)
{
	struct folio *folio = page_folio(pte_page(pte));

	if (!test_bit(PG_dcache_clean, &folio->flags)) {
		flush_icache_mm(mm, false);
		set_bit(PG_dcache_clean, &folio->flags);
	}
}
#endif /* CONFIG_MMU */

unsigned int riscv_cbom_block_size;
EXPORT_SYMBOL_GPL(riscv_cbom_block_size);

unsigned int riscv_cboz_block_size;
EXPORT_SYMBOL_GPL(riscv_cboz_block_size);

static void __init cbo_get_block_size(struct device_node *node,
				      const char *name, u32 *block_size,
				      unsigned long *first_hartid)
{
	unsigned long hartid;
	u32 val;

	if (riscv_of_processor_hartid(node, &hartid))
		return;

	if (of_property_read_u32(node, name, &val))
		return;

	if (!*block_size) {
		*block_size = val;
		*first_hartid = hartid;
	} else if (*block_size != val) {
		pr_warn("%s mismatched between harts %lu and %lu\n",
			name, *first_hartid, hartid);
	}
}

void __init riscv_init_cbo_blocksizes(void)
{
	unsigned long cbom_hartid, cboz_hartid;
	u32 cbom_block_size = 0, cboz_block_size = 0;
	struct device_node *node;
	struct acpi_table_header *rhct;
	acpi_status status;

	if (acpi_disabled) {
		for_each_of_cpu_node(node) {
			/* set block-size for cbom and/or cboz extension if available */
			cbo_get_block_size(node, "riscv,cbom-block-size",
					   &cbom_block_size, &cbom_hartid);
			cbo_get_block_size(node, "riscv,cboz-block-size",
					   &cboz_block_size, &cboz_hartid);
		}
	} else {
		status = acpi_get_table(ACPI_SIG_RHCT, 0, &rhct);
		if (ACPI_FAILURE(status))
			return;

		acpi_get_cbo_block_size(rhct, &cbom_block_size, &cboz_block_size, NULL);
		acpi_put_table((struct acpi_table_header *)rhct);
	}

	if (cbom_block_size)
		riscv_cbom_block_size = cbom_block_size;

	if (cboz_block_size)
		riscv_cboz_block_size = cboz_block_size;
}

#ifdef CONFIG_SMP
static void set_icache_stale_mask(void)
{
	int cpu = get_cpu();
	cpumask_t *mask;
	bool stale_cpu;

	/*
	 * Mark every other hart's icache as needing a flush for
	 * this MM. Maintain the previous value of the current
	 * cpu to handle the case when this function is called
	 * concurrently on different harts.
	 */
	mask = &current->mm->context.icache_stale_mask;
	stale_cpu = cpumask_test_cpu(cpu, mask);

	cpumask_setall(mask);
	cpumask_assign_cpu(cpu, mask, stale_cpu);
	put_cpu();
}
#endif

/**
 * riscv_set_icache_flush_ctx() - Enable/disable icache flushing instructions in
 * userspace.
 * @ctx: Set the type of icache flushing instructions permitted/prohibited in
 *	 userspace. Supported values described below.
 *
 * Supported values for ctx:
 *
 * * %PR_RISCV_CTX_SW_FENCEI_ON: Allow fence.i in user space.
 *
 * * %PR_RISCV_CTX_SW_FENCEI_OFF: Disallow fence.i in user space. All threads in
 *   a process will be affected when ``scope == PR_RISCV_SCOPE_PER_PROCESS``.
 *   Therefore, caution must be taken; use this flag only when you can guarantee
 *   that no thread in the process will emit fence.i from this point onward.
 *
 * @scope: Set scope of where icache flushing instructions are allowed to be
 *	   emitted. Supported values described below.
 *
 * Supported values for scope:
 *
 * * %PR_RISCV_SCOPE_PER_PROCESS: Ensure the icache of any thread in this process
 *                               is coherent with instruction storage upon
 *                               migration.
 *
 * * %PR_RISCV_SCOPE_PER_THREAD: Ensure the icache of the current thread is
 *                              coherent with instruction storage upon
 *                              migration.
 *
 * When ``scope == PR_RISCV_SCOPE_PER_PROCESS``, all threads in the process are
 * permitted to emit icache flushing instructions. Whenever any thread in the
 * process is migrated, the corresponding hart's icache will be guaranteed to be
 * consistent with instruction storage. This does not enforce any guarantees
 * outside of migration. If a thread modifies an instruction that another thread
 * may attempt to execute, the other thread must still emit an icache flushing
 * instruction before attempting to execute the potentially modified
 * instruction. This must be performed by the user-space program.
 *
 * In per-thread context (eg. ``scope == PR_RISCV_SCOPE_PER_THREAD``) only the
 * thread calling this function is permitted to emit icache flushing
 * instructions. When the thread is migrated, the corresponding hart's icache
 * will be guaranteed to be consistent with instruction storage.
 *
 * On kernels configured without SMP, this function is a nop as migrations
 * across harts will not occur.
 */
int riscv_set_icache_flush_ctx(unsigned long ctx, unsigned long scope)
{
#ifdef CONFIG_SMP
	switch (ctx) {
	case PR_RISCV_CTX_SW_FENCEI_ON:
		switch (scope) {
		case PR_RISCV_SCOPE_PER_PROCESS:
			current->mm->context.force_icache_flush = true;
			break;
		case PR_RISCV_SCOPE_PER_THREAD:
			current->thread.force_icache_flush = true;
			break;
		default:
			return -EINVAL;
		}
		break;
	case PR_RISCV_CTX_SW_FENCEI_OFF:
		switch (scope) {
		case PR_RISCV_SCOPE_PER_PROCESS:
			set_icache_stale_mask();
			current->mm->context.force_icache_flush = false;
			break;
		case PR_RISCV_SCOPE_PER_THREAD:
			set_icache_stale_mask();
			current->thread.force_icache_flush = false;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
#else
	switch (ctx) {
	case PR_RISCV_CTX_SW_FENCEI_ON:
	case PR_RISCV_CTX_SW_FENCEI_OFF:
		return 0;
	default:
		return -EINVAL;
	}
#endif
}
