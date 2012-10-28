/*
 * GMA500 Backlight Interface
 *
 * Copyright (c) 2009-2011, Intel Corporation.
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

#include "psb_drv.h"
#include "psb_intel_reg.h"
#include "psb_intel_drv.h"
#include "intel_bios.h"
#include "power.h"

static void do_gma_backlight_set(struct drm_device *dev)
{
#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE
	struct drm_psb_private *dev_priv = dev->dev_private;
	backlight_update_status(dev_priv->backlight_device);
#endif	
}

void gma_backlight_enable(struct drm_device *dev)
{
#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE
	struct drm_psb_private *dev_priv = dev->dev_private;
	dev_priv->backlight_enabled = true;
	if (dev_priv->backlight_device) {
		dev_priv->backlight_device->props.brightness = dev_priv->backlight_level;
		do_gma_backlight_set(dev);
	}
#endif	
}

void gma_backlight_disable(struct drm_device *dev)
{
#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE
	struct drm_psb_private *dev_priv = dev->dev_private;
	dev_priv->backlight_enabled = false;
	if (dev_priv->backlight_device) {
		dev_priv->backlight_device->props.brightness = 0;
		do_gma_backlight_set(dev);
	}
#endif	
}

void gma_backlight_set(struct drm_device *dev, int v)
{
#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE
	struct drm_psb_private *dev_priv = dev->dev_private;
	dev_priv->backlight_level = v;
	if (dev_priv->backlight_device && dev_priv->backlight_enabled) {
		dev_priv->backlight_device->props.brightness = v;
		do_gma_backlight_set(dev);
	}
#endif	
}

int gma_backlight_init(struct drm_device *dev)
{
#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE
	struct drm_psb_private *dev_priv = dev->dev_private;
	dev_priv->backlight_enabled = true;
	return dev_priv->ops->backlight_init(dev);
#else
	return 0;
#endif
}

void gma_backlight_exit(struct drm_device *dev)
{
#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE
	struct drm_psb_private *dev_priv = dev->dev_private;
	if (dev_priv->backlight_device) {
		dev_priv->backlight_device->props.brightness = 0;
		backlight_update_status(dev_priv->backlight_device);
		backlight_device_unregister(dev_priv->backlight_device);
	}
#endif
}
