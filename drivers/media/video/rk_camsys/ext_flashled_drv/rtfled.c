/* drivers/leds/rtfled.c
 * Richtek Flash LED Universal Architecture
 *
 * Copyright (C) 2013 Richtek Technology Corp.
 * Author: Patrick Chang <patrick_chang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include "rtfled.h"
#include <linux/init.h>
#include <linux/version.h>

#define RTFLED_INFO(format, args...) \
	pr_info("%s:%s() line-%d: " format, \
	  ALIAS_NAME, __func__, __LINE__, ## args)
#define RTFLED_WARN(format, args...) \
	pr_warn("%s:%s() line-%d: " format, \
	  ALIAS_NAME, __func__, __LINE__, ## args)
#define RTFLED_ERR(format, args...) \
	pr_err("%s:%s() line-%d: " format, \
	  ALIAS_NAME, __func__, __LINE__, ## args)

#define RT_FLED_DEVICE  "rt-flash-led"
#define ALIAS_NAME RT_FLED_DEVICE

rt_fled_info_t *rt_fled_get_info_by_name(char *name)
{
	struct flashlight_device *flashlight_dev;

	flashlight_dev = find_flashlight_by_name(name ? name : RT_FLED_DEVICE);
	if (flashlight_dev == NULL)
		return (rt_fled_info_t *) NULL;
	return flashlight_get_data(flashlight_dev);
}
EXPORT_SYMBOL(rt_fled_get_info_by_name);

static int rtfled_set_torch_brightness(struct flashlight_device *flashlight_dev,
				       int brightness_sel)
{
	rt_fled_info_t *info = flashlight_get_data(flashlight_dev);

	return info->hal->fled_set_torch_current_sel(info, brightness_sel);
}

static int rtfled_set_strobe_brightness(struct flashlight_device
					*flashlight_dev, int brightness_sel)
{
	rt_fled_info_t *info = flashlight_get_data(flashlight_dev);

	return info->hal->fled_set_strobe_current_sel(info, brightness_sel);
}

static int rtfled_set_strobe_timeout(struct flashlight_device *flashlight_dev,
				     int timeout)
{
	rt_fled_info_t *info = flashlight_get_data(flashlight_dev);

	int sel;

	return info->hal->fled_set_strobe_timeout(info, timeout, timeout, &sel);
}

static int rtfled_list_strobe_timeout(struct flashlight_device *flashlight_dev,
				      int selector)
{
	rt_fled_info_t *info = flashlight_get_data(flashlight_dev);

	return info->hal->fled_strobe_timeout_list(info, selector);
}

static int rtfled_set_mode(struct flashlight_device *flashlight_dev, int mode)
{
	rt_fled_info_t *info = flashlight_get_data(flashlight_dev);

	return info->hal->fled_set_mode(info, mode);
}

static int rtfled_strobe(struct flashlight_device *flashlight_dev)
{
	rt_fled_info_t *info = flashlight_get_data(flashlight_dev);

	return info->hal->fled_strobe(info);
}

static int rtfled_set_color_temperature(struct flashlight_device
					*flashlight_dev, int color_temp)
{
	/* Doesn't support color temperature */
	return -EINVAL;
}

static int rtfled_list_color_temperature(struct flashlight_device
					 *flashlight_dev, int selector)
{
	/* Doesn't support color temperature */
	return -EINVAL;
}

static int rtfled_suspend(struct flashlight_device *flashlight_dev,
			  pm_message_t state)
{
	rt_fled_info_t *info = flashlight_get_data(flashlight_dev);

	if (info->hal->fled_suspend)
		return info->hal->fled_suspend(info, state);
	return 0;
}

static int rtfled_resume(struct flashlight_device *flashlight_dev)
{
	rt_fled_info_t *info = flashlight_get_data(flashlight_dev);

	if (info->hal->fled_resume)
		return info->hal->fled_resume(info);
	return 0;
}

static struct flashlight_ops rtfled_impl_ops = {
	.set_torch_brightness = rtfled_set_torch_brightness,
	.set_strobe_brightness = rtfled_set_strobe_brightness,
	.set_strobe_timeout = rtfled_set_strobe_timeout,
	.list_strobe_timeout = rtfled_list_strobe_timeout,
	.set_mode = rtfled_set_mode,
	.strobe = rtfled_strobe,
	.set_color_temperature = rtfled_set_color_temperature,
	.list_color_temperature = rtfled_list_color_temperature,
	.suspend = rtfled_suspend,
	.resume = rtfled_resume,
};

static void rfled_shutdown(struct platform_device *pdev)
{
	struct rt_fled_info *info = platform_get_drvdata(pdev);

	if (info->hal->fled_shutdown)
		info->hal->fled_shutdown(info);
}

static int rtled_impl_set_torch_current(struct rt_fled_info *info,
					int min_uA, int max_uA, int *selector)
{
	int sel = 0;
	int rc;

	for (sel = 0;; sel++) {
		rc = info->hal->fled_torch_current_list(info, sel);
		if (rc < 0)
			return rc;
		if (rc >= min_uA && rc <= max_uA) {
			*selector = sel;
			return info->hal->fled_set_torch_current_sel(info, sel);
		}
	}
	return -EINVAL;
}

static int rtled_impl_set_strobe_current(struct rt_fled_info *info,
					 int min_uA, int max_uA, int *selector)
{
	int sel = 0;
	int rc;

	for (sel = 0;; sel++) {
		rc = info->hal->fled_strobe_current_list(info, sel);
		if (rc < 0)
			return rc;
		if (rc >= min_uA && rc <= max_uA) {
			*selector = sel;
			return info->hal->fled_set_strobe_current_sel(info,
								      sel);
		}
	}
	return -EINVAL;
}

static int rtled_impl_set_timeout_level(struct rt_fled_info *info,
					int min_uA, int max_uA, int *selector)
{
	int sel = 0;
	int rc;

	for (sel = 0;; sel++) {
		rc = info->hal->fled_timeout_level_list(info, sel);
		if (rc < 0)
			return rc;
		if (rc >= min_uA && rc <= max_uA) {
			*selector = sel;
			return info->hal->fled_set_timeout_level_sel(info, sel);
		}
	}
	return -EINVAL;
}

static int rtled_impl_set_lv_protection(struct rt_fled_info *info,
					int min_mV, int max_mV, int *selector)
{
	int sel = 0;
	int rc;

	for (sel = 0;; sel++) {
		rc = info->hal->fled_lv_protection_list(info, sel);
		if (rc < 0)
			return rc;
		if (rc >= min_mV && rc <= max_mV) {
			*selector = sel;
			return info->hal->fled_set_lv_protection_sel(info, sel);
		}
	}
	return -EINVAL;
}

static int rtled_impl_set_strobe_timeout(struct rt_fled_info *info,
					 int min_ms, int max_ms, int *selector)
{
	int sel = 0;
	int rc;

	for (sel = 0;; sel++) {
		rc = info->hal->fled_strobe_timeout_list(info, sel);
		if (rc < 0)
			return rc;
		if (rc >= min_ms && rc <= max_ms) {
			*selector = sel;
			return info->hal->fled_set_strobe_timeout_sel(info,
								      sel);
		}
	}
	return -EINVAL;
}

static int rtled_impl_get_torch_current(struct rt_fled_info *info)
{
	int sel = info->hal->fled_get_torch_current_sel(info);

	if (sel < 0)
		return sel;
	return info->hal->fled_torch_current_list(info, sel);
}

static int rtled_impl_get_strobe_current(struct rt_fled_info *info)
{
	int sel = info->hal->fled_get_strobe_current_sel(info);

	if (sel < 0)
		return sel;
	return info->hal->fled_strobe_current_list(info, sel);
}

static int rtled_impl_get_timeout_level(struct rt_fled_info *info)
{
	int sel = info->hal->fled_get_timeout_level_sel(info);

	if (sel < 0)
		return sel;
	return info->hal->fled_timeout_level_list(info, sel);
}

static int rtled_impl_get_lv_protection(struct rt_fled_info *info)
{
	int sel = info->hal->fled_get_lv_protection_sel(info);

	if (sel < 0)
		return sel;
	return info->hal->fled_lv_protection_list(info, sel);
}

static int rtled_impl_get_strobe_timeout(struct rt_fled_info *info)
{
	int sel = info->hal->fled_get_strobe_timeout_sel(info);

	if (sel < 0)
		return sel;
	return info->hal->fled_strobe_timeout_list(info, sel);
}

#define HAL_NOT_IMPLEMENTED(x) (hal->x == NULL)
#define CHECK_HAL_IMPLEMENTED(x)	\
	do {				\
		if (hal->x == NULL)	\
			return -EINVAL; \
	} while (0)

static int rtfled_check_hal_implement(struct rt_fled_hal *hal)
{
	if (HAL_NOT_IMPLEMENTED(fled_set_torch_current))
		hal->fled_set_torch_current = rtled_impl_set_torch_current;
	if (HAL_NOT_IMPLEMENTED(fled_set_strobe_current))
		hal->fled_set_strobe_current = rtled_impl_set_strobe_current;
	if (HAL_NOT_IMPLEMENTED(fled_set_timeout_level))
		hal->fled_set_timeout_level = rtled_impl_set_timeout_level;
	if (HAL_NOT_IMPLEMENTED(fled_set_lv_protection))
		hal->fled_set_lv_protection = rtled_impl_set_lv_protection;
	if (HAL_NOT_IMPLEMENTED(fled_set_strobe_timeout))
		hal->fled_set_strobe_timeout = rtled_impl_set_strobe_timeout;
	if (HAL_NOT_IMPLEMENTED(fled_get_torch_current))
		hal->fled_get_torch_current = rtled_impl_get_torch_current;
	if (HAL_NOT_IMPLEMENTED(fled_get_strobe_current))
		hal->fled_get_strobe_current = rtled_impl_get_strobe_current;
	if (HAL_NOT_IMPLEMENTED(fled_get_timeout_level))
		hal->fled_get_timeout_level = rtled_impl_get_timeout_level;
	if (HAL_NOT_IMPLEMENTED(fled_get_lv_protection))
		hal->fled_get_lv_protection = rtled_impl_get_lv_protection;
	if (HAL_NOT_IMPLEMENTED(fled_get_strobe_timeout))
		hal->fled_get_strobe_timeout = rtled_impl_get_strobe_timeout;
	CHECK_HAL_IMPLEMENTED(fled_set_mode);
	CHECK_HAL_IMPLEMENTED(fled_get_mode);
	CHECK_HAL_IMPLEMENTED(fled_strobe);
	CHECK_HAL_IMPLEMENTED(fled_torch_current_list);
	CHECK_HAL_IMPLEMENTED(fled_strobe_current_list);
	CHECK_HAL_IMPLEMENTED(fled_timeout_level_list);
	CHECK_HAL_IMPLEMENTED(fled_lv_protection_list);
	CHECK_HAL_IMPLEMENTED(fled_strobe_timeout_list);
	CHECK_HAL_IMPLEMENTED(fled_set_torch_current_sel);
	CHECK_HAL_IMPLEMENTED(fled_set_strobe_current_sel);
	CHECK_HAL_IMPLEMENTED(fled_set_timeout_level_sel);
	CHECK_HAL_IMPLEMENTED(fled_set_lv_protection_sel);
	CHECK_HAL_IMPLEMENTED(fled_set_strobe_timeout_sel);
	CHECK_HAL_IMPLEMENTED(fled_get_torch_current_sel);
	CHECK_HAL_IMPLEMENTED(fled_get_strobe_current_sel);
	CHECK_HAL_IMPLEMENTED(fled_get_timeout_level_sel);
	CHECK_HAL_IMPLEMENTED(fled_get_lv_protection_sel);
	CHECK_HAL_IMPLEMENTED(fled_get_strobe_timeout_sel);
	return 0;
}

static int rtfled_probe(struct platform_device *pdev)
{
	rt_fled_info_t *info = dev_get_drvdata(pdev->dev.parent);
	int rc;

	BUG_ON(info == NULL);
	BUG_ON(info->hal == NULL);

	RTFLED_INFO("Richtek FlashLED Driver is probing\n");
	rc = rtfled_check_hal_implement(info->hal);
	if (rc < 0) {
		RTFLED_ERR("HAL implemented uncompletedly\n");
		goto err_check_hal;
	}
	platform_set_drvdata(pdev, info);
	info->flashlight_dev =
	    flashlight_device_register(info->name ? info->name : RT_FLED_DEVICE,
				       &pdev->dev, info, &rtfled_impl_ops,
				       info->init_props);
	if (info->hal->fled_init) {
		rc = info->hal->fled_init(info);
		if (rc < 0) {
			RTFLED_ERR("Initialization failed\n");
			goto err_init;
		}
	}
	RTFLED_INFO("Richtek FlashLED Driver initialized successfully\n");
	return 0;
err_init:
	flashlight_device_unregister(info->flashlight_dev);
err_check_hal:
	return rc;
}

static int rtfled_remove(struct platform_device *pdev)
{
	rt_fled_info_t *info = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	flashlight_device_unregister(info->flashlight_dev);
	return 0;
}

static struct platform_driver rt_flash_led_driver = {
	.driver = {
		   .name = RT_FLED_DEVICE,
		   .owner = THIS_MODULE,
		   },
	.shutdown = rfled_shutdown,
	.probe = rtfled_probe,
	.remove = rtfled_remove,
};

static int rtfled_init(void)
{
	return platform_driver_register(&rt_flash_led_driver);
}
subsys_initcall(rtfled_init);

static void rtfled_exit(void)
{
	platform_driver_unregister(&rt_flash_led_driver);
}
module_exit(rtfled_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick Chang <patrick_chang@richtek.com");
MODULE_VERSION("1.0.0_G");
MODULE_DESCRIPTION("Richtek Flash LED Driver");
