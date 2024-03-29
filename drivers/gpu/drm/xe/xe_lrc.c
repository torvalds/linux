// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_lrc.h"

#include "instructions/xe_mi_commands.h"
#include "instructions/xe_gfxpipe_commands.h"
#include "regs/xe_engine_regs.h"
#include "regs/xe_gpu_commands.h"
#include "regs/xe_lrc_layout.h"
#include "xe_bb.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_drm_client.h"
#include "xe_exec_queue_types.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_hw_fence.h"
#include "xe_map.h"
#include "xe_memirq.h"
#include "xe_sriov.h"
#include "xe_vm.h"

#define LRC_VALID				(1 << 0)
#define LRC_PRIVILEGE				(1 << 8)
#define LRC_ADDRESSING_MODE_SHIFT		3
#define LRC_LEGACY_64B_CONTEXT			3

#define ENGINE_CLASS_SHIFT			61
#define ENGINE_INSTANCE_SHIFT			48

static struct xe_device *
lrc_to_xe(struct xe_lrc *lrc)
{
	return gt_to_xe(lrc->fence_ctx.gt);
}

size_t xe_lrc_size(struct xe_device *xe, enum xe_engine_class class)
{
	switch (class) {
	case XE_ENGINE_CLASS_RENDER:
		if (GRAPHICS_VER(xe) >= 20)
			return 4 * SZ_4K;
		else
			return 14 * SZ_4K;
	case XE_ENGINE_CLASS_COMPUTE:
		/* 14 pages since graphics_ver == 11 */
		if (GRAPHICS_VER(xe) >= 20)
			return 3 * SZ_4K;
		else
			return 14 * SZ_4K;
	default:
		WARN(1, "Unknown engine class: %d", class);
		fallthrough;
	case XE_ENGINE_CLASS_COPY:
	case XE_ENGINE_CLASS_VIDEO_DECODE:
	case XE_ENGINE_CLASS_VIDEO_ENHANCE:
	case XE_ENGINE_CLASS_OTHER:
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

		*regs = MI_LOAD_REGISTER_IMM | MI_LRI_NUM_REGS(count);
		if (flags & POSTED)
			*regs |= MI_LRI_FORCE_POSTED;
		*regs |= MI_LRI_LRM_CS_MMIO;
		regs++;

		xe_gt_assert(hwe->gt, count);
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

	0
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

	0
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

	0
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

	0
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

	0
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

	0
};

#define XE2_CTX_COMMON \
	NOP(1),                 /* [0x00] */ \
	LRI(15, POSTED),        /* [0x01] */ \
	REG16(0x244),           /* [0x02] CTXT_SR_CTL */ \
	REG(0x034),             /* [0x04] RING_BUFFER_HEAD */ \
	REG(0x030),             /* [0x06] RING_BUFFER_TAIL */ \
	REG(0x038),             /* [0x08] RING_BUFFER_START */ \
	REG(0x03c),             /* [0x0a] RING_BUFFER_CONTROL */ \
	REG(0x168),             /* [0x0c] BB_ADDR_UDW */ \
	REG(0x140),             /* [0x0e] BB_ADDR */ \
	REG(0x110),             /* [0x10] BB_STATE */ \
	REG(0x1c0),             /* [0x12] BB_PER_CTX_PTR */ \
	REG(0x1c4),             /* [0x14] RCS_INDIRECT_CTX */ \
	REG(0x1c8),             /* [0x16] RCS_INDIRECT_CTX_OFFSET */ \
	REG(0x180),             /* [0x18] CCID */ \
	REG16(0x2b4),           /* [0x1a] SEMAPHORE_TOKEN */ \
	REG(0x120),             /* [0x1c] PRT_BB_STATE */ \
	REG(0x124),             /* [0x1e] PRT_BB_STATE_UDW */ \
	\
	NOP(1),                 /* [0x20] */ \
	LRI(9, POSTED),         /* [0x21] */ \
	REG16(0x3a8),           /* [0x22] CTX_TIMESTAMP */ \
	REG16(0x3ac),           /* [0x24] CTX_TIMESTAMP_UDW */ \
	REG(0x108),             /* [0x26] INDIRECT_RING_STATE */ \
	REG16(0x284),           /* [0x28] dummy reg */ \
	REG16(0x280),           /* [0x2a] CS_ACC_CTR_THOLD */ \
	REG16(0x27c),           /* [0x2c] CS_CTX_SYS_PASID */ \
	REG16(0x278),           /* [0x2e] CS_CTX_ASID */ \
	REG16(0x274),           /* [0x30] PTBP_UDW */ \
	REG16(0x270)            /* [0x32] PTBP_LDW */

static const u8 xe2_rcs_offsets[] = {
	XE2_CTX_COMMON,

	NOP(2),                 /* [0x34] */
	LRI(2, POSTED),         /* [0x36] */
	REG16(0x5a8),           /* [0x37] CONTEXT_SCHEDULING_ATTRIBUTES */
	REG16(0x5ac),           /* [0x39] PREEMPTION_STATUS */

	NOP(6),                 /* [0x41] */
	LRI(1, 0),              /* [0x47] */
	REG(0x0c8),             /* [0x48] R_PWR_CLK_STATE */

	0
};

static const u8 xe2_bcs_offsets[] = {
	XE2_CTX_COMMON,

	NOP(4 + 8 + 1),         /* [0x34] */
	LRI(2, POSTED),         /* [0x41] */
	REG16(0x200),           /* [0x42] BCS_SWCTRL */
	REG16(0x204),           /* [0x44] BLIT_CCTL */

	0
};

static const u8 xe2_xcs_offsets[] = {
	XE2_CTX_COMMON,

	0
};

#undef REG16
#undef REG
#undef LRI
#undef NOP

static const u8 *reg_offsets(struct xe_device *xe, enum xe_engine_class class)
{
	if (class == XE_ENGINE_CLASS_RENDER) {
		if (GRAPHICS_VER(xe) >= 20)
			return xe2_rcs_offsets;
		else if (GRAPHICS_VERx100(xe) >= 1270)
			return mtl_rcs_offsets;
		else if (GRAPHICS_VERx100(xe) >= 1255)
			return dg2_rcs_offsets;
		else if (GRAPHICS_VERx100(xe) >= 1250)
			return xehp_rcs_offsets;
		else
			return gen12_rcs_offsets;
	} else if (class == XE_ENGINE_CLASS_COPY) {
		if (GRAPHICS_VER(xe) >= 20)
			return xe2_bcs_offsets;
		else
			return gen12_xcs_offsets;
	} else {
		if (GRAPHICS_VER(xe) >= 20)
			return xe2_xcs_offsets;
		else if (GRAPHICS_VERx100(xe) >= 1255)
			return dg2_xcs_offsets;
		else
			return gen12_xcs_offsets;
	}
}

static void set_context_control(u32 *regs, struct xe_hw_engine *hwe)
{
	regs[CTX_CONTEXT_CONTROL] = _MASKED_BIT_ENABLE(CTX_CTRL_INHIBIT_SYN_CTX_SWITCH) |
				    _MASKED_BIT_DISABLE(CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT) |
				    CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT;

	/* TODO: Timestamp */
}

static void set_memory_based_intr(u32 *regs, struct xe_hw_engine *hwe)
{
	struct xe_memirq *memirq = &gt_to_tile(hwe->gt)->sriov.vf.memirq;
	struct xe_device *xe = gt_to_xe(hwe->gt);

	if (!IS_SRIOV_VF(xe) || !xe_device_has_memirq(xe))
		return;

	regs[CTX_LRM_INT_MASK_ENABLE] = MI_LOAD_REGISTER_MEM |
					MI_LRI_LRM_CS_MMIO | MI_LRM_USE_GGTT;
	regs[CTX_INT_MASK_ENABLE_REG] = RING_IMR(0).addr;
	regs[CTX_INT_MASK_ENABLE_PTR] = xe_memirq_enable_ptr(memirq);

	regs[CTX_LRI_INT_REPORT_PTR] = MI_LOAD_REGISTER_IMM | MI_LRI_NUM_REGS(2) |
				       MI_LRI_LRM_CS_MMIO | MI_LRI_FORCE_POSTED;
	regs[CTX_INT_STATUS_REPORT_REG] = RING_INT_STATUS_RPT_PTR(0).addr;
	regs[CTX_INT_STATUS_REPORT_PTR] = xe_memirq_status_ptr(memirq);
	regs[CTX_INT_SRC_REPORT_REG] = RING_INT_SRC_RPT_PTR(0).addr;
	regs[CTX_INT_SRC_REPORT_PTR] = xe_memirq_source_ptr(memirq);
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
#define LRC_START_SEQNO_PPHWSP_OFFSET (LRC_SEQNO_PPHWSP_OFFSET + 8)
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
	xe_assert(lrc_to_xe(lrc), !iosys_map_is_null(&map));  \
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
	set_memory_based_intr(regs, hwe);
	reset_stop_ring(regs, hwe);

	return data;
}

static void xe_lrc_set_ppgtt(struct xe_lrc *lrc, struct xe_vm *vm)
{
	u64 desc = xe_vm_pdp4_descriptor(vm, lrc->tile);

	xe_lrc_write_ctx_reg(lrc, CTX_PDP0_UDW, upper_32_bits(desc));
	xe_lrc_write_ctx_reg(lrc, CTX_PDP0_LDW, lower_32_bits(desc));
}

#define PVC_CTX_ASID		(0x2e + 1)
#define PVC_CTX_ACC_CTR_THOLD	(0x2a + 1)

int xe_lrc_init(struct xe_lrc *lrc, struct xe_hw_engine *hwe,
		struct xe_exec_queue *q, struct xe_vm *vm, u32 ring_size)
{
	struct xe_gt *gt = hwe->gt;
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_device *xe = gt_to_xe(gt);
	struct iosys_map map;
	void *init_data = NULL;
	u32 arb_enable;
	int err;

	lrc->flags = 0;

	/*
	 * FIXME: Perma-pinning LRC as we don't yet support moving GGTT address
	 * via VM bind calls.
	 */
	lrc->bo = xe_bo_create_pin_map(xe, tile, vm,
				      ring_size + xe_lrc_size(xe, hwe->class),
				      ttm_bo_type_kernel,
				      XE_BO_CREATE_VRAM_IF_DGFX(tile) |
				      XE_BO_CREATE_GGTT_BIT);
	if (IS_ERR(lrc->bo))
		return PTR_ERR(lrc->bo);

	lrc->tile = gt_to_tile(hwe->gt);
	lrc->ring.size = ring_size;
	lrc->ring.tail = 0;

	xe_hw_fence_ctx_init(&lrc->fence_ctx, hwe->gt,
			     hwe->fence_irq, hwe->name);

	if (!gt->default_lrc[hwe->class]) {
		init_data = empty_lrc_data(hwe);
		if (!init_data) {
			err = -ENOMEM;
			goto err_lrc_finish;
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

	if (vm) {
		xe_lrc_set_ppgtt(lrc, vm);

		if (vm->xef)
			xe_drm_client_add_bo(vm->xef->client, lrc->bo);
	}

	xe_lrc_write_ctx_reg(lrc, CTX_RING_START, __xe_lrc_ring_ggtt_addr(lrc));
	xe_lrc_write_ctx_reg(lrc, CTX_RING_HEAD, 0);
	xe_lrc_write_ctx_reg(lrc, CTX_RING_TAIL, lrc->ring.tail);
	xe_lrc_write_ctx_reg(lrc, CTX_RING_CTL,
			     RING_CTL_SIZE(lrc->ring.size) | RING_VALID);
	if (xe->info.has_asid && vm)
		xe_lrc_write_ctx_reg(lrc, PVC_CTX_ASID, vm->usm.asid);

	lrc->desc = LRC_VALID;
	lrc->desc |= LRC_LEGACY_64B_CONTEXT << LRC_ADDRESSING_MODE_SHIFT;
	/* TODO: Priority */

	/* While this appears to have something about privileged batches or
	 * some such, it really just means PPGTT mode.
	 */
	if (vm)
		lrc->desc |= LRC_PRIVILEGE;

	if (GRAPHICS_VERx100(xe) < 1250) {
		lrc->desc |= (u64)hwe->instance << ENGINE_INSTANCE_SHIFT;
		lrc->desc |= (u64)hwe->class << ENGINE_CLASS_SHIFT;
	}

	arb_enable = MI_ARB_ON_OFF | MI_ARB_ENABLE;
	xe_lrc_write_ring(lrc, &arb_enable, sizeof(arb_enable));

	map = __xe_lrc_seqno_map(lrc);
	xe_map_write32(lrc_to_xe(lrc), &map, lrc->fence_ctx.next_seqno - 1);

	map = __xe_lrc_start_seqno_map(lrc);
	xe_map_write32(lrc_to_xe(lrc), &map, lrc->fence_ctx.next_seqno - 1);

	return 0;

err_lrc_finish:
	xe_lrc_finish(lrc);
	return err;
}

void xe_lrc_finish(struct xe_lrc *lrc)
{
	xe_hw_fence_ctx_finish(&lrc->fence_ctx);
	xe_bo_lock(lrc->bo, false);
	xe_bo_unpin(lrc->bo);
	xe_bo_unlock(lrc->bo);
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
	struct xe_device *xe = lrc_to_xe(lrc);
	struct iosys_map ring;
	u32 rhs;
	size_t aligned_size;

	xe_assert(xe, IS_ALIGNED(size, 4));
	aligned_size = ALIGN(size, 8);

	ring = __xe_lrc_ring_map(lrc);

	xe_assert(xe, lrc->ring.tail < lrc->ring.size);
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

static int instr_dw(u32 cmd_header)
{
	/* GFXPIPE "SINGLE_DW" opcodes are a single dword */
	if ((cmd_header & (XE_INSTR_CMD_TYPE | GFXPIPE_PIPELINE)) ==
	    GFXPIPE_SINGLE_DW_CMD(0, 0))
		return 1;

	/* 3DSTATE_SO_DECL_LIST has a 9-bit dword length rather than 8 */
	if ((cmd_header & GFXPIPE_MATCH_MASK) == CMD_3DSTATE_SO_DECL_LIST)
		return REG_FIELD_GET(CMD_3DSTATE_SO_DECL_LIST_DW_LEN, cmd_header) + 2;

	/* Most instructions have the # of dwords (minus 2) in 7:0 */
	return REG_FIELD_GET(XE_INSTR_LEN_MASK, cmd_header) + 2;
}

static int dump_mi_command(struct drm_printer *p,
			   struct xe_gt *gt,
			   u32 *dw,
			   int remaining_dw)
{
	u32 inst_header = *dw;
	u32 numdw = instr_dw(inst_header);
	u32 opcode = REG_FIELD_GET(MI_OPCODE, inst_header);
	int num_noop;

	/* First check for commands that don't have/use a '# DW' field */
	switch (inst_header & MI_OPCODE) {
	case MI_NOOP:
		num_noop = 1;
		while (num_noop < remaining_dw &&
		       (*(++dw) & REG_GENMASK(31, 23)) == MI_NOOP)
			num_noop++;
		drm_printf(p, "[%#010x] MI_NOOP (%d dwords)\n", inst_header, num_noop);
		return num_noop;

	case MI_TOPOLOGY_FILTER:
		drm_printf(p, "[%#010x] MI_TOPOLOGY_FILTER\n", inst_header);
		return 1;

	case MI_BATCH_BUFFER_END:
		drm_printf(p, "[%#010x] MI_BATCH_BUFFER_END\n", inst_header);
		/* Return 'remaining_dw' to consume the rest of the LRC */
		return remaining_dw;
	}

	/*
	 * Any remaining commands include a # of dwords.  We should make sure
	 * it doesn't exceed the remaining size of the LRC.
	 */
	if (xe_gt_WARN_ON(gt, numdw > remaining_dw))
		numdw = remaining_dw;

	switch (inst_header & MI_OPCODE) {
	case MI_LOAD_REGISTER_IMM:
		drm_printf(p, "[%#010x] MI_LOAD_REGISTER_IMM: %d regs\n",
			   inst_header, (numdw - 1) / 2);
		for (int i = 1; i < numdw; i += 2)
			drm_printf(p, " - %#6x = %#010x\n", dw[i], dw[i + 1]);
		return numdw;

	case MI_LOAD_REGISTER_MEM & MI_OPCODE:
		drm_printf(p, "[%#010x] MI_LOAD_REGISTER_MEM: %s%s\n",
			   inst_header,
			   dw[0] & MI_LRI_LRM_CS_MMIO ? "CS_MMIO " : "",
			   dw[0] & MI_LRM_USE_GGTT ? "USE_GGTT " : "");
		if (numdw == 4)
			drm_printf(p, " - %#6x = %#010llx\n",
				   dw[1], ((u64)(dw[3]) << 32 | (u64)(dw[2])));
		else
			drm_printf(p, " - %*ph (%s)\n",
				   (int)sizeof(u32) * (numdw - 1), dw + 1,
				   numdw < 4 ? "truncated" : "malformed");
		return numdw;

	case MI_FORCE_WAKEUP:
		drm_printf(p, "[%#010x] MI_FORCE_WAKEUP\n", inst_header);
		return numdw;

	default:
		drm_printf(p, "[%#010x] unknown MI opcode %#x, likely %d dwords\n",
			   inst_header, opcode, numdw);
		return numdw;
	}
}

static int dump_gfxpipe_command(struct drm_printer *p,
				struct xe_gt *gt,
				u32 *dw,
				int remaining_dw)
{
	u32 numdw = instr_dw(*dw);
	u32 pipeline = REG_FIELD_GET(GFXPIPE_PIPELINE, *dw);
	u32 opcode = REG_FIELD_GET(GFXPIPE_OPCODE, *dw);
	u32 subopcode = REG_FIELD_GET(GFXPIPE_SUBOPCODE, *dw);

	/*
	 * Make sure we haven't mis-parsed a number of dwords that exceeds the
	 * remaining size of the LRC.
	 */
	if (xe_gt_WARN_ON(gt, numdw > remaining_dw))
		numdw = remaining_dw;

	switch (*dw & GFXPIPE_MATCH_MASK) {
#define MATCH(cmd) \
	case cmd: \
		drm_printf(p, "[%#010x] " #cmd " (%d dwords)\n", *dw, numdw); \
		return numdw
#define MATCH3D(cmd) \
	case CMD_##cmd: \
		drm_printf(p, "[%#010x] " #cmd " (%d dwords)\n", *dw, numdw); \
		return numdw

	MATCH(STATE_BASE_ADDRESS);
	MATCH(STATE_SIP);
	MATCH(GPGPU_CSR_BASE_ADDRESS);
	MATCH(STATE_COMPUTE_MODE);
	MATCH3D(3DSTATE_BTD);

	MATCH3D(3DSTATE_VF_STATISTICS);

	MATCH(PIPELINE_SELECT);

	MATCH3D(3DSTATE_DRAWING_RECTANGLE_FAST);
	MATCH3D(3DSTATE_CLEAR_PARAMS);
	MATCH3D(3DSTATE_DEPTH_BUFFER);
	MATCH3D(3DSTATE_STENCIL_BUFFER);
	MATCH3D(3DSTATE_HIER_DEPTH_BUFFER);
	MATCH3D(3DSTATE_VERTEX_BUFFERS);
	MATCH3D(3DSTATE_VERTEX_ELEMENTS);
	MATCH3D(3DSTATE_INDEX_BUFFER);
	MATCH3D(3DSTATE_VF);
	MATCH3D(3DSTATE_MULTISAMPLE);
	MATCH3D(3DSTATE_CC_STATE_POINTERS);
	MATCH3D(3DSTATE_SCISSOR_STATE_POINTERS);
	MATCH3D(3DSTATE_VS);
	MATCH3D(3DSTATE_GS);
	MATCH3D(3DSTATE_CLIP);
	MATCH3D(3DSTATE_SF);
	MATCH3D(3DSTATE_WM);
	MATCH3D(3DSTATE_CONSTANT_VS);
	MATCH3D(3DSTATE_CONSTANT_GS);
	MATCH3D(3DSTATE_SAMPLE_MASK);
	MATCH3D(3DSTATE_CONSTANT_HS);
	MATCH3D(3DSTATE_CONSTANT_DS);
	MATCH3D(3DSTATE_HS);
	MATCH3D(3DSTATE_TE);
	MATCH3D(3DSTATE_DS);
	MATCH3D(3DSTATE_STREAMOUT);
	MATCH3D(3DSTATE_SBE);
	MATCH3D(3DSTATE_PS);
	MATCH3D(3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP);
	MATCH3D(3DSTATE_CPS_POINTERS);
	MATCH3D(3DSTATE_VIEWPORT_STATE_POINTERS_CC);
	MATCH3D(3DSTATE_BLEND_STATE_POINTERS);
	MATCH3D(3DSTATE_BINDING_TABLE_POINTERS_VS);
	MATCH3D(3DSTATE_BINDING_TABLE_POINTERS_HS);
	MATCH3D(3DSTATE_BINDING_TABLE_POINTERS_DS);
	MATCH3D(3DSTATE_BINDING_TABLE_POINTERS_GS);
	MATCH3D(3DSTATE_BINDING_TABLE_POINTERS_PS);
	MATCH3D(3DSTATE_SAMPLER_STATE_POINTERS_VS);
	MATCH3D(3DSTATE_SAMPLER_STATE_POINTERS_HS);
	MATCH3D(3DSTATE_SAMPLER_STATE_POINTERS_DS);
	MATCH3D(3DSTATE_SAMPLER_STATE_POINTERS_GS);
	MATCH3D(3DSTATE_SAMPLER_STATE_POINTERS_PS);
	MATCH3D(3DSTATE_VF_INSTANCING);
	MATCH3D(3DSTATE_VF_SGVS);
	MATCH3D(3DSTATE_VF_TOPOLOGY);
	MATCH3D(3DSTATE_WM_CHROMAKEY);
	MATCH3D(3DSTATE_PS_BLEND);
	MATCH3D(3DSTATE_WM_DEPTH_STENCIL);
	MATCH3D(3DSTATE_PS_EXTRA);
	MATCH3D(3DSTATE_RASTER);
	MATCH3D(3DSTATE_SBE_SWIZ);
	MATCH3D(3DSTATE_WM_HZ_OP);
	MATCH3D(3DSTATE_VF_COMPONENT_PACKING);
	MATCH3D(3DSTATE_VF_SGVS_2);
	MATCH3D(3DSTATE_VFG);
	MATCH3D(3DSTATE_URB_ALLOC_VS);
	MATCH3D(3DSTATE_URB_ALLOC_HS);
	MATCH3D(3DSTATE_URB_ALLOC_DS);
	MATCH3D(3DSTATE_URB_ALLOC_GS);
	MATCH3D(3DSTATE_SO_BUFFER_INDEX_0);
	MATCH3D(3DSTATE_SO_BUFFER_INDEX_1);
	MATCH3D(3DSTATE_SO_BUFFER_INDEX_2);
	MATCH3D(3DSTATE_SO_BUFFER_INDEX_3);
	MATCH3D(3DSTATE_PRIMITIVE_REPLICATION);
	MATCH3D(3DSTATE_TBIMR_TILE_PASS_INFO);
	MATCH3D(3DSTATE_AMFS);
	MATCH3D(3DSTATE_DEPTH_BOUNDS);
	MATCH3D(3DSTATE_AMFS_TEXTURE_POINTERS);
	MATCH3D(3DSTATE_CONSTANT_TS_POINTER);
	MATCH3D(3DSTATE_MESH_CONTROL);
	MATCH3D(3DSTATE_MESH_DISTRIB);
	MATCH3D(3DSTATE_TASK_REDISTRIB);
	MATCH3D(3DSTATE_MESH_SHADER);
	MATCH3D(3DSTATE_MESH_SHADER_DATA);
	MATCH3D(3DSTATE_TASK_CONTROL);
	MATCH3D(3DSTATE_TASK_SHADER);
	MATCH3D(3DSTATE_TASK_SHADER_DATA);
	MATCH3D(3DSTATE_URB_ALLOC_MESH);
	MATCH3D(3DSTATE_URB_ALLOC_TASK);
	MATCH3D(3DSTATE_CLIP_MESH);
	MATCH3D(3DSTATE_SBE_MESH);
	MATCH3D(3DSTATE_CPSIZE_CONTROL_BUFFER);

	MATCH3D(3DSTATE_DRAWING_RECTANGLE);
	MATCH3D(3DSTATE_CHROMA_KEY);
	MATCH3D(3DSTATE_POLY_STIPPLE_OFFSET);
	MATCH3D(3DSTATE_POLY_STIPPLE_PATTERN);
	MATCH3D(3DSTATE_LINE_STIPPLE);
	MATCH3D(3DSTATE_AA_LINE_PARAMETERS);
	MATCH3D(3DSTATE_MONOFILTER_SIZE);
	MATCH3D(3DSTATE_PUSH_CONSTANT_ALLOC_VS);
	MATCH3D(3DSTATE_PUSH_CONSTANT_ALLOC_HS);
	MATCH3D(3DSTATE_PUSH_CONSTANT_ALLOC_DS);
	MATCH3D(3DSTATE_PUSH_CONSTANT_ALLOC_GS);
	MATCH3D(3DSTATE_PUSH_CONSTANT_ALLOC_PS);
	MATCH3D(3DSTATE_SO_DECL_LIST);
	MATCH3D(3DSTATE_SO_BUFFER);
	MATCH3D(3DSTATE_BINDING_TABLE_POOL_ALLOC);
	MATCH3D(3DSTATE_SAMPLE_PATTERN);
	MATCH3D(3DSTATE_3D_MODE);
	MATCH3D(3DSTATE_SUBSLICE_HASH_TABLE);
	MATCH3D(3DSTATE_SLICE_TABLE_STATE_POINTERS);
	MATCH3D(3DSTATE_PTBR_TILE_PASS_INFO);

	default:
		drm_printf(p, "[%#010x] unknown GFXPIPE command (pipeline=%#x, opcode=%#x, subopcode=%#x), likely %d dwords\n",
			   *dw, pipeline, opcode, subopcode, numdw);
		return numdw;
	}
}

void xe_lrc_dump_default(struct drm_printer *p,
			 struct xe_gt *gt,
			 enum xe_engine_class hwe_class)
{
	u32 *dw;
	int remaining_dw, num_dw;

	if (!gt->default_lrc[hwe_class]) {
		drm_printf(p, "No default LRC for class %d\n", hwe_class);
		return;
	}

	/*
	 * Skip the beginning of the LRC since it contains the per-process
	 * hardware status page.
	 */
	dw = gt->default_lrc[hwe_class] + LRC_PPHWSP_SIZE;
	remaining_dw = (xe_lrc_size(gt_to_xe(gt), hwe_class) - LRC_PPHWSP_SIZE) / 4;

	while (remaining_dw > 0) {
		if ((*dw & XE_INSTR_CMD_TYPE) == XE_INSTR_MI) {
			num_dw = dump_mi_command(p, gt, dw, remaining_dw);
		} else if ((*dw & XE_INSTR_CMD_TYPE) == XE_INSTR_GFXPIPE) {
			num_dw = dump_gfxpipe_command(p, gt, dw, remaining_dw);
		} else {
			num_dw = min(instr_dw(*dw), remaining_dw);
			drm_printf(p, "[%#10x] Unknown instruction of type %#x, likely %d dwords\n",
				   *dw, REG_FIELD_GET(XE_INSTR_CMD_TYPE, *dw),
				   num_dw);
		}

		dw += num_dw;
		remaining_dw -= num_dw;
	}
}

struct instr_state {
	u32 instr;
	u16 num_dw;
};

static const struct instr_state xe_hpg_svg_state[] = {
	{ .instr = CMD_3DSTATE_CONSTANT_VS, .num_dw = 11 },
	{ .instr = CMD_3DSTATE_CONSTANT_HS, .num_dw = 11 },
	{ .instr = CMD_3DSTATE_CONSTANT_DS, .num_dw = 11 },
	{ .instr = CMD_3DSTATE_CONSTANT_GS, .num_dw = 11 },
	{ .instr = CMD_3DSTATE_VERTEX_ELEMENTS, .num_dw = 69 },
	{ .instr = CMD_3DSTATE_VF_COMPONENT_PACKING, .num_dw = 5 },
	{ .instr = CMD_3DSTATE_VF_SGVS, .num_dw = 2 },
	{ .instr = CMD_3DSTATE_VF_SGVS_2, .num_dw = 3 },
	{ .instr = CMD_3DSTATE_VS, .num_dw = 9 },
	{ .instr = CMD_3DSTATE_BINDING_TABLE_POINTERS_VS, .num_dw = 2 },
	{ .instr = CMD_3DSTATE_SAMPLER_STATE_POINTERS_VS, .num_dw = 2 },
	{ .instr = CMD_3DSTATE_URB_ALLOC_VS, .num_dw = 3 },
	{ .instr = CMD_3DSTATE_STREAMOUT, .num_dw = 5 },
	{ .instr = CMD_3DSTATE_SO_BUFFER_INDEX_0, .num_dw = 8 },
	{ .instr = CMD_3DSTATE_SO_BUFFER_INDEX_1, .num_dw = 8 },
	{ .instr = CMD_3DSTATE_SO_BUFFER_INDEX_2, .num_dw = 8 },
	{ .instr = CMD_3DSTATE_SO_BUFFER_INDEX_3, .num_dw = 8 },
	{ .instr = CMD_3DSTATE_CLIP, .num_dw = 4 },
	{ .instr = CMD_3DSTATE_PRIMITIVE_REPLICATION, .num_dw = 6 },
	{ .instr = CMD_3DSTATE_CLIP_MESH, .num_dw = 2 },
	{ .instr = CMD_3DSTATE_SF, .num_dw = 4 },
	{ .instr = CMD_3DSTATE_SCISSOR_STATE_POINTERS, .num_dw = 2 },
	{ .instr = CMD_3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP, .num_dw = 2 },
	{ .instr = CMD_3DSTATE_RASTER, .num_dw = 5 },
	{ .instr = CMD_3DSTATE_TBIMR_TILE_PASS_INFO, .num_dw = 4 },
	{ .instr = CMD_3DSTATE_WM_HZ_OP, .num_dw = 6 },
	{ .instr = CMD_3DSTATE_MULTISAMPLE, .num_dw = 2 },
	{ .instr = CMD_3DSTATE_HS, .num_dw = 9 },
	{ .instr = CMD_3DSTATE_BINDING_TABLE_POINTERS_HS, .num_dw = 2 },
	{ .instr = CMD_3DSTATE_SAMPLER_STATE_POINTERS_HS, .num_dw = 2 },
	{ .instr = CMD_3DSTATE_URB_ALLOC_HS, .num_dw = 3 },
	{ .instr = CMD_3DSTATE_TASK_CONTROL, .num_dw = 3 },
	{ .instr = CMD_3DSTATE_TASK_SHADER, .num_dw = 7 },
	{ .instr = CMD_3DSTATE_TASK_SHADER_DATA, .num_dw = 10 },
	{ .instr = CMD_3DSTATE_URB_ALLOC_TASK, .num_dw = 3 },
	{ .instr = CMD_3DSTATE_TE, .num_dw = 5 },
	{ .instr = CMD_3DSTATE_TASK_REDISTRIB, .num_dw = 2 },
	{ .instr = CMD_3DSTATE_DS, .num_dw = 11 },
	{ .instr = CMD_3DSTATE_BINDING_TABLE_POINTERS_DS, .num_dw = 2 },
	{ .instr = CMD_3DSTATE_SAMPLER_STATE_POINTERS_DS, .num_dw = 2 },
	{ .instr = CMD_3DSTATE_URB_ALLOC_DS, .num_dw = 3 },
	{ .instr = CMD_3DSTATE_GS, .num_dw = 10 },
	{ .instr = CMD_3DSTATE_BINDING_TABLE_POINTERS_GS, .num_dw = 2 },
	{ .instr = CMD_3DSTATE_SAMPLER_STATE_POINTERS_GS, .num_dw = 2 },
	{ .instr = CMD_3DSTATE_URB_ALLOC_GS, .num_dw = 3 },
	{ .instr = CMD_3DSTATE_MESH_CONTROL, .num_dw = 3 },
	{ .instr = CMD_3DSTATE_MESH_SHADER_DATA, .num_dw = 10 },
	{ .instr = CMD_3DSTATE_URB_ALLOC_MESH, .num_dw = 3 },
	{ .instr = CMD_3DSTATE_MESH_SHADER, .num_dw = 8 },
	{ .instr = CMD_3DSTATE_DRAWING_RECTANGLE, .num_dw = 4 },
};

void xe_lrc_emit_hwe_state_instructions(struct xe_exec_queue *q, struct xe_bb *bb)
{
	struct xe_gt *gt = q->hwe->gt;
	struct xe_device *xe = gt_to_xe(gt);
	const struct instr_state *state_table = NULL;
	int state_table_size = 0;

	/*
	 * At the moment we only need to emit non-register state for the RCS
	 * engine.
	 */
	if (q->hwe->class != XE_ENGINE_CLASS_RENDER)
		return;

	switch (GRAPHICS_VERx100(xe)) {
	case 1255:
	case 1270 ... 2004:
		state_table = xe_hpg_svg_state;
		state_table_size = ARRAY_SIZE(xe_hpg_svg_state);
		break;
	default:
		xe_gt_dbg(gt, "No non-register state to emit on graphics ver %d.%02d\n",
			  GRAPHICS_VER(xe), GRAPHICS_VERx100(xe) % 100);
		return;
	}

	for (int i = 0; i < state_table_size; i++) {
		u32 instr = state_table[i].instr;
		u16 num_dw = state_table[i].num_dw;
		bool is_single_dw = ((instr & GFXPIPE_PIPELINE) == PIPELINE_SINGLE_DW);

		xe_gt_assert(gt, (instr & XE_INSTR_CMD_TYPE) == XE_INSTR_GFXPIPE);
		xe_gt_assert(gt, num_dw != 0);
		xe_gt_assert(gt, is_single_dw ^ (num_dw > 1));

		/*
		 * Xe2's SVG context is the same as the one on DG2 / MTL
		 * except that 3DSTATE_DRAWING_RECTANGLE (non-pipelined) has
		 * been replaced by 3DSTATE_DRAWING_RECTANGLE_FAST (pipelined).
		 * Just make the replacement here rather than defining a
		 * whole separate table for the single trivial change.
		 */
		if (GRAPHICS_VER(xe) >= 20 &&
		    instr == CMD_3DSTATE_DRAWING_RECTANGLE)
			instr = CMD_3DSTATE_DRAWING_RECTANGLE_FAST;

		bb->cs[bb->len] = instr;
		if (!is_single_dw)
			bb->cs[bb->len] |= (num_dw - 2);

		bb->len += num_dw;
	}
}
