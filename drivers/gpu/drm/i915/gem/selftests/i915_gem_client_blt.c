// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_selftest.h"

#include "gt/intel_context.h"
#include "gt/intel_engine_user.h"
#include "gt/intel_gpu_commands.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_regs.h"
#include "gem/i915_gem_lmem.h"

#include "selftests/igt_flush_test.h"
#include "selftests/mock_drm.h"
#include "selftests/i915_random.h"
#include "huge_gem_object.h"
#include "mock_context.h"

enum client_tiling {
	CLIENT_TILING_LINEAR,
	CLIENT_TILING_X,
	CLIENT_TILING_Y,
	CLIENT_NUM_TILING_TYPES
};

#define WIDTH 512
#define HEIGHT 32

struct blit_buffer {
	struct i915_vma *vma;
	u32 start_val;
	enum client_tiling tiling;
};

struct tiled_blits {
	struct intel_context *ce;
	struct blit_buffer buffers[3];
	struct blit_buffer scratch;
	struct i915_vma *batch;
	u64 hole;
	u64 align;
	u32 width;
	u32 height;
};

static int prepare_blit(const struct tiled_blits *t,
			struct blit_buffer *dst,
			struct blit_buffer *src,
			struct drm_i915_gem_object *batch)
{
	const int ver = GRAPHICS_VER(to_i915(batch->base.dev));
	bool use_64b_reloc = ver >= 8;
	u32 src_pitch, dst_pitch;
	u32 cmd, *cs;

	cs = i915_gem_object_pin_map_unlocked(batch, I915_MAP_WC);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = MI_LOAD_REGISTER_IMM(1);
	*cs++ = i915_mmio_reg_offset(BCS_SWCTRL);
	cmd = (BCS_SRC_Y | BCS_DST_Y) << 16;
	if (src->tiling == CLIENT_TILING_Y)
		cmd |= BCS_SRC_Y;
	if (dst->tiling == CLIENT_TILING_Y)
		cmd |= BCS_DST_Y;
	*cs++ = cmd;

	cmd = MI_FLUSH_DW;
	if (ver >= 8)
		cmd++;
	*cs++ = cmd;
	*cs++ = 0;
	*cs++ = 0;
	*cs++ = 0;

	cmd = XY_SRC_COPY_BLT_CMD | BLT_WRITE_RGBA | (8 - 2);
	if (ver >= 8)
		cmd += 2;

	src_pitch = t->width * 4;
	if (src->tiling) {
		cmd |= XY_SRC_COPY_BLT_SRC_TILED;
		src_pitch /= 4;
	}

	dst_pitch = t->width * 4;
	if (dst->tiling) {
		cmd |= XY_SRC_COPY_BLT_DST_TILED;
		dst_pitch /= 4;
	}

	*cs++ = cmd;
	*cs++ = BLT_DEPTH_32 | BLT_ROP_SRC_COPY | dst_pitch;
	*cs++ = 0;
	*cs++ = t->height << 16 | t->width;
	*cs++ = lower_32_bits(dst->vma->node.start);
	if (use_64b_reloc)
		*cs++ = upper_32_bits(dst->vma->node.start);
	*cs++ = 0;
	*cs++ = src_pitch;
	*cs++ = lower_32_bits(src->vma->node.start);
	if (use_64b_reloc)
		*cs++ = upper_32_bits(src->vma->node.start);

	*cs++ = MI_BATCH_BUFFER_END;

	i915_gem_object_flush_map(batch);
	i915_gem_object_unpin_map(batch);

	return 0;
}

static void tiled_blits_destroy_buffers(struct tiled_blits *t)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(t->buffers); i++)
		i915_vma_put(t->buffers[i].vma);

	i915_vma_put(t->scratch.vma);
	i915_vma_put(t->batch);
}

static struct i915_vma *
__create_vma(struct tiled_blits *t, size_t size, bool lmem)
{
	struct drm_i915_private *i915 = t->ce->vm->i915;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;

	if (lmem)
		obj = i915_gem_object_create_lmem(i915, size, 0);
	else
		obj = i915_gem_object_create_shmem(i915, size);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	vma = i915_vma_instance(obj, t->ce->vm, NULL);
	if (IS_ERR(vma))
		i915_gem_object_put(obj);

	return vma;
}

static struct i915_vma *create_vma(struct tiled_blits *t, bool lmem)
{
	return __create_vma(t, PAGE_ALIGN(t->width * t->height * 4), lmem);
}

static int tiled_blits_create_buffers(struct tiled_blits *t,
				      int width, int height,
				      struct rnd_state *prng)
{
	struct drm_i915_private *i915 = t->ce->engine->i915;
	int i;

	t->width = width;
	t->height = height;

	t->batch = __create_vma(t, PAGE_SIZE, false);
	if (IS_ERR(t->batch))
		return PTR_ERR(t->batch);

	t->scratch.vma = create_vma(t, false);
	if (IS_ERR(t->scratch.vma)) {
		i915_vma_put(t->batch);
		return PTR_ERR(t->scratch.vma);
	}

	for (i = 0; i < ARRAY_SIZE(t->buffers); i++) {
		struct i915_vma *vma;

		vma = create_vma(t, HAS_LMEM(i915) && i % 2);
		if (IS_ERR(vma)) {
			tiled_blits_destroy_buffers(t);
			return PTR_ERR(vma);
		}

		t->buffers[i].vma = vma;
		t->buffers[i].tiling =
			i915_prandom_u32_max_state(CLIENT_TILING_Y + 1, prng);
	}

	return 0;
}

static void fill_scratch(struct tiled_blits *t, u32 *vaddr, u32 val)
{
	int i;

	t->scratch.start_val = val;
	for (i = 0; i < t->width * t->height; i++)
		vaddr[i] = val++;

	i915_gem_object_flush_map(t->scratch.vma->obj);
}

static u64 swizzle_bit(unsigned int bit, u64 offset)
{
	return (offset & BIT_ULL(bit)) >> (bit - 6);
}

static u64 tiled_offset(const struct intel_gt *gt,
			u64 v,
			unsigned int stride,
			enum client_tiling tiling)
{
	unsigned int swizzle;
	u64 x, y;

	if (tiling == CLIENT_TILING_LINEAR)
		return v;

	y = div64_u64_rem(v, stride, &x);

	if (tiling == CLIENT_TILING_X) {
		v = div64_u64_rem(y, 8, &y) * stride * 8;
		v += y * 512;
		v += div64_u64_rem(x, 512, &x) << 12;
		v += x;

		swizzle = gt->ggtt->bit_6_swizzle_x;
	} else {
		const unsigned int ytile_span = 16;
		const unsigned int ytile_height = 512;

		v = div64_u64_rem(y, 32, &y) * stride * 32;
		v += y * ytile_span;
		v += div64_u64_rem(x, ytile_span, &x) * ytile_height;
		v += x;

		swizzle = gt->ggtt->bit_6_swizzle_y;
	}

	switch (swizzle) {
	case I915_BIT_6_SWIZZLE_9:
		v ^= swizzle_bit(9, v);
		break;
	case I915_BIT_6_SWIZZLE_9_10:
		v ^= swizzle_bit(9, v) ^ swizzle_bit(10, v);
		break;
	case I915_BIT_6_SWIZZLE_9_11:
		v ^= swizzle_bit(9, v) ^ swizzle_bit(11, v);
		break;
	case I915_BIT_6_SWIZZLE_9_10_11:
		v ^= swizzle_bit(9, v) ^ swizzle_bit(10, v) ^ swizzle_bit(11, v);
		break;
	}

	return v;
}

static const char *repr_tiling(enum client_tiling tiling)
{
	switch (tiling) {
	case CLIENT_TILING_LINEAR: return "linear";
	case CLIENT_TILING_X: return "X";
	case CLIENT_TILING_Y: return "Y";
	default: return "unknown";
	}
}

static int verify_buffer(const struct tiled_blits *t,
			 struct blit_buffer *buf,
			 struct rnd_state *prng)
{
	const u32 *vaddr;
	int ret = 0;
	int x, y, p;

	x = i915_prandom_u32_max_state(t->width, prng);
	y = i915_prandom_u32_max_state(t->height, prng);
	p = y * t->width + x;

	vaddr = i915_gem_object_pin_map_unlocked(buf->vma->obj, I915_MAP_WC);
	if (IS_ERR(vaddr))
		return PTR_ERR(vaddr);

	if (vaddr[0] != buf->start_val) {
		ret = -EINVAL;
	} else {
		u64 v = tiled_offset(buf->vma->vm->gt,
				     p * 4, t->width * 4,
				     buf->tiling);

		if (vaddr[v / sizeof(*vaddr)] != buf->start_val + p)
			ret = -EINVAL;
	}
	if (ret) {
		pr_err("Invalid %s tiling detected at (%d, %d), start_val %x\n",
		       repr_tiling(buf->tiling),
		       x, y, buf->start_val);
		igt_hexdump(vaddr, 4096);
	}

	i915_gem_object_unpin_map(buf->vma->obj);
	return ret;
}

static int move_to_active(struct i915_vma *vma,
			  struct i915_request *rq,
			  unsigned int flags)
{
	int err;

	i915_vma_lock(vma);
	err = i915_request_await_object(rq, vma->obj, false);
	if (err == 0)
		err = i915_vma_move_to_active(vma, rq, flags);
	i915_vma_unlock(vma);

	return err;
}

static int pin_buffer(struct i915_vma *vma, u64 addr)
{
	int err;

	if (drm_mm_node_allocated(&vma->node) && vma->node.start != addr) {
		err = i915_vma_unbind_unlocked(vma);
		if (err)
			return err;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_USER | PIN_OFFSET_FIXED | addr);
	if (err)
		return err;

	return 0;
}

static int
tiled_blit(struct tiled_blits *t,
	   struct blit_buffer *dst, u64 dst_addr,
	   struct blit_buffer *src, u64 src_addr)
{
	struct i915_request *rq;
	int err;

	err = pin_buffer(src->vma, src_addr);
	if (err) {
		pr_err("Cannot pin src @ %llx\n", src_addr);
		return err;
	}

	err = pin_buffer(dst->vma, dst_addr);
	if (err) {
		pr_err("Cannot pin dst @ %llx\n", dst_addr);
		goto err_src;
	}

	err = i915_vma_pin(t->batch, 0, 0, PIN_USER | PIN_HIGH);
	if (err) {
		pr_err("cannot pin batch\n");
		goto err_dst;
	}

	err = prepare_blit(t, dst, src, t->batch->obj);
	if (err)
		goto err_bb;

	rq = intel_context_create_request(t->ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_bb;
	}

	err = move_to_active(t->batch, rq, 0);
	if (!err)
		err = move_to_active(src->vma, rq, 0);
	if (!err)
		err = move_to_active(dst->vma, rq, 0);
	if (!err)
		err = rq->engine->emit_bb_start(rq,
						t->batch->node.start,
						t->batch->node.size,
						0);
	i915_request_get(rq);
	i915_request_add(rq);
	if (i915_request_wait(rq, 0, HZ / 2) < 0)
		err = -ETIME;
	i915_request_put(rq);

	dst->start_val = src->start_val;
err_bb:
	i915_vma_unpin(t->batch);
err_dst:
	i915_vma_unpin(dst->vma);
err_src:
	i915_vma_unpin(src->vma);
	return err;
}

static struct tiled_blits *
tiled_blits_create(struct intel_engine_cs *engine, struct rnd_state *prng)
{
	struct drm_mm_node hole;
	struct tiled_blits *t;
	u64 hole_size;
	int err;

	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		return ERR_PTR(-ENOMEM);

	t->ce = intel_context_create(engine);
	if (IS_ERR(t->ce)) {
		err = PTR_ERR(t->ce);
		goto err_free;
	}

	t->align = i915_vm_min_alignment(t->ce->vm, INTEL_MEMORY_LOCAL);
	t->align = max(t->align,
		       i915_vm_min_alignment(t->ce->vm, INTEL_MEMORY_SYSTEM));

	hole_size = 2 * round_up(WIDTH * HEIGHT * 4, t->align);
	hole_size *= 2; /* room to maneuver */
	hole_size += 2 * t->align; /* padding on either side */

	mutex_lock(&t->ce->vm->mutex);
	memset(&hole, 0, sizeof(hole));
	err = drm_mm_insert_node_in_range(&t->ce->vm->mm, &hole,
					  hole_size, t->align,
					  I915_COLOR_UNEVICTABLE,
					  0, U64_MAX,
					  DRM_MM_INSERT_BEST);
	if (!err)
		drm_mm_remove_node(&hole);
	mutex_unlock(&t->ce->vm->mutex);
	if (err) {
		err = -ENODEV;
		goto err_put;
	}

	t->hole = hole.start + t->align;
	pr_info("Using hole at %llx\n", t->hole);

	err = tiled_blits_create_buffers(t, WIDTH, HEIGHT, prng);
	if (err)
		goto err_put;

	return t;

err_put:
	intel_context_put(t->ce);
err_free:
	kfree(t);
	return ERR_PTR(err);
}

static void tiled_blits_destroy(struct tiled_blits *t)
{
	tiled_blits_destroy_buffers(t);

	intel_context_put(t->ce);
	kfree(t);
}

static int tiled_blits_prepare(struct tiled_blits *t,
			       struct rnd_state *prng)
{
	u64 offset = round_up(t->width * t->height * 4, t->align);
	u32 *map;
	int err;
	int i;

	map = i915_gem_object_pin_map_unlocked(t->scratch.vma->obj, I915_MAP_WC);
	if (IS_ERR(map))
		return PTR_ERR(map);

	/* Use scratch to fill objects */
	for (i = 0; i < ARRAY_SIZE(t->buffers); i++) {
		fill_scratch(t, map, prandom_u32_state(prng));
		GEM_BUG_ON(verify_buffer(t, &t->scratch, prng));

		err = tiled_blit(t,
				 &t->buffers[i], t->hole + offset,
				 &t->scratch, t->hole);
		if (err == 0)
			err = verify_buffer(t, &t->buffers[i], prng);
		if (err) {
			pr_err("Failed to create buffer %d\n", i);
			break;
		}
	}

	i915_gem_object_unpin_map(t->scratch.vma->obj);
	return err;
}

static int tiled_blits_bounce(struct tiled_blits *t, struct rnd_state *prng)
{
	u64 offset = round_up(t->width * t->height * 4, 2 * t->align);
	int err;

	/* We want to check position invariant tiling across GTT eviction */

	err = tiled_blit(t,
			 &t->buffers[1], t->hole + offset / 2,
			 &t->buffers[0], t->hole + 2 * offset);
	if (err)
		return err;

	/* Reposition so that we overlap the old addresses, and slightly off */
	err = tiled_blit(t,
			 &t->buffers[2], t->hole + t->align,
			 &t->buffers[1], t->hole + 3 * offset / 2);
	if (err)
		return err;

	err = verify_buffer(t, &t->buffers[2], prng);
	if (err)
		return err;

	return 0;
}

static int __igt_client_tiled_blits(struct intel_engine_cs *engine,
				    struct rnd_state *prng)
{
	struct tiled_blits *t;
	int err;

	t = tiled_blits_create(engine, prng);
	if (IS_ERR(t))
		return PTR_ERR(t);

	err = tiled_blits_prepare(t, prng);
	if (err)
		goto out;

	err = tiled_blits_bounce(t, prng);
	if (err)
		goto out;

out:
	tiled_blits_destroy(t);
	return err;
}

static bool has_bit17_swizzle(int sw)
{
	return (sw == I915_BIT_6_SWIZZLE_9_10_17 ||
		sw == I915_BIT_6_SWIZZLE_9_17);
}

static bool bad_swizzling(struct drm_i915_private *i915)
{
	struct i915_ggtt *ggtt = to_gt(i915)->ggtt;

	if (i915->quirks & QUIRK_PIN_SWIZZLED_PAGES)
		return true;

	if (has_bit17_swizzle(ggtt->bit_6_swizzle_x) ||
	    has_bit17_swizzle(ggtt->bit_6_swizzle_y))
		return true;

	return false;
}

static int igt_client_tiled_blits(void *arg)
{
	struct drm_i915_private *i915 = arg;
	I915_RND_STATE(prng);
	int inst = 0;

	/* Test requires explicit BLT tiling controls */
	if (GRAPHICS_VER(i915) < 4)
		return 0;

	if (bad_swizzling(i915)) /* Requires sane (sub-page) swizzling */
		return 0;

	do {
		struct intel_engine_cs *engine;
		int err;

		engine = intel_engine_lookup_user(i915,
						  I915_ENGINE_CLASS_COPY,
						  inst++);
		if (!engine)
			return 0;

		err = __igt_client_tiled_blits(engine, &prng);
		if (err == -ENODEV)
			err = 0;
		if (err)
			return err;
	} while (1);
}

int i915_gem_client_blt_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_client_tiled_blits),
	};

	if (intel_gt_is_wedged(to_gt(i915)))
		return 0;

	return i915_live_subtests(tests, i915);
}
