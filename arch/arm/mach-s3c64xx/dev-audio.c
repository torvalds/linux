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

#include <mach/irqs.h>
#include <mach/map.h>
#include <mach/dma.h>

#include <plat/devs.h>
#include <plat/audio.h>
#include <plat/gpio-cfg.h>

#include <mach/gpio-bank-c.h>
#include <mach/gpio-bank-d.h>
#include <mach/gpio-bank-e.h>
#include <mach/gpio-bank-h.h>

static int s3c64xx_i2sv3_cfg_gpio(struct platform_device *pdev)
{
	switch (pdev->id) {
	case 0:
		s3c_gpio_cfgpin(S3C64XX_GPD(0), S3C64XX_GPD0_I2S0_CLK);
		s3c_gpio_cfgpin(S3C64XX_GPD(1), S3C64XX_GPD1_I2S0_CDCLK);
		s3c_gpio_cfgpin(S3C64XX_GPD(2), S3C64XX_GPD2_I2S0_LRCLK);
		s3c_gpio_cfgpin(S3C64XX_GPD(3), S3C64XX_GPD3_I2S0_DI);
		s3c_gpio_cfgpin(S3C64XX_GPD(4), S3C64XX_GPD4_I2S0_D0);
		break;
	case 1:
		s3c_gpio_cfgpin(S3C64XX_GPE(0), S3C64XX_GPE0_I2S1_CLK);
		s3c_gpio_cfgpin(S3C64XX_GPE(1), S3C64XX_GPE1_I2S1_CDCLK);
		s3c_gpio_cfgpin(S3C64XX_GPE(2), S3C64XX_GPE2_I2S1_LRCLK);
		s3c_gpio_cfgpin(S3C64XX_GPE(3), S3C64XX_GPE3_I2S1_DI);
		s3c_gpio_cfgpin(S3C64XX_GPE(4), S3C64XX_GPE4_I2S1_D0);
	default:
		printk(KERN_DEBUG "Invalid I2S Controller number!");
		return -EINVAL;
	}

	return 0;
}

static int s3c64xx_i2sv4_cfg_gpio(struct platform_device *pdev)
{
	s3c_gpio_cfgpin(S3C64XX_GPC(4), S3C64XX_GPC4_I2S_V40_DO0);
	s3c_gpio_cfgpin(S3C64XX_GPC(5), S3C64XX_GPC5_I2S_V40_DO1);
	s3c_gpio_cfgpin(S3C64XX_GPC(7), S3C64XX_GPC7_I2S_V40_DO2);
	s3c_gpio_cfgpin(S3C64XX_GPH(6), S3C64XX_GPH6_I2S_V40_BCLK);
	s3c_gpio_cfgpin(S3C64XX_GPH(7), S3C64XX_GPH7_I2S_V40_CDCLK);
	s3c_gpio_cfgpin(S3C64XX_GPH(8), S3C64XX_GPH8_I2S_V40_LRCLK);
	s3c_gpio_cfgpin(S3C64XX_GPH(9), S3C64XX_GPH9_I2S_V40_DI);

	return 0;
}

static struct resource s3c64xx_iis0_resource[] = {
	[0] = {
		.start = S3C64XX_PA_IIS0,
		.end   = S3C64XX_PA_IIS0 + 0x100 - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = DMACH_I2S0_OUT,
		.end   = DMACH_I2S0_OUT,
		.flags = IORESOURCE_DMA,
	},
	[2] = {
		.start = DMACH_I2S0_IN,
		.end   = DMACH_I2S0_IN,
		.flags = IORESOURCE_DMA,
	},
};

static struct s3c_audio_pdata s3c_i2s0_pdata = {
	.cfg_gpio = s3c64xx_i2sv3_cfg_gpio,
};

struct platform_device s3c64xx_device_iis0 = {
	.name		  = "s3c64xx-iis",
	.id		  = 0,
	.num_resources	  = ARRAY_SIZE(s3c64xx_iis0_resource),
	.resource	  = s3c64xx_iis0_resource,
	.dev = {
		.platform_data = &s3c_i2s0_pdata,
	},
};
EXPORT_SYMBOL(s3c64xx_device_iis0);

static struct resource s3c64xx_iis1_resource[] = {
	[0] = {
		.start = S3C64XX_PA_IIS1,
		.end   = S3C64XX_PA_IIS1 + 0x100 - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = DMACH_I2S1_OUT,
		.end   = DMACH_I2S1_OUT,
		.flags = IORESOURCE_DMA,
	},
	[2] = {
		.start = DMACH_I2S1_IN,
		.end   = DMACH_I2S1_IN,
		.flags = IORESOURCE_DMA,
	},
};

static struct s3c_audio_pdata s3c_i2s1_pdata = {
	.cfg_gpio = s3c64xx_i2sv3_cfg_gpio,
};

struct platform_device s3c64xx_device_iis1 = {
	.name		  = "s3c64xx-iis",
	.id		  = 1,
	.num_resources	  = ARRAY_SIZE(s3c64xx_iis1_resource),
	.resource	  = s3c64xx_iis1_resource,
	.dev = {
		.platform_data = &s3c_i2s1_pdata,
	},
};
EXPORT_SYMBOL(s3c64xx_device_iis1);

static struct resource s3c64xx_iisv4_resource[] = {
	[0] = {
		.start = S3C64XX_PA_IISV4,
		.end   = S3C64XX_PA_IISV4 + 0x100 - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = DMACH_HSI_I2SV40_TX,
		.end   = DMACH_HSI_I2SV40_TX,
		.flags = IORESOURCE_DMA,
	},
	[2] = {
		.start = DMACH_HSI_I2SV40_RX,
		.end   = DMACH_HSI_I2SV40_RX,
		.flags = IORESOURCE_DMA,
	},
};

static struct s3c_audio_pdata s3c_i2sv4_pdata = {
	.cfg_gpio = s3c64xx_i2sv4_cfg_gpio,
};

struct platform_device s3c64xx_device_iisv4 = {
	.name		  = "s3c64xx-iis-v4",
	.id		  = -1,
	.num_resources	  = ARRAY_SIZE(s3c64xx_iisv4_resource),
	.resource	  = s3c64xx_iisv4_resource,
	.dev = {
		.platform_data = &s3c_i2sv4_pdata,
	},
};
EXPORT_SYMBOL(s3c64xx_device_iisv4);


/* PCM Controller platform_devices */

static int s3c64xx_pcm_cfg_gpio(struct platform_device *pdev)
{
	switch (pdev->id) {
	case 0:
		s3c_gpio_cfgpin(S3C64XX_GPD(0), S3C64XX_GPD0_PCM0_SCLK);
		s3c_gpio_cfgpin(S3C64XX_GPD(1), S3C64XX_GPD1_PCM0_EXTCLK);
		s3c_gpio_cfgpin(S3C64XX_GPD(2), S3C64XX_GPD2_PCM0_FSYNC);
		s3c_gpio_cfgpin(S3C64XX_GPD(3), S3C64XX_GPD3_PCM0_SIN);
		s3c_gpio_cfgpin(S3C64XX_GPD(4), S3C64XX_GPD4_PCM0_SOUT);
		break;
	case 1:
		s3c_gpio_cfgpin(S3C64XX_GPE(0), S3C64XX_GPE0_PCM1_SCLK);
		s3c_gpio_cfgpin(S3C64XX_GPE(1), S3C64XX_GPE1_PCM1_EXTCLK);
		s3c_gpio_cfgpin(S3C64XX_GPE(2), S3C64XX_GPE2_PCM1_FSYNC);
		s3c_gpio_cfgpin(S3C64XX_GPE(3), S3C64XX_GPE3_PCM1_SIN);
		s3c_gpio_cfgpin(S3C64XX_GPE(4), S3C64XX_GPE4_PCM1_SOUT);
		break;
	default:
		printk(KERN_DEBUG "Invalid PCM Controller number!");
		return -EINVAL;
	}

	return 0;
}

static struct resource s3c64xx_pcm0_resource[] = {
	[0] = {
		.start = S3C64XX_PA_PCM0,
		.end   = S3C64XX_PA_PCM0 + 0x100 - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = DMACH_PCM0_TX,
		.end   = DMACH_PCM0_TX,
		.flags = IORESOURCE_DMA,
	},
	[2] = {
		.start = DMACH_PCM0_RX,
		.end   = DMACH_PCM0_RX,
		.flags = IORESOURCE_DMA,
	},
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
	[0] = {
		.start = S3C64XX_PA_PCM1,
		.end   = S3C64XX_PA_PCM1 + 0x100 - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = DMACH_PCM1_TX,
		.end   = DMACH_PCM1_TX,
		.flags = IORESOURCE_DMA,
	},
	[2] = {
		.start = DMACH_PCM1_RX,
		.end   = DMACH_PCM1_RX,
		.flags = IORESOURCE_DMA,
	},
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
	s3c_gpio_cfgpin(S3C64XX_GPD(0), S3C64XX_GPD0_AC97_BITCLK);
	s3c_gpio_cfgpin(S3C64XX_GPD(1), S3C64XX_GPD1_AC97_nRESET);
	s3c_gpio_cfgpin(S3C64XX_GPD(2), S3C64XX_GPD2_AC97_SYNC);
	s3c_gpio_cfgpin(S3C64XX_GPD(3), S3C64XX_GPD3_AC97_SDI);
	s3c_gpio_cfgpin(S3C64XX_GPD(4), S3C64XX_GPD4_AC97_SDO);

	return 0;
}

static int s3c64xx_ac97_cfg_gpe(struct platform_device *pdev)
{
	s3c_gpio_cfgpin(S3C64XX_GPE(0), S3C64XX_GPE0_AC97_BITCLK);
	s3c_gpio_cfgpin(S3C64XX_GPE(1), S3C64XX_GPE1_AC97_nRESET);
	s3c_gpio_cfgpin(S3C64XX_GPE(2), S3C64XX_GPE2_AC97_SYNC);
	s3c_gpio_cfgpin(S3C64XX_GPE(3), S3C64XX_GPE3_AC97_SDI);
	s3c_gpio_cfgpin(S3C64XX_GPE(4), S3C64XX_GPE4_AC97_SDO);

	return 0;
}

static struct resource s3c64xx_ac97_resource[] = {
	[0] = {
		.start = S3C64XX_PA_AC97,
		.end   = S3C64XX_PA_AC97 + 0x100 - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = DMACH_AC97_PCMOUT,
		.end   = DMACH_AC97_PCMOUT,
		.flags = IORESOURCE_DMA,
	},
	[2] = {
		.start = DMACH_AC97_PCMIN,
		.end   = DMACH_AC97_PCMIN,
		.flags = IORESOURCE_DMA,
	},
	[3] = {
		.start = DMACH_AC97_MICIN,
		.end   = DMACH_AC97_MICIN,
		.flags = IORESOURCE_DMA,
	},
	[4] = {
		.start = IRQ_AC97,
		.end   = IRQ_AC97,
		.flags = IORESOURCE_IRQ,
	},
};

static struct s3c_audio_pdata s3c_ac97_pdata;

static u64 s3c64xx_ac97_dmamask = DMA_BIT_MASK(32);

struct platform_device s3c64xx_device_ac97 = {
	.name		  = "s3c-ac97",
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
