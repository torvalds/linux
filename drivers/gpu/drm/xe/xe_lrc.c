// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_lrc.h"

#include <generated/xe_wa_oob.h>

#include <linux/ascii85.h>
#include <linux/panic.h>

#include "instructions/xe_mi_commands.h"
#include "instructions/xe_gfxpipe_commands.h"
#include "instructions/xe_gfx_state_commands.h"
#include "regs/xe_engine_regs.h"
#include "regs/xe_lrc_layout.h"
#include "xe_bb.h"
#include "xe_bo.h"
#include "xe_configfs.h"
#include "xe_device.h"
#include "xe_drm_client.h"
#include "xe_exec_queue_types.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_hw_fence.h"
#include "xe_map.h"
#include "xe_memirq.h"
#include "xe_mmio.h"
#include "xe_sriov.h"
#include "xe_trace_lrc.h"
#include "xe_vm.h"
#include "xe_wa.h"

#define LRC_VALID				BIT_ULL(0)
#define LRC_PRIVILEGE				BIT_ULL(8)
#define LRC_ADDRESSING_MODE			GENMASK_ULL(4, 3)
#define LRC_LEGACY_64B_CONTEXT			3

#define LRC_ENGINE_CLASS			GENMASK_ULL(63, 61)
#define LRC_ENGINE_INSTANCE			GENMASK_ULL(53, 48)

#define LRC_PPHWSP_SIZE				SZ_4K
#define LRC_INDIRECT_CTX_BO_SIZE		SZ_4K
#define LRC_INDIRECT_RING_STATE_SIZE		SZ_4K

/*
 * Layout of the LRC and associated data allocated as
 * lrc->bo:
 *
 *   Region                       Size
 *  +============================+=================================+ <- __xe_lrc_ring_offset()
 *  | Ring                       | ring_size, see                  |
 *  |                            | xe_lrc_init()                   |
 *  +============================+=================================+ <- __xe_lrc_pphwsp_offset()
 *  | PPHWSP (includes SW state) | 4K                              |
 *  +----------------------------+---------------------------------+ <- __xe_lrc_regs_offset()
 *  | Engine Context Image       | n * 4K, see                     |
 *  |                            | xe_gt_lrc_size()                |
 *  +----------------------------+---------------------------------+ <- __xe_lrc_indirect_ring_offset()
 *  | Indirect Ring State Page   | 0 or 4k, see                    |
 *  |                            | XE_LRC_FLAG_INDIRECT_RING_STATE |
 *  +============================+=================================+ <- __xe_lrc_indirect_ctx_offset()
 *  | Indirect Context Page      | 0 or 4k, see                    |
 *  |                            | XE_LRC_FLAG_INDIRECT_CTX        |
 *  +============================+=================================+ <- __xe_lrc_wa_bb_offset()
 *  | WA BB Per Ctx              | 4k                              |
 *  +============================+=================================+ <- xe_bo_size(lrc->bo)
 */

static struct xe_device *
lrc_to_xe(struct xe_lrc *lrc)
{
	return gt_to_xe(lrc->fence_ctx.gt);
}

static bool
gt_engine_needs_indirect_ctx(struct xe_gt *gt, enum xe_engine_class class)
{
	struct xe_device *xe = gt_to_xe(gt);

	if (XE_GT_WA(gt, 16010904313) &&
	    (class == XE_ENGINE_CLASS_RENDER ||
	     class == XE_ENGINE_CLASS_COMPUTE))
		return true;

	if (xe_configfs_get_ctx_restore_mid_bb(to_pci_dev(xe->drm.dev),
					       class, NULL))
		return true;

	return false;
}

size_t xe_gt_lrc_size(struct xe_gt *gt, enum xe_engine_class class)
{
	struct xe_device *xe = gt_to_xe(gt);
	size_t size;

	/* Per-process HW status page (PPHWSP) */
	size = LRC_PPHWSP_SIZE;

	/* Engine context image */
	switch (class) {
	case XE_ENGINE_CLASS_RENDER:
		if (GRAPHICS_VER(xe) >= 20)
			size += 3 * SZ_4K;
		else
			size += 13 * SZ_4K;
		break;
	case XE_ENGINE_CLASS_COMPUTE:
		if (GRAPHICS_VER(xe) >= 20)
			size += 2 * SZ_4K;
		else
			size += 13 * SZ_4K;
		break;
	default:
		WARN(1, "Unknown engine class: %d", class);
		fallthrough;
	case XE_ENGINE_CLASS_COPY:
	case XE_ENGINE_CLASS_VIDEO_DECODE:
	case XE_ENGINE_CLASS_VIDEO_ENHANCE:
	case XE_ENGINE_CLASS_OTHER:
		size += 1 * SZ_4K;
	}

	/* Add indirect ring state page */
	if (xe_gt_has_indirect_ring_state(gt))
		size += LRC_INDIRECT_RING_STATE_SIZE;

	return size;
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

static const u8 xe2_indirect_ring_state_offsets[] = {
	NOP(1),                 /* [0x00] */
	LRI(5, POSTED),         /* [0x01] */
	REG(0x034),             /* [0x02] RING_BUFFER_HEAD */
	REG(0x030),             /* [0x04] RING_BUFFER_TAIL */
	REG(0x038),             /* [0x06] RING_BUFFER_START */
	REG(0x048),             /* [0x08] RING_BUFFER_START_UDW */
	REG(0x03c),             /* [0x0a] RING_BUFFER_CONTROL */

	NOP(5),                 /* [0x0c] */
	LRI(9, POSTED),         /* [0x11] */
	REG(0x168),             /* [0x12] BB_ADDR_UDW */
	REG(0x140),             /* [0x14] BB_ADDR */
	REG(0x110),             /* [0x16] BB_STATE */
	REG16(0x588),           /* [0x18] BB_STACK_WRITE_PORT */
	REG16(0x588),           /* [0x20] BB_STACK_WRITE_PORT */
	REG16(0x588),           /* [0x22] BB_STACK_WRITE_PORT */
	REG16(0x588),           /* [0x24] BB_STACK_WRITE_PORT */
	REG16(0x588),           /* [0x26] BB_STACK_WRITE_PORT */
	REG16(0x588),           /* [0x28] BB_STACK_WRITE_PORT */

	NOP(12),                 /* [0x00] */

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
	regs[CTX_CONTEXT_CONTROL] = _MASKED_BIT_ENABLE(CTX_CTRL_INHIBIT_SYN_CTX_SWITCH |
						       CTX_CTRL_ENGINE_CTX_RESTORE_INHIBIT);

	if (xe_gt_has_indirect_ring_state(hwe->gt))
		regs[CTX_CONTEXT_CONTROL] |=
			_MASKED_BIT_ENABLE(CTX_CTRL_INDIRECT_RING_STATE_ENABLE);
}

static void set_memory_based_intr(u32 *regs, struct xe_hw_engine *hwe)
{
	struct xe_memirq *memirq = &gt_to_tile(hwe->gt)->memirq;
	struct xe_device *xe = gt_to_xe(hwe->gt);
	u8 num_regs;

	if (!xe_device_uses_memirq(xe))
		return;

	regs[CTX_LRM_INT_MASK_ENABLE] = MI_LOAD_REGISTER_MEM |
					MI_LRI_LRM_CS_MMIO | MI_LRM_USE_GGTT;
	regs[CTX_INT_MASK_ENABLE_REG] = RING_IMR(0).addr;
	regs[CTX_INT_MASK_ENABLE_PTR] = xe_memirq_enable_ptr(memirq);

	num_regs = xe_device_has_msix(xe) ? 3 : 2;
	regs[CTX_LRI_INT_REPORT_PTR] = MI_LOAD_REGISTER_IMM | MI_LRI_NUM_REGS(num_regs) |
				       MI_LRI_LRM_CS_MMIO | MI_LRI_FORCE_POSTED;
	regs[CTX_INT_STATUS_REPORT_REG] = RING_INT_STATUS_RPT_PTR(0).addr;
	regs[CTX_INT_STATUS_REPORT_PTR] = xe_memirq_status_ptr(memirq, hwe);
	regs[CTX_INT_SRC_REPORT_REG] = RING_INT_SRC_RPT_PTR(0).addr;
	regs[CTX_INT_SRC_REPORT_PTR] = xe_memirq_source_ptr(memirq, hwe);

	if (xe_device_has_msix(xe)) {
		regs[CTX_CS_INT_VEC_REG] = CS_INT_VEC(0).addr;
		/* CTX_CS_INT_VEC_DATA will be set in xe_lrc_init */
	}
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

static inline bool xe_lrc_has_indirect_ring_state(struct xe_lrc *lrc)
{
	return lrc->flags & XE_LRC_FLAG_INDIRECT_RING_STATE;
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
#define __xe_lrc_regs_offset xe_lrc_regs_offset

#define LRC_SEQNO_PPHWSP_OFFSET 512
#define LRC_START_SEQNO_PPHWSP_OFFSET (LRC_SEQNO_PPHWSP_OFFSET + 8)
#define LRC_CTX_JOB_TIMESTAMP_OFFSET (LRC_START_SEQNO_PPHWSP_OFFSET + 8)
#define LRC_ENGINE_ID_PPHWSP_OFFSET 1024
#define LRC_PARALLEL_PPHWSP_OFFSET 2048

u32 xe_lrc_regs_offset(struct xe_lrc *lrc)
{
	return xe_lrc_pphwsp_offset(lrc) + LRC_PPHWSP_SIZE;
}

/**
 * xe_lrc_reg_size() - Get size of the LRC registers area within queues
 * @xe: the &xe_device struct instance
 *
 * Returns: Size of the LRC registers area for current platform
 */
size_t xe_lrc_reg_size(struct xe_device *xe)
{
	if (GRAPHICS_VERx100(xe) >= 1250)
		return 96 * sizeof(u32);
	else
		return 80 * sizeof(u32);
}

size_t xe_lrc_skip_size(struct xe_device *xe)
{
	return LRC_PPHWSP_SIZE + xe_lrc_reg_size(xe);
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

static u32 __xe_lrc_ctx_job_timestamp_offset(struct xe_lrc *lrc)
{
	/* This is stored in the driver-defined portion of PPHWSP */
	return xe_lrc_pphwsp_offset(lrc) + LRC_CTX_JOB_TIMESTAMP_OFFSET;
}

static inline u32 __xe_lrc_parallel_offset(struct xe_lrc *lrc)
{
	/* The parallel is stored in the driver-defined portion of PPHWSP */
	return xe_lrc_pphwsp_offset(lrc) + LRC_PARALLEL_PPHWSP_OFFSET;
}

static inline u32 __xe_lrc_engine_id_offset(struct xe_lrc *lrc)
{
	return xe_lrc_pphwsp_offset(lrc) + LRC_ENGINE_ID_PPHWSP_OFFSET;
}

static u32 __xe_lrc_ctx_timestamp_offset(struct xe_lrc *lrc)
{
	return __xe_lrc_regs_offset(lrc) + CTX_TIMESTAMP * sizeof(u32);
}

static u32 __xe_lrc_ctx_timestamp_udw_offset(struct xe_lrc *lrc)
{
	return __xe_lrc_regs_offset(lrc) + CTX_TIMESTAMP_UDW * sizeof(u32);
}

static inline u32 __xe_lrc_indirect_ring_offset(struct xe_lrc *lrc)
{
	u32 offset = xe_bo_size(lrc->bo) - LRC_WA_BB_SIZE -
		     LRC_INDIRECT_RING_STATE_SIZE;

	if (lrc->flags & XE_LRC_FLAG_INDIRECT_CTX)
		offset -= LRC_INDIRECT_CTX_BO_SIZE;

	return offset;
}

static inline u32 __xe_lrc_indirect_ctx_offset(struct xe_lrc *lrc)
{
	return xe_bo_size(lrc->bo) - LRC_WA_BB_SIZE - LRC_INDIRECT_CTX_BO_SIZE;
}

static inline u32 __xe_lrc_wa_bb_offset(struct xe_lrc *lrc)
{
	return xe_bo_size(lrc->bo) - LRC_WA_BB_SIZE;
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
static inline u32 __maybe_unused __xe_lrc_##elem##_ggtt_addr(struct xe_lrc *lrc) \
{ \
	return xe_bo_ggtt_addr(lrc->bo) + __xe_lrc_##elem##_offset(lrc); \
} \

DECL_MAP_ADDR_HELPERS(ring)
DECL_MAP_ADDR_HELPERS(pphwsp)
DECL_MAP_ADDR_HELPERS(seqno)
DECL_MAP_ADDR_HELPERS(regs)
DECL_MAP_ADDR_HELPERS(start_seqno)
DECL_MAP_ADDR_HELPERS(ctx_job_timestamp)
DECL_MAP_ADDR_HELPERS(ctx_timestamp)
DECL_MAP_ADDR_HELPERS(ctx_timestamp_udw)
DECL_MAP_ADDR_HELPERS(parallel)
DECL_MAP_ADDR_HELPERS(indirect_ring)
DECL_MAP_ADDR_HELPERS(engine_id)

#undef DECL_MAP_ADDR_HELPERS

/**
 * xe_lrc_ctx_timestamp_ggtt_addr() - Get ctx timestamp GGTT address
 * @lrc: Pointer to the lrc.
 *
 * Returns: ctx timestamp GGTT address
 */
u32 xe_lrc_ctx_timestamp_ggtt_addr(struct xe_lrc *lrc)
{
	return __xe_lrc_ctx_timestamp_ggtt_addr(lrc);
}

/**
 * xe_lrc_ctx_timestamp_udw_ggtt_addr() - Get ctx timestamp udw GGTT address
 * @lrc: Pointer to the lrc.
 *
 * Returns: ctx timestamp udw GGTT address
 */
u32 xe_lrc_ctx_timestamp_udw_ggtt_addr(struct xe_lrc *lrc)
{
	return __xe_lrc_ctx_timestamp_udw_ggtt_addr(lrc);
}

/**
 * xe_lrc_ctx_timestamp() - Read ctx timestamp value
 * @lrc: Pointer to the lrc.
 *
 * Returns: ctx timestamp value
 */
u64 xe_lrc_ctx_timestamp(struct xe_lrc *lrc)
{
	struct xe_device *xe = lrc_to_xe(lrc);
	struct iosys_map map;
	u32 ldw, udw = 0;

	map = __xe_lrc_ctx_timestamp_map(lrc);
	ldw = xe_map_read32(xe, &map);

	if (xe->info.has_64bit_timestamp) {
		map = __xe_lrc_ctx_timestamp_udw_map(lrc);
		udw = xe_map_read32(xe, &map);
	}

	return (u64)udw << 32 | ldw;
}

/**
 * xe_lrc_ctx_job_timestamp_ggtt_addr() - Get ctx job timestamp GGTT address
 * @lrc: Pointer to the lrc.
 *
 * Returns: ctx timestamp job GGTT address
 */
u32 xe_lrc_ctx_job_timestamp_ggtt_addr(struct xe_lrc *lrc)
{
	return __xe_lrc_ctx_job_timestamp_ggtt_addr(lrc);
}

/**
 * xe_lrc_ctx_job_timestamp() - Read ctx job timestamp value
 * @lrc: Pointer to the lrc.
 *
 * Returns: ctx timestamp job value
 */
u32 xe_lrc_ctx_job_timestamp(struct xe_lrc *lrc)
{
	struct xe_device *xe = lrc_to_xe(lrc);
	struct iosys_map map;

	map = __xe_lrc_ctx_job_timestamp_map(lrc);
	return xe_map_read32(xe, &map);
}

u32 xe_lrc_ggtt_addr(struct xe_lrc *lrc)
{
	return __xe_lrc_pphwsp_ggtt_addr(lrc);
}

u32 xe_lrc_indirect_ring_ggtt_addr(struct xe_lrc *lrc)
{
	if (!xe_lrc_has_indirect_ring_state(lrc))
		return 0;

	return __xe_lrc_indirect_ring_ggtt_addr(lrc);
}

static u32 xe_lrc_read_indirect_ctx_reg(struct xe_lrc *lrc, int reg_nr)
{
	struct xe_device *xe = lrc_to_xe(lrc);
	struct iosys_map map;

	map = __xe_lrc_indirect_ring_map(lrc);
	iosys_map_incr(&map, reg_nr * sizeof(u32));
	return xe_map_read32(xe, &map);
}

static void xe_lrc_write_indirect_ctx_reg(struct xe_lrc *lrc,
					  int reg_nr, u32 val)
{
	struct xe_device *xe = lrc_to_xe(lrc);
	struct iosys_map map;

	map = __xe_lrc_indirect_ring_map(lrc);
	iosys_map_incr(&map, reg_nr * sizeof(u32));
	xe_map_write32(xe, &map, val);
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
	struct xe_gt *gt = hwe->gt;
	void *data;
	u32 *regs;

	data = kzalloc(xe_gt_lrc_size(gt, hwe->class), GFP_KERNEL);
	if (!data)
		return NULL;

	/* 1st page: Per-Process of HW status Page */
	regs = data + LRC_PPHWSP_SIZE;
	set_offsets(regs, reg_offsets(gt_to_xe(gt), hwe->class), hwe);
	set_context_control(regs, hwe);
	set_memory_based_intr(regs, hwe);
	reset_stop_ring(regs, hwe);
	if (xe_gt_has_indirect_ring_state(gt)) {
		regs = data + xe_gt_lrc_size(gt, hwe->class) -
		       LRC_INDIRECT_RING_STATE_SIZE;
		set_offsets(regs, xe2_indirect_ring_state_offsets, hwe);
	}

	return data;
}

/**
 * xe_default_lrc_update_memirq_regs_with_address - Re-compute GGTT references in default LRC
 * of given engine.
 * @hwe: the &xe_hw_engine struct instance
 */
void xe_default_lrc_update_memirq_regs_with_address(struct xe_hw_engine *hwe)
{
	struct xe_gt *gt = hwe->gt;
	u32 *regs;

	if (!gt->default_lrc[hwe->class])
		return;

	regs = gt->default_lrc[hwe->class] + LRC_PPHWSP_SIZE;
	set_memory_based_intr(regs, hwe);
}

/**
 * xe_lrc_update_memirq_regs_with_address - Re-compute GGTT references in mem interrupt data
 * for given LRC.
 * @lrc: the &xe_lrc struct instance
 * @hwe: the &xe_hw_engine struct instance
 * @regs: scratch buffer to be used as temporary storage
 */
void xe_lrc_update_memirq_regs_with_address(struct xe_lrc *lrc, struct xe_hw_engine *hwe,
					    u32 *regs)
{
	struct xe_gt *gt = hwe->gt;
	struct iosys_map map;
	size_t regs_len;

	if (!xe_device_uses_memirq(gt_to_xe(gt)))
		return;

	map = __xe_lrc_regs_map(lrc);
	regs_len = xe_lrc_reg_size(gt_to_xe(gt));
	xe_map_memcpy_from(gt_to_xe(gt), regs, &map, 0, regs_len);
	set_memory_based_intr(regs, hwe);
	xe_map_memcpy_to(gt_to_xe(gt), &map, 0, regs, regs_len);
}

static void xe_lrc_set_ppgtt(struct xe_lrc *lrc, struct xe_vm *vm)
{
	u64 desc = xe_vm_pdp4_descriptor(vm, gt_to_tile(lrc->gt));

	xe_lrc_write_ctx_reg(lrc, CTX_PDP0_UDW, upper_32_bits(desc));
	xe_lrc_write_ctx_reg(lrc, CTX_PDP0_LDW, lower_32_bits(desc));
}

static void xe_lrc_finish(struct xe_lrc *lrc)
{
	xe_hw_fence_ctx_finish(&lrc->fence_ctx);
	xe_bo_unpin_map_no_vm(lrc->bo);
}

/*
 * wa_bb_setup_utilization() - Write commands to wa bb to assist
 * in calculating active context run ticks.
 *
 * Context Timestamp (CTX_TIMESTAMP) in the LRC accumulates the run ticks of the
 * context, but only gets updated when the context switches out. In order to
 * check how long a context has been active before it switches out, two things
 * are required:
 *
 * (1) Determine if the context is running:
 * To do so, we program the WA BB to set an initial value for CTX_TIMESTAMP in
 * the LRC. The value chosen is 1 since 0 is the initial value when the LRC is
 * initialized. During a query, we just check for this value to determine if the
 * context is active. If the context switched out, it would overwrite this
 * location with the actual CTX_TIMESTAMP MMIO value. Note that WA BB runs as
 * the last part of context restore, so reusing this LRC location will not
 * clobber anything.
 *
 * (2) Calculate the time that the context has been active for:
 * The CTX_TIMESTAMP ticks only when the context is active. If a context is
 * active, we just use the CTX_TIMESTAMP MMIO as the new value of utilization.
 * While doing so, we need to read the CTX_TIMESTAMP MMIO for the specific
 * engine instance. Since we do not know which instance the context is running
 * on until it is scheduled, we also read the ENGINE_ID MMIO in the WA BB and
 * store it in the PPHSWP.
 */
#define CONTEXT_ACTIVE 1ULL
static ssize_t setup_utilization_wa(struct xe_lrc *lrc,
				    struct xe_hw_engine *hwe,
				    u32 *batch,
				    size_t max_len)
{
	u32 *cmd = batch;

	if (xe_gt_WARN_ON(lrc->gt, max_len < 12))
		return -ENOSPC;

	*cmd++ = MI_STORE_REGISTER_MEM | MI_SRM_USE_GGTT | MI_SRM_ADD_CS_OFFSET;
	*cmd++ = ENGINE_ID(0).addr;
	*cmd++ = __xe_lrc_engine_id_ggtt_addr(lrc);
	*cmd++ = 0;

	*cmd++ = MI_STORE_DATA_IMM | MI_SDI_GGTT | MI_SDI_NUM_DW(1);
	*cmd++ = __xe_lrc_ctx_timestamp_ggtt_addr(lrc);
	*cmd++ = 0;
	*cmd++ = lower_32_bits(CONTEXT_ACTIVE);

	if (lrc_to_xe(lrc)->info.has_64bit_timestamp) {
		*cmd++ = MI_STORE_DATA_IMM | MI_SDI_GGTT | MI_SDI_NUM_DW(1);
		*cmd++ = __xe_lrc_ctx_timestamp_udw_ggtt_addr(lrc);
		*cmd++ = 0;
		*cmd++ = upper_32_bits(CONTEXT_ACTIVE);
	}

	return cmd - batch;
}

static ssize_t setup_timestamp_wa(struct xe_lrc *lrc, struct xe_hw_engine *hwe,
				  u32 *batch, size_t max_len)
{
	const u32 ts_addr = __xe_lrc_ctx_timestamp_ggtt_addr(lrc);
	u32 *cmd = batch;

	if (!XE_GT_WA(lrc->gt, 16010904313) ||
	    !(hwe->class == XE_ENGINE_CLASS_RENDER ||
	      hwe->class == XE_ENGINE_CLASS_COMPUTE ||
	      hwe->class == XE_ENGINE_CLASS_COPY ||
	      hwe->class == XE_ENGINE_CLASS_VIDEO_DECODE ||
	      hwe->class == XE_ENGINE_CLASS_VIDEO_ENHANCE))
		return 0;

	if (xe_gt_WARN_ON(lrc->gt, max_len < 12))
		return -ENOSPC;

	*cmd++ = MI_LOAD_REGISTER_MEM | MI_LRM_USE_GGTT | MI_LRI_LRM_CS_MMIO |
		 MI_LRM_ASYNC;
	*cmd++ = RING_CTX_TIMESTAMP(0).addr;
	*cmd++ = ts_addr;
	*cmd++ = 0;

	*cmd++ = MI_LOAD_REGISTER_MEM | MI_LRM_USE_GGTT | MI_LRI_LRM_CS_MMIO |
		 MI_LRM_ASYNC;
	*cmd++ = RING_CTX_TIMESTAMP(0).addr;
	*cmd++ = ts_addr;
	*cmd++ = 0;

	*cmd++ = MI_LOAD_REGISTER_MEM | MI_LRM_USE_GGTT | MI_LRI_LRM_CS_MMIO;
	*cmd++ = RING_CTX_TIMESTAMP(0).addr;
	*cmd++ = ts_addr;
	*cmd++ = 0;

	return cmd - batch;
}

static ssize_t setup_configfs_post_ctx_restore_bb(struct xe_lrc *lrc,
						  struct xe_hw_engine *hwe,
						  u32 *batch, size_t max_len)
{
	struct xe_device *xe = gt_to_xe(lrc->gt);
	const u32 *user_batch;
	u32 *cmd = batch;
	u32 count;

	count = xe_configfs_get_ctx_restore_post_bb(to_pci_dev(xe->drm.dev),
						    hwe->class, &user_batch);
	if (!count)
		return 0;

	if (count > max_len)
		return -ENOSPC;

	/*
	 * This should be used only for tests and validation. Taint the kernel
	 * as anything could be submitted directly in context switches
	 */
	add_taint(TAINT_TEST, LOCKDEP_STILL_OK);

	memcpy(cmd, user_batch, count * sizeof(u32));
	cmd += count;

	return cmd - batch;
}

static ssize_t setup_configfs_mid_ctx_restore_bb(struct xe_lrc *lrc,
						 struct xe_hw_engine *hwe,
						 u32 *batch, size_t max_len)
{
	struct xe_device *xe = gt_to_xe(lrc->gt);
	const u32 *user_batch;
	u32 *cmd = batch;
	u32 count;

	count = xe_configfs_get_ctx_restore_mid_bb(to_pci_dev(xe->drm.dev),
						   hwe->class, &user_batch);
	if (!count)
		return 0;

	if (count > max_len)
		return -ENOSPC;

	/*
	 * This should be used only for tests and validation. Taint the kernel
	 * as anything could be submitted directly in context switches
	 */
	add_taint(TAINT_TEST, LOCKDEP_STILL_OK);

	memcpy(cmd, user_batch, count * sizeof(u32));
	cmd += count;

	return cmd - batch;
}

static ssize_t setup_invalidate_state_cache_wa(struct xe_lrc *lrc,
					       struct xe_hw_engine *hwe,
					       u32 *batch, size_t max_len)
{
	u32 *cmd = batch;

	if (!XE_GT_WA(lrc->gt, 18022495364) ||
	    hwe->class != XE_ENGINE_CLASS_RENDER)
		return 0;

	if (xe_gt_WARN_ON(lrc->gt, max_len < 3))
		return -ENOSPC;

	*cmd++ = MI_LOAD_REGISTER_IMM | MI_LRI_NUM_REGS(1);
	*cmd++ = CS_DEBUG_MODE1(0).addr;
	*cmd++ = _MASKED_BIT_ENABLE(INSTRUCTION_STATE_CACHE_INVALIDATE);

	return cmd - batch;
}

struct bo_setup {
	ssize_t (*setup)(struct xe_lrc *lrc, struct xe_hw_engine *hwe,
			 u32 *batch, size_t max_size);
};

struct bo_setup_state {
	/* Input: */
	struct xe_lrc		*lrc;
	struct xe_hw_engine	*hwe;
	size_t			max_size;
	size_t                  reserve_dw;
	unsigned int		offset;
	const struct bo_setup	*funcs;
	unsigned int		num_funcs;

	/* State: */
	u32			*buffer;
	u32			*ptr;
	unsigned int		written;
};

static int setup_bo(struct bo_setup_state *state)
{
	ssize_t remain;

	if (state->lrc->bo->vmap.is_iomem) {
		if (!state->buffer)
			return -ENOMEM;
		state->ptr = state->buffer;
	} else {
		state->ptr = state->lrc->bo->vmap.vaddr + state->offset;
	}

	remain = state->max_size / sizeof(u32);

	for (size_t i = 0; i < state->num_funcs; i++) {
		ssize_t len = state->funcs[i].setup(state->lrc, state->hwe,
						    state->ptr, remain);

		remain -= len;

		/*
		 * Caller has asked for at least reserve_dw to remain unused.
		 */
		if (len < 0 ||
		    xe_gt_WARN_ON(state->lrc->gt, remain < state->reserve_dw))
			goto fail;

		state->ptr += len;
		state->written += len;
	}

	return 0;

fail:
	return -ENOSPC;
}

static void finish_bo(struct bo_setup_state *state)
{
	if (!state->buffer)
		return;

	xe_map_memcpy_to(gt_to_xe(state->lrc->gt), &state->lrc->bo->vmap,
			 state->offset, state->buffer,
			 state->written * sizeof(u32));
}

/**
 * xe_lrc_setup_wa_bb_with_scratch - Execute all wa bb setup callbacks.
 * @lrc: the &xe_lrc struct instance
 * @hwe: the &xe_hw_engine struct instance
 * @scratch: preallocated scratch buffer for temporary storage
 * Return: 0 on success, negative error code on failure
 */
int xe_lrc_setup_wa_bb_with_scratch(struct xe_lrc *lrc, struct xe_hw_engine *hwe, u32 *scratch)
{
	static const struct bo_setup funcs[] = {
		{ .setup = setup_timestamp_wa },
		{ .setup = setup_invalidate_state_cache_wa },
		{ .setup = setup_utilization_wa },
		{ .setup = setup_configfs_post_ctx_restore_bb },
	};
	struct bo_setup_state state = {
		.lrc = lrc,
		.hwe = hwe,
		.max_size = LRC_WA_BB_SIZE,
		.buffer = scratch,
		.reserve_dw = 1,
		.offset = __xe_lrc_wa_bb_offset(lrc),
		.funcs = funcs,
		.num_funcs = ARRAY_SIZE(funcs),
	};
	int ret;

	ret = setup_bo(&state);
	if (ret)
		return ret;

	*state.ptr++ = MI_BATCH_BUFFER_END;
	state.written++;

	finish_bo(&state);

	xe_lrc_write_ctx_reg(lrc, CTX_BB_PER_CTX_PTR,
			     xe_bo_ggtt_addr(lrc->bo) + state.offset + 1);

	return 0;
}

static int setup_wa_bb(struct xe_lrc *lrc, struct xe_hw_engine *hwe)
{
	u32 *buf = NULL;
	int ret;

	if (lrc->bo->vmap.is_iomem)
		buf = kmalloc(LRC_WA_BB_SIZE, GFP_KERNEL);

	ret = xe_lrc_setup_wa_bb_with_scratch(lrc, hwe, buf);

	kfree(buf);

	return ret;
}

static int
setup_indirect_ctx(struct xe_lrc *lrc, struct xe_hw_engine *hwe)
{
	static const struct bo_setup rcs_funcs[] = {
		{ .setup = setup_timestamp_wa },
		{ .setup = setup_configfs_mid_ctx_restore_bb },
	};
	static const struct bo_setup xcs_funcs[] = {
		{ .setup = setup_configfs_mid_ctx_restore_bb },
	};
	struct bo_setup_state state = {
		.lrc = lrc,
		.hwe = hwe,
		.max_size = (63 * 64) /* max 63 cachelines */,
		.buffer = NULL,
		.offset = __xe_lrc_indirect_ctx_offset(lrc),
	};
	int ret;

	if (!(lrc->flags & XE_LRC_FLAG_INDIRECT_CTX))
		return 0;

	if (hwe->class == XE_ENGINE_CLASS_RENDER ||
	    hwe->class == XE_ENGINE_CLASS_COMPUTE) {
		state.funcs = rcs_funcs;
		state.num_funcs = ARRAY_SIZE(rcs_funcs);
	} else {
		state.funcs = xcs_funcs;
		state.num_funcs = ARRAY_SIZE(xcs_funcs);
	}

	if (xe_gt_WARN_ON(lrc->gt, !state.funcs))
		return 0;

	if (lrc->bo->vmap.is_iomem)
		state.buffer = kmalloc(state.max_size, GFP_KERNEL);

	ret = setup_bo(&state);
	if (ret) {
		kfree(state.buffer);
		return ret;
	}

	/*
	 * Align to 64B cacheline so there's no garbage at the end for CS to
	 * execute: size for indirect ctx must be a multiple of 64.
	 */
	while (state.written & 0xf) {
		*state.ptr++ = MI_NOOP;
		state.written++;
	}

	finish_bo(&state);
	kfree(state.buffer);

	/*
	 * Enable INDIRECT_CTX leaving INDIRECT_CTX_OFFSET at its default: it
	 * varies per engine class, but the default is good enough
	 */
	xe_lrc_write_ctx_reg(lrc,
			     CTX_CS_INDIRECT_CTX,
			     (xe_bo_ggtt_addr(lrc->bo) + state.offset) |
			     /* Size in CLs. */
			     (state.written * sizeof(u32) / 64));

	return 0;
}

static int xe_lrc_init(struct xe_lrc *lrc, struct xe_hw_engine *hwe,
		       struct xe_vm *vm, u32 ring_size, u16 msix_vec,
		       u32 init_flags)
{
	struct xe_gt *gt = hwe->gt;
	const u32 lrc_size = xe_gt_lrc_size(gt, hwe->class);
	u32 bo_size = ring_size + lrc_size + LRC_WA_BB_SIZE;
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_device *xe = gt_to_xe(gt);
	struct iosys_map map;
	u32 arb_enable;
	u32 bo_flags;
	int err;

	kref_init(&lrc->refcount);
	lrc->gt = gt;
	lrc->size = lrc_size;
	lrc->flags = 0;
	lrc->ring.size = ring_size;
	lrc->ring.tail = 0;

	if (gt_engine_needs_indirect_ctx(gt, hwe->class)) {
		lrc->flags |= XE_LRC_FLAG_INDIRECT_CTX;
		bo_size += LRC_INDIRECT_CTX_BO_SIZE;
	}

	if (xe_gt_has_indirect_ring_state(gt))
		lrc->flags |= XE_LRC_FLAG_INDIRECT_RING_STATE;

	bo_flags = XE_BO_FLAG_VRAM_IF_DGFX(tile) | XE_BO_FLAG_GGTT |
		   XE_BO_FLAG_GGTT_INVALIDATE;
	if (vm && vm->xef) /* userspace */
		bo_flags |= XE_BO_FLAG_PINNED_LATE_RESTORE;

	lrc->bo = xe_bo_create_pin_map_novm(xe, tile,
					    bo_size,
					    ttm_bo_type_kernel,
					    bo_flags, false);
	if (IS_ERR(lrc->bo))
		return PTR_ERR(lrc->bo);

	xe_hw_fence_ctx_init(&lrc->fence_ctx, hwe->gt,
			     hwe->fence_irq, hwe->name);

	/*
	 * Init Per-Process of HW status Page, LRC / context state to known
	 * values. If there's already a primed default_lrc, just copy it, otherwise
	 * it's the early submission to record the lrc: build a new empty one from
	 * scratch.
	 */
	map = __xe_lrc_pphwsp_map(lrc);
	if (gt->default_lrc[hwe->class]) {
		xe_map_memset(xe, &map, 0, 0, LRC_PPHWSP_SIZE);	/* PPHWSP */
		xe_map_memcpy_to(xe, &map, LRC_PPHWSP_SIZE,
				 gt->default_lrc[hwe->class] + LRC_PPHWSP_SIZE,
				 lrc_size - LRC_PPHWSP_SIZE);
	} else {
		void *init_data = empty_lrc_data(hwe);

		if (!init_data) {
			err = -ENOMEM;
			goto err_lrc_finish;
		}

		xe_map_memcpy_to(xe, &map, 0, init_data, lrc_size);
		kfree(init_data);
	}

	if (vm) {
		xe_lrc_set_ppgtt(lrc, vm);

		if (vm->xef)
			xe_drm_client_add_bo(vm->xef->client, lrc->bo);
	}

	if (xe_device_has_msix(xe)) {
		xe_lrc_write_ctx_reg(lrc, CTX_INT_STATUS_REPORT_PTR,
				     xe_memirq_status_ptr(&tile->memirq, hwe));
		xe_lrc_write_ctx_reg(lrc, CTX_INT_SRC_REPORT_PTR,
				     xe_memirq_source_ptr(&tile->memirq, hwe));
		xe_lrc_write_ctx_reg(lrc, CTX_CS_INT_VEC_DATA, msix_vec << 16 | msix_vec);
	}

	if (xe_gt_has_indirect_ring_state(gt)) {
		xe_lrc_write_ctx_reg(lrc, CTX_INDIRECT_RING_STATE,
				     __xe_lrc_indirect_ring_ggtt_addr(lrc));

		xe_lrc_write_indirect_ctx_reg(lrc, INDIRECT_CTX_RING_START,
					      __xe_lrc_ring_ggtt_addr(lrc));
		xe_lrc_write_indirect_ctx_reg(lrc, INDIRECT_CTX_RING_START_UDW, 0);
		xe_lrc_write_indirect_ctx_reg(lrc, INDIRECT_CTX_RING_HEAD, 0);
		xe_lrc_write_indirect_ctx_reg(lrc, INDIRECT_CTX_RING_TAIL, lrc->ring.tail);
		xe_lrc_write_indirect_ctx_reg(lrc, INDIRECT_CTX_RING_CTL,
					      RING_CTL_SIZE(lrc->ring.size) | RING_VALID);
	} else {
		xe_lrc_write_ctx_reg(lrc, CTX_RING_START, __xe_lrc_ring_ggtt_addr(lrc));
		xe_lrc_write_ctx_reg(lrc, CTX_RING_HEAD, 0);
		xe_lrc_write_ctx_reg(lrc, CTX_RING_TAIL, lrc->ring.tail);
		xe_lrc_write_ctx_reg(lrc, CTX_RING_CTL,
				     RING_CTL_SIZE(lrc->ring.size) | RING_VALID);
	}

	if (init_flags & XE_LRC_CREATE_RUNALONE)
		xe_lrc_write_ctx_reg(lrc, CTX_CONTEXT_CONTROL,
				     xe_lrc_read_ctx_reg(lrc, CTX_CONTEXT_CONTROL) |
				     _MASKED_BIT_ENABLE(CTX_CTRL_RUN_ALONE));

	if (init_flags & XE_LRC_CREATE_PXP)
		xe_lrc_write_ctx_reg(lrc, CTX_CONTEXT_CONTROL,
				     xe_lrc_read_ctx_reg(lrc, CTX_CONTEXT_CONTROL) |
				     _MASKED_BIT_ENABLE(CTX_CTRL_PXP_ENABLE));

	lrc->ctx_timestamp = 0;
	xe_lrc_write_ctx_reg(lrc, CTX_TIMESTAMP, 0);
	if (lrc_to_xe(lrc)->info.has_64bit_timestamp)
		xe_lrc_write_ctx_reg(lrc, CTX_TIMESTAMP_UDW, 0);

	if (xe->info.has_asid && vm)
		xe_lrc_write_ctx_reg(lrc, CTX_ASID, vm->usm.asid);

	lrc->desc = LRC_VALID;
	lrc->desc |= FIELD_PREP(LRC_ADDRESSING_MODE, LRC_LEGACY_64B_CONTEXT);
	/* TODO: Priority */

	/* While this appears to have something about privileged batches or
	 * some such, it really just means PPGTT mode.
	 */
	if (vm)
		lrc->desc |= LRC_PRIVILEGE;

	if (GRAPHICS_VERx100(xe) < 1250) {
		lrc->desc |= FIELD_PREP(LRC_ENGINE_INSTANCE, hwe->instance);
		lrc->desc |= FIELD_PREP(LRC_ENGINE_CLASS, hwe->class);
	}

	arb_enable = MI_ARB_ON_OFF | MI_ARB_ENABLE;
	xe_lrc_write_ring(lrc, &arb_enable, sizeof(arb_enable));

	map = __xe_lrc_seqno_map(lrc);
	xe_map_write32(lrc_to_xe(lrc), &map, lrc->fence_ctx.next_seqno - 1);

	map = __xe_lrc_start_seqno_map(lrc);
	xe_map_write32(lrc_to_xe(lrc), &map, lrc->fence_ctx.next_seqno - 1);

	err = setup_wa_bb(lrc, hwe);
	if (err)
		goto err_lrc_finish;

	err = setup_indirect_ctx(lrc, hwe);
	if (err)
		goto err_lrc_finish;

	return 0;

err_lrc_finish:
	xe_lrc_finish(lrc);
	return err;
}

/**
 * xe_lrc_create - Create a LRC
 * @hwe: Hardware Engine
 * @vm: The VM (address space)
 * @ring_size: LRC ring size
 * @msix_vec: MSI-X interrupt vector (for platforms that support it)
 * @flags: LRC initialization flags
 *
 * Allocate and initialize the Logical Ring Context (LRC).
 *
 * Return pointer to created LRC upon success and an error pointer
 * upon failure.
 */
struct xe_lrc *xe_lrc_create(struct xe_hw_engine *hwe, struct xe_vm *vm,
			     u32 ring_size, u16 msix_vec, u32 flags)
{
	struct xe_lrc *lrc;
	int err;

	lrc = kzalloc(sizeof(*lrc), GFP_KERNEL);
	if (!lrc)
		return ERR_PTR(-ENOMEM);

	err = xe_lrc_init(lrc, hwe, vm, ring_size, msix_vec, flags);
	if (err) {
		kfree(lrc);
		return ERR_PTR(err);
	}

	return lrc;
}

/**
 * xe_lrc_destroy - Destroy the LRC
 * @ref: reference to LRC
 *
 * Called when ref == 0, release resources held by the Logical Ring Context
 * (LRC) and free the LRC memory.
 */
void xe_lrc_destroy(struct kref *ref)
{
	struct xe_lrc *lrc = container_of(ref, struct xe_lrc, refcount);

	xe_lrc_finish(lrc);
	kfree(lrc);
}

/**
 * xe_lrc_update_hwctx_regs_with_address - Re-compute GGTT references within given LRC.
 * @lrc: the &xe_lrc struct instance
 */
void xe_lrc_update_hwctx_regs_with_address(struct xe_lrc *lrc)
{
	if (xe_lrc_has_indirect_ring_state(lrc)) {
		xe_lrc_write_ctx_reg(lrc, CTX_INDIRECT_RING_STATE,
				     __xe_lrc_indirect_ring_ggtt_addr(lrc));

		xe_lrc_write_indirect_ctx_reg(lrc, INDIRECT_CTX_RING_START,
					      __xe_lrc_ring_ggtt_addr(lrc));
	} else {
		xe_lrc_write_ctx_reg(lrc, CTX_RING_START, __xe_lrc_ring_ggtt_addr(lrc));
	}
}

void xe_lrc_set_ring_tail(struct xe_lrc *lrc, u32 tail)
{
	if (xe_lrc_has_indirect_ring_state(lrc))
		xe_lrc_write_indirect_ctx_reg(lrc, INDIRECT_CTX_RING_TAIL, tail);
	else
		xe_lrc_write_ctx_reg(lrc, CTX_RING_TAIL, tail);
}

u32 xe_lrc_ring_tail(struct xe_lrc *lrc)
{
	if (xe_lrc_has_indirect_ring_state(lrc))
		return xe_lrc_read_indirect_ctx_reg(lrc, INDIRECT_CTX_RING_TAIL) & TAIL_ADDR;
	else
		return xe_lrc_read_ctx_reg(lrc, CTX_RING_TAIL) & TAIL_ADDR;
}

static u32 xe_lrc_ring_start(struct xe_lrc *lrc)
{
	if (xe_lrc_has_indirect_ring_state(lrc))
		return xe_lrc_read_indirect_ctx_reg(lrc, INDIRECT_CTX_RING_START);
	else
		return xe_lrc_read_ctx_reg(lrc, CTX_RING_START);
}

void xe_lrc_set_ring_head(struct xe_lrc *lrc, u32 head)
{
	if (xe_lrc_has_indirect_ring_state(lrc))
		xe_lrc_write_indirect_ctx_reg(lrc, INDIRECT_CTX_RING_HEAD, head);
	else
		xe_lrc_write_ctx_reg(lrc, CTX_RING_HEAD, head);
}

u32 xe_lrc_ring_head(struct xe_lrc *lrc)
{
	if (xe_lrc_has_indirect_ring_state(lrc))
		return xe_lrc_read_indirect_ctx_reg(lrc, INDIRECT_CTX_RING_HEAD) & HEAD_ADDR;
	else
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

/**
 * xe_lrc_alloc_seqno_fence() - Allocate an lrc seqno fence.
 *
 * Allocate but don't initialize an lrc seqno fence.
 *
 * Return: Pointer to the allocated fence or
 * negative error pointer on error.
 */
struct dma_fence *xe_lrc_alloc_seqno_fence(void)
{
	return xe_hw_fence_alloc();
}

/**
 * xe_lrc_free_seqno_fence() - Free an lrc seqno fence.
 * @fence: Pointer to the fence to free.
 *
 * Frees an lrc seqno fence that hasn't yet been
 * initialized.
 */
void xe_lrc_free_seqno_fence(struct dma_fence *fence)
{
	xe_hw_fence_free(fence);
}

/**
 * xe_lrc_init_seqno_fence() - Initialize an lrc seqno fence.
 * @lrc: Pointer to the lrc.
 * @fence: Pointer to the fence to initialize.
 *
 * Initializes a pre-allocated lrc seqno fence.
 * After initialization, the fence is subject to normal
 * dma-fence refcounting.
 */
void xe_lrc_init_seqno_fence(struct xe_lrc *lrc, struct dma_fence *fence)
{
	xe_hw_fence_init(fence, &lrc->fence_ctx, __xe_lrc_seqno_map(lrc));
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

/**
 * xe_lrc_engine_id() - Read engine id value
 * @lrc: Pointer to the lrc.
 *
 * Returns: context id value
 */
static u32 xe_lrc_engine_id(struct xe_lrc *lrc)
{
	struct xe_device *xe = lrc_to_xe(lrc);
	struct iosys_map map;

	map = __xe_lrc_engine_id_map(lrc);
	return xe_map_read32(xe, &map);
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
	MATCH(STATE_SYSTEM_MEM_FENCE_ADDRESS);
	MATCH(STATE_CONTEXT_DATA_BASE_ADDRESS);

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
	MATCH3D(3DSTATE_CONSTANT_PS);
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
	MATCH3D(3DSTATE_COARSE_PIXEL);

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

static int dump_gfx_state_command(struct drm_printer *p,
				  struct xe_gt *gt,
				  u32 *dw,
				  int remaining_dw)
{
	u32 numdw = instr_dw(*dw);
	u32 opcode = REG_FIELD_GET(GFX_STATE_OPCODE, *dw);

	/*
	 * Make sure we haven't mis-parsed a number of dwords that exceeds the
	 * remaining size of the LRC.
	 */
	if (xe_gt_WARN_ON(gt, numdw > remaining_dw))
		numdw = remaining_dw;

	switch (*dw & (XE_INSTR_GFX_STATE | GFX_STATE_OPCODE)) {
	MATCH(STATE_WRITE_INLINE);

	default:
		drm_printf(p, "[%#010x] unknown GFX_STATE command (opcode=%#x), likely %d dwords\n",
			   *dw, opcode, numdw);
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
	remaining_dw = (xe_gt_lrc_size(gt, hwe_class) - LRC_PPHWSP_SIZE) / 4;

	while (remaining_dw > 0) {
		if ((*dw & XE_INSTR_CMD_TYPE) == XE_INSTR_MI) {
			num_dw = dump_mi_command(p, gt, dw, remaining_dw);
		} else if ((*dw & XE_INSTR_CMD_TYPE) == XE_INSTR_GFXPIPE) {
			num_dw = dump_gfxpipe_command(p, gt, dw, remaining_dw);
		} else if ((*dw & XE_INSTR_CMD_TYPE) == XE_INSTR_GFX_STATE) {
			num_dw = dump_gfx_state_command(p, gt, dw, remaining_dw);
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

u32 *xe_lrc_emit_hwe_state_instructions(struct xe_exec_queue *q, u32 *cs)
{
	struct xe_gt *gt = q->hwe->gt;
	struct xe_device *xe = gt_to_xe(gt);
	const struct instr_state *state_table = NULL;
	int state_table_size = 0;

	/*
	 * Wa_14019789679
	 *
	 * If the driver doesn't explicitly emit the SVG instructions while
	 * setting up the default LRC, the context switch will write 0's
	 * (noops) into the LRC memory rather than the expected instruction
	 * headers.  Application contexts start out as a copy of the default
	 * LRC, and if they also do not emit specific settings for some SVG
	 * state, then on context restore they'll unintentionally inherit
	 * whatever state setting the previous context had programmed into the
	 * hardware (i.e., the lack of a 3DSTATE_* instruction in the LRC will
	 * prevent the hardware from resetting that state back to any specific
	 * value).
	 *
	 * The official workaround only requires emitting 3DSTATE_MESH_CONTROL
	 * since that's a specific state setting that can easily cause GPU
	 * hangs if unintentionally inherited.  However to be safe we'll
	 * continue to emit all of the SVG state since it's best not to leak
	 * any of the state between contexts, even if that leakage is harmless.
	 */
	if (XE_GT_WA(gt, 14019789679) && q->hwe->class == XE_ENGINE_CLASS_RENDER) {
		state_table = xe_hpg_svg_state;
		state_table_size = ARRAY_SIZE(xe_hpg_svg_state);
	}

	if (!state_table) {
		xe_gt_dbg(gt, "No non-register state to emit on graphics ver %d.%02d\n",
			  GRAPHICS_VER(xe), GRAPHICS_VERx100(xe) % 100);
		return cs;
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

		*cs = instr;
		if (!is_single_dw)
			*cs |= (num_dw - 2);

		cs += num_dw;
	}

	return cs;
}

struct xe_lrc_snapshot *xe_lrc_snapshot_capture(struct xe_lrc *lrc)
{
	struct xe_lrc_snapshot *snapshot = kmalloc(sizeof(*snapshot), GFP_NOWAIT);

	if (!snapshot)
		return NULL;

	snapshot->context_desc = xe_lrc_ggtt_addr(lrc);
	snapshot->ring_addr = __xe_lrc_ring_ggtt_addr(lrc);
	snapshot->indirect_context_desc = xe_lrc_indirect_ring_ggtt_addr(lrc);
	snapshot->head = xe_lrc_ring_head(lrc);
	snapshot->tail.internal = lrc->ring.tail;
	snapshot->tail.memory = xe_lrc_ring_tail(lrc);
	snapshot->start = xe_lrc_ring_start(lrc);
	snapshot->start_seqno = xe_lrc_start_seqno(lrc);
	snapshot->seqno = xe_lrc_seqno(lrc);
	snapshot->lrc_bo = xe_bo_get(lrc->bo);
	snapshot->lrc_offset = xe_lrc_pphwsp_offset(lrc);
	snapshot->lrc_size = lrc->size;
	snapshot->lrc_snapshot = NULL;
	snapshot->ctx_timestamp = lower_32_bits(xe_lrc_ctx_timestamp(lrc));
	snapshot->ctx_job_timestamp = xe_lrc_ctx_job_timestamp(lrc);
	return snapshot;
}

void xe_lrc_snapshot_capture_delayed(struct xe_lrc_snapshot *snapshot)
{
	struct xe_bo *bo;
	struct iosys_map src;

	if (!snapshot)
		return;

	bo = snapshot->lrc_bo;
	snapshot->lrc_bo = NULL;

	snapshot->lrc_snapshot = kvmalloc(snapshot->lrc_size, GFP_KERNEL);
	if (!snapshot->lrc_snapshot)
		goto put_bo;

	xe_bo_lock(bo, false);
	if (!ttm_bo_vmap(&bo->ttm, &src)) {
		xe_map_memcpy_from(xe_bo_device(bo),
				   snapshot->lrc_snapshot, &src, snapshot->lrc_offset,
				   snapshot->lrc_size);
		ttm_bo_vunmap(&bo->ttm, &src);
	} else {
		kvfree(snapshot->lrc_snapshot);
		snapshot->lrc_snapshot = NULL;
	}
	xe_bo_unlock(bo);
put_bo:
	xe_bo_put(bo);
}

void xe_lrc_snapshot_print(struct xe_lrc_snapshot *snapshot, struct drm_printer *p)
{
	unsigned long i;

	if (!snapshot)
		return;

	drm_printf(p, "\tHW Context Desc: 0x%08x\n", snapshot->context_desc);
	drm_printf(p, "\tHW Ring address: 0x%08x\n",
		   snapshot->ring_addr);
	drm_printf(p, "\tHW Indirect Ring State: 0x%08x\n",
		   snapshot->indirect_context_desc);
	drm_printf(p, "\tLRC Head: (memory) %u\n", snapshot->head);
	drm_printf(p, "\tLRC Tail: (internal) %u, (memory) %u\n",
		   snapshot->tail.internal, snapshot->tail.memory);
	drm_printf(p, "\tRing start: (memory) 0x%08x\n", snapshot->start);
	drm_printf(p, "\tStart seqno: (memory) %d\n", snapshot->start_seqno);
	drm_printf(p, "\tSeqno: (memory) %d\n", snapshot->seqno);
	drm_printf(p, "\tTimestamp: 0x%08x\n", snapshot->ctx_timestamp);
	drm_printf(p, "\tJob Timestamp: 0x%08x\n", snapshot->ctx_job_timestamp);

	if (!snapshot->lrc_snapshot)
		return;

	drm_printf(p, "\t[HWSP].length: 0x%x\n", LRC_PPHWSP_SIZE);
	drm_puts(p, "\t[HWSP].data: ");
	for (i = 0; i < LRC_PPHWSP_SIZE; i += sizeof(u32)) {
		u32 *val = snapshot->lrc_snapshot + i;
		char dumped[ASCII85_BUFSZ];

		drm_puts(p, ascii85_encode(*val, dumped));
	}

	drm_printf(p, "\n\t[HWCTX].length: 0x%lx\n", snapshot->lrc_size - LRC_PPHWSP_SIZE);
	drm_puts(p, "\t[HWCTX].data: ");
	for (; i < snapshot->lrc_size; i += sizeof(u32)) {
		u32 *val = snapshot->lrc_snapshot + i;
		char dumped[ASCII85_BUFSZ];

		drm_puts(p, ascii85_encode(*val, dumped));
	}
	drm_puts(p, "\n");
}

void xe_lrc_snapshot_free(struct xe_lrc_snapshot *snapshot)
{
	if (!snapshot)
		return;

	kvfree(snapshot->lrc_snapshot);
	if (snapshot->lrc_bo)
		xe_bo_put(snapshot->lrc_bo);

	kfree(snapshot);
}

static int get_ctx_timestamp(struct xe_lrc *lrc, u32 engine_id, u64 *reg_ctx_ts)
{
	u16 class = REG_FIELD_GET(ENGINE_CLASS_ID, engine_id);
	u16 instance = REG_FIELD_GET(ENGINE_INSTANCE_ID, engine_id);
	struct xe_hw_engine *hwe;
	u64 val;

	hwe = xe_gt_hw_engine(lrc->gt, class, instance, false);
	if (xe_gt_WARN_ONCE(lrc->gt, !hwe || xe_hw_engine_is_reserved(hwe),
			    "Unexpected engine class:instance %d:%d for context utilization\n",
			    class, instance))
		return -1;

	if (lrc_to_xe(lrc)->info.has_64bit_timestamp)
		val = xe_mmio_read64_2x32(&hwe->gt->mmio,
					  RING_CTX_TIMESTAMP(hwe->mmio_base));
	else
		val = xe_mmio_read32(&hwe->gt->mmio,
				     RING_CTX_TIMESTAMP(hwe->mmio_base));

	*reg_ctx_ts = val;

	return 0;
}

/**
 * xe_lrc_update_timestamp() - Update ctx timestamp
 * @lrc: Pointer to the lrc.
 * @old_ts: Old timestamp value
 *
 * Populate @old_ts current saved ctx timestamp, read new ctx timestamp and
 * update saved value. With support for active contexts, the calculation may be
 * slightly racy, so follow a read-again logic to ensure that the context is
 * still active before returning the right timestamp.
 *
 * Returns: New ctx timestamp value
 */
u64 xe_lrc_update_timestamp(struct xe_lrc *lrc, u64 *old_ts)
{
	u64 lrc_ts, reg_ts;
	u32 engine_id;

	*old_ts = lrc->ctx_timestamp;

	lrc_ts = xe_lrc_ctx_timestamp(lrc);
	/* CTX_TIMESTAMP mmio read is invalid on VF, so return the LRC value */
	if (IS_SRIOV_VF(lrc_to_xe(lrc))) {
		lrc->ctx_timestamp = lrc_ts;
		goto done;
	}

	if (lrc_ts == CONTEXT_ACTIVE) {
		engine_id = xe_lrc_engine_id(lrc);
		if (!get_ctx_timestamp(lrc, engine_id, &reg_ts))
			lrc->ctx_timestamp = reg_ts;

		/* read lrc again to ensure context is still active */
		lrc_ts = xe_lrc_ctx_timestamp(lrc);
	}

	/*
	 * If context switched out, just use the lrc_ts. Note that this needs to
	 * be a separate if condition.
	 */
	if (lrc_ts != CONTEXT_ACTIVE)
		lrc->ctx_timestamp = lrc_ts;

done:
	trace_xe_lrc_update_timestamp(lrc, *old_ts);

	return lrc->ctx_timestamp;
}

/**
 * xe_lrc_ring_is_idle() - LRC is idle
 * @lrc: Pointer to the lrc.
 *
 * Compare LRC ring head and tail to determine if idle.
 *
 * Return: True is ring is idle, False otherwise
 */
bool xe_lrc_ring_is_idle(struct xe_lrc *lrc)
{
	return xe_lrc_ring_head(lrc) == xe_lrc_ring_tail(lrc);
}
