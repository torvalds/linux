// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "gen7_renderclear.h"
#include "i915_drv.h"
#include "intel_gpu_commands.h"

#define MAX_URB_ENTRIES 64
#define STATE_SIZE (4 * 1024)
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
	u32 max_primitives;
	u32 max_urb_entries;
	u32 cmd_size;
	u32 state_size;
	u32 state_start;
	u32 batch_size;
	u32 surface_height;
	u32 surface_width;
	u32 scratch_size;
	u32 max_size;
};

static void
batch_get_defaults(struct drm_i915_private *i915, struct batch_vals *bv)
{
	if (IS_HASWELL(i915)) {
		bv->max_primitives = 280;
		bv->max_urb_entries = MAX_URB_ENTRIES;
		bv->surface_height = 16 * 16;
		bv->surface_width = 32 * 2 * 16;
	} else {
		bv->max_primitives = 128;
		bv->max_urb_entries = MAX_URB_ENTRIES / 2;
		bv->surface_height = 16 * 8;
		bv->surface_width = 32 * 16;
	}
	bv->cmd_size = bv->max_primitives * 4096;
	bv->state_size = STATE_SIZE;
	bv->state_start = bv->cmd_size;
	bv->batch_size = bv->cmd_size + bv->state_size;
	bv->scratch_size = bv->surface_height * bv->surface_width;
	bv->max_size = bv->batch_size + bv->scratch_size;
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
	u32 surface_start = gen7_fill_surface_state(state, bv->batch_size, bv);
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
	u32 *cs = batch_alloc_items(batch, 0, 12);

	*cs++ = STATE_BASE_ADDRESS | (12 - 2);
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
	*cs++ = 0;
	*cs++ = 0;
	batch_advance(batch, cs);
}

static void
gen7_emit_vfe_state(struct batch_chunk *batch,
		    const struct batch_vals *bv,
		    u32 urb_size, u32 curbe_size,
		    u32 mode)
{
	u32 urb_entries = bv->max_urb_entries;
	u32 threads = bv->max_primitives - 1;
	u32 *cs = batch_alloc_items(batch, 32, 8);

	*cs++ = MEDIA_VFE_STATE | (8 - 2);

	/* scratch buffer */
	*cs++ = 0;

	/* number of threads & urb entries for GPGPU vs Media Mode */
	*cs++ = threads << 16 | urb_entries << 8 | mode << 2;

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
	unsigned int inline_data_size;
	unsigned int media_batch_size;
	unsigned int i;
	u32 *cs;

	inline_data_size = 112 * 8;
	media_batch_size = inline_data_size + 6;

	cs = batch_alloc_items(batch, 8, media_batch_size);

	*cs++ = MEDIA_OBJECT | (media_batch_size - 2);

	/* interface descriptor offset */
	*cs++ = 0;

	/* without indirect data */
	*cs++ = 0;
	*cs++ = 0;

	/* scoreboard */
	*cs++ = 0;
	*cs++ = 0;

	/* inline */
	*cs++ = (y_offset << 16) | (x_offset);
	*cs++ = 0;
	*cs++ = GT3_INLINE_DATA_DELAYS;
	for (i = 3; i < inline_data_size; i++)
		*cs++ = 0;

	batch_advance(batch, cs);
}

static void gen7_emit_pipeline_flush(struct batch_chunk *batch)
{
	u32 *cs = batch_alloc_items(batch, 0, 5);

	*cs++ = GFX_OP_PIPE_CONTROL(5);
	*cs++ = PIPE_CONTROL_STATE_CACHE_INVALIDATE |
		PIPE_CONTROL_GLOBAL_GTT_IVB;
	*cs++ = 0;
	*cs++ = 0;
	*cs++ = 0;
	batch_advance(batch, cs);
}

static void emit_batch(struct i915_vma * const vma,
		       u32 *start,
		       const struct batch_vals *bv)
{
	struct drm_i915_private *i915 = vma->vm->i915;
	unsigned int desc_count = 64;
	const u32 urb_size = 112;
	struct batch_chunk cmds, state;
	u32 interface_descriptor;
	unsigned int i;

	batch_init(&cmds, vma, start, 0, bv->cmd_size);
	batch_init(&state, vma, start, bv->state_start, bv->state_size);

	interface_descriptor =
		gen7_fill_interface_descriptor(&state, bv,
					       IS_HASWELL(i915) ?
					       &cb_kernel_hsw :
					       &cb_kernel_ivb,
					       desc_count);
	gen7_emit_pipeline_flush(&cmds);
	batch_add(&cmds, PIPELINE_SELECT | PIPELINE_SELECT_MEDIA);
	batch_add(&cmds, MI_NOOP);
	gen7_emit_state_base_address(&cmds, interface_descriptor);
	gen7_emit_pipeline_flush(&cmds);

	gen7_emit_vfe_state(&cmds, bv, urb_size - 1, 0, 0);

	gen7_emit_interface_descriptor_load(&cmds,
					    interface_descriptor,
					    desc_count);

	for (i = 0; i < bv->max_primitives; i++)
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
		return bv.max_size;

	GEM_BUG_ON(vma->obj->base.size < bv.max_size);

	batch = i915_gem_object_pin_map(vma->obj, I915_MAP_WC);
	if (IS_ERR(batch))
		return PTR_ERR(batch);

	emit_batch(vma, memset(batch, 0, bv.max_size), &bv);

	i915_gem_object_flush_map(vma->obj);
	__i915_gem_object_release_map(vma->obj);

	return 0;
}
