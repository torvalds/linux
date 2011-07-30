/*
 * Copyright 2009 Amit Kucheria <amit.kucheria@canonical.com>
 * Copyright (C) 2010 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <mach/hardware.h>
#include <mach/imx-uart.h>
#include <mach/irqs.h>

static struct resource mxc_hsi2c_resources[] = {
	{
		.start = MX51_HSI2C_DMA_BASE_ADDR,
		.end = MX51_HSI2C_DMA_BASE_ADDR + SZ_16K - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = MX51_INT_HS_I2C,
		.end = MX51_INT_HS_I2C,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_hsi2c_device = {
	.name = "imx-i2c",
	.id = 2,
	.num_resources = ARRAY_SIZE(mxc_hsi2c_resources),
	.resource = mxc_hsi2c_resources
};
