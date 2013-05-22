/*
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *  Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *  Copyright (c) 2014 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/smp.h>
#include <linux/io.h>

#include <asm/smp_plat.h>

#include "scm-boot.h"

#define VDD_SC1_ARRAY_CLAMP_GFS_CTL	0x35a0
#define SCSS_CPU1CORE_RESET		0x2d80
#define SCSS_DBG_STATUS_CORE_PWRDUP	0x2e64

extern void secondary_startup(void);

static DEFINE_SPINLOCK(boot_lock);

#ifdef CONFIG_HOTPLUG_CPU
static void __ref qcom_cpu_die(unsigned int cpu)
{
	wfi();
}
#endif

static void qcom_secondary_init(unsigned int cpu)
{
	/*
	 * Synchronise with the boot thread.
	 */
	spin_lock(&boot_lock);
	spin_unlock(&boot_lock);
}

static int scss_release_secondary(unsigned int cpu)
{
	struct device_node *node;
	void __iomem *base;

	node = of_find_compatible_node(NULL, NULL, "qcom,gcc-msm8660");
	if (!node) {
		pr_err("%s: can't find node\n", __func__);
		return -ENXIO;
	}

	base = of_iomap(node, 0);
	of_node_put(node);
	if (!base)
		return -ENOMEM;

	writel_relaxed(0, base + VDD_SC1_ARRAY_CLAMP_GFS_CTL);
	writel_relaxed(0, base + SCSS_CPU1CORE_RESET);
	writel_relaxed(3, base + SCSS_DBG_STATUS_CORE_PWRDUP);
	mb();
	iounmap(base);

	return 0;
}

static DEFINE_PER_CPU(int, cold_boot_done);

static int qcom_boot_secondary(unsigned int cpu, int (*func)(unsigned int))
{
	int ret = 0;

	if (!per_cpu(cold_boot_done, cpu)) {
		ret = func(cpu);
		if (!ret)
			per_cpu(cold_boot_done, cpu) = true;
	}

	/*
	 * set synchronisation state between this boot processor
	 * and the secondary one
	 */
	spin_lock(&boot_lock);

	/*
	 * Send the secondary CPU a soft interrupt, thereby causing
	 * the boot monitor to read the system wide flags register,
	 * and branch to the address found there.
	 */
	arch_send_wakeup_ipi_mask(cpumask_of(cpu));

	/*
	 * now the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish
	 */
	spin_unlock(&boot_lock);

	return ret;
}

static int msm8660_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	return qcom_boot_secondary(cpu, scss_release_secondary);
}

static void __init qcom_smp_prepare_cpus(unsigned int max_cpus)
{
	int cpu, map;
	unsigned int flags = 0;
	static const int cold_boot_flags[] = {
		0,
		SCM_FLAG_COLDBOOT_CPU1,
	};

	for_each_present_cpu(cpu) {
		map = cpu_logical_map(cpu);
		if (WARN_ON(map >= ARRAY_SIZE(cold_boot_flags))) {
			set_cpu_present(cpu, false);
			continue;
		}
		flags |= cold_boot_flags[map];
	}

	if (scm_set_boot_addr(virt_to_phys(secondary_startup), flags)) {
		for_each_present_cpu(cpu) {
			if (cpu == smp_processor_id())
				continue;
			set_cpu_present(cpu, false);
		}
		pr_warn("Failed to set CPU boot address, disabling SMP\n");
	}
}

static struct smp_operations smp_msm8660_ops __initdata = {
	.smp_prepare_cpus	= qcom_smp_prepare_cpus,
	.smp_secondary_init	= qcom_secondary_init,
	.smp_boot_secondary	= msm8660_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= qcom_cpu_die,
#endif
};
CPU_METHOD_OF_DECLARE(qcom_smp, "qcom,gcc-msm8660", &smp_msm8660_ops);
