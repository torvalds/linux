/* linux/arch/arm/mach-exynos4/dev-audio.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
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
#include <linux/gpio.h>
#include <linux/platform_data/asoc-s3c.h>

#include <plat/gpio-cfg.h>

#include <mach/map.h>
#include <mach/dma.h>
#include <mach/irqs.h>

#define EXYNOS4_AUDSS_INT_MEM	(0x03000000)

static int exynos4_cfg_i2s(struct platform_device *pdev)
{
	/* configure GPIO for i2s port */
	switch (pdev->id) {
	case 0:
		s3c_gpio_cfgpin_range(EXYNOS4_GPZ(0), 7, S3C_GPIO_SFN(2));
		break;
	case 1:
		s3c_gpio_cfgpin_range(EXYNOS4_GPC0(0), 5, S3C_GPIO_SFN(2));
		break;
	case 2:
		s3c_gpio_cfgpin_range(EXYNOS4_GPC1(0), 5, S3C_GPIO_SFN(4));
		break;
	default:
		printk(KERN_ERR "Invalid Device %d\n", pdev->id);
		return -EINVAL;
	}

	return 0;
}

static struct s3c_audio_pdata i2sv5_pdata = {
	.cfg_gpio = exynos4_cfg_i2s,
	.type = {
		.i2s = {
			.quirks = QUIRK_PRI_6CHAN | QUIRK_SEC_DAI
					 | QUIRK_NEED_RSTCLR,
			.idma_addr = EXYNOS4_AUDSS_INT_MEM,
		},
	},
};

static struct resource exynos4_i2s0_resource[] = {
	[0] = DEFINE_RES_MEM(EXYNOS4_PA_I2S0, SZ_256),
	[1] = DEFINE_RES_DMA(DMACH_I2S0_TX),
	[2] = DEFINE_RES_DMA(DMACH_I2S0_RX),
	[3] = DEFINE_RES_DMA(DMACH_I2S0S_TX),
};

struct platform_device exynos4_device_i2s0 = {
	.name = "samsung-i2s",
	.id = 0,
	.num_resources = ARRAY_SIZE(exynos4_i2s0_resource),
	.resource = exynos4_i2s0_resource,
	.dev = {
		.platform_data = &i2sv5_pdata,
	},
};

static struct s3c_audio_pdata i2sv3_pdata = {
	.cfg_gpio = exynos4_cfg_i2s,
	.type = {
		.i2s = {
			.quirks = QUIRK_NO_MUXPSR,
		},
	},
};

static struct resource exynos4_i2s1_resource[] = {
	[0] = DEFINE_RES_MEM(EXYNOS4_PA_I2S1, SZ_256),
	[1] = DEFINE_RES_DMA(DMACH_I2S1_TX),
	[2] = DEFINE_RES_DMA(DMACH_I2S1_RX),
};

struct platform_device exynos4_device_i2s1 = {
	.name = "samsung-i2s",
	.id = 1,
	.num_resources = ARRAY_SIZE(exynos4_i2s1_resource),
	.resource = exynos4_i2s1_resource,
	.dev = {
		.platform_data = &i2sv3_pdata,
	},
};

static struct resource exynos4_i2s2_resource[] = {
	[0] = DEFINE_RES_MEM(EXYNOS4_PA_I2S2, SZ_256),
	[1] = DEFINE_RES_DMA(DMACH_I2S2_TX),
	[2] = DEFINE_RES_DMA(DMACH_I2S2_RX),
};

struct platform_device exynos4_device_i2s2 = {
	.name = "samsung-i2s",
	.id = 2,
	.num_resources = ARRAY_SIZE(exynos4_i2s2_resource),
	.resource = exynos4_i2s2_resource,
	.dev = {
		.platform_data = &i2sv3_pdata,
	},
};

/* PCM Controller platform_devices */

static int exynos4_pcm_cfg_gpio(struct platform_device *pdev)
{
	switch (pdev->id) {
	case 0:
		s3c_gpio_cfgpin_range(EXYNOS4_GPZ(0), 5, S3C_GPIO_SFN(3));
		break;
	case 1:
		s3c_gpio_cfgpin_range(EXYNOS4_GPC0(0), 5, S3C_GPIO_SFN(3));
		break;
	case 2:
		s3c_gpio_cfgpin_range(EXYNOS4_GPC1(0), 5, S3C_GPIO_SFN(3));
		break;
	default:
		printk(KERN_DEBUG "Invalid PCM Controller number!");
		return -EINVAL;
	}

	return 0;
}

static struct s3c_audio_pdata s3c_pcm_pdata = {
	.cfg_gpio = exynos4_pcm_cfg_gpio,
};

static struct resource exynos4_pcm0_resource[] = {
	[0] = DEFINE_RES_MEM(EXYNOS4_PA_PCM0, SZ_256),
	[1] = DEFINE_RES_DMA(DMACH_PCM0_TX),
	[2] = DEFINE_RES_DMA(DMACH_PCM0_RX),
};

struct platform_device exynos4_device_pcm0 = {
	.name = "samsung-pcm",
	.id = 0,
	.num_resources = ARRAY_SIZE(exynos4_pcm0_resource),
	.resource = exynos4_pcm0_resource,
	.dev = {
		.platform_data = &s3c_pcm_pdata,
	},
};

static struct resource exynos4_pcm1_resource[] = {
	[0] = DEFINE_RES_MEM(EXYNOS4_PA_PCM1, SZ_256),
	[1] = DEFINE_RES_DMA(DMACH_PCM1_TX),
	[2] = DEFINE_RES_DMA(DMACH_PCM1_RX),
};

struct platform_device exynos4_device_pcm1 = {
	.name = "samsung-pcm",
	.id = 1,
	.num_resources = ARRAY_SIZE(exynos4_pcm1_resource),
	.resource = exynos4_pcm1_resource,
	.dev = {
		.platform_data = &s3c_pcm_pdata,
	},
};

static struct resource exynos4_pcm2_resource[] = {
	[0] = DEFINE_RES_MEM(EXYNOS4_PA_PCM2, SZ_256),
	[1] = DEFINE_RES_DMA(DMACH_PCM2_TX),
	[2] = DEFINE_RES_DMA(DMACH_PCM2_RX),
};

struct platform_device exynos4_device_pcm2 = {
	.name = "samsung-pcm",
	.id = 2,
	.num_resources = ARRAY_SIZE(exynos4_pcm2_resource),
	.resource = exynos4_pcm2_resource,
	.dev = {
		.platform_data = &s3c_pcm_pdata,
	},
};

/* AC97 Controller platform devices */

static int exynos4_ac97_cfg_gpio(struct platform_device *pdev)
{
	return s3c_gpio_cfgpin_range(EXYNOS4_GPC0(0), 5, S3C_GPIO_SFN(4));
}

static struct resource exynos4_ac97_resource[] = {
	[0] = DEFINE_RES_MEM(EXYNOS4_PA_AC97, SZ_256),
	[1] = DEFINE_RES_DMA(DMACH_AC97_PCMOUT),
	[2] = DEFINE_RES_DMA(DMACH_AC97_PCMIN),
	[3] = DEFINE_RES_DMA(DMACH_AC97_MICIN),
	[4] = DEFINE_RES_IRQ(EXYNOS4_IRQ_AC97),
};

static struct s3c_audio_pdata s3c_ac97_pdata = {
	.cfg_gpio = exynos4_ac97_cfg_gpio,
};

static u64 exynos4_ac97_dmamask = DMA_BIT_MASK(32);

struct platform_device exynos4_device_ac97 = {
	.name = "samsung-ac97",
	.id = -1,
	.num_resources = ARRAY_SIZE(exynos4_ac97_resource),
	.resource = exynos4_ac97_resource,
	.dev = {
		.platform_data = &s3c_ac97_pdata,
		.dma_mask = &exynos4_ac97_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

/* S/PDIF Controller platform_device */

static int exynos4_spdif_cfg_gpio(struct platform_device *pdev)
{
	s3c_gpio_cfgpin_range(EXYNOS4_GPC1(0), 2, S3C_GPIO_SFN(4));

	return 0;
}

static struct resource exynos4_spdif_resource[] = {
	[0] = DEFINE_RES_MEM(EXYNOS4_PA_SPDIF, SZ_256),
	[1] = DEFINE_RES_DMA(DMACH_SPDIF),
};

static struct s3c_audio_pdata samsung_spdif_pdata = {
	.cfg_gpio = exynos4_spdif_cfg_gpio,
};

static u64 exynos4_spdif_dmamask = DMA_BIT_MASK(32);

struct platform_device exynos4_device_spdif = {
	.name = "samsung-spdif",
	.id = -1,
	.num_resources = ARRAY_SIZE(exynos4_spdif_resource),
	.resource = exynos4_spdif_resource,
	.dev = {
		.platform_data = &samsung_spdif_pdata,
		.dma_mask = &exynos4_spdif_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};
