// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
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

#include "vmwgfx_drv.h"
#include "vmwgfx_resource_priv.h"
#include "vmwgfx_so.h"
#include "vmwgfx_binding.h"

/*
 * The currently only reason we need to keep track of views is that if we
 * destroy a hardware surface, all views pointing to it must also be destroyed,
 * otherwise the device will error.
 * So in particuar if a surface is evicted, we must destroy all views pointing
 * to it, and all context bindings of that view. Similarly we must restore
 * the view bindings, views and surfaces pointed to by the views when a
 * context is referenced in the command stream.
 */

/**
 * struct vmw_view - view metadata
 *
 * @res: The struct vmw_resource we derive from
 * @ctx: Non-refcounted pointer to the context this view belongs to.
 * @srf: Refcounted pointer to the surface pointed to by this view.
 * @cotable: Refcounted pointer to the cotable holding this view.
 * @srf_head: List head for the surface-to-view list.
 * @cotable_head: List head for the cotable-to_view list.
 * @view_type: View type.
 * @view_id: User-space per context view id. Currently used also as per
 * context device view id.
 * @cmd_size: Size of the SVGA3D define view command that we've copied from the
 * command stream.
 * @committed: Whether the view is actually created or pending creation at the
 * device level.
 * @cmd: The SVGA3D define view command copied from the command stream.
 */
struct vmw_view {
	struct rcu_head rcu;
	struct vmw_resource res;
	struct vmw_resource *ctx;      /* Immutable */
	struct vmw_resource *srf;      /* Immutable */
	struct vmw_resource *cotable;  /* Immutable */
	struct list_head srf_head;     /* Protected by binding_mutex */
	struct list_head cotable_head; /* Protected by binding_mutex */
	unsigned view_type;            /* Immutable */
	unsigned view_id;              /* Immutable */
	u32 cmd_size;                  /* Immutable */
	bool committed;                /* Protected by binding_mutex */
	u32 cmd[1];                    /* Immutable */
};

static int vmw_view_create(struct vmw_resource *res);
static int vmw_view_destroy(struct vmw_resource *res);
static void vmw_hw_view_destroy(struct vmw_resource *res);
static void vmw_view_commit_notify(struct vmw_resource *res,
				   enum vmw_cmdbuf_res_state state);

static const struct vmw_res_func vmw_view_func = {
	.res_type = vmw_res_view,
	.needs_backup = false,
	.may_evict = false,
	.type_name = "DX view",
	.backup_placement = NULL,
	.create = vmw_view_create,
	.commit_notify = vmw_view_commit_notify,
};

/**
 * struct vmw_view - view define command body stub
 *
 * @view_id: The device id of the view being defined
 * @sid: The surface id of the view being defined
 *
 * This generic struct is used by the code to change @view_id and @sid of a
 * saved view define command.
 */
struct vmw_view_define {
	uint32 view_id;
	uint32 sid;
};

/**
 * vmw_view - Convert a struct vmw_resource to a struct vmw_view
 *
 * @res: Pointer to the resource to convert.
 *
 * Returns a pointer to a struct vmw_view.
 */
static struct vmw_view *vmw_view(struct vmw_resource *res)
{
	return container_of(res, struct vmw_view, res);
}

/**
 * vmw_view_commit_notify - Notify that a view operation has been committed to
 * hardware from a user-supplied command stream.
 *
 * @res: Pointer to the view resource.
 * @state: Indicating whether a creation or removal has been committed.
 *
 */
static void vmw_view_commit_notify(struct vmw_resource *res,
				   enum vmw_cmdbuf_res_state state)
{
	struct vmw_view *view = vmw_view(res);
	struct vmw_private *dev_priv = res->dev_priv;

	mutex_lock(&dev_priv->binding_mutex);
	if (state == VMW_CMDBUF_RES_ADD) {
		struct vmw_surface *srf = vmw_res_to_srf(view->srf);

		list_add_tail(&view->srf_head, &srf->view_list);
		vmw_cotable_add_resource(view->cotable, &view->cotable_head);
		view->committed = true;
		res->id = view->view_id;

	} else {
		list_del_init(&view->cotable_head);
		list_del_init(&view->srf_head);
		view->committed = false;
		res->id = -1;
	}
	mutex_unlock(&dev_priv->binding_mutex);
}

/**
 * vmw_view_create - Create a hardware view.
 *
 * @res: Pointer to the view resource.
 *
 * Create a hardware view. Typically used if that view has previously been
 * destroyed by an eviction operation.
 */
static int vmw_view_create(struct vmw_resource *res)
{
	struct vmw_view *view = vmw_view(res);
	struct vmw_surface *srf = vmw_res_to_srf(view->srf);
	struct vmw_private *dev_priv = res->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		struct vmw_view_define body;
	} *cmd;

	mutex_lock(&dev_priv->binding_mutex);
	if (!view->committed) {
		mutex_unlock(&dev_priv->binding_mutex);
		return 0;
	}

	cmd = VMW_FIFO_RESERVE_DX(res->dev_priv, view->cmd_size, view->ctx->id);
	if (!cmd) {
		mutex_unlock(&dev_priv->binding_mutex);
		return -ENOMEM;
	}

	memcpy(cmd, &view->cmd, view->cmd_size);
	WARN_ON(cmd->body.view_id != view->view_id);
	/* Sid may have changed due to surface eviction. */
	WARN_ON(view->srf->id == SVGA3D_INVALID_ID);
	cmd->body.sid = view->srf->id;
	vmw_fifo_commit(res->dev_priv, view->cmd_size);
	res->id = view->view_id;
	list_add_tail(&view->srf_head, &srf->view_list);
	vmw_cotable_add_resource(view->cotable, &view->cotable_head);
	mutex_unlock(&dev_priv->binding_mutex);

	return 0;
}

/**
 * vmw_view_destroy - Destroy a hardware view.
 *
 * @res: Pointer to the view resource.
 *
 * Destroy a hardware view. Typically used on unexpected termination of the
 * owning process or if the surface the view is pointing to is destroyed.
 */
static int vmw_view_destroy(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;
	struct vmw_view *view = vmw_view(res);
	struct {
		SVGA3dCmdHeader header;
		union vmw_view_destroy body;
	} *cmd;

	lockdep_assert_held_once(&dev_priv->binding_mutex);
	vmw_binding_res_list_scrub(&res->binding_head);

	if (!view->committed || res->id == -1)
		return 0;

	cmd = VMW_FIFO_RESERVE_DX(dev_priv, sizeof(*cmd), view->ctx->id);
	if (!cmd)
		return -ENOMEM;

	cmd->header.id = vmw_view_destroy_cmds[view->view_type];
	cmd->header.size = sizeof(cmd->body);
	cmd->body.view_id = view->view_id;
	vmw_fifo_commit(dev_priv, sizeof(*cmd));
	res->id = -1;
	list_del_init(&view->cotable_head);
	list_del_init(&view->srf_head);

	return 0;
}

/**
 * vmw_hw_view_destroy - Destroy a hardware view as part of resource cleanup.
 *
 * @res: Pointer to the view resource.
 *
 * Destroy a hardware view if it's still present.
 */
static void vmw_hw_view_destroy(struct vmw_resource *res)
{
	struct vmw_private *dev_priv = res->dev_priv;

	mutex_lock(&dev_priv->binding_mutex);
	WARN_ON(vmw_view_destroy(res));
	res->id = -1;
	mutex_unlock(&dev_priv->binding_mutex);
}

/**
 * vmw_view_key - Compute a view key suitable for the cmdbuf resource manager
 *
 * @user_key: The user-space id used for the view.
 * @view_type: The view type.
 *
 * Destroy a hardware view if it's still present.
 */
static u32 vmw_view_key(u32 user_key, enum vmw_view_type view_type)
{
	return user_key | (view_type << 20);
}

/**
 * vmw_view_id_ok - Basic view id and type range checks.
 *
 * @user_key: The user-space id used for the view.
 * @view_type: The view type.
 *
 * Checks that the view id and type (typically provided by user-space) is
 * valid.
 */
static bool vmw_view_id_ok(u32 user_key, enum vmw_view_type view_type)
{
	return (user_key < SVGA_COTABLE_MAX_IDS &&
		view_type < vmw_view_max);
}

/**
 * vmw_view_res_free - resource res_free callback for view resources
 *
 * @res: Pointer to a struct vmw_resource
 *
 * Frees memory and memory accounting held by a struct vmw_view.
 */
static void vmw_view_res_free(struct vmw_resource *res)
{
	struct vmw_view *view = vmw_view(res);
	size_t size = offsetof(struct vmw_view, cmd) + view->cmd_size;
	struct vmw_private *dev_priv = res->dev_priv;

	vmw_resource_unreference(&view->cotable);
	vmw_resource_unreference(&view->srf);
	kfree_rcu(view, rcu);
	ttm_mem_global_free(vmw_mem_glob(dev_priv), size);
}

/**
 * vmw_view_add - Create a view resource and stage it for addition
 * as a command buffer managed resource.
 *
 * @man: Pointer to the compat shader manager identifying the shader namespace.
 * @ctx: Pointer to a struct vmw_resource identifying the active context.
 * @srf: Pointer to a struct vmw_resource identifying the surface the view
 * points to.
 * @view_type: The view type deduced from the view create command.
 * @user_key: The key that is used to identify the shader. The key is
 * unique to the view type and to the context.
 * @cmd: Pointer to the view create command in the command stream.
 * @cmd_size: Size of the view create command in the command stream.
 * @list: Caller's list of staged command buffer resource actions.
 */
int vmw_view_add(struct vmw_cmdbuf_res_manager *man,
		 struct vmw_resource *ctx,
		 struct vmw_resource *srf,
		 enum vmw_view_type view_type,
		 u32 user_key,
		 const void *cmd,
		 size_t cmd_size,
		 struct list_head *list)
{
	static const size_t vmw_view_define_sizes[] = {
		[vmw_view_sr] = sizeof(SVGA3dCmdDXDefineShaderResourceView),
		[vmw_view_rt] = sizeof(SVGA3dCmdDXDefineRenderTargetView),
		[vmw_view_ds] = sizeof(SVGA3dCmdDXDefineDepthStencilView),
		[vmw_view_ua] = sizeof(SVGA3dCmdDXDefineUAView)
	};

	struct vmw_private *dev_priv = ctx->dev_priv;
	struct vmw_resource *res;
	struct vmw_view *view;
	struct ttm_operation_ctx ttm_opt_ctx = {
		.interruptible = true,
		.no_wait_gpu = false
	};
	size_t size;
	int ret;

	if (cmd_size != vmw_view_define_sizes[view_type] +
	    sizeof(SVGA3dCmdHeader)) {
		VMW_DEBUG_USER("Illegal view create command size.\n");
		return -EINVAL;
	}

	if (!vmw_view_id_ok(user_key, view_type)) {
		VMW_DEBUG_USER("Illegal view add view id.\n");
		return -EINVAL;
	}

	size = offsetof(struct vmw_view, cmd) + cmd_size;

	ret = ttm_mem_global_alloc(vmw_mem_glob(dev_priv), size, &ttm_opt_ctx);
	if (ret) {
		if (ret != -ERESTARTSYS)
			DRM_ERROR("Out of graphics memory for view creation\n");
		return ret;
	}

	view = kmalloc(size, GFP_KERNEL);
	if (!view) {
		ttm_mem_global_free(vmw_mem_glob(dev_priv), size);
		return -ENOMEM;
	}

	res = &view->res;
	view->ctx = ctx;
	view->srf = vmw_resource_reference(srf);
	view->cotable = vmw_resource_reference
		(vmw_context_cotable(ctx, vmw_view_cotables[view_type]));
	view->view_type = view_type;
	view->view_id = user_key;
	view->cmd_size = cmd_size;
	view->committed = false;
	INIT_LIST_HEAD(&view->srf_head);
	INIT_LIST_HEAD(&view->cotable_head);
	memcpy(&view->cmd, cmd, cmd_size);
	ret = vmw_resource_init(dev_priv, res, true,
				vmw_view_res_free, &vmw_view_func);
	if (ret)
		goto out_resource_init;

	ret = vmw_cmdbuf_res_add(man, vmw_cmdbuf_res_view,
				 vmw_view_key(user_key, view_type),
				 res, list);
	if (ret)
		goto out_resource_init;

	res->id = view->view_id;
	res->hw_destroy = vmw_hw_view_destroy;

out_resource_init:
	vmw_resource_unreference(&res);

	return ret;
}

/**
 * vmw_view_remove - Stage a view for removal.
 *
 * @man: Pointer to the view manager identifying the shader namespace.
 * @user_key: The key that is used to identify the view. The key is
 * unique to the view type.
 * @view_type: View type
 * @list: Caller's list of staged command buffer resource actions.
 * @res_p: If the resource is in an already committed state, points to the
 * struct vmw_resource on successful return. The pointer will be
 * non ref-counted.
 */
int vmw_view_remove(struct vmw_cmdbuf_res_manager *man,
		    u32 user_key, enum vmw_view_type view_type,
		    struct list_head *list,
		    struct vmw_resource **res_p)
{
	if (!vmw_view_id_ok(user_key, view_type)) {
		VMW_DEBUG_USER("Illegal view remove view id.\n");
		return -EINVAL;
	}

	return vmw_cmdbuf_res_remove(man, vmw_cmdbuf_res_view,
				     vmw_view_key(user_key, view_type),
				     list, res_p);
}

/**
 * vmw_view_cotable_list_destroy - Evict all views belonging to a cotable.
 *
 * @dev_priv: Pointer to a device private struct.
 * @list: List of views belonging to a cotable.
 * @readback: Unused. Needed for function interface only.
 *
 * This function evicts all views belonging to a cotable.
 * It must be called with the binding_mutex held, and the caller must hold
 * a reference to the view resource. This is typically called before the
 * cotable is paged out.
 */
void vmw_view_cotable_list_destroy(struct vmw_private *dev_priv,
				   struct list_head *list,
				   bool readback)
{
	struct vmw_view *entry, *next;

	lockdep_assert_held_once(&dev_priv->binding_mutex);

	list_for_each_entry_safe(entry, next, list, cotable_head)
		WARN_ON(vmw_view_destroy(&entry->res));
}

/**
 * vmw_view_surface_list_destroy - Evict all views pointing to a surface
 *
 * @dev_priv: Pointer to a device private struct.
 * @list: List of views pointing to a surface.
 *
 * This function evicts all views pointing to a surface. This is typically
 * called before the surface is evicted.
 */
void vmw_view_surface_list_destroy(struct vmw_private *dev_priv,
				   struct list_head *list)
{
	struct vmw_view *entry, *next;

	lockdep_assert_held_once(&dev_priv->binding_mutex);

	list_for_each_entry_safe(entry, next, list, srf_head)
		WARN_ON(vmw_view_destroy(&entry->res));
}

/**
 * vmw_view_srf - Return a non-refcounted pointer to the surface a view is
 * pointing to.
 *
 * @res: pointer to a view resource.
 *
 * Note that the view itself is holding a reference, so as long
 * the view resource is alive, the surface resource will be.
 */
struct vmw_resource *vmw_view_srf(struct vmw_resource *res)
{
	return vmw_view(res)->srf;
}

/**
 * vmw_view_lookup - Look up a view.
 *
 * @man: The context's cmdbuf ref manager.
 * @view_type: The view type.
 * @user_key: The view user id.
 *
 * returns a refcounted pointer to a view or an error pointer if not found.
 */
struct vmw_resource *vmw_view_lookup(struct vmw_cmdbuf_res_manager *man,
				     enum vmw_view_type view_type,
				     u32 user_key)
{
	return vmw_cmdbuf_res_lookup(man, vmw_cmdbuf_res_view,
				     vmw_view_key(user_key, view_type));
}

/**
 * vmw_view_dirtying - Return whether a view type is dirtying its resource
 * @res: Pointer to the view
 *
 * Each time a resource is put on the validation list as the result of a
 * view pointing to it, we need to determine whether that resource will
 * be dirtied (written to by the GPU) as a result of the corresponding
 * GPU operation. Currently only rendertarget-, depth-stencil and unordered
 * access views are capable of dirtying its resource.
 *
 * Return: Whether the view type of @res dirties the resource it points to.
 */
u32 vmw_view_dirtying(struct vmw_resource *res)
{
	static u32 view_is_dirtying[vmw_view_max] = {
		[vmw_view_rt] = VMW_RES_DIRTY_SET,
		[vmw_view_ds] = VMW_RES_DIRTY_SET,
		[vmw_view_ua] = VMW_RES_DIRTY_SET,
	};

	/* Update this function as we add more view types */
	BUILD_BUG_ON(vmw_view_max != 4);
	return view_is_dirtying[vmw_view(res)->view_type];
}

const u32 vmw_view_destroy_cmds[] = {
	[vmw_view_sr] = SVGA_3D_CMD_DX_DESTROY_SHADERRESOURCE_VIEW,
	[vmw_view_rt] = SVGA_3D_CMD_DX_DESTROY_RENDERTARGET_VIEW,
	[vmw_view_ds] = SVGA_3D_CMD_DX_DESTROY_DEPTHSTENCIL_VIEW,
	[vmw_view_ua] = SVGA_3D_CMD_DX_DESTROY_UA_VIEW,
};

const SVGACOTableType vmw_view_cotables[] = {
	[vmw_view_sr] = SVGA_COTABLE_SRVIEW,
	[vmw_view_rt] = SVGA_COTABLE_RTVIEW,
	[vmw_view_ds] = SVGA_COTABLE_DSVIEW,
	[vmw_view_ua] = SVGA_COTABLE_UAVIEW,
};

const SVGACOTableType vmw_so_cotables[] = {
	[vmw_so_el] = SVGA_COTABLE_ELEMENTLAYOUT,
	[vmw_so_bs] = SVGA_COTABLE_BLENDSTATE,
	[vmw_so_ds] = SVGA_COTABLE_DEPTHSTENCIL,
	[vmw_so_rs] = SVGA_COTABLE_RASTERIZERSTATE,
	[vmw_so_ss] = SVGA_COTABLE_SAMPLER,
	[vmw_so_so] = SVGA_COTABLE_STREAMOUTPUT
};


/* To remove unused function warning */
static void vmw_so_build_asserts(void) __attribute__((used));


/*
 * This function is unused at run-time, and only used to dump various build
 * asserts important for code optimization assumptions.
 */
static void vmw_so_build_asserts(void)
{
	/* Assert that our vmw_view_cmd_to_type() function is correct. */
	BUILD_BUG_ON(SVGA_3D_CMD_DX_DESTROY_SHADERRESOURCE_VIEW !=
		     SVGA_3D_CMD_DX_DEFINE_SHADERRESOURCE_VIEW + 1);
	BUILD_BUG_ON(SVGA_3D_CMD_DX_DEFINE_RENDERTARGET_VIEW !=
		     SVGA_3D_CMD_DX_DEFINE_SHADERRESOURCE_VIEW + 2);
	BUILD_BUG_ON(SVGA_3D_CMD_DX_DESTROY_RENDERTARGET_VIEW !=
		     SVGA_3D_CMD_DX_DEFINE_SHADERRESOURCE_VIEW + 3);
	BUILD_BUG_ON(SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_VIEW !=
		     SVGA_3D_CMD_DX_DEFINE_SHADERRESOURCE_VIEW + 4);
	BUILD_BUG_ON(SVGA_3D_CMD_DX_DESTROY_DEPTHSTENCIL_VIEW !=
		     SVGA_3D_CMD_DX_DEFINE_SHADERRESOURCE_VIEW + 5);

	/* Assert that our "one body fits all" assumption is valid */
	BUILD_BUG_ON(sizeof(union vmw_view_destroy) != sizeof(u32));

	/* Assert that the view key space can hold all view ids. */
	BUILD_BUG_ON(SVGA_COTABLE_MAX_IDS >= ((1 << 20) - 1));

	/*
	 * Assert that the offset of sid in all view define commands
	 * is what we assume it to be.
	 */
	BUILD_BUG_ON(offsetof(struct vmw_view_define, sid) !=
		     offsetof(SVGA3dCmdDXDefineShaderResourceView, sid));
	BUILD_BUG_ON(offsetof(struct vmw_view_define, sid) !=
		     offsetof(SVGA3dCmdDXDefineRenderTargetView, sid));
	BUILD_BUG_ON(offsetof(struct vmw_view_define, sid) !=
		     offsetof(SVGA3dCmdDXDefineDepthStencilView, sid));
}
