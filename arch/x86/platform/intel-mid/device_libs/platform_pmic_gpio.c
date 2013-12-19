/*
 * platform_pmic_gpio.c: PMIC GPIO platform data initilization file
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
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/sfi.h>
#include <linux/intel_pmic_gpio.h>
#include <asm/intel-mid.h>

#include "platform_ipc.h"

static void __init *pmic_gpio_platform_data(void *info)
{
	static struct intel_pmic_gpio_platform_data pmic_gpio_pdata;
	int gpio_base = get_gpio_by_name("pmic_gpio_base");

	if (gpio_base == -1)
		gpio_base = 64;
	pmic_gpio_pdata.gpio_base = gpio_base;
	pmic_gpio_pdata.irq_base = gpio_base + INTEL_MID_IRQ_OFFSET;
	pmic_gpio_pdata.gpiointr = 0xffffeff8;

	return &pmic_gpio_pdata;
}

static const struct devs_id pmic_gpio_spi_dev_id __initconst = {
	.name = "pmic_gpio",
	.type = SFI_DEV_TYPE_SPI,
	.delay = 1,
	.get_platform_data = &pmic_gpio_platform_data,
};

static const struct devs_id pmic_gpio_ipc_dev_id __initconst = {
	.name = "pmic_gpio",
	.type = SFI_DEV_TYPE_IPC,
	.delay = 1,
	.get_platform_data = &pmic_gpio_platform_data,
	.device_handler = &ipc_device_handler
};

sfi_device(pmic_gpio_spi_dev_id);
sfi_device(pmic_gpio_ipc_dev_id);
