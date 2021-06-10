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
#include "vmwgfx_binding.h"

struct vmw_user_context {
	struct ttm_base_object base;
	struct vmw_resource res;
	struct vmw_ctx_binding_state *cbs;
	struct vmw_cmdbuf_res_manager *man;
	struct vmw_resource *cotables[SVGA_COTABLE_MAX];
	spinlock_t cotable_lock;
	struct vmw_buffer_object *dx_query_mob;
};

static void vmw_user_context_free(struct vmw_resource *res);
static struct vmw_resource *
vmw_user_context_base_to_res(struct ttm_base_object *base);

static int vmw_gb_context_create(struct vmw_resource *res);
static int vmw_gb_context_bind(struct vmw_resource *res,
			       struct ttm_validate_buffer *val_buf);
static int vmw_gb_context_unbind(struct vmw_resource *res,
				 bool readback,
				 struct ttm_validate_buffer *val_buf);
static int vmw_gb_context_destroy(struct vmw_resource *res);
static int vmw_dx_context_create(struct vmw_resource *res);
static int vmw_dx_context_bind(struct vmw_resource *res,
			       struct ttm_validate_buffer *val_buf);
static int vmw_dx_context_unbind(struct vmw_resource *res,
				 bool readback,
				 struct ttm_validate_buffer *val_buf);
static int vmw_dx_context_destroy(struct vmw_resource *res);

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

static const struct vmw_res_func vmw_gb_context_func = {
	.res_type = vmw_res_context,
	.needs_backup = true,
	.may_evict = true,
	.prio = 3,
	.dirty_prio = 3,
	.type_name = "guest backed contexts",
	.backup_placement = &vmw_mob_placement,
	.create = vmw_gb_context_create,
	.destroy = vmw_gb_context_destroy,
	.bind = vmw_gb_context_bind,
	.unbind = vmw_gb_context_unbind
};

static const struct vmw_res_func vmw_dx_context_func = {
	.res_type = vmw_res_dx_context,
	.needs_backup = true,
	.may_evict = true,
	.prio = 3,
	.dirty_prio = 3,
	.type_name = "dx contexts",
	.backup_placement = &vmw_mob_placement,
	.create = vmw_dx_context_create,
	.destroy = vmw_dx_context_destroy,
	.bind = vmw_dx_context_bind,
	.unbind = vmw_dx_context_unbind
};

/*
 * Context management:
 */

static void vmw_context_cotables_unref(struct vmw_private *dev_priv,
				       struct vmw_user_context *uctx)
{
	struct vmw_resource *res;
	int i;
	u32 cotable_max = has_sm5_context(dev_priv) ?
		SVGA_COTABLE_MAX : SVGA_COTABLE_DX10_MAX;

	for (i = 0; i < cotable_max; ++i) {
		spin_lock(&uctx->cotable_lock);
		res = uctx->cotables[i];
		uctx->cotables[i] = NULL;
		spin_unlock(&uctx->cotable_lock);

		if (res)
			vmw_resource_unreference(&res);
	}
}

static void vmw_hw_context_destroy(struct vmw_resource *res)
{
	struct vmw_user_context *uctx =
		container_of(res, struct vmw_user_context, res);
	struct vmw_private *dev_priv = res->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDestroyContext body;
	} *cmd;


	if (res->func->destroy == vmw_gb_context_destroy ||
	    res->func->destroy == vmw_dx_context_destroy) {
		mutex_lock(&dev_priv->cmdbuf_mutex);
		vmw_cmdbuf_res_man_destroy(uctx->man);
		mutex_lock(&dev_priv->binding_mutex);
		vmw_binding_state_kill(uctx->cbs);
		(void) res->func->destroy(res);
		mutex_unlock(&dev_priv->binding_mutex);
		if (dev_priv->pinned_bo != NULL &&
		    !dev_priv->query_cid_valid)
			__vmw_execbuf_release_pinned_bo(dev_priv, NULL);
		mutex_unlock(&dev_priv->cmdbuf_mutex);
		vmw_context_cotables_unref(dev_priv, uctx);
		return;
	}

	vmw_execbuf_release_pinned_bo(dev_priv);
	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL))
		return;

	cmd->header.id = SVGA_3D_CMD_CONTEXT_DESTROY;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.cid = res->id;

	vmw_cmd_commit(dev_priv, sizeof(*cmd));
	vmw_fifo_resource_dec(dev_priv);
}

static int vmw_gb_context_init(struct vmw_private *dev_priv,
			       bool dx,
			       struct vmw_resource *res,
			       void (*res_free)(struct vmw_resource *res))
{
	int ret, i;
	struct vmw_user_context *uctx =
		container_of(res, struct vmw_user_context, res);

	res->backup_size = (dx ? sizeof(SVGADXContextMobFormat) :
			    SVGA3D_CONTEXT_DATA_SIZE);
	ret = vmw_resource_init(dev_priv, res, true,
				res_free,
				dx ? &vmw_dx_context_func :
				&vmw_gb_context_func);
	if (unlikely(ret != 0))
		goto out_err;

	if (dev_priv->has_mob) {
		uctx->man = vmw_cmdbuf_res_man_create(dev_priv);
		if (IS_ERR(uctx->man)) {
			ret = PTR_ERR(uctx->man);
			uctx->man = NULL;
			goto out_err;
		}
	}

	uctx->cbs = vmw_binding_state_alloc(dev_priv);
	if (IS_ERR(uctx->cbs)) {
		ret = PTR_ERR(uctx->cbs);
		goto out_err;
	}

	spin_lock_init(&uctx->cotable_lock);

	if (dx) {
		u32 cotable_max = has_sm5_context(dev_priv) ?
			SVGA_COTABLE_MAX : SVGA_COTABLE_DX10_MAX;
		for (i = 0; i < cotable_max; ++i) {
			uctx->cotables[i] = vmw_cotable_alloc(dev_priv,
							      &uctx->res, i);
			if (IS_ERR(uctx->cotables[i])) {
				ret = PTR_ERR(uctx->cotables[i]);
				goto out_cotables;
			}
		}
	}

	res->hw_destroy = vmw_hw_context_destroy;
	return 0;

out_cotables:
	vmw_context_cotables_unref(dev_priv, uctx);
out_err:
	if (res_free)
		res_free(res);
	else
		kfree(res);
	return ret;
}

static int vmw_context_init(struct vmw_private *dev_priv,
			    struct vmw_resource *res,
			    void (*res_free)(struct vmw_resource *res),
			    bool dx)
{
	int ret;

	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDefineContext body;
	} *cmd;

	if (dev_priv->has_mob)
		return vmw_gb_context_init(dev_priv, dx, res, res_free);

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

	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL)) {
		vmw_resource_unreference(&res);
		return -ENOMEM;
	}

	cmd->header.id = SVGA_3D_CMD_CONTEXT_DEFINE;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.cid = res->id;

	vmw_cmd_commit(dev_priv, sizeof(*cmd));
	vmw_fifo_resource_inc(dev_priv);
	res->hw_destroy = vmw_hw_context_destroy;
	return 0;

out_early:
	if (res_free == NULL)
		kfree(res);
	else
		res_free(res);
	return ret;
}


/*
 * GB context.
 */

static int vmw_gb_context_create(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	int ret;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDefineGBContext body;
	} *cmd;

	if (likely(res->id != -1))
		return 0;

	ret = vmw_resource_alloc_id(res);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed to allocate a context id.\n");
		goto out_no_id;
	}

	if (unlikely(res->id >= VMWGFX_NUM_GB_CONTEXT)) {
		ret = -EBUSY;
		goto out_no_fifo;
	}

	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL)) {
		ret = -ENOMEM;
		goto out_no_fifo;
	}

	cmd->header.id = SVGA_3D_CMD_DEFINE_GB_CONTEXT;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.cid = res->id;
	vmw_cmd_commit(dev_priv, sizeof(*cmd));
	vmw_fifo_resource_inc(dev_priv);

	return 0;

out_no_fifo:
	vmw_resource_release_id(res);
out_no_id:
	return ret;
}

static int vmw_gb_context_bind(struct vmw_resource *res,
			       struct ttm_validate_buffer *val_buf)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdBindGBContext body;
	} *cmd;
	struct ttm_buffer_object *bo = val_buf->bo;

	BUG_ON(bo->resource->mem_type != VMW_PL_MOB);

	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_BIND_GB_CONTEXT;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.cid = res->id;
	cmd->body.mobid = bo->resource->start;
	cmd->body.validContents = res->backup_dirty;
	res->backup_dirty = false;
	vmw_cmd_commit(dev_priv, sizeof(*cmd));

	return 0;
}

static int vmw_gb_context_unbind(struct vmw_resource *res,
				 bool readback,
				 struct ttm_validate_buffer *val_buf)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct ttm_buffer_object *bo = val_buf->bo;
	struct vmw_fence_obj *fence;
	struct vmw_user_context *uctx =
		container_of(res, struct vmw_user_context, res);

	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdReadbackGBContext body;
	} *cmd1;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdBindGBContext body;
	} *cmd2;
	uint32_t submit_size;
	uint8_t *cmd;


	BUG_ON(bo->resource->mem_type != VMW_PL_MOB);

	mutex_lock(&dev_priv->binding_mutex);
	vmw_binding_state_scrub(uctx->cbs);

	submit_size = sizeof(*cmd2) + (readback ? sizeof(*cmd1) : 0);

	cmd = VMW_CMD_RESERVE(dev_priv, submit_size);
	if (unlikely(cmd == NULL)) {
		mutex_unlock(&dev_priv->binding_mutex);
		return -ENOMEM;
	}

	cmd2 = (void *) cmd;
	if (readback) {
		cmd1 = (void *) cmd;
		cmd1->header.id = SVGA_3D_CMD_READBACK_GB_CONTEXT;
		cmd1->header.size = sizeof(cmd1->body);
		cmd1->body.cid = res->id;
		cmd2 = (void *) (&cmd1[1]);
	}
	cmd2->header.id = SVGA_3D_CMD_BIND_GB_CONTEXT;
	cmd2->header.size = sizeof(cmd2->body);
	cmd2->body.cid = res->id;
	cmd2->body.mobid = SVGA3D_INVALID_ID;

	vmw_cmd_commit(dev_priv, submit_size);
	mutex_unlock(&dev_priv->binding_mutex);

	/*
	 * Create a fence object and fence the backup buffer.
	 */

	(void) vmw_execbuf_fence_commands(NULL, dev_priv,
					  &fence, NULL);

	vmw_bo_fence_single(bo, fence);

	if (likely(fence != NULL))
		vmw_fence_obj_unreference(&fence);

	return 0;
}

static int vmw_gb_context_destroy(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDestroyGBContext body;
	} *cmd;

	if (likely(res->id == -1))
		return 0;

	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_DESTROY_GB_CONTEXT;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.cid = res->id;
	vmw_cmd_commit(dev_priv, sizeof(*cmd));
	if (dev_priv->query_cid == res->id)
		dev_priv->query_cid_valid = false;
	vmw_resource_release_id(res);
	vmw_fifo_resource_dec(dev_priv);

	return 0;
}

/*
 * DX context.
 */

static int vmw_dx_context_create(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	int ret;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXDefineContext body;
	} *cmd;

	if (likely(res->id != -1))
		return 0;

	ret = vmw_resource_alloc_id(res);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed to allocate a context id.\n");
		goto out_no_id;
	}

	if (unlikely(res->id >= VMWGFX_NUM_DXCONTEXT)) {
		ret = -EBUSY;
		goto out_no_fifo;
	}

	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL)) {
		ret = -ENOMEM;
		goto out_no_fifo;
	}

	cmd->header.id = SVGA_3D_CMD_DX_DEFINE_CONTEXT;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.cid = res->id;
	vmw_cmd_commit(dev_priv, sizeof(*cmd));
	vmw_fifo_resource_inc(dev_priv);

	return 0;

out_no_fifo:
	vmw_resource_release_id(res);
out_no_id:
	return ret;
}

static int vmw_dx_context_bind(struct vmw_resource *res,
			       struct ttm_validate_buffer *val_buf)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXBindContext body;
	} *cmd;
	struct ttm_buffer_object *bo = val_buf->bo;

	BUG_ON(bo->resource->mem_type != VMW_PL_MOB);

	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_DX_BIND_CONTEXT;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.cid = res->id;
	cmd->body.mobid = bo->resource->start;
	cmd->body.validContents = res->backup_dirty;
	res->backup_dirty = false;
	vmw_cmd_commit(dev_priv, sizeof(*cmd));


	return 0;
}

/**
 * vmw_dx_context_scrub_cotables - Scrub all bindings and
 * cotables from a context
 *
 * @ctx: Pointer to the context resource
 * @readback: Whether to save the otable contents on scrubbing.
 *
 * COtables must be unbound before their context, but unbinding requires
 * the backup buffer being reserved, whereas scrubbing does not.
 * This function scrubs all cotables of a context, potentially reading back
 * the contents into their backup buffers. However, scrubbing cotables
 * also makes the device context invalid, so scrub all bindings first so
 * that doesn't have to be done later with an invalid context.
 */
void vmw_dx_context_scrub_cotables(struct vmw_resource *ctx,
				   bool readback)
{
	struct vmw_user_context *uctx =
		container_of(ctx, struct vmw_user_context, res);
	u32 cotable_max = has_sm5_context(ctx->dev_priv) ?
		SVGA_COTABLE_MAX : SVGA_COTABLE_DX10_MAX;
	int i;

	vmw_binding_state_scrub(uctx->cbs);
	for (i = 0; i < cotable_max; ++i) {
		struct vmw_resource *res;

		/* Avoid racing with ongoing cotable destruction. */
		spin_lock(&uctx->cotable_lock);
		res = uctx->cotables[vmw_cotable_scrub_order[i]];
		if (res)
			res = vmw_resource_reference_unless_doomed(res);
		spin_unlock(&uctx->cotable_lock);
		if (!res)
			continue;

		WARN_ON(vmw_cotable_scrub(res, readback));
		vmw_resource_unreference(&res);
	}
}

static int vmw_dx_context_unbind(struct vmw_resource *res,
				 bool readback,
				 struct ttm_validate_buffer *val_buf)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct ttm_buffer_object *bo = val_buf->bo;
	struct vmw_fence_obj *fence;
	struct vmw_user_context *uctx =
		container_of(res, struct vmw_user_context, res);

	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXReadbackContext body;
	} *cmd1;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXBindContext body;
	} *cmd2;
	uint32_t submit_size;
	uint8_t *cmd;


	BUG_ON(bo->resource->mem_type != VMW_PL_MOB);

	mutex_lock(&dev_priv->binding_mutex);
	vmw_dx_context_scrub_cotables(res, readback);

	if (uctx->dx_query_mob && uctx->dx_query_mob->dx_query_ctx &&
	    readback) {
		WARN_ON(uctx->dx_query_mob->dx_query_ctx != res);
		if (vmw_query_readback_all(uctx->dx_query_mob))
			DRM_ERROR("Failed to read back query states\n");
	}

	submit_size = sizeof(*cmd2) + (readback ? sizeof(*cmd1) : 0);

	cmd = VMW_CMD_RESERVE(dev_priv, submit_size);
	if (unlikely(cmd == NULL)) {
		mutex_unlock(&dev_priv->binding_mutex);
		return -ENOMEM;
	}

	cmd2 = (void *) cmd;
	if (readback) {
		cmd1 = (void *) cmd;
		cmd1->header.id = SVGA_3D_CMD_DX_READBACK_CONTEXT;
		cmd1->header.size = sizeof(cmd1->body);
		cmd1->body.cid = res->id;
		cmd2 = (void *) (&cmd1[1]);
	}
	cmd2->header.id = SVGA_3D_CMD_DX_BIND_CONTEXT;
	cmd2->header.size = sizeof(cmd2->body);
	cmd2->body.cid = res->id;
	cmd2->body.mobid = SVGA3D_INVALID_ID;

	vmw_cmd_commit(dev_priv, submit_size);
	mutex_unlock(&dev_priv->binding_mutex);

	/*
	 * Create a fence object and fence the backup buffer.
	 */

	(void) vmw_execbuf_fence_commands(NULL, dev_priv,
					  &fence, NULL);

	vmw_bo_fence_single(bo, fence);

	if (likely(fence != NULL))
		vmw_fence_obj_unreference(&fence);

	return 0;
}

static int vmw_dx_context_destroy(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXDestroyContext body;
	} *cmd;

	if (likely(res->id == -1))
		return 0;

	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_DX_DESTROY_CONTEXT;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.cid = res->id;
	vmw_cmd_commit(dev_priv, sizeof(*cmd));
	if (dev_priv->query_cid == res->id)
		dev_priv->query_cid_valid = false;
	vmw_resource_release_id(res);
	vmw_fifo_resource_dec(dev_priv);

	return 0;
}

/*
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

	if (ctx->cbs)
		vmw_binding_state_free(ctx->cbs);

	(void) vmw_context_bind_dx_query(res, NULL);

	ttm_base_object_kfree(ctx, base);
	ttm_mem_global_free(vmw_mem_glob(dev_priv),
			    vmw_user_context_size);
}

/*
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

static int vmw_context_define(struct drm_device *dev, void *data,
			      struct drm_file *file_priv, bool dx)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct vmw_user_context *ctx;
	struct vmw_resource *res;
	struct vmw_resource *tmp;
	struct drm_vmw_context_arg *arg = (struct drm_vmw_context_arg *)data;
	struct ttm_object_file *tfile = vmw_fpriv(file_priv)->tfile;
	struct ttm_operation_ctx ttm_opt_ctx = {
		.interruptible = true,
		.no_wait_gpu = false
	};
	int ret;

	if (!has_sm4_context(dev_priv) && dx) {
		VMW_DEBUG_USER("DX contexts not supported by device.\n");
		return -EINVAL;
	}

	if (unlikely(vmw_user_context_size == 0))
		vmw_user_context_size = ttm_round_pot(sizeof(*ctx)) +
		  ((dev_priv->has_mob) ? vmw_cmdbuf_res_man_size() : 0) +
		  + VMW_IDA_ACC_SIZE + TTM_OBJ_EXTRA_SIZE;

	ret = ttm_mem_global_alloc(vmw_mem_glob(dev_priv),
				   vmw_user_context_size,
				   &ttm_opt_ctx);
	if (unlikely(ret != 0)) {
		if (ret != -ERESTARTSYS)
			DRM_ERROR("Out of graphics memory for context"
				  " creation.\n");
		goto out_ret;
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (unlikely(!ctx)) {
		ttm_mem_global_free(vmw_mem_glob(dev_priv),
				    vmw_user_context_size);
		ret = -ENOMEM;
		goto out_ret;
	}

	res = &ctx->res;
	ctx->base.shareable = false;
	ctx->base.tfile = NULL;

	/*
	 * From here on, the destructor takes over resource freeing.
	 */

	ret = vmw_context_init(dev_priv, res, vmw_user_context_free, dx);
	if (unlikely(ret != 0))
		goto out_ret;

	tmp = vmw_resource_reference(&ctx->res);
	ret = ttm_base_object_init(tfile, &ctx->base, false, VMW_RES_CONTEXT,
				   &vmw_user_context_base_release, NULL);

	if (unlikely(ret != 0)) {
		vmw_resource_unreference(&tmp);
		goto out_err;
	}

	arg->cid = ctx->base.handle;
out_err:
	vmw_resource_unreference(&res);
out_ret:
	return ret;
}

int vmw_context_define_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv)
{
	return vmw_context_define(dev, data, file_priv, false);
}

int vmw_extended_context_define_ioctl(struct drm_device *dev, void *data,
				      struct drm_file *file_priv)
{
	union drm_vmw_extended_context_arg *arg = (typeof(arg)) data;
	struct drm_vmw_context_arg *rep = &arg->rep;

	switch (arg->req) {
	case drm_vmw_context_legacy:
		return vmw_context_define(dev, rep, file_priv, false);
	case drm_vmw_context_dx:
		return vmw_context_define(dev, rep, file_priv, true);
	default:
		break;
	}
	return -EINVAL;
}

/**
 * vmw_context_binding_list - Return a list of context bindings
 *
 * @ctx: The context resource
 *
 * Returns the current list of bindings of the given context. Note that
 * this list becomes stale as soon as the dev_priv::binding_mutex is unlocked.
 */
struct list_head *vmw_context_binding_list(struct vmw_resource *ctx)
{
	struct vmw_user_context *uctx =
		container_of(ctx, struct vmw_user_context, res);

	return vmw_binding_state_list(uctx->cbs);
}

struct vmw_cmdbuf_res_manager *vmw_context_res_man(struct vmw_resource *ctx)
{
	return container_of(ctx, struct vmw_user_context, res)->man;
}

struct vmw_resource *vmw_context_cotable(struct vmw_resource *ctx,
					 SVGACOTableType cotable_type)
{
	u32 cotable_max = has_sm5_context(ctx->dev_priv) ?
		SVGA_COTABLE_MAX : SVGA_COTABLE_DX10_MAX;

	if (cotable_type >= cotable_max)
		return ERR_PTR(-EINVAL);

	return container_of(ctx, struct vmw_user_context, res)->
		cotables[cotable_type];
}

/**
 * vmw_context_binding_state -
 * Return a pointer to a context binding state structure
 *
 * @ctx: The context resource
 *
 * Returns the current state of bindings of the given context. Note that
 * this state becomes stale as soon as the dev_priv::binding_mutex is unlocked.
 */
struct vmw_ctx_binding_state *
vmw_context_binding_state(struct vmw_resource *ctx)
{
	return container_of(ctx, struct vmw_user_context, res)->cbs;
}

/**
 * vmw_context_bind_dx_query -
 * Sets query MOB for the context.  If @mob is NULL, then this function will
 * remove the association between the MOB and the context.  This function
 * assumes the binding_mutex is held.
 *
 * @ctx_res: The context resource
 * @mob: a reference to the query MOB
 *
 * Returns -EINVAL if a MOB has already been set and does not match the one
 * specified in the parameter.  0 otherwise.
 */
int vmw_context_bind_dx_query(struct vmw_resource *ctx_res,
			      struct vmw_buffer_object *mob)
{
	struct vmw_user_context *uctx =
		container_of(ctx_res, struct vmw_user_context, res);

	if (mob == NULL) {
		if (uctx->dx_query_mob) {
			uctx->dx_query_mob->dx_query_ctx = NULL;
			vmw_bo_unreference(&uctx->dx_query_mob);
			uctx->dx_query_mob = NULL;
		}

		return 0;
	}

	/* Can only have one MOB per context for queries */
	if (uctx->dx_query_mob && uctx->dx_query_mob != mob)
		return -EINVAL;

	mob->dx_query_ctx  = ctx_res;

	if (!uctx->dx_query_mob)
		uctx->dx_query_mob = vmw_bo_reference(mob);

	return 0;
}

/**
 * vmw_context_get_dx_query_mob - Returns non-counted reference to DX query mob
 *
 * @ctx_res: The context resource
 */
struct vmw_buffer_object *
vmw_context_get_dx_query_mob(struct vmw_resource *ctx_res)
{
	struct vmw_user_context *uctx =
		container_of(ctx_res, struct vmw_user_context, res);

	return uctx->dx_query_mob;
}
