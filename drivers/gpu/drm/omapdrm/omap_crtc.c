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

#include "omap_drv.h"

#include <drm/drm_mode.h>
#include <drm/drm_plane_helper.h>
#include "drm_crtc.h"
#include "drm_crtc_helper.h"

#define to_omap_crtc(x) container_of(x, struct omap_crtc, base)

struct omap_crtc {
	struct drm_crtc base;

	const char *name;
	int pipe;
	enum omap_channel channel;
	struct omap_overlay_manager_info info;
	struct drm_encoder *current_encoder;

	/*
	 * Temporary: eventually this will go away, but it is needed
	 * for now to keep the output's happy.  (They only need
	 * mgr->id.)  Eventually this will be replaced w/ something
	 * more common-panel-framework-y
	 */
	struct omap_overlay_manager *mgr;

	struct omap_video_timings timings;
	bool enabled;

	struct omap_drm_apply apply;

	struct omap_drm_irq apply_irq;
	struct omap_drm_irq error_irq;

	/* list of in-progress apply's: */
	struct list_head pending_applies;

	/* list of queued apply's: */
	struct list_head queued_applies;

	/* for handling queued and in-progress applies: */
	struct work_struct apply_work;

	/* if there is a pending flip, these will be non-null: */
	struct drm_pending_vblank_event *event;
	struct drm_framebuffer *old_fb;

	/* for handling page flips without caring about what
	 * the callback is called from.  Possibly we should just
	 * make omap_gem always call the cb from the worker so
	 * we don't have to care about this..
	 *
	 * XXX maybe fold into apply_work??
	 */
	struct work_struct page_flip_work;
};

uint32_t pipe2vbl(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);

	return dispc_mgr_get_vsync_irq(omap_crtc->channel);
}

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
static int omap_crtc_connect(struct omap_overlay_manager *mgr,
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

static void omap_crtc_disconnect(struct omap_overlay_manager *mgr,
		struct omap_dss_device *dst)
{
	mgr->output->manager = NULL;
	mgr->output = NULL;
}

static void omap_crtc_start_update(struct omap_overlay_manager *mgr)
{
}

static void set_enabled(struct drm_crtc *crtc, bool enable);

static int omap_crtc_enable(struct omap_overlay_manager *mgr)
{
	struct omap_crtc *omap_crtc = omap_crtcs[mgr->id];

	dispc_mgr_setup(omap_crtc->channel, &omap_crtc->info);
	dispc_mgr_set_timings(omap_crtc->channel,
			&omap_crtc->timings);
	set_enabled(&omap_crtc->base, true);

	return 0;
}

static void omap_crtc_disable(struct omap_overlay_manager *mgr)
{
	struct omap_crtc *omap_crtc = omap_crtcs[mgr->id];

	set_enabled(&omap_crtc->base, false);
}

static void omap_crtc_set_timings(struct omap_overlay_manager *mgr,
		const struct omap_video_timings *timings)
{
	struct omap_crtc *omap_crtc = omap_crtcs[mgr->id];
	DBG("%s", omap_crtc->name);
	omap_crtc->timings = *timings;
}

static void omap_crtc_set_lcd_config(struct omap_overlay_manager *mgr,
		const struct dss_lcd_mgr_config *config)
{
	struct omap_crtc *omap_crtc = omap_crtcs[mgr->id];
	DBG("%s", omap_crtc->name);
	dispc_mgr_set_lcd_config(omap_crtc->channel, config);
}

static int omap_crtc_register_framedone_handler(
		struct omap_overlay_manager *mgr,
		void (*handler)(void *), void *data)
{
	return 0;
}

static void omap_crtc_unregister_framedone_handler(
		struct omap_overlay_manager *mgr,
		void (*handler)(void *), void *data)
{
}

static const struct dss_mgr_ops mgr_ops = {
	.connect = omap_crtc_connect,
	.disconnect = omap_crtc_disconnect,
	.start_update = omap_crtc_start_update,
	.enable = omap_crtc_enable,
	.disable = omap_crtc_disable,
	.set_timings = omap_crtc_set_timings,
	.set_lcd_config = omap_crtc_set_lcd_config,
	.register_framedone_handler = omap_crtc_register_framedone_handler,
	.unregister_framedone_handler = omap_crtc_unregister_framedone_handler,
};

/*
 * CRTC funcs:
 */

static void omap_crtc_destroy(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);

	DBG("%s", omap_crtc->name);

	WARN_ON(omap_crtc->apply_irq.registered);
	omap_irq_unregister(crtc->dev, &omap_crtc->error_irq);

	drm_crtc_cleanup(crtc);

	kfree(omap_crtc);
}

static void omap_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct omap_drm_private *priv = crtc->dev->dev_private;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	bool enabled = (mode == DRM_MODE_DPMS_ON);
	int i;

	DBG("%s: %d", omap_crtc->name, mode);

	if (enabled != omap_crtc->enabled) {
		omap_crtc->enabled = enabled;
		omap_crtc_apply(crtc, &omap_crtc->apply);

		/* Enable/disable all planes associated with the CRTC. */
		for (i = 0; i < priv->num_planes; i++) {
			struct drm_plane *plane = priv->planes[i];
			if (plane->crtc == crtc)
				WARN_ON(omap_plane_set_enable(plane, enabled));
		}
	}
}

static bool omap_crtc_mode_fixup(struct drm_crtc *crtc,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	return true;
}

static int omap_crtc_mode_set(struct drm_crtc *crtc,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode,
		int x, int y,
		struct drm_framebuffer *old_fb)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);

	mode = adjusted_mode;

	DBG("%s: set mode: %d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x",
			omap_crtc->name, mode->base.id, mode->name,
			mode->vrefresh, mode->clock,
			mode->hdisplay, mode->hsync_start,
			mode->hsync_end, mode->htotal,
			mode->vdisplay, mode->vsync_start,
			mode->vsync_end, mode->vtotal,
			mode->type, mode->flags);

	copy_timings_drm_to_omap(&omap_crtc->timings, mode);

	/*
	 * The primary plane CRTC can be reset if the plane is disabled directly
	 * through the universal plane API. Set it again here.
	 */
	crtc->primary->crtc = crtc;

	return omap_plane_mode_set(crtc->primary, crtc, crtc->primary->fb,
			0, 0, mode->hdisplay, mode->vdisplay,
			x << 16, y << 16,
			mode->hdisplay << 16, mode->vdisplay << 16,
			NULL, NULL);
}

static void omap_crtc_prepare(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	DBG("%s", omap_crtc->name);
	omap_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void omap_crtc_commit(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	DBG("%s", omap_crtc->name);
	omap_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
}

static int omap_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
		struct drm_framebuffer *old_fb)
{
	struct drm_plane *plane = crtc->primary;
	struct drm_display_mode *mode = &crtc->mode;

	return omap_plane_mode_set(plane, crtc, crtc->primary->fb,
			0, 0, mode->hdisplay, mode->vdisplay,
			x << 16, y << 16,
			mode->hdisplay << 16, mode->vdisplay << 16,
			NULL, NULL);
}

static void vblank_cb(void *arg)
{
	struct drm_crtc *crtc = arg;
	struct drm_device *dev = crtc->dev;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);

	/* wakeup userspace */
	if (omap_crtc->event)
		drm_send_vblank_event(dev, omap_crtc->pipe, omap_crtc->event);

	omap_crtc->event = NULL;
	omap_crtc->old_fb = NULL;

	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static void page_flip_worker(struct work_struct *work)
{
	struct omap_crtc *omap_crtc =
			container_of(work, struct omap_crtc, page_flip_work);
	struct drm_crtc *crtc = &omap_crtc->base;
	struct drm_display_mode *mode = &crtc->mode;
	struct drm_gem_object *bo;

	drm_modeset_lock(&crtc->mutex, NULL);
	omap_plane_mode_set(crtc->primary, crtc, crtc->primary->fb,
			0, 0, mode->hdisplay, mode->vdisplay,
			crtc->x << 16, crtc->y << 16,
			mode->hdisplay << 16, mode->vdisplay << 16,
			vblank_cb, crtc);
	drm_modeset_unlock(&crtc->mutex);

	bo = omap_framebuffer_bo(crtc->primary->fb, 0);
	drm_gem_object_unreference_unlocked(bo);
}

static void page_flip_cb(void *arg)
{
	struct drm_crtc *crtc = arg;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct omap_drm_private *priv = crtc->dev->dev_private;

	/* avoid assumptions about what ctxt we are called from: */
	queue_work(priv->wq, &omap_crtc->page_flip_work);
}

static int omap_crtc_page_flip_locked(struct drm_crtc *crtc,
		 struct drm_framebuffer *fb,
		 struct drm_pending_vblank_event *event,
		 uint32_t page_flip_flags)
{
	struct drm_device *dev = crtc->dev;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	struct drm_plane *primary = crtc->primary;
	struct drm_gem_object *bo;
	unsigned long flags;

	DBG("%d -> %d (event=%p)", primary->fb ? primary->fb->base.id : -1,
			fb->base.id, event);

	spin_lock_irqsave(&dev->event_lock, flags);

	if (omap_crtc->old_fb) {
		spin_unlock_irqrestore(&dev->event_lock, flags);
		dev_err(dev->dev, "already a pending flip\n");
		return -EINVAL;
	}

	omap_crtc->event = event;
	omap_crtc->old_fb = primary->fb = fb;

	spin_unlock_irqrestore(&dev->event_lock, flags);

	/*
	 * Hold a reference temporarily until the crtc is updated
	 * and takes the reference to the bo.  This avoids it
	 * getting freed from under us:
	 */
	bo = omap_framebuffer_bo(fb, 0);
	drm_gem_object_reference(bo);

	omap_gem_op_async(bo, OMAP_GEM_READ, page_flip_cb, crtc);

	return 0;
}

static int omap_crtc_set_property(struct drm_crtc *crtc,
		struct drm_property *property, uint64_t val)
{
	struct omap_drm_private *priv = crtc->dev->dev_private;

	if (property == priv->rotation_prop) {
		crtc->invert_dimensions =
				!!(val & ((1LL << DRM_ROTATE_90) | (1LL << DRM_ROTATE_270)));
	}

	return omap_plane_set_property(crtc->primary, property, val);
}

static const struct drm_crtc_funcs omap_crtc_funcs = {
	.set_config = drm_crtc_helper_set_config,
	.destroy = omap_crtc_destroy,
	.page_flip = omap_crtc_page_flip_locked,
	.set_property = omap_crtc_set_property,
};

static const struct drm_crtc_helper_funcs omap_crtc_helper_funcs = {
	.dpms = omap_crtc_dpms,
	.mode_fixup = omap_crtc_mode_fixup,
	.mode_set = omap_crtc_mode_set,
	.prepare = omap_crtc_prepare,
	.commit = omap_crtc_commit,
	.mode_set_base = omap_crtc_mode_set_base,
};

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

static void omap_crtc_error_irq(struct omap_drm_irq *irq, uint32_t irqstatus)
{
	struct omap_crtc *omap_crtc =
			container_of(irq, struct omap_crtc, error_irq);
	struct drm_crtc *crtc = &omap_crtc->base;
	DRM_ERROR("%s: errors: %08x\n", omap_crtc->name, irqstatus);
	/* avoid getting in a flood, unregister the irq until next vblank */
	__omap_irq_unregister(crtc->dev, &omap_crtc->error_irq);
}

static void omap_crtc_apply_irq(struct omap_drm_irq *irq, uint32_t irqstatus)
{
	struct omap_crtc *omap_crtc =
			container_of(irq, struct omap_crtc, apply_irq);
	struct drm_crtc *crtc = &omap_crtc->base;

	if (!omap_crtc->error_irq.registered)
		__omap_irq_register(crtc->dev, &omap_crtc->error_irq);

	if (!dispc_mgr_go_busy(omap_crtc->channel)) {
		struct omap_drm_private *priv =
				crtc->dev->dev_private;
		DBG("%s: apply done", omap_crtc->name);
		__omap_irq_unregister(crtc->dev, &omap_crtc->apply_irq);
		queue_work(priv->wq, &omap_crtc->apply_work);
	}
}

static void apply_worker(struct work_struct *work)
{
	struct omap_crtc *omap_crtc =
			container_of(work, struct omap_crtc, apply_work);
	struct drm_crtc *crtc = &omap_crtc->base;
	struct drm_device *dev = crtc->dev;
	struct omap_drm_apply *apply, *n;
	bool need_apply;

	/*
	 * Synchronize everything on mode_config.mutex, to keep
	 * the callbacks and list modification all serialized
	 * with respect to modesetting ioctls from userspace.
	 */
	drm_modeset_lock(&crtc->mutex, NULL);
	dispc_runtime_get();

	/*
	 * If we are still pending a previous update, wait.. when the
	 * pending update completes, we get kicked again.
	 */
	if (omap_crtc->apply_irq.registered)
		goto out;

	/* finish up previous apply's: */
	list_for_each_entry_safe(apply, n,
			&omap_crtc->pending_applies, pending_node) {
		apply->post_apply(apply);
		list_del(&apply->pending_node);
	}

	need_apply = !list_empty(&omap_crtc->queued_applies);

	/* then handle the next round of of queued apply's: */
	list_for_each_entry_safe(apply, n,
			&omap_crtc->queued_applies, queued_node) {
		apply->pre_apply(apply);
		list_del(&apply->queued_node);
		apply->queued = false;
		list_add_tail(&apply->pending_node,
				&omap_crtc->pending_applies);
	}

	if (need_apply) {
		enum omap_channel channel = omap_crtc->channel;

		DBG("%s: GO", omap_crtc->name);

		if (dispc_mgr_is_enabled(channel)) {
			dispc_mgr_go(channel);
			omap_irq_register(dev, &omap_crtc->apply_irq);
		} else {
			struct omap_drm_private *priv = dev->dev_private;
			queue_work(priv->wq, &omap_crtc->apply_work);
		}
	}

out:
	dispc_runtime_put();
	drm_modeset_unlock(&crtc->mutex);
}

int omap_crtc_apply(struct drm_crtc *crtc,
		struct omap_drm_apply *apply)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);

	WARN_ON(!drm_modeset_is_locked(&crtc->mutex));

	/* no need to queue it again if it is already queued: */
	if (apply->queued)
		return 0;

	apply->queued = true;
	list_add_tail(&apply->queued_node, &omap_crtc->queued_applies);

	/*
	 * If there are no currently pending updates, then go ahead and
	 * kick the worker immediately, otherwise it will run again when
	 * the current update finishes.
	 */
	if (list_empty(&omap_crtc->pending_applies)) {
		struct omap_drm_private *priv = crtc->dev->dev_private;
		queue_work(priv->wq, &omap_crtc->apply_work);
	}

	return 0;
}

/* called only from apply */
static void set_enabled(struct drm_crtc *crtc, bool enable)
{
	struct drm_device *dev = crtc->dev;
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	enum omap_channel channel = omap_crtc->channel;
	struct omap_irq_wait *wait;
	u32 framedone_irq, vsync_irq;
	int ret;

	if (dispc_mgr_is_enabled(channel) == enable)
		return;

	/*
	 * Digit output produces some sync lost interrupts during the first
	 * frame when enabling, so we need to ignore those.
	 */
	omap_irq_unregister(crtc->dev, &omap_crtc->error_irq);

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

	omap_irq_register(crtc->dev, &omap_crtc->error_irq);
}

static void omap_crtc_pre_apply(struct omap_drm_apply *apply)
{
	struct omap_crtc *omap_crtc =
			container_of(apply, struct omap_crtc, apply);
	struct drm_crtc *crtc = &omap_crtc->base;
	struct omap_drm_private *priv = crtc->dev->dev_private;
	struct drm_encoder *encoder = NULL;
	unsigned int i;

	DBG("%s: enabled=%d", omap_crtc->name, omap_crtc->enabled);

	for (i = 0; i < priv->num_encoders; i++) {
		if (priv->encoders[i]->crtc == crtc) {
			encoder = priv->encoders[i];
			break;
		}
	}

	if (omap_crtc->current_encoder && encoder != omap_crtc->current_encoder)
		omap_encoder_set_enabled(omap_crtc->current_encoder, false);

	omap_crtc->current_encoder = encoder;

	if (!omap_crtc->enabled) {
		if (encoder)
			omap_encoder_set_enabled(encoder, false);
	} else {
		if (encoder) {
			omap_encoder_set_enabled(encoder, false);
			omap_encoder_update(encoder, omap_crtc->mgr,
					&omap_crtc->timings);
			omap_encoder_set_enabled(encoder, true);
		}
	}
}

static void omap_crtc_post_apply(struct omap_drm_apply *apply)
{
	/* nothing needed for post-apply */
}

void omap_crtc_flush(struct drm_crtc *crtc)
{
	struct omap_crtc *omap_crtc = to_omap_crtc(crtc);
	int loops = 0;

	while (!list_empty(&omap_crtc->pending_applies) ||
		!list_empty(&omap_crtc->queued_applies) ||
		omap_crtc->event || omap_crtc->old_fb) {

		if (++loops > 10) {
			dev_err(crtc->dev->dev,
				"omap_crtc_flush() timeout\n");
			break;
		}

		schedule_timeout_uninterruptible(msecs_to_jiffies(20));
	}
}

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
	struct omap_overlay_manager_info *info;
	int ret;

	DBG("%s", channel_names[channel]);

	omap_crtc = kzalloc(sizeof(*omap_crtc), GFP_KERNEL);
	if (!omap_crtc)
		return NULL;

	crtc = &omap_crtc->base;

	INIT_WORK(&omap_crtc->page_flip_work, page_flip_worker);
	INIT_WORK(&omap_crtc->apply_work, apply_worker);

	INIT_LIST_HEAD(&omap_crtc->pending_applies);
	INIT_LIST_HEAD(&omap_crtc->queued_applies);

	omap_crtc->apply.pre_apply  = omap_crtc_pre_apply;
	omap_crtc->apply.post_apply = omap_crtc_post_apply;

	omap_crtc->channel = channel;
	omap_crtc->name = channel_names[channel];
	omap_crtc->pipe = id;

	omap_crtc->apply_irq.irqmask = pipe2vbl(crtc);
	omap_crtc->apply_irq.irq = omap_crtc_apply_irq;

	omap_crtc->error_irq.irqmask =
			dispc_mgr_get_sync_lost_irq(channel);
	omap_crtc->error_irq.irq = omap_crtc_error_irq;
	omap_irq_register(dev, &omap_crtc->error_irq);

	/* temporary: */
	omap_crtc->mgr = omap_dss_get_overlay_manager(channel);

	/* TODO: fix hard-coded setup.. add properties! */
	info = &omap_crtc->info;
	info->default_color = 0x00000000;
	info->trans_key = 0x00000000;
	info->trans_key_type = OMAP_DSS_COLOR_KEY_GFX_DST;
	info->trans_enabled = false;

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
