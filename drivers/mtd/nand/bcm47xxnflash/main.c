/*
 * BCM47XX NAND flash driver
 *
 * Copyright (C) 2012 Rafał Miłecki <zajec5@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include "bcm47xxnflash.h"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/bcma/bcma.h>

MODULE_DESCRIPTION("NAND flash driver for BCMA bus");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rafał Miłecki");

static const char *probes[] = { "bcm47xxpart", NULL };

static int bcm47xxnflash_probe(struct platform_device *pdev)
{
	struct bcma_nflash *nflash = dev_get_platdata(&pdev->dev);
	struct bcm47xxnflash *b47n;
	int err = 0;

	b47n = devm_kzalloc(&pdev->dev, sizeof(*b47n), GFP_KERNEL);
	if (!b47n)
		return -ENOMEM;

	b47n->nand_chip.priv = b47n;
	b47n->mtd.owner = THIS_MODULE;
	b47n->mtd.priv = &b47n->nand_chip; /* Required */
	b47n->cc = container_of(nflash, struct bcma_drv_cc, nflash);

	if (b47n->cc->core->bus->chipinfo.id == BCMA_CHIP_ID_BCM4706) {
		err = bcm47xxnflash_ops_bcm4706_init(b47n);
	} else {
		pr_err("Device not supported\n");
		err = -ENOTSUPP;
	}
	if (err) {
		pr_err("Initialization failed: %d\n", err);
		return err;
	}

	err = mtd_device_parse_register(&b47n->mtd, probes, NULL, NULL, 0);
	if (err) {
		pr_err("Failed to register MTD device: %d\n", err);
		return err;
	}

	return 0;
}

static int bcm47xxnflash_remove(struct platform_device *pdev)
{
	struct bcma_nflash *nflash = dev_get_platdata(&pdev->dev);

	if (nflash->mtd)
		mtd_device_unregister(nflash->mtd);

	return 0;
}

static struct platform_driver bcm47xxnflash_driver = {
	.probe	= bcm47xxnflash_probe,
	.remove = bcm47xxnflash_remove,
	.driver = {
		.name = "bcma_nflash",
	},
};

module_platform_driver(bcm47xxnflash_driver);
