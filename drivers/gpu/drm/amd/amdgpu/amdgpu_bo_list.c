/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */
/*
 * Authors:
 *    Christian König <deathsimple@vodafone.de>
 */

#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_trace.h"

static int amdgpu_bo_list_create(struct amdgpu_fpriv *fpriv,
				 struct amdgpu_bo_list **result,
				 int *id)
{
	int r;

	*result = kzalloc(sizeof(struct amdgpu_bo_list), GFP_KERNEL);
	if (!*result)
		return -ENOMEM;

	mutex_lock(&fpriv->bo_list_lock);
	r = idr_alloc(&fpriv->bo_list_handles, *result,
		      1, 0, GFP_KERNEL);
	if (r < 0) {
		mutex_unlock(&fpriv->bo_list_lock);
		kfree(*result);
		return r;
	}
	*id = r;

	mutex_init(&(*result)->lock);
	(*result)->num_entries = 0;
	(*result)->array = NULL;

	mutex_lock(&(*result)->lock);
	mutex_unlock(&fpriv->bo_list_lock);

	return 0;
}

static void amdgpu_bo_list_destroy(struct amdgpu_fpriv *fpriv, int id)
{
	struct amdgpu_bo_list *list;

	mutex_lock(&fpriv->bo_list_lock);
	list = idr_find(&fpriv->bo_list_handles, id);
	if (list) {
		mutex_lock(&list->lock);
		idr_remove(&fpriv->bo_list_handles, id);
		mutex_unlock(&list->lock);
		amdgpu_bo_list_free(list);
	}
	mutex_unlock(&fpriv->bo_list_lock);
}

static int amdgpu_bo_list_set(struct amdgpu_device *adev,
				     struct drm_file *filp,
				     struct amdgpu_bo_list *list,
				     struct drm_amdgpu_bo_list_entry *info,
				     unsigned num_entries)
{
	struct amdgpu_bo_list_entry *array;
	struct amdgpu_bo *gds_obj = adev->gds.gds_gfx_bo;
	struct amdgpu_bo *gws_obj = adev->gds.gws_gfx_bo;
	struct amdgpu_bo *oa_obj = adev->gds.oa_gfx_bo;

	bool has_userptr = false;
	unsigned i;

	array = drm_malloc_ab(num_entries, sizeof(struct amdgpu_bo_list_entry));
	if (!array)
		return -ENOMEM;
	memset(array, 0, num_entries * sizeof(struct amdgpu_bo_list_entry));

	for (i = 0; i < num_entries; ++i) {
		struct amdgpu_bo_list_entry *entry = &array[i];
		struct drm_gem_object *gobj;

		gobj = drm_gem_object_lookup(adev->ddev, filp, info[i].bo_handle);
		if (!gobj)
			goto error_free;

		entry->robj = amdgpu_bo_ref(gem_to_amdgpu_bo(gobj));
		drm_gem_object_unreference_unlocked(gobj);
		entry->priority = info[i].bo_priority;
		entry->prefered_domains = entry->robj->initial_domain;
		entry->allowed_domains = entry->prefered_domains;
		if (entry->allowed_domains == AMDGPU_GEM_DOMAIN_VRAM)
			entry->allowed_domains |= AMDGPU_GEM_DOMAIN_GTT;
		if (amdgpu_ttm_tt_has_userptr(entry->robj->tbo.ttm)) {
			has_userptr = true;
			entry->prefered_domains = AMDGPU_GEM_DOMAIN_GTT;
			entry->allowed_domains = AMDGPU_GEM_DOMAIN_GTT;
		}
		entry->tv.bo = &entry->robj->tbo;
		entry->tv.shared = true;

		if (entry->prefered_domains == AMDGPU_GEM_DOMAIN_GDS)
			gds_obj = entry->robj;
		if (entry->prefered_domains == AMDGPU_GEM_DOMAIN_GWS)
			gws_obj = entry->robj;
		if (entry->prefered_domains == AMDGPU_GEM_DOMAIN_OA)
			oa_obj = entry->robj;

		trace_amdgpu_bo_list_set(list, entry->robj);
	}

	for (i = 0; i < list->num_entries; ++i)
		amdgpu_bo_unref(&list->array[i].robj);

	drm_free_large(list->array);

	list->gds_obj = gds_obj;
	list->gws_obj = gws_obj;
	list->oa_obj = oa_obj;
	list->has_userptr = has_userptr;
	list->array = array;
	list->num_entries = num_entries;

	return 0;

error_free:
	drm_free_large(array);
	return -ENOENT;
}

struct amdgpu_bo_list *
amdgpu_bo_list_get(struct amdgpu_fpriv *fpriv, int id)
{
	struct amdgpu_bo_list *result;

	mutex_lock(&fpriv->bo_list_lock);
	result = idr_find(&fpriv->bo_list_handles, id);
	if (result)
		mutex_lock(&result->lock);
	mutex_unlock(&fpriv->bo_list_lock);
	return result;
}

void amdgpu_bo_list_put(struct amdgpu_bo_list *list)
{
	mutex_unlock(&list->lock);
}

void amdgpu_bo_list_free(struct amdgpu_bo_list *list)
{
	unsigned i;

	for (i = 0; i < list->num_entries; ++i)
		amdgpu_bo_unref(&list->array[i].robj);

	mutex_destroy(&list->lock);
	drm_free_large(list->array);
	kfree(list);
}

int amdgpu_bo_list_ioctl(struct drm_device *dev, void *data,
				struct drm_file *filp)
{
	const uint32_t info_size = sizeof(struct drm_amdgpu_bo_list_entry);

	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_fpriv *fpriv = filp->driver_priv;
	union drm_amdgpu_bo_list *args = data;
	uint32_t handle = args->in.list_handle;
	const void __user *uptr = (const void*)(long)args->in.bo_info_ptr;

	struct drm_amdgpu_bo_list_entry *info;
	struct amdgpu_bo_list *list;

	int r;

	info = drm_malloc_ab(args->in.bo_number,
			     sizeof(struct drm_amdgpu_bo_list_entry));
	if (!info)
		return -ENOMEM;

	/* copy the handle array from userspace to a kernel buffer */
	r = -EFAULT;
	if (likely(info_size == args->in.bo_info_size)) {
		unsigned long bytes = args->in.bo_number *
			args->in.bo_info_size;

		if (copy_from_user(info, uptr, bytes))
			goto error_free;

	} else {
		unsigned long bytes = min(args->in.bo_info_size, info_size);
		unsigned i;

		memset(info, 0, args->in.bo_number * info_size);
		for (i = 0; i < args->in.bo_number; ++i) {
			if (copy_from_user(&info[i], uptr, bytes))
				goto error_free;
			
			uptr += args->in.bo_info_size;
		}
	}

	switch (args->in.operation) {
	case AMDGPU_BO_LIST_OP_CREATE:
		r = amdgpu_bo_list_create(fpriv, &list, &handle);
		if (r) 
			goto error_free;

		r = amdgpu_bo_list_set(adev, filp, list, info,
					      args->in.bo_number);
		amdgpu_bo_list_put(list);
		if (r)
			goto error_free;

		break;
		
	case AMDGPU_BO_LIST_OP_DESTROY:
		amdgpu_bo_list_destroy(fpriv, handle);
		handle = 0;
		break;

	case AMDGPU_BO_LIST_OP_UPDATE:
		r = -ENOENT;
		list = amdgpu_bo_list_get(fpriv, handle);
		if (!list)
			goto error_free;

		r = amdgpu_bo_list_set(adev, filp, list, info,
					      args->in.bo_number);
		amdgpu_bo_list_put(list);
		if (r)
			goto error_free;

		break;

	default:
		r = -EINVAL;
		goto error_free;
	}

	memset(args, 0, sizeof(*args));
	args->out.list_handle = handle;
	drm_free_large(info);

	return 0;

error_free:
	drm_free_large(info);
	return r;
}
