/*
 * Xilinx SPI OF device driver
 *
 * Copyright (c) 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* Supports:
 * Xilinx SPI devices as OF devices
 *
 * Inspired by xilinx_spi.c, 2002-2007 (c) MontaVista Software, Inc.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/of_spi.h>

#include <linux/spi/xilinx_spi.h>
#include "xilinx_spi.h"


static int __devinit xilinx_spi_of_probe(struct of_device *ofdev,
	const struct of_device_id *match)
{
	struct spi_master *master;
	struct xspi_platform_data *pdata;
	struct resource r_mem;
	struct resource r_irq;
	int rc = 0;
	const u32 *prop;
	int len;

	rc = of_address_to_resource(ofdev->node, 0, &r_mem);
	if (rc) {
		dev_warn(&ofdev->dev, "invalid address\n");
		return rc;
	}

	rc = of_irq_to_resource(ofdev->node, 0, &r_irq);
	if (rc == NO_IRQ) {
		dev_warn(&ofdev->dev, "no IRQ found\n");
		return -ENODEV;
	}

	ofdev->dev.platform_data =
		kzalloc(sizeof(struct xspi_platform_data), GFP_KERNEL);
	pdata = ofdev->dev.platform_data;
	if (!pdata)
		return -ENOMEM;

	/* number of slave select bits is required */
	prop = of_get_property(ofdev->node, "xlnx,num-ss-bits", &len);
	if (!prop || len < sizeof(*prop)) {
		dev_warn(&ofdev->dev, "no 'xlnx,num-ss-bits' property\n");
		return -EINVAL;
	}
	pdata->num_chipselect = *prop;
	pdata->bits_per_word = 8;
	master = xilinx_spi_init(&ofdev->dev, &r_mem, r_irq.start, -1);
	if (!master)
		return -ENODEV;

	dev_set_drvdata(&ofdev->dev, master);

	/* Add any subnodes on the SPI bus */
	of_register_spi_devices(master, ofdev->node);

	return 0;
}

static int __devexit xilinx_spi_remove(struct of_device *ofdev)
{
	xilinx_spi_deinit(dev_get_drvdata(&ofdev->dev));
	dev_set_drvdata(&ofdev->dev, 0);
	kfree(ofdev->dev.platform_data);
	ofdev->dev.platform_data = NULL;
	return 0;
}

static int __exit xilinx_spi_of_remove(struct of_device *op)
{
	return xilinx_spi_remove(op);
}

static struct of_device_id xilinx_spi_of_match[] = {
	{ .compatible = "xlnx,xps-spi-2.00.a", },
	{ .compatible = "xlnx,xps-spi-2.00.b", },
	{}
};

MODULE_DEVICE_TABLE(of, xilinx_spi_of_match);

static struct of_platform_driver xilinx_spi_of_driver = {
	.match_table = xilinx_spi_of_match,
	.probe = xilinx_spi_of_probe,
	.remove = __exit_p(xilinx_spi_of_remove),
	.driver = {
		.name = "xilinx-xps-spi",
		.owner = THIS_MODULE,
	},
};

static int __init xilinx_spi_of_init(void)
{
	return of_register_platform_driver(&xilinx_spi_of_driver);
}
module_init(xilinx_spi_of_init);

static void __exit xilinx_spi_of_exit(void)
{
	of_unregister_platform_driver(&xilinx_spi_of_driver);
}
module_exit(xilinx_spi_of_exit);

MODULE_AUTHOR("Mocean Laboratories <info@mocean-labs.com>");
MODULE_DESCRIPTION("Xilinx SPI platform driver");
MODULE_LICENSE("GPL v2");
