/* linux/arch/arm/plat-s5p/setup-i2c0.c
 *
 * Copyright (c) 2009 Samsung Electronics Co., Ltd.
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

struct platform_device; /* don't need the contents */

#include <plat/iic.h>

void s3c_i2c0_cfg_gpio(struct platform_device *dev)
{
	/* Will be populated later */
}
