/*
 * linux/arch/arm/plat-omap/i2c.c
 *
 * Helper module for board specific I2C bus registration
 *
 * Copyright (C) 2007 Nokia Corporation.
 *
 * Contact: Jarkko Nikula <jarkko.nikula@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <mach/mux.h>

#define OMAP_I2C_SIZE		0x3f
#define OMAP1_I2C_BASE		0xfffb3800
#define OMAP2_I2C_BASE1		0x48070000
#define OMAP2_I2C_BASE2		0x48072000
#define OMAP2_I2C_BASE3		0x48060000

static const char name[] = "i2c_omap";

#define I2C_RESOURCE_BUILDER(base, irq)			\
	{						\
		.start	= (base),			\
		.end	= (base) + OMAP_I2C_SIZE,	\
		.flags	= IORESOURCE_MEM,		\
	},						\
	{						\
		.start	= (irq),			\
		.flags	= IORESOURCE_IRQ,		\
	},

static struct resource i2c_resources[][2] = {
	{ I2C_RESOURCE_BUILDER(0, 0) },
#if	defined(CONFIG_ARCH_OMAP24XX) || defined(CONFIG_ARCH_OMAP34XX)
	{ I2C_RESOURCE_BUILDER(OMAP2_I2C_BASE2, INT_24XX_I2C2_IRQ) },
#endif
#if	defined(CONFIG_ARCH_OMAP34XX)
	{ I2C_RESOURCE_BUILDER(OMAP2_I2C_BASE3, INT_34XX_I2C3_IRQ) },
#endif
};

#define I2C_DEV_BUILDER(bus_id, res, data)		\
	{						\
		.id	= (bus_id),			\
		.name	= name,				\
		.num_resources	= ARRAY_SIZE(res),	\
		.resource	= (res),		\
		.dev		= {			\
			.platform_data	= (data),	\
		},					\
	}

static u32 i2c_rate[ARRAY_SIZE(i2c_resources)];
static struct platform_device omap_i2c_devices[] = {
	I2C_DEV_BUILDER(1, i2c_resources[0], &i2c_rate[0]),
#if	defined(CONFIG_ARCH_OMAP24XX) || defined(CONFIG_ARCH_OMAP34XX)
	I2C_DEV_BUILDER(2, i2c_resources[1], &i2c_rate[1]),
#endif
#if	defined(CONFIG_ARCH_OMAP34XX)
	I2C_DEV_BUILDER(3, i2c_resources[2], &i2c_rate[2]),
#endif
};

static void __init omap_i2c_mux_pins(int bus_id)
{
	/* TODO: Muxing for OMAP3 */
	switch (bus_id) {
	case 1:
		if (cpu_class_is_omap1()) {
			omap_cfg_reg(I2C_SCL);
			omap_cfg_reg(I2C_SDA);
		} else if (cpu_is_omap24xx()) {
			omap_cfg_reg(M19_24XX_I2C1_SCL);
			omap_cfg_reg(L15_24XX_I2C1_SDA);
		}
		break;
	case 2:
		if (cpu_is_omap24xx()) {
			omap_cfg_reg(J15_24XX_I2C2_SCL);
			omap_cfg_reg(H19_24XX_I2C2_SDA);
		}
		break;
	}
}

int __init omap_register_i2c_bus(int bus_id, u32 clkrate,
			  struct i2c_board_info const *info,
			  unsigned len)
{
	int ports, err;
	struct platform_device *pdev;
	struct resource *res;
	resource_size_t base, irq;

	if (cpu_class_is_omap1())
		ports = 1;
	else if (cpu_is_omap24xx())
		ports = 2;
	else if (cpu_is_omap34xx())
		ports = 3;

	BUG_ON(bus_id < 1 || bus_id > ports);

	if (info) {
		err = i2c_register_board_info(bus_id, info, len);
		if (err)
			return err;
	}

	pdev = &omap_i2c_devices[bus_id - 1];
	*(u32 *)pdev->dev.platform_data = clkrate;

	if (bus_id == 1) {
		res = pdev->resource;
		if (cpu_class_is_omap1()) {
			base = OMAP1_I2C_BASE;
			irq = INT_I2C;
		} else {
			base = OMAP2_I2C_BASE1;
			irq = INT_24XX_I2C1_IRQ;
		}
		res[0].start = base;
		res[0].end = base + OMAP_I2C_SIZE;
		res[1].start = irq;
	}

	omap_i2c_mux_pins(bus_id);
	return platform_device_register(pdev);
}
