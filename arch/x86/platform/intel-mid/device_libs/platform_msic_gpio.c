/*
 * platform_msic_gpio.c: MSIC GPIO platform data initialization file
 *
 * (C) Copyright 2013 Intel Corporation
 * Author: Sathyanarayanan Kuppuswamy <sathyanarayanan.kuppuswamy@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/scatterlist.h>
#include <linux/sfi.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/mfd/intel_msic.h>
#include <asm/intel-mid.h>

#include "platform_msic.h"

static void __init *msic_gpio_platform_data(void *info)
{
	static struct intel_msic_gpio_pdata msic_gpio_pdata;

	int gpio = get_gpio_by_name("msic_gpio_base");

	if (gpio < 0)
		return NULL;

	msic_gpio_pdata.gpio_base = gpio;
	msic_pdata.gpio = &msic_gpio_pdata;

	return msic_generic_platform_data(info, INTEL_MSIC_BLOCK_GPIO);
}

static const struct devs_id msic_gpio_dev_id __initconst = {
	.name = "msic_gpio",
	.type = SFI_DEV_TYPE_IPC,
	.delay = 1,
	.msic = 1,
	.get_platform_data = &msic_gpio_platform_data,
};

sfi_device(msic_gpio_dev_id);
