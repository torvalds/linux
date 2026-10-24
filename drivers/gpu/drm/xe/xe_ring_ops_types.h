/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_RING_OPS_TYPES_H_
#define _XE_RING_OPS_TYPES_H_

#include <linux/build_bug.h>
#include <linux/types.h>

struct xe_gt;
struct xe_sched_job;

#define MAX_JOB_SIZE_DW 74
#define MAX_JOB_SIZE_BYTES (MAX_JOB_SIZE_DW * 4)

/*
 * ULLS migration jobs advance the ring tail from within the ring itself, so a
 * job has to know where its successor will end before that successor has been
 * emitted. Every ULLS job is therefore padded to a fixed size, letting the
 * next tail be derived arithmetically.
 *
 * Sized for the largest such job, emitted by emit_migration_job_gen12():
 * preamble (4), copy timestamp (8, its size on an SRIOV VF), start seqno
 * store (4), arbitration off (1), batch buffer starts (2 * 3), pre-parser
 * bracketed flush invalidate (6), seqno flush (4), user interrupt (3) and
 * postamble (7 + 5).
 */
#define ULLS_JOB_SIZE_DW 48
#define ULLS_JOB_SIZE_BYTES (ULLS_JOB_SIZE_DW * 4)

/*
 * RING_TAIL only encodes a qword aligned offset, and xe_lrc_write_ring()
 * appends a NOP to anything shorter, either of which would desynchronise the
 * ring from the tail a job predicts for its successor.
 */
static_assert(ULLS_JOB_SIZE_BYTES % 8 == 0);
static_assert(ULLS_JOB_SIZE_DW <= MAX_JOB_SIZE_DW);

/**
 * struct xe_ring_ops - Ring operations
 */
struct xe_ring_ops {
	/** @emit_job: Write job to ring */
	void (*emit_job)(struct xe_sched_job *job);

	/** @emit_aux_table_inv: Emit aux table invalidation to the ring */
	u32 *(*emit_aux_table_inv)(struct xe_gt *gt, u32 *cmd);
};

#endif
