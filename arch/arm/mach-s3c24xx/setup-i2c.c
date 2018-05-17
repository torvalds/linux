// SPDX-License-Identifier: GPL-2.0
//
// Copyright 2008 Simtec Electronics
//	Ben Dooks <ben@simtec.co.uk>
//
// S3C24XX Base setup for i2c device

#include <linux/kernel.h>
#include <linux/gpio.h>

struct platform_device;

#include <plat/gpio-cfg.h>
#include <linux/platform_data/i2c-s3c2410.h>
#include <mach/hardware.h>
#include <mach/regs-gpio.h>
#include <mach/gpio-samsung.h>

void s3c_i2c0_cfg_gpio(struct platform_device *dev)
{
	s3c_gpio_cfgpin(S3C2410_GPE(15), S3C2410_GPE15_IICSDA);
	s3c_gpio_cfgpin(S3C2410_GPE(14), S3C2410_GPE14_IICSCL);
}
