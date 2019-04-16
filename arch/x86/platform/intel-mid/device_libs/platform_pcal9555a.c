/*
 * PCAL9555a platform data initialization file
 *
 * Copyright (C) 2016, Intel Corporation
 *
 * Authors: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *	    Dan O'Donovan <dan@emutex.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/platform_data/pca953x.h>
#include <linux/sfi.h>

#include <asm/intel-mid.h>

#define PCAL9555A_NUM	4

static struct pca953x_platform_data pcal9555a_pdata[PCAL9555A_NUM];
static int nr;

static void __init *pcal9555a_platform_data(void *info)
{
	struct i2c_board_info *i2c_info = info;
	char *type = i2c_info->type;
	struct pca953x_platform_data *pcal9555a;
	char base_pin_name[SFI_NAME_LEN + 1];
	char intr_pin_name[SFI_NAME_LEN + 1];
	int gpio_base, intr;

	snprintf(base_pin_name, sizeof(base_pin_name), "%s_base", type);
	snprintf(intr_pin_name, sizeof(intr_pin_name), "%s_int", type);

	gpio_base = get_gpio_by_name(base_pin_name);
	intr = get_gpio_by_name(intr_pin_name);

	/* Check if the SFI record valid */
	if (gpio_base == -1)
		return NULL;

	if (nr >= PCAL9555A_NUM) {
		pr_err("%s: Too many instances, only %d supported\n", __func__,
		       PCAL9555A_NUM);
		return NULL;
	}

	pcal9555a = &pcal9555a_pdata[nr++];
	pcal9555a->gpio_base = gpio_base;

	if (intr >= 0) {
		i2c_info->irq = intr + INTEL_MID_IRQ_OFFSET;
		pcal9555a->irq_base = gpio_base + INTEL_MID_IRQ_OFFSET;
	} else {
		i2c_info->irq = -1;
		pcal9555a->irq_base = -1;
	}

	strcpy(type, "pcal9555a");
	return pcal9555a;
}

static const struct devs_id pcal9555a_1_dev_id __initconst = {
	.name			= "pcal9555a-1",
	.type			= SFI_DEV_TYPE_I2C,
	.delay			= 1,
	.get_platform_data	= &pcal9555a_platform_data,
};

static const struct devs_id pcal9555a_2_dev_id __initconst = {
	.name			= "pcal9555a-2",
	.type			= SFI_DEV_TYPE_I2C,
	.delay			= 1,
	.get_platform_data	= &pcal9555a_platform_data,
};

static const struct devs_id pcal9555a_3_dev_id __initconst = {
	.name			= "pcal9555a-3",
	.type			= SFI_DEV_TYPE_I2C,
	.delay			= 1,
	.get_platform_data	= &pcal9555a_platform_data,
};

static const struct devs_id pcal9555a_4_dev_id __initconst = {
	.name			= "pcal9555a-4",
	.type			= SFI_DEV_TYPE_I2C,
	.delay			= 1,
	.get_platform_data	= &pcal9555a_platform_data,
};

sfi_device(pcal9555a_1_dev_id);
sfi_device(pcal9555a_2_dev_id);
sfi_device(pcal9555a_3_dev_id);
sfi_device(pcal9555a_4_dev_id);
