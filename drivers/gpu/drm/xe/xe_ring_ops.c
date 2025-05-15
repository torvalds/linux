// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_ring_ops.h"

#include <generated/xe_wa_oob.h>

#include "instructions/xe_gpu_commands.h"
#include "instructions/xe_mi_commands.h"
#include "regs/xe_engine_regs.h"
#include "regs/xe_gt_regs.h"
#include "regs/xe_lrc_layout.h"
#include "xe_exec_queue_types.h"
#include "xe_gt.h"
#include "xe_lrc.h"
#include "xe_macros.h"
#include "xe_sched_job.h"
#include "xe_sriov.h"
#include "xe_vm_types.h"
#include "xe_vm.h"
#include "xe_wa.h"

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

static int emit_aux_table_inv(struct xe_gt *gt, struct xe_reg reg,
			      u32 *dw, int i)
{
	dw[i++] = MI_LOAD_REGISTER_IMM | MI_LRI_NUM_REGS(1) | MI_LRI_MMIO_REMAP_EN;
	dw[i++] = reg.addr + gt->mmio.adj_offset;
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
	dw[i++] = MI_STORE_DATA_IMM | MI_SDI_GGTT | MI_SDI_NUM_DW(1);
	dw[i++] = addr;
	dw[i++] = 0;
	dw[i++] = value;

	return i;
}

static int emit_flush_dw(u32 *dw, int i)
{
	dw[i++] = MI_FLUSH_DW | MI_FLUSH_IMM_DW;
	dw[i++] = 0;
	dw[i++] = 0;
	dw[i++] = 0;

	return i;
}

static int emit_flush_imm_ggtt(u32 addr, u32 value, u32 flags, u32 *dw, int i)
{
	dw[i++] = MI_FLUSH_DW | MI_FLUSH_DW_OP_STOREDW | MI_FLUSH_IMM_DW |
		  flags;
	dw[i++] = addr | MI_FLUSH_DW_USE_GTT;
	dw[i++] = 0;
	dw[i++] = value;

	return i;
}

static int emit_bb_start(u64 batch_addr, u32 ppgtt_flag, u32 *dw, int i)
{
	dw[i++] = MI_BATCH_BUFFER_START | ppgtt_flag | XE_INSTR_NUM_DW(3);
	dw[i++] = lower_32_bits(batch_addr);
	dw[i++] = upper_32_bits(batch_addr);

	return i;
}

static int emit_flush_invalidate(u32 *dw, int i)
{
	dw[i++] = MI_FLUSH_DW | MI_INVALIDATE_TLB | MI_FLUSH_DW_OP_STOREDW |
		  MI_FLUSH_IMM_DW | MI_FLUSH_DW_STORE_INDEX;
	dw[i++] = LRC_PPHWSP_FLUSH_INVAL_SCRATCH_ADDR;
	dw[i++] = 0;
	dw[i++] = 0;

	return i;
}

static int
emit_pipe_control(u32 *dw, int i, u32 bit_group_0, u32 bit_group_1, u32 offset, u32 value)
{
	dw[i++] = GFX_OP_PIPE_CONTROL(6) | bit_group_0;
	dw[i++] = bit_group_1;
	dw[i++] = offset;
	dw[i++] = 0;
	dw[i++] = value;
	dw[i++] = 0;

	return i;
}

static int emit_pipe_invalidate(u32 mask_flags, bool invalidate_tlb, u32 *dw,
				int i)
{
	u32 flags0 = 0;
	u32 flags1 = PIPE_CONTROL_CS_STALL |
		PIPE_CONTROL_COMMAND_CACHE_INVALIDATE |
		PIPE_CONTROL_INSTRUCTION_CACHE_INVALIDATE |
		PIPE_CONTROL_TEXTURE_CACHE_INVALIDATE |
		PIPE_CONTROL_VF_CACHE_INVALIDATE |
		PIPE_CONTROL_CONST_CACHE_INVALIDATE |
		PIPE_CONTROL_STATE_CACHE_INVALIDATE |
		PIPE_CONTROL_QW_WRITE |
		PIPE_CONTROL_STORE_DATA_INDEX;

	if (invalidate_tlb)
		flags1 |= PIPE_CONTROL_TLB_INVALIDATE;

	flags1 &= ~mask_flags;

	if (flags1 & PIPE_CONTROL_VF_CACHE_INVALIDATE)
		flags0 |= PIPE_CONTROL0_L3_READ_ONLY_CACHE_INVALIDATE;

	return emit_pipe_control(dw, i, flags0, flags1,
				 LRC_PPHWSP_FLUSH_INVAL_SCRATCH_ADDR, 0);
}

static int emit_store_imm_ppgtt_posted(u64 addr, u64 value,
				       u32 *dw, int i)
{
	dw[i++] = MI_STORE_DATA_IMM | MI_SDI_NUM_QW(1);
	dw[i++] = lower_32_bits(addr);
	dw[i++] = upper_32_bits(addr);
	dw[i++] = lower_32_bits(value);
	dw[i++] = upper_32_bits(value);

	return i;
}

static int emit_render_cache_flush(struct xe_sched_job *job, u32 *dw, int i)
{
	struct xe_gt *gt = job->q->gt;
	bool lacks_render = !(gt->info.engine_mask & XE_HW_ENGINE_RCS_MASK);
	u32 flags;

	if (XE_WA(gt, 14016712196))
		i = emit_pipe_control(dw, i, 0, PIPE_CONTROL_DEPTH_CACHE_FLUSH,
				      LRC_PPHWSP_FLUSH_INVAL_SCRATCH_ADDR, 0);

	flags = (PIPE_CONTROL_CS_STALL |
		 PIPE_CONTROL_TILE_CACHE_FLUSH |
		 PIPE_CONTROL_RENDER_TARGET_CACHE_FLUSH |
		 PIPE_CONTROL_DEPTH_CACHE_FLUSH |
		 PIPE_CONTROL_DC_FLUSH_ENABLE |
		 PIPE_CONTROL_FLUSH_ENABLE);

	if (XE_WA(gt, 1409600907))
		flags |= PIPE_CONTROL_DEPTH_STALL;

	if (lacks_render)
		flags &= ~PIPE_CONTROL_3D_ARCH_FLAGS;
	else if (job->q->class == XE_ENGINE_CLASS_COMPUTE)
		flags &= ~PIPE_CONTROL_3D_ENGINE_FLAGS;

	return emit_pipe_control(dw, i, PIPE_CONTROL0_HDC_PIPELINE_FLUSH, flags, 0, 0);
}

static int emit_pipe_control_to_ring_end(struct xe_hw_engine *hwe, u32 *dw, int i)
{
	if (hwe->class != XE_ENGINE_CLASS_RENDER)
		return i;

	if (XE_WA(hwe->gt, 16020292621))
		i = emit_pipe_control(dw, i, 0, PIPE_CONTROL_LRI_POST_SYNC,
				      RING_NOPID(hwe->mmio_base).addr, 0);

	return i;
}

static int emit_pipe_imm_ggtt(u32 addr, u32 value, bool stall_only, u32 *dw,
			      int i)
{
	u32 flags = PIPE_CONTROL_CS_STALL | PIPE_CONTROL_GLOBAL_GTT_IVB |
		    PIPE_CONTROL_QW_WRITE;

	if (!stall_only)
		flags |= PIPE_CONTROL_FLUSH_ENABLE;

	return emit_pipe_control(dw, i, 0, flags, addr, value);
}

static u32 get_ppgtt_flag(struct xe_sched_job *job)
{
	if (job->q->vm && !job->ggtt)
		return BIT(8);

	return 0;
}

static int emit_copy_timestamp(struct xe_lrc *lrc, u32 *dw, int i)
{
	dw[i++] = MI_COPY_MEM_MEM | MI_COPY_MEM_MEM_SRC_GGTT |
		MI_COPY_MEM_MEM_DST_GGTT;
	dw[i++] = xe_lrc_ctx_job_timestamp_ggtt_addr(lrc);
	dw[i++] = 0;
	dw[i++] = xe_lrc_ctx_timestamp_ggtt_addr(lrc);
	dw[i++] = 0;
	dw[i++] = MI_NOOP;

	return i;
}

/* for engines that don't require any special HW handling (no EUs, no aux inval, etc) */
static void __emit_job_gen12_simple(struct xe_sched_job *job, struct xe_lrc *lrc,
				    u64 batch_addr, u32 seqno)
{
	u32 dw[MAX_JOB_SIZE_DW], i = 0;
	u32 ppgtt_flag = get_ppgtt_flag(job);
	struct xe_gt *gt = job->q->gt;

	i = emit_copy_timestamp(lrc, dw, i);

	if (job->ring_ops_flush_tlb) {
		dw[i++] = preparser_disable(true);
		i = emit_flush_imm_ggtt(xe_lrc_start_seqno_ggtt_addr(lrc),
					seqno, MI_INVALIDATE_TLB, dw, i);
		dw[i++] = preparser_disable(false);
	} else {
		i = emit_store_imm_ggtt(xe_lrc_start_seqno_ggtt_addr(lrc),
					seqno, dw, i);
	}

	i = emit_bb_start(batch_addr, ppgtt_flag, dw, i);

	if (job->user_fence.used) {
		i = emit_flush_dw(dw, i);
		i = emit_store_imm_ppgtt_posted(job->user_fence.addr,
						job->user_fence.value,
						dw, i);
	}

	i = emit_flush_imm_ggtt(xe_lrc_seqno_ggtt_addr(lrc), seqno, 0, dw, i);

	i = emit_user_interrupt(dw, i);

	xe_gt_assert(gt, i <= MAX_JOB_SIZE_DW);

	xe_lrc_write_ring(lrc, dw, i * sizeof(*dw));
}

static bool has_aux_ccs(struct xe_device *xe)
{
	/*
	 * PVC is a special case that has no compression of either type
	 * (FlatCCS or AuxCCS).  Also, AuxCCS is no longer used from Xe2
	 * onward, so any future platforms with no FlatCCS will not have
	 * AuxCCS either.
	 */
	if (GRAPHICS_VER(xe) >= 20 || xe->info.platform == XE_PVC)
		return false;

	return !xe->info.has_flat_ccs;
}

static void __emit_job_gen12_video(struct xe_sched_job *job, struct xe_lrc *lrc,
				   u64 batch_addr, u32 seqno)
{
	u32 dw[MAX_JOB_SIZE_DW], i = 0;
	u32 ppgtt_flag = get_ppgtt_flag(job);
	struct xe_gt *gt = job->q->gt;
	struct xe_device *xe = gt_to_xe(gt);
	bool decode = job->q->class == XE_ENGINE_CLASS_VIDEO_DECODE;

	i = emit_copy_timestamp(lrc, dw, i);

	dw[i++] = preparser_disable(true);

	/* hsdes: 1809175790 */
	if (has_aux_ccs(xe)) {
		if (decode)
			i = emit_aux_table_inv(gt, VD0_AUX_INV, dw, i);
		else
			i = emit_aux_table_inv(gt, VE0_AUX_INV, dw, i);
	}

	if (job->ring_ops_flush_tlb)
		i = emit_flush_imm_ggtt(xe_lrc_start_seqno_ggtt_addr(lrc),
					seqno, MI_INVALIDATE_TLB, dw, i);

	dw[i++] = preparser_disable(false);

	if (!job->ring_ops_flush_tlb)
		i = emit_store_imm_ggtt(xe_lrc_start_seqno_ggtt_addr(lrc),
					seqno, dw, i);

	i = emit_bb_start(batch_addr, ppgtt_flag, dw, i);

	if (job->user_fence.used) {
		i = emit_flush_dw(dw, i);
		i = emit_store_imm_ppgtt_posted(job->user_fence.addr,
						job->user_fence.value,
						dw, i);
	}

	i = emit_flush_imm_ggtt(xe_lrc_seqno_ggtt_addr(lrc), seqno, 0, dw, i);

	i = emit_user_interrupt(dw, i);

	xe_gt_assert(gt, i <= MAX_JOB_SIZE_DW);

	xe_lrc_write_ring(lrc, dw, i * sizeof(*dw));
}

static void __emit_job_gen12_render_compute(struct xe_sched_job *job,
					    struct xe_lrc *lrc,
					    u64 batch_addr, u32 seqno)
{
	u32 dw[MAX_JOB_SIZE_DW], i = 0;
	u32 ppgtt_flag = get_ppgtt_flag(job);
	struct xe_gt *gt = job->q->gt;
	struct xe_device *xe = gt_to_xe(gt);
	bool lacks_render = !(gt->info.engine_mask & XE_HW_ENGINE_RCS_MASK);
	u32 mask_flags = 0;

	i = emit_copy_timestamp(lrc, dw, i);

	dw[i++] = preparser_disable(true);
	if (lacks_render)
		mask_flags = PIPE_CONTROL_3D_ARCH_FLAGS;
	else if (job->q->class == XE_ENGINE_CLASS_COMPUTE)
		mask_flags = PIPE_CONTROL_3D_ENGINE_FLAGS;

	/* See __xe_pt_bind_vma() for a discussion on TLB invalidations. */
	i = emit_pipe_invalidate(mask_flags, job->ring_ops_flush_tlb, dw, i);

	/* hsdes: 1809175790 */
	if (has_aux_ccs(xe))
		i = emit_aux_table_inv(gt, CCS_AUX_INV, dw, i);

	dw[i++] = preparser_disable(false);

	i = emit_store_imm_ggtt(xe_lrc_start_seqno_ggtt_addr(lrc),
				seqno, dw, i);

	i = emit_bb_start(batch_addr, ppgtt_flag, dw, i);

	i = emit_render_cache_flush(job, dw, i);

	if (job->user_fence.used)
		i = emit_store_imm_ppgtt_posted(job->user_fence.addr,
						job->user_fence.value,
						dw, i);

	i = emit_pipe_imm_ggtt(xe_lrc_seqno_ggtt_addr(lrc), seqno, lacks_render, dw, i);

	i = emit_user_interrupt(dw, i);

	i = emit_pipe_control_to_ring_end(job->q->hwe, dw, i);

	xe_gt_assert(gt, i <= MAX_JOB_SIZE_DW);

	xe_lrc_write_ring(lrc, dw, i * sizeof(*dw));
}

static void emit_migration_job_gen12(struct xe_sched_job *job,
				     struct xe_lrc *lrc, u32 seqno)
{
	u32 dw[MAX_JOB_SIZE_DW], i = 0;

	i = emit_copy_timestamp(lrc, dw, i);

	i = emit_store_imm_ggtt(xe_lrc_start_seqno_ggtt_addr(lrc),
				seqno, dw, i);

	dw[i++] = MI_ARB_ON_OFF | MI_ARB_DISABLE; /* Enabled again below */

	i = emit_bb_start(job->ptrs[0].batch_addr, BIT(8), dw, i);

	if (!IS_SRIOV_VF(gt_to_xe(job->q->gt))) {
		/* XXX: Do we need this? Leaving for now. */
		dw[i++] = preparser_disable(true);
		i = emit_flush_invalidate(dw, i);
		dw[i++] = preparser_disable(false);
	}

	i = emit_bb_start(job->ptrs[1].batch_addr, BIT(8), dw, i);

	dw[i++] = MI_FLUSH_DW | MI_INVALIDATE_TLB | job->migrate_flush_flags |
		MI_FLUSH_DW_OP_STOREDW | MI_FLUSH_IMM_DW;
	dw[i++] = xe_lrc_seqno_ggtt_addr(lrc) | MI_FLUSH_DW_USE_GTT;
	dw[i++] = 0;
	dw[i++] = seqno; /* value */

	i = emit_user_interrupt(dw, i);

	xe_gt_assert(job->q->gt, i <= MAX_JOB_SIZE_DW);

	xe_lrc_write_ring(lrc, dw, i * sizeof(*dw));
}

static void emit_job_gen12_gsc(struct xe_sched_job *job)
{
	struct xe_gt *gt = job->q->gt;

	xe_gt_assert(gt, job->q->width <= 1); /* no parallel submission for GSCCS */

	__emit_job_gen12_simple(job, job->q->lrc[0],
				job->ptrs[0].batch_addr,
				xe_sched_job_lrc_seqno(job));
}

static void emit_job_gen12_copy(struct xe_sched_job *job)
{
	int i;

	if (xe_sched_job_is_migration(job->q)) {
		emit_migration_job_gen12(job, job->q->lrc[0],
					 xe_sched_job_lrc_seqno(job));
		return;
	}

	for (i = 0; i < job->q->width; ++i)
		__emit_job_gen12_simple(job, job->q->lrc[i],
					job->ptrs[i].batch_addr,
					xe_sched_job_lrc_seqno(job));
}

static void emit_job_gen12_video(struct xe_sched_job *job)
{
	int i;

	/* FIXME: Not doing parallel handshake for now */
	for (i = 0; i < job->q->width; ++i)
		__emit_job_gen12_video(job, job->q->lrc[i],
				       job->ptrs[i].batch_addr,
				       xe_sched_job_lrc_seqno(job));
}

static void emit_job_gen12_render_compute(struct xe_sched_job *job)
{
	int i;

	for (i = 0; i < job->q->width; ++i)
		__emit_job_gen12_render_compute(job, job->q->lrc[i],
						job->ptrs[i].batch_addr,
						xe_sched_job_lrc_seqno(job));
}

static const struct xe_ring_ops ring_ops_gen12_gsc = {
	.emit_job = emit_job_gen12_gsc,
};

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
	case XE_ENGINE_CLASS_OTHER:
		return &ring_ops_gen12_gsc;
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
