// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#include <linux/platform_device.h>

#include <drm/drm_drv.h>
#include <drm/drm_print.h>

#include "tidss_crtc.h"
#include "tidss_dispc.h"
#include "tidss_drv.h"
#include "tidss_irq.h"
#include "tidss_plane.h"

/* call with wait_lock and dispc runtime held */
static void tidss_irq_update(struct tidss_device *tidss)
{
	assert_spin_locked(&tidss->wait_lock);

	dispc_set_irqenable(tidss->dispc, tidss->irq_mask);
}

void tidss_irq_enable_vblank(struct drm_crtc *crtc)
{
	struct drm_device *ddev = crtc->dev;
	struct tidss_device *tidss = to_tidss(ddev);
	struct tidss_crtc *tcrtc = to_tidss_crtc(crtc);
	u32 hw_videoport = tcrtc->hw_videoport;
	unsigned long flags;

	spin_lock_irqsave(&tidss->wait_lock, flags);
	tidss->irq_mask |= DSS_IRQ_VP_VSYNC_EVEN(hw_videoport) |
			   DSS_IRQ_VP_VSYNC_ODD(hw_videoport);
	tidss_irq_update(tidss);
	spin_unlock_irqrestore(&tidss->wait_lock, flags);
}

void tidss_irq_disable_vblank(struct drm_crtc *crtc)
{
	struct drm_device *ddev = crtc->dev;
	struct tidss_device *tidss = to_tidss(ddev);
	struct tidss_crtc *tcrtc = to_tidss_crtc(crtc);
	u32 hw_videoport = tcrtc->hw_videoport;
	unsigned long flags;

	spin_lock_irqsave(&tidss->wait_lock, flags);
	tidss->irq_mask &= ~(DSS_IRQ_VP_VSYNC_EVEN(hw_videoport) |
			     DSS_IRQ_VP_VSYNC_ODD(hw_videoport));
	tidss_irq_update(tidss);
	spin_unlock_irqrestore(&tidss->wait_lock, flags);
}

static irqreturn_t tidss_irq_handler(int irq, void *arg)
{
	struct drm_device *ddev = (struct drm_device *)arg;
	struct tidss_device *tidss = to_tidss(ddev);
	unsigned int id;
	dispc_irq_t irqstatus;

	irqstatus = dispc_read_and_clear_irqstatus(tidss->dispc);

	for (id = 0; id < tidss->num_crtcs; id++) {
		struct drm_crtc *crtc = tidss->crtcs[id];
		struct tidss_crtc *tcrtc = to_tidss_crtc(crtc);
		u32 hw_videoport = tcrtc->hw_videoport;

		if (irqstatus & (DSS_IRQ_VP_VSYNC_EVEN(hw_videoport) |
				 DSS_IRQ_VP_VSYNC_ODD(hw_videoport)))
			tidss_crtc_vblank_irq(crtc);

		if (irqstatus & (DSS_IRQ_VP_FRAME_DONE(hw_videoport)))
			tidss_crtc_framedone_irq(crtc);

		if (irqstatus & DSS_IRQ_VP_SYNC_LOST(hw_videoport))
			tidss_crtc_error_irq(crtc, irqstatus);
	}

	for (unsigned int i = 0; i < tidss->num_planes; ++i) {
		struct drm_plane *plane = tidss->planes[i];
		struct tidss_plane *tplane = to_tidss_plane(plane);

		if (irqstatus & DSS_IRQ_PLANE_FIFO_UNDERFLOW(tplane->hw_plane_id))
			tidss_plane_error_irq(plane, irqstatus);
	}

	return IRQ_HANDLED;
}

void tidss_irq_resume(struct tidss_device *tidss)
{
	unsigned long flags;

	spin_lock_irqsave(&tidss->wait_lock, flags);
	tidss_irq_update(tidss);
	spin_unlock_irqrestore(&tidss->wait_lock, flags);
}

int tidss_irq_install(struct drm_device *ddev, unsigned int irq)
{
	struct tidss_device *tidss = to_tidss(ddev);
	int ret;

	if (irq == IRQ_NOTCONNECTED)
		return -ENOTCONN;

	ret = request_irq(irq, tidss_irq_handler, 0, ddev->driver->name, ddev);
	if (ret)
		return ret;

	tidss->irq_mask = 0;

	for (unsigned int i = 0; i < tidss->num_crtcs; ++i) {
		struct tidss_crtc *tcrtc = to_tidss_crtc(tidss->crtcs[i]);

		tidss->irq_mask |= DSS_IRQ_VP_SYNC_LOST(tcrtc->hw_videoport);

		tidss->irq_mask |= DSS_IRQ_VP_FRAME_DONE(tcrtc->hw_videoport);
	}

	for (unsigned int i = 0; i < tidss->num_planes; ++i) {
		struct tidss_plane *tplane = to_tidss_plane(tidss->planes[i]);

		tidss->irq_mask |= DSS_IRQ_PLANE_FIFO_UNDERFLOW(tplane->hw_plane_id);
	}

	return 0;
}

void tidss_irq_uninstall(struct drm_device *ddev)
{
	struct tidss_device *tidss = to_tidss(ddev);

	free_irq(tidss->irq, ddev);
}
