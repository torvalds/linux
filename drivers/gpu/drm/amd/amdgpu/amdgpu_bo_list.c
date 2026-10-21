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

#include <linux/sort.h>
#include <linux/uaccess.h>

#include "amdgpu.h"
#include "amdgpu_trace.h"

#define AMDGPU_BO_LIST_MAX_PRIORITY	32u
#define AMDGPU_BO_LIST_NUM_BUCKETS	(AMDGPU_BO_LIST_MAX_PRIORITY + 1)
#define AMDGPU_BO_LIST_MAX_ENTRIES	(128 * 1024)

static void amdgpu_bo_list_free(struct kref *ref)
{
	struct amdgpu_bo_list *list = container_of(ref, struct amdgpu_bo_list,
						   refcount);
	struct amdgpu_bo_list_entry *e;

	amdgpu_bo_list_for_each_entry(e, list)
		amdgpu_bo_unref(&e->bo);

	kvfree(list);
}

static int amdgpu_bo_list_entry_cmp(const void *_a, const void *_b)
{
	const struct amdgpu_bo_list_entry *a = _a, *b = _b;

	BUILD_BUG_ON(AMDGPU_BO_LIST_MAX_PRIORITY >= INT_MAX);

	return (int)a->priority - (int)b->priority;
}

struct amdgpu_bo_list *
amdgpu_bo_list_create(struct amdgpu_device *adev, struct drm_file *filp,
		      struct drm_amdgpu_bo_list_entry *info, size_t num_entries)
{
	unsigned last_entry = 0, first_userptr = num_entries;
	struct amdgpu_bo_list_entry *array;
	struct amdgpu_bo_list *list;
	uint64_t total_size = 0;
	unsigned i;
	int r;

	list = kvzalloc_flex(*list, entries, num_entries);
	if (!list)
		return ERR_PTR(-ENOMEM);

	kref_init(&list->refcount);

	list->num_entries = num_entries;
	array = list->entries;

	for (i = 0; i < num_entries; ++i) {
		struct amdgpu_bo_list_entry *entry;
		struct drm_gem_object *gobj;
		struct amdgpu_bo *bo;
		struct mm_struct *usermm;

		gobj = drm_gem_object_lookup(filp, info[i].bo_handle);
		if (!gobj) {
			r = -ENOENT;
			goto error_free;
		}

		bo = amdgpu_bo_ref(gem_to_amdgpu_bo(gobj));
		drm_gem_object_put(gobj);

		usermm = amdgpu_ttm_tt_get_usermm(bo->tbo.ttm);
		if (usermm) {
			if (usermm != current->mm) {
				amdgpu_bo_unref(&bo);
				r = -EPERM;
				goto error_free;
			}
			entry = &array[--first_userptr];
		} else {
			entry = &array[last_entry++];
		}

		entry->priority = min(info[i].bo_priority,
				      AMDGPU_BO_LIST_MAX_PRIORITY);
		entry->bo = bo;

		if (bo->preferred_domains == AMDGPU_GEM_DOMAIN_GDS)
			list->gds_obj = bo;
		if (bo->preferred_domains == AMDGPU_GEM_DOMAIN_GWS)
			list->gws_obj = bo;
		if (bo->preferred_domains == AMDGPU_GEM_DOMAIN_OA)
			list->oa_obj = bo;

		total_size += amdgpu_bo_size(bo);
		trace_amdgpu_bo_list_set(list, bo);
	}

	list->first_userptr = first_userptr;
	sort(array, last_entry, sizeof(struct amdgpu_bo_list_entry),
	     amdgpu_bo_list_entry_cmp, NULL);

	trace_amdgpu_cs_bo_status(list->num_entries, total_size);

	return list;

error_free:
	for (i = 0; i < last_entry; ++i)
		amdgpu_bo_unref(&array[i].bo);
	for (i = first_userptr; i < num_entries; ++i)
		amdgpu_bo_unref(&array[i].bo);
	kvfree(list);
	return ERR_PTR(r);

}

struct amdgpu_bo_list *amdgpu_bo_list_get(struct amdgpu_fpriv *fpriv, u32 id)
{
	struct amdgpu_bo_list *list;

	xa_lock(&fpriv->bo_list_handles);
	list = xa_load(&fpriv->bo_list_handles, id);
	if (list)
		kref_get(&list->refcount);
	else
		list = ERR_PTR(-ENOENT);
	xa_unlock(&fpriv->bo_list_handles);

	return list;
}

void amdgpu_bo_list_put(struct amdgpu_bo_list *list)
{
	if (list)
		kref_put(&list->refcount, amdgpu_bo_list_free);
}

struct drm_amdgpu_bo_list_entry *
amdgpu_bo_create_list_entry_array(struct drm_amdgpu_bo_list_in *in)
{
	const void __user *uptr = u64_to_user_ptr(in->bo_info_ptr);
	const uint32_t bo_number = in->bo_number;

	if (bo_number > AMDGPU_BO_LIST_MAX_ENTRIES)
		return ERR_PTR(-EINVAL);

	if (in->bo_info_size != sizeof(struct drm_amdgpu_bo_list_entry))
		return ERR_PTR(-EINVAL);

	return vmemdup_array_user(uptr, bo_number,
				  sizeof(struct drm_amdgpu_bo_list_entry));
}

int amdgpu_bo_list_ioctl(struct drm_device *dev, void *data,
				struct drm_file *filp)
{
	struct amdgpu_fpriv *fpriv = filp->driver_priv;
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct amdgpu_bo_list *list, *prev, *curr;
	union drm_amdgpu_bo_list *args = data;
	uint32_t handle = args->in.list_handle;
	struct drm_amdgpu_bo_list_entry *info;
	int r;

	switch (args->in.operation) {
	case AMDGPU_BO_LIST_OP_CREATE:
	case AMDGPU_BO_LIST_OP_UPDATE:
		info = amdgpu_bo_create_list_entry_array(&args->in);
		if (IS_ERR(info))
			return PTR_ERR(info);

		list = amdgpu_bo_list_create(adev, filp, info,
					     args->in.bo_number);
		kvfree(info);
		if (IS_ERR(list))
			return PTR_ERR(list);

		break;

	case AMDGPU_BO_LIST_OP_DESTROY:
		list = xa_erase(&fpriv->bo_list_handles, handle);
		amdgpu_bo_list_put(list);
		handle = 0;

		break;

	default:
		return -EINVAL;
	};

	switch (args->in.operation) {
	case AMDGPU_BO_LIST_OP_CREATE:
		r = xa_alloc(&fpriv->bo_list_handles, &handle, list,
			     xa_limit_32b, GFP_KERNEL);
		if (r)
			goto error_put_list;

		break;

	case AMDGPU_BO_LIST_OP_UPDATE:
		curr = xa_load(&fpriv->bo_list_handles, handle);
		if (!curr) {
			r = -ENOENT;
			goto error_put_list;
		}

		prev = xa_cmpxchg(&fpriv->bo_list_handles, handle, curr, list,
				  GFP_KERNEL);
		if (xa_is_err(prev)) {
			r = xa_err(prev);
			goto error_put_list;
		} else if (prev != curr) {
			r = -ENOENT;
			goto error_put_list;
		}

		amdgpu_bo_list_put(curr);
		break;

	case AMDGPU_BO_LIST_OP_DESTROY:
	default:
		/* Handled above. */
		break;
	}

	memset(args, 0, sizeof(*args));
	args->out.list_handle = handle;

	return 0;

error_put_list:
	amdgpu_bo_list_put(list);
	return r;
}
