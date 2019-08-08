// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic PowerPC 44x RNG driver
 *
 * Copyright 2011 IBM Corporation
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/hw_random.h>
#include <linux/delay.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/io.h>

#include "crypto4xx_core.h"
#include "crypto4xx_trng.h"
#include "crypto4xx_reg_def.h"

#define PPC4XX_TRNG_CTRL	0x0008
#define PPC4XX_TRNG_CTRL_DALM	0x20
#define PPC4XX_TRNG_STAT	0x0004
#define PPC4XX_TRNG_STAT_B	0x1
#define PPC4XX_TRNG_DATA	0x0000

static int ppc4xx_trng_data_present(struct hwrng *rng, int wait)
{
	struct crypto4xx_device *dev = (void *)rng->priv;
	int busy, i, present = 0;

	for (i = 0; i < 20; i++) {
		busy = (in_le32(dev->trng_base + PPC4XX_TRNG_STAT) &
			PPC4XX_TRNG_STAT_B);
		if (!busy || !wait) {
			present = 1;
			break;
		}
		udelay(10);
	}
	return present;
}

static int ppc4xx_trng_data_read(struct hwrng *rng, u32 *data)
{
	struct crypto4xx_device *dev = (void *)rng->priv;
	*data = in_le32(dev->trng_base + PPC4XX_TRNG_DATA);
	return 4;
}

static void ppc4xx_trng_enable(struct crypto4xx_device *dev, bool enable)
{
	u32 device_ctrl;

	device_ctrl = readl(dev->ce_base + CRYPTO4XX_DEVICE_CTRL);
	if (enable)
		device_ctrl |= PPC4XX_TRNG_EN;
	else
		device_ctrl &= ~PPC4XX_TRNG_EN;
	writel(device_ctrl, dev->ce_base + CRYPTO4XX_DEVICE_CTRL);
}

static const struct of_device_id ppc4xx_trng_match[] = {
	{ .compatible = "ppc4xx-rng", },
	{ .compatible = "amcc,ppc460ex-rng", },
	{ .compatible = "amcc,ppc440epx-rng", },
	{},
};

void ppc4xx_trng_probe(struct crypto4xx_core_device *core_dev)
{
	struct crypto4xx_device *dev = core_dev->dev;
	struct device_node *trng = NULL;
	struct hwrng *rng = NULL;
	int err;

	/* Find the TRNG device node and map it */
	trng = of_find_matching_node(NULL, ppc4xx_trng_match);
	if (!trng || !of_device_is_available(trng)) {
		of_node_put(trng);
		return;
	}

	dev->trng_base = of_iomap(trng, 0);
	of_node_put(trng);
	if (!dev->trng_base)
		goto err_out;

	rng = kzalloc(sizeof(*rng), GFP_KERNEL);
	if (!rng)
		goto err_out;

	rng->name = KBUILD_MODNAME;
	rng->data_present = ppc4xx_trng_data_present;
	rng->data_read = ppc4xx_trng_data_read;
	rng->priv = (unsigned long) dev;
	core_dev->trng = rng;
	ppc4xx_trng_enable(dev, true);
	out_le32(dev->trng_base + PPC4XX_TRNG_CTRL, PPC4XX_TRNG_CTRL_DALM);
	err = devm_hwrng_register(core_dev->device, core_dev->trng);
	if (err) {
		ppc4xx_trng_enable(dev, false);
		dev_err(core_dev->device, "failed to register hwrng (%d).\n",
			err);
		goto err_out;
	}
	return;

err_out:
	of_node_put(trng);
	iounmap(dev->trng_base);
	kfree(rng);
	dev->trng_base = NULL;
	core_dev->trng = NULL;
}

void ppc4xx_trng_remove(struct crypto4xx_core_device *core_dev)
{
	if (core_dev && core_dev->trng) {
		struct crypto4xx_device *dev = core_dev->dev;

		devm_hwrng_unregister(core_dev->device, core_dev->trng);
		ppc4xx_trng_enable(dev, false);
		iounmap(dev->trng_base);
		kfree(core_dev->trng);
	}
}

MODULE_ALIAS("ppc4xx_rng");
