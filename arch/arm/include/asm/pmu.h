/*
 *  linux/arch/arm/include/asm/pmu.h
 *
 *  Copyright (C) 2009 picoChip Designs Ltd, Jamie Iles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __ARM_PMU_H__
#define __ARM_PMU_H__

#ifdef CONFIG_CPU_HAS_PMU

struct pmu_irqs {
	const int   *irqs;
	int	    num_irqs;
};

/**
 * reserve_pmu() - reserve the hardware performance counters
 *
 * Reserve the hardware performance counters in the system for exclusive use.
 * The 'struct pmu_irqs' for the system is returned on success, ERR_PTR()
 * encoded error on failure.
 */
extern const struct pmu_irqs *
reserve_pmu(void);

/**
 * release_pmu() - Relinquish control of the performance counters
 *
 * Release the performance counters and allow someone else to use them.
 * Callers must have disabled the counters and released IRQs before calling
 * this. The 'struct pmu_irqs' returned from reserve_pmu() must be passed as
 * a cookie.
 */
extern int
release_pmu(const struct pmu_irqs *irqs);

/**
 * init_pmu() - Initialise the PMU.
 *
 * Initialise the system ready for PMU enabling. This should typically set the
 * IRQ affinity and nothing else. The users (oprofile/perf events etc) will do
 * the actual hardware initialisation.
 */
extern int
init_pmu(void);

#else /* CONFIG_CPU_HAS_PMU */

static inline const struct pmu_irqs *
reserve_pmu(void)
{
	return ERR_PTR(-ENODEV);
}

static inline int
release_pmu(const struct pmu_irqs *irqs)
{
	return -ENODEV;
}

static inline int
init_pmu(void)
{
	return -ENODEV;
}

#endif /* CONFIG_CPU_HAS_PMU */

#endif /* __ARM_PMU_H__ */
