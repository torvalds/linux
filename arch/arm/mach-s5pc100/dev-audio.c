/* linux/arch/arm/mach-s5pc100/dev-audio.c
 *
 * Copyright (c) 2010 Samsung Electronics Co. Ltd
 *	Jaswinder Singh <jassi.brar@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

#include <plat/gpio-cfg.h>
#include <linux/platform_data/asoc-s3c.h>

#include <mach/map.h>
#include <mach/dma.h>
#include <mach/irqs.h>
#include <mach/gpio-samsung.h>

static int s5pc100_cfg_i2s(struct platform_device *pdev)
{
	/* configure GPIO for i2s port */
	switch (pdev->id) {
	case 0: /* Dedicated pins */
		break;
	case 1:
		s3c_gpio_cfgpin_range(S5PC100_GPC(0), 5, S3C_GPIO_SFN(2));
		break;
	case 2:
		s3c_gpio_cfgpin_range(S5PC100_GPG3(0), 5, S3C_GPIO_SFN(4));
		break;
	default:
		printk(KERN_ERR "Invalid Device %d\n", pdev->id);
		return -EINVAL;
	}

	return 0;
}

static struct s3c_audio_pdata i2sv5_pdata = {
	.cfg_gpio = s5pc100_cfg_i2s,
	.type = {
		.i2s = {
			.quirks = QUIRK_PRI_6CHAN | QUIRK_SEC_DAI
					 | QUIRK_NEED_RSTCLR,
		},
	},
};

static struct resource s5pc100_iis0_resource[] = {
	[0] = DEFINE_RES_MEM(S5PC100_PA_I2S0, SZ_256),
	[1] = DEFINE_RES_DMA(DMACH_I2S0_TX),
	[2] = DEFINE_RES_DMA(DMACH_I2S0_RX),
	[3] = DEFINE_RES_DMA(DMACH_I2S0S_TX),
};

struct platform_device s5pc100_device_iis0 = {
	.name = "samsung-i2s",
	.id = 0,
	.num_resources	  = ARRAY_SIZE(s5pc100_iis0_resource),
	.resource	  = s5pc100_iis0_resource,
	.dev = {
		.platform_data = &i2sv5_pdata,
	},
};

static struct s3c_audio_pdata i2sv3_pdata = {
	.cfg_gpio = s5pc100_cfg_i2s,
};

static struct resource s5pc100_iis1_resource[] = {
	[0] = DEFINE_RES_MEM(S5PC100_PA_I2S1, SZ_256),
	[1] = DEFINE_RES_DMA(DMACH_I2S1_TX),
	[2] = DEFINE_RES_DMA(DMACH_I2S1_RX),
};

struct platform_device s5pc100_device_iis1 = {
	.name		  = "samsung-i2s",
	.id		  = 1,
	.num_resources	  = ARRAY_SIZE(s5pc100_iis1_resource),
	.resource	  = s5pc100_iis1_resource,
	.dev = {
		.platform_data = &i2sv3_pdata,
	},
};

static struct resource s5pc100_iis2_resource[] = {
	[0] = DEFINE_RES_MEM(S5PC100_PA_I2S2, SZ_256),
	[1] = DEFINE_RES_DMA(DMACH_I2S2_TX),
	[2] = DEFINE_RES_DMA(DMACH_I2S2_RX),
};

struct platform_device s5pc100_device_iis2 = {
	.name		  = "samsung-i2s",
	.id		  = 2,
	.num_resources	  = ARRAY_SIZE(s5pc100_iis2_resource),
	.resource	  = s5pc100_iis2_resource,
	.dev = {
		.platform_data = &i2sv3_pdata,
	},
};

/* PCM Controller platform_devices */

static int s5pc100_pcm_cfg_gpio(struct platform_device *pdev)
{
	switch (pdev->id) {
	case 0:
		s3c_gpio_cfgpin_range(S5PC100_GPG3(0), 5, S3C_GPIO_SFN(5));
		break;

	case 1:
		s3c_gpio_cfgpin_range(S5PC100_GPC(0), 5, S3C_GPIO_SFN(3));
		break;

	default:
		printk(KERN_DEBUG "Invalid PCM Controller number!");
		return -EINVAL;
	}

	return 0;
}

static struct s3c_audio_pdata s3c_pcm_pdata = {
	.cfg_gpio = s5pc100_pcm_cfg_gpio,
};

static struct resource s5pc100_pcm0_resource[] = {
	[0] = DEFINE_RES_MEM(S5PC100_PA_PCM0, SZ_256),
	[1] = DEFINE_RES_DMA(DMACH_PCM0_TX),
	[2] = DEFINE_RES_DMA(DMACH_PCM0_RX),
};

struct platform_device s5pc100_device_pcm0 = {
	.name		  = "samsung-pcm",
	.id		  = 0,
	.num_resources	  = ARRAY_SIZE(s5pc100_pcm0_resource),
	.resource	  = s5pc100_pcm0_resource,
	.dev = {
		.platform_data = &s3c_pcm_pdata,
	},
};

static struct resource s5pc100_pcm1_resource[] = {
	[0] = DEFINE_RES_MEM(S5PC100_PA_PCM1, SZ_256),
	[1] = DEFINE_RES_DMA(DMACH_PCM1_TX),
	[2] = DEFINE_RES_DMA(DMACH_PCM1_RX),
};

struct platform_device s5pc100_device_pcm1 = {
	.name		  = "samsung-pcm",
	.id		  = 1,
	.num_resources	  = ARRAY_SIZE(s5pc100_pcm1_resource),
	.resource	  = s5pc100_pcm1_resource,
	.dev = {
		.platform_data = &s3c_pcm_pdata,
	},
};

/* AC97 Controller platform devices */

static int s5pc100_ac97_cfg_gpio(struct platform_device *pdev)
{
	return s3c_gpio_cfgpin_range(S5PC100_GPC(0), 5, S3C_GPIO_SFN(4));
}

static struct resource s5pc100_ac97_resource[] = {
	[0] = DEFINE_RES_MEM(S5PC100_PA_AC97, SZ_256),
	[1] = DEFINE_RES_DMA(DMACH_AC97_PCMOUT),
	[2] = DEFINE_RES_DMA(DMACH_AC97_PCMIN),
	[3] = DEFINE_RES_DMA(DMACH_AC97_MICIN),
	[4] = DEFINE_RES_IRQ(IRQ_AC97),
};

static struct s3c_audio_pdata s3c_ac97_pdata = {
	.cfg_gpio = s5pc100_ac97_cfg_gpio,
};

static u64 s5pc100_ac97_dmamask = DMA_BIT_MASK(32);

struct platform_device s5pc100_device_ac97 = {
	.name		  = "samsung-ac97",
	.id		  = -1,
	.num_resources	  = ARRAY_SIZE(s5pc100_ac97_resource),
	.resource	  = s5pc100_ac97_resource,
	.dev = {
		.platform_data = &s3c_ac97_pdata,
		.dma_mask = &s5pc100_ac97_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

/* S/PDIF Controller platform_device */
static int s5pc100_spdif_cfg_gpd(struct platform_device *pdev)
{
	s3c_gpio_cfgpin_range(S5PC100_GPD(5), 2, S3C_GPIO_SFN(3));

	return 0;
}

static int s5pc100_spdif_cfg_gpg3(struct platform_device *pdev)
{
	s3c_gpio_cfgpin_range(S5PC100_GPG3(5), 2, S3C_GPIO_SFN(3));

	return 0;
}

static struct resource s5pc100_spdif_resource[] = {
	[0] = DEFINE_RES_MEM(S5PC100_PA_SPDIF, SZ_256),
	[1] = DEFINE_RES_DMA(DMACH_SPDIF),
};

static struct s3c_audio_pdata s5p_spdif_pdata = {
	.cfg_gpio = s5pc100_spdif_cfg_gpd,
};

static u64 s5pc100_spdif_dmamask = DMA_BIT_MASK(32);

struct platform_device s5pc100_device_spdif = {
	.name		= "samsung-spdif",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s5pc100_spdif_resource),
	.resource	= s5pc100_spdif_resource,
	.dev = {
		.platform_data = &s5p_spdif_pdata,
		.dma_mask = &s5pc100_spdif_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

void __init s5pc100_spdif_setup_gpio(int gpio)
{
	if (gpio == S5PC100_SPDIF_GPD)
		s5p_spdif_pdata.cfg_gpio = s5pc100_spdif_cfg_gpd;
	else
		s5p_spdif_pdata.cfg_gpio = s5pc100_spdif_cfg_gpg3;
}
