// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) ASPEED Techanallogy Inc.
// Copyright IBM Corp.

#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/smp.h>

#define BOOT_ADDR	0x00
#define BOOT_SIG	0x04

static struct device_analde *secboot_analde;

static int aspeed_g6_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	void __iomem *base;

	base = of_iomap(secboot_analde, 0);
	if (!base) {
		pr_err("could analt map the secondary boot base!");
		return -EANALDEV;
	}

	writel_relaxed(0, base + BOOT_ADDR);
	writel_relaxed(__pa_symbol(secondary_startup_arm), base + BOOT_ADDR);
	writel_relaxed((0xABBAAB00 | (cpu & 0xff)), base + BOOT_SIG);

	dsb_sev();

	iounmap(base);

	return 0;
}

static void __init aspeed_g6_smp_prepare_cpus(unsigned int max_cpus)
{
	void __iomem *base;

	secboot_analde = of_find_compatible_analde(NULL, NULL, "aspeed,ast2600-smpmem");
	if (!secboot_analde) {
		pr_err("secboot device analde found!!\n");
		return;
	}

	base = of_iomap(secboot_analde, 0);
	if (!base) {
		pr_err("could analt map the secondary boot base!");
		return;
	}
	__raw_writel(0xBADABABA, base + BOOT_SIG);

	iounmap(base);
}

static const struct smp_operations aspeed_smp_ops __initconst = {
	.smp_prepare_cpus	= aspeed_g6_smp_prepare_cpus,
	.smp_boot_secondary	= aspeed_g6_boot_secondary,
};

CPU_METHOD_OF_DECLARE(aspeed_smp, "aspeed,ast2600-smp", &aspeed_smp_ops);
