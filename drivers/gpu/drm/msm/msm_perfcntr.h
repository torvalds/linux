// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __MSM_PERFCNTR_H__
#define __MSM_PERFCNTR_H__

#include "linux/array_size.h"
#include "linux/circ_buf.h"
#include "linux/hrtimer.h"
#include "linux/kthread.h"
#include "linux/wait.h"
#include "linux/workqueue.h"

#include "adreno_common.xml.h"

/*
 * This is a subset of the tables used by mesa.  We don't need to
 * enumerate the countables on the kernel side.
 */

/* Describes a single counter: */
struct msm_perfcntr_counter {
	/* offset of the SELect register to choose what to count: */
	unsigned select_reg;
	/* additional SEL regs to enable slice counters (gen8+) */
	unsigned slice_select_regs[2];
	/* offset of the lo/hi 32b to read current counter value: */
	unsigned counter_reg_lo;
	unsigned counter_reg_hi;
	/* TODO some counters have enable/clear registers */
};

/* Describes an entire counter group: */
struct msm_perfcntr_group {
	const char *name;
	enum adreno_pipe pipe;
	unsigned num_counters;
	const struct msm_perfcntr_counter *counters;
};

/**
 * struct msm_perfcntr_stream - state for a single open stream fd
 */
struct msm_perfcntr_stream {
	/** @gpu: Back-link to the GPU */
	struct msm_gpu *gpu;

	/** @sample_timer: Timer to sample counters */
	struct hrtimer sample_timer;

	/** @poll_wq: Wait queue for waiting for OA data to be available */
	wait_queue_head_t poll_wq;

	/** @sample_period_ns: Sampling period */
	uint64_t sample_period_ns;

	/** @nr_groups: # of counter groups with enabled counters */
	uint32_t nr_groups;

	/** @seqno: counter for collected samples */
	uint32_t seqno;

	/** @sel_fence: Fence for SEL reg programming  */
	uint32_t sel_fence;

	/**
	 * @sel_work: Worker for SEL reg programming
	 *
	 * Initial SEL reg programming (as opposed to restoring the SEL
	 * regs on runpm resume) must run on the same ordered wq as is
	 * used by drm_sched, to serialize it with GEM_SUBMITs written
	 * into the same ringbuffer.
	 */
	struct work_struct sel_work;

	/**
	 * @sample_work: Worker for collecting samples
	 */
	struct kthread_work sample_work;

	/**
	 * @read_lock:
	 *
	 * Fifo access is synchronied on the producer side by virtue
	 * of there being a single timer collecting samples and writing
	 * into the fifo.  It is protected on the consumer side by
	 * @read_lock.
	 */
	struct mutex read_lock;

	/**
	 * @group_idx: array of nr_groups
	 *
	 * Maps the order of groups in PERFCNTR_CONFIG ioctl to group idx,
	 * so that results in the results stream can be ordered to match
	 * the ioctl call that setup the stream
	 */
	uint32_t *group_idx;

	/** @fifo: circular buffer for samples */
	struct circ_buf fifo;

	/** @fifo_size: circular buffer size */
	size_t fifo_size;

	/** @period_size: size of data for single sampling period */
	size_t period_size;
};

uint32_t msm_perfcntr_group_idx(const struct msm_perfcntr_stream *stream, uint32_t n);
uint32_t msm_perfcntr_counter_base(const struct msm_perfcntr_stream *stream, uint32_t group_idx);

/**
 * struct msm_perfcntr_context_state - per-msm_context counter state
 *
 * A given counter can either be unused, reserved for global counter
 * collection exclusively, or reserved for local per-context counter
 * collection inclusively.  Multiple contexts can reserve the same
 * counter, since SEL reg programming and counter begin/end sampling
 * happen locally (within a single GEM_SUBMIT ioctl).
 */
struct msm_perfcntr_context_state {
	/** @dummy: Some compilers dislike structs with only a flex array */
	unsigned dummy;

	/**
	 * @reserved_counters:
	 *
	 * The number of reserved counters indexed by perfcntr group.
	 */
	unsigned reserved_counters[];
};

extern const struct msm_perfcntr_group a6xx_perfcntr_groups[];
extern const unsigned a6xx_num_perfcntr_groups;

extern const struct msm_perfcntr_group a7xx_perfcntr_groups[];
extern const unsigned a7xx_num_perfcntr_groups;

extern const struct msm_perfcntr_group a8xx_perfcntr_groups[];
extern const unsigned a8xx_num_perfcntr_groups;

#define GROUP(_name, _pipe, _counters, _countables) {                          \
      .name = _name,                                                           \
      .pipe = _pipe,                                                           \
      .num_counters = ARRAY_SIZE(_counters),                                   \
      .counters = _counters,                                                   \
   }

#define fd_perfcntr_counter msm_perfcntr_counter
#define fd_perfcntr_group   msm_perfcntr_group

#endif /* __MSM_PERFCNTR_H__ */
