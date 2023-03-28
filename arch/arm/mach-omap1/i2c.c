// SPDX-License-Identifier: GPL-2.0-only
/*
 * Helper module for board specific I2C bus registration
 *
 * Copyright (C) 2009 Nokia Corporation.
 */

#include <linux/i2c.h>
#include <linux/platform_data/i2c-omap.h>

#include "mux.h"
#include "soc.h"
#include "i2c.h"

#define OMAP_I2C_SIZE		0x3f
#define OMAP1_I2C_BASE		0xfffb3800

static const char name[] = "omap_i2c";

static struct resource i2c_resources[2] = {
};

static struct platform_device omap_i2c_devices[1] = {
};

static void __init omap1_i2c_mux_pins(int bus_id)
{
	omap_cfg_reg(I2C_SDA);
	omap_cfg_reg(I2C_SCL);
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

	pdata->flags |= OMAP_I2C_FLAG_BUS_SHIFT_2;

	pdev->dev.platform_data = pdata;

	return platform_device_register(pdev);
}

#define OMAP_I2C_MAX_CONTROLLERS 4
static struct omap_i2c_bus_platform_data i2c_pdata[OMAP_I2C_MAX_CONTROLLERS];

#define OMAP_I2C_CMDLINE_SETUP	(BIT(31))

/**
 * omap_i2c_bus_setup - Process command line options for the I2C bus speed
 * @str: String of options
 *
 * This function allow to override the default I2C bus speed for given I2C
 * bus with a command line option.
 *
 * Format: i2c_bus=bus_id,clkrate (in kHz)
 *
 * Returns 1 on success, 0 otherwise.
 */
static int __init omap_i2c_bus_setup(char *str)
{
	int ints[3];

	get_options(str, 3, ints);
	if (ints[0] < 2 || ints[1] < 1 ||
			ints[1] > OMAP_I2C_MAX_CONTROLLERS)
		return 0;
	i2c_pdata[ints[1] - 1].clkrate = ints[2];
	i2c_pdata[ints[1] - 1].clkrate |= OMAP_I2C_CMDLINE_SETUP;

	return 1;
}
__setup("i2c_bus=", omap_i2c_bus_setup);

/*
 * Register busses defined in command line but that are not registered with
 * omap_register_i2c_bus from board initialization code.
 */
int __init omap_register_i2c_bus_cmdline(void)
{
	int i, err = 0;

	for (i = 0; i < ARRAY_SIZE(i2c_pdata); i++)
		if (i2c_pdata[i].clkrate & OMAP_I2C_CMDLINE_SETUP) {
			i2c_pdata[i].clkrate &= ~OMAP_I2C_CMDLINE_SETUP;
			err = omap_i2c_add_bus(&i2c_pdata[i], i + 1);
			if (err)
				goto out;
		}

out:
	return err;
}

/**
 * omap_register_i2c_bus - register I2C bus with device descriptors
 * @bus_id: bus id counting from number 1
 * @clkrate: clock rate of the bus in kHz
 * @info: pointer into I2C device descriptor table or NULL
 * @len: number of descriptors in the table
 *
 * Returns 0 on success or an error code.
 */
int __init omap_register_i2c_bus(int bus_id, u32 clkrate,
			  struct i2c_board_info const *info,
			  unsigned len)
{
	int err;

	BUG_ON(bus_id < 1 || bus_id > OMAP_I2C_MAX_CONTROLLERS);

	if (info) {
		err = i2c_register_board_info(bus_id, info, len);
		if (err)
			return err;
	}

	if (!i2c_pdata[bus_id - 1].clkrate)
		i2c_pdata[bus_id - 1].clkrate = clkrate;

	i2c_pdata[bus_id - 1].clkrate &= ~OMAP_I2C_CMDLINE_SETUP;

	return omap_i2c_add_bus(&i2c_pdata[bus_id - 1], bus_id);
}

static  int __init omap_i2c_cmdline(void)
{
	return omap_register_i2c_bus_cmdline();
}
subsys_initcall(omap_i2c_cmdline);
