// SPDX-License-Identifier: GPL-2.0-only
/*
 * Broadcom BCM63138 DSL SoCs SMP support code
 *
 * Copyright (C) 2015, Broadcom Corporation
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/cacheflush.h>
#include <asm/smp_scu.h>
#include <asm/smp_plat.h>
#include <asm/vfp.h>

#include "bcm63xx_smp.h"

/* Size of mapped Cortex A9 SCU address space */
#define CORTEX_A9_SCU_SIZE	0x58

/*
 * Enable the Cortex A9 Snoop Control Unit
 *
 * By the time this is called we already know there are multiple
 * cores present.  We assume we're running on a Cortex A9 processor,
 * so any trouble getting the base address register or getting the
 * SCU base is a problem.
 *
 * Return 0 if successful or an error code otherwise.
 */
static int __init scu_a9_enable(void)
{
	unsigned long config_base;
	void __iomem *scu_base;
	unsigned int i, ncores;

	if (!scu_a9_has_base()) {
		pr_err("no configuration base address register!\n");
		return -ENXIO;
	}

	/* Config base address register value is zero for uniprocessor */
	config_base = scu_a9_get_base();
	if (!config_base) {
		pr_err("hardware reports only one core\n");
		return -ENOENT;
	}

	scu_base = ioremap((phys_addr_t)config_base, CORTEX_A9_SCU_SIZE);
	if (!scu_base) {
		pr_err("failed to remap config base (%lu/%u) for SCU\n",
			config_base, CORTEX_A9_SCU_SIZE);
		return -ENOMEM;
	}

	scu_enable(scu_base);

	ncores = scu_base ? scu_get_core_count(scu_base) : 1;

	if (ncores > nr_cpu_ids) {
		pr_warn("SMP: %u cores greater than maximum (%u), clipping\n",
				ncores, nr_cpu_ids);
		ncores = nr_cpu_ids;
	}

	/* The BCM63138 SoC has two Cortex-A9 CPUs, CPU0 features a complete
	 * and fully functional VFP unit that can be used, but CPU1 does not.
	 * Since we will not be able to trap kernel-mode NEON to force
	 * migration to CPU0, just do not advertise VFP support at all.
	 *
	 * This will make vfp_init bail out and do not attempt to use VFP at
	 * all, for kernel-mode NEON, we do not want to introduce any
	 * conditionals in hot-paths, so we just restrict the system to UP.
	 */
#ifdef CONFIG_VFP
	if (ncores > 1) {
		pr_warn("SMP: secondary CPUs lack VFP unit, disabling VFP\n");
		vfp_disable();

#ifdef CONFIG_KERNEL_MODE_NEON
		WARN(1, "SMP: kernel-mode NEON enabled, restricting to UP\n");
		ncores = 1;
#endif
	}
#endif

	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);

	iounmap(scu_base);	/* That's the last we'll need of this */

	return 0;
}

static const struct of_device_id bcm63138_bootlut_ids[] = {
	{ .compatible = "brcm,bcm63138-bootlut", },
	{ /* sentinel */ },
};

#define BOOTLUT_RESET_VECT	0x20

static int bcm63138_smp_boot_secondary(unsigned int cpu,
				       struct task_struct *idle)
{
	void __iomem *bootlut_base;
	struct device_node *dn;
	int ret = 0;
	u32 val;

	dn = of_find_matching_node(NULL, bcm63138_bootlut_ids);
	if (!dn) {
		pr_err("SMP: unable to find bcm63138 boot LUT node\n");
		return -ENODEV;
	}

	bootlut_base = of_iomap(dn, 0);
	of_node_put(dn);

	if (!bootlut_base) {
		pr_err("SMP: unable to remap boot LUT base register\n");
		return -ENOMEM;
	}

	/* Locate the secondary CPU node */
	dn = of_get_cpu_node(cpu, NULL);
	if (!dn) {
		pr_err("SMP: failed to locate secondary CPU%d node\n", cpu);
		ret = -ENODEV;
		goto out;
	}

	/* Write the secondary init routine to the BootLUT reset vector */
	val = __pa_symbol(secondary_startup);
	writel_relaxed(val, bootlut_base + BOOTLUT_RESET_VECT);

	/* Power up the core, will jump straight to its reset vector when we
	 * return
	 */
	ret = bcm63xx_pmb_power_on_cpu(dn);
	of_node_put(dn);
	if (ret)
		goto out;
out:
	iounmap(bootlut_base);

	return ret;
}

static void __init bcm63138_smp_prepare_cpus(unsigned int max_cpus)
{
	int ret;

	ret = scu_a9_enable();
	if (ret) {
		pr_warn("SMP: Cortex-A9 SCU setup failed\n");
		return;
	}
}

static const struct smp_operations bcm63138_smp_ops __initconst = {
	.smp_prepare_cpus	= bcm63138_smp_prepare_cpus,
	.smp_boot_secondary	= bcm63138_smp_boot_secondary,
};

CPU_METHOD_OF_DECLARE(bcm63138_smp, "brcm,bcm63138", &bcm63138_smp_ops);
