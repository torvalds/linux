/*
 * arch/arm/common/mcpm_entry.c -- entry point for multi-cluster PM
 *
 * Created by:  Nicolas Pitre, March 2012
 * Copyright:   (C) 2012-2013  Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irqflags.h>

#include <asm/mcpm.h>
#include <asm/cacheflush.h>
#include <asm/idmap.h>

extern unsigned long mcpm_entry_vectors[MAX_NR_CLUSTERS][MAX_CPUS_PER_CLUSTER];

void mcpm_set_entry_vector(unsigned cpu, unsigned cluster, void *ptr)
{
	unsigned long val = ptr ? virt_to_phys(ptr) : 0;
	mcpm_entry_vectors[cluster][cpu] = val;
	sync_cache_w(&mcpm_entry_vectors[cluster][cpu]);
}

static const struct mcpm_platform_ops *platform_ops;

int __init mcpm_platform_register(const struct mcpm_platform_ops *ops)
{
	if (platform_ops)
		return -EBUSY;
	platform_ops = ops;
	return 0;
}

int mcpm_cpu_power_up(unsigned int cpu, unsigned int cluster)
{
	if (!platform_ops)
		return -EUNATCH; /* try not to shadow power_up errors */
	might_sleep();
	return platform_ops->power_up(cpu, cluster);
}

typedef void (*phys_reset_t)(unsigned long);

void mcpm_cpu_power_down(void)
{
	phys_reset_t phys_reset;

	BUG_ON(!platform_ops);
	BUG_ON(!irqs_disabled());

	/*
	 * Do this before calling into the power_down method,
	 * as it might not always be safe to do afterwards.
	 */
	setup_mm_for_reboot();

	platform_ops->power_down();

	/*
	 * It is possible for a power_up request to happen concurrently
	 * with a power_down request for the same CPU. In this case the
	 * power_down method might not be able to actually enter a
	 * powered down state with the WFI instruction if the power_up
	 * method has removed the required reset condition.  The
	 * power_down method is then allowed to return. We must perform
	 * a re-entry in the kernel as if the power_up method just had
	 * deasserted reset on the CPU.
	 *
	 * To simplify race issues, the platform specific implementation
	 * must accommodate for the possibility of unordered calls to
	 * power_down and power_up with a usage count. Therefore, if a
	 * call to power_up is issued for a CPU that is not down, then
	 * the next call to power_down must not attempt a full shutdown
	 * but only do the minimum (normally disabling L1 cache and CPU
	 * coherency) and return just as if a concurrent power_up request
	 * had happened as described above.
	 */

	phys_reset = (phys_reset_t)(unsigned long)virt_to_phys(cpu_reset);
	phys_reset(virt_to_phys(mcpm_entry_point));

	/* should never get here */
	BUG();
}

void mcpm_cpu_suspend(u64 expected_residency)
{
	phys_reset_t phys_reset;

	BUG_ON(!platform_ops);
	BUG_ON(!irqs_disabled());

	/* Very similar to mcpm_cpu_power_down() */
	setup_mm_for_reboot();
	platform_ops->suspend(expected_residency);
	phys_reset = (phys_reset_t)(unsigned long)virt_to_phys(cpu_reset);
	phys_reset(virt_to_phys(mcpm_entry_point));
	BUG();
}

int mcpm_cpu_powered_up(void)
{
	if (!platform_ops)
		return -EUNATCH;
	if (platform_ops->powered_up)
		platform_ops->powered_up();
	return 0;
}
