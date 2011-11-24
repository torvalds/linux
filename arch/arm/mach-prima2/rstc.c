/*
 * reset controller for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>

void __iomem *sirfsoc_rstc_base;
static DEFINE_MUTEX(rstc_lock);

static struct of_device_id rstc_ids[]  = {
	{ .compatible = "sirf,prima2-rstc" },
	{},
};

static int __init sirfsoc_of_rstc_init(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, rstc_ids);
	if (!np)
		panic("unable to find compatible rstc node in dtb\n");

	sirfsoc_rstc_base = of_iomap(np, 0);
	if (!sirfsoc_rstc_base)
		panic("unable to map rstc cpu registers\n");

	of_node_put(np);

	return 0;
}
early_initcall(sirfsoc_of_rstc_init);

int sirfsoc_reset_device(struct device *dev)
{
	const unsigned int *prop = of_get_property(dev->of_node, "reset-bit", NULL);
	unsigned int reset_bit;

	if (!prop)
		return -ENODEV;

	reset_bit = be32_to_cpup(prop);

	mutex_lock(&rstc_lock);

	/*
	 * Writing 1 to this bit resets corresponding block. Writing 0 to this
	 * bit de-asserts reset signal of the corresponding block.
	 * datasheet doesn't require explicit delay between the set and clear
	 * of reset bit. it could be shorter if tests pass.
	 */
	writel(readl(sirfsoc_rstc_base + (reset_bit / 32) * 4) | reset_bit,
		sirfsoc_rstc_base + (reset_bit / 32) * 4);
	msleep(10);
	writel(readl(sirfsoc_rstc_base + (reset_bit / 32) * 4) & ~reset_bit,
		sirfsoc_rstc_base + (reset_bit / 32) * 4);

	mutex_unlock(&rstc_lock);

	return 0;
}
