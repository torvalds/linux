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
#include "vmw_surface_cache.h"
#include "device_include/svga3d_surfacedefs.h"

#define SVGA3D_FLAGS_64(upper32, lower32) (((uint64_t)upper32 << 32) | lower32)
#define SVGA3D_FLAGS_UPPER_32(svga3d_flags) (svga3d_flags >> 32)
#define SVGA3D_FLAGS_LOWER_32(svga3d_flags) \
	(svga3d_flags & ((uint64_t)U32_MAX))

/**
 * struct vmw_user_surface - User-space visible surface resource
 *
 * @prime:          The TTM prime object.
 * @base:           The TTM base object handling user-space visibility.
 * @srf:            The surface metadata.
 * @master:         Master of the creating client. Used for security check.
 */
struct vmw_user_surface {
	struct ttm_prime_object prime;
	struct vmw_surface srf;
	struct drm_master *master;
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

/**
 * struct vmw_surface_dirty - Surface dirty-tracker
 * @cache: Cached layout information of the surface.
 * @num_subres: Number of subresources.
 * @boxes: Array of SVGA3dBoxes indicating dirty regions. One per subresource.
 */
struct vmw_surface_dirty {
	struct vmw_surface_cache cache;
	u32 num_subres;
	SVGA3dBox boxes[];
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

static void vmw_surface_dirty_free(struct vmw_resource *res);
static int vmw_surface_dirty_alloc(struct vmw_resource *res);
static int vmw_surface_dirty_sync(struct vmw_resource *res);
static void vmw_surface_dirty_range_add(struct vmw_resource *res, size_t start,
					size_t end);
static int vmw_surface_clean(struct vmw_resource *res);

static const struct vmw_user_resource_conv user_surface_conv = {
	.object_type = VMW_RES_SURFACE,
	.base_obj_to_res = vmw_user_surface_base_to_res,
	.res_free = vmw_user_surface_free
};

const struct vmw_user_resource_conv *user_surface_converter =
	&user_surface_conv;

static const struct vmw_res_func vmw_legacy_surface_func = {
	.res_type = vmw_res_surface,
	.needs_backup = false,
	.may_evict = true,
	.prio = 1,
	.dirty_prio = 1,
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
	.prio = 1,
	.dirty_prio = 2,
	.type_name = "guest backed surfaces",
	.backup_placement = &vmw_mob_placement,
	.create = vmw_gb_surface_create,
	.destroy = vmw_gb_surface_destroy,
	.bind = vmw_gb_surface_bind,
	.unbind = vmw_gb_surface_unbind,
	.dirty_alloc = vmw_surface_dirty_alloc,
	.dirty_free = vmw_surface_dirty_free,
	.dirty_sync = vmw_surface_dirty_sync,
	.dirty_range_add = vmw_surface_dirty_range_add,
	.clean = vmw_surface_clean,
};

/*
 * struct vmw_surface_dma - SVGA3D DMA command
 */
struct vmw_surface_dma {
	SVGA3dCmdHeader header;
	SVGA3dCmdSurfaceDMA body;
	SVGA3dCopyBox cb;
	SVGA3dCmdSurfaceDMASuffix suffix;
};

/*
 * struct vmw_surface_define - SVGA3D Surface Define command
 */
struct vmw_surface_define {
	SVGA3dCmdHeader header;
	SVGA3dCmdDefineSurface body;
};

/*
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
	return srf->metadata.num_sizes * sizeof(struct vmw_surface_dma);
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
	return sizeof(struct vmw_surface_define) + srf->metadata.num_sizes *
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

	cmd_len = sizeof(cmd->body) + srf->metadata.num_sizes *
		sizeof(SVGA3dSize);

	cmd->header.id = SVGA_3D_CMD_SURFACE_DEFINE;
	cmd->header.size = cmd_len;
	cmd->body.sid = srf->res.id;
	/*
	 * Downcast of surfaceFlags, was upcasted when received from user-space,
	 * since driver internally stores as 64 bit.
	 * For legacy surface define only 32 bit flag is supported.
	 */
	cmd->body.surfaceFlags = (SVGA3dSurface1Flags)srf->metadata.flags;
	cmd->body.format = srf->metadata.format;
	for (i = 0; i < DRM_VMW_MAX_SURFACE_FACES; ++i)
		cmd->body.face[i].numMipLevels = srf->metadata.mip_levels[i];

	cmd += 1;
	cmd_size = (SVGA3dSize *) cmd;
	src_size = srf->metadata.sizes;

	for (i = 0; i < srf->metadata.num_sizes; ++i, cmd_size++, src_size++) {
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
	const struct SVGA3dSurfaceDesc *desc =
		vmw_surface_get_desc(srf->metadata.format);

	for (i = 0; i < srf->metadata.num_sizes; ++i) {
		SVGA3dCmdHeader *header = &cmd->header;
		SVGA3dCmdSurfaceDMA *body = &cmd->body;
		SVGA3dCopyBox *cb = &cmd->cb;
		SVGA3dCmdSurfaceDMASuffix *suffix = &cmd->suffix;
		const struct vmw_surface_offset *cur_offset = &srf->offsets[i];
		const struct drm_vmw_size *cur_size = &srf->metadata.sizes[i];

		header->id = SVGA_3D_CMD_SURFACE_DMA;
		header->size = sizeof(*body) + sizeof(*cb) + sizeof(*suffix);

		body->guest.ptr = *ptr;
		body->guest.ptr.offset += cur_offset->bo_offset;
		body->guest.pitch = vmw_surface_calculate_pitch(desc, cur_size);
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
			vmw_surface_get_image_buffer_size(desc, cur_size,
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
 * any, and adjusts resource count accordingly.
 */
static void vmw_hw_surface_destroy(struct vmw_resource *res)
{

	struct vmw_private *dev_priv = res->dev_priv;
	void *cmd;

	if (res->func->destroy == vmw_gb_surface_destroy) {
		(void) vmw_gb_surface_destroy(res);
		return;
	}

	if (res->id != -1) {

		cmd = VMW_CMD_RESERVE(dev_priv, vmw_surface_destroy_size());
		if (unlikely(!cmd))
			return;

		vmw_surface_destroy_encode(res->id, cmd);
		vmw_cmd_commit(dev_priv, vmw_surface_destroy_size());

		/*
		 * used_memory_size_atomic, or separate lock
		 * to avoid taking dev_priv::cmdbuf_mutex in
		 * the destroy path.
		 */

		mutex_lock(&dev_priv->cmdbuf_mutex);
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

	if (unlikely(res->id >= SVGA3D_HB_MAX_SURFACE_IDS)) {
		ret = -EBUSY;
		goto out_no_fifo;
	}

	/*
	 * Encode surface define- commands.
	 */

	submit_size = vmw_surface_define_size(srf);
	cmd = VMW_CMD_RESERVE(dev_priv, submit_size);
	if (unlikely(!cmd)) {
		ret = -ENOMEM;
		goto out_no_fifo;
	}

	vmw_surface_define_encode(srf, cmd);
	vmw_cmd_commit(dev_priv, submit_size);
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
	cmd = VMW_CMD_RESERVE(dev_priv, submit_size);
	if (unlikely(!cmd))
		return -ENOMEM;

	vmw_bo_get_guest_ptr(val_buf->bo, &ptr);
	vmw_surface_dma_encode(srf, cmd, &ptr, bind);

	vmw_cmd_commit(dev_priv, submit_size);

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
 * @readback:       Readback - only true if dirty
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
	cmd = VMW_CMD_RESERVE(dev_priv, submit_size);
	if (unlikely(!cmd))
		return -ENOMEM;

	vmw_surface_destroy_encode(res->id, cmd);
	vmw_cmd_commit(dev_priv, submit_size);

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
	res->hw_destroy = vmw_hw_surface_destroy;
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

	WARN_ON_ONCE(res->dirty);
	if (user_srf->master)
		drm_master_put(&user_srf->master);
	kfree(srf->offsets);
	kfree(srf->metadata.sizes);
	kfree(srf->snooper.image);
	ttm_prime_object_kfree(user_srf, prime);
}

/**
 * vmw_user_surface_base_release - User visible surface TTM base object destructor
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

	if (res && res->backup)
		drm_gem_object_put(&res->backup->base.base);

	*p_base = NULL;
	vmw_resource_unreference(&res);
}

/**
 * vmw_surface_destroy_ioctl - Ioctl function implementing
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

	return ttm_ref_object_base_unref(tfile, arg->sid);
}

/**
 * vmw_surface_define_ioctl - Ioctl function implementing
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
	struct vmw_surface_metadata *metadata;
	struct vmw_resource *res;
	struct vmw_resource *tmp;
	union drm_vmw_surface_create_arg *arg =
	    (union drm_vmw_surface_create_arg *)data;
	struct drm_vmw_surface_create_req *req = &arg->req;
	struct drm_vmw_surface_arg *rep = &arg->rep;
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	int ret;
	int i, j;
	uint32_t cur_bo_offset;
	struct drm_vmw_size *cur_size;
	struct vmw_surface_offset *cur_offset;
	uint32_t num_sizes;
	const SVGA3dSurfaceDesc *desc;

	num_sizes = 0;
	for (i = 0; i < DRM_VMW_MAX_SURFACE_FACES; ++i) {
		if (req->mip_levels[i] > DRM_VMW_MAX_MIP_LEVELS)
			return -EINVAL;
		num_sizes += req->mip_levels[i];
	}

	if (num_sizes > DRM_VMW_MAX_SURFACE_FACES * DRM_VMW_MAX_MIP_LEVELS ||
	    num_sizes == 0)
		return -EINVAL;

	desc = vmw_surface_get_desc(req->format);
	if (unlikely(desc->blockDesc == SVGA3DBLOCKDESC_NONE)) {
		VMW_DEBUG_USER("Invalid format %d for surface creation.\n",
			       req->format);
		return -EINVAL;
	}

	user_srf = kzalloc(sizeof(*user_srf), GFP_KERNEL);
	if (unlikely(!user_srf)) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	srf = &user_srf->srf;
	metadata = &srf->metadata;
	res = &srf->res;

	/* Driver internally stores as 64-bit flags */
	metadata->flags = (SVGA3dSurfaceAllFlags)req->flags;
	metadata->format = req->format;
	metadata->scanout = req->scanout;

	memcpy(metadata->mip_levels, req->mip_levels,
	       sizeof(metadata->mip_levels));
	metadata->num_sizes = num_sizes;
	metadata->sizes =
		memdup_user((struct drm_vmw_size __user *)(unsigned long)
			    req->size_addr,
			    sizeof(*metadata->sizes) * metadata->num_sizes);
	if (IS_ERR(metadata->sizes)) {
		ret = PTR_ERR(metadata->sizes);
		goto out_no_sizes;
	}
	srf->offsets = kmalloc_array(metadata->num_sizes, sizeof(*srf->offsets),
				     GFP_KERNEL);
	if (unlikely(!srf->offsets)) {
		ret = -ENOMEM;
		goto out_no_offsets;
	}

	metadata->base_size = *srf->metadata.sizes;
	metadata->autogen_filter = SVGA3D_TEX_FILTER_NONE;
	metadata->multisample_count = 0;
	metadata->multisample_pattern = SVGA3D_MS_PATTERN_NONE;
	metadata->quality_level = SVGA3D_MS_QUALITY_NONE;

	cur_bo_offset = 0;
	cur_offset = srf->offsets;
	cur_size = metadata->sizes;

	for (i = 0; i < DRM_VMW_MAX_SURFACE_FACES; ++i) {
		for (j = 0; j < metadata->mip_levels[i]; ++j) {
			uint32_t stride = vmw_surface_calculate_pitch(
						  desc, cur_size);

			cur_offset->face = i;
			cur_offset->mip = j;
			cur_offset->bo_offset = cur_bo_offset;
			cur_bo_offset += vmw_surface_get_image_buffer_size
				(desc, cur_size, stride);
			++cur_offset;
			++cur_size;
		}
	}
	res->backup_size = cur_bo_offset;
	if (metadata->scanout &&
	    metadata->num_sizes == 1 &&
	    metadata->sizes[0].width == VMW_CURSOR_SNOOP_WIDTH &&
	    metadata->sizes[0].height == VMW_CURSOR_SNOOP_HEIGHT &&
	    metadata->format == VMW_CURSOR_SNOOP_FORMAT) {
		const struct SVGA3dSurfaceDesc *desc =
			vmw_surface_get_desc(VMW_CURSOR_SNOOP_FORMAT);
		const u32 cursor_size_bytes = VMW_CURSOR_SNOOP_WIDTH *
					      VMW_CURSOR_SNOOP_HEIGHT *
					      desc->pitchBytesPerBlock;
		srf->snooper.image = kzalloc(cursor_size_bytes, GFP_KERNEL);
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
		user_srf->master = drm_file_get_master(file_priv);

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

		ret = vmw_gem_object_create_with_handle(dev_priv,
							file_priv,
							res->backup_size,
							&backup_handle,
							&res->backup);
		if (unlikely(ret != 0)) {
			vmw_resource_unreference(&res);
			goto out_unlock;
		}
		vmw_bo_reference(res->backup);
		/*
		 * We don't expose the handle to the userspace and surface
		 * already holds a gem reference
		 */
		drm_gem_handle_delete(file_priv, backup_handle);
	}

	tmp = vmw_resource_reference(&srf->res);
	ret = ttm_prime_object_init(tfile, res->backup_size, &user_srf->prime,
				    req->shareable, VMW_RES_SURFACE,
				    &vmw_user_surface_base_release);

	if (unlikely(ret != 0)) {
		vmw_resource_unreference(&tmp);
		vmw_resource_unreference(&res);
		goto out_unlock;
	}

	rep->sid = user_srf->prime.base.handle;
	vmw_resource_unreference(&res);

	return 0;
out_no_copy:
	kfree(srf->offsets);
out_no_offsets:
	kfree(metadata->sizes);
out_no_sizes:
	ttm_prime_object_kfree(user_srf, prime);
out_unlock:
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

	if (handle_type == DRM_VMW_HANDLE_PRIME) {
		ret = ttm_prime_fd_to_handle(tfile, u_handle, &handle);
		if (unlikely(ret != 0))
			return ret;
	} else {
		handle = u_handle;
	}

	ret = -EINVAL;
	base = ttm_base_object_lookup_for_ref(dev_priv->tdev, handle);
	if (unlikely(!base)) {
		VMW_DEBUG_USER("Could not find surface to reference.\n");
		goto out_no_lookup;
	}

	if (unlikely(ttm_base_object_type(base) != VMW_RES_SURFACE)) {
		VMW_DEBUG_USER("Referenced object is not a surface.\n");
		goto out_bad_resource;
	}
	if (handle_type != DRM_VMW_HANDLE_PRIME) {
		bool require_exist = false;

		user_srf = container_of(base, struct vmw_user_surface,
					prime.base);

		/* Error out if we are unauthenticated primary */
		if (drm_is_primary_client(file_priv) &&
		    !file_priv->authenticated) {
			ret = -EACCES;
			goto out_bad_resource;
		}

		/*
		 * Make sure the surface creator has the same
		 * authenticating master, or is already registered with us.
		 */
		if (drm_is_primary_client(file_priv) &&
		    user_srf->master != file_priv->master)
			require_exist = true;

		if (unlikely(drm_is_render_client(file_priv)))
			require_exist = true;

		ret = ttm_ref_object_add(tfile, base, NULL, require_exist);
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
		(void) ttm_ref_object_base_unref(tfile, handle);

	return ret;
}

/**
 * vmw_surface_reference_ioctl - Ioctl function implementing
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
	rep->flags = (uint32_t)srf->metadata.flags;
	rep->format = srf->metadata.format;
	memcpy(rep->mip_levels, srf->metadata.mip_levels,
	       sizeof(srf->metadata.mip_levels));
	user_sizes = (struct drm_vmw_size __user *)(unsigned long)
	    rep->size_addr;

	if (user_sizes)
		ret = copy_to_user(user_sizes, &srf->metadata.base_size,
				   sizeof(srf->metadata.base_size));
	if (unlikely(ret != 0)) {
		VMW_DEBUG_USER("copy_to_user failed %p %u\n", user_sizes,
			       srf->metadata.num_sizes);
		ttm_ref_object_base_unref(tfile, base->handle);
		ret = -EFAULT;
	}

	ttm_base_object_unref(&base);

	return ret;
}

/**
 * vmw_gb_surface_create - Encode a surface_define command.
 *
 * @res:        Pointer to a struct vmw_resource embedded in a struct
 *              vmw_surface.
 */
static int vmw_gb_surface_create(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct vmw_surface *srf = vmw_res_to_srf(res);
	struct vmw_surface_metadata *metadata = &srf->metadata;
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
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDefineGBSurface_v4 body;
	} *cmd4;

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

	if (has_sm5_context(dev_priv) && metadata->array_size > 0) {
		cmd_id = SVGA_3D_CMD_DEFINE_GB_SURFACE_V4;
		cmd_len = sizeof(cmd4->body);
		submit_len = sizeof(*cmd4);
	} else if (has_sm4_1_context(dev_priv) && metadata->array_size > 0) {
		cmd_id = SVGA_3D_CMD_DEFINE_GB_SURFACE_V3;
		cmd_len = sizeof(cmd3->body);
		submit_len = sizeof(*cmd3);
	} else if (metadata->array_size > 0) {
		/* VMW_SM_4 support verified at creation time. */
		cmd_id = SVGA_3D_CMD_DEFINE_GB_SURFACE_V2;
		cmd_len = sizeof(cmd2->body);
		submit_len = sizeof(*cmd2);
	} else {
		cmd_id = SVGA_3D_CMD_DEFINE_GB_SURFACE;
		cmd_len = sizeof(cmd->body);
		submit_len = sizeof(*cmd);
	}

	cmd = VMW_CMD_RESERVE(dev_priv, submit_len);
	cmd2 = (typeof(cmd2))cmd;
	cmd3 = (typeof(cmd3))cmd;
	cmd4 = (typeof(cmd4))cmd;
	if (unlikely(!cmd)) {
		ret = -ENOMEM;
		goto out_no_fifo;
	}

	if (has_sm5_context(dev_priv) && metadata->array_size > 0) {
		cmd4->header.id = cmd_id;
		cmd4->header.size = cmd_len;
		cmd4->body.sid = srf->res.id;
		cmd4->body.surfaceFlags = metadata->flags;
		cmd4->body.format = metadata->format;
		cmd4->body.numMipLevels = metadata->mip_levels[0];
		cmd4->body.multisampleCount = metadata->multisample_count;
		cmd4->body.multisamplePattern = metadata->multisample_pattern;
		cmd4->body.qualityLevel = metadata->quality_level;
		cmd4->body.autogenFilter = metadata->autogen_filter;
		cmd4->body.size.width = metadata->base_size.width;
		cmd4->body.size.height = metadata->base_size.height;
		cmd4->body.size.depth = metadata->base_size.depth;
		cmd4->body.arraySize = metadata->array_size;
		cmd4->body.bufferByteStride = metadata->buffer_byte_stride;
	} else if (has_sm4_1_context(dev_priv) && metadata->array_size > 0) {
		cmd3->header.id = cmd_id;
		cmd3->header.size = cmd_len;
		cmd3->body.sid = srf->res.id;
		cmd3->body.surfaceFlags = metadata->flags;
		cmd3->body.format = metadata->format;
		cmd3->body.numMipLevels = metadata->mip_levels[0];
		cmd3->body.multisampleCount = metadata->multisample_count;
		cmd3->body.multisamplePattern = metadata->multisample_pattern;
		cmd3->body.qualityLevel = metadata->quality_level;
		cmd3->body.autogenFilter = metadata->autogen_filter;
		cmd3->body.size.width = metadata->base_size.width;
		cmd3->body.size.height = metadata->base_size.height;
		cmd3->body.size.depth = metadata->base_size.depth;
		cmd3->body.arraySize = metadata->array_size;
	} else if (metadata->array_size > 0) {
		cmd2->header.id = cmd_id;
		cmd2->header.size = cmd_len;
		cmd2->body.sid = srf->res.id;
		cmd2->body.surfaceFlags = metadata->flags;
		cmd2->body.format = metadata->format;
		cmd2->body.numMipLevels = metadata->mip_levels[0];
		cmd2->body.multisampleCount = metadata->multisample_count;
		cmd2->body.autogenFilter = metadata->autogen_filter;
		cmd2->body.size.width = metadata->base_size.width;
		cmd2->body.size.height = metadata->base_size.height;
		cmd2->body.size.depth = metadata->base_size.depth;
		cmd2->body.arraySize = metadata->array_size;
	} else {
		cmd->header.id = cmd_id;
		cmd->header.size = cmd_len;
		cmd->body.sid = srf->res.id;
		cmd->body.surfaceFlags = metadata->flags;
		cmd->body.format = metadata->format;
		cmd->body.numMipLevels = metadata->mip_levels[0];
		cmd->body.multisampleCount = metadata->multisample_count;
		cmd->body.autogenFilter = metadata->autogen_filter;
		cmd->body.size.width = metadata->base_size.width;
		cmd->body.size.height = metadata->base_size.height;
		cmd->body.size.depth = metadata->base_size.depth;
	}

	vmw_cmd_commit(dev_priv, submit_len);

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

	BUG_ON(bo->resource->mem_type != VMW_PL_MOB);

	submit_size = sizeof(*cmd1) + (res->backup_dirty ? sizeof(*cmd2) : 0);

	cmd1 = VMW_CMD_RESERVE(dev_priv, submit_size);
	if (unlikely(!cmd1))
		return -ENOMEM;

	cmd1->header.id = SVGA_3D_CMD_BIND_GB_SURFACE;
	cmd1->header.size = sizeof(cmd1->body);
	cmd1->body.sid = res->id;
	cmd1->body.mobid = bo->resource->start;
	if (res->backup_dirty) {
		cmd2 = (void *) &cmd1[1];
		cmd2->header.id = SVGA_3D_CMD_UPDATE_GB_SURFACE;
		cmd2->header.size = sizeof(cmd2->body);
		cmd2->body.sid = res->id;
	}
	vmw_cmd_commit(dev_priv, submit_size);

	if (res->backup->dirty && res->backup_dirty) {
		/* We've just made a full upload. Cear dirty regions. */
		vmw_bo_dirty_clear_res(res);
	}

	res->backup_dirty = false;

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


	BUG_ON(bo->resource->mem_type != VMW_PL_MOB);

	submit_size = sizeof(*cmd3) + (readback ? sizeof(*cmd1) : sizeof(*cmd2));
	cmd = VMW_CMD_RESERVE(dev_priv, submit_size);
	if (unlikely(!cmd))
		return -ENOMEM;

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

	vmw_cmd_commit(dev_priv, submit_size);

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

	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(!cmd)) {
		mutex_unlock(&dev_priv->binding_mutex);
		return -ENOMEM;
	}

	cmd->header.id = SVGA_3D_CMD_DESTROY_GB_SURFACE;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.sid = res->id;
	vmw_cmd_commit(dev_priv, sizeof(*cmd));
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
	req_ext.buffer_byte_stride = 0;
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
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct vmw_user_surface *user_srf;
	struct vmw_surface_metadata metadata = {0};
	struct vmw_surface *srf;
	struct vmw_resource *res;
	struct vmw_resource *tmp;
	int ret = 0;
	uint32_t backup_handle = 0;
	SVGA3dSurfaceAllFlags svga3d_flags_64 =
		SVGA3D_FLAGS_64(req->svga3d_flags_upper_32_bits,
				req->base.svga3d_flags);

	/* array_size must be null for non-GL3 host. */
	if (req->base.array_size > 0 && !has_sm4_context(dev_priv)) {
		VMW_DEBUG_USER("SM4 surface not supported.\n");
		return -EINVAL;
	}

	if (!has_sm4_1_context(dev_priv)) {
		if (req->svga3d_flags_upper_32_bits != 0)
			ret = -EINVAL;

		if (req->base.multisample_count != 0)
			ret = -EINVAL;

		if (req->multisample_pattern != SVGA3D_MS_PATTERN_NONE)
			ret = -EINVAL;

		if (req->quality_level != SVGA3D_MS_QUALITY_NONE)
			ret = -EINVAL;

		if (ret) {
			VMW_DEBUG_USER("SM4.1 surface not supported.\n");
			return ret;
		}
	}

	if (req->buffer_byte_stride > 0 && !has_sm5_context(dev_priv)) {
		VMW_DEBUG_USER("SM5 surface not supported.\n");
		return -EINVAL;
	}

	if ((svga3d_flags_64 & SVGA3D_SURFACE_MULTISAMPLE) &&
	    req->base.multisample_count == 0) {
		VMW_DEBUG_USER("Invalid sample count.\n");
		return -EINVAL;
	}

	if (req->base.mip_levels > DRM_VMW_MAX_MIP_LEVELS) {
		VMW_DEBUG_USER("Invalid mip level.\n");
		return -EINVAL;
	}

	metadata.flags = svga3d_flags_64;
	metadata.format = req->base.format;
	metadata.mip_levels[0] = req->base.mip_levels;
	metadata.multisample_count = req->base.multisample_count;
	metadata.multisample_pattern = req->multisample_pattern;
	metadata.quality_level = req->quality_level;
	metadata.array_size = req->base.array_size;
	metadata.buffer_byte_stride = req->buffer_byte_stride;
	metadata.num_sizes = 1;
	metadata.base_size = req->base.base_size;
	metadata.scanout = req->base.drm_surface_flags &
		drm_vmw_surface_flag_scanout;

	/* Define a surface based on the parameters. */
	ret = vmw_gb_surface_define(dev_priv, &metadata, &srf);
	if (ret != 0) {
		VMW_DEBUG_USER("Failed to define surface.\n");
		return ret;
	}

	user_srf = container_of(srf, struct vmw_user_surface, srf);
	if (drm_is_primary_client(file_priv))
		user_srf->master = drm_file_get_master(file_priv);

	res = &user_srf->srf.res;

	if (req->base.buffer_handle != SVGA3D_INVALID_ID) {
		ret = vmw_user_bo_lookup(file_priv, req->base.buffer_handle,
					 &res->backup);
		if (ret == 0) {
			if (res->backup->base.base.size < res->backup_size) {
				VMW_DEBUG_USER("Surface backup buffer too small.\n");
				vmw_bo_unreference(&res->backup);
				ret = -EINVAL;
				goto out_unlock;
			} else {
				backup_handle = req->base.buffer_handle;
			}
		}
	} else if (req->base.drm_surface_flags &
		   (drm_vmw_surface_flag_create_buffer |
		    drm_vmw_surface_flag_coherent)) {
		ret = vmw_gem_object_create_with_handle(dev_priv, file_priv,
							res->backup_size,
							&backup_handle,
							&res->backup);
		if (ret == 0)
			vmw_bo_reference(res->backup);
	}

	if (unlikely(ret != 0)) {
		vmw_resource_unreference(&res);
		goto out_unlock;
	}

	if (req->base.drm_surface_flags & drm_vmw_surface_flag_coherent) {
		struct vmw_buffer_object *backup = res->backup;

		ttm_bo_reserve(&backup->base, false, false, NULL);
		if (!res->func->dirty_alloc)
			ret = -EINVAL;
		if (!ret)
			ret = vmw_bo_dirty_add(backup);
		if (!ret) {
			res->coherent = true;
			ret = res->func->dirty_alloc(res);
		}
		ttm_bo_unreserve(&backup->base);
		if (ret) {
			vmw_resource_unreference(&res);
			goto out_unlock;
		}

	}

	tmp = vmw_resource_reference(res);
	ret = ttm_prime_object_init(tfile, res->backup_size, &user_srf->prime,
				    req->base.drm_surface_flags &
				    drm_vmw_surface_flag_shareable,
				    VMW_RES_SURFACE,
				    &vmw_user_surface_base_release);

	if (unlikely(ret != 0)) {
		vmw_resource_unreference(&tmp);
		vmw_resource_unreference(&res);
		goto out_unlock;
	}

	rep->handle      = user_srf->prime.base.handle;
	rep->backup_size = res->backup_size;
	if (res->backup) {
		rep->buffer_map_handle =
			drm_vma_node_offset_addr(&res->backup->base.base.vma_node);
		rep->buffer_size = res->backup->base.base.size;
		rep->buffer_handle = backup_handle;
	} else {
		rep->buffer_map_handle = 0;
		rep->buffer_size = 0;
		rep->buffer_handle = SVGA3D_INVALID_ID;
	}
	vmw_resource_unreference(&res);

out_unlock:
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
	struct vmw_surface *srf;
	struct vmw_user_surface *user_srf;
	struct vmw_surface_metadata *metadata;
	struct ttm_base_object *base;
	u32 backup_handle;
	int ret;

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
	metadata = &srf->metadata;

	mutex_lock(&dev_priv->cmdbuf_mutex); /* Protect res->backup */
	ret = drm_gem_handle_create(file_priv, &srf->res.backup->base.base,
				    &backup_handle);
	mutex_unlock(&dev_priv->cmdbuf_mutex);
	if (ret != 0) {
		drm_err(dev, "Wasn't able to create a backing handle for surface sid = %u.\n",
			req->sid);
		goto out_bad_resource;
	}

	rep->creq.base.svga3d_flags = SVGA3D_FLAGS_LOWER_32(metadata->flags);
	rep->creq.base.format = metadata->format;
	rep->creq.base.mip_levels = metadata->mip_levels[0];
	rep->creq.base.drm_surface_flags = 0;
	rep->creq.base.multisample_count = metadata->multisample_count;
	rep->creq.base.autogen_filter = metadata->autogen_filter;
	rep->creq.base.array_size = metadata->array_size;
	rep->creq.base.buffer_handle = backup_handle;
	rep->creq.base.base_size = metadata->base_size;
	rep->crep.handle = user_srf->prime.base.handle;
	rep->crep.backup_size = srf->res.backup_size;
	rep->crep.buffer_handle = backup_handle;
	rep->crep.buffer_map_handle =
		drm_vma_node_offset_addr(&srf->res.backup->base.base.vma_node);
	rep->crep.buffer_size = srf->res.backup->base.base.size;

	rep->creq.version = drm_vmw_gb_surface_v1;
	rep->creq.svga3d_flags_upper_32_bits =
		SVGA3D_FLAGS_UPPER_32(metadata->flags);
	rep->creq.multisample_pattern = metadata->multisample_pattern;
	rep->creq.quality_level = metadata->quality_level;
	rep->creq.must_be_zero = 0;

out_bad_resource:
	ttm_base_object_unref(&base);

	return ret;
}

/**
 * vmw_subres_dirty_add - Add a dirty region to a subresource
 * @dirty: The surfaces's dirty tracker.
 * @loc_start: The location corresponding to the start of the region.
 * @loc_end: The location corresponding to the end of the region.
 *
 * As we are assuming that @loc_start and @loc_end represent a sequential
 * range of backing store memory, if the region spans multiple lines then
 * regardless of the x coordinate, the full lines are dirtied.
 * Correspondingly if the region spans multiple z slices, then full rather
 * than partial z slices are dirtied.
 */
static void vmw_subres_dirty_add(struct vmw_surface_dirty *dirty,
				 const struct vmw_surface_loc *loc_start,
				 const struct vmw_surface_loc *loc_end)
{
	const struct vmw_surface_cache *cache = &dirty->cache;
	SVGA3dBox *box = &dirty->boxes[loc_start->sub_resource];
	u32 mip = loc_start->sub_resource % cache->num_mip_levels;
	const struct drm_vmw_size *size = &cache->mip[mip].size;
	u32 box_c2 = box->z + box->d;

	if (WARN_ON(loc_start->sub_resource >= dirty->num_subres))
		return;

	if (box->d == 0 || box->z > loc_start->z)
		box->z = loc_start->z;
	if (box_c2 < loc_end->z)
		box->d = loc_end->z - box->z;

	if (loc_start->z + 1 == loc_end->z) {
		box_c2 = box->y + box->h;
		if (box->h == 0 || box->y > loc_start->y)
			box->y = loc_start->y;
		if (box_c2 < loc_end->y)
			box->h = loc_end->y - box->y;

		if (loc_start->y + 1 == loc_end->y) {
			box_c2 = box->x + box->w;
			if (box->w == 0 || box->x > loc_start->x)
				box->x = loc_start->x;
			if (box_c2 < loc_end->x)
				box->w = loc_end->x - box->x;
		} else {
			box->x = 0;
			box->w = size->width;
		}
	} else {
		box->y = 0;
		box->h = size->height;
		box->x = 0;
		box->w = size->width;
	}
}

/**
 * vmw_subres_dirty_full - Mark a full subresource as dirty
 * @dirty: The surface's dirty tracker.
 * @subres: The subresource
 */
static void vmw_subres_dirty_full(struct vmw_surface_dirty *dirty, u32 subres)
{
	const struct vmw_surface_cache *cache = &dirty->cache;
	u32 mip = subres % cache->num_mip_levels;
	const struct drm_vmw_size *size = &cache->mip[mip].size;
	SVGA3dBox *box = &dirty->boxes[subres];

	box->x = 0;
	box->y = 0;
	box->z = 0;
	box->w = size->width;
	box->h = size->height;
	box->d = size->depth;
}

/*
 * vmw_surface_tex_dirty_add_range - The dirty_add_range callback for texture
 * surfaces.
 */
static void vmw_surface_tex_dirty_range_add(struct vmw_resource *res,
					    size_t start, size_t end)
{
	struct vmw_surface_dirty *dirty =
		(struct vmw_surface_dirty *) res->dirty;
	size_t backup_end = res->backup_offset + res->backup_size;
	struct vmw_surface_loc loc1, loc2;
	const struct vmw_surface_cache *cache;

	start = max_t(size_t, start, res->backup_offset) - res->backup_offset;
	end = min(end, backup_end) - res->backup_offset;
	cache = &dirty->cache;
	vmw_surface_get_loc(cache, &loc1, start);
	vmw_surface_get_loc(cache, &loc2, end - 1);
	vmw_surface_inc_loc(cache, &loc2);

	if (loc1.sheet != loc2.sheet) {
		u32 sub_res;

		/*
		 * Multiple multisample sheets. To do this in an optimized
		 * fashion, compute the dirty region for each sheet and the
		 * resulting union. Since this is not a common case, just dirty
		 * the whole surface.
		 */
		for (sub_res = 0; sub_res < dirty->num_subres; ++sub_res)
			vmw_subres_dirty_full(dirty, sub_res);
		return;
	}
	if (loc1.sub_resource + 1 == loc2.sub_resource) {
		/* Dirty range covers a single sub-resource */
		vmw_subres_dirty_add(dirty, &loc1, &loc2);
	} else {
		/* Dirty range covers multiple sub-resources */
		struct vmw_surface_loc loc_min, loc_max;
		u32 sub_res;

		vmw_surface_max_loc(cache, loc1.sub_resource, &loc_max);
		vmw_subres_dirty_add(dirty, &loc1, &loc_max);
		vmw_surface_min_loc(cache, loc2.sub_resource - 1, &loc_min);
		vmw_subres_dirty_add(dirty, &loc_min, &loc2);
		for (sub_res = loc1.sub_resource + 1;
		     sub_res < loc2.sub_resource - 1; ++sub_res)
			vmw_subres_dirty_full(dirty, sub_res);
	}
}

/*
 * vmw_surface_tex_dirty_add_range - The dirty_add_range callback for buffer
 * surfaces.
 */
static void vmw_surface_buf_dirty_range_add(struct vmw_resource *res,
					    size_t start, size_t end)
{
	struct vmw_surface_dirty *dirty =
		(struct vmw_surface_dirty *) res->dirty;
	const struct vmw_surface_cache *cache = &dirty->cache;
	size_t backup_end = res->backup_offset + cache->mip_chain_bytes;
	SVGA3dBox *box = &dirty->boxes[0];
	u32 box_c2;

	box->h = box->d = 1;
	start = max_t(size_t, start, res->backup_offset) - res->backup_offset;
	end = min(end, backup_end) - res->backup_offset;
	box_c2 = box->x + box->w;
	if (box->w == 0 || box->x > start)
		box->x = start;
	if (box_c2 < end)
		box->w = end - box->x;
}

/*
 * vmw_surface_tex_dirty_add_range - The dirty_add_range callback for surfaces
 */
static void vmw_surface_dirty_range_add(struct vmw_resource *res, size_t start,
					size_t end)
{
	struct vmw_surface *srf = vmw_res_to_srf(res);

	if (WARN_ON(end <= res->backup_offset ||
		    start >= res->backup_offset + res->backup_size))
		return;

	if (srf->metadata.format == SVGA3D_BUFFER)
		vmw_surface_buf_dirty_range_add(res, start, end);
	else
		vmw_surface_tex_dirty_range_add(res, start, end);
}

/*
 * vmw_surface_dirty_sync - The surface's dirty_sync callback.
 */
static int vmw_surface_dirty_sync(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	u32 i, num_dirty;
	struct vmw_surface_dirty *dirty =
		(struct vmw_surface_dirty *) res->dirty;
	size_t alloc_size;
	const struct vmw_surface_cache *cache = &dirty->cache;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXUpdateSubResource body;
	} *cmd1;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdUpdateGBImage body;
	} *cmd2;
	void *cmd;

	num_dirty = 0;
	for (i = 0; i < dirty->num_subres; ++i) {
		const SVGA3dBox *box = &dirty->boxes[i];

		if (box->d)
			num_dirty++;
	}

	if (!num_dirty)
		goto out;

	alloc_size = num_dirty * ((has_sm4_context(dev_priv)) ? sizeof(*cmd1) : sizeof(*cmd2));
	cmd = VMW_CMD_RESERVE(dev_priv, alloc_size);
	if (!cmd)
		return -ENOMEM;

	cmd1 = cmd;
	cmd2 = cmd;

	for (i = 0; i < dirty->num_subres; ++i) {
		const SVGA3dBox *box = &dirty->boxes[i];

		if (!box->d)
			continue;

		/*
		 * DX_UPDATE_SUBRESOURCE is aware of array surfaces.
		 * UPDATE_GB_IMAGE is not.
		 */
		if (has_sm4_context(dev_priv)) {
			cmd1->header.id = SVGA_3D_CMD_DX_UPDATE_SUBRESOURCE;
			cmd1->header.size = sizeof(cmd1->body);
			cmd1->body.sid = res->id;
			cmd1->body.subResource = i;
			cmd1->body.box = *box;
			cmd1++;
		} else {
			cmd2->header.id = SVGA_3D_CMD_UPDATE_GB_IMAGE;
			cmd2->header.size = sizeof(cmd2->body);
			cmd2->body.image.sid = res->id;
			cmd2->body.image.face = i / cache->num_mip_levels;
			cmd2->body.image.mipmap = i -
				(cache->num_mip_levels * cmd2->body.image.face);
			cmd2->body.box = *box;
			cmd2++;
		}

	}
	vmw_cmd_commit(dev_priv, alloc_size);
 out:
	memset(&dirty->boxes[0], 0, sizeof(dirty->boxes[0]) *
	       dirty->num_subres);

	return 0;
}

/*
 * vmw_surface_dirty_alloc - The surface's dirty_alloc callback.
 */
static int vmw_surface_dirty_alloc(struct vmw_resource *res)
{
	struct vmw_surface *srf = vmw_res_to_srf(res);
	const struct vmw_surface_metadata *metadata = &srf->metadata;
	struct vmw_surface_dirty *dirty;
	u32 num_layers = 1;
	u32 num_mip;
	u32 num_subres;
	u32 num_samples;
	size_t dirty_size;
	int ret;

	if (metadata->array_size)
		num_layers = metadata->array_size;
	else if (metadata->flags & SVGA3D_SURFACE_CUBEMAP)
		num_layers *= SVGA3D_MAX_SURFACE_FACES;

	num_mip = metadata->mip_levels[0];
	if (!num_mip)
		num_mip = 1;

	num_subres = num_layers * num_mip;
	dirty_size = struct_size(dirty, boxes, num_subres);

	dirty = kvzalloc(dirty_size, GFP_KERNEL);
	if (!dirty) {
		ret = -ENOMEM;
		goto out_no_dirty;
	}

	num_samples = max_t(u32, 1, metadata->multisample_count);
	ret = vmw_surface_setup_cache(&metadata->base_size, metadata->format,
				      num_mip, num_layers, num_samples,
				      &dirty->cache);
	if (ret)
		goto out_no_cache;

	dirty->num_subres = num_subres;
	res->dirty = (struct vmw_resource_dirty *) dirty;

	return 0;

out_no_cache:
	kvfree(dirty);
out_no_dirty:
	return ret;
}

/*
 * vmw_surface_dirty_free - The surface's dirty_free callback
 */
static void vmw_surface_dirty_free(struct vmw_resource *res)
{
	struct vmw_surface_dirty *dirty =
		(struct vmw_surface_dirty *) res->dirty;

	kvfree(dirty);
	res->dirty = NULL;
}

/*
 * vmw_surface_clean - The surface's clean callback
 */
static int vmw_surface_clean(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	size_t alloc_size;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdReadbackGBSurface body;
	} *cmd;

	alloc_size = sizeof(*cmd);
	cmd = VMW_CMD_RESERVE(dev_priv, alloc_size);
	if (!cmd)
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_READBACK_GB_SURFACE;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.sid = res->id;
	vmw_cmd_commit(dev_priv, alloc_size);

	return 0;
}

/*
 * vmw_gb_surface_define - Define a private GB surface
 *
 * @dev_priv: Pointer to a device private.
 * @metadata: Metadata representing the surface to create.
 * @user_srf_out: allocated user_srf. Set to NULL on failure.
 *
 * GB surfaces allocated by this function will not have a user mode handle, and
 * thus will only be visible to vmwgfx.  For optimization reasons the
 * surface may later be given a user mode handle by another function to make
 * it available to user mode drivers.
 */
int vmw_gb_surface_define(struct vmw_private *dev_priv,
			  const struct vmw_surface_metadata *req,
			  struct vmw_surface **srf_out)
{
	struct vmw_surface_metadata *metadata;
	struct vmw_user_surface *user_srf;
	struct vmw_surface *srf;
	u32 sample_count = 1;
	u32 num_layers = 1;
	int ret;

	*srf_out = NULL;

	if (req->scanout) {
		if (!vmw_surface_is_screen_target_format(req->format)) {
			VMW_DEBUG_USER("Invalid Screen Target surface format.");
			return -EINVAL;
		}

		if (req->base_size.width > dev_priv->texture_max_width ||
		    req->base_size.height > dev_priv->texture_max_height) {
			VMW_DEBUG_USER("%ux%u\n, exceed max surface size %ux%u",
				       req->base_size.width,
				       req->base_size.height,
				       dev_priv->texture_max_width,
				       dev_priv->texture_max_height);
			return -EINVAL;
		}
	} else {
		const SVGA3dSurfaceDesc *desc =
			vmw_surface_get_desc(req->format);

		if (desc->blockDesc == SVGA3DBLOCKDESC_NONE) {
			VMW_DEBUG_USER("Invalid surface format.\n");
			return -EINVAL;
		}
	}

	if (req->autogen_filter != SVGA3D_TEX_FILTER_NONE)
		return -EINVAL;

	if (req->num_sizes != 1)
		return -EINVAL;

	if (req->sizes != NULL)
		return -EINVAL;

	user_srf = kzalloc(sizeof(*user_srf), GFP_KERNEL);
	if (unlikely(!user_srf)) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	*srf_out  = &user_srf->srf;
	user_srf->prime.base.shareable = false;
	user_srf->prime.base.tfile = NULL;

	srf = &user_srf->srf;
	srf->metadata = *req;
	srf->offsets = NULL;

	metadata = &srf->metadata;

	if (metadata->array_size)
		num_layers = req->array_size;
	else if (metadata->flags & SVGA3D_SURFACE_CUBEMAP)
		num_layers = SVGA3D_MAX_SURFACE_FACES;

	if (metadata->flags & SVGA3D_SURFACE_MULTISAMPLE)
		sample_count = metadata->multisample_count;

	srf->res.backup_size =
		vmw_surface_get_serialized_size_extended(
				metadata->format,
				metadata->base_size,
				metadata->mip_levels[0],
				num_layers,
				sample_count);

	if (metadata->flags & SVGA3D_SURFACE_BIND_STREAM_OUTPUT)
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
	    metadata->scanout &&
	    metadata->base_size.width <= dev_priv->stdu_max_width &&
	    metadata->base_size.height <= dev_priv->stdu_max_height)
		metadata->flags |= SVGA3D_SURFACE_SCREENTARGET;

	/*
	 * From this point, the generic resource management functions
	 * destroy the object on failure.
	 */
	ret = vmw_surface_init(dev_priv, srf, vmw_user_surface_free);

	return ret;

out_unlock:
	return ret;
}
