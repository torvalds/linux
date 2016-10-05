/*
 * Copyright 2016 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "spi-bcm-qspi.h"

#define INTR_BASE_BIT_SHIFT			0x02
#define INTR_COUNT				0x07

struct bcm_iproc_intc {
	struct bcm_qspi_soc_intc soc_intc;
	struct platform_device *pdev;
	void __iomem *int_reg;
	void __iomem *int_status_reg;
	spinlock_t soclock;
	bool big_endian;
};

static u32 bcm_iproc_qspi_get_l2_int_status(struct bcm_qspi_soc_intc *soc_intc)
{
	struct bcm_iproc_intc *priv =
			container_of(soc_intc, struct bcm_iproc_intc, soc_intc);
	void __iomem *mmio = priv->int_status_reg;
	int i;
	u32 val = 0, sts = 0;

	for (i = 0; i < INTR_COUNT; i++) {
		if (bcm_qspi_readl(priv->big_endian, mmio + (i * 4)))
			val |= 1UL << i;
	}

	if (val & INTR_MSPI_DONE_MASK)
		sts |= MSPI_DONE;

	if (val & BSPI_LR_INTERRUPTS_ALL)
		sts |= BSPI_DONE;

	if (val & BSPI_LR_INTERRUPTS_ERROR)
		sts |= BSPI_ERR;

	return sts;
}

static void bcm_iproc_qspi_int_ack(struct bcm_qspi_soc_intc *soc_intc, int type)
{
	struct bcm_iproc_intc *priv =
			container_of(soc_intc, struct bcm_iproc_intc, soc_intc);
	void __iomem *mmio = priv->int_status_reg;
	u32 mask = get_qspi_mask(type);
	int i;

	for (i = 0; i < INTR_COUNT; i++) {
		if (mask & (1UL << i))
			bcm_qspi_writel(priv->big_endian, 1, mmio + (i * 4));
	}
}

static void bcm_iproc_qspi_int_set(struct bcm_qspi_soc_intc *soc_intc, int type,
				   bool en)
{
	struct bcm_iproc_intc *priv =
			container_of(soc_intc, struct bcm_iproc_intc, soc_intc);
	void __iomem *mmio = priv->int_reg;
	u32 mask = get_qspi_mask(type);
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&priv->soclock, flags);

	val = bcm_qspi_readl(priv->big_endian, mmio);

	if (en)
		val = val | (mask << INTR_BASE_BIT_SHIFT);
	else
		val = val & ~(mask << INTR_BASE_BIT_SHIFT);

	bcm_qspi_writel(priv->big_endian, val, mmio);

	spin_unlock_irqrestore(&priv->soclock, flags);
}

static int bcm_iproc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bcm_iproc_intc *priv;
	struct bcm_qspi_soc_intc *soc_intc;
	struct resource *res;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	soc_intc = &priv->soc_intc;
	priv->pdev = pdev;

	spin_lock_init(&priv->soclock);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "intr_regs");
	priv->int_reg = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->int_reg))
		return PTR_ERR(priv->int_reg);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "intr_status_reg");
	priv->int_status_reg = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->int_status_reg))
		return PTR_ERR(priv->int_status_reg);

	priv->big_endian = of_device_is_big_endian(dev->of_node);

	bcm_iproc_qspi_int_ack(soc_intc, MSPI_BSPI_DONE);
	bcm_iproc_qspi_int_set(soc_intc, MSPI_BSPI_DONE, false);

	soc_intc->bcm_qspi_int_ack = bcm_iproc_qspi_int_ack;
	soc_intc->bcm_qspi_int_set = bcm_iproc_qspi_int_set;
	soc_intc->bcm_qspi_get_int_status = bcm_iproc_qspi_get_l2_int_status;

	return bcm_qspi_probe(pdev, soc_intc);
}

static int bcm_iproc_remove(struct platform_device *pdev)
{
	return bcm_qspi_remove(pdev);
}

static const struct of_device_id bcm_iproc_of_match[] = {
	{ .compatible = "brcm,spi-nsp-qspi" },
	{ .compatible = "brcm,spi-ns2-qspi" },
	{},
};
MODULE_DEVICE_TABLE(of, bcm_iproc_of_match);

static struct platform_driver bcm_iproc_driver = {
	.probe			= bcm_iproc_probe,
	.remove			= bcm_iproc_remove,
	.driver = {
		.name		= "bcm_iproc",
		.pm		= &bcm_qspi_pm_ops,
		.of_match_table = bcm_iproc_of_match,
	}
};
module_platform_driver(bcm_iproc_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Kamal Dasu");
MODULE_DESCRIPTION("SPI flash driver for Broadcom iProc SoCs");
