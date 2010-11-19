/* linux/arch/arm/mach-s5p6442/dev-audio.c
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

static int s5p6442_cfg_i2s(struct platform_device *pdev)
{
	unsigned int base;

	/* configure GPIO for i2s port */
	switch (pdev->id) {
	case 1:
		base = S5P6442_GPC1(0);
		break;

	case 0:
		base = S5P6442_GPC0(0);
		break;

	default:
		printk(KERN_ERR "Invalid Device %d\n", pdev->id);
		return -EINVAL;
	}

	s3c_gpio_cfgpin_range(base, 5, S3C_GPIO_SFN(2));
	return 0;
}

static const char *rclksrc_v35[] = {
	[0] = "busclk",
	[1] = "i2sclk",
};

static struct s3c_audio_pdata i2sv35_pdata = {
	.cfg_gpio = s5p6442_cfg_i2s,
	.type = {
		.i2s = {
			.quirks = QUIRK_SEC_DAI | QUIRK_NEED_RSTCLR,
			.src_clk = rclksrc_v35,
		},
	},
};

static struct resource s5p6442_iis0_resource[] = {
	[0] = {
		.start = S5P6442_PA_I2S0,
		.end   = S5P6442_PA_I2S0 + 0x100 - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = DMACH_I2S0_TX,
		.end   = DMACH_I2S0_TX,
		.flags = IORESOURCE_DMA,
	},
	[2] = {
		.start = DMACH_I2S0_RX,
		.end   = DMACH_I2S0_RX,
		.flags = IORESOURCE_DMA,
	},
	[3] = {
		.start = DMACH_I2S0S_TX,
		.end = DMACH_I2S0S_TX,
		.flags = IORESOURCE_DMA,
	},
};

struct platform_device s5p6442_device_iis0 = {
	.name = "samsung-i2s",
	.id = 0,
	.num_resources	  = ARRAY_SIZE(s5p6442_iis0_resource),
	.resource	  = s5p6442_iis0_resource,
	.dev = {
		.platform_data = &i2sv35_pdata,
	},
};

static const char *rclksrc_v3[] = {
	[0] = "iis",
	[1] = "sclk_audio",
};

static struct s3c_audio_pdata i2sv3_pdata = {
	.cfg_gpio = s5p6442_cfg_i2s,
	.type = {
		.i2s = {
			.src_clk = rclksrc_v3,
		},
	},
};

static struct resource s5p6442_iis1_resource[] = {
	[0] = {
		.start = S5P6442_PA_I2S1,
		.end   = S5P6442_PA_I2S1 + 0x100 - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = DMACH_I2S1_TX,
		.end   = DMACH_I2S1_TX,
		.flags = IORESOURCE_DMA,
	},
	[2] = {
		.start = DMACH_I2S1_RX,
		.end   = DMACH_I2S1_RX,
		.flags = IORESOURCE_DMA,
	},
};

struct platform_device s5p6442_device_iis1 = {
	.name		  = "samsung-i2s",
	.id		  = 1,
	.num_resources	  = ARRAY_SIZE(s5p6442_iis1_resource),
	.resource	  = s5p6442_iis1_resource,
	.dev = {
		.platform_data = &i2sv3_pdata,
	},
};

/* PCM Controller platform_devices */

static int s5p6442_pcm_cfg_gpio(struct platform_device *pdev)
{
	unsigned int base;

	switch (pdev->id) {
	case 0:
		base = S5P6442_GPC0(0);
		break;

	case 1:
		base = S5P6442_GPC1(0);
		break;

	default:
		printk(KERN_DEBUG "Invalid PCM Controller number!");
		return -EINVAL;
	}

	s3c_gpio_cfgpin_range(base, 5, S3C_GPIO_SFN(3));
	return 0;
}

static struct s3c_audio_pdata s3c_pcm_pdata = {
	.cfg_gpio = s5p6442_pcm_cfg_gpio,
};

static struct resource s5p6442_pcm0_resource[] = {
	[0] = {
		.start = S5P6442_PA_PCM0,
		.end   = S5P6442_PA_PCM0 + 0x100 - 1,
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

struct platform_device s5p6442_device_pcm0 = {
	.name		  = "samsung-pcm",
	.id		  = 0,
	.num_resources	  = ARRAY_SIZE(s5p6442_pcm0_resource),
	.resource	  = s5p6442_pcm0_resource,
	.dev = {
		.platform_data = &s3c_pcm_pdata,
	},
};

static struct resource s5p6442_pcm1_resource[] = {
	[0] = {
		.start = S5P6442_PA_PCM1,
		.end   = S5P6442_PA_PCM1 + 0x100 - 1,
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

struct platform_device s5p6442_device_pcm1 = {
	.name		  = "samsung-pcm",
	.id		  = 1,
	.num_resources	  = ARRAY_SIZE(s5p6442_pcm1_resource),
	.resource	  = s5p6442_pcm1_resource,
	.dev = {
		.platform_data = &s3c_pcm_pdata,
	},
};
