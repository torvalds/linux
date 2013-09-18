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

#include "drmP.h"
#include "drm/drm.h"
#include "qxl_drv.h"
#include "qxl_object.h"

int qxl_gem_object_init(struct drm_gem_object *obj)
{
	/* we do nothings here */
	return 0;
}

void qxl_gem_object_free(struct drm_gem_object *gobj)
{
	struct qxl_bo *qobj = gem_to_qxl_bo(gobj);

	if (qobj)
		qxl_bo_unref(&qobj);
}

int qxl_gem_object_create(struct qxl_device *qdev, int size,
			  int alignment, int initial_domain,
			  bool discardable, bool kernel,
			  struct qxl_surface *surf,
			  struct drm_gem_object **obj)
{
	struct qxl_bo *qbo;
	int r;

	*obj = NULL;
	/* At least align on page size */
	if (alignment < PAGE_SIZE)
		alignment = PAGE_SIZE;
	r = qxl_bo_create(qdev, size, kernel, false, initial_domain, surf, &qbo);
	if (r) {
		if (r != -ERESTARTSYS)
			DRM_ERROR(
			"Failed to allocate GEM object (%d, %d, %u, %d)\n",
				  size, initial_domain, alignment, r);
		return r;
	}
	*obj = &qbo->gem_base;

	mutex_lock(&qdev->gem.mutex);
	list_add_tail(&qbo->list, &qdev->gem.objects);
	mutex_unlock(&qdev->gem.mutex);

	return 0;
}

int qxl_gem_object_create_with_handle(struct qxl_device *qdev,
				      struct drm_file *file_priv,
				      u32 domain,
				      size_t size,
				      struct qxl_surface *surf,
				      struct qxl_bo **qobj,
				      uint32_t *handle)
{
	struct drm_gem_object *gobj;
	int r;

	BUG_ON(!qobj);
	BUG_ON(!handle);

	r = qxl_gem_object_create(qdev, size, 0,
				  domain,
				  false, false, surf,
				  &gobj);
	if (r)
		return -ENOMEM;
	r = drm_gem_handle_create(file_priv, gobj, handle);
	if (r)
		return r;
	/* drop reference from allocate - handle holds it now */
	*qobj = gem_to_qxl_bo(gobj);
	drm_gem_object_unreference_unlocked(gobj);
	return 0;
}

int qxl_gem_object_pin(struct drm_gem_object *obj, uint32_t pin_domain,
			  uint64_t *gpu_addr)
{
	struct qxl_bo *qobj = obj->driver_private;
	int r;

	r = qxl_bo_reserve(qobj, false);
	if (unlikely(r != 0))
		return r;
	r = qxl_bo_pin(qobj, pin_domain, gpu_addr);
	qxl_bo_unreserve(qobj);
	return r;
}

void qxl_gem_object_unpin(struct drm_gem_object *obj)
{
	struct qxl_bo *qobj = obj->driver_private;
	int r;

	r = qxl_bo_reserve(qobj, false);
	if (likely(r == 0)) {
		qxl_bo_unpin(qobj);
		qxl_bo_unreserve(qobj);
	}
}

int qxl_gem_object_open(struct drm_gem_object *obj, struct drm_file *file_priv)
{
	return 0;
}

void qxl_gem_object_close(struct drm_gem_object *obj,
			  struct drm_file *file_priv)
{
}

int qxl_gem_init(struct qxl_device *qdev)
{
	INIT_LIST_HEAD(&qdev->gem.objects);
	return 0;
}

void qxl_gem_fini(struct qxl_device *qdev)
{
	qxl_bo_force_delete(qdev);
}
