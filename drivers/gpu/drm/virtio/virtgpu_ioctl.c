/*
 * Copyright (C) 2015 Red Hat, Inc.
 * All Rights Reserved.
 *
 * Authors:
 *    Dave Airlie
 *    Alon Levy
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
 */

#include <drm/drmP.h>
#include "virtgpu_drv.h"
#include <drm/virtgpu_drm.h>
#include "ttm/ttm_execbuf_util.h"

static void convert_to_hw_box(struct virtio_gpu_box *dst,
			      const struct drm_virtgpu_3d_box *src)
{
	dst->x = cpu_to_le32(src->x);
	dst->y = cpu_to_le32(src->y);
	dst->z = cpu_to_le32(src->z);
	dst->w = cpu_to_le32(src->w);
	dst->h = cpu_to_le32(src->h);
	dst->d = cpu_to_le32(src->d);
}

static int virtio_gpu_map_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct drm_virtgpu_map *virtio_gpu_map = data;

	return virtio_gpu_mode_dumb_mmap(file_priv, vgdev->ddev,
					 virtio_gpu_map->handle,
					 &virtio_gpu_map->offset);
}

static int virtio_gpu_object_list_validate(struct ww_acquire_ctx *ticket,
					   struct list_head *head)
{
	struct ttm_validate_buffer *buf;
	struct ttm_buffer_object *bo;
	struct virtio_gpu_object *qobj;
	int ret;

	ret = ttm_eu_reserve_buffers(ticket, head, true, NULL);
	if (ret != 0)
		return ret;

	list_for_each_entry(buf, head, head) {
		bo = buf->bo;
		qobj = container_of(bo, struct virtio_gpu_object, tbo);
		ret = ttm_bo_validate(bo, &qobj->placement, false, false);
		if (ret) {
			ttm_eu_backoff_reservation(ticket, head);
			return ret;
		}
	}
	return 0;
}

static void virtio_gpu_unref_list(struct list_head *head)
{
	struct ttm_validate_buffer *buf;
	struct ttm_buffer_object *bo;
	struct virtio_gpu_object *qobj;
	list_for_each_entry(buf, head, head) {
		bo = buf->bo;
		qobj = container_of(bo, struct virtio_gpu_object, tbo);

		drm_gem_object_unreference_unlocked(&qobj->gem_base);
	}
}

static int virtio_gpu_execbuffer(struct drm_device *dev,
				 struct drm_virtgpu_execbuffer *exbuf,
				 struct drm_file *drm_file)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct virtio_gpu_fpriv *vfpriv = drm_file->driver_priv;
	struct drm_gem_object *gobj;
	struct virtio_gpu_fence *fence;
	struct virtio_gpu_object *qobj;
	int ret;
	uint32_t *bo_handles = NULL;
	void __user *user_bo_handles = NULL;
	struct list_head validate_list;
	struct ttm_validate_buffer *buflist = NULL;
	int i;
	struct ww_acquire_ctx ticket;
	void *buf;

	if (vgdev->has_virgl_3d == false)
		return -ENOSYS;

	INIT_LIST_HEAD(&validate_list);
	if (exbuf->num_bo_handles) {

		bo_handles = drm_malloc_ab(exbuf->num_bo_handles,
					   sizeof(uint32_t));
		buflist = drm_calloc_large(exbuf->num_bo_handles,
					   sizeof(struct ttm_validate_buffer));
		if (!bo_handles || !buflist) {
			drm_free_large(bo_handles);
			drm_free_large(buflist);
			return -ENOMEM;
		}

		user_bo_handles = (void __user *)(uintptr_t)exbuf->bo_handles;
		if (copy_from_user(bo_handles, user_bo_handles,
				   exbuf->num_bo_handles * sizeof(uint32_t))) {
			ret = -EFAULT;
			drm_free_large(bo_handles);
			drm_free_large(buflist);
			return ret;
		}

		for (i = 0; i < exbuf->num_bo_handles; i++) {
			gobj = drm_gem_object_lookup(drm_file, bo_handles[i]);
			if (!gobj) {
				drm_free_large(bo_handles);
				drm_free_large(buflist);
				return -ENOENT;
			}

			qobj = gem_to_virtio_gpu_obj(gobj);
			buflist[i].bo = &qobj->tbo;

			list_add(&buflist[i].head, &validate_list);
		}
		drm_free_large(bo_handles);
	}

	ret = virtio_gpu_object_list_validate(&ticket, &validate_list);
	if (ret)
		goto out_free;

	buf = memdup_user((void __user *)(uintptr_t)exbuf->command,
			  exbuf->size);
	if (IS_ERR(buf)) {
		ret = PTR_ERR(buf);
		goto out_unresv;
	}
	virtio_gpu_cmd_submit(vgdev, buf, exbuf->size,
			      vfpriv->ctx_id, &fence);

	ttm_eu_fence_buffer_objects(&ticket, &validate_list, &fence->f);

	/* fence the command bo */
	virtio_gpu_unref_list(&validate_list);
	drm_free_large(buflist);
	fence_put(&fence->f);
	return 0;

out_unresv:
	ttm_eu_backoff_reservation(&ticket, &validate_list);
out_free:
	virtio_gpu_unref_list(&validate_list);
	drm_free_large(buflist);
	return ret;
}

/*
 * Usage of execbuffer:
 * Relocations need to take into account the full VIRTIO_GPUDrawable size.
 * However, the command as passed from user space must *not* contain the initial
 * VIRTIO_GPUReleaseInfo struct (first XXX bytes)
 */
static int virtio_gpu_execbuffer_ioctl(struct drm_device *dev, void *data,
				       struct drm_file *file_priv)
{
	struct drm_virtgpu_execbuffer *execbuffer = data;
	return virtio_gpu_execbuffer(dev, execbuffer, file_priv);
}


static int virtio_gpu_getparam_ioctl(struct drm_device *dev, void *data,
				     struct drm_file *file_priv)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct drm_virtgpu_getparam *param = data;
	int value;

	switch (param->param) {
	case VIRTGPU_PARAM_3D_FEATURES:
		value = vgdev->has_virgl_3d == true ? 1 : 0;
		break;
	default:
		return -EINVAL;
	}
	if (copy_to_user((void __user *)(unsigned long)param->value,
			 &value, sizeof(int))) {
		return -EFAULT;
	}
	return 0;
}

static int virtio_gpu_resource_create_ioctl(struct drm_device *dev, void *data,
					    struct drm_file *file_priv)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct drm_virtgpu_resource_create *rc = data;
	int ret;
	uint32_t res_id;
	struct virtio_gpu_object *qobj;
	struct drm_gem_object *obj;
	uint32_t handle = 0;
	uint32_t size;
	struct list_head validate_list;
	struct ttm_validate_buffer mainbuf;
	struct virtio_gpu_fence *fence = NULL;
	struct ww_acquire_ctx ticket;
	struct virtio_gpu_resource_create_3d rc_3d;

	if (vgdev->has_virgl_3d == false) {
		if (rc->depth > 1)
			return -EINVAL;
		if (rc->nr_samples > 1)
			return -EINVAL;
		if (rc->last_level > 1)
			return -EINVAL;
		if (rc->target != 2)
			return -EINVAL;
		if (rc->array_size > 1)
			return -EINVAL;
	}

	INIT_LIST_HEAD(&validate_list);
	memset(&mainbuf, 0, sizeof(struct ttm_validate_buffer));

	virtio_gpu_resource_id_get(vgdev, &res_id);

	size = rc->size;

	/* allocate a single page size object */
	if (size == 0)
		size = PAGE_SIZE;

	qobj = virtio_gpu_alloc_object(dev, size, false, false);
	if (IS_ERR(qobj)) {
		ret = PTR_ERR(qobj);
		goto fail_id;
	}
	obj = &qobj->gem_base;

	if (!vgdev->has_virgl_3d) {
		virtio_gpu_cmd_create_resource(vgdev, res_id, rc->format,
					       rc->width, rc->height);

		ret = virtio_gpu_object_attach(vgdev, qobj, res_id, NULL);
	} else {
		/* use a gem reference since unref list undoes them */
		drm_gem_object_reference(&qobj->gem_base);
		mainbuf.bo = &qobj->tbo;
		list_add(&mainbuf.head, &validate_list);

		ret = virtio_gpu_object_list_validate(&ticket, &validate_list);
		if (ret) {
			DRM_DEBUG("failed to validate\n");
			goto fail_unref;
		}

		rc_3d.resource_id = cpu_to_le32(res_id);
		rc_3d.target = cpu_to_le32(rc->target);
		rc_3d.format = cpu_to_le32(rc->format);
		rc_3d.bind = cpu_to_le32(rc->bind);
		rc_3d.width = cpu_to_le32(rc->width);
		rc_3d.height = cpu_to_le32(rc->height);
		rc_3d.depth = cpu_to_le32(rc->depth);
		rc_3d.array_size = cpu_to_le32(rc->array_size);
		rc_3d.last_level = cpu_to_le32(rc->last_level);
		rc_3d.nr_samples = cpu_to_le32(rc->nr_samples);
		rc_3d.flags = cpu_to_le32(rc->flags);

		virtio_gpu_cmd_resource_create_3d(vgdev, &rc_3d, NULL);
		ret = virtio_gpu_object_attach(vgdev, qobj, res_id, &fence);
		if (ret) {
			ttm_eu_backoff_reservation(&ticket, &validate_list);
			goto fail_unref;
		}
		ttm_eu_fence_buffer_objects(&ticket, &validate_list, &fence->f);
	}

	qobj->hw_res_handle = res_id;

	ret = drm_gem_handle_create(file_priv, obj, &handle);
	if (ret) {

		drm_gem_object_release(obj);
		if (vgdev->has_virgl_3d) {
			virtio_gpu_unref_list(&validate_list);
			fence_put(&fence->f);
		}
		return ret;
	}
	drm_gem_object_unreference_unlocked(obj);

	rc->res_handle = res_id; /* similiar to a VM address */
	rc->bo_handle = handle;

	if (vgdev->has_virgl_3d) {
		virtio_gpu_unref_list(&validate_list);
		fence_put(&fence->f);
	}
	return 0;
fail_unref:
	if (vgdev->has_virgl_3d) {
		virtio_gpu_unref_list(&validate_list);
		fence_put(&fence->f);
	}
//fail_obj:
//	drm_gem_object_handle_unreference_unlocked(obj);
fail_id:
	virtio_gpu_resource_id_put(vgdev, res_id);
	return ret;
}

static int virtio_gpu_resource_info_ioctl(struct drm_device *dev, void *data,
					  struct drm_file *file_priv)
{
	struct drm_virtgpu_resource_info *ri = data;
	struct drm_gem_object *gobj = NULL;
	struct virtio_gpu_object *qobj = NULL;

	gobj = drm_gem_object_lookup(file_priv, ri->bo_handle);
	if (gobj == NULL)
		return -ENOENT;

	qobj = gem_to_virtio_gpu_obj(gobj);

	ri->size = qobj->gem_base.size;
	ri->res_handle = qobj->hw_res_handle;
	drm_gem_object_unreference_unlocked(gobj);
	return 0;
}

static int virtio_gpu_transfer_from_host_ioctl(struct drm_device *dev,
					       void *data,
					       struct drm_file *file)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct virtio_gpu_fpriv *vfpriv = file->driver_priv;
	struct drm_virtgpu_3d_transfer_from_host *args = data;
	struct drm_gem_object *gobj = NULL;
	struct virtio_gpu_object *qobj = NULL;
	struct virtio_gpu_fence *fence;
	int ret;
	u32 offset = args->offset;
	struct virtio_gpu_box box;

	if (vgdev->has_virgl_3d == false)
		return -ENOSYS;

	gobj = drm_gem_object_lookup(file, args->bo_handle);
	if (gobj == NULL)
		return -ENOENT;

	qobj = gem_to_virtio_gpu_obj(gobj);

	ret = virtio_gpu_object_reserve(qobj, false);
	if (ret)
		goto out;

	ret = ttm_bo_validate(&qobj->tbo, &qobj->placement,
			      true, false);
	if (unlikely(ret))
		goto out_unres;

	convert_to_hw_box(&box, &args->box);
	virtio_gpu_cmd_transfer_from_host_3d
		(vgdev, qobj->hw_res_handle,
		 vfpriv->ctx_id, offset, args->level,
		 &box, &fence);
	reservation_object_add_excl_fence(qobj->tbo.resv,
					  &fence->f);

	fence_put(&fence->f);
out_unres:
	virtio_gpu_object_unreserve(qobj);
out:
	drm_gem_object_unreference_unlocked(gobj);
	return ret;
}

static int virtio_gpu_transfer_to_host_ioctl(struct drm_device *dev, void *data,
					     struct drm_file *file)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct virtio_gpu_fpriv *vfpriv = file->driver_priv;
	struct drm_virtgpu_3d_transfer_to_host *args = data;
	struct drm_gem_object *gobj = NULL;
	struct virtio_gpu_object *qobj = NULL;
	struct virtio_gpu_fence *fence;
	struct virtio_gpu_box box;
	int ret;
	u32 offset = args->offset;

	gobj = drm_gem_object_lookup(file, args->bo_handle);
	if (gobj == NULL)
		return -ENOENT;

	qobj = gem_to_virtio_gpu_obj(gobj);

	ret = virtio_gpu_object_reserve(qobj, false);
	if (ret)
		goto out;

	ret = ttm_bo_validate(&qobj->tbo, &qobj->placement,
			      true, false);
	if (unlikely(ret))
		goto out_unres;

	convert_to_hw_box(&box, &args->box);
	if (!vgdev->has_virgl_3d) {
		virtio_gpu_cmd_transfer_to_host_2d
			(vgdev, qobj->hw_res_handle, offset,
			 box.w, box.h, box.x, box.y, NULL);
	} else {
		virtio_gpu_cmd_transfer_to_host_3d
			(vgdev, qobj->hw_res_handle,
			 vfpriv ? vfpriv->ctx_id : 0, offset,
			 args->level, &box, &fence);
		reservation_object_add_excl_fence(qobj->tbo.resv,
						  &fence->f);
		fence_put(&fence->f);
	}

out_unres:
	virtio_gpu_object_unreserve(qobj);
out:
	drm_gem_object_unreference_unlocked(gobj);
	return ret;
}

static int virtio_gpu_wait_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file)
{
	struct drm_virtgpu_3d_wait *args = data;
	struct drm_gem_object *gobj = NULL;
	struct virtio_gpu_object *qobj = NULL;
	int ret;
	bool nowait = false;

	gobj = drm_gem_object_lookup(file, args->handle);
	if (gobj == NULL)
		return -ENOENT;

	qobj = gem_to_virtio_gpu_obj(gobj);

	if (args->flags & VIRTGPU_WAIT_NOWAIT)
		nowait = true;
	ret = virtio_gpu_object_wait(qobj, nowait);

	drm_gem_object_unreference_unlocked(gobj);
	return ret;
}

static int virtio_gpu_get_caps_ioctl(struct drm_device *dev,
				void *data, struct drm_file *file)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct drm_virtgpu_get_caps *args = data;
	int size;
	int i;
	int found_valid = -1;
	int ret;
	struct virtio_gpu_drv_cap_cache *cache_ent;
	void *ptr;
	if (vgdev->num_capsets == 0)
		return -ENOSYS;

	spin_lock(&vgdev->display_info_lock);
	for (i = 0; i < vgdev->num_capsets; i++) {
		if (vgdev->capsets[i].id == args->cap_set_id) {
			if (vgdev->capsets[i].max_version >= args->cap_set_ver) {
				found_valid = i;
				break;
			}
		}
	}

	if (found_valid == -1) {
		spin_unlock(&vgdev->display_info_lock);
		return -EINVAL;
	}

	size = vgdev->capsets[found_valid].max_size;
	if (args->size > size) {
		spin_unlock(&vgdev->display_info_lock);
		return -EINVAL;
	}

	list_for_each_entry(cache_ent, &vgdev->cap_cache, head) {
		if (cache_ent->id == args->cap_set_id &&
		    cache_ent->version == args->cap_set_ver) {
			ptr = cache_ent->caps_cache;
			spin_unlock(&vgdev->display_info_lock);
			goto copy_exit;
		}
	}
	spin_unlock(&vgdev->display_info_lock);

	/* not in cache - need to talk to hw */
	virtio_gpu_cmd_get_capset(vgdev, found_valid, args->cap_set_ver,
				  &cache_ent);

	ret = wait_event_timeout(vgdev->resp_wq,
				 atomic_read(&cache_ent->is_valid), 5 * HZ);

	ptr = cache_ent->caps_cache;

copy_exit:
	if (copy_to_user((void __user *)(unsigned long)args->addr, ptr, size))
		return -EFAULT;

	return 0;
}

struct drm_ioctl_desc virtio_gpu_ioctls[DRM_VIRTIO_NUM_IOCTLS] = {
	DRM_IOCTL_DEF_DRV(VIRTGPU_MAP, virtio_gpu_map_ioctl,
			  DRM_AUTH|DRM_UNLOCKED|DRM_RENDER_ALLOW),

	DRM_IOCTL_DEF_DRV(VIRTGPU_EXECBUFFER, virtio_gpu_execbuffer_ioctl,
			  DRM_AUTH|DRM_UNLOCKED|DRM_RENDER_ALLOW),

	DRM_IOCTL_DEF_DRV(VIRTGPU_GETPARAM, virtio_gpu_getparam_ioctl,
			  DRM_AUTH|DRM_UNLOCKED|DRM_RENDER_ALLOW),

	DRM_IOCTL_DEF_DRV(VIRTGPU_RESOURCE_CREATE,
			  virtio_gpu_resource_create_ioctl,
			  DRM_AUTH|DRM_UNLOCKED|DRM_RENDER_ALLOW),

	DRM_IOCTL_DEF_DRV(VIRTGPU_RESOURCE_INFO, virtio_gpu_resource_info_ioctl,
			  DRM_AUTH|DRM_UNLOCKED|DRM_RENDER_ALLOW),

	/* make transfer async to the main ring? - no sure, can we
	   thread these in the underlying GL */
	DRM_IOCTL_DEF_DRV(VIRTGPU_TRANSFER_FROM_HOST,
			  virtio_gpu_transfer_from_host_ioctl,
			  DRM_AUTH|DRM_UNLOCKED|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(VIRTGPU_TRANSFER_TO_HOST,
			  virtio_gpu_transfer_to_host_ioctl,
			  DRM_AUTH|DRM_UNLOCKED|DRM_RENDER_ALLOW),

	DRM_IOCTL_DEF_DRV(VIRTGPU_WAIT, virtio_gpu_wait_ioctl,
			  DRM_AUTH|DRM_UNLOCKED|DRM_RENDER_ALLOW),

	DRM_IOCTL_DEF_DRV(VIRTGPU_GET_CAPS, virtio_gpu_get_caps_ioctl,
			  DRM_AUTH|DRM_UNLOCKED|DRM_RENDER_ALLOW),
};
