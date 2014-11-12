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
#include "vmwgfx_reg.h"
#include <drm/ttm/ttm_bo_api.h>
#include <drm/ttm/ttm_placement.h>

#define VMW_RES_HT_ORDER 12

/**
 * struct vmw_resource_relocation - Relocation info for resources
 *
 * @head: List head for the software context's relocation list.
 * @res: Non-ref-counted pointer to the resource.
 * @offset: Offset of 4 byte entries into the command buffer where the
 * id that needs fixup is located.
 */
struct vmw_resource_relocation {
	struct list_head head;
	const struct vmw_resource *res;
	unsigned long offset;
};

/**
 * struct vmw_resource_val_node - Validation info for resources
 *
 * @head: List head for the software context's resource list.
 * @hash: Hash entry for quick resouce to val_node lookup.
 * @res: Ref-counted pointer to the resource.
 * @switch_backup: Boolean whether to switch backup buffer on unreserve.
 * @new_backup: Refcounted pointer to the new backup buffer.
 * @staged_bindings: If @res is a context, tracks bindings set up during
 * the command batch. Otherwise NULL.
 * @new_backup_offset: New backup buffer offset if @new_backup is non-NUll.
 * @first_usage: Set to true the first time the resource is referenced in
 * the command stream.
 * @no_buffer_needed: Resources do not need to allocate buffer backup on
 * reservation. The command stream will provide one.
 */
struct vmw_resource_val_node {
	struct list_head head;
	struct drm_hash_item hash;
	struct vmw_resource *res;
	struct vmw_dma_buffer *new_backup;
	struct vmw_ctx_binding_state *staged_bindings;
	unsigned long new_backup_offset;
	bool first_usage;
	bool no_buffer_needed;
};

/**
 * struct vmw_cmd_entry - Describe a command for the verifier
 *
 * @user_allow: Whether allowed from the execbuf ioctl.
 * @gb_disable: Whether disabled if guest-backed objects are available.
 * @gb_enable: Whether enabled iff guest-backed objects are available.
 */
struct vmw_cmd_entry {
	int (*func) (struct vmw_private *, struct vmw_sw_context *,
		     SVGA3dCmdHeader *);
	bool user_allow;
	bool gb_disable;
	bool gb_enable;
};

#define VMW_CMD_DEF(_cmd, _func, _user_allow, _gb_disable, _gb_enable)	\
	[(_cmd) - SVGA_3D_CMD_BASE] = {(_func), (_user_allow),\
				       (_gb_disable), (_gb_enable)}

/**
 * vmw_resource_unreserve - unreserve resources previously reserved for
 * command submission.
 *
 * @list_head: list of resources to unreserve.
 * @backoff: Whether command submission failed.
 */
static void vmw_resource_list_unreserve(struct list_head *list,
					bool backoff)
{
	struct vmw_resource_val_node *val;

	list_for_each_entry(val, list, head) {
		struct vmw_resource *res = val->res;
		struct vmw_dma_buffer *new_backup =
			backoff ? NULL : val->new_backup;

		/*
		 * Transfer staged context bindings to the
		 * persistent context binding tracker.
		 */
		if (unlikely(val->staged_bindings)) {
			if (!backoff) {
				vmw_context_binding_state_transfer
					(val->res, val->staged_bindings);
			}
			kfree(val->staged_bindings);
			val->staged_bindings = NULL;
		}
		vmw_resource_unreserve(res, new_backup,
			val->new_backup_offset);
		vmw_dmabuf_unreference(&val->new_backup);
	}
}


/**
 * vmw_resource_val_add - Add a resource to the software context's
 * resource list if it's not already on it.
 *
 * @sw_context: Pointer to the software context.
 * @res: Pointer to the resource.
 * @p_node On successful return points to a valid pointer to a
 * struct vmw_resource_val_node, if non-NULL on entry.
 */
static int vmw_resource_val_add(struct vmw_sw_context *sw_context,
				struct vmw_resource *res,
				struct vmw_resource_val_node **p_node)
{
	struct vmw_resource_val_node *node;
	struct drm_hash_item *hash;
	int ret;

	if (likely(drm_ht_find_item(&sw_context->res_ht, (unsigned long) res,
				    &hash) == 0)) {
		node = container_of(hash, struct vmw_resource_val_node, hash);
		node->first_usage = false;
		if (unlikely(p_node != NULL))
			*p_node = node;
		return 0;
	}

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (unlikely(node == NULL)) {
		DRM_ERROR("Failed to allocate a resource validation "
			  "entry.\n");
		return -ENOMEM;
	}

	node->hash.key = (unsigned long) res;
	ret = drm_ht_insert_item(&sw_context->res_ht, &node->hash);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed to initialize a resource validation "
			  "entry.\n");
		kfree(node);
		return ret;
	}
	list_add_tail(&node->head, &sw_context->resource_list);
	node->res = vmw_resource_reference(res);
	node->first_usage = true;

	if (unlikely(p_node != NULL))
		*p_node = node;

	return 0;
}

/**
 * vmw_resource_context_res_add - Put resources previously bound to a context on
 * the validation list
 *
 * @dev_priv: Pointer to a device private structure
 * @sw_context: Pointer to a software context used for this command submission
 * @ctx: Pointer to the context resource
 *
 * This function puts all resources that were previously bound to @ctx on
 * the resource validation list. This is part of the context state reemission
 */
static int vmw_resource_context_res_add(struct vmw_private *dev_priv,
					struct vmw_sw_context *sw_context,
					struct vmw_resource *ctx)
{
	struct list_head *binding_list;
	struct vmw_ctx_binding *entry;
	int ret = 0;
	struct vmw_resource *res;

	mutex_lock(&dev_priv->binding_mutex);
	binding_list = vmw_context_binding_list(ctx);

	list_for_each_entry(entry, binding_list, ctx_list) {
		res = vmw_resource_reference_unless_doomed(entry->bi.res);
		if (unlikely(res == NULL))
			continue;

		ret = vmw_resource_val_add(sw_context, entry->bi.res, NULL);
		vmw_resource_unreference(&res);
		if (unlikely(ret != 0))
			break;
	}

	mutex_unlock(&dev_priv->binding_mutex);
	return ret;
}

/**
 * vmw_resource_relocation_add - Add a relocation to the relocation list
 *
 * @list: Pointer to head of relocation list.
 * @res: The resource.
 * @offset: Offset into the command buffer currently being parsed where the
 * id that needs fixup is located. Granularity is 4 bytes.
 */
static int vmw_resource_relocation_add(struct list_head *list,
				       const struct vmw_resource *res,
				       unsigned long offset)
{
	struct vmw_resource_relocation *rel;

	rel = kmalloc(sizeof(*rel), GFP_KERNEL);
	if (unlikely(rel == NULL)) {
		DRM_ERROR("Failed to allocate a resource relocation.\n");
		return -ENOMEM;
	}

	rel->res = res;
	rel->offset = offset;
	list_add_tail(&rel->head, list);

	return 0;
}

/**
 * vmw_resource_relocations_free - Free all relocations on a list
 *
 * @list: Pointer to the head of the relocation list.
 */
static void vmw_resource_relocations_free(struct list_head *list)
{
	struct vmw_resource_relocation *rel, *n;

	list_for_each_entry_safe(rel, n, list, head) {
		list_del(&rel->head);
		kfree(rel);
	}
}

/**
 * vmw_resource_relocations_apply - Apply all relocations on a list
 *
 * @cb: Pointer to the start of the command buffer bein patch. This need
 * not be the same buffer as the one being parsed when the relocation
 * list was built, but the contents must be the same modulo the
 * resource ids.
 * @list: Pointer to the head of the relocation list.
 */
static void vmw_resource_relocations_apply(uint32_t *cb,
					   struct list_head *list)
{
	struct vmw_resource_relocation *rel;

	list_for_each_entry(rel, list, head) {
		if (likely(rel->res != NULL))
			cb[rel->offset] = rel->res->id;
		else
			cb[rel->offset] = SVGA_3D_CMD_NOP;
	}
}

static int vmw_cmd_invalid(struct vmw_private *dev_priv,
			   struct vmw_sw_context *sw_context,
			   SVGA3dCmdHeader *header)
{
	return capable(CAP_SYS_ADMIN) ? : -EINVAL;
}

static int vmw_cmd_ok(struct vmw_private *dev_priv,
		      struct vmw_sw_context *sw_context,
		      SVGA3dCmdHeader *header)
{
	return 0;
}

/**
 * vmw_bo_to_validate_list - add a bo to a validate list
 *
 * @sw_context: The software context used for this command submission batch.
 * @bo: The buffer object to add.
 * @validate_as_mob: Validate this buffer as a MOB.
 * @p_val_node: If non-NULL Will be updated with the validate node number
 * on return.
 *
 * Returns -EINVAL if the limit of number of buffer objects per command
 * submission is reached.
 */
static int vmw_bo_to_validate_list(struct vmw_sw_context *sw_context,
				   struct ttm_buffer_object *bo,
				   bool validate_as_mob,
				   uint32_t *p_val_node)
{
	uint32_t val_node;
	struct vmw_validate_buffer *vval_buf;
	struct ttm_validate_buffer *val_buf;
	struct drm_hash_item *hash;
	int ret;

	if (likely(drm_ht_find_item(&sw_context->res_ht, (unsigned long) bo,
				    &hash) == 0)) {
		vval_buf = container_of(hash, struct vmw_validate_buffer,
					hash);
		if (unlikely(vval_buf->validate_as_mob != validate_as_mob)) {
			DRM_ERROR("Inconsistent buffer usage.\n");
			return -EINVAL;
		}
		val_buf = &vval_buf->base;
		val_node = vval_buf - sw_context->val_bufs;
	} else {
		val_node = sw_context->cur_val_buf;
		if (unlikely(val_node >= VMWGFX_MAX_VALIDATIONS)) {
			DRM_ERROR("Max number of DMA buffers per submission "
				  "exceeded.\n");
			return -EINVAL;
		}
		vval_buf = &sw_context->val_bufs[val_node];
		vval_buf->hash.key = (unsigned long) bo;
		ret = drm_ht_insert_item(&sw_context->res_ht, &vval_buf->hash);
		if (unlikely(ret != 0)) {
			DRM_ERROR("Failed to initialize a buffer validation "
				  "entry.\n");
			return ret;
		}
		++sw_context->cur_val_buf;
		val_buf = &vval_buf->base;
		val_buf->bo = ttm_bo_reference(bo);
		val_buf->shared = false;
		list_add_tail(&val_buf->head, &sw_context->validate_nodes);
		vval_buf->validate_as_mob = validate_as_mob;
	}

	if (p_val_node)
		*p_val_node = val_node;

	return 0;
}

/**
 * vmw_resources_reserve - Reserve all resources on the sw_context's
 * resource list.
 *
 * @sw_context: Pointer to the software context.
 *
 * Note that since vmware's command submission currently is protected by
 * the cmdbuf mutex, no fancy deadlock avoidance is required for resources,
 * since only a single thread at once will attempt this.
 */
static int vmw_resources_reserve(struct vmw_sw_context *sw_context)
{
	struct vmw_resource_val_node *val;
	int ret;

	list_for_each_entry(val, &sw_context->resource_list, head) {
		struct vmw_resource *res = val->res;

		ret = vmw_resource_reserve(res, val->no_buffer_needed);
		if (unlikely(ret != 0))
			return ret;

		if (res->backup) {
			struct ttm_buffer_object *bo = &res->backup->base;

			ret = vmw_bo_to_validate_list
				(sw_context, bo,
				 vmw_resource_needs_backup(res), NULL);

			if (unlikely(ret != 0))
				return ret;
		}
	}
	return 0;
}

/**
 * vmw_resources_validate - Validate all resources on the sw_context's
 * resource list.
 *
 * @sw_context: Pointer to the software context.
 *
 * Before this function is called, all resource backup buffers must have
 * been validated.
 */
static int vmw_resources_validate(struct vmw_sw_context *sw_context)
{
	struct vmw_resource_val_node *val;
	int ret;

	list_for_each_entry(val, &sw_context->resource_list, head) {
		struct vmw_resource *res = val->res;

		ret = vmw_resource_validate(res);
		if (unlikely(ret != 0)) {
			if (ret != -ERESTARTSYS)
				DRM_ERROR("Failed to validate resource.\n");
			return ret;
		}
	}
	return 0;
}


/**
 * vmw_cmd_res_reloc_add - Add a resource to a software context's
 * relocation- and validation lists.
 *
 * @dev_priv: Pointer to a struct vmw_private identifying the device.
 * @sw_context: Pointer to the software context.
 * @res_type: Resource type.
 * @id_loc: Pointer to where the id that needs translation is located.
 * @res: Valid pointer to a struct vmw_resource.
 * @p_val: If non null, a pointer to the struct vmw_resource_validate_node
 * used for this resource is returned here.
 */
static int vmw_cmd_res_reloc_add(struct vmw_private *dev_priv,
				 struct vmw_sw_context *sw_context,
				 enum vmw_res_type res_type,
				 uint32_t *id_loc,
				 struct vmw_resource *res,
				 struct vmw_resource_val_node **p_val)
{
	int ret;
	struct vmw_resource_val_node *node;

	*p_val = NULL;
	ret = vmw_resource_relocation_add(&sw_context->res_relocations,
					  res,
					  id_loc - sw_context->buf_start);
	if (unlikely(ret != 0))
		return ret;

	ret = vmw_resource_val_add(sw_context, res, &node);
	if (unlikely(ret != 0))
		return ret;

	if (res_type == vmw_res_context && dev_priv->has_mob &&
	    node->first_usage) {

		/*
		 * Put contexts first on the list to be able to exit
		 * list traversal for contexts early.
		 */
		list_del(&node->head);
		list_add(&node->head, &sw_context->resource_list);

		ret = vmw_resource_context_res_add(dev_priv, sw_context, res);
		if (unlikely(ret != 0))
			return ret;
		node->staged_bindings =
			kzalloc(sizeof(*node->staged_bindings), GFP_KERNEL);
		if (node->staged_bindings == NULL) {
			DRM_ERROR("Failed to allocate context binding "
				  "information.\n");
			return -ENOMEM;
		}
		INIT_LIST_HEAD(&node->staged_bindings->list);
	}

	if (p_val)
		*p_val = node;

	return 0;
}


/**
 * vmw_cmd_res_check - Check that a resource is present and if so, put it
 * on the resource validate list unless it's already there.
 *
 * @dev_priv: Pointer to a device private structure.
 * @sw_context: Pointer to the software context.
 * @res_type: Resource type.
 * @converter: User-space visisble type specific information.
 * @id_loc: Pointer to the location in the command buffer currently being
 * parsed from where the user-space resource id handle is located.
 * @p_val: Pointer to pointer to resource validalidation node. Populated
 * on exit.
 */
static int
vmw_cmd_res_check(struct vmw_private *dev_priv,
		  struct vmw_sw_context *sw_context,
		  enum vmw_res_type res_type,
		  const struct vmw_user_resource_conv *converter,
		  uint32_t *id_loc,
		  struct vmw_resource_val_node **p_val)
{
	struct vmw_res_cache_entry *rcache =
		&sw_context->res_cache[res_type];
	struct vmw_resource *res;
	struct vmw_resource_val_node *node;
	int ret;

	if (*id_loc == SVGA3D_INVALID_ID) {
		if (p_val)
			*p_val = NULL;
		if (res_type == vmw_res_context) {
			DRM_ERROR("Illegal context invalid id.\n");
			return -EINVAL;
		}
		return 0;
	}

	/*
	 * Fastpath in case of repeated commands referencing the same
	 * resource
	 */

	if (likely(rcache->valid && *id_loc == rcache->handle)) {
		const struct vmw_resource *res = rcache->res;

		rcache->node->first_usage = false;
		if (p_val)
			*p_val = rcache->node;

		return vmw_resource_relocation_add
			(&sw_context->res_relocations, res,
			 id_loc - sw_context->buf_start);
	}

	ret = vmw_user_resource_lookup_handle(dev_priv,
					      sw_context->fp->tfile,
					      *id_loc,
					      converter,
					      &res);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Could not find or use resource 0x%08x.\n",
			  (unsigned) *id_loc);
		dump_stack();
		return ret;
	}

	rcache->valid = true;
	rcache->res = res;
	rcache->handle = *id_loc;

	ret = vmw_cmd_res_reloc_add(dev_priv, sw_context, res_type, id_loc,
				    res, &node);
	if (unlikely(ret != 0))
		goto out_no_reloc;

	rcache->node = node;
	if (p_val)
		*p_val = node;
	vmw_resource_unreference(&res);
	return 0;

out_no_reloc:
	BUG_ON(sw_context->error_resource != NULL);
	sw_context->error_resource = res;

	return ret;
}

/**
 * vmw_rebind_contexts - Rebind all resources previously bound to
 * referenced contexts.
 *
 * @sw_context: Pointer to the software context.
 *
 * Rebind context binding points that have been scrubbed because of eviction.
 */
static int vmw_rebind_contexts(struct vmw_sw_context *sw_context)
{
	struct vmw_resource_val_node *val;
	int ret;

	list_for_each_entry(val, &sw_context->resource_list, head) {
		if (unlikely(!val->staged_bindings))
			break;

		ret = vmw_context_rebind_all(val->res);
		if (unlikely(ret != 0)) {
			if (ret != -ERESTARTSYS)
				DRM_ERROR("Failed to rebind context.\n");
			return ret;
		}
	}

	return 0;
}

/**
 * vmw_cmd_cid_check - Check a command header for valid context information.
 *
 * @dev_priv: Pointer to a device private structure.
 * @sw_context: Pointer to the software context.
 * @header: A command header with an embedded user-space context handle.
 *
 * Convenience function: Call vmw_cmd_res_check with the user-space context
 * handle embedded in @header.
 */
static int vmw_cmd_cid_check(struct vmw_private *dev_priv,
			     struct vmw_sw_context *sw_context,
			     SVGA3dCmdHeader *header)
{
	struct vmw_cid_cmd {
		SVGA3dCmdHeader header;
		uint32_t cid;
	} *cmd;

	cmd = container_of(header, struct vmw_cid_cmd, header);
	return vmw_cmd_res_check(dev_priv, sw_context, vmw_res_context,
				 user_context_converter, &cmd->cid, NULL);
}

static int vmw_cmd_set_render_target_check(struct vmw_private *dev_priv,
					   struct vmw_sw_context *sw_context,
					   SVGA3dCmdHeader *header)
{
	struct vmw_sid_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdSetRenderTarget body;
	} *cmd;
	struct vmw_resource_val_node *ctx_node;
	struct vmw_resource_val_node *res_node;
	int ret;

	cmd = container_of(header, struct vmw_sid_cmd, header);

	ret = vmw_cmd_res_check(dev_priv, sw_context, vmw_res_context,
				user_context_converter, &cmd->body.cid,
				&ctx_node);
	if (unlikely(ret != 0))
		return ret;

	ret = vmw_cmd_res_check(dev_priv, sw_context, vmw_res_surface,
				user_surface_converter,
				&cmd->body.target.sid, &res_node);
	if (unlikely(ret != 0))
		return ret;

	if (dev_priv->has_mob) {
		struct vmw_ctx_bindinfo bi;

		bi.ctx = ctx_node->res;
		bi.res = res_node ? res_node->res : NULL;
		bi.bt = vmw_ctx_binding_rt;
		bi.i1.rt_type = cmd->body.type;
		return vmw_context_binding_add(ctx_node->staged_bindings, &bi);
	}

	return 0;
}

static int vmw_cmd_surface_copy_check(struct vmw_private *dev_priv,
				      struct vmw_sw_context *sw_context,
				      SVGA3dCmdHeader *header)
{
	struct vmw_sid_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdSurfaceCopy body;
	} *cmd;
	int ret;

	cmd = container_of(header, struct vmw_sid_cmd, header);
	ret = vmw_cmd_res_check(dev_priv, sw_context, vmw_res_surface,
				user_surface_converter,
				&cmd->body.src.sid, NULL);
	if (unlikely(ret != 0))
		return ret;
	return vmw_cmd_res_check(dev_priv, sw_context, vmw_res_surface,
				 user_surface_converter,
				 &cmd->body.dest.sid, NULL);
}

static int vmw_cmd_stretch_blt_check(struct vmw_private *dev_priv,
				     struct vmw_sw_context *sw_context,
				     SVGA3dCmdHeader *header)
{
	struct vmw_sid_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdSurfaceStretchBlt body;
	} *cmd;
	int ret;

	cmd = container_of(header, struct vmw_sid_cmd, header);
	ret = vmw_cmd_res_check(dev_priv, sw_context, vmw_res_surface,
				user_surface_converter,
				&cmd->body.src.sid, NULL);
	if (unlikely(ret != 0))
		return ret;
	return vmw_cmd_res_check(dev_priv, sw_context, vmw_res_surface,
				 user_surface_converter,
				 &cmd->body.dest.sid, NULL);
}

static int vmw_cmd_blt_surf_screen_check(struct vmw_private *dev_priv,
					 struct vmw_sw_context *sw_context,
					 SVGA3dCmdHeader *header)
{
	struct vmw_sid_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdBlitSurfaceToScreen body;
	} *cmd;

	cmd = container_of(header, struct vmw_sid_cmd, header);

	return vmw_cmd_res_check(dev_priv, sw_context, vmw_res_surface,
				 user_surface_converter,
				 &cmd->body.srcImage.sid, NULL);
}

static int vmw_cmd_present_check(struct vmw_private *dev_priv,
				 struct vmw_sw_context *sw_context,
				 SVGA3dCmdHeader *header)
{
	struct vmw_sid_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdPresent body;
	} *cmd;


	cmd = container_of(header, struct vmw_sid_cmd, header);

	return vmw_cmd_res_check(dev_priv, sw_context, vmw_res_surface,
				 user_surface_converter, &cmd->body.sid,
				 NULL);
}

/**
 * vmw_query_bo_switch_prepare - Prepare to switch pinned buffer for queries.
 *
 * @dev_priv: The device private structure.
 * @new_query_bo: The new buffer holding query results.
 * @sw_context: The software context used for this command submission.
 *
 * This function checks whether @new_query_bo is suitable for holding
 * query results, and if another buffer currently is pinned for query
 * results. If so, the function prepares the state of @sw_context for
 * switching pinned buffers after successful submission of the current
 * command batch.
 */
static int vmw_query_bo_switch_prepare(struct vmw_private *dev_priv,
				       struct ttm_buffer_object *new_query_bo,
				       struct vmw_sw_context *sw_context)
{
	struct vmw_res_cache_entry *ctx_entry =
		&sw_context->res_cache[vmw_res_context];
	int ret;

	BUG_ON(!ctx_entry->valid);
	sw_context->last_query_ctx = ctx_entry->res;

	if (unlikely(new_query_bo != sw_context->cur_query_bo)) {

		if (unlikely(new_query_bo->num_pages > 4)) {
			DRM_ERROR("Query buffer too large.\n");
			return -EINVAL;
		}

		if (unlikely(sw_context->cur_query_bo != NULL)) {
			sw_context->needs_post_query_barrier = true;
			ret = vmw_bo_to_validate_list(sw_context,
						      sw_context->cur_query_bo,
						      dev_priv->has_mob, NULL);
			if (unlikely(ret != 0))
				return ret;
		}
		sw_context->cur_query_bo = new_query_bo;

		ret = vmw_bo_to_validate_list(sw_context,
					      dev_priv->dummy_query_bo,
					      dev_priv->has_mob, NULL);
		if (unlikely(ret != 0))
			return ret;

	}

	return 0;
}


/**
 * vmw_query_bo_switch_commit - Finalize switching pinned query buffer
 *
 * @dev_priv: The device private structure.
 * @sw_context: The software context used for this command submission batch.
 *
 * This function will check if we're switching query buffers, and will then,
 * issue a dummy occlusion query wait used as a query barrier. When the fence
 * object following that query wait has signaled, we are sure that all
 * preceding queries have finished, and the old query buffer can be unpinned.
 * However, since both the new query buffer and the old one are fenced with
 * that fence, we can do an asynchronus unpin now, and be sure that the
 * old query buffer won't be moved until the fence has signaled.
 *
 * As mentioned above, both the new - and old query buffers need to be fenced
 * using a sequence emitted *after* calling this function.
 */
static void vmw_query_bo_switch_commit(struct vmw_private *dev_priv,
				     struct vmw_sw_context *sw_context)
{
	/*
	 * The validate list should still hold references to all
	 * contexts here.
	 */

	if (sw_context->needs_post_query_barrier) {
		struct vmw_res_cache_entry *ctx_entry =
			&sw_context->res_cache[vmw_res_context];
		struct vmw_resource *ctx;
		int ret;

		BUG_ON(!ctx_entry->valid);
		ctx = ctx_entry->res;

		ret = vmw_fifo_emit_dummy_query(dev_priv, ctx->id);

		if (unlikely(ret != 0))
			DRM_ERROR("Out of fifo space for dummy query.\n");
	}

	if (dev_priv->pinned_bo != sw_context->cur_query_bo) {
		if (dev_priv->pinned_bo) {
			vmw_bo_pin(dev_priv->pinned_bo, false);
			ttm_bo_unref(&dev_priv->pinned_bo);
		}

		if (!sw_context->needs_post_query_barrier) {
			vmw_bo_pin(sw_context->cur_query_bo, true);

			/*
			 * We pin also the dummy_query_bo buffer so that we
			 * don't need to validate it when emitting
			 * dummy queries in context destroy paths.
			 */

			vmw_bo_pin(dev_priv->dummy_query_bo, true);
			dev_priv->dummy_query_bo_pinned = true;

			BUG_ON(sw_context->last_query_ctx == NULL);
			dev_priv->query_cid = sw_context->last_query_ctx->id;
			dev_priv->query_cid_valid = true;
			dev_priv->pinned_bo =
				ttm_bo_reference(sw_context->cur_query_bo);
		}
	}
}

/**
 * vmw_translate_mob_pointer - Prepare to translate a user-space buffer
 * handle to a MOB id.
 *
 * @dev_priv: Pointer to a device private structure.
 * @sw_context: The software context used for this command batch validation.
 * @id: Pointer to the user-space handle to be translated.
 * @vmw_bo_p: Points to a location that, on successful return will carry
 * a reference-counted pointer to the DMA buffer identified by the
 * user-space handle in @id.
 *
 * This function saves information needed to translate a user-space buffer
 * handle to a MOB id. The translation does not take place immediately, but
 * during a call to vmw_apply_relocations(). This function builds a relocation
 * list and a list of buffers to validate. The former needs to be freed using
 * either vmw_apply_relocations() or vmw_free_relocations(). The latter
 * needs to be freed using vmw_clear_validations.
 */
static int vmw_translate_mob_ptr(struct vmw_private *dev_priv,
				 struct vmw_sw_context *sw_context,
				 SVGAMobId *id,
				 struct vmw_dma_buffer **vmw_bo_p)
{
	struct vmw_dma_buffer *vmw_bo = NULL;
	struct ttm_buffer_object *bo;
	uint32_t handle = *id;
	struct vmw_relocation *reloc;
	int ret;

	ret = vmw_user_dmabuf_lookup(sw_context->fp->tfile, handle, &vmw_bo);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Could not find or use MOB buffer.\n");
		return -EINVAL;
	}
	bo = &vmw_bo->base;

	if (unlikely(sw_context->cur_reloc >= VMWGFX_MAX_RELOCATIONS)) {
		DRM_ERROR("Max number relocations per submission"
			  " exceeded\n");
		ret = -EINVAL;
		goto out_no_reloc;
	}

	reloc = &sw_context->relocs[sw_context->cur_reloc++];
	reloc->mob_loc = id;
	reloc->location = NULL;

	ret = vmw_bo_to_validate_list(sw_context, bo, true, &reloc->index);
	if (unlikely(ret != 0))
		goto out_no_reloc;

	*vmw_bo_p = vmw_bo;
	return 0;

out_no_reloc:
	vmw_dmabuf_unreference(&vmw_bo);
	vmw_bo_p = NULL;
	return ret;
}

/**
 * vmw_translate_guest_pointer - Prepare to translate a user-space buffer
 * handle to a valid SVGAGuestPtr
 *
 * @dev_priv: Pointer to a device private structure.
 * @sw_context: The software context used for this command batch validation.
 * @ptr: Pointer to the user-space handle to be translated.
 * @vmw_bo_p: Points to a location that, on successful return will carry
 * a reference-counted pointer to the DMA buffer identified by the
 * user-space handle in @id.
 *
 * This function saves information needed to translate a user-space buffer
 * handle to a valid SVGAGuestPtr. The translation does not take place
 * immediately, but during a call to vmw_apply_relocations().
 * This function builds a relocation list and a list of buffers to validate.
 * The former needs to be freed using either vmw_apply_relocations() or
 * vmw_free_relocations(). The latter needs to be freed using
 * vmw_clear_validations.
 */
static int vmw_translate_guest_ptr(struct vmw_private *dev_priv,
				   struct vmw_sw_context *sw_context,
				   SVGAGuestPtr *ptr,
				   struct vmw_dma_buffer **vmw_bo_p)
{
	struct vmw_dma_buffer *vmw_bo = NULL;
	struct ttm_buffer_object *bo;
	uint32_t handle = ptr->gmrId;
	struct vmw_relocation *reloc;
	int ret;

	ret = vmw_user_dmabuf_lookup(sw_context->fp->tfile, handle, &vmw_bo);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Could not find or use GMR region.\n");
		return -EINVAL;
	}
	bo = &vmw_bo->base;

	if (unlikely(sw_context->cur_reloc >= VMWGFX_MAX_RELOCATIONS)) {
		DRM_ERROR("Max number relocations per submission"
			  " exceeded\n");
		ret = -EINVAL;
		goto out_no_reloc;
	}

	reloc = &sw_context->relocs[sw_context->cur_reloc++];
	reloc->location = ptr;

	ret = vmw_bo_to_validate_list(sw_context, bo, false, &reloc->index);
	if (unlikely(ret != 0))
		goto out_no_reloc;

	*vmw_bo_p = vmw_bo;
	return 0;

out_no_reloc:
	vmw_dmabuf_unreference(&vmw_bo);
	vmw_bo_p = NULL;
	return ret;
}

/**
 * vmw_cmd_begin_gb_query - validate a  SVGA_3D_CMD_BEGIN_GB_QUERY command.
 *
 * @dev_priv: Pointer to a device private struct.
 * @sw_context: The software context used for this command submission.
 * @header: Pointer to the command header in the command stream.
 */
static int vmw_cmd_begin_gb_query(struct vmw_private *dev_priv,
				  struct vmw_sw_context *sw_context,
				  SVGA3dCmdHeader *header)
{
	struct vmw_begin_gb_query_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdBeginGBQuery q;
	} *cmd;

	cmd = container_of(header, struct vmw_begin_gb_query_cmd,
			   header);

	return vmw_cmd_res_check(dev_priv, sw_context, vmw_res_context,
				 user_context_converter, &cmd->q.cid,
				 NULL);
}

/**
 * vmw_cmd_begin_query - validate a  SVGA_3D_CMD_BEGIN_QUERY command.
 *
 * @dev_priv: Pointer to a device private struct.
 * @sw_context: The software context used for this command submission.
 * @header: Pointer to the command header in the command stream.
 */
static int vmw_cmd_begin_query(struct vmw_private *dev_priv,
			       struct vmw_sw_context *sw_context,
			       SVGA3dCmdHeader *header)
{
	struct vmw_begin_query_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdBeginQuery q;
	} *cmd;

	cmd = container_of(header, struct vmw_begin_query_cmd,
			   header);

	if (unlikely(dev_priv->has_mob)) {
		struct {
			SVGA3dCmdHeader header;
			SVGA3dCmdBeginGBQuery q;
		} gb_cmd;

		BUG_ON(sizeof(gb_cmd) != sizeof(*cmd));

		gb_cmd.header.id = SVGA_3D_CMD_BEGIN_GB_QUERY;
		gb_cmd.header.size = cmd->header.size;
		gb_cmd.q.cid = cmd->q.cid;
		gb_cmd.q.type = cmd->q.type;

		memcpy(cmd, &gb_cmd, sizeof(*cmd));
		return vmw_cmd_begin_gb_query(dev_priv, sw_context, header);
	}

	return vmw_cmd_res_check(dev_priv, sw_context, vmw_res_context,
				 user_context_converter, &cmd->q.cid,
				 NULL);
}

/**
 * vmw_cmd_end_gb_query - validate a  SVGA_3D_CMD_END_GB_QUERY command.
 *
 * @dev_priv: Pointer to a device private struct.
 * @sw_context: The software context used for this command submission.
 * @header: Pointer to the command header in the command stream.
 */
static int vmw_cmd_end_gb_query(struct vmw_private *dev_priv,
				struct vmw_sw_context *sw_context,
				SVGA3dCmdHeader *header)
{
	struct vmw_dma_buffer *vmw_bo;
	struct vmw_query_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdEndGBQuery q;
	} *cmd;
	int ret;

	cmd = container_of(header, struct vmw_query_cmd, header);
	ret = vmw_cmd_cid_check(dev_priv, sw_context, header);
	if (unlikely(ret != 0))
		return ret;

	ret = vmw_translate_mob_ptr(dev_priv, sw_context,
				    &cmd->q.mobid,
				    &vmw_bo);
	if (unlikely(ret != 0))
		return ret;

	ret = vmw_query_bo_switch_prepare(dev_priv, &vmw_bo->base, sw_context);

	vmw_dmabuf_unreference(&vmw_bo);
	return ret;
}

/**
 * vmw_cmd_end_query - validate a  SVGA_3D_CMD_END_QUERY command.
 *
 * @dev_priv: Pointer to a device private struct.
 * @sw_context: The software context used for this command submission.
 * @header: Pointer to the command header in the command stream.
 */
static int vmw_cmd_end_query(struct vmw_private *dev_priv,
			     struct vmw_sw_context *sw_context,
			     SVGA3dCmdHeader *header)
{
	struct vmw_dma_buffer *vmw_bo;
	struct vmw_query_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdEndQuery q;
	} *cmd;
	int ret;

	cmd = container_of(header, struct vmw_query_cmd, header);
	if (dev_priv->has_mob) {
		struct {
			SVGA3dCmdHeader header;
			SVGA3dCmdEndGBQuery q;
		} gb_cmd;

		BUG_ON(sizeof(gb_cmd) != sizeof(*cmd));

		gb_cmd.header.id = SVGA_3D_CMD_END_GB_QUERY;
		gb_cmd.header.size = cmd->header.size;
		gb_cmd.q.cid = cmd->q.cid;
		gb_cmd.q.type = cmd->q.type;
		gb_cmd.q.mobid = cmd->q.guestResult.gmrId;
		gb_cmd.q.offset = cmd->q.guestResult.offset;

		memcpy(cmd, &gb_cmd, sizeof(*cmd));
		return vmw_cmd_end_gb_query(dev_priv, sw_context, header);
	}

	ret = vmw_cmd_cid_check(dev_priv, sw_context, header);
	if (unlikely(ret != 0))
		return ret;

	ret = vmw_translate_guest_ptr(dev_priv, sw_context,
				      &cmd->q.guestResult,
				      &vmw_bo);
	if (unlikely(ret != 0))
		return ret;

	ret = vmw_query_bo_switch_prepare(dev_priv, &vmw_bo->base, sw_context);

	vmw_dmabuf_unreference(&vmw_bo);
	return ret;
}

/**
 * vmw_cmd_wait_gb_query - validate a  SVGA_3D_CMD_WAIT_GB_QUERY command.
 *
 * @dev_priv: Pointer to a device private struct.
 * @sw_context: The software context used for this command submission.
 * @header: Pointer to the command header in the command stream.
 */
static int vmw_cmd_wait_gb_query(struct vmw_private *dev_priv,
				 struct vmw_sw_context *sw_context,
				 SVGA3dCmdHeader *header)
{
	struct vmw_dma_buffer *vmw_bo;
	struct vmw_query_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdWaitForGBQuery q;
	} *cmd;
	int ret;

	cmd = container_of(header, struct vmw_query_cmd, header);
	ret = vmw_cmd_cid_check(dev_priv, sw_context, header);
	if (unlikely(ret != 0))
		return ret;

	ret = vmw_translate_mob_ptr(dev_priv, sw_context,
				    &cmd->q.mobid,
				    &vmw_bo);
	if (unlikely(ret != 0))
		return ret;

	vmw_dmabuf_unreference(&vmw_bo);
	return 0;
}

/**
 * vmw_cmd_wait_query - validate a  SVGA_3D_CMD_WAIT_QUERY command.
 *
 * @dev_priv: Pointer to a device private struct.
 * @sw_context: The software context used for this command submission.
 * @header: Pointer to the command header in the command stream.
 */
static int vmw_cmd_wait_query(struct vmw_private *dev_priv,
			      struct vmw_sw_context *sw_context,
			      SVGA3dCmdHeader *header)
{
	struct vmw_dma_buffer *vmw_bo;
	struct vmw_query_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdWaitForQuery q;
	} *cmd;
	int ret;

	cmd = container_of(header, struct vmw_query_cmd, header);
	if (dev_priv->has_mob) {
		struct {
			SVGA3dCmdHeader header;
			SVGA3dCmdWaitForGBQuery q;
		} gb_cmd;

		BUG_ON(sizeof(gb_cmd) != sizeof(*cmd));

		gb_cmd.header.id = SVGA_3D_CMD_WAIT_FOR_GB_QUERY;
		gb_cmd.header.size = cmd->header.size;
		gb_cmd.q.cid = cmd->q.cid;
		gb_cmd.q.type = cmd->q.type;
		gb_cmd.q.mobid = cmd->q.guestResult.gmrId;
		gb_cmd.q.offset = cmd->q.guestResult.offset;

		memcpy(cmd, &gb_cmd, sizeof(*cmd));
		return vmw_cmd_wait_gb_query(dev_priv, sw_context, header);
	}

	ret = vmw_cmd_cid_check(dev_priv, sw_context, header);
	if (unlikely(ret != 0))
		return ret;

	ret = vmw_translate_guest_ptr(dev_priv, sw_context,
				      &cmd->q.guestResult,
				      &vmw_bo);
	if (unlikely(ret != 0))
		return ret;

	vmw_dmabuf_unreference(&vmw_bo);
	return 0;
}

static int vmw_cmd_dma(struct vmw_private *dev_priv,
		       struct vmw_sw_context *sw_context,
		       SVGA3dCmdHeader *header)
{
	struct vmw_dma_buffer *vmw_bo = NULL;
	struct vmw_surface *srf = NULL;
	struct vmw_dma_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdSurfaceDMA dma;
	} *cmd;
	int ret;
	SVGA3dCmdSurfaceDMASuffix *suffix;
	uint32_t bo_size;

	cmd = container_of(header, struct vmw_dma_cmd, header);
	suffix = (SVGA3dCmdSurfaceDMASuffix *)((unsigned long) &cmd->dma +
					       header->size - sizeof(*suffix));

	/* Make sure device and verifier stays in sync. */
	if (unlikely(suffix->suffixSize != sizeof(*suffix))) {
		DRM_ERROR("Invalid DMA suffix size.\n");
		return -EINVAL;
	}

	ret = vmw_translate_guest_ptr(dev_priv, sw_context,
				      &cmd->dma.guest.ptr,
				      &vmw_bo);
	if (unlikely(ret != 0))
		return ret;

	/* Make sure DMA doesn't cross BO boundaries. */
	bo_size = vmw_bo->base.num_pages * PAGE_SIZE;
	if (unlikely(cmd->dma.guest.ptr.offset > bo_size)) {
		DRM_ERROR("Invalid DMA offset.\n");
		return -EINVAL;
	}

	bo_size -= cmd->dma.guest.ptr.offset;
	if (unlikely(suffix->maximumOffset > bo_size))
		suffix->maximumOffset = bo_size;

	ret = vmw_cmd_res_check(dev_priv, sw_context, vmw_res_surface,
				user_surface_converter, &cmd->dma.host.sid,
				NULL);
	if (unlikely(ret != 0)) {
		if (unlikely(ret != -ERESTARTSYS))
			DRM_ERROR("could not find surface for DMA.\n");
		goto out_no_surface;
	}

	srf = vmw_res_to_srf(sw_context->res_cache[vmw_res_surface].res);

	vmw_kms_cursor_snoop(srf, sw_context->fp->tfile, &vmw_bo->base,
			     header);

out_no_surface:
	vmw_dmabuf_unreference(&vmw_bo);
	return ret;
}

static int vmw_cmd_draw(struct vmw_private *dev_priv,
			struct vmw_sw_context *sw_context,
			SVGA3dCmdHeader *header)
{
	struct vmw_draw_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdDrawPrimitives body;
	} *cmd;
	SVGA3dVertexDecl *decl = (SVGA3dVertexDecl *)(
		(unsigned long)header + sizeof(*cmd));
	SVGA3dPrimitiveRange *range;
	uint32_t i;
	uint32_t maxnum;
	int ret;

	ret = vmw_cmd_cid_check(dev_priv, sw_context, header);
	if (unlikely(ret != 0))
		return ret;

	cmd = container_of(header, struct vmw_draw_cmd, header);
	maxnum = (header->size - sizeof(cmd->body)) / sizeof(*decl);

	if (unlikely(cmd->body.numVertexDecls > maxnum)) {
		DRM_ERROR("Illegal number of vertex declarations.\n");
		return -EINVAL;
	}

	for (i = 0; i < cmd->body.numVertexDecls; ++i, ++decl) {
		ret = vmw_cmd_res_check(dev_priv, sw_context, vmw_res_surface,
					user_surface_converter,
					&decl->array.surfaceId, NULL);
		if (unlikely(ret != 0))
			return ret;
	}

	maxnum = (header->size - sizeof(cmd->body) -
		  cmd->body.numVertexDecls * sizeof(*decl)) / sizeof(*range);
	if (unlikely(cmd->body.numRanges > maxnum)) {
		DRM_ERROR("Illegal number of index ranges.\n");
		return -EINVAL;
	}

	range = (SVGA3dPrimitiveRange *) decl;
	for (i = 0; i < cmd->body.numRanges; ++i, ++range) {
		ret = vmw_cmd_res_check(dev_priv, sw_context, vmw_res_surface,
					user_surface_converter,
					&range->indexArray.surfaceId, NULL);
		if (unlikely(ret != 0))
			return ret;
	}
	return 0;
}


static int vmw_cmd_tex_state(struct vmw_private *dev_priv,
			     struct vmw_sw_context *sw_context,
			     SVGA3dCmdHeader *header)
{
	struct vmw_tex_state_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdSetTextureState state;
	} *cmd;

	SVGA3dTextureState *last_state = (SVGA3dTextureState *)
	  ((unsigned long) header + header->size + sizeof(header));
	SVGA3dTextureState *cur_state = (SVGA3dTextureState *)
		((unsigned long) header + sizeof(struct vmw_tex_state_cmd));
	struct vmw_resource_val_node *ctx_node;
	struct vmw_resource_val_node *res_node;
	int ret;

	cmd = container_of(header, struct vmw_tex_state_cmd,
			   header);

	ret = vmw_cmd_res_check(dev_priv, sw_context, vmw_res_context,
				user_context_converter, &cmd->state.cid,
				&ctx_node);
	if (unlikely(ret != 0))
		return ret;

	for (; cur_state < last_state; ++cur_state) {
		if (likely(cur_state->name != SVGA3D_TS_BIND_TEXTURE))
			continue;

		ret = vmw_cmd_res_check(dev_priv, sw_context, vmw_res_surface,
					user_surface_converter,
					&cur_state->value, &res_node);
		if (unlikely(ret != 0))
			return ret;

		if (dev_priv->has_mob) {
			struct vmw_ctx_bindinfo bi;

			bi.ctx = ctx_node->res;
			bi.res = res_node ? res_node->res : NULL;
			bi.bt = vmw_ctx_binding_tex;
			bi.i1.texture_stage = cur_state->stage;
			vmw_context_binding_add(ctx_node->staged_bindings,
						&bi);
		}
	}

	return 0;
}

static int vmw_cmd_check_define_gmrfb(struct vmw_private *dev_priv,
				      struct vmw_sw_context *sw_context,
				      void *buf)
{
	struct vmw_dma_buffer *vmw_bo;
	int ret;

	struct {
		uint32_t header;
		SVGAFifoCmdDefineGMRFB body;
	} *cmd = buf;

	ret = vmw_translate_guest_ptr(dev_priv, sw_context,
				      &cmd->body.ptr,
				      &vmw_bo);
	if (unlikely(ret != 0))
		return ret;

	vmw_dmabuf_unreference(&vmw_bo);

	return ret;
}

/**
 * vmw_cmd_switch_backup - Utility function to handle backup buffer switching
 *
 * @dev_priv: Pointer to a device private struct.
 * @sw_context: The software context being used for this batch.
 * @res_type: The resource type.
 * @converter: Information about user-space binding for this resource type.
 * @res_id: Pointer to the user-space resource handle in the command stream.
 * @buf_id: Pointer to the user-space backup buffer handle in the command
 * stream.
 * @backup_offset: Offset of backup into MOB.
 *
 * This function prepares for registering a switch of backup buffers
 * in the resource metadata just prior to unreserving.
 */
static int vmw_cmd_switch_backup(struct vmw_private *dev_priv,
				 struct vmw_sw_context *sw_context,
				 enum vmw_res_type res_type,
				 const struct vmw_user_resource_conv
				 *converter,
				 uint32_t *res_id,
				 uint32_t *buf_id,
				 unsigned long backup_offset)
{
	int ret;
	struct vmw_dma_buffer *dma_buf;
	struct vmw_resource_val_node *val_node;

	ret = vmw_cmd_res_check(dev_priv, sw_context, res_type,
				converter, res_id, &val_node);
	if (unlikely(ret != 0))
		return ret;

	ret = vmw_translate_mob_ptr(dev_priv, sw_context, buf_id, &dma_buf);
	if (unlikely(ret != 0))
		return ret;

	if (val_node->first_usage)
		val_node->no_buffer_needed = true;

	vmw_dmabuf_unreference(&val_node->new_backup);
	val_node->new_backup = dma_buf;
	val_node->new_backup_offset = backup_offset;

	return 0;
}

/**
 * vmw_cmd_bind_gb_surface - Validate an SVGA_3D_CMD_BIND_GB_SURFACE
 * command
 *
 * @dev_priv: Pointer to a device private struct.
 * @sw_context: The software context being used for this batch.
 * @header: Pointer to the command header in the command stream.
 */
static int vmw_cmd_bind_gb_surface(struct vmw_private *dev_priv,
				   struct vmw_sw_context *sw_context,
				   SVGA3dCmdHeader *header)
{
	struct vmw_bind_gb_surface_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdBindGBSurface body;
	} *cmd;

	cmd = container_of(header, struct vmw_bind_gb_surface_cmd, header);

	return vmw_cmd_switch_backup(dev_priv, sw_context, vmw_res_surface,
				     user_surface_converter,
				     &cmd->body.sid, &cmd->body.mobid,
				     0);
}

/**
 * vmw_cmd_update_gb_image - Validate an SVGA_3D_CMD_UPDATE_GB_IMAGE
 * command
 *
 * @dev_priv: Pointer to a device private struct.
 * @sw_context: The software context being used for this batch.
 * @header: Pointer to the command header in the command stream.
 */
static int vmw_cmd_update_gb_image(struct vmw_private *dev_priv,
				   struct vmw_sw_context *sw_context,
				   SVGA3dCmdHeader *header)
{
	struct vmw_gb_surface_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdUpdateGBImage body;
	} *cmd;

	cmd = container_of(header, struct vmw_gb_surface_cmd, header);

	return vmw_cmd_res_check(dev_priv, sw_context, vmw_res_surface,
				 user_surface_converter,
				 &cmd->body.image.sid, NULL);
}

/**
 * vmw_cmd_update_gb_surface - Validate an SVGA_3D_CMD_UPDATE_GB_SURFACE
 * command
 *
 * @dev_priv: Pointer to a device private struct.
 * @sw_context: The software context being used for this batch.
 * @header: Pointer to the command header in the command stream.
 */
static int vmw_cmd_update_gb_surface(struct vmw_private *dev_priv,
				     struct vmw_sw_context *sw_context,
				     SVGA3dCmdHeader *header)
{
	struct vmw_gb_surface_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdUpdateGBSurface body;
	} *cmd;

	cmd = container_of(header, struct vmw_gb_surface_cmd, header);

	return vmw_cmd_res_check(dev_priv, sw_context, vmw_res_surface,
				 user_surface_converter,
				 &cmd->body.sid, NULL);
}

/**
 * vmw_cmd_readback_gb_image - Validate an SVGA_3D_CMD_READBACK_GB_IMAGE
 * command
 *
 * @dev_priv: Pointer to a device private struct.
 * @sw_context: The software context being used for this batch.
 * @header: Pointer to the command header in the command stream.
 */
static int vmw_cmd_readback_gb_image(struct vmw_private *dev_priv,
				     struct vmw_sw_context *sw_context,
				     SVGA3dCmdHeader *header)
{
	struct vmw_gb_surface_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdReadbackGBImage body;
	} *cmd;

	cmd = container_of(header, struct vmw_gb_surface_cmd, header);

	return vmw_cmd_res_check(dev_priv, sw_context, vmw_res_surface,
				 user_surface_converter,
				 &cmd->body.image.sid, NULL);
}

/**
 * vmw_cmd_readback_gb_surface - Validate an SVGA_3D_CMD_READBACK_GB_SURFACE
 * command
 *
 * @dev_priv: Pointer to a device private struct.
 * @sw_context: The software context being used for this batch.
 * @header: Pointer to the command header in the command stream.
 */
static int vmw_cmd_readback_gb_surface(struct vmw_private *dev_priv,
				       struct vmw_sw_context *sw_context,
				       SVGA3dCmdHeader *header)
{
	struct vmw_gb_surface_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdReadbackGBSurface body;
	} *cmd;

	cmd = container_of(header, struct vmw_gb_surface_cmd, header);

	return vmw_cmd_res_check(dev_priv, sw_context, vmw_res_surface,
				 user_surface_converter,
				 &cmd->body.sid, NULL);
}

/**
 * vmw_cmd_invalidate_gb_image - Validate an SVGA_3D_CMD_INVALIDATE_GB_IMAGE
 * command
 *
 * @dev_priv: Pointer to a device private struct.
 * @sw_context: The software context being used for this batch.
 * @header: Pointer to the command header in the command stream.
 */
static int vmw_cmd_invalidate_gb_image(struct vmw_private *dev_priv,
				       struct vmw_sw_context *sw_context,
				       SVGA3dCmdHeader *header)
{
	struct vmw_gb_surface_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdInvalidateGBImage body;
	} *cmd;

	cmd = container_of(header, struct vmw_gb_surface_cmd, header);

	return vmw_cmd_res_check(dev_priv, sw_context, vmw_res_surface,
				 user_surface_converter,
				 &cmd->body.image.sid, NULL);
}

/**
 * vmw_cmd_invalidate_gb_surface - Validate an
 * SVGA_3D_CMD_INVALIDATE_GB_SURFACE command
 *
 * @dev_priv: Pointer to a device private struct.
 * @sw_context: The software context being used for this batch.
 * @header: Pointer to the command header in the command stream.
 */
static int vmw_cmd_invalidate_gb_surface(struct vmw_private *dev_priv,
					 struct vmw_sw_context *sw_context,
					 SVGA3dCmdHeader *header)
{
	struct vmw_gb_surface_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdInvalidateGBSurface body;
	} *cmd;

	cmd = container_of(header, struct vmw_gb_surface_cmd, header);

	return vmw_cmd_res_check(dev_priv, sw_context, vmw_res_surface,
				 user_surface_converter,
				 &cmd->body.sid, NULL);
}


/**
 * vmw_cmd_shader_define - Validate an SVGA_3D_CMD_SHADER_DEFINE
 * command
 *
 * @dev_priv: Pointer to a device private struct.
 * @sw_context: The software context being used for this batch.
 * @header: Pointer to the command header in the command stream.
 */
static int vmw_cmd_shader_define(struct vmw_private *dev_priv,
				 struct vmw_sw_context *sw_context,
				 SVGA3dCmdHeader *header)
{
	struct vmw_shader_define_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdDefineShader body;
	} *cmd;
	int ret;
	size_t size;
	struct vmw_resource_val_node *val;

	cmd = container_of(header, struct vmw_shader_define_cmd,
			   header);

	ret = vmw_cmd_res_check(dev_priv, sw_context, vmw_res_context,
				user_context_converter, &cmd->body.cid,
				&val);
	if (unlikely(ret != 0))
		return ret;

	if (unlikely(!dev_priv->has_mob))
		return 0;

	size = cmd->header.size - sizeof(cmd->body);
	ret = vmw_compat_shader_add(dev_priv,
				    vmw_context_res_man(val->res),
				    cmd->body.shid, cmd + 1,
				    cmd->body.type, size,
				    &sw_context->staged_cmd_res);
	if (unlikely(ret != 0))
		return ret;

	return vmw_resource_relocation_add(&sw_context->res_relocations,
					   NULL, &cmd->header.id -
					   sw_context->buf_start);

	return 0;
}

/**
 * vmw_cmd_shader_destroy - Validate an SVGA_3D_CMD_SHADER_DESTROY
 * command
 *
 * @dev_priv: Pointer to a device private struct.
 * @sw_context: The software context being used for this batch.
 * @header: Pointer to the command header in the command stream.
 */
static int vmw_cmd_shader_destroy(struct vmw_private *dev_priv,
				  struct vmw_sw_context *sw_context,
				  SVGA3dCmdHeader *header)
{
	struct vmw_shader_destroy_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdDestroyShader body;
	} *cmd;
	int ret;
	struct vmw_resource_val_node *val;

	cmd = container_of(header, struct vmw_shader_destroy_cmd,
			   header);

	ret = vmw_cmd_res_check(dev_priv, sw_context, vmw_res_context,
				user_context_converter, &cmd->body.cid,
				&val);
	if (unlikely(ret != 0))
		return ret;

	if (unlikely(!dev_priv->has_mob))
		return 0;

	ret = vmw_compat_shader_remove(vmw_context_res_man(val->res),
				       cmd->body.shid,
				       cmd->body.type,
				       &sw_context->staged_cmd_res);
	if (unlikely(ret != 0))
		return ret;

	return vmw_resource_relocation_add(&sw_context->res_relocations,
					   NULL, &cmd->header.id -
					   sw_context->buf_start);

	return 0;
}

/**
 * vmw_cmd_set_shader - Validate an SVGA_3D_CMD_SET_SHADER
 * command
 *
 * @dev_priv: Pointer to a device private struct.
 * @sw_context: The software context being used for this batch.
 * @header: Pointer to the command header in the command stream.
 */
static int vmw_cmd_set_shader(struct vmw_private *dev_priv,
			      struct vmw_sw_context *sw_context,
			      SVGA3dCmdHeader *header)
{
	struct vmw_set_shader_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdSetShader body;
	} *cmd;
	struct vmw_resource_val_node *ctx_node, *res_node = NULL;
	struct vmw_ctx_bindinfo bi;
	struct vmw_resource *res = NULL;
	int ret;

	cmd = container_of(header, struct vmw_set_shader_cmd,
			   header);

	ret = vmw_cmd_res_check(dev_priv, sw_context, vmw_res_context,
				user_context_converter, &cmd->body.cid,
				&ctx_node);
	if (unlikely(ret != 0))
		return ret;

	if (!dev_priv->has_mob)
		return 0;

	if (cmd->body.shid != SVGA3D_INVALID_ID) {
		res = vmw_compat_shader_lookup
			(vmw_context_res_man(ctx_node->res),
			 cmd->body.shid,
			 cmd->body.type);

		if (!IS_ERR(res)) {
			ret = vmw_cmd_res_reloc_add(dev_priv, sw_context,
						    vmw_res_shader,
						    &cmd->body.shid, res,
						    &res_node);
			vmw_resource_unreference(&res);
			if (unlikely(ret != 0))
				return ret;
		}
	}

	if (!res_node) {
		ret = vmw_cmd_res_check(dev_priv, sw_context,
					vmw_res_shader,
					user_shader_converter,
					&cmd->body.shid, &res_node);
		if (unlikely(ret != 0))
			return ret;
	}

	bi.ctx = ctx_node->res;
	bi.res = res_node ? res_node->res : NULL;
	bi.bt = vmw_ctx_binding_shader;
	bi.i1.shader_type = cmd->body.type;
	return vmw_context_binding_add(ctx_node->staged_bindings, &bi);
}

/**
 * vmw_cmd_set_shader_const - Validate an SVGA_3D_CMD_SET_SHADER_CONST
 * command
 *
 * @dev_priv: Pointer to a device private struct.
 * @sw_context: The software context being used for this batch.
 * @header: Pointer to the command header in the command stream.
 */
static int vmw_cmd_set_shader_const(struct vmw_private *dev_priv,
				    struct vmw_sw_context *sw_context,
				    SVGA3dCmdHeader *header)
{
	struct vmw_set_shader_const_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdSetShaderConst body;
	} *cmd;
	int ret;

	cmd = container_of(header, struct vmw_set_shader_const_cmd,
			   header);

	ret = vmw_cmd_res_check(dev_priv, sw_context, vmw_res_context,
				user_context_converter, &cmd->body.cid,
				NULL);
	if (unlikely(ret != 0))
		return ret;

	if (dev_priv->has_mob)
		header->id = SVGA_3D_CMD_SET_GB_SHADERCONSTS_INLINE;

	return 0;
}

/**
 * vmw_cmd_bind_gb_shader - Validate an SVGA_3D_CMD_BIND_GB_SHADER
 * command
 *
 * @dev_priv: Pointer to a device private struct.
 * @sw_context: The software context being used for this batch.
 * @header: Pointer to the command header in the command stream.
 */
static int vmw_cmd_bind_gb_shader(struct vmw_private *dev_priv,
				  struct vmw_sw_context *sw_context,
				  SVGA3dCmdHeader *header)
{
	struct vmw_bind_gb_shader_cmd {
		SVGA3dCmdHeader header;
		SVGA3dCmdBindGBShader body;
	} *cmd;

	cmd = container_of(header, struct vmw_bind_gb_shader_cmd,
			   header);

	return vmw_cmd_switch_backup(dev_priv, sw_context, vmw_res_shader,
				     user_shader_converter,
				     &cmd->body.shid, &cmd->body.mobid,
				     cmd->body.offsetInBytes);
}

static int vmw_cmd_check_not_3d(struct vmw_private *dev_priv,
				struct vmw_sw_context *sw_context,
				void *buf, uint32_t *size)
{
	uint32_t size_remaining = *size;
	uint32_t cmd_id;

	cmd_id = le32_to_cpu(((uint32_t *)buf)[0]);
	switch (cmd_id) {
	case SVGA_CMD_UPDATE:
		*size = sizeof(uint32_t) + sizeof(SVGAFifoCmdUpdate);
		break;
	case SVGA_CMD_DEFINE_GMRFB:
		*size = sizeof(uint32_t) + sizeof(SVGAFifoCmdDefineGMRFB);
		break;
	case SVGA_CMD_BLIT_GMRFB_TO_SCREEN:
		*size = sizeof(uint32_t) + sizeof(SVGAFifoCmdBlitGMRFBToScreen);
		break;
	case SVGA_CMD_BLIT_SCREEN_TO_GMRFB:
		*size = sizeof(uint32_t) + sizeof(SVGAFifoCmdBlitGMRFBToScreen);
		break;
	default:
		DRM_ERROR("Unsupported SVGA command: %u.\n", cmd_id);
		return -EINVAL;
	}

	if (*size > size_remaining) {
		DRM_ERROR("Invalid SVGA command (size mismatch):"
			  " %u.\n", cmd_id);
		return -EINVAL;
	}

	if (unlikely(!sw_context->kernel)) {
		DRM_ERROR("Kernel only SVGA command: %u.\n", cmd_id);
		return -EPERM;
	}

	if (cmd_id == SVGA_CMD_DEFINE_GMRFB)
		return vmw_cmd_check_define_gmrfb(dev_priv, sw_context, buf);

	return 0;
}

static const struct vmw_cmd_entry vmw_cmd_entries[SVGA_3D_CMD_MAX] = {
	VMW_CMD_DEF(SVGA_3D_CMD_SURFACE_DEFINE, &vmw_cmd_invalid,
		    false, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_SURFACE_DESTROY, &vmw_cmd_invalid,
		    false, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_SURFACE_COPY, &vmw_cmd_surface_copy_check,
		    true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_SURFACE_STRETCHBLT, &vmw_cmd_stretch_blt_check,
		    true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_SURFACE_DMA, &vmw_cmd_dma,
		    true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_CONTEXT_DEFINE, &vmw_cmd_invalid,
		    false, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_CONTEXT_DESTROY, &vmw_cmd_invalid,
		    false, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_SETTRANSFORM, &vmw_cmd_cid_check,
		    true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_SETZRANGE, &vmw_cmd_cid_check,
		    true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_SETRENDERSTATE, &vmw_cmd_cid_check,
		    true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_SETRENDERTARGET,
		    &vmw_cmd_set_render_target_check, true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_SETTEXTURESTATE, &vmw_cmd_tex_state,
		    true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_SETMATERIAL, &vmw_cmd_cid_check,
		    true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_SETLIGHTDATA, &vmw_cmd_cid_check,
		    true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_SETLIGHTENABLED, &vmw_cmd_cid_check,
		    true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_SETVIEWPORT, &vmw_cmd_cid_check,
		    true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_SETCLIPPLANE, &vmw_cmd_cid_check,
		    true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_CLEAR, &vmw_cmd_cid_check,
		    true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_PRESENT, &vmw_cmd_present_check,
		    false, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_SHADER_DEFINE, &vmw_cmd_shader_define,
		    true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_SHADER_DESTROY, &vmw_cmd_shader_destroy,
		    true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_SET_SHADER, &vmw_cmd_set_shader,
		    true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_SET_SHADER_CONST, &vmw_cmd_set_shader_const,
		    true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_DRAW_PRIMITIVES, &vmw_cmd_draw,
		    true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_SETSCISSORRECT, &vmw_cmd_cid_check,
		    true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_BEGIN_QUERY, &vmw_cmd_begin_query,
		    true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_END_QUERY, &vmw_cmd_end_query,
		    true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_WAIT_FOR_QUERY, &vmw_cmd_wait_query,
		    true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_PRESENT_READBACK, &vmw_cmd_ok,
		    true, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_BLIT_SURFACE_TO_SCREEN,
		    &vmw_cmd_blt_surf_screen_check, false, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_SURFACE_DEFINE_V2, &vmw_cmd_invalid,
		    false, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_GENERATE_MIPMAPS, &vmw_cmd_invalid,
		    false, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_ACTIVATE_SURFACE, &vmw_cmd_invalid,
		    false, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_DEACTIVATE_SURFACE, &vmw_cmd_invalid,
		    false, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_SCREEN_DMA, &vmw_cmd_invalid,
		    false, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_SET_UNITY_SURFACE_COOKIE, &vmw_cmd_invalid,
		    false, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_OPEN_CONTEXT_SURFACE, &vmw_cmd_invalid,
		    false, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_LOGICOPS_BITBLT, &vmw_cmd_invalid,
		    false, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_LOGICOPS_TRANSBLT, &vmw_cmd_invalid,
		    false, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_LOGICOPS_STRETCHBLT, &vmw_cmd_invalid,
		    false, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_LOGICOPS_COLORFILL, &vmw_cmd_invalid,
		    false, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_LOGICOPS_ALPHABLEND, &vmw_cmd_invalid,
		    false, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_LOGICOPS_CLEARTYPEBLEND, &vmw_cmd_invalid,
		    false, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_SET_OTABLE_BASE, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_READBACK_OTABLE, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_DEFINE_GB_MOB, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_DESTROY_GB_MOB, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_REDEFINE_GB_MOB, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_UPDATE_GB_MOB_MAPPING, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_DEFINE_GB_SURFACE, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_DESTROY_GB_SURFACE, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_BIND_GB_SURFACE, &vmw_cmd_bind_gb_surface,
		    true, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_COND_BIND_GB_SURFACE, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_UPDATE_GB_IMAGE, &vmw_cmd_update_gb_image,
		    true, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_UPDATE_GB_SURFACE,
		    &vmw_cmd_update_gb_surface, true, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_READBACK_GB_IMAGE,
		    &vmw_cmd_readback_gb_image, true, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_READBACK_GB_SURFACE,
		    &vmw_cmd_readback_gb_surface, true, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_INVALIDATE_GB_IMAGE,
		    &vmw_cmd_invalidate_gb_image, true, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_INVALIDATE_GB_SURFACE,
		    &vmw_cmd_invalidate_gb_surface, true, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_DEFINE_GB_CONTEXT, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_DESTROY_GB_CONTEXT, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_BIND_GB_CONTEXT, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_READBACK_GB_CONTEXT, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_INVALIDATE_GB_CONTEXT, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_DEFINE_GB_SHADER, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_BIND_GB_SHADER, &vmw_cmd_bind_gb_shader,
		    true, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_DESTROY_GB_SHADER, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_SET_OTABLE_BASE64, &vmw_cmd_invalid,
		    false, false, false),
	VMW_CMD_DEF(SVGA_3D_CMD_BEGIN_GB_QUERY, &vmw_cmd_begin_gb_query,
		    true, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_END_GB_QUERY, &vmw_cmd_end_gb_query,
		    true, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_WAIT_FOR_GB_QUERY, &vmw_cmd_wait_gb_query,
		    true, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_NOP, &vmw_cmd_ok,
		    true, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_ENABLE_GART, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_DISABLE_GART, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_MAP_MOB_INTO_GART, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_UNMAP_GART_RANGE, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_DEFINE_GB_SCREENTARGET, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_DESTROY_GB_SCREENTARGET, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_BIND_GB_SCREENTARGET, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_UPDATE_GB_SCREENTARGET, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_READBACK_GB_IMAGE_PARTIAL, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_INVALIDATE_GB_IMAGE_PARTIAL, &vmw_cmd_invalid,
		    false, false, true),
	VMW_CMD_DEF(SVGA_3D_CMD_SET_GB_SHADERCONSTS_INLINE, &vmw_cmd_cid_check,
		    true, false, true)
};

static int vmw_cmd_check(struct vmw_private *dev_priv,
			 struct vmw_sw_context *sw_context,
			 void *buf, uint32_t *size)
{
	uint32_t cmd_id;
	uint32_t size_remaining = *size;
	SVGA3dCmdHeader *header = (SVGA3dCmdHeader *) buf;
	int ret;
	const struct vmw_cmd_entry *entry;
	bool gb = dev_priv->capabilities & SVGA_CAP_GBOBJECTS;

	cmd_id = le32_to_cpu(((uint32_t *)buf)[0]);
	/* Handle any none 3D commands */
	if (unlikely(cmd_id < SVGA_CMD_MAX))
		return vmw_cmd_check_not_3d(dev_priv, sw_context, buf, size);


	cmd_id = le32_to_cpu(header->id);
	*size = le32_to_cpu(header->size) + sizeof(SVGA3dCmdHeader);

	cmd_id -= SVGA_3D_CMD_BASE;
	if (unlikely(*size > size_remaining))
		goto out_invalid;

	if (unlikely(cmd_id >= SVGA_3D_CMD_MAX - SVGA_3D_CMD_BASE))
		goto out_invalid;

	entry = &vmw_cmd_entries[cmd_id];
	if (unlikely(!entry->func))
		goto out_invalid;

	if (unlikely(!entry->user_allow && !sw_context->kernel))
		goto out_privileged;

	if (unlikely(entry->gb_disable && gb))
		goto out_old;

	if (unlikely(entry->gb_enable && !gb))
		goto out_new;

	ret = entry->func(dev_priv, sw_context, header);
	if (unlikely(ret != 0))
		goto out_invalid;

	return 0;
out_invalid:
	DRM_ERROR("Invalid SVGA3D command: %d\n",
		  cmd_id + SVGA_3D_CMD_BASE);
	return -EINVAL;
out_privileged:
	DRM_ERROR("Privileged SVGA3D command: %d\n",
		  cmd_id + SVGA_3D_CMD_BASE);
	return -EPERM;
out_old:
	DRM_ERROR("Deprecated (disallowed) SVGA3D command: %d\n",
		  cmd_id + SVGA_3D_CMD_BASE);
	return -EINVAL;
out_new:
	DRM_ERROR("SVGA3D command: %d not supported by virtual hardware.\n",
		  cmd_id + SVGA_3D_CMD_BASE);
	return -EINVAL;
}

static int vmw_cmd_check_all(struct vmw_private *dev_priv,
			     struct vmw_sw_context *sw_context,
			     void *buf,
			     uint32_t size)
{
	int32_t cur_size = size;
	int ret;

	sw_context->buf_start = buf;

	while (cur_size > 0) {
		size = cur_size;
		ret = vmw_cmd_check(dev_priv, sw_context, buf, &size);
		if (unlikely(ret != 0))
			return ret;
		buf = (void *)((unsigned long) buf + size);
		cur_size -= size;
	}

	if (unlikely(cur_size != 0)) {
		DRM_ERROR("Command verifier out of sync.\n");
		return -EINVAL;
	}

	return 0;
}

static void vmw_free_relocations(struct vmw_sw_context *sw_context)
{
	sw_context->cur_reloc = 0;
}

static void vmw_apply_relocations(struct vmw_sw_context *sw_context)
{
	uint32_t i;
	struct vmw_relocation *reloc;
	struct ttm_validate_buffer *validate;
	struct ttm_buffer_object *bo;

	for (i = 0; i < sw_context->cur_reloc; ++i) {
		reloc = &sw_context->relocs[i];
		validate = &sw_context->val_bufs[reloc->index].base;
		bo = validate->bo;
		switch (bo->mem.mem_type) {
		case TTM_PL_VRAM:
			reloc->location->offset += bo->offset;
			reloc->location->gmrId = SVGA_GMR_FRAMEBUFFER;
			break;
		case VMW_PL_GMR:
			reloc->location->gmrId = bo->mem.start;
			break;
		case VMW_PL_MOB:
			*reloc->mob_loc = bo->mem.start;
			break;
		default:
			BUG();
		}
	}
	vmw_free_relocations(sw_context);
}

/**
 * vmw_resource_list_unrefererence - Free up a resource list and unreference
 * all resources referenced by it.
 *
 * @list: The resource list.
 */
static void vmw_resource_list_unreference(struct list_head *list)
{
	struct vmw_resource_val_node *val, *val_next;

	/*
	 * Drop references to resources held during command submission.
	 */

	list_for_each_entry_safe(val, val_next, list, head) {
		list_del_init(&val->head);
		vmw_resource_unreference(&val->res);
		if (unlikely(val->staged_bindings))
			kfree(val->staged_bindings);
		kfree(val);
	}
}

static void vmw_clear_validations(struct vmw_sw_context *sw_context)
{
	struct vmw_validate_buffer *entry, *next;
	struct vmw_resource_val_node *val;

	/*
	 * Drop references to DMA buffers held during command submission.
	 */
	list_for_each_entry_safe(entry, next, &sw_context->validate_nodes,
				 base.head) {
		list_del(&entry->base.head);
		ttm_bo_unref(&entry->base.bo);
		(void) drm_ht_remove_item(&sw_context->res_ht, &entry->hash);
		sw_context->cur_val_buf--;
	}
	BUG_ON(sw_context->cur_val_buf != 0);

	list_for_each_entry(val, &sw_context->resource_list, head)
		(void) drm_ht_remove_item(&sw_context->res_ht, &val->hash);
}

static int vmw_validate_single_buffer(struct vmw_private *dev_priv,
				      struct ttm_buffer_object *bo,
				      bool validate_as_mob)
{
	int ret;


	/*
	 * Don't validate pinned buffers.
	 */

	if (bo == dev_priv->pinned_bo ||
	    (bo == dev_priv->dummy_query_bo &&
	     dev_priv->dummy_query_bo_pinned))
		return 0;

	if (validate_as_mob)
		return ttm_bo_validate(bo, &vmw_mob_placement, true, false);

	/**
	 * Put BO in VRAM if there is space, otherwise as a GMR.
	 * If there is no space in VRAM and GMR ids are all used up,
	 * start evicting GMRs to make room. If the DMA buffer can't be
	 * used as a GMR, this will return -ENOMEM.
	 */

	ret = ttm_bo_validate(bo, &vmw_vram_gmr_placement, true, false);
	if (likely(ret == 0 || ret == -ERESTARTSYS))
		return ret;

	/**
	 * If that failed, try VRAM again, this time evicting
	 * previous contents.
	 */

	DRM_INFO("Falling through to VRAM.\n");
	ret = ttm_bo_validate(bo, &vmw_vram_placement, true, false);
	return ret;
}

static int vmw_validate_buffers(struct vmw_private *dev_priv,
				struct vmw_sw_context *sw_context)
{
	struct vmw_validate_buffer *entry;
	int ret;

	list_for_each_entry(entry, &sw_context->validate_nodes, base.head) {
		ret = vmw_validate_single_buffer(dev_priv, entry->base.bo,
						 entry->validate_as_mob);
		if (unlikely(ret != 0))
			return ret;
	}
	return 0;
}

static int vmw_resize_cmd_bounce(struct vmw_sw_context *sw_context,
				 uint32_t size)
{
	if (likely(sw_context->cmd_bounce_size >= size))
		return 0;

	if (sw_context->cmd_bounce_size == 0)
		sw_context->cmd_bounce_size = VMWGFX_CMD_BOUNCE_INIT_SIZE;

	while (sw_context->cmd_bounce_size < size) {
		sw_context->cmd_bounce_size =
			PAGE_ALIGN(sw_context->cmd_bounce_size +
				   (sw_context->cmd_bounce_size >> 1));
	}

	if (sw_context->cmd_bounce != NULL)
		vfree(sw_context->cmd_bounce);

	sw_context->cmd_bounce = vmalloc(sw_context->cmd_bounce_size);

	if (sw_context->cmd_bounce == NULL) {
		DRM_ERROR("Failed to allocate command bounce buffer.\n");
		sw_context->cmd_bounce_size = 0;
		return -ENOMEM;
	}

	return 0;
}

/**
 * vmw_execbuf_fence_commands - create and submit a command stream fence
 *
 * Creates a fence object and submits a command stream marker.
 * If this fails for some reason, We sync the fifo and return NULL.
 * It is then safe to fence buffers with a NULL pointer.
 *
 * If @p_handle is not NULL @file_priv must also not be NULL. Creates
 * a userspace handle if @p_handle is not NULL, otherwise not.
 */

int vmw_execbuf_fence_commands(struct drm_file *file_priv,
			       struct vmw_private *dev_priv,
			       struct vmw_fence_obj **p_fence,
			       uint32_t *p_handle)
{
	uint32_t sequence;
	int ret;
	bool synced = false;

	/* p_handle implies file_priv. */
	BUG_ON(p_handle != NULL && file_priv == NULL);

	ret = vmw_fifo_send_fence(dev_priv, &sequence);
	if (unlikely(ret != 0)) {
		DRM_ERROR("Fence submission error. Syncing.\n");
		synced = true;
	}

	if (p_handle != NULL)
		ret = vmw_user_fence_create(file_priv, dev_priv->fman,
					    sequence, p_fence, p_handle);
	else
		ret = vmw_fence_create(dev_priv->fman, sequence, p_fence);

	if (unlikely(ret != 0 && !synced)) {
		(void) vmw_fallback_wait(dev_priv, false, false,
					 sequence, false,
					 VMW_FENCE_WAIT_TIMEOUT);
		*p_fence = NULL;
	}

	return 0;
}

/**
 * vmw_execbuf_copy_fence_user - copy fence object information to
 * user-space.
 *
 * @dev_priv: Pointer to a vmw_private struct.
 * @vmw_fp: Pointer to the struct vmw_fpriv representing the calling file.
 * @ret: Return value from fence object creation.
 * @user_fence_rep: User space address of a struct drm_vmw_fence_rep to
 * which the information should be copied.
 * @fence: Pointer to the fenc object.
 * @fence_handle: User-space fence handle.
 *
 * This function copies fence information to user-space. If copying fails,
 * The user-space struct drm_vmw_fence_rep::error member is hopefully
 * left untouched, and if it's preloaded with an -EFAULT by user-space,
 * the error will hopefully be detected.
 * Also if copying fails, user-space will be unable to signal the fence
 * object so we wait for it immediately, and then unreference the
 * user-space reference.
 */
void
vmw_execbuf_copy_fence_user(struct vmw_private *dev_priv,
			    struct vmw_fpriv *vmw_fp,
			    int ret,
			    struct drm_vmw_fence_rep __user *user_fence_rep,
			    struct vmw_fence_obj *fence,
			    uint32_t fence_handle)
{
	struct drm_vmw_fence_rep fence_rep;

	if (user_fence_rep == NULL)
		return;

	memset(&fence_rep, 0, sizeof(fence_rep));

	fence_rep.error = ret;
	if (ret == 0) {
		BUG_ON(fence == NULL);

		fence_rep.handle = fence_handle;
		fence_rep.seqno = fence->base.seqno;
		vmw_update_seqno(dev_priv, &dev_priv->fifo);
		fence_rep.passed_seqno = dev_priv->last_read_seqno;
	}

	/*
	 * copy_to_user errors will be detected by user space not
	 * seeing fence_rep::error filled in. Typically
	 * user-space would have pre-set that member to -EFAULT.
	 */
	ret = copy_to_user(user_fence_rep, &fence_rep,
			   sizeof(fence_rep));

	/*
	 * User-space lost the fence object. We need to sync
	 * and unreference the handle.
	 */
	if (unlikely(ret != 0) && (fence_rep.error == 0)) {
		ttm_ref_object_base_unref(vmw_fp->tfile,
					  fence_handle, TTM_REF_USAGE);
		DRM_ERROR("Fence copy error. Syncing.\n");
		(void) vmw_fence_obj_wait(fence, false, false,
					  VMW_FENCE_WAIT_TIMEOUT);
	}
}



int vmw_execbuf_process(struct drm_file *file_priv,
			struct vmw_private *dev_priv,
			void __user *user_commands,
			void *kernel_commands,
			uint32_t command_size,
			uint64_t throttle_us,
			struct drm_vmw_fence_rep __user *user_fence_rep,
			struct vmw_fence_obj **out_fence)
{
	struct vmw_sw_context *sw_context = &dev_priv->ctx;
	struct vmw_fence_obj *fence = NULL;
	struct vmw_resource *error_resource;
	struct list_head resource_list;
	struct ww_acquire_ctx ticket;
	uint32_t handle;
	void *cmd;
	int ret;

	ret = mutex_lock_interruptible(&dev_priv->cmdbuf_mutex);
	if (unlikely(ret != 0))
		return -ERESTARTSYS;

	if (kernel_commands == NULL) {
		sw_context->kernel = false;

		ret = vmw_resize_cmd_bounce(sw_context, command_size);
		if (unlikely(ret != 0))
			goto out_unlock;


		ret = copy_from_user(sw_context->cmd_bounce,
				     user_commands, command_size);

		if (unlikely(ret != 0)) {
			ret = -EFAULT;
			DRM_ERROR("Failed copying commands.\n");
			goto out_unlock;
		}
		kernel_commands = sw_context->cmd_bounce;
	} else
		sw_context->kernel = true;

	sw_context->fp = vmw_fpriv(file_priv);
	sw_context->cur_reloc = 0;
	sw_context->cur_val_buf = 0;
	INIT_LIST_HEAD(&sw_context->resource_list);
	sw_context->cur_query_bo = dev_priv->pinned_bo;
	sw_context->last_query_ctx = NULL;
	sw_context->needs_post_query_barrier = false;
	memset(sw_context->res_cache, 0, sizeof(sw_context->res_cache));
	INIT_LIST_HEAD(&sw_context->validate_nodes);
	INIT_LIST_HEAD(&sw_context->res_relocations);
	if (!sw_context->res_ht_initialized) {
		ret = drm_ht_create(&sw_context->res_ht, VMW_RES_HT_ORDER);
		if (unlikely(ret != 0))
			goto out_unlock;
		sw_context->res_ht_initialized = true;
	}
	INIT_LIST_HEAD(&sw_context->staged_cmd_res);

	INIT_LIST_HEAD(&resource_list);
	ret = vmw_cmd_check_all(dev_priv, sw_context, kernel_commands,
				command_size);
	if (unlikely(ret != 0))
		goto out_err_nores;

	ret = vmw_resources_reserve(sw_context);
	if (unlikely(ret != 0))
		goto out_err_nores;

	ret = ttm_eu_reserve_buffers(&ticket, &sw_context->validate_nodes, true);
	if (unlikely(ret != 0))
		goto out_err;

	ret = vmw_validate_buffers(dev_priv, sw_context);
	if (unlikely(ret != 0))
		goto out_err;

	ret = vmw_resources_validate(sw_context);
	if (unlikely(ret != 0))
		goto out_err;

	if (throttle_us) {
		ret = vmw_wait_lag(dev_priv, &dev_priv->fifo.marker_queue,
				   throttle_us);

		if (unlikely(ret != 0))
			goto out_err;
	}

	ret = mutex_lock_interruptible(&dev_priv->binding_mutex);
	if (unlikely(ret != 0)) {
		ret = -ERESTARTSYS;
		goto out_err;
	}

	if (dev_priv->has_mob) {
		ret = vmw_rebind_contexts(sw_context);
		if (unlikely(ret != 0))
			goto out_unlock_binding;
	}

	cmd = vmw_fifo_reserve(dev_priv, command_size);
	if (unlikely(cmd == NULL)) {
		DRM_ERROR("Failed reserving fifo space for commands.\n");
		ret = -ENOMEM;
		goto out_unlock_binding;
	}

	vmw_apply_relocations(sw_context);
	memcpy(cmd, kernel_commands, command_size);

	vmw_resource_relocations_apply(cmd, &sw_context->res_relocations);
	vmw_resource_relocations_free(&sw_context->res_relocations);

	vmw_fifo_commit(dev_priv, command_size);

	vmw_query_bo_switch_commit(dev_priv, sw_context);
	ret = vmw_execbuf_fence_commands(file_priv, dev_priv,
					 &fence,
					 (user_fence_rep) ? &handle : NULL);
	/*
	 * This error is harmless, because if fence submission fails,
	 * vmw_fifo_send_fence will sync. The error will be propagated to
	 * user-space in @fence_rep
	 */

	if (ret != 0)
		DRM_ERROR("Fence submission error. Syncing.\n");

	vmw_resource_list_unreserve(&sw_context->resource_list, false);
	mutex_unlock(&dev_priv->binding_mutex);

	ttm_eu_fence_buffer_objects(&ticket, &sw_context->validate_nodes,
				    (void *) fence);

	if (unlikely(dev_priv->pinned_bo != NULL &&
		     !dev_priv->query_cid_valid))
		__vmw_execbuf_release_pinned_bo(dev_priv, fence);

	vmw_clear_validations(sw_context);
	vmw_execbuf_copy_fence_user(dev_priv, vmw_fpriv(file_priv), ret,
				    user_fence_rep, fence, handle);

	/* Don't unreference when handing fence out */
	if (unlikely(out_fence != NULL)) {
		*out_fence = fence;
		fence = NULL;
	} else if (likely(fence != NULL)) {
		vmw_fence_obj_unreference(&fence);
	}

	list_splice_init(&sw_context->resource_list, &resource_list);
	vmw_cmdbuf_res_commit(&sw_context->staged_cmd_res);
	mutex_unlock(&dev_priv->cmdbuf_mutex);

	/*
	 * Unreference resources outside of the cmdbuf_mutex to
	 * avoid deadlocks in resource destruction paths.
	 */
	vmw_resource_list_unreference(&resource_list);

	return 0;

out_unlock_binding:
	mutex_unlock(&dev_priv->binding_mutex);
out_err:
	ttm_eu_backoff_reservation(&ticket, &sw_context->validate_nodes);
out_err_nores:
	vmw_resource_list_unreserve(&sw_context->resource_list, true);
	vmw_resource_relocations_free(&sw_context->res_relocations);
	vmw_free_relocations(sw_context);
	vmw_clear_validations(sw_context);
	if (unlikely(dev_priv->pinned_bo != NULL &&
		     !dev_priv->query_cid_valid))
		__vmw_execbuf_release_pinned_bo(dev_priv, NULL);
out_unlock:
	list_splice_init(&sw_context->resource_list, &resource_list);
	error_resource = sw_context->error_resource;
	sw_context->error_resource = NULL;
	vmw_cmdbuf_res_revert(&sw_context->staged_cmd_res);
	mutex_unlock(&dev_priv->cmdbuf_mutex);

	/*
	 * Unreference resources outside of the cmdbuf_mutex to
	 * avoid deadlocks in resource destruction paths.
	 */
	vmw_resource_list_unreference(&resource_list);
	if (unlikely(error_resource != NULL))
		vmw_resource_unreference(&error_resource);

	return ret;
}

/**
 * vmw_execbuf_unpin_panic - Idle the fifo and unpin the query buffer.
 *
 * @dev_priv: The device private structure.
 *
 * This function is called to idle the fifo and unpin the query buffer
 * if the normal way to do this hits an error, which should typically be
 * extremely rare.
 */
static void vmw_execbuf_unpin_panic(struct vmw_private *dev_priv)
{
	DRM_ERROR("Can't unpin query buffer. Trying to recover.\n");

	(void) vmw_fallback_wait(dev_priv, false, true, 0, false, 10*HZ);
	vmw_bo_pin(dev_priv->pinned_bo, false);
	vmw_bo_pin(dev_priv->dummy_query_bo, false);
	dev_priv->dummy_query_bo_pinned = false;
}


/**
 * __vmw_execbuf_release_pinned_bo - Flush queries and unpin the pinned
 * query bo.
 *
 * @dev_priv: The device private structure.
 * @fence: If non-NULL should point to a struct vmw_fence_obj issued
 * _after_ a query barrier that flushes all queries touching the current
 * buffer pointed to by @dev_priv->pinned_bo
 *
 * This function should be used to unpin the pinned query bo, or
 * as a query barrier when we need to make sure that all queries have
 * finished before the next fifo command. (For example on hardware
 * context destructions where the hardware may otherwise leak unfinished
 * queries).
 *
 * This function does not return any failure codes, but make attempts
 * to do safe unpinning in case of errors.
 *
 * The function will synchronize on the previous query barrier, and will
 * thus not finish until that barrier has executed.
 *
 * the @dev_priv->cmdbuf_mutex needs to be held by the current thread
 * before calling this function.
 */
void __vmw_execbuf_release_pinned_bo(struct vmw_private *dev_priv,
				     struct vmw_fence_obj *fence)
{
	int ret = 0;
	struct list_head validate_list;
	struct ttm_validate_buffer pinned_val, query_val;
	struct vmw_fence_obj *lfence = NULL;
	struct ww_acquire_ctx ticket;

	if (dev_priv->pinned_bo == NULL)
		goto out_unlock;

	INIT_LIST_HEAD(&validate_list);

	pinned_val.bo = ttm_bo_reference(dev_priv->pinned_bo);
	pinned_val.shared = false;
	list_add_tail(&pinned_val.head, &validate_list);

	query_val.bo = ttm_bo_reference(dev_priv->dummy_query_bo);
	query_val.shared = false;
	list_add_tail(&query_val.head, &validate_list);

	ret = ttm_eu_reserve_buffers(&ticket, &validate_list, false);
	if (unlikely(ret != 0)) {
		vmw_execbuf_unpin_panic(dev_priv);
		goto out_no_reserve;
	}

	if (dev_priv->query_cid_valid) {
		BUG_ON(fence != NULL);
		ret = vmw_fifo_emit_dummy_query(dev_priv, dev_priv->query_cid);
		if (unlikely(ret != 0)) {
			vmw_execbuf_unpin_panic(dev_priv);
			goto out_no_emit;
		}
		dev_priv->query_cid_valid = false;
	}

	vmw_bo_pin(dev_priv->pinned_bo, false);
	vmw_bo_pin(dev_priv->dummy_query_bo, false);
	dev_priv->dummy_query_bo_pinned = false;

	if (fence == NULL) {
		(void) vmw_execbuf_fence_commands(NULL, dev_priv, &lfence,
						  NULL);
		fence = lfence;
	}
	ttm_eu_fence_buffer_objects(&ticket, &validate_list, (void *) fence);
	if (lfence != NULL)
		vmw_fence_obj_unreference(&lfence);

	ttm_bo_unref(&query_val.bo);
	ttm_bo_unref(&pinned_val.bo);
	ttm_bo_unref(&dev_priv->pinned_bo);

out_unlock:
	return;

out_no_emit:
	ttm_eu_backoff_reservation(&ticket, &validate_list);
out_no_reserve:
	ttm_bo_unref(&query_val.bo);
	ttm_bo_unref(&pinned_val.bo);
	ttm_bo_unref(&dev_priv->pinned_bo);
}

/**
 * vmw_execbuf_release_pinned_bo - Flush queries and unpin the pinned
 * query bo.
 *
 * @dev_priv: The device private structure.
 *
 * This function should be used to unpin the pinned query bo, or
 * as a query barrier when we need to make sure that all queries have
 * finished before the next fifo command. (For example on hardware
 * context destructions where the hardware may otherwise leak unfinished
 * queries).
 *
 * This function does not return any failure codes, but make attempts
 * to do safe unpinning in case of errors.
 *
 * The function will synchronize on the previous query barrier, and will
 * thus not finish until that barrier has executed.
 */
void vmw_execbuf_release_pinned_bo(struct vmw_private *dev_priv)
{
	mutex_lock(&dev_priv->cmdbuf_mutex);
	if (dev_priv->query_cid_valid)
		__vmw_execbuf_release_pinned_bo(dev_priv, NULL);
	mutex_unlock(&dev_priv->cmdbuf_mutex);
}


int vmw_execbuf_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file_priv)
{
	struct vmw_private *dev_priv = vmw_priv(dev);
	struct drm_vmw_execbuf_arg *arg = (struct drm_vmw_execbuf_arg *)data;
	int ret;

	/*
	 * This will allow us to extend the ioctl argument while
	 * maintaining backwards compatibility:
	 * We take different code paths depending on the value of
	 * arg->version.
	 */

	if (unlikely(arg->version != DRM_VMW_EXECBUF_VERSION)) {
		DRM_ERROR("Incorrect execbuf version.\n");
		DRM_ERROR("You're running outdated experimental "
			  "vmwgfx user-space drivers.");
		return -EINVAL;
	}

	ret = ttm_read_lock(&dev_priv->reservation_sem, true);
	if (unlikely(ret != 0))
		return ret;

	ret = vmw_execbuf_process(file_priv, dev_priv,
				  (void __user *)(unsigned long)arg->commands,
				  NULL, arg->command_size, arg->throttle_us,
				  (void __user *)(unsigned long)arg->fence_rep,
				  NULL);

	if (unlikely(ret != 0))
		goto out_unlock;

	vmw_kms_cursor_post_execbuf(dev_priv);

out_unlock:
	ttm_read_unlock(&dev_priv->reservation_sem);
	return ret;
}
