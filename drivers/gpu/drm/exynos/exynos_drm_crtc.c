/* exynos_drm_crtc.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_encoder.h"
#include "exynos_drm_plane.h"

#define to_exynos_crtc(x)	container_of(x, struct exynos_drm_crtc,\
				drm_crtc)

enum exynos_crtc_mode {
	CRTC_MODE_NORMAL,	/* normal mode */
	CRTC_MODE_BLANK,	/* The private plane of crtc is blank */
};

/*
 * Exynos specific crtc structure.
 *
 * @drm_crtc: crtc object.
 * @drm_plane: pointer of private plane object for this crtc
 * @pipe: a crtc index created at load() with a new crtc object creation
 *	and the crtc object would be set to private->crtc array
 *	to get a crtc object corresponding to this pipe from private->crtc
 *	array when irq interrupt occured. the reason of using this pipe is that
 *	drm framework doesn't support multiple irq yet.
 *	we can refer to the crtc to current hardware interrupt occured through
 *	this pipe value.
 * @dpms: store the crtc dpms value
 * @mode: store the crtc mode value
 */
struct exynos_drm_crtc {
	struct drm_crtc			drm_crtc;
	struct drm_plane		*plane;
	unsigned int			pipe;
	unsigned int			dpms;
	enum exynos_crtc_mode		mode;
	wait_queue_head_t		pending_flip_queue;
	atomic_t			pending_flip;
};

static void exynos_drm_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct exynos_drm_crtc *exynos_crtc = to_exynos_crtc(crtc);

	DRM_DEBUG_KMS("crtc[%d] mode[%d]\n", crtc->base.id, mode);

	if (exynos_crtc->dpms == mode) {
		DRM_DEBUG_KMS("desired dpms mode is same as previous one.\n");
		return;
	}

	if (mode > DRM_MODE_DPMS_ON) {
		/* wait for the completion of page flip. */
		wait_event(exynos_crtc->pending_flip_queue,
				atomic_read(&exynos_crtc->pending_flip) == 0);
		drm_vblank_off(crtc->dev, exynos_crtc->pipe);
	}

	exynos_drm_fn_encoder(crtc, &mode, exynos_drm_encoder_crtc_dpms);
	exynos_crtc->dpms = mode;
}

static void exynos_drm_crtc_prepare(struct drm_crtc *crtc)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* drm framework doesn't check NULL. */
}

static void exynos_drm_crtc_commit(struct drm_crtc *crtc)
{
	struct exynos_drm_crtc *exynos_crtc = to_exynos_crtc(crtc);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	exynos_drm_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
	exynos_plane_commit(exynos_crtc->plane);
	exynos_plane_dpms(exynos_crtc->plane, DRM_MODE_DPMS_ON);
}

static bool
exynos_drm_crtc_mode_fixup(struct drm_crtc *crtc,
			    const struct drm_display_mode *mode,
			    struct drm_display_mode *adjusted_mode)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* drm framework doesn't check NULL */
	return true;
}

static int
exynos_drm_crtc_mode_set(struct drm_crtc *crtc, struct drm_display_mode *mode,
			  struct drm_display_mode *adjusted_mode, int x, int y,
			  struct drm_framebuffer *old_fb)
{
	struct exynos_drm_crtc *exynos_crtc = to_exynos_crtc(crtc);
	struct drm_plane *plane = exynos_crtc->plane;
	unsigned int crtc_w;
	unsigned int crtc_h;
	int pipe = exynos_crtc->pipe;
	int ret;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	/*
	 * copy the mode data adjusted by mode_fixup() into crtc->mode
	 * so that hardware can be seet to proper mode.
	 */
	memcpy(&crtc->mode, adjusted_mode, sizeof(*adjusted_mode));

	crtc_w = crtc->fb->width - x;
	crtc_h = crtc->fb->height - y;

	ret = exynos_plane_mode_set(plane, crtc, crtc->fb, 0, 0, crtc_w, crtc_h,
				    x, y, crtc_w, crtc_h);
	if (ret)
		return ret;

	plane->crtc = crtc;
	plane->fb = crtc->fb;

	exynos_drm_fn_encoder(crtc, &pipe, exynos_drm_encoder_crtc_pipe);

	return 0;
}

static int exynos_drm_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
					  struct drm_framebuffer *old_fb)
{
	struct exynos_drm_crtc *exynos_crtc = to_exynos_crtc(crtc);
	struct drm_plane *plane = exynos_crtc->plane;
	unsigned int crtc_w;
	unsigned int crtc_h;
	int ret;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* when framebuffer changing is requested, crtc's dpms should be on */
	if (exynos_crtc->dpms > DRM_MODE_DPMS_ON) {
		DRM_ERROR("failed framebuffer changing request.\n");
		return -EPERM;
	}

	crtc_w = crtc->fb->width - x;
	crtc_h = crtc->fb->height - y;

	ret = exynos_plane_mode_set(plane, crtc, crtc->fb, 0, 0, crtc_w, crtc_h,
				    x, y, crtc_w, crtc_h);
	if (ret)
		return ret;

	exynos_drm_crtc_commit(crtc);

	return 0;
}

static void exynos_drm_crtc_load_lut(struct drm_crtc *crtc)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);
	/* drm framework doesn't check NULL */
}

static void exynos_drm_crtc_disable(struct drm_crtc *crtc)
{
	struct exynos_drm_crtc *exynos_crtc = to_exynos_crtc(crtc);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	exynos_plane_dpms(exynos_crtc->plane, DRM_MODE_DPMS_OFF);
	exynos_drm_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static struct drm_crtc_helper_funcs exynos_crtc_helper_funcs = {
	.dpms		= exynos_drm_crtc_dpms,
	.prepare	= exynos_drm_crtc_prepare,
	.commit		= exynos_drm_crtc_commit,
	.mode_fixup	= exynos_drm_crtc_mode_fixup,
	.mode_set	= exynos_drm_crtc_mode_set,
	.mode_set_base	= exynos_drm_crtc_mode_set_base,
	.load_lut	= exynos_drm_crtc_load_lut,
	.disable	= exynos_drm_crtc_disable,
};

static int exynos_drm_crtc_page_flip(struct drm_crtc *crtc,
				     struct drm_framebuffer *fb,
				     struct drm_pending_vblank_event *event,
				     uint32_t page_flip_flags)
{
	struct drm_device *dev = crtc->dev;
	struct exynos_drm_private *dev_priv = dev->dev_private;
	struct exynos_drm_crtc *exynos_crtc = to_exynos_crtc(crtc);
	struct drm_framebuffer *old_fb = crtc->fb;
	int ret = -EINVAL;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* when the page flip is requested, crtc's dpms should be on */
	if (exynos_crtc->dpms > DRM_MODE_DPMS_ON) {
		DRM_ERROR("failed page flip request.\n");
		return -EINVAL;
	}

	mutex_lock(&dev->struct_mutex);

	if (event) {
		/*
		 * the pipe from user always is 0 so we can set pipe number
		 * of current owner to event.
		 */
		event->pipe = exynos_crtc->pipe;

		ret = drm_vblank_get(dev, exynos_crtc->pipe);
		if (ret) {
			DRM_DEBUG("failed to acquire vblank counter\n");

			goto out;
		}

		spin_lock_irq(&dev->event_lock);
		list_add_tail(&event->base.link,
				&dev_priv->pageflip_event_list);
		atomic_set(&exynos_crtc->pending_flip, 1);
		spin_unlock_irq(&dev->event_lock);

		crtc->fb = fb;
		ret = exynos_drm_crtc_mode_set_base(crtc, crtc->x, crtc->y,
						    NULL);
		if (ret) {
			crtc->fb = old_fb;

			spin_lock_irq(&dev->event_lock);
			drm_vblank_put(dev, exynos_crtc->pipe);
			list_del(&event->base.link);
			spin_unlock_irq(&dev->event_lock);

			goto out;
		}
	}
out:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

static void exynos_drm_crtc_destroy(struct drm_crtc *crtc)
{
	struct exynos_drm_crtc *exynos_crtc = to_exynos_crtc(crtc);
	struct exynos_drm_private *private = crtc->dev->dev_private;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	private->crtc[exynos_crtc->pipe] = NULL;

	drm_crtc_cleanup(crtc);
	kfree(exynos_crtc);
}

static int exynos_drm_crtc_set_property(struct drm_crtc *crtc,
					struct drm_property *property,
					uint64_t val)
{
	struct drm_device *dev = crtc->dev;
	struct exynos_drm_private *dev_priv = dev->dev_private;
	struct exynos_drm_crtc *exynos_crtc = to_exynos_crtc(crtc);

	DRM_DEBUG_KMS("%s\n", __func__);

	if (property == dev_priv->crtc_mode_property) {
		enum exynos_crtc_mode mode = val;

		if (mode == exynos_crtc->mode)
			return 0;

		exynos_crtc->mode = mode;

		switch (mode) {
		case CRTC_MODE_NORMAL:
			exynos_drm_crtc_commit(crtc);
			break;
		case CRTC_MODE_BLANK:
			exynos_plane_dpms(exynos_crtc->plane,
					  DRM_MODE_DPMS_OFF);
			break;
		default:
			break;
		}

		return 0;
	}

	return -EINVAL;
}

static struct drm_crtc_funcs exynos_crtc_funcs = {
	.set_config	= drm_crtc_helper_set_config,
	.page_flip	= exynos_drm_crtc_page_flip,
	.destroy	= exynos_drm_crtc_destroy,
	.set_property	= exynos_drm_crtc_set_property,
};

static const struct drm_prop_enum_list mode_names[] = {
	{ CRTC_MODE_NORMAL, "normal" },
	{ CRTC_MODE_BLANK, "blank" },
};

static void exynos_drm_crtc_attach_mode_property(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct exynos_drm_private *dev_priv = dev->dev_private;
	struct drm_property *prop;

	DRM_DEBUG_KMS("%s\n", __func__);

	prop = dev_priv->crtc_mode_property;
	if (!prop) {
		prop = drm_property_create_enum(dev, 0, "mode", mode_names,
						ARRAY_SIZE(mode_names));
		if (!prop)
			return;

		dev_priv->crtc_mode_property = prop;
	}

	drm_object_attach_property(&crtc->base, prop, 0);
}

int exynos_drm_crtc_create(struct drm_device *dev, unsigned int nr)
{
	struct exynos_drm_crtc *exynos_crtc;
	struct exynos_drm_private *private = dev->dev_private;
	struct drm_crtc *crtc;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	exynos_crtc = kzalloc(sizeof(*exynos_crtc), GFP_KERNEL);
	if (!exynos_crtc) {
		DRM_ERROR("failed to allocate exynos crtc\n");
		return -ENOMEM;
	}

	exynos_crtc->pipe = nr;
	exynos_crtc->dpms = DRM_MODE_DPMS_OFF;
	init_waitqueue_head(&exynos_crtc->pending_flip_queue);
	atomic_set(&exynos_crtc->pending_flip, 0);
	exynos_crtc->plane = exynos_plane_init(dev, 1 << nr, true);
	if (!exynos_crtc->plane) {
		kfree(exynos_crtc);
		return -ENOMEM;
	}

	crtc = &exynos_crtc->drm_crtc;

	private->crtc[nr] = crtc;

	drm_crtc_init(dev, crtc, &exynos_crtc_funcs);
	drm_crtc_helper_add(crtc, &exynos_crtc_helper_funcs);

	exynos_drm_crtc_attach_mode_property(crtc);

	return 0;
}

int exynos_drm_crtc_enable_vblank(struct drm_device *dev, int crtc)
{
	struct exynos_drm_private *private = dev->dev_private;
	struct exynos_drm_crtc *exynos_crtc =
		to_exynos_crtc(private->crtc[crtc]);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (exynos_crtc->dpms != DRM_MODE_DPMS_ON)
		return -EPERM;

	exynos_drm_fn_encoder(private->crtc[crtc], &crtc,
			exynos_drm_enable_vblank);

	return 0;
}

void exynos_drm_crtc_disable_vblank(struct drm_device *dev, int crtc)
{
	struct exynos_drm_private *private = dev->dev_private;
	struct exynos_drm_crtc *exynos_crtc =
		to_exynos_crtc(private->crtc[crtc]);

	DRM_DEBUG_KMS("%s\n", __FILE__);

	if (exynos_crtc->dpms != DRM_MODE_DPMS_ON)
		return;

	exynos_drm_fn_encoder(private->crtc[crtc], &crtc,
			exynos_drm_disable_vblank);
}

void exynos_drm_crtc_finish_pageflip(struct drm_device *dev, int crtc)
{
	struct exynos_drm_private *dev_priv = dev->dev_private;
	struct drm_pending_vblank_event *e, *t;
	struct drm_crtc *drm_crtc = dev_priv->crtc[crtc];
	struct exynos_drm_crtc *exynos_crtc = to_exynos_crtc(drm_crtc);
	unsigned long flags;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	spin_lock_irqsave(&dev->event_lock, flags);

	list_for_each_entry_safe(e, t, &dev_priv->pageflip_event_list,
			base.link) {
		/* if event's pipe isn't same as crtc then ignore it. */
		if (crtc != e->pipe)
			continue;

		list_del(&e->base.link);
		drm_send_vblank_event(dev, -1, e);
		drm_vblank_put(dev, crtc);
		atomic_set(&exynos_crtc->pending_flip, 0);
		wake_up(&exynos_crtc->pending_flip_queue);
	}

	spin_unlock_irqrestore(&dev->event_lock, flags);
}
