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
#include <linux/of_fdt.h>
#include <linux/vexpress.h>

#include <asm/smp_scu.h>
#include <asm/mach/map.h>

#include <mach/motherboard.h>

#include <plat/platsmp.h>

#include "core.h"

#if defined(CONFIG_OF)

static enum {
	GENERIC_SCU,
	CORTEX_A9_SCU,
} vexpress_dt_scu __initdata = GENERIC_SCU;

static struct map_desc vexpress_dt_cortex_a9_scu_map __initdata = {
	.virtual	= V2T_PERIPH,
	/* .pfn	set in vexpress_dt_init_cortex_a9_scu() */
	.length		= SZ_128,
	.type		= MT_DEVICE,
};

static void *vexpress_dt_cortex_a9_scu_base __initdata;

const static char *vexpress_dt_cortex_a9_match[] __initconst = {
	"arm,cortex-a5-scu",
	"arm,cortex-a9-scu",
	NULL
};

static int __init vexpress_dt_find_scu(unsigned long node,
		const char *uname, int depth, void *data)
{
	if (of_flat_dt_match(node, vexpress_dt_cortex_a9_match)) {
		phys_addr_t phys_addr;
		__be32 *reg = of_get_flat_dt_prop(node, "reg", NULL);

		if (WARN_ON(!reg))
			return -EINVAL;

		phys_addr = be32_to_cpup(reg);
		vexpress_dt_scu = CORTEX_A9_SCU;

		vexpress_dt_cortex_a9_scu_map.pfn = __phys_to_pfn(phys_addr);
		iotable_init(&vexpress_dt_cortex_a9_scu_map, 1);
		vexpress_dt_cortex_a9_scu_base = ioremap(phys_addr, SZ_256);
		if (WARN_ON(!vexpress_dt_cortex_a9_scu_base))
			return -EFAULT;
	}

	return 0;
}

void __init vexpress_dt_smp_map_io(void)
{
	if (initial_boot_params)
		WARN_ON(of_scan_flat_dt(vexpress_dt_find_scu, NULL));
}

static int __init vexpress_dt_cpus_num(unsigned long node, const char *uname,
		int depth, void *data)
{
	static int prev_depth = -1;
	static int nr_cpus = -1;

	if (prev_depth > depth && nr_cpus > 0)
		return nr_cpus;

	if (nr_cpus < 0 && strcmp(uname, "cpus") == 0)
		nr_cpus = 0;

	if (nr_cpus >= 0) {
		const char *device_type = of_get_flat_dt_prop(node,
				"device_type", NULL);

		if (device_type && strcmp(device_type, "cpu") == 0)
			nr_cpus++;
	}

	prev_depth = depth;

	return 0;
}

static void __init vexpress_dt_smp_init_cpus(void)
{
	int ncores = 0, i;

	switch (vexpress_dt_scu) {
	case GENERIC_SCU:
		ncores = of_scan_flat_dt(vexpress_dt_cpus_num, NULL);
		break;
	case CORTEX_A9_SCU:
		ncores = scu_get_core_count(vexpress_dt_cortex_a9_scu_base);
		break;
	default:
		WARN_ON(1);
		break;
	}

	if (ncores < 2)
		return;

	if (ncores > nr_cpu_ids) {
		pr_warn("SMP: %u cores greater than maximum (%u), clipping\n",
				ncores, nr_cpu_ids);
		ncores = nr_cpu_ids;
	}

	for (i = 0; i < ncores; ++i)
		set_cpu_possible(i, true);
}

static void __init vexpress_dt_smp_prepare_cpus(unsigned int max_cpus)
{
	int i;

	switch (vexpress_dt_scu) {
	case GENERIC_SCU:
		for (i = 0; i < max_cpus; i++)
			set_cpu_present(i, true);
		break;
	case CORTEX_A9_SCU:
		scu_enable(vexpress_dt_cortex_a9_scu_base);
		break;
	default:
		WARN_ON(1);
		break;
	}
}

#else

static void __init vexpress_dt_smp_init_cpus(void)
{
	WARN_ON(1);
}

void __init vexpress_dt_smp_prepare_cpus(unsigned int max_cpus)
{
	WARN_ON(1);
}

#endif

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
static void __init vexpress_smp_init_cpus(void)
{
	if (ct_desc)
		ct_desc->init_cpu_map();
	else
		vexpress_dt_smp_init_cpus();

}

static void __init vexpress_smp_prepare_cpus(unsigned int max_cpus)
{
	/*
	 * Initialise the present map, which describes the set of CPUs
	 * actually populated at the present time.
	 */
	if (ct_desc)
		ct_desc->smp_enable(max_cpus);
	else
		vexpress_dt_smp_prepare_cpus(max_cpus);

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
