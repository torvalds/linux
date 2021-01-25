// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "gen7_renderclear.h"
#include "i915_drv.h"
#include "intel_gpu_commands.h"

#define GT3_INLINE_DATA_DELAYS 0x1E00
#define batch_advance(Y, CS) GEM_BUG_ON((Y)->end != (CS))

struct cb_kernel {
	const void *data;
	u32 size;
};

#define CB_KERNEL(name) { .data = (name), .size = sizeof(name) }

#include "ivb_clear_kernel.c"
static const struct cb_kernel cb_kernel_ivb = CB_KERNEL(ivb_clear_kernel);

#include "hsw_clear_kernel.c"
static const struct cb_kernel cb_kernel_hsw = CB_KERNEL(hsw_clear_kernel);

struct batch_chunk {
	struct i915_vma *vma;
	u32 offset;
	u32 *start;
	u32 *end;
	u32 max_items;
};

struct batch_vals {
	u32 max_threads;
	u32 state_start;
	u32 surface_start;
	u32 surface_height;
	u32 surface_width;
	u32 size;
};

static inline int num_primitives(const struct batch_vals *bv)
{
	/*
	 * We need to saturate the GPU with work in order to dispatch
	 * a shader on every HW thread, and clear the thread-local registers.
	 * In short, we have to dispatch work faster than the shaders can
	 * run in order to fill the EU and occupy each HW thread.
	 */
	return bv->max_threads;
}

static void
batch_get_defaults(struct drm_i915_private *i915, struct batch_vals *bv)
{
	if (IS_HASWELL(i915)) {
		switch (INTEL_INFO(i915)->gt) {
		default:
		case 1:
			bv->max_threads = 70;
			break;
		case 2:
			bv->max_threads = 140;
			break;
		case 3:
			bv->max_threads = 280;
			break;
		}
		bv->surface_height = 16 * 16;
		bv->surface_width = 32 * 2 * 16;
	} else {
		switch (INTEL_INFO(i915)->gt) {
		default:
		case 1: /* including vlv */
			bv->max_threads = 36;
			break;
		case 2:
			bv->max_threads = 128;
			break;
		}
		bv->surface_height = 16 * 8;
		bv->surface_width = 32 * 16;
	}
	bv->state_start = round_up(SZ_1K + num_primitives(bv) * 64, SZ_4K);
	bv->surface_start = bv->state_start + SZ_4K;
	bv->size = bv->surface_start + bv->surface_height * bv->surface_width;
}

static void batch_init(struct batch_chunk *bc,
		       struct i915_vma *vma,
		       u32 *start, u32 offset, u32 max_bytes)
{
	bc->vma = vma;
	bc->offset = offset;
	bc->start = start + bc->offset / sizeof(*bc->start);
	bc->end = bc->start;
	bc->max_items = max_bytes / sizeof(*bc->start);
}

static u32 batch_offset(const struct batch_chunk *bc, u32 *cs)
{
	return (cs - bc->start) * sizeof(*bc->start) + bc->offset;
}

static u32 batch_addr(const struct batch_chunk *bc)
{
	return bc->vma->node.start;
}

static void batch_add(struct batch_chunk *bc, const u32 d)
{
	GEM_BUG_ON((bc->end - bc->start) >= bc->max_items);
	*bc->end++ = d;
}

static u32 *batch_alloc_items(struct batch_chunk *bc, u32 align, u32 items)
{
	u32 *map;

	if (align) {
		u32 *end = PTR_ALIGN(bc->end, align);

		memset32(bc->end, 0, end - bc->end);
		bc->end = end;
	}

	map = bc->end;
	bc->end += items;

	return map;
}

static u32 *batch_alloc_bytes(struct batch_chunk *bc, u32 align, u32 bytes)
{
	GEM_BUG_ON(!IS_ALIGNED(bytes, sizeof(*bc->start)));
	return batch_alloc_items(bc, align, bytes / sizeof(*bc->start));
}

static u32
gen7_fill_surface_state(struct batch_chunk *state,
			const u32 dst_offset,
			const struct batch_vals *bv)
{
	u32 surface_h = bv->surface_height;
	u32 surface_w = bv->surface_width;
	u32 *cs = batch_alloc_items(state, 32, 8);
	u32 offset = batch_offset(state, cs);

#define SURFACE_2D 1
#define SURFACEFORMAT_B8G8R8A8_UNORM 0x0C0
#define RENDER_CACHE_READ_WRITE 1

	*cs++ = SURFACE_2D << 29 |
		(SURFACEFORMAT_B8G8R8A8_UNORM << 18) |
		(RENDER_CACHE_READ_WRITE << 8);

	*cs++ = batch_addr(state) + dst_offset;

	*cs++ = ((surface_h / 4 - 1) << 16) | (surface_w / 4 - 1);
	*cs++ = surface_w;
	*cs++ = 0;
	*cs++ = 0;
	*cs++ = 0;
#define SHADER_CHANNELS(r, g, b, a) \
	(((r) << 25) | ((g) << 22) | ((b) << 19) | ((a) << 16))
	*cs++ = SHADER_CHANNELS(4, 5, 6, 7);
	batch_advance(state, cs);

	return offset;
}

static u32
gen7_fill_binding_table(struct batch_chunk *state,
			const struct batch_vals *bv)
{
	u32 surface_start =
		gen7_fill_surface_state(state, bv->surface_start, bv);
	u32 *cs = batch_alloc_items(state, 32, 8);
	u32 offset = batch_offset(state, cs);

	*cs++ = surface_start - state->offset;
	*cs++ = 0;
	*cs++ = 0;
	*cs++ = 0;
	*cs++ = 0;
	*cs++ = 0;
	*cs++ = 0;
	*cs++ = 0;
	batch_advance(state, cs);

	return offset;
}

static u32
gen7_fill_kernel_data(struct batch_chunk *state,
		      const u32 *data,
		      const u32 size)
{
	return batch_offset(state,
			    memcpy(batch_alloc_bytes(state, 64, size),
				   data, size));
}

static u32
gen7_fill_interface_descriptor(struct batch_chunk *state,
			       const struct batch_vals *bv,
			       const struct cb_kernel *kernel,
			       unsigned int count)
{
	u32 kernel_offset =
		gen7_fill_kernel_data(state, kernel->data, kernel->size);
	u32 binding_table = gen7_fill_binding_table(state, bv);
	u32 *cs = batch_alloc_items(state, 32, 8 * count);
	u32 offset = batch_offset(state, cs);

	*cs++ = kernel_offset;
	*cs++ = (1 << 7) | (1 << 13);
	*cs++ = 0;
	*cs++ = (binding_table - state->offset) | 1;
	*cs++ = 0;
	*cs++ = 0;
	*cs++ = 0;
	*cs++ = 0;

	/* 1 - 63dummy idds */
	memset32(cs, 0x00, (count - 1) * 8);
	batch_advance(state, cs + (count - 1) * 8);

	return offset;
}

static void
gen7_emit_state_base_address(struct batch_chunk *batch,
			     u32 surface_state_base)
{
	u32 *cs = batch_alloc_items(batch, 0, 10);

	*cs++ = STATE_BASE_ADDRESS | (10 - 2);
	/* general */
	*cs++ = batch_addr(batch) | BASE_ADDRESS_MODIFY;
	/* surface */
	*cs++ = batch_addr(batch) | surface_state_base | BASE_ADDRESS_MODIFY;
	/* dynamic */
	*cs++ = batch_addr(batch) | BASE_ADDRESS_MODIFY;
	/* indirect */
	*cs++ = batch_addr(batch) | BASE_ADDRESS_MODIFY;
	/* instruction */
	*cs++ = batch_addr(batch) | BASE_ADDRESS_MODIFY;

	/* general/dynamic/indirect/instruction access Bound */
	*cs++ = 0;
	*cs++ = BASE_ADDRESS_MODIFY;
	*cs++ = 0;
	*cs++ = BASE_ADDRESS_MODIFY;
	batch_advance(batch, cs);
}

static void
gen7_emit_vfe_state(struct batch_chunk *batch,
		    const struct batch_vals *bv,
		    u32 urb_size, u32 curbe_size,
		    u32 mode)
{
	u32 threads = bv->max_threads - 1;
	u32 *cs = batch_alloc_items(batch, 32, 8);

	*cs++ = MEDIA_VFE_STATE | (8 - 2);

	/* scratch buffer */
	*cs++ = 0;

	/* number of threads & urb entries for GPGPU vs Media Mode */
	*cs++ = threads << 16 | 1 << 8 | mode << 2;

	*cs++ = 0;

	/* urb entry size & curbe size in 256 bits unit */
	*cs++ = urb_size << 16 | curbe_size;

	/* scoreboard */
	*cs++ = 0;
	*cs++ = 0;
	*cs++ = 0;
	batch_advance(batch, cs);
}

static void
gen7_emit_interface_descriptor_load(struct batch_chunk *batch,
				    const u32 interface_descriptor,
				    unsigned int count)
{
	u32 *cs = batch_alloc_items(batch, 8, 4);

	*cs++ = MEDIA_INTERFACE_DESCRIPTOR_LOAD | (4 - 2);
	*cs++ = 0;
	*cs++ = count * 8 * sizeof(*cs);

	/*
	 * interface descriptor address - it is relative to the dynamics base
	 * address
	 */
	*cs++ = interface_descriptor;
	batch_advance(batch, cs);
}

static void
gen7_emit_media_object(struct batch_chunk *batch,
		       unsigned int media_object_index)
{
	unsigned int x_offset = (media_object_index % 16) * 64;
	unsigned int y_offset = (media_object_index / 16) * 16;
	unsigned int pkt = 6 + 3;
	u32 *cs;

	cs = batch_alloc_items(batch, 8, pkt);

	*cs++ = MEDIA_OBJECT | (pkt - 2);

	/* interface descriptor offset */
	*cs++ = 0;

	/* without indirect data */
	*cs++ = 0;
	*cs++ = 0;

	/* scoreboard */
	*cs++ = 0;
	*cs++ = 0;

	/* inline */
	*cs++ = y_offset << 16 | x_offset;
	*cs++ = 0;
	*cs++ = GT3_INLINE_DATA_DELAYS;

	batch_advance(batch, cs);
}

static void gen7_emit_pipeline_flush(struct batch_chunk *batch)
{
	u32 *cs = batch_alloc_items(batch, 0, 4);

	*cs++ = GFX_OP_PIPE_CONTROL(4);
	*cs++ = PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH |
		PIPE_CONTROL_DEPTH_CACHE_FLUSH |
		PIPE_CONTROL_DC_FLUSH_ENABLE |
		PIPE_CONTROL_CS_STALL;
	*cs++ = 0;
	*cs++ = 0;

	batch_advance(batch, cs);
}

static void gen7_emit_pipeline_invalidate(struct batch_chunk *batch)
{
	u32 *cs = batch_alloc_items(batch, 0, 8);

	/* ivb: Stall before STATE_CACHE_INVALIDATE */
	*cs++ = GFX_OP_PIPE_CONTROL(4);
	*cs++ = PIPE_CONTROL_STALL_AT_SCOREBOARD |
		PIPE_CONTROL_CS_STALL;
	*cs++ = 0;
	*cs++ = 0;

	*cs++ = GFX_OP_PIPE_CONTROL(4);
	*cs++ = PIPE_CONTROL_STATE_CACHE_INVALIDATE;
	*cs++ = 0;
	*cs++ = 0;

	batch_advance(batch, cs);
}

static void emit_batch(struct i915_vma * const vma,
		       u32 *start,
		       const struct batch_vals *bv)
{
	struct drm_i915_private *i915 = vma->vm->i915;
	const unsigned int desc_count = 1;
	const unsigned int urb_size = 1;
	struct batch_chunk cmds, state;
	u32 descriptors;
	unsigned int i;

	batch_init(&cmds, vma, start, 0, bv->state_start);
	batch_init(&state, vma, start, bv->state_start, SZ_4K);

	descriptors = gen7_fill_interface_descriptor(&state, bv,
						     IS_HASWELL(i915) ?
						     &cb_kernel_hsw :
						     &cb_kernel_ivb,
						     desc_count);

	gen7_emit_pipeline_invalidate(&cmds);
	batch_add(&cmds, PIPELINE_SELECT | PIPELINE_SELECT_MEDIA);
	batch_add(&cmds, MI_NOOP);
	gen7_emit_pipeline_invalidate(&cmds);

	gen7_emit_pipeline_flush(&cmds);
	gen7_emit_state_base_address(&cmds, descriptors);
	gen7_emit_pipeline_invalidate(&cmds);

	gen7_emit_vfe_state(&cmds, bv, urb_size - 1, 0, 0);
	gen7_emit_interface_descriptor_load(&cmds, descriptors, desc_count);

	for (i = 0; i < num_primitives(bv); i++)
		gen7_emit_media_object(&cmds, i);

	batch_add(&cmds, MI_BATCH_BUFFER_END);
}

int gen7_setup_clear_gpr_bb(struct intel_engine_cs * const engine,
			    struct i915_vma * const vma)
{
	struct batch_vals bv;
	u32 *batch;

	batch_get_defaults(engine->i915, &bv);
	if (!vma)
		return bv.size;

	GEM_BUG_ON(vma->obj->base.size < bv.size);

	batch = i915_gem_object_pin_map(vma->obj, I915_MAP_WC);
	if (IS_ERR(batch))
		return PTR_ERR(batch);

	emit_batch(vma, memset(batch, 0, bv.size), &bv);

	i915_gem_object_flush_map(vma->obj);
	__i915_gem_object_release_map(vma->obj);

	return 0;
}
