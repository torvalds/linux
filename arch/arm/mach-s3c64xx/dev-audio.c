/* linux/arch/arm/plat-s3c/dev-audio.c
 *
 * Copyright 2009 Wolfson Microelectronics
 *      Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <linux/export.h>

#include <mach/irqs.h>
#include <mach/map.h>
#include <mach/dma.h>

#include <plat/devs.h>
#include <linux/platform_data/asoc-s3c.h>
#include <plat/gpio-cfg.h>

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
	[1] = DEFINE_RES_DMA(DMACH_I2S0_OUT),
	[2] = DEFINE_RES_DMA(DMACH_I2S0_IN),
};

static struct s3c_audio_pdata i2sv3_pdata = {
	.cfg_gpio = s3c64xx_i2s_cfg_gpio,
};

struct platform_device s3c64xx_device_iis0 = {
	.name		  = "samsung-i2s",
	.id		  = 0,
	.num_resources	  = ARRAY_SIZE(s3c64xx_iis0_resource),
	.resource	  = s3c64xx_iis0_resource,
	.dev = {
		.platform_data = &i2sv3_pdata,
	},
};
EXPORT_SYMBOL(s3c64xx_device_iis0);

static struct resource s3c64xx_iis1_resource[] = {
	[0] = DEFINE_RES_MEM(S3C64XX_PA_IIS1, SZ_256),
	[1] = DEFINE_RES_DMA(DMACH_I2S1_OUT),
	[2] = DEFINE_RES_DMA(DMACH_I2S1_IN),
};

struct platform_device s3c64xx_device_iis1 = {
	.name		  = "samsung-i2s",
	.id		  = 1,
	.num_resources	  = ARRAY_SIZE(s3c64xx_iis1_resource),
	.resource	  = s3c64xx_iis1_resource,
	.dev = {
		.platform_data = &i2sv3_pdata,
	},
};
EXPORT_SYMBOL(s3c64xx_device_iis1);

static struct resource s3c64xx_iisv4_resource[] = {
	[0] = DEFINE_RES_MEM(S3C64XX_PA_IISV4, SZ_256),
	[1] = DEFINE_RES_DMA(DMACH_HSI_I2SV40_TX),
	[2] = DEFINE_RES_DMA(DMACH_HSI_I2SV40_RX),
};

static struct s3c_audio_pdata i2sv4_pdata = {
	.cfg_gpio = s3c64xx_i2s_cfg_gpio,
	.type = {
		.i2s = {
			.quirks = QUIRK_PRI_6CHAN,
		},
	},
};

struct platform_device s3c64xx_device_iisv4 = {
	.name = "samsung-i2s",
	.id = 2,
	.num_resources	  = ARRAY_SIZE(s3c64xx_iisv4_resource),
	.resource	  = s3c64xx_iisv4_resource,
	.dev = {
		.platform_data = &i2sv4_pdata,
	},
};
EXPORT_SYMBOL(s3c64xx_device_iisv4);


/* PCM Controller platform_devices */

static int s3c64xx_pcm_cfg_gpio(struct platform_device *pdev)
{
	unsigned int base;

	switch (pdev->id) {
	case 0:
		base = S3C64XX_GPD(0);
		break;
	case 1:
		base = S3C64XX_GPE(0);
		break;
	default:
		printk(KERN_DEBUG "Invalid PCM Controller number: %d\n",
			pdev->id);
		return -EINVAL;
	}

	s3c_gpio_cfgpin_range(base, 5, S3C_GPIO_SFN(2));
	return 0;
}

static struct resource s3c64xx_pcm0_resource[] = {
	[0] = DEFINE_RES_MEM(S3C64XX_PA_PCM0, SZ_256),
	[1] = DEFINE_RES_DMA(DMACH_PCM0_TX),
	[2] = DEFINE_RES_DMA(DMACH_PCM0_RX),
};

static struct s3c_audio_pdata s3c_pcm0_pdata = {
	.cfg_gpio = s3c64xx_pcm_cfg_gpio,
};

struct platform_device s3c64xx_device_pcm0 = {
	.name		  = "samsung-pcm",
	.id		  = 0,
	.num_resources	  = ARRAY_SIZE(s3c64xx_pcm0_resource),
	.resource	  = s3c64xx_pcm0_resource,
	.dev = {
		.platform_data = &s3c_pcm0_pdata,
	},
};
EXPORT_SYMBOL(s3c64xx_device_pcm0);

static struct resource s3c64xx_pcm1_resource[] = {
	[0] = DEFINE_RES_MEM(S3C64XX_PA_PCM1, SZ_256),
	[1] = DEFINE_RES_DMA(DMACH_PCM1_TX),
	[2] = DEFINE_RES_DMA(DMACH_PCM1_RX),
};

static struct s3c_audio_pdata s3c_pcm1_pdata = {
	.cfg_gpio = s3c64xx_pcm_cfg_gpio,
};

struct platform_device s3c64xx_device_pcm1 = {
	.name		  = "samsung-pcm",
	.id		  = 1,
	.num_resources	  = ARRAY_SIZE(s3c64xx_pcm1_resource),
	.resource	  = s3c64xx_pcm1_resource,
	.dev = {
		.platform_data = &s3c_pcm1_pdata,
	},
};
EXPORT_SYMBOL(s3c64xx_device_pcm1);

/* AC97 Controller platform devices */

static int s3c64xx_ac97_cfg_gpd(struct platform_device *pdev)
{
	return s3c_gpio_cfgpin_range(S3C64XX_GPD(0), 5, S3C_GPIO_SFN(4));
}

static int s3c64xx_ac97_cfg_gpe(struct platform_device *pdev)
{
	return s3c_gpio_cfgpin_range(S3C64XX_GPE(0), 5, S3C_GPIO_SFN(4));
}

static struct resource s3c64xx_ac97_resource[] = {
	[0] = DEFINE_RES_MEM(S3C64XX_PA_AC97, SZ_256),
	[1] = DEFINE_RES_DMA(DMACH_AC97_PCMOUT),
	[2] = DEFINE_RES_DMA(DMACH_AC97_PCMIN),
	[3] = DEFINE_RES_DMA(DMACH_AC97_MICIN),
	[4] = DEFINE_RES_IRQ(IRQ_AC97),
};

static struct s3c_audio_pdata s3c_ac97_pdata;

static u64 s3c64xx_ac97_dmamask = DMA_BIT_MASK(32);

struct platform_device s3c64xx_device_ac97 = {
	.name		  = "samsung-ac97",
	.id		  = -1,
	.num_resources	  = ARRAY_SIZE(s3c64xx_ac97_resource),
	.resource	  = s3c64xx_ac97_resource,
	.dev = {
		.platform_data = &s3c_ac97_pdata,
		.dma_mask = &s3c64xx_ac97_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};
EXPORT_SYMBOL(s3c64xx_device_ac97);

void __init s3c64xx_ac97_setup_gpio(int num)
{
	if (num == S3C64XX_AC97_GPD)
		s3c_ac97_pdata.cfg_gpio = s3c64xx_ac97_cfg_gpd;
	else
		s3c_ac97_pdata.cfg_gpio = s3c64xx_ac97_cfg_gpe;
}
