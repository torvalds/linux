/**************************************************************************
 *
 * Copyright © 2009 VMware, Inc., Palo Alto, CA., USA
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
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "vmwgfx_drv.h"
#include <drm/vmwgfx_drm.h>
#include <drm/ttm/ttm_object.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/drmP.h>
#include "vmwgfx_resource_priv.h"

struct vmw_user_dma_buffer {
	struct ttm_base_object base;
	struct vmw_dma_buffer dma;
};

struct vmw_bo_user_rep {
	uint32_t handle;
	uint64_t map_handle;
};

struct vmw_stream {
	struct vmw_resource res;
	uint32_t stream_id;
};

struct vmw_user_stream {
	struct ttm_base_object base;
	struct vmw_stream stream;
};


static uint64_t vmw_user_stream_size;

static const struct vmw_res_func vmw_stream_func = {
	.res_type = vmw_res_stream,
	.needs_backup = false,
	.may_evict = false,
	.type_name = "video streams",
	.backup_placement = NULL,
	.create = NULL,
	.destroy = NULL,
	.bind = NULL,
	.unbind = NULL
};

static inline struct vmw_dma_buffer *
vmw_dma_buffer(struct ttm_buffer_object *bo)
{
	return container_of(bo, struct vmw_dma_buffer, base);
}

static inline struct vmw_user_dma_buffer *
vmw_user_dma_buffer(struct ttm_buffer_object *bo)
{
	struct vmw_dma_buffer *vmw_bo = vmw_dma_buffer(bo);
	return container_of(vmw_bo, struct vmw_user_dma_buffer, dma);
}

struct vmw_resource *vmw_resource_reference(struct vmw_resource *res)
{
	kref_get(&res->kref);
	return res;
}


/**
 * vmw_resource_release_id - release a resource id to the id manager.
 *
 * @res: Pointer to the resource.
 *
 * Release the resource id to the resource id manager and set it to -1
 */
void vmw_resource_release_id(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct idr *idr = &dev_priv->res_idr[res->func->res_type];

	write_lock(&dev_priv->resource_lock);
	if (res->id != -1)
		idr_remove(idr, res->id);
	res->id = -1;
	write_unlock(&dev_priv->resource_lock);
}

static void vmw_resource_release(struct kref *kref)
{
	struct vmw_resource *res =
	    container_of(kref, struct vmw_resource, kref);
	struct vmw_private *dev_priv = res->dev_priv;
	int id;
	struct idr *idr = &dev_priv->res_idr[res->func->res_type];

	res->avail = false;
	list_del_init(&res->lru_head);
	write_unlock(&dev_priv->resource_lock);
	if (res->backup) {
		struct ttm_buffer_object *bo = &res->backup->base;

		ttm_bo_reserve(bo, false, false, false, 0);
		if (!list_empty(&res->mob_head) &&
		    res->func->unbind != NULL) {
			struct ttm_validate_buffer val_buf;

			val_buf.bo = bo;
			res->func->unbind(res, false, &val_buf);
		}
		res->backup_dirty = false;
		list_del_init(&res->mob_head);
		ttm_bo_unreserve(bo);
		vmw_dmabuf_unreference(&res->backup);
	}

	if (likely(res->hw_destroy != NULL))
		res->hw_destroy(res);

	id = res->id;
	if (res->res_free != NULL)
		res->res_free(res);
	else
		kfree(res);

	write_lock(&dev_priv->resource_lock);

	if (id != -1)
		idr_remove(idr, id);
}

void vmw_resource_unreference(struct vmw_resource **p_res)
{
	struct vmw_resource *res = *p_res;
	struct vmw_private *dev_priv = res->dev_priv;

	*p_res = NULL;
	write_lock(&dev_priv->resource_lock);
	kref_put(&res->kref, vmw_resource_release);
	write_unlock(&dev_priv->resource_lock);
}


/**
 * vmw_resource_alloc_id - release a resource id to the id manager.
 *
 * @res: Pointer to the resource.
 *
 * Allocate the lowest free resource from the resource manager, and set
 * @res->id to that id. Returns 0 on success and -ENOMEM on failure.
 */
int vmw_resource_alloc_id(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	int ret;
	struct idr *idr = &dev_priv->res_idr[res->func->res_type];

	BUG_ON(res->id != -1);

	do {
		if (unlikely(idr_pre_get(idr, GFP_KERNEL) == 0))
			return -ENOMEM;

		write_lock(&dev_priv->resource_lock);
		ret = idr_get_new_above(idr, res, 1, &res->id);
		write_unlock(&dev_priv->resource_lock);

	} while (ret == -EAGAIN);

	return ret;
}

/**
 * vmw_resource_init - initialize a struct vmw_resource
 *
 * @dev_priv:       Pointer to a device private struct.
 * @res:            The struct vmw_resource to initialize.
 * @obj_type:       Resource object type.
 * @delay_id:       Boolean whether to defer device id allocation until
 *                  the first validation.
 * @res_free:       Resource destructor.
 * @func:           Resource function table.
 */
int vmw_resource_init(struct vmw_private *dev_priv, struct vmw_resource *res,
		      bool delay_id,
		      void (*res_free) (struct vmw_resource *res),
		      const struct vmw_res_func *func)
{
	kref_init(&res->kref);
	res->hw_destroy = NULL;
	res->res_free = res_free;
	res->avail = false;
	res->dev_priv = dev_priv;
	res->func = func;
	INIT_LIST_HEAD(&res->lru_head);
	INIT_LIST_HEAD(&res->mob_head);
	res->id = -1;
	res->backup = NULL;
	res->backup_offset = 0;
	res->backup_dirty = false;
	res->res_dirty = false;
	if (delay_id)
		return 0;
	else
		return vmw_resource_alloc_id(res);
}

/**
 * vmw_resource_activate
 *
 * @res:        Pointer to the newly created resource
 * @hw_destroy: Destroy function. NULL if none.
 *
 * Activate a resource after the hardware has been made aware of it.
 * Set tye destroy function to @destroy. Typically this frees the
 * resource and destroys the hardware resources associated with it.
 * Activate basically means that the function vmw_resource_lookup will
 * find it.
 */
void vmw_resource_activate(struct vmw_resource *res,
			   void (*hw_destroy) (struct vmw_resource *))
{
	struct vmw_private *dev_priv = res->dev_priv;

	write_lock(&dev_priv->resource_lock);
	res->avail = true;
	res->hw_destroy = hw_destroy;
	write_unlock(&dev_priv->resource_lock);
}

struct vmw_resource *vmw_resource_lookup(struct vmw_private *dev_priv,
					 struct idr *idr, int id)
{
	struct vmw_resource *res;

	read_lock(&dev_priv->resource_lock);
	res = idr_find(idr, id);
	if (res && res->avail)
		kref_get(&res->kref);
	else
		res = NULL;
	read_unlock(&dev_priv->resource_lock);

	if (unlikely(res == NULL))
		return NULL;

	return res;
}

/**
 * vmw_user_resource_lookup_handle - lookup a struct resource from a
 * TTM user-space handle and perform basic type checks
 *
 * @dev_priv:     Pointer to a device private struct
 * @tfile:        Pointer to a struct ttm_object_file identifying the caller
 * @handle:       The TTM user-space handle
 * @converter:    Pointer to an object describing the resource type
 * @p_res:        On successful return the location pointed to will contain
 *                a pointer to a refcounted struct vmw_resource.
 *
 * If the handle can't be found or is associated with an incorrect resource
 * type, -EINVAL will be returned.
 */
int vmw_user_resource_lookup_handle(struct vmw_private *dev_priv,
				    struct ttm_object_file *tfile,
				    uint32_t handle,
				    const struct vmw_user_resource_conv
				    *converter,
				    struct vmw_resource **p_res)
{
	struct ttm_base_object *base;
	struct vmw_resource *res;
	int ret = -EINVAL;

	base = ttm_base_object_lookup(tfile, handle);
	if (unlikely(base == NULL))
		return -EINVAL;

	if (unlikely(base->object_type != converter->object_type))
		goto out_bad_resource;

	res = converter->base_obj_to_res(base);

	read_lock(&dev_priv->resource_lock);
	if (!res->avail || res->res_free != converter->res_free) {
		read_unlock(&dev_priv->resource_lock);
		goto out_bad_resource;
	}

	kref_get(&res->kref);
	read_unlock(&dev_priv->resource_lock);

	*p_res = res;
	ret = 0;

out_bad_resource:
	ttm_base_object_unref(&base);

	return ret;
}

/**
 * Helper function that looks either a surface or dmabuf.
 *
 * The pointer this pointed at by out_surf and out_buf needs to be null.
 */
int vmw_user_lookup_handle(struct vmw_private *dev_priv,
			   struct ttm_object_file *tfile,
			   uint32_t handle,
			   struct vmw_surface **out_surf,
			   struct vmw_dma_buffer **out_buf)
{
	struct vmw_resource *res;
	int ret;

	BUG_ON(*out_surf || *out_buf);

	ret = vmw_user_resource_lookup_handle(dev_priv, tfile, handle,
					      user_surface_converter,
					      &res);
	if (!ret) {
		*out_surf = vmw_res_to_srf(res);
		return 0;
	}

	*out_surf = NULL;
	ret = vmw_user_dmabuf_lookup(tfile, handle, out_buf);
	return ret;
}

/**
 * Buffer management.
 */
void vmw_dmabuf_bo_free(struct ttm_buffer_object *bo)
{
	struct vmw_dma_buffer *vmw_bo = vmw_dma_buffer(bo);

	kfree(vmw_bo);
}

int vmw_dmabuf_init(struct vmw_private *dev_priv,
		    struct vmw_dma_buffer *vmw_bo,
		    size_t size, struct ttm_placement *placement,
		    bool interruptible,
		    void (*bo_free) (struct ttm_buffer_object *bo))
{
	struct ttm_bo_device *bdev = &dev_priv->bdev;
	size_t acc_size;
	int ret;

	BUG_ON(!bo_free);

	acc_size = ttm_bo_acc_size(bdev, size, sizeof(struct vmw_dma_buffer));
	memset(vmw_bo, 0, sizeof(*vmw_bo));

	INIT_LIST_HEAD(&vmw_bo->res_list);

	ret = ttm_bo_init(bdev, &vmw_bo->base, size,
			  ttm_bo_type_device, placement,
			  0, interruptible,
			  NULL, acc_size, NULL, bo_free);
	return ret;
}

static void vmw_user_dmabuf_destroy(struct ttm_buffer_object *bo)
{
	struct vmw_user_dma_buffer *vmw_user_bo = vmw_user_dma_buffer(bo);

	ttm_base_object_kfree(vmw_user_bo, base);
}

static void vmw_user_dmabuf_release(struct ttm_base_object **p_base)
{
	struct vmw_user_dma_buffer *vmw_user_bo;
	struct ttm_base_object *base = *p_base;
	struct ttm_buffer_object *bo;

	*p_base = NULL;

	if (unlikely(base == NULL))
		return;

	vmw_user_bo = container_of(base, struct vmw_user_dma_buffer, base);
	bo = &vmw_user_bo->dma.base;
	ttm_bo_unref(&bo);
}

/**
 * vmw_user_dmabuf_alloc - Allocate a user dma buffer
 *
 * @dev_priv: Pointer to a struct device private.
 * @tfile: Pointer to a struct ttm_object_file on which to register the user
 * object.
 * @size: Size of the dma buffer.
 * @shareable: Boolean whether the buffer is shareable with other open files.
 * @handle: Pointer to where the handle value should be assigned.
 * @p_dma_buf: Pointer to where the refcounted struct vmw_dma_buffer pointer
 * should be assigned.
 */
int vmw_user_dmabuf_alloc(struct vmw_private *dev_priv,
			  struct ttm_object_file *tfile,
			  uint32_t size,
			  bool shareable,
			  uint32_t *handle,
			  struct vmw_dma_buffer **p_dma_buf)
{
	struct vmw_user_dma_buffer *user_bo;
	struct ttm_buffer_object *tmp;
	int ret;

	user_bo = kzalloc(sizeof(*user_bo), GFP_KERNEL);
	if (unlikely(user_bo == NULL)) {
		DRM_ERROR("Failed to allocate a buffer.\n");
		return -ENOMEM;
	}

	ret = vmw_dmabuf_init(dev_priv, &user_bo->dma, size,
			      &vmw_vram_sys_placement, true,
			      &vmw_user_dmabuf_destroy);
	if (unlikely(ret != 0))
		return ret;

	tmp = ttm_bo_reference(&user_bo->dma.base);
	ret = ttm_base_object_init(tfile,
				   &user_bo->base,
				   shareable,
				   ttm_buffer_type,
				   &vmw_user_dmabuf_release, NULL);
	if (unlikely(ret != 0)) {
		ttm_bo_unref(&tmp);
		goto out_no_base_object;
	}

	*p_dma_buf = &user_bo->dma;
	*handle = user_bo->base.hash.key;

out_no_base_object:
	return ret;
}

int vmw_dmabuf_alloc_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	union drm_vmw_alloc_dmabuf_arg *arg =
	    (union drm_vmw_alloc_dmabuf_arg *)data;
	struct drm_vmw_alloc_dmabuf_req *req = &arg->req;
	struct drm_vmw_dmabuf_rep *rep = &arg->rep;
	struct vmw_dma_buffer *dma_buf;
	uint32_t handle;
	struct vmw_master *vmaster = vmw_master(file_priv->master);
	int ret;

	ret = ttm_read_lock(&vmaster->lock, true);
	if (unlikely(ret != 0))
		return ret;

	ret = vmw_user_dmabuf_alloc(dev_priv, vmw_fpriv(file_priv)->tfile,
				    req->size, false, &handle, &dma_buf);
	if (unlikely(ret != 0))
		goto out_no_dmabuf;

	rep->handle = handle;
	rep->map_handle = dma_buf->base.addr_space_offset;
	rep->cur_gmr_id = handle;
	rep->cur_gmr_offset = 0;

	vmw_dmabuf_unreference(&dma_buf);

out_no_dmabuf:
	ttm_read_unlock(&vmaster->lock);

	return ret;
}

int vmw_dmabuf_unref_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct drm_vmw_unref_dmabuf_arg *arg =
	    (struct drm_vmw_unref_dmabuf_arg *)data;

	return ttm_ref_object_base_unref(vmw_fpriv(file_priv)->tfile,
					 arg->handle,
					 TTM_REF_USAGE);
}

int vmw_user_dmabuf_lookup(struct ttm_object_file *tfile,
			   uint32_t handle, struct vmw_dma_buffer **out)
{
	struct vmw_user_dma_buffer *vmw_user_bo;
	struct ttm_base_object *base;

	base = ttm_base_object_lookup(tfile, handle);
	if (unlikely(base == NULL)) {
		printk(KERN_ERR "Invalid buffer object handle 0x%08lx.\n",
		       (unsigned long)handle);
		return -ESRCH;
	}

	if (unlikely(base->object_type != ttm_buffer_type)) {
		ttm_base_object_unref(&base);
		printk(KERN_ERR "Invalid buffer object handle 0x%08lx.\n",
		       (unsigned long)handle);
		return -EINVAL;
	}

	vmw_user_bo = container_of(base, struct vmw_user_dma_buffer, base);
	(void)ttm_bo_reference(&vmw_user_bo->dma.base);
	ttm_base_object_unref(&base);
	*out = &vmw_user_bo->dma;

	return 0;
}

int vmw_user_dmabuf_reference(struct ttm_object_file *tfile,
			      struct vmw_dma_buffer *dma_buf)
{
	struct vmw_user_dma_buffer *user_bo;

	if (dma_buf->base.destroy != vmw_user_dmabuf_destroy)
		return -EINVAL;

	user_bo = container_of(dma_buf, struct vmw_user_dma_buffer, dma);
	return ttm_ref_object_add(tfile, &user_bo->base, TTM_REF_USAGE, NULL);
}

/*
 * Stream management
 */

static void vmw_stream_destroy(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct vmw_stream *stream;
	int ret;

	DRM_INFO("%s: unref\n", __func__);
	stream = container_of(res, struct vmw_stream, res);

	ret = vmw_overlay_unref(dev_priv, stream->stream_id);
	WARN_ON(ret != 0);
}

static int vmw_stream_init(struct vmw_private *dev_priv,
			   struct vmw_stream *stream,
			   void (*res_free) (struct vmw_resource *res))
{
	struct vmw_resource *res = &stream->res;
	int ret;

	ret = vmw_resource_init(dev_priv, res, false, res_free,
				&vmw_stream_func);

	if (unlikely(ret != 0)) {
		if (res_free == NULL)
			kfree(stream);
		else
			res_free(&stream->res);
		return ret;
	}

	ret = vmw_overlay_claim(dev_priv, &stream->stream_id);
	if (ret) {
		vmw_resource_unreference(&res);
		return ret;
	}

	DRM_INFO("%s: claimed\n", __func__);

	vmw_resource_activate(&stream->res, vmw_stream_destroy);
	return 0;
}

static void vmw_user_stream_free(struct vmw_resource *res)
{
	struct vmw_user_stream *stream =
	    container_of(res, struct vmw_user_stream, stream.res);
	struct vmw_private *dev_priv = res->dev_priv;

	ttm_base_object_kfree(stream, base);
	ttm_mem_global_free(vmw_mem_glob(dev_priv),
			    vmw_user_stream_size);
}

/**
 * This function is called when user space has no more references on the
 * base object. It releases the base-object's reference on the resource object.
 */

static void vmw_user_stream_base_release(struct ttm_base_object **p_base)
{
	struct ttm_base_object *base = *p_base;
	struct vmw_user_stream *stream =
	    container_of(base, struct vmw_user_stream, base);
	struct vmw_resource *res = &stream->stream.res;

	*p_base = NULL;
	vmw_resource_unreference(&res);
}

int vmw_stream_unref_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct vmw_resource *res;
	struct vmw_user_stream *stream;
	struct drm_vmw_stream_arg *arg = (struct drm_vmw_stream_arg *)data;
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct idr *idr = &dev_priv->res_idr[vmw_res_stream];
	int ret = 0;


	res = vmw_resource_lookup(dev_priv, idr, arg->stream_id);
	if (unlikely(res == NULL))
		return -EINVAL;

	if (res->res_free != &vmw_user_stream_free) {
		ret = -EINVAL;
		goto out;
	}

	stream = container_of(res, struct vmw_user_stream, stream.res);
	if (stream->base.tfile != tfile) {
		ret = -EINVAL;
		goto out;
	}

	ttm_ref_object_base_unref(tfile, stream->base.hash.key, TTM_REF_USAGE);
out:
	vmw_resource_unreference(&res);
	return ret;
}

int vmw_stream_claim_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct vmw_user_stream *stream;
	struct vmw_resource *res;
	struct vmw_resource *tmp;
	struct drm_vmw_stream_arg *arg = (struct drm_vmw_stream_arg *)data;
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct vmw_master *vmaster = vmw_master(file_priv->master);
	int ret;

	/*
	 * Approximate idr memory usage with 128 bytes. It will be limited
	 * by maximum number_of streams anyway?
	 */

	if (unlikely(vmw_user_stream_size == 0))
		vmw_user_stream_size = ttm_round_pot(sizeof(*stream)) + 128;

	ret = ttm_read_lock(&vmaster->lock, true);
	if (unlikely(ret != 0))
		return ret;

	ret = ttm_mem_global_alloc(vmw_mem_glob(dev_priv),
				   vmw_user_stream_size,
				   false, true);
	if (unlikely(ret != 0)) {
		if (ret != -ERESTARTSYS)
			DRM_ERROR("Out of graphics memory for stream"
				  " creation.\n");
		goto out_unlock;
	}


	stream = kmalloc(sizeof(*stream), GFP_KERNEL);
	if (unlikely(stream == NULL)) {
		ttm_mem_global_free(vmw_mem_glob(dev_priv),
				    vmw_user_stream_size);
		ret = -ENOMEM;
		goto out_unlock;
	}

	res = &stream->stream.res;
	stream->base.shareable = false;
	stream->base.tfile = NULL;

	/*
	 * From here on, the destructor takes over resource freeing.
	 */

	ret = vmw_stream_init(dev_priv, &stream->stream, vmw_user_stream_free);
	if (unlikely(ret != 0))
		goto out_unlock;

	tmp = vmw_resource_reference(res);
	ret = ttm_base_object_init(tfile, &stream->base, false, VMW_RES_STREAM,
				   &vmw_user_stream_base_release, NULL);

	if (unlikely(ret != 0)) {
		vmw_resource_unreference(&tmp);
		goto out_err;
	}

	arg->stream_id = res->id;
out_err:
	vmw_resource_unreference(&res);
out_unlock:
	ttm_read_unlock(&vmaster->lock);
	return ret;
}

int vmw_user_stream_lookup(struct vmw_private *dev_priv,
			   struct ttm_object_file *tfile,
			   uint32_t *inout_id, struct vmw_resource **out)
{
	struct vmw_user_stream *stream;
	struct vmw_resource *res;
	int ret;

	res = vmw_resource_lookup(dev_priv, &dev_priv->res_idr[vmw_res_stream],
				  *inout_id);
	if (unlikely(res == NULL))
		return -EINVAL;

	if (res->res_free != &vmw_user_stream_free) {
		ret = -EINVAL;
		goto err_ref;
	}

	stream = container_of(res, struct vmw_user_stream, stream.res);
	if (stream->base.tfile != tfile) {
		ret = -EPERM;
		goto err_ref;
	}

	*inout_id = stream->stream.stream_id;
	*out = res;
	return 0;
err_ref:
	vmw_resource_unreference(&res);
	return ret;
}


int vmw_dumb_create(struct drm_file *file_priv,
		    struct drm_device *dev,
		    struct drm_mode_create_dumb *args)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct vmw_master *vmaster = vmw_master(file_priv->master);
	struct vmw_user_dma_buffer *vmw_user_bo;
	struct ttm_buffer_object *tmp;
	int ret;

	args->pitch = args->width * ((args->bpp + 7) / 8);
	args->size = args->pitch * args->height;

	vmw_user_bo = kzalloc(sizeof(*vmw_user_bo), GFP_KERNEL);
	if (vmw_user_bo == NULL)
		return -ENOMEM;

	ret = ttm_read_lock(&vmaster->lock, true);
	if (ret != 0) {
		kfree(vmw_user_bo);
		return ret;
	}

	ret = vmw_dmabuf_init(dev_priv, &vmw_user_bo->dma, args->size,
			      &vmw_vram_sys_placement, true,
			      &vmw_user_dmabuf_destroy);
	if (ret != 0)
		goto out_no_dmabuf;

	tmp = ttm_bo_reference(&vmw_user_bo->dma.base);
	ret = ttm_base_object_init(vmw_fpriv(file_priv)->tfile,
				   &vmw_user_bo->base,
				   false,
				   ttm_buffer_type,
				   &vmw_user_dmabuf_release, NULL);
	if (unlikely(ret != 0))
		goto out_no_base_object;

	args->handle = vmw_user_bo->base.hash.key;

out_no_base_object:
	ttm_bo_unref(&tmp);
out_no_dmabuf:
	ttm_read_unlock(&vmaster->lock);
	return ret;
}

int vmw_dumb_map_offset(struct drm_file *file_priv,
			struct drm_device *dev, uint32_t handle,
			uint64_t *offset)
{
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct vmw_dma_buffer *out_buf;
	int ret;

	ret = vmw_user_dmabuf_lookup(tfile, handle, &out_buf);
	if (ret != 0)
		return -EINVAL;

	*offset = out_buf->base.addr_space_offset;
	vmw_dmabuf_unreference(&out_buf);
	return 0;
}

int vmw_dumb_destroy(struct drm_file *file_priv,
		     struct drm_device *dev,
		     uint32_t handle)
{
	return ttm_ref_object_base_unref(vmw_fpriv(file_priv)->tfile,
					 handle, TTM_REF_USAGE);
}

/**
 * vmw_resource_buf_alloc - Allocate a backup buffer for a resource.
 *
 * @res:            The resource for which to allocate a backup buffer.
 * @interruptible:  Whether any sleeps during allocation should be
 *                  performed while interruptible.
 */
static int vmw_resource_buf_alloc(struct vmw_resource *res,
				  bool interruptible)
{
	unsigned long size =
		(res->backup_size + PAGE_SIZE - 1) & PAGE_MASK;
	struct vmw_dma_buffer *backup;
	int ret;

	if (likely(res->backup)) {
		BUG_ON(res->backup->base.num_pages * PAGE_SIZE < size);
		return 0;
	}

	backup = kzalloc(sizeof(*backup), GFP_KERNEL);
	if (unlikely(backup == NULL))
		return -ENOMEM;

	ret = vmw_dmabuf_init(res->dev_priv, backup, res->backup_size,
			      res->func->backup_placement,
			      interruptible,
			      &vmw_dmabuf_bo_free);
	if (unlikely(ret != 0))
		goto out_no_dmabuf;

	res->backup = backup;

out_no_dmabuf:
	return ret;
}

/**
 * vmw_resource_do_validate - Make a resource up-to-date and visible
 *                            to the device.
 *
 * @res:            The resource to make visible to the device.
 * @val_buf:        Information about a buffer possibly
 *                  containing backup data if a bind operation is needed.
 *
 * On hardware resource shortage, this function returns -EBUSY and
 * should be retried once resources have been freed up.
 */
static int vmw_resource_do_validate(struct vmw_resource *res,
				    struct ttm_validate_buffer *val_buf)
{
	int ret = 0;
	const struct vmw_res_func *func = res->func;

	if (unlikely(res->id == -1)) {
		ret = func->create(res);
		if (unlikely(ret != 0))
			return ret;
	}

	if (func->bind &&
	    ((func->needs_backup && list_empty(&res->mob_head) &&
	      val_buf->bo != NULL) ||
	     (!func->needs_backup && val_buf->bo != NULL))) {
		ret = func->bind(res, val_buf);
		if (unlikely(ret != 0))
			goto out_bind_failed;
		if (func->needs_backup)
			list_add_tail(&res->mob_head, &res->backup->res_list);
	}

	/*
	 * Only do this on write operations, and move to
	 * vmw_resource_unreserve if it can be called after
	 * backup buffers have been unreserved. Otherwise
	 * sort out locking.
	 */
	res->res_dirty = true;

	return 0;

out_bind_failed:
	func->destroy(res);

	return ret;
}

/**
 * vmw_resource_unreserve - Unreserve a resource previously reserved for
 * command submission.
 *
 * @res:               Pointer to the struct vmw_resource to unreserve.
 * @new_backup:        Pointer to new backup buffer if command submission
 *                     switched.
 * @new_backup_offset: New backup offset if @new_backup is !NULL.
 *
 * Currently unreserving a resource means putting it back on the device's
 * resource lru list, so that it can be evicted if necessary.
 */
void vmw_resource_unreserve(struct vmw_resource *res,
			    struct vmw_dma_buffer *new_backup,
			    unsigned long new_backup_offset)
{
	struct vmw_private *dev_priv = res->dev_priv;

	if (!list_empty(&res->lru_head))
		return;

	if (new_backup && new_backup != res->backup) {

		if (res->backup) {
			BUG_ON(atomic_read(&res->backup->base.reserved) == 0);
			list_del_init(&res->mob_head);
			vmw_dmabuf_unreference(&res->backup);
		}

		res->backup = vmw_dmabuf_reference(new_backup);
		BUG_ON(atomic_read(&new_backup->base.reserved) == 0);
		list_add_tail(&res->mob_head, &new_backup->res_list);
	}
	if (new_backup)
		res->backup_offset = new_backup_offset;

	if (!res->func->may_evict)
		return;

	write_lock(&dev_priv->resource_lock);
	list_add_tail(&res->lru_head,
		      &res->dev_priv->res_lru[res->func->res_type]);
	write_unlock(&dev_priv->resource_lock);
}

/**
 * vmw_resource_check_buffer - Check whether a backup buffer is needed
 *                             for a resource and in that case, allocate
 *                             one, reserve and validate it.
 *
 * @res:            The resource for which to allocate a backup buffer.
 * @interruptible:  Whether any sleeps during allocation should be
 *                  performed while interruptible.
 * @val_buf:        On successful return contains data about the
 *                  reserved and validated backup buffer.
 */
int vmw_resource_check_buffer(struct vmw_resource *res,
			      bool interruptible,
			      struct ttm_validate_buffer *val_buf)
{
	struct list_head val_list;
	bool backup_dirty = false;
	int ret;

	if (unlikely(res->backup == NULL)) {
		ret = vmw_resource_buf_alloc(res, interruptible);
		if (unlikely(ret != 0))
			return ret;
	}

	INIT_LIST_HEAD(&val_list);
	val_buf->bo = ttm_bo_reference(&res->backup->base);
	list_add_tail(&val_buf->head, &val_list);
	ret = ttm_eu_reserve_buffers(&val_list);
	if (unlikely(ret != 0))
		goto out_no_reserve;

	if (res->func->needs_backup && list_empty(&res->mob_head))
		return 0;

	backup_dirty = res->backup_dirty;
	ret = ttm_bo_validate(&res->backup->base,
			      res->func->backup_placement,
			      true, false, false);

	if (unlikely(ret != 0))
		goto out_no_validate;

	return 0;

out_no_validate:
	ttm_eu_backoff_reservation(&val_list);
out_no_reserve:
	ttm_bo_unref(&val_buf->bo);
	if (backup_dirty)
		vmw_dmabuf_unreference(&res->backup);

	return ret;
}

/**
 * vmw_resource_reserve - Reserve a resource for command submission
 *
 * @res:            The resource to reserve.
 *
 * This function takes the resource off the LRU list and make sure
 * a backup buffer is present for guest-backed resources. However,
 * the buffer may not be bound to the resource at this point.
 *
 */
int vmw_resource_reserve(struct vmw_resource *res, bool no_backup)
{
	struct vmw_private *dev_priv = res->dev_priv;
	int ret;

	write_lock(&dev_priv->resource_lock);
	list_del_init(&res->lru_head);
	write_unlock(&dev_priv->resource_lock);

	if (res->func->needs_backup && res->backup == NULL &&
	    !no_backup) {
		ret = vmw_resource_buf_alloc(res, true);
		if (unlikely(ret != 0))
			return ret;
	}

	return 0;
}

/**
 * vmw_resource_backoff_reservation - Unreserve and unreference a
 *                                    backup buffer
 *.
 * @val_buf:        Backup buffer information.
 */
void vmw_resource_backoff_reservation(struct ttm_validate_buffer *val_buf)
{
	struct list_head val_list;

	if (likely(val_buf->bo == NULL))
		return;

	INIT_LIST_HEAD(&val_list);
	list_add_tail(&val_buf->head, &val_list);
	ttm_eu_backoff_reservation(&val_list);
	ttm_bo_unref(&val_buf->bo);
}

/**
 * vmw_resource_do_evict - Evict a resource, and transfer its data
 *                         to a backup buffer.
 *
 * @res:            The resource to evict.
 */
int vmw_resource_do_evict(struct vmw_resource *res)
{
	struct ttm_validate_buffer val_buf;
	const struct vmw_res_func *func = res->func;
	int ret;

	BUG_ON(!func->may_evict);

	val_buf.bo = NULL;
	ret = vmw_resource_check_buffer(res, true, &val_buf);
	if (unlikely(ret != 0))
		return ret;

	if (unlikely(func->unbind != NULL &&
		     (!func->needs_backup || !list_empty(&res->mob_head)))) {
		ret = func->unbind(res, res->res_dirty, &val_buf);
		if (unlikely(ret != 0))
			goto out_no_unbind;
		list_del_init(&res->mob_head);
	}
	ret = func->destroy(res);
	res->backup_dirty = true;
	res->res_dirty = false;
out_no_unbind:
	vmw_resource_backoff_reservation(&val_buf);

	return ret;
}


/**
 * vmw_resource_validate - Make a resource up-to-date and visible
 *                         to the device.
 *
 * @res:            The resource to make visible to the device.
 *
 * On succesful return, any backup DMA buffer pointed to by @res->backup will
 * be reserved and validated.
 * On hardware resource shortage, this function will repeatedly evict
 * resources of the same type until the validation succeeds.
 */
int vmw_resource_validate(struct vmw_resource *res)
{
	int ret;
	struct vmw_resource *evict_res;
	struct vmw_private *dev_priv = res->dev_priv;
	struct list_head *lru_list = &dev_priv->res_lru[res->func->res_type];
	struct ttm_validate_buffer val_buf;

	if (likely(!res->func->may_evict))
		return 0;

	val_buf.bo = NULL;
	if (res->backup)
		val_buf.bo = &res->backup->base;
	do {
		ret = vmw_resource_do_validate(res, &val_buf);
		if (likely(ret != -EBUSY))
			break;

		write_lock(&dev_priv->resource_lock);
		if (list_empty(lru_list) || !res->func->may_evict) {
			DRM_ERROR("Out of device device id entries "
				  "for %s.\n", res->func->type_name);
			ret = -EBUSY;
			write_unlock(&dev_priv->resource_lock);
			break;
		}

		evict_res = vmw_resource_reference
			(list_first_entry(lru_list, struct vmw_resource,
					  lru_head));
		list_del_init(&evict_res->lru_head);

		write_unlock(&dev_priv->resource_lock);
		vmw_resource_do_evict(evict_res);
		vmw_resource_unreference(&evict_res);
	} while (1);

	if (unlikely(ret != 0))
		goto out_no_validate;
	else if (!res->func->needs_backup && res->backup) {
		list_del_init(&res->mob_head);
		vmw_dmabuf_unreference(&res->backup);
	}

	return 0;

out_no_validate:
	return ret;
}

/**
 * vmw_fence_single_bo - Utility function to fence a single TTM buffer
 *                       object without unreserving it.
 *
 * @bo:             Pointer to the struct ttm_buffer_object to fence.
 * @fence:          Pointer to the fence. If NULL, this function will
 *                  insert a fence into the command stream..
 *
 * Contrary to the ttm_eu version of this function, it takes only
 * a single buffer object instead of a list, and it also doesn't
 * unreserve the buffer object, which needs to be done separately.
 */
void vmw_fence_single_bo(struct ttm_buffer_object *bo,
			 struct vmw_fence_obj *fence)
{
	struct ttm_bo_device *bdev = bo->bdev;
	struct ttm_bo_driver *driver = bdev->driver;
	struct vmw_fence_obj *old_fence_obj;
	struct vmw_private *dev_priv =
		container_of(bdev, struct vmw_private, bdev);

	if (fence == NULL)
		vmw_execbuf_fence_commands(NULL, dev_priv, &fence, NULL);
	else
		driver->sync_obj_ref(fence);

	spin_lock(&bdev->fence_lock);

	old_fence_obj = bo->sync_obj;
	bo->sync_obj = fence;

	spin_unlock(&bdev->fence_lock);

	if (old_fence_obj)
		vmw_fence_obj_unreference(&old_fence_obj);
}

/**
 * vmw_resource_move_notify - TTM move_notify_callback
 *
 * @bo:             The TTM buffer object about to move.
 * @mem:            The truct ttm_mem_reg indicating to what memory
 *                  region the move is taking place.
 *
 * For now does nothing.
 */
void vmw_resource_move_notify(struct ttm_buffer_object *bo,
			      struct ttm_mem_reg *mem)
{
}

/**
 * vmw_resource_needs_backup - Return whether a resource needs a backup buffer.
 *
 * @res:            The resource being queried.
 */
bool vmw_resource_needs_backup(const struct vmw_resource *res)
{
	return res->func->needs_backup;
}

/**
 * vmw_resource_evict_type - Evict all resources of a specific type
 *
 * @dev_priv:       Pointer to a device private struct
 * @type:           The resource type to evict
 *
 * To avoid thrashing starvation or as part of the hibernation sequence,
 * evict all evictable resources of a specific type.
 */
static void vmw_resource_evict_type(struct vmw_private *dev_priv,
				    enum vmw_res_type type)
{
	struct list_head *lru_list = &dev_priv->res_lru[type];
	struct vmw_resource *evict_res;

	do {
		write_lock(&dev_priv->resource_lock);

		if (list_empty(lru_list))
			goto out_unlock;

		evict_res = vmw_resource_reference(
			list_first_entry(lru_list, struct vmw_resource,
					 lru_head));
		list_del_init(&evict_res->lru_head);
		write_unlock(&dev_priv->resource_lock);
		vmw_resource_do_evict(evict_res);
		vmw_resource_unreference(&evict_res);
	} while (1);

out_unlock:
	write_unlock(&dev_priv->resource_lock);
}

/**
 * vmw_resource_evict_all - Evict all evictable resources
 *
 * @dev_priv:       Pointer to a device private struct
 *
 * To avoid thrashing starvation or as part of the hibernation sequence,
 * evict all evictable resources. In particular this means that all
 * guest-backed resources that are registered with the device are
 * evicted and the OTable becomes clean.
 */
void vmw_resource_evict_all(struct vmw_private *dev_priv)
{
	enum vmw_res_type type;

	mutex_lock(&dev_priv->cmdbuf_mutex);

	for (type = 0; type < vmw_res_max; ++type)
		vmw_resource_evict_type(dev_priv, type);

	mutex_unlock(&dev_priv->cmdbuf_mutex);
}
