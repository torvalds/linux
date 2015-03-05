/*
 * rk1000_tv.c
 *
 * Driver for rockchip rk1000 tv control
 *  Copyright (C) 2009
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *
 */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/fb.h>
#include <linux/rk_fb.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <dt-bindings/rkfb/rk_fb.h>
#endif
#include "rk1000_tve.h"

struct rk1000_tve rk1000_tve;


int rk1000_tv_write_block(u8 reg, u8 *buf, u8 len)
{
	int i, ret;

	for (i = 0; i < len; i++) {
		ret = rk1000_i2c_send(I2C_ADDR_TVE, reg + i, buf[i]);
		if (ret)
			break;
	}
	return ret;
}

int rk1000_control_write_block(u8 reg, u8 *buf, u8 len)
{
	int i;
	int ret;

	for (i = 0; i < len; i++) {
		ret = rk1000_i2c_send(I2C_ADDR_CTRL, reg + i, buf[i]);
		if (ret)
			break;
	}
	return ret;
}

int rk1000_switch_fb(const struct fb_videomode *modedb, int tv_mode)
{
	struct rk_screen *screen;

	if (modedb == NULL)
		return -1;
	screen =  kzalloc(sizeof(*screen), GFP_KERNEL);
	if (screen == NULL)
		return -1;
	memset(screen, 0, sizeof(*screen));
	/* screen type & face */
	screen->type = SCREEN_RGB;
	screen->face = OUT_P888;
	screen->mode = *modedb;
	screen->mode.vmode = 0;
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
	/* Swap rule */
	screen->swap_rb = 0;
	screen->swap_rg = 0;
	screen->swap_gb = 0;
	screen->swap_delta = 0;
	screen->swap_dumy = 0;
	screen->overscan.left = 95;
	screen->overscan.top = 95;
	screen->overscan.right = 95;
	screen->overscan.bottom = 95;
	/* Operation function*/
	screen->init = NULL;
	screen->standby = NULL;
	switch (tv_mode) {
	#ifdef CONFIG_RK1000_TVOUT_CVBS
	case TVOUT_CVBS_NTSC:
		screen->init = rk1000_tv_ntsc_init;
	break;
	case TVOUT_CVBS_PAL:
		screen->init = rk1000_tv_pal_init;
	break;
	#endif
	#ifdef CONFIG_RK1000_TVOUT_YPBPR
	case TVOUT_YPBPR_720X480P_60:
		screen->init = rk1000_tv_ypbpr480_init;
	break;
	case TVOUT_YPBPR_720X576P_50:
		screen->init = rk1000_tv_ypbpr576_init;
	break;
	case TVOUT_YPBPR_1280X720P_50:
		screen->init = rk1000_tv_ypbpr720_50_init;
	break;
	case TVOUT_YPBPR_1280X720P_60:
		screen->init = rk1000_tv_ypbpr720_60_init;
	break;
	#endif
	default:
		kfree(screen);
		return -1;
	}
	rk_fb_switch_screen(screen, 1 , rk1000_tve.video_source);
	rk1000_tve.mode = tv_mode;
	kfree(screen);
	if (gpio_is_valid(rk1000_tve.io_switch.gpio)) {
		if (tv_mode < TVOUT_YPBPR_720X480P_60)
			gpio_direction_output(rk1000_tve.io_switch.gpio,
					      !(rk1000_tve.io_switch.active));
		else
			gpio_direction_output(rk1000_tve.io_switch.gpio,
					      rk1000_tve.io_switch.active);
	}
	return 0;
}

int rk1000_tv_standby(int type)
{
	unsigned char val;
	int ret;
	int ypbpr;
	int cvbs;
	struct rk_screen screen;

	ypbpr = 0;
	cvbs = 0;
	if (rk1000_tve.ypbpr)
		ypbpr = rk1000_tve.ypbpr->enable;
	if (rk1000_tve.cvbs)
		cvbs = rk1000_tve.cvbs->enable;
	if (cvbs || ypbpr)
		return 0;
	val = 0x00;
	ret = rk1000_control_write_block(0x03, &val, 1);
	if (ret < 0) {
		pr_err("rk1000_control_write_block err!\n");
		return ret;
	}
	val = 0x07;
	ret = rk1000_tv_write_block(0x03, &val, 1);
	if (ret < 0) {
		pr_err("rk1000_tv_write_block err!\n");
		return ret;
	}
	screen.type = SCREEN_RGB;
	rk_fb_switch_screen(&screen, 0 , rk1000_tve.video_source);
	pr_err("rk1000 tve standby\n");
	return 0;
}

static int rk1000_tve_initial(void)
{
	struct rk_screen screen;

	/* RK1000 tvencoder i2c reg need dclk, so we open lcdc.*/
	memset(&screen, 0, sizeof(struct rk_screen));
	/* screen type & face */
	screen.type = SCREEN_RGB;
	screen.face = OUT_P888;
	/* Screen size */
	screen.mode.xres = 720;
	screen.mode.yres = 480;
	/* Timing */
	screen.mode.pixclock = 27000000;
	screen.mode.refresh = 60;
	screen.mode.left_margin = 116;
	screen.mode.right_margin = 16;
	screen.mode.hsync_len = 6;
	screen.mode.upper_margin = 25;
	screen.mode.lower_margin = 14;
	screen.mode.vsync_len = 6;
	rk_fb_switch_screen(&screen, 2 , rk1000_tve.video_source);
	/* Power down RK1000 output DAC. */
	return rk1000_i2c_send(I2C_ADDR_TVE, 0x03, 0x07);
}


static void rk1000_early_suspend(void *h)
{
	pr_info("rk1000_early_suspend\n");
	if (rk1000_tve.ypbpr) {
		rk1000_tve.ypbpr->ddev->ops->setenable(rk1000_tve.ypbpr->ddev,
						       0);
		rk1000_tve.ypbpr->suspend = 1;
	}
	if (rk1000_tve.cvbs) {
		rk1000_tve.cvbs->ddev->ops->setenable(rk1000_tve.cvbs->ddev,
						      0);
		rk1000_tve.cvbs->suspend = 1;
	}
}


static void rk1000_early_resume(void *h)
{
	pr_info("rk1000 tve exit early resume\n");
	if (rk1000_tve.cvbs) {
		rk1000_tve.cvbs->suspend = 0;
		if (rk1000_tve.mode < TVOUT_YPBPR_720X480P_60)
			rk_display_device_enable((rk1000_tve.cvbs)->ddev);
	}
	if (rk1000_tve.ypbpr) {
		rk1000_tve.ypbpr->suspend = 0;
		if (rk1000_tve.mode > TVOUT_CVBS_PAL)
			rk_display_device_enable((rk1000_tve.ypbpr)->ddev);
	}
}


static int rk1000_fb_event_notify(struct notifier_block *self,
				  unsigned long action, void *data)
{
	struct fb_event *event;
	int blank_mode;

	event = data;
	blank_mode = *((int *)event->data);
	if (action == FB_EARLY_EVENT_BLANK) {
		switch (blank_mode) {
		case FB_BLANK_UNBLANK:
		break;
		default:
		rk1000_early_suspend(NULL);
		break;
		}
	} else if (action == FB_EVENT_BLANK) {
		switch (blank_mode) {
		case FB_BLANK_UNBLANK:
			rk1000_early_resume(NULL);
		break;
		default:
		break;
		}
	}
	return NOTIFY_OK;
}

static struct notifier_block rk1000_fb_notifier = {
	.notifier_call = rk1000_fb_event_notify,
};

static int rk1000_tve_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct device_node *tve_np;
	enum of_gpio_flags flags;
	int rc;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		rc = -ENODEV;
		goto failout;
	}

	memset(&rk1000_tve, 0, sizeof(struct rk1000_tve));
	rk1000_tve.client = client;
#ifdef CONFIG_OF
	tve_np = client->dev.of_node;
	rk1000_tve.io_switch.gpio = of_get_named_gpio_flags(tve_np,
							    "gpio-reset",
							    0, &flags);
	if (gpio_is_valid(rk1000_tve.io_switch.gpio)) {
		rc = gpio_request(rk1000_tve.io_switch.gpio,
				  "rk1000-tve-swicth-io");
		if (!rc) {
			rk1000_tve.io_switch.active = !(flags &
							OF_GPIO_ACTIVE_LOW);
			gpio_direction_output(rk1000_tve.io_switch.gpio,
					      !(rk1000_tve.io_switch.active));
		} else
			pr_err("gpio request rk1000-tve-swicth-io err: %d\n",
			       rk1000_tve.io_switch.gpio);
	}
	of_property_read_u32(tve_np, "rockchip,source", &(rc));
	rk1000_tve.video_source = rc;
	of_property_read_u32(tve_np, "rockchip,prop", &(rc));
	rk1000_tve.property = rc - 1;
	pr_err("video src is lcdc%d, prop is %d\n", rk1000_tve.video_source,
	       rk1000_tve.property);
#endif
	rk1000_tve.mode = RK1000_TVOUT_DEAULT;
	rc = rk1000_tve_initial();
	if (rc) {
		dev_err(&client->dev, "rk1000 tvencoder probe error %d\n", rc);
		return -EINVAL;
	}

#ifdef CONFIG_RK1000_TVOUT_YPBPR
	rk1000_register_display_ypbpr(&client->dev);
#endif

#ifdef CONFIG_RK1000_TVOUT_CVBS
	rk1000_register_display_cvbs(&client->dev);
#endif
	#ifdef CONFIG_HAS_EARLYSUSPEND
	rk1000_tve.early_suspend.suspend = rk1000_early_suspend;
	rk1000_tve.early_suspend.resume = rk1000_early_resume;
	rk1000_tve.early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB - 11;
	register_early_suspend(&(rk1000_tve.early_suspend));
	#endif
	fb_register_client(&rk1000_fb_notifier);
	pr_info("rk1000 tvencoder ver 2.0 probe ok\n");
	return 0;
failout:
	kfree(client);
	client = NULL;
	return rc;
}

static int rk1000_tve_remove(struct i2c_client *client)
{
	return 0;
}

static void rk1000_tve_shutdown(struct i2c_client *client)
{
	rk1000_i2c_send(I2C_ADDR_TVE, 0x03, 0x07);
}

static const struct i2c_device_id rk1000_tve_id[] = {
	{ "rk1000_tve", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rk1000_tve_id);

static struct i2c_driver rk1000_tve_driver = {
	.driver		= {
		.name	= "rk1000_tve",
	},
	.id_table = rk1000_tve_id,
	.probe = rk1000_tve_probe,
	.remove = rk1000_tve_remove,
	.shutdown = rk1000_tve_shutdown,
};

static int __init rk1000_tve_init(void)
{
	int ret;

	ret = i2c_add_driver(&rk1000_tve_driver);
	if (ret < 0)
		pr_err("i2c_add_driver err, ret = %d\n", ret);
	return ret;
}

static void __exit rk1000_tve_exit(void)
{
	i2c_del_driver(&rk1000_tve_driver);
}

late_initcall(rk1000_tve_init);
module_exit(rk1000_tve_exit);

/* Module information */
MODULE_DESCRIPTION("ROCKCHIP rk1000 TV Encoder ");
MODULE_LICENSE("GPL");
