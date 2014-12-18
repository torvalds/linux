/*
 *  A low-level PATA driver to handle a Compact Flash connected on the
 *  Mikrotik's RouterBoard 532 board.
 *
 *  Copyright (C) 2007 Gabor Juhos <juhosg at openwrt.org>
 *  Copyright (C) 2008 Florian Fainelli <florian@openwrt.org>
 *
 *  This file was based on: drivers/ata/pata_ixp4xx_cf.c
 *	Copyright (C) 2006-07 Tower Technologies
 *	Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 *  Also was based on the driver for Linux 2.4.xx published by Mikrotik for
 *  their RouterBoard 1xx and 5xx series devices. The original Mikrotik code
 *  seems not to have a license.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <linux/libata.h>
#include <scsi/scsi_host.h>

#include <asm/gpio.h>

#define DRV_NAME	"pata-rb532-cf"
#define DRV_VERSION	"0.1.0"
#define DRV_DESC	"PATA driver for RouterBOARD 532 Compact Flash"

#define RB500_CF_MAXPORTS	1
#define RB500_CF_IO_DELAY	400

#define RB500_CF_REG_BASE	0x0800
#define RB500_CF_REG_ERR	0x080D
#define RB500_CF_REG_CTRL	0x080E
/* 32bit buffered data register offset */
#define RB500_CF_REG_DBUF32	0x0C00

struct rb532_cf_info {
	void __iomem	*iobase;
	unsigned int	gpio_line;
	unsigned int	irq;
};

/* ------------------------------------------------------------------------ */

static irqreturn_t rb532_pata_irq_handler(int irq, void *dev_instance)
{
	struct ata_host *ah = dev_instance;
	struct rb532_cf_info *info = ah->private_data;

	if (gpio_get_value(info->gpio_line)) {
		irq_set_irq_type(info->irq, IRQ_TYPE_LEVEL_LOW);
		ata_sff_interrupt(info->irq, dev_instance);
	} else {
		irq_set_irq_type(info->irq, IRQ_TYPE_LEVEL_HIGH);
	}

	return IRQ_HANDLED;
}

static struct ata_port_operations rb532_pata_port_ops = {
	.inherits		= &ata_sff_port_ops,
	.sff_data_xfer		= ata_sff_data_xfer32,
};

/* ------------------------------------------------------------------------ */

static struct scsi_host_template rb532_pata_sht = {
	ATA_PIO_SHT(DRV_NAME),
};

/* ------------------------------------------------------------------------ */

static void rb532_pata_setup_ports(struct ata_host *ah)
{
	struct rb532_cf_info *info = ah->private_data;
	struct ata_port *ap;

	ap = ah->ports[0];

	ap->ops		= &rb532_pata_port_ops;
	ap->pio_mask	= ATA_PIO4;

	ap->ioaddr.cmd_addr	= info->iobase + RB500_CF_REG_BASE;
	ap->ioaddr.ctl_addr	= info->iobase + RB500_CF_REG_CTRL;
	ap->ioaddr.altstatus_addr = info->iobase + RB500_CF_REG_CTRL;

	ata_sff_std_ports(&ap->ioaddr);

	ap->ioaddr.data_addr	= info->iobase + RB500_CF_REG_DBUF32;
	ap->ioaddr.error_addr	= info->iobase + RB500_CF_REG_ERR;
}

static int rb532_pata_driver_probe(struct platform_device *pdev)
{
	int irq;
	int gpio;
	struct resource *res;
	struct ata_host *ah;
	struct rb532_cf_info *info;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no IOMEM resource found\n");
		return -EINVAL;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(&pdev->dev, "no IRQ resource found\n");
		return -ENOENT;
	}

	gpio = irq_to_gpio(irq);
	if (gpio < 0) {
		dev_err(&pdev->dev, "no GPIO found for irq%d\n", irq);
		return -ENOENT;
	}

	ret = gpio_request(gpio, DRV_NAME);
	if (ret) {
		dev_err(&pdev->dev, "GPIO request failed\n");
		return ret;
	}

	/* allocate host */
	ah = ata_host_alloc(&pdev->dev, RB500_CF_MAXPORTS);
	if (!ah)
		return -ENOMEM;

	platform_set_drvdata(pdev, ah);

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	ah->private_data = info;
	info->gpio_line = gpio;
	info->irq = irq;

	info->iobase = devm_ioremap_nocache(&pdev->dev, res->start,
				resource_size(res));
	if (!info->iobase)
		return -ENOMEM;

	ret = gpio_direction_input(gpio);
	if (ret) {
		dev_err(&pdev->dev, "unable to set GPIO direction, err=%d\n",
				ret);
		goto err_free_gpio;
	}

	rb532_pata_setup_ports(ah);

	ret = ata_host_activate(ah, irq, rb532_pata_irq_handler,
				IRQF_TRIGGER_LOW, &rb532_pata_sht);
	if (ret)
		goto err_free_gpio;

	return 0;

err_free_gpio:
	gpio_free(gpio);

	return ret;
}

static int rb532_pata_driver_remove(struct platform_device *pdev)
{
	struct ata_host *ah = platform_get_drvdata(pdev);
	struct rb532_cf_info *info = ah->private_data;

	ata_host_detach(ah);
	gpio_free(info->gpio_line);

	return 0;
}

static struct platform_driver rb532_pata_platform_driver = {
	.probe		= rb532_pata_driver_probe,
	.remove		= rb532_pata_driver_remove,
	.driver	 = {
		.name   = DRV_NAME,
	},
};

#define DRV_INFO DRV_DESC " version " DRV_VERSION

module_platform_driver(rb532_pata_platform_driver);

MODULE_AUTHOR("Gabor Juhos <juhosg at openwrt.org>");
MODULE_AUTHOR("Florian Fainelli <florian@openwrt.org>");
MODULE_DESCRIPTION(DRV_DESC);
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
