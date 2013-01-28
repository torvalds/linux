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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/bcma/bcma.h>

#include "bcm47xxnflash.h"

MODULE_DESCRIPTION("NAND flash driver for BCMA bus");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rafał Miłecki");

static const char *probes[] = { "bcm47xxpart", NULL };

static int bcm47xxnflash_probe(struct platform_device *pdev)
{
	struct bcma_nflash *nflash = dev_get_platdata(&pdev->dev);
	struct bcm47xxnflash *b47n;
	int err = 0;

	b47n = kzalloc(sizeof(*b47n), GFP_KERNEL);
	if (!b47n) {
		err = -ENOMEM;
		goto out;
	}

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
		goto err_init;
	}

	err = mtd_device_parse_register(&b47n->mtd, probes, NULL, NULL, 0);
	if (err) {
		pr_err("Failed to register MTD device: %d\n", err);
		goto err_dev_reg;
	}

	return 0;

err_dev_reg:
err_init:
	kfree(b47n);
out:
	return err;
}

static int bcm47xxnflash_remove(struct platform_device *pdev)
{
	struct bcma_nflash *nflash = dev_get_platdata(&pdev->dev);

	if (nflash->mtd)
		mtd_device_unregister(nflash->mtd);

	return 0;
}

static struct platform_driver bcm47xxnflash_driver = {
	.remove = bcm47xxnflash_remove,
	.driver = {
		.name = "bcma_nflash",
		.owner = THIS_MODULE,
	},
};

static int __init bcm47xxnflash_init(void)
{
	int err;

	/*
	 * Platform device "bcma_nflash" exists on SoCs and is registered very
	 * early, it won't be added during runtime (use platform_driver_probe).
	 */
	err = platform_driver_probe(&bcm47xxnflash_driver, bcm47xxnflash_probe);
	if (err)
		pr_err("Failed to register bcm47xx nand flash driver: %d\n",
		       err);

	return err;
}

static void __exit bcm47xxnflash_exit(void)
{
	platform_driver_unregister(&bcm47xxnflash_driver);
}

module_init(bcm47xxnflash_init);
module_exit(bcm47xxnflash_exit);
