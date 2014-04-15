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
#include "ttm/ttm_placement.h"

struct vmw_user_context {
	struct ttm_base_object base;
	struct vmw_resource res;
	struct vmw_ctx_binding_state cbs;
};



typedef int (*vmw_scrub_func)(struct vmw_ctx_bindinfo *, bool);

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
static int vmw_context_scrub_shader(struct vmw_ctx_bindinfo *bi, bool rebind);
static int vmw_context_scrub_render_target(struct vmw_ctx_bindinfo *bi,
					   bool rebind);
static int vmw_context_scrub_texture(struct vmw_ctx_bindinfo *bi, bool rebind);
static void vmw_context_binding_state_scrub(struct vmw_ctx_binding_state *cbs);
static void vmw_context_binding_state_kill(struct vmw_ctx_binding_state *cbs);
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
	.type_name = "guest backed contexts",
	.backup_placement = &vmw_mob_placement,
	.create = vmw_gb_context_create,
	.destroy = vmw_gb_context_destroy,
	.bind = vmw_gb_context_bind,
	.unbind = vmw_gb_context_unbind
};

static const vmw_scrub_func vmw_scrub_funcs[vmw_ctx_binding_max] = {
	[vmw_ctx_binding_shader] = vmw_context_scrub_shader,
	[vmw_ctx_binding_rt] = vmw_context_scrub_render_target,
	[vmw_ctx_binding_tex] = vmw_context_scrub_texture };

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


	if (res->func->destroy == vmw_gb_context_destroy) {
		mutex_lock(&dev_priv->cmdbuf_mutex);
		mutex_lock(&dev_priv->binding_mutex);
		(void) vmw_context_binding_state_kill
			(&container_of(res, struct vmw_user_context, res)->cbs);
		(void) vmw_gb_context_destroy(res);
		mutex_unlock(&dev_priv->binding_mutex);
		if (dev_priv->pinned_bo != NULL &&
		    !dev_priv->query_cid_valid)
			__vmw_execbuf_release_pinned_bo(dev_priv, NULL);
		mutex_unlock(&dev_priv->cmdbuf_mutex);
		return;
	}

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

static int vmw_gb_context_init(struct vmw_private *dev_priv,
			       struct vmw_resource *res,
			       void (*res_free) (struct vmw_resource *res))
{
	int ret;
	struct vmw_user_context *uctx =
		container_of(res, struct vmw_user_context, res);

	ret = vmw_resource_init(dev_priv, res, true,
				res_free, &vmw_gb_context_func);
	res->backup_size = SVGA3D_CONTEXT_DATA_SIZE;

	if (unlikely(ret != 0)) {
		if (res_free)
			res_free(res);
		else
			kfree(res);
		return ret;
	}

	memset(&uctx->cbs, 0, sizeof(uctx->cbs));
	INIT_LIST_HEAD(&uctx->cbs.list);

	vmw_resource_activate(res, vmw_hw_context_destroy);
	return 0;
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

	if (dev_priv->has_mob)
		return vmw_gb_context_init(dev_priv, res, res_free);

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

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Failed reserving FIFO space for context "
			  "creation.\n");
		ret = -ENOMEM;
		goto out_no_fifo;
	}

	cmd->header.id = SVGA_3D_CMD_DEFINE_GB_CONTEXT;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.cid = res->id;
	vmw_fifo_commit(dev_priv, sizeof(*cmd));
	(void) vmw_3d_resource_inc(dev_priv, false);

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

	BUG_ON(bo->mem.mem_type != VMW_PL_MOB);

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Failed reserving FIFO space for context "
			  "binding.\n");
		return -ENOMEM;
	}

	cmd->header.id = SVGA_3D_CMD_BIND_GB_CONTEXT;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.cid = res->id;
	cmd->body.mobid = bo->mem.start;
	cmd->body.validContents = res->backup_dirty;
	res->backup_dirty = false;
	vmw_fifo_commit(dev_priv, sizeof(*cmd));

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


	BUG_ON(bo->mem.mem_type != VMW_PL_MOB);

	mutex_lock(&dev_priv->binding_mutex);
	vmw_context_binding_state_scrub(&uctx->cbs);

	submit_size = sizeof(*cmd2) + (readback ? sizeof(*cmd1) : 0);

	cmd = vmw_fifo_reserve(dev_priv, submit_size);
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Failed reserving FIFO space for context "
			  "unbinding.\n");
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

	vmw_fifo_commit(dev_priv, submit_size);
	mutex_unlock(&dev_priv->binding_mutex);

	/*
	 * Create a fence object and fence the backup buffer.
	 */

	(void) vmw_execbuf_fence_commands(NULL, dev_priv,
					  &fence, NULL);

	vmw_fence_single_bo(bo, fence);

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

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Failed reserving FIFO space for context "
			  "destruction.\n");
		return -ENOMEM;
	}

	cmd->header.id = SVGA_3D_CMD_DESTROY_GB_CONTEXT;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.cid = res->id;
	vmw_fifo_commit(dev_priv, sizeof(*cmd));
	if (dev_priv->query_cid == res->id)
		dev_priv->query_cid_valid = false;
	vmw_resource_release_id(res);
	vmw_3d_resource_dec(dev_priv, false);

	return 0;
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
	int ret;


	/*
	 * Approximate idr memory usage with 128 bytes. It will be limited
	 * by maximum number_of contexts anyway.
	 */

	if (unlikely(vmw_user_context_size == 0))
		vmw_user_context_size = ttm_round_pot(sizeof(*ctx)) + 128;

	ret = ttm_read_lock(&dev_priv->reservation_sem, true);
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
	ttm_read_unlock(&dev_priv->reservation_sem);
	return ret;

}

/**
 * vmw_context_scrub_shader - scrub a shader binding from a context.
 *
 * @bi: single binding information.
 * @rebind: Whether to issue a bind instead of scrub command.
 */
static int vmw_context_scrub_shader(struct vmw_ctx_bindinfo *bi, bool rebind)
{
	struct vmw_private *dev_priv = bi->ctx->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdSetShader body;
	} *cmd;

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Failed reserving FIFO space for shader "
			  "unbinding.\n");
		return -ENOMEM;
	}

	cmd->header.id = SVGA_3D_CMD_SET_SHADER;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.cid = bi->ctx->id;
	cmd->body.type = bi->i1.shader_type;
	cmd->body.shid = ((rebind) ? bi->res->id : SVGA3D_INVALID_ID);
	vmw_fifo_commit(dev_priv, sizeof(*cmd));

	return 0;
}

/**
 * vmw_context_scrub_render_target - scrub a render target binding
 * from a context.
 *
 * @bi: single binding information.
 * @rebind: Whether to issue a bind instead of scrub command.
 */
static int vmw_context_scrub_render_target(struct vmw_ctx_bindinfo *bi,
					   bool rebind)
{
	struct vmw_private *dev_priv = bi->ctx->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdSetRenderTarget body;
	} *cmd;

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Failed reserving FIFO space for render target "
			  "unbinding.\n");
		return -ENOMEM;
	}

	cmd->header.id = SVGA_3D_CMD_SETRENDERTARGET;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.cid = bi->ctx->id;
	cmd->body.type = bi->i1.rt_type;
	cmd->body.target.sid = ((rebind) ? bi->res->id : SVGA3D_INVALID_ID);
	cmd->body.target.face = 0;
	cmd->body.target.mipmap = 0;
	vmw_fifo_commit(dev_priv, sizeof(*cmd));

	return 0;
}

/**
 * vmw_context_scrub_texture - scrub a texture binding from a context.
 *
 * @bi: single binding information.
 * @rebind: Whether to issue a bind instead of scrub command.
 *
 * TODO: Possibly complement this function with a function that takes
 * a list of texture bindings and combines them to a single command.
 */
static int vmw_context_scrub_texture(struct vmw_ctx_bindinfo *bi,
				     bool rebind)
{
	struct vmw_private *dev_priv = bi->ctx->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		struct {
			SVGA3dCmdSetTextureState c;
			SVGA3dTextureState s1;
		} body;
	} *cmd;

	cmd = vmw_fifo_reserve(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Failed reserving FIFO space for texture "
			  "unbinding.\n");
		return -ENOMEM;
	}


	cmd->header.id = SVGA_3D_CMD_SETTEXTURESTATE;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.c.cid = bi->ctx->id;
	cmd->body.s1.stage = bi->i1.texture_stage;
	cmd->body.s1.name = SVGA3D_TS_BIND_TEXTURE;
	cmd->body.s1.value = ((rebind) ? bi->res->id : SVGA3D_INVALID_ID);
	vmw_fifo_commit(dev_priv, sizeof(*cmd));

	return 0;
}

/**
 * vmw_context_binding_drop: Stop tracking a context binding
 *
 * @cb: Pointer to binding tracker storage.
 *
 * Stops tracking a context binding, and re-initializes its storage.
 * Typically used when the context binding is replaced with a binding to
 * another (or the same, for that matter) resource.
 */
static void vmw_context_binding_drop(struct vmw_ctx_binding *cb)
{
	list_del(&cb->ctx_list);
	if (!list_empty(&cb->res_list))
		list_del(&cb->res_list);
	cb->bi.ctx = NULL;
}

/**
 * vmw_context_binding_add: Start tracking a context binding
 *
 * @cbs: Pointer to the context binding state tracker.
 * @bi: Information about the binding to track.
 *
 * Performs basic checks on the binding to make sure arguments are within
 * bounds and then starts tracking the binding in the context binding
 * state structure @cbs.
 */
int vmw_context_binding_add(struct vmw_ctx_binding_state *cbs,
			    const struct vmw_ctx_bindinfo *bi)
{
	struct vmw_ctx_binding *loc;

	switch (bi->bt) {
	case vmw_ctx_binding_rt:
		if (unlikely((unsigned)bi->i1.rt_type >= SVGA3D_RT_MAX)) {
			DRM_ERROR("Illegal render target type %u.\n",
				  (unsigned) bi->i1.rt_type);
			return -EINVAL;
		}
		loc = &cbs->render_targets[bi->i1.rt_type];
		break;
	case vmw_ctx_binding_tex:
		if (unlikely((unsigned)bi->i1.texture_stage >=
			     SVGA3D_NUM_TEXTURE_UNITS)) {
			DRM_ERROR("Illegal texture/sampler unit %u.\n",
				  (unsigned) bi->i1.texture_stage);
			return -EINVAL;
		}
		loc = &cbs->texture_units[bi->i1.texture_stage];
		break;
	case vmw_ctx_binding_shader:
		if (unlikely((unsigned)bi->i1.shader_type >=
			     SVGA3D_SHADERTYPE_MAX)) {
			DRM_ERROR("Illegal shader type %u.\n",
				  (unsigned) bi->i1.shader_type);
			return -EINVAL;
		}
		loc = &cbs->shaders[bi->i1.shader_type];
		break;
	default:
		BUG();
	}

	if (loc->bi.ctx != NULL)
		vmw_context_binding_drop(loc);

	loc->bi = *bi;
	loc->bi.scrubbed = false;
	list_add_tail(&loc->ctx_list, &cbs->list);
	INIT_LIST_HEAD(&loc->res_list);

	return 0;
}

/**
 * vmw_context_binding_transfer: Transfer a context binding tracking entry.
 *
 * @cbs: Pointer to the persistent context binding state tracker.
 * @bi: Information about the binding to track.
 *
 */
static void vmw_context_binding_transfer(struct vmw_ctx_binding_state *cbs,
					 const struct vmw_ctx_bindinfo *bi)
{
	struct vmw_ctx_binding *loc;

	switch (bi->bt) {
	case vmw_ctx_binding_rt:
		loc = &cbs->render_targets[bi->i1.rt_type];
		break;
	case vmw_ctx_binding_tex:
		loc = &cbs->texture_units[bi->i1.texture_stage];
		break;
	case vmw_ctx_binding_shader:
		loc = &cbs->shaders[bi->i1.shader_type];
		break;
	default:
		BUG();
	}

	if (loc->bi.ctx != NULL)
		vmw_context_binding_drop(loc);

	if (bi->res != NULL) {
		loc->bi = *bi;
		list_add_tail(&loc->ctx_list, &cbs->list);
		list_add_tail(&loc->res_list, &bi->res->binding_head);
	}
}

/**
 * vmw_context_binding_kill - Kill a binding on the device
 * and stop tracking it.
 *
 * @cb: Pointer to binding tracker storage.
 *
 * Emits FIFO commands to scrub a binding represented by @cb.
 * Then stops tracking the binding and re-initializes its storage.
 */
static void vmw_context_binding_kill(struct vmw_ctx_binding *cb)
{
	if (!cb->bi.scrubbed) {
		(void) vmw_scrub_funcs[cb->bi.bt](&cb->bi, false);
		cb->bi.scrubbed = true;
	}
	vmw_context_binding_drop(cb);
}

/**
 * vmw_context_binding_state_kill - Kill all bindings associated with a
 * struct vmw_ctx_binding state structure, and re-initialize the structure.
 *
 * @cbs: Pointer to the context binding state tracker.
 *
 * Emits commands to scrub all bindings associated with the
 * context binding state tracker. Then re-initializes the whole structure.
 */
static void vmw_context_binding_state_kill(struct vmw_ctx_binding_state *cbs)
{
	struct vmw_ctx_binding *entry, *next;

	list_for_each_entry_safe(entry, next, &cbs->list, ctx_list)
		vmw_context_binding_kill(entry);
}

/**
 * vmw_context_binding_state_scrub - Scrub all bindings associated with a
 * struct vmw_ctx_binding state structure.
 *
 * @cbs: Pointer to the context binding state tracker.
 *
 * Emits commands to scrub all bindings associated with the
 * context binding state tracker.
 */
static void vmw_context_binding_state_scrub(struct vmw_ctx_binding_state *cbs)
{
	struct vmw_ctx_binding *entry;

	list_for_each_entry(entry, &cbs->list, ctx_list) {
		if (!entry->bi.scrubbed) {
			(void) vmw_scrub_funcs[entry->bi.bt](&entry->bi, false);
			entry->bi.scrubbed = true;
		}
	}
}

/**
 * vmw_context_binding_res_list_kill - Kill all bindings on a
 * resource binding list
 *
 * @head: list head of resource binding list
 *
 * Kills all bindings associated with a specific resource. Typically
 * called before the resource is destroyed.
 */
void vmw_context_binding_res_list_kill(struct list_head *head)
{
	struct vmw_ctx_binding *entry, *next;

	list_for_each_entry_safe(entry, next, head, res_list)
		vmw_context_binding_kill(entry);
}

/**
 * vmw_context_binding_res_list_scrub - Scrub all bindings on a
 * resource binding list
 *
 * @head: list head of resource binding list
 *
 * Scrub all bindings associated with a specific resource. Typically
 * called before the resource is evicted.
 */
void vmw_context_binding_res_list_scrub(struct list_head *head)
{
	struct vmw_ctx_binding *entry;

	list_for_each_entry(entry, head, res_list) {
		if (!entry->bi.scrubbed) {
			(void) vmw_scrub_funcs[entry->bi.bt](&entry->bi, false);
			entry->bi.scrubbed = true;
		}
	}
}

/**
 * vmw_context_binding_state_transfer - Commit staged binding info
 *
 * @ctx: Pointer to context to commit the staged binding info to.
 * @from: Staged binding info built during execbuf.
 *
 * Transfers binding info from a temporary structure to the persistent
 * structure in the context. This can be done once commands
 */
void vmw_context_binding_state_transfer(struct vmw_resource *ctx,
					struct vmw_ctx_binding_state *from)
{
	struct vmw_user_context *uctx =
		container_of(ctx, struct vmw_user_context, res);
	struct vmw_ctx_binding *entry, *next;

	list_for_each_entry_safe(entry, next, &from->list, ctx_list)
		vmw_context_binding_transfer(&uctx->cbs, &entry->bi);
}

/**
 * vmw_context_rebind_all - Rebind all scrubbed bindings of a context
 *
 * @ctx: The context resource
 *
 * Walks through the context binding list and rebinds all scrubbed
 * resources.
 */
int vmw_context_rebind_all(struct vmw_resource *ctx)
{
	struct vmw_ctx_binding *entry;
	struct vmw_user_context *uctx =
		container_of(ctx, struct vmw_user_context, res);
	struct vmw_ctx_binding_state *cbs = &uctx->cbs;
	int ret;

	list_for_each_entry(entry, &cbs->list, ctx_list) {
		if (likely(!entry->bi.scrubbed))
			continue;

		if (WARN_ON(entry->bi.res == NULL || entry->bi.res->id ==
			    SVGA3D_INVALID_ID))
			continue;

		ret = vmw_scrub_funcs[entry->bi.bt](&entry->bi, true);
		if (unlikely(ret != 0))
			return ret;

		entry->bi.scrubbed = false;
	}

	return 0;
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
	return &(container_of(ctx, struct vmw_user_context, res)->cbs.list);
}
