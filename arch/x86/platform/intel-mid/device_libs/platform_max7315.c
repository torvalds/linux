// SPDX-License-Identifier: GPL-2.0-only
/*
 * platform_max7315.c: max7315 platform data initialization file
 *
 * (C) Copyright 2013 Intel Corporation
 * Author: Sathyanarayanan Kuppuswamy <sathyanarayanan.kuppuswamy@intel.com>
 */

#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/platform_data/pca953x.h>
#include <asm/intel-mid.h>

#define MAX7315_NUM 2

static void __init *max7315_platform_data(void *info)
{
	static struct pca953x_platform_data max7315_pdata[MAX7315_NUM];
	static int nr;
	struct pca953x_platform_data *max7315 = &max7315_pdata[nr];
	struct i2c_board_info *i2c_info = info;
	int gpio_base, intr;
	char base_pin_name[SFI_NAME_LEN + 1];
	char intr_pin_name[SFI_NAME_LEN + 1];

	if (nr == MAX7315_NUM) {
		pr_err("too many max7315s, we only support %d\n",
				MAX7315_NUM);
		return NULL;
	}
	/* we have several max7315 on the board, we only need load several
	 * instances of the same pca953x driver to cover them
	 */
	strcpy(i2c_info->type, "max7315");
	if (nr++) {
		snprintf(base_pin_name, sizeof(base_pin_name),
			 "max7315_%d_base", nr);
		snprintf(intr_pin_name, sizeof(intr_pin_name),
			 "max7315_%d_int", nr);
	} else {
		strcpy(base_pin_name, "max7315_base");
		strcpy(intr_pin_name, "max7315_int");
	}

	gpio_base = get_gpio_by_name(base_pin_name);
	intr = get_gpio_by_name(intr_pin_name);

	if (gpio_base < 0)
		return NULL;
	max7315->gpio_base = gpio_base;
	if (intr != -1) {
		i2c_info->irq = intr + INTEL_MID_IRQ_OFFSET;
		max7315->irq_base = gpio_base + INTEL_MID_IRQ_OFFSET;
	} else {
		i2c_info->irq = -1;
		max7315->irq_base = -1;
	}
	return max7315;
}

static const struct devs_id max7315_dev_id __initconst = {
	.name = "i2c_max7315",
	.type = SFI_DEV_TYPE_I2C,
	.delay = 1,
	.get_platform_data = &max7315_platform_data,
};

static const struct devs_id max7315_2_dev_id __initconst = {
	.name = "i2c_max7315_2",
	.type = SFI_DEV_TYPE_I2C,
	.delay = 1,
	.get_platform_data = &max7315_platform_data,
};

sfi_device(max7315_dev_id);
sfi_device(max7315_2_dev_id);
