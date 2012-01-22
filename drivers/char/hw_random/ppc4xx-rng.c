/*
 * Generic PowerPC 44x RNG driver
 *
 * Copyright 2011 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/hw_random.h>
#include <linux/delay.h>
#include <linux/of_platform.h>
#include <asm/io.h>

#define PPC4XX_TRNG_DEV_CTRL 0x60080

#define PPC4XX_TRNGE 0x00020000
#define PPC4XX_TRNG_CTRL 0x0008
#define PPC4XX_TRNG_CTRL_DALM 0x20
#define PPC4XX_TRNG_STAT 0x0004
#define PPC4XX_TRNG_STAT_B 0x1
#define PPC4XX_TRNG_DATA 0x0000

#define MODULE_NAME "ppc4xx_rng"

static int ppc4xx_rng_data_present(struct hwrng *rng, int wait)
{
	void __iomem *rng_regs = (void __iomem *) rng->priv;
	int busy, i, present = 0;

	for (i = 0; i < 20; i++) {
		busy = (in_le32(rng_regs + PPC4XX_TRNG_STAT) & PPC4XX_TRNG_STAT_B);
		if (!busy || !wait) {
			present = 1;
			break;
		}
		udelay(10);
	}
	return present;
}

static int ppc4xx_rng_data_read(struct hwrng *rng, u32 *data)
{
	void __iomem *rng_regs = (void __iomem *) rng->priv;
	*data = in_le32(rng_regs + PPC4XX_TRNG_DATA);
	return 4;
}

static int ppc4xx_rng_enable(int enable)
{
	struct device_node *ctrl;
	void __iomem *ctrl_reg;
	int err = 0;
	u32 val;

	/* Find the main crypto device node and map it to turn the TRNG on */
	ctrl = of_find_compatible_node(NULL, NULL, "amcc,ppc4xx-crypto");
	if (!ctrl)
		return -ENODEV;

	ctrl_reg = of_iomap(ctrl, 0);
	if (!ctrl_reg) {
		err = -ENODEV;
		goto out;
	}

	val = in_le32(ctrl_reg + PPC4XX_TRNG_DEV_CTRL);

	if (enable)
		val |= PPC4XX_TRNGE;
	else
		val = val & ~PPC4XX_TRNGE;

	out_le32(ctrl_reg + PPC4XX_TRNG_DEV_CTRL, val);
	iounmap(ctrl_reg);

out:
	of_node_put(ctrl);

	return err;
}

static struct hwrng ppc4xx_rng = {
	.name = MODULE_NAME,
	.data_present = ppc4xx_rng_data_present,
	.data_read = ppc4xx_rng_data_read,
};

static int __devinit ppc4xx_rng_probe(struct platform_device *dev)
{
	void __iomem *rng_regs;
	int err = 0;

	rng_regs = of_iomap(dev->dev.of_node, 0);
	if (!rng_regs)
		return -ENODEV;

	err = ppc4xx_rng_enable(1);
	if (err)
		return err;

	out_le32(rng_regs + PPC4XX_TRNG_CTRL, PPC4XX_TRNG_CTRL_DALM);
	ppc4xx_rng.priv = (unsigned long) rng_regs;

	err = hwrng_register(&ppc4xx_rng);

	return err;
}

static int __devexit ppc4xx_rng_remove(struct platform_device *dev)
{
	void __iomem *rng_regs = (void __iomem *) ppc4xx_rng.priv;

	hwrng_unregister(&ppc4xx_rng);
	ppc4xx_rng_enable(0);
	iounmap(rng_regs);

	return 0;
}

static struct of_device_id ppc4xx_rng_match[] = {
	{ .compatible = "ppc4xx-rng", },
	{ .compatible = "amcc,ppc460ex-rng", },
	{ .compatible = "amcc,ppc440epx-rng", },
	{},
};

static struct platform_driver ppc4xx_rng_driver = {
	.driver = {
		.name = MODULE_NAME,
		.owner = THIS_MODULE,
		.of_match_table = ppc4xx_rng_match,
	},
	.probe = ppc4xx_rng_probe,
	.remove = ppc4xx_rng_remove,
};

module_platform_driver(ppc4xx_rng_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Josh Boyer <jwboyer@linux.vnet.ibm.com>");
MODULE_DESCRIPTION("HW RNG driver for PPC 4xx processors");
