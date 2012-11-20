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

/**
 * struct vmw_user_resource_conv - Identify a derived user-exported resource
 * type and provide a function to convert its ttm_base_object pointer to
 * a struct vmw_resource
 */
struct vmw_user_resource_conv {
	enum ttm_object_type object_type;
	struct vmw_resource *(*base_obj_to_res)(struct ttm_base_object *base);
	void (*res_free) (struct vmw_resource *res);
};

/**
 * struct vmw_res_func - members and functions common for a resource type
 *
 * @res_type:          Enum that identifies the lru list to use for eviction.
 * @needs_backup:      Whether the resource is guest-backed and needs
 *                     persistent buffer storage.
 * @type_name:         String that identifies the resource type.
 * @backup_placement:  TTM placement for backup buffers.
 * @may_evict          Whether the resource may be evicted.
 * @create:            Create a hardware resource.
 * @destroy:           Destroy a hardware resource.
 * @bind:              Bind a hardware resource to persistent buffer storage.
 * @unbind:            Unbind a hardware resource from persistent
 *                     buffer storage.
 */

struct vmw_res_func {
	enum vmw_res_type res_type;
	bool needs_backup;
	const char *type_name;
	struct ttm_placement *backup_placement;
	bool may_evict;

	int (*create) (struct vmw_resource *res);
	int (*destroy) (struct vmw_resource *res);
	int (*bind) (struct vmw_resource *res,
		     struct ttm_validate_buffer *val_buf);
	int (*unbind) (struct vmw_resource *res,
		       bool readback,
		       struct ttm_validate_buffer *val_buf);
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

struct vmw_user_context {
	struct ttm_base_object base;
	struct vmw_resource res;
};

static void vmw_user_context_free(struct vmw_resource *res);
static struct vmw_resource *
vmw_user_context_base_to_res(struct ttm_base_object *base);

static uint64_t vmw_user_context_size;

static const struct vmw_user_resource_conv user_context_conv = {
	.object_type = VMW_RES_CONTEXT,
	.base_obj_to_res = vmw_user_context_base_to_res,
	.res_free = vmw_user_context_free
};

const struct vmw_user_resource_conv *user_context_converter =
	&user_context_conv;


static const struct vmw_res_func vmw_legacy_context_func = {
	.res_type = vmw_res_context,
	.needs_backup = false,
	.may_evict = false,
	.type_name = "legacy contexts",
	.backup_placement = NULL,
	.create = NULL,
	.destroy = NULL,
	.bind = NULL,
	.unbind = NULL
};


/**
 * struct vmw_user_surface - User-space visible surface resource
 *
 * @base:           The TTM base object handling user-space visibility.
 * @srf:            The surface metadata.
 * @size:           TTM accounting size for the surface.
 */
struct vmw_user_surface {
	struct ttm_base_object base;
	struct vmw_surface srf;
	uint32_t size;
	uint32_t backup_handle;
};

/**
 * struct vmw_surface_offset - Backing store mip level offset info
 *
 * @face:           Surface face.
 * @mip:            Mip level.
 * @bo_offset:      Offset into backing store of this mip level.
 *
 */
struct vmw_surface_offset {
	uint32_t face;
	uint32_t mip;
	uint32_t bo_offset;
};

static void vmw_user_surface_free(struct vmw_resource *res);
static struct vmw_resource *
vmw_user_surface_base_to_res(struct ttm_base_object *base);
static int vmw_legacy_srf_bind(struct vmw_resource *res,
			       struct ttm_validate_buffer *val_buf);
static int vmw_legacy_srf_unbind(struct vmw_resource *res,
				 bool readback,
				 struct ttm_validate_buffer *val_buf);
static int vmw_legacy_srf_create(struct vmw_resource *res);
static int vmw_legacy_srf_destroy(struct vmw_resource *res);

static const struct vmw_user_resource_conv user_surface_conv = {
	.object_type = VMW_RES_SURFACE,
	.base_obj_to_res = vmw_user_surface_base_to_res,
	.res_free = vmw_user_surface_free
};

const struct vmw_user_resource_conv *user_surface_converter =
	&user_surface_conv;


static uint64_t vmw_user_surface_size;

static const struct vmw_res_func vmw_legacy_surface_func = {
	.res_type = vmw_res_surface,
	.needs_backup = false,
	.may_evict = true,
	.type_name = "legacy surfaces",
	.backup_placement = &vmw_srf_placement,
	.create = &vmw_legacy_srf_create,
	.destroy = &vmw_legacy_srf_destroy,
	.bind = &vmw_legacy_srf_bind,
	.unbind = &vmw_legacy_srf_unbind
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
static void vmw_resource_release_id(struct vmw_resource *res)
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
static int vmw_resource_alloc_id(struct vmw_resource *res)
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
static int vmw_resource_init(struct vmw_private *dev_priv,
			     struct vmw_resource *res,
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
	} *cmd;


	vmw_execbuf_release_pinned_bo(dev_priv);
	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Failed reserving FIFO space for surface "
			  "destruction.\n");
		return;
	}

	cmd->header.id = cpu_to_le32(SVGA_3D_CMD_CONTEXT_DESTROY);
	cmd->header.size = cpu_to_le32(sizeof(cmd->body));
	cmd->body.cid = cpu_to_le32(res->id);

	vmw_fifo_commit(dev_priv, sizeof(*cmd));
	vmw_3d_resource_dec(dev_priv, false);
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

	ret = vmw_resource_init(dev_priv, res, false,
				res_free, &vmw_legacy_context_func);

	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed to allocate a resource id.\n");
		goto out_early;
	}

	if (unlikely(res->id >= SVGA3D_MAX_CONTEXT_IDS)) {
		DRM_ERROR("Out of hw context ids.\n");
		vmw_resource_unreference(&res);
		return -ENOMEM;
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
	(void) vmw_3d_resource_inc(dev_priv, false);
	vmw_resource_activate(res, vmw_hw_context_destroy);
	return 0;

out_early:
	if (res_free == NULL)
		kfree(res);
	else
		res_free(res);
	return ret;
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

static struct vmw_resource *
vmw_user_context_base_to_res(struct ttm_base_object *base)
{
	return &(container_of(base, struct vmw_user_context, base)->res);
}

static void vmw_user_context_free(struct vmw_resource *res)
{
	struct vmw_user_context *ctx =
	    container_of(res, struct vmw_user_context, res);
	struct vmw_private *dev_priv = res->dev_priv;

	ttm_base_object_kfree(ctx, base);
	ttm_mem_global_free(vmw_mem_glob(dev_priv),
			    vmw_user_context_size);
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
	struct drm_vmw_context_arg *arg = (struct drm_vmw_context_arg *)data;
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;

	return ttm_ref_object_base_unref(tfile, arg->cid, TTM_REF_USAGE);
}

int vmw_context_define_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct vmw_user_context *ctx;
	struct vmw_resource *res;
	struct vmw_resource *tmp;
	struct drm_vmw_context_arg *arg = (struct drm_vmw_context_arg *)data;
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct vmw_master *vmaster = vmw_master(file_priv->master);
	int ret;


	/*
	 * Approximate idr memory usage with 128 bytes. It will be limited
	 * by maximum number_of contexts anyway.
	 */

	if (unlikely(vmw_user_context_size == 0))
		vmw_user_context_size = ttm_round_pot(sizeof(*ctx)) + 128;

	ret = ttm_read_lock(&vmaster->lock, true);
	if (unlikely(ret != 0))
		return ret;

	ret = ttm_mem_global_alloc(vmw_mem_glob(dev_priv),
				   vmw_user_context_size,
				   false, true);
	if (unlikely(ret != 0)) {
		if (ret != -ERESTARTSYS)
			DRM_ERROR("Out of graphics memory for context"
				  " creation.\n");
		goto out_unlock;
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (unlikely(ctx == NULL)) {
		ttm_mem_global_free(vmw_mem_glob(dev_priv),
				    vmw_user_context_size);
		ret = -ENOMEM;
		goto out_unlock;
	}

	res = &ctx->res;
	ctx->base.shareable = false;
	ctx->base.tfile = NULL;

	/*
	 * From here on, the destructor takes over resource freeing.
	 */

	ret = vmw_context_init(dev_priv, res, vmw_user_context_free);
	if (unlikely(ret != 0))
		goto out_unlock;

	tmp = vmw_resource_reference(&ctx->res);
	ret = ttm_base_object_init(tfile, &ctx->base, false, VMW_RES_CONTEXT,
				   &vmw_user_context_base_release, NULL);

	if (unlikely(ret != 0)) {
		vmw_resource_unreference(&tmp);
		goto out_err;
	}

	arg->cid = ctx->base.hash.key;
out_err:
	vmw_resource_unreference(&res);
out_unlock:
	ttm_read_unlock(&vmaster->lock);
	return ret;

}

/**
 * struct vmw_bpp - Bits per pixel info for surface storage size computation.
 *
 * @bpp:         Bits per pixel.
 * @s_bpp:       Stride bits per pixel. See definition below.
 *
 */
struct vmw_bpp {
	uint8_t bpp;
	uint8_t s_bpp;
};

/*
 * Size table for the supported SVGA3D surface formats. It consists of
 * two values. The bpp value and the s_bpp value which is short for
 * "stride bits per pixel" The values are given in such a way that the
 * minimum stride for the image is calculated using
 *
 * min_stride = w*s_bpp
 *
 * and the total memory requirement for the image is
 *
 * h*min_stride*bpp/s_bpp
 *
 */
static const struct vmw_bpp vmw_sf_bpp[] = {
	[SVGA3D_FORMAT_INVALID] = {0, 0},
	[SVGA3D_X8R8G8B8] = {32, 32},
	[SVGA3D_A8R8G8B8] = {32, 32},
	[SVGA3D_R5G6B5] = {16, 16},
	[SVGA3D_X1R5G5B5] = {16, 16},
	[SVGA3D_A1R5G5B5] = {16, 16},
	[SVGA3D_A4R4G4B4] = {16, 16},
	[SVGA3D_Z_D32] = {32, 32},
	[SVGA3D_Z_D16] = {16, 16},
	[SVGA3D_Z_D24S8] = {32, 32},
	[SVGA3D_Z_D15S1] = {16, 16},
	[SVGA3D_LUMINANCE8] = {8, 8},
	[SVGA3D_LUMINANCE4_ALPHA4] = {8, 8},
	[SVGA3D_LUMINANCE16] = {16, 16},
	[SVGA3D_LUMINANCE8_ALPHA8] = {16, 16},
	[SVGA3D_DXT1] = {4, 16},
	[SVGA3D_DXT2] = {8, 32},
	[SVGA3D_DXT3] = {8, 32},
	[SVGA3D_DXT4] = {8, 32},
	[SVGA3D_DXT5] = {8, 32},
	[SVGA3D_BUMPU8V8] = {16, 16},
	[SVGA3D_BUMPL6V5U5] = {16, 16},
	[SVGA3D_BUMPX8L8V8U8] = {32, 32},
	[SVGA3D_ARGB_S10E5] = {16, 16},
	[SVGA3D_ARGB_S23E8] = {32, 32},
	[SVGA3D_A2R10G10B10] = {32, 32},
	[SVGA3D_V8U8] = {16, 16},
	[SVGA3D_Q8W8V8U8] = {32, 32},
	[SVGA3D_CxV8U8] = {16, 16},
	[SVGA3D_X8L8V8U8] = {32, 32},
	[SVGA3D_A2W10V10U10] = {32, 32},
	[SVGA3D_ALPHA8] = {8, 8},
	[SVGA3D_R_S10E5] = {16, 16},
	[SVGA3D_R_S23E8] = {32, 32},
	[SVGA3D_RG_S10E5] = {16, 16},
	[SVGA3D_RG_S23E8] = {32, 32},
	[SVGA3D_BUFFER] = {8, 8},
	[SVGA3D_Z_D24X8] = {32, 32},
	[SVGA3D_V16U16] = {32, 32},
	[SVGA3D_G16R16] = {32, 32},
	[SVGA3D_A16B16G16R16] = {64,  64},
	[SVGA3D_UYVY] = {12, 12},
	[SVGA3D_YUY2] = {12, 12},
	[SVGA3D_NV12] = {12, 8},
	[SVGA3D_AYUV] = {32, 32},
	[SVGA3D_BC4_UNORM] = {4,  16},
	[SVGA3D_BC5_UNORM] = {8,  32},
	[SVGA3D_Z_DF16] = {16,  16},
	[SVGA3D_Z_DF24] = {24,  24},
	[SVGA3D_Z_D24S8_INT] = {32,  32}
};


/**
 * struct vmw_surface_dma - SVGA3D DMA command
 */
struct vmw_surface_dma {
	SVGA3dCmdHeader header;
	SVGA3dCmdSurfaceDMA body;
	SVGA3dCopyBox cb;
	SVGA3dCmdSurfaceDMASuffix suffix;
};

/**
 * struct vmw_surface_define - SVGA3D Surface Define command
 */
struct vmw_surface_define {
	SVGA3dCmdHeader header;
	SVGA3dCmdDefineSurface body;
};

/**
 * struct vmw_surface_destroy - SVGA3D Surface Destroy command
 */
struct vmw_surface_destroy {
	SVGA3dCmdHeader header;
	SVGA3dCmdDestroySurface body;
};


/**
 * vmw_surface_dma_size - Compute fifo size for a dma command.
 *
 * @srf: Pointer to a struct vmw_surface
 *
 * Computes the required size for a surface dma command for backup or
 * restoration of the surface represented by @srf.
 */
static inline uint32_t vmw_surface_dma_size(const struct vmw_surface *srf)
{
	return srf->num_sizes * sizeof(struct vmw_surface_dma);
}


/**
 * vmw_surface_define_size - Compute fifo size for a surface define command.
 *
 * @srf: Pointer to a struct vmw_surface
 *
 * Computes the required size for a surface define command for the definition
 * of the surface represented by @srf.
 */
static inline uint32_t vmw_surface_define_size(const struct vmw_surface *srf)
{
	return sizeof(struct vmw_surface_define) + srf->num_sizes *
		sizeof(SVGA3dSize);
}


/**
 * vmw_surface_destroy_size - Compute fifo size for a surface destroy command.
 *
 * Computes the required size for a surface destroy command for the destruction
 * of a hw surface.
 */
static inline uint32_t vmw_surface_destroy_size(void)
{
	return sizeof(struct vmw_surface_destroy);
}

/**
 * vmw_surface_destroy_encode - Encode a surface_destroy command.
 *
 * @id: The surface id
 * @cmd_space: Pointer to memory area in which the commands should be encoded.
 */
static void vmw_surface_destroy_encode(uint32_t id,
				       void *cmd_space)
{
	struct vmw_surface_destroy *cmd = (struct vmw_surface_destroy *)
		cmd_space;

	cmd->header.id = SVGA_3D_CMD_SURFACE_DESTROY;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.sid = id;
}

/**
 * vmw_surface_define_encode - Encode a surface_define command.
 *
 * @srf: Pointer to a struct vmw_surface object.
 * @cmd_space: Pointer to memory area in which the commands should be encoded.
 */
static void vmw_surface_define_encode(const struct vmw_surface *srf,
				      void *cmd_space)
{
	struct vmw_surface_define *cmd = (struct vmw_surface_define *)
		cmd_space;
	struct drm_vmw_size *src_size;
	SVGA3dSize *cmd_size;
	uint32_t cmd_len;
	int i;

	cmd_len = sizeof(cmd->body) + srf->num_sizes * sizeof(SVGA3dSize);

	cmd->header.id = SVGA_3D_CMD_SURFACE_DEFINE;
	cmd->header.size = cmd_len;
	cmd->body.sid = srf->res.id;
	cmd->body.surfaceFlags = srf->flags;
	cmd->body.format = cpu_to_le32(srf->format);
	for (i = 0; i < DRM_VMW_MAX_SURFACE_FACES; ++i)
		cmd->body.face[i].numMipLevels = srf->mip_levels[i];

	cmd += 1;
	cmd_size = (SVGA3dSize *) cmd;
	src_size = srf->sizes;

	for (i = 0; i < srf->num_sizes; ++i, cmd_size++, src_size++) {
		cmd_size->width = src_size->width;
		cmd_size->height = src_size->height;
		cmd_size->depth = src_size->depth;
	}
}

/**
 * vmw_surface_dma_encode - Encode a surface_dma command.
 *
 * @srf: Pointer to a struct vmw_surface object.
 * @cmd_space: Pointer to memory area in which the commands should be encoded.
 * @ptr: Pointer to an SVGAGuestPtr indicating where the surface contents
 * should be placed or read from.
 * @to_surface: Boolean whether to DMA to the surface or from the surface.
 */
static void vmw_surface_dma_encode(struct vmw_surface *srf,
				   void *cmd_space,
				   const SVGAGuestPtr *ptr,
				   bool to_surface)
{
	uint32_t i;
	uint32_t bpp = vmw_sf_bpp[srf->format].bpp;
	uint32_t stride_bpp = vmw_sf_bpp[srf->format].s_bpp;
	struct vmw_surface_dma *cmd = (struct vmw_surface_dma *)cmd_space;

	for (i = 0; i < srf->num_sizes; ++i) {
		SVGA3dCmdHeader *header = &cmd->header;
		SVGA3dCmdSurfaceDMA *body = &cmd->body;
		SVGA3dCopyBox *cb = &cmd->cb;
		SVGA3dCmdSurfaceDMASuffix *suffix = &cmd->suffix;
		const struct vmw_surface_offset *cur_offset = &srf->offsets[i];
		const struct drm_vmw_size *cur_size = &srf->sizes[i];

		header->id = SVGA_3D_CMD_SURFACE_DMA;
		header->size = sizeof(*body) + sizeof(*cb) + sizeof(*suffix);

		body->guest.ptr = *ptr;
		body->guest.ptr.offset += cur_offset->bo_offset;
		body->guest.pitch = (cur_size->width * stride_bpp + 7) >> 3;
		body->host.sid = srf->res.id;
		body->host.face = cur_offset->face;
		body->host.mipmap = cur_offset->mip;
		body->transfer = ((to_surface) ?  SVGA3D_WRITE_HOST_VRAM :
				  SVGA3D_READ_HOST_VRAM);
		cb->x = 0;
		cb->y = 0;
		cb->z = 0;
		cb->srcx = 0;
		cb->srcy = 0;
		cb->srcz = 0;
		cb->w = cur_size->width;
		cb->h = cur_size->height;
		cb->d = cur_size->depth;

		suffix->suffixSize = sizeof(*suffix);
		suffix->maximumOffset = body->guest.pitch*cur_size->height*
			cur_size->depth*bpp / stride_bpp;
		suffix->flags.discard = 0;
		suffix->flags.unsynchronized = 0;
		suffix->flags.reserved = 0;
		++cmd;
	}
};


/**
 * vmw_hw_surface_destroy - destroy a Device surface
 *
 * @res:        Pointer to a struct vmw_resource embedded in a struct
 *              vmw_surface.
 *
 * Destroys a the device surface associated with a struct vmw_surface if
 * any, and adjusts accounting and resource count accordingly.
 */
static void vmw_hw_surface_destroy(struct vmw_resource *res)
{

	struct vmw_private *dev_priv = res->dev_priv;
	struct vmw_surface *srf;
	void *cmd;

	if (res->id != -1) {

		cmd = vmw_fifo_reserve(dev_priv, vmw_surface_destroy_size());
		if (unlikely(cmd == NULL)) {
			DRM_ERROR("Failed reserving FIFO space for surface "
				  "destruction.\n");
			return;
		}

		vmw_surface_destroy_encode(res->id, cmd);
		vmw_fifo_commit(dev_priv, vmw_surface_destroy_size());

		/*
		 * used_memory_size_atomic, or separate lock
		 * to avoid taking dev_priv::cmdbuf_mutex in
		 * the destroy path.
		 */

		mutex_lock(&dev_priv->cmdbuf_mutex);
		srf = vmw_res_to_srf(res);
		dev_priv->used_memory_size -= res->backup_size;
		mutex_unlock(&dev_priv->cmdbuf_mutex);
	}
	vmw_3d_resource_dec(dev_priv, false);
}

/**
 * vmw_legacy_srf_create - Create a device surface as part of the
 * resource validation process.
 *
 * @res: Pointer to a struct vmw_surface.
 *
 * If the surface doesn't have a hw id.
 *
 * Returns -EBUSY if there wasn't sufficient device resources to
 * complete the validation. Retry after freeing up resources.
 *
 * May return other errors if the kernel is out of guest resources.
 */
static int vmw_legacy_srf_create(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct vmw_surface *srf;
	uint32_t submit_size;
	uint8_t *cmd;
	int ret;

	if (likely(res->id != -1))
		return 0;

	srf = vmw_res_to_srf(res);
	if (unlikely(dev_priv->used_memory_size + res->backup_size >=
		     dev_priv->memory_size))
		return -EBUSY;

	/*
	 * Alloc id for the resource.
	 */

	ret = vmw_resource_alloc_id(res);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed to allocate a surface id.\n");
		goto out_no_id;
	}

	if (unlikely(res->id >= SVGA3D_MAX_SURFACE_IDS)) {
		ret = -EBUSY;
		goto out_no_fifo;
	}

	/*
	 * Encode surface define- commands.
	 */

	submit_size = vmw_surface_define_size(srf);
	cmd = vmw_fifo_reserve(dev_priv, submit_size);
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Failed reserving FIFO space for surface "
			  "creation.\n");
		ret = -ENOMEM;
		goto out_no_fifo;
	}

	vmw_surface_define_encode(srf, cmd);
	vmw_fifo_commit(dev_priv, submit_size);
	/*
	 * Surface memory usage accounting.
	 */

	dev_priv->used_memory_size += res->backup_size;
	return 0;

out_no_fifo:
	vmw_resource_release_id(res);
out_no_id:
	return ret;
}

/**
 * vmw_legacy_srf_dma - Copy backup data to or from a legacy surface.
 *
 * @res:            Pointer to a struct vmw_res embedded in a struct
 *                  vmw_surface.
 * @val_buf:        Pointer to a struct ttm_validate_buffer containing
 *                  information about the backup buffer.
 * @bind:           Boolean wether to DMA to the surface.
 *
 * Transfer backup data to or from a legacy surface as part of the
 * validation process.
 * May return other errors if the kernel is out of guest resources.
 * The backup buffer will be fenced or idle upon successful completion,
 * and if the surface needs persistent backup storage, the backup buffer
 * will also be returned reserved iff @bind is true.
 */
static int vmw_legacy_srf_dma(struct vmw_resource *res,
			      struct ttm_validate_buffer *val_buf,
			      bool bind)
{
	SVGAGuestPtr ptr;
	struct vmw_fence_obj *fence;
	uint32_t submit_size;
	struct vmw_surface *srf = vmw_res_to_srf(res);
	uint8_t *cmd;
	struct vmw_private *dev_priv = res->dev_priv;

	BUG_ON(val_buf->bo == NULL);

	submit_size = vmw_surface_dma_size(srf);
	cmd = vmw_fifo_reserve(dev_priv, submit_size);
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Failed reserving FIFO space for surface "
			  "DMA.\n");
		return -ENOMEM;
	}
	vmw_bo_get_guest_ptr(val_buf->bo, &ptr);
	vmw_surface_dma_encode(srf, cmd, &ptr, bind);

	vmw_fifo_commit(dev_priv, submit_size);

	/*
	 * Create a fence object and fence the backup buffer.
	 */

	(void) vmw_execbuf_fence_commands(NULL, dev_priv,
					  &fence, NULL);

	vmw_fence_single_bo(val_buf->bo, fence);

	if (likely(fence != NULL))
		vmw_fence_obj_unreference(&fence);

	return 0;
}

/**
 * vmw_legacy_srf_bind - Perform a legacy surface bind as part of the
 *                       surface validation process.
 *
 * @res:            Pointer to a struct vmw_res embedded in a struct
 *                  vmw_surface.
 * @val_buf:        Pointer to a struct ttm_validate_buffer containing
 *                  information about the backup buffer.
 *
 * This function will copy backup data to the surface if the
 * backup buffer is dirty.
 */
static int vmw_legacy_srf_bind(struct vmw_resource *res,
			       struct ttm_validate_buffer *val_buf)
{
	if (!res->backup_dirty)
		return 0;

	return vmw_legacy_srf_dma(res, val_buf, true);
}


/**
 * vmw_legacy_srf_unbind - Perform a legacy surface unbind as part of the
 *                         surface eviction process.
 *
 * @res:            Pointer to a struct vmw_res embedded in a struct
 *                  vmw_surface.
 * @val_buf:        Pointer to a struct ttm_validate_buffer containing
 *                  information about the backup buffer.
 *
 * This function will copy backup data from the surface.
 */
static int vmw_legacy_srf_unbind(struct vmw_resource *res,
				 bool readback,
				 struct ttm_validate_buffer *val_buf)
{
	if (unlikely(readback))
		return vmw_legacy_srf_dma(res, val_buf, false);
	return 0;
}

/**
 * vmw_legacy_srf_destroy - Destroy a device surface as part of a
 *                          resource eviction process.
 *
 * @res:            Pointer to a struct vmw_res embedded in a struct
 *                  vmw_surface.
 */
static int vmw_legacy_srf_destroy(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	uint32_t submit_size;
	uint8_t *cmd;

	BUG_ON(res->id == -1);

	/*
	 * Encode the dma- and surface destroy commands.
	 */

	submit_size = vmw_surface_destroy_size();
	cmd = vmw_fifo_reserve(dev_priv, submit_size);
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Failed reserving FIFO space for surface "
			  "eviction.\n");
		return -ENOMEM;
	}

	vmw_surface_destroy_encode(res->id, cmd);
	vmw_fifo_commit(dev_priv, submit_size);

	/*
	 * Surface memory usage accounting.
	 */

	dev_priv->used_memory_size -= res->backup_size;

	/*
	 * Release the surface ID.
	 */

	vmw_resource_release_id(res);

	return 0;
}


/**
 * vmw_surface_init - initialize a struct vmw_surface
 *
 * @dev_priv:       Pointer to a device private struct.
 * @srf:            Pointer to the struct vmw_surface to initialize.
 * @res_free:       Pointer to a resource destructor used to free
 *                  the object.
 */
static int vmw_surface_init(struct vmw_private *dev_priv,
			    struct vmw_surface *srf,
			    void (*res_free) (struct vmw_resource *res))
{
	int ret;
	struct vmw_resource *res = &srf->res;

	BUG_ON(res_free == NULL);
	(void) vmw_3d_resource_inc(dev_priv, false);
	ret = vmw_resource_init(dev_priv, res, true, res_free,
				&vmw_legacy_surface_func);

	if (unlikely(ret != 0)) {
		vmw_3d_resource_dec(dev_priv, false);
		res_free(res);
		return ret;
	}

	/*
	 * The surface won't be visible to hardware until a
	 * surface validate.
	 */

	vmw_resource_activate(res, vmw_hw_surface_destroy);
	return ret;
}

/**
 * vmw_user_surface_base_to_res - TTM base object to resource converter for
 *                                user visible surfaces
 *
 * @base:           Pointer to a TTM base object
 *
 * Returns the struct vmw_resource embedded in a struct vmw_surface
 * for the user-visible object identified by the TTM base object @base.
 */
static struct vmw_resource *
vmw_user_surface_base_to_res(struct ttm_base_object *base)
{
	return &(container_of(base, struct vmw_user_surface, base)->srf.res);
}

/**
 * vmw_user_surface_free - User visible surface resource destructor
 *
 * @res:            A struct vmw_resource embedded in a struct vmw_surface.
 */
static void vmw_user_surface_free(struct vmw_resource *res)
{
	struct vmw_surface *srf = vmw_res_to_srf(res);
	struct vmw_user_surface *user_srf =
	    container_of(srf, struct vmw_user_surface, srf);
	struct vmw_private *dev_priv = srf->res.dev_priv;
	uint32_t size = user_srf->size;

	kfree(srf->offsets);
	kfree(srf->sizes);
	kfree(srf->snooper.image);
	ttm_base_object_kfree(user_srf, base);
	ttm_mem_global_free(vmw_mem_glob(dev_priv), size);
}

/**
 * vmw_user_surface_free - User visible surface TTM base object destructor
 *
 * @p_base:         Pointer to a pointer to a TTM base object
 *                  embedded in a struct vmw_user_surface.
 *
 * Drops the base object's reference on its resource, and the
 * pointer pointed to by *p_base is set to NULL.
 */
static void vmw_user_surface_base_release(struct ttm_base_object **p_base)
{
	struct ttm_base_object *base = *p_base;
	struct vmw_user_surface *user_srf =
	    container_of(base, struct vmw_user_surface, base);
	struct vmw_resource *res = &user_srf->srf.res;

	*p_base = NULL;
	vmw_resource_unreference(&res);
}

/**
 * vmw_user_surface_destroy_ioctl - Ioctl function implementing
 *                                  the user surface destroy functionality.
 *
 * @dev:            Pointer to a struct drm_device.
 * @data:           Pointer to data copied from / to user-space.
 * @file_priv:      Pointer to a drm file private structure.
 */
int vmw_surface_destroy_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct drm_vmw_surface_arg *arg = (struct drm_vmw_surface_arg *)data;
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;

	return ttm_ref_object_base_unref(tfile, arg->sid, TTM_REF_USAGE);
}

/**
 * vmw_user_surface_define_ioctl - Ioctl function implementing
 *                                  the user surface define functionality.
 *
 * @dev:            Pointer to a struct drm_device.
 * @data:           Pointer to data copied from / to user-space.
 * @file_priv:      Pointer to a drm file private structure.
 */
int vmw_surface_define_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct vmw_user_surface *user_srf;
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
	int i, j;
	uint32_t cur_bo_offset;
	struct drm_vmw_size *cur_size;
	struct vmw_surface_offset *cur_offset;
	uint32_t stride_bpp;
	uint32_t bpp;
	uint32_t num_sizes;
	uint32_t size;
	struct vmw_master *vmaster = vmw_master(file_priv->master);

	if (unlikely(vmw_user_surface_size == 0))
		vmw_user_surface_size = ttm_round_pot(sizeof(*user_srf)) +
			128;

	num_sizes = 0;
	for (i = 0; i < DRM_VMW_MAX_SURFACE_FACES; ++i)
		num_sizes += req->mip_levels[i];

	if (num_sizes > DRM_VMW_MAX_SURFACE_FACES *
	    DRM_VMW_MAX_MIP_LEVELS)
		return -EINVAL;

	size = vmw_user_surface_size + 128 +
		ttm_round_pot(num_sizes * sizeof(struct drm_vmw_size)) +
		ttm_round_pot(num_sizes * sizeof(struct vmw_surface_offset));


	ret = ttm_read_lock(&vmaster->lock, true);
	if (unlikely(ret != 0))
		return ret;

	ret = ttm_mem_global_alloc(vmw_mem_glob(dev_priv),
				   size, false, true);
	if (unlikely(ret != 0)) {
		if (ret != -ERESTARTSYS)
			DRM_ERROR("Out of graphics memory for surface"
				  " creation.\n");
		goto out_unlock;
	}

	user_srf = kzalloc(sizeof(*user_srf), GFP_KERNEL);
	if (unlikely(user_srf == NULL)) {
		ret = -ENOMEM;
		goto out_no_user_srf;
	}

	srf = &user_srf->srf;
	res = &srf->res;

	srf->flags = req->flags;
	srf->format = req->format;
	srf->scanout = req->scanout;

	memcpy(srf->mip_levels, req->mip_levels, sizeof(srf->mip_levels));
	srf->num_sizes = num_sizes;
	user_srf->size = size;

	srf->sizes = kmalloc(srf->num_sizes * sizeof(*srf->sizes), GFP_KERNEL);
	if (unlikely(srf->sizes == NULL)) {
		ret = -ENOMEM;
		goto out_no_sizes;
	}
	srf->offsets = kmalloc(srf->num_sizes * sizeof(*srf->offsets),
			       GFP_KERNEL);
	if (unlikely(srf->sizes == NULL)) {
		ret = -ENOMEM;
		goto out_no_offsets;
	}

	user_sizes = (struct drm_vmw_size __user *)(unsigned long)
	    req->size_addr;

	ret = copy_from_user(srf->sizes, user_sizes,
			     srf->num_sizes * sizeof(*srf->sizes));
	if (unlikely(ret != 0)) {
		ret = -EFAULT;
		goto out_no_copy;
	}

	srf->base_size = *srf->sizes;
	srf->autogen_filter = SVGA3D_TEX_FILTER_NONE;
	srf->multisample_count = 1;

	cur_bo_offset = 0;
	cur_offset = srf->offsets;
	cur_size = srf->sizes;

	bpp = vmw_sf_bpp[srf->format].bpp;
	stride_bpp = vmw_sf_bpp[srf->format].s_bpp;

	for (i = 0; i < DRM_VMW_MAX_SURFACE_FACES; ++i) {
		for (j = 0; j < srf->mip_levels[i]; ++j) {
			uint32_t stride =
				(cur_size->width * stride_bpp + 7) >> 3;

			cur_offset->face = i;
			cur_offset->mip = j;
			cur_offset->bo_offset = cur_bo_offset;
			cur_bo_offset += stride * cur_size->height *
				cur_size->depth * bpp / stride_bpp;
			++cur_offset;
			++cur_size;
		}
	}
	res->backup_size = cur_bo_offset;

	if (srf->scanout &&
	    srf->num_sizes == 1 &&
	    srf->sizes[0].width == 64 &&
	    srf->sizes[0].height == 64 &&
	    srf->format == SVGA3D_A8R8G8B8) {

		/* allocate image area and clear it */
		srf->snooper.image = kzalloc(64 * 64 * 4, GFP_KERNEL);
		if (!srf->snooper.image) {
			DRM_ERROR("Failed to allocate cursor_image\n");
			ret = -ENOMEM;
			goto out_no_copy;
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
		goto out_unlock;

	tmp = vmw_resource_reference(&srf->res);
	ret = ttm_base_object_init(tfile, &user_srf->base,
				   req->shareable, VMW_RES_SURFACE,
				   &vmw_user_surface_base_release, NULL);

	if (unlikely(ret != 0)) {
		vmw_resource_unreference(&tmp);
		vmw_resource_unreference(&res);
		goto out_unlock;
	}

	rep->sid = user_srf->base.hash.key;
	vmw_resource_unreference(&res);

	ttm_read_unlock(&vmaster->lock);
	return 0;
out_no_copy:
	kfree(srf->offsets);
out_no_offsets:
	kfree(srf->sizes);
out_no_sizes:
	kfree(user_srf);
out_no_user_srf:
	ttm_mem_global_free(vmw_mem_glob(dev_priv), size);
out_unlock:
	ttm_read_unlock(&vmaster->lock);
	return ret;
}

/**
 * vmw_user_surface_define_ioctl - Ioctl function implementing
 *                                  the user surface reference functionality.
 *
 * @dev:            Pointer to a struct drm_device.
 * @data:           Pointer to data copied from / to user-space.
 * @file_priv:      Pointer to a drm file private structure.
 */
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
