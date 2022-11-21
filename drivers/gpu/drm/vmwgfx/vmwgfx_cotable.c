// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright 2014-2015 VMware, Inc., Palo Alto, CA., USA
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
/*
 * Treat context OTables as resources to make use of the resource
 * backing MOB eviction mechanism, that is used to read back the COTable
 * whenever the backing MOB is evicted.
 */

#include <drm/ttm/ttm_placement.h>

#include "vmwgfx_drv.h"
#include "vmwgfx_resource_priv.h"
#include "vmwgfx_so.h"

/**
 * struct vmw_cotable - Context Object Table resource
 *
 * @res: struct vmw_resource we are deriving from.
 * @ctx: non-refcounted pointer to the owning context.
 * @size_read_back: Size of data read back during eviction.
 * @seen_entries: Seen entries in command stream for this cotable.
 * @type: The cotable type.
 * @scrubbed: Whether the cotable has been scrubbed.
 * @resource_list: List of resources in the cotable.
 */
struct vmw_cotable {
	struct vmw_resource res;
	struct vmw_resource *ctx;
	size_t size_read_back;
	int seen_entries;
	u32 type;
	bool scrubbed;
	struct list_head resource_list;
};

/**
 * struct vmw_cotable_info - Static info about cotable types
 *
 * @min_initial_entries: Min number of initial intries at cotable allocation
 * for this cotable type.
 * @size: Size of each entry.
 * @unbind_func: Unbind call-back function.
 */
struct vmw_cotable_info {
	u32 min_initial_entries;
	u32 size;
	void (*unbind_func)(struct vmw_private *, struct list_head *,
			    bool);
};

static const struct vmw_cotable_info co_info[] = {
	{1, sizeof(SVGACOTableDXRTViewEntry), &vmw_view_cotable_list_destroy},
	{1, sizeof(SVGACOTableDXDSViewEntry), &vmw_view_cotable_list_destroy},
	{1, sizeof(SVGACOTableDXSRViewEntry), &vmw_view_cotable_list_destroy},
	{1, sizeof(SVGACOTableDXElementLayoutEntry), NULL},
	{1, sizeof(SVGACOTableDXBlendStateEntry), NULL},
	{1, sizeof(SVGACOTableDXDepthStencilEntry), NULL},
	{1, sizeof(SVGACOTableDXRasterizerStateEntry), NULL},
	{1, sizeof(SVGACOTableDXSamplerEntry), NULL},
	{1, sizeof(SVGACOTableDXStreamOutputEntry), &vmw_dx_streamoutput_cotable_list_scrub},
	{1, sizeof(SVGACOTableDXQueryEntry), NULL},
	{1, sizeof(SVGACOTableDXShaderEntry), &vmw_dx_shader_cotable_list_scrub},
	{1, sizeof(SVGACOTableDXUAViewEntry), &vmw_view_cotable_list_destroy}
};

/*
 * Cotables with bindings that we remove must be scrubbed first,
 * otherwise, the device will swap in an invalid context when we remove
 * bindings before scrubbing a cotable...
 */
const SVGACOTableType vmw_cotable_scrub_order[] = {
	SVGA_COTABLE_RTVIEW,
	SVGA_COTABLE_DSVIEW,
	SVGA_COTABLE_SRVIEW,
	SVGA_COTABLE_DXSHADER,
	SVGA_COTABLE_ELEMENTLAYOUT,
	SVGA_COTABLE_BLENDSTATE,
	SVGA_COTABLE_DEPTHSTENCIL,
	SVGA_COTABLE_RASTERIZERSTATE,
	SVGA_COTABLE_SAMPLER,
	SVGA_COTABLE_STREAMOUTPUT,
	SVGA_COTABLE_DXQUERY,
	SVGA_COTABLE_UAVIEW,
};

static int vmw_cotable_bind(struct vmw_resource *res,
			    struct ttm_validate_buffer *val_buf);
static int vmw_cotable_unbind(struct vmw_resource *res,
			      bool readback,
			      struct ttm_validate_buffer *val_buf);
static int vmw_cotable_create(struct vmw_resource *res);
static int vmw_cotable_destroy(struct vmw_resource *res);

static const struct vmw_res_func vmw_cotable_func = {
	.res_type = vmw_res_cotable,
	.needs_backup = true,
	.may_evict = true,
	.prio = 3,
	.dirty_prio = 3,
	.type_name = "context guest backed object tables",
	.backup_placement = &vmw_mob_placement,
	.create = vmw_cotable_create,
	.destroy = vmw_cotable_destroy,
	.bind = vmw_cotable_bind,
	.unbind = vmw_cotable_unbind,
};

/**
 * vmw_cotable - Convert a struct vmw_resource pointer to a struct
 * vmw_cotable pointer
 *
 * @res: Pointer to the resource.
 */
static struct vmw_cotable *vmw_cotable(struct vmw_resource *res)
{
	return container_of(res, struct vmw_cotable, res);
}

/**
 * vmw_cotable_destroy - Cotable resource destroy callback
 *
 * @res: Pointer to the cotable resource.
 *
 * There is no device cotable destroy command, so this function only
 * makes sure that the resource id is set to invalid.
 */
static int vmw_cotable_destroy(struct vmw_resource *res)
{
	res->id = -1;
	return 0;
}

/**
 * vmw_cotable_unscrub - Undo a cotable unscrub operation
 *
 * @res: Pointer to the cotable resource
 *
 * This function issues commands to (re)bind the cotable to
 * its backing mob, which needs to be validated and reserved at this point.
 * This is identical to bind() except the function interface looks different.
 */
static int vmw_cotable_unscrub(struct vmw_resource *res)
{
	struct vmw_cotable *vcotbl = vmw_cotable(res);
	struct vmw_private *dev_priv = res->dev_priv;
	struct ttm_buffer_object *bo = &res->backup->base;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXSetCOTable body;
	} *cmd;

	WARN_ON_ONCE(bo->resource->mem_type != VMW_PL_MOB);
	dma_resv_assert_held(bo->base.resv);

	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (!cmd)
		return -ENOMEM;

	WARN_ON(vcotbl->ctx->id == SVGA3D_INVALID_ID);
	WARN_ON(bo->resource->mem_type != VMW_PL_MOB);
	cmd->header.id = SVGA_3D_CMD_DX_SET_COTABLE;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.cid = vcotbl->ctx->id;
	cmd->body.type = vcotbl->type;
	cmd->body.mobid = bo->resource->start;
	cmd->body.validSizeInBytes = vcotbl->size_read_back;

	vmw_cmd_commit_flush(dev_priv, sizeof(*cmd));
	vcotbl->scrubbed = false;

	return 0;
}

/**
 * vmw_cotable_bind - Undo a cotable unscrub operation
 *
 * @res: Pointer to the cotable resource
 * @val_buf: Pointer to a struct ttm_validate_buffer prepared by the caller
 * for convenience / fencing.
 *
 * This function issues commands to (re)bind the cotable to
 * its backing mob, which needs to be validated and reserved at this point.
 */
static int vmw_cotable_bind(struct vmw_resource *res,
			    struct ttm_validate_buffer *val_buf)
{
	/*
	 * The create() callback may have changed @res->backup without
	 * the caller noticing, and with val_buf->bo still pointing to
	 * the old backup buffer. Although hackish, and not used currently,
	 * take the opportunity to correct the value here so that it's not
	 * misused in the future.
	 */
	val_buf->bo = &res->backup->base;

	return vmw_cotable_unscrub(res);
}

/**
 * vmw_cotable_scrub - Scrub the cotable from the device.
 *
 * @res: Pointer to the cotable resource.
 * @readback: Whether initiate a readback of the cotable data to the backup
 * buffer.
 *
 * In some situations (context swapouts) it might be desirable to make the
 * device forget about the cotable without performing a full unbind. A full
 * unbind requires reserved backup buffers and it might not be possible to
 * reserve them due to locking order violation issues. The vmw_cotable_scrub
 * function implements a partial unbind() without that requirement but with the
 * following restrictions.
 * 1) Before the cotable is again used by the GPU, vmw_cotable_unscrub() must
 *    be called.
 * 2) Before the cotable backing buffer is used by the CPU, or during the
 *    resource destruction, vmw_cotable_unbind() must be called.
 */
int vmw_cotable_scrub(struct vmw_resource *res, bool readback)
{
	struct vmw_cotable *vcotbl = vmw_cotable(res);
	struct vmw_private *dev_priv = res->dev_priv;
	size_t submit_size;

	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXReadbackCOTable body;
	} *cmd0;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXSetCOTable body;
	} *cmd1;

	if (vcotbl->scrubbed)
		return 0;

	if (co_info[vcotbl->type].unbind_func)
		co_info[vcotbl->type].unbind_func(dev_priv,
						  &vcotbl->resource_list,
						  readback);
	submit_size = sizeof(*cmd1);
	if (readback)
		submit_size += sizeof(*cmd0);

	cmd1 = VMW_CMD_RESERVE(dev_priv, submit_size);
	if (!cmd1)
		return -ENOMEM;

	vcotbl->size_read_back = 0;
	if (readback) {
		cmd0 = (void *) cmd1;
		cmd0->header.id = SVGA_3D_CMD_DX_READBACK_COTABLE;
		cmd0->header.size = sizeof(cmd0->body);
		cmd0->body.cid = vcotbl->ctx->id;
		cmd0->body.type = vcotbl->type;
		cmd1 = (void *) &cmd0[1];
		vcotbl->size_read_back = res->backup_size;
	}
	cmd1->header.id = SVGA_3D_CMD_DX_SET_COTABLE;
	cmd1->header.size = sizeof(cmd1->body);
	cmd1->body.cid = vcotbl->ctx->id;
	cmd1->body.type = vcotbl->type;
	cmd1->body.mobid = SVGA3D_INVALID_ID;
	cmd1->body.validSizeInBytes = 0;
	vmw_cmd_commit_flush(dev_priv, submit_size);
	vcotbl->scrubbed = true;

	/* Trigger a create() on next validate. */
	res->id = -1;

	return 0;
}

/**
 * vmw_cotable_unbind - Cotable resource unbind callback
 *
 * @res: Pointer to the cotable resource.
 * @readback: Whether to read back cotable data to the backup buffer.
 * @val_buf: Pointer to a struct ttm_validate_buffer prepared by the caller
 * for convenience / fencing.
 *
 * Unbinds the cotable from the device and fences the backup buffer.
 */
static int vmw_cotable_unbind(struct vmw_resource *res,
			      bool readback,
			      struct ttm_validate_buffer *val_buf)
{
	struct vmw_cotable *vcotbl = vmw_cotable(res);
	struct vmw_private *dev_priv = res->dev_priv;
	struct ttm_buffer_object *bo = val_buf->bo;
	struct vmw_fence_obj *fence;

	if (!vmw_resource_mob_attached(res))
		return 0;

	WARN_ON_ONCE(bo->resource->mem_type != VMW_PL_MOB);
	dma_resv_assert_held(bo->base.resv);

	mutex_lock(&dev_priv->binding_mutex);
	if (!vcotbl->scrubbed)
		vmw_dx_context_scrub_cotables(vcotbl->ctx, readback);
	mutex_unlock(&dev_priv->binding_mutex);
	(void) vmw_execbuf_fence_commands(NULL, dev_priv, &fence, NULL);
	vmw_bo_fence_single(bo, fence);
	if (likely(fence != NULL))
		vmw_fence_obj_unreference(&fence);

	return 0;
}

/**
 * vmw_cotable_readback - Read back a cotable without unbinding.
 *
 * @res: The cotable resource.
 *
 * Reads back a cotable to its backing mob without scrubbing the MOB from
 * the cotable. The MOB is fenced for subsequent CPU access.
 */
static int vmw_cotable_readback(struct vmw_resource *res)
{
	struct vmw_cotable *vcotbl = vmw_cotable(res);
	struct vmw_private *dev_priv = res->dev_priv;

	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXReadbackCOTable body;
	} *cmd;
	struct vmw_fence_obj *fence;

	if (!vcotbl->scrubbed) {
		cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
		if (!cmd)
			return -ENOMEM;

		cmd->header.id = SVGA_3D_CMD_DX_READBACK_COTABLE;
		cmd->header.size = sizeof(cmd->body);
		cmd->body.cid = vcotbl->ctx->id;
		cmd->body.type = vcotbl->type;
		vcotbl->size_read_back = res->backup_size;
		vmw_cmd_commit(dev_priv, sizeof(*cmd));
	}

	(void) vmw_execbuf_fence_commands(NULL, dev_priv, &fence, NULL);
	vmw_bo_fence_single(&res->backup->base, fence);
	vmw_fence_obj_unreference(&fence);

	return 0;
}

/**
 * vmw_cotable_resize - Resize a cotable.
 *
 * @res: The cotable resource.
 * @new_size: The new size.
 *
 * Resizes a cotable and binds the new backup buffer.
 * On failure the cotable is left intact.
 * Important! This function may not fail once the MOB switch has been
 * committed to hardware. That would put the device context in an
 * invalid state which we can't currently recover from.
 */
static int vmw_cotable_resize(struct vmw_resource *res, size_t new_size)
{
	struct ttm_operation_ctx ctx = { false, false };
	struct vmw_private *dev_priv = res->dev_priv;
	struct vmw_cotable *vcotbl = vmw_cotable(res);
	struct vmw_buffer_object *buf, *old_buf = res->backup;
	struct ttm_buffer_object *bo, *old_bo = &res->backup->base;
	size_t old_size = res->backup_size;
	size_t old_size_read_back = vcotbl->size_read_back;
	size_t cur_size_read_back;
	struct ttm_bo_kmap_obj old_map, new_map;
	int ret;
	size_t i;

	ret = vmw_cotable_readback(res);
	if (ret)
		return ret;

	cur_size_read_back = vcotbl->size_read_back;
	vcotbl->size_read_back = old_size_read_back;

	/*
	 * While device is processing, Allocate and reserve a buffer object
	 * for the new COTable. Initially pin the buffer object to make sure
	 * we can use tryreserve without failure.
	 */
	ret = vmw_bo_create(dev_priv, new_size, &vmw_mob_placement,
			    true, true, vmw_bo_bo_free, &buf);
	if (ret) {
		DRM_ERROR("Failed initializing new cotable MOB.\n");
		return ret;
	}

	bo = &buf->base;
	WARN_ON_ONCE(ttm_bo_reserve(bo, false, true, NULL));

	ret = ttm_bo_wait(old_bo, false, false);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed waiting for cotable unbind.\n");
		goto out_wait;
	}

	/*
	 * Do a page by page copy of COTables. This eliminates slow vmap()s.
	 * This should really be a TTM utility.
	 */
	for (i = 0; i < old_bo->resource->num_pages; ++i) {
		bool dummy;

		ret = ttm_bo_kmap(old_bo, i, 1, &old_map);
		if (unlikely(ret != 0)) {
			DRM_ERROR("Failed mapping old COTable on resize.\n");
			goto out_wait;
		}
		ret = ttm_bo_kmap(bo, i, 1, &new_map);
		if (unlikely(ret != 0)) {
			DRM_ERROR("Failed mapping new COTable on resize.\n");
			goto out_map_new;
		}
		memcpy(ttm_kmap_obj_virtual(&new_map, &dummy),
		       ttm_kmap_obj_virtual(&old_map, &dummy),
		       PAGE_SIZE);
		ttm_bo_kunmap(&new_map);
		ttm_bo_kunmap(&old_map);
	}

	/* Unpin new buffer, and switch backup buffers. */
	ret = ttm_bo_validate(bo, &vmw_mob_placement, &ctx);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed validating new COTable backup buffer.\n");
		goto out_wait;
	}

	vmw_resource_mob_detach(res);
	res->backup = buf;
	res->backup_size = new_size;
	vcotbl->size_read_back = cur_size_read_back;

	/*
	 * Now tell the device to switch. If this fails, then we need to
	 * revert the full resize.
	 */
	ret = vmw_cotable_unscrub(res);
	if (ret) {
		DRM_ERROR("Failed switching COTable backup buffer.\n");
		res->backup = old_buf;
		res->backup_size = old_size;
		vcotbl->size_read_back = old_size_read_back;
		vmw_resource_mob_attach(res);
		goto out_wait;
	}

	vmw_resource_mob_attach(res);
	/* Let go of the old mob. */
	vmw_bo_unreference(&old_buf);
	res->id = vcotbl->type;

	/* Release the pin acquired in vmw_bo_init */
	ttm_bo_unpin(bo);

	return 0;

out_map_new:
	ttm_bo_kunmap(&old_map);
out_wait:
	ttm_bo_unpin(bo);
	ttm_bo_unreserve(bo);
	vmw_bo_unreference(&buf);

	return ret;
}

/**
 * vmw_cotable_create - Cotable resource create callback
 *
 * @res: Pointer to a cotable resource.
 *
 * There is no separate create command for cotables, so this callback, which
 * is called before bind() in the validation sequence is instead used for two
 * things.
 * 1) Unscrub the cotable if it is scrubbed and still attached to a backup
 *    buffer.
 * 2) Resize the cotable if needed.
 */
static int vmw_cotable_create(struct vmw_resource *res)
{
	struct vmw_cotable *vcotbl = vmw_cotable(res);
	size_t new_size = res->backup_size;
	size_t needed_size;
	int ret;

	/* Check whether we need to resize the cotable */
	needed_size = (vcotbl->seen_entries + 1) * co_info[vcotbl->type].size;
	while (needed_size > new_size)
		new_size *= 2;

	if (likely(new_size <= res->backup_size)) {
		if (vcotbl->scrubbed && vmw_resource_mob_attached(res)) {
			ret = vmw_cotable_unscrub(res);
			if (ret)
				return ret;
		}
		res->id = vcotbl->type;
		return 0;
	}

	return vmw_cotable_resize(res, new_size);
}

/**
 * vmw_hw_cotable_destroy - Cotable hw_destroy callback
 *
 * @res: Pointer to a cotable resource.
 *
 * The final (part of resource destruction) destroy callback.
 */
static void vmw_hw_cotable_destroy(struct vmw_resource *res)
{
	(void) vmw_cotable_destroy(res);
}

/**
 * vmw_cotable_free - Cotable resource destructor
 *
 * @res: Pointer to a cotable resource.
 */
static void vmw_cotable_free(struct vmw_resource *res)
{
	kfree(res);
}

/**
 * vmw_cotable_alloc - Create a cotable resource
 *
 * @dev_priv: Pointer to a device private struct.
 * @ctx: Pointer to the context resource.
 * The cotable resource will not add a refcount.
 * @type: The cotable type.
 */
struct vmw_resource *vmw_cotable_alloc(struct vmw_private *dev_priv,
				       struct vmw_resource *ctx,
				       u32 type)
{
	struct vmw_cotable *vcotbl;
	int ret;
	u32 num_entries;

	vcotbl = kzalloc(sizeof(*vcotbl), GFP_KERNEL);
	if (unlikely(!vcotbl)) {
		ret = -ENOMEM;
		goto out_no_alloc;
	}

	ret = vmw_resource_init(dev_priv, &vcotbl->res, true,
				vmw_cotable_free, &vmw_cotable_func);
	if (unlikely(ret != 0))
		goto out_no_init;

	INIT_LIST_HEAD(&vcotbl->resource_list);
	vcotbl->res.id = type;
	vcotbl->res.backup_size = PAGE_SIZE;
	num_entries = PAGE_SIZE / co_info[type].size;
	if (num_entries < co_info[type].min_initial_entries) {
		vcotbl->res.backup_size = co_info[type].min_initial_entries *
			co_info[type].size;
		vcotbl->res.backup_size = PFN_ALIGN(vcotbl->res.backup_size);
	}

	vcotbl->scrubbed = true;
	vcotbl->seen_entries = -1;
	vcotbl->type = type;
	vcotbl->ctx = ctx;

	vcotbl->res.hw_destroy = vmw_hw_cotable_destroy;

	return &vcotbl->res;

out_no_init:
	kfree(vcotbl);
out_no_alloc:
	return ERR_PTR(ret);
}

/**
 * vmw_cotable_notify - Notify the cotable about an item creation
 *
 * @res: Pointer to a cotable resource.
 * @id: Item id.
 */
int vmw_cotable_notify(struct vmw_resource *res, int id)
{
	struct vmw_cotable *vcotbl = vmw_cotable(res);

	if (id < 0 || id >= SVGA_COTABLE_MAX_IDS) {
		DRM_ERROR("Illegal COTable id. Type is %u. Id is %d\n",
			  (unsigned) vcotbl->type, id);
		return -EINVAL;
	}

	if (vcotbl->seen_entries < id) {
		/* Trigger a call to create() on next validate */
		res->id = -1;
		vcotbl->seen_entries = id;
	}

	return 0;
}

/**
 * vmw_cotable_add_resource - add a view to the cotable's list of active views.
 *
 * @res: pointer struct vmw_resource representing the cotable.
 * @head: pointer to the struct list_head member of the resource, dedicated
 * to the cotable active resource list.
 */
void vmw_cotable_add_resource(struct vmw_resource *res, struct list_head *head)
{
	struct vmw_cotable *vcotbl =
		container_of(res, struct vmw_cotable, res);

	list_add_tail(head, &vcotbl->resource_list);
}
