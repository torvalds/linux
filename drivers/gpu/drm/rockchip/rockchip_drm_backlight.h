/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _ROCKCHIP_DRM_BACKLIGHT_H
#define _ROCKCHIP_DRM_BACKLIGHT_H

#include <linux/pwm_backlight.h>

struct rockchip_drm_backlight {
	struct device *dev;
	struct backlight_device *bd;
	struct pinctrl *pinctrl;
	struct pinctrl_state *dummy_state;
	struct pwm_device *pwm;

	struct drm_connector *connector;
	struct sub_backlight *current_sub, *sub;

	struct regulator *power_supply;
	struct gpio_desc *enable_gpio;
	bool enabled;

	unsigned int max_brightness;
	unsigned int dft_brightness;
	unsigned int lth_brightness;
	unsigned int scale;
	unsigned int *levels;
};

struct rockchip_sub_backlight_ops {
	void (*config_done)(struct device *dev, bool async);
};

#if IS_ENABLED(CONFIG_ROCKCHIP_DRM_BACKLIGHT)
int of_rockchip_drm_sub_backlight_register(struct device *dev,
				struct drm_crtc *crtc,
				const struct rockchip_sub_backlight_ops *ops);
void rockchip_drm_backlight_update(struct drm_device *drm);
#else
static inline int
of_rockchip_drm_sub_backlight_register(struct device *dev,
				struct drm_crtc *crtc,
				const struct rockchip_sub_backlight_ops *ops)
{
	return 0;
}

static inline void rockchip_drm_backlight_update(struct drm_device *drm)
{
}

#endif
#endif
