// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright 2015 VMware, Inc., Palo Alto, CA., USA
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
 * This file implements the vmwgfx context binding manager,
 * The sole reason for having to use this code is that vmware guest
 * backed contexts can be swapped out to their backing mobs by the device
 * at any time, also swapped in at any time. At swapin time, the device
 * validates the context bindings to make sure they point to valid resources.
 * It's this outside-of-drawcall validation (that can happen at any time),
 * that makes this code necessary.
 *
 * We therefore need to kill any context bindings pointing to a resource
 * when the resource is swapped out. Furthermore, if the vmwgfx driver has
 * swapped out the context we can't swap it in again to kill bindings because
 * of backing mob reservation lockdep violations, so as part of
 * context swapout, also kill all bindings of a context, so that they are
 * already killed if a resource to which a binding points
 * needs to be swapped out.
 *
 * Note that a resource can be pointed to by bindings from multiple contexts,
 * Therefore we can't easily protect this data by a per context mutex
 * (unless we use deadlock-safe WW mutexes). So we use a global binding_mutex
 * to protect all binding manager data.
 *
 * Finally, any association between a context and a global resource
 * (surface, shader or even DX query) is conceptually a context binding that
 * needs to be tracked by this code.
 */

#include "vmwgfx_drv.h"
#include "vmwgfx_binding.h"
#include "device_include/svga3d_reg.h"

#define VMW_BINDING_RT_BIT     0
#define VMW_BINDING_PS_BIT     1
#define VMW_BINDING_SO_T_BIT   2
#define VMW_BINDING_VB_BIT     3
#define VMW_BINDING_UAV_BIT    4
#define VMW_BINDING_CS_UAV_BIT 5
#define VMW_BINDING_NUM_BITS   6

#define VMW_BINDING_PS_SR_BIT  0

/**
 * struct vmw_ctx_binding_state - per context binding state
 *
 * @dev_priv: Pointer to device private structure.
 * @list: linked list of individual active bindings.
 * @render_targets: Render target bindings.
 * @texture_units: Texture units bindings.
 * @ds_view: Depth-stencil view binding.
 * @so_targets: StreamOutput target bindings.
 * @vertex_buffers: Vertex buffer bindings.
 * @index_buffer: Index buffer binding.
 * @per_shader: Per shader-type bindings.
 * @ua_views: UAV bindings.
 * @so_state: StreamOutput bindings.
 * @dirty: Bitmap tracking per binding-type changes that have not yet
 * been emitted to the device.
 * @dirty_vb: Bitmap tracking individual vertex buffer binding changes that
 * have not yet been emitted to the device.
 * @bind_cmd_buffer: Scratch space used to construct binding commands.
 * @bind_cmd_count: Number of binding command data entries in @bind_cmd_buffer
 * @bind_first_slot: Used together with @bind_cmd_buffer to indicate the
 * device binding slot of the first command data entry in @bind_cmd_buffer.
 *
 * Note that this structure also provides storage space for the individual
 * struct vmw_ctx_binding objects, so that no dynamic allocation is needed
 * for individual bindings.
 *
 */
struct vmw_ctx_binding_state {
	struct vmw_private *dev_priv;
	struct list_head list;
	struct vmw_ctx_bindinfo_view render_targets[SVGA3D_RT_MAX];
	struct vmw_ctx_bindinfo_tex texture_units[SVGA3D_NUM_TEXTURE_UNITS];
	struct vmw_ctx_bindinfo_view ds_view;
	struct vmw_ctx_bindinfo_so_target so_targets[SVGA3D_DX_MAX_SOTARGETS];
	struct vmw_ctx_bindinfo_vb vertex_buffers[SVGA3D_DX_MAX_VERTEXBUFFERS];
	struct vmw_ctx_bindinfo_ib index_buffer;
	struct vmw_dx_shader_bindings per_shader[SVGA3D_NUM_SHADERTYPE];
	struct vmw_ctx_bindinfo_uav ua_views[VMW_MAX_UAV_BIND_TYPE];
	struct vmw_ctx_bindinfo_so so_state;

	unsigned long dirty;
	DECLARE_BITMAP(dirty_vb, SVGA3D_DX_MAX_VERTEXBUFFERS);

	u32 bind_cmd_buffer[VMW_MAX_VIEW_BINDINGS];
	u32 bind_cmd_count;
	u32 bind_first_slot;
};

static int vmw_binding_scrub_shader(struct vmw_ctx_bindinfo *bi, bool rebind);
static int vmw_binding_scrub_render_target(struct vmw_ctx_bindinfo *bi,
					   bool rebind);
static int vmw_binding_scrub_texture(struct vmw_ctx_bindinfo *bi, bool rebind);
static int vmw_binding_scrub_cb(struct vmw_ctx_bindinfo *bi, bool rebind);
static int vmw_binding_scrub_dx_rt(struct vmw_ctx_bindinfo *bi, bool rebind);
static int vmw_binding_scrub_sr(struct vmw_ctx_bindinfo *bi, bool rebind);
static int vmw_binding_scrub_so_target(struct vmw_ctx_bindinfo *bi, bool rebind);
static int vmw_binding_emit_dirty(struct vmw_ctx_binding_state *cbs);
static int vmw_binding_scrub_dx_shader(struct vmw_ctx_bindinfo *bi,
				       bool rebind);
static int vmw_binding_scrub_ib(struct vmw_ctx_bindinfo *bi, bool rebind);
static int vmw_binding_scrub_vb(struct vmw_ctx_bindinfo *bi, bool rebind);
static int vmw_binding_scrub_uav(struct vmw_ctx_bindinfo *bi, bool rebind);
static int vmw_binding_scrub_cs_uav(struct vmw_ctx_bindinfo *bi, bool rebind);
static int vmw_binding_scrub_so(struct vmw_ctx_bindinfo *bi, bool rebind);

static void vmw_binding_build_asserts(void) __attribute__ ((unused));

typedef int (*vmw_scrub_func)(struct vmw_ctx_bindinfo *, bool);

/**
 * struct vmw_binding_info - Per binding type information for the binding
 * manager
 *
 * @size: The size of the struct binding derived from a struct vmw_ctx_bindinfo.
 * @offsets: array[shader_slot] of offsets to the array[slot]
 * of struct bindings for the binding type.
 * @scrub_func: Pointer to the scrub function for this binding type.
 *
 * Holds static information to help optimize the binding manager and avoid
 * an excessive amount of switch statements.
 */
struct vmw_binding_info {
	size_t size;
	const size_t *offsets;
	vmw_scrub_func scrub_func;
};

/*
 * A number of static variables that help determine the scrub func and the
 * location of the struct vmw_ctx_bindinfo slots for each binding type.
 */
static const size_t vmw_binding_shader_offsets[] = {
	offsetof(struct vmw_ctx_binding_state, per_shader[0].shader),
	offsetof(struct vmw_ctx_binding_state, per_shader[1].shader),
	offsetof(struct vmw_ctx_binding_state, per_shader[2].shader),
	offsetof(struct vmw_ctx_binding_state, per_shader[3].shader),
	offsetof(struct vmw_ctx_binding_state, per_shader[4].shader),
	offsetof(struct vmw_ctx_binding_state, per_shader[5].shader),
};
static const size_t vmw_binding_rt_offsets[] = {
	offsetof(struct vmw_ctx_binding_state, render_targets),
};
static const size_t vmw_binding_tex_offsets[] = {
	offsetof(struct vmw_ctx_binding_state, texture_units),
};
static const size_t vmw_binding_cb_offsets[] = {
	offsetof(struct vmw_ctx_binding_state, per_shader[0].const_buffers),
	offsetof(struct vmw_ctx_binding_state, per_shader[1].const_buffers),
	offsetof(struct vmw_ctx_binding_state, per_shader[2].const_buffers),
	offsetof(struct vmw_ctx_binding_state, per_shader[3].const_buffers),
	offsetof(struct vmw_ctx_binding_state, per_shader[4].const_buffers),
	offsetof(struct vmw_ctx_binding_state, per_shader[5].const_buffers),
};
static const size_t vmw_binding_dx_ds_offsets[] = {
	offsetof(struct vmw_ctx_binding_state, ds_view),
};
static const size_t vmw_binding_sr_offsets[] = {
	offsetof(struct vmw_ctx_binding_state, per_shader[0].shader_res),
	offsetof(struct vmw_ctx_binding_state, per_shader[1].shader_res),
	offsetof(struct vmw_ctx_binding_state, per_shader[2].shader_res),
	offsetof(struct vmw_ctx_binding_state, per_shader[3].shader_res),
	offsetof(struct vmw_ctx_binding_state, per_shader[4].shader_res),
	offsetof(struct vmw_ctx_binding_state, per_shader[5].shader_res),
};
static const size_t vmw_binding_so_target_offsets[] = {
	offsetof(struct vmw_ctx_binding_state, so_targets),
};
static const size_t vmw_binding_vb_offsets[] = {
	offsetof(struct vmw_ctx_binding_state, vertex_buffers),
};
static const size_t vmw_binding_ib_offsets[] = {
	offsetof(struct vmw_ctx_binding_state, index_buffer),
};
static const size_t vmw_binding_uav_offsets[] = {
	offsetof(struct vmw_ctx_binding_state, ua_views[0].views),
};
static const size_t vmw_binding_cs_uav_offsets[] = {
	offsetof(struct vmw_ctx_binding_state, ua_views[1].views),
};
static const size_t vmw_binding_so_offsets[] = {
	offsetof(struct vmw_ctx_binding_state, so_state),
};

static const struct vmw_binding_info vmw_binding_infos[] = {
	[vmw_ctx_binding_shader] = {
		.size = sizeof(struct vmw_ctx_bindinfo_shader),
		.offsets = vmw_binding_shader_offsets,
		.scrub_func = vmw_binding_scrub_shader},
	[vmw_ctx_binding_rt] = {
		.size = sizeof(struct vmw_ctx_bindinfo_view),
		.offsets = vmw_binding_rt_offsets,
		.scrub_func = vmw_binding_scrub_render_target},
	[vmw_ctx_binding_tex] = {
		.size = sizeof(struct vmw_ctx_bindinfo_tex),
		.offsets = vmw_binding_tex_offsets,
		.scrub_func = vmw_binding_scrub_texture},
	[vmw_ctx_binding_cb] = {
		.size = sizeof(struct vmw_ctx_bindinfo_cb),
		.offsets = vmw_binding_cb_offsets,
		.scrub_func = vmw_binding_scrub_cb},
	[vmw_ctx_binding_dx_shader] = {
		.size = sizeof(struct vmw_ctx_bindinfo_shader),
		.offsets = vmw_binding_shader_offsets,
		.scrub_func = vmw_binding_scrub_dx_shader},
	[vmw_ctx_binding_dx_rt] = {
		.size = sizeof(struct vmw_ctx_bindinfo_view),
		.offsets = vmw_binding_rt_offsets,
		.scrub_func = vmw_binding_scrub_dx_rt},
	[vmw_ctx_binding_sr] = {
		.size = sizeof(struct vmw_ctx_bindinfo_view),
		.offsets = vmw_binding_sr_offsets,
		.scrub_func = vmw_binding_scrub_sr},
	[vmw_ctx_binding_ds] = {
		.size = sizeof(struct vmw_ctx_bindinfo_view),
		.offsets = vmw_binding_dx_ds_offsets,
		.scrub_func = vmw_binding_scrub_dx_rt},
	[vmw_ctx_binding_so_target] = {
		.size = sizeof(struct vmw_ctx_bindinfo_so_target),
		.offsets = vmw_binding_so_target_offsets,
		.scrub_func = vmw_binding_scrub_so_target},
	[vmw_ctx_binding_vb] = {
		.size = sizeof(struct vmw_ctx_bindinfo_vb),
		.offsets = vmw_binding_vb_offsets,
		.scrub_func = vmw_binding_scrub_vb},
	[vmw_ctx_binding_ib] = {
		.size = sizeof(struct vmw_ctx_bindinfo_ib),
		.offsets = vmw_binding_ib_offsets,
		.scrub_func = vmw_binding_scrub_ib},
	[vmw_ctx_binding_uav] = {
		.size = sizeof(struct vmw_ctx_bindinfo_view),
		.offsets = vmw_binding_uav_offsets,
		.scrub_func = vmw_binding_scrub_uav},
	[vmw_ctx_binding_cs_uav] = {
		.size = sizeof(struct vmw_ctx_bindinfo_view),
		.offsets = vmw_binding_cs_uav_offsets,
		.scrub_func = vmw_binding_scrub_cs_uav},
	[vmw_ctx_binding_so] = {
		.size = sizeof(struct vmw_ctx_bindinfo_so),
		.offsets = vmw_binding_so_offsets,
		.scrub_func = vmw_binding_scrub_so},
};

/**
 * vmw_cbs_context - Return a pointer to the context resource of a
 * context binding state tracker.
 *
 * @cbs: The context binding state tracker.
 *
 * Provided there are any active bindings, this function will return an
 * unreferenced pointer to the context resource that owns the context
 * binding state tracker. If there are no active bindings, this function
 * will return NULL. Note that the caller must somehow ensure that a reference
 * is held on the context resource prior to calling this function.
 */
static const struct vmw_resource *
vmw_cbs_context(const struct vmw_ctx_binding_state *cbs)
{
	if (list_empty(&cbs->list))
		return NULL;

	return list_first_entry(&cbs->list, struct vmw_ctx_bindinfo,
				ctx_list)->ctx;
}

/**
 * vmw_binding_loc - determine the struct vmw_ctx_bindinfo slot location.
 *
 * @cbs: Pointer to a struct vmw_ctx_binding state which holds the slot.
 * @bt: The binding type.
 * @shader_slot: The shader slot of the binding. If none, then set to 0.
 * @slot: The slot of the binding.
 */
static struct vmw_ctx_bindinfo *
vmw_binding_loc(struct vmw_ctx_binding_state *cbs,
		enum vmw_ctx_binding_type bt, u32 shader_slot, u32 slot)
{
	const struct vmw_binding_info *b = &vmw_binding_infos[bt];
	size_t offset = b->offsets[shader_slot] + b->size*slot;

	return (struct vmw_ctx_bindinfo *)((u8 *) cbs + offset);
}

/**
 * vmw_binding_drop: Stop tracking a context binding
 *
 * @bi: Pointer to binding tracker storage.
 *
 * Stops tracking a context binding, and re-initializes its storage.
 * Typically used when the context binding is replaced with a binding to
 * another (or the same, for that matter) resource.
 */
static void vmw_binding_drop(struct vmw_ctx_bindinfo *bi)
{
	list_del(&bi->ctx_list);
	if (!list_empty(&bi->res_list))
		list_del(&bi->res_list);
	bi->ctx = NULL;
}

/**
 * vmw_binding_add: Start tracking a context binding
 *
 * @cbs: Pointer to the context binding state tracker.
 * @bi: Information about the binding to track.
 *
 * Starts tracking the binding in the context binding
 * state structure @cbs.
 */
void vmw_binding_add(struct vmw_ctx_binding_state *cbs,
		    const struct vmw_ctx_bindinfo *bi,
		    u32 shader_slot, u32 slot)
{
	struct vmw_ctx_bindinfo *loc =
		vmw_binding_loc(cbs, bi->bt, shader_slot, slot);
	const struct vmw_binding_info *b = &vmw_binding_infos[bi->bt];

	if (loc->ctx != NULL)
		vmw_binding_drop(loc);

	memcpy(loc, bi, b->size);
	loc->scrubbed = false;
	list_add(&loc->ctx_list, &cbs->list);
	INIT_LIST_HEAD(&loc->res_list);
}

/**
 * vmw_binding_add_uav_index - Add UAV index for tracking.
 * @cbs: Pointer to the context binding state tracker.
 * @slot: UAV type to which bind this index.
 * @index: The splice index to track.
 */
void vmw_binding_add_uav_index(struct vmw_ctx_binding_state *cbs, uint32 slot,
			       uint32 index)
{
	cbs->ua_views[slot].index = index;
}

/**
 * vmw_binding_transfer: Transfer a context binding tracking entry.
 *
 * @cbs: Pointer to the persistent context binding state tracker.
 * @bi: Information about the binding to track.
 *
 */
static void vmw_binding_transfer(struct vmw_ctx_binding_state *cbs,
				 const struct vmw_ctx_binding_state *from,
				 const struct vmw_ctx_bindinfo *bi)
{
	size_t offset = (unsigned long)bi - (unsigned long)from;
	struct vmw_ctx_bindinfo *loc = (struct vmw_ctx_bindinfo *)
		((unsigned long) cbs + offset);

	if (loc->ctx != NULL) {
		WARN_ON(bi->scrubbed);

		vmw_binding_drop(loc);
	}

	if (bi->res != NULL) {
		memcpy(loc, bi, vmw_binding_infos[bi->bt].size);
		list_add_tail(&loc->ctx_list, &cbs->list);
		list_add_tail(&loc->res_list, &loc->res->binding_head);
	}
}

/**
 * vmw_binding_state_kill - Kill all bindings associated with a
 * struct vmw_ctx_binding state structure, and re-initialize the structure.
 *
 * @cbs: Pointer to the context binding state tracker.
 *
 * Emits commands to scrub all bindings associated with the
 * context binding state tracker. Then re-initializes the whole structure.
 */
void vmw_binding_state_kill(struct vmw_ctx_binding_state *cbs)
{
	struct vmw_ctx_bindinfo *entry, *next;

	vmw_binding_state_scrub(cbs);
	list_for_each_entry_safe(entry, next, &cbs->list, ctx_list)
		vmw_binding_drop(entry);
}

/**
 * vmw_binding_state_scrub - Scrub all bindings associated with a
 * struct vmw_ctx_binding state structure.
 *
 * @cbs: Pointer to the context binding state tracker.
 *
 * Emits commands to scrub all bindings associated with the
 * context binding state tracker.
 */
void vmw_binding_state_scrub(struct vmw_ctx_binding_state *cbs)
{
	struct vmw_ctx_bindinfo *entry;

	list_for_each_entry(entry, &cbs->list, ctx_list) {
		if (!entry->scrubbed) {
			(void) vmw_binding_infos[entry->bt].scrub_func
				(entry, false);
			entry->scrubbed = true;
		}
	}

	(void) vmw_binding_emit_dirty(cbs);
}

/**
 * vmw_binding_res_list_kill - Kill all bindings on a
 * resource binding list
 *
 * @head: list head of resource binding list
 *
 * Kills all bindings associated with a specific resource. Typically
 * called before the resource is destroyed.
 */
void vmw_binding_res_list_kill(struct list_head *head)
{
	struct vmw_ctx_bindinfo *entry, *next;

	vmw_binding_res_list_scrub(head);
	list_for_each_entry_safe(entry, next, head, res_list)
		vmw_binding_drop(entry);
}

/**
 * vmw_binding_res_list_scrub - Scrub all bindings on a
 * resource binding list
 *
 * @head: list head of resource binding list
 *
 * Scrub all bindings associated with a specific resource. Typically
 * called before the resource is evicted.
 */
void vmw_binding_res_list_scrub(struct list_head *head)
{
	struct vmw_ctx_bindinfo *entry;

	list_for_each_entry(entry, head, res_list) {
		if (!entry->scrubbed) {
			(void) vmw_binding_infos[entry->bt].scrub_func
				(entry, false);
			entry->scrubbed = true;
		}
	}

	list_for_each_entry(entry, head, res_list) {
		struct vmw_ctx_binding_state *cbs =
			vmw_context_binding_state(entry->ctx);

		(void) vmw_binding_emit_dirty(cbs);
	}
}


/**
 * vmw_binding_state_commit - Commit staged binding info
 *
 * @ctx: Pointer to context to commit the staged binding info to.
 * @from: Staged binding info built during execbuf.
 * @scrubbed: Transfer only scrubbed bindings.
 *
 * Transfers binding info from a temporary structure
 * (typically used by execbuf) to the persistent
 * structure in the context. This can be done once commands have been
 * submitted to hardware
 */
void vmw_binding_state_commit(struct vmw_ctx_binding_state *to,
			      struct vmw_ctx_binding_state *from)
{
	struct vmw_ctx_bindinfo *entry, *next;

	list_for_each_entry_safe(entry, next, &from->list, ctx_list) {
		vmw_binding_transfer(to, from, entry);
		vmw_binding_drop(entry);
	}

	/* Also transfer uav splice indices */
	to->ua_views[0].index = from->ua_views[0].index;
	to->ua_views[1].index = from->ua_views[1].index;
}

/**
 * vmw_binding_rebind_all - Rebind all scrubbed bindings of a context
 *
 * @ctx: The context resource
 *
 * Walks through the context binding list and rebinds all scrubbed
 * resources.
 */
int vmw_binding_rebind_all(struct vmw_ctx_binding_state *cbs)
{
	struct vmw_ctx_bindinfo *entry;
	int ret;

	list_for_each_entry(entry, &cbs->list, ctx_list) {
		if (likely(!entry->scrubbed))
			continue;

		if ((entry->res == NULL || entry->res->id ==
			    SVGA3D_INVALID_ID))
			continue;

		ret = vmw_binding_infos[entry->bt].scrub_func(entry, true);
		if (unlikely(ret != 0))
			return ret;

		entry->scrubbed = false;
	}

	return vmw_binding_emit_dirty(cbs);
}

/**
 * vmw_binding_scrub_shader - scrub a shader binding from a context.
 *
 * @bi: single binding information.
 * @rebind: Whether to issue a bind instead of scrub command.
 */
static int vmw_binding_scrub_shader(struct vmw_ctx_bindinfo *bi, bool rebind)
{
	struct vmw_ctx_bindinfo_shader *binding =
		container_of(bi, typeof(*binding), bi);
	struct vmw_private *dev_priv = bi->ctx->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdSetShader body;
	} *cmd;

	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_SET_SHADER;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.cid = bi->ctx->id;
	cmd->body.type = binding->shader_slot + SVGA3D_SHADERTYPE_MIN;
	cmd->body.shid = ((rebind) ? bi->res->id : SVGA3D_INVALID_ID);
	vmw_cmd_commit(dev_priv, sizeof(*cmd));

	return 0;
}

/**
 * vmw_binding_scrub_render_target - scrub a render target binding
 * from a context.
 *
 * @bi: single binding information.
 * @rebind: Whether to issue a bind instead of scrub command.
 */
static int vmw_binding_scrub_render_target(struct vmw_ctx_bindinfo *bi,
					   bool rebind)
{
	struct vmw_ctx_bindinfo_view *binding =
		container_of(bi, typeof(*binding), bi);
	struct vmw_private *dev_priv = bi->ctx->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdSetRenderTarget body;
	} *cmd;

	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_SETRENDERTARGET;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.cid = bi->ctx->id;
	cmd->body.type = binding->slot;
	cmd->body.target.sid = ((rebind) ? bi->res->id : SVGA3D_INVALID_ID);
	cmd->body.target.face = 0;
	cmd->body.target.mipmap = 0;
	vmw_cmd_commit(dev_priv, sizeof(*cmd));

	return 0;
}

/**
 * vmw_binding_scrub_texture - scrub a texture binding from a context.
 *
 * @bi: single binding information.
 * @rebind: Whether to issue a bind instead of scrub command.
 *
 * TODO: Possibly complement this function with a function that takes
 * a list of texture bindings and combines them to a single command.
 */
static int vmw_binding_scrub_texture(struct vmw_ctx_bindinfo *bi,
				     bool rebind)
{
	struct vmw_ctx_bindinfo_tex *binding =
		container_of(bi, typeof(*binding), bi);
	struct vmw_private *dev_priv = bi->ctx->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		struct {
			SVGA3dCmdSetTextureState c;
			SVGA3dTextureState s1;
		} body;
	} *cmd;

	cmd = VMW_CMD_RESERVE(dev_priv, sizeof(*cmd));
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_SETTEXTURESTATE;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.c.cid = bi->ctx->id;
	cmd->body.s1.stage = binding->texture_stage;
	cmd->body.s1.name = SVGA3D_TS_BIND_TEXTURE;
	cmd->body.s1.value = ((rebind) ? bi->res->id : SVGA3D_INVALID_ID);
	vmw_cmd_commit(dev_priv, sizeof(*cmd));

	return 0;
}

/**
 * vmw_binding_scrub_dx_shader - scrub a dx shader binding from a context.
 *
 * @bi: single binding information.
 * @rebind: Whether to issue a bind instead of scrub command.
 */
static int vmw_binding_scrub_dx_shader(struct vmw_ctx_bindinfo *bi, bool rebind)
{
	struct vmw_ctx_bindinfo_shader *binding =
		container_of(bi, typeof(*binding), bi);
	struct vmw_private *dev_priv = bi->ctx->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXSetShader body;
	} *cmd;

	cmd = VMW_CMD_CTX_RESERVE(dev_priv, sizeof(*cmd), bi->ctx->id);
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_DX_SET_SHADER;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.type = binding->shader_slot + SVGA3D_SHADERTYPE_MIN;
	cmd->body.shaderId = ((rebind) ? bi->res->id : SVGA3D_INVALID_ID);
	vmw_cmd_commit(dev_priv, sizeof(*cmd));

	return 0;
}

/**
 * vmw_binding_scrub_cb - scrub a constant buffer binding from a context.
 *
 * @bi: single binding information.
 * @rebind: Whether to issue a bind instead of scrub command.
 */
static int vmw_binding_scrub_cb(struct vmw_ctx_bindinfo *bi, bool rebind)
{
	struct vmw_ctx_bindinfo_cb *binding =
		container_of(bi, typeof(*binding), bi);
	struct vmw_private *dev_priv = bi->ctx->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXSetSingleConstantBuffer body;
	} *cmd;

	cmd = VMW_CMD_CTX_RESERVE(dev_priv, sizeof(*cmd), bi->ctx->id);
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_DX_SET_SINGLE_CONSTANT_BUFFER;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.slot = binding->slot;
	cmd->body.type = binding->shader_slot + SVGA3D_SHADERTYPE_MIN;
	if (rebind) {
		cmd->body.offsetInBytes = binding->offset;
		cmd->body.sizeInBytes = binding->size;
		cmd->body.sid = bi->res->id;
	} else {
		cmd->body.offsetInBytes = 0;
		cmd->body.sizeInBytes = 0;
		cmd->body.sid = SVGA3D_INVALID_ID;
	}
	vmw_cmd_commit(dev_priv, sizeof(*cmd));

	return 0;
}

/**
 * vmw_collect_view_ids - Build view id data for a view binding command
 * without checking which bindings actually need to be emitted
 *
 * @cbs: Pointer to the context's struct vmw_ctx_binding_state
 * @bi: Pointer to where the binding info array is stored in @cbs
 * @max_num: Maximum number of entries in the @bi array.
 *
 * Scans the @bi array for bindings and builds a buffer of view id data.
 * Stops at the first non-existing binding in the @bi array.
 * On output, @cbs->bind_cmd_count contains the number of bindings to be
 * emitted, @cbs->bind_first_slot is set to zero, and @cbs->bind_cmd_buffer
 * contains the command data.
 */
static void vmw_collect_view_ids(struct vmw_ctx_binding_state *cbs,
				 const struct vmw_ctx_bindinfo *bi,
				 u32 max_num)
{
	const struct vmw_ctx_bindinfo_view *biv =
		container_of(bi, struct vmw_ctx_bindinfo_view, bi);
	unsigned long i;

	cbs->bind_cmd_count = 0;
	cbs->bind_first_slot = 0;

	for (i = 0; i < max_num; ++i, ++biv) {
		if (!biv->bi.ctx)
			break;

		cbs->bind_cmd_buffer[cbs->bind_cmd_count++] =
			((biv->bi.scrubbed) ?
			 SVGA3D_INVALID_ID : biv->bi.res->id);
	}
}

/**
 * vmw_collect_dirty_view_ids - Build view id data for a view binding command
 *
 * @cbs: Pointer to the context's struct vmw_ctx_binding_state
 * @bi: Pointer to where the binding info array is stored in @cbs
 * @dirty: Bitmap indicating which bindings need to be emitted.
 * @max_num: Maximum number of entries in the @bi array.
 *
 * Scans the @bi array for bindings that need to be emitted and
 * builds a buffer of view id data.
 * On output, @cbs->bind_cmd_count contains the number of bindings to be
 * emitted, @cbs->bind_first_slot indicates the index of the first emitted
 * binding, and @cbs->bind_cmd_buffer contains the command data.
 */
static void vmw_collect_dirty_view_ids(struct vmw_ctx_binding_state *cbs,
				       const struct vmw_ctx_bindinfo *bi,
				       unsigned long *dirty,
				       u32 max_num)
{
	const struct vmw_ctx_bindinfo_view *biv =
		container_of(bi, struct vmw_ctx_bindinfo_view, bi);
	unsigned long i, next_bit;

	cbs->bind_cmd_count = 0;
	i = find_first_bit(dirty, max_num);
	next_bit = i;
	cbs->bind_first_slot = i;

	biv += i;
	for (; i < max_num; ++i, ++biv) {
		cbs->bind_cmd_buffer[cbs->bind_cmd_count++] =
			((!biv->bi.ctx || biv->bi.scrubbed) ?
			 SVGA3D_INVALID_ID : biv->bi.res->id);

		if (next_bit == i) {
			next_bit = find_next_bit(dirty, max_num, i + 1);
			if (next_bit >= max_num)
				break;
		}
	}
}

/**
 * vmw_binding_emit_set_sr - Issue delayed DX shader resource binding commands
 *
 * @cbs: Pointer to the context's struct vmw_ctx_binding_state
 */
static int vmw_emit_set_sr(struct vmw_ctx_binding_state *cbs,
			   int shader_slot)
{
	const struct vmw_ctx_bindinfo *loc =
		&cbs->per_shader[shader_slot].shader_res[0].bi;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXSetShaderResources body;
	} *cmd;
	size_t cmd_size, view_id_size;
	const struct vmw_resource *ctx = vmw_cbs_context(cbs);

	vmw_collect_dirty_view_ids(cbs, loc,
				   cbs->per_shader[shader_slot].dirty_sr,
				   SVGA3D_DX_MAX_SRVIEWS);
	if (cbs->bind_cmd_count == 0)
		return 0;

	view_id_size = cbs->bind_cmd_count*sizeof(uint32);
	cmd_size = sizeof(*cmd) + view_id_size;
	cmd = VMW_CMD_CTX_RESERVE(ctx->dev_priv, cmd_size, ctx->id);
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_DX_SET_SHADER_RESOURCES;
	cmd->header.size = sizeof(cmd->body) + view_id_size;
	cmd->body.type = shader_slot + SVGA3D_SHADERTYPE_MIN;
	cmd->body.startView = cbs->bind_first_slot;

	memcpy(&cmd[1], cbs->bind_cmd_buffer, view_id_size);

	vmw_cmd_commit(ctx->dev_priv, cmd_size);
	bitmap_clear(cbs->per_shader[shader_slot].dirty_sr,
		     cbs->bind_first_slot, cbs->bind_cmd_count);

	return 0;
}

/**
 * vmw_binding_emit_set_rt - Issue delayed DX rendertarget binding commands
 *
 * @cbs: Pointer to the context's struct vmw_ctx_binding_state
 */
static int vmw_emit_set_rt(struct vmw_ctx_binding_state *cbs)
{
	const struct vmw_ctx_bindinfo *loc = &cbs->render_targets[0].bi;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXSetRenderTargets body;
	} *cmd;
	size_t cmd_size, view_id_size;
	const struct vmw_resource *ctx = vmw_cbs_context(cbs);

	vmw_collect_view_ids(cbs, loc, SVGA3D_MAX_SIMULTANEOUS_RENDER_TARGETS);
	view_id_size = cbs->bind_cmd_count*sizeof(uint32);
	cmd_size = sizeof(*cmd) + view_id_size;
	cmd = VMW_CMD_CTX_RESERVE(ctx->dev_priv, cmd_size, ctx->id);
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_DX_SET_RENDERTARGETS;
	cmd->header.size = sizeof(cmd->body) + view_id_size;

	if (cbs->ds_view.bi.ctx && !cbs->ds_view.bi.scrubbed)
		cmd->body.depthStencilViewId = cbs->ds_view.bi.res->id;
	else
		cmd->body.depthStencilViewId = SVGA3D_INVALID_ID;

	memcpy(&cmd[1], cbs->bind_cmd_buffer, view_id_size);

	vmw_cmd_commit(ctx->dev_priv, cmd_size);

	return 0;

}

/**
 * vmw_collect_so_targets - Build SVGA3dSoTarget data for a binding command
 * without checking which bindings actually need to be emitted
 *
 * @cbs: Pointer to the context's struct vmw_ctx_binding_state
 * @bi: Pointer to where the binding info array is stored in @cbs
 * @max_num: Maximum number of entries in the @bi array.
 *
 * Scans the @bi array for bindings and builds a buffer of SVGA3dSoTarget data.
 * Stops at the first non-existing binding in the @bi array.
 * On output, @cbs->bind_cmd_count contains the number of bindings to be
 * emitted, @cbs->bind_first_slot is set to zero, and @cbs->bind_cmd_buffer
 * contains the command data.
 */
static void vmw_collect_so_targets(struct vmw_ctx_binding_state *cbs,
				   const struct vmw_ctx_bindinfo *bi,
				   u32 max_num)
{
	const struct vmw_ctx_bindinfo_so_target *biso =
		container_of(bi, struct vmw_ctx_bindinfo_so_target, bi);
	unsigned long i;
	SVGA3dSoTarget *so_buffer = (SVGA3dSoTarget *) cbs->bind_cmd_buffer;

	cbs->bind_cmd_count = 0;
	cbs->bind_first_slot = 0;

	for (i = 0; i < max_num; ++i, ++biso, ++so_buffer,
		    ++cbs->bind_cmd_count) {
		if (!biso->bi.ctx)
			break;

		if (!biso->bi.scrubbed) {
			so_buffer->sid = biso->bi.res->id;
			so_buffer->offset = biso->offset;
			so_buffer->sizeInBytes = biso->size;
		} else {
			so_buffer->sid = SVGA3D_INVALID_ID;
			so_buffer->offset = 0;
			so_buffer->sizeInBytes = 0;
		}
	}
}

/**
 * vmw_emit_set_so_target - Issue delayed streamout binding commands
 *
 * @cbs: Pointer to the context's struct vmw_ctx_binding_state
 */
static int vmw_emit_set_so_target(struct vmw_ctx_binding_state *cbs)
{
	const struct vmw_ctx_bindinfo *loc = &cbs->so_targets[0].bi;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXSetSOTargets body;
	} *cmd;
	size_t cmd_size, so_target_size;
	const struct vmw_resource *ctx = vmw_cbs_context(cbs);

	vmw_collect_so_targets(cbs, loc, SVGA3D_DX_MAX_SOTARGETS);
	if (cbs->bind_cmd_count == 0)
		return 0;

	so_target_size = cbs->bind_cmd_count*sizeof(SVGA3dSoTarget);
	cmd_size = sizeof(*cmd) + so_target_size;
	cmd = VMW_CMD_CTX_RESERVE(ctx->dev_priv, cmd_size, ctx->id);
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_DX_SET_SOTARGETS;
	cmd->header.size = sizeof(cmd->body) + so_target_size;
	memcpy(&cmd[1], cbs->bind_cmd_buffer, so_target_size);

	vmw_cmd_commit(ctx->dev_priv, cmd_size);

	return 0;

}

/**
 * vmw_binding_emit_dirty_ps - Issue delayed per shader binding commands
 *
 * @cbs: Pointer to the context's struct vmw_ctx_binding_state
 *
 */
static int vmw_binding_emit_dirty_ps(struct vmw_ctx_binding_state *cbs)
{
	struct vmw_dx_shader_bindings *sb = &cbs->per_shader[0];
	u32 i;
	int ret;

	for (i = 0; i < SVGA3D_NUM_SHADERTYPE_DX10; ++i, ++sb) {
		if (!test_bit(VMW_BINDING_PS_SR_BIT, &sb->dirty))
			continue;

		ret = vmw_emit_set_sr(cbs, i);
		if (ret)
			break;

		__clear_bit(VMW_BINDING_PS_SR_BIT, &sb->dirty);
	}

	return 0;
}

/**
 * vmw_collect_dirty_vbs - Build SVGA3dVertexBuffer data for a
 * SVGA3dCmdDXSetVertexBuffers command
 *
 * @cbs: Pointer to the context's struct vmw_ctx_binding_state
 * @bi: Pointer to where the binding info array is stored in @cbs
 * @dirty: Bitmap indicating which bindings need to be emitted.
 * @max_num: Maximum number of entries in the @bi array.
 *
 * Scans the @bi array for bindings that need to be emitted and
 * builds a buffer of SVGA3dVertexBuffer data.
 * On output, @cbs->bind_cmd_count contains the number of bindings to be
 * emitted, @cbs->bind_first_slot indicates the index of the first emitted
 * binding, and @cbs->bind_cmd_buffer contains the command data.
 */
static void vmw_collect_dirty_vbs(struct vmw_ctx_binding_state *cbs,
				  const struct vmw_ctx_bindinfo *bi,
				  unsigned long *dirty,
				  u32 max_num)
{
	const struct vmw_ctx_bindinfo_vb *biv =
		container_of(bi, struct vmw_ctx_bindinfo_vb, bi);
	unsigned long i, next_bit;
	SVGA3dVertexBuffer *vbs = (SVGA3dVertexBuffer *) &cbs->bind_cmd_buffer;

	cbs->bind_cmd_count = 0;
	i = find_first_bit(dirty, max_num);
	next_bit = i;
	cbs->bind_first_slot = i;

	biv += i;
	for (; i < max_num; ++i, ++biv, ++vbs) {
		if (!biv->bi.ctx || biv->bi.scrubbed) {
			vbs->sid = SVGA3D_INVALID_ID;
			vbs->stride = 0;
			vbs->offset = 0;
		} else {
			vbs->sid = biv->bi.res->id;
			vbs->stride = biv->stride;
			vbs->offset = biv->offset;
		}
		cbs->bind_cmd_count++;
		if (next_bit == i) {
			next_bit = find_next_bit(dirty, max_num, i + 1);
			if (next_bit >= max_num)
				break;
		}
	}
}

/**
 * vmw_binding_emit_set_vb - Issue delayed vertex buffer binding commands
 *
 * @cbs: Pointer to the context's struct vmw_ctx_binding_state
 *
 */
static int vmw_emit_set_vb(struct vmw_ctx_binding_state *cbs)
{
	const struct vmw_ctx_bindinfo *loc =
		&cbs->vertex_buffers[0].bi;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXSetVertexBuffers body;
	} *cmd;
	size_t cmd_size, set_vb_size;
	const struct vmw_resource *ctx = vmw_cbs_context(cbs);

	vmw_collect_dirty_vbs(cbs, loc, cbs->dirty_vb,
			     SVGA3D_DX_MAX_VERTEXBUFFERS);
	if (cbs->bind_cmd_count == 0)
		return 0;

	set_vb_size = cbs->bind_cmd_count*sizeof(SVGA3dVertexBuffer);
	cmd_size = sizeof(*cmd) + set_vb_size;
	cmd = VMW_CMD_CTX_RESERVE(ctx->dev_priv, cmd_size, ctx->id);
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_DX_SET_VERTEX_BUFFERS;
	cmd->header.size = sizeof(cmd->body) + set_vb_size;
	cmd->body.startBuffer = cbs->bind_first_slot;

	memcpy(&cmd[1], cbs->bind_cmd_buffer, set_vb_size);

	vmw_cmd_commit(ctx->dev_priv, cmd_size);
	bitmap_clear(cbs->dirty_vb,
		     cbs->bind_first_slot, cbs->bind_cmd_count);

	return 0;
}

static int vmw_emit_set_uav(struct vmw_ctx_binding_state *cbs)
{
	const struct vmw_ctx_bindinfo *loc = &cbs->ua_views[0].views[0].bi;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXSetUAViews body;
	} *cmd;
	size_t cmd_size, view_id_size;
	const struct vmw_resource *ctx = vmw_cbs_context(cbs);

	vmw_collect_view_ids(cbs, loc, SVGA3D_MAX_UAVIEWS);
	view_id_size = cbs->bind_cmd_count*sizeof(uint32);
	cmd_size = sizeof(*cmd) + view_id_size;
	cmd = VMW_CMD_CTX_RESERVE(ctx->dev_priv, cmd_size, ctx->id);
	if (!cmd)
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_DX_SET_UA_VIEWS;
	cmd->header.size = sizeof(cmd->body) + view_id_size;

	/* Splice index is specified user-space   */
	cmd->body.uavSpliceIndex = cbs->ua_views[0].index;

	memcpy(&cmd[1], cbs->bind_cmd_buffer, view_id_size);

	vmw_cmd_commit(ctx->dev_priv, cmd_size);

	return 0;
}

static int vmw_emit_set_cs_uav(struct vmw_ctx_binding_state *cbs)
{
	const struct vmw_ctx_bindinfo *loc = &cbs->ua_views[1].views[0].bi;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXSetCSUAViews body;
	} *cmd;
	size_t cmd_size, view_id_size;
	const struct vmw_resource *ctx = vmw_cbs_context(cbs);

	vmw_collect_view_ids(cbs, loc, SVGA3D_MAX_UAVIEWS);
	view_id_size = cbs->bind_cmd_count*sizeof(uint32);
	cmd_size = sizeof(*cmd) + view_id_size;
	cmd = VMW_CMD_CTX_RESERVE(ctx->dev_priv, cmd_size, ctx->id);
	if (!cmd)
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_DX_SET_CS_UA_VIEWS;
	cmd->header.size = sizeof(cmd->body) + view_id_size;

	/* Start index is specified user-space */
	cmd->body.startIndex = cbs->ua_views[1].index;

	memcpy(&cmd[1], cbs->bind_cmd_buffer, view_id_size);

	vmw_cmd_commit(ctx->dev_priv, cmd_size);

	return 0;
}

/**
 * vmw_binding_emit_dirty - Issue delayed binding commands
 *
 * @cbs: Pointer to the context's struct vmw_ctx_binding_state
 *
 * This function issues the delayed binding commands that arise from
 * previous scrub / unscrub calls. These binding commands are typically
 * commands that batch a number of bindings and therefore it makes sense
 * to delay them.
 */
static int vmw_binding_emit_dirty(struct vmw_ctx_binding_state *cbs)
{
	int ret = 0;
	unsigned long hit = 0;

	while ((hit = find_next_bit(&cbs->dirty, VMW_BINDING_NUM_BITS, hit))
	      < VMW_BINDING_NUM_BITS) {

		switch (hit) {
		case VMW_BINDING_RT_BIT:
			ret = vmw_emit_set_rt(cbs);
			break;
		case VMW_BINDING_PS_BIT:
			ret = vmw_binding_emit_dirty_ps(cbs);
			break;
		case VMW_BINDING_SO_T_BIT:
			ret = vmw_emit_set_so_target(cbs);
			break;
		case VMW_BINDING_VB_BIT:
			ret = vmw_emit_set_vb(cbs);
			break;
		case VMW_BINDING_UAV_BIT:
			ret = vmw_emit_set_uav(cbs);
			break;
		case VMW_BINDING_CS_UAV_BIT:
			ret = vmw_emit_set_cs_uav(cbs);
			break;
		default:
			BUG();
		}
		if (ret)
			return ret;

		__clear_bit(hit, &cbs->dirty);
		hit++;
	}

	return 0;
}

/**
 * vmw_binding_scrub_sr - Schedule a dx shaderresource binding
 * scrub from a context
 *
 * @bi: single binding information.
 * @rebind: Whether to issue a bind instead of scrub command.
 */
static int vmw_binding_scrub_sr(struct vmw_ctx_bindinfo *bi, bool rebind)
{
	struct vmw_ctx_bindinfo_view *biv =
		container_of(bi, struct vmw_ctx_bindinfo_view, bi);
	struct vmw_ctx_binding_state *cbs =
		vmw_context_binding_state(bi->ctx);

	__set_bit(biv->slot, cbs->per_shader[biv->shader_slot].dirty_sr);
	__set_bit(VMW_BINDING_PS_SR_BIT,
		  &cbs->per_shader[biv->shader_slot].dirty);
	__set_bit(VMW_BINDING_PS_BIT, &cbs->dirty);

	return 0;
}

/**
 * vmw_binding_scrub_dx_rt - Schedule a dx rendertarget binding
 * scrub from a context
 *
 * @bi: single binding information.
 * @rebind: Whether to issue a bind instead of scrub command.
 */
static int vmw_binding_scrub_dx_rt(struct vmw_ctx_bindinfo *bi, bool rebind)
{
	struct vmw_ctx_binding_state *cbs =
		vmw_context_binding_state(bi->ctx);

	__set_bit(VMW_BINDING_RT_BIT, &cbs->dirty);

	return 0;
}

/**
 * vmw_binding_scrub_so_target - Schedule a dx streamoutput buffer binding
 * scrub from a context
 *
 * @bi: single binding information.
 * @rebind: Whether to issue a bind instead of scrub command.
 */
static int vmw_binding_scrub_so_target(struct vmw_ctx_bindinfo *bi, bool rebind)
{
	struct vmw_ctx_binding_state *cbs =
		vmw_context_binding_state(bi->ctx);

	__set_bit(VMW_BINDING_SO_T_BIT, &cbs->dirty);

	return 0;
}

/**
 * vmw_binding_scrub_vb - Schedule a dx vertex buffer binding
 * scrub from a context
 *
 * @bi: single binding information.
 * @rebind: Whether to issue a bind instead of scrub command.
 */
static int vmw_binding_scrub_vb(struct vmw_ctx_bindinfo *bi, bool rebind)
{
	struct vmw_ctx_bindinfo_vb *bivb =
		container_of(bi, struct vmw_ctx_bindinfo_vb, bi);
	struct vmw_ctx_binding_state *cbs =
		vmw_context_binding_state(bi->ctx);

	__set_bit(bivb->slot, cbs->dirty_vb);
	__set_bit(VMW_BINDING_VB_BIT, &cbs->dirty);

	return 0;
}

/**
 * vmw_binding_scrub_ib - scrub a dx index buffer binding from a context
 *
 * @bi: single binding information.
 * @rebind: Whether to issue a bind instead of scrub command.
 */
static int vmw_binding_scrub_ib(struct vmw_ctx_bindinfo *bi, bool rebind)
{
	struct vmw_ctx_bindinfo_ib *binding =
		container_of(bi, typeof(*binding), bi);
	struct vmw_private *dev_priv = bi->ctx->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXSetIndexBuffer body;
	} *cmd;

	cmd = VMW_CMD_CTX_RESERVE(dev_priv, sizeof(*cmd), bi->ctx->id);
	if (unlikely(cmd == NULL))
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_DX_SET_INDEX_BUFFER;
	cmd->header.size = sizeof(cmd->body);
	if (rebind) {
		cmd->body.sid = bi->res->id;
		cmd->body.format = binding->format;
		cmd->body.offset = binding->offset;
	} else {
		cmd->body.sid = SVGA3D_INVALID_ID;
		cmd->body.format = 0;
		cmd->body.offset = 0;
	}

	vmw_cmd_commit(dev_priv, sizeof(*cmd));

	return 0;
}

static int vmw_binding_scrub_uav(struct vmw_ctx_bindinfo *bi, bool rebind)
{
	struct vmw_ctx_binding_state *cbs = vmw_context_binding_state(bi->ctx);

	__set_bit(VMW_BINDING_UAV_BIT, &cbs->dirty);
	return 0;
}

static int vmw_binding_scrub_cs_uav(struct vmw_ctx_bindinfo *bi, bool rebind)
{
	struct vmw_ctx_binding_state *cbs = vmw_context_binding_state(bi->ctx);

	__set_bit(VMW_BINDING_CS_UAV_BIT, &cbs->dirty);
	return 0;
}

/**
 * vmw_binding_scrub_so - Scrub a streamoutput binding from context.
 * @bi: Single binding information.
 * @rebind: Whether to issue a bind instead of scrub command.
 */
static int vmw_binding_scrub_so(struct vmw_ctx_bindinfo *bi, bool rebind)
{
	struct vmw_ctx_bindinfo_so *binding =
		container_of(bi, typeof(*binding), bi);
	struct vmw_private *dev_priv = bi->ctx->dev_priv;
	struct {
		SVGA3dCmdHeader header;
		SVGA3dCmdDXSetStreamOutput body;
	} *cmd;

	cmd = VMW_CMD_CTX_RESERVE(dev_priv, sizeof(*cmd), bi->ctx->id);
	if (!cmd)
		return -ENOMEM;

	cmd->header.id = SVGA_3D_CMD_DX_SET_STREAMOUTPUT;
	cmd->header.size = sizeof(cmd->body);
	cmd->body.soid = rebind ? bi->res->id : SVGA3D_INVALID_ID;
	vmw_cmd_commit(dev_priv, sizeof(*cmd));

	return 0;
}

/**
 * vmw_binding_state_alloc - Allocate a struct vmw_ctx_binding_state with
 * memory accounting.
 *
 * @dev_priv: Pointer to a device private structure.
 *
 * Returns a pointer to a newly allocated struct or an error pointer on error.
 */
struct vmw_ctx_binding_state *
vmw_binding_state_alloc(struct vmw_private *dev_priv)
{
	struct vmw_ctx_binding_state *cbs;
	struct ttm_operation_ctx ctx = {
		.interruptible = false,
		.no_wait_gpu = false
	};
	int ret;

	ret = ttm_mem_global_alloc(vmw_mem_glob(dev_priv), sizeof(*cbs),
				&ctx);
	if (ret)
		return ERR_PTR(ret);

	cbs = vzalloc(sizeof(*cbs));
	if (!cbs) {
		ttm_mem_global_free(vmw_mem_glob(dev_priv), sizeof(*cbs));
		return ERR_PTR(-ENOMEM);
	}

	cbs->dev_priv = dev_priv;
	INIT_LIST_HEAD(&cbs->list);

	return cbs;
}

/**
 * vmw_binding_state_free - Free a struct vmw_ctx_binding_state and its
 * memory accounting info.
 *
 * @cbs: Pointer to the struct vmw_ctx_binding_state to be freed.
 */
void vmw_binding_state_free(struct vmw_ctx_binding_state *cbs)
{
	struct vmw_private *dev_priv = cbs->dev_priv;

	vfree(cbs);
	ttm_mem_global_free(vmw_mem_glob(dev_priv), sizeof(*cbs));
}

/**
 * vmw_binding_state_list - Get the binding list of a
 * struct vmw_ctx_binding_state
 *
 * @cbs: Pointer to the struct vmw_ctx_binding_state
 *
 * Returns the binding list which can be used to traverse through the bindings
 * and access the resource information of all bindings.
 */
struct list_head *vmw_binding_state_list(struct vmw_ctx_binding_state *cbs)
{
	return &cbs->list;
}

/**
 * vmwgfx_binding_state_reset - clear a struct vmw_ctx_binding_state
 *
 * @cbs: Pointer to the struct vmw_ctx_binding_state to be cleared
 *
 * Drops all bindings registered in @cbs. No device binding actions are
 * performed.
 */
void vmw_binding_state_reset(struct vmw_ctx_binding_state *cbs)
{
	struct vmw_ctx_bindinfo *entry, *next;

	list_for_each_entry_safe(entry, next, &cbs->list, ctx_list)
		vmw_binding_drop(entry);
}

/**
 * vmw_binding_dirtying - Return whether a binding type is dirtying its resource
 * @binding_type: The binding type
 *
 * Each time a resource is put on the validation list as the result of a
 * context binding referencing it, we need to determine whether that resource
 * will be dirtied (written to by the GPU) as a result of the corresponding
 * GPU operation. Currently rendertarget-, depth-stencil-, stream-output-target
 * and unordered access view bindings are capable of dirtying its resource.
 *
 * Return: Whether the binding type dirties the resource its binding points to.
 */
u32 vmw_binding_dirtying(enum vmw_ctx_binding_type binding_type)
{
	static u32 is_binding_dirtying[vmw_ctx_binding_max] = {
		[vmw_ctx_binding_rt] = VMW_RES_DIRTY_SET,
		[vmw_ctx_binding_dx_rt] = VMW_RES_DIRTY_SET,
		[vmw_ctx_binding_ds] = VMW_RES_DIRTY_SET,
		[vmw_ctx_binding_so_target] = VMW_RES_DIRTY_SET,
		[vmw_ctx_binding_uav] = VMW_RES_DIRTY_SET,
		[vmw_ctx_binding_cs_uav] = VMW_RES_DIRTY_SET,
	};

	/* Review this function as new bindings are added. */
	BUILD_BUG_ON(vmw_ctx_binding_max != 14);
	return is_binding_dirtying[binding_type];
}

/*
 * This function is unused at run-time, and only used to hold various build
 * asserts important for code optimization assumptions.
 */
static void vmw_binding_build_asserts(void)
{
	BUILD_BUG_ON(SVGA3D_NUM_SHADERTYPE_DX10 != 3);
	BUILD_BUG_ON(SVGA3D_MAX_SIMULTANEOUS_RENDER_TARGETS > SVGA3D_RT_MAX);
	BUILD_BUG_ON(sizeof(uint32) != sizeof(u32));

	/*
	 * struct vmw_ctx_binding_state::bind_cmd_buffer is used for various
	 * view id arrays.
	 */
	BUILD_BUG_ON(VMW_MAX_VIEW_BINDINGS < SVGA3D_RT_MAX);
	BUILD_BUG_ON(VMW_MAX_VIEW_BINDINGS < SVGA3D_DX_MAX_SRVIEWS);
	BUILD_BUG_ON(VMW_MAX_VIEW_BINDINGS < SVGA3D_DX_MAX_CONSTBUFFERS);

	/*
	 * struct vmw_ctx_binding_state::bind_cmd_buffer is used for
	 * u32 view ids, SVGA3dSoTargets and SVGA3dVertexBuffers
	 */
	BUILD_BUG_ON(SVGA3D_DX_MAX_SOTARGETS*sizeof(SVGA3dSoTarget) >
		     VMW_MAX_VIEW_BINDINGS*sizeof(u32));
	BUILD_BUG_ON(SVGA3D_DX_MAX_VERTEXBUFFERS*sizeof(SVGA3dVertexBuffer) >
		     VMW_MAX_VIEW_BINDINGS*sizeof(u32));
}
