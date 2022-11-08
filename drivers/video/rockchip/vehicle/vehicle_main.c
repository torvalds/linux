// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/video/rockchip/video/vehicle_main.c
 *
 * Copyright (C) 2022 Rockchip Electronics Co.Ltd
 * Authors:
 *	Zhiqin Wei <wzq@rock-chips.com>
 *      <randy.wang@rock-chips.com>
 *	Jianwei Fan <jianwei.fan@rock-chips.com>
 *
 */

#define CAMMODULE_NAME    "vehicle_main"

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/fb.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/completion.h>
#include <linux/wakelock.h>
#include <linux/of_gpio.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/pm_runtime.h>
#include <linux/interrupt.h>
#include "vehicle_flinger.h"
#include "vehicle_cfg.h"
#include "vehicle_ad.h"
#include "vehicle_main.h"
#include "vehicle_cif.h"
#include "vehicle_gpio.h"
#include <linux/version.h>
#include "../../../media/platform/rockchip/cif/dev.h"
#include "../../../phy/rockchip/phy-rockchip-csi2-dphy-common.h"

#define DRIVER_VERSION		KERNEL_VERSION(0, 0x03, 0x00)

static bool flinger_inited;
static bool TEST_GPIO = true;
static bool dvr_apk_need_start;

enum {
	STATE_CLOSE = 0,
	STATE_OPEN,
};

struct vehicle {
	struct device	*dev;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_default;
	struct wake_lock wake_lock;
	struct gpio_detect gpio_data;
	struct vehicle_cif cif;
	struct vehicle_ad_dev ad;
	int mirror;
	wait_queue_head_t vehicle_wait;
	atomic_t vehicle_atomic;
	int state;
	bool android_is_ready;
	bool gpio_over;
};

static struct vehicle *g_vehicle;

static int vehicle_parse_dt(struct vehicle *vehicle_info)
{
	struct device	*dev = vehicle_info->dev;

	/*  1. pinctrl */
	vehicle_info->pinctrl = devm_pinctrl_get(dev);

	if (IS_ERR(vehicle_info->pinctrl)) {
		dev_err(dev, "pinctrl get failed\n");
		return PTR_ERR(vehicle_info->pinctrl);
	}

	vehicle_info->pins_default = pinctrl_lookup_state(vehicle_info->pinctrl,
			"default");

	if (IS_ERR(vehicle_info->pins_default))
		dev_err(dev, "get default pinstate failed\n");

	return 0;
}

void vehicle_ad_stat_change_notify(void)
{
	if (g_vehicle) {
		VEHICLE_INFO("ad state change! set atpmic to 1!\n");
		atomic_set(&g_vehicle->vehicle_atomic, 1);
	}
}

void vehicle_cif_stat_change_notify(void)
{
	if (g_vehicle) {
		VEHICLE_INFO("cif state change! set atpmic to 1!\n");
		atomic_set(&g_vehicle->vehicle_atomic, 1);
	}
}

void vehicle_gpio_stat_change_notify(void)
{
	if (g_vehicle && !g_vehicle->gpio_over) {
		VEHICLE_INFO("reverse gpio state change! set atpmic to 1!\n");
		atomic_set(&g_vehicle->vehicle_atomic, 1);
	}
}

void vehicle_cif_error_notify(int last_line)
{
	if (g_vehicle) {
		VEHICLE_INFO("cif error notify\n");
		vehicle_ad_check_cif_error(&g_vehicle->ad, last_line);
	}
}

static void vehicle_open(struct vehicle_cfg *v_cfg)
{
	VEHICLE_INFO("%s enter: android_is_ready ?= %d",
			__func__, g_vehicle->android_is_ready);
	vehicle_flinger_reverse_open(v_cfg, g_vehicle->android_is_ready);
	vehicle_cif_reverse_open(v_cfg);
}

static void vehicle_close(void)
{
	vehicle_cif_reverse_close();
	vehicle_flinger_reverse_close(g_vehicle->android_is_ready);
}

static void vehicle_open_close(void)
{
	vehicle_cif_reverse_close();
}

static int vehicle_state_change(struct vehicle *v)
{
	struct vehicle_cfg *v_cfg;
	struct gpio_detect *gpiod = &v->gpio_data;
	bool gpio_reverse_on;
	int ret = 0;

	/*  1. get ad sensor cfg */
	v_cfg = vehicle_ad_get_vehicle_cfg();

	if (!v_cfg) {
		VEHICLE_DGERR("v_cfg is NULL, if for test continue.\n");
		return -ENODEV;
	}

	if (!flinger_inited) {
		do {
			/*  2. flinger */
			VEHICLE_DG("%s: flinger init start\r\n", __func__);
			ret = vehicle_flinger_init(v->dev, v_cfg);
			if (ret < 0) {
				VEHICLE_DG("rk_vehicle_system_main: flinger init failed\r\n");
				msleep(20);
			}
		} while (ret);
	}
	VEHICLE_DG("%s: flinger init success\r\n", __func__);
	flinger_inited = true;

	gpio_reverse_on = vehicle_gpio_reverse_check(gpiod);
	gpio_reverse_on = TEST_GPIO & gpio_reverse_on;
	VEHICLE_DG(
	"%s, gpio = reverse %s, width = %d, sensor_ready = %d, state=%d dvr_apk_need_start = %d\n",
	__func__, gpio_reverse_on ? "on" : "over",
	v_cfg->width, v_cfg->ad_ready, v->state, dvr_apk_need_start);
	if (v_cfg->mbus_flags & V4L2_MBUS_CSI2_CONTINUOUS_CLOCK) {
		switch (v->state) {
		case STATE_CLOSE:
			if (dvr_apk_need_start) {
				vehicle_open(v_cfg);
				msleep(20);
				vehicle_ad_stream(&v->ad, 0);
				vehicle_ad_channel_set(&g_vehicle->ad, 0);
				vehicle_ad_stream(&v->ad, 1);
				v->state = STATE_OPEN;
			}
			if (gpio_reverse_on) {
				vehicle_open(v_cfg);
				msleep(20);
				vehicle_ad_stream(&v->ad, 0);
				vehicle_ad_channel_set(&g_vehicle->ad, 0);
				vehicle_ad_stream(&v->ad, 1);
				v->state = STATE_OPEN;
			}
			break;
		case STATE_OPEN:
			/*  reverse exit || video loss */
			if (!dvr_apk_need_start && (!gpio_reverse_on || !v_cfg->ad_ready)) {
				vehicle_close();
				vehicle_ad_stream(&v->ad, 0);
				v->state = STATE_CLOSE;
			} else if (gpio_reverse_on) {  //  reverse on & video format change
				vehicle_open_close();
				vehicle_open(v_cfg);
				msleep(100);
				vehicle_ad_stream(&v->ad, 0);
				vehicle_ad_channel_set(&g_vehicle->ad, 0);
				vehicle_ad_stream(&v->ad, 1);
			} else if (!gpio_reverse_on && dvr_apk_need_start) {
				vehicle_close();
				vehicle_open(v_cfg);
				msleep(20);
				vehicle_ad_stream(&v->ad, 0);
				vehicle_ad_channel_set(&g_vehicle->ad, 0);
				vehicle_ad_stream(&v->ad, 1);
			}
			break;
		}
	} else if (v_cfg->mbus_flags & V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK) {
		switch (v->state) {
		case STATE_CLOSE:
			if (dvr_apk_need_start) {
				vehicle_ad_stream(&v->ad, 0);
				vehicle_ad_channel_set(&g_vehicle->ad, 0);
				vehicle_ad_stream(&v->ad, 1);
				msleep(20);
				vehicle_open(v_cfg);
				v->state = STATE_OPEN;
			}
			if (gpio_reverse_on) {
				vehicle_ad_stream(&v->ad, 0);
				vehicle_ad_channel_set(&g_vehicle->ad, 0);
				vehicle_ad_stream(&v->ad, 1);
				msleep(20);
				vehicle_open(v_cfg);
				v->state = STATE_OPEN;
			}
			break;
		case STATE_OPEN:
			/*  reverse exit || video loss */
			if (!dvr_apk_need_start && (!gpio_reverse_on || !v_cfg->ad_ready)) {
				vehicle_close();
				vehicle_ad_stream(&v->ad, 0);
				v->state = STATE_CLOSE;
			} else if (gpio_reverse_on) {  //  reverse on & video format change
				vehicle_open_close();
				vehicle_ad_stream(&v->ad, 0);
				vehicle_ad_channel_set(&g_vehicle->ad, 0);
				vehicle_ad_stream(&v->ad, 1);
				msleep(100);
				vehicle_open(v_cfg);
			} else if (!gpio_reverse_on && dvr_apk_need_start) {
				vehicle_close();
				vehicle_ad_stream(&v->ad, 0);
				vehicle_ad_channel_set(&g_vehicle->ad, 0);
				vehicle_ad_stream(&v->ad, 1);
				msleep(20);
				vehicle_open(v_cfg);
			}
			break;
		}
	} else {
		switch (v->state) {
		case STATE_CLOSE:
			if (dvr_apk_need_start) {
				vehicle_ad_stream(&v->ad, 0);
				vehicle_ad_channel_set(&g_vehicle->ad, 0);
				vehicle_ad_stream(&v->ad, 1);
				msleep(20);
				vehicle_open(v_cfg);
				v->state = STATE_OPEN;
			}
			if (gpio_reverse_on) {
				vehicle_ad_stream(&v->ad, 0);
				vehicle_ad_channel_set(&g_vehicle->ad, 0);
				vehicle_ad_stream(&v->ad, 1);
				msleep(20);
				vehicle_open(v_cfg);
				v->state = STATE_OPEN;
			}
			break;
		case STATE_OPEN:
			/*  reverse exit || video loss */
			if (!dvr_apk_need_start && (!gpio_reverse_on || !v_cfg->ad_ready)) {
				vehicle_close();
				vehicle_ad_stream(&v->ad, 0);
				v->state = STATE_CLOSE;
			} else if (gpio_reverse_on) {  //  reverse on & video format change
				vehicle_open_close();
				vehicle_ad_stream(&v->ad, 0);
				vehicle_ad_channel_set(&g_vehicle->ad, 0);
				vehicle_ad_stream(&v->ad, 1);
				msleep(100);
				vehicle_open(v_cfg);
			} else if (!gpio_reverse_on && dvr_apk_need_start) {
				vehicle_close();
				vehicle_ad_stream(&v->ad, 0);
				vehicle_ad_channel_set(&g_vehicle->ad, 0);
				vehicle_ad_stream(&v->ad, 1);
				msleep(20);
				vehicle_open(v_cfg);
			}
			break;
		}
	}

	return 0;
}

static int vehicle_probe(struct platform_device *pdev)
{
	struct vehicle *vehicle_info;

	dev_info(&pdev->dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	vehicle_info = devm_kzalloc(&pdev->dev,
				    sizeof(struct vehicle), GFP_KERNEL);
	if (!vehicle_info)
		return -ENOMEM;

	vehicle_info->dev = &pdev->dev;
	vehicle_info->gpio_data.dev = &pdev->dev;
	vehicle_info->cif.dev = &pdev->dev;
	vehicle_info->ad.dev = &pdev->dev;

	dev_set_name(vehicle_info->dev, "vehicle_main");
	if (!pdev->dev.of_node)
		return -EINVAL;

	vehicle_parse_dt(vehicle_info);

	if (vehicle_parse_sensor(&vehicle_info->ad) < 0) {
		VEHICLE_DGERR("parse sensor failed!\n");
		return -EINVAL;
	}

	wake_lock_init(&vehicle_info->wake_lock, WAKE_LOCK_SUSPEND, "vehicle");

	dev_info(vehicle_info->dev, "vehicle driver probe success\n");

	init_waitqueue_head(&vehicle_info->vehicle_wait);
	atomic_set(&vehicle_info->vehicle_atomic, 0);
	vehicle_info->state = STATE_CLOSE;
	vehicle_info->android_is_ready = false;
	vehicle_info->gpio_over = false;

	g_vehicle = vehicle_info;

	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id vehicle_of_match[] = {
	{ .compatible = "rockchip,vehicle", },
	{},
};
#endif

static struct platform_driver vehicle_driver = {
	.driver     = {
		.name   = "vehicle",
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(vehicle_of_match),
	},
	.probe      = vehicle_probe,
};

void vehicle_android_is_ready_notify(void)
{
	if (g_vehicle)
		g_vehicle->android_is_ready = true;
	TEST_GPIO = !TEST_GPIO;
	atomic_set(&g_vehicle->vehicle_atomic, 1);
}

void vehicle_apk_state_change(char data[22])
{
	if (memcmp(data, "11", 2) == 0)
		dvr_apk_need_start = true;
	else if (memcmp(data, "10", 2) == 0)
		dvr_apk_need_start = false;

	if (g_vehicle)
		atomic_set(&g_vehicle->vehicle_atomic, 1);
}

static void vehicle_exit_complete_notify(struct vehicle *v)
{
	char *status = NULL;
	char *envp[2];

	if (!v)
		return;
	status = kasprintf(GFP_KERNEL, "vehicle_exit=done");
	envp[0] = status;
	envp[1] = NULL;
	wake_lock_timeout(&v->wake_lock, 5 * HZ);
	kobject_uevent_env(&v->dev->kobj, KOBJ_CHANGE, envp);

	kfree(status);
}

static int rk_vehicle_system_main(void *arg)
{
	int ret = -1;
	struct vehicle *v = g_vehicle;
	int loop_times = 0;

	if (!g_vehicle) {
		VEHICLE_DGERR("vehicle probe failed, g_vehicle is NULL.\n");
		goto VEHICLE_EXIT;
	}

	/*  0. gpio init and check state */
	ret = vehicle_gpio_init(&v->gpio_data, v->ad.ad_name);
	if (ret < 0) {
		VEHICLE_DGERR("%s: gpio init failed\r\n", __func__);
		goto VEHICLE_GPIO_DEINIT;
	}
	VEHICLE_DG("vehicle_gpio_init ok!\n");

	/*  1.ad */
	VEHICLE_DG("%s: vehicle_ad_init start\r\n", __func__);
	/* config mclk first */
	ret = vehicle_cif_init_mclk(&v->cif);
	ret |= vehicle_ad_init(&v->ad);
	if (ret < 0) {
		VEHICLE_DGERR("%s: ad init failed\r\n", __func__);
		goto VEHICLE_AD_DEINIT;
	}
	VEHICLE_DG("vehicle_ad_init ok!\r\n");

	/*  3. cif init */
	ret = vehicle_cif_init(&v->cif);
	if (ret < 0) {
		VEHICLE_DGERR("%s: cif init failed\r\n", __func__);
		goto VEHICLE_CIF_DEINIT;
	}
	VEHICLE_DG("%s: vehicle_cif_init ok!\r\n", __func__);
	pm_runtime_enable(v->dev);
	pm_runtime_get_sync(v->dev);

	//while (STATE_OPEN == v->state || !v->vehicle_need_exit) {
	while (v->state == STATE_OPEN || !v->android_is_ready) {
		if (v->android_is_ready && !v->state)
			v->gpio_over = true;
		wait_event_timeout(v->vehicle_wait,
				   atomic_read(&v->vehicle_atomic),
				   msecs_to_jiffies(100));
		if (atomic_read(&v->vehicle_atomic)) {
			atomic_set(&v->vehicle_atomic, 0);
			vehicle_state_change(v);
		}
		VEHICLE_DG("loop time(%d) \r\n", loop_times);
		loop_times++;
	}

VEHICLE_CIF_DEINIT:
	vehicle_cif_deinit(&v->cif);

VEHICLE_AD_DEINIT:
	vehicle_ad_deinit();

VEHICLE_GPIO_DEINIT:
	vehicle_gpio_deinit(&v->gpio_data);

	/*Init normal drivers*/
VEHICLE_EXIT:
	if (flinger_inited)
		vehicle_flinger_deinit();
	// if (v && v->pinctrl)
	//	pinctrl_put(v->pinctrl);
	vehicle_to_v4l2_drv_init();
	msleep(500);
	rockchip_csi2_dphy_hw_init();
	rockchip_csi2_dphy_init();
	rk_cif_plat_drv_init();
	// rkcif_csi2_plat_drv_init();
	rkcif_clr_unready_dev();
#ifdef CONFIG_GPIO_DET
	//gpio_det_init();
#endif
	// msleep(1000);
	vehicle_exit_complete_notify(v);
	return 0;
}

static int __init vehicle_system_start(void)
{
	platform_driver_register(&vehicle_driver);
	kthread_run(rk_vehicle_system_main, NULL, "vehicle main");

	return 0;
}

subsys_initcall_sync(vehicle_system_start);
