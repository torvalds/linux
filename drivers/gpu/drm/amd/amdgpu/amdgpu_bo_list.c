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

#define AMDGPU_BO_LIST_MAX_PRIORITY	32u
#define AMDGPU_BO_LIST_NUM_BUCKETS	(AMDGPU_BO_LIST_MAX_PRIORITY + 1)

static int amdgpu_bo_list_set(struct amdgpu_device *adev,
				     struct drm_file *filp,
				     struct amdgpu_bo_list *list,
				     struct drm_amdgpu_bo_list_entry *info,
				     unsigned num_entries);

static void amdgpu_bo_list_release_rcu(struct kref *ref)
{
	unsigned i;
	struct amdgpu_bo_list *list = container_of(ref, struct amdgpu_bo_list,
						   refcount);

	for (i = 0; i < list->num_entries; ++i)
		amdgpu_bo_unref(&list->array[i].robj);

	mutex_destroy(&list->lock);
	kvfree(list->array);
	kfree_rcu(list, rhead);
}

static int amdgpu_bo_list_create(struct amdgpu_device *adev,
				 struct drm_file *filp,
				 struct drm_amdgpu_bo_list_entry *info,
				 unsigned num_entries,
				 int *id)
{
	int r;
	struct amdgpu_fpriv *fpriv = filp->driver_priv;
	struct amdgpu_bo_list *list;

	list = kzalloc(sizeof(struct amdgpu_bo_list), GFP_KERNEL);
	if (!list)
		return -ENOMEM;

	/* initialize bo list*/
	mutex_init(&list->lock);
	kref_init(&list->refcount);
	r = amdgpu_bo_list_set(adev, filp, list, info, num_entries);
	if (r) {
		kfree(list);
		return r;
	}

	/* idr alloc should be called only after initialization of bo list. */
	mutex_lock(&fpriv->bo_list_lock);
	r = idr_alloc(&fpriv->bo_list_handles, list, 1, 0, GFP_KERNEL);
	mutex_unlock(&fpriv->bo_list_lock);
	if (r < 0) {
		amdgpu_bo_list_free(list);
		return r;
	}
	*id = r;

	return 0;
}

static void amdgpu_bo_list_destroy(struct amdgpu_fpriv *fpriv, int id)
{
	struct amdgpu_bo_list *list;

	mutex_lock(&fpriv->bo_list_lock);
	list = idr_remove(&fpriv->bo_list_handles, id);
	mutex_unlock(&fpriv->bo_list_lock);
	if (list)
		kref_put(&list->refcount, amdgpu_bo_list_release_rcu);
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

	unsigned last_entry = 0, first_userptr = num_entries;
	unsigned i;
	int r;
	unsigned long total_size = 0;

	array = kvmalloc_array(num_entries, sizeof(struct amdgpu_bo_list_entry), GFP_KERNEL);
	if (!array)
		return -ENOMEM;
	memset(array, 0, num_entries * sizeof(struct amdgpu_bo_list_entry));

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
		drm_gem_object_unreference_unlocked(gobj);

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

		entry->robj = bo;
		entry->priority = min(info[i].bo_priority,
				      AMDGPU_BO_LIST_MAX_PRIORITY);
		entry->tv.bo = &entry->robj->tbo;
		entry->tv.shared = !entry->robj->prime_shared_count;

		if (entry->robj->prefered_domains == AMDGPU_GEM_DOMAIN_GDS)
			gds_obj = entry->robj;
		if (entry->robj->prefered_domains == AMDGPU_GEM_DOMAIN_GWS)
			gws_obj = entry->robj;
		if (entry->robj->prefered_domains == AMDGPU_GEM_DOMAIN_OA)
			oa_obj = entry->robj;

		total_size += amdgpu_bo_size(entry->robj);
		trace_amdgpu_bo_list_set(list, entry->robj);
	}

	for (i = 0; i < list->num_entries; ++i)
		amdgpu_bo_unref(&list->array[i].robj);

	kvfree(list->array);

	list->gds_obj = gds_obj;
	list->gws_obj = gws_obj;
	list->oa_obj = oa_obj;
	list->first_userptr = first_userptr;
	list->array = array;
	list->num_entries = num_entries;

	trace_amdgpu_cs_bo_status(list->num_entries, total_size);
	return 0;

error_free:
	while (i--)
		amdgpu_bo_unref(&array[i].robj);
	kvfree(array);
	return r;
}

struct amdgpu_bo_list *
amdgpu_bo_list_get(struct amdgpu_fpriv *fpriv, int id)
{
	struct amdgpu_bo_list *result;

	rcu_read_lock();
	result = idr_find(&fpriv->bo_list_handles, id);

	if (result) {
		if (kref_get_unless_zero(&result->refcount))
			mutex_lock(&result->lock);
		else
			result = NULL;
	}
	rcu_read_unlock();

	return result;
}

void amdgpu_bo_list_get_list(struct amdgpu_bo_list *list,
			     struct list_head *validated)
{
	/* This is based on the bucket sort with O(n) time complexity.
	 * An item with priority "i" is added to bucket[i]. The lists are then
	 * concatenated in descending order.
	 */
	struct list_head bucket[AMDGPU_BO_LIST_NUM_BUCKETS];
	unsigned i;

	for (i = 0; i < AMDGPU_BO_LIST_NUM_BUCKETS; i++)
		INIT_LIST_HEAD(&bucket[i]);

	/* Since buffers which appear sooner in the relocation list are
	 * likely to be used more often than buffers which appear later
	 * in the list, the sort mustn't change the ordering of buffers
	 * with the same priority, i.e. it must be stable.
	 */
	for (i = 0; i < list->num_entries; i++) {
		unsigned priority = list->array[i].priority;

		list_add_tail(&list->array[i].tv.head,
			      &bucket[priority]);
		list->array[i].user_pages = NULL;
	}

	/* Connect the sorted buckets in the output list. */
	for (i = 0; i < AMDGPU_BO_LIST_NUM_BUCKETS; i++)
		list_splice(&bucket[i], validated);
}

void amdgpu_bo_list_put(struct amdgpu_bo_list *list)
{
	mutex_unlock(&list->lock);
	kref_put(&list->refcount, amdgpu_bo_list_release_rcu);
}

void amdgpu_bo_list_free(struct amdgpu_bo_list *list)
{
	unsigned i;

	for (i = 0; i < list->num_entries; ++i)
		amdgpu_bo_unref(&list->array[i].robj);

	mutex_destroy(&list->lock);
	kvfree(list->array);
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
	const void __user *uptr = (const void*)(uintptr_t)args->in.bo_info_ptr;

	struct drm_amdgpu_bo_list_entry *info;
	struct amdgpu_bo_list *list;

	int r;

	info = kvmalloc_array(args->in.bo_number,
			     sizeof(struct drm_amdgpu_bo_list_entry), GFP_KERNEL);
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
		r = amdgpu_bo_list_create(adev, filp, info, args->in.bo_number,
					  &handle);
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
	kvfree(info);

	return 0;

error_free:
	kvfree(info);
	return r;
}
