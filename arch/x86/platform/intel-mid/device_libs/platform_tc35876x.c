// SPDX-License-Identifier: GPL-2.0-only
/*
 * platform_tc35876x.c: tc35876x platform data initialization file
 *
 * (C) Copyright 2013 Intel Corporation
 * Author: Sathyanarayanan Kuppuswamy <sathyanarayanan.kuppuswamy@intel.com>
 */

#include <linux/gpio/machine.h>
#include <asm/intel-mid.h>

static struct gpiod_lookup_table tc35876x_gpio_table = {
	.dev_id	= "i2c_disp_brig",
	.table	= {
		GPIO_LOOKUP("0000:00:0c.0", -1, "bridge-reset", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("0000:00:0c.0", -1, "bl-en", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("0000:00:0c.0", -1, "vadd", GPIO_ACTIVE_HIGH),
		{ },
	},
};

/*tc35876x DSI_LVDS bridge chip and panel platform data*/
static void *tc35876x_platform_data(void *data)
{
	struct gpiod_lookup_table *table = &tc35876x_gpio_table;
	struct gpiod_lookup *lookup = table->table;

	lookup[0].chip_hwnum = get_gpio_by_name("LCMB_RXEN");
	lookup[1].chip_hwnum = get_gpio_by_name("6S6P_BL_EN");
	lookup[2].chip_hwnum = get_gpio_by_name("EN_VREG_LCD_V3P3");
	gpiod_add_lookup_table(table);

	return NULL;
}

static const struct devs_id tc35876x_dev_id __initconst = {
	.name = "i2c_disp_brig",
	.type = SFI_DEV_TYPE_I2C,
	.get_platform_data = &tc35876x_platform_data,
};

sfi_device(tc35876x_dev_id);
