// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2015 Broadcom Corporation
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "brcmnand.h"

struct bcm63138_nand_soc {
	struct brcmnand_soc soc;
	void __iomem *base;
};

#define BCM63138_NAND_INT_STATUS		0x00
#define BCM63138_NAND_INT_EN			0x04

enum {
	BCM63138_CTLRDY		= BIT(4),
};

static bool bcm63138_nand_intc_ack(struct brcmnand_soc *soc)
{
	struct bcm63138_nand_soc *priv =
			container_of(soc, struct bcm63138_nand_soc, soc);
	void __iomem *mmio = priv->base + BCM63138_NAND_INT_STATUS;
	u32 val = brcmnand_readl(mmio);

	if (val & BCM63138_CTLRDY) {
		brcmnand_writel(val & ~BCM63138_CTLRDY, mmio);
		return true;
	}

	return false;
}

static void bcm63138_nand_intc_set(struct brcmnand_soc *soc, bool en)
{
	struct bcm63138_nand_soc *priv =
			container_of(soc, struct bcm63138_nand_soc, soc);
	void __iomem *mmio = priv->base + BCM63138_NAND_INT_EN;
	u32 val = brcmnand_readl(mmio);

	if (en)
		val |= BCM63138_CTLRDY;
	else
		val &= ~BCM63138_CTLRDY;

	brcmnand_writel(val, mmio);
}

static int bcm63138_nand_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bcm63138_nand_soc *priv;
	struct brcmnand_soc *soc;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	soc = &priv->soc;

	priv->base = devm_platform_ioremap_resource_byname(pdev, "nand-int-base");
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	soc->ctlrdy_ack = bcm63138_nand_intc_ack;
	soc->ctlrdy_set_enabled = bcm63138_nand_intc_set;

	return brcmnand_probe(pdev, soc);
}

static const struct of_device_id bcm63138_nand_of_match[] = {
	{ .compatible = "brcm,nand-bcm63138" },
	{},
};
MODULE_DEVICE_TABLE(of, bcm63138_nand_of_match);

static struct platform_driver bcm63138_nand_driver = {
	.probe			= bcm63138_nand_probe,
	.remove_new		= brcmnand_remove,
	.driver = {
		.name		= "bcm63138_nand",
		.pm		= &brcmnand_pm_ops,
		.of_match_table	= bcm63138_nand_of_match,
	}
};
module_platform_driver(bcm63138_nand_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Brian Norris");
MODULE_DESCRIPTION("NAND driver for BCM63138");
