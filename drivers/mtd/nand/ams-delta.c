/*
 *  drivers/mtd/nand/ams-delta.c
 *
 *  Copyright (C) 2006 Jonathan McDowell <noodles@earth.li>
 *
 *  Derived from drivers/mtd/toto.c
 *  Converted to platform driver by Janusz Krzysztofik <jkrzyszt@tis.icnet.pl>
 *  Partially stolen from drivers/mtd/nand/plat_nand.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Overview:
 *   This is a device driver for the NAND flash device found on the
 *   Amstrad E3 (Delta).
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/gpio.h>
#include <linux/platform_data/gpio-omap.h>

#include <asm/io.h>
#include <asm/sizes.h>

#include <mach/board-ams-delta.h>

#include <mach/hardware.h>

/*
 * MTD structure for E3 (Delta)
 */
static struct mtd_info *ams_delta_mtd = NULL;

/*
 * Define partitions for flash devices
 */

static struct mtd_partition partition_info[] = {
	{ .name		= "Kernel",
	  .offset	= 0,
	  .size		= 3 * SZ_1M + SZ_512K },
	{ .name		= "u-boot",
	  .offset	= 3 * SZ_1M + SZ_512K,
	  .size		= SZ_256K },
	{ .name		= "u-boot params",
	  .offset	= 3 * SZ_1M + SZ_512K + SZ_256K,
	  .size		= SZ_256K },
	{ .name		= "Amstrad LDR",
	  .offset	= 4 * SZ_1M,
	  .size		= SZ_256K },
	{ .name		= "File system",
	  .offset	= 4 * SZ_1M + 1 * SZ_256K,
	  .size		= 27 * SZ_1M },
	{ .name		= "PBL reserved",
	  .offset	= 32 * SZ_1M - 3 * SZ_256K,
	  .size		=  3 * SZ_256K },
};

static void ams_delta_write_byte(struct mtd_info *mtd, u_char byte)
{
	struct nand_chip *this = mtd_to_nand(mtd);
	void __iomem *io_base = this->priv;

	writew(0, io_base + OMAP_MPUIO_IO_CNTL);
	writew(byte, this->IO_ADDR_W);
	gpio_set_value(AMS_DELTA_GPIO_PIN_NAND_NWE, 0);
	ndelay(40);
	gpio_set_value(AMS_DELTA_GPIO_PIN_NAND_NWE, 1);
}

static u_char ams_delta_read_byte(struct mtd_info *mtd)
{
	u_char res;
	struct nand_chip *this = mtd_to_nand(mtd);
	void __iomem *io_base = this->priv;

	gpio_set_value(AMS_DELTA_GPIO_PIN_NAND_NRE, 0);
	ndelay(40);
	writew(~0, io_base + OMAP_MPUIO_IO_CNTL);
	res = readw(this->IO_ADDR_R);
	gpio_set_value(AMS_DELTA_GPIO_PIN_NAND_NRE, 1);

	return res;
}

static void ams_delta_write_buf(struct mtd_info *mtd, const u_char *buf,
				int len)
{
	int i;

	for (i=0; i<len; i++)
		ams_delta_write_byte(mtd, buf[i]);
}

static void ams_delta_read_buf(struct mtd_info *mtd, u_char *buf, int len)
{
	int i;

	for (i=0; i<len; i++)
		buf[i] = ams_delta_read_byte(mtd);
}

/*
 * Command control function
 *
 * ctrl:
 * NAND_NCE: bit 0 -> bit 2
 * NAND_CLE: bit 1 -> bit 7
 * NAND_ALE: bit 2 -> bit 6
 */
static void ams_delta_hwcontrol(struct mtd_info *mtd, int cmd,
				unsigned int ctrl)
{

	if (ctrl & NAND_CTRL_CHANGE) {
		gpio_set_value(AMS_DELTA_GPIO_PIN_NAND_NCE,
				(ctrl & NAND_NCE) == 0);
		gpio_set_value(AMS_DELTA_GPIO_PIN_NAND_CLE,
				(ctrl & NAND_CLE) != 0);
		gpio_set_value(AMS_DELTA_GPIO_PIN_NAND_ALE,
				(ctrl & NAND_ALE) != 0);
	}

	if (cmd != NAND_CMD_NONE)
		ams_delta_write_byte(mtd, cmd);
}

static int ams_delta_nand_ready(struct mtd_info *mtd)
{
	return gpio_get_value(AMS_DELTA_GPIO_PIN_NAND_RB);
}

static const struct gpio _mandatory_gpio[] = {
	{
		.gpio	= AMS_DELTA_GPIO_PIN_NAND_NCE,
		.flags	= GPIOF_OUT_INIT_HIGH,
		.label	= "nand_nce",
	},
	{
		.gpio	= AMS_DELTA_GPIO_PIN_NAND_NRE,
		.flags	= GPIOF_OUT_INIT_HIGH,
		.label	= "nand_nre",
	},
	{
		.gpio	= AMS_DELTA_GPIO_PIN_NAND_NWP,
		.flags	= GPIOF_OUT_INIT_HIGH,
		.label	= "nand_nwp",
	},
	{
		.gpio	= AMS_DELTA_GPIO_PIN_NAND_NWE,
		.flags	= GPIOF_OUT_INIT_HIGH,
		.label	= "nand_nwe",
	},
	{
		.gpio	= AMS_DELTA_GPIO_PIN_NAND_ALE,
		.flags	= GPIOF_OUT_INIT_LOW,
		.label	= "nand_ale",
	},
	{
		.gpio	= AMS_DELTA_GPIO_PIN_NAND_CLE,
		.flags	= GPIOF_OUT_INIT_LOW,
		.label	= "nand_cle",
	},
};

/*
 * Main initialization routine
 */
static int ams_delta_init(struct platform_device *pdev)
{
	struct nand_chip *this;
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	void __iomem *io_base;
	int err = 0;

	if (!res)
		return -ENXIO;

	/* Allocate memory for MTD device structure and private data */
	ams_delta_mtd = kzalloc(sizeof(struct mtd_info) +
				sizeof(struct nand_chip), GFP_KERNEL);
	if (!ams_delta_mtd) {
		printk (KERN_WARNING "Unable to allocate E3 NAND MTD device structure.\n");
		err = -ENOMEM;
		goto out;
	}

	ams_delta_mtd->owner = THIS_MODULE;

	/* Get pointer to private data */
	this = (struct nand_chip *) (&ams_delta_mtd[1]);

	/* Link the private data with the MTD structure */
	ams_delta_mtd->priv = this;

	/*
	 * Don't try to request the memory region from here,
	 * it should have been already requested from the
	 * gpio-omap driver and requesting it again would fail.
	 */

	io_base = ioremap(res->start, resource_size(res));
	if (io_base == NULL) {
		dev_err(&pdev->dev, "ioremap failed\n");
		err = -EIO;
		goto out_free;
	}

	this->priv = io_base;

	/* Set address of NAND IO lines */
	this->IO_ADDR_R = io_base + OMAP_MPUIO_INPUT_LATCH;
	this->IO_ADDR_W = io_base + OMAP_MPUIO_OUTPUT;
	this->read_byte = ams_delta_read_byte;
	this->write_buf = ams_delta_write_buf;
	this->read_buf = ams_delta_read_buf;
	this->cmd_ctrl = ams_delta_hwcontrol;
	if (gpio_request(AMS_DELTA_GPIO_PIN_NAND_RB, "nand_rdy") == 0) {
		this->dev_ready = ams_delta_nand_ready;
	} else {
		this->dev_ready = NULL;
		printk(KERN_NOTICE "Couldn't request gpio for Delta NAND ready.\n");
	}
	/* 25 us command delay time */
	this->chip_delay = 30;
	this->ecc.mode = NAND_ECC_SOFT;

	platform_set_drvdata(pdev, io_base);

	/* Set chip enabled, but  */
	err = gpio_request_array(_mandatory_gpio, ARRAY_SIZE(_mandatory_gpio));
	if (err)
		goto out_gpio;

	/* Scan to find existence of the device */
	if (nand_scan(ams_delta_mtd, 1)) {
		err = -ENXIO;
		goto out_mtd;
	}

	/* Register the partitions */
	mtd_device_register(ams_delta_mtd, partition_info,
			    ARRAY_SIZE(partition_info));

	goto out;

 out_mtd:
	gpio_free_array(_mandatory_gpio, ARRAY_SIZE(_mandatory_gpio));
out_gpio:
	gpio_free(AMS_DELTA_GPIO_PIN_NAND_RB);
	iounmap(io_base);
out_free:
	kfree(ams_delta_mtd);
 out:
	return err;
}

/*
 * Clean up routine
 */
static int ams_delta_cleanup(struct platform_device *pdev)
{
	void __iomem *io_base = platform_get_drvdata(pdev);

	/* Release resources, unregister device */
	nand_release(ams_delta_mtd);

	gpio_free_array(_mandatory_gpio, ARRAY_SIZE(_mandatory_gpio));
	gpio_free(AMS_DELTA_GPIO_PIN_NAND_RB);
	iounmap(io_base);

	/* Free the MTD device structure */
	kfree(ams_delta_mtd);

	return 0;
}

static struct platform_driver ams_delta_nand_driver = {
	.probe		= ams_delta_init,
	.remove		= ams_delta_cleanup,
	.driver		= {
		.name	= "ams-delta-nand",
	},
};

module_platform_driver(ams_delta_nand_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jonathan McDowell <noodles@earth.li>");
MODULE_DESCRIPTION("Glue layer for NAND flash on Amstrad E3 (Delta)");
