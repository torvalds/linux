// SPDX-License-Identifier: GPL-2.0
//
//	originally from arch/arm/plat-s3c24xx/devs.c
//
// Copyright (c) 2004 Simtec Electronics
//	Ben Dooks <ben@simtec.co.uk>
//
// Base S3C24XX platform device definitions

#include <linux/kernel.h>
#include <linux/platform_device.h>

#include "devs.h"

/* uart devices */

static struct platform_device s3c24xx_uart_device0 = {
	.id		= 0,
};

static struct platform_device s3c24xx_uart_device1 = {
	.id		= 1,
};

static struct platform_device s3c24xx_uart_device2 = {
	.id		= 2,
};

static struct platform_device s3c24xx_uart_device3 = {
	.id		= 3,
};

struct platform_device *s3c24xx_uart_src[4] = {
	&s3c24xx_uart_device0,
	&s3c24xx_uart_device1,
	&s3c24xx_uart_device2,
	&s3c24xx_uart_device3,
};

struct platform_device *s3c24xx_uart_devs[4] = {
};
