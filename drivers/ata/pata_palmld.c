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
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/libata.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>

#include <scsi/scsi_host.h>
#include <mach/palmld.h>

#define DRV_NAME "pata_palmld"

static struct gpio_desc *palmld_pata_power;

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
	struct ata_host *host;
	struct ata_port *ap;
	void __iomem *mem;
	struct device *dev = &pdev->dev;
	struct gpio_desc *reset;
	int ret;

	/* allocate host */
	host = ata_host_alloc(dev, 1);
	if (!host)
		return -ENOMEM;

	/* remap drive's physical memory address */
	mem = devm_ioremap(dev, PALMLD_IDE_PHYS, 0x1000);
	if (!mem)
		return -ENOMEM;

	/* request and activate power and reset GPIOs */
	palmld_pata_power = devm_gpiod_get(dev, "power", GPIOD_OUT_HIGH);
	if (IS_ERR(palmld_pata_power))
		return PTR_ERR(palmld_pata_power);
	reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(reset)) {
		gpiod_set_value(palmld_pata_power, 0);
		return PTR_ERR(reset);
	}

	/* Assert reset to reset the drive */
	gpiod_set_value(reset, 1);
	msleep(30);
	gpiod_set_value(reset, 0);
	msleep(30);

	/* setup the ata port */
	ap = host->ports[0];
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
	ret = ata_host_activate(host, 0, NULL, IRQF_TRIGGER_RISING,
					&palmld_sht);
	/* power down on failure */
	if (ret)
		gpiod_set_value(palmld_pata_power, 0);
	return ret;
}

static int palmld_pata_remove(struct platform_device *dev)
{
	ata_platform_remove_one(dev);

	/* power down the HDD */
	gpiod_set_value(palmld_pata_power, 0);

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
