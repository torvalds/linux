/*
 * Actions Semi Leopard
 *
 * This file is based on arm realview smp platform.
 *
 * Copyright 2012 Actions Semi Inc.
 * Author: Actions Semi, Inc.
 *
 * Copyright (c) 2017 Andreas Färber
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/smp.h>
#include <linux/soc/actions/owl-sps.h>
#include <asm/cacheflush.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>

#define OWL_CPU1_ADDR	0x50
#define OWL_CPU1_FLAG	0x5c

#define OWL_CPUx_FLAG_BOOT	0x55aa

#define OWL_SPS_PG_CTL_PWR_CPU2	BIT(5)
#define OWL_SPS_PG_CTL_PWR_CPU3	BIT(6)
#define OWL_SPS_PG_CTL_ACK_CPU2	BIT(21)
#define OWL_SPS_PG_CTL_ACK_CPU3	BIT(22)

static void __iomem *scu_base_addr;
static void __iomem *sps_base_addr;
static void __iomem *timer_base_addr;
static int ncores;

static DEFINE_SPINLOCK(boot_lock);

void owl_secondary_startup(void);

static int s500_wakeup_secondary(unsigned int cpu)
{
	int ret;

	if (cpu > 3)
		return -EINVAL;

	/* The generic PM domain driver is not available this early. */
	switch (cpu) {
	case 2:
		ret = owl_sps_set_pg(sps_base_addr,
		                     OWL_SPS_PG_CTL_PWR_CPU2,
				     OWL_SPS_PG_CTL_ACK_CPU2, true);
		if (ret)
			return ret;
		break;
	case 3:
		ret = owl_sps_set_pg(sps_base_addr,
		                     OWL_SPS_PG_CTL_PWR_CPU3,
				     OWL_SPS_PG_CTL_ACK_CPU3, true);
		if (ret)
			return ret;
		break;
	}

	/* wait for CPUx to run to WFE instruction */
	udelay(200);

	writel(__pa_symbol(secondary_startup),
	       timer_base_addr + OWL_CPU1_ADDR + (cpu - 1) * 4);
	writel(OWL_CPUx_FLAG_BOOT,
	       timer_base_addr + OWL_CPU1_FLAG + (cpu - 1) * 4);

	dsb_sev();
	mb();

	return 0;
}

static int s500_smp_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned long timeout;
	int ret;

	ret = s500_wakeup_secondary(cpu);
	if (ret)
		return ret;

	udelay(10);

	spin_lock(&boot_lock);

	smp_send_reschedule(cpu);

	timeout = jiffies + (1 * HZ);
	while (time_before(jiffies, timeout)) {
		if (pen_release == -1)
			break;
	}

	writel(0, timer_base_addr + OWL_CPU1_ADDR + (cpu - 1) * 4);
	writel(0, timer_base_addr + OWL_CPU1_FLAG + (cpu - 1) * 4);

	spin_unlock(&boot_lock);

	return 0;
}

static void __init s500_smp_prepare_cpus(unsigned int max_cpus)
{
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "actions,s500-timer");
	if (!node) {
		pr_err("%s: missing timer\n", __func__);
		return;
	}

	timer_base_addr = of_iomap(node, 0);
	if (!timer_base_addr) {
		pr_err("%s: could not map timer registers\n", __func__);
		return;
	}

	node = of_find_compatible_node(NULL, NULL, "actions,s500-sps");
	if (!node) {
		pr_err("%s: missing sps\n", __func__);
		return;
	}

	sps_base_addr = of_iomap(node, 0);
	if (!sps_base_addr) {
		pr_err("%s: could not map sps registers\n", __func__);
		return;
	}

	if (read_cpuid_part() == ARM_CPU_PART_CORTEX_A9) {
		node = of_find_compatible_node(NULL, NULL, "arm,cortex-a9-scu");
		if (!node) {
			pr_err("%s: missing scu\n", __func__);
			return;
		}

		scu_base_addr = of_iomap(node, 0);
		if (!scu_base_addr) {
			pr_err("%s: could not map scu registers\n", __func__);
			return;
		}

		/*
		 * While the number of cpus is gathered from dt, also get the
		 * number of cores from the scu to verify this value when
		 * booting the cores.
		 */
		ncores = scu_get_core_count(scu_base_addr);
		pr_debug("%s: ncores %d\n", __func__, ncores);

		scu_enable(scu_base_addr);
	}
}

static const struct smp_operations s500_smp_ops __initconst = {
	.smp_prepare_cpus = s500_smp_prepare_cpus,
	.smp_boot_secondary = s500_smp_boot_secondary,
};
CPU_METHOD_OF_DECLARE(s500_smp, "actions,s500-smp", &s500_smp_ops);
