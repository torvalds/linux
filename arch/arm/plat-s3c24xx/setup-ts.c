/* linux/arch/arm/plat-s3c24xx/setup-ts.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *                     http://www.samsung.com/
 *
 * Based on S3C24XX setup for i2c device
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/gpio.h>

struct platform_device; /* don't need the contents */

#include <mach/hardware.h>
#include <mach/regs-gpio.h>

/**
 * s3c24xx_ts_cfg_gpio - configure gpio for s3c2410 systems
 *
 * Configure the GPIO for the S3C2410 system, where we have external FETs
 * connected to the device (later systems such as the S3C2440 integrate
 * these into the device).
 */
void s3c24xx_ts_cfg_gpio(struct platform_device *dev)
{
	s3c2410_gpio_cfgpin(S3C2410_GPG(12), S3C2410_GPG12_XMON);
	s3c2410_gpio_cfgpin(S3C2410_GPG(13), S3C2410_GPG13_nXPON);
	s3c2410_gpio_cfgpin(S3C2410_GPG(14), S3C2410_GPG14_YMON);
	s3c2410_gpio_cfgpin(S3C2410_GPG(15), S3C2410_GPG15_nYPON);
}
