/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 * Copyright (C) 2014 Endless Mobile
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_flip_work.h>
#include <drm/drm_crtc_helper.h>

#include "meson_crtc.h"
#include "meson_plane.h"
#include "meson_vpp.h"
#include "meson_viu.h"
#include "meson_registers.h"

/* CRTC definition */

struct meson_crtc {
	struct drm_crtc base;
	struct drm_pending_vblank_event *event;
	struct meson_drm *priv;
};
#define to_meson_crtc(x) container_of(x, struct meson_crtc, base)

/* CRTC */

static const struct drm_crtc_funcs meson_crtc_funcs = {
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.destroy		= drm_crtc_cleanup,
	.page_flip		= drm_atomic_helper_page_flip,
	.reset			= drm_atomic_helper_crtc_reset,
	.set_config             = drm_atomic_helper_set_config,
};

static void meson_crtc_enable(struct drm_crtc *crtc)
{
	struct meson_crtc *meson_crtc = to_meson_crtc(crtc);
	struct drm_plane *plane = meson_crtc->priv->primary_plane;
	struct meson_drm *priv = meson_crtc->priv;

	/* Enable VPP Postblend */
	writel(plane->state->crtc_w,
	       priv->io_base + _REG(VPP_POSTBLEND_H_SIZE));

	writel_bits_relaxed(VPP_POSTBLEND_ENABLE, VPP_POSTBLEND_ENABLE,
			    priv->io_base + _REG(VPP_MISC));

	priv->viu.osd1_enabled = true;
}

static void meson_crtc_disable(struct drm_crtc *crtc)
{
	struct meson_crtc *meson_crtc = to_meson_crtc(crtc);
	struct meson_drm *priv = meson_crtc->priv;

	priv->viu.osd1_enabled = false;

	/* Disable VPP Postblend */
	writel_bits_relaxed(VPP_POSTBLEND_ENABLE, 0,
			    priv->io_base + _REG(VPP_MISC));

	if (crtc->state->event && !crtc->state->active) {
		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		spin_unlock_irq(&crtc->dev->event_lock);

		crtc->state->event = NULL;
	}
}

static void meson_crtc_atomic_begin(struct drm_crtc *crtc,
				    struct drm_crtc_state *state)
{
	struct meson_crtc *meson_crtc = to_meson_crtc(crtc);
	unsigned long flags;

	if (crtc->state->event) {
		WARN_ON(drm_crtc_vblank_get(crtc) != 0);

		spin_lock_irqsave(&crtc->dev->event_lock, flags);
		meson_crtc->event = crtc->state->event;
		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
		crtc->state->event = NULL;
	}
}

static void meson_crtc_atomic_flush(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_crtc_state)
{
	struct meson_crtc *meson_crtc = to_meson_crtc(crtc);
	struct meson_drm *priv = meson_crtc->priv;

	if (priv->viu.osd1_enabled)
		priv->viu.osd1_commit = true;
}

static const struct drm_crtc_helper_funcs meson_crtc_helper_funcs = {
	.enable		= meson_crtc_enable,
	.disable	= meson_crtc_disable,
	.atomic_begin	= meson_crtc_atomic_begin,
	.atomic_flush	= meson_crtc_atomic_flush,
};

void meson_crtc_irq(struct meson_drm *priv)
{
	struct meson_crtc *meson_crtc = to_meson_crtc(priv->crtc);
	unsigned long flags;

	/* Update the OSD registers */
	if (priv->viu.osd1_enabled && priv->viu.osd1_commit) {
		writel_relaxed(priv->viu.osd1_ctrl_stat,
				priv->io_base + _REG(VIU_OSD1_CTRL_STAT));
		writel_relaxed(priv->viu.osd1_blk0_cfg[0],
				priv->io_base + _REG(VIU_OSD1_BLK0_CFG_W0));
		writel_relaxed(priv->viu.osd1_blk0_cfg[1],
				priv->io_base + _REG(VIU_OSD1_BLK0_CFG_W1));
		writel_relaxed(priv->viu.osd1_blk0_cfg[2],
				priv->io_base + _REG(VIU_OSD1_BLK0_CFG_W2));
		writel_relaxed(priv->viu.osd1_blk0_cfg[3],
				priv->io_base + _REG(VIU_OSD1_BLK0_CFG_W3));
		writel_relaxed(priv->viu.osd1_blk0_cfg[4],
				priv->io_base + _REG(VIU_OSD1_BLK0_CFG_W4));

		/* If output is interlace, make use of the Scaler */
		if (priv->viu.osd1_interlace) {
			struct drm_plane *plane = priv->primary_plane;
			struct drm_plane_state *state = plane->state;
			struct drm_rect dest = {
				.x1 = state->crtc_x,
				.y1 = state->crtc_y,
				.x2 = state->crtc_x + state->crtc_w,
				.y2 = state->crtc_y + state->crtc_h,
			};

			meson_vpp_setup_interlace_vscaler_osd1(priv, &dest);
		} else
			meson_vpp_disable_interlace_vscaler_osd1(priv);

		/* Enable OSD1 */
		writel_bits_relaxed(VPP_OSD1_POSTBLEND, VPP_OSD1_POSTBLEND,
				    priv->io_base + _REG(VPP_MISC));

		priv->viu.osd1_commit = false;
	}

	drm_crtc_handle_vblank(priv->crtc);

	spin_lock_irqsave(&priv->drm->event_lock, flags);
	if (meson_crtc->event) {
		drm_crtc_send_vblank_event(priv->crtc, meson_crtc->event);
		drm_crtc_vblank_put(priv->crtc);
		meson_crtc->event = NULL;
	}
	spin_unlock_irqrestore(&priv->drm->event_lock, flags);
}

int meson_crtc_create(struct meson_drm *priv)
{
	struct meson_crtc *meson_crtc;
	struct drm_crtc *crtc;
	int ret;

	meson_crtc = devm_kzalloc(priv->drm->dev, sizeof(*meson_crtc),
				  GFP_KERNEL);
	if (!meson_crtc)
		return -ENOMEM;

	meson_crtc->priv = priv;
	crtc = &meson_crtc->base;
	ret = drm_crtc_init_with_planes(priv->drm, crtc,
					priv->primary_plane, NULL,
					&meson_crtc_funcs, "meson_crtc");
	if (ret) {
		dev_err(priv->drm->dev, "Failed to init CRTC\n");
		return ret;
	}

	drm_crtc_helper_add(crtc, &meson_crtc_helper_funcs);

	priv->crtc = crtc;

	return 0;
}
