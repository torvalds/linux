// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 SiFive
 */

#include <linux/of.h>
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

	if (IS_ENABLED(CONFIG_RISCV_SBI) && !riscv_use_ipi_for_rfence())
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
	} else if (IS_ENABLED(CONFIG_RISCV_SBI) &&
		   !riscv_use_ipi_for_rfence()) {
		sbi_remote_fence_i(&others);
	} else {
		on_each_cpu_mask(&others, ipi_remote_fence_i, NULL, 1);
	}

	preempt_enable();
}

#endif /* CONFIG_SMP */

#ifdef CONFIG_MMU
void flush_icache_pte(pte_t pte)
{
	struct page *page = pte_page(pte);

	/*
	 * HugeTLB pages are always fully mapped, so only setting head page's
	 * PG_dcache_clean flag is enough.
	 */
	if (PageHuge(page))
		page = compound_head(page);

	if (!test_bit(PG_dcache_clean, &page->flags)) {
		flush_icache_all();
		set_bit(PG_dcache_clean, &page->flags);
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

	for_each_of_cpu_node(node) {
		/* set block-size for cbom and/or cboz extension if available */
		cbo_get_block_size(node, "riscv,cbom-block-size",
				   &cbom_block_size, &cbom_hartid);
		cbo_get_block_size(node, "riscv,cboz-block-size",
				   &cboz_block_size, &cboz_hartid);
	}

	if (cbom_block_size)
		riscv_cbom_block_size = cbom_block_size;

	if (cboz_block_size)
		riscv_cboz_block_size = cboz_block_size;
}
