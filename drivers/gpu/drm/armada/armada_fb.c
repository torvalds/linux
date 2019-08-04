// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Russell King
 */

#include <drm/drm_modeset_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_framebuffer_helper.h>

#include "armada_drm.h"
#include "armada_fb.h"
#include "armada_gem.h"
#include "armada_hw.h"

static const struct drm_framebuffer_funcs armada_fb_funcs = {
	.destroy	= drm_gem_fb_destroy,
	.create_handle	= drm_gem_fb_create_handle,
};

struct armada_framebuffer *armada_framebuffer_create(struct drm_device *dev,
	const struct drm_mode_fb_cmd2 *mode, struct armada_gem_object *obj)
{
	struct armada_framebuffer *dfb;
	uint8_t format, config;
	int ret;

	switch (mode->pixel_format) {
#define FMT(drm, fmt, mod)		\
	case DRM_FORMAT_##drm:		\
		format = CFG_##fmt;	\
		config = mod;		\
		break
	FMT(RGB565,	565,		CFG_SWAPRB);
	FMT(BGR565,	565,		0);
	FMT(ARGB1555,	1555,		CFG_SWAPRB);
	FMT(ABGR1555,	1555,		0);
	FMT(RGB888,	888PACK,	CFG_SWAPRB);
	FMT(BGR888,	888PACK,	0);
	FMT(XRGB8888,	X888,		CFG_SWAPRB);
	FMT(XBGR8888,	X888,		0);
	FMT(ARGB8888,	8888,		CFG_SWAPRB);
	FMT(ABGR8888,	8888,		0);
	FMT(YUYV,	422PACK,	CFG_YUV2RGB | CFG_SWAPYU | CFG_SWAPUV);
	FMT(UYVY,	422PACK,	CFG_YUV2RGB);
	FMT(VYUY,	422PACK,	CFG_YUV2RGB | CFG_SWAPUV);
	FMT(YVYU,	422PACK,	CFG_YUV2RGB | CFG_SWAPYU);
	FMT(YUV422,	422,		CFG_YUV2RGB);
	FMT(YVU422,	422,		CFG_YUV2RGB | CFG_SWAPUV);
	FMT(YUV420,	420,		CFG_YUV2RGB);
	FMT(YVU420,	420,		CFG_YUV2RGB | CFG_SWAPUV);
	FMT(C8,		PSEUDO8,	0);
#undef FMT
	default:
		return ERR_PTR(-EINVAL);
	}

	dfb = kzalloc(sizeof(*dfb), GFP_KERNEL);
	if (!dfb) {
		DRM_ERROR("failed to allocate Armada fb object\n");
		return ERR_PTR(-ENOMEM);
	}

	dfb->fmt = format;
	dfb->mod = config;
	dfb->fb.obj[0] = &obj->obj;

	drm_helper_mode_fill_fb_struct(dev, &dfb->fb, mode);

	ret = drm_framebuffer_init(dev, &dfb->fb, &armada_fb_funcs);
	if (ret) {
		kfree(dfb);
		return ERR_PTR(ret);
	}

	/*
	 * Take a reference on our object as we're successful - the
	 * caller already holds a reference, which keeps us safe for
	 * the above call, but the caller will drop their reference
	 * to it.  Hence we need to take our own reference.
	 */
	drm_gem_object_get(&obj->obj);

	return dfb;
}

struct drm_framebuffer *armada_fb_create(struct drm_device *dev,
	struct drm_file *dfile, const struct drm_mode_fb_cmd2 *mode)
{
	const struct drm_format_info *info = drm_get_format_info(dev, mode);
	struct armada_gem_object *obj;
	struct armada_framebuffer *dfb;
	int ret;

	DRM_DEBUG_DRIVER("w%u h%u pf%08x f%u p%u,%u,%u\n",
		mode->width, mode->height, mode->pixel_format,
		mode->flags, mode->pitches[0], mode->pitches[1],
		mode->pitches[2]);

	/* We can only handle a single plane at the moment */
	if (info->num_planes > 1 &&
	    (mode->handles[0] != mode->handles[1] ||
	     mode->handles[0] != mode->handles[2])) {
		ret = -EINVAL;
		goto err;
	}

	obj = armada_gem_object_lookup(dfile, mode->handles[0]);
	if (!obj) {
		ret = -ENOENT;
		goto err;
	}

	if (obj->obj.import_attach && !obj->sgt) {
		ret = armada_gem_map_import(obj);
		if (ret)
			goto err_unref;
	}

	/* Framebuffer objects must have a valid device address for scanout */
	if (!obj->mapped) {
		ret = -EINVAL;
		goto err_unref;
	}

	dfb = armada_framebuffer_create(dev, mode, obj);
	if (IS_ERR(dfb)) {
		ret = PTR_ERR(dfb);
		goto err;
	}

	drm_gem_object_put_unlocked(&obj->obj);

	return &dfb->fb;

 err_unref:
	drm_gem_object_put_unlocked(&obj->obj);
 err:
	DRM_ERROR("failed to initialize framebuffer: %d\n", ret);
	return ERR_PTR(ret);
}
