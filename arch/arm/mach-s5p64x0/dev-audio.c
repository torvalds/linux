/* linux/arch/arm/mach-s5p64x0/dev-audio.c
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

#include <mach/map.h>
#include <mach/dma.h>
#include <mach/irqs.h>

static const char *rclksrc[] = {
	[0] = "iis",
	[1] = "sclk_audio2",
};

static int s5p6440_cfg_i2s(struct platform_device *pdev)
{
	switch (pdev->id) {
	case 0:
		s3c_gpio_cfgpin_range(S5P6440_GPC(4), 2, S3C_GPIO_SFN(5));
		s3c_gpio_cfgpin(S5P6440_GPC(7), S3C_GPIO_SFN(5));
		s3c_gpio_cfgpin_range(S5P6440_GPH(6), 4, S3C_GPIO_SFN(5));
		break;
	default:
		printk(KERN_ERR "Invalid Device %d\n", pdev->id);
		return -EINVAL;
	}

	return 0;
}

static struct s3c_audio_pdata s5p6440_i2s_pdata = {
	.cfg_gpio = s5p6440_cfg_i2s,
	.type = {
		.i2s = {
			.quirks = QUIRK_PRI_6CHAN,
			.src_clk = rclksrc,
		},
	},
};

static struct resource s5p64x0_i2s0_resource[] = {
	[0] = {
		.start	= S5P64X0_PA_I2S,
		.end	= S5P64X0_PA_I2S + 0x100 - 1,
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
};

struct platform_device s5p6440_device_iis = {
	.name		= "samsung-i2s",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(s5p64x0_i2s0_resource),
	.resource	= s5p64x0_i2s0_resource,
	.dev = {
		.platform_data = &s5p6440_i2s_pdata,
	},
};

static int s5p6450_cfg_i2s(struct platform_device *pdev)
{
	switch (pdev->id) {
	case 0:
		s3c_gpio_cfgpin_range(S5P6450_GPR(4), 5, S3C_GPIO_SFN(5));
		s3c_gpio_cfgpin_range(S5P6450_GPR(13), 2, S3C_GPIO_SFN(5));
		break;
	case 1:
		s3c_gpio_cfgpin(S5P6440_GPB(4), S3C_GPIO_SFN(5));
		s3c_gpio_cfgpin_range(S5P6450_GPC(0), 4, S3C_GPIO_SFN(5));
		break;
	case 2:
		s3c_gpio_cfgpin_range(S5P6450_GPK(0), 5, S3C_GPIO_SFN(5));
		break;
	default:
		printk(KERN_ERR "Invalid Device %d\n", pdev->id);
		return -EINVAL;
	}

	return 0;
}

static struct s3c_audio_pdata s5p6450_i2s0_pdata = {
	.cfg_gpio = s5p6450_cfg_i2s,
	.type = {
		.i2s = {
			.quirks = QUIRK_PRI_6CHAN,
			.src_clk = rclksrc,
		},
	},
};

struct platform_device s5p6450_device_iis0 = {
	.name		= "samsung-i2s",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(s5p64x0_i2s0_resource),
	.resource	= s5p64x0_i2s0_resource,
	.dev = {
		.platform_data = &s5p6450_i2s0_pdata,
	},
};

static struct s3c_audio_pdata s5p6450_i2s_pdata = {
	.cfg_gpio = s5p6450_cfg_i2s,
	.type = {
		.i2s = {
			.src_clk = rclksrc,
		},
	},
};

static struct resource s5p6450_i2s1_resource[] = {
	[0] = {
		.start	= S5P6450_PA_I2S1,
		.end	= S5P6450_PA_I2S1 + 0x100 - 1,
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

struct platform_device s5p6450_device_iis1 = {
	.name		= "samsung-i2s",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(s5p6450_i2s1_resource),
	.resource	= s5p6450_i2s1_resource,
	.dev = {
		.platform_data = &s5p6450_i2s_pdata,
	},
};

static struct resource s5p6450_i2s2_resource[] = {
	[0] = {
		.start	= S5P6450_PA_I2S2,
		.end	= S5P6450_PA_I2S2 + 0x100 - 1,
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

struct platform_device s5p6450_device_iis2 = {
	.name		= "samsung-i2s",
	.id		= 2,
	.num_resources	= ARRAY_SIZE(s5p6450_i2s2_resource),
	.resource	= s5p6450_i2s2_resource,
	.dev = {
		.platform_data = &s5p6450_i2s_pdata,
	},
};

/* PCM Controller platform_devices */

static int s5p6440_pcm_cfg_gpio(struct platform_device *pdev)
{
	switch (pdev->id) {
	case 0:
		s3c_gpio_cfgpin_range(S5P6440_GPR(6), 3, S3C_GPIO_SFN(2));
		s3c_gpio_cfgpin_range(S5P6440_GPR(13), 2, S3C_GPIO_SFN(2));
		break;

	default:
		printk(KERN_DEBUG "Invalid PCM Controller number!");
		return -EINVAL;
	}

	return 0;
}

static struct s3c_audio_pdata s5p6440_pcm_pdata = {
	.cfg_gpio = s5p6440_pcm_cfg_gpio,
};

static struct resource s5p6440_pcm0_resource[] = {
	[0] = {
		.start	= S5P64X0_PA_PCM,
		.end	= S5P64X0_PA_PCM + 0x100 - 1,
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

struct platform_device s5p6440_device_pcm = {
	.name		= "samsung-pcm",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(s5p6440_pcm0_resource),
	.resource	= s5p6440_pcm0_resource,
	.dev = {
		.platform_data = &s5p6440_pcm_pdata,
	},
};
