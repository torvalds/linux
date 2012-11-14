/*
 * drivers/staging/omapdrm/omap_plane.c
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

#include <linux/kfifo.h>

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
	struct omap_overlay *ovl;
	struct omap_overlay_info info;

	/* position/orientation of scanout within the fb: */
	struct omap_drm_window win;


	/* last fb that we pinned: */
	struct drm_framebuffer *pinned_fb;

	uint32_t nformats;
	uint32_t formats[32];

	/* for synchronizing access to unpins fifo */
	struct mutex unpin_mutex;

	/* set of bo's pending unpin until next END_WIN irq */
	DECLARE_KFIFO_PTR(unpin_fifo, struct drm_gem_object *);
	int num_unpins, pending_num_unpins;

	/* for deferred unpin when we need to wait for scanout complete irq */
	struct work_struct work;

	/* callback on next endwin irq */
	struct callback endwin;
};

/* map from ovl->id to the irq we are interested in for scanout-done */
static const uint32_t id2irq[] = {
		[OMAP_DSS_GFX]    = DISPC_IRQ_GFX_END_WIN,
		[OMAP_DSS_VIDEO1] = DISPC_IRQ_VID1_END_WIN,
		[OMAP_DSS_VIDEO2] = DISPC_IRQ_VID2_END_WIN,
		[OMAP_DSS_VIDEO3] = DISPC_IRQ_VID3_END_WIN,
};

static void dispc_isr(void *arg, uint32_t mask)
{
	struct drm_plane *plane = arg;
	struct omap_plane *omap_plane = to_omap_plane(plane);
	struct omap_drm_private *priv = plane->dev->dev_private;

	omap_dispc_unregister_isr(dispc_isr, plane,
			id2irq[omap_plane->ovl->id]);

	queue_work(priv->wq, &omap_plane->work);
}

static void unpin_worker(struct work_struct *work)
{
	struct omap_plane *omap_plane =
			container_of(work, struct omap_plane, work);
	struct callback endwin;

	mutex_lock(&omap_plane->unpin_mutex);
	DBG("unpinning %d of %d", omap_plane->num_unpins,
			omap_plane->num_unpins + omap_plane->pending_num_unpins);
	while (omap_plane->num_unpins > 0) {
		struct drm_gem_object *bo = NULL;
		int ret = kfifo_get(&omap_plane->unpin_fifo, &bo);
		WARN_ON(!ret);
		omap_gem_put_paddr(bo);
		drm_gem_object_unreference_unlocked(bo);
		omap_plane->num_unpins--;
	}
	endwin = omap_plane->endwin;
	omap_plane->endwin.fxn = NULL;
	mutex_unlock(&omap_plane->unpin_mutex);

	if (endwin.fxn)
		endwin.fxn(endwin.arg);
}

static void install_irq(struct drm_plane *plane)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);
	struct omap_overlay *ovl = omap_plane->ovl;
	int ret;

	ret = omap_dispc_register_isr(dispc_isr, plane, id2irq[ovl->id]);

	/*
	 * omapdss has upper limit on # of registered irq handlers,
	 * which we shouldn't hit.. but if we do the limit should
	 * be raised or bad things happen:
	 */
	WARN_ON(ret == -EBUSY);
}

/* push changes down to dss2 */
static int commit(struct drm_plane *plane)
{
	struct drm_device *dev = plane->dev;
	struct omap_plane *omap_plane = to_omap_plane(plane);
	struct omap_overlay *ovl = omap_plane->ovl;
	struct omap_overlay_info *info = &omap_plane->info;
	int ret;

	DBG("%s", ovl->name);
	DBG("%dx%d -> %dx%d (%d)", info->width, info->height, info->out_width,
			info->out_height, info->screen_width);
	DBG("%d,%d %08x %08x", info->pos_x, info->pos_y,
			info->paddr, info->p_uv_addr);

	/* NOTE: do we want to do this at all here, or just wait
	 * for dpms(ON) since other CRTC's may not have their mode
	 * set yet, so fb dimensions may still change..
	 */
	ret = ovl->set_overlay_info(ovl, info);
	if (ret) {
		dev_err(dev->dev, "could not set overlay info\n");
		return ret;
	}

	mutex_lock(&omap_plane->unpin_mutex);
	omap_plane->num_unpins += omap_plane->pending_num_unpins;
	omap_plane->pending_num_unpins = 0;
	mutex_unlock(&omap_plane->unpin_mutex);

	/* our encoder doesn't necessarily get a commit() after this, in
	 * particular in the dpms() and mode_set_base() cases, so force the
	 * manager to update:
	 *
	 * could this be in the encoder somehow?
	 */
	if (ovl->manager) {
		ret = ovl->manager->apply(ovl->manager);
		if (ret) {
			dev_err(dev->dev, "could not apply settings\n");
			return ret;
		}

		/*
		 * NOTE: really this should be atomic w/ mgr->apply() but
		 * omapdss does not expose such an API
		 */
		if (omap_plane->num_unpins > 0)
			install_irq(plane);

	} else {
		struct omap_drm_private *priv = dev->dev_private;
		queue_work(priv->wq, &omap_plane->work);
	}


	if (ovl->is_enabled(ovl)) {
		omap_framebuffer_flush(plane->fb, info->pos_x, info->pos_y,
				info->out_width, info->out_height);
	}

	return 0;
}

/* when CRTC that we are attached to has potentially changed, this checks
 * if we are attached to proper manager, and if necessary updates.
 */
static void update_manager(struct drm_plane *plane)
{
	struct omap_drm_private *priv = plane->dev->dev_private;
	struct omap_plane *omap_plane = to_omap_plane(plane);
	struct omap_overlay *ovl = omap_plane->ovl;
	struct omap_overlay_manager *mgr = NULL;
	int i;

	if (plane->crtc) {
		for (i = 0; i < priv->num_encoders; i++) {
			struct drm_encoder *encoder = priv->encoders[i];
			if (encoder->crtc == plane->crtc) {
				mgr = omap_encoder_get_manager(encoder);
				break;
			}
		}
	}

	if (ovl->manager != mgr) {
		bool enabled = ovl->is_enabled(ovl);

		/* don't switch things around with enabled overlays: */
		if (enabled)
			omap_plane_dpms(plane, DRM_MODE_DPMS_OFF);

		if (ovl->manager) {
			DBG("disconnecting %s from %s", ovl->name,
					ovl->manager->name);
			ovl->unset_manager(ovl);
		}

		if (mgr) {
			DBG("connecting %s to %s", ovl->name, mgr->name);
			ovl->set_manager(ovl, mgr);
		}

		if (enabled && mgr)
			omap_plane_dpms(plane, DRM_MODE_DPMS_ON);
	}
}

static void unpin(void *arg, struct drm_gem_object *bo)
{
	struct drm_plane *plane = arg;
	struct omap_plane *omap_plane = to_omap_plane(plane);

	if (kfifo_put(&omap_plane->unpin_fifo,
			(const struct drm_gem_object **)&bo)) {
		omap_plane->pending_num_unpins++;
		/* also hold a ref so it isn't free'd while pinned */
		drm_gem_object_reference(bo);
	} else {
		dev_err(plane->dev->dev, "unpin fifo full!\n");
		omap_gem_put_paddr(bo);
	}
}

/* update which fb (if any) is pinned for scanout */
static int update_pin(struct drm_plane *plane, struct drm_framebuffer *fb)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);
	struct drm_framebuffer *pinned_fb = omap_plane->pinned_fb;

	if (pinned_fb != fb) {
		int ret;

		DBG("%p -> %p", pinned_fb, fb);

		mutex_lock(&omap_plane->unpin_mutex);
		ret = omap_framebuffer_replace(pinned_fb, fb, plane, unpin);
		mutex_unlock(&omap_plane->unpin_mutex);

		if (ret) {
			dev_err(plane->dev->dev, "could not swap %p -> %p\n",
					omap_plane->pinned_fb, fb);
			omap_plane->pinned_fb = NULL;
			return ret;
		}

		omap_plane->pinned_fb = fb;
	}

	return 0;
}

/* update parameters that are dependent on the framebuffer dimensions and
 * position within the fb that this plane scans out from. This is called
 * when framebuffer or x,y base may have changed.
 */
static void update_scanout(struct drm_plane *plane)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);
	struct omap_overlay_info *info = &omap_plane->info;
	struct omap_drm_window *win = &omap_plane->win;
	int ret;

	ret = update_pin(plane, plane->fb);
	if (ret) {
		dev_err(plane->dev->dev,
			"could not pin fb: %d\n", ret);
		omap_plane_dpms(plane, DRM_MODE_DPMS_OFF);
		return;
	}

	omap_framebuffer_update_scanout(plane->fb, win, info);

	DBG("%s: %d,%d: %08x %08x (%d)", omap_plane->ovl->name,
			win->src_x, win->src_y,
			(u32)info->paddr, (u32)info->p_uv_addr,
			info->screen_width);
}

int omap_plane_mode_set(struct drm_plane *plane,
		struct drm_crtc *crtc, struct drm_framebuffer *fb,
		int crtc_x, int crtc_y,
		unsigned int crtc_w, unsigned int crtc_h,
		uint32_t src_x, uint32_t src_y,
		uint32_t src_w, uint32_t src_h)
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

	/* note: this is done after this fxn returns.. but if we need
	 * to do a commit/update_scanout, etc before this returns we
	 * need the current value.
	 */
	plane->fb = fb;
	plane->crtc = crtc;

	update_scanout(plane);
	update_manager(plane);

	return 0;
}

static int omap_plane_update(struct drm_plane *plane,
		struct drm_crtc *crtc, struct drm_framebuffer *fb,
		int crtc_x, int crtc_y,
		unsigned int crtc_w, unsigned int crtc_h,
		uint32_t src_x, uint32_t src_y,
		uint32_t src_w, uint32_t src_h)
{
	omap_plane_mode_set(plane, crtc, fb, crtc_x, crtc_y, crtc_w, crtc_h,
			src_x, src_y, src_w, src_h);
	return omap_plane_dpms(plane, DRM_MODE_DPMS_ON);
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
	DBG("%s", omap_plane->ovl->name);
	omap_plane_disable(plane);
	drm_plane_cleanup(plane);
	WARN_ON(omap_plane->pending_num_unpins + omap_plane->num_unpins > 0);
	kfifo_free(&omap_plane->unpin_fifo);
	kfree(omap_plane);
}

int omap_plane_dpms(struct drm_plane *plane, int mode)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);
	struct omap_overlay *ovl = omap_plane->ovl;
	int r;

	DBG("%s: %d", omap_plane->ovl->name, mode);

	if (mode == DRM_MODE_DPMS_ON) {
		update_scanout(plane);
		r = commit(plane);
		if (!r)
			r = ovl->enable(ovl);
	} else {
		struct omap_drm_private *priv = plane->dev->dev_private;
		r = ovl->disable(ovl);
		update_pin(plane, NULL);
		queue_work(priv->wq, &omap_plane->work);
	}

	return r;
}

void omap_plane_on_endwin(struct drm_plane *plane,
		void (*fxn)(void *), void *arg)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);

	mutex_lock(&omap_plane->unpin_mutex);
	omap_plane->endwin.fxn = fxn;
	omap_plane->endwin.arg = arg;
	mutex_unlock(&omap_plane->unpin_mutex);

	install_irq(plane);
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
		struct omap_overlay *ovl = omap_plane->ovl;

		DBG("%s: rotation: %02x", ovl->name, (uint32_t)val);
		omap_plane->win.rotation = val;

		if (ovl->is_enabled(ovl))
			ret = omap_plane_dpms(plane, DRM_MODE_DPMS_ON);
		else
			ret = 0;
	} else if (property == priv->zorder_prop) {
		struct omap_overlay *ovl = omap_plane->ovl;

		DBG("%s: zorder: %d", ovl->name, (uint32_t)val);
		omap_plane->info.zorder = val;

		if (ovl->is_enabled(ovl))
			ret = omap_plane_dpms(plane, DRM_MODE_DPMS_ON);
		else
			ret = 0;
	}

	return ret;
}

static const struct drm_plane_funcs omap_plane_funcs = {
		.update_plane = omap_plane_update,
		.disable_plane = omap_plane_disable,
		.destroy = omap_plane_destroy,
		.set_property = omap_plane_set_property,
};

/* initialize plane */
struct drm_plane *omap_plane_init(struct drm_device *dev,
		struct omap_overlay *ovl, unsigned int possible_crtcs,
		bool priv)
{
	struct drm_plane *plane = NULL;
	struct omap_plane *omap_plane;
	int ret;

	DBG("%s: possible_crtcs=%08x, priv=%d", ovl->name,
			possible_crtcs, priv);

	/* friendly reminder to update table for future hw: */
	WARN_ON(ovl->id >= ARRAY_SIZE(id2irq));

	omap_plane = kzalloc(sizeof(*omap_plane), GFP_KERNEL);
	if (!omap_plane) {
		dev_err(dev->dev, "could not allocate plane\n");
		goto fail;
	}

	mutex_init(&omap_plane->unpin_mutex);

	ret = kfifo_alloc(&omap_plane->unpin_fifo, 16, GFP_KERNEL);
	if (ret) {
		dev_err(dev->dev, "could not allocate unpin FIFO\n");
		goto fail;
	}

	INIT_WORK(&omap_plane->work, unpin_worker);

	omap_plane->nformats = omap_framebuffer_get_formats(
			omap_plane->formats, ARRAY_SIZE(omap_plane->formats),
			ovl->supported_modes);
	omap_plane->ovl = ovl;
	plane = &omap_plane->base;

	drm_plane_init(dev, plane, possible_crtcs, &omap_plane_funcs,
			omap_plane->formats, omap_plane->nformats, priv);

	omap_plane_install_properties(plane, &plane->base);

	/* get our starting configuration, set defaults for parameters
	 * we don't currently use, etc:
	 */
	ovl->get_overlay_info(ovl, &omap_plane->info);
	omap_plane->info.rotation_type = OMAP_DSS_ROT_DMA;
	omap_plane->info.rotation = OMAP_DSS_ROT_0;
	omap_plane->info.global_alpha = 0xff;
	omap_plane->info.mirror = 0;

	/* Set defaults depending on whether we are a CRTC or overlay
	 * layer.
	 * TODO add ioctl to give userspace an API to change this.. this
	 * will come in a subsequent patch.
	 */
	if (priv)
		omap_plane->info.zorder = 0;
	else
		omap_plane->info.zorder = ovl->id;

	update_manager(plane);

	return plane;

fail:
	if (plane)
		omap_plane_destroy(plane);

	return NULL;
}
