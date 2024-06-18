// SPDX-License-Identifier: GPL-2.0
//
// Copyright 2009 Wolfson Microelectronics
//      Mark Brown <broonie@opensource.wolfsonmicro.com>

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <linux/export.h>

#include "irqs.h"
#include "map.h"

#include "devs.h"
#include <linux/platform_data/asoc-s3c.h>
#include "gpio-cfg.h"
#include "gpio-samsung.h"

static int s3c64xx_i2s_cfg_gpio(struct platform_device *pdev)
{
	unsigned int base;

	switch (pdev->id) {
	case 0:
		base = S3C64XX_GPD(0);
		break;
	case 1:
		base = S3C64XX_GPE(0);
		break;
	case 2:
		s3c_gpio_cfgpin(S3C64XX_GPC(4), S3C_GPIO_SFN(5));
		s3c_gpio_cfgpin(S3C64XX_GPC(5), S3C_GPIO_SFN(5));
		s3c_gpio_cfgpin(S3C64XX_GPC(7), S3C_GPIO_SFN(5));
		s3c_gpio_cfgpin_range(S3C64XX_GPH(6), 4, S3C_GPIO_SFN(5));
		return 0;
	default:
		printk(KERN_DEBUG "Invalid I2S Controller number: %d\n",
			pdev->id);
		return -EINVAL;
	}

	s3c_gpio_cfgpin_range(base, 5, S3C_GPIO_SFN(3));

	return 0;
}

static struct resource s3c64xx_iis0_resource[] = {
	[0] = DEFINE_RES_MEM(S3C64XX_PA_IIS0, SZ_256),
};

static struct s3c_audio_pdata i2s0_pdata = {
	.cfg_gpio = s3c64xx_i2s_cfg_gpio,
};

struct platform_device s3c64xx_device_iis0 = {
	.name		  = "samsung-i2s",
	.id		  = 0,
	.num_resources	  = ARRAY_SIZE(s3c64xx_iis0_resource),
	.resource	  = s3c64xx_iis0_resource,
	.dev = {
		.platform_data = &i2s0_pdata,
	},
};
EXPORT_SYMBOL(s3c64xx_device_iis0);

static struct resource s3c64xx_iis1_resource[] = {
	[0] = DEFINE_RES_MEM(S3C64XX_PA_IIS1, SZ_256),
};

static struct s3c_audio_pdata i2s1_pdata = {
	.cfg_gpio = s3c64xx_i2s_cfg_gpio,
};

struct platform_device s3c64xx_device_iis1 = {
	.name		  = "samsung-i2s",
	.id		  = 1,
	.num_resources	  = ARRAY_SIZE(s3c64xx_iis1_resource),
	.resource	  = s3c64xx_iis1_resource,
	.dev = {
		.platform_data = &i2s1_pdata,
	},
};
EXPORT_SYMBOL(s3c64xx_device_iis1);
