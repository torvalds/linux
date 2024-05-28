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

struct bcmbca_nand_soc {
	struct brcmnand_soc soc;
	void __iomem *base;
};

#define BCMBCA_NAND_INT_STATUS		0x00
#define BCMBCA_NAND_INT_EN			0x04

enum {
	BCMBCA_CTLRDY		= BIT(4),
};

#if defined(CONFIG_ARM64)
#define ALIGN_REQ		8
#else
#define ALIGN_REQ		4
#endif

static inline bool bcmbca_nand_is_buf_aligned(void *flash_cache,  void *buffer)
{
	return IS_ALIGNED((uintptr_t)buffer, ALIGN_REQ) &&
				IS_ALIGNED((uintptr_t)flash_cache, ALIGN_REQ);
}

static bool bcmbca_nand_intc_ack(struct brcmnand_soc *soc)
{
	struct bcmbca_nand_soc *priv =
			container_of(soc, struct bcmbca_nand_soc, soc);
	void __iomem *mmio = priv->base + BCMBCA_NAND_INT_STATUS;
	u32 val = brcmnand_readl(mmio);

	if (val & BCMBCA_CTLRDY) {
		brcmnand_writel(val & ~BCMBCA_CTLRDY, mmio);
		return true;
	}

	return false;
}

static void bcmbca_nand_intc_set(struct brcmnand_soc *soc, bool en)
{
	struct bcmbca_nand_soc *priv =
			container_of(soc, struct bcmbca_nand_soc, soc);
	void __iomem *mmio = priv->base + BCMBCA_NAND_INT_EN;
	u32 val = brcmnand_readl(mmio);

	if (en)
		val |= BCMBCA_CTLRDY;
	else
		val &= ~BCMBCA_CTLRDY;

	brcmnand_writel(val, mmio);
}

static void bcmbca_read_data_bus(struct brcmnand_soc *soc,
				 void __iomem *flash_cache,  u32 *buffer, int fc_words)
{
	/*
	 * memcpy can do unaligned aligned access depending on source
	 * and dest address, which is incompatible with nand cache. Fallback
	 * to the memcpy_fromio in such case
	 */
	if (bcmbca_nand_is_buf_aligned((void __force *)flash_cache, buffer))
		memcpy((void *)buffer, (void __force *)flash_cache, fc_words * 4);
	else
		memcpy_fromio((void *)buffer, flash_cache, fc_words * 4);
}

static int bcmbca_nand_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bcmbca_nand_soc *priv;
	struct brcmnand_soc *soc;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	soc = &priv->soc;

	priv->base = devm_platform_ioremap_resource_byname(pdev, "nand-int-base");
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	soc->ctlrdy_ack = bcmbca_nand_intc_ack;
	soc->ctlrdy_set_enabled = bcmbca_nand_intc_set;
	soc->read_data_bus = bcmbca_read_data_bus;

	return brcmnand_probe(pdev, soc);
}

static const struct of_device_id bcmbca_nand_of_match[] = {
	{ .compatible = "brcm,nand-bcm63138" },
	{},
};
MODULE_DEVICE_TABLE(of, bcmbca_nand_of_match);

static struct platform_driver bcmbca_nand_driver = {
	.probe			= bcmbca_nand_probe,
	.remove_new		= brcmnand_remove,
	.driver = {
		.name		= "bcmbca_nand",
		.pm		= &brcmnand_pm_ops,
		.of_match_table	= bcmbca_nand_of_match,
	}
};
module_platform_driver(bcmbca_nand_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Brian Norris");
MODULE_DESCRIPTION("NAND driver for BCMBCA");
