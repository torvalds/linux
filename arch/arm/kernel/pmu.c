/*
 *  linux/arch/arm/kernel/pmu.c
 *
 *  Copyright (C) 2009 picoChip Designs Ltd, Jamie Iles
 *  Copyright (C) 2010 ARM Ltd, Will Deacon
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <asm/pmu.h>

/*
 * PMU locking to ensure mutual exclusion between different subsystems.
 */
static unsigned long pmu_lock[BITS_TO_LONGS(ARM_NUM_PMU_DEVICES)];

int
reserve_pmu(enum arm_pmu_type type)
{
	return test_and_set_bit_lock(type, pmu_lock) ? -EBUSY : 0;
}
EXPORT_SYMBOL_GPL(reserve_pmu);

void
release_pmu(enum arm_pmu_type type)
{
	clear_bit_unlock(type, pmu_lock);
}
EXPORT_SYMBOL_GPL(release_pmu);
