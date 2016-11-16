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
#include <linux/clk.h>
#include <linux/display-sys.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/rk_fb.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/iomap.h>
#include "../../hdmi/rockchip-hdmiv2/rockchip_hdmiv2.h"
#include "rk3036_tve.h"

static const struct fb_videomode rk3036_cvbs_mode[] = {
	/*name		refresh	xres	yres	pixclock	h_bp	h_fp	v_bp	v_fp	h_pw	v_pw			polariry				PorI		flag*/
/*	{"NTSC",        60,     720,    480,    27000000,       57,     19,     19,     0,      62,     3,      FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,   FB_VMODE_INTERLACED,    0},
	{"PAL",         50,     720,    576,    27000000,       69,     12,     19,     2,      63,     3,      FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,   FB_VMODE_INTERLACED,    0},
*/	{"NTSC",	60,	720,	480,	27000000,	43,	33,	19,	0,	62,	3,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	FB_VMODE_INTERLACED,	0},
	{"PAL",		50,	720,	576,	27000000,	48,	33,	19,	2,	63,	3,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	FB_VMODE_INTERLACED,	0},
};

static struct rk3036_tve *rk3036_tve;

static int cvbsformat = -1;

#define tve_writel(offset, v)	writel_relaxed(v, rk3036_tve->regbase + offset)
#define tve_readl(offset)	readl_relaxed(rk3036_tve->regbase + offset)

#define tve_dac_writel(offset, v)   writel_relaxed(v, rk3036_tve->vdacbase + offset)
#define tve_dac_readl(offset)	readl_relaxed(rk3036_tve->vdacbase + offset)

#ifdef DEBUG
#define TVEDBG(format, ...) \
		dev_info(rk3036_tve->dev,\
		 "RK3036 TVE: " format "\n", ## __VA_ARGS__)
#else
#define TVEDBG(format, ...)
#endif

static void dac_enable(bool enable)
{
	u32 mask, val;
	u32 grfreg = 0;

	TVEDBG("%s enable %d\n", __func__, enable);

	if (enable) {
		mask = m_VBG_EN | m_DAC_EN | m_DAC_GAIN;
		if (rk3036_tve->soctype == SOC_RK312X) {
			val = m_VBG_EN | m_DAC_EN |
			      v_DAC_GAIN(rk3036_tve->daclevel);
			grfreg = RK312X_GRF_TVE_CON;
		} else if (rk3036_tve->soctype == SOC_RK3036) {
			val = m_VBG_EN | m_DAC_EN |
			      v_DAC_GAIN(rk3036_tve->daclevel);
			grfreg = RK3036_GRF_SOC_CON3;
		} else if (rk3036_tve->soctype == SOC_RK322X) {
			val = 0x70;
		}
	} else {
		mask = m_VBG_EN | m_DAC_EN;
		val = 0;
		if (rk3036_tve->soctype == SOC_RK312X)
			grfreg = RK312X_GRF_TVE_CON;
		else if (rk3036_tve->soctype == SOC_RK3036)
			grfreg = RK3036_GRF_SOC_CON3;
		else if (rk3036_tve->soctype == SOC_RK322X)
			val = v_CUR_REG(0x7) | m_DR_PWR_DOWN | m_BG_PWR_DOWN;
	}
	if (grfreg)
		grf_writel(grfreg, (mask << 16) | val);
	else if (rk3036_tve->vdacbase)
		tve_dac_writel(VDAC_VDAC1, val);
}

static void rk322x_dac_init(void)
{
	/*tve_dac_writel(VDAC_VDAC0, 0x0);*/
	tve_dac_writel(VDAC_VDAC1, v_CUR_REG(0x7) |
				   m_DR_PWR_DOWN | m_BG_PWR_DOWN);
	tve_dac_writel(VDAC_VDAC2, v_CUR_CTR(rk3036_tve->daclevel));
	tve_dac_writel(VDAC_VDAC3, v_CAB_EN(0));
}

static void tve_set_mode(int mode)
{
	TVEDBG("%s mode %d\n", __func__, mode);
	if (cvbsformat >= 0)
		return;
	if (rk3036_tve->soctype != SOC_RK322X) {
		tve_writel(TV_RESET, v_RESET(1));
		usleep_range(100, 110);
		tve_writel(TV_RESET, v_RESET(0));
	}
	if (rk3036_tve->inputformat == INPUT_FORMAT_RGB)
		tve_writel(TV_CTRL, v_CVBS_MODE(mode) | v_CLK_UPSTREAM_EN(2) |
			   v_TIMING_EN(2) | v_LUMA_FILTER_GAIN(0) |
			   v_LUMA_FILTER_UPSAMPLE(1) | v_CSC_PATH(0));
	else
		tve_writel(TV_CTRL, v_CVBS_MODE(mode) | v_CLK_UPSTREAM_EN(2) |
			   v_TIMING_EN(2) | v_LUMA_FILTER_GAIN(0) |
			   v_LUMA_FILTER_UPSAMPLE(1) | v_CSC_PATH(3));

	tve_writel(TV_LUMA_FILTER0, rk3036_tve->lumafilter0);
	tve_writel(TV_LUMA_FILTER1, rk3036_tve->lumafilter1);
	tve_writel(TV_LUMA_FILTER2, rk3036_tve->lumafilter2);

	if (mode == TVOUT_CVBS_NTSC) {
		tve_writel(TV_ROUTING, v_DAC_SENSE_EN(0) | v_Y_IRE_7_5(1) |
			v_Y_AGC_PULSE_ON(0) | v_Y_VIDEO_ON(1) |
			v_YPP_MODE(1) | v_Y_SYNC_ON(1) | v_PIC_MODE(mode));
		tve_writel(TV_BW_CTRL, v_CHROMA_BW(BP_FILTER_NTSC) |
			v_COLOR_DIFF_BW(COLOR_DIFF_FILTER_BW_1_3));
		tve_writel(TV_SATURATION, 0x0042543C);
		if (rk3036_tve->test_mode)
			tve_writel(TV_BRIGHTNESS_CONTRAST, 0x00008300);
		else
			tve_writel(TV_BRIGHTNESS_CONTRAST, 0x00007900);

		tve_writel(TV_FREQ_SC,	0x21F07BD7);
		tve_writel(TV_SYNC_TIMING, 0x00C07a81);
		tve_writel(TV_ADJ_TIMING, 0x96B40000 | 0x70);
		tve_writel(TV_ACT_ST,	0x001500D6);
		tve_writel(TV_ACT_TIMING, 0x069800FC | (1 << 12) | (1 << 28));

	} else if (mode == TVOUT_CVBS_PAL) {
		tve_writel(TV_ROUTING, v_DAC_SENSE_EN(0) | v_Y_IRE_7_5(0) |
			v_Y_AGC_PULSE_ON(0) | v_Y_VIDEO_ON(1) |
			v_YPP_MODE(1) | v_Y_SYNC_ON(1) | v_PIC_MODE(mode));
		tve_writel(TV_BW_CTRL, v_CHROMA_BW(BP_FILTER_PAL) |
			v_COLOR_DIFF_BW(COLOR_DIFF_FILTER_BW_1_3));

		tve_writel(TV_SATURATION, rk3036_tve->saturation);
		tve_writel(TV_BRIGHTNESS_CONTRAST, rk3036_tve->brightcontrast);

		tve_writel(TV_FREQ_SC,	0x2A098ACB);
		tve_writel(TV_SYNC_TIMING, 0x00C28381);
		tve_writel(TV_ADJ_TIMING, (0xc << 28) | 0x06c00800 | 0x80);
		tve_writel(TV_ACT_ST,	0x001500F6);
		tve_writel(TV_ACT_TIMING, 0x0694011D | (1 << 12) | (2 << 28));

		tve_writel(TV_ADJ_TIMING, rk3036_tve->adjtiming);
		tve_writel(TV_ACT_TIMING, 0x0694011D |
				(1 << 12) | (2 << 28));
	}
}

static int tve_switch_fb(const struct fb_videomode *modedb, int enable)
{
	struct rk_screen *screen = &rk3036_tve->screen;

	if (!modedb)
		return -1;

	memset(screen, 0, sizeof(struct rk_screen));
	/* screen type & face */
	if (rk3036_tve->test_mode)
		screen->type = SCREEN_TVOUT_TEST;
	else
		screen->type = SCREEN_TVOUT;
	screen->face = OUT_P888;
	screen->color_mode = COLOR_YCBCR;
	screen->mode = *modedb;

	/* Pin polarity */
	if (FB_SYNC_HOR_HIGH_ACT & modedb->sync)
		screen->pin_hsync = 1;
	else
		screen->pin_hsync = 0;
	if (FB_SYNC_VERT_HIGH_ACT & modedb->sync)
		screen->pin_vsync = 1;
	else
		screen->pin_vsync = 0;

	screen->pin_den = 0;
	screen->pin_dclk = 0;
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

	if (enable) {
		if (rk3036_tve->soctype == SOC_RK322X)
			ext_pll_set_27m_out();
		if (screen->mode.yres == 480)
			tve_set_mode(TVOUT_CVBS_NTSC);
		else
			tve_set_mode(TVOUT_CVBS_PAL);
	}
	return 0;
}

static int cvbs_set_enable(struct rk_display_device *device, int enable)
{
	TVEDBG("%s enable %d\n", __func__, enable);
	mutex_lock(&rk3036_tve->tve_lock);
	if (rk3036_tve->enable != enable) {
		rk3036_tve->enable = enable;
		if (rk3036_tve->suspend) {
			mutex_unlock(&rk3036_tve->tve_lock);
			return 0;
		}

		if (enable == 0) {
			dac_enable(false);
			cvbsformat = -1;
			tve_switch_fb(rk3036_tve->mode, 0);
		} else if (enable == 1) {
			tve_switch_fb(rk3036_tve->mode, 1);
			dac_enable(true);
		}
	}
	mutex_unlock(&rk3036_tve->tve_lock);
	return 0;
}

static int cvbs_get_enable(struct rk_display_device *device)
{
	TVEDBG("%s enable %d\n", __func__, rk3036_tve->enable);
	return rk3036_tve->enable;
}

static int cvbs_get_status(struct rk_display_device *device)
{
	return 1;
}

static int
cvbs_get_modelist(struct rk_display_device *device, struct list_head **modelist)
{
	*modelist = &rk3036_tve->modelist;
	return 0;
}

static int
cvbs_set_mode(struct rk_display_device *device, struct fb_videomode *mode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rk3036_cvbs_mode); i++) {
		if (fb_mode_is_equal(&rk3036_cvbs_mode[i], mode)) {
			if (rk3036_tve->mode != &rk3036_cvbs_mode[i]) {
				rk3036_tve->mode =
				(struct fb_videomode *)&rk3036_cvbs_mode[i];
				if (rk3036_tve->enable && !rk3036_tve->suspend) {
					dac_enable(false);
					msleep(200);
					tve_switch_fb(rk3036_tve->mode, 1);
					dac_enable(true);
				}
			}
			return 0;
		}
	}
	TVEDBG("%s\n", __func__);
	return -1;
}

static int
cvbs_get_mode(struct rk_display_device *device, struct fb_videomode *mode)
{
	*mode = *rk3036_tve->mode;
	return 0;
}

static int
tve_fb_event_notify(struct notifier_block *self,
		    unsigned long action, void *data)
{
	struct fb_event *event = data;

	if (action == FB_EARLY_EVENT_BLANK) {
		switch (*((int *)event->data)) {
		case FB_BLANK_UNBLANK:
			break;
		default:
			TVEDBG("suspend tve\n");
			if (!rk3036_tve->suspend) {
				rk3036_tve->suspend = 1;
				if (rk3036_tve->enable) {
					tve_switch_fb(rk3036_tve->mode, 0);
					dac_enable(false);
					if (rk3036_tve->soctype == SOC_RK322X)
						clk_disable_unprepare(rk3036_tve->dac_clk);
				}
			}
			break;
		}
	} else if (action == FB_EVENT_BLANK) {
		switch (*((int *)event->data)) {
		case FB_BLANK_UNBLANK:
			TVEDBG("resume tve\n");
			mutex_lock(&rk3036_tve->tve_lock);
			if (rk3036_tve->suspend) {
				if (rk3036_tve->soctype == SOC_RK322X) {
					clk_prepare_enable(rk3036_tve->dac_clk);
					rk322x_dac_init();
				}
				rk3036_tve->suspend = 0;
				if (rk3036_tve->enable) {
					tve_switch_fb(rk3036_tve->mode, 1);
					dac_enable(true);
				}
			}
			mutex_unlock(&rk3036_tve->tve_lock);
			break;
		default:
			break;
		}
	}
	return NOTIFY_OK;
}

static struct notifier_block tve_fb_notifier = {
	.notifier_call = tve_fb_event_notify,
};

static struct rk_display_ops cvbs_display_ops = {
	.setenable = cvbs_set_enable,
	.getenable = cvbs_get_enable,
	.getstatus = cvbs_get_status,
	.getmodelist = cvbs_get_modelist,
	.setmode = cvbs_set_mode,
	.getmode = cvbs_get_mode,
};

static int
display_cvbs_probe(struct rk_display_device *device, void *devdata)
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

#if defined(CONFIG_OF)
static const struct of_device_id rk3036_tve_dt_ids[] = {
	{.compatible = "rockchip,rk3036-tve",},
	{.compatible = "rockchip,rk312x-tve",},
	{.compatible = "rockchip,rk322x-tve",},
	{}
};
#endif

static int __init bootloader_tve_setup(char *str)
{
	static int ret;

	if (str) {
		pr_info("cvbs init tve.format is %s\n", str);
		ret = sscanf(str, "%d", &cvbsformat);
	}

	return 0;
}

early_param("tve.format", bootloader_tve_setup);

static int rk3036_tve_parse_dt(struct device_node *np,
			       struct rk3036_tve *rk3036_tve)
{
	int ret;
	u32 val;

	if (rk3036_tve->soctype == SOC_RK312X) {
		ret = of_property_read_u32(np, "test_mode", &val);
		if (ret < 0)
			goto errer;
		else
			rk3036_tve->test_mode = val;
	}

	ret = of_property_read_u32(np, "saturation", &val);
	if ((val == 0) || (ret < 0))
		goto errer;
	else
		rk3036_tve->saturation = val;

	ret = of_property_read_u32(np, "brightcontrast", &val);
	if ((val == 0) || (ret < 0))
		goto errer;
	else
		rk3036_tve->brightcontrast = val;

	ret = of_property_read_u32(np, "adjtiming", &val);
	if ((val == 0) || (ret < 0))
		goto errer;
	else
		rk3036_tve->adjtiming = val;

	ret = of_property_read_u32(np, "lumafilter0", &val);
	if ((val == 0) || (ret < 0))
		goto errer;
	else
		rk3036_tve->lumafilter0 = val;

	ret = of_property_read_u32(np, "lumafilter1", &val);
	if ((val == 0) || (ret < 0))
		goto errer;
	else
		rk3036_tve->lumafilter1 = val;

	ret = of_property_read_u32(np, "lumafilter2", &val);
	if ((val == 0) || (ret < 0))
		goto errer;
	else
		rk3036_tve->lumafilter2 = val;

	ret = of_property_read_u32(np, "daclevel", &val);
	if ((val == 0) || (ret < 0))
		goto errer;
	else
		rk3036_tve->daclevel = val;

	return 0;
errer:
	return -1;
}

static int rk3036_tve_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	const struct of_device_id *match;
	int i;
	int ret;

	match = of_match_node(rk3036_tve_dt_ids, np);
	if (!match)
		return PTR_ERR(match);

	rk3036_tve = devm_kzalloc(&pdev->dev,
				  sizeof(struct rk3036_tve), GFP_KERNEL);
	if (!rk3036_tve) {
		dev_err(&pdev->dev, "rk3036 tv encoder device kmalloc fail!");
		return -ENOMEM;
	}

	if (!strcmp(match->compatible, "rockchip,rk3036-tve")) {
		rk3036_tve->soctype = SOC_RK3036;
		rk3036_tve->inputformat = INPUT_FORMAT_RGB;
	} else if (!strcmp(match->compatible, "rockchip,rk312x-tve")) {
		rk3036_tve->soctype = SOC_RK312X;
		rk3036_tve->inputformat = INPUT_FORMAT_YUV;
	} else if (!strcmp(match->compatible, "rockchip,rk322x-tve")) {
		rk3036_tve->soctype = SOC_RK322X;
		rk3036_tve->inputformat = INPUT_FORMAT_YUV;
	} else {
		dev_err(&pdev->dev, "It is not a valid tv encoder!");
		return -ENOMEM;
	}

	ret = rk3036_tve_parse_dt(np, rk3036_tve);
	if (ret) {
		dev_err(&pdev->dev, "TVE parse dts error!");
		return -EINVAL;
	}

	platform_set_drvdata(pdev, rk3036_tve);
	rk3036_tve->dev = &pdev->dev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rk3036_tve->reg_phy_base = res->start;
	rk3036_tve->len = resource_size(res);
	rk3036_tve->regbase = ioremap(res->start, rk3036_tve->len);
	if (IS_ERR(rk3036_tve->regbase)) {
		dev_err(&pdev->dev,
			"rk3036 tv encoder device map registers failed!");
		return PTR_ERR(rk3036_tve->regbase);
	}
	if (rk3036_tve->soctype == SOC_RK322X) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		rk3036_tve->len = resource_size(res);
		rk3036_tve->vdacbase = devm_ioremap(rk3036_tve->dev,
						    res->start,
						    rk3036_tve->len);
		if (IS_ERR(rk3036_tve->vdacbase)) {
			dev_err(&pdev->dev,
				"rk3036 tv encoder device dac map registers failed!");
			return PTR_ERR(rk3036_tve->vdacbase);
		}
		rk3036_tve->dac_clk =
				devm_clk_get(rk3036_tve->dev, "pclk_vdac");
		if (IS_ERR(rk3036_tve->dac_clk)) {
			dev_err(&pdev->dev,
				"Unable to get vdac_clk\n");
			return PTR_ERR(rk3036_tve->dac_clk);
		}
		clk_prepare_enable(rk3036_tve->dac_clk);
		if (cvbsformat < 0)
			rk322x_dac_init();
	}
	mutex_init(&rk3036_tve->tve_lock);
	INIT_LIST_HEAD(&rk3036_tve->modelist);
	for (i = 0; i < ARRAY_SIZE(rk3036_cvbs_mode); i++)
		fb_add_videomode(&rk3036_cvbs_mode[i], &rk3036_tve->modelist);
	 if (cvbsformat >= 0) {
		rk3036_tve->mode =
			(struct fb_videomode *)&rk3036_cvbs_mode[cvbsformat];
		rk3036_tve->enable = 1;
		tve_switch_fb(rk3036_tve->mode, 1);
	} else {
		rk3036_tve->mode = (struct fb_videomode *)&rk3036_cvbs_mode[1];
	}
	rk3036_tve->ddev =
		rk_display_device_register(&display_cvbs, &pdev->dev, NULL);
	rk_display_device_enable(rk3036_tve->ddev);

	fb_register_client(&tve_fb_notifier);
	cvbsformat = -1;
	dev_info(&pdev->dev, "%s tv encoder probe ok\n", match->compatible);
	return 0;
}

static void rk3036_tve_shutdown(struct platform_device *pdev)
{
}

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
