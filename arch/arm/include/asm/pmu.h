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

#include <linux/interrupt.h>

/*
 * Types of PMUs that can be accessed directly and require mutual
 * exclusion between profiling tools.
 */
enum arm_pmu_type {
	ARM_PMU_DEVICE_CPU	= 0,
	ARM_NUM_PMU_DEVICES,
};

/*
 * struct arm_pmu_platdata - ARM PMU platform data
 *
 * @handle_irq: an optional handler which will be called from the interrupt and
 * passed the address of the low level handler, and can be used to implement
 * any platform specific handling before or after calling it.
 */
struct arm_pmu_platdata {
	irqreturn_t (*handle_irq)(int irq, void *dev,
				  irq_handler_t pmu_handler);
};

#ifdef CONFIG_CPU_HAS_PMU

/**
 * reserve_pmu() - reserve the hardware performance counters
 *
 * Reserve the hardware performance counters in the system for exclusive use.
 * Returns 0 on success or -EBUSY if the lock is already held.
 */
extern int
reserve_pmu(enum arm_pmu_type type);

/**
 * release_pmu() - Relinquish control of the performance counters
 *
 * Release the performance counters and allow someone else to use them.
 */
extern void
release_pmu(enum arm_pmu_type type);

/**
 * init_pmu() - Initialise the PMU.
 *
 * Initialise the system ready for PMU enabling. This should typically set the
 * IRQ affinity and nothing else. The users (oprofile/perf events etc) will do
 * the actual hardware initialisation.
 */
extern int
init_pmu(enum arm_pmu_type type);

#else /* CONFIG_CPU_HAS_PMU */

#include <linux/err.h>

static inline int
reserve_pmu(enum arm_pmu_type type)
{
	return -ENODEV;
}

static inline void
release_pmu(enum arm_pmu_type type)	{ }

#endif /* CONFIG_CPU_HAS_PMU */

#endif /* __ARM_PMU_H__ */
