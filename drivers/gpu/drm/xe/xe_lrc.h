/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */
#ifndef _XE_LRC_H_
#define _XE_LRC_H_

#include <linux/types.h>

#include "xe_lrc_types.h"

struct drm_printer;
struct xe_bb;
struct xe_device;
struct xe_exec_queue;
enum xe_engine_class;
struct xe_gt;
struct xe_hw_engine;
struct xe_lrc;
struct xe_vm;

struct xe_lrc_snapshot {
	struct xe_bo *lrc_bo;
	void *lrc_snapshot;
	unsigned long lrc_size, lrc_offset;

	u32 context_desc;
	u32 ring_addr;
	u32 indirect_context_desc;
	u32 head;
	u32 start;
	struct {
		u32 internal;
		u32 memory;
	} tail;
	u32 start_seqno;
	u32 seqno;
	u32 ctx_timestamp;
	u32 ctx_job_timestamp;
};

#define LRC_PPHWSP_SCRATCH_ADDR (0x34 * 4)

struct xe_lrc *xe_lrc_create(struct xe_hw_engine *hwe, struct xe_vm *vm,
			     u32 ring_size);
void xe_lrc_destroy(struct kref *ref);

/**
 * xe_lrc_get - Get reference to the LRC
 * @lrc: Logical Ring Context
 *
 * Increment reference count of @lrc
 */
static inline struct xe_lrc *xe_lrc_get(struct xe_lrc *lrc)
{
	kref_get(&lrc->refcount);
	return lrc;
}

/**
 * xe_lrc_put - Put reference of the LRC
 * @lrc: Logical Ring Context
 *
 * Decrement reference count of @lrc, call xe_lrc_destroy when
 * reference count reaches 0.
 */
static inline void xe_lrc_put(struct xe_lrc *lrc)
{
	kref_put(&lrc->refcount, xe_lrc_destroy);
}

size_t xe_gt_lrc_size(struct xe_gt *gt, enum xe_engine_class class);
u32 xe_lrc_pphwsp_offset(struct xe_lrc *lrc);
u32 xe_lrc_regs_offset(struct xe_lrc *lrc);

void xe_lrc_set_ring_tail(struct xe_lrc *lrc, u32 tail);
u32 xe_lrc_ring_tail(struct xe_lrc *lrc);
void xe_lrc_set_ring_head(struct xe_lrc *lrc, u32 head);
u32 xe_lrc_ring_head(struct xe_lrc *lrc);
u32 xe_lrc_ring_space(struct xe_lrc *lrc);
void xe_lrc_write_ring(struct xe_lrc *lrc, const void *data, size_t size);

bool xe_lrc_ring_is_idle(struct xe_lrc *lrc);

u32 xe_lrc_indirect_ring_ggtt_addr(struct xe_lrc *lrc);
u32 xe_lrc_ggtt_addr(struct xe_lrc *lrc);
u32 *xe_lrc_regs(struct xe_lrc *lrc);

u32 xe_lrc_read_ctx_reg(struct xe_lrc *lrc, int reg_nr);
void xe_lrc_write_ctx_reg(struct xe_lrc *lrc, int reg_nr, u32 val);

u64 xe_lrc_descriptor(struct xe_lrc *lrc);

u32 xe_lrc_seqno_ggtt_addr(struct xe_lrc *lrc);
struct dma_fence *xe_lrc_alloc_seqno_fence(void);
void xe_lrc_free_seqno_fence(struct dma_fence *fence);
void xe_lrc_init_seqno_fence(struct xe_lrc *lrc, struct dma_fence *fence);
s32 xe_lrc_seqno(struct xe_lrc *lrc);

u32 xe_lrc_start_seqno_ggtt_addr(struct xe_lrc *lrc);
s32 xe_lrc_start_seqno(struct xe_lrc *lrc);

u32 xe_lrc_parallel_ggtt_addr(struct xe_lrc *lrc);
struct iosys_map xe_lrc_parallel_map(struct xe_lrc *lrc);

size_t xe_lrc_skip_size(struct xe_device *xe);

void xe_lrc_dump_default(struct drm_printer *p,
			 struct xe_gt *gt,
			 enum xe_engine_class);

void xe_lrc_emit_hwe_state_instructions(struct xe_exec_queue *q, struct xe_bb *bb);

struct xe_lrc_snapshot *xe_lrc_snapshot_capture(struct xe_lrc *lrc);
void xe_lrc_snapshot_capture_delayed(struct xe_lrc_snapshot *snapshot);
void xe_lrc_snapshot_print(struct xe_lrc_snapshot *snapshot, struct drm_printer *p);
void xe_lrc_snapshot_free(struct xe_lrc_snapshot *snapshot);

u32 xe_lrc_ctx_timestamp_ggtt_addr(struct xe_lrc *lrc);
u32 xe_lrc_ctx_timestamp(struct xe_lrc *lrc);
u32 xe_lrc_ctx_job_timestamp_ggtt_addr(struct xe_lrc *lrc);
u32 xe_lrc_ctx_job_timestamp(struct xe_lrc *lrc);

/**
 * xe_lrc_update_timestamp - readout LRC timestamp and update cached value
 * @lrc: logical ring context for this exec queue
 * @old_ts: pointer where to save the previous timestamp
 *
 * Read the current timestamp for this LRC and update the cached value. The
 * previous cached value is also returned in @old_ts so the caller can calculate
 * the delta between 2 updates. Note that this is not intended to be called from
 * any place, but just by the paths updating the drm client utilization.
 *
 * Returns the current LRC timestamp
 */
u32 xe_lrc_update_timestamp(struct xe_lrc *lrc, u32 *old_ts);

#endif
