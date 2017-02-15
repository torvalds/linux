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

	struct videomode vm;

	bool ignore_digit_sync_lost;

	bool enabled;
	bool pending;
	wait_queue_head_t pending_wait;
	struct drm_pending_vblank_event *event;
};

/* -----------------------------------------------------------------------------
 * Helper Functions
 */

struct videomode *omap_crtc_timings(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	return &omap_crtc->vm;
}

enum omap_channel omap_crtc_channel(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	return omap_crtc->channel;
}

static bool omap_crtc_is_pending(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	unsigned long flags;
	bool pending;

	spin_lock_irqsave(&crtc->dev->event_lock, flags);
	pending = omap_crtc->pending;
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);

	return pending;
}

int omap_crtc_wait_pending(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);

	/*
	 * Timeout is set to a "sufficiently" high value, which should cover
	 * a single frame refresh even on slower displays.
	 */
	return wait_event_timeout(omap_crtc->pending_wait,
				  !omap_crtc_is_pending(crtc),
				  msecs_to_jiffies(250));
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
static struct omap_dss_device *omap_crtc_output[8];

/* we can probably ignore these until we support command-mode panels: */
static int omap_crtc_dss_connect(enum omap_channel channel,
		struct omap_dss_device *dst)
{
	if (omap_crtc_output[channel])
		return -EINVAL;

	if ((dispc_mgr_get_supported_outputs(channel) & dst->id) == 0)
		return -EINVAL;

	omap_crtc_output[channel] = dst;
	dst->dispc_channel_connected = true;

	return 0;
}

static void omap_crtc_dss_disconnect(enum omap_channel channel,
		struct omap_dss_device *dst)
{
	omap_crtc_output[channel] = NULL;
	dst->dispc_channel_connected = false;
}

static void omap_crtc_dss_start_update(enum omap_channel channel)
{
}

/* Called only from the encoder enable/disable and suspend/resume handlers. */
static void omap_crtc_set_enabled(struct drm_crtc *crtc, bool enable)
{
	struct drm_device *dev = crtc->dev;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	enum omap_channel channel = omap_crtc->channel;
	struct omap_irq_wait *wait;
	u32 framedone_irq, vsync_irq;
	int ret;

	if (WARN_ON(omap_crtc->enabled == enable))
		return;

	if (omap_crtc_output[channel]->output_type == OMAP_DISPLAY_TYPE_HDMI) {
		dispc_mgr_enable(channel, enable);
		omap_crtc->enabled = enable;
		return;
	}

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
	omap_crtc->enabled = enable;

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


static int omap_crtc_dss_enable(enum omap_channel channel)
{
	struct omap_crtc *omap_crtc = omap_crtcs[channel];
	struct omap_overlay_manager_info info;

	memset(&info, 0, sizeof(info));
	info.default_color = 0x00000000;
	info.trans_key = 0x00000000;
	info.trans_key_type = OMAP_DSS_COLOR_KEY_GFX_DST;
	info.trans_enabled = false;

	dispc_mgr_setup(omap_crtc->channel, &info);
	dispc_mgr_set_timings(omap_crtc->channel,
			&omap_crtc->vm);
	omap_crtc_set_enabled(&omap_crtc->base, true);

	return 0;
}

static void omap_crtc_dss_disable(enum omap_channel channel)
{
	struct omap_crtc *omap_crtc = omap_crtcs[channel];

	omap_crtc_set_enabled(&omap_crtc->base, false);
}

static void omap_crtc_dss_set_timings(enum omap_channel channel,
		const struct videomode *vm)
{
	struct omap_crtc *omap_crtc = omap_crtcs[channel];
	DBG("%s", omap_crtc->name);
	omap_crtc->vm = *vm;
}

static void omap_crtc_dss_set_lcd_config(enum omap_channel channel,
		const struct dss_lcd_mgr_config *config)
{
	struct omap_crtc *omap_crtc = omap_crtcs[channel];
	DBG("%s", omap_crtc->name);
	dispc_mgr_set_lcd_config(omap_crtc->channel, config);
}

static int omap_crtc_dss_register_framedone(
		enum omap_channel channel,
		void (*handler)(void *), void *data)
{
	return 0;
}

static void omap_crtc_dss_unregister_framedone(
		enum omap_channel channel,
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

void omap_crtc_error_irq(struct drm_crtc *crtc, uint32_t irqstatus)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);

	if (omap_crtc->ignore_digit_sync_lost) {
		irqstatus &= ~DISPC_IRQ_SYNC_LOST_DIGIT;
		if (!irqstatus)
			return;
	}

	DRM_ERROR_RATELIMITED("%s: errors: %08x\n", omap_crtc->name, irqstatus);
}

void omap_crtc_vblank_irq(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	bool pending;

	spin_lock(&crtc->dev->event_lock);
	/*
	 * If the dispc is busy we're racing the flush operation. Try again on
	 * the next vblank interrupt.
	 */
	if (dispc_mgr_go_busy(omap_crtc->channel)) {
		spin_unlock(&crtc->dev->event_lock);
		return;
	}

	/* Send the vblank event if one has been requested. */
	if (omap_crtc->event) {
		drm_crtc_send_vblank_event(crtc, omap_crtc->event);
		omap_crtc->event = NULL;
	}

	pending = omap_crtc->pending;
	omap_crtc->pending = false;
	spin_unlock(&crtc->dev->event_lock);

	if (pending)
		drm_crtc_vblank_put(crtc);

	/* Wake up omap_atomic_complete. */
	wake_up(&omap_crtc->pending_wait);

	DBG("%s: apply done", omap_crtc->name);
}

/* -----------------------------------------------------------------------------
 * CRTC Functions
 */

static void omap_crtc_destroy(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);

	DBG("%s", omap_crtc->name);

	drm_crtc_cleanup(crtc);

	kfree(omap_crtc);
}

static void omap_crtc_enable(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	int ret;

	DBG("%s", omap_crtc->name);

	spin_lock_irq(&crtc->dev->event_lock);
	drm_crtc_vblank_on(crtc);
	ret = drm_crtc_vblank_get(crtc);
	WARN_ON(ret != 0);

	WARN_ON(omap_crtc->pending);
	omap_crtc->pending = true;
	spin_unlock_irq(&crtc->dev->event_lock);
}

static void omap_crtc_disable(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);

	DBG("%s", omap_crtc->name);

	drm_crtc_vblank_off(crtc);
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

	drm_display_mode_to_videomode(mode, &omap_crtc->vm);
	omap_crtc->vm.flags |= DISPLAY_FLAGS_DE_HIGH |
			       DISPLAY_FLAGS_PIXDATA_POSEDGE |
			       DISPLAY_FLAGS_SYNC_NEGEDGE;
}

static int omap_crtc_atomic_check(struct drm_crtc *crtc,
				struct drm_crtc_state *state)
{
	if (state->color_mgmt_changed && state->gamma_lut) {
		uint length = state->gamma_lut->length /
			sizeof(struct drm_color_lut);

		if (length < 2)
			return -EINVAL;
	}

	return 0;
}

static void omap_crtc_atomic_begin(struct drm_crtc *crtc,
				   struct drm_crtc_state *old_crtc_state)
{
}

static void omap_crtc_atomic_flush(struct drm_crtc *crtc,
				   struct drm_crtc_state *old_crtc_state)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	int ret;

	if (crtc->state->color_mgmt_changed) {
		struct drm_color_lut *lut = NULL;
		uint length = 0;

		if (crtc->state->gamma_lut) {
			lut = (struct drm_color_lut *)
				crtc->state->gamma_lut->data;
			length = crtc->state->gamma_lut->length /
				sizeof(*lut);
		}
		dispc_mgr_set_gamma(omap_crtc->channel, lut, length);
	}

	/*
	 * Only flush the CRTC if it is currently enabled. CRTCs that require a
	 * mode set are disabled prior plane updates and enabled afterwards.
	 * They are thus not active (regardless of what their CRTC core state
	 * reports) and the DRM core could thus call this function even though
	 * the CRTC is currently disabled. Do nothing in that case.
	 */
	if (!omap_crtc->enabled)
		return;

	DBG("%s: GO", omap_crtc->name);

	ret = drm_crtc_vblank_get(crtc);
	WARN_ON(ret != 0);

	spin_lock_irq(&crtc->dev->event_lock);
	dispc_mgr_go(omap_crtc->channel);

	WARN_ON(omap_crtc->pending);
	omap_crtc->pending = true;

	if (crtc->state->event)
		omap_crtc->event = crtc->state->event;
	spin_unlock_irq(&crtc->dev->event_lock);
}

static bool omap_crtc_is_plane_prop(struct drm_crtc *crtc,
	struct drm_property *property)
{
	struct drm_device *dev = crtc->dev;
	struct omap_drm_private *priv = dev->dev_private;

	return property == priv->zorder_prop ||
		property == crtc->primary->rotation_property;
}

static int omap_crtc_atomic_set_property(struct drm_crtc *crtc,
					 struct drm_crtc_state *state,
					 struct drm_property *property,
					 uint64_t val)
{
	if (omap_crtc_is_plane_prop(crtc, property)) {
		struct drm_plane_state *plane_state;
		struct drm_plane *plane = crtc->primary;

		/*
		 * Delegate property set to the primary plane. Get the plane
		 * state and set the property directly.
		 */

		plane_state = drm_atomic_get_plane_state(state->state, plane);
		if (IS_ERR(plane_state))
			return PTR_ERR(plane_state);

		return drm_atomic_plane_set_property(plane, plane_state,
				property, val);
	}

	return -EINVAL;
}

static int omap_crtc_atomic_get_property(struct drm_crtc *crtc,
					 const struct drm_crtc_state *state,
					 struct drm_property *property,
					 uint64_t *val)
{
	if (omap_crtc_is_plane_prop(crtc, property)) {
		/*
		 * Delegate property get to the primary plane. The
		 * drm_atomic_plane_get_property() function isn't exported, but
		 * can be called through drm_object_property_get_value() as that
		 * will call drm_atomic_get_property() for atomic drivers.
		 */
		return drm_object_property_get_value(&crtc->primary->base,
				property, val);
	}

	return -EINVAL;
}

static const struct drm_crtc_funcs omap_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.set_config = drm_atomic_helper_set_config,
	.destroy = omap_crtc_destroy,
	.page_flip = drm_atomic_helper_page_flip,
	.gamma_set = drm_atomic_helper_legacy_gamma_set,
	.set_property = drm_atomic_helper_crtc_set_property,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.atomic_set_property = omap_crtc_atomic_set_property,
	.atomic_get_property = omap_crtc_atomic_get_property,
};

static const struct drm_crtc_helper_funcs omap_crtc_helper_funcs = {
	.mode_set_nofb = omap_crtc_mode_set_nofb,
	.disable = omap_crtc_disable,
	.enable = omap_crtc_enable,
	.atomic_check = omap_crtc_atomic_check,
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

	init_waitqueue_head(&omap_crtc->pending_wait);

	omap_crtc->channel = channel;
	omap_crtc->name = channel_names[channel];

	ret = drm_crtc_init_with_planes(dev, crtc, plane, NULL,
					&omap_crtc_funcs, NULL);
	if (ret < 0) {
		kfree(omap_crtc);
		return NULL;
	}

	drm_crtc_helper_add(crtc, &omap_crtc_helper_funcs);

	/* The dispc API adapts to what ever size, but the HW supports
	 * 256 element gamma table for LCDs and 1024 element table for
	 * OMAP_DSS_CHANNEL_DIGIT. X server assumes 256 element gamma
	 * tables so lets use that. Size of HW gamma table can be
	 * extracted with dispc_mgr_gamma_size(). If it returns 0
	 * gamma table is not supprted.
	 */
	if (dispc_mgr_gamma_size(channel)) {
		uint gamma_lut_size = 256;

		drm_crtc_enable_color_mgmt(crtc, 0, false, gamma_lut_size);
		drm_mode_crtc_set_gamma_size(crtc, gamma_lut_size);
	}

	omap_plane_install_properties(crtc->primary, &crtc->base);

	omap_crtcs[channel] = omap_crtc;

	return crtc;
}
