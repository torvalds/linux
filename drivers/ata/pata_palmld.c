// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/ata/pata_palmld.c
 *
 * Driver for IDE channel in Palm LifeDrive
 *
 * Based on research of:
 *		Alex Osborne <ato@meshy.org>
 *
 * Rewrite for mainline:
 *		Marek Vasut <marek.vasut@gmail.com>
 *
 * Rewritten version based on pata_ixp4xx_cf.c:
 * ixp4xx PATA/Compact Flash driver
 * Copyright (C) 2006-07 Tower Technologies
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/libata.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>

#include <scsi/scsi_host.h>

#define DRV_NAME "pata_palmld"

struct palmld_pata {
	struct ata_host *host;
	struct gpio_desc *power;
	struct gpio_desc *reset;
};

static struct scsi_host_template palmld_sht = {
	ATA_PIO_SHT(DRV_NAME),
};

static struct ata_port_operations palmld_port_ops = {
	.inherits		= &ata_sff_port_ops,
	.sff_data_xfer		= ata_sff_data_xfer32,
	.cable_detect		= ata_cable_40wire,
};

static int palmld_pata_probe(struct platform_device *pdev)
{
	struct palmld_pata *lda;
	struct ata_port *ap;
	void __iomem *mem;
	struct device *dev = &pdev->dev;
	int ret;

	lda = devm_kzalloc(dev, sizeof(*lda), GFP_KERNEL);
	if (!lda)
		return -ENOMEM;

	/* allocate host */
	lda->host = ata_host_alloc(dev, 1);
	if (!lda->host)
		return -ENOMEM;

	/* remap drive's physical memory address */
	mem = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mem))
		return PTR_ERR(mem);

	/* request and activate power and reset GPIOs */
	lda->power = devm_gpiod_get(dev, "power", GPIOD_OUT_HIGH);
	if (IS_ERR(lda->power))
		return PTR_ERR(lda->power);
	lda->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(lda->reset)) {
		gpiod_set_value(lda->power, 0);
		return PTR_ERR(lda->reset);
	}

	/* Assert reset to reset the drive */
	gpiod_set_value(lda->reset, 1);
	msleep(30);
	gpiod_set_value(lda->reset, 0);
	msleep(30);

	/* setup the ata port */
	ap = lda->host->ports[0];
	ap->ops	= &palmld_port_ops;
	ap->pio_mask = ATA_PIO4;
	ap->flags |= ATA_FLAG_PIO_POLLING;

	/* memory mapping voodoo */
	ap->ioaddr.cmd_addr = mem + 0x10;
	ap->ioaddr.altstatus_addr = mem + 0xe;
	ap->ioaddr.ctl_addr = mem + 0xe;

	/* start the port */
	ata_sff_std_ports(&ap->ioaddr);

	/* activate host */
	ret = ata_host_activate(lda->host, 0, NULL, IRQF_TRIGGER_RISING,
				&palmld_sht);
	/* power down on failure */
	if (ret) {
		gpiod_set_value(lda->power, 0);
		return ret;
	}

	platform_set_drvdata(pdev, lda);
	return 0;
}

static int palmld_pata_remove(struct platform_device *pdev)
{
	struct palmld_pata *lda = platform_get_drvdata(pdev);

	ata_platform_remove_one(pdev);

	/* power down the HDD */
	gpiod_set_value(lda->power, 0);

	return 0;
}

static struct platform_driver palmld_pata_platform_driver = {
	.driver	 = {
		.name   = DRV_NAME,
	},
	.probe		= palmld_pata_probe,
	.remove		= palmld_pata_remove,
};

module_platform_driver(palmld_pata_platform_driver);

MODULE_AUTHOR("Marek Vasut <marek.vasut@gmail.com>");
MODULE_DESCRIPTION("PalmLD PATA driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
