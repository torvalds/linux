/*
 *  psb backlight interface
 *
 * Copyright (c) 2009, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Authors: Eric Knopp
 *
 */

#include <linux/backlight.h>
#include <linux/version.h>
#include "psb_drv.h"
#include "psb_intel_reg.h"
#include "psb_intel_drv.h"
#include "psb_intel_bios.h"
#include "psb_powermgmt.h"

#define MRST_BLC_MAX_PWM_REG_FREQ	    0xFFFF
#define BLC_PWM_PRECISION_FACTOR 100	/* 10000000 */
#define BLC_PWM_FREQ_CALC_CONSTANT 32
#define MHz 1000000
#define BRIGHTNESS_MIN_LEVEL 1
#define BRIGHTNESS_MAX_LEVEL 100
#define BRIGHTNESS_MASK	0xFF
#define BLC_POLARITY_NORMAL 0
#define BLC_POLARITY_INVERSE 1
#define BLC_ADJUSTMENT_MAX 100

#define PSB_BLC_PWM_PRECISION_FACTOR    10
#define PSB_BLC_MAX_PWM_REG_FREQ        0xFFFE
#define PSB_BLC_MIN_PWM_REG_FREQ        0x2

#define PSB_BACKLIGHT_PWM_POLARITY_BIT_CLEAR (0xFFFE)
#define PSB_BACKLIGHT_PWM_CTL_SHIFT	(16)

static int psb_brightness;
static struct backlight_device *psb_backlight_device;
static u8 blc_brightnesscmd;
static u8 blc_pol;
static u8 blc_type;

int psb_set_brightness(struct backlight_device *bd)
{
	struct drm_device *dev = bl_get_data(psb_backlight_device);
	int level = bd->props.brightness;

	DRM_DEBUG_DRIVER("backlight level set to %d\n", level);

	/* Perform value bounds checking */
	if (level < BRIGHTNESS_MIN_LEVEL)
		level = BRIGHTNESS_MIN_LEVEL;

	psb_intel_lvds_set_brightness(dev, level);
	psb_brightness = level;
	return 0;
}

int psb_get_brightness(struct backlight_device *bd)
{
	DRM_DEBUG_DRIVER("brightness = 0x%x\n", psb_brightness);

	/* return locally cached var instead of HW read (due to DPST etc.) */
	/* FIXME: ideally return actual value in case firmware fiddled with
	   it */
	return psb_brightness;
}

static const struct backlight_ops psb_ops = {
	.get_brightness = psb_get_brightness,
	.update_status  = psb_set_brightness,
};

static int device_backlight_init(struct drm_device *dev)
{
	unsigned long core_clock;
	/* u32 bl_max_freq; */
	/* unsigned long value; */
	u16 bl_max_freq;
	uint32_t value;
	uint32_t blc_pwm_precision_factor;
	struct drm_psb_private *dev_priv = dev->dev_private;

	/* get bl_max_freq and pol from dev_priv*/
	if (!dev_priv->lvds_bl) {
		DRM_ERROR("Has no valid LVDS backlight info\n");
		return 1;
	}
	bl_max_freq = dev_priv->lvds_bl->freq;
	blc_pol = dev_priv->lvds_bl->pol;
	blc_pwm_precision_factor = PSB_BLC_PWM_PRECISION_FACTOR;
	blc_brightnesscmd = dev_priv->lvds_bl->brightnesscmd;
	blc_type = dev_priv->lvds_bl->type;

	core_clock = dev_priv->core_freq;

	value = (core_clock * MHz) / BLC_PWM_FREQ_CALC_CONSTANT;
	value *= blc_pwm_precision_factor;
	value /= bl_max_freq;
	value /= blc_pwm_precision_factor;

	if (ospm_power_using_hw_begin(OSPM_DISPLAY_ISLAND,
						OSPM_UHB_ONLY_IF_ON)) {
		/* Check: may be MFLD only */
		if (
		 value > (unsigned long long)PSB_BLC_MAX_PWM_REG_FREQ ||
		 value < (unsigned long long)PSB_BLC_MIN_PWM_REG_FREQ)
			return 2;
		else {
			value &= PSB_BACKLIGHT_PWM_POLARITY_BIT_CLEAR;
			REG_WRITE(BLC_PWM_CTL,
				(value << PSB_BACKLIGHT_PWM_CTL_SHIFT) |
				(value));
		}
		ospm_power_using_hw_end(OSPM_DISPLAY_ISLAND);
	}
	return 0;
}

int psb_backlight_init(struct drm_device *dev)
{
#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE
	int ret = 0;

	struct backlight_properties props;
	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = BRIGHTNESS_MAX_LEVEL;

	psb_backlight_device = backlight_device_register("psb-bl", NULL,
						(void *)dev, &psb_ops, &props);
	if (IS_ERR(psb_backlight_device))
		return PTR_ERR(psb_backlight_device);

	ret = device_backlight_init(dev);
	if (ret < 0)
		return ret;

	psb_backlight_device->props.brightness = BRIGHTNESS_MAX_LEVEL;
	psb_backlight_device->props.max_brightness = BRIGHTNESS_MAX_LEVEL;
	backlight_update_status(psb_backlight_device);
#endif
	return 0;
}

void psb_backlight_exit(void)
{
#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE
	psb_backlight_device->props.brightness = 0;
	backlight_update_status(psb_backlight_device);
	backlight_device_unregister(psb_backlight_device);
#endif
}

struct backlight_device *psb_get_backlight_device(void)
{
	return psb_backlight_device;
}
