// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_lrc.h"

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_engine_types.h"
#include "xe_gt.h"
#include "xe_map.h"
#include "xe_hw_fence.h"
#include "xe_vm.h"

#include "i915_reg.h"
#include "gt/intel_gpu_commands.h"
#include "gt/intel_gt_regs.h"
#include "gt/intel_lrc_reg.h"
#include "gt/intel_engine_regs.h"

#define GEN8_CTX_VALID				(1 << 0)
#define GEN8_CTX_L3LLC_COHERENT			(1 << 5)
#define GEN8_CTX_PRIVILEGE			(1 << 8)
#define GEN8_CTX_ADDRESSING_MODE_SHIFT		3
#define INTEL_LEGACY_64B_CONTEXT		3

#define GEN11_ENGINE_CLASS_SHIFT		61
#define GEN11_ENGINE_INSTANCE_SHIFT		48

static struct xe_device *
lrc_to_xe(struct xe_lrc *lrc)
{
	return gt_to_xe(lrc->fence_ctx.gt);
}

size_t xe_lrc_size(struct xe_device *xe, enum xe_engine_class class)
{
	switch (class) {
	case XE_ENGINE_CLASS_RENDER:
	case XE_ENGINE_CLASS_COMPUTE:
		/* 14 pages since graphics_ver == 11 */
		return 14 * SZ_4K;
	default:
		WARN(1, "Unknown engine class: %d", class);
		fallthrough;
	case XE_ENGINE_CLASS_COPY:
	case XE_ENGINE_CLASS_VIDEO_DECODE:
	case XE_ENGINE_CLASS_VIDEO_ENHANCE:
		return 2 * SZ_4K;
	}
}

/*
 * The per-platform tables are u8-encoded in @data. Decode @data and set the
 * addresses' offset and commands in @regs. The following encoding is used
 * for each byte. There are 2 steps: decoding commands and decoding addresses.
 *
 * Commands:
 * [7]: create NOPs - number of NOPs are set in lower bits
 * [6]: When creating MI_LOAD_REGISTER_IMM command, allow to set
 *      MI_LRI_FORCE_POSTED
 * [5:0]: Number of NOPs or registers to set values to in case of
 *        MI_LOAD_REGISTER_IMM
 *
 * Addresses: these are decoded after a MI_LOAD_REGISTER_IMM command by "count"
 * number of registers. They are set by using the REG/REG16 macros: the former
 * is used for offsets smaller than 0x200 while the latter is for values bigger
 * than that. Those macros already set all the bits documented below correctly:
 *
 * [7]: When a register offset needs more than 6 bits, use additional bytes, to
 *      follow, for the lower bits
 * [6:0]: Register offset, without considering the engine base.
 *
 * This function only tweaks the commands and register offsets. Values are not
 * filled out.
 */
static void set_offsets(u32 *regs,
			const u8 *data,
			const struct xe_hw_engine *hwe)
#define NOP(x) (BIT(7) | (x))
#define LRI(count, flags) ((flags) << 6 | (count) | \
			   BUILD_BUG_ON_ZERO(count >= BIT(6)))
#define POSTED BIT(0)
#define REG(x) (((x) >> 2) | BUILD_BUG_ON_ZERO(x >= 0x200))
#define REG16(x) \
	(((x) >> 9) | BIT(7) | BUILD_BUG_ON_ZERO(x >= 0x10000)), \
	(((x) >> 2) & 0x7f)
#define END 0
{
	const u32 base = hwe->mmio_base;

	while (*data) {
		u8 count, flags;

		if (*data & BIT(7)) { /* skip */
			count = *data++ & ~BIT(7);
			regs += count;
			continue;
		}

		count = *data & 0x3f;
		flags = *data >> 6;
		data++;

		*regs = MI_LOAD_REGISTER_IMM(count);
		if (flags & POSTED)
			*regs |= MI_LRI_FORCE_POSTED;
		*regs |= MI_LRI_LRM_CS_MMIO;
		regs++;

		XE_BUG_ON(!count);
		do {
			u32 offset = 0;
			u8 v;

			do {
				v = *data++;
				offset <<= 7;
				offset |= v & ~BIT(7);
			} while (v & BIT(7));

			regs[0] = base + (offset << 2);
			regs += 2;
		} while (--count);
	}

	*regs = MI_BATCH_BUFFER_END | BIT(0);
}

static const u8 gen12_xcs_offsets[] = {
	NOP(1),
	LRI(13, POSTED),
	REG16(0x244),
	REG(0x034),
	REG(0x030),
	REG(0x038),
	REG(0x03c),
	REG(0x168),
	REG(0x140),
	REG(0x110),
	REG(0x1c0),
	REG(0x1c4),
	REG(0x1c8),
	REG(0x180),
	REG16(0x2b4),

	NOP(5),
	LRI(9, POSTED),
	REG16(0x3a8),
	REG16(0x28c),
	REG16(0x288),
	REG16(0x284),
	REG16(0x280),
	REG16(0x27c),
	REG16(0x278),
	REG16(0x274),
	REG16(0x270),

	END
};

static const u8 dg2_xcs_offsets[] = {
	NOP(1),
	LRI(15, POSTED),
	REG16(0x244),
	REG(0x034),
	REG(0x030),
	REG(0x038),
	REG(0x03c),
	REG(0x168),
	REG(0x140),
	REG(0x110),
	REG(0x1c0),
	REG(0x1c4),
	REG(0x1c8),
	REG(0x180),
	REG16(0x2b4),
	REG(0x120),
	REG(0x124),

	NOP(1),
	LRI(9, POSTED),
	REG16(0x3a8),
	REG16(0x28c),
	REG16(0x288),
	REG16(0x284),
	REG16(0x280),
	REG16(0x27c),
	REG16(0x278),
	REG16(0x274),
	REG16(0x270),

	END
};

static const u8 gen12_rcs_offsets[] = {
	NOP(1),
	LRI(13, POSTED),
	REG16(0x244),
	REG(0x034),
	REG(0x030),
	REG(0x038),
	REG(0x03c),
	REG(0x168),
	REG(0x140),
	REG(0x110),
	REG(0x1c0),
	REG(0x1c4),
	REG(0x1c8),
	REG(0x180),
	REG16(0x2b4),

	NOP(5),
	LRI(9, POSTED),
	REG16(0x3a8),
	REG16(0x28c),
	REG16(0x288),
	REG16(0x284),
	REG16(0x280),
	REG16(0x27c),
	REG16(0x278),
	REG16(0x274),
	REG16(0x270),

	LRI(3, POSTED),
	REG(0x1b0),
	REG16(0x5a8),
	REG16(0x5ac),

	NOP(6),
	LRI(1, 0),
	REG(0x0c8),
	NOP(3 + 9 + 1),

	LRI(51, POSTED),
	REG16(0x588),
	REG16(0x588),
	REG16(0x588),
	REG16(0x588),
	REG16(0x588),
	REG16(0x588),
	REG(0x028),
	REG(0x09c),
	REG(0x0c0),
	REG(0x178),
	REG(0x17c),
	REG16(0x358),
	REG(0x170),
	REG(0x150),
	REG(0x154),
	REG(0x158),
	REG16(0x41c),
	REG16(0x600),
	REG16(0x604),
	REG16(0x608),
	REG16(0x60c),
	REG16(0x610),
	REG16(0x614),
	REG16(0x618),
	REG16(0x61c),
	REG16(0x620),
	REG16(0x624),
	REG16(0x628),
	REG16(0x62c),
	REG16(0x630),
	REG16(0x634),
	REG16(0x638),
	REG16(0x63c),
	REG16(0x640),
	REG16(0x644),
	REG16(0x648),
	REG16(0x64c),
	REG16(0x650),
	REG16(0x654),
	REG16(0x658),
	REG16(0x65c),
	REG16(0x660),
	REG16(0x664),
	REG16(0x668),
	REG16(0x66c),
	REG16(0x670),
	REG16(0x674),
	REG16(0x678),
	REG16(0x67c),
	REG(0x068),
	REG(0x084),
	NOP(1),

	END
};

static const u8 xehp_rcs_offsets[] = {
	NOP(1),
	LRI(13, POSTED),
	REG16(0x244),
	REG(0x034),
	REG(0x030),
	REG(0x038),
	REG(0x03c),
	REG(0x168),
	REG(0x140),
	REG(0x110),
	REG(0x1c0),
	REG(0x1c4),
	REG(0x1c8),
	REG(0x180),
	REG16(0x2b4),

	NOP(5),
	LRI(9, POSTED),
	REG16(0x3a8),
	REG16(0x28c),
	REG16(0x288),
	REG16(0x284),
	REG16(0x280),
	REG16(0x27c),
	REG16(0x278),
	REG16(0x274),
	REG16(0x270),

	LRI(3, POSTED),
	REG(0x1b0),
	REG16(0x5a8),
	REG16(0x5ac),

	NOP(6),
	LRI(1, 0),
	REG(0x0c8),

	END
};

static const u8 dg2_rcs_offsets[] = {
	NOP(1),
	LRI(15, POSTED),
	REG16(0x244),
	REG(0x034),
	REG(0x030),
	REG(0x038),
	REG(0x03c),
	REG(0x168),
	REG(0x140),
	REG(0x110),
	REG(0x1c0),
	REG(0x1c4),
	REG(0x1c8),
	REG(0x180),
	REG16(0x2b4),
	REG(0x120),
	REG(0x124),

	NOP(1),
	LRI(9, POSTED),
	REG16(0x3a8),
	REG16(0x28c),
	REG16(0x288),
	REG16(0x284),
	REG16(0x280),
	REG16(0x27c),
	REG16(0x278),
	REG16(0x274),
	REG16(0x270),

	LRI(3, POSTED),
	REG(0x1b0),
	REG16(0x5a8),
	REG16(0x5ac),

	NOP(6),
	LRI(1, 0),
	REG(0x0c8),

	END
};

static const u8 mtl_rcs_offsets[] = {
       NOP(1),
       LRI(15, POSTED),
       REG16(0x244),
       REG(0x034),
       REG(0x030),
       REG(0x038),
       REG(0x03c),
       REG(0x168),
       REG(0x140),
       REG(0x110),
       REG(0x1c0),
       REG(0x1c4),
       REG(0x1c8),
       REG(0x180),
       REG16(0x2b4),
       REG(0x120),
       REG(0x124),

       NOP(1),
       LRI(9, POSTED),
       REG16(0x3a8),
       REG16(0x28c),
       REG16(0x288),
       REG16(0x284),
       REG16(0x280),
       REG16(0x27c),
       REG16(0x278),
       REG16(0x274),
       REG16(0x270),

       NOP(2),
       LRI(2, POSTED),
       REG16(0x5a8),
       REG16(0x5ac),

       NOP(6),
       LRI(1, 0),
       REG(0x0c8),

       END
};

#undef END
#undef REG16
#undef REG
#undef LRI
#undef NOP

static const u8 *reg_offsets(struct xe_device *xe, enum xe_engine_class class)
{
	if (class == XE_ENGINE_CLASS_RENDER) {
		if (GRAPHICS_VERx100(xe) >= 1270)
			return mtl_rcs_offsets;
		else if (GRAPHICS_VERx100(xe) >= 1255)
			return dg2_rcs_offsets;
		else if (GRAPHICS_VERx100(xe) >= 1250)
			return xehp_rcs_offsets;
		else
			return gen12_rcs_offsets;
	} else {
		if (GRAPHICS_VERx100(xe) >= 1255)
			return dg2_xcs_offsets;
		else
			return gen12_xcs_offsets;
	}
}

static void set_context_control(u32 * regs, struct xe_hw_engine *hwe)
{
	regs[CTX_CONTEXT_CONTROL] = _MASKED_BIT_ENABLE(CTX_CTRL_INHIBIT_SYN_CTX_SWITCH) |
				    _MASKED_BIT_DISABLE(CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT) |
				    CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT;

	/* TODO: Timestamp */
}

static int lrc_ring_mi_mode(struct xe_hw_engine *hwe)
{
	struct xe_device *xe = gt_to_xe(hwe->gt);

	if (GRAPHICS_VERx100(xe) >= 1250)
		return 0x70;
	else
		return 0x60;
}

static void reset_stop_ring(u32 *regs, struct xe_hw_engine *hwe)
{
	int x;

	x = lrc_ring_mi_mode(hwe);
	regs[x + 1] &= ~STOP_RING;
	regs[x + 1] |= STOP_RING << 16;
}

static inline u32 __xe_lrc_ring_offset(struct xe_lrc *lrc)
{
	return 0;
}

u32 xe_lrc_pphwsp_offset(struct xe_lrc *lrc)
{
	return lrc->ring.size;
}

/* Make the magic macros work */
#define __xe_lrc_pphwsp_offset xe_lrc_pphwsp_offset

#define LRC_SEQNO_PPHWSP_OFFSET 512
#define LRC_START_SEQNO_PPHWSP_OFFSET LRC_SEQNO_PPHWSP_OFFSET + 8
#define LRC_PARALLEL_PPHWSP_OFFSET 2048
#define LRC_PPHWSP_SIZE SZ_4K

static size_t lrc_reg_size(struct xe_device *xe)
{
	if (GRAPHICS_VERx100(xe) >= 1250)
		return 96 * sizeof(u32);
	else
		return 80 * sizeof(u32);
}

size_t xe_lrc_skip_size(struct xe_device *xe)
{
	return LRC_PPHWSP_SIZE + lrc_reg_size(xe);
}

static inline u32 __xe_lrc_seqno_offset(struct xe_lrc *lrc)
{
	/* The seqno is stored in the driver-defined portion of PPHWSP */
	return xe_lrc_pphwsp_offset(lrc) + LRC_SEQNO_PPHWSP_OFFSET;
}

static inline u32 __xe_lrc_start_seqno_offset(struct xe_lrc *lrc)
{
	/* The start seqno is stored in the driver-defined portion of PPHWSP */
	return xe_lrc_pphwsp_offset(lrc) + LRC_START_SEQNO_PPHWSP_OFFSET;
}

static inline u32 __xe_lrc_parallel_offset(struct xe_lrc *lrc)
{
	/* The parallel is stored in the driver-defined portion of PPHWSP */
	return xe_lrc_pphwsp_offset(lrc) + LRC_PARALLEL_PPHWSP_OFFSET;
}

static inline u32 __xe_lrc_regs_offset(struct xe_lrc *lrc)
{
	return xe_lrc_pphwsp_offset(lrc) + LRC_PPHWSP_SIZE;
}

#define DECL_MAP_ADDR_HELPERS(elem) \
static inline struct iosys_map __xe_lrc_##elem##_map(struct xe_lrc *lrc) \
{ \
	struct iosys_map map = lrc->bo->vmap; \
\
	XE_BUG_ON(iosys_map_is_null(&map)); \
	iosys_map_incr(&map, __xe_lrc_##elem##_offset(lrc)); \
	return map; \
} \
static inline u32 __xe_lrc_##elem##_ggtt_addr(struct xe_lrc *lrc) \
{ \
	return xe_bo_ggtt_addr(lrc->bo) + __xe_lrc_##elem##_offset(lrc); \
} \

DECL_MAP_ADDR_HELPERS(ring)
DECL_MAP_ADDR_HELPERS(pphwsp)
DECL_MAP_ADDR_HELPERS(seqno)
DECL_MAP_ADDR_HELPERS(regs)
DECL_MAP_ADDR_HELPERS(start_seqno)
DECL_MAP_ADDR_HELPERS(parallel)

#undef DECL_MAP_ADDR_HELPERS

u32 xe_lrc_ggtt_addr(struct xe_lrc *lrc)
{
	return __xe_lrc_pphwsp_ggtt_addr(lrc);
}

u32 xe_lrc_read_ctx_reg(struct xe_lrc *lrc, int reg_nr)
{
	struct xe_device *xe = lrc_to_xe(lrc);
	struct iosys_map map;

	map = __xe_lrc_regs_map(lrc);
	iosys_map_incr(&map, reg_nr * sizeof(u32));
	return xe_map_read32(xe, &map);
}

void xe_lrc_write_ctx_reg(struct xe_lrc *lrc, int reg_nr, u32 val)
{
	struct xe_device *xe = lrc_to_xe(lrc);
	struct iosys_map map;

	map = __xe_lrc_regs_map(lrc);
	iosys_map_incr(&map, reg_nr * sizeof(u32));
	xe_map_write32(xe, &map, val);
}

static void *empty_lrc_data(struct xe_hw_engine *hwe)
{
	struct xe_device *xe = gt_to_xe(hwe->gt);
	void *data;
	u32 *regs;

	data = kzalloc(xe_lrc_size(xe, hwe->class), GFP_KERNEL);
	if (!data)
		return NULL;

	/* 1st page: Per-Process of HW status Page */
	regs = data + LRC_PPHWSP_SIZE;
	set_offsets(regs, reg_offsets(xe, hwe->class), hwe);
	set_context_control(regs, hwe);
	reset_stop_ring(regs, hwe);

	return data;
}

static void xe_lrc_set_ppgtt(struct xe_lrc *lrc, struct xe_vm *vm)
{
	u64 desc = xe_vm_pdp4_descriptor(vm, lrc->full_gt);

	xe_lrc_write_ctx_reg(lrc, CTX_PDP0_UDW, upper_32_bits(desc));
	xe_lrc_write_ctx_reg(lrc, CTX_PDP0_LDW, lower_32_bits(desc));
}

#define PVC_CTX_ASID		(0x2e + 1)
#define PVC_CTX_ACC_CTR_THOLD	(0x2a + 1)
#define ACC_GRANULARITY_S       20
#define ACC_NOTIFY_S            16

int xe_lrc_init(struct xe_lrc *lrc, struct xe_hw_engine *hwe,
		struct xe_engine *e, struct xe_vm *vm, u32 ring_size)
{
	struct xe_gt *gt = hwe->gt;
	struct xe_device *xe = gt_to_xe(gt);
	struct iosys_map map;
	void *init_data = NULL;
	u32 arb_enable;
	int err;

	lrc->flags = 0;

	lrc->bo = xe_bo_create_locked(xe, hwe->gt, vm,
				      ring_size + xe_lrc_size(xe, hwe->class),
				      ttm_bo_type_kernel,
				      XE_BO_CREATE_VRAM_IF_DGFX(hwe->gt) |
				      XE_BO_CREATE_GGTT_BIT);
	if (IS_ERR(lrc->bo))
		return PTR_ERR(lrc->bo);

	if (xe_gt_is_media_type(hwe->gt))
		lrc->full_gt = xe_find_full_gt(hwe->gt);
	else
		lrc->full_gt = hwe->gt;

	/*
	 * FIXME: Perma-pinning LRC as we don't yet support moving GGTT address
	 * via VM bind calls.
	 */
	err = xe_bo_pin(lrc->bo);
	if (err)
		goto err_unlock_put_bo;
	lrc->flags |= XE_LRC_PINNED;

	err = xe_bo_vmap(lrc->bo);
	if (err)
		goto err_unpin_bo;

	xe_bo_unlock_vm_held(lrc->bo);

	lrc->ring.size = ring_size;
	lrc->ring.tail = 0;

	xe_hw_fence_ctx_init(&lrc->fence_ctx, hwe->gt,
			     hwe->fence_irq, hwe->name);

	if (!gt->default_lrc[hwe->class]) {
		init_data = empty_lrc_data(hwe);
		if (!init_data) {
			xe_lrc_finish(lrc);
			return -ENOMEM;
		}
	}

	/*
	 * Init Per-Process of HW status Page, LRC / context state to known
	 * values
	 */
	map = __xe_lrc_pphwsp_map(lrc);
	if (!init_data) {
		xe_map_memset(xe, &map, 0, 0, LRC_PPHWSP_SIZE);	/* PPHWSP */
		xe_map_memcpy_to(xe, &map, LRC_PPHWSP_SIZE,
				 gt->default_lrc[hwe->class] + LRC_PPHWSP_SIZE,
				 xe_lrc_size(xe, hwe->class) - LRC_PPHWSP_SIZE);
	} else {
		xe_map_memcpy_to(xe, &map, 0, init_data,
				 xe_lrc_size(xe, hwe->class));
		kfree(init_data);
	}

	if (vm)
		xe_lrc_set_ppgtt(lrc, vm);

	xe_lrc_write_ctx_reg(lrc, CTX_RING_START, __xe_lrc_ring_ggtt_addr(lrc));
	xe_lrc_write_ctx_reg(lrc, CTX_RING_HEAD, 0);
	xe_lrc_write_ctx_reg(lrc, CTX_RING_TAIL, lrc->ring.tail);
	xe_lrc_write_ctx_reg(lrc, CTX_RING_CTL,
			     RING_CTL_SIZE(lrc->ring.size) | RING_VALID);
	if (xe->info.supports_usm && vm) {
		xe_lrc_write_ctx_reg(lrc, PVC_CTX_ASID,
				     (e->usm.acc_granularity <<
				      ACC_GRANULARITY_S) | vm->usm.asid);
		xe_lrc_write_ctx_reg(lrc, PVC_CTX_ACC_CTR_THOLD,
				     (e->usm.acc_notify << ACC_NOTIFY_S) |
				     e->usm.acc_trigger);
	}

	lrc->desc = GEN8_CTX_VALID;
	lrc->desc |= INTEL_LEGACY_64B_CONTEXT << GEN8_CTX_ADDRESSING_MODE_SHIFT;
	/* TODO: Priority */

	/* While this appears to have something about privileged batches or
	 * some such, it really just means PPGTT mode.
	 */
	if (vm)
		lrc->desc |= GEN8_CTX_PRIVILEGE;

	if (GRAPHICS_VERx100(xe) < 1250) {
		lrc->desc |= (u64)hwe->instance << GEN11_ENGINE_INSTANCE_SHIFT;
		lrc->desc |= (u64)hwe->class << GEN11_ENGINE_CLASS_SHIFT;
	}

	arb_enable = MI_ARB_ON_OFF | MI_ARB_ENABLE;
	xe_lrc_write_ring(lrc, &arb_enable, sizeof(arb_enable));

	return 0;

err_unpin_bo:
	if (lrc->flags & XE_LRC_PINNED)
		xe_bo_unpin(lrc->bo);
err_unlock_put_bo:
	xe_bo_unlock_vm_held(lrc->bo);
	xe_bo_put(lrc->bo);
	return err;
}

void xe_lrc_finish(struct xe_lrc *lrc)
{
	struct ww_acquire_ctx ww;

	xe_hw_fence_ctx_finish(&lrc->fence_ctx);
	if (lrc->flags & XE_LRC_PINNED) {
		if (lrc->bo->vm)
			xe_vm_lock(lrc->bo->vm, &ww, 0, false);
		else
			xe_bo_lock_no_vm(lrc->bo, NULL);
		xe_bo_unpin(lrc->bo);
		if (lrc->bo->vm)
			xe_vm_unlock(lrc->bo->vm, &ww);
		else
			xe_bo_unlock_no_vm(lrc->bo);
	}
	xe_bo_put(lrc->bo);
}

void xe_lrc_set_ring_head(struct xe_lrc *lrc, u32 head)
{
	xe_lrc_write_ctx_reg(lrc, CTX_RING_HEAD, head);
}

u32 xe_lrc_ring_head(struct xe_lrc *lrc)
{
	return xe_lrc_read_ctx_reg(lrc, CTX_RING_HEAD) & HEAD_ADDR;
}

u32 xe_lrc_ring_space(struct xe_lrc *lrc)
{
	const u32 head = xe_lrc_ring_head(lrc);
	const u32 tail = lrc->ring.tail;
	const u32 size = lrc->ring.size;

	return ((head - tail - 1) & (size - 1)) + 1;
}

static void __xe_lrc_write_ring(struct xe_lrc *lrc, struct iosys_map ring,
				const void *data, size_t size)
{
	struct xe_device *xe = lrc_to_xe(lrc);

	iosys_map_incr(&ring, lrc->ring.tail);
	xe_map_memcpy_to(xe, &ring, 0, data, size);
	lrc->ring.tail = (lrc->ring.tail + size) & (lrc->ring.size - 1);
}

void xe_lrc_write_ring(struct xe_lrc *lrc, const void *data, size_t size)
{
	struct iosys_map ring;
	u32 rhs;
	size_t aligned_size;

	XE_BUG_ON(!IS_ALIGNED(size, 4));
	aligned_size = ALIGN(size, 8);

	ring = __xe_lrc_ring_map(lrc);

	XE_BUG_ON(lrc->ring.tail >= lrc->ring.size);
	rhs = lrc->ring.size - lrc->ring.tail;
	if (size > rhs) {
		__xe_lrc_write_ring(lrc, ring, data, rhs);
		__xe_lrc_write_ring(lrc, ring, data + rhs, size - rhs);
	} else {
		__xe_lrc_write_ring(lrc, ring, data, size);
	}

	if (aligned_size > size) {
		u32 noop = MI_NOOP;

		__xe_lrc_write_ring(lrc, ring, &noop, sizeof(noop));
	}
}

u64 xe_lrc_descriptor(struct xe_lrc *lrc)
{
	return lrc->desc | xe_lrc_ggtt_addr(lrc);
}

u32 xe_lrc_seqno_ggtt_addr(struct xe_lrc *lrc)
{
	return __xe_lrc_seqno_ggtt_addr(lrc);
}

struct dma_fence *xe_lrc_create_seqno_fence(struct xe_lrc *lrc)
{
	return &xe_hw_fence_create(&lrc->fence_ctx,
				   __xe_lrc_seqno_map(lrc))->dma;
}

s32 xe_lrc_seqno(struct xe_lrc *lrc)
{
	struct iosys_map map = __xe_lrc_seqno_map(lrc);

	return xe_map_read32(lrc_to_xe(lrc), &map);
}

s32 xe_lrc_start_seqno(struct xe_lrc *lrc)
{
	struct iosys_map map = __xe_lrc_start_seqno_map(lrc);

	return xe_map_read32(lrc_to_xe(lrc), &map);
}

u32 xe_lrc_start_seqno_ggtt_addr(struct xe_lrc *lrc)
{
	return __xe_lrc_start_seqno_ggtt_addr(lrc);
}

u32 xe_lrc_parallel_ggtt_addr(struct xe_lrc *lrc)
{
	return __xe_lrc_parallel_ggtt_addr(lrc);
}

struct iosys_map xe_lrc_parallel_map(struct xe_lrc *lrc)
{
	return __xe_lrc_parallel_map(lrc);
}
