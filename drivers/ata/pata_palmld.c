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
#include <linux/gpio.h>

#include <scsi/scsi_host.h>
#include <mach/palmld.h>

#define DRV_NAME "pata_palmld"

static struct gpio palmld_hdd_gpios[] = {
	{ GPIO_NR_PALMLD_IDE_PWEN,	GPIOF_INIT_HIGH,	"HDD Power" },
	{ GPIO_NR_PALMLD_IDE_RESET,	GPIOF_INIT_LOW,		"HDD Reset" },
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
	struct ata_host *host;
	struct ata_port *ap;
	void __iomem *mem;
	int ret;

	/* allocate host */
	host = ata_host_alloc(&pdev->dev, 1);
	if (!host) {
		ret = -ENOMEM;
		goto err1;
	}

	/* remap drive's physical memory address */
	mem = devm_ioremap(&pdev->dev, PALMLD_IDE_PHYS, 0x1000);
	if (!mem) {
		ret = -ENOMEM;
		goto err1;
	}

	/* request and activate power GPIO, IRQ GPIO */
	ret = gpio_request_array(palmld_hdd_gpios,
				ARRAY_SIZE(palmld_hdd_gpios));
	if (ret)
		goto err1;

	/* reset the drive */
	gpio_set_value(GPIO_NR_PALMLD_IDE_RESET, 0);
	msleep(30);
	gpio_set_value(GPIO_NR_PALMLD_IDE_RESET, 1);
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
	if (ret)
		goto err2;

	return ret;

err2:
	gpio_free_array(palmld_hdd_gpios, ARRAY_SIZE(palmld_hdd_gpios));
err1:
	return ret;
}

static int palmld_pata_remove(struct platform_device *dev)
{
	ata_platform_remove_one(dev);

	/* power down the HDD */
	gpio_set_value(GPIO_NR_PALMLD_IDE_PWEN, 0);

	gpio_free_array(palmld_hdd_gpios, ARRAY_SIZE(palmld_hdd_gpios));

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
