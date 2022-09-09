// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright © 2018-2019 VMware, Inc., Palo Alto, CA., USA
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

#include <drm/ttm/ttm_placement.h>

#include "vmwgfx_drv.h"
#include "vmwgfx_resource_priv.h"
#include "vmwgfx_binding.h"

/**
 * struct vmw_dx_streamoutput - Streamoutput resource metadata.
 * @res: Base resource struct.
 * @ctx: Non-refcounted context to which @res belong.
 * @cotable: Refcounted cotable holding this Streamoutput.
 * @cotable_head: List head for cotable-so_res list.
 * @id: User-space provided identifier.
 * @size: User-space provided mob size.
 * @committed: Whether streamoutput is actually created or pending creation.
 */
struct vmw_dx_streamoutput {
	struct vmw_resource res;
	struct vmw_resource *ctx;
	struct vmw_resource *cotable;
	struct list_head cotable_head;
	u32 id;
	u32 size;
	bool committed;
};

static int vmw_dx_streamoutput_create(struct vmw_resource *res);
static int vmw_dx_streamoutput_bind(struct vmw_resource *res,
				    struct ttm_validate_buffer *val_buf);
static int vmw_dx_streamoutput_unbind(struct vmw_resource *res, bool readback,
				      struct ttm_validate_buffer *val_buf);
static void vmw_dx_streamoutput_commit_notify(struct vmw_resource *res,
					      enum vmw_cmdbuf_res_state state);

static const struct vmw_res_func vmw_dx_streamoutput_func = {
	.res_type = vmw_res_streamoutput,
	.needs_backup = true,
	.may_evict = false,
	.type_name = "DX streamoutput",
	.backup_placement = &vmw_mob_placement,
	.create = vmw_dx_streamoutput_create,
	.destroy = NULL, /* Command buffer managed resource. */
	.bind = vmw_dx_streamoutput_bind,
	.unbind = vmw_dx_streamoutput_unbind,
	.commit_notify = vmw_dx_streamoutput_commit_notify,
};

static inline struct vmw_dx_streamoutput *
vmw_res_to_dx_streamoutput(struct vmw_resource *res)
{
	return container_of(res, struct vmw_dx_streamoutput, res);
}

/**
 * vmw_dx_streamoutput_unscrub - Reattach the MOB to streamoutput.
 * @res: The streamoutput resource.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int vmw_dx_streamoutput_unscrub(struct vmw_resource *res)
{
	struct vmw_dx_streamoutput *so = vmw_res_to_dx_streamoutput(res);
	struct vmw_private *dev_priv = res->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXBindStreamOutput body;
	} *cmd;

	if (!list_empty(&so->cotable_head) || !so->committed )
		return 0;

	cmd = VMW_CMD_CTX_RESERVE(dev_priv, sizeof(*cmd), so->ctx->id);
	if (!cmd)
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_DX_BIND_STREAMOUTPUT;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.soid = so->id;
	cmd->body.mobid = res->backup->base.resource->start;
	cmd->body.offsetInBytes = res->backup_offset;
	cmd->body.sizeInBytes = so->size;
	vmw_cmd_commit(dev_priv, sizeof(*cmd));

	vmw_cotable_add_resource(so->cotable, &so->cotable_head);

	return 0;
}

static int vmw_dx_streamoutput_create(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct vmw_dx_streamoutput *so = vmw_res_to_dx_streamoutput(res);
	int ret = 0;

	WARN_ON_ONCE(!so->committed);

	if (vmw_resource_mob_attached(res)) {
		mutex_lock(&dev_priv->binding_mutex);
		ret = vmw_dx_streamoutput_unscrub(res);
		mutex_unlock(&dev_priv->binding_mutex);
	}

	res->id = so->id;

	return ret;
}

static int vmw_dx_streamoutput_bind(struct vmw_resource *res,
				    struct ttm_validate_buffer *val_buf)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct ttm_buffer_object *bo = val_buf->bo;
	int ret;

	if (WARN_ON(bo->resource->mem_type != VMW_PL_MOB))
		return -EINVAL;

	mutex_lock(&dev_priv->binding_mutex);
	ret = vmw_dx_streamoutput_unscrub(res);
	mutex_unlock(&dev_priv->binding_mutex);

	return ret;
}

/**
 * vmw_dx_streamoutput_scrub - Unbind the MOB from streamoutput.
 * @res: The streamoutput resource.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int vmw_dx_streamoutput_scrub(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct vmw_dx_streamoutput *so = vmw_res_to_dx_streamoutput(res);
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXBindStreamOutput body;
	} *cmd;

	if (list_empty(&so->cotable_head))
		return 0;

	WARN_ON_ONCE(!so->committed);

	cmd = VMW_CMD_CTX_RESERVE(dev_priv, sizeof(*cmd), so->ctx->id);
	if (!cmd)
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_DX_BIND_STREAMOUTPUT;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.soid = res->id;
	cmd->body.mobid = SVGA3D_INVALID_ID;
	cmd->body.offsetInBytes = 0;
	cmd->body.sizeInBytes = so->size;
	vmw_cmd_commit(dev_priv, sizeof(*cmd));

	res->id = -1;
	list_del_init(&so->cotable_head);

	return 0;
}

static int vmw_dx_streamoutput_unbind(struct vmw_resource *res, bool readback,
				      struct ttm_validate_buffer *val_buf)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct vmw_fence_obj *fence;
	int ret;

	if (WARN_ON(res->backup->base.resource->mem_type != VMW_PL_MOB))
		return -EINVAL;

	mutex_lock(&dev_priv->binding_mutex);
	ret = vmw_dx_streamoutput_scrub(res);
	mutex_unlock(&dev_priv->binding_mutex);

	if (ret)
		return ret;

	(void) vmw_execbuf_fence_commands(NULL, dev_priv, &fence, NULL);
	vmw_bo_fence_single(val_buf->bo, fence);

	if (fence != NULL)
		vmw_fence_obj_unreference(&fence);

	return 0;
}

static void vmw_dx_streamoutput_commit_notify(struct vmw_resource *res,
					   enum vmw_cmdbuf_res_state state)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct vmw_dx_streamoutput *so = vmw_res_to_dx_streamoutput(res);

	if (state == VMW_CMDBUF_RES_ADD) {
		mutex_lock(&dev_priv->binding_mutex);
		vmw_cotable_add_resource(so->cotable, &so->cotable_head);
		so->committed = true;
		res->id = so->id;
		mutex_unlock(&dev_priv->binding_mutex);
	} else {
		mutex_lock(&dev_priv->binding_mutex);
		list_del_init(&so->cotable_head);
		so->committed = false;
		res->id = -1;
		mutex_unlock(&dev_priv->binding_mutex);
	}
}

/**
 * vmw_dx_streamoutput_lookup - Do a streamoutput resource lookup by user key.
 * @man: Command buffer managed resource manager for current context.
 * @user_key: User-space identifier for lookup.
 *
 * Return: Valid refcounted vmw_resource on success, error pointer on failure.
 */
struct vmw_resource *
vmw_dx_streamoutput_lookup(struct vmw_cmdbuf_res_manager *man,
			   u32 user_key)
{
	return vmw_cmdbuf_res_lookup(man, vmw_cmdbuf_res_streamoutput,
				     user_key);
}

static void vmw_dx_streamoutput_res_free(struct vmw_resource *res)
{
	struct vmw_dx_streamoutput *so = vmw_res_to_dx_streamoutput(res);

	vmw_resource_unreference(&so->cotable);
	kfree(so);
}

static void vmw_dx_streamoutput_hw_destroy(struct vmw_resource *res)
{
	/* Destroyed by user-space cmd buf or as part of context takedown. */
	res->id = -1;
}

/**
 * vmw_dx_streamoutput_add - Add a streamoutput as a cmd buf managed resource.
 * @man: Command buffer managed resource manager for current context.
 * @ctx: Pointer to context resource.
 * @user_key: The identifier for this streamoutput.
 * @list: The list of staged command buffer managed resources.
 *
 * Return: 0 on success, negative error code on failure.
 */
int vmw_dx_streamoutput_add(struct vmw_cmdbuf_res_manager *man,
			    struct vmw_resource *ctx, u32 user_key,
			    struct list_head *list)
{
	struct vmw_dx_streamoutput *so;
	struct vmw_resource *res;
	struct vmw_private *dev_priv = ctx->dev_priv;
	int ret;

	so = kmalloc(sizeof(*so), GFP_KERNEL);
	if (!so) {
		return -ENOMEM;
	}

	res = &so->res;
	so->ctx = ctx;
	so->cotable = vmw_resource_reference
		(vmw_context_cotable(ctx, SVGA_COTABLE_STREAMOUTPUT));
	so->id = user_key;
	so->committed = false;
	INIT_LIST_HEAD(&so->cotable_head);
	ret = vmw_resource_init(dev_priv, res, true,
				vmw_dx_streamoutput_res_free,
				&vmw_dx_streamoutput_func);
	if (ret)
		goto out_resource_init;

	ret = vmw_cmdbuf_res_add(man, vmw_cmdbuf_res_streamoutput, user_key,
				 res, list);
	if (ret)
		goto out_resource_init;

	res->id = so->id;
	res->hw_destroy = vmw_dx_streamoutput_hw_destroy;

out_resource_init:
	vmw_resource_unreference(&res);

	return ret;
}

/**
 * vmw_dx_streamoutput_set_size - Sets streamoutput mob size in res struct.
 * @res: The streamoutput res for which need to set size.
 * @size: The size provided by user-space to set.
 */
void vmw_dx_streamoutput_set_size(struct vmw_resource *res, u32 size)
{
	struct vmw_dx_streamoutput *so = vmw_res_to_dx_streamoutput(res);

	so->size = size;
}

/**
 * vmw_dx_streamoutput_remove - Stage streamoutput for removal.
 * @man: Command buffer managed resource manager for current context.
 * @user_key: The identifier for this streamoutput.
 * @list: The list of staged command buffer managed resources.
 *
 * Return: 0 on success, negative error code on failure.
 */
int vmw_dx_streamoutput_remove(struct vmw_cmdbuf_res_manager *man,
			       u32 user_key,
			       struct list_head *list)
{
	struct vmw_resource *r;

	return vmw_cmdbuf_res_remove(man, vmw_cmdbuf_res_streamoutput,
				     (u32)user_key, list, &r);
}

/**
 * vmw_dx_streamoutput_cotable_list_scrub - cotable unbind_func callback.
 * @dev_priv: Device private.
 * @list: The list of cotable resources.
 * @readback: Whether the call was part of a readback unbind.
 */
void vmw_dx_streamoutput_cotable_list_scrub(struct vmw_private *dev_priv,
					    struct list_head *list,
					    bool readback)
{
	struct vmw_dx_streamoutput *entry, *next;

	lockdep_assert_held_once(&dev_priv->binding_mutex);

	list_for_each_entry_safe(entry, next, list, cotable_head) {
		WARN_ON(vmw_dx_streamoutput_scrub(&entry->res));
		if (!readback)
			entry->committed =false;
	}
}
