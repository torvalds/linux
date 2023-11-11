// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/arm/mach-axxia/platsmp.c
 *
 * Copyright (C) 2012 LSI Corporation
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/smp.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <asm/cacheflush.h>

/* Syscon register offsets for releasing cores from reset */
#define SC_CRIT_WRITE_KEY	0x1000
#define SC_RST_CPU_HOLD		0x1010

/*
 * Write the kernel entry point for secondary CPUs to the specified address
 */
static void write_release_addr(u32 release_phys)
{
	u32 *virt = (u32 *) phys_to_virt(release_phys);
	writel_relaxed(__pa_symbol(secondary_startup), virt);
	/* Make sure this store is visible to other CPUs */
	smp_wmb();
	__cpuc_flush_dcache_area(virt, sizeof(u32));
}

static int axxia_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	struct device_node *syscon_np;
	void __iomem *syscon;
	u32 tmp;

	syscon_np = of_find_compatible_node(NULL, NULL, "lsi,axxia-syscon");
	if (!syscon_np)
		return -ENOENT;

	syscon = of_iomap(syscon_np, 0);
	of_node_put(syscon_np);
	if (!syscon)
		return -ENOMEM;

	tmp = readl(syscon + SC_RST_CPU_HOLD);
	writel(0xab, syscon + SC_CRIT_WRITE_KEY);
	tmp &= ~(1 << cpu);
	writel(tmp, syscon + SC_RST_CPU_HOLD);

	return 0;
}

static void __init axxia_smp_prepare_cpus(unsigned int max_cpus)
{
	int cpu_count = 0;
	int cpu;

	/*
	 * Initialise the present map, which describes the set of CPUs actually
	 * populated at the present time.
	 */
	for_each_possible_cpu(cpu) {
		struct device_node *np;
		u32 release_phys;

		np = of_get_cpu_node(cpu, NULL);
		if (!np)
			continue;
		if (of_property_read_u32(np, "cpu-release-addr", &release_phys))
			continue;

		if (cpu_count < max_cpus) {
			set_cpu_present(cpu, true);
			cpu_count++;
		}

		if (release_phys != 0)
			write_release_addr(release_phys);
	}
}

static const struct smp_operations axxia_smp_ops __initconst = {
	.smp_prepare_cpus	= axxia_smp_prepare_cpus,
	.smp_boot_secondary	= axxia_boot_secondary,
};
CPU_METHOD_OF_DECLARE(axxia_smp, "lsi,syscon-release", &axxia_smp_ops);
