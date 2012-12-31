/* linux/arch/arm/mach-exynos/dev-audio.c
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

#include <plat/gpio-cfg.h>
#include <plat/audio.h>
#include <plat/cpu.h>

#include <mach/map.h>
#include <mach/dma.h>
#include <mach/irqs.h>

static const char *rclksrc[] = {
	[0] = "busclk",
	[1] = "i2sclk",
};

struct exynos_gpio_cfg {
	unsigned int	addr;
	unsigned int	num;
	unsigned int	bit;
};

static int exynos_cfg_i2s_gpio(struct platform_device *pdev)
{
	/* configure GPIO for i2s port */
	struct exynos_gpio_cfg exynos4_cfg[3] = {
				{ EXYNOS4_GPZ(0),  7, S3C_GPIO_SFN(2) },
				{ EXYNOS4_GPC0(0), 5, S3C_GPIO_SFN(2) },
				{ EXYNOS4_GPC1(0), 5, S3C_GPIO_SFN(2) }
	};
	struct exynos_gpio_cfg exynos5_cfg[3] = {
				{ EXYNOS5_GPZ(0),  7, S3C_GPIO_SFN(2) },
				{ EXYNOS5_GPB0(0), 5, S3C_GPIO_SFN(2) },
				{ EXYNOS5_GPB1(0), 5, S3C_GPIO_SFN(2) }
	};

	if (pdev->id < 0 || pdev->id > 2) {
		printk(KERN_ERR "Invalid Device %d\n", pdev->id);
		return -EINVAL;
	}

	if (soc_is_exynos4210() || soc_is_exynos4212() || soc_is_exynos4412())
		s3c_gpio_cfgpin_range(exynos4_cfg[pdev->id].addr,
			exynos4_cfg[pdev->id].num, exynos4_cfg[pdev->id].bit);
	else if (soc_is_exynos5250())
		s3c_gpio_cfgpin_range(exynos5_cfg[pdev->id].addr,
			exynos5_cfg[pdev->id].num, exynos5_cfg[pdev->id].bit);

	return 0;
}

static struct s3c_audio_pdata i2sv5_pdata = {
	.cfg_gpio = exynos_cfg_i2s_gpio,
	.type = {
		.i2s = {
			.quirks = QUIRK_PRI_6CHAN
				| QUIRK_SEC_DAI
#if defined(CONFIG_SND_SAMSUNG_LP) || defined(CONFIG_SND_SAMSUNG_ALP)
				| QUIRK_ENABLED_IDMA
#elif defined(CONFIG_SND_SAMSUNG_RP)
				| QUIRK_ENABLED_SRP
#endif
				| QUIRK_NEED_RSTCLR,
			.src_clk = rclksrc,
		},
	},
};

static struct resource exynos_i2s0_resource[] = {
	[0] = {
		.start	= EXYNOS_PA_I2S0,
		.end	= EXYNOS_PA_I2S0 + 0x100 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= DMACH_I2S0_TX,
		.end	= DMACH_I2S0_TX,
		.flags	= IORESOURCE_DMA,
	},
	[2] = {
		.start	= DMACH_I2S0_RX,
		.end	= DMACH_I2S0_RX,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		.start	= DMACH_I2S0S_TX,
		.end	= DMACH_I2S0S_TX,
		.flags	= IORESOURCE_DMA,
	},
};

struct platform_device exynos_device_i2s0 = {
	.name = "samsung-i2s",
	.id = 0,
	.num_resources = ARRAY_SIZE(exynos_i2s0_resource),
	.resource = exynos_i2s0_resource,
	.dev = {
		.platform_data = &i2sv5_pdata,
	},
};

static const char *rclksrc_v3[] = {
	[0] = "sclk_i2s",
	[1] = "no_such_clock",
};

static struct s3c_audio_pdata i2sv3_pdata = {
	.cfg_gpio = exynos_cfg_i2s_gpio,
	.type = {
		.i2s = {
			.quirks = QUIRK_NO_MUXPSR,
			.src_clk = rclksrc_v3,
		},
	},
};

static struct resource exynos_i2s1_resource[] = {
	[0] = {
		.start	= EXYNOS_PA_I2S1,
		.end	= EXYNOS_PA_I2S1 + 0x100 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= DMACH_I2S1_TX,
		.end	= DMACH_I2S1_TX,
		.flags	= IORESOURCE_DMA,
	},
	[2] = {
		.start	= DMACH_I2S1_RX,
		.end	= DMACH_I2S1_RX,
		.flags	= IORESOURCE_DMA,
	},
};

struct platform_device exynos_device_i2s1 = {
	.name = "samsung-i2s",
	.id = 1,
	.num_resources = ARRAY_SIZE(exynos_i2s1_resource),
	.resource = exynos_i2s1_resource,
	.dev = {
		.platform_data = &i2sv3_pdata,
	},
};

static struct resource exynos_i2s2_resource[] = {
	[0] = {
		.start	= EXYNOS_PA_I2S2,
		.end	= EXYNOS_PA_I2S2 + 0x100 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= DMACH_I2S2_TX,
		.end	= DMACH_I2S2_TX,
		.flags	= IORESOURCE_DMA,
	},
	[2] = {
		.start	= DMACH_I2S2_RX,
		.end	= DMACH_I2S2_RX,
		.flags	= IORESOURCE_DMA,
	},
};

struct platform_device exynos_device_i2s2 = {
	.name = "samsung-i2s",
	.id = 2,
	.num_resources = ARRAY_SIZE(exynos_i2s2_resource),
	.resource = exynos_i2s2_resource,
	.dev = {
		.platform_data = &i2sv3_pdata,
	},
};

/* PCM Controller platform_devices */

static int exynos_pcm_cfg_gpio(struct platform_device *pdev)
{
	/* configure GPIO for pcm port */
	struct exynos_gpio_cfg exynos4_cfg[3] = {
				{ EXYNOS4_GPZ(0),  5, S3C_GPIO_SFN(3) },
				{ EXYNOS4_GPC0(0), 5, S3C_GPIO_SFN(3) },
				{ EXYNOS4_GPC1(0), 5, S3C_GPIO_SFN(3) }
	};
	struct exynos_gpio_cfg exynos5_cfg[3] = {
				{ EXYNOS5_GPZ(0),  5, S3C_GPIO_SFN(3) },
				{ EXYNOS5_GPB0(0), 5, S3C_GPIO_SFN(3) },
				{ EXYNOS5_GPB1(0), 5, S3C_GPIO_SFN(3) }
	};

	if (pdev->id < 0 || pdev->id > 2) {
		printk(KERN_ERR "Invalid Device %d\n", pdev->id);
		return -EINVAL;
	}

	if (soc_is_exynos4210() || soc_is_exynos4212() || soc_is_exynos4412())
		s3c_gpio_cfgpin_range(exynos4_cfg[pdev->id].addr,
			exynos4_cfg[pdev->id].num, exynos4_cfg[pdev->id].bit);
	else if (soc_is_exynos5250())
		s3c_gpio_cfgpin_range(exynos5_cfg[pdev->id].addr,
			exynos5_cfg[pdev->id].num, exynos5_cfg[pdev->id].bit);

	return 0;
}

static struct s3c_audio_pdata s3c_pcm_pdata = {
	.cfg_gpio = exynos_pcm_cfg_gpio,
};

static struct resource exynos_pcm0_resource[] = {
	[0] = {
		.start	= EXYNOS_PA_PCM0,
		.end	= EXYNOS_PA_PCM0 + 0x100 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= DMACH_PCM0_TX,
		.end	= DMACH_PCM0_TX,
		.flags	= IORESOURCE_DMA,
	},
	[2] = {
		.start	= DMACH_PCM0_RX,
		.end	= DMACH_PCM0_RX,
		.flags	= IORESOURCE_DMA,
	},
};

struct platform_device exynos_device_pcm0 = {
	.name = "samsung-pcm",
	.id = 0,
	.num_resources = ARRAY_SIZE(exynos_pcm0_resource),
	.resource = exynos_pcm0_resource,
	.dev = {
		.platform_data = &s3c_pcm_pdata,
	},
};

static struct resource exynos_pcm1_resource[] = {
	[0] = {
		.start	= EXYNOS_PA_PCM1,
		.end	= EXYNOS_PA_PCM1 + 0x100 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= DMACH_PCM1_TX,
		.end	= DMACH_PCM1_TX,
		.flags	= IORESOURCE_DMA,
	},
	[2] = {
		.start	= DMACH_PCM1_RX,
		.end	= DMACH_PCM1_RX,
		.flags	= IORESOURCE_DMA,
	},
};

struct platform_device exynos_device_pcm1 = {
	.name = "samsung-pcm",
	.id = 1,
	.num_resources = ARRAY_SIZE(exynos_pcm1_resource),
	.resource = exynos_pcm1_resource,
	.dev = {
		.platform_data = &s3c_pcm_pdata,
	},
};

static struct resource exynos_pcm2_resource[] = {
	[0] = {
		.start	= EXYNOS_PA_PCM2,
		.end	= EXYNOS_PA_PCM2 + 0x100 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= DMACH_PCM2_TX,
		.end	= DMACH_PCM2_TX,
		.flags	= IORESOURCE_DMA,
	},
	[2] = {
		.start	= DMACH_PCM2_RX,
		.end	= DMACH_PCM2_RX,
		.flags	= IORESOURCE_DMA,
	},
};

struct platform_device exynos_device_pcm2 = {
	.name = "samsung-pcm",
	.id = 2,
	.num_resources = ARRAY_SIZE(exynos_pcm2_resource),
	.resource = exynos_pcm2_resource,
	.dev = {
		.platform_data = &s3c_pcm_pdata,
	},
};

/* AC97 Controller platform devices */

static int exynos_ac97_cfg_gpio(struct platform_device *pdev)
{
	/* configure GPIO for ac97 port */
	if (soc_is_exynos4210() || soc_is_exynos4212() || soc_is_exynos4412())
		s3c_gpio_cfgpin_range(EXYNOS4_GPC0(0), 5, S3C_GPIO_SFN(4));
	else if (soc_is_exynos5250())
		s3c_gpio_cfgpin_range(EXYNOS5_GPB0(0), 5, S3C_GPIO_SFN(4));

	return 0;
}

static struct resource exynos_ac97_resource[] = {
	[0] = {
		.start	= EXYNOS_PA_AC97,
		.end	= EXYNOS_PA_AC97 + 0x100 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= DMACH_AC97_PCMOUT,
		.end	= DMACH_AC97_PCMOUT,
		.flags	= IORESOURCE_DMA,
	},
	[2] = {
		.start	= DMACH_AC97_PCMIN,
		.end	= DMACH_AC97_PCMIN,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		.start	= DMACH_AC97_MICIN,
		.end	= DMACH_AC97_MICIN,
		.flags	= IORESOURCE_DMA,
	},
	[4] = {
		.start	= IRQ_AC97,
		.end	= IRQ_AC97,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct s3c_audio_pdata s3c_ac97_pdata = {
	.cfg_gpio = exynos_ac97_cfg_gpio,
};

static u64 exynos_ac97_dmamask = DMA_BIT_MASK(32);

struct platform_device exynos_device_ac97 = {
	.name = "samsung-ac97",
	.id = -1,
	.num_resources = ARRAY_SIZE(exynos_ac97_resource),
	.resource = exynos_ac97_resource,
	.dev = {
		.platform_data = &s3c_ac97_pdata,
		.dma_mask = &exynos_ac97_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

/* S/PDIF Controller platform_device */

static int exynos_spdif_cfg_gpio(struct platform_device *pdev)
{
#if !defined(CONFIG_MACH_ODROID_4X12)
	/* configure GPIO for SPDIF port */
	if (soc_is_exynos4210() || soc_is_exynos4212() || soc_is_exynos4412())
		s3c_gpio_cfgpin_range(EXYNOS4_GPC1(0), 2, S3C_GPIO_SFN(4));
	else if (soc_is_exynos5250())
		s3c_gpio_cfgpin_range(EXYNOS5_GPB1(0), 2, S3C_GPIO_SFN(4));
#endif
	return 0;
}

static struct resource exynos_spdif_resource[] = {
	[0] = {
		.start	= EXYNOS_PA_SPDIF,
		.end	= EXYNOS_PA_SPDIF + 0x100 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= DMACH_SPDIF,
		.end	= DMACH_SPDIF,
		.flags	= IORESOURCE_DMA,
	},
};

static struct s3c_audio_pdata samsung_spdif_pdata = {
	.cfg_gpio = exynos_spdif_cfg_gpio,
};

static u64 exynos_spdif_dmamask = DMA_BIT_MASK(32);

struct platform_device exynos_device_spdif = {
	.name = "samsung-spdif",
	.id = -1,
	.num_resources = ARRAY_SIZE(exynos_spdif_resource),
	.resource = exynos_spdif_resource,
	.dev = {
		.platform_data = &samsung_spdif_pdata,
		.dma_mask = &exynos_spdif_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

#if defined(CONFIG_SND_SAMSUNG_RP) || defined(CONFIG_SND_SAMSUNG_ALP)
static struct resource exynos_srp_resource[] = {
};

static u64 exynos_srp_dmamask = DMA_BIT_MASK(32);

struct platform_device exynos_device_srp = {
	.name             = "samsung-rp",
	.id               = -1,
	.num_resources    = ARRAY_SIZE(exynos_srp_resource),
	.resource         = exynos_srp_resource,
	.dev = {
		.dma_mask = &exynos_srp_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};
EXPORT_SYMBOL(exynos_device_srp);
#endif

#ifdef CONFIG_ARCH_EXYNOS4
void __init exynos4_i2sv3_setup_resource(void)
{
	if (!soc_is_exynos4210()) {
		exynos_i2s1_resource[0].start = EXYNOS4212_PA_I2S1;
		exynos_i2s1_resource[0].end   = EXYNOS4212_PA_I2S1 + 0x100 - 1;
		exynos_i2s2_resource[0].start = EXYNOS4212_PA_I2S2;
		exynos_i2s2_resource[0].end   = EXYNOS4212_PA_I2S2 + 0x100 - 1;
	}
}
#endif
