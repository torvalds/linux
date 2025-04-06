// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, The Linux Foundation. All rights reserved. */
/* Copyright (c) 2023 Collabora, Ltd. */
/* Copyright (c) 2024 Valve Corporation */

#include "msm_gem.h"
#include "a6xx_gpu.h"
#include "a6xx_gmu.xml.h"
#include "msm_mmu.h"
#include "msm_gpu_trace.h"

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
	/* ... and after*/
	smp_mb__after_atomic();
}

/* Write the most recent wptr for the given ring into the hardware */
static inline void update_wptr(struct msm_gpu *gpu, struct msm_ringbuffer *ring)
{
	unsigned long flags;
	uint32_t wptr;

	spin_lock_irqsave(&ring->preempt_lock, flags);

	if (ring->restore_wptr) {
		wptr = get_wptr(ring);

		gpu_write(gpu, REG_A6XX_CP_RB_WPTR, wptr);

		ring->restore_wptr = false;
	}

	spin_unlock_irqrestore(&ring->preempt_lock, flags);
}

/* Return the highest priority ringbuffer with something in it */
static struct msm_ringbuffer *get_next_ring(struct msm_gpu *gpu)
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

static void a6xx_preempt_timer(struct timer_list *t)
{
	struct a6xx_gpu *a6xx_gpu = from_timer(a6xx_gpu, t, preempt_timer);
	struct msm_gpu *gpu = &a6xx_gpu->base.base;
	struct drm_device *dev = gpu->dev;

	if (!try_preempt_state(a6xx_gpu, PREEMPT_TRIGGERED, PREEMPT_FAULTED))
		return;

	dev_err(dev->dev, "%s: preemption timed out\n", gpu->name);
	kthread_queue_work(gpu->worker, &gpu->recover_work);
}

static void preempt_prepare_postamble(struct a6xx_gpu *a6xx_gpu)
{
	u32 *postamble = a6xx_gpu->preempt_postamble_ptr;
	u32 count = 0;

	postamble[count++] = PKT7(CP_REG_RMW, 3);
	postamble[count++] = REG_A6XX_RBBM_PERFCTR_SRAM_INIT_CMD;
	postamble[count++] = 0;
	postamble[count++] = 1;

	postamble[count++] = PKT7(CP_WAIT_REG_MEM, 6);
	postamble[count++] = CP_WAIT_REG_MEM_0_FUNCTION(WRITE_EQ);
	postamble[count++] = CP_WAIT_REG_MEM_1_POLL_ADDR_LO(
				REG_A6XX_RBBM_PERFCTR_SRAM_INIT_STATUS);
	postamble[count++] = CP_WAIT_REG_MEM_2_POLL_ADDR_HI(0);
	postamble[count++] = CP_WAIT_REG_MEM_3_REF(0x1);
	postamble[count++] = CP_WAIT_REG_MEM_4_MASK(0x1);
	postamble[count++] = CP_WAIT_REG_MEM_5_DELAY_LOOP_CYCLES(0);

	a6xx_gpu->preempt_postamble_len = count;

	a6xx_gpu->postamble_enabled = true;
}

static void preempt_disable_postamble(struct a6xx_gpu *a6xx_gpu)
{
	u32 *postamble = a6xx_gpu->preempt_postamble_ptr;

	/*
	 * Disable the postamble by replacing the first packet header with a NOP
	 * that covers the whole buffer.
	 */
	*postamble = PKT7(CP_NOP, (a6xx_gpu->preempt_postamble_len - 1));

	a6xx_gpu->postamble_enabled = false;
}

void a6xx_preempt_irq(struct msm_gpu *gpu)
{
	uint32_t status;
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	struct drm_device *dev = gpu->dev;

	if (!try_preempt_state(a6xx_gpu, PREEMPT_TRIGGERED, PREEMPT_PENDING))
		return;

	/* Delete the preemption watchdog timer */
	timer_delete(&a6xx_gpu->preempt_timer);

	/*
	 * The hardware should be setting the stop bit of CP_CONTEXT_SWITCH_CNTL
	 * to zero before firing the interrupt, but there is a non zero chance
	 * of a hardware condition or a software race that could set it again
	 * before we have a chance to finish. If that happens, log and go for
	 * recovery
	 */
	status = gpu_read(gpu, REG_A6XX_CP_CONTEXT_SWITCH_CNTL);
	if (unlikely(status & A6XX_CP_CONTEXT_SWITCH_CNTL_STOP)) {
		DRM_DEV_ERROR(&gpu->pdev->dev,
					  "!!!!!!!!!!!!!!!! preemption faulted !!!!!!!!!!!!!! irq\n");
		set_preempt_state(a6xx_gpu, PREEMPT_FAULTED);
		dev_err(dev->dev, "%s: Preemption failed to complete\n",
			gpu->name);
		kthread_queue_work(gpu->worker, &gpu->recover_work);
		return;
	}

	a6xx_gpu->cur_ring = a6xx_gpu->next_ring;
	a6xx_gpu->next_ring = NULL;

	set_preempt_state(a6xx_gpu, PREEMPT_FINISH);

	update_wptr(gpu, a6xx_gpu->cur_ring);

	set_preempt_state(a6xx_gpu, PREEMPT_NONE);

	trace_msm_gpu_preemption_irq(a6xx_gpu->cur_ring->id);

	/*
	 * Retrigger preemption to avoid a deadlock that might occur when preemption
	 * is skipped due to it being already in flight when requested.
	 */
	a6xx_preempt_trigger(gpu);
}

void a6xx_preempt_hw_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	int i;

	/* No preemption if we only have one ring */
	if (gpu->nr_rings == 1)
		return;

	for (i = 0; i < gpu->nr_rings; i++) {
		struct a6xx_preempt_record *record_ptr = a6xx_gpu->preempt[i];

		record_ptr->wptr = 0;
		record_ptr->rptr = 0;
		record_ptr->rptr_addr = shadowptr(a6xx_gpu, gpu->rb[i]);
		record_ptr->info = 0;
		record_ptr->data = 0;
		record_ptr->rbase = gpu->rb[i]->iova;
	}

	/* Write a 0 to signal that we aren't switching pagetables */
	gpu_write64(gpu, REG_A6XX_CP_CONTEXT_SWITCH_SMMU_INFO, 0);

	/* Enable the GMEM save/restore feature for preemption */
	gpu_write(gpu, REG_A6XX_RB_CONTEXT_SWITCH_GMEM_SAVE_RESTORE, 0x1);

	/* Reset the preemption state */
	set_preempt_state(a6xx_gpu, PREEMPT_NONE);

	spin_lock_init(&a6xx_gpu->eval_lock);

	/* Always come up on rb 0 */
	a6xx_gpu->cur_ring = gpu->rb[0];
}

void a6xx_preempt_trigger(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	unsigned long flags;
	struct msm_ringbuffer *ring;
	unsigned int cntl;
	bool sysprof;

	if (gpu->nr_rings == 1)
		return;

	/*
	 * Lock to make sure another thread attempting preemption doesn't skip it
	 * while we are still evaluating the next ring. This makes sure the other
	 * thread does start preemption if we abort it and avoids a soft lock.
	 */
	spin_lock_irqsave(&a6xx_gpu->eval_lock, flags);

	/*
	 * Try to start preemption by moving from NONE to START. If
	 * unsuccessful, a preemption is already in flight
	 */
	if (!try_preempt_state(a6xx_gpu, PREEMPT_NONE, PREEMPT_START)) {
		spin_unlock_irqrestore(&a6xx_gpu->eval_lock, flags);
		return;
	}

	cntl = A6XX_CP_CONTEXT_SWITCH_CNTL_LEVEL(a6xx_gpu->preempt_level);

	if (a6xx_gpu->skip_save_restore)
		cntl |= A6XX_CP_CONTEXT_SWITCH_CNTL_SKIP_SAVE_RESTORE;

	if (a6xx_gpu->uses_gmem)
		cntl |= A6XX_CP_CONTEXT_SWITCH_CNTL_USES_GMEM;

	cntl |= A6XX_CP_CONTEXT_SWITCH_CNTL_STOP;

	/* Get the next ring to preempt to */
	ring = get_next_ring(gpu);

	/*
	 * If no ring is populated or the highest priority ring is the current
	 * one do nothing except to update the wptr to the latest and greatest
	 */
	if (!ring || (a6xx_gpu->cur_ring == ring)) {
		set_preempt_state(a6xx_gpu, PREEMPT_FINISH);
		update_wptr(gpu, a6xx_gpu->cur_ring);
		set_preempt_state(a6xx_gpu, PREEMPT_NONE);
		spin_unlock_irqrestore(&a6xx_gpu->eval_lock, flags);
		return;
	}

	spin_unlock_irqrestore(&a6xx_gpu->eval_lock, flags);

	spin_lock_irqsave(&ring->preempt_lock, flags);

	struct a7xx_cp_smmu_info *smmu_info_ptr =
		a6xx_gpu->preempt_smmu[ring->id];
	struct a6xx_preempt_record *record_ptr = a6xx_gpu->preempt[ring->id];
	u64 ttbr0 = ring->memptrs->ttbr0;
	u32 context_idr = ring->memptrs->context_idr;

	smmu_info_ptr->ttbr0 = ttbr0;
	smmu_info_ptr->context_idr = context_idr;
	record_ptr->wptr = get_wptr(ring);

	/*
	 * The GPU will write the wptr we set above when we preempt. Reset
	 * restore_wptr to make sure that we don't write WPTR to the same
	 * thing twice. It's still possible subsequent submissions will update
	 * wptr again, in which case they will set the flag to true. This has
	 * to be protected by the lock for setting the flag and updating wptr
	 * to be atomic.
	 */
	ring->restore_wptr = false;

	trace_msm_gpu_preemption_trigger(a6xx_gpu->cur_ring->id, ring->id);

	spin_unlock_irqrestore(&ring->preempt_lock, flags);

	gpu_write64(gpu,
		REG_A6XX_CP_CONTEXT_SWITCH_SMMU_INFO,
		a6xx_gpu->preempt_smmu_iova[ring->id]);

	gpu_write64(gpu,
		REG_A6XX_CP_CONTEXT_SWITCH_PRIV_NON_SECURE_RESTORE_ADDR,
		a6xx_gpu->preempt_iova[ring->id]);

	a6xx_gpu->next_ring = ring;

	/* Start a timer to catch a stuck preemption */
	mod_timer(&a6xx_gpu->preempt_timer, jiffies + msecs_to_jiffies(10000));

	/* Enable or disable postamble as needed */
	sysprof = refcount_read(&a6xx_gpu->base.base.sysprof_active) > 1;

	if (!sysprof && !a6xx_gpu->postamble_enabled)
		preempt_prepare_postamble(a6xx_gpu);

	if (sysprof && a6xx_gpu->postamble_enabled)
		preempt_disable_postamble(a6xx_gpu);

	/* Set the preemption state to triggered */
	set_preempt_state(a6xx_gpu, PREEMPT_TRIGGERED);

	/* Trigger the preemption */
	gpu_write(gpu, REG_A6XX_CP_CONTEXT_SWITCH_CNTL, cntl);
}

static int preempt_init_ring(struct a6xx_gpu *a6xx_gpu,
		struct msm_ringbuffer *ring)
{
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	struct msm_gpu *gpu = &adreno_gpu->base;
	struct drm_gem_object *bo = NULL;
	phys_addr_t ttbr;
	u64 iova = 0;
	void *ptr;
	int asid;

	ptr = msm_gem_kernel_new(gpu->dev,
		PREEMPT_RECORD_SIZE(adreno_gpu),
		MSM_BO_WC | MSM_BO_MAP_PRIV, gpu->aspace, &bo, &iova);

	if (IS_ERR(ptr))
		return PTR_ERR(ptr);

	memset(ptr, 0, PREEMPT_RECORD_SIZE(adreno_gpu));

	msm_gem_object_set_name(bo, "preempt_record ring%d", ring->id);

	a6xx_gpu->preempt_bo[ring->id] = bo;
	a6xx_gpu->preempt_iova[ring->id] = iova;
	a6xx_gpu->preempt[ring->id] = ptr;

	struct a6xx_preempt_record *record_ptr = ptr;

	ptr = msm_gem_kernel_new(gpu->dev,
		PREEMPT_SMMU_INFO_SIZE,
		MSM_BO_WC | MSM_BO_MAP_PRIV | MSM_BO_GPU_READONLY,
		gpu->aspace, &bo, &iova);

	if (IS_ERR(ptr))
		return PTR_ERR(ptr);

	memset(ptr, 0, PREEMPT_SMMU_INFO_SIZE);

	msm_gem_object_set_name(bo, "preempt_smmu_info ring%d", ring->id);

	a6xx_gpu->preempt_smmu_bo[ring->id] = bo;
	a6xx_gpu->preempt_smmu_iova[ring->id] = iova;
	a6xx_gpu->preempt_smmu[ring->id] = ptr;

	struct a7xx_cp_smmu_info *smmu_info_ptr = ptr;

	msm_iommu_pagetable_params(gpu->aspace->mmu, &ttbr, &asid);

	smmu_info_ptr->magic = GEN7_CP_SMMU_INFO_MAGIC;
	smmu_info_ptr->ttbr0 = ttbr;
	smmu_info_ptr->asid = 0xdecafbad;
	smmu_info_ptr->context_idr = 0;

	/* Set up the defaults on the preemption record */
	record_ptr->magic = A6XX_PREEMPT_RECORD_MAGIC;
	record_ptr->info = 0;
	record_ptr->data = 0;
	record_ptr->rptr = 0;
	record_ptr->wptr = 0;
	record_ptr->cntl = MSM_GPU_RB_CNTL_DEFAULT;
	record_ptr->rbase = ring->iova;
	record_ptr->counter = 0;
	record_ptr->bv_rptr_addr = rbmemptr(ring, bv_rptr);

	return 0;
}

void a6xx_preempt_fini(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	int i;

	for (i = 0; i < gpu->nr_rings; i++)
		msm_gem_kernel_put(a6xx_gpu->preempt_bo[i], gpu->aspace);
}

void a6xx_preempt_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	int i;

	/* No preemption if we only have one ring */
	if (gpu->nr_rings <= 1)
		return;

	for (i = 0; i < gpu->nr_rings; i++) {
		if (preempt_init_ring(a6xx_gpu, gpu->rb[i]))
			goto fail;
	}

	/* TODO: make this configurable? */
	a6xx_gpu->preempt_level = 1;
	a6xx_gpu->uses_gmem = 1;
	a6xx_gpu->skip_save_restore = 1;

	a6xx_gpu->preempt_postamble_ptr  = msm_gem_kernel_new(gpu->dev,
			PAGE_SIZE,
			MSM_BO_WC | MSM_BO_MAP_PRIV | MSM_BO_GPU_READONLY,
			gpu->aspace, &a6xx_gpu->preempt_postamble_bo,
			&a6xx_gpu->preempt_postamble_iova);

	preempt_prepare_postamble(a6xx_gpu);

	if (IS_ERR(a6xx_gpu->preempt_postamble_ptr))
		goto fail;

	timer_setup(&a6xx_gpu->preempt_timer, a6xx_preempt_timer, 0);

	return;
fail:
	/*
	 * On any failure our adventure is over. Clean up and
	 * set nr_rings to 1 to force preemption off
	 */
	a6xx_preempt_fini(gpu);
	gpu->nr_rings = 1;

	DRM_DEV_ERROR(&gpu->pdev->dev,
				  "preemption init failed, disabling preemption\n");

	return;
}
