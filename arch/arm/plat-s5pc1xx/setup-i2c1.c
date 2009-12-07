/* linux/arch/arm/plat-s3c64xx/setup-i2c1.c
 *
 * Copyright 2009 Samsung Electronics Co.
 *	Byungho Min <bhmin@samsung.com>
 *
 * Base S5PC1XX I2C bus 1 gpio configuration
 *
 * Based on plat-s3c64xx/setup-i2c1.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>

struct platform_device; /* don't need the contents */

#include <plat/iic.h>

void s3c_i2c1_cfg_gpio(struct platform_device *dev)
{
	/* Pin configuration would be needed */
}
