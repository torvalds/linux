// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2006-2007 PA Semi, Inc
 *
 * Author: Egor Martovetsky <egor@pasemi.com>
 * Maintained by: Olof Johansson <olof@lixom.net>
 *
 * Driver for the PWRficient onchip NAND flash interface
 */

#undef DEBUG

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pci.h>

#include <asm/io.h>

#define LBICTRL_LPCCTL_NR		0x00004000
#define CLE_PIN_CTL			15
#define ALE_PIN_CTL			14

struct pasemi_ddata {
	struct nand_chip chip;
	unsigned int lpcctl;
	struct nand_controller controller;
};

static const char driver_name[] = "pasemi-nand";

static void pasemi_read_buf(struct nand_chip *chip, u_char *buf, int len)
{
	while (len > 0x800) {
		memcpy_fromio(buf, chip->legacy.IO_ADDR_R, 0x800);
		buf += 0x800;
		len -= 0x800;
	}
	memcpy_fromio(buf, chip->legacy.IO_ADDR_R, len);
}

static void pasemi_write_buf(struct nand_chip *chip, const u_char *buf,
			     int len)
{
	while (len > 0x800) {
		memcpy_toio(chip->legacy.IO_ADDR_R, buf, 0x800);
		buf += 0x800;
		len -= 0x800;
	}
	memcpy_toio(chip->legacy.IO_ADDR_R, buf, len);
}

static void pasemi_hwcontrol(struct nand_chip *chip, int cmd,
			     unsigned int ctrl)
{
	struct pasemi_ddata *ddata = container_of(chip, struct pasemi_ddata, chip);

	if (cmd == NAND_CMD_NONE)
		return;

	if (ctrl & NAND_CLE)
		out_8(chip->legacy.IO_ADDR_W + (1 << CLE_PIN_CTL), cmd);
	else
		out_8(chip->legacy.IO_ADDR_W + (1 << ALE_PIN_CTL), cmd);

	/* Push out posted writes */
	eieio();
	inl(ddata->lpcctl);
}

static int pasemi_device_ready(struct nand_chip *chip)
{
	struct pasemi_ddata *ddata = container_of(chip, struct pasemi_ddata, chip);

	return !!(inl(ddata->lpcctl) & LBICTRL_LPCCTL_NR);
}

static int pasemi_attach_chip(struct nand_chip *chip)
{
	if (chip->ecc.engine_type == NAND_ECC_ENGINE_TYPE_SOFT &&
	    chip->ecc.algo == NAND_ECC_ALGO_UNKNOWN)
		chip->ecc.algo = NAND_ECC_ALGO_HAMMING;

	return 0;
}

static const struct nand_controller_ops pasemi_ops = {
	.attach_chip = pasemi_attach_chip,
};

static int pasemi_nand_probe(struct platform_device *ofdev)
{
	struct device *dev = &ofdev->dev;
	struct pci_dev *pdev;
	struct device_node *np = dev->of_node;
	struct resource res;
	struct nand_chip *chip;
	struct nand_controller *controller;
	int err = 0;
	struct pasemi_ddata *ddata;
	struct mtd_info *pasemi_nand_mtd;

	err = of_address_to_resource(np, 0, &res);

	if (err)
		return -EINVAL;

	dev_dbg(dev, "pasemi_nand at %pR\n", &res);

	/* Allocate memory for MTD device structure and private data */
	ddata = kzalloc(sizeof(*ddata), GFP_KERNEL);
	if (!ddata) {
		err = -ENOMEM;
		goto out;
	}
	platform_set_drvdata(ofdev, ddata);
	chip = &ddata->chip;
	controller = &ddata->controller;

	controller->ops = &pasemi_ops;
	nand_controller_init(controller);
	chip->controller = controller;

	pasemi_nand_mtd = nand_to_mtd(chip);

	/* Link the private data with the MTD structure */
	pasemi_nand_mtd->dev.parent = dev;

	chip->legacy.IO_ADDR_R = of_iomap(np, 0);
	chip->legacy.IO_ADDR_W = chip->legacy.IO_ADDR_R;

	if (!chip->legacy.IO_ADDR_R) {
		err = -EIO;
		goto out_mtd;
	}

	pdev = pci_get_device(PCI_VENDOR_ID_PASEMI, 0xa008, NULL);
	if (!pdev) {
		err = -ENODEV;
		goto out_ior;
	}

	ddata->lpcctl = pci_resource_start(pdev, 0);
	pci_dev_put(pdev);

	if (!request_region(ddata->lpcctl, 4, driver_name)) {
		err = -EBUSY;
		goto out_ior;
	}

	chip->legacy.cmd_ctrl = pasemi_hwcontrol;
	chip->legacy.dev_ready = pasemi_device_ready;
	chip->legacy.read_buf = pasemi_read_buf;
	chip->legacy.write_buf = pasemi_write_buf;
	chip->legacy.chip_delay = 0;

	/* Enable the following for a flash based bad block table */
	chip->bbt_options = NAND_BBT_USE_FLASH;

	/*
	 * This driver assumes that the default ECC engine should be TYPE_SOFT.
	 * Set ->engine_type before registering the NAND devices in order to
	 * provide a driver specific default value.
	 */
	chip->ecc.engine_type = NAND_ECC_ENGINE_TYPE_SOFT;

	/* Scan to find existence of the device */
	err = nand_scan(chip, 1);
	if (err)
		goto out_lpc;

	if (mtd_device_register(pasemi_nand_mtd, NULL, 0)) {
		dev_err(dev, "Unable to register MTD device\n");
		err = -ENODEV;
		goto out_cleanup_nand;
	}

	dev_info(dev, "PA Semi NAND flash at %pR, control at I/O %x\n", &res,
		 ddata->lpcctl);

	return 0;

 out_cleanup_nand:
	nand_cleanup(chip);
 out_lpc:
	release_region(ddata->lpcctl, 4);
 out_ior:
	iounmap(chip->legacy.IO_ADDR_R);
 out_mtd:
	kfree(ddata);
 out:
	return err;
}

static void pasemi_nand_remove(struct platform_device *ofdev)
{
	struct pasemi_ddata *ddata = platform_get_drvdata(ofdev);
	struct mtd_info *pasemi_nand_mtd;
	int ret;
	struct nand_chip *chip;

	chip = &ddata->chip;
	pasemi_nand_mtd = nand_to_mtd(chip);

	/* Release resources, unregister device */
	ret = mtd_device_unregister(pasemi_nand_mtd);
	WARN_ON(ret);
	nand_cleanup(chip);

	release_region(ddata->lpcctl, 4);

	iounmap(chip->legacy.IO_ADDR_R);

	/* Free the MTD device structure */
	kfree(ddata);
}

static const struct of_device_id pasemi_nand_match[] =
{
	{
		.compatible   = "pasemi,localbus-nand",
	},
	{},
};

MODULE_DEVICE_TABLE(of, pasemi_nand_match);

static struct platform_driver pasemi_nand_driver =
{
	.driver = {
		.name = driver_name,
		.of_match_table = pasemi_nand_match,
	},
	.probe		= pasemi_nand_probe,
	.remove		= pasemi_nand_remove,
};

module_platform_driver(pasemi_nand_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Egor Martovetsky <egor@pasemi.com>");
MODULE_DESCRIPTION("NAND flash interface driver for PA Semi PWRficient");
