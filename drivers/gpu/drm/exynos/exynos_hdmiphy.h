/*
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Authors:
 *	Rahul Sharma <rahul.sharma@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _EXYNOS_HDMIPHY_H_
#define _EXYNOS_HDMIPHY_H_

/*
 * Exynos DRM Hdmiphy Structure.
 *
 * @check_mode: check if mode is supported.
 * @mode_set: set the mode if supported.
 * @commit: apply the mode.
 * @enable: enable phy operation.
 * @poweron: phy power on or off.
 */
struct exynos_hdmiphy_ops {
	int (*check_mode)(struct device *dev,
			struct drm_display_mode *mode);
	int (*mode_set)(struct device *dev,
			struct drm_display_mode *mode);
	int (*commit)(struct device *dev);
	void (*enable)(struct device *dev, int enable);
	void (*poweron)(struct device *dev, int mode);
};

int exynos_hdmiphy_i2c_driver_register(void);
void exynos_hdmiphy_i2c_driver_unregister(void);

int exynos_hdmiphy_platform_driver_register(void);
void exynos_hdmiphy_platform_driver_unregister(void);

struct exynos_hdmiphy_ops *exynos_hdmiphy_i2c_device_get_ops
			(struct device *dev);
struct exynos_hdmiphy_ops *exynos_hdmiphy_platform_device_get_ops
			(struct device *dev);

#endif
