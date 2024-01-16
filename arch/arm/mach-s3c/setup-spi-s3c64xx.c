// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2011 Samsung Electronics Ltd.
//		http://www.samsung.com/

#include <linux/gpio.h>
#include <linux/platform_data/spi-s3c64xx.h>
#include "gpio-cfg.h"
#include "gpio-samsung.h"

#ifdef CONFIG_S3C64XX_DEV_SPI0
int s3c64xx_spi0_cfg_gpio(void)
{
	s3c_gpio_cfgall_range(S3C64XX_GPC(0), 3,
				S3C_GPIO_SFN(2), S3C_GPIO_PULL_UP);
	return 0;
}
#endif
