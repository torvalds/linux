// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2010 Samsung Electronics Co., Ltd.
//                     http://www.samsung.com/
//
// Based on S3C24XX setup for i2c device

#include <linux/kernel.h>
#include <linux/gpio.h>

struct platform_device; /* don't need the contents */

#include <linux/platform_data/touchscreen-s3c2410.h>

#include "gpio-cfg.h"
#include "gpio-samsung.h"

/**
 * s3c24xx_ts_cfg_gpio - configure gpio for s3c2410 systems
 * @dev: Device to configure GPIO for (ignored)
 *
 * Configure the GPIO for the S3C2410 system, where we have external FETs
 * connected to the device (later systems such as the S3C2440 integrate
 * these into the device).
 */
void s3c24xx_ts_cfg_gpio(struct platform_device *dev)
{
	s3c_gpio_cfgpin_range(S3C2410_GPG(12), 4, S3C_GPIO_SFN(3));
}
