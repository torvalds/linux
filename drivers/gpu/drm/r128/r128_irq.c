/* r128_irq.c -- IRQ handling for radeon -*- linux-c -*- */
/*
 * Copyright (C) The Weather Channel, Inc.  2002.  All Rights Reserved.
 *
 * The Weather Channel (TM) funded Tungsten Graphics to develop the
 * initial release of the Radeon 8500 driver under the XFree86 license.
 * This notice must be preserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Keith Whitwell <keith@tungstengraphics.com>
 *    Eric Anholt <anholt@FreeBSD.org>
 */

#include "drmP.h"
#include "drm.h"
#include "r128_drm.h"
#include "r128_drv.h"

u32 r128_get_vblank_counter(struct drm_device *dev, int crtc)
{
	const drm_r128_private_t *dev_priv = dev->dev_private;

	if (crtc != 0)
		return 0;

	return atomic_read(&dev_priv->vbl_received);
}

irqreturn_t r128_driver_irq_handler(DRM_IRQ_ARGS)
{
	struct drm_device *dev = (struct drm_device *) arg;
	drm_r128_private_t *dev_priv = (drm_r128_private_t *) dev->dev_private;
	int status;

	status = R128_READ(R128_GEN_INT_STATUS);

	/* VBLANK interrupt */
	if (status & R128_CRTC_VBLANK_INT) {
		R128_WRITE(R128_GEN_INT_STATUS, R128_CRTC_VBLANK_INT_AK);
		atomic_inc(&dev_priv->vbl_received);
		drm_handle_vblank(dev, 0);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

int r128_enable_vblank(struct drm_device *dev, int crtc)
{
	drm_r128_private_t *dev_priv = dev->dev_private;

	if (crtc != 0) {
		DRM_ERROR("%s:  bad crtc %d\n", __func__, crtc);
		return -EINVAL;
	}

	R128_WRITE(R128_GEN_INT_CNTL, R128_CRTC_VBLANK_INT_EN);
	return 0;
}

void r128_disable_vblank(struct drm_device *dev, int crtc)
{
	if (crtc != 0)
		DRM_ERROR("%s:  bad crtc %d\n", __func__, crtc);

	/*
	 * FIXME: implement proper interrupt disable by using the vblank
	 * counter register (if available)
	 *
	 * R128_WRITE(R128_GEN_INT_CNTL,
	 *            R128_READ(R128_GEN_INT_CNTL) & ~R128_CRTC_VBLANK_INT_EN);
	 */
}

void r128_driver_irq_preinstall(struct drm_device *dev)
{
	drm_r128_private_t *dev_priv = (drm_r128_private_t *) dev->dev_private;

	/* Disable *all* interrupts */
	R128_WRITE(R128_GEN_INT_CNTL, 0);
	/* Clear vblank bit if it's already high */
	R128_WRITE(R128_GEN_INT_STATUS, R128_CRTC_VBLANK_INT_AK);
}

int r128_driver_irq_postinstall(struct drm_device *dev)
{
	return 0;
}

void r128_driver_irq_uninstall(struct drm_device *dev)
{
	drm_r128_private_t *dev_priv = (drm_r128_private_t *) dev->dev_private;
	if (!dev_priv)
		return;

	/* Disable *all* interrupts */
	R128_WRITE(R128_GEN_INT_CNTL, 0);
}
