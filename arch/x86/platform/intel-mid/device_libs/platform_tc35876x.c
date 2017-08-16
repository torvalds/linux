/*
 * platform_tc35876x.c: tc35876x platform data initialization file
 *
 * (C) Copyright 2013 Intel Corporation
 * Author: Sathyanarayanan Kuppuswamy <sathyanarayanan.kuppuswamy@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/gpio.h>
#include <linux/i2c/tc35876x.h>
#include <asm/intel-mid.h>

/*tc35876x DSI_LVDS bridge chip and panel platform data*/
static void *tc35876x_platform_data(void *data)
{
	static struct tc35876x_platform_data pdata;

	/* gpio pins set to -1 will not be used by the driver */
	pdata.gpio_bridge_reset = get_gpio_by_name("LCMB_RXEN");
	pdata.gpio_panel_bl_en = get_gpio_by_name("6S6P_BL_EN");
	pdata.gpio_panel_vadd = get_gpio_by_name("EN_VREG_LCD_V3P3");

	return &pdata;
}

static const struct devs_id tc35876x_dev_id __initconst = {
	.name = "i2c_disp_brig",
	.type = SFI_DEV_TYPE_I2C,
	.get_platform_data = &tc35876x_platform_data,
};

sfi_device(tc35876x_dev_id);
