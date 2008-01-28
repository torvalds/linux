/* linux/arch/arm/mach-msm/common.c
 *
 * Common setup code for MSM7K Boards
 *
 * Copyright (C) 2007 Google, Inc.
 * Author: Brian Swetland <swetland@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <asm/mach/flash.h>
#include <asm/io.h>

#include <asm/setup.h>

#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

#include <asm/arch/msm_iomap.h>

#include <asm/arch/board.h>

struct flash_platform_data msm_nand_data = {
	.parts		= 0,
	.nr_parts	= 0,
};

static struct resource msm_nand_resources[] = {
	[0] = {
		.start	= 7,
		.end	= 7,
		.flags	= IORESOURCE_DMA,
	},
};

static struct platform_device msm_nand_device = {
	.name		= "msm_nand",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(msm_nand_resources),
	.resource	= msm_nand_resources,
	.dev		= {
		.platform_data	= &msm_nand_data,
	},
};

static struct platform_device msm_smd_device = {
	.name	= "msm_smd",
	.id	= -1,
};

static struct resource msm_i2c_resources[] = {
	{
		.start	= MSM_I2C_BASE,
		.end	= MSM_I2C_BASE + MSM_I2C_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_PWB_I2C,
		.end	= INT_PWB_I2C,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device msm_i2c_device = {
	.name		= "msm_i2c",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(msm_i2c_resources),
	.resource	= msm_i2c_resources,
};

static struct resource usb_resources[] = {
	{
		.start	= MSM_HSUSB_PHYS,
		.end	= MSM_HSUSB_PHYS + MSM_HSUSB_SIZE,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_USB_HS,
		.end	= INT_USB_HS,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device msm_hsusb_device = {
	.name		= "msm_hsusb",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(usb_resources),
	.resource	= usb_resources,
	.dev		= {
		.coherent_dma_mask	= 0xffffffff,
	},
};

static struct platform_device *devices[] __initdata = {
	&msm_nand_device,
	&msm_smd_device,
	&msm_i2c_device,
	&msm_hsusb_device,
};

void __init msm_add_devices(void)
{
	platform_add_devices(devices, ARRAY_SIZE(devices));
}
