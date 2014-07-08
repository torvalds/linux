/*
 * rk3036_tve.c 
 *
 * Driver for rockchip rk3036 tv encoder control
 * Copyright (C) 2014 
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 *
 */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/rk_fb.h>
#include <linux/display-sys.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/iomap.h>
#include "rk3036_tve.h"


static const struct fb_videomode rk3036_cvbs_mode [] = {
	/*	name		refresh	xres	yres	pixclock	h_bp	h_fp	v_bp	v_fp	h_pw	v_pw	polariry	PorI		flag*/
	{	"NTSC",		60,	720,	480,	27000000,	69,	12,	19,	2,	63,	3,	0,	FB_VMODE_INTERLACED,	0},
	{	"PAL",		50,	720,	576,	27000000,	57,	19,	19,	0,	62,	3,	0,	FB_VMODE_INTERLACED,	0},
};

static struct rk3036_tve *rk3036_tve = NULL;

#define tve_writel(offset, v)	writel_relaxed(v, rk3036_tve->regbase + offset);
#define tve_readl(offset, v)	readl_relaxed(v, rk3036_tve->regbase + offset);

static void dac_enable(bool enable)
{
	u32 mask, val;
	if(enable) {
		mask = m_VBG_EN | m_DAC_EN;
		val = mask;
	} else {
		mask = m_VBG_EN | m_DAC_EN;
		val = 0;
	}
	grf_writel(RK3036_GRF_SOC_CON3, (mask << 16) | val);
}

static void tve_set_mode (int mode)
{
	tve_writel(TV_RESET, v_RESET(1));
	udelay(100);
	tve_writel(TV_RESET, v_RESET(0));
	
	tve_writel(TV_CTRL, v_CVBS_MODE(mode) | v_CLK_UPSTREAM_EN(2) | 
			v_TIMING_EN(2) | v_LUMA_FILTER_GAIN(0) | 
			v_LUMA_FILTER_UPSAMPLE(1) | v_CSC_PATH(0) );
	tve_writel(TV_LUMA_FILTER0, 0x02ff0000);
	tve_writel(TV_LUMA_FILTER1, 0xF40202fd);
	tve_writel(TV_LUMA_FILTER2, 0xF332d919);
	
	if(mode == TVOUT_CVBS_NTSC) {
		tve_writel(TV_ROUTING, v_DAC_SENSE_EN(0) | v_Y_IRE_7_5(0) | 
			v_Y_AGC_PULSE_ON(1) | v_Y_VIDEO_ON(1) | 
			v_Y_SYNC_ON(1) | v_PIC_MODE(mode));
		tve_writel(TV_BW_CTRL, v_CHROMA_BW(BP_FILTER_NTSC) | v_COLOR_DIFF_BW(COLOR_DIFF_FILTER_BW_1_3));
		tve_writel(TV_SATURATION, 0x0052543C);
		tve_writel(TV_BRIGHTNESS_CONTRAST, 0x00008300);
		
		tve_writel(TV_FREQ_SC,	0x21F07BD7);
		tve_writel(TV_SYNC_TIMING, 0x00C07a81);
		tve_writel(TV_ADJ_TIMING, 0x96B40000);
		tve_writel(TV_ACT_ST,	0x001500D6);
		tve_writel(TV_ACT_TIMING, 0x169800FC);

	} else if (mode == TVOUT_CVBS_PAL) {
		tve_writel(TV_ROUTING, v_DAC_SENSE_EN(0) | v_Y_IRE_7_5(1) | 
			v_Y_AGC_PULSE_ON(1) | v_Y_VIDEO_ON(1) | 
			v_Y_SYNC_ON(1) | v_CVBS_MODE(mode));
		tve_writel(TV_BW_CTRL, v_CHROMA_BW(BP_FILTER_PAL) | v_COLOR_DIFF_BW(COLOR_DIFF_FILTER_BW_1_3));
		tve_writel(TV_SATURATION, 0x002e553c);
		tve_writel(TV_BRIGHTNESS_CONTRAST, 0x00007579);
		
		tve_writel(TV_FREQ_SC,	0x2A098ACB);
		tve_writel(TV_SYNC_TIMING, 0x00C28381);
		tve_writel(TV_ADJ_TIMING, 0xB6C00880);
		tve_writel(TV_ACT_ST,	0x001500F6);
		tve_writel(TV_ACT_TIMING, 0x2694011D);
	}
}

static int tve_switch_fb(const struct fb_videomode *modedb, int enable)
{	
	struct rk_screen *screen;
	
	if(modedb == NULL)
		return -1;
	screen =  kzalloc(sizeof(struct rk_screen), GFP_KERNEL);
	if(screen == NULL)
		return -1;
	
	memset(screen, 0, sizeof(struct rk_screen));	
	/* screen type & face */
	screen->type = SCREEN_TVOUT;
	screen->face = OUT_P888;
	
	screen->mode = *modedb;
	
	/* Pin polarity */
	if(FB_SYNC_HOR_HIGH_ACT & modedb->sync)
		screen->pin_hsync = 1;
	else
		screen->pin_hsync = 0;
	if(FB_SYNC_VERT_HIGH_ACT & modedb->sync)
		screen->pin_vsync = 1;
	else
		screen->pin_vsync = 0;
		
	screen->pin_den = 0;
	screen->pin_dclk = 1;
	screen->pixelrepeat = 1;
	
	/* Swap rule */
	screen->swap_rb = 0;
	screen->swap_rg = 0;
	screen->swap_gb = 0;
	screen->swap_delta = 0;
	screen->swap_dumy = 0;
	
	/* Operation function*/
	screen->init = NULL;
	screen->standby = NULL;	
	
	rk_fb_switch_screen(screen, enable, 0);

	kfree(screen);
	if(enable) {
		if(screen->mode.yres == 480)
			tve_set_mode(TVOUT_CVBS_NTSC);
		else
			tve_set_mode(TVOUT_CVBS_PAL);
	}
	return 0;
}

static int cvbs_set_enable(struct rk_display_device *device, int enable)
{
	if(rk3036_tve->enable != enable)
	{
		rk3036_tve->enable = enable;
		if(rk3036_tve->suspend)
			return 0;
		
		if(enable == 0) {
			dac_enable(false);
			tve_switch_fb(rk3036_tve->mode, 0);
		}
		else if(enable == 1) {
			tve_switch_fb(rk3036_tve->mode, 1);
			dac_enable(true);
		}
	}
	return 0;
}

static int cvbs_get_enable(struct rk_display_device *device)
{
	return rk3036_tve->enable;
}

static int cvbs_get_status(struct rk_display_device *device)
{
	return 1;
}

static int cvbs_get_modelist(struct rk_display_device *device, struct list_head **modelist)
{
	*modelist = &(rk3036_tve->modelist);
	return 0;
}

static int cvbs_set_mode(struct rk_display_device *device, struct fb_videomode *mode)
{
	int i;

	for(i = 0; i < ARRAY_SIZE(rk3036_cvbs_mode); i++)
	{
		if(fb_mode_is_equal(&rk3036_cvbs_mode[i], mode))
		{	
			if( rk3036_tve->mode != &rk3036_cvbs_mode[i] ) {
				rk3036_tve->mode = (struct fb_videomode *)&rk3036_cvbs_mode[i];
				if(rk3036_tve->enable && !rk3036_tve->suspend)
					dac_enable(false);
					tve_switch_fb(rk3036_tve->mode, 1);
					dac_enable(true);
			}
			return 0;
		}
	}
	
	return -1;
}

static int cvbs_get_mode(struct rk_display_device *device, struct fb_videomode *mode)
{
	*mode = *(rk3036_tve->mode);
	return 0;
}

static struct rk_display_ops cvbs_display_ops = {
	.setenable = cvbs_set_enable,
	.getenable = cvbs_get_enable,
	.getstatus = cvbs_get_status,
	.getmodelist = cvbs_get_modelist,
	.setmode = cvbs_set_mode,
	.getmode = cvbs_get_mode,
};

static int display_cvbs_probe(struct rk_display_device *device, void *devdata)
{
	device->owner = THIS_MODULE;
	strcpy(device->type, "TV");
	device->name = "cvbs";
	device->priority = DISPLAY_PRIORITY_TV;
	device->priv_data = devdata;
	device->ops = &cvbs_display_ops;
	return 1;
}

static struct rk_display_driver display_cvbs = {
	.probe = display_cvbs_probe,
};

static int rk3036_tve_probe(struct platform_device *pdev)
{
	struct resource *res;
	int i;
	
	rk3036_tve = devm_kzalloc(&pdev->dev, sizeof(struct rk3036_tve), 
					GFP_KERNEL);
	if(!rk3036_tve) {
		dev_err(&pdev->dev, "rk3036 tv encoder device kmalloc fail!");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, rk3036_tve);
	rk3036_tve->dev = &pdev->dev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rk3036_tve->reg_phy_base = res->start;
	rk3036_tve->len = resource_size(res);
	rk3036_tve->regbase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rk3036_tve->regbase)) {
		dev_err(&pdev->dev, "rk3036 tv encoder device map registers failed!");
		return PTR_ERR(rk3036_tve->regbase);
	}	
	
	INIT_LIST_HEAD(&(rk3036_tve->modelist));
	for(i = 0; i < ARRAY_SIZE(rk3036_cvbs_mode); i++)
		fb_add_videomode(&rk3036_cvbs_mode[i], &(rk3036_tve->modelist));

	rk3036_tve->ddev = rk_display_device_register(&display_cvbs, &pdev->dev, NULL);
	rk_display_device_enable(rk3036_tve->ddev);
	
	dev_info(&pdev->dev, "rk3036 tv encoder probe ok\n");
	return 0;
}

static void rk3036_tve_shutdown(struct platform_device *pdev)
{
	
}

#if defined(CONFIG_OF)
static const struct of_device_id rk3036_tve_dt_ids[] = {
	{.compatible = "rockchip,rk3036-tve",},
	{}
};
#endif

static struct platform_driver rk3036_tve_driver = {
	.probe = rk3036_tve_probe,
	.remove = NULL,
	.driver = {
		.name = "rk3036-tve",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(rk3036_tve_dt_ids),
	},
	.shutdown = rk3036_tve_shutdown,
};

static int __init rk3036_tve_init(void)
{
	return platform_driver_register(&rk3036_tve_driver);
}

static void __exit rk3036_tve_exit(void)
{
    platform_driver_unregister(&rk3036_tve_driver);
}

module_init(rk3036_tve_init);
module_exit(rk3036_tve_exit);

/* Module information */
MODULE_DESCRIPTION("ROCKCHIP RK3036 TV Encoder ");
MODULE_LICENSE("GPL");