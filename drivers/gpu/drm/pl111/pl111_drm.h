/*
 *
 * (C) COPYRIGHT 2012-2013 ARM Limited. All rights reserved.
 *
 *
 * Parts of this file were based on sources as follows:
 *
 * Copyright (c) 2006-2008 Intel Corporation
 * Copyright (c) 2007 Dave Airlie <airlied@linux.ie>
 * Copyright (C) 2011 Texas Instruments
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms of
 * such GNU licence.
 *
 */

#ifndef _PL111_DRM_H_
#define _PL111_DRM_H_

#include <drm/drm_gem.h>
#include <drm/drm_simple_kms_helper.h>

#define CLCD_IRQ_NEXTBASE_UPDATE BIT(2)

struct pl111_drm_connector {
	struct drm_connector connector;
	struct drm_panel *panel;
};

struct pl111_drm_dev_private {
	struct drm_device *drm;

	struct pl111_drm_connector connector;
	struct drm_simple_display_pipe pipe;
	struct drm_fbdev_cma *fbdev;

	void *regs;
	struct clk *clk;
};

#define to_pl111_connector(x) \
	container_of(x, struct pl111_drm_connector, connector)

int pl111_display_init(struct drm_device *dev);
int pl111_enable_vblank(struct drm_device *drm, unsigned int crtc);
void pl111_disable_vblank(struct drm_device *drm, unsigned int crtc);
irqreturn_t pl111_irq(int irq, void *data);
int pl111_connector_init(struct drm_device *dev);
int pl111_encoder_init(struct drm_device *dev);
int pl111_dumb_create(struct drm_file *file_priv,
		      struct drm_device *dev,
		      struct drm_mode_create_dumb *args);

#endif /* _PL111_DRM_H_ */
