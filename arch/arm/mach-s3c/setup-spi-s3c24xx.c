// SPDX-License-Identifier: GPL-2.0
//
// HS-SPI device setup for S3C2443/S3C2416
//
// Copyright (C) 2011 Samsung Electronics Ltd.
//		http://www.samsung.com/

#include <linux/gpio.h>
#include <linux/platform_device.h>

#include "gpio-cfg.h"

#include "hardware-s3c24xx.h"
#include "regs-gpio.h"

#ifdef CONFIG_S3C64XX_DEV_SPI0
int s3c64xx_spi0_cfg_gpio(void)
{
	/* enable hsspi bit in misccr */
	s3c2410_modify_misccr(S3C2416_MISCCR_HSSPI_EN2, 1);

	s3c_gpio_cfgall_range(S3C2410_GPE(11), 3,
			      S3C_GPIO_SFN(2), S3C_GPIO_PULL_UP);

	return 0;
}
#endif
