// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2015 Simon Arlott
 *
 * Derived from bcm63138_nand.c:
 * Copyright © 2015 Broadcom Corporation
 *
 * Derived from bcm963xx_4.12L.06B_consumer/shared/opensource/include/bcm963xx/63268_map_part.h:
 * Copyright 2000-2010 Broadcom Corporation
 *
 * Derived from bcm963xx_4.12L.06B_consumer/shared/opensource/flash/nandflash.c:
 * Copyright 2000-2010 Broadcom Corporation
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

struct bcm6368_nand_soc {
	struct brcmnand_soc soc;
	void __iomem *base;
};

#define BCM6368_NAND_INT		0x00
#define  BCM6368_NAND_STATUS_SHIFT	0
#define  BCM6368_NAND_STATUS_MASK	(0xfff << BCM6368_NAND_STATUS_SHIFT)
#define  BCM6368_NAND_ENABLE_SHIFT	16
#define  BCM6368_NAND_ENABLE_MASK	(0xffff << BCM6368_NAND_ENABLE_SHIFT)
#define BCM6368_NAND_BASE_ADDR0	0x04
#define BCM6368_NAND_BASE_ADDR1	0x0c

enum {
	BCM6368_NP_READ		= BIT(0),
	BCM6368_BLOCK_ERASE	= BIT(1),
	BCM6368_COPY_BACK	= BIT(2),
	BCM6368_PAGE_PGM	= BIT(3),
	BCM6368_CTRL_READY	= BIT(4),
	BCM6368_DEV_RBPIN	= BIT(5),
	BCM6368_ECC_ERR_UNC	= BIT(6),
	BCM6368_ECC_ERR_CORR	= BIT(7),
};

static bool bcm6368_nand_intc_ack(struct brcmnand_soc *soc)
{
	struct bcm6368_nand_soc *priv =
			container_of(soc, struct bcm6368_nand_soc, soc);
	void __iomem *mmio = priv->base + BCM6368_NAND_INT;
	u32 val = brcmnand_readl(mmio);

	if (val & (BCM6368_CTRL_READY << BCM6368_NAND_STATUS_SHIFT)) {
		/* Ack interrupt */
		val &= ~BCM6368_NAND_STATUS_MASK;
		val |= BCM6368_CTRL_READY << BCM6368_NAND_STATUS_SHIFT;
		brcmnand_writel(val, mmio);
		return true;
	}

	return false;
}

static void bcm6368_nand_intc_set(struct brcmnand_soc *soc, bool en)
{
	struct bcm6368_nand_soc *priv =
			container_of(soc, struct bcm6368_nand_soc, soc);
	void __iomem *mmio = priv->base + BCM6368_NAND_INT;
	u32 val = brcmnand_readl(mmio);

	/* Don't ack any interrupts */
	val &= ~BCM6368_NAND_STATUS_MASK;

	if (en)
		val |= BCM6368_CTRL_READY << BCM6368_NAND_ENABLE_SHIFT;
	else
		val &= ~(BCM6368_CTRL_READY << BCM6368_NAND_ENABLE_SHIFT);

	brcmnand_writel(val, mmio);
}

static int bcm6368_nand_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bcm6368_nand_soc *priv;
	struct brcmnand_soc *soc;
	struct resource *res;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	soc = &priv->soc;

	res = platform_get_resource_byname(pdev,
		IORESOURCE_MEM, "nand-int-base");
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	soc->ctlrdy_ack = bcm6368_nand_intc_ack;
	soc->ctlrdy_set_enabled = bcm6368_nand_intc_set;

	/* Disable and ack all interrupts  */
	brcmnand_writel(0, priv->base + BCM6368_NAND_INT);
	brcmnand_writel(BCM6368_NAND_STATUS_MASK,
			priv->base + BCM6368_NAND_INT);

	return brcmnand_probe(pdev, soc);
}

static const struct of_device_id bcm6368_nand_of_match[] = {
	{ .compatible = "brcm,nand-bcm6368" },
	{},
};
MODULE_DEVICE_TABLE(of, bcm6368_nand_of_match);

static struct platform_driver bcm6368_nand_driver = {
	.probe			= bcm6368_nand_probe,
	.remove			= brcmnand_remove,
	.driver = {
		.name		= "bcm6368_nand",
		.pm		= &brcmnand_pm_ops,
		.of_match_table	= bcm6368_nand_of_match,
	}
};
module_platform_driver(bcm6368_nand_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Simon Arlott");
MODULE_DESCRIPTION("NAND driver for BCM6368");
