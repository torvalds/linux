/*
 * platform_emc1403.c: emc1403 platform data initialization file
 *
 * (C) Copyright 2013 Intel Corporation
 * Author: Sathyanarayanan Kuppuswamy <sathyanarayanan.kuppuswamy@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <asm/intel-mid.h>

static void __init *emc1403_platform_data(void *info)
{
	static short intr2nd_pdata;
	struct i2c_board_info *i2c_info = info;
	int intr = get_gpio_by_name("thermal_int");
	int intr2nd = get_gpio_by_name("thermal_alert");

	if (intr < 0)
		return NULL;
	if (intr2nd < 0)
		return NULL;

	i2c_info->irq = intr + INTEL_MID_IRQ_OFFSET;
	intr2nd_pdata = intr2nd + INTEL_MID_IRQ_OFFSET;

	return &intr2nd_pdata;
}

static const struct devs_id emc1403_dev_id __initconst = {
	.name = "emc1403",
	.type = SFI_DEV_TYPE_I2C,
	.delay = 1,
	.get_platform_data = &emc1403_platform_data,
};

sfi_device(emc1403_dev_id);
