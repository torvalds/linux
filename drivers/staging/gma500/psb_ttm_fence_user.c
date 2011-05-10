/**************************************************************************
 *
 * Copyright (c) 2006-2008 Tungsten Graphics, Inc., Cedar Park, TX., USA
 * All Rights Reserved.
 * Copyright (c) 2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thomas-at-tungstengraphics-dot-com>
 */

#include <drm/drmP.h>
#include "psb_ttm_fence_user.h"
#include "ttm/ttm_object.h"
#include "psb_ttm_fence_driver.h"
#include "psb_ttm_userobj_api.h"

/**
 * struct ttm_fence_user_object
 *
 * @base:    The base object used for user-space visibility and refcounting.
 *
 * @fence:   The fence object itself.
 *
 */

struct ttm_fence_user_object {
	struct ttm_base_object base;
	struct ttm_fence_object fence;
};

static struct ttm_fence_user_object *ttm_fence_user_object_lookup(
					struct ttm_object_file *tfile,
					uint32_t handle)
{
	struct ttm_base_object *base;

	base = ttm_base_object_lookup(tfile, handle);
	if (unlikely(base == NULL)) {
		printk(KERN_ERR "Invalid fence handle 0x%08lx\n",
		       (unsigned long)handle);
		return NULL;
	}

	if (unlikely(base->object_type != ttm_fence_type)) {
		ttm_base_object_unref(&base);
		printk(KERN_ERR "Invalid fence handle 0x%08lx\n",
		       (unsigned long)handle);
		return NULL;
	}

	return container_of(base, struct ttm_fence_user_object, base);
}

/*
 * The fence object destructor.
 */

static void ttm_fence_user_destroy(struct ttm_fence_object *fence)
{
	struct ttm_fence_user_object *ufence =
	    container_of(fence, struct ttm_fence_user_object, fence);

	ttm_mem_global_free(fence->fdev->mem_glob, sizeof(*ufence));
	kfree(ufence);
}

/*
 * The base object destructor. We basically unly unreference the
 * attached fence object.
 */

static void ttm_fence_user_release(struct ttm_base_object **p_base)
{
	struct ttm_fence_user_object *ufence;
	struct ttm_base_object *base = *p_base;
	struct ttm_fence_object *fence;

	*p_base = NULL;

	if (unlikely(base == NULL))
		return;

	ufence = container_of(base, struct ttm_fence_user_object, base);
	fence = &ufence->fence;
	ttm_fence_object_unref(&fence);
}

int
ttm_fence_user_create(struct ttm_fence_device *fdev,
		      struct ttm_object_file *tfile,
		      uint32_t fence_class,
		      uint32_t fence_types,
		      uint32_t create_flags,
		      struct ttm_fence_object **fence,
		      uint32_t *user_handle)
{
	int ret;
	struct ttm_fence_object *tmp;
	struct ttm_fence_user_object *ufence;

	ret = ttm_mem_global_alloc(fdev->mem_glob,
				   sizeof(*ufence),
				   false,
				   false);
	if (unlikely(ret != 0))
		return -ENOMEM;

	ufence = kmalloc(sizeof(*ufence), GFP_KERNEL);
	if (unlikely(ufence == NULL)) {
		ttm_mem_global_free(fdev->mem_glob, sizeof(*ufence));
		return -ENOMEM;
	}

	ret = ttm_fence_object_init(fdev,
				    fence_class,
				    fence_types, create_flags,
				    &ttm_fence_user_destroy, &ufence->fence);

	if (unlikely(ret != 0))
		goto out_err0;

	/*
	 * One fence ref is held by the fence ptr we return.
	 * The other one by the base object. Need to up the
	 * fence refcount before we publish this object to
	 * user-space.
	 */

	tmp = ttm_fence_object_ref(&ufence->fence);
	ret = ttm_base_object_init(tfile, &ufence->base,
				   false, ttm_fence_type,
				   &ttm_fence_user_release, NULL);

	if (unlikely(ret != 0))
		goto out_err1;

	*fence = &ufence->fence;
	*user_handle = ufence->base.hash.key;

	return 0;
out_err1:
	ttm_fence_object_unref(&tmp);
	tmp = &ufence->fence;
	ttm_fence_object_unref(&tmp);
	return ret;
out_err0:
	ttm_mem_global_free(fdev->mem_glob, sizeof(*ufence));
	kfree(ufence);
	return ret;
}

int ttm_fence_signaled_ioctl(struct ttm_object_file *tfile, void *data)
{
	int ret;
	union ttm_fence_signaled_arg *arg = data;
	struct ttm_fence_object *fence;
	struct ttm_fence_info info;
	struct ttm_fence_user_object *ufence;
	struct ttm_base_object *base;
	ret = 0;

	ufence = ttm_fence_user_object_lookup(tfile, arg->req.handle);
	if (unlikely(ufence == NULL))
		return -EINVAL;

	fence = &ufence->fence;

	if (arg->req.flush) {
		ret = ttm_fence_object_flush(fence, arg->req.fence_type);
		if (unlikely(ret != 0))
			goto out;
	}

	info = ttm_fence_get_info(fence);
	arg->rep.signaled_types = info.signaled_types;
	arg->rep.fence_error = info.error;

out:
	base = &ufence->base;
	ttm_base_object_unref(&base);
	return ret;
}

int ttm_fence_finish_ioctl(struct ttm_object_file *tfile, void *data)
{
	int ret;
	union ttm_fence_finish_arg *arg = data;
	struct ttm_fence_user_object *ufence;
	struct ttm_base_object *base;
	struct ttm_fence_object *fence;
	ret = 0;

	ufence = ttm_fence_user_object_lookup(tfile, arg->req.handle);
	if (unlikely(ufence == NULL))
		return -EINVAL;

	fence = &ufence->fence;

	ret = ttm_fence_object_wait(fence,
				    arg->req.mode & TTM_FENCE_FINISH_MODE_LAZY,
				    true, arg->req.fence_type);
	if (likely(ret == 0)) {
		struct ttm_fence_info info = ttm_fence_get_info(fence);

		arg->rep.signaled_types = info.signaled_types;
		arg->rep.fence_error = info.error;
	}

	base = &ufence->base;
	ttm_base_object_unref(&base);

	return ret;
}

int ttm_fence_unref_ioctl(struct ttm_object_file *tfile, void *data)
{
	struct ttm_fence_unref_arg *arg = data;
	int ret = 0;

	ret = ttm_ref_object_base_unref(tfile, arg->handle, ttm_fence_type);
	return ret;
}
