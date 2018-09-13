// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright 2009-2015 VMware, Inc., Palo Alto, CA., USA
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

#include <drm/ttm/ttm_placement.h>

#include "vmwgfx_drv.h"
#include "vmwgfx_resource_priv.h"
#include "vmwgfx_so.h"
#include "vmwgfx_binding.h"
#include "device_include/svga3d_surfacedefs.h"

#define SVGA3D_FLAGS_64(upper32, lower32) (((uint64_t)upper32 << 32) | lower32)
#define SVGA3D_FLAGS_UPPER_32(svga3d_flags) (svga3d_flags >> 32)
#define SVGA3D_FLAGS_LOWER_32(svga3d_flags) \
	(svga3d_flags & ((uint64_t)U32_MAX))

/**
 * struct vmw_user_surface - User-space visible surface resource
 *
 * @base:           The TTM base object handling user-space visibility.
 * @srf:            The surface metadata.
 * @size:           TTM accounting size for the surface.
 * @master: master of the creating client. Used for security check.
 */
struct vmw_user_surface {
	struct ttm_prime_object prime;
	struct vmw_surface srf;
	uint32_t size;
	struct drm_master *master;
	struct ttm_base_object *backup_base;
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
static int vmw_gb_surface_create(struct vmw_resource *res);
static int vmw_gb_surface_bind(struct vmw_resource *res,
			       struct ttm_validate_buffer *val_buf);
static int vmw_gb_surface_unbind(struct vmw_resource *res,
				 bool readback,
				 struct ttm_validate_buffer *val_buf);
static int vmw_gb_surface_destroy(struct vmw_resource *res);
static int
vmw_gb_surface_define_internal(struct drm_device *dev,
			       struct drm_vmw_gb_surface_create_ext_req *req,
			       struct drm_vmw_gb_surface_create_rep *rep,
			       struct drm_file *file_priv);
static int
vmw_gb_surface_reference_internal(struct drm_device *dev,
				  struct drm_vmw_surface_arg *req,
				  struct drm_vmw_gb_surface_ref_ext_rep *rep,
				  struct drm_file *file_priv);

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

static const struct vmw_res_func vmw_gb_surface_func = {
	.res_type = vmw_res_surface,
	.needs_backup = true,
	.may_evict = true,
	.type_name = "guest backed surfaces",
	.backup_placement = &vmw_mob_placement,
	.create = vmw_gb_surface_create,
	.destroy = vmw_gb_surface_destroy,
	.bind = vmw_gb_surface_bind,
	.unbind = vmw_gb_surface_unbind
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
	/*
	 * Downcast of surfaceFlags, was upcasted when received from user-space,
	 * since driver internally stores as 64 bit.
	 * For legacy surface define only 32 bit flag is supported.
	 */
	cmd->body.surfaceFlags = (SVGA3dSurface1Flags)srf->flags;
	cmd->body.format = srf->format;
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
	struct vmw_surface_dma *cmd = (struct vmw_surface_dma *)cmd_space;
	const struct svga3d_surface_desc *desc =
		svga3dsurface_get_desc(srf->format);

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
		body->guest.pitch = svga3dsurface_calculate_pitch(desc,
								  cur_size);
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
		suffix->maximumOffset =
			svga3dsurface_get_image_buffer_size(desc, cur_size,
							    body->guest.pitch);
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

	if (res->func->destroy == vmw_gb_surface_destroy) {
		(void) vmw_gb_surface_destroy(res);
		return;
	}

	if (res->id != -1) {

		cmd = vmw_fifo_reserve(dev_priv, vmw_surface_destroy_size());
		if (unlikely(!cmd)) {
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
	if (unlikely(!cmd)) {
		DRM_ERROR("Failed reserving FIFO space for surface "
			  "creation.\n");
		ret = -ENOMEM;
		goto out_no_fifo;
	}

	vmw_surface_define_encode(srf, cmd);
	vmw_fifo_commit(dev_priv, submit_size);
	vmw_fifo_resource_inc(dev_priv);

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

	BUG_ON(!val_buf->bo);
	submit_size = vmw_surface_dma_size(srf);
	cmd = vmw_fifo_reserve(dev_priv, submit_size);
	if (unlikely(!cmd)) {
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

	vmw_bo_fence_single(val_buf->bo, fence);

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
	if (unlikely(!cmd)) {
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
	vmw_fifo_resource_dec(dev_priv);

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

	BUG_ON(!res_free);
	ret = vmw_resource_init(dev_priv, res, true, res_free,
				(dev_priv->has_mob) ? &vmw_gb_surface_func :
				&vmw_legacy_surface_func);

	if (unlikely(ret != 0)) {
		res_free(res);
		return ret;
	}

	/*
	 * The surface won't be visible to hardware until a
	 * surface validate.
	 */

	INIT_LIST_HEAD(&srf->view_list);
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
	return &(container_of(base, struct vmw_user_surface,
			      prime.base)->srf.res);
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

	if (user_srf->master)
		drm_master_put(&user_srf->master);
	kfree(srf->offsets);
	kfree(srf->sizes);
	kfree(srf->snooper.image);
	ttm_prime_object_kfree(user_srf, prime);
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
	    container_of(base, struct vmw_user_surface, prime.base);
	struct vmw_resource *res = &user_srf->srf.res;

	*p_base = NULL;
	if (user_srf->backup_base)
		ttm_base_object_unref(&user_srf->backup_base);
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
	struct ttm_operation_ctx ctx = {
		.interruptible = true,
		.no_wait_gpu = false
	};
	int ret;
	int i, j;
	uint32_t cur_bo_offset;
	struct drm_vmw_size *cur_size;
	struct vmw_surface_offset *cur_offset;
	uint32_t num_sizes;
	uint32_t size;
	const struct svga3d_surface_desc *desc;

	if (unlikely(vmw_user_surface_size == 0))
		vmw_user_surface_size = ttm_round_pot(sizeof(*user_srf)) +
			128;

	num_sizes = 0;
	for (i = 0; i < DRM_VMW_MAX_SURFACE_FACES; ++i) {
		if (req->mip_levels[i] > DRM_VMW_MAX_MIP_LEVELS)
			return -EINVAL;
		num_sizes += req->mip_levels[i];
	}

	if (num_sizes > DRM_VMW_MAX_SURFACE_FACES * DRM_VMW_MAX_MIP_LEVELS ||
	    num_sizes == 0)
		return -EINVAL;

	size = vmw_user_surface_size + 128 +
		ttm_round_pot(num_sizes * sizeof(struct drm_vmw_size)) +
		ttm_round_pot(num_sizes * sizeof(struct vmw_surface_offset));


	desc = svga3dsurface_get_desc(req->format);
	if (unlikely(desc->block_desc == SVGA3DBLOCKDESC_NONE)) {
		DRM_ERROR("Invalid surface format for surface creation.\n");
		DRM_ERROR("Format requested is: %d\n", req->format);
		return -EINVAL;
	}

	ret = ttm_read_lock(&dev_priv->reservation_sem, true);
	if (unlikely(ret != 0))
		return ret;

	ret = ttm_mem_global_alloc(vmw_mem_glob(dev_priv),
				   size, &ctx);
	if (unlikely(ret != 0)) {
		if (ret != -ERESTARTSYS)
			DRM_ERROR("Out of graphics memory for surface"
				  " creation.\n");
		goto out_unlock;
	}

	user_srf = kzalloc(sizeof(*user_srf), GFP_KERNEL);
	if (unlikely(!user_srf)) {
		ret = -ENOMEM;
		goto out_no_user_srf;
	}

	srf = &user_srf->srf;
	res = &srf->res;

	/* Driver internally stores as 64-bit flags */
	srf->flags = (SVGA3dSurfaceAllFlags)req->flags;
	srf->format = req->format;
	srf->scanout = req->scanout;

	memcpy(srf->mip_levels, req->mip_levels, sizeof(srf->mip_levels));
	srf->num_sizes = num_sizes;
	user_srf->size = size;
	srf->sizes = memdup_user((struct drm_vmw_size __user *)(unsigned long)
				 req->size_addr,
				 sizeof(*srf->sizes) * srf->num_sizes);
	if (IS_ERR(srf->sizes)) {
		ret = PTR_ERR(srf->sizes);
		goto out_no_sizes;
	}
	srf->offsets = kmalloc_array(srf->num_sizes,
				     sizeof(*srf->offsets),
				     GFP_KERNEL);
	if (unlikely(!srf->offsets)) {
		ret = -ENOMEM;
		goto out_no_offsets;
	}

	srf->base_size = *srf->sizes;
	srf->autogen_filter = SVGA3D_TEX_FILTER_NONE;
	srf->multisample_count = 0;
	srf->multisample_pattern = SVGA3D_MS_PATTERN_NONE;
	srf->quality_level = SVGA3D_MS_QUALITY_NONE;

	cur_bo_offset = 0;
	cur_offset = srf->offsets;
	cur_size = srf->sizes;

	for (i = 0; i < DRM_VMW_MAX_SURFACE_FACES; ++i) {
		for (j = 0; j < srf->mip_levels[i]; ++j) {
			uint32_t stride = svga3dsurface_calculate_pitch
				(desc, cur_size);

			cur_offset->face = i;
			cur_offset->mip = j;
			cur_offset->bo_offset = cur_bo_offset;
			cur_bo_offset += svga3dsurface_get_image_buffer_size
				(desc, cur_size, stride);
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

		srf->snooper.image = kzalloc(64 * 64 * 4, GFP_KERNEL);
		if (!srf->snooper.image) {
			DRM_ERROR("Failed to allocate cursor_image\n");
			ret = -ENOMEM;
			goto out_no_copy;
		}
	} else {
		srf->snooper.image = NULL;
	}

	user_srf->prime.base.shareable = false;
	user_srf->prime.base.tfile = NULL;
	if (drm_is_primary_client(file_priv))
		user_srf->master = drm_master_get(file_priv->master);

	/**
	 * From this point, the generic resource management functions
	 * destroy the object on failure.
	 */

	ret = vmw_surface_init(dev_priv, srf, vmw_user_surface_free);
	if (unlikely(ret != 0))
		goto out_unlock;

	/*
	 * A gb-aware client referencing a shared surface will
	 * expect a backup buffer to be present.
	 */
	if (dev_priv->has_mob && req->shareable) {
		uint32_t backup_handle;

		ret = vmw_user_bo_alloc(dev_priv, tfile,
					res->backup_size,
					true,
					&backup_handle,
					&res->backup,
					&user_srf->backup_base);
		if (unlikely(ret != 0)) {
			vmw_resource_unreference(&res);
			goto out_unlock;
		}
	}

	tmp = vmw_resource_reference(&srf->res);
	ret = ttm_prime_object_init(tfile, res->backup_size, &user_srf->prime,
				    req->shareable, VMW_RES_SURFACE,
				    &vmw_user_surface_base_release, NULL);

	if (unlikely(ret != 0)) {
		vmw_resource_unreference(&tmp);
		vmw_resource_unreference(&res);
		goto out_unlock;
	}

	rep->sid = user_srf->prime.base.hash.key;
	vmw_resource_unreference(&res);

	ttm_read_unlock(&dev_priv->reservation_sem);
	return 0;
out_no_copy:
	kfree(srf->offsets);
out_no_offsets:
	kfree(srf->sizes);
out_no_sizes:
	ttm_prime_object_kfree(user_srf, prime);
out_no_user_srf:
	ttm_mem_global_free(vmw_mem_glob(dev_priv), size);
out_unlock:
	ttm_read_unlock(&dev_priv->reservation_sem);
	return ret;
}


static int
vmw_surface_handle_reference(struct vmw_private *dev_priv,
			     struct drm_file *file_priv,
			     uint32_t u_handle,
			     enum drm_vmw_handle_type handle_type,
			     struct ttm_base_object **base_p)
{
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct vmw_user_surface *user_srf;
	uint32_t handle;
	struct ttm_base_object *base;
	int ret;
	bool require_exist = false;

	if (handle_type == DRM_VMW_HANDLE_PRIME) {
		ret = ttm_prime_fd_to_handle(tfile, u_handle, &handle);
		if (unlikely(ret != 0))
			return ret;
	} else {
		if (unlikely(drm_is_render_client(file_priv)))
			require_exist = true;

		if (READ_ONCE(vmw_fpriv(file_priv)->locked_master)) {
			DRM_ERROR("Locked master refused legacy "
				  "surface reference.\n");
			return -EACCES;
		}

		handle = u_handle;
	}

	ret = -EINVAL;
	base = ttm_base_object_lookup_for_ref(dev_priv->tdev, handle);
	if (unlikely(!base)) {
		DRM_ERROR("Could not find surface to reference.\n");
		goto out_no_lookup;
	}

	if (unlikely(ttm_base_object_type(base) != VMW_RES_SURFACE)) {
		DRM_ERROR("Referenced object is not a surface.\n");
		goto out_bad_resource;
	}

	if (handle_type != DRM_VMW_HANDLE_PRIME) {
		user_srf = container_of(base, struct vmw_user_surface,
					prime.base);

		/*
		 * Make sure the surface creator has the same
		 * authenticating master, or is already registered with us.
		 */
		if (drm_is_primary_client(file_priv) &&
		    user_srf->master != file_priv->master)
			require_exist = true;

		ret = ttm_ref_object_add(tfile, base, TTM_REF_USAGE, NULL,
					 require_exist);
		if (unlikely(ret != 0)) {
			DRM_ERROR("Could not add a reference to a surface.\n");
			goto out_bad_resource;
		}
	}

	*base_p = base;
	return 0;

out_bad_resource:
	ttm_base_object_unref(&base);
out_no_lookup:
	if (handle_type == DRM_VMW_HANDLE_PRIME)
		(void) ttm_ref_object_base_unref(tfile, handle, TTM_REF_USAGE);

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
	struct vmw_private *dev_priv = vmw_priv(dev);
	union drm_vmw_surface_reference_arg *arg =
	    (union drm_vmw_surface_reference_arg *)data;
	struct drm_vmw_surface_arg *req = &arg->req;
	struct drm_vmw_surface_create_req *rep = &arg->rep;
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct vmw_surface *srf;
	struct vmw_user_surface *user_srf;
	struct drm_vmw_size __user *user_sizes;
	struct ttm_base_object *base;
	int ret;

	ret = vmw_surface_handle_reference(dev_priv, file_priv, req->sid,
					   req->handle_type, &base);
	if (unlikely(ret != 0))
		return ret;

	user_srf = container_of(base, struct vmw_user_surface, prime.base);
	srf = &user_srf->srf;

	/* Downcast of flags when sending back to user space */
	rep->flags = (uint32_t)srf->flags;
	rep->format = srf->format;
	memcpy(rep->mip_levels, srf->mip_levels, sizeof(srf->mip_levels));
	user_sizes = (struct drm_vmw_size __user *)(unsigned long)
	    rep->size_addr;

	if (user_sizes)
		ret = copy_to_user(user_sizes, &srf->base_size,
				   sizeof(srf->base_size));
	if (unlikely(ret != 0)) {
		DRM_ERROR("copy_to_user failed %p %u\n",
			  user_sizes, srf->num_sizes);
		ttm_ref_object_base_unref(tfile, base->hash.key, TTM_REF_USAGE);
		ret = -EFAULT;
	}

	ttm_base_object_unref(&base);

	return ret;
}

/**
 * vmw_surface_define_encode - Encode a surface_define command.
 *
 * @srf: Pointer to a struct vmw_surface object.
 * @cmd_space: Pointer to memory area in which the commands should be encoded.
 */
static int vmw_gb_surface_create(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct vmw_surface *srf = vmw_res_to_srf(res);
	uint32_t cmd_len, cmd_id, submit_len;
	int ret;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDefineGBSurface body;
	} *cmd;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDefineGBSurface_v2 body;
	} *cmd2;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDefineGBSurface_v3 body;
	} *cmd3;

	if (likely(res->id != -1))
		return 0;

	vmw_fifo_resource_inc(dev_priv);
	ret = vmw_resource_alloc_id(res);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed to allocate a surface id.\n");
		goto out_no_id;
	}

	if (unlikely(res->id >= VMWGFX_NUM_GB_SURFACE)) {
		ret = -EBUSY;
		goto out_no_fifo;
	}

	if (dev_priv->has_sm4_1 && srf->array_size > 0) {
		cmd_id = SVGA_3D_CMD_DEFINE_GB_SURFACE_V3;
		cmd_len = sizeof(cmd3->body);
		submit_len = sizeof(*cmd3);
	} else if (srf->array_size > 0) {
		/* has_dx checked on creation time. */
		cmd_id = SVGA_3D_CMD_DEFINE_GB_SURFACE_V2;
		cmd_len = sizeof(cmd2->body);
		submit_len = sizeof(*cmd2);
	} else {
		cmd_id = SVGA_3D_CMD_DEFINE_GB_SURFACE;
		cmd_len = sizeof(cmd->body);
		submit_len = sizeof(*cmd);
	}

	cmd = vmw_fifo_reserve(dev_priv, submit_len);
	cmd2 = (typeof(cmd2))cmd;
	cmd3 = (typeof(cmd3))cmd;
	if (unlikely(!cmd)) {
		DRM_ERROR("Failed reserving FIFO space for surface "
			  "creation.\n");
		ret = -ENOMEM;
		goto out_no_fifo;
	}

	if (dev_priv->has_sm4_1 && srf->array_size > 0) {
		cmd3->header.id = cmd_id;
		cmd3->header.size = cmd_len;
		cmd3->body.sid = srf->res.id;
		cmd3->body.surfaceFlags = srf->flags;
		cmd3->body.format = srf->format;
		cmd3->body.numMipLevels = srf->mip_levels[0];
		cmd3->body.multisampleCount = srf->multisample_count;
		cmd3->body.multisamplePattern = srf->multisample_pattern;
		cmd3->body.qualityLevel = srf->quality_level;
		cmd3->body.autogenFilter = srf->autogen_filter;
		cmd3->body.size.width = srf->base_size.width;
		cmd3->body.size.height = srf->base_size.height;
		cmd3->body.size.depth = srf->base_size.depth;
		cmd3->body.arraySize = srf->array_size;
	} else if (srf->array_size > 0) {
		cmd2->header.id = cmd_id;
		cmd2->header.size = cmd_len;
		cmd2->body.sid = srf->res.id;
		cmd2->body.surfaceFlags = srf->flags;
		cmd2->body.format = srf->format;
		cmd2->body.numMipLevels = srf->mip_levels[0];
		cmd2->body.multisampleCount = srf->multisample_count;
		cmd2->body.autogenFilter = srf->autogen_filter;
		cmd2->body.size.width = srf->base_size.width;
		cmd2->body.size.height = srf->base_size.height;
		cmd2->body.size.depth = srf->base_size.depth;
		cmd2->body.arraySize = srf->array_size;
	} else {
		cmd->header.id = cmd_id;
		cmd->header.size = cmd_len;
		cmd->body.sid = srf->res.id;
		cmd->body.surfaceFlags = srf->flags;
		cmd->body.format = srf->format;
		cmd->body.numMipLevels = srf->mip_levels[0];
		cmd->body.multisampleCount = srf->multisample_count;
		cmd->body.autogenFilter = srf->autogen_filter;
		cmd->body.size.width = srf->base_size.width;
		cmd->body.size.height = srf->base_size.height;
		cmd->body.size.depth = srf->base_size.depth;
	}

	vmw_fifo_commit(dev_priv, submit_len);

	return 0;

out_no_fifo:
	vmw_resource_release_id(res);
out_no_id:
	vmw_fifo_resource_dec(dev_priv);
	return ret;
}


static int vmw_gb_surface_bind(struct vmw_resource *res,
			       struct ttm_validate_buffer *val_buf)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdBindGBSurface body;
	} *cmd1;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdUpdateGBSurface body;
	} *cmd2;
	uint32_t submit_size;
	struct ttm_buffer_object *bo = val_buf->bo;

	BUG_ON(bo->mem.mem_type != VMW_PL_MOB);

	submit_size = sizeof(*cmd1) + (res->backup_dirty ? sizeof(*cmd2) : 0);

	cmd1 = vmw_fifo_reserve(dev_priv, submit_size);
	if (unlikely(!cmd1)) {
		DRM_ERROR("Failed reserving FIFO space for surface "
			  "binding.\n");
		return -ENOMEM;
	}

	cmd1->header.id = SVGA_3D_CMD_BIND_GB_SURFACE;
	cmd1->header.size = sizeof(cmd1->body);
	cmd1->body.sid = res->id;
	cmd1->body.mobid = bo->mem.start;
	if (res->backup_dirty) {
		cmd2 = (void *) &cmd1[1];
		cmd2->header.id = SVGA_3D_CMD_UPDATE_GB_SURFACE;
		cmd2->header.size = sizeof(cmd2->body);
		cmd2->body.sid = res->id;
		res->backup_dirty = false;
	}
	vmw_fifo_commit(dev_priv, submit_size);

	return 0;
}

static int vmw_gb_surface_unbind(struct vmw_resource *res,
				 bool readback,
				 struct ttm_validate_buffer *val_buf)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct ttm_buffer_object *bo = val_buf->bo;
	struct vmw_fence_obj *fence;

	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdReadbackGBSurface body;
	} *cmd1;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdInvalidateGBSurface body;
	} *cmd2;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdBindGBSurface body;
	} *cmd3;
	uint32_t submit_size;
	uint8_t *cmd;


	BUG_ON(bo->mem.mem_type != VMW_PL_MOB);

	submit_size = sizeof(*cmd3) + (readback ? sizeof(*cmd1) : sizeof(*cmd2));
	cmd = vmw_fifo_reserve(dev_priv, submit_size);
	if (unlikely(!cmd)) {
		DRM_ERROR("Failed reserving FIFO space for surface "
			  "unbinding.\n");
		return -ENOMEM;
	}

	if (readback) {
		cmd1 = (void *) cmd;
		cmd1->header.id = SVGA_3D_CMD_READBACK_GB_SURFACE;
		cmd1->header.size = sizeof(cmd1->body);
		cmd1->body.sid = res->id;
		cmd3 = (void *) &cmd1[1];
	} else {
		cmd2 = (void *) cmd;
		cmd2->header.id = SVGA_3D_CMD_INVALIDATE_GB_SURFACE;
		cmd2->header.size = sizeof(cmd2->body);
		cmd2->body.sid = res->id;
		cmd3 = (void *) &cmd2[1];
	}

	cmd3->header.id = SVGA_3D_CMD_BIND_GB_SURFACE;
	cmd3->header.size = sizeof(cmd3->body);
	cmd3->body.sid = res->id;
	cmd3->body.mobid = SVGA3D_INVALID_ID;

	vmw_fifo_commit(dev_priv, submit_size);

	/*
	 * Create a fence object and fence the backup buffer.
	 */

	(void) vmw_execbuf_fence_commands(NULL, dev_priv,
					  &fence, NULL);

	vmw_bo_fence_single(val_buf->bo, fence);

	if (likely(fence != NULL))
		vmw_fence_obj_unreference(&fence);

	return 0;
}

static int vmw_gb_surface_destroy(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct vmw_surface *srf = vmw_res_to_srf(res);
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDestroyGBSurface body;
	} *cmd;

	if (likely(res->id == -1))
		return 0;

	mutex_lock(&dev_priv->binding_mutex);
	vmw_view_surface_list_destroy(dev_priv, &srf->view_list);
	vmw_binding_res_list_scrub(&res->binding_head);

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));
	if (unlikely(!cmd)) {
		DRM_ERROR("Failed reserving FIFO space for surface "
			  "destruction.\n");
		mutex_unlock(&dev_priv->binding_mutex);
		return -ENOMEM;
	}

	cmd->header.id = SVGA_3D_CMD_DESTROY_GB_SURFACE;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.sid = res->id;
	vmw_fifo_commit(dev_priv, sizeof(*cmd));
	mutex_unlock(&dev_priv->binding_mutex);
	vmw_resource_release_id(res);
	vmw_fifo_resource_dec(dev_priv);

	return 0;
}


/**
 * vmw_gb_surface_define_ioctl - Ioctl function implementing
 * the user surface define functionality.
 *
 * @dev: Pointer to a struct drm_device.
 * @data: Pointer to data copied from / to user-space.
 * @file_priv: Pointer to a drm file private structure.
 */
int vmw_gb_surface_define_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	union drm_vmw_gb_surface_create_arg *arg =
	    (union drm_vmw_gb_surface_create_arg *)data;
	struct drm_vmw_gb_surface_create_rep *rep = &arg->rep;
	struct drm_vmw_gb_surface_create_ext_req req_ext;

	req_ext.base = arg->req;
	req_ext.version = drm_vmw_gb_surface_v1;
	req_ext.svga3d_flags_upper_32_bits = 0;
	req_ext.multisample_pattern = SVGA3D_MS_PATTERN_NONE;
	req_ext.quality_level = SVGA3D_MS_QUALITY_NONE;
	req_ext.must_be_zero = 0;

	return vmw_gb_surface_define_internal(dev, &req_ext, rep, file_priv);
}

/**
 * vmw_gb_surface_reference_ioctl - Ioctl function implementing
 * the user surface reference functionality.
 *
 * @dev: Pointer to a struct drm_device.
 * @data: Pointer to data copied from / to user-space.
 * @file_priv: Pointer to a drm file private structure.
 */
int vmw_gb_surface_reference_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file_priv)
{
	union drm_vmw_gb_surface_reference_arg *arg =
	    (union drm_vmw_gb_surface_reference_arg *)data;
	struct drm_vmw_surface_arg *req = &arg->req;
	struct drm_vmw_gb_surface_ref_rep *rep = &arg->rep;
	struct drm_vmw_gb_surface_ref_ext_rep rep_ext;
	int ret;

	ret = vmw_gb_surface_reference_internal(dev, req, &rep_ext, file_priv);

	if (unlikely(ret != 0))
		return ret;

	rep->creq = rep_ext.creq.base;
	rep->crep = rep_ext.crep;

	return ret;
}

/**
 * vmw_surface_gb_priv_define - Define a private GB surface
 *
 * @dev:  Pointer to a struct drm_device
 * @user_accounting_size:  Used to track user-space memory usage, set
 *                         to 0 for kernel mode only memory
 * @svga3d_flags: SVGA3d surface flags for the device
 * @format: requested surface format
 * @for_scanout: true if inteded to be used for scanout buffer
 * @num_mip_levels:  number of MIP levels
 * @multisample_count:
 * @array_size: Surface array size.
 * @size: width, heigh, depth of the surface requested
 * @multisample_pattern: Multisampling pattern when msaa is supported
 * @quality_level: Precision settings
 * @user_srf_out: allocated user_srf.  Set to NULL on failure.
 *
 * GB surfaces allocated by this function will not have a user mode handle, and
 * thus will only be visible to vmwgfx.  For optimization reasons the
 * surface may later be given a user mode handle by another function to make
 * it available to user mode drivers.
 */
int vmw_surface_gb_priv_define(struct drm_device *dev,
			       uint32_t user_accounting_size,
			       SVGA3dSurfaceAllFlags svga3d_flags,
			       SVGA3dSurfaceFormat format,
			       bool for_scanout,
			       uint32_t num_mip_levels,
			       uint32_t multisample_count,
			       uint32_t array_size,
			       struct drm_vmw_size size,
			       SVGA3dMSPattern multisample_pattern,
			       SVGA3dMSQualityLevel quality_level,
			       struct vmw_surface **srf_out)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct vmw_user_surface *user_srf;
	struct ttm_operation_ctx ctx = {
		.interruptible = true,
		.no_wait_gpu = false
	};
	struct vmw_surface *srf;
	int ret;
	u32 num_layers = 1;
	u32 sample_count = 1;

	*srf_out = NULL;

	if (for_scanout) {
		if (!svga3dsurface_is_screen_target_format(format)) {
			DRM_ERROR("Invalid Screen Target surface format.");
			return -EINVAL;
		}

		if (size.width > dev_priv->texture_max_width ||
		    size.height > dev_priv->texture_max_height) {
			DRM_ERROR("%ux%u\n, exceeds max surface size %ux%u",
				  size.width, size.height,
				  dev_priv->texture_max_width,
				  dev_priv->texture_max_height);
			return -EINVAL;
		}
	} else {
		const struct svga3d_surface_desc *desc;

		desc = svga3dsurface_get_desc(format);
		if (unlikely(desc->block_desc == SVGA3DBLOCKDESC_NONE)) {
			DRM_ERROR("Invalid surface format.\n");
			return -EINVAL;
		}
	}

	/* array_size must be null for non-GL3 host. */
	if (array_size > 0 && !dev_priv->has_dx) {
		DRM_ERROR("Tried to create DX surface on non-DX host.\n");
		return -EINVAL;
	}

	ret = ttm_read_lock(&dev_priv->reservation_sem, true);
	if (unlikely(ret != 0))
		return ret;

	ret = ttm_mem_global_alloc(vmw_mem_glob(dev_priv),
				   user_accounting_size, &ctx);
	if (unlikely(ret != 0)) {
		if (ret != -ERESTARTSYS)
			DRM_ERROR("Out of graphics memory for surface"
				  " creation.\n");
		goto out_unlock;
	}

	user_srf = kzalloc(sizeof(*user_srf), GFP_KERNEL);
	if (unlikely(!user_srf)) {
		ret = -ENOMEM;
		goto out_no_user_srf;
	}

	*srf_out  = &user_srf->srf;
	user_srf->size = user_accounting_size;
	user_srf->prime.base.shareable = false;
	user_srf->prime.base.tfile     = NULL;

	srf = &user_srf->srf;
	srf->flags             = svga3d_flags;
	srf->format            = format;
	srf->scanout           = for_scanout;
	srf->mip_levels[0]     = num_mip_levels;
	srf->num_sizes         = 1;
	srf->sizes             = NULL;
	srf->offsets           = NULL;
	srf->base_size         = size;
	srf->autogen_filter    = SVGA3D_TEX_FILTER_NONE;
	srf->array_size        = array_size;
	srf->multisample_count = multisample_count;
	srf->multisample_pattern = multisample_pattern;
	srf->quality_level = quality_level;

	if (array_size)
		num_layers = array_size;
	else if (svga3d_flags & SVGA3D_SURFACE_CUBEMAP)
		num_layers = SVGA3D_MAX_SURFACE_FACES;

	if (srf->flags & SVGA3D_SURFACE_MULTISAMPLE)
		sample_count = srf->multisample_count;

	srf->res.backup_size   =
		svga3dsurface_get_serialized_size_extended(srf->format,
							   srf->base_size,
							   srf->mip_levels[0],
							   num_layers,
							   sample_count);

	if (srf->flags & SVGA3D_SURFACE_BIND_STREAM_OUTPUT)
		srf->res.backup_size += sizeof(SVGA3dDXSOState);

	/*
	 * Don't set SVGA3D_SURFACE_SCREENTARGET flag for a scanout surface with
	 * size greater than STDU max width/height. This is really a workaround
	 * to support creation of big framebuffer requested by some user-space
	 * for whole topology. That big framebuffer won't really be used for
	 * binding with screen target as during prepare_fb a separate surface is
	 * created so it's safe to ignore SVGA3D_SURFACE_SCREENTARGET flag.
	 */
	if (dev_priv->active_display_unit == vmw_du_screen_target &&
	    for_scanout && size.width <= dev_priv->stdu_max_width &&
	    size.height <= dev_priv->stdu_max_height)
		srf->flags |= SVGA3D_SURFACE_SCREENTARGET;

	/*
	 * From this point, the generic resource management functions
	 * destroy the object on failure.
	 */
	ret = vmw_surface_init(dev_priv, srf, vmw_user_surface_free);

	ttm_read_unlock(&dev_priv->reservation_sem);
	return ret;

out_no_user_srf:
	ttm_mem_global_free(vmw_mem_glob(dev_priv), user_accounting_size);

out_unlock:
	ttm_read_unlock(&dev_priv->reservation_sem);
	return ret;
}

/**
 * vmw_gb_surface_define_ext_ioctl - Ioctl function implementing
 * the user surface define functionality.
 *
 * @dev: Pointer to a struct drm_device.
 * @data: Pointer to data copied from / to user-space.
 * @file_priv: Pointer to a drm file private structure.
 */
int vmw_gb_surface_define_ext_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	union drm_vmw_gb_surface_create_ext_arg *arg =
	    (union drm_vmw_gb_surface_create_ext_arg *)data;
	struct drm_vmw_gb_surface_create_ext_req *req = &arg->req;
	struct drm_vmw_gb_surface_create_rep *rep = &arg->rep;

	return vmw_gb_surface_define_internal(dev, req, rep, file_priv);
}

/**
 * vmw_gb_surface_reference_ext_ioctl - Ioctl function implementing
 * the user surface reference functionality.
 *
 * @dev: Pointer to a struct drm_device.
 * @data: Pointer to data copied from / to user-space.
 * @file_priv: Pointer to a drm file private structure.
 */
int vmw_gb_surface_reference_ext_ioctl(struct drm_device *dev, void *data,
				   struct drm_file *file_priv)
{
	union drm_vmw_gb_surface_reference_ext_arg *arg =
	    (union drm_vmw_gb_surface_reference_ext_arg *)data;
	struct drm_vmw_surface_arg *req = &arg->req;
	struct drm_vmw_gb_surface_ref_ext_rep *rep = &arg->rep;

	return vmw_gb_surface_reference_internal(dev, req, rep, file_priv);
}

/**
 * vmw_gb_surface_define_internal - Ioctl function implementing
 * the user surface define functionality.
 *
 * @dev: Pointer to a struct drm_device.
 * @req: Request argument from user-space.
 * @rep: Response argument to user-space.
 * @file_priv: Pointer to a drm file private structure.
 */
static int
vmw_gb_surface_define_internal(struct drm_device *dev,
			       struct drm_vmw_gb_surface_create_ext_req *req,
			       struct drm_vmw_gb_surface_create_rep *rep,
			       struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct vmw_user_surface *user_srf;
	struct vmw_surface *srf;
	struct vmw_resource *res;
	struct vmw_resource *tmp;
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	int ret;
	uint32_t size;
	uint32_t backup_handle = 0;
	SVGA3dSurfaceAllFlags svga3d_flags_64 =
		SVGA3D_FLAGS_64(req->svga3d_flags_upper_32_bits,
				req->base.svga3d_flags);

	if (!dev_priv->has_sm4_1) {
		/*
		 * If SM4_1 is not support then cannot send 64-bit flag to
		 * device.
		 */
		if (req->svga3d_flags_upper_32_bits != 0)
			return -EINVAL;

		if (req->base.multisample_count != 0)
			return -EINVAL;

		if (req->multisample_pattern != SVGA3D_MS_PATTERN_NONE)
			return -EINVAL;

		if (req->quality_level != SVGA3D_MS_QUALITY_NONE)
			return -EINVAL;
	}

	if ((svga3d_flags_64 & SVGA3D_SURFACE_MULTISAMPLE) &&
	    req->base.multisample_count == 0)
		return -EINVAL;

	if (req->base.mip_levels > DRM_VMW_MAX_MIP_LEVELS)
		return -EINVAL;

	if (unlikely(vmw_user_surface_size == 0))
		vmw_user_surface_size = ttm_round_pot(sizeof(*user_srf)) +
			128;

	size = vmw_user_surface_size + 128;

	/* Define a surface based on the parameters. */
	ret = vmw_surface_gb_priv_define(dev,
					 size,
					 svga3d_flags_64,
					 req->base.format,
					 req->base.drm_surface_flags &
					 drm_vmw_surface_flag_scanout,
					 req->base.mip_levels,
					 req->base.multisample_count,
					 req->base.array_size,
					 req->base.base_size,
					 req->multisample_pattern,
					 req->quality_level,
					 &srf);
	if (unlikely(ret != 0))
		return ret;

	user_srf = container_of(srf, struct vmw_user_surface, srf);
	if (drm_is_primary_client(file_priv))
		user_srf->master = drm_master_get(file_priv->master);

	ret = ttm_read_lock(&dev_priv->reservation_sem, true);
	if (unlikely(ret != 0))
		return ret;

	res = &user_srf->srf.res;

	if (req->base.buffer_handle != SVGA3D_INVALID_ID) {
		ret = vmw_user_bo_lookup(tfile, req->base.buffer_handle,
					 &res->backup,
					 &user_srf->backup_base);
		if (ret == 0) {
			if (res->backup->base.num_pages * PAGE_SIZE <
			    res->backup_size) {
				DRM_ERROR("Surface backup buffer too small.\n");
				vmw_bo_unreference(&res->backup);
				ret = -EINVAL;
				goto out_unlock;
			} else {
				backup_handle = req->base.buffer_handle;
			}
		}
	} else if (req->base.drm_surface_flags &
		   drm_vmw_surface_flag_create_buffer)
		ret = vmw_user_bo_alloc(dev_priv, tfile,
					res->backup_size,
					req->base.drm_surface_flags &
					drm_vmw_surface_flag_shareable,
					&backup_handle,
					&res->backup,
					&user_srf->backup_base);

	if (unlikely(ret != 0)) {
		vmw_resource_unreference(&res);
		goto out_unlock;
	}

	tmp = vmw_resource_reference(res);
	ret = ttm_prime_object_init(tfile, res->backup_size, &user_srf->prime,
				    req->base.drm_surface_flags &
				    drm_vmw_surface_flag_shareable,
				    VMW_RES_SURFACE,
				    &vmw_user_surface_base_release, NULL);

	if (unlikely(ret != 0)) {
		vmw_resource_unreference(&tmp);
		vmw_resource_unreference(&res);
		goto out_unlock;
	}

	rep->handle      = user_srf->prime.base.hash.key;
	rep->backup_size = res->backup_size;
	if (res->backup) {
		rep->buffer_map_handle =
			drm_vma_node_offset_addr(&res->backup->base.vma_node);
		rep->buffer_size = res->backup->base.num_pages * PAGE_SIZE;
		rep->buffer_handle = backup_handle;
	} else {
		rep->buffer_map_handle = 0;
		rep->buffer_size = 0;
		rep->buffer_handle = SVGA3D_INVALID_ID;
	}

	vmw_resource_unreference(&res);

out_unlock:
	ttm_read_unlock(&dev_priv->reservation_sem);
	return ret;
}

/**
 * vmw_gb_surface_reference_internal - Ioctl function implementing
 * the user surface reference functionality.
 *
 * @dev: Pointer to a struct drm_device.
 * @req: Pointer to user-space request surface arg.
 * @rep: Pointer to response to user-space.
 * @file_priv: Pointer to a drm file private structure.
 */
static int
vmw_gb_surface_reference_internal(struct drm_device *dev,
				  struct drm_vmw_surface_arg *req,
				  struct drm_vmw_gb_surface_ref_ext_rep *rep,
				  struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct vmw_surface *srf;
	struct vmw_user_surface *user_srf;
	struct ttm_base_object *base;
	uint32_t backup_handle;
	int ret = -EINVAL;

	ret = vmw_surface_handle_reference(dev_priv, file_priv, req->sid,
					   req->handle_type, &base);
	if (unlikely(ret != 0))
		return ret;

	user_srf = container_of(base, struct vmw_user_surface, prime.base);
	srf = &user_srf->srf;
	if (!srf->res.backup) {
		DRM_ERROR("Shared GB surface is missing a backup buffer.\n");
		goto out_bad_resource;
	}

	mutex_lock(&dev_priv->cmdbuf_mutex); /* Protect res->backup */
	ret = vmw_user_bo_reference(tfile, srf->res.backup, &backup_handle);
	mutex_unlock(&dev_priv->cmdbuf_mutex);

	if (unlikely(ret != 0)) {
		DRM_ERROR("Could not add a reference to a GB surface "
			  "backup buffer.\n");
		(void) ttm_ref_object_base_unref(tfile, base->hash.key,
						 TTM_REF_USAGE);
		goto out_bad_resource;
	}

	rep->creq.base.svga3d_flags = SVGA3D_FLAGS_LOWER_32(srf->flags);
	rep->creq.base.format = srf->format;
	rep->creq.base.mip_levels = srf->mip_levels[0];
	rep->creq.base.drm_surface_flags = 0;
	rep->creq.base.multisample_count = srf->multisample_count;
	rep->creq.base.autogen_filter = srf->autogen_filter;
	rep->creq.base.array_size = srf->array_size;
	rep->creq.base.buffer_handle = backup_handle;
	rep->creq.base.base_size = srf->base_size;
	rep->crep.handle = user_srf->prime.base.hash.key;
	rep->crep.backup_size = srf->res.backup_size;
	rep->crep.buffer_handle = backup_handle;
	rep->crep.buffer_map_handle =
		drm_vma_node_offset_addr(&srf->res.backup->base.vma_node);
	rep->crep.buffer_size = srf->res.backup->base.num_pages * PAGE_SIZE;

	rep->creq.version = drm_vmw_gb_surface_v1;
	rep->creq.svga3d_flags_upper_32_bits =
		SVGA3D_FLAGS_UPPER_32(srf->flags);
	rep->creq.multisample_pattern = srf->multisample_pattern;
	rep->creq.quality_level = srf->quality_level;
	rep->creq.must_be_zero = 0;

out_bad_resource:
	ttm_base_object_unref(&base);

	return ret;
}
