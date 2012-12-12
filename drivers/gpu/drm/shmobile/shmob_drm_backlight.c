/*
 * shmob_drm_backlight.c  --  SH Mobile DRM Backlight
 *
 * Copyright (C) 2012 Renesas Corporation
 *
 * Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/backlight.h>

#include "shmob_drm_backlight.h"
#include "shmob_drm_crtc.h"
#include "shmob_drm_drv.h"

static int shmob_drm_backlight_update(struct backlight_device *bdev)
{
	struct shmob_drm_connector *scon = bl_get_data(bdev);
	struct shmob_drm_device *sdev = scon->connector.dev->dev_private;
	const struct shmob_drm_backlight_data *bdata = &sdev->pdata->backlight;
	int brightness = bdev->props.brightness;

	if (bdev->props.power != FB_BLANK_UNBLANK ||
	    bdev->props.state & BL_CORE_SUSPENDED)
		brightness = 0;

	return bdata->set_brightness(brightness);
}

static int shmob_drm_backlight_get_brightness(struct backlight_device *bdev)
{
	struct shmob_drm_connector *scon = bl_get_data(bdev);
	struct shmob_drm_device *sdev = scon->connector.dev->dev_private;
	const struct shmob_drm_backlight_data *bdata = &sdev->pdata->backlight;

	return bdata->get_brightness();
}

static const struct backlight_ops shmob_drm_backlight_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.update_status	= shmob_drm_backlight_update,
	.get_brightness	= shmob_drm_backlight_get_brightness,
};

void shmob_drm_backlight_dpms(struct shmob_drm_connector *scon, int mode)
{
	if (scon->backlight == NULL)
		return;

	scon->backlight->props.power = mode == DRM_MODE_DPMS_ON
				     ? FB_BLANK_UNBLANK : FB_BLANK_POWERDOWN;
	backlight_update_status(scon->backlight);
}

int shmob_drm_backlight_init(struct shmob_drm_connector *scon)
{
	struct shmob_drm_device *sdev = scon->connector.dev->dev_private;
	const struct shmob_drm_backlight_data *bdata = &sdev->pdata->backlight;
	struct drm_connector *connector = &scon->connector;
	struct drm_device *dev = connector->dev;
	struct backlight_device *backlight;

	if (!bdata->max_brightness)
		return 0;

	backlight = backlight_device_register(bdata->name, dev->dev, scon,
					      &shmob_drm_backlight_ops, NULL);
	if (IS_ERR(backlight)) {
		dev_err(dev->dev, "unable to register backlight device: %ld\n",
			PTR_ERR(backlight));
		return PTR_ERR(backlight);
	}

	backlight->props.max_brightness = bdata->max_brightness;
	backlight->props.brightness = bdata->max_brightness;
	backlight->props.power = FB_BLANK_POWERDOWN;
	backlight_update_status(backlight);

	scon->backlight = backlight;
	return 0;
}

void shmob_drm_backlight_exit(struct shmob_drm_connector *scon)
{
	backlight_device_unregister(scon->backlight);
}
