/*
 * drivers/gpu/drm/omapdrm/omap_plane.c
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob.clark@linaro.org>
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

#include "drm_flip_work.h"

#include "omap_drv.h"
#include "omap_dmm_tiler.h"

/* some hackery because omapdss has an 'enum omap_plane' (which would be
 * better named omap_plane_id).. and compiler seems unhappy about having
 * both a 'struct omap_plane' and 'enum omap_plane'
 */
#define omap_plane _omap_plane

/*
 * plane funcs
 */

struct callback {
	void (*fxn)(void *);
	void *arg;
};

#define to_omap_plane(x) container_of(x, struct omap_plane, base)

struct omap_plane {
	struct drm_plane base;
	int id;  /* TODO rename omap_plane -> omap_plane_id in omapdss so I can use the enum */
	const char *name;
	struct omap_overlay_info info;
	struct omap_drm_apply apply;

	/* position/orientation of scanout within the fb: */
	struct omap_drm_window win;
	bool enabled;

	/* last fb that we pinned: */
	struct drm_framebuffer *pinned_fb;

	uint32_t nformats;
	uint32_t formats[32];

	struct omap_drm_irq error_irq;

	/* for deferring bo unpin's until next post_apply(): */
	struct drm_flip_work unpin_work;

	// XXX maybe get rid of this and handle vblank in crtc too?
	struct callback apply_done_cb;
};

static void unpin_worker(struct drm_flip_work *work, void *val)
{
	struct omap_plane *omap_plane =
			container_of(work, struct omap_plane, unpin_work);
	struct drm_device *dev = omap_plane->base.dev;

	omap_framebuffer_unpin(val);
	mutex_lock(&dev->mode_config.mutex);
	drm_framebuffer_unreference(val);
	mutex_unlock(&dev->mode_config.mutex);
}

/* update which fb (if any) is pinned for scanout */
static int update_pin(struct drm_plane *plane, struct drm_framebuffer *fb)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);
	struct drm_framebuffer *pinned_fb = omap_plane->pinned_fb;

	if (pinned_fb != fb) {
		int ret = 0;

		DBG("%p -> %p", pinned_fb, fb);

		if (fb) {
			drm_framebuffer_reference(fb);
			ret = omap_framebuffer_pin(fb);
		}

		if (pinned_fb)
			drm_flip_work_queue(&omap_plane->unpin_work, pinned_fb);

		if (ret) {
			dev_err(plane->dev->dev, "could not swap %p -> %p\n",
					omap_plane->pinned_fb, fb);
			drm_framebuffer_unreference(fb);
			omap_plane->pinned_fb = NULL;
			return ret;
		}

		omap_plane->pinned_fb = fb;
	}

	return 0;
}

static void omap_plane_pre_apply(struct omap_drm_apply *apply)
{
	struct omap_plane *omap_plane =
			container_of(apply, struct omap_plane, apply);
	struct omap_drm_window *win = &omap_plane->win;
	struct drm_plane *plane = &omap_plane->base;
	struct drm_device *dev = plane->dev;
	struct omap_overlay_info *info = &omap_plane->info;
	struct drm_crtc *crtc = plane->crtc;
	enum omap_channel channel;
	bool enabled = omap_plane->enabled && crtc;
	bool ilace, replication;
	int ret;

	DBG("%s, enabled=%d", omap_plane->name, enabled);

	/* if fb has changed, pin new fb: */
	update_pin(plane, enabled ? plane->fb : NULL);

	if (!enabled) {
		dispc_ovl_enable(omap_plane->id, false);
		return;
	}

	channel = omap_crtc_channel(crtc);

	/* update scanout: */
	omap_framebuffer_update_scanout(plane->fb, win, info);

	DBG("%dx%d -> %dx%d (%d)", info->width, info->height,
			info->out_width, info->out_height,
			info->screen_width);
	DBG("%d,%d %pad %pad", info->pos_x, info->pos_y,
			&info->paddr, &info->p_uv_addr);

	/* TODO: */
	ilace = false;
	replication = false;

	/* and finally, update omapdss: */
	ret = dispc_ovl_setup(omap_plane->id, info,
			replication, omap_crtc_timings(crtc), false);
	if (ret) {
		dev_err(dev->dev, "dispc_ovl_setup failed: %d\n", ret);
		return;
	}

	dispc_ovl_enable(omap_plane->id, true);
	dispc_ovl_set_channel_out(omap_plane->id, channel);
}

static void omap_plane_post_apply(struct omap_drm_apply *apply)
{
	struct omap_plane *omap_plane =
			container_of(apply, struct omap_plane, apply);
	struct drm_plane *plane = &omap_plane->base;
	struct omap_drm_private *priv = plane->dev->dev_private;
	struct omap_overlay_info *info = &omap_plane->info;
	struct callback cb;

	cb = omap_plane->apply_done_cb;
	omap_plane->apply_done_cb.fxn = NULL;

	drm_flip_work_commit(&omap_plane->unpin_work, priv->wq);

	if (cb.fxn)
		cb.fxn(cb.arg);

	if (omap_plane->enabled) {
		omap_framebuffer_flush(plane->fb, info->pos_x, info->pos_y,
				info->out_width, info->out_height);
	}
}

static int apply(struct drm_plane *plane)
{
	if (plane->crtc) {
		struct omap_plane *omap_plane = to_omap_plane(plane);
		return omap_crtc_apply(plane->crtc, &omap_plane->apply);
	}
	return 0;
}

int omap_plane_mode_set(struct drm_plane *plane,
		struct drm_crtc *crtc, struct drm_framebuffer *fb,
		int crtc_x, int crtc_y,
		unsigned int crtc_w, unsigned int crtc_h,
		uint32_t src_x, uint32_t src_y,
		uint32_t src_w, uint32_t src_h,
		void (*fxn)(void *), void *arg)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);
	struct omap_drm_window *win = &omap_plane->win;

	win->crtc_x = crtc_x;
	win->crtc_y = crtc_y;
	win->crtc_w = crtc_w;
	win->crtc_h = crtc_h;

	/* src values are in Q16 fixed point, convert to integer: */
	win->src_x = src_x >> 16;
	win->src_y = src_y >> 16;
	win->src_w = src_w >> 16;
	win->src_h = src_h >> 16;

	if (fxn) {
		/* omap_crtc should ensure that a new page flip
		 * isn't permitted while there is one pending:
		 */
		BUG_ON(omap_plane->apply_done_cb.fxn);

		omap_plane->apply_done_cb.fxn = fxn;
		omap_plane->apply_done_cb.arg = arg;
	}

	if (plane->fb)
		drm_framebuffer_unreference(plane->fb);

	drm_framebuffer_reference(fb);

	plane->fb = fb;
	plane->crtc = crtc;

	return apply(plane);
}

static int omap_plane_update(struct drm_plane *plane,
		struct drm_crtc *crtc, struct drm_framebuffer *fb,
		int crtc_x, int crtc_y,
		unsigned int crtc_w, unsigned int crtc_h,
		uint32_t src_x, uint32_t src_y,
		uint32_t src_w, uint32_t src_h)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);
	omap_plane->enabled = true;

	/* omap_plane_mode_set() takes adjusted src */
	switch (omap_plane->win.rotation & 0xf) {
	case BIT(DRM_ROTATE_90):
	case BIT(DRM_ROTATE_270):
		swap(src_w, src_h);
		break;
	}

	return omap_plane_mode_set(plane, crtc, fb,
			crtc_x, crtc_y, crtc_w, crtc_h,
			src_x, src_y, src_w, src_h,
			NULL, NULL);
}

static int omap_plane_disable(struct drm_plane *plane)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);
	omap_plane->win.rotation = BIT(DRM_ROTATE_0);
	return omap_plane_dpms(plane, DRM_MODE_DPMS_OFF);
}

static void omap_plane_destroy(struct drm_plane *plane)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);

	DBG("%s", omap_plane->name);

	omap_irq_unregister(plane->dev, &omap_plane->error_irq);

	omap_plane_disable(plane);
	drm_plane_cleanup(plane);

	drm_flip_work_cleanup(&omap_plane->unpin_work);

	kfree(omap_plane);
}

int omap_plane_dpms(struct drm_plane *plane, int mode)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);
	bool enabled = (mode == DRM_MODE_DPMS_ON);
	int ret = 0;

	if (enabled != omap_plane->enabled) {
		omap_plane->enabled = enabled;
		ret = apply(plane);
	}

	return ret;
}

/* helper to install properties which are common to planes and crtcs */
void omap_plane_install_properties(struct drm_plane *plane,
		struct drm_mode_object *obj)
{
	struct drm_device *dev = plane->dev;
	struct omap_drm_private *priv = dev->dev_private;
	struct drm_property *prop;

	if (priv->has_dmm) {
		prop = priv->rotation_prop;
		if (!prop) {
			const struct drm_prop_enum_list props[] = {
					{ DRM_ROTATE_0,   "rotate-0" },
					{ DRM_ROTATE_90,  "rotate-90" },
					{ DRM_ROTATE_180, "rotate-180" },
					{ DRM_ROTATE_270, "rotate-270" },
					{ DRM_REFLECT_X,  "reflect-x" },
					{ DRM_REFLECT_Y,  "reflect-y" },
			};
			prop = drm_property_create_bitmask(dev, 0, "rotation",
					props, ARRAY_SIZE(props));
			if (prop == NULL)
				return;
			priv->rotation_prop = prop;
		}
		drm_object_attach_property(obj, prop, 0);
	}

	prop = priv->zorder_prop;
	if (!prop) {
		prop = drm_property_create_range(dev, 0, "zorder", 0, 3);
		if (prop == NULL)
			return;
		priv->zorder_prop = prop;
	}
	drm_object_attach_property(obj, prop, 0);
}

int omap_plane_set_property(struct drm_plane *plane,
		struct drm_property *property, uint64_t val)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);
	struct omap_drm_private *priv = plane->dev->dev_private;
	int ret = -EINVAL;

	if (property == priv->rotation_prop) {
		DBG("%s: rotation: %02x", omap_plane->name, (uint32_t)val);
		omap_plane->win.rotation = val;
		ret = apply(plane);
	} else if (property == priv->zorder_prop) {
		DBG("%s: zorder: %02x", omap_plane->name, (uint32_t)val);
		omap_plane->info.zorder = val;
		ret = apply(plane);
	}

	return ret;
}

static const struct drm_plane_funcs omap_plane_funcs = {
		.update_plane = omap_plane_update,
		.disable_plane = omap_plane_disable,
		.destroy = omap_plane_destroy,
		.set_property = omap_plane_set_property,
};

static void omap_plane_error_irq(struct omap_drm_irq *irq, uint32_t irqstatus)
{
	struct omap_plane *omap_plane =
			container_of(irq, struct omap_plane, error_irq);
	DRM_ERROR("%s: errors: %08x\n", omap_plane->name, irqstatus);
}

static const char *plane_names[] = {
		[OMAP_DSS_GFX] = "gfx",
		[OMAP_DSS_VIDEO1] = "vid1",
		[OMAP_DSS_VIDEO2] = "vid2",
		[OMAP_DSS_VIDEO3] = "vid3",
};

static const uint32_t error_irqs[] = {
		[OMAP_DSS_GFX] = DISPC_IRQ_GFX_FIFO_UNDERFLOW,
		[OMAP_DSS_VIDEO1] = DISPC_IRQ_VID1_FIFO_UNDERFLOW,
		[OMAP_DSS_VIDEO2] = DISPC_IRQ_VID2_FIFO_UNDERFLOW,
		[OMAP_DSS_VIDEO3] = DISPC_IRQ_VID3_FIFO_UNDERFLOW,
};

/* initialize plane */
struct drm_plane *omap_plane_init(struct drm_device *dev,
		int id, bool private_plane)
{
	struct omap_drm_private *priv = dev->dev_private;
	struct drm_plane *plane = NULL;
	struct omap_plane *omap_plane;
	struct omap_overlay_info *info;
	int ret;

	DBG("%s: priv=%d", plane_names[id], private_plane);

	omap_plane = kzalloc(sizeof(*omap_plane), GFP_KERNEL);
	if (!omap_plane)
		goto fail;

	ret = drm_flip_work_init(&omap_plane->unpin_work, 16,
			"unpin", unpin_worker);
	if (ret) {
		dev_err(dev->dev, "could not allocate unpin FIFO\n");
		goto fail;
	}

	omap_plane->nformats = omap_framebuffer_get_formats(
			omap_plane->formats, ARRAY_SIZE(omap_plane->formats),
			dss_feat_get_supported_color_modes(id));
	omap_plane->id = id;
	omap_plane->name = plane_names[id];

	plane = &omap_plane->base;

	omap_plane->apply.pre_apply  = omap_plane_pre_apply;
	omap_plane->apply.post_apply = omap_plane_post_apply;

	omap_plane->error_irq.irqmask = error_irqs[id];
	omap_plane->error_irq.irq = omap_plane_error_irq;
	omap_irq_register(dev, &omap_plane->error_irq);

	drm_plane_init(dev, plane, (1 << priv->num_crtcs) - 1, &omap_plane_funcs,
			omap_plane->formats, omap_plane->nformats, private_plane);

	omap_plane_install_properties(plane, &plane->base);

	/* get our starting configuration, set defaults for parameters
	 * we don't currently use, etc:
	 */
	info = &omap_plane->info;
	info->rotation_type = OMAP_DSS_ROT_DMA;
	info->rotation = OMAP_DSS_ROT_0;
	info->global_alpha = 0xff;
	info->mirror = 0;

	/* Set defaults depending on whether we are a CRTC or overlay
	 * layer.
	 * TODO add ioctl to give userspace an API to change this.. this
	 * will come in a subsequent patch.
	 */
	if (private_plane)
		omap_plane->info.zorder = 0;
	else
		omap_plane->info.zorder = id;

	return plane;

fail:
	if (plane)
		omap_plane_destroy(plane);

	return NULL;
}
