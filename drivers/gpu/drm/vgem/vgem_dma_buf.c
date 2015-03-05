/*
 * Copyright © 2012 Intel Corporation
 * Copyright © 2014 The Chromium OS Authors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Ben Widawsky <ben@bwidawsk.net>
 *
 */

#include <linux/dma-buf.h>
#include "vgem_drv.h"

struct sg_table *vgem_gem_prime_get_sg_table(struct drm_gem_object *gobj)
{
	struct drm_vgem_gem_object *obj = to_vgem_bo(gobj);
	BUG_ON(obj->pages == NULL);

	return drm_prime_pages_to_sg(obj->pages, obj->base.size / PAGE_SIZE);
}

int vgem_gem_prime_pin(struct drm_gem_object *gobj)
{
	struct drm_vgem_gem_object *obj = to_vgem_bo(gobj);
	return vgem_gem_get_pages(obj);
}

void vgem_gem_prime_unpin(struct drm_gem_object *gobj)
{
	struct drm_vgem_gem_object *obj = to_vgem_bo(gobj);
	vgem_gem_put_pages(obj);
}

void *vgem_gem_prime_vmap(struct drm_gem_object *gobj)
{
	struct drm_vgem_gem_object *obj = to_vgem_bo(gobj);
	BUG_ON(obj->pages == NULL);

	return vmap(obj->pages, obj->base.size / PAGE_SIZE, 0, PAGE_KERNEL);
}

void vgem_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr)
{
	vunmap(vaddr);
}

struct drm_gem_object *vgem_gem_prime_import(struct drm_device *dev,
					     struct dma_buf *dma_buf)
{
	struct drm_vgem_gem_object *obj = NULL;
	int ret;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (obj == NULL) {
		ret = -ENOMEM;
		goto fail;
	}

	ret = drm_gem_object_init(dev, &obj->base, dma_buf->size);
	if (ret) {
		ret = -ENOMEM;
		goto fail_free;
	}

	get_dma_buf(dma_buf);

	obj->base.dma_buf = dma_buf;
	obj->use_dma_buf = true;

	return &obj->base;

fail_free:
	kfree(obj);
fail:
	return ERR_PTR(ret);
}
