/*
 * Helper module for board specific I2C bus registration
 *
 * Copyright (C) 2009 Nokia Corporation.
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

#include <linux/i2c-omap.h>
#include <mach/mux.h>
#include "soc.h"

#include <plat/i2c.h>

#define OMAP_I2C_SIZE		0x3f
#define OMAP1_I2C_BASE		0xfffb3800

static const char name[] = "omap_i2c";

static struct resource i2c_resources[2] = {
};

static struct platform_device omap_i2c_devices[1] = {
};

static void __init omap1_i2c_mux_pins(int bus_id)
{
	if (cpu_is_omap7xx()) {
		omap_cfg_reg(I2C_7XX_SDA);
		omap_cfg_reg(I2C_7XX_SCL);
	} else {
		omap_cfg_reg(I2C_SDA);
		omap_cfg_reg(I2C_SCL);
	}
}

int __init omap_i2c_add_bus(struct omap_i2c_bus_platform_data *pdata,
				int bus_id)
{
	struct platform_device *pdev;
	struct resource *res;

	if (bus_id > 1)
		return -EINVAL;

	omap1_i2c_mux_pins(bus_id);

	pdev = &omap_i2c_devices[bus_id - 1];
	pdev->id = bus_id;
	pdev->name = name;
	pdev->num_resources = ARRAY_SIZE(i2c_resources);
	res = i2c_resources;
	res[0].start = OMAP1_I2C_BASE;
	res[0].end = res[0].start + OMAP_I2C_SIZE;
	res[0].flags = IORESOURCE_MEM;
	res[1].start = INT_I2C;
	res[1].flags = IORESOURCE_IRQ;
	pdev->resource = res;

	/* all OMAP1 have IP version 1 register set */
	pdata->rev = OMAP_I2C_IP_VERSION_1;

	/* all OMAP1 I2C are implemented like this */
	pdata->flags = OMAP_I2C_FLAG_NO_FIFO |
		       OMAP_I2C_FLAG_SIMPLE_CLOCK |
		       OMAP_I2C_FLAG_16BIT_DATA_REG |
		       OMAP_I2C_FLAG_ALWAYS_ARMXOR_CLK;

	/* how the cpu bus is wired up differs for 7xx only */

	if (cpu_is_omap7xx())
		pdata->flags |= OMAP_I2C_FLAG_BUS_SHIFT_1;
	else
		pdata->flags |= OMAP_I2C_FLAG_BUS_SHIFT_2;

	pdev->dev.platform_data = pdata;

	return platform_device_register(pdev);
}

static  int __init omap_i2c_cmdline(void)
{
	return omap_register_i2c_bus_cmdline();
}
subsys_initcall(omap_i2c_cmdline);
