/*
 * Copyright (C) 2015 Red Hat, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <drm/drmP.h>
#include "virtgpu_drv.h"

void virtio_gpu_gem_free_object(struct drm_gem_object *gem_obj)
{
	struct virtio_gpu_object *obj = gem_to_virtio_gpu_obj(gem_obj);

	if (obj)
		virtio_gpu_object_unref(&obj);
}

struct virtio_gpu_object *virtio_gpu_alloc_object(struct drm_device *dev,
						  size_t size, bool kernel,
						  bool pinned)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct virtio_gpu_object *obj;
	int ret;

	ret = virtio_gpu_object_create(vgdev, size, kernel, pinned, &obj);
	if (ret)
		return ERR_PTR(ret);

	return obj;
}

int virtio_gpu_gem_create(struct drm_file *file,
			  struct drm_device *dev,
			  uint64_t size,
			  struct drm_gem_object **obj_p,
			  uint32_t *handle_p)
{
	struct virtio_gpu_object *obj;
	int ret;
	u32 handle;

	obj = virtio_gpu_alloc_object(dev, size, false, false);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	ret = drm_gem_handle_create(file, &obj->gem_base, &handle);
	if (ret) {
		drm_gem_object_release(&obj->gem_base);
		return ret;
	}

	*obj_p = &obj->gem_base;

	/* drop reference from allocate - handle holds it now */
	drm_gem_object_unreference_unlocked(&obj->gem_base);

	*handle_p = handle;
	return 0;
}

int virtio_gpu_mode_dumb_create(struct drm_file *file_priv,
				struct drm_device *dev,
				struct drm_mode_create_dumb *args)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct drm_gem_object *gobj;
	struct virtio_gpu_object *obj;
	int ret;
	uint32_t pitch;
	uint32_t resid;

	pitch = args->width * ((args->bpp + 1) / 8);
	args->size = pitch * args->height;
	args->size = ALIGN(args->size, PAGE_SIZE);

	ret = virtio_gpu_gem_create(file_priv, dev, args->size, &gobj,
				    &args->handle);
	if (ret)
		goto fail;

	virtio_gpu_resource_id_get(vgdev, &resid);
	virtio_gpu_cmd_create_resource(vgdev, resid,
				       2, args->width, args->height);

	/* attach the object to the resource */
	obj = gem_to_virtio_gpu_obj(gobj);
	ret = virtio_gpu_object_attach(vgdev, obj, resid, NULL);
	if (ret)
		goto fail;

	obj->dumb = true;
	args->pitch = pitch;
	return ret;

fail:
	return ret;
}

int virtio_gpu_mode_dumb_destroy(struct drm_file *file_priv,
				 struct drm_device *dev,
				 uint32_t handle)
{
	return drm_gem_handle_delete(file_priv, handle);
}

int virtio_gpu_mode_dumb_mmap(struct drm_file *file_priv,
			      struct drm_device *dev,
			      uint32_t handle, uint64_t *offset_p)
{
	struct drm_gem_object *gobj;
	struct virtio_gpu_object *obj;
	BUG_ON(!offset_p);
	gobj = drm_gem_object_lookup(file_priv, handle);
	if (gobj == NULL)
		return -ENOENT;
	obj = gem_to_virtio_gpu_obj(gobj);
	*offset_p = virtio_gpu_object_mmap_offset(obj);
	drm_gem_object_unreference_unlocked(gobj);
	return 0;
}

int virtio_gpu_gem_object_open(struct drm_gem_object *obj,
			       struct drm_file *file)
{
	struct virtio_gpu_device *vgdev = obj->dev->dev_private;
	struct virtio_gpu_fpriv *vfpriv = file->driver_priv;
	struct virtio_gpu_object *qobj = gem_to_virtio_gpu_obj(obj);
	int r;

	if (!vgdev->has_virgl_3d)
		return 0;

	r = virtio_gpu_object_reserve(qobj, false);
	if (r)
		return r;

	virtio_gpu_cmd_context_attach_resource(vgdev, vfpriv->ctx_id,
					       qobj->hw_res_handle);
	virtio_gpu_object_unreserve(qobj);
	return 0;
}

void virtio_gpu_gem_object_close(struct drm_gem_object *obj,
				 struct drm_file *file)
{
	struct virtio_gpu_device *vgdev = obj->dev->dev_private;
	struct virtio_gpu_fpriv *vfpriv = file->driver_priv;
	struct virtio_gpu_object *qobj = gem_to_virtio_gpu_obj(obj);
	int r;

	if (!vgdev->has_virgl_3d)
		return;

	r = virtio_gpu_object_reserve(qobj, false);
	if (r)
		return;

	virtio_gpu_cmd_context_detach_resource(vgdev, vfpriv->ctx_id,
						qobj->hw_res_handle);
	virtio_gpu_object_unreserve(qobj);
}
