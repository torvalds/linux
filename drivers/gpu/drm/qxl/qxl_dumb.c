/*
 * Copyright 2013 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alon Levy
 */

#include "qxl_drv.h"
#include "qxl_object.h"

/* dumb ioctls implementation */

int qxl_mode_dumb_create(struct drm_file *file_priv,
			    struct drm_device *dev,
			    struct drm_mode_create_dumb *args)
{
	struct qxl_device *qdev = dev->dev_private;
	struct qxl_bo *qobj;
	uint32_t handle;
	int r;
	struct qxl_surface surf;
	uint32_t pitch, format;

	pitch = args->width * ((args->bpp + 1) / 8);
	args->size = pitch * args->height;
	args->size = ALIGN(args->size, PAGE_SIZE);

	switch (args->bpp) {
	case 16:
		format = SPICE_SURFACE_FMT_16_565;
		break;
	case 32:
		format = SPICE_SURFACE_FMT_32_xRGB;
		break;
	default:
		return -EINVAL;
	}

	surf.width = args->width;
	surf.height = args->height;
	surf.stride = pitch;
	surf.format = format;
	r = qxl_gem_object_create_with_handle(qdev, file_priv,
					      QXL_GEM_DOMAIN_SURFACE,
					      args->size, &surf, &qobj,
					      &handle);
	if (r)
		return r;
	qobj->is_dumb = true;
	args->pitch = pitch;
	args->handle = handle;
	return 0;
}

int qxl_mode_dumb_mmap(struct drm_file *file_priv,
		       struct drm_device *dev,
		       uint32_t handle, uint64_t *offset_p)
{
	struct drm_gem_object *gobj;
	struct qxl_bo *qobj;

	BUG_ON(!offset_p);
	gobj = drm_gem_object_lookup(file_priv, handle);
	if (gobj == NULL)
		return -ENOENT;
	qobj = gem_to_qxl_bo(gobj);
	*offset_p = qxl_bo_mmap_offset(qobj);
	drm_gem_object_put_unlocked(gobj);
	return 0;
}
