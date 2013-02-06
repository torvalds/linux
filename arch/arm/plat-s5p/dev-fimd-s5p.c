/* linux/arch/arm/plat-s5p/dev-fimd-s5p.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Base S5P platform device definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <mach/map.h>
#include <plat/fb-s5p.h>

#ifdef CONFIG_FB_S5P
static struct resource s3cfb_resource[] = {
	[0] = {
		.start	= S5P_PA_FIMD0,
		.end	= S5P_PA_FIMD0 + SZ_32K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_FIMD0_VSYNC,
		.end	= IRQ_FIMD0_VSYNC,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= IRQ_FIMD0_FIFO,
		.end	= IRQ_FIMD0_FIFO,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 fb_dma_mask = 0xffffffffUL;

struct platform_device s3c_device_fb = {
	.name		= "s3cfb",
#if defined(CONFIG_ARCH_EXYNOS4)
	.id		= 0,
#else
	.id		= -1,
#endif
	.num_resources	= ARRAY_SIZE(s3cfb_resource),
	.resource	= s3cfb_resource,
	.dev		= {
		.dma_mask		= &fb_dma_mask,
		.coherent_dma_mask	= 0xffffffffUL
	}
};

static struct s3c_platform_fb default_fb_data __initdata = {
#if defined(CONFIG_ARCH_EXYNOS4)
	.hw_ver	= 0x70,
#else
	.hw_ver	= 0x62,
#endif
	.nr_wins	= 5,
#if defined(CONFIG_FB_S5P_DEFAULT_WINDOW)
	.default_win	= CONFIG_FB_S5P_DEFAULT_WINDOW,
#else
	.default_win	= 0,
#endif
	.swap		= FB_SWAP_WORD | FB_SWAP_HWORD,
};

void __init s3cfb_set_platdata(struct s3c_platform_fb *pd)
{
	struct s3c_platform_fb *npd;
	int i;

	if (!pd)
		pd = &default_fb_data;

	npd = kmemdup(pd, sizeof(struct s3c_platform_fb), GFP_KERNEL);
	if (!npd)
		printk(KERN_ERR "%s: no memory for platform data\n", __func__);
	else {
		for (i = 0; i < npd->nr_wins; i++)
			npd->nr_buffers[i] = 1;

#if defined(CONFIG_FB_S5P_NR_BUFFERS)
		npd->nr_buffers[npd->default_win] = CONFIG_FB_S5P_NR_BUFFERS;
#else
		npd->nr_buffers[npd->default_win] = 1;
#endif

		s3cfb_get_clk_name(npd->clk_name);
		npd->set_display_path = s3cfb_set_display_path;
		npd->cfg_gpio = s3cfb_cfg_gpio;
		npd->backlight_on = s3cfb_backlight_on;
		npd->backlight_off = s3cfb_backlight_off;
		npd->lcd_on = s3cfb_lcd_on;
		npd->lcd_off = s3cfb_lcd_off;
		npd->clk_on = s3cfb_clk_on;
		npd->clk_off = s3cfb_clk_off;

		s3c_device_fb.dev.platform_data = npd;
	}
}
#endif

