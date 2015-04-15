/*
 * drivers/gpu/drm/omapdrm/omap_crtc.c
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/completion.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_mode.h>
#include <drm/drm_plane_helper.h>

#include "omap_drv.h"

#define to_omap_crtc(x) container_of(x, struct omap_crtc, base)

struct omap_crtc {
	struct drm_crtc base;

	const char *name;
	enum omap_channel channel;
	struct drm_encoder *current_encoder;

	/*
	 * Temporary: eventually this will go away, but it is needed
	 * for now to keep the output's happy.  (They only need
	 * mgr->id.)  Eventually this will be replaced w/ something
	 * more common-panel-framework-y
	 */
	struct omap_overlay_manager *mgr;

	struct omap_video_timings timings;

	struct omap_drm_irq vblank_irq;
	struct omap_drm_irq error_irq;

	/* pending event */
	struct drm_pending_vblank_event *event;
	wait_queue_head_t flip_wait;

	struct completion completion;

	bool ignore_digit_sync_lost;
};

/* -----------------------------------------------------------------------------
 * Helper Functions
 */

uint32_t pipe2vbl(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);

	return dispc_mgr_get_vsync_irq(omap_crtc->channel);
}

const struct omap_video_timings *omap_crtc_timings(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	return &omap_crtc->timings;
}

enum omap_channel omap_crtc_channel(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	return omap_crtc->channel;
}

/* -----------------------------------------------------------------------------
 * DSS Manager Functions
 */

/*
 * Manager-ops, callbacks from output when they need to configure
 * the upstream part of the video pipe.
 *
 * Most of these we can ignore until we add support for command-mode
 * panels.. for video-mode the crtc-helpers already do an adequate
 * job of sequencing the setup of the video pipe in the proper order
 */

/* ovl-mgr-id -> crtc */
static struct omap_crtc *omap_crtcs[8];

/* we can probably ignore these until we support command-mode panels: */
static int omap_crtc_dss_connect(struct omap_overlay_manager *mgr,
		struct omap_dss_device *dst)
{
	if (mgr->output)
		return -EINVAL;

	if ((mgr->supported_outputs & dst->id) == 0)
		return -EINVAL;

	dst->manager = mgr;
	mgr->output = dst;

	return 0;
}

static void omap_crtc_dss_disconnect(struct omap_overlay_manager *mgr,
		struct omap_dss_device *dst)
{
	mgr->output->manager = NULL;
	mgr->output = NULL;
}

static void omap_crtc_dss_start_update(struct omap_overlay_manager *mgr)
{
}

/* Called only from omap_crtc_encoder_setup and suspend/resume handlers. */
static void omap_crtc_set_enabled(struct drm_crtc *crtc, bool enable)
{
	struct drm_device *dev = crtc->dev;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	enum omap_channel channel = omap_crtc->channel;
	struct omap_irq_wait *wait;
	u32 framedone_irq, vsync_irq;
	int ret;

	if (dispc_mgr_is_enabled(channel) == enable)
		return;

	if (omap_crtc->channel == OMAP_DSS_CHANNEL_DIGIT) {
		/*
		 * Digit output produces some sync lost interrupts during the
		 * first frame when enabling, so we need to ignore those.
		 */
		omap_crtc->ignore_digit_sync_lost = true;
	}

	framedone_irq = dispc_mgr_get_framedone_irq(channel);
	vsync_irq = dispc_mgr_get_vsync_irq(channel);

	if (enable) {
		wait = omap_irq_wait_init(dev, vsync_irq, 1);
	} else {
		/*
		 * When we disable the digit output, we need to wait for
		 * FRAMEDONE to know that DISPC has finished with the output.
		 *
		 * OMAP2/3 does not have FRAMEDONE irq for digit output, and in
		 * that case we need to use vsync interrupt, and wait for both
		 * even and odd frames.
		 */

		if (framedone_irq)
			wait = omap_irq_wait_init(dev, framedone_irq, 1);
		else
			wait = omap_irq_wait_init(dev, vsync_irq, 2);
	}

	dispc_mgr_enable(channel, enable);

	ret = omap_irq_wait(dev, wait, msecs_to_jiffies(100));
	if (ret) {
		dev_err(dev->dev, "%s: timeout waiting for %s\n",
				omap_crtc->name, enable ? "enable" : "disable");
	}

	if (omap_crtc->channel == OMAP_DSS_CHANNEL_DIGIT) {
		omap_crtc->ignore_digit_sync_lost = false;
		/* make sure the irq handler sees the value above */
		mb();
	}
}


static int omap_crtc_dss_enable(struct omap_overlay_manager *mgr)
{
	struct omap_crtc *omap_crtc = omap_crtcs[mgr->id];
	struct omap_overlay_manager_info info;

	memset(&info, 0, sizeof(info));
	info.default_color = 0x00000000;
	info.trans_key = 0x00000000;
	info.trans_key_type = OMAP_DSS_COLOR_KEY_GFX_DST;
	info.trans_enabled = false;

	dispc_mgr_setup(omap_crtc->channel, &info);
	dispc_mgr_set_timings(omap_crtc->channel,
			&omap_crtc->timings);
	omap_crtc_set_enabled(&omap_crtc->base, true);

	return 0;
}

static void omap_crtc_dss_disable(struct omap_overlay_manager *mgr)
{
	struct omap_crtc *omap_crtc = omap_crtcs[mgr->id];

	omap_crtc_set_enabled(&omap_crtc->base, false);
}

static void omap_crtc_dss_set_timings(struct omap_overlay_manager *mgr,
		const struct omap_video_timings *timings)
{
	struct omap_crtc *omap_crtc = omap_crtcs[mgr->id];
	DBG("%s", omap_crtc->name);
	omap_crtc->timings = *timings;
}

static void omap_crtc_dss_set_lcd_config(struct omap_overlay_manager *mgr,
		const struct dss_lcd_mgr_config *config)
{
	struct omap_crtc *omap_crtc = omap_crtcs[mgr->id];
	DBG("%s", omap_crtc->name);
	dispc_mgr_set_lcd_config(omap_crtc->channel, config);
}

static int omap_crtc_dss_register_framedone(
		struct omap_overlay_manager *mgr,
		void (*handler)(void *), void *data)
{
	return 0;
}

static void omap_crtc_dss_unregister_framedone(
		struct omap_overlay_manager *mgr,
		void (*handler)(void *), void *data)
{
}

static const struct dss_mgr_ops mgr_ops = {
	.connect = omap_crtc_dss_connect,
	.disconnect = omap_crtc_dss_disconnect,
	.start_update = omap_crtc_dss_start_update,
	.enable = omap_crtc_dss_enable,
	.disable = omap_crtc_dss_disable,
	.set_timings = omap_crtc_dss_set_timings,
	.set_lcd_config = omap_crtc_dss_set_lcd_config,
	.register_framedone_handler = omap_crtc_dss_register_framedone,
	.unregister_framedone_handler = omap_crtc_dss_unregister_framedone,
};

/* -----------------------------------------------------------------------------
 * Setup, Flush and Page Flip
 */

void omap_crtc_cancel_page_flip(struct drm_crtc *crtc, struct drm_file *file)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct drm_pending_vblank_event *event;
	struct drm_device *dev = crtc->dev;
	unsigned long flags;

	/* Destroy the pending vertical blanking event associated with the
	 * pending page flip, if any, and disable vertical blanking interrupts.
	 */

	spin_lock_irqsave(&dev->event_lock, flags);

	event = omap_crtc->event;
	omap_crtc->event = NULL;

	if (event && event->base.file_priv == file) {
		event->base.destroy(&event->base);
		drm_crtc_vblank_put(crtc);
	}

	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static void omap_crtc_complete_page_flip(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct drm_pending_vblank_event *event;
	struct drm_device *dev = crtc->dev;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);

	event = omap_crtc->event;
	omap_crtc->event = NULL;

	if (event) {
		drm_crtc_send_vblank_event(crtc, event);
		wake_up(&omap_crtc->flip_wait);
		drm_crtc_vblank_put(crtc);
	}

	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static bool omap_crtc_page_flip_pending(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	unsigned long flags;
	bool pending;

	spin_lock_irqsave(&dev->event_lock, flags);
	pending = omap_crtc->event != NULL;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	return pending;
}

static void omap_crtc_wait_page_flip(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);

	if (wait_event_timeout(omap_crtc->flip_wait,
			       !omap_crtc_page_flip_pending(crtc),
			       msecs_to_jiffies(50)))
		return;

	dev_warn(crtc->dev->dev, "page flip timeout!\n");

	omap_crtc_complete_page_flip(crtc);
}

static void omap_crtc_error_irq(struct omap_drm_irq *irq, uint32_t irqstatus)
{
	struct omap_crtc *omap_crtc =
			container_of(irq, struct omap_crtc, error_irq);

	if (omap_crtc->ignore_digit_sync_lost) {
		irqstatus &= ~DISPC_IRQ_SYNC_LOST_DIGIT;
		if (!irqstatus)
			return;
	}

	DRM_ERROR_RATELIMITED("%s: errors: %08x\n", omap_crtc->name, irqstatus);
}

static void omap_crtc_vblank_irq(struct omap_drm_irq *irq, uint32_t irqstatus)
{
	struct omap_crtc *omap_crtc =
			container_of(irq, struct omap_crtc, vblank_irq);
	struct drm_device *dev = omap_crtc->base.dev;

	if (dispc_mgr_go_busy(omap_crtc->channel))
		return;

	DBG("%s: apply done", omap_crtc->name);
	__omap_irq_unregister(dev, &omap_crtc->vblank_irq);

	/* wakeup userspace */
	omap_crtc_complete_page_flip(&omap_crtc->base);

	complete(&omap_crtc->completion);
}

int omap_crtc_flush(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);

	DBG("%s: GO", omap_crtc->name);

	WARN_ON(!drm_modeset_is_locked(&crtc->mutex));
	WARN_ON(omap_crtc->vblank_irq.registered);

	dispc_runtime_get();

	if (dispc_mgr_is_enabled(omap_crtc->channel)) {
		dispc_mgr_go(omap_crtc->channel);
		omap_irq_register(crtc->dev, &omap_crtc->vblank_irq);

		WARN_ON(!wait_for_completion_timeout(&omap_crtc->completion,
						     msecs_to_jiffies(100)));
		reinit_completion(&omap_crtc->completion);
	}

	dispc_runtime_put();

	return 0;
}

static void omap_crtc_encoder_setup(struct drm_crtc *crtc, bool enable)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct omap_drm_private *priv = crtc->dev->dev_private;
	struct drm_encoder *encoder = NULL;
	unsigned int i;

	DBG("%s: enable=%d", omap_crtc->name, enable);

	dispc_runtime_get();

	for (i = 0; i < priv->num_encoders; i++) {
		if (priv->encoders[i]->crtc == crtc) {
			encoder = priv->encoders[i];
			break;
		}
	}

	if (omap_crtc->current_encoder && encoder != omap_crtc->current_encoder)
		omap_encoder_set_enabled(omap_crtc->current_encoder, false);

	omap_crtc->current_encoder = encoder;

	if (encoder) {
		omap_encoder_set_enabled(encoder, false);
		if (enable) {
			omap_encoder_update(encoder, omap_crtc->mgr,
					    &omap_crtc->timings);
			omap_encoder_set_enabled(encoder, true);
		}
	}

	dispc_runtime_put();
}

/* -----------------------------------------------------------------------------
 * CRTC Functions
 */

static void omap_crtc_destroy(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);

	DBG("%s", omap_crtc->name);

	WARN_ON(omap_crtc->vblank_irq.registered);
	omap_irq_unregister(crtc->dev, &omap_crtc->error_irq);

	drm_crtc_cleanup(crtc);

	kfree(omap_crtc);
}

static bool omap_crtc_mode_fixup(struct drm_crtc *crtc,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void omap_crtc_enable(struct drm_crtc *crtc)
{
	struct omap_drm_private *priv = crtc->dev->dev_private;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	unsigned int i;

	DBG("%s", omap_crtc->name);

	/* Enable all planes associated with the CRTC. */
	for (i = 0; i < priv->num_planes; i++) {
		struct drm_plane *plane = priv->planes[i];

		if (plane->crtc == crtc)
			WARN_ON(omap_plane_set_enable(plane, true));
	}

	omap_crtc_encoder_setup(crtc, true);
	omap_crtc_flush(crtc);

	dispc_runtime_get();
	drm_crtc_vblank_on(crtc);
	dispc_runtime_put();
}

static void omap_crtc_disable(struct drm_crtc *crtc)
{
	struct omap_drm_private *priv = crtc->dev->dev_private;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	unsigned int i;

	DBG("%s", omap_crtc->name);

	omap_crtc_wait_page_flip(crtc);
	dispc_runtime_get();
	drm_crtc_vblank_off(crtc);
	dispc_runtime_put();

	/* Disable all planes associated with the CRTC. */
	for (i = 0; i < priv->num_planes; i++) {
		struct drm_plane *plane = priv->planes[i];

		if (plane->crtc == crtc)
			WARN_ON(omap_plane_set_enable(plane, false));
	}

	omap_crtc_encoder_setup(crtc, false);
	omap_crtc_flush(crtc);
}

static void omap_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;

	DBG("%s: set mode: %d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x",
	    omap_crtc->name, mode->base.id, mode->name,
	    mode->vrefresh, mode->clock,
	    mode->hdisplay, mode->hsync_start, mode->hsync_end, mode->htotal,
	    mode->vdisplay, mode->vsync_start, mode->vsync_end, mode->vtotal,
	    mode->type, mode->flags);

	copy_timings_drm_to_omap(&omap_crtc->timings, mode);
}

static void omap_crtc_atomic_begin(struct drm_crtc *crtc)
{
	struct drm_pending_vblank_event *event = crtc->state->event;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	unsigned long flags;

	dispc_runtime_get();

	if (event) {
		WARN_ON(omap_crtc->event);
		WARN_ON(drm_crtc_vblank_get(crtc) != 0);

		spin_lock_irqsave(&dev->event_lock, flags);
		omap_crtc->event = event;
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}
}

static void omap_crtc_atomic_flush(struct drm_crtc *crtc)
{
	omap_crtc_flush(crtc);

	dispc_runtime_put();

	crtc->invert_dimensions = !!(crtc->primary->state->rotation &
				    (BIT(DRM_ROTATE_90) | BIT(DRM_ROTATE_270)));
}

static int omap_crtc_atomic_set_property(struct drm_crtc *crtc,
					 struct drm_crtc_state *state,
					 struct drm_property *property,
					 uint64_t val)
{
	struct drm_plane_state *plane_state;
	struct drm_plane *plane = crtc->primary;

	/*
	 * Delegate property set to the primary plane. Get the plane state and
	 * set the property directly.
	 */

	plane_state = drm_atomic_get_plane_state(state->state, plane);
	if (!plane_state)
		return -EINVAL;

	return drm_atomic_plane_set_property(plane, plane_state, property, val);
}

static int omap_crtc_atomic_get_property(struct drm_crtc *crtc,
					 const struct drm_crtc_state *state,
					 struct drm_property *property,
					 uint64_t *val)
{
	/*
	 * Delegate property get to the primary plane. The
	 * drm_atomic_plane_get_property() function isn't exported, but can be
	 * called through drm_object_property_get_value() as that will call
	 * drm_atomic_get_property() for atomic drivers.
	 */
	return drm_object_property_get_value(&crtc->primary->base, property,
					     val);
}

static const struct drm_crtc_funcs omap_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.set_config = drm_atomic_helper_set_config,
	.destroy = omap_crtc_destroy,
	.page_flip = drm_atomic_helper_page_flip,
	.set_property = drm_atomic_helper_crtc_set_property,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.atomic_set_property = omap_crtc_atomic_set_property,
	.atomic_get_property = omap_crtc_atomic_get_property,
};

static const struct drm_crtc_helper_funcs omap_crtc_helper_funcs = {
	.mode_fixup = omap_crtc_mode_fixup,
	.mode_set_nofb = omap_crtc_mode_set_nofb,
	.disable = omap_crtc_disable,
	.enable = omap_crtc_enable,
	.atomic_begin = omap_crtc_atomic_begin,
	.atomic_flush = omap_crtc_atomic_flush,
};

/* -----------------------------------------------------------------------------
 * Init and Cleanup
 */

static const char *channel_names[] = {
	[OMAP_DSS_CHANNEL_LCD] = "lcd",
	[OMAP_DSS_CHANNEL_DIGIT] = "tv",
	[OMAP_DSS_CHANNEL_LCD2] = "lcd2",
	[OMAP_DSS_CHANNEL_LCD3] = "lcd3",
};

void omap_crtc_pre_init(void)
{
	dss_install_mgr_ops(&mgr_ops);
}

void omap_crtc_pre_uninit(void)
{
	dss_uninstall_mgr_ops();
}

/* initialize crtc */
struct drm_crtc *omap_crtc_init(struct drm_device *dev,
		struct drm_plane *plane, enum omap_channel channel, int id)
{
	struct drm_crtc *crtc = NULL;
	struct omap_crtc *omap_crtc;
	int ret;

	DBG("%s", channel_names[channel]);

	omap_crtc = kzalloc(sizeof(*omap_crtc), GFP_KERNEL);
	if (!omap_crtc)
		return NULL;

	crtc = &omap_crtc->base;

	init_waitqueue_head(&omap_crtc->flip_wait);
	init_completion(&omap_crtc->completion);

	omap_crtc->channel = channel;
	omap_crtc->name = channel_names[channel];

	omap_crtc->vblank_irq.irqmask = pipe2vbl(crtc);
	omap_crtc->vblank_irq.irq = omap_crtc_vblank_irq;

	omap_crtc->error_irq.irqmask =
			dispc_mgr_get_sync_lost_irq(channel);
	omap_crtc->error_irq.irq = omap_crtc_error_irq;
	omap_irq_register(dev, &omap_crtc->error_irq);

	/* temporary: */
	omap_crtc->mgr = omap_dss_get_overlay_manager(channel);

	ret = drm_crtc_init_with_planes(dev, crtc, plane, NULL,
					&omap_crtc_funcs);
	if (ret < 0) {
		kfree(omap_crtc);
		return NULL;
	}

	drm_crtc_helper_add(crtc, &omap_crtc_helper_funcs);

	omap_plane_install_properties(crtc->primary, &crtc->base);

	omap_crtcs[channel] = omap_crtc;

	return crtc;
}
