/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2016-2017 The Linux Foundation. All rights reserved.
 */
#ifndef __A5XX_GPU_H__
#define __A5XX_GPU_H__

#include "adreno_gpu.h"

/* Bringing over the hack from the previous targets */
#undef ROP_COPY
#undef ROP_XOR

#include "a5xx.xml.h"

struct a5xx_gpu {
	struct adreno_gpu base;

	struct drm_gem_object *pm4_bo;
	uint64_t pm4_iova;

	struct drm_gem_object *pfp_bo;
	uint64_t pfp_iova;

	struct drm_gem_object *gpmu_bo;
	uint64_t gpmu_iova;
	uint32_t gpmu_dwords;

	uint32_t lm_leakage;

	struct msm_ringbuffer *cur_ring;
	struct msm_ringbuffer *next_ring;

	struct drm_gem_object *preempt_bo[MSM_GPU_MAX_RINGS];
	struct a5xx_preempt_record *preempt[MSM_GPU_MAX_RINGS];
	uint64_t preempt_iova[MSM_GPU_MAX_RINGS];

	atomic_t preempt_state;
	struct timer_list preempt_timer;
};

#define to_a5xx_gpu(x) container_of(x, struct a5xx_gpu, base)

#ifdef CONFIG_DEBUG_FS
int a5xx_debugfs_init(struct msm_gpu *gpu, struct drm_minor *minor);
#endif

/*
 * In order to do lockless preemption we use a simple state machine to progress
 * through the process.
 *
 * PREEMPT_NONE - no preemption in progress.  Next state START.
 * PREEMPT_START - The trigger is evaulating if preemption is possible. Next
 * states: TRIGGERED, NONE
 * PREEMPT_ABORT - An intermediate state before moving back to NONE. Next
 * state: NONE.
 * PREEMPT_TRIGGERED: A preemption has been executed on the hardware. Next
 * states: FAULTED, PENDING
 * PREEMPT_FAULTED: A preemption timed out (never completed). This will trigger
 * recovery.  Next state: N/A
 * PREEMPT_PENDING: Preemption complete interrupt fired - the callback is
 * checking the success of the operation. Next state: FAULTED, NONE.
 */

enum preempt_state {
	PREEMPT_NONE = 0,
	PREEMPT_START,
	PREEMPT_ABORT,
	PREEMPT_TRIGGERED,
	PREEMPT_FAULTED,
	PREEMPT_PENDING,
};

/*
 * struct a5xx_preempt_record is a shared buffer between the microcode and the
 * CPU to store the state for preemption. The record itself is much larger
 * (64k) but most of that is used by the CP for storage.
 *
 * There is a preemption record assigned per ringbuffer. When the CPU triggers a
 * preemption, it fills out the record with the useful information (wptr, ring
 * base, etc) and the microcode uses that information to set up the CP following
 * the preemption.  When a ring is switched out, the CP will save the ringbuffer
 * state back to the record. In this way, once the records are properly set up
 * the CPU can quickly switch back and forth between ringbuffers by only
 * updating a few registers (often only the wptr).
 *
 * These are the CPU aware registers in the record:
 * @magic: Must always be 0x27C4BAFC
 * @info: Type of the record - written 0 by the CPU, updated by the CP
 * @data: Data field from SET_RENDER_MODE or a checkpoint. Written and used by
 * the CP
 * @cntl: Value of RB_CNTL written by CPU, save/restored by CP
 * @rptr: Value of RB_RPTR written by CPU, save/restored by CP
 * @wptr: Value of RB_WPTR written by CPU, save/restored by CP
 * @rptr_addr: Value of RB_RPTR_ADDR written by CPU, save/restored by CP
 * @rbase: Value of RB_BASE written by CPU, save/restored by CP
 * @counter: GPU address of the storage area for the performance counters
 */
struct a5xx_preempt_record {
	uint32_t magic;
	uint32_t info;
	uint32_t data;
	uint32_t cntl;
	uint32_t rptr;
	uint32_t wptr;
	uint64_t rptr_addr;
	uint64_t rbase;
	uint64_t counter;
};

/* Magic identifier for the preemption record */
#define A5XX_PREEMPT_RECORD_MAGIC 0x27C4BAFCUL

/*
 * Even though the structure above is only a few bytes, we need a full 64k to
 * store the entire preemption record from the CP
 */
#define A5XX_PREEMPT_RECORD_SIZE (64 * 1024)

/*
 * The preemption counter block is a storage area for the value of the
 * preemption counters that are saved immediately before context switch. We
 * append it on to the end of the allocation for the preemption record.
 */
#define A5XX_PREEMPT_COUNTER_SIZE (16 * 4)


int a5xx_power_init(struct msm_gpu *gpu);
void a5xx_gpmu_ucode_init(struct msm_gpu *gpu);

static inline int spin_usecs(struct msm_gpu *gpu, uint32_t usecs,
		uint32_t reg, uint32_t mask, uint32_t value)
{
	while (usecs--) {
		udelay(1);
		if ((gpu_read(gpu, reg) & mask) == value)
			return 0;
		cpu_relax();
	}

	return -ETIMEDOUT;
}

bool a5xx_idle(struct msm_gpu *gpu, struct msm_ringbuffer *ring);
void a5xx_set_hwcg(struct msm_gpu *gpu, bool state);

void a5xx_preempt_init(struct msm_gpu *gpu);
void a5xx_preempt_hw_init(struct msm_gpu *gpu);
void a5xx_preempt_trigger(struct msm_gpu *gpu);
void a5xx_preempt_irq(struct msm_gpu *gpu);
void a5xx_preempt_fini(struct msm_gpu *gpu);

/* Return true if we are in a preempt state */
static inline bool a5xx_in_preempt(struct a5xx_gpu *a5xx_gpu)
{
	int preempt_state = atomic_read(&a5xx_gpu->preempt_state);

	return !(preempt_state == PREEMPT_NONE ||
			preempt_state == PREEMPT_ABORT);
}

#endif /* __A5XX_GPU_H__ */
