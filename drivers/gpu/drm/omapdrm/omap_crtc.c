// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Rob Clark <rob@ti.com>
 */

#include <linux/math64.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mode.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_vblank.h>

#include "omap_drv.h"

#define to_omap_crtc_state(x) container_of(x, struct omap_crtc_state, base)

struct omap_crtc_state {
	/* Must be first. */
	struct drm_crtc_state base;
	/* Shadow values for legacy userspace support. */
	unsigned int rotation;
	unsigned int zpos;
	bool manually_updated;
};

#define to_omap_crtc(x) container_of(x, struct omap_crtc, base)

struct omap_crtc {
	struct drm_crtc base;

	const char *name;
	struct omap_drm_pipeline *pipe;
	enum omap_channel channel;

	struct videomode vm;

	bool ignore_digit_sync_lost;

	bool enabled;
	bool pending;
	wait_queue_head_t pending_wait;
	struct drm_pending_vblank_event *event;
	struct delayed_work update_work;

	void (*framedone_handler)(void *);
	void *framedone_handler_data;
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
 */

static void omap_crtc_dss_start_update(struct omap_drm_private *priv,
				       enum omap_channel channel)
{
	priv->dispc_ops->mgr_enable(priv->dispc, channel, true);
}

/* Called only from the encoder enable/disable and suspend/resume handlers. */
static void omap_crtc_set_enabled(struct drm_crtc *crtc, bool enable)
{
	struct omap_crtc_state *omap_state = to_omap_crtc_state(crtc->state);
	struct drm_device *dev = crtc->dev;
	struct omap_drm_private *priv = dev->dev_private;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	enum omap_channel channel = omap_crtc->channel;
	struct omap_irq_wait *wait;
	u32 framedone_irq, vsync_irq;
	int ret;

	if (WARN_ON(omap_crtc->enabled == enable))
		return;

	if (omap_state->manually_updated) {
		omap_irq_enable_framedone(crtc, enable);
		omap_crtc->enabled = enable;
		return;
	}

	if (omap_crtc->pipe->output->type == OMAP_DISPLAY_TYPE_HDMI) {
		priv->dispc_ops->mgr_enable(priv->dispc, channel, enable);
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

	framedone_irq = priv->dispc_ops->mgr_get_framedone_irq(priv->dispc,
							       channel);
	vsync_irq = priv->dispc_ops->mgr_get_vsync_irq(priv->dispc, channel);

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

	priv->dispc_ops->mgr_enable(priv->dispc, channel, enable);
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


static int omap_crtc_dss_enable(struct omap_drm_private *priv,
				enum omap_channel channel)
{
	struct drm_crtc *crtc = priv->channels[channel]->crtc;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);

	priv->dispc_ops->mgr_set_timings(priv->dispc, omap_crtc->channel,
					 &omap_crtc->vm);
	omap_crtc_set_enabled(&omap_crtc->base, true);

	return 0;
}

static void omap_crtc_dss_disable(struct omap_drm_private *priv,
				  enum omap_channel channel)
{
	struct drm_crtc *crtc = priv->channels[channel]->crtc;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);

	omap_crtc_set_enabled(&omap_crtc->base, false);
}

static void omap_crtc_dss_set_timings(struct omap_drm_private *priv,
		enum omap_channel channel,
		const struct videomode *vm)
{
	struct drm_crtc *crtc = priv->channels[channel]->crtc;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);

	DBG("%s", omap_crtc->name);
	omap_crtc->vm = *vm;
}

static void omap_crtc_dss_set_lcd_config(struct omap_drm_private *priv,
		enum omap_channel channel,
		const struct dss_lcd_mgr_config *config)
{
	struct drm_crtc *crtc = priv->channels[channel]->crtc;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);

	DBG("%s", omap_crtc->name);
	priv->dispc_ops->mgr_set_lcd_config(priv->dispc, omap_crtc->channel,
					    config);
}

static int omap_crtc_dss_register_framedone(
		struct omap_drm_private *priv, enum omap_channel channel,
		void (*handler)(void *), void *data)
{
	struct drm_crtc *crtc = priv->channels[channel]->crtc;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct drm_device *dev = omap_crtc->base.dev;

	if (omap_crtc->framedone_handler)
		return -EBUSY;

	dev_dbg(dev->dev, "register framedone %s", omap_crtc->name);

	omap_crtc->framedone_handler = handler;
	omap_crtc->framedone_handler_data = data;

	return 0;
}

static void omap_crtc_dss_unregister_framedone(
		struct omap_drm_private *priv, enum omap_channel channel,
		void (*handler)(void *), void *data)
{
	struct drm_crtc *crtc = priv->channels[channel]->crtc;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct drm_device *dev = omap_crtc->base.dev;

	dev_dbg(dev->dev, "unregister framedone %s", omap_crtc->name);

	WARN_ON(omap_crtc->framedone_handler != handler);
	WARN_ON(omap_crtc->framedone_handler_data != data);

	omap_crtc->framedone_handler = NULL;
	omap_crtc->framedone_handler_data = NULL;
}

static const struct dss_mgr_ops mgr_ops = {
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

void omap_crtc_error_irq(struct drm_crtc *crtc, u32 irqstatus)
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
	struct drm_device *dev = omap_crtc->base.dev;
	struct omap_drm_private *priv = dev->dev_private;
	bool pending;

	spin_lock(&crtc->dev->event_lock);
	/*
	 * If the dispc is busy we're racing the flush operation. Try again on
	 * the next vblank interrupt.
	 */
	if (priv->dispc_ops->mgr_go_busy(priv->dispc, omap_crtc->channel)) {
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

void omap_crtc_framedone_irq(struct drm_crtc *crtc, uint32_t irqstatus)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);

	if (!omap_crtc->framedone_handler)
		return;

	omap_crtc->framedone_handler(omap_crtc->framedone_handler_data);

	spin_lock(&crtc->dev->event_lock);
	/* Send the vblank event if one has been requested. */
	if (omap_crtc->event) {
		drm_crtc_send_vblank_event(crtc, omap_crtc->event);
		omap_crtc->event = NULL;
	}
	omap_crtc->pending = false;
	spin_unlock(&crtc->dev->event_lock);

	/* Wake up omap_atomic_complete. */
	wake_up(&omap_crtc->pending_wait);
}

void omap_crtc_flush(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct omap_crtc_state *omap_state = to_omap_crtc_state(crtc->state);

	if (!omap_state->manually_updated)
		return;

	if (!delayed_work_pending(&omap_crtc->update_work))
		schedule_delayed_work(&omap_crtc->update_work, 0);
}

static void omap_crtc_manual_display_update(struct work_struct *data)
{
	struct omap_crtc *omap_crtc =
			container_of(data, struct omap_crtc, update_work.work);
	struct drm_display_mode *mode = &omap_crtc->pipe->crtc->mode;
	struct omap_dss_device *dssdev = omap_crtc->pipe->output->next;
	struct drm_device *dev = omap_crtc->base.dev;
	const struct omap_dss_driver *dssdrv;
	int ret;

	if (!dssdev) {
		dev_err_once(dev->dev, "missing display dssdev!");
		return;
	}

	dssdrv = dssdev->driver;
	if (!dssdrv || !dssdrv->update) {
		dev_err_once(dev->dev, "missing or incorrect dssdrv!");
		return;
	}

	if (dssdrv->sync)
		dssdrv->sync(dssdev);

	ret = dssdrv->update(dssdev, 0, 0, mode->hdisplay, mode->vdisplay);
	if (ret < 0) {
		spin_lock_irq(&dev->event_lock);
		omap_crtc->pending = false;
		spin_unlock_irq(&dev->event_lock);
		wake_up(&omap_crtc->pending_wait);
	}
}

static void omap_crtc_write_crtc_properties(struct drm_crtc *crtc)
{
	struct omap_drm_private *priv = crtc->dev->dev_private;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct omap_overlay_manager_info info;

	memset(&info, 0, sizeof(info));

	info.default_color = 0x000000;
	info.trans_enabled = false;
	info.partial_alpha_enabled = false;
	info.cpr_enable = false;

	priv->dispc_ops->mgr_setup(priv->dispc, omap_crtc->channel, &info);
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

static void omap_crtc_arm_event(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);

	WARN_ON(omap_crtc->pending);
	omap_crtc->pending = true;

	if (crtc->state->event) {
		omap_crtc->event = crtc->state->event;
		crtc->state->event = NULL;
	}
}

static void omap_crtc_atomic_enable(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_state)
{
	struct omap_drm_private *priv = crtc->dev->dev_private;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct omap_crtc_state *omap_state = to_omap_crtc_state(crtc->state);
	int ret;

	DBG("%s", omap_crtc->name);

	priv->dispc_ops->runtime_get(priv->dispc);

	/* manual updated display will not trigger vsync irq */
	if (omap_state->manually_updated)
		return;

	spin_lock_irq(&crtc->dev->event_lock);
	drm_crtc_vblank_on(crtc);
	ret = drm_crtc_vblank_get(crtc);
	WARN_ON(ret != 0);

	omap_crtc_arm_event(crtc);
	spin_unlock_irq(&crtc->dev->event_lock);
}

static void omap_crtc_atomic_disable(struct drm_crtc *crtc,
				     struct drm_crtc_state *old_state)
{
	struct omap_drm_private *priv = crtc->dev->dev_private;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct drm_device *dev = crtc->dev;

	DBG("%s", omap_crtc->name);

	spin_lock_irq(&crtc->dev->event_lock);
	if (crtc->state->event) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&crtc->dev->event_lock);

	cancel_delayed_work(&omap_crtc->update_work);

	if (!omap_crtc_wait_pending(crtc))
		dev_warn(dev->dev, "manual display update did not finish!");

	drm_crtc_vblank_off(crtc);

	priv->dispc_ops->runtime_put(priv->dispc);
}

static enum drm_mode_status omap_crtc_mode_valid(struct drm_crtc *crtc,
					const struct drm_display_mode *mode)
{
	struct omap_drm_private *priv = crtc->dev->dev_private;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct videomode vm = {0};
	int r;

	drm_display_mode_to_videomode(mode, &vm);

	/*
	 * DSI might not call this, since the supplied mode is not a
	 * valid DISPC mode. DSI will calculate and configure the
	 * proper DISPC mode later.
	 */
	if (omap_crtc->pipe->output->next == NULL ||
	    omap_crtc->pipe->output->next->type != OMAP_DISPLAY_TYPE_DSI) {
		r = priv->dispc_ops->mgr_check_timings(priv->dispc,
						       omap_crtc->channel,
						       &vm);
		if (r)
			return r;
	}

	/* Check for bandwidth limit */
	if (priv->max_bandwidth) {
		/*
		 * Estimation for the bandwidth need of a given mode with one
		 * full screen plane:
		 * bandwidth = resolution * 32bpp * (pclk / (vtotal * htotal))
		 *					^^ Refresh rate ^^
		 *
		 * The interlaced mode is taken into account by using the
		 * pixelclock in the calculation.
		 *
		 * The equation is rearranged for 64bit arithmetic.
		 */
		uint64_t bandwidth = mode->clock * 1000;
		unsigned int bpp = 4;

		bandwidth = bandwidth * mode->hdisplay * mode->vdisplay * bpp;
		bandwidth = div_u64(bandwidth, mode->htotal * mode->vtotal);

		/*
		 * Reject modes which would need more bandwidth if used with one
		 * full resolution plane (most common use case).
		 */
		if (priv->max_bandwidth < bandwidth)
			return MODE_BAD;
	}

	return MODE_OK;
}

static void omap_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;

	DBG("%s: set mode: " DRM_MODE_FMT,
	    omap_crtc->name, DRM_MODE_ARG(mode));

	drm_display_mode_to_videomode(mode, &omap_crtc->vm);
}

static bool omap_crtc_is_manually_updated(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct omap_dss_device *display = omap_crtc->pipe->output->next;

	if (!display)
		return false;

	if (display->caps & OMAP_DSS_DISPLAY_CAP_MANUAL_UPDATE) {
		DBG("detected manually updated display!");
		return true;
	}

	return false;
}

static int omap_crtc_atomic_check(struct drm_crtc *crtc,
				struct drm_crtc_state *state)
{
	struct drm_plane_state *pri_state;

	if (state->color_mgmt_changed && state->gamma_lut) {
		unsigned int length = state->gamma_lut->length /
			sizeof(struct drm_color_lut);

		if (length < 2)
			return -EINVAL;
	}

	pri_state = drm_atomic_get_new_plane_state(state->state, crtc->primary);
	if (pri_state) {
		struct omap_crtc_state *omap_crtc_state =
			to_omap_crtc_state(state);

		/* Mirror new values for zpos and rotation in omap_crtc_state */
		omap_crtc_state->zpos = pri_state->zpos;
		omap_crtc_state->rotation = pri_state->rotation;

		/* Check if this CRTC is for a manually updated display */
		omap_crtc_state->manually_updated = omap_crtc_is_manually_updated(crtc);
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
	struct omap_drm_private *priv = crtc->dev->dev_private;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct omap_crtc_state *omap_crtc_state = to_omap_crtc_state(crtc->state);
	int ret;

	if (crtc->state->color_mgmt_changed) {
		struct drm_color_lut *lut = NULL;
		unsigned int length = 0;

		if (crtc->state->gamma_lut) {
			lut = (struct drm_color_lut *)
				crtc->state->gamma_lut->data;
			length = crtc->state->gamma_lut->length /
				sizeof(*lut);
		}
		priv->dispc_ops->mgr_set_gamma(priv->dispc, omap_crtc->channel,
					       lut, length);
	}

	omap_crtc_write_crtc_properties(crtc);

	/* Only flush the CRTC if it is currently enabled. */
	if (!omap_crtc->enabled)
		return;

	DBG("%s: GO", omap_crtc->name);

	if (omap_crtc_state->manually_updated) {
		/* send new image for page flips and modeset changes */
		spin_lock_irq(&crtc->dev->event_lock);
		omap_crtc_flush(crtc);
		omap_crtc_arm_event(crtc);
		spin_unlock_irq(&crtc->dev->event_lock);
		return;
	}

	ret = drm_crtc_vblank_get(crtc);
	WARN_ON(ret != 0);

	spin_lock_irq(&crtc->dev->event_lock);
	priv->dispc_ops->mgr_go(priv->dispc, omap_crtc->channel);
	omap_crtc_arm_event(crtc);
	spin_unlock_irq(&crtc->dev->event_lock);
}

static int omap_crtc_atomic_set_property(struct drm_crtc *crtc,
					 struct drm_crtc_state *state,
					 struct drm_property *property,
					 u64 val)
{
	struct omap_drm_private *priv = crtc->dev->dev_private;
	struct drm_plane_state *plane_state;

	/*
	 * Delegate property set to the primary plane. Get the plane state and
	 * set the property directly, the shadow copy will be assigned in the
	 * omap_crtc_atomic_check callback. This way updates to plane state will
	 * always be mirrored in the crtc state correctly.
	 */
	plane_state = drm_atomic_get_plane_state(state->state, crtc->primary);
	if (IS_ERR(plane_state))
		return PTR_ERR(plane_state);

	if (property == crtc->primary->rotation_property)
		plane_state->rotation = val;
	else if (property == priv->zorder_prop)
		plane_state->zpos = val;
	else
		return -EINVAL;

	return 0;
}

static int omap_crtc_atomic_get_property(struct drm_crtc *crtc,
					 const struct drm_crtc_state *state,
					 struct drm_property *property,
					 u64 *val)
{
	struct omap_drm_private *priv = crtc->dev->dev_private;
	struct omap_crtc_state *omap_state = to_omap_crtc_state(state);

	if (property == crtc->primary->rotation_property)
		*val = omap_state->rotation;
	else if (property == priv->zorder_prop)
		*val = omap_state->zpos;
	else
		return -EINVAL;

	return 0;
}

static void omap_crtc_reset(struct drm_crtc *crtc)
{
	if (crtc->state)
		__drm_atomic_helper_crtc_destroy_state(crtc->state);

	kfree(crtc->state);
	crtc->state = kzalloc(sizeof(struct omap_crtc_state), GFP_KERNEL);

	if (crtc->state)
		crtc->state->crtc = crtc;
}

static struct drm_crtc_state *
omap_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct omap_crtc_state *state, *current_state;

	if (WARN_ON(!crtc->state))
		return NULL;

	current_state = to_omap_crtc_state(crtc->state);

	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &state->base);

	state->zpos = current_state->zpos;
	state->rotation = current_state->rotation;
	state->manually_updated = current_state->manually_updated;

	return &state->base;
}

static const struct drm_crtc_funcs omap_crtc_funcs = {
	.reset = omap_crtc_reset,
	.set_config = drm_atomic_helper_set_config,
	.destroy = omap_crtc_destroy,
	.page_flip = drm_atomic_helper_page_flip,
	.gamma_set = drm_atomic_helper_legacy_gamma_set,
	.atomic_duplicate_state = omap_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.atomic_set_property = omap_crtc_atomic_set_property,
	.atomic_get_property = omap_crtc_atomic_get_property,
	.enable_vblank = omap_irq_enable_vblank,
	.disable_vblank = omap_irq_disable_vblank,
};

static const struct drm_crtc_helper_funcs omap_crtc_helper_funcs = {
	.mode_set_nofb = omap_crtc_mode_set_nofb,
	.atomic_check = omap_crtc_atomic_check,
	.atomic_begin = omap_crtc_atomic_begin,
	.atomic_flush = omap_crtc_atomic_flush,
	.atomic_enable = omap_crtc_atomic_enable,
	.atomic_disable = omap_crtc_atomic_disable,
	.mode_valid = omap_crtc_mode_valid,
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

void omap_crtc_pre_init(struct omap_drm_private *priv)
{
	dss_install_mgr_ops(priv->dss, &mgr_ops, priv);
}

void omap_crtc_pre_uninit(struct omap_drm_private *priv)
{
	dss_uninstall_mgr_ops(priv->dss);
}

/* initialize crtc */
struct drm_crtc *omap_crtc_init(struct drm_device *dev,
				struct omap_drm_pipeline *pipe,
				struct drm_plane *plane)
{
	struct omap_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc = NULL;
	struct omap_crtc *omap_crtc;
	enum omap_channel channel;
	int ret;

	channel = pipe->output->dispc_channel;

	DBG("%s", channel_names[channel]);

	omap_crtc = kzalloc(sizeof(*omap_crtc), GFP_KERNEL);
	if (!omap_crtc)
		return ERR_PTR(-ENOMEM);

	crtc = &omap_crtc->base;

	init_waitqueue_head(&omap_crtc->pending_wait);

	omap_crtc->pipe = pipe;
	omap_crtc->channel = channel;
	omap_crtc->name = channel_names[channel];

	/*
	 * We want to refresh manually updated displays from dirty callback,
	 * which is called quite often (e.g. for each drawn line). This will
	 * be used to do the display update asynchronously to avoid blocking
	 * the rendering process and merges multiple dirty calls into one
	 * update if they arrive very fast. We also call this function for
	 * atomic display updates (e.g. for page flips), which means we do
	 * not need extra locking. Atomic updates should be synchronous, but
	 * need to wait for the framedone interrupt anyways.
	 */
	INIT_DELAYED_WORK(&omap_crtc->update_work,
			  omap_crtc_manual_display_update);

	ret = drm_crtc_init_with_planes(dev, crtc, plane, NULL,
					&omap_crtc_funcs, NULL);
	if (ret < 0) {
		dev_err(dev->dev, "%s(): could not init crtc for: %s\n",
			__func__, pipe->output->name);
		kfree(omap_crtc);
		return ERR_PTR(ret);
	}

	drm_crtc_helper_add(crtc, &omap_crtc_helper_funcs);

	/* The dispc API adapts to what ever size, but the HW supports
	 * 256 element gamma table for LCDs and 1024 element table for
	 * OMAP_DSS_CHANNEL_DIGIT. X server assumes 256 element gamma
	 * tables so lets use that. Size of HW gamma table can be
	 * extracted with dispc_mgr_gamma_size(). If it returns 0
	 * gamma table is not supported.
	 */
	if (priv->dispc_ops->mgr_gamma_size(priv->dispc, channel)) {
		unsigned int gamma_lut_size = 256;

		drm_crtc_enable_color_mgmt(crtc, 0, false, gamma_lut_size);
		drm_mode_crtc_set_gamma_size(crtc, gamma_lut_size);
	}

	omap_plane_install_properties(crtc->primary, &crtc->base);

	return crtc;
}
