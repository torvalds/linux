// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_ring_ops.h"

#include "regs/xe_gpu_commands.h"
#include "regs/xe_gt_regs.h"
#include "regs/xe_lrc_layout.h"
#include "regs/xe_regs.h"
#include "xe_engine_types.h"
#include "xe_gt.h"
#include "xe_lrc.h"
#include "xe_macros.h"
#include "xe_sched_job.h"
#include "xe_vm_types.h"

/*
 * 3D-related flags that can't be set on _engines_ that lack access to the 3D
 * pipeline (i.e., CCS engines).
 */
#define PIPE_CONTROL_3D_ENGINE_FLAGS (\
		PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH | \
		PIPE_CONTROL_DEPTH_CACHE_FLUSH | \
		PIPE_CONTROL_TILE_CACHE_FLUSH | \
		PIPE_CONTROL_DEPTH_STALL | \
		PIPE_CONTROL_STALL_AT_SCOREBOARD | \
		PIPE_CONTROL_PSD_SYNC | \
		PIPE_CONTROL_AMFS_FLUSH | \
		PIPE_CONTROL_VF_CACHE_INVALIDATE | \
		PIPE_CONTROL_GLOBAL_SNAPSHOT_RESET)

/* 3D-related flags that can't be set on _platforms_ that lack a 3D pipeline */
#define PIPE_CONTROL_3D_ARCH_FLAGS ( \
		PIPE_CONTROL_3D_ENGINE_FLAGS | \
		PIPE_CONTROL_INDIRECT_STATE_DISABLE | \
		PIPE_CONTROL_FLUSH_ENABLE | \
		PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE | \
		PIPE_CONTROL_DC_FLUSH_ENABLE)

static u32 preparser_disable(bool state)
{
	return MI_ARB_CHECK | BIT(8) | state;
}

static int emit_aux_table_inv(struct xe_gt *gt, u32 addr, u32 *dw, int i)
{
	dw[i++] = MI_LOAD_REGISTER_IMM(1) | MI_LRI_MMIO_REMAP_EN;
	dw[i++] = addr + gt->mmio.adj_offset;
	dw[i++] = AUX_INV;
	dw[i++] = MI_NOOP;

	return i;
}

static int emit_user_interrupt(u32 *dw, int i)
{
	dw[i++] = MI_USER_INTERRUPT;
	dw[i++] = MI_ARB_ON_OFF | MI_ARB_ENABLE;
	dw[i++] = MI_ARB_CHECK;

	return i;
}

static int emit_store_imm_ggtt(u32 addr, u32 value, u32 *dw, int i)
{
	dw[i++] = MI_STORE_DATA_IMM | BIT(22) /* GGTT */ | 2;
	dw[i++] = addr;
	dw[i++] = 0;
	dw[i++] = value;

	return i;
}

static int emit_flush_imm_ggtt(u32 addr, u32 value, u32 *dw, int i)
{
	dw[i++] = (MI_FLUSH_DW + 1) | MI_FLUSH_DW_OP_STOREDW;
	dw[i++] = addr | MI_FLUSH_DW_USE_GTT;
	dw[i++] = 0;
	dw[i++] = value;

	return i;
}

static int emit_bb_start(u64 batch_addr, u32 ppgtt_flag, u32 *dw, int i)
{
	dw[i++] = MI_BATCH_BUFFER_START | ppgtt_flag;
	dw[i++] = lower_32_bits(batch_addr);
	dw[i++] = upper_32_bits(batch_addr);

	return i;
}

static int emit_flush_invalidate(u32 flag, u32 *dw, int i)
{
	dw[i] = MI_FLUSH_DW + 1;
	dw[i] |= flag;
	dw[i++] |= MI_INVALIDATE_TLB | MI_FLUSH_DW_OP_STOREDW |
		MI_FLUSH_DW_STORE_INDEX;

	dw[i++] = LRC_PPHWSP_SCRATCH_ADDR | MI_FLUSH_DW_USE_GTT;
	dw[i++] = 0;
	dw[i++] = ~0U;

	return i;
}

static int emit_pipe_invalidate(u32 mask_flags, u32 *dw, int i)
{
	u32 flags = PIPE_CONTROL_CS_STALL |
		PIPE_CONTROL_COMMAND_CACHE_INVALIDATE |
		PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE |
		PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE |
		PIPE_CONTROL_VF_CACHE_INVALIDATE |
		PIPE_CONTROL_CONST_CACHE_INVALIDATE |
		PIPE_CONTROL_STATE_CACHE_INVALIDATE |
		PIPE_CONTROL_QW_WRITE |
		PIPE_CONTROL_STORE_DATA_INDEX;

	flags &= ~mask_flags;

	dw[i++] = GFX_OP_PIPE_CONTROL(6);
	dw[i++] = flags;
	dw[i++] = LRC_PPHWSP_SCRATCH_ADDR;
	dw[i++] = 0;
	dw[i++] = 0;
	dw[i++] = 0;

	return i;
}

#define MI_STORE_QWORD_IMM_GEN8_POSTED (MI_INSTR(0x20, 3) | (1 << 21))

static int emit_store_imm_ppgtt_posted(u64 addr, u64 value,
				       u32 *dw, int i)
{
	dw[i++] = MI_STORE_QWORD_IMM_GEN8_POSTED;
	dw[i++] = lower_32_bits(addr);
	dw[i++] = upper_32_bits(addr);
	dw[i++] = lower_32_bits(value);
	dw[i++] = upper_32_bits(value);

	return i;
}

static int emit_pipe_imm_ggtt(u32 addr, u32 value, bool stall_only, u32 *dw,
			      int i)
{
	dw[i++] = GFX_OP_PIPE_CONTROL(6);
	dw[i++] = (stall_only ? PIPE_CONTROL_CS_STALL :
		   PIPE_CONTROL_FLUSH_ENABLE | PIPE_CONTROL_CS_STALL) |
		PIPE_CONTROL_GLOBAL_GTT_IVB | PIPE_CONTROL_QW_WRITE;
	dw[i++] = addr;
	dw[i++] = 0;
	dw[i++] = value;
	dw[i++] = 0; /* We're thrashing one extra dword. */

	return i;
}

static u32 get_ppgtt_flag(struct xe_sched_job *job)
{
	return !(job->engine->flags & ENGINE_FLAG_WA) ? BIT(8) : 0;
}

static void __emit_job_gen12_copy(struct xe_sched_job *job, struct xe_lrc *lrc,
				  u64 batch_addr, u32 seqno)
{
	u32 dw[MAX_JOB_SIZE_DW], i = 0;
	u32 ppgtt_flag = get_ppgtt_flag(job);

	i = emit_store_imm_ggtt(xe_lrc_start_seqno_ggtt_addr(lrc),
				seqno, dw, i);

	i = emit_bb_start(batch_addr, ppgtt_flag, dw, i);

	if (job->user_fence.used)
		i = emit_store_imm_ppgtt_posted(job->user_fence.addr,
						job->user_fence.value,
						dw, i);

	i = emit_flush_imm_ggtt(xe_lrc_seqno_ggtt_addr(lrc), seqno, dw, i);

	i = emit_user_interrupt(dw, i);

	XE_BUG_ON(i > MAX_JOB_SIZE_DW);

	xe_lrc_write_ring(lrc, dw, i * sizeof(*dw));
}

static void __emit_job_gen12_video(struct xe_sched_job *job, struct xe_lrc *lrc,
				   u64 batch_addr, u32 seqno)
{
	u32 dw[MAX_JOB_SIZE_DW], i = 0;
	u32 ppgtt_flag = get_ppgtt_flag(job);
	struct xe_gt *gt = job->engine->gt;
	struct xe_device *xe = gt_to_xe(gt);
	bool decode = job->engine->class == XE_ENGINE_CLASS_VIDEO_DECODE;

	dw[i++] = preparser_disable(true);

	/* hsdes: 1809175790 */
	if (!xe->info.has_flat_ccs) {
		if (decode)
			i = emit_aux_table_inv(gt, VD0_AUX_INV.reg, dw, i);
		else
			i = emit_aux_table_inv(gt, VE0_AUX_INV.reg, dw, i);
	}
	dw[i++] = preparser_disable(false);

	i = emit_store_imm_ggtt(xe_lrc_start_seqno_ggtt_addr(lrc),
				seqno, dw, i);

	i = emit_bb_start(batch_addr, ppgtt_flag, dw, i);

	if (job->user_fence.used)
		i = emit_store_imm_ppgtt_posted(job->user_fence.addr,
						job->user_fence.value,
						dw, i);

	i = emit_flush_imm_ggtt(xe_lrc_seqno_ggtt_addr(lrc), seqno, dw, i);

	i = emit_user_interrupt(dw, i);

	XE_BUG_ON(i > MAX_JOB_SIZE_DW);

	xe_lrc_write_ring(lrc, dw, i * sizeof(*dw));
}

static void __emit_job_gen12_render_compute(struct xe_sched_job *job,
					    struct xe_lrc *lrc,
					    u64 batch_addr, u32 seqno)
{
	u32 dw[MAX_JOB_SIZE_DW], i = 0;
	u32 ppgtt_flag = get_ppgtt_flag(job);
	struct xe_gt *gt = job->engine->gt;
	struct xe_device *xe = gt_to_xe(gt);
	bool pvc = xe->info.platform == XE_PVC;
	u32 mask_flags = 0;

	dw[i++] = preparser_disable(true);
	if (pvc)
		mask_flags = PIPE_CONTROL_3D_ARCH_FLAGS;
	else if (job->engine->class == XE_ENGINE_CLASS_COMPUTE)
		mask_flags = PIPE_CONTROL_3D_ENGINE_FLAGS;
	i = emit_pipe_invalidate(mask_flags, dw, i);

	/* hsdes: 1809175790 */
	if (!xe->info.has_flat_ccs)
		i = emit_aux_table_inv(gt, CCS_AUX_INV.reg, dw, i);

	dw[i++] = preparser_disable(false);

	i = emit_store_imm_ggtt(xe_lrc_start_seqno_ggtt_addr(lrc),
				seqno, dw, i);

	i = emit_bb_start(batch_addr, ppgtt_flag, dw, i);

	if (job->user_fence.used)
		i = emit_store_imm_ppgtt_posted(job->user_fence.addr,
						job->user_fence.value,
						dw, i);

	i = emit_pipe_imm_ggtt(xe_lrc_seqno_ggtt_addr(lrc), seqno, pvc, dw, i);

	i = emit_user_interrupt(dw, i);

	XE_BUG_ON(i > MAX_JOB_SIZE_DW);

	xe_lrc_write_ring(lrc, dw, i * sizeof(*dw));
}

static void emit_migration_job_gen12(struct xe_sched_job *job,
				     struct xe_lrc *lrc, u32 seqno)
{
	u32 dw[MAX_JOB_SIZE_DW], i = 0;

	i = emit_store_imm_ggtt(xe_lrc_start_seqno_ggtt_addr(lrc),
				seqno, dw, i);

	i = emit_bb_start(job->batch_addr[0], BIT(8), dw, i);

	/* XXX: Do we need this? Leaving for now. */
	dw[i++] = preparser_disable(true);
	i = emit_flush_invalidate(0, dw, i);
	dw[i++] = preparser_disable(false);

	i = emit_bb_start(job->batch_addr[1], BIT(8), dw, i);

	dw[i++] = (MI_FLUSH_DW | MI_INVALIDATE_TLB | job->migrate_flush_flags |
		   MI_FLUSH_DW_OP_STOREDW) + 1;
	dw[i++] = xe_lrc_seqno_ggtt_addr(lrc) | MI_FLUSH_DW_USE_GTT;
	dw[i++] = 0;
	dw[i++] = seqno; /* value */

	i = emit_user_interrupt(dw, i);

	XE_BUG_ON(i > MAX_JOB_SIZE_DW);

	xe_lrc_write_ring(lrc, dw, i * sizeof(*dw));
}

static void emit_job_gen12_copy(struct xe_sched_job *job)
{
	int i;

	if (xe_sched_job_is_migration(job->engine)) {
		emit_migration_job_gen12(job, job->engine->lrc,
					 xe_sched_job_seqno(job));
		return;
	}

	for (i = 0; i < job->engine->width; ++i)
		__emit_job_gen12_copy(job, job->engine->lrc + i,
				      job->batch_addr[i],
				      xe_sched_job_seqno(job));
}

static void emit_job_gen12_video(struct xe_sched_job *job)
{
	int i;

	/* FIXME: Not doing parallel handshake for now */
	for (i = 0; i < job->engine->width; ++i)
		__emit_job_gen12_video(job, job->engine->lrc + i,
				       job->batch_addr[i],
				       xe_sched_job_seqno(job));
}

static void emit_job_gen12_render_compute(struct xe_sched_job *job)
{
	int i;

	for (i = 0; i < job->engine->width; ++i)
		__emit_job_gen12_render_compute(job, job->engine->lrc + i,
						job->batch_addr[i],
						xe_sched_job_seqno(job));
}

static const struct xe_ring_ops ring_ops_gen12_copy = {
	.emit_job = emit_job_gen12_copy,
};

static const struct xe_ring_ops ring_ops_gen12_video = {
	.emit_job = emit_job_gen12_video,
};

static const struct xe_ring_ops ring_ops_gen12_render_compute = {
	.emit_job = emit_job_gen12_render_compute,
};

const struct xe_ring_ops *
xe_ring_ops_get(struct xe_gt *gt, enum xe_engine_class class)
{
	switch (class) {
	case XE_ENGINE_CLASS_COPY:
		return &ring_ops_gen12_copy;
	case XE_ENGINE_CLASS_VIDEO_DECODE:
	case XE_ENGINE_CLASS_VIDEO_ENHANCE:
		return &ring_ops_gen12_video;
	case XE_ENGINE_CLASS_RENDER:
	case XE_ENGINE_CLASS_COMPUTE:
		return &ring_ops_gen12_render_compute;
	default:
		return NULL;
	}
}
