/*
 *  linux/arch/arm/kernel/pmu.c
 *
 *  Copyright (C) 2009 picoChip Designs Ltd, Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/cpumask.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <asm/pmu.h>

/*
 * Define the IRQs for the system. We could use something like a platform
 * device but that seems fairly heavyweight for this. Also, the performance
 * counters can't be removed or hotplugged.
 *
 * Ordering is important: init_pmu() will use the ordering to set the affinity
 * to the corresponding core. e.g. the first interrupt will go to cpu 0, the
 * second goes to cpu 1 etc.
 */
static const int irqs[] = {
#if defined(CONFIG_ARCH_OMAP2)
	3,
#elif defined(CONFIG_ARCH_BCMRING)
	IRQ_PMUIRQ,
#elif defined(CONFIG_MACH_REALVIEW_EB)
	IRQ_EB11MP_PMU_CPU0,
	IRQ_EB11MP_PMU_CPU1,
	IRQ_EB11MP_PMU_CPU2,
	IRQ_EB11MP_PMU_CPU3,
#elif defined(CONFIG_ARCH_OMAP3)
	INT_34XX_BENCH_MPU_EMUL,
#elif defined(CONFIG_ARCH_IOP32X)
	IRQ_IOP32X_CORE_PMU,
#elif defined(CONFIG_ARCH_IOP33X)
	IRQ_IOP33X_CORE_PMU,
#elif defined(CONFIG_ARCH_PXA)
	IRQ_PMU,
#endif
};

static const struct pmu_irqs pmu_irqs = {
	.irqs	    = irqs,
	.num_irqs   = ARRAY_SIZE(irqs),
};

static volatile long pmu_lock;

const struct pmu_irqs *
reserve_pmu(void)
{
	return test_and_set_bit_lock(0, &pmu_lock) ? ERR_PTR(-EBUSY) :
		&pmu_irqs;
}
EXPORT_SYMBOL_GPL(reserve_pmu);

int
release_pmu(const struct pmu_irqs *irqs)
{
	if (WARN_ON(irqs != &pmu_irqs))
		return -EINVAL;
	clear_bit_unlock(0, &pmu_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(release_pmu);

static int
set_irq_affinity(int irq,
		 unsigned int cpu)
{
#ifdef CONFIG_SMP
	int err = irq_set_affinity(irq, cpumask_of(cpu));
	if (err)
		pr_warning("unable to set irq affinity (irq=%d, cpu=%u)\n",
			   irq, cpu);
	return err;
#else
	return 0;
#endif
}

int
init_pmu(void)
{
	int i, err = 0;

	for (i = 0; i < pmu_irqs.num_irqs; ++i) {
		err = set_irq_affinity(pmu_irqs.irqs[i], i);
		if (err)
			break;
	}

	return err;
}
EXPORT_SYMBOL_GPL(init_pmu);
