/*
 * Copyright (C) 2013-2014 ROCKCHIP, Inc.
 * Copyright (c) 2013 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/rockchip/common.h>
#include <linux/rockchip/pmu.h>

#include <asm/cacheflush.h>
#include <asm/smp_scu.h>
#include <asm/smp_plat.h>
#include <asm/mach/map.h>


#define SCU_CTRL		0x00
#define   SCU_STANDBY_EN	(1 << 5)

static int ncores;

extern void secondary_startup(void);
extern void v7_invalidate_l1(void);

static void __naked rockchip_a9_secondary_startup(void)
{
	v7_invalidate_l1();
	secondary_startup();
}

static void __naked rockchip_secondary_trampoline(void)
{
	asm volatile (
		"ldr	pc, 1f\n"
		".globl	rockchip_boot_fn\n"
		"rockchip_boot_fn:\n"
		"1:	.space	4\n"
	);
}

/*
 * Handling of CPU cores
 */

static int __cpuinit rockchip_boot_secondary(unsigned int cpu,
					     struct task_struct *idle)
{
	if (cpu >= ncores) {
		pr_err("%s: cpu %d outside maximum number of cpus %d\n",
							__func__, cpu, ncores);
		return -EINVAL;
	}

	/* start the core */
	rockchip_pmu_ops.set_power_domain(PD_CPU_0 + cpu, true);

	return 0;
}

/**
 * rockchip_smp_prepare_bootram - populate necessary bootram block
 * Starting cores execute the code residing at the start of the on-chip bootram
 * after power-on. Therefore make sure, this sram region is reserved and
 * big enough. After this check, copy the trampoline code that directs the
 * core to the real startup code in ram into the sram-region.
 */
static int __init rockchip_smp_prepare_bootram(void)
{
	struct device_node *node;
	void __iomem *bootram_base_addr;

	node = of_find_compatible_node(NULL, NULL, "rockchip,bootram");
	if (!node) {
		pr_err("%s: could not find bootram dt node\n", __func__);
		return -ENODEV;
	}

	bootram_base_addr = of_iomap(node, 0);
	if (!bootram_base_addr) {
		pr_err("%s: could not map bootram\n", __func__);
		BUG();
	}

	/* set the boot function for the bootram code */
	if (read_cpuid_part_number() == ARM_CPU_PART_CORTEX_A9)
		rockchip_boot_fn = virt_to_phys(rockchip_a9_secondary_startup);
	else
		rockchip_boot_fn = virt_to_phys(secondary_startup);

	/* copy the trampoline to bootram, that runs during startup of the core */
	memcpy(bootram_base_addr, &rockchip_secondary_trampoline, 8);

	iounmap(bootram_base_addr);

	return 0;
}

static void __init rockchip_a9_smp_prepare_cpus(unsigned int max_cpus)
{
	void __iomem *scu_base_addr;
	unsigned int i, cpu;

	scu_base_addr = ioremap(scu_a9_get_base(), 0x100);

	if (!scu_base_addr) {
		pr_err("%s: could not map scu registers\n", __func__);
		BUG();
	}

	if (rockchip_smp_prepare_bootram())
		return;

	/*
	 * While the number of cpus is gathered from dt, also get the number
	 * of cores from the scu to verify this value when booting the cores.
	 */
	ncores = scu_get_core_count(scu_base_addr);

	writel_relaxed(readl_relaxed(scu_base_addr + SCU_CTRL) | SCU_STANDBY_EN, scu_base_addr + SCU_CTRL);
	scu_enable(scu_base_addr);

	cpu = MPIDR_AFFINITY_LEVEL(read_cpuid_mpidr(), 0);
	/* Make sure that all cores except myself are really off */
	for (i = 0; i < ncores; i++) {
		if (i == cpu)
			continue;
		rockchip_pmu_ops.set_power_domain(PD_CPU_0 + i, false);
	}

	iounmap(scu_base_addr);
}

static void __init rockchip_smp_prepare_cpus(unsigned int max_cpus)
{
	unsigned int i, cpu;
	unsigned long l2ctlr;

	if (scu_a9_has_base())
		return rockchip_a9_smp_prepare_cpus(max_cpus);

	asm("mrc p15, 1, %0, c9, c0, 2" : "=r" (l2ctlr));
	ncores = ((l2ctlr >> 24) & 3) + 1;
	cpu = MPIDR_AFFINITY_LEVEL(read_cpuid_mpidr(), 0);
	/* Make sure that all cores except myself are really off */
	for (i = 0; i < ncores; i++) {
		if (i == cpu)
			continue;
		rockchip_pmu_ops.set_power_domain(PD_CPU_0 + i, false);
	}
}

struct smp_operations rockchip_smp_ops __initdata = {
	.smp_prepare_cpus	= rockchip_smp_prepare_cpus,
	.smp_boot_secondary	= rockchip_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_kill		= rockchip_cpu_kill,
	.cpu_die		= rockchip_cpu_die,
	.cpu_disable		= rockchip_cpu_disable,
#endif
};
