// SPDX-License-Identifier: GPL-2.0-only
/*
 * BCM47XX NAND flash driver
 *
 * Copyright (C) 2012 Rafał Miłecki <zajec5@gmail.com>
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
	struct mtd_info *mtd;
	int err = 0;

	b47n = devm_kzalloc(&pdev->dev, sizeof(*b47n), GFP_KERNEL);
	if (!b47n)
		return -ENOMEM;

	nand_set_controller_data(&b47n->nand_chip, b47n);
	mtd = nand_to_mtd(&b47n->nand_chip);
	mtd->dev.parent = &pdev->dev;
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

	platform_set_drvdata(pdev, b47n);

	err = mtd_device_parse_register(mtd, probes, NULL, NULL, 0);
	if (err) {
		pr_err("Failed to register MTD device: %d\n", err);
		return err;
	}

	return 0;
}

static void bcm47xxnflash_remove(struct platform_device *pdev)
{
	struct bcm47xxnflash *nflash = platform_get_drvdata(pdev);
	struct nand_chip *chip = &nflash->nand_chip;
	int ret;

	ret = mtd_device_unregister(nand_to_mtd(chip));
	WARN_ON(ret);
	nand_cleanup(chip);
}

static struct platform_driver bcm47xxnflash_driver = {
	.probe	= bcm47xxnflash_probe,
	.remove_new = bcm47xxnflash_remove,
	.driver = {
		.name = "bcma_nflash",
	},
};

module_platform_driver(bcm47xxnflash_driver);
