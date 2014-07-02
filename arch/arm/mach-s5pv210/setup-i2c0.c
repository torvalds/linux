/* linux/arch/arm/mach-s5pv210/setup-i2c0.c
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * I2C0 GPIO configuration.
 *
 * Based on plat-s3c64xx/setup-i2c0.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/gpio.h>

struct platform_device; /* don't need the contents */

#include <linux/platform_data/i2c-s3c2410.h>
#include <plat/gpio-cfg.h>

void s3c_i2c0_cfg_gpio(struct platform_device *dev)
{
	/*
	 * FIXME: Used only by legacy code that is not used anymore,
	 * but still compiled in, until all dependencies are removed.
	 */
}
