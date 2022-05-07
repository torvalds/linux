// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/mach-vexpress/dcscb.c - Dual Cluster System Configuration Block
 *
 * Created by:	Nicolas Pitre, May 2012
 * Copyright:	(C) 2012-2013  Linaro Limited
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/errno.h>
#include <linux/of_address.h>
#include <linux/vexpress.h>
#include <linux/arm-cci.h>

#include <asm/mcpm.h>
#include <asm/proc-fns.h>
#include <asm/cacheflush.h>
#include <asm/cputype.h>
#include <asm/cp15.h>

#include "core.h"

#define RST_HOLD0	0x0
#define RST_HOLD1	0x4
#define SYS_SWRESET	0x8
#define RST_STAT0	0xc
#define RST_STAT1	0x10
#define EAG_CFG_R	0x20
#define EAG_CFG_W	0x24
#define KFC_CFG_R	0x28
#define KFC_CFG_W	0x2c
#define DCS_CFG_R	0x30

static void __iomem *dcscb_base;
static int dcscb_allcpus_mask[2];

static int dcscb_cpu_powerup(unsigned int cpu, unsigned int cluster)
{
	unsigned int rst_hold, cpumask = (1 << cpu);

	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	if (cluster >= 2 || !(cpumask & dcscb_allcpus_mask[cluster]))
		return -EINVAL;

	rst_hold = readl_relaxed(dcscb_base + RST_HOLD0 + cluster * 4);
	rst_hold &= ~(cpumask | (cpumask << 4));
	writel_relaxed(rst_hold, dcscb_base + RST_HOLD0 + cluster * 4);
	return 0;
}

static int dcscb_cluster_powerup(unsigned int cluster)
{
	unsigned int rst_hold;

	pr_debug("%s: cluster %u\n", __func__, cluster);
	if (cluster >= 2)
		return -EINVAL;

	/* remove cluster reset and add individual CPU's reset */
	rst_hold = readl_relaxed(dcscb_base + RST_HOLD0 + cluster * 4);
	rst_hold &= ~(1 << 8);
	rst_hold |= dcscb_allcpus_mask[cluster];
	writel_relaxed(rst_hold, dcscb_base + RST_HOLD0 + cluster * 4);
	return 0;
}

static void dcscb_cpu_powerdown_prepare(unsigned int cpu, unsigned int cluster)
{
	unsigned int rst_hold;

	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	BUG_ON(cluster >= 2 || !((1 << cpu) & dcscb_allcpus_mask[cluster]));

	rst_hold = readl_relaxed(dcscb_base + RST_HOLD0 + cluster * 4);
	rst_hold |= (1 << cpu);
	writel_relaxed(rst_hold, dcscb_base + RST_HOLD0 + cluster * 4);
}

static void dcscb_cluster_powerdown_prepare(unsigned int cluster)
{
	unsigned int rst_hold;

	pr_debug("%s: cluster %u\n", __func__, cluster);
	BUG_ON(cluster >= 2);

	rst_hold = readl_relaxed(dcscb_base + RST_HOLD0 + cluster * 4);
	rst_hold |= (1 << 8);
	writel_relaxed(rst_hold, dcscb_base + RST_HOLD0 + cluster * 4);
}

static void dcscb_cpu_cache_disable(void)
{
	/* Disable and flush the local CPU cache. */
	v7_exit_coherency_flush(louis);
}

static void dcscb_cluster_cache_disable(void)
{
	/* Flush all cache levels for this cluster. */
	v7_exit_coherency_flush(all);

	/*
	 * A full outer cache flush could be needed at this point
	 * on platforms with such a cache, depending on where the
	 * outer cache sits. In some cases the notion of a "last
	 * cluster standing" would need to be implemented if the
	 * outer cache is shared across clusters. In any case, when
	 * the outer cache needs flushing, there is no concurrent
	 * access to the cache controller to worry about and no
	 * special locking besides what is already provided by the
	 * MCPM state machinery is needed.
	 */

	/*
	 * Disable cluster-level coherency by masking
	 * incoming snoops and DVM messages:
	 */
	cci_disable_port_by_cpu(read_cpuid_mpidr());
}

static const struct mcpm_platform_ops dcscb_power_ops = {
	.cpu_powerup		= dcscb_cpu_powerup,
	.cluster_powerup	= dcscb_cluster_powerup,
	.cpu_powerdown_prepare	= dcscb_cpu_powerdown_prepare,
	.cluster_powerdown_prepare = dcscb_cluster_powerdown_prepare,
	.cpu_cache_disable	= dcscb_cpu_cache_disable,
	.cluster_cache_disable	= dcscb_cluster_cache_disable,
};

extern void dcscb_power_up_setup(unsigned int affinity_level);

static int __init dcscb_init(void)
{
	struct device_node *node;
	unsigned int cfg;
	int ret;

	if (!cci_probed())
		return -ENODEV;

	node = of_find_compatible_node(NULL, NULL, "arm,rtsm,dcscb");
	if (!node)
		return -ENODEV;
	dcscb_base = of_iomap(node, 0);
	if (!dcscb_base)
		return -EADDRNOTAVAIL;
	cfg = readl_relaxed(dcscb_base + DCS_CFG_R);
	dcscb_allcpus_mask[0] = (1 << (((cfg >> 16) >> (0 << 2)) & 0xf)) - 1;
	dcscb_allcpus_mask[1] = (1 << (((cfg >> 16) >> (1 << 2)) & 0xf)) - 1;

	ret = mcpm_platform_register(&dcscb_power_ops);
	if (!ret)
		ret = mcpm_sync_init(dcscb_power_up_setup);
	if (ret) {
		iounmap(dcscb_base);
		return ret;
	}

	pr_info("VExpress DCSCB support installed\n");

	/*
	 * Future entries into the kernel can now go
	 * through the cluster entry vectors.
	 */
	vexpress_flags_set(__pa_symbol(mcpm_entry_point));

	return 0;
}

early_initcall(dcscb_init);
