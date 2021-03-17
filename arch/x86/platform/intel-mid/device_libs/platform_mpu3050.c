// SPDX-License-Identifier: GPL-2.0-only
/*
 * platform_mpu3050.c: mpu3050 platform data initialization file
 *
 * (C) Copyright 2013 Intel Corporation
 * Author: Sathyanarayanan Kuppuswamy <sathyanarayanan.kuppuswamy@intel.com>
 */

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <asm/intel-mid.h>

static void *mpu3050_platform_data(void *info)
{
	struct i2c_board_info *i2c_info = info;
	int intr = get_gpio_by_name("mpu3050_int");

	if (intr < 0)
		return NULL;

	i2c_info->irq = intr + INTEL_MID_IRQ_OFFSET;
	return NULL;
}

static const struct devs_id mpu3050_dev_id __initconst = {
	.name = "mpu3050",
	.type = SFI_DEV_TYPE_I2C,
	.delay = 1,
	.get_platform_data = &mpu3050_platform_data,
};

sfi_device(mpu3050_dev_id);
