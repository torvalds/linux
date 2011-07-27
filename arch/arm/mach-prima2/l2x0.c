/*
 * l2 cache initialization for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <asm/hardware/cache-l2x0.h>
#include <mach/memory.h>

#define L2X0_ADDR_FILTERING_START       0xC00
#define L2X0_ADDR_FILTERING_END         0xC04

static struct of_device_id l2x_ids[]  = {
	{ .compatible = "arm,pl310-cache" },
};

static int __init sirfsoc_of_l2x_init(void)
{
	struct device_node *np;
	void __iomem *sirfsoc_l2x_base;

	np = of_find_matching_node(NULL, l2x_ids);
	if (!np)
		panic("unable to find compatible l2x node in dtb\n");

	sirfsoc_l2x_base = of_iomap(np, 0);
	if (!sirfsoc_l2x_base)
		panic("unable to map l2x cpu registers\n");

	of_node_put(np);

	if (!(readl_relaxed(sirfsoc_l2x_base + L2X0_CTRL) & 1)) {
		/*
		 * set the physical memory windows L2 cache will cover
		 */
		writel_relaxed(PLAT_PHYS_OFFSET + 1024 * 1024 * 1024,
			sirfsoc_l2x_base + L2X0_ADDR_FILTERING_END);
		writel_relaxed(PLAT_PHYS_OFFSET | 0x1,
			sirfsoc_l2x_base + L2X0_ADDR_FILTERING_START);

		writel_relaxed(0,
			sirfsoc_l2x_base + L2X0_TAG_LATENCY_CTRL);
		writel_relaxed(0,
			sirfsoc_l2x_base + L2X0_DATA_LATENCY_CTRL);
	}
	l2x0_init((void __iomem *)sirfsoc_l2x_base, 0x00040000,
		0x00000000);

	return 0;
}
early_initcall(sirfsoc_of_l2x_init);
