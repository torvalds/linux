/*
 * Copyright 2014 Canonical
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
 * Authors: Andreas Pokorny
 */

#include "qxl_drv.h"
#include "qxl_object.h"

/* Empty Implementations as there should not be any other driver for a virtual
 * device that might share buffers with qxl */

int qxl_gem_prime_pin(struct drm_gem_object *obj)
{
	struct qxl_bo *bo = gem_to_qxl_bo(obj);

	return qxl_bo_pin(bo);
}

void qxl_gem_prime_unpin(struct drm_gem_object *obj)
{
	struct qxl_bo *bo = gem_to_qxl_bo(obj);

	qxl_bo_unpin(bo);
}

struct sg_table *qxl_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	return ERR_PTR(-ENOSYS);
}

struct drm_gem_object *qxl_gem_prime_import_sg_table(
	struct drm_device *dev, struct dma_buf_attachment *attach,
	struct sg_table *table)
{
	return ERR_PTR(-ENOSYS);
}

int qxl_gem_prime_vmap(struct drm_gem_object *obj, struct dma_buf_map *map)
{
	struct qxl_bo *bo = gem_to_qxl_bo(obj);
	int ret;

	ret = qxl_bo_kmap(bo, map);
	if (ret < 0)
		return ret;

	return 0;
}

void qxl_gem_prime_vunmap(struct drm_gem_object *obj,
			  struct dma_buf_map *map)
{
	struct qxl_bo *bo = gem_to_qxl_bo(obj);

	qxl_bo_kunmap(bo);
}

int qxl_gem_prime_mmap(struct drm_gem_object *obj,
		       struct vm_area_struct *area)
{
	return -ENOSYS;
}
