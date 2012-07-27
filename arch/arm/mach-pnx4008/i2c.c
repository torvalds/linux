/*
 * I2C initialization for PNX4008.
 *
 * Author: Vitaly Wool <vitalywool@gmail.com>
 *
 * 2005-2006 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/i2c-pnx.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <mach/platform.h>
#include <mach/irqs.h>

static struct resource i2c0_resources[] = {
	{
		.start = PNX4008_I2C1_BASE,
		.end = PNX4008_I2C1_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	}, {
		.start = I2C_1_INT,
		.end = I2C_1_INT,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource i2c1_resources[] = {
	{
		.start = PNX4008_I2C2_BASE,
		.end = PNX4008_I2C2_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	}, {
		.start = I2C_2_INT,
		.end = I2C_2_INT,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource i2c2_resources[] = {
	{
		.start = PNX4008_USB_CONFIG_BASE + 0x300,
		.end = PNX4008_USB_CONFIG_BASE + 0x300 + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	}, {
		.start = USB_I2C_INT,
		.end = USB_I2C_INT,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device i2c0_device = {
	.name = "pnx-i2c.0",
	.id = 0,
	.resource = i2c0_resources,
	.num_resources = ARRAY_SIZE(i2c0_resources),
};

static struct platform_device i2c1_device = {
	.name = "pnx-i2c.1",
	.id = 1,
	.resource = i2c1_resources,
	.num_resources = ARRAY_SIZE(i2c1_resources),
};

static struct platform_device i2c2_device = {
	.name = "pnx-i2c.2",
	.id = 2,
	.resource = i2c2_resources,
	.num_resources = ARRAY_SIZE(i2c2_resources),
};

static struct platform_device *devices[] __initdata = {
	&i2c0_device,
	&i2c1_device,
	&i2c2_device,
};

void __init pnx4008_register_i2c_devices(void)
{
	platform_add_devices(devices, ARRAY_SIZE(devices));
}
