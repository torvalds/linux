/*
 * OF-platform PATA driver
 *
 * Copyright (c) 2007  MontaVista Software, Inc.
 *                     Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/ata_platform.h>
#include <linux/libata.h>

#define DRV_NAME "pata_of_platform"

static struct scsi_host_template pata_platform_sht = {
	ATA_PIO_SHT(DRV_NAME),
};

static int pata_of_platform_probe(struct platform_device *ofdev)
{
	int ret;
	struct device_node *dn = ofdev->dev.of_node;
	struct resource io_res;
	struct resource ctl_res;
	struct resource *irq_res;
	unsigned int reg_shift = 0;
	int pio_mode = 0;
	int pio_mask;
	bool use16bit;

	ret = of_address_to_resource(dn, 0, &io_res);
	if (ret) {
		dev_err(&ofdev->dev, "can't get IO address from "
			"device tree\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(dn, 1, &ctl_res);
	if (ret) {
		dev_err(&ofdev->dev, "can't get CTL address from "
			"device tree\n");
		return -EINVAL;
	}

	irq_res = platform_get_resource(ofdev, IORESOURCE_IRQ, 0);

	of_property_read_u32(dn, "reg-shift", &reg_shift);

	if (!of_property_read_u32(dn, "pio-mode", &pio_mode)) {
		if (pio_mode > 6) {
			dev_err(&ofdev->dev, "invalid pio-mode\n");
			return -EINVAL;
		}
	} else {
		dev_info(&ofdev->dev, "pio-mode unspecified, assuming PIO0\n");
	}

	use16bit = of_property_read_bool(dn, "ata-generic,use16bit");

	pio_mask = 1 << pio_mode;
	pio_mask |= (1 << pio_mode) - 1;

	return __pata_platform_probe(&ofdev->dev, &io_res, &ctl_res, irq_res,
				     reg_shift, pio_mask, &pata_platform_sht,
				     use16bit);
}

static const struct of_device_id pata_of_platform_match[] = {
	{ .compatible = "ata-generic", },
	{ },
};
MODULE_DEVICE_TABLE(of, pata_of_platform_match);

static struct platform_driver pata_of_platform_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = pata_of_platform_match,
	},
	.probe		= pata_of_platform_probe,
	.remove		= ata_platform_remove_one,
};

module_platform_driver(pata_of_platform_driver);

MODULE_DESCRIPTION("OF-platform PATA driver");
MODULE_AUTHOR("Anton Vorontsov <avorontsov@ru.mvista.com>");
MODULE_LICENSE("GPL");
