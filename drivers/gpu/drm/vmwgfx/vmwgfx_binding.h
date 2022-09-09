/* SPDX-License-Identifier: GPL-2.0 OR MIT */
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
#ifndef _VMWGFX_BINDING_H_
#define _VMWGFX_BINDING_H_

#include <linux/list.h>

#include "device_include/svga3d_reg.h"

#define VMW_MAX_VIEW_BINDINGS 128

#define VMW_MAX_UAV_BIND_TYPE 2

struct vmw_private;
struct vmw_ctx_binding_state;

/*
 * enum vmw_ctx_binding_type - abstract resource to context binding types
 */
enum vmw_ctx_binding_type {
	vmw_ctx_binding_shader,
	vmw_ctx_binding_rt,
	vmw_ctx_binding_tex,
	vmw_ctx_binding_cb,
	vmw_ctx_binding_dx_shader,
	vmw_ctx_binding_dx_rt,
	vmw_ctx_binding_sr,
	vmw_ctx_binding_ds,
	vmw_ctx_binding_so_target,
	vmw_ctx_binding_vb,
	vmw_ctx_binding_ib,
	vmw_ctx_binding_uav,
	vmw_ctx_binding_cs_uav,
	vmw_ctx_binding_so,
	vmw_ctx_binding_max
};

/**
 * struct vmw_ctx_bindinfo - single binding metadata
 *
 * @ctx_list: List head for the context's list of bindings.
 * @res_list: List head for a resource's list of bindings.
 * @ctx: Non-refcounted pointer to the context that owns the binding. NULL
 * indicates no binding present.
 * @res: Non-refcounted pointer to the resource the binding points to. This
 * is typically a surface or a view.
 * @bt: Binding type.
 * @scrubbed: Whether the binding has been scrubbed from the context.
 */
struct vmw_ctx_bindinfo {
	struct list_head ctx_list;
	struct list_head res_list;
	struct vmw_resource *ctx;
	struct vmw_resource *res;
	enum vmw_ctx_binding_type bt;
	bool scrubbed;
};

/**
 * struct vmw_ctx_bindinfo_tex - texture stage binding metadata
 *
 * @bi: struct vmw_ctx_bindinfo we derive from.
 * @texture_stage: Device data used to reconstruct binding command.
 */
struct vmw_ctx_bindinfo_tex {
	struct vmw_ctx_bindinfo bi;
	uint32 texture_stage;
};

/**
 * struct vmw_ctx_bindinfo_shader - Shader binding metadata
 *
 * @bi: struct vmw_ctx_bindinfo we derive from.
 * @shader_slot: Device data used to reconstruct binding command.
 */
struct vmw_ctx_bindinfo_shader {
	struct vmw_ctx_bindinfo bi;
	SVGA3dShaderType shader_slot;
};

/**
 * struct vmw_ctx_bindinfo_cb - Constant buffer binding metadata
 *
 * @bi: struct vmw_ctx_bindinfo we derive from.
 * @shader_slot: Device data used to reconstruct binding command.
 * @offset: Device data used to reconstruct binding command.
 * @size: Device data used to reconstruct binding command.
 * @slot: Device data used to reconstruct binding command.
 */
struct vmw_ctx_bindinfo_cb {
	struct vmw_ctx_bindinfo bi;
	SVGA3dShaderType shader_slot;
	uint32 offset;
	uint32 size;
	uint32 slot;
};

/**
 * struct vmw_ctx_bindinfo_view - View binding metadata
 *
 * @bi: struct vmw_ctx_bindinfo we derive from.
 * @shader_slot: Device data used to reconstruct binding command.
 * @slot: Device data used to reconstruct binding command.
 */
struct vmw_ctx_bindinfo_view {
	struct vmw_ctx_bindinfo bi;
	SVGA3dShaderType shader_slot;
	uint32 slot;
};

/**
 * struct vmw_ctx_bindinfo_so_target - StreamOutput binding metadata
 *
 * @bi: struct vmw_ctx_bindinfo we derive from.
 * @offset: Device data used to reconstruct binding command.
 * @size: Device data used to reconstruct binding command.
 * @slot: Device data used to reconstruct binding command.
 */
struct vmw_ctx_bindinfo_so_target {
	struct vmw_ctx_bindinfo bi;
	uint32 offset;
	uint32 size;
	uint32 slot;
};

/**
 * struct vmw_ctx_bindinfo_vb - Vertex buffer binding metadata
 *
 * @bi: struct vmw_ctx_bindinfo we derive from.
 * @offset: Device data used to reconstruct binding command.
 * @stride: Device data used to reconstruct binding command.
 * @slot: Device data used to reconstruct binding command.
 */
struct vmw_ctx_bindinfo_vb {
	struct vmw_ctx_bindinfo bi;
	uint32 offset;
	uint32 stride;
	uint32 slot;
};

/**
 * struct vmw_ctx_bindinfo_ib - StreamOutput binding metadata
 *
 * @bi: struct vmw_ctx_bindinfo we derive from.
 * @offset: Device data used to reconstruct binding command.
 * @format: Device data used to reconstruct binding command.
 */
struct vmw_ctx_bindinfo_ib {
	struct vmw_ctx_bindinfo bi;
	uint32 offset;
	uint32 format;
};

/**
 * struct vmw_dx_shader_bindings - per shader type context binding state
 *
 * @shader: The shader binding for this shader type
 * @const_buffer: Const buffer bindings for this shader type.
 * @shader_res: Shader resource view bindings for this shader type.
 * @dirty_sr: Bitmap tracking individual shader resource bindings changes
 * that have not yet been emitted to the device.
 * @dirty: Bitmap tracking per-binding type binding changes that have not
 * yet been emitted to the device.
 */
struct vmw_dx_shader_bindings {
	struct vmw_ctx_bindinfo_shader shader;
	struct vmw_ctx_bindinfo_cb const_buffers[SVGA3D_DX_MAX_CONSTBUFFERS];
	struct vmw_ctx_bindinfo_view shader_res[SVGA3D_DX_MAX_SRVIEWS];
	DECLARE_BITMAP(dirty_sr, SVGA3D_DX_MAX_SRVIEWS);
	unsigned long dirty;
};

/**
 * struct vmw_ctx_bindinfo_uav - UAV context binding state.
 * @views: UAV view bindings.
 * @splice_index: The device splice index set by user-space.
 */
struct vmw_ctx_bindinfo_uav {
	struct vmw_ctx_bindinfo_view views[SVGA3D_DX11_1_MAX_UAVIEWS];
	uint32 index;
};

/**
 * struct vmw_ctx_bindinfo_so - Stream output binding metadata.
 * @bi: struct vmw_ctx_bindinfo we derive from.
 * @slot: Device data used to reconstruct binding command.
 */
struct vmw_ctx_bindinfo_so {
	struct vmw_ctx_bindinfo bi;
	uint32 slot;
};

extern void vmw_binding_add(struct vmw_ctx_binding_state *cbs,
			    const struct vmw_ctx_bindinfo *ci,
			    u32 shader_slot, u32 slot);
extern void vmw_binding_cb_offset_update(struct vmw_ctx_binding_state *cbs,
					 u32 shader_slot, u32 slot, u32 offsetInBytes);
extern void vmw_binding_add_uav_index(struct vmw_ctx_binding_state *cbs,
				      uint32 slot, uint32 splice_index);
extern void
vmw_binding_state_commit(struct vmw_ctx_binding_state *to,
			 struct vmw_ctx_binding_state *from);
extern void vmw_binding_res_list_kill(struct list_head *head);
extern void vmw_binding_res_list_scrub(struct list_head *head);
extern int vmw_binding_rebind_all(struct vmw_ctx_binding_state *cbs);
extern void vmw_binding_state_kill(struct vmw_ctx_binding_state *cbs);
extern void vmw_binding_state_scrub(struct vmw_ctx_binding_state *cbs);
extern struct vmw_ctx_binding_state *
vmw_binding_state_alloc(struct vmw_private *dev_priv);
extern void vmw_binding_state_free(struct vmw_ctx_binding_state *cbs);
extern struct list_head *
vmw_binding_state_list(struct vmw_ctx_binding_state *cbs);
extern void vmw_binding_state_reset(struct vmw_ctx_binding_state *cbs);
extern u32 vmw_binding_dirtying(enum vmw_ctx_binding_type binding_type);


#endif
