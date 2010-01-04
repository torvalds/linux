/*
 * Support for Xilinx SPI platform devices
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
 * Xilinx SPI devices as platform devices
 *
 * Inspired by xilinx_spi.c, 2002-2007 (c) MontaVista Software, Inc.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>

#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/spi/xilinx_spi.h>

#include "xilinx_spi.h"

static int __devinit xilinx_spi_probe(struct platform_device *dev)
{
	struct xspi_platform_data *pdata;
	struct resource *r;
	int irq;
	struct spi_master *master;
	u8 i;

	pdata = dev->dev.platform_data;
	if (!pdata)
		return -ENODEV;

	r = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!r)
		return -ENODEV;

	irq = platform_get_irq(dev, 0);
	if (irq < 0)
		return -ENXIO;

	master = xilinx_spi_init(&dev->dev, r, irq, dev->id);
	if (!master)
		return -ENODEV;

	for (i = 0; i < pdata->num_devices; i++)
		spi_new_device(master, pdata->devices + i);

	platform_set_drvdata(dev, master);
	return 0;
}

static int __devexit xilinx_spi_remove(struct platform_device *dev)
{
	xilinx_spi_deinit(platform_get_drvdata(dev));
	platform_set_drvdata(dev, 0);

	return 0;
}

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:" XILINX_SPI_NAME);

static struct platform_driver xilinx_spi_driver = {
	.probe	= xilinx_spi_probe,
	.remove	= __devexit_p(xilinx_spi_remove),
	.driver = {
		.name = XILINX_SPI_NAME,
		.owner = THIS_MODULE,
	},
};

static int __init xilinx_spi_pltfm_init(void)
{
	return platform_driver_register(&xilinx_spi_driver);
}
module_init(xilinx_spi_pltfm_init);

static void __exit xilinx_spi_pltfm_exit(void)
{
	platform_driver_unregister(&xilinx_spi_driver);
}
module_exit(xilinx_spi_pltfm_exit);

MODULE_AUTHOR("Mocean Laboratories <info@mocean-labs.com>");
MODULE_DESCRIPTION("Xilinx SPI platform driver");
MODULE_LICENSE("GPL v2");
