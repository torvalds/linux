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

#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>

#include "omap_dmm_tiler.h"
#include "omap_drv.h"

/* some hackery because omapdss has an 'enum omap_plane' (which would be
 * better named omap_plane_id).. and compiler seems unhappy about having
 * both a 'struct omap_plane' and 'enum omap_plane'
 */
#define omap_plane _omap_plane

/*
 * plane funcs
 */

#define to_omap_plane(x) container_of(x, struct omap_plane, base)

struct omap_plane {
	struct drm_plane base;
	int id;  /* TODO rename omap_plane -> omap_plane_id in omapdss so I can use the enum */
	const char *name;
	struct omap_overlay_info info;

	/* position/orientation of scanout within the fb: */
	struct omap_drm_window win;
	bool enabled;

	/* last fb that we pinned: */
	struct drm_framebuffer *pinned_fb;

	uint32_t nformats;
	uint32_t formats[32];

	struct omap_drm_irq error_irq;
};

/* update which fb (if any) is pinned for scanout */
static int omap_plane_update_pin(struct drm_plane *plane)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);
	struct drm_framebuffer *pinned_fb = omap_plane->pinned_fb;
	struct drm_framebuffer *fb = omap_plane->enabled ? plane->fb : NULL;
	int ret = 0;

	if (pinned_fb == fb)
		return 0;

	DBG("%p -> %p", pinned_fb, fb);

	if (fb) {
		drm_framebuffer_reference(fb);
		ret = omap_framebuffer_pin(fb);
	}

	if (pinned_fb)
		omap_crtc_queue_unpin(plane->crtc, pinned_fb);

	if (ret) {
		dev_err(plane->dev->dev, "could not swap %p -> %p\n",
				omap_plane->pinned_fb, fb);
		drm_framebuffer_unreference(fb);
		omap_plane->pinned_fb = NULL;
		return ret;
	}

	omap_plane->pinned_fb = fb;

	return 0;
}

static int __omap_plane_setup(struct omap_plane *omap_plane,
			      struct drm_crtc *crtc,
			      struct drm_framebuffer *fb)
{
	struct omap_overlay_info *info = &omap_plane->info;
	struct drm_device *dev = omap_plane->base.dev;
	int ret;

	DBG("%s, enabled=%d", omap_plane->name, omap_plane->enabled);

	if (!omap_plane->enabled) {
		dispc_ovl_enable(omap_plane->id, false);
		return 0;
	}

	/* update scanout: */
	omap_framebuffer_update_scanout(fb, &omap_plane->win, info);

	DBG("%dx%d -> %dx%d (%d)", info->width, info->height,
			info->out_width, info->out_height,
			info->screen_width);
	DBG("%d,%d %pad %pad", info->pos_x, info->pos_y,
			&info->paddr, &info->p_uv_addr);

	dispc_ovl_set_channel_out(omap_plane->id,
				  omap_crtc_channel(crtc));

	/* and finally, update omapdss: */
	ret = dispc_ovl_setup(omap_plane->id, info, false,
			      omap_crtc_timings(crtc), false);
	if (ret) {
		dev_err(dev->dev, "dispc_ovl_setup failed: %d\n", ret);
		return ret;
	}

	dispc_ovl_enable(omap_plane->id, true);

	return 0;
}

static int omap_plane_setup(struct omap_plane *omap_plane)
{
	struct drm_plane *plane = &omap_plane->base;
	int ret;

	ret = omap_plane_update_pin(plane);
	if (ret < 0)
		return ret;

	dispc_runtime_get();
	ret = __omap_plane_setup(omap_plane, plane->crtc, plane->fb);
	dispc_runtime_put();

	return ret;
}

int omap_plane_set_enable(struct drm_plane *plane, bool enable)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);

	if (enable == omap_plane->enabled)
		return 0;

	omap_plane->enabled = enable;
	return omap_plane_setup(omap_plane);
}

static int omap_plane_prepare_fb(struct drm_plane *plane,
				 struct drm_framebuffer *fb,
				 const struct drm_plane_state *new_state)
{
	return omap_framebuffer_pin(fb);
}

static void omap_plane_cleanup_fb(struct drm_plane *plane,
				  struct drm_framebuffer *fb,
				  const struct drm_plane_state *old_state)
{
	omap_framebuffer_unpin(fb);
}

static void omap_plane_atomic_update(struct drm_plane *plane,
				     struct drm_plane_state *old_state)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);
	struct omap_drm_window *win = &omap_plane->win;
	struct drm_plane_state *state = plane->state;
	uint32_t src_w;
	uint32_t src_h;

	if (!state->fb || !state->crtc)
		return;

	/* omap_framebuffer_update_scanout() takes adjusted src */
	switch (omap_plane->win.rotation & 0xf) {
	case BIT(DRM_ROTATE_90):
	case BIT(DRM_ROTATE_270):
		src_w = state->src_h;
		src_h = state->src_w;
		break;
	default:
		src_w = state->src_w;
		src_h = state->src_h;
		break;
	}

	/* src values are in Q16 fixed point, convert to integer. */
	win->crtc_x = state->crtc_x;
	win->crtc_y = state->crtc_y;
	win->crtc_w = state->crtc_w;
	win->crtc_h = state->crtc_h;

	win->src_x = state->src_x >> 16;
	win->src_y = state->src_y >> 16;
	win->src_w = src_w >> 16;
	win->src_h = src_h >> 16;

	omap_plane->enabled = true;
	__omap_plane_setup(omap_plane, state->crtc, state->fb);
}

static void omap_plane_atomic_disable(struct drm_plane *plane,
				      struct drm_plane_state *old_state)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);

	omap_plane->win.rotation = BIT(DRM_ROTATE_0);
	omap_plane->info.zorder = plane->type == DRM_PLANE_TYPE_PRIMARY
				? 0 : omap_plane->id;

	if (!omap_plane->enabled)
		return;

	omap_plane->enabled = false;
	__omap_plane_setup(omap_plane, NULL, NULL);
}

static const struct drm_plane_helper_funcs omap_plane_helper_funcs = {
	.prepare_fb = omap_plane_prepare_fb,
	.cleanup_fb = omap_plane_cleanup_fb,
	.atomic_update = omap_plane_atomic_update,
	.atomic_disable = omap_plane_atomic_disable,
};

static void omap_plane_destroy(struct drm_plane *plane)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);

	DBG("%s", omap_plane->name);

	omap_irq_unregister(plane->dev, &omap_plane->error_irq);

	drm_plane_cleanup(plane);

	kfree(omap_plane);
}

/* helper to install properties which are common to planes and crtcs */
void omap_plane_install_properties(struct drm_plane *plane,
		struct drm_mode_object *obj)
{
	struct drm_device *dev = plane->dev;
	struct omap_drm_private *priv = dev->dev_private;

	if (priv->has_dmm) {
		struct drm_property *prop = dev->mode_config.rotation_property;

		drm_object_attach_property(obj, prop, 0);
	}

	drm_object_attach_property(obj, priv->zorder_prop, 0);
}

int omap_plane_set_property(struct drm_plane *plane,
		struct drm_property *property, uint64_t val)
{
	struct omap_plane *omap_plane = to_omap_plane(plane);
	struct omap_drm_private *priv = plane->dev->dev_private;
	int ret;

	if (property == plane->dev->mode_config.rotation_property) {
		DBG("%s: rotation: %02x", omap_plane->name, (uint32_t)val);
		omap_plane->win.rotation = val;
	} else if (property == priv->zorder_prop) {
		DBG("%s: zorder: %02x", omap_plane->name, (uint32_t)val);
		omap_plane->info.zorder = val;
	} else {
		return -EINVAL;
	}

	/*
	 * We're done if the plane is disabled, properties will be applied the
	 * next time it becomes enabled.
	 */
	if (!omap_plane->enabled)
		return 0;

	ret = omap_plane_setup(omap_plane);
	if (ret < 0)
		return ret;

	return omap_crtc_flush(plane->crtc);
}

static const struct drm_plane_funcs omap_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.reset = drm_atomic_helper_plane_reset,
	.destroy = omap_plane_destroy,
	.set_property = omap_plane_set_property,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

static void omap_plane_error_irq(struct omap_drm_irq *irq, uint32_t irqstatus)
{
	struct omap_plane *omap_plane =
			container_of(irq, struct omap_plane, error_irq);
	DRM_ERROR_RATELIMITED("%s: errors: %08x\n", omap_plane->name,
		irqstatus);
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
		int id, enum drm_plane_type type)
{
	struct omap_drm_private *priv = dev->dev_private;
	struct drm_plane *plane;
	struct omap_plane *omap_plane;
	struct omap_overlay_info *info;
	int ret;

	DBG("%s: type=%d", plane_names[id], type);

	omap_plane = kzalloc(sizeof(*omap_plane), GFP_KERNEL);
	if (!omap_plane)
		return ERR_PTR(-ENOMEM);

	omap_plane->nformats = omap_framebuffer_get_formats(
			omap_plane->formats, ARRAY_SIZE(omap_plane->formats),
			dss_feat_get_supported_color_modes(id));
	omap_plane->id = id;
	omap_plane->name = plane_names[id];

	plane = &omap_plane->base;

	omap_plane->error_irq.irqmask = error_irqs[id];
	omap_plane->error_irq.irq = omap_plane_error_irq;
	omap_irq_register(dev, &omap_plane->error_irq);

	ret = drm_universal_plane_init(dev, plane, (1 << priv->num_crtcs) - 1,
				       &omap_plane_funcs, omap_plane->formats,
				       omap_plane->nformats, type);
	if (ret < 0)
		goto error;

	drm_plane_helper_add(plane, &omap_plane_helper_funcs);

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
	if (type == DRM_PLANE_TYPE_PRIMARY)
		omap_plane->info.zorder = 0;
	else
		omap_plane->info.zorder = id;

	return plane;

error:
	omap_irq_unregister(plane->dev, &omap_plane->error_irq);
	kfree(omap_plane);
	return NULL;
}
