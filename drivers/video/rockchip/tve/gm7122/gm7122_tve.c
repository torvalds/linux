/*
 * gm7122_tve.c
 *
 * Driver for rockchip gm7122 tv encoder control
 * Copyright (C) 2015
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
#include "gm7122_tve.h"
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/mfd/core.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/syscon.h>

static const struct fb_videomode gm7122_cvbs_mode[] = {
/*name		refresh		xres	yres	pixclock	h_bp	h_fp	v_bp	v_fp	h_pw	v_pw	polariry	PorI				flag */
{"NTSC",	60,		720,	480,	27000000,	57,	19,	15,	4,	62,	3,	0,		FB_VMODE_INTERLACED,		0},
{"PAL",		50,		720,	576,	27000000,	62,	14,	17,	2,	68,	5,	0,		FB_VMODE_INTERLACED,		0},
};

static struct gm7122_tve *gm7122_tve;

static int cvbsformat;

#define tve_writel(offset, v)	gm7122_i2c_send(offset, v)
/*#define tve_readl(offset, *v)	gm7122_i2c_recv(offset, v)*/

#ifdef DEBUG
#define TVEDBG(format, ...) \
		dev_info(gm7122_tve->dev,\
		 "GM7122 TVE: " format "\n", ## __VA_ARGS__)
#else
#define TVEDBG(format, ...)
#endif

int gm7122_i2c_send(const u8 reg, const u8 value)
{
	char buf[2];
	int ret;

	buf[0] = reg;
	buf[1] = value;
	ret = i2c_master_send(gm7122_tve->client, buf, 2);
	if (ret != 2) {
		TVEDBG("gm7122 control i2c write err,ret =%d\n", ret);
		return -1;
	}
	return 0;
}

int gm7122_i2c_recv(const u8 reg, char *value)
{
	int ret;

	ret = i2c_master_send(gm7122_tve->client, &reg, 1);
	i2c_master_recv(gm7122_tve->client, value, 1);
	pr_info("%s reg = 0x%x , value = 0x%c\n", __func__, reg, *value);
	return (ret == 2) ? 0 : -1;
}


static void tve_set_mode(int mode)
{
	TVEDBG("%s mode %d\n", __func__, mode);
	if (cvbsformat >= 0)
		return;

	if (mode == TVOUT_CVBS_NTSC) {
		tve_writel(BURST_START, V_D0_BS0(1) | V_D0_BS5(1));
		tve_writel(BURST_END, V_D0_BE0(1) | V_D0_BE2(1) |
			V_D0_BE3(1) | V_D0_BE4(1));
		tve_writel(INPUT_PORT_CTL, V_SYMP(1) | V_UV2C(1) | V_Y2C(1));
		tve_writel(COLOR_DIFF_CTL, 0x00);
		tve_writel(U_GAIN_CTL, V_GAINU0(1) | V_GAINU2(1) |
			V_GAINU3(3) | V_GAINU5(1) | V_GAINU6(1));
		tve_writel(V_GAIN_CTL, V_GAINV0(1) | V_GAINV1(1) |
			V_GAINV2(1) | V_GAINV3(1) | V_GAINV4(1) |
			V_GAINV7(1));
		tve_writel(UMSB_BLACK_GAIN,	V_BLACK1(1) | V_BLACK2(1) |
			V_BLACK3(1));
		tve_writel(VMSB_BLNNL_GAIN, V_BLNNL2(1) | V_BLNNL3(1) |
			V_BLNNL4(1));
		tve_writel(STANDARD_CTL, V_PAL(0) | V_BIT0(1));
		tve_writel(RTCEN_BURST_CTL, V_BSTA0(1) | V_BSTA1(1)|
			V_BSTA3(1) | V_BSTA4(1) | V_BSTA5(1));
		tve_writel(SUBCARRIER0, V_FSC00(1) | V_FSC01(1)|
			V_FSC02(1) | V_FSC03(1) | V_FSC04(1));
		tve_writel(SUBCARRIER1, V_FSC10(1) | V_FSC11(1)|
			V_FSC12(1) | V_FSC13(1) | V_FSC14(1));
		tve_writel(SUBCARRIER2, V_FSC20(1) | V_FSC21(1) |
			V_FSC22(1) | V_FSC23(1));
		tve_writel(SUBCARRIER3, V_FSC29(1) | V_FSC24(1));
		tve_writel(RCV_PORT_CTL, 0x00);
		tve_writel(TRIG0_CTL, V_HTRIG0(1) | V_HTRIG2(1) | V_HTRIG4(1) |
				       V_HTRIG5(1) | V_HTRIG6(1) | V_HTRIG7(1));
		tve_writel(TRIG1_CTL, V_VTRIG0(1) | V_VTRIG4(1) | V_HTRIG8(1) |
				      V_HTRIG10(1));
	} else if (mode == TVOUT_CVBS_PAL) {
		tve_writel(BURST_START, V_D0_BS0(1) | V_D0_BS5(1));
		tve_writel(BURST_END, V_D0_BE0(1) | V_D0_BE2(1) |
			V_D0_BE3(1) | V_D0_BE4(1));
		tve_writel(INPUT_PORT_CTL, V_SYMP(1) | V_UV2C(1) | V_Y2C(1));
		/*tve_writel(INPUT_PORT_CTL, 0x93);*//*color bar for debug*/
		tve_writel(COLOR_DIFF_CTL, V_CHPS0(1));
		tve_writel(U_GAIN_CTL, V_GAINU1(1) | V_GAINU3(1) |
			V_GAINU5(1) | V_GAINU6(1));
		tve_writel(V_GAIN_CTL, V_GAINV0(1) | V_GAINV1(1) |
			V_GAINV2(1) | V_GAINV3(1) | V_GAINV4(1) |
			V_GAINV7(1));
		tve_writel(UMSB_BLACK_GAIN,	V_BLACK1(1) | V_BLACK4(1));
		tve_writel(VMSB_BLNNL_GAIN, V_BLNNL0(1) | V_BLNNL1(1) |
			V_BLNNL2(1) | V_BLNNL3(1) | V_BLNNL4(1));
		tve_writel(STANDARD_CTL, V_PAL(1) | V_SCBW(1));
		tve_writel(RTCEN_BURST_CTL, V_BSTA0(1) | V_BSTA1(1)|
			V_BSTA3(1) | V_BSTA4(1) | V_BSTA5(1));
		tve_writel(SUBCARRIER0, V_FSC00(1) | V_FSC01(1)|
			V_FSC03(1) | V_FSC06(1) | V_FSC07(1));
		tve_writel(SUBCARRIER1, V_FSC15(1) | V_FSC11(1)|
			V_FSC09(1));
		tve_writel(SUBCARRIER2, V_FSC19(1) | V_FSC16(1));
		tve_writel(SUBCARRIER3, V_FSC29(1) | V_FSC27(1) | V_FSC25(1));
		tve_writel(RCV_PORT_CTL, 0x00);
		tve_writel(TRIG0_CTL, V_HTRIG0(1) | V_HTRIG2(1) | V_HTRIG4(1) |
				       V_HTRIG5(1) | V_HTRIG6(1) | V_HTRIG7(1));
		tve_writel(TRIG1_CTL, V_VTRIG0(1) | V_VTRIG4(1) | V_HTRIG8(1) |
				      V_HTRIG10(1));
	}
}

static int tve_switch_fb(const struct fb_videomode *modedb, int enable)
{
	struct rk_screen *screen = &gm7122_tve->screen;

	if (modedb == NULL)
		return -1;

	memset(screen, 0, sizeof(struct rk_screen));
	/* screen type & face */
	/*screen->type = SCREEN_TVOUT;*/
	screen->type = SCREEN_RGB;
	screen->face = OUT_CCIR656;
	screen->color_mode = COLOR_YCBCR;
	screen->mode = *modedb;
	/*screen->mode.vmode = 0;*/

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
	screen->pin_dclk = 1;
	/*screen->pixelrepeat = 1;*/

	/* Swap rule */
	screen->swap_rb = 0;
	screen->swap_rg = 0;
	screen->swap_gb = 0;
	screen->swap_delta = 0;
	screen->swap_dumy = 0;
	screen->overscan.left = 100;
	screen->overscan.top = 100;
	screen->overscan.right = 100;
	screen->overscan.bottom = 100;
	/* Operation function*/
	screen->init = NULL;
	screen->standby = NULL;
	rk_fb_switch_screen(screen, enable, gm7122_tve->lcdcid);
	if (enable) {
		if (screen->mode.yres == 480)
			tve_set_mode(TVOUT_CVBS_NTSC);
		else
			tve_set_mode(TVOUT_CVBS_PAL);
	}
	return 0;
}

static int cvbs_set_enable(struct rk_display_device *device, int enable)
{
	if (gm7122_tve->enable != enable) {
		gm7122_tve->enable = enable;
		if (gm7122_tve->suspend)
			return 0;

		if (enable == 0) {
			/*tve_enable(false);*/
			cvbsformat = -1;
			tve_switch_fb(gm7122_tve->mode, 0);
		} else if (enable == 1) {
			tve_switch_fb(gm7122_tve->mode, 1);
			/*tve_enable(true);*/
		}
	}
	return 0;
}

static int cvbs_get_enable(struct rk_display_device *device)
{
	TVEDBG("%s enable %d\n", __func__, gm7122_tve->enable);
	return gm7122_tve->enable;
}

static int cvbs_get_status(struct rk_display_device *device)
{
	return 1;
}

static int
cvbs_get_modelist(struct rk_display_device *device, struct list_head **modelist)
{
	*modelist = &(gm7122_tve->modelist);
	return 0;
}

static int
cvbs_set_mode(struct rk_display_device *device, struct fb_videomode *mode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(gm7122_cvbs_mode); i++) {
		if (fb_mode_is_equal(&gm7122_cvbs_mode[i], mode)) {
			if (gm7122_tve->mode != &gm7122_cvbs_mode[i]) {
				gm7122_tve->mode =
				(struct fb_videomode *)&gm7122_cvbs_mode[i];
				if (gm7122_tve->enable &&
				    !gm7122_tve->suspend) {
					/*tve_enable(false);*/
					if (!fb_mode_is_equal(gm7122_tve->mode,
							      mode)) {
						gpio_set_value(
						gm7122_tve->io_sleep.gpio,
						gm7122_tve->io_sleep.active);
						msleep(20);
						gpio_set_value(
						gm7122_tve->io_sleep.gpio,
						!(gm7122_tve->io_sleep.active));
					}
					tve_switch_fb(gm7122_tve->mode, 1);
				}
					/*tve_enable(true);*/
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
	*mode = *(gm7122_tve->mode);
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
			if (!gm7122_tve->suspend) {
				gm7122_tve->suspend = 1;
				if (gm7122_tve->enable) {
					tve_switch_fb(gm7122_tve->mode, 0);
					/*tve_enable(false);*/
				}
			}
			break;
		}
	} else if (action == FB_EVENT_BLANK) {
		switch (*((int *)event->data)) {
		case FB_BLANK_UNBLANK:
			TVEDBG("resume tve\n");
			if (gm7122_tve->suspend) {
				gm7122_tve->suspend = 0;
				if (gm7122_tve->enable) {
					tve_switch_fb(gm7122_tve->mode, 1);
					/*tve_enable(true);*/
				}
			}
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
	device->property = 0;/*just for test*/
	device->priv_data = devdata;
	device->ops = &cvbs_display_ops;
	return 1;
}

static struct rk_display_driver display_cvbs = {
	.probe = display_cvbs_probe,
};

#if defined(CONFIG_OF)
static const struct i2c_device_id gm7122_tve_dt_ids[] = {
	{ "gm7122_tve", 0 },
	{}
};
#endif

static int __init bootloader_tve_setup(char *str)
{
	if (str) {
		pr_info("cvbs init tve.format is %s\n", str);
		if (kstrtoint(str, 0, &cvbsformat) < 0)
			cvbsformat = -1;
	}
	return 0;
}

early_param("tve.format", bootloader_tve_setup);


static int gm7122_tve_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	int i;
	struct device_node *gm7122_np;
	enum of_gpio_flags flags;
	int ret;

	gm7122_tve = kmalloc(sizeof(*gm7122_tve), GFP_KERNEL);
	if (!gm7122_tve) {
		dev_err(&client->dev, "gm7122 tv encoder device kmalloc fail!\n");
		return -ENOMEM;
	}
	memset(gm7122_tve, 0, sizeof(*gm7122_tve));
	gm7122_tve->client = client;
	gm7122_tve->dev = &client->dev;
	gm7122_np = gm7122_tve->dev->of_node;
	of_property_read_u32(gm7122_np, "rockchip,source", &(ret));
	gm7122_tve->lcdcid = ret;
	of_property_read_u32(gm7122_np, "rockchip,prop", &(ret));
	gm7122_tve->property = ret;
	/********Get reset pin***********/
	gm7122_tve->io_reset.gpio = of_get_named_gpio_flags(gm7122_np, "gpio-reset",
							    0, &flags);
	if (!gpio_is_valid(gm7122_tve->io_reset.gpio)) {
		TVEDBG("invalid gm7122_tve->io_reset.gpio: %d\n",
		       gm7122_tve->io_reset.gpio);
		goto failout;
		}
	ret = gpio_request(gm7122_tve->io_reset.gpio, "gm7122-reset-io");
	if (ret != 0) {
		TVEDBG("gpio_request gm7122_tve->io_reset.gpio invalid: %d\n",
		       gm7122_tve->io_reset.gpio);
		goto failout;
		}
	gm7122_tve->io_reset.active = (flags & OF_GPIO_ACTIVE_LOW);
	gpio_direction_output(gm7122_tve->io_reset.gpio,
			      !(gm7122_tve->io_reset.active));
	gpio_set_value(gm7122_tve->io_reset.gpio,
		       !(gm7122_tve->io_reset.active));
	/********Reset pin end***********/
	/********Get sleep pin***********/
	gm7122_tve->io_sleep.gpio = of_get_named_gpio_flags(gm7122_np, "gpio-sleep", 0, &flags);
	if (!gpio_is_valid(gm7122_tve->io_sleep.gpio)) {
		TVEDBG("invalid gm7122_tve->io_reset.gpio: %d\n",
		       gm7122_tve->io_sleep.gpio);
		}
	ret = gpio_request(gm7122_tve->io_sleep.gpio, "gm7122-sleep-io");
	if (ret != 0) {
		TVEDBG("gpio_request gm7122_tve->io_reset.gpio invalid: %d\n",
		       gm7122_tve->io_sleep.gpio);
		goto failout;
		}
	gm7122_tve->io_sleep.active = !(flags & OF_GPIO_ACTIVE_LOW);
	gpio_direction_output(gm7122_tve->io_sleep.gpio,
			      !(gm7122_tve->io_sleep.active));
	gpio_set_value(gm7122_tve->io_sleep.gpio,
		       !(gm7122_tve->io_sleep.active));
	/********Sleep pin end***********/
	INIT_LIST_HEAD(&(gm7122_tve->modelist));
	for (i = 0; i < ARRAY_SIZE(gm7122_cvbs_mode); i++)
		fb_add_videomode(&gm7122_cvbs_mode[i], &(gm7122_tve->modelist));
	if (cvbsformat >= 0) {
		gm7122_tve->mode =
			(struct fb_videomode *)&gm7122_cvbs_mode[cvbsformat];
		/*gm7122_tve->enable = 1;
		tve_switch_fb(gm7122_tve->mode, 1);*/
	} else {
		gm7122_tve->mode = (struct fb_videomode *)&gm7122_cvbs_mode[1];
	}
	gm7122_tve->ddev =
		rk_display_device_register(&display_cvbs,
					   gm7122_tve->dev, NULL);
	rk_display_device_enable(gm7122_tve->ddev);
	fb_register_client(&tve_fb_notifier);
	cvbsformat = -1;
	pr_info("%s tv encoder probe ok!\n", __func__);
	return 0;

failout:
	kfree(gm7122_tve);
	return -ENODEV;
}

/*static void gm7122_tve_shutdown(struct platform_device *pdev)
{
}*/
static int gm7122_tve_remove(struct i2c_client *client)
{
	return 0;
}

MODULE_DEVICE_TABLE(i2c, gm7122_tve_dt_ids);

static struct i2c_driver gm7122_tve_driver = {
	.probe = gm7122_tve_probe,
	.remove = gm7122_tve_remove,
	.driver = {
		.name = "gm7122_tve",
		.owner = THIS_MODULE,
	},
	/*.shutdown = gm7122_tve_shutdown,*/
	.id_table = gm7122_tve_dt_ids,
};

static int __init gm7122_tve_init(void)
{
	return  i2c_add_driver(&gm7122_tve_driver);
}

static void __exit gm7122_tve_exit(void)
{
	i2c_del_driver(&gm7122_tve_driver);
}

module_init(gm7122_tve_init);
module_exit(gm7122_tve_exit);


MODULE_DESCRIPTION("ROCKCHIP GM7122 TV Encoder ");
MODULE_AUTHOR("Rock-chips, <www.rock-chips.com>");
MODULE_LICENSE("GPL");
