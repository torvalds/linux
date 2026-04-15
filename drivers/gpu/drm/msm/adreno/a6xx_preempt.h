/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, The Linux Foundation. All rights reserved. */
/* Copyright (c) 2023 Collabora, Ltd. */
/* Copyright (c) 2024 Valve Corporation */
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. */

/*
 * Try to transition the preemption state from old to new. Return
 * true on success or false if the original state wasn't 'old'
 */
static inline bool try_preempt_state(struct a6xx_gpu *a6xx_gpu,
		enum a6xx_preempt_state old, enum a6xx_preempt_state new)
{
	enum a6xx_preempt_state cur = atomic_cmpxchg(&a6xx_gpu->preempt_state,
		old, new);

	return (cur == old);
}

/*
 * Force the preemption state to the specified state.  This is used in cases
 * where the current state is known and won't change
 */
static inline void set_preempt_state(struct a6xx_gpu *gpu,
		enum a6xx_preempt_state new)
{
	/*
	 * preempt_state may be read by other cores trying to trigger a
	 * preemption or in the interrupt handler so barriers are needed
	 * before...
	 */
	smp_mb__before_atomic();
	atomic_set(&gpu->preempt_state, new);
	/* ... and after */
	smp_mb__after_atomic();
}

/* Write the most recent wptr for the given ring into the hardware */
static inline void update_wptr(struct a6xx_gpu *a6xx_gpu, struct msm_ringbuffer *ring)
{
	unsigned long flags;
	uint32_t wptr;

	spin_lock_irqsave(&ring->preempt_lock, flags);

	if (ring->restore_wptr) {
		wptr = get_wptr(ring);

		a6xx_fenced_write(a6xx_gpu, REG_A6XX_CP_RB_WPTR, wptr, BIT(0), false);

		ring->restore_wptr = false;
	}

	spin_unlock_irqrestore(&ring->preempt_lock, flags);
}

/* Return the highest priority ringbuffer with something in it */
static inline struct msm_ringbuffer *get_next_ring(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);

	unsigned long flags;
	int i;

	for (i = 0; i < gpu->nr_rings; i++) {
		bool empty;
		struct msm_ringbuffer *ring = gpu->rb[i];

		spin_lock_irqsave(&ring->preempt_lock, flags);
		empty = (get_wptr(ring) == gpu->funcs->get_rptr(gpu, ring));
		if (!empty && ring == a6xx_gpu->cur_ring)
			empty = ring->memptrs->fence == a6xx_gpu->last_seqno[i];
		spin_unlock_irqrestore(&ring->preempt_lock, flags);

		if (!empty)
			return ring;
	}

	return NULL;
}

