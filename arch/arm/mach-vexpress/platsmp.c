/*
 *  linux/arch/arm/mach-vexpress/platsmp.c
 *
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/vexpress.h>

#include <asm/mcpm.h>
#include <asm/smp_scu.h>
#include <asm/mach/map.h>

#include <mach/motherboard.h>

#include <plat/platsmp.h>

#include "core.h"

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
static void __init vexpress_smp_init_cpus(void)
{
	ct_desc->init_cpu_map();
}

static void __init vexpress_smp_prepare_cpus(unsigned int max_cpus)
{
	/*
	 * Initialise the present map, which describes the set of CPUs
	 * actually populated at the present time.
	 */
	ct_desc->smp_enable(max_cpus);

	/*
	 * Write the address of secondary startup into the
	 * system-wide flags register. The boot monitor waits
	 * until it receives a soft interrupt, and then the
	 * secondary CPU branches to this address.
	 */
	vexpress_flags_set(virt_to_phys(versatile_secondary_startup));
}

struct smp_operations __initdata vexpress_smp_ops = {
	.smp_init_cpus		= vexpress_smp_init_cpus,
	.smp_prepare_cpus	= vexpress_smp_prepare_cpus,
	.smp_secondary_init	= versatile_secondary_init,
	.smp_boot_secondary	= versatile_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= vexpress_cpu_die,
#endif
};

bool __init vexpress_smp_init_ops(void)
{
#ifdef CONFIG_MCPM
	/*
	 * The best way to detect a multi-cluster configuration at the moment
	 * is to look for the presence of a CCI in the system.
	 * Override the default vexpress_smp_ops if so.
	 */
	struct device_node *node;
	node = of_find_compatible_node(NULL, NULL, "arm,cci-400");
	if (node && of_device_is_available(node)) {
		mcpm_smp_set_ops();
		return true;
	}
#endif
	return false;
}

#if defined(CONFIG_OF)

static const struct of_device_id vexpress_smp_dt_scu_match[] __initconst = {
	{ .compatible = "arm,cortex-a5-scu", },
	{ .compatible = "arm,cortex-a9-scu", },
	{}
};

static void __init vexpress_smp_dt_prepare_cpus(unsigned int max_cpus)
{
	struct device_node *scu = of_find_matching_node(NULL,
			vexpress_smp_dt_scu_match);

	if (scu)
		scu_enable(of_iomap(scu, 0));

	/*
	 * Write the address of secondary startup into the
	 * system-wide flags register. The boot monitor waits
	 * until it receives a soft interrupt, and then the
	 * secondary CPU branches to this address.
	 */
	vexpress_flags_set(virt_to_phys(versatile_secondary_startup));
}

struct smp_operations __initdata vexpress_smp_dt_ops = {
	.smp_prepare_cpus	= vexpress_smp_dt_prepare_cpus,
	.smp_secondary_init	= versatile_secondary_init,
	.smp_boot_secondary	= versatile_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= vexpress_cpu_die,
#endif
};

#endif
