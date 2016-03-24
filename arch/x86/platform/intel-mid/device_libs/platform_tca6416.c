/*
 * platform_tca6416.c: tca6416 platform data initialization file
 *
 * (C) Copyright 2013 Intel Corporation
 * Author: Sathyanarayanan Kuppuswamy <sathyanarayanan.kuppuswamy@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/platform_data/pca953x.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <asm/intel-mid.h>

#define TCA6416_NAME	"tca6416"
#define TCA6416_BASE	"tca6416_base"
#define TCA6416_INTR	"tca6416_int"

static void *tca6416_platform_data(void *info)
{
	static struct pca953x_platform_data tca6416;
	struct i2c_board_info *i2c_info = info;
	int gpio_base, intr;
	char base_pin_name[SFI_NAME_LEN + 1];
	char intr_pin_name[SFI_NAME_LEN + 1];

	strcpy(i2c_info->type, TCA6416_NAME);
	strcpy(base_pin_name, TCA6416_BASE);
	strcpy(intr_pin_name, TCA6416_INTR);

	gpio_base = get_gpio_by_name(base_pin_name);
	intr = get_gpio_by_name(intr_pin_name);

	if (gpio_base < 0)
		return NULL;
	tca6416.gpio_base = gpio_base;
	if (intr >= 0) {
		i2c_info->irq = intr + INTEL_MID_IRQ_OFFSET;
		tca6416.irq_base = gpio_base + INTEL_MID_IRQ_OFFSET;
	} else {
		i2c_info->irq = -1;
		tca6416.irq_base = -1;
	}
	return &tca6416;
}

static const struct devs_id tca6416_dev_id __initconst = {
	.name = "tca6416",
	.type = SFI_DEV_TYPE_I2C,
	.delay = 1,
	.get_platform_data = &tca6416_platform_data,
};

sfi_device(tca6416_dev_id);
