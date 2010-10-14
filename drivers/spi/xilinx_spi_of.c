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
#include <linux/slab.h>

#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/of_spi.h>

#include <linux/spi/xilinx_spi.h>
#include "xilinx_spi.h"


static int __devinit xilinx_spi_of_probe(struct platform_device *ofdev,
	const struct of_device_id *match)
{
	struct spi_master *master;
	struct resource r_mem;
	struct resource r_irq;
	int rc = 0;
	const u32 *prop;
	int len, num_cs;

	rc = of_address_to_resource(ofdev->dev.of_node, 0, &r_mem);
	if (rc) {
		dev_warn(&ofdev->dev, "invalid address\n");
		return rc;
	}

	rc = of_irq_to_resource(ofdev->dev.of_node, 0, &r_irq);
	if (rc == NO_IRQ) {
		dev_warn(&ofdev->dev, "no IRQ found\n");
		return -ENODEV;
	}

	/* number of slave select bits is required */
	prop = of_get_property(ofdev->dev.of_node, "xlnx,num-ss-bits", &len);
	if (!prop || len < sizeof(*prop)) {
		dev_warn(&ofdev->dev, "no 'xlnx,num-ss-bits' property\n");
		return -EINVAL;
	}
	num_cs = __be32_to_cpup(prop);
	master = xilinx_spi_init(&ofdev->dev, &r_mem, r_irq.start, -1,
				 num_cs, 0, 8);
	if (!master)
		return -ENODEV;

	dev_set_drvdata(&ofdev->dev, master);

	return 0;
}

static int __devexit xilinx_spi_remove(struct platform_device *ofdev)
{
	xilinx_spi_deinit(dev_get_drvdata(&ofdev->dev));
	dev_set_drvdata(&ofdev->dev, 0);
	return 0;
}

static int __exit xilinx_spi_of_remove(struct platform_device *op)
{
	return xilinx_spi_remove(op);
}

static const struct of_device_id xilinx_spi_of_match[] = {
	{ .compatible = "xlnx,xps-spi-2.00.a", },
	{ .compatible = "xlnx,xps-spi-2.00.b", },
	{}
};

MODULE_DEVICE_TABLE(of, xilinx_spi_of_match);

static struct of_platform_driver xilinx_spi_of_driver = {
	.probe = xilinx_spi_of_probe,
	.remove = __exit_p(xilinx_spi_of_remove),
	.driver = {
		.name = "xilinx-xps-spi",
		.owner = THIS_MODULE,
		.of_match_table = xilinx_spi_of_match,
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
