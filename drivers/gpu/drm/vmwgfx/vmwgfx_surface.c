/**************************************************************************
 *
 * Copyright © 2009-2012 VMware, Inc., Palo Alto, CA., USA
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
#include "vmwgfx_resource_priv.h"
#include <ttm/ttm_placement.h>

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

		srf->snooper.image = kmalloc(64 * 64 * 4, GFP_KERNEL);
		/* clear the image */
		if (srf->snooper.image) {
			memset(srf->snooper.image, 0x00, 64 * 64 * 4);
		} else {
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
	ttm_base_object_kfree(user_srf, base);
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
