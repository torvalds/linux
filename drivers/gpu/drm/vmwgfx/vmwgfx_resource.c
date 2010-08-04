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
#include "vmwgfx_drm.h"
#include "ttm/ttm_object.h"
#include "ttm/ttm_placement.h"
#include "drmP.h"

#define VMW_RES_CONTEXT ttm_driver_type0
#define VMW_RES_SURFACE ttm_driver_type1
#define VMW_RES_STREAM ttm_driver_type2

struct vmw_user_context {
	struct ttm_base_object base;
	struct vmw_resource res;
};

struct vmw_user_surface {
	struct ttm_base_object base;
	struct vmw_surface srf;
};

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

static void vmw_resource_release(struct kref *kref)
{
	struct vmw_resource *res =
	    container_of(kref, struct vmw_resource, kref);
	struct vmw_private *dev_priv = res->dev_priv;

	idr_remove(res->idr, res->id);
	write_unlock(&dev_priv->resource_lock);

	if (likely(res->hw_destroy != NULL))
		res->hw_destroy(res);

	if (res->res_free != NULL)
		res->res_free(res);
	else
		kfree(res);

	write_lock(&dev_priv->resource_lock);
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

static int vmw_resource_init(struct vmw_private *dev_priv,
			     struct vmw_resource *res,
			     struct idr *idr,
			     enum ttm_object_type obj_type,
			     void (*res_free) (struct vmw_resource *res))
{
	int ret;

	kref_init(&res->kref);
	res->hw_destroy = NULL;
	res->res_free = res_free;
	res->res_type = obj_type;
	res->idr = idr;
	res->avail = false;
	res->dev_priv = dev_priv;

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

static void vmw_resource_activate(struct vmw_resource *res,
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
 * Context management:
 */

static void vmw_hw_context_destroy(struct vmw_resource *res)
{

	struct vmw_private *dev_priv = res->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDestroyContext body;
	} *cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));

	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Failed reserving FIFO space for surface "
			  "destruction.\n");
		return;
	}

	cmd->header.id = cpu_to_le32(SVGA_3D_CMD_CONTEXT_DESTROY);
	cmd->header.size = cpu_to_le32(sizeof(cmd->body));
	cmd->body.cid = cpu_to_le32(res->id);

	vmw_fifo_commit(dev_priv, sizeof(*cmd));
}

static int vmw_context_init(struct vmw_private *dev_priv,
			    struct vmw_resource *res,
			    void (*res_free) (struct vmw_resource *res))
{
	int ret;

	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDefineContext body;
	} *cmd;

	ret = vmw_resource_init(dev_priv, res, &dev_priv->context_idr,
				VMW_RES_CONTEXT, res_free);

	if (unlikely(ret != 0)) {
		if (res_free == NULL)
			kfree(res);
		else
			res_free(res);
		return ret;
	}

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Fifo reserve failed.\n");
		vmw_resource_unreference(&res);
		return -ENOMEM;
	}

	cmd->header.id = cpu_to_le32(SVGA_3D_CMD_CONTEXT_DEFINE);
	cmd->header.size = cpu_to_le32(sizeof(cmd->body));
	cmd->body.cid = cpu_to_le32(res->id);

	vmw_fifo_commit(dev_priv, sizeof(*cmd));
	vmw_resource_activate(res, vmw_hw_context_destroy);
	return 0;
}

struct vmw_resource *vmw_context_alloc(struct vmw_private *dev_priv)
{
	struct vmw_resource *res = kmalloc(sizeof(*res), GFP_KERNEL);
	int ret;

	if (unlikely(res == NULL))
		return NULL;

	ret = vmw_context_init(dev_priv, res, NULL);
	return (ret == 0) ? res : NULL;
}

/**
 * User-space context management:
 */

static void vmw_user_context_free(struct vmw_resource *res)
{
	struct vmw_user_context *ctx =
	    container_of(res, struct vmw_user_context, res);

	kfree(ctx);
}

/**
 * This function is called when user space has no more references on the
 * base object. It releases the base-object's reference on the resource object.
 */

static void vmw_user_context_base_release(struct ttm_base_object **p_base)
{
	struct ttm_base_object *base = *p_base;
	struct vmw_user_context *ctx =
	    container_of(base, struct vmw_user_context, base);
	struct vmw_resource *res = &ctx->res;

	*p_base = NULL;
	vmw_resource_unreference(&res);
}

int vmw_context_destroy_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct vmw_resource *res;
	struct vmw_user_context *ctx;
	struct drm_vmw_context_arg *arg = (struct drm_vmw_context_arg *)data;
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	int ret = 0;

	res = vmw_resource_lookup(dev_priv, &dev_priv->context_idr, arg->cid);
	if (unlikely(res == NULL))
		return -EINVAL;

	if (res->res_free != &vmw_user_context_free) {
		ret = -EINVAL;
		goto out;
	}

	ctx = container_of(res, struct vmw_user_context, res);
	if (ctx->base.tfile != tfile && !ctx->base.shareable) {
		ret = -EPERM;
		goto out;
	}

	ttm_ref_object_base_unref(tfile, ctx->base.hash.key, TTM_REF_USAGE);
out:
	vmw_resource_unreference(&res);
	return ret;
}

int vmw_context_define_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct vmw_user_context *ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	struct vmw_resource *res;
	struct vmw_resource *tmp;
	struct drm_vmw_context_arg *arg = (struct drm_vmw_context_arg *)data;
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	int ret;

	if (unlikely(ctx == NULL))
		return -ENOMEM;

	res = &ctx->res;
	ctx->base.shareable = false;
	ctx->base.tfile = NULL;

	ret = vmw_context_init(dev_priv, res, vmw_user_context_free);
	if (unlikely(ret != 0))
		return ret;

	tmp = vmw_resource_reference(&ctx->res);
	ret = ttm_base_object_init(tfile, &ctx->base, false, VMW_RES_CONTEXT,
				   &vmw_user_context_base_release, NULL);

	if (unlikely(ret != 0)) {
		vmw_resource_unreference(&tmp);
		goto out_err;
	}

	arg->cid = res->id;
out_err:
	vmw_resource_unreference(&res);
	return ret;

}

int vmw_context_check(struct vmw_private *dev_priv,
		      struct ttm_object_file *tfile,
		      int id)
{
	struct vmw_resource *res;
	int ret = 0;

	read_lock(&dev_priv->resource_lock);
	res = idr_find(&dev_priv->context_idr, id);
	if (res && res->avail) {
		struct vmw_user_context *ctx =
			container_of(res, struct vmw_user_context, res);
		if (ctx->base.tfile != tfile && !ctx->base.shareable)
			ret = -EPERM;
	} else
		ret = -EINVAL;
	read_unlock(&dev_priv->resource_lock);

	return ret;
}


/**
 * Surface management.
 */

static void vmw_hw_surface_destroy(struct vmw_resource *res)
{

	struct vmw_private *dev_priv = res->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDestroySurface body;
	} *cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));

	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Failed reserving FIFO space for surface "
			  "destruction.\n");
		return;
	}

	cmd->header.id = cpu_to_le32(SVGA_3D_CMD_SURFACE_DESTROY);
	cmd->header.size = cpu_to_le32(sizeof(cmd->body));
	cmd->body.sid = cpu_to_le32(res->id);

	vmw_fifo_commit(dev_priv, sizeof(*cmd));
}

void vmw_surface_res_free(struct vmw_resource *res)
{
	struct vmw_surface *srf = container_of(res, struct vmw_surface, res);

	kfree(srf->sizes);
	kfree(srf->snooper.image);
	kfree(srf);
}

int vmw_surface_init(struct vmw_private *dev_priv,
		     struct vmw_surface *srf,
		     void (*res_free) (struct vmw_resource *res))
{
	int ret;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDefineSurface body;
	} *cmd;
	SVGA3dSize *cmd_size;
	struct vmw_resource *res = &srf->res;
	struct drm_vmw_size *src_size;
	size_t submit_size;
	uint32_t cmd_len;
	int i;

	BUG_ON(res_free == NULL);
	ret = vmw_resource_init(dev_priv, res, &dev_priv->surface_idr,
				VMW_RES_SURFACE, res_free);

	if (unlikely(ret != 0)) {
		res_free(res);
		return ret;
	}

	submit_size = sizeof(*cmd) + srf->num_sizes * sizeof(SVGA3dSize);
	cmd_len = sizeof(cmd->body) + srf->num_sizes * sizeof(SVGA3dSize);

	cmd = vmw_fifo_reserve(dev_priv, submit_size);
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Fifo reserve failed for create surface.\n");
		vmw_resource_unreference(&res);
		return -ENOMEM;
	}

	cmd->header.id = cpu_to_le32(SVGA_3D_CMD_SURFACE_DEFINE);
	cmd->header.size = cpu_to_le32(cmd_len);
	cmd->body.sid = cpu_to_le32(res->id);
	cmd->body.surfaceFlags = cpu_to_le32(srf->flags);
	cmd->body.format = cpu_to_le32(srf->format);
	for (i = 0; i < DRM_VMW_MAX_SURFACE_FACES; ++i) {
		cmd->body.face[i].numMipLevels =
		    cpu_to_le32(srf->mip_levels[i]);
	}

	cmd += 1;
	cmd_size = (SVGA3dSize *) cmd;
	src_size = srf->sizes;

	for (i = 0; i < srf->num_sizes; ++i, cmd_size++, src_size++) {
		cmd_size->width = cpu_to_le32(src_size->width);
		cmd_size->height = cpu_to_le32(src_size->height);
		cmd_size->depth = cpu_to_le32(src_size->depth);
	}

	vmw_fifo_commit(dev_priv, submit_size);
	vmw_resource_activate(res, vmw_hw_surface_destroy);
	return 0;
}

static void vmw_user_surface_free(struct vmw_resource *res)
{
	struct vmw_surface *srf = container_of(res, struct vmw_surface, res);
	struct vmw_user_surface *user_srf =
	    container_of(srf, struct vmw_user_surface, srf);

	kfree(srf->sizes);
	kfree(srf->snooper.image);
	kfree(user_srf);
}

int vmw_user_surface_lookup_handle(struct vmw_private *dev_priv,
				   struct ttm_object_file *tfile,
				   uint32_t handle, struct vmw_surface **out)
{
	struct vmw_resource *res;
	struct vmw_surface *srf;
	struct vmw_user_surface *user_srf;
	struct ttm_base_object *base;
	int ret = -EINVAL;

	base = ttm_base_object_lookup(tfile, handle);
	if (unlikely(base == NULL))
		return -EINVAL;

	if (unlikely(base->object_type != VMW_RES_SURFACE))
		goto out_bad_resource;

	user_srf = container_of(base, struct vmw_user_surface, base);
	srf = &user_srf->srf;
	res = &srf->res;

	read_lock(&dev_priv->resource_lock);

	if (!res->avail || res->res_free != &vmw_user_surface_free) {
		read_unlock(&dev_priv->resource_lock);
		goto out_bad_resource;
	}

	kref_get(&res->kref);
	read_unlock(&dev_priv->resource_lock);

	*out = srf;
	ret = 0;

out_bad_resource:
	ttm_base_object_unref(&base);

	return ret;
}

static void vmw_user_surface_base_release(struct ttm_base_object **p_base)
{
	struct ttm_base_object *base = *p_base;
	struct vmw_user_surface *user_srf =
	    container_of(base, struct vmw_user_surface, base);
	struct vmw_resource *res = &user_srf->srf.res;

	*p_base = NULL;
	vmw_resource_unreference(&res);
}

int vmw_surface_destroy_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct drm_vmw_surface_arg *arg = (struct drm_vmw_surface_arg *)data;
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;

	return ttm_ref_object_base_unref(tfile, arg->sid, TTM_REF_USAGE);
}

int vmw_surface_define_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct vmw_user_surface *user_srf =
	    kmalloc(sizeof(*user_srf), GFP_KERNEL);
	struct vmw_surface *srf;
	struct vmw_resource *res;
	struct vmw_resource *tmp;
	union drm_vmw_surface_create_arg *arg =
	    (union drm_vmw_surface_create_arg *)data;
	struct drm_vmw_surface_create_req *req = &arg->req;
	struct drm_vmw_surface_arg *rep = &arg->rep;
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct drm_vmw_size __user *user_sizes;
	int ret;
	int i;

	if (unlikely(user_srf == NULL))
		return -ENOMEM;

	srf = &user_srf->srf;
	res = &srf->res;

	srf->flags = req->flags;
	srf->format = req->format;
	srf->scanout = req->scanout;
	memcpy(srf->mip_levels, req->mip_levels, sizeof(srf->mip_levels));
	srf->num_sizes = 0;
	for (i = 0; i < DRM_VMW_MAX_SURFACE_FACES; ++i)
		srf->num_sizes += srf->mip_levels[i];

	if (srf->num_sizes > DRM_VMW_MAX_SURFACE_FACES *
	    DRM_VMW_MAX_MIP_LEVELS) {
		ret = -EINVAL;
		goto out_err0;
	}

	srf->sizes = kmalloc(srf->num_sizes * sizeof(*srf->sizes), GFP_KERNEL);
	if (unlikely(srf->sizes == NULL)) {
		ret = -ENOMEM;
		goto out_err0;
	}

	user_sizes = (struct drm_vmw_size __user *)(unsigned long)
	    req->size_addr;

	ret = copy_from_user(srf->sizes, user_sizes,
			     srf->num_sizes * sizeof(*srf->sizes));
	if (unlikely(ret != 0)) {
		ret = -EFAULT;
		goto out_err1;
	}

	if (srf->scanout &&
	    srf->num_sizes == 1 &&
	    srf->sizes[0].width == 64 &&
	    srf->sizes[0].height == 64 &&
	    srf->format == SVGA3D_A8R8G8B8) {

		srf->snooper.image = kmalloc(64 * 64 * 4, GFP_KERNEL);
		/* clear the image */
		if (srf->snooper.image) {
			memset(srf->snooper.image, 0x00, 64 * 64 * 4);
		} else {
			DRM_ERROR("Failed to allocate cursor_image\n");
			ret = -ENOMEM;
			goto out_err1;
		}
	} else {
		srf->snooper.image = NULL;
	}
	srf->snooper.crtc = NULL;

	user_srf->base.shareable = false;
	user_srf->base.tfile = NULL;

	/**
	 * From this point, the generic resource management functions
	 * destroy the object on failure.
	 */

	ret = vmw_surface_init(dev_priv, srf, vmw_user_surface_free);
	if (unlikely(ret != 0))
		return ret;

	tmp = vmw_resource_reference(&srf->res);
	ret = ttm_base_object_init(tfile, &user_srf->base,
				   req->shareable, VMW_RES_SURFACE,
				   &vmw_user_surface_base_release, NULL);

	if (unlikely(ret != 0)) {
		vmw_resource_unreference(&tmp);
		vmw_resource_unreference(&res);
		return ret;
	}

	rep->sid = user_srf->base.hash.key;
	if (rep->sid == SVGA3D_INVALID_ID)
		DRM_ERROR("Created bad Surface ID.\n");

	vmw_resource_unreference(&res);
	return 0;
out_err1:
	kfree(srf->sizes);
out_err0:
	kfree(user_srf);
	return ret;
}

int vmw_surface_reference_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	union drm_vmw_surface_reference_arg *arg =
	    (union drm_vmw_surface_reference_arg *)data;
	struct drm_vmw_surface_arg *req = &arg->req;
	struct drm_vmw_surface_create_req *rep = &arg->rep;
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct vmw_surface *srf;
	struct vmw_user_surface *user_srf;
	struct drm_vmw_size __user *user_sizes;
	struct ttm_base_object *base;
	int ret = -EINVAL;

	base = ttm_base_object_lookup(tfile, req->sid);
	if (unlikely(base == NULL)) {
		DRM_ERROR("Could not find surface to reference.\n");
		return -EINVAL;
	}

	if (unlikely(base->object_type != VMW_RES_SURFACE))
		goto out_bad_resource;

	user_srf = container_of(base, struct vmw_user_surface, base);
	srf = &user_srf->srf;

	ret = ttm_ref_object_add(tfile, &user_srf->base, TTM_REF_USAGE, NULL);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Could not add a reference to a surface.\n");
		goto out_no_reference;
	}

	rep->flags = srf->flags;
	rep->format = srf->format;
	memcpy(rep->mip_levels, srf->mip_levels, sizeof(srf->mip_levels));
	user_sizes = (struct drm_vmw_size __user *)(unsigned long)
	    rep->size_addr;

	if (user_sizes)
		ret = copy_to_user(user_sizes, srf->sizes,
				   srf->num_sizes * sizeof(*srf->sizes));
	if (unlikely(ret != 0)) {
		DRM_ERROR("copy_to_user failed %p %u\n",
			  user_sizes, srf->num_sizes);
		ret = -EFAULT;
	}
out_bad_resource:
out_no_reference:
	ttm_base_object_unref(&base);

	return ret;
}

int vmw_surface_check(struct vmw_private *dev_priv,
		      struct ttm_object_file *tfile,
		      uint32_t handle, int *id)
{
	struct ttm_base_object *base;
	struct vmw_user_surface *user_srf;

	int ret = -EPERM;

	base = ttm_base_object_lookup(tfile, handle);
	if (unlikely(base == NULL))
		return -EINVAL;

	if (unlikely(base->object_type != VMW_RES_SURFACE))
		goto out_bad_surface;

	user_srf = container_of(base, struct vmw_user_surface, base);
	*id = user_srf->srf.res.id;
	ret = 0;

out_bad_surface:
	/**
	 * FIXME: May deadlock here when called from the
	 * command parsing code.
	 */

	ttm_base_object_unref(&base);
	return ret;
}

/**
 * Buffer management.
 */

static size_t vmw_dmabuf_acc_size(struct ttm_bo_global *glob,
				  unsigned long num_pages)
{
	static size_t bo_user_size = ~0;

	size_t page_array_size =
	    (num_pages * sizeof(void *) + PAGE_SIZE - 1) & PAGE_MASK;

	if (unlikely(bo_user_size == ~0)) {
		bo_user_size = glob->ttm_bo_extra_size +
		    ttm_round_pot(sizeof(struct vmw_dma_buffer));
	}

	return bo_user_size + page_array_size;
}

void vmw_dmabuf_gmr_unbind(struct ttm_buffer_object *bo)
{
	struct vmw_dma_buffer *vmw_bo = vmw_dma_buffer(bo);
	struct ttm_bo_global *glob = bo->glob;
	struct vmw_private *dev_priv =
		container_of(bo->bdev, struct vmw_private, bdev);

	if (vmw_bo->gmr_bound) {
		vmw_gmr_unbind(dev_priv, vmw_bo->gmr_id);
		spin_lock(&glob->lru_lock);
		ida_remove(&dev_priv->gmr_ida, vmw_bo->gmr_id);
		spin_unlock(&glob->lru_lock);
		vmw_bo->gmr_bound = false;
	}
}

void vmw_dmabuf_bo_free(struct ttm_buffer_object *bo)
{
	struct vmw_dma_buffer *vmw_bo = vmw_dma_buffer(bo);
	struct ttm_bo_global *glob = bo->glob;

	vmw_dmabuf_gmr_unbind(bo);
	ttm_mem_global_free(glob->mem_glob, bo->acc_size);
	kfree(vmw_bo);
}

int vmw_dmabuf_init(struct vmw_private *dev_priv,
		    struct vmw_dma_buffer *vmw_bo,
		    size_t size, struct ttm_placement *placement,
		    bool interruptible,
		    void (*bo_free) (struct ttm_buffer_object *bo))
{
	struct ttm_bo_device *bdev = &dev_priv->bdev;
	struct ttm_mem_global *mem_glob = bdev->glob->mem_glob;
	size_t acc_size;
	int ret;

	BUG_ON(!bo_free);

	acc_size =
	    vmw_dmabuf_acc_size(bdev->glob,
				(size + PAGE_SIZE - 1) >> PAGE_SHIFT);

	ret = ttm_mem_global_alloc(mem_glob, acc_size, false, false);
	if (unlikely(ret != 0)) {
		/* we must free the bo here as
		 * ttm_buffer_object_init does so as well */
		bo_free(&vmw_bo->base);
		return ret;
	}

	memset(vmw_bo, 0, sizeof(*vmw_bo));

	INIT_LIST_HEAD(&vmw_bo->gmr_lru);
	INIT_LIST_HEAD(&vmw_bo->validate_list);
	vmw_bo->gmr_id = 0;
	vmw_bo->gmr_bound = false;

	ret = ttm_bo_init(bdev, &vmw_bo->base, size,
			  ttm_bo_type_device, placement,
			  0, 0, interruptible,
			  NULL, acc_size, bo_free);
	return ret;
}

static void vmw_user_dmabuf_destroy(struct ttm_buffer_object *bo)
{
	struct vmw_user_dma_buffer *vmw_user_bo = vmw_user_dma_buffer(bo);
	struct ttm_bo_global *glob = bo->glob;

	vmw_dmabuf_gmr_unbind(bo);
	ttm_mem_global_free(glob->mem_glob, bo->acc_size);
	kfree(vmw_user_bo);
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

int vmw_dmabuf_alloc_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	union drm_vmw_alloc_dmabuf_arg *arg =
	    (union drm_vmw_alloc_dmabuf_arg *)data;
	struct drm_vmw_alloc_dmabuf_req *req = &arg->req;
	struct drm_vmw_dmabuf_rep *rep = &arg->rep;
	struct vmw_user_dma_buffer *vmw_user_bo;
	struct ttm_buffer_object *tmp;
	struct vmw_master *vmaster = vmw_master(file_priv->master);
	int ret;

	vmw_user_bo = kzalloc(sizeof(*vmw_user_bo), GFP_KERNEL);
	if (unlikely(vmw_user_bo == NULL))
		return -ENOMEM;

	ret = ttm_read_lock(&vmaster->lock, true);
	if (unlikely(ret != 0)) {
		kfree(vmw_user_bo);
		return ret;
	}

	ret = vmw_dmabuf_init(dev_priv, &vmw_user_bo->dma, req->size,
			      &vmw_vram_sys_placement, true,
			      &vmw_user_dmabuf_destroy);
	if (unlikely(ret != 0))
		return ret;

	tmp = ttm_bo_reference(&vmw_user_bo->dma.base);
	ret = ttm_base_object_init(vmw_fpriv(file_priv)->tfile,
				   &vmw_user_bo->base,
				   false,
				   ttm_buffer_type,
				   &vmw_user_dmabuf_release, NULL);
	if (unlikely(ret != 0)) {
		ttm_bo_unref(&tmp);
	} else {
		rep->handle = vmw_user_bo->base.hash.key;
		rep->map_handle = vmw_user_bo->dma.base.addr_space_offset;
		rep->cur_gmr_id = vmw_user_bo->base.hash.key;
		rep->cur_gmr_offset = 0;
	}
	ttm_bo_unref(&tmp);

	ttm_read_unlock(&vmaster->lock);

	return 0;
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

uint32_t vmw_dmabuf_validate_node(struct ttm_buffer_object *bo,
				  uint32_t cur_validate_node)
{
	struct vmw_dma_buffer *vmw_bo = vmw_dma_buffer(bo);

	if (likely(vmw_bo->on_validate_list))
		return vmw_bo->cur_validate_node;

	vmw_bo->cur_validate_node = cur_validate_node;
	vmw_bo->on_validate_list = true;

	return cur_validate_node;
}

void vmw_dmabuf_validate_clear(struct ttm_buffer_object *bo)
{
	struct vmw_dma_buffer *vmw_bo = vmw_dma_buffer(bo);

	vmw_bo->on_validate_list = false;
}

uint32_t vmw_dmabuf_gmr(struct ttm_buffer_object *bo)
{
	struct vmw_dma_buffer *vmw_bo;

	if (bo->mem.mem_type == TTM_PL_VRAM)
		return SVGA_GMR_FRAMEBUFFER;

	vmw_bo = vmw_dma_buffer(bo);

	return (vmw_bo->gmr_bound) ? vmw_bo->gmr_id : SVGA_GMR_NULL;
}

void vmw_dmabuf_set_gmr(struct ttm_buffer_object *bo, uint32_t id)
{
	struct vmw_dma_buffer *vmw_bo = vmw_dma_buffer(bo);
	vmw_bo->gmr_bound = true;
	vmw_bo->gmr_id = id;
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

/**
 * TODO: Implement a gmr id eviction mechanism. Currently we just fail
 * when we're out of ids, causing GMR space to be allocated
 * out of VRAM.
 */

int vmw_gmr_id_alloc(struct vmw_private *dev_priv, uint32_t *p_id)
{
	struct ttm_bo_global *glob = dev_priv->bdev.glob;
	int id;
	int ret;

	do {
		if (unlikely(ida_pre_get(&dev_priv->gmr_ida, GFP_KERNEL) == 0))
			return -ENOMEM;

		spin_lock(&glob->lru_lock);
		ret = ida_get_new(&dev_priv->gmr_ida, &id);
		spin_unlock(&glob->lru_lock);
	} while (ret == -EAGAIN);

	if (unlikely(ret != 0))
		return ret;

	if (unlikely(id >= dev_priv->max_gmr_ids)) {
		spin_lock(&glob->lru_lock);
		ida_remove(&dev_priv->gmr_ida, id);
		spin_unlock(&glob->lru_lock);
		return -EBUSY;
	}

	*p_id = (uint32_t) id;
	return 0;
}

/*
 * Stream managment
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

	ret = vmw_resource_init(dev_priv, res, &dev_priv->stream_idr,
				VMW_RES_STREAM, res_free);

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

/**
 * User-space context management:
 */

static void vmw_user_stream_free(struct vmw_resource *res)
{
	struct vmw_user_stream *stream =
	    container_of(res, struct vmw_user_stream, stream.res);

	kfree(stream);
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
	int ret = 0;

	res = vmw_resource_lookup(dev_priv, &dev_priv->stream_idr, arg->stream_id);
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
	struct vmw_user_stream *stream = kmalloc(sizeof(*stream), GFP_KERNEL);
	struct vmw_resource *res;
	struct vmw_resource *tmp;
	struct drm_vmw_stream_arg *arg = (struct drm_vmw_stream_arg *)data;
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	int ret;

	if (unlikely(stream == NULL))
		return -ENOMEM;

	res = &stream->stream.res;
	stream->base.shareable = false;
	stream->base.tfile = NULL;

	ret = vmw_stream_init(dev_priv, &stream->stream, vmw_user_stream_free);
	if (unlikely(ret != 0))
		return ret;

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
	return ret;
}

int vmw_user_stream_lookup(struct vmw_private *dev_priv,
			   struct ttm_object_file *tfile,
			   uint32_t *inout_id, struct vmw_resource **out)
{
	struct vmw_user_stream *stream;
	struct vmw_resource *res;
	int ret;

	res = vmw_resource_lookup(dev_priv, &dev_priv->stream_idr, *inout_id);
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
