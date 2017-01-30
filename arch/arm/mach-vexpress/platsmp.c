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

#include <plat/platsmp.h>

#include "core.h"

bool __init vexpress_smp_init_ops(void)
{
#ifdef CONFIG_MCPM
	int cpu;
	struct device_node *cpu_node, *cci_node;

	/*
	 * The best way to detect a multi-cluster configuration
	 * is to detect if the kernel can take over CCI ports
	 * control. Loop over possible CPUs and check if CCI
	 * port control is available.
	 * Override the default vexpress_smp_ops if so.
	 */
	for_each_possible_cpu(cpu) {
		bool available;

		cpu_node = of_get_cpu_node(cpu, NULL);
		if (WARN(!cpu_node, "Missing cpu device node!"))
			return false;

		cci_node = of_parse_phandle(cpu_node, "cci-control-port", 0);
		available = cci_node && of_device_is_available(cci_node);
		of_node_put(cci_node);
		of_node_put(cpu_node);

		if (!available)
			return false;
	}

	mcpm_smp_set_ops();
	return true;
#else
	return false;
#endif
}

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

const struct smp_operations vexpress_smp_dt_ops __initconst = {
	.smp_prepare_cpus	= vexpress_smp_dt_prepare_cpus,
	.smp_secondary_init	= versatile_secondary_init,
	.smp_boot_secondary	= versatile_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= vexpress_cpu_die,
#endif
};
