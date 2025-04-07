// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2017-2019 The Linux Foundation. All rights reserved. */


#include "msm_gem.h"
#include "msm_mmu.h"
#include "msm_gpu_trace.h"
#include "a6xx_gpu.h"
#include "a6xx_gmu.xml.h"

#include <linux/bitfield.h>
#include <linux/devfreq.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/pm_domain.h>
#include <linux/soc/qcom/llcc-qcom.h>

#define GPU_PAS_ID 13

static inline bool _a6xx_check_idle(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);

	/* Check that the GMU is idle */
	if (!adreno_has_gmu_wrapper(adreno_gpu) && !a6xx_gmu_isidle(&a6xx_gpu->gmu))
		return false;

	/* Check tha the CX master is idle */
	if (gpu_read(gpu, REG_A6XX_RBBM_STATUS) &
			~A6XX_RBBM_STATUS_CP_AHB_BUSY_CX_MASTER)
		return false;

	return !(gpu_read(gpu, REG_A6XX_RBBM_INT_0_STATUS) &
		A6XX_RBBM_INT_0_MASK_RBBM_HANG_DETECT);
}

static bool a6xx_idle(struct msm_gpu *gpu, struct msm_ringbuffer *ring)
{
	/* wait for CP to drain ringbuffer: */
	if (!adreno_idle(gpu, ring))
		return false;

	if (spin_until(_a6xx_check_idle(gpu))) {
		DRM_ERROR("%s: %ps: timeout waiting for GPU to idle: status %8.8X irq %8.8X rptr/wptr %d/%d\n",
			gpu->name, __builtin_return_address(0),
			gpu_read(gpu, REG_A6XX_RBBM_STATUS),
			gpu_read(gpu, REG_A6XX_RBBM_INT_0_STATUS),
			gpu_read(gpu, REG_A6XX_CP_RB_RPTR),
			gpu_read(gpu, REG_A6XX_CP_RB_WPTR));
		return false;
	}

	return true;
}

static void update_shadow_rptr(struct msm_gpu *gpu, struct msm_ringbuffer *ring)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);

	/* Expanded APRIV doesn't need to issue the WHERE_AM_I opcode */
	if (a6xx_gpu->has_whereami && !adreno_gpu->base.hw_apriv) {
		OUT_PKT7(ring, CP_WHERE_AM_I, 2);
		OUT_RING(ring, lower_32_bits(shadowptr(a6xx_gpu, ring)));
		OUT_RING(ring, upper_32_bits(shadowptr(a6xx_gpu, ring)));
	}
}

static void a6xx_flush(struct msm_gpu *gpu, struct msm_ringbuffer *ring)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	uint32_t wptr;
	unsigned long flags;

	update_shadow_rptr(gpu, ring);

	spin_lock_irqsave(&ring->preempt_lock, flags);

	/* Copy the shadow to the actual register */
	ring->cur = ring->next;

	/* Make sure to wrap wptr if we need to */
	wptr = get_wptr(ring);

	/* Update HW if this is the current ring and we are not in preempt*/
	if (!a6xx_in_preempt(a6xx_gpu)) {
		if (a6xx_gpu->cur_ring == ring)
			gpu_write(gpu, REG_A6XX_CP_RB_WPTR, wptr);
		else
			ring->restore_wptr = true;
	} else {
		ring->restore_wptr = true;
	}

	spin_unlock_irqrestore(&ring->preempt_lock, flags);
}

static void get_stats_counter(struct msm_ringbuffer *ring, u32 counter,
		u64 iova)
{
	OUT_PKT7(ring, CP_REG_TO_MEM, 3);
	OUT_RING(ring, CP_REG_TO_MEM_0_REG(counter) |
		CP_REG_TO_MEM_0_CNT(2) |
		CP_REG_TO_MEM_0_64B);
	OUT_RING(ring, lower_32_bits(iova));
	OUT_RING(ring, upper_32_bits(iova));
}

static void a6xx_set_pagetable(struct a6xx_gpu *a6xx_gpu,
		struct msm_ringbuffer *ring, struct msm_gem_submit *submit)
{
	bool sysprof = refcount_read(&a6xx_gpu->base.base.sysprof_active) > 1;
	struct msm_file_private *ctx = submit->queue->ctx;
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	phys_addr_t ttbr;
	u32 asid;
	u64 memptr = rbmemptr(ring, ttbr0);

	if (ctx->seqno == ring->cur_ctx_seqno)
		return;

	if (msm_iommu_pagetable_params(ctx->aspace->mmu, &ttbr, &asid))
		return;

	if (adreno_gpu->info->family >= ADRENO_7XX_GEN1) {
		/* Wait for previous submit to complete before continuing: */
		OUT_PKT7(ring, CP_WAIT_TIMESTAMP, 4);
		OUT_RING(ring, 0);
		OUT_RING(ring, lower_32_bits(rbmemptr(ring, fence)));
		OUT_RING(ring, upper_32_bits(rbmemptr(ring, fence)));
		OUT_RING(ring, submit->seqno - 1);
	}

	if (!sysprof) {
		if (!adreno_is_a7xx(adreno_gpu)) {
			/* Turn off protected mode to write to special registers */
			OUT_PKT7(ring, CP_SET_PROTECTED_MODE, 1);
			OUT_RING(ring, 0);
		}

		OUT_PKT4(ring, REG_A6XX_RBBM_PERFCTR_SRAM_INIT_CMD, 1);
		OUT_RING(ring, 1);
	}

	/* Execute the table update */
	OUT_PKT7(ring, CP_SMMU_TABLE_UPDATE, 4);
	OUT_RING(ring, CP_SMMU_TABLE_UPDATE_0_TTBR0_LO(lower_32_bits(ttbr)));

	OUT_RING(ring,
		CP_SMMU_TABLE_UPDATE_1_TTBR0_HI(upper_32_bits(ttbr)) |
		CP_SMMU_TABLE_UPDATE_1_ASID(asid));
	OUT_RING(ring, CP_SMMU_TABLE_UPDATE_2_CONTEXTIDR(0));
	OUT_RING(ring, CP_SMMU_TABLE_UPDATE_3_CONTEXTBANK(0));

	/*
	 * Write the new TTBR0 to the memstore. This is good for debugging.
	 * Needed for preemption
	 */
	OUT_PKT7(ring, CP_MEM_WRITE, 5);
	OUT_RING(ring, CP_MEM_WRITE_0_ADDR_LO(lower_32_bits(memptr)));
	OUT_RING(ring, CP_MEM_WRITE_1_ADDR_HI(upper_32_bits(memptr)));
	OUT_RING(ring, lower_32_bits(ttbr));
	OUT_RING(ring, upper_32_bits(ttbr));
	OUT_RING(ring, ctx->seqno);

	/*
	 * Sync both threads after switching pagetables and enable BR only
	 * to make sure BV doesn't race ahead while BR is still switching
	 * pagetables.
	 */
	if (adreno_is_a7xx(&a6xx_gpu->base)) {
		OUT_PKT7(ring, CP_THREAD_CONTROL, 1);
		OUT_RING(ring, CP_THREAD_CONTROL_0_SYNC_THREADS | CP_SET_THREAD_BR);
	}

	/*
	 * And finally, trigger a uche flush to be sure there isn't anything
	 * lingering in that part of the GPU
	 */

	OUT_PKT7(ring, CP_EVENT_WRITE, 1);
	OUT_RING(ring, CACHE_INVALIDATE);

	if (!sysprof) {
		/*
		 * Wait for SRAM clear after the pgtable update, so the
		 * two can happen in parallel:
		 */
		OUT_PKT7(ring, CP_WAIT_REG_MEM, 6);
		OUT_RING(ring, CP_WAIT_REG_MEM_0_FUNCTION(WRITE_EQ));
		OUT_RING(ring, CP_WAIT_REG_MEM_1_POLL_ADDR_LO(
				REG_A6XX_RBBM_PERFCTR_SRAM_INIT_STATUS));
		OUT_RING(ring, CP_WAIT_REG_MEM_2_POLL_ADDR_HI(0));
		OUT_RING(ring, CP_WAIT_REG_MEM_3_REF(0x1));
		OUT_RING(ring, CP_WAIT_REG_MEM_4_MASK(0x1));
		OUT_RING(ring, CP_WAIT_REG_MEM_5_DELAY_LOOP_CYCLES(0));

		if (!adreno_is_a7xx(adreno_gpu)) {
			/* Re-enable protected mode: */
			OUT_PKT7(ring, CP_SET_PROTECTED_MODE, 1);
			OUT_RING(ring, 1);
		}
	}
}

static void a6xx_submit(struct msm_gpu *gpu, struct msm_gem_submit *submit)
{
	unsigned int index = submit->seqno % MSM_GPU_SUBMIT_STATS_COUNT;
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	struct msm_ringbuffer *ring = submit->ring;
	unsigned int i, ibs = 0;

	a6xx_set_pagetable(a6xx_gpu, ring, submit);

	get_stats_counter(ring, REG_A6XX_RBBM_PERFCTR_CP(0),
		rbmemptr_stats(ring, index, cpcycles_start));

	/*
	 * For PM4 the GMU register offsets are calculated from the base of the
	 * GPU registers so we need to add 0x1a800 to the register value on A630
	 * to get the right value from PM4.
	 */
	get_stats_counter(ring, REG_A6XX_CP_ALWAYS_ON_COUNTER,
		rbmemptr_stats(ring, index, alwayson_start));

	/* Invalidate CCU depth and color */
	OUT_PKT7(ring, CP_EVENT_WRITE, 1);
	OUT_RING(ring, CP_EVENT_WRITE_0_EVENT(PC_CCU_INVALIDATE_DEPTH));

	OUT_PKT7(ring, CP_EVENT_WRITE, 1);
	OUT_RING(ring, CP_EVENT_WRITE_0_EVENT(PC_CCU_INVALIDATE_COLOR));

	/* Submit the commands */
	for (i = 0; i < submit->nr_cmds; i++) {
		switch (submit->cmd[i].type) {
		case MSM_SUBMIT_CMD_IB_TARGET_BUF:
			break;
		case MSM_SUBMIT_CMD_CTX_RESTORE_BUF:
			if (ring->cur_ctx_seqno == submit->queue->ctx->seqno)
				break;
			fallthrough;
		case MSM_SUBMIT_CMD_BUF:
			OUT_PKT7(ring, CP_INDIRECT_BUFFER_PFE, 3);
			OUT_RING(ring, lower_32_bits(submit->cmd[i].iova));
			OUT_RING(ring, upper_32_bits(submit->cmd[i].iova));
			OUT_RING(ring, submit->cmd[i].size);
			ibs++;
			break;
		}

		/*
		 * Periodically update shadow-wptr if needed, so that we
		 * can see partial progress of submits with large # of
		 * cmds.. otherwise we could needlessly stall waiting for
		 * ringbuffer state, simply due to looking at a shadow
		 * rptr value that has not been updated
		 */
		if ((ibs % 32) == 0)
			update_shadow_rptr(gpu, ring);
	}

	get_stats_counter(ring, REG_A6XX_RBBM_PERFCTR_CP(0),
		rbmemptr_stats(ring, index, cpcycles_end));
	get_stats_counter(ring, REG_A6XX_CP_ALWAYS_ON_COUNTER,
		rbmemptr_stats(ring, index, alwayson_end));

	/* Write the fence to the scratch register */
	OUT_PKT4(ring, REG_A6XX_CP_SCRATCH_REG(2), 1);
	OUT_RING(ring, submit->seqno);

	/*
	 * Execute a CACHE_FLUSH_TS event. This will ensure that the
	 * timestamp is written to the memory and then triggers the interrupt
	 */
	OUT_PKT7(ring, CP_EVENT_WRITE, 4);
	OUT_RING(ring, CP_EVENT_WRITE_0_EVENT(CACHE_FLUSH_TS) |
		CP_EVENT_WRITE_0_IRQ);
	OUT_RING(ring, lower_32_bits(rbmemptr(ring, fence)));
	OUT_RING(ring, upper_32_bits(rbmemptr(ring, fence)));
	OUT_RING(ring, submit->seqno);

	trace_msm_gpu_submit_flush(submit,
		gpu_read64(gpu, REG_A6XX_CP_ALWAYS_ON_COUNTER));

	a6xx_flush(gpu, ring);
}

static void a6xx_emit_set_pseudo_reg(struct msm_ringbuffer *ring,
		struct a6xx_gpu *a6xx_gpu, struct msm_gpu_submitqueue *queue)
{
	u64 preempt_postamble;

	OUT_PKT7(ring, CP_SET_PSEUDO_REG, 12);

	OUT_RING(ring, SMMU_INFO);
	/* don't save SMMU, we write the record from the kernel instead */
	OUT_RING(ring, 0);
	OUT_RING(ring, 0);

	/* privileged and non secure buffer save */
	OUT_RING(ring, NON_SECURE_SAVE_ADDR);
	OUT_RING(ring, lower_32_bits(
		a6xx_gpu->preempt_iova[ring->id]));
	OUT_RING(ring, upper_32_bits(
		a6xx_gpu->preempt_iova[ring->id]));

	/* user context buffer save, seems to be unnused by fw */
	OUT_RING(ring, NON_PRIV_SAVE_ADDR);
	OUT_RING(ring, 0);
	OUT_RING(ring, 0);

	OUT_RING(ring, COUNTER);
	/* seems OK to set to 0 to disable it */
	OUT_RING(ring, 0);
	OUT_RING(ring, 0);

	/* Emit postamble to clear perfcounters */
	preempt_postamble = a6xx_gpu->preempt_postamble_iova;

	OUT_PKT7(ring, CP_SET_AMBLE, 3);
	OUT_RING(ring, lower_32_bits(preempt_postamble));
	OUT_RING(ring, upper_32_bits(preempt_postamble));
	OUT_RING(ring, CP_SET_AMBLE_2_DWORDS(
				 a6xx_gpu->preempt_postamble_len) |
			 CP_SET_AMBLE_2_TYPE(KMD_AMBLE_TYPE));
}

static void a7xx_submit(struct msm_gpu *gpu, struct msm_gem_submit *submit)
{
	unsigned int index = submit->seqno % MSM_GPU_SUBMIT_STATS_COUNT;
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	struct msm_ringbuffer *ring = submit->ring;
	unsigned int i, ibs = 0;

	/*
	 * Toggle concurrent binning for pagetable switch and set the thread to
	 * BR since only it can execute the pagetable switch packets.
	 */
	OUT_PKT7(ring, CP_THREAD_CONTROL, 1);
	OUT_RING(ring, CP_THREAD_CONTROL_0_SYNC_THREADS | CP_SET_THREAD_BR);

	a6xx_set_pagetable(a6xx_gpu, ring, submit);

	/*
	 * If preemption is enabled, then set the pseudo register for the save
	 * sequence
	 */
	if (gpu->nr_rings > 1)
		a6xx_emit_set_pseudo_reg(ring, a6xx_gpu, submit->queue);

	get_stats_counter(ring, REG_A7XX_RBBM_PERFCTR_CP(0),
		rbmemptr_stats(ring, index, cpcycles_start));
	get_stats_counter(ring, REG_A6XX_CP_ALWAYS_ON_COUNTER,
		rbmemptr_stats(ring, index, alwayson_start));

	OUT_PKT7(ring, CP_THREAD_CONTROL, 1);
	OUT_RING(ring, CP_SET_THREAD_BOTH);

	OUT_PKT7(ring, CP_SET_MARKER, 1);
	OUT_RING(ring, 0x101); /* IFPC disable */

	if (submit->queue->flags & MSM_SUBMITQUEUE_ALLOW_PREEMPT) {
		OUT_PKT7(ring, CP_SET_MARKER, 1);
		OUT_RING(ring, 0x00d); /* IB1LIST start */
	}

	/* Submit the commands */
	for (i = 0; i < submit->nr_cmds; i++) {
		switch (submit->cmd[i].type) {
		case MSM_SUBMIT_CMD_IB_TARGET_BUF:
			break;
		case MSM_SUBMIT_CMD_CTX_RESTORE_BUF:
			if (ring->cur_ctx_seqno == submit->queue->ctx->seqno)
				break;
			fallthrough;
		case MSM_SUBMIT_CMD_BUF:
			OUT_PKT7(ring, CP_INDIRECT_BUFFER_PFE, 3);
			OUT_RING(ring, lower_32_bits(submit->cmd[i].iova));
			OUT_RING(ring, upper_32_bits(submit->cmd[i].iova));
			OUT_RING(ring, submit->cmd[i].size);
			ibs++;
			break;
		}

		/*
		 * Periodically update shadow-wptr if needed, so that we
		 * can see partial progress of submits with large # of
		 * cmds.. otherwise we could needlessly stall waiting for
		 * ringbuffer state, simply due to looking at a shadow
		 * rptr value that has not been updated
		 */
		if ((ibs % 32) == 0)
			update_shadow_rptr(gpu, ring);
	}

	if (submit->queue->flags & MSM_SUBMITQUEUE_ALLOW_PREEMPT) {
		OUT_PKT7(ring, CP_SET_MARKER, 1);
		OUT_RING(ring, 0x00e); /* IB1LIST end */
	}

	get_stats_counter(ring, REG_A7XX_RBBM_PERFCTR_CP(0),
		rbmemptr_stats(ring, index, cpcycles_end));
	get_stats_counter(ring, REG_A6XX_CP_ALWAYS_ON_COUNTER,
		rbmemptr_stats(ring, index, alwayson_end));

	/* Write the fence to the scratch register */
	OUT_PKT4(ring, REG_A6XX_CP_SCRATCH_REG(2), 1);
	OUT_RING(ring, submit->seqno);

	OUT_PKT7(ring, CP_THREAD_CONTROL, 1);
	OUT_RING(ring, CP_SET_THREAD_BR);

	OUT_PKT7(ring, CP_EVENT_WRITE, 1);
	OUT_RING(ring, CCU_INVALIDATE_DEPTH);

	OUT_PKT7(ring, CP_EVENT_WRITE, 1);
	OUT_RING(ring, CCU_INVALIDATE_COLOR);

	OUT_PKT7(ring, CP_THREAD_CONTROL, 1);
	OUT_RING(ring, CP_SET_THREAD_BV);

	/*
	 * Make sure the timestamp is committed once BV pipe is
	 * completely done with this submission.
	 */
	OUT_PKT7(ring, CP_EVENT_WRITE, 4);
	OUT_RING(ring, CACHE_CLEAN | BIT(27));
	OUT_RING(ring, lower_32_bits(rbmemptr(ring, bv_fence)));
	OUT_RING(ring, upper_32_bits(rbmemptr(ring, bv_fence)));
	OUT_RING(ring, submit->seqno);

	OUT_PKT7(ring, CP_THREAD_CONTROL, 1);
	OUT_RING(ring, CP_SET_THREAD_BR);

	/*
	 * This makes sure that BR doesn't race ahead and commit
	 * timestamp to memstore while BV is still processing
	 * this submission.
	 */
	OUT_PKT7(ring, CP_WAIT_TIMESTAMP, 4);
	OUT_RING(ring, 0);
	OUT_RING(ring, lower_32_bits(rbmemptr(ring, bv_fence)));
	OUT_RING(ring, upper_32_bits(rbmemptr(ring, bv_fence)));
	OUT_RING(ring, submit->seqno);

	a6xx_gpu->last_seqno[ring->id] = submit->seqno;

	/* write the ringbuffer timestamp */
	OUT_PKT7(ring, CP_EVENT_WRITE, 4);
	OUT_RING(ring, CACHE_CLEAN | CP_EVENT_WRITE_0_IRQ | BIT(27));
	OUT_RING(ring, lower_32_bits(rbmemptr(ring, fence)));
	OUT_RING(ring, upper_32_bits(rbmemptr(ring, fence)));
	OUT_RING(ring, submit->seqno);

	OUT_PKT7(ring, CP_THREAD_CONTROL, 1);
	OUT_RING(ring, CP_SET_THREAD_BOTH);

	OUT_PKT7(ring, CP_SET_MARKER, 1);
	OUT_RING(ring, 0x100); /* IFPC enable */

	/* If preemption is enabled */
	if (gpu->nr_rings > 1) {
		/* Yield the floor on command completion */
		OUT_PKT7(ring, CP_CONTEXT_SWITCH_YIELD, 4);

		/*
		 * If dword[2:1] are non zero, they specify an address for
		 * the CP to write the value of dword[3] to on preemption
		 * complete. Write 0 to skip the write
		 */
		OUT_RING(ring, 0x00);
		OUT_RING(ring, 0x00);
		/* Data value - not used if the address above is 0 */
		OUT_RING(ring, 0x01);
		/* generate interrupt on preemption completion */
		OUT_RING(ring, 0x00);
	}


	trace_msm_gpu_submit_flush(submit,
		gpu_read64(gpu, REG_A6XX_CP_ALWAYS_ON_COUNTER));

	a6xx_flush(gpu, ring);

	/* Check to see if we need to start preemption */
	a6xx_preempt_trigger(gpu);
}

static void a6xx_set_hwcg(struct msm_gpu *gpu, bool state)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	struct a6xx_gmu *gmu = &a6xx_gpu->gmu;
	const struct adreno_reglist *reg;
	unsigned int i;
	u32 cgc_delay, cgc_hyst;
	u32 val, clock_cntl_on;

	if (!(adreno_gpu->info->a6xx->hwcg || adreno_is_a7xx(adreno_gpu)))
		return;

	if (adreno_is_a630(adreno_gpu))
		clock_cntl_on = 0x8aa8aa02;
	else if (adreno_is_a610(adreno_gpu))
		clock_cntl_on = 0xaaa8aa82;
	else if (adreno_is_a702(adreno_gpu))
		clock_cntl_on = 0xaaaaaa82;
	else
		clock_cntl_on = 0x8aa8aa82;

	cgc_delay = adreno_is_a615_family(adreno_gpu) ? 0x111 : 0x10111;
	cgc_hyst = adreno_is_a615_family(adreno_gpu) ? 0x555 : 0x5555;

	gmu_write(&a6xx_gpu->gmu, REG_A6XX_GPU_GMU_AO_GMU_CGC_MODE_CNTL,
			state ? adreno_gpu->info->a6xx->gmu_cgc_mode : 0);
	gmu_write(&a6xx_gpu->gmu, REG_A6XX_GPU_GMU_AO_GMU_CGC_DELAY_CNTL,
			state ? cgc_delay : 0);
	gmu_write(&a6xx_gpu->gmu, REG_A6XX_GPU_GMU_AO_GMU_CGC_HYST_CNTL,
			state ? cgc_hyst : 0);

	if (!adreno_gpu->info->a6xx->hwcg) {
		gpu_write(gpu, REG_A7XX_RBBM_CLOCK_CNTL_GLOBAL, 1);
		gpu_write(gpu, REG_A7XX_RBBM_CGC_GLOBAL_LOAD_CMD, state ? 1 : 0);

		if (state) {
			gpu_write(gpu, REG_A7XX_RBBM_CGC_P2S_TRIG_CMD, 1);

			if (gpu_poll_timeout(gpu, REG_A7XX_RBBM_CGC_P2S_STATUS, val,
					     val & A7XX_RBBM_CGC_P2S_STATUS_TXDONE, 1, 10)) {
				dev_err(&gpu->pdev->dev, "RBBM_CGC_P2S_STATUS TXDONE Poll failed\n");
				return;
			}

			gpu_write(gpu, REG_A7XX_RBBM_CLOCK_CNTL_GLOBAL, 0);
		}

		return;
	}

	val = gpu_read(gpu, REG_A6XX_RBBM_CLOCK_CNTL);

	/* Don't re-program the registers if they are already correct */
	if ((!state && !val) || (state && (val == clock_cntl_on)))
		return;

	/* Disable SP clock before programming HWCG registers */
	if (!adreno_is_a610_family(adreno_gpu) && !adreno_is_a7xx(adreno_gpu))
		gmu_rmw(gmu, REG_A6XX_GPU_GMU_GX_SPTPRAC_CLOCK_CONTROL, 1, 0);

	for (i = 0; (reg = &adreno_gpu->info->a6xx->hwcg[i], reg->offset); i++)
		gpu_write(gpu, reg->offset, state ? reg->value : 0);

	/* Enable SP clock */
	if (!adreno_is_a610_family(adreno_gpu) && !adreno_is_a7xx(adreno_gpu))
		gmu_rmw(gmu, REG_A6XX_GPU_GMU_GX_SPTPRAC_CLOCK_CONTROL, 0, 1);

	gpu_write(gpu, REG_A6XX_RBBM_CLOCK_CNTL, state ? clock_cntl_on : 0);
}

static void a6xx_set_cp_protect(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	const struct adreno_protect *protect = adreno_gpu->info->a6xx->protect;
	unsigned i;

	/*
	 * Enable access protection to privileged registers, fault on an access
	 * protect violation and select the last span to protect from the start
	 * address all the way to the end of the register address space
	 */
	gpu_write(gpu, REG_A6XX_CP_PROTECT_CNTL,
		  A6XX_CP_PROTECT_CNTL_ACCESS_PROT_EN |
		  A6XX_CP_PROTECT_CNTL_ACCESS_FAULT_ON_VIOL_EN |
		  A6XX_CP_PROTECT_CNTL_LAST_SPAN_INF_RANGE);

	for (i = 0; i < protect->count - 1; i++) {
		/* Intentionally skip writing to some registers */
		if (protect->regs[i])
			gpu_write(gpu, REG_A6XX_CP_PROTECT(i), protect->regs[i]);
	}
	/* last CP_PROTECT to have "infinite" length on the last entry */
	gpu_write(gpu, REG_A6XX_CP_PROTECT(protect->count_max - 1), protect->regs[i]);
}

static void a6xx_calc_ubwc_config(struct adreno_gpu *gpu)
{
	gpu->ubwc_config.rgb565_predicator = 0;
	gpu->ubwc_config.uavflagprd_inv = 0;
	gpu->ubwc_config.min_acc_len = 0;
	gpu->ubwc_config.ubwc_swizzle = 0x6;
	gpu->ubwc_config.macrotile_mode = 0;
	gpu->ubwc_config.highest_bank_bit = 15;

	if (adreno_is_a610(gpu)) {
		gpu->ubwc_config.highest_bank_bit = 13;
		gpu->ubwc_config.min_acc_len = 1;
		gpu->ubwc_config.ubwc_swizzle = 0x7;
	}

	if (adreno_is_a618(gpu))
		gpu->ubwc_config.highest_bank_bit = 14;

	if (adreno_is_a619(gpu))
		/* TODO: Should be 14 but causes corruption at e.g. 1920x1200 on DP */
		gpu->ubwc_config.highest_bank_bit = 13;

	if (adreno_is_a619_holi(gpu))
		gpu->ubwc_config.highest_bank_bit = 13;

	if (adreno_is_a621(gpu)) {
		gpu->ubwc_config.highest_bank_bit = 13;
		gpu->ubwc_config.amsbc = 1;
		gpu->ubwc_config.uavflagprd_inv = 2;
	}

	if (adreno_is_a623(gpu)) {
		gpu->ubwc_config.highest_bank_bit = 16;
		gpu->ubwc_config.amsbc = 1;
		gpu->ubwc_config.rgb565_predicator = 1;
		gpu->ubwc_config.uavflagprd_inv = 2;
		gpu->ubwc_config.macrotile_mode = 1;
	}

	if (adreno_is_a640_family(gpu))
		gpu->ubwc_config.amsbc = 1;

	if (adreno_is_a680(gpu))
		gpu->ubwc_config.macrotile_mode = 1;

	if (adreno_is_a650(gpu) ||
	    adreno_is_a660(gpu) ||
	    adreno_is_a690(gpu) ||
	    adreno_is_a730(gpu) ||
	    adreno_is_a740_family(gpu)) {
		/* TODO: get ddr type from bootloader and use 2 for LPDDR4 */
		gpu->ubwc_config.highest_bank_bit = 16;
		gpu->ubwc_config.amsbc = 1;
		gpu->ubwc_config.rgb565_predicator = 1;
		gpu->ubwc_config.uavflagprd_inv = 2;
		gpu->ubwc_config.macrotile_mode = 1;
	}

	if (adreno_is_a663(gpu)) {
		gpu->ubwc_config.highest_bank_bit = 13;
		gpu->ubwc_config.amsbc = 1;
		gpu->ubwc_config.rgb565_predicator = 1;
		gpu->ubwc_config.uavflagprd_inv = 2;
		gpu->ubwc_config.macrotile_mode = 1;
		gpu->ubwc_config.ubwc_swizzle = 0x4;
	}

	if (adreno_is_7c3(gpu)) {
		gpu->ubwc_config.highest_bank_bit = 14;
		gpu->ubwc_config.amsbc = 1;
		gpu->ubwc_config.rgb565_predicator = 1;
		gpu->ubwc_config.uavflagprd_inv = 2;
		gpu->ubwc_config.macrotile_mode = 1;
	}

	if (adreno_is_a702(gpu)) {
		gpu->ubwc_config.highest_bank_bit = 14;
		gpu->ubwc_config.min_acc_len = 1;
	}
}

static void a6xx_set_ubwc_config(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	/*
	 * We subtract 13 from the highest bank bit (13 is the minimum value
	 * allowed by hw) and write the lowest two bits of the remaining value
	 * as hbb_lo and the one above it as hbb_hi to the hardware.
	 */
	BUG_ON(adreno_gpu->ubwc_config.highest_bank_bit < 13);
	u32 hbb = adreno_gpu->ubwc_config.highest_bank_bit - 13;
	u32 hbb_hi = hbb >> 2;
	u32 hbb_lo = hbb & 3;
	u32 ubwc_mode = adreno_gpu->ubwc_config.ubwc_swizzle & 1;
	u32 level2_swizzling_dis = !(adreno_gpu->ubwc_config.ubwc_swizzle & 2);

	gpu_write(gpu, REG_A6XX_RB_NC_MODE_CNTL,
		  level2_swizzling_dis << 12 |
		  adreno_gpu->ubwc_config.rgb565_predicator << 11 |
		  hbb_hi << 10 | adreno_gpu->ubwc_config.amsbc << 4 |
		  adreno_gpu->ubwc_config.min_acc_len << 3 |
		  hbb_lo << 1 | ubwc_mode);

	gpu_write(gpu, REG_A6XX_TPL1_NC_MODE_CNTL,
		  level2_swizzling_dis << 6 | hbb_hi << 4 |
		  adreno_gpu->ubwc_config.min_acc_len << 3 |
		  hbb_lo << 1 | ubwc_mode);

	gpu_write(gpu, REG_A6XX_SP_NC_MODE_CNTL,
		  level2_swizzling_dis << 12 | hbb_hi << 10 |
		  adreno_gpu->ubwc_config.uavflagprd_inv << 4 |
		  adreno_gpu->ubwc_config.min_acc_len << 3 |
		  hbb_lo << 1 | ubwc_mode);

	if (adreno_is_a7xx(adreno_gpu))
		gpu_write(gpu, REG_A7XX_GRAS_NC_MODE_CNTL,
			  FIELD_PREP(GENMASK(8, 5), hbb_lo));

	gpu_write(gpu, REG_A6XX_UCHE_MODE_CNTL,
		  adreno_gpu->ubwc_config.min_acc_len << 23 | hbb_lo << 21);

	gpu_write(gpu, REG_A6XX_RBBM_NC_MODE_CNTL,
		  adreno_gpu->ubwc_config.macrotile_mode);
}

static void a7xx_patch_pwrup_reglist(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	const struct adreno_reglist_list *reglist;
	void *ptr = a6xx_gpu->pwrup_reglist_ptr;
	struct cpu_gpu_lock *lock = ptr;
	u32 *dest = (u32 *)&lock->regs[0];
	int i;

	reglist = adreno_gpu->info->a6xx->pwrup_reglist;

	lock->gpu_req = lock->cpu_req = lock->turn = 0;
	lock->ifpc_list_len = 0;
	lock->preemption_list_len = reglist->count;

	/*
	 * For each entry in each of the lists, write the offset and the current
	 * register value into the GPU buffer
	 */
	for (i = 0; i < reglist->count; i++) {
		*dest++ = reglist->regs[i];
		*dest++ = gpu_read(gpu, reglist->regs[i]);
	}

	/*
	 * The overall register list is composed of
	 * 1. Static IFPC-only registers
	 * 2. Static IFPC + preemption registers
	 * 3. Dynamic IFPC + preemption registers (ex: perfcounter selects)
	 *
	 * The first two lists are static. Size of these lists are stored as
	 * number of pairs in ifpc_list_len and preemption_list_len
	 * respectively. With concurrent binning, Some of the perfcounter
	 * registers being virtualized, CP needs to know the pipe id to program
	 * the aperture inorder to restore the same. Thus, third list is a
	 * dynamic list with triplets as
	 * (<aperture, shifted 12 bits> <address> <data>), and the length is
	 * stored as number for triplets in dynamic_list_len.
	 */
	lock->dynamic_list_len = 0;
}

static int a7xx_preempt_start(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	struct msm_ringbuffer *ring = gpu->rb[0];

	if (gpu->nr_rings <= 1)
		return 0;

	/* Turn CP protection off */
	OUT_PKT7(ring, CP_SET_PROTECTED_MODE, 1);
	OUT_RING(ring, 0);

	a6xx_emit_set_pseudo_reg(ring, a6xx_gpu, NULL);

	/* Yield the floor on command completion */
	OUT_PKT7(ring, CP_CONTEXT_SWITCH_YIELD, 4);
	OUT_RING(ring, 0x00);
	OUT_RING(ring, 0x00);
	OUT_RING(ring, 0x00);
	/* Generate interrupt on preemption completion */
	OUT_RING(ring, 0x00);

	a6xx_flush(gpu, ring);

	return a6xx_idle(gpu, ring) ? 0 : -EINVAL;
}

static int a6xx_cp_init(struct msm_gpu *gpu)
{
	struct msm_ringbuffer *ring = gpu->rb[0];

	OUT_PKT7(ring, CP_ME_INIT, 8);

	OUT_RING(ring, 0x0000002f);

	/* Enable multiple hardware contexts */
	OUT_RING(ring, 0x00000003);

	/* Enable error detection */
	OUT_RING(ring, 0x20000000);

	/* Don't enable header dump */
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);

	/* No workarounds enabled */
	OUT_RING(ring, 0x00000000);

	/* Pad rest of the cmds with 0's */
	OUT_RING(ring, 0x00000000);
	OUT_RING(ring, 0x00000000);

	a6xx_flush(gpu, ring);
	return a6xx_idle(gpu, ring) ? 0 : -EINVAL;
}

static int a7xx_cp_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	struct msm_ringbuffer *ring = gpu->rb[0];
	u32 mask;

	/* Disable concurrent binning before sending CP init */
	OUT_PKT7(ring, CP_THREAD_CONTROL, 1);
	OUT_RING(ring, BIT(27));

	OUT_PKT7(ring, CP_ME_INIT, 7);

	/* Use multiple HW contexts */
	mask = BIT(0);

	/* Enable error detection */
	mask |= BIT(1);

	/* Set default reset state */
	mask |= BIT(3);

	/* Disable save/restore of performance counters across preemption */
	mask |= BIT(6);

	/* Enable the register init list with the spinlock */
	mask |= BIT(8);

	OUT_RING(ring, mask);

	/* Enable multiple hardware contexts */
	OUT_RING(ring, 0x00000003);

	/* Enable error detection */
	OUT_RING(ring, 0x20000000);

	/* Operation mode mask */
	OUT_RING(ring, 0x00000002);

	/* *Don't* send a power up reg list for concurrent binning (TODO) */
	/* Lo address */
	OUT_RING(ring, lower_32_bits(a6xx_gpu->pwrup_reglist_iova));
	/* Hi address */
	OUT_RING(ring, upper_32_bits(a6xx_gpu->pwrup_reglist_iova));
	/* BIT(31) set => read the regs from the list */
	OUT_RING(ring, BIT(31));

	a6xx_flush(gpu, ring);
	return a6xx_idle(gpu, ring) ? 0 : -EINVAL;
}

/*
 * Check that the microcode version is new enough to include several key
 * security fixes. Return true if the ucode is safe.
 */
static bool a6xx_ucode_check_version(struct a6xx_gpu *a6xx_gpu,
		struct drm_gem_object *obj)
{
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	struct msm_gpu *gpu = &adreno_gpu->base;
	const char *sqe_name = adreno_gpu->info->fw[ADRENO_FW_SQE];
	u32 *buf = msm_gem_get_vaddr(obj);
	bool ret = false;

	if (IS_ERR(buf))
		return false;

	/* A7xx is safe! */
	if (adreno_is_a7xx(adreno_gpu) || adreno_is_a702(adreno_gpu))
		return true;

	/*
	 * Targets up to a640 (a618, a630 and a640) need to check for a
	 * microcode version that is patched to support the whereami opcode or
	 * one that is new enough to include it by default.
	 *
	 * a650 tier targets don't need whereami but still need to be
	 * equal to or newer than 0.95 for other security fixes
	 *
	 * a660 targets have all the critical security fixes from the start
	 */
	if (!strcmp(sqe_name, "a630_sqe.fw")) {
		/*
		 * If the lowest nibble is 0xa that is an indication that this
		 * microcode has been patched. The actual version is in dword
		 * [3] but we only care about the patchlevel which is the lowest
		 * nibble of dword [3]
		 *
		 * Otherwise check that the firmware is greater than or equal
		 * to 1.90 which was the first version that had this fix built
		 * in
		 */
		if ((((buf[0] & 0xf) == 0xa) && (buf[2] & 0xf) >= 1) ||
			(buf[0] & 0xfff) >= 0x190) {
			a6xx_gpu->has_whereami = true;
			ret = true;
			goto out;
		}

		DRM_DEV_ERROR(&gpu->pdev->dev,
			"a630 SQE ucode is too old. Have version %x need at least %x\n",
			buf[0] & 0xfff, 0x190);
	} else if (!strcmp(sqe_name, "a650_sqe.fw")) {
		if ((buf[0] & 0xfff) >= 0x095) {
			ret = true;
			goto out;
		}

		DRM_DEV_ERROR(&gpu->pdev->dev,
			"a650 SQE ucode is too old. Have version %x need at least %x\n",
			buf[0] & 0xfff, 0x095);
	} else if (!strcmp(sqe_name, "a660_sqe.fw")) {
		ret = true;
	} else {
		DRM_DEV_ERROR(&gpu->pdev->dev,
			"unknown GPU, add it to a6xx_ucode_check_version()!!\n");
	}
out:
	msm_gem_put_vaddr(obj);
	return ret;
}

static int a6xx_ucode_load(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);

	if (!a6xx_gpu->sqe_bo) {
		a6xx_gpu->sqe_bo = adreno_fw_create_bo(gpu,
			adreno_gpu->fw[ADRENO_FW_SQE], &a6xx_gpu->sqe_iova);

		if (IS_ERR(a6xx_gpu->sqe_bo)) {
			int ret = PTR_ERR(a6xx_gpu->sqe_bo);

			a6xx_gpu->sqe_bo = NULL;
			DRM_DEV_ERROR(&gpu->pdev->dev,
				"Could not allocate SQE ucode: %d\n", ret);

			return ret;
		}

		msm_gem_object_set_name(a6xx_gpu->sqe_bo, "sqefw");
		if (!a6xx_ucode_check_version(a6xx_gpu, a6xx_gpu->sqe_bo)) {
			msm_gem_unpin_iova(a6xx_gpu->sqe_bo, gpu->aspace);
			drm_gem_object_put(a6xx_gpu->sqe_bo);

			a6xx_gpu->sqe_bo = NULL;
			return -EPERM;
		}
	}

	/*
	 * Expanded APRIV and targets that support WHERE_AM_I both need a
	 * privileged buffer to store the RPTR shadow
	 */
	if ((adreno_gpu->base.hw_apriv || a6xx_gpu->has_whereami) &&
	    !a6xx_gpu->shadow_bo) {
		a6xx_gpu->shadow = msm_gem_kernel_new(gpu->dev,
						      sizeof(u32) * gpu->nr_rings,
						      MSM_BO_WC | MSM_BO_MAP_PRIV,
						      gpu->aspace, &a6xx_gpu->shadow_bo,
						      &a6xx_gpu->shadow_iova);

		if (IS_ERR(a6xx_gpu->shadow))
			return PTR_ERR(a6xx_gpu->shadow);

		msm_gem_object_set_name(a6xx_gpu->shadow_bo, "shadow");
	}

	a6xx_gpu->pwrup_reglist_ptr = msm_gem_kernel_new(gpu->dev, PAGE_SIZE,
							 MSM_BO_WC  | MSM_BO_MAP_PRIV,
							 gpu->aspace, &a6xx_gpu->pwrup_reglist_bo,
							 &a6xx_gpu->pwrup_reglist_iova);

	if (IS_ERR(a6xx_gpu->pwrup_reglist_ptr))
		return PTR_ERR(a6xx_gpu->pwrup_reglist_ptr);

	msm_gem_object_set_name(a6xx_gpu->pwrup_reglist_bo, "pwrup_reglist");

	return 0;
}

static int a6xx_zap_shader_init(struct msm_gpu *gpu)
{
	static bool loaded;
	int ret;

	if (loaded)
		return 0;

	ret = adreno_zap_shader_load(gpu, GPU_PAS_ID);

	loaded = !ret;
	return ret;
}

#define A6XX_INT_MASK (A6XX_RBBM_INT_0_MASK_CP_AHB_ERROR | \
		       A6XX_RBBM_INT_0_MASK_RBBM_ATB_ASYNCFIFO_OVERFLOW | \
		       A6XX_RBBM_INT_0_MASK_CP_HW_ERROR | \
		       A6XX_RBBM_INT_0_MASK_CP_IB2 | \
		       A6XX_RBBM_INT_0_MASK_CP_IB1 | \
		       A6XX_RBBM_INT_0_MASK_CP_RB | \
		       A6XX_RBBM_INT_0_MASK_CP_CACHE_FLUSH_TS | \
		       A6XX_RBBM_INT_0_MASK_RBBM_ATB_BUS_OVERFLOW | \
		       A6XX_RBBM_INT_0_MASK_RBBM_HANG_DETECT | \
		       A6XX_RBBM_INT_0_MASK_UCHE_OOB_ACCESS | \
		       A6XX_RBBM_INT_0_MASK_UCHE_TRAP_INTR)

#define A7XX_INT_MASK (A6XX_RBBM_INT_0_MASK_CP_AHB_ERROR | \
		       A6XX_RBBM_INT_0_MASK_RBBM_ATB_ASYNCFIFO_OVERFLOW | \
		       A6XX_RBBM_INT_0_MASK_RBBM_GPC_ERROR | \
		       A6XX_RBBM_INT_0_MASK_CP_SW | \
		       A6XX_RBBM_INT_0_MASK_CP_HW_ERROR | \
		       A6XX_RBBM_INT_0_MASK_PM4CPINTERRUPT | \
		       A6XX_RBBM_INT_0_MASK_CP_RB_DONE_TS | \
		       A6XX_RBBM_INT_0_MASK_CP_CACHE_FLUSH_TS | \
		       A6XX_RBBM_INT_0_MASK_RBBM_ATB_BUS_OVERFLOW | \
		       A6XX_RBBM_INT_0_MASK_RBBM_HANG_DETECT | \
		       A6XX_RBBM_INT_0_MASK_UCHE_OOB_ACCESS | \
		       A6XX_RBBM_INT_0_MASK_UCHE_TRAP_INTR | \
		       A6XX_RBBM_INT_0_MASK_TSBWRITEERROR | \
		       A6XX_RBBM_INT_0_MASK_SWFUSEVIOLATION)

#define A7XX_APRIV_MASK (A6XX_CP_APRIV_CNTL_ICACHE | \
			 A6XX_CP_APRIV_CNTL_RBFETCH | \
			 A6XX_CP_APRIV_CNTL_RBPRIVLEVEL | \
			 A6XX_CP_APRIV_CNTL_RBRPWB)

#define A7XX_BR_APRIVMASK (A7XX_APRIV_MASK | \
			   A6XX_CP_APRIV_CNTL_CDREAD | \
			   A6XX_CP_APRIV_CNTL_CDWRITE)

static int hw_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	struct a6xx_gmu *gmu = &a6xx_gpu->gmu;
	u64 gmem_range_min;
	unsigned int i;
	int ret;

	if (!adreno_has_gmu_wrapper(adreno_gpu)) {
		/* Make sure the GMU keeps the GPU on while we set it up */
		ret = a6xx_gmu_set_oob(&a6xx_gpu->gmu, GMU_OOB_GPU_SET);
		if (ret)
			return ret;
	}

	/* Clear GBIF halt in case GX domain was not collapsed */
	if (adreno_is_a619_holi(adreno_gpu)) {
		gpu_write(gpu, REG_A6XX_GBIF_HALT, 0);
		gpu_read(gpu, REG_A6XX_GBIF_HALT);

		gpu_write(gpu, REG_A6XX_RBBM_GPR0_CNTL, 0);
		gpu_read(gpu, REG_A6XX_RBBM_GPR0_CNTL);
	} else if (a6xx_has_gbif(adreno_gpu)) {
		gpu_write(gpu, REG_A6XX_GBIF_HALT, 0);
		gpu_read(gpu, REG_A6XX_GBIF_HALT);

		gpu_write(gpu, REG_A6XX_RBBM_GBIF_HALT, 0);
		gpu_read(gpu, REG_A6XX_RBBM_GBIF_HALT);
	}

	gpu_write(gpu, REG_A6XX_RBBM_SECVID_TSB_CNTL, 0);

	if (adreno_is_a619_holi(adreno_gpu))
		a6xx_sptprac_enable(gmu);

	/*
	 * Disable the trusted memory range - we don't actually supported secure
	 * memory rendering at this point in time and we don't want to block off
	 * part of the virtual memory space.
	 */
	gpu_write64(gpu, REG_A6XX_RBBM_SECVID_TSB_TRUSTED_BASE, 0x00000000);
	gpu_write(gpu, REG_A6XX_RBBM_SECVID_TSB_TRUSTED_SIZE, 0x00000000);

	if (!adreno_is_a7xx(adreno_gpu)) {
		/* Turn on 64 bit addressing for all blocks */
		gpu_write(gpu, REG_A6XX_CP_ADDR_MODE_CNTL, 0x1);
		gpu_write(gpu, REG_A6XX_VSC_ADDR_MODE_CNTL, 0x1);
		gpu_write(gpu, REG_A6XX_GRAS_ADDR_MODE_CNTL, 0x1);
		gpu_write(gpu, REG_A6XX_RB_ADDR_MODE_CNTL, 0x1);
		gpu_write(gpu, REG_A6XX_PC_ADDR_MODE_CNTL, 0x1);
		gpu_write(gpu, REG_A6XX_HLSQ_ADDR_MODE_CNTL, 0x1);
		gpu_write(gpu, REG_A6XX_VFD_ADDR_MODE_CNTL, 0x1);
		gpu_write(gpu, REG_A6XX_VPC_ADDR_MODE_CNTL, 0x1);
		gpu_write(gpu, REG_A6XX_UCHE_ADDR_MODE_CNTL, 0x1);
		gpu_write(gpu, REG_A6XX_SP_ADDR_MODE_CNTL, 0x1);
		gpu_write(gpu, REG_A6XX_TPL1_ADDR_MODE_CNTL, 0x1);
		gpu_write(gpu, REG_A6XX_RBBM_SECVID_TSB_ADDR_MODE_CNTL, 0x1);
	}

	/* enable hardware clockgating */
	a6xx_set_hwcg(gpu, true);

	/* VBIF/GBIF start*/
	if (adreno_is_a610_family(adreno_gpu) ||
	    adreno_is_a640_family(adreno_gpu) ||
	    adreno_is_a650_family(adreno_gpu) ||
	    adreno_is_a7xx(adreno_gpu)) {
		gpu_write(gpu, REG_A6XX_GBIF_QSB_SIDE0, 0x00071620);
		gpu_write(gpu, REG_A6XX_GBIF_QSB_SIDE1, 0x00071620);
		gpu_write(gpu, REG_A6XX_GBIF_QSB_SIDE2, 0x00071620);
		gpu_write(gpu, REG_A6XX_GBIF_QSB_SIDE3, 0x00071620);
		gpu_write(gpu, REG_A6XX_RBBM_GBIF_CLIENT_QOS_CNTL,
			  adreno_is_a7xx(adreno_gpu) ? 0x2120212 : 0x3);
	} else {
		gpu_write(gpu, REG_A6XX_RBBM_VBIF_CLIENT_QOS_CNTL, 0x3);
	}

	if (adreno_is_a630(adreno_gpu))
		gpu_write(gpu, REG_A6XX_VBIF_GATE_OFF_WRREQ_EN, 0x00000009);

	if (adreno_is_a7xx(adreno_gpu))
		gpu_write(gpu, REG_A6XX_UCHE_GBIF_GX_CONFIG, 0x10240e0);

	/* Make all blocks contribute to the GPU BUSY perf counter */
	gpu_write(gpu, REG_A6XX_RBBM_PERFCTR_GPU_BUSY_MASKED, 0xffffffff);

	/* Disable L2 bypass in the UCHE */
	if (adreno_is_a7xx(adreno_gpu)) {
		gpu_write64(gpu, REG_A6XX_UCHE_TRAP_BASE, adreno_gpu->uche_trap_base);
		gpu_write64(gpu, REG_A6XX_UCHE_WRITE_THRU_BASE, adreno_gpu->uche_trap_base);
	} else {
		gpu_write64(gpu, REG_A6XX_UCHE_WRITE_RANGE_MAX, adreno_gpu->uche_trap_base + 0xfc0);
		gpu_write64(gpu, REG_A6XX_UCHE_TRAP_BASE, adreno_gpu->uche_trap_base);
		gpu_write64(gpu, REG_A6XX_UCHE_WRITE_THRU_BASE, adreno_gpu->uche_trap_base);
	}

	if (!(adreno_is_a650_family(adreno_gpu) ||
	      adreno_is_a702(adreno_gpu) ||
	      adreno_is_a730(adreno_gpu))) {
		gmem_range_min = adreno_is_a740_family(adreno_gpu) ? SZ_16M : SZ_1M;

		/* Set the GMEM VA range [0x100000:0x100000 + gpu->gmem - 1] */
		gpu_write64(gpu, REG_A6XX_UCHE_GMEM_RANGE_MIN, gmem_range_min);

		gpu_write64(gpu, REG_A6XX_UCHE_GMEM_RANGE_MAX,
			gmem_range_min + adreno_gpu->info->gmem - 1);
	}

	if (adreno_is_a7xx(adreno_gpu))
		gpu_write(gpu, REG_A6XX_UCHE_CACHE_WAYS, BIT(23));
	else {
		gpu_write(gpu, REG_A6XX_UCHE_FILTER_CNTL, 0x804);
		gpu_write(gpu, REG_A6XX_UCHE_CACHE_WAYS, 0x4);
	}

	if (adreno_is_a640_family(adreno_gpu) || adreno_is_a650_family(adreno_gpu)) {
		gpu_write(gpu, REG_A6XX_CP_ROQ_THRESHOLDS_2, 0x02000140);
		gpu_write(gpu, REG_A6XX_CP_ROQ_THRESHOLDS_1, 0x8040362c);
	} else if (adreno_is_a610_family(adreno_gpu)) {
		gpu_write(gpu, REG_A6XX_CP_ROQ_THRESHOLDS_2, 0x00800060);
		gpu_write(gpu, REG_A6XX_CP_ROQ_THRESHOLDS_1, 0x40201b16);
	} else if (!adreno_is_a7xx(adreno_gpu)) {
		gpu_write(gpu, REG_A6XX_CP_ROQ_THRESHOLDS_2, 0x010000c0);
		gpu_write(gpu, REG_A6XX_CP_ROQ_THRESHOLDS_1, 0x8040362c);
	}

	if (adreno_is_a660_family(adreno_gpu))
		gpu_write(gpu, REG_A6XX_CP_LPAC_PROG_FIFO_SIZE, 0x00000020);

	/* Setting the mem pool size */
	if (adreno_is_a610(adreno_gpu)) {
		gpu_write(gpu, REG_A6XX_CP_MEM_POOL_SIZE, 48);
		gpu_write(gpu, REG_A6XX_CP_MEM_POOL_DBG_ADDR, 47);
	} else if (adreno_is_a702(adreno_gpu)) {
		gpu_write(gpu, REG_A6XX_CP_MEM_POOL_SIZE, 64);
		gpu_write(gpu, REG_A6XX_CP_MEM_POOL_DBG_ADDR, 63);
	} else if (!adreno_is_a7xx(adreno_gpu))
		gpu_write(gpu, REG_A6XX_CP_MEM_POOL_SIZE, 128);


	/* Set the default primFifo threshold values */
	if (adreno_gpu->info->a6xx->prim_fifo_threshold)
		gpu_write(gpu, REG_A6XX_PC_DBG_ECO_CNTL,
			  adreno_gpu->info->a6xx->prim_fifo_threshold);

	/* Set the AHB default slave response to "ERROR" */
	gpu_write(gpu, REG_A6XX_CP_AHB_CNTL, 0x1);

	/* Turn on performance counters */
	gpu_write(gpu, REG_A6XX_RBBM_PERFCTR_CNTL, 0x1);

	if (adreno_is_a7xx(adreno_gpu)) {
		/* Turn on the IFPC counter (countable 4 on XOCLK4) */
		gmu_write(&a6xx_gpu->gmu, REG_A6XX_GMU_CX_GMU_POWER_COUNTER_SELECT_1,
			  FIELD_PREP(GENMASK(7, 0), 0x4));
	}

	/* Select CP0 to always count cycles */
	gpu_write(gpu, REG_A6XX_CP_PERFCTR_CP_SEL(0), PERF_CP_ALWAYS_COUNT);

	a6xx_set_ubwc_config(gpu);

	/* Enable fault detection */
	if (adreno_is_a730(adreno_gpu) ||
	    adreno_is_a740_family(adreno_gpu))
		gpu_write(gpu, REG_A6XX_RBBM_INTERFACE_HANG_INT_CNTL, (1 << 30) | 0xcfffff);
	else if (adreno_is_a690(adreno_gpu))
		gpu_write(gpu, REG_A6XX_RBBM_INTERFACE_HANG_INT_CNTL, (1 << 30) | 0x4fffff);
	else if (adreno_is_a619(adreno_gpu))
		gpu_write(gpu, REG_A6XX_RBBM_INTERFACE_HANG_INT_CNTL, (1 << 30) | 0x3fffff);
	else if (adreno_is_a610(adreno_gpu) || adreno_is_a702(adreno_gpu))
		gpu_write(gpu, REG_A6XX_RBBM_INTERFACE_HANG_INT_CNTL, (1 << 30) | 0x3ffff);
	else
		gpu_write(gpu, REG_A6XX_RBBM_INTERFACE_HANG_INT_CNTL, (1 << 30) | 0x1fffff);

	gpu_write(gpu, REG_A6XX_UCHE_CLIENT_PF, BIT(7) | 0x1);

	/* Set weights for bicubic filtering */
	if (adreno_is_a650_family(adreno_gpu) || adreno_is_x185(adreno_gpu)) {
		gpu_write(gpu, REG_A6XX_TPL1_BICUBIC_WEIGHTS_TABLE_0, 0);
		gpu_write(gpu, REG_A6XX_TPL1_BICUBIC_WEIGHTS_TABLE_1,
			0x3fe05ff4);
		gpu_write(gpu, REG_A6XX_TPL1_BICUBIC_WEIGHTS_TABLE_2,
			0x3fa0ebee);
		gpu_write(gpu, REG_A6XX_TPL1_BICUBIC_WEIGHTS_TABLE_3,
			0x3f5193ed);
		gpu_write(gpu, REG_A6XX_TPL1_BICUBIC_WEIGHTS_TABLE_4,
			0x3f0243f0);
	}

	/* Set up the CX GMU counter 0 to count busy ticks */
	gmu_write(gmu, REG_A6XX_GPU_GMU_AO_GPU_CX_BUSY_MASK, 0xff000000);

	/* Enable the power counter */
	gmu_rmw(gmu, REG_A6XX_GMU_CX_GMU_POWER_COUNTER_SELECT_0, 0xff, BIT(5));
	gmu_write(gmu, REG_A6XX_GMU_CX_GMU_POWER_COUNTER_ENABLE, 1);

	/* Protect registers from the CP */
	a6xx_set_cp_protect(gpu);

	if (adreno_is_a660_family(adreno_gpu)) {
		if (adreno_is_a690(adreno_gpu))
			gpu_write(gpu, REG_A6XX_CP_CHICKEN_DBG, 0x00028801);
		else
			gpu_write(gpu, REG_A6XX_CP_CHICKEN_DBG, 0x1);
		gpu_write(gpu, REG_A6XX_RBBM_GBIF_CLIENT_QOS_CNTL, 0x0);
	} else if (adreno_is_a702(adreno_gpu)) {
		/* Something to do with the HLSQ cluster */
		gpu_write(gpu, REG_A6XX_CP_CHICKEN_DBG, BIT(24));
	}

	if (adreno_is_a690(adreno_gpu))
		gpu_write(gpu, REG_A6XX_UCHE_CMDQ_CONFIG, 0x90);
	/* Set dualQ + disable afull for A660 GPU */
	else if (adreno_is_a660(adreno_gpu) || adreno_is_a663(adreno_gpu))
		gpu_write(gpu, REG_A6XX_UCHE_CMDQ_CONFIG, 0x66906);
	else if (adreno_is_a7xx(adreno_gpu))
		gpu_write(gpu, REG_A6XX_UCHE_CMDQ_CONFIG,
			  FIELD_PREP(GENMASK(19, 16), 6) |
			  FIELD_PREP(GENMASK(15, 12), 6) |
			  FIELD_PREP(GENMASK(11, 8), 9) |
			  BIT(3) | BIT(2) |
			  FIELD_PREP(GENMASK(1, 0), 2));

	/* Enable expanded apriv for targets that support it */
	if (gpu->hw_apriv) {
		if (adreno_is_a7xx(adreno_gpu)) {
			gpu_write(gpu, REG_A6XX_CP_APRIV_CNTL,
				  A7XX_BR_APRIVMASK);
			gpu_write(gpu, REG_A7XX_CP_BV_APRIV_CNTL,
				  A7XX_APRIV_MASK);
			gpu_write(gpu, REG_A7XX_CP_LPAC_APRIV_CNTL,
				  A7XX_APRIV_MASK);
		} else
			gpu_write(gpu, REG_A6XX_CP_APRIV_CNTL,
				  BIT(6) | BIT(5) | BIT(3) | BIT(2) | BIT(1));
	}

	if (adreno_is_a750(adreno_gpu)) {
		/* Disable ubwc merged UFC request feature */
		gpu_rmw(gpu, REG_A6XX_RB_CMP_DBG_ECO_CNTL, BIT(19), BIT(19));

		/* Enable TP flaghint and other performance settings */
		gpu_write(gpu, REG_A6XX_TPL1_DBG_ECO_CNTL1, 0xc0700);
	} else if (adreno_is_a7xx(adreno_gpu)) {
		/* Disable non-ubwc read reqs from passing write reqs */
		gpu_rmw(gpu, REG_A6XX_RB_CMP_DBG_ECO_CNTL, BIT(11), BIT(11));
	}

	/* Enable interrupts */
	gpu_write(gpu, REG_A6XX_RBBM_INT_0_MASK,
		  adreno_is_a7xx(adreno_gpu) ? A7XX_INT_MASK : A6XX_INT_MASK);

	ret = adreno_hw_init(gpu);
	if (ret)
		goto out;

	gpu_write64(gpu, REG_A6XX_CP_SQE_INSTR_BASE, a6xx_gpu->sqe_iova);

	/* Set the ringbuffer address */
	gpu_write64(gpu, REG_A6XX_CP_RB_BASE, gpu->rb[0]->iova);

	/* Targets that support extended APRIV can use the RPTR shadow from
	 * hardware but all the other ones need to disable the feature. Targets
	 * that support the WHERE_AM_I opcode can use that instead
	 */
	if (adreno_gpu->base.hw_apriv)
		gpu_write(gpu, REG_A6XX_CP_RB_CNTL, MSM_GPU_RB_CNTL_DEFAULT);
	else
		gpu_write(gpu, REG_A6XX_CP_RB_CNTL,
			MSM_GPU_RB_CNTL_DEFAULT | AXXX_CP_RB_CNTL_NO_UPDATE);

	/* Configure the RPTR shadow if needed: */
	if (a6xx_gpu->shadow_bo) {
		gpu_write64(gpu, REG_A6XX_CP_RB_RPTR_ADDR,
			shadowptr(a6xx_gpu, gpu->rb[0]));
		for (unsigned int i = 0; i < gpu->nr_rings; i++)
			a6xx_gpu->shadow[i] = 0;
	}

	/* ..which means "always" on A7xx, also for BV shadow */
	if (adreno_is_a7xx(adreno_gpu)) {
		gpu_write64(gpu, REG_A7XX_CP_BV_RB_RPTR_ADDR,
			    rbmemptr(gpu->rb[0], bv_rptr));
	}

	a6xx_preempt_hw_init(gpu);

	/* Always come up on rb 0 */
	a6xx_gpu->cur_ring = gpu->rb[0];

	for (i = 0; i < gpu->nr_rings; i++)
		gpu->rb[i]->cur_ctx_seqno = 0;

	/* Enable the SQE_to start the CP engine */
	gpu_write(gpu, REG_A6XX_CP_SQE_CNTL, 1);

	if (adreno_is_a7xx(adreno_gpu) && !a6xx_gpu->pwrup_reglist_emitted) {
		a7xx_patch_pwrup_reglist(gpu);
		a6xx_gpu->pwrup_reglist_emitted = true;
	}

	ret = adreno_is_a7xx(adreno_gpu) ? a7xx_cp_init(gpu) : a6xx_cp_init(gpu);
	if (ret)
		goto out;

	/*
	 * Try to load a zap shader into the secure world. If successful
	 * we can use the CP to switch out of secure mode. If not then we
	 * have no resource but to try to switch ourselves out manually. If we
	 * guessed wrong then access to the RBBM_SECVID_TRUST_CNTL register will
	 * be blocked and a permissions violation will soon follow.
	 */
	ret = a6xx_zap_shader_init(gpu);
	if (!ret) {
		OUT_PKT7(gpu->rb[0], CP_SET_SECURE_MODE, 1);
		OUT_RING(gpu->rb[0], 0x00000000);

		a6xx_flush(gpu, gpu->rb[0]);
		if (!a6xx_idle(gpu, gpu->rb[0]))
			return -EINVAL;
	} else if (ret == -ENODEV) {
		/*
		 * This device does not use zap shader (but print a warning
		 * just in case someone got their dt wrong.. hopefully they
		 * have a debug UART to realize the error of their ways...
		 * if you mess this up you are about to crash horribly)
		 */
		dev_warn_once(gpu->dev->dev,
			"Zap shader not enabled - using SECVID_TRUST_CNTL instead\n");
		gpu_write(gpu, REG_A6XX_RBBM_SECVID_TRUST_CNTL, 0x0);
		ret = 0;
	} else {
		return ret;
	}

out:
	if (adreno_has_gmu_wrapper(adreno_gpu))
		return ret;

	/* Last step - yield the ringbuffer */
	a7xx_preempt_start(gpu);

	/*
	 * Tell the GMU that we are done touching the GPU and it can start power
	 * management
	 */
	a6xx_gmu_clear_oob(&a6xx_gpu->gmu, GMU_OOB_GPU_SET);

	if (a6xx_gpu->gmu.legacy) {
		/* Take the GMU out of its special boot mode */
		a6xx_gmu_clear_oob(&a6xx_gpu->gmu, GMU_OOB_BOOT_SLUMBER);
	}

	return ret;
}

static int a6xx_hw_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	int ret;

	mutex_lock(&a6xx_gpu->gmu.lock);
	ret = hw_init(gpu);
	mutex_unlock(&a6xx_gpu->gmu.lock);

	return ret;
}

static void a6xx_dump(struct msm_gpu *gpu)
{
	DRM_DEV_INFO(&gpu->pdev->dev, "status:   %08x\n",
			gpu_read(gpu, REG_A6XX_RBBM_STATUS));
	adreno_dump(gpu);
}

static void a6xx_recover(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	struct a6xx_gmu *gmu = &a6xx_gpu->gmu;
	int i, active_submits;

	adreno_dump_info(gpu);

	for (i = 0; i < 8; i++)
		DRM_DEV_INFO(&gpu->pdev->dev, "CP_SCRATCH_REG%d: %u\n", i,
			gpu_read(gpu, REG_A6XX_CP_SCRATCH_REG(i)));

	if (hang_debug)
		a6xx_dump(gpu);

	/*
	 * To handle recovery specific sequences during the rpm suspend we are
	 * about to trigger
	 */
	a6xx_gpu->hung = true;

	/* Halt SQE first */
	gpu_write(gpu, REG_A6XX_CP_SQE_CNTL, 3);

	pm_runtime_dont_use_autosuspend(&gpu->pdev->dev);

	/* active_submit won't change until we make a submission */
	mutex_lock(&gpu->active_lock);
	active_submits = gpu->active_submits;

	/*
	 * Temporarily clear active_submits count to silence a WARN() in the
	 * runtime suspend cb
	 */
	gpu->active_submits = 0;

	if (adreno_has_gmu_wrapper(adreno_gpu)) {
		/* Drain the outstanding traffic on memory buses */
		a6xx_bus_clear_pending_transactions(adreno_gpu, true);

		/* Reset the GPU to a clean state */
		a6xx_gpu_sw_reset(gpu, true);
		a6xx_gpu_sw_reset(gpu, false);
	}

	reinit_completion(&gmu->pd_gate);
	dev_pm_genpd_add_notifier(gmu->cxpd, &gmu->pd_nb);
	dev_pm_genpd_synced_poweroff(gmu->cxpd);

	/* Drop the rpm refcount from active submits */
	if (active_submits)
		pm_runtime_put(&gpu->pdev->dev);

	/* And the final one from recover worker */
	pm_runtime_put_sync(&gpu->pdev->dev);

	if (!wait_for_completion_timeout(&gmu->pd_gate, msecs_to_jiffies(1000)))
		DRM_DEV_ERROR(&gpu->pdev->dev, "cx gdsc didn't collapse\n");

	dev_pm_genpd_remove_notifier(gmu->cxpd);

	pm_runtime_use_autosuspend(&gpu->pdev->dev);

	if (active_submits)
		pm_runtime_get(&gpu->pdev->dev);

	pm_runtime_get_sync(&gpu->pdev->dev);

	gpu->active_submits = active_submits;
	mutex_unlock(&gpu->active_lock);

	msm_gpu_hw_init(gpu);
	a6xx_gpu->hung = false;
}

static const char *a6xx_uche_fault_block(struct msm_gpu *gpu, u32 mid)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	static const char *uche_clients[7] = {
		"VFD", "SP", "VSC", "VPC", "HLSQ", "PC", "LRZ",
	};
	u32 val;

	if (adreno_is_a7xx(adreno_gpu)) {
		if (mid != 1 && mid != 2 && mid != 3 && mid != 8)
			return "UNKNOWN";
	} else {
		if (mid < 1 || mid > 3)
			return "UNKNOWN";
	}

	/*
	 * The source of the data depends on the mid ID read from FSYNR1.
	 * and the client ID read from the UCHE block
	 */
	val = gpu_read(gpu, REG_A6XX_UCHE_CLIENT_PF);

	if (adreno_is_a7xx(adreno_gpu)) {
		/* Bit 3 for mid=3 indicates BR or BV */
		static const char *uche_clients_a7xx[16] = {
			"BR_VFD", "BR_SP", "BR_VSC", "BR_VPC",
			"BR_HLSQ", "BR_PC", "BR_LRZ", "BR_TP",
			"BV_VFD", "BV_SP", "BV_VSC", "BV_VPC",
			"BV_HLSQ", "BV_PC", "BV_LRZ", "BV_TP",
		};

		/* LPAC has the same clients as BR and BV, but because it is
		 * compute-only some of them do not exist and there are holes
		 * in the array.
		 */
		static const char *uche_clients_lpac_a7xx[8] = {
			"-", "LPAC_SP", "-", "-",
			"LPAC_HLSQ", "-", "-", "LPAC_TP",
		};

		val &= GENMASK(6, 0);

		/* mid=3 refers to BR or BV */
		if (mid == 3) {
			if (val < ARRAY_SIZE(uche_clients_a7xx))
				return uche_clients_a7xx[val];
			else
				return "UCHE";
		}

		/* mid=8 refers to LPAC */
		if (mid == 8) {
			if (val < ARRAY_SIZE(uche_clients_lpac_a7xx))
				return uche_clients_lpac_a7xx[val];
			else
				return "UCHE_LPAC";
		}

		/* mid=2 is a catchall for everything else in LPAC */
		if (mid == 2)
			return "UCHE_LPAC";

		/* mid=1 is a catchall for everything else in BR/BV */
		return "UCHE";
	} else if (adreno_is_a660_family(adreno_gpu)) {
		static const char *uche_clients_a660[8] = {
			"VFD", "SP", "VSC", "VPC", "HLSQ", "PC", "LRZ", "TP",
		};

		static const char *uche_clients_a660_not[8] = {
			"not VFD", "not SP", "not VSC", "not VPC",
			"not HLSQ", "not PC", "not LRZ", "not TP",
		};

		val &= GENMASK(6, 0);

		if (mid == 3 && val < ARRAY_SIZE(uche_clients_a660))
			return uche_clients_a660[val];

		if (mid == 1 && val < ARRAY_SIZE(uche_clients_a660_not))
			return uche_clients_a660_not[val];

		return "UCHE";
	} else {
		/* mid = 3 is most precise and refers to only one block per client */
		if (mid == 3)
			return uche_clients[val & 7];

		/* For mid=2 the source is TP or VFD except when the client id is 0 */
		if (mid == 2)
			return ((val & 7) == 0) ? "TP" : "TP|VFD";

		/* For mid=1 just return "UCHE" as a catchall for everything else */
		return "UCHE";
	}
}

static const char *a6xx_fault_block(struct msm_gpu *gpu, u32 id)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);

	if (id == 0)
		return "CP";
	else if (id == 4)
		return "CCU";
	else if (id == 6)
		return "CDP Prefetch";
	else if (id == 7)
		return "GMU";
	else if (id == 5 && adreno_is_a7xx(adreno_gpu))
		return "Flag cache";

	return a6xx_uche_fault_block(gpu, id);
}

static int a6xx_fault_handler(void *arg, unsigned long iova, int flags, void *data)
{
	struct msm_gpu *gpu = arg;
	struct adreno_smmu_fault_info *info = data;
	const char *block = "unknown";

	u32 scratch[] = {
			gpu_read(gpu, REG_A6XX_CP_SCRATCH_REG(4)),
			gpu_read(gpu, REG_A6XX_CP_SCRATCH_REG(5)),
			gpu_read(gpu, REG_A6XX_CP_SCRATCH_REG(6)),
			gpu_read(gpu, REG_A6XX_CP_SCRATCH_REG(7)),
	};

	if (info)
		block = a6xx_fault_block(gpu, info->fsynr1 & 0xff);

	return adreno_fault_handler(gpu, iova, flags, info, block, scratch);
}

static void a6xx_cp_hw_err_irq(struct msm_gpu *gpu)
{
	u32 status = gpu_read(gpu, REG_A6XX_CP_INTERRUPT_STATUS);

	if (status & A6XX_CP_INT_CP_OPCODE_ERROR) {
		u32 val;

		gpu_write(gpu, REG_A6XX_CP_SQE_STAT_ADDR, 1);
		val = gpu_read(gpu, REG_A6XX_CP_SQE_STAT_DATA);
		dev_err_ratelimited(&gpu->pdev->dev,
			"CP | opcode error | possible opcode=0x%8.8X\n",
			val);
	}

	if (status & A6XX_CP_INT_CP_UCODE_ERROR)
		dev_err_ratelimited(&gpu->pdev->dev,
			"CP ucode error interrupt\n");

	if (status & A6XX_CP_INT_CP_HW_FAULT_ERROR)
		dev_err_ratelimited(&gpu->pdev->dev, "CP | HW fault | status=0x%8.8X\n",
			gpu_read(gpu, REG_A6XX_CP_HW_FAULT));

	if (status & A6XX_CP_INT_CP_REGISTER_PROTECTION_ERROR) {
		u32 val = gpu_read(gpu, REG_A6XX_CP_PROTECT_STATUS);

		dev_err_ratelimited(&gpu->pdev->dev,
			"CP | protected mode error | %s | addr=0x%8.8X | status=0x%8.8X\n",
			val & (1 << 20) ? "READ" : "WRITE",
			(val & 0x3ffff), val);
	}

	if (status & A6XX_CP_INT_CP_AHB_ERROR && !adreno_is_a7xx(to_adreno_gpu(gpu)))
		dev_err_ratelimited(&gpu->pdev->dev, "CP AHB error interrupt\n");

	if (status & A6XX_CP_INT_CP_VSD_PARITY_ERROR)
		dev_err_ratelimited(&gpu->pdev->dev, "CP VSD decoder parity error\n");

	if (status & A6XX_CP_INT_CP_ILLEGAL_INSTR_ERROR)
		dev_err_ratelimited(&gpu->pdev->dev, "CP illegal instruction error\n");

}

static void a6xx_fault_detect_irq(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	struct msm_ringbuffer *ring = gpu->funcs->active_ring(gpu);

	/*
	 * If stalled on SMMU fault, we could trip the GPU's hang detection,
	 * but the fault handler will trigger the devcore dump, and we want
	 * to otherwise resume normally rather than killing the submit, so
	 * just bail.
	 */
	if (gpu_read(gpu, REG_A6XX_RBBM_STATUS3) & A6XX_RBBM_STATUS3_SMMU_STALLED_ON_FAULT)
		return;

	/*
	 * Force the GPU to stay on until after we finish
	 * collecting information
	 */
	if (!adreno_has_gmu_wrapper(adreno_gpu))
		gmu_write(&a6xx_gpu->gmu, REG_A6XX_GMU_GMU_PWR_COL_KEEPALIVE, 1);

	DRM_DEV_ERROR(&gpu->pdev->dev,
		"gpu fault ring %d fence %x status %8.8X rb %4.4x/%4.4x ib1 %16.16llX/%4.4x ib2 %16.16llX/%4.4x\n",
		ring ? ring->id : -1, ring ? ring->fctx->last_fence : 0,
		gpu_read(gpu, REG_A6XX_RBBM_STATUS),
		gpu_read(gpu, REG_A6XX_CP_RB_RPTR),
		gpu_read(gpu, REG_A6XX_CP_RB_WPTR),
		gpu_read64(gpu, REG_A6XX_CP_IB1_BASE),
		gpu_read(gpu, REG_A6XX_CP_IB1_REM_SIZE),
		gpu_read64(gpu, REG_A6XX_CP_IB2_BASE),
		gpu_read(gpu, REG_A6XX_CP_IB2_REM_SIZE));

	/* Turn off the hangcheck timer to keep it from bothering us */
	timer_delete(&gpu->hangcheck_timer);

	kthread_queue_work(gpu->worker, &gpu->recover_work);
}

static void a7xx_sw_fuse_violation_irq(struct msm_gpu *gpu)
{
	u32 status;

	status = gpu_read(gpu, REG_A7XX_RBBM_SW_FUSE_INT_STATUS);
	gpu_write(gpu, REG_A7XX_RBBM_SW_FUSE_INT_MASK, 0);

	dev_err_ratelimited(&gpu->pdev->dev, "SW fuse violation status=%8.8x\n", status);

	/*
	 * Ignore FASTBLEND violations, because the HW will silently fall back
	 * to legacy blending.
	 */
	if (status & (A7XX_CX_MISC_SW_FUSE_VALUE_RAYTRACING |
		      A7XX_CX_MISC_SW_FUSE_VALUE_LPAC)) {
		timer_delete(&gpu->hangcheck_timer);

		kthread_queue_work(gpu->worker, &gpu->recover_work);
	}
}

static irqreturn_t a6xx_irq(struct msm_gpu *gpu)
{
	struct msm_drm_private *priv = gpu->dev->dev_private;
	u32 status = gpu_read(gpu, REG_A6XX_RBBM_INT_0_STATUS);

	gpu_write(gpu, REG_A6XX_RBBM_INT_CLEAR_CMD, status);

	if (priv->disable_err_irq)
		status &= A6XX_RBBM_INT_0_MASK_CP_CACHE_FLUSH_TS;

	if (status & A6XX_RBBM_INT_0_MASK_RBBM_HANG_DETECT)
		a6xx_fault_detect_irq(gpu);

	if (status & A6XX_RBBM_INT_0_MASK_CP_AHB_ERROR)
		dev_err_ratelimited(&gpu->pdev->dev, "CP | AHB bus error\n");

	if (status & A6XX_RBBM_INT_0_MASK_CP_HW_ERROR)
		a6xx_cp_hw_err_irq(gpu);

	if (status & A6XX_RBBM_INT_0_MASK_RBBM_ATB_ASYNCFIFO_OVERFLOW)
		dev_err_ratelimited(&gpu->pdev->dev, "RBBM | ATB ASYNC overflow\n");

	if (status & A6XX_RBBM_INT_0_MASK_RBBM_ATB_BUS_OVERFLOW)
		dev_err_ratelimited(&gpu->pdev->dev, "RBBM | ATB bus overflow\n");

	if (status & A6XX_RBBM_INT_0_MASK_UCHE_OOB_ACCESS)
		dev_err_ratelimited(&gpu->pdev->dev, "UCHE | Out of bounds access\n");

	if (status & A6XX_RBBM_INT_0_MASK_SWFUSEVIOLATION)
		a7xx_sw_fuse_violation_irq(gpu);

	if (status & A6XX_RBBM_INT_0_MASK_CP_CACHE_FLUSH_TS) {
		msm_gpu_retire(gpu);
		a6xx_preempt_trigger(gpu);
	}

	if (status & A6XX_RBBM_INT_0_MASK_CP_SW)
		a6xx_preempt_irq(gpu);

	return IRQ_HANDLED;
}

static void a6xx_llc_deactivate(struct a6xx_gpu *a6xx_gpu)
{
	llcc_slice_deactivate(a6xx_gpu->llc_slice);
	llcc_slice_deactivate(a6xx_gpu->htw_llc_slice);
}

static void a6xx_llc_activate(struct a6xx_gpu *a6xx_gpu)
{
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	struct msm_gpu *gpu = &adreno_gpu->base;
	u32 cntl1_regval = 0;

	if (IS_ERR(a6xx_gpu->llc_mmio))
		return;

	if (!llcc_slice_activate(a6xx_gpu->llc_slice)) {
		u32 gpu_scid = llcc_get_slice_id(a6xx_gpu->llc_slice);

		gpu_scid &= 0x1f;
		cntl1_regval = (gpu_scid << 0) | (gpu_scid << 5) | (gpu_scid << 10) |
			       (gpu_scid << 15) | (gpu_scid << 20);

		/* On A660, the SCID programming for UCHE traffic is done in
		 * A6XX_GBIF_SCACHE_CNTL0[14:10]
		 */
		if (adreno_is_a660_family(adreno_gpu))
			gpu_rmw(gpu, REG_A6XX_GBIF_SCACHE_CNTL0, (0x1f << 10) |
				(1 << 8), (gpu_scid << 10) | (1 << 8));
	}

	/*
	 * For targets with a MMU500, activate the slice but don't program the
	 * register.  The XBL will take care of that.
	 */
	if (!llcc_slice_activate(a6xx_gpu->htw_llc_slice)) {
		if (!a6xx_gpu->have_mmu500) {
			u32 gpuhtw_scid = llcc_get_slice_id(a6xx_gpu->htw_llc_slice);

			gpuhtw_scid &= 0x1f;
			cntl1_regval |= FIELD_PREP(GENMASK(29, 25), gpuhtw_scid);
		}
	}

	if (!cntl1_regval)
		return;

	/*
	 * Program the slice IDs for the various GPU blocks and GPU MMU
	 * pagetables
	 */
	if (!a6xx_gpu->have_mmu500) {
		a6xx_llc_write(a6xx_gpu,
			REG_A6XX_CX_MISC_SYSTEM_CACHE_CNTL_1, cntl1_regval);

		/*
		 * Program cacheability overrides to not allocate cache
		 * lines on a write miss
		 */
		a6xx_llc_rmw(a6xx_gpu,
			REG_A6XX_CX_MISC_SYSTEM_CACHE_CNTL_0, 0xF, 0x03);
		return;
	}

	gpu_rmw(gpu, REG_A6XX_GBIF_SCACHE_CNTL1, GENMASK(24, 0), cntl1_regval);
}

static void a7xx_llc_activate(struct a6xx_gpu *a6xx_gpu)
{
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	struct msm_gpu *gpu = &adreno_gpu->base;

	if (IS_ERR(a6xx_gpu->llc_mmio))
		return;

	if (!llcc_slice_activate(a6xx_gpu->llc_slice)) {
		u32 gpu_scid = llcc_get_slice_id(a6xx_gpu->llc_slice);

		gpu_scid &= GENMASK(4, 0);

		gpu_write(gpu, REG_A6XX_GBIF_SCACHE_CNTL1,
			  FIELD_PREP(GENMASK(29, 25), gpu_scid) |
			  FIELD_PREP(GENMASK(24, 20), gpu_scid) |
			  FIELD_PREP(GENMASK(19, 15), gpu_scid) |
			  FIELD_PREP(GENMASK(14, 10), gpu_scid) |
			  FIELD_PREP(GENMASK(9, 5), gpu_scid) |
			  FIELD_PREP(GENMASK(4, 0), gpu_scid));

		gpu_write(gpu, REG_A6XX_GBIF_SCACHE_CNTL0,
			  FIELD_PREP(GENMASK(14, 10), gpu_scid) |
			  BIT(8));
	}

	llcc_slice_activate(a6xx_gpu->htw_llc_slice);
}

static void a6xx_llc_slices_destroy(struct a6xx_gpu *a6xx_gpu)
{
	/* No LLCC on non-RPMh (and by extension, non-GMU) SoCs */
	if (adreno_has_gmu_wrapper(&a6xx_gpu->base))
		return;

	llcc_slice_putd(a6xx_gpu->llc_slice);
	llcc_slice_putd(a6xx_gpu->htw_llc_slice);
}

static void a6xx_llc_slices_init(struct platform_device *pdev,
		struct a6xx_gpu *a6xx_gpu, bool is_a7xx)
{
	struct device_node *phandle;

	/* No LLCC on non-RPMh (and by extension, non-GMU) SoCs */
	if (adreno_has_gmu_wrapper(&a6xx_gpu->base))
		return;

	/*
	 * There is a different programming path for A6xx targets with an
	 * mmu500 attached, so detect if that is the case
	 */
	phandle = of_parse_phandle(pdev->dev.of_node, "iommus", 0);
	a6xx_gpu->have_mmu500 = (phandle &&
		of_device_is_compatible(phandle, "arm,mmu-500"));
	of_node_put(phandle);

	if (is_a7xx || !a6xx_gpu->have_mmu500)
		a6xx_gpu->llc_mmio = msm_ioremap(pdev, "cx_mem");
	else
		a6xx_gpu->llc_mmio = NULL;

	a6xx_gpu->llc_slice = llcc_slice_getd(LLCC_GPU);
	a6xx_gpu->htw_llc_slice = llcc_slice_getd(LLCC_GPUHTW);

	if (IS_ERR_OR_NULL(a6xx_gpu->llc_slice) && IS_ERR_OR_NULL(a6xx_gpu->htw_llc_slice))
		a6xx_gpu->llc_mmio = ERR_PTR(-EINVAL);
}

static int a7xx_cx_mem_init(struct a6xx_gpu *a6xx_gpu)
{
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	struct msm_gpu *gpu = &adreno_gpu->base;
	u32 fuse_val;
	int ret;

	if (adreno_is_a750(adreno_gpu)) {
		/*
		 * Assume that if qcom scm isn't available, that whatever
		 * replacement allows writing the fuse register ourselves.
		 * Users of alternative firmware need to make sure this
		 * register is writeable or indicate that it's not somehow.
		 * Print a warning because if you mess this up you're about to
		 * crash horribly.
		 */
		if (!qcom_scm_is_available()) {
			dev_warn_once(gpu->dev->dev,
				"SCM is not available, poking fuse register\n");
			a6xx_llc_write(a6xx_gpu, REG_A7XX_CX_MISC_SW_FUSE_VALUE,
				A7XX_CX_MISC_SW_FUSE_VALUE_RAYTRACING |
				A7XX_CX_MISC_SW_FUSE_VALUE_FASTBLEND |
				A7XX_CX_MISC_SW_FUSE_VALUE_LPAC);
			adreno_gpu->has_ray_tracing = true;
			return 0;
		}

		ret = qcom_scm_gpu_init_regs(QCOM_SCM_GPU_ALWAYS_EN_REQ |
					     QCOM_SCM_GPU_TSENSE_EN_REQ);
		if (ret)
			return ret;

		/*
		 * On a750 raytracing may be disabled by the firmware, find out
		 * whether that's the case. The scm call above sets the fuse
		 * register.
		 */
		fuse_val = a6xx_llc_read(a6xx_gpu,
					 REG_A7XX_CX_MISC_SW_FUSE_VALUE);
		adreno_gpu->has_ray_tracing =
			!!(fuse_val & A7XX_CX_MISC_SW_FUSE_VALUE_RAYTRACING);
	} else if (adreno_is_a740(adreno_gpu)) {
		/* Raytracing is always enabled on a740 */
		adreno_gpu->has_ray_tracing = true;
	}

	return 0;
}


#define GBIF_CLIENT_HALT_MASK		BIT(0)
#define GBIF_ARB_HALT_MASK		BIT(1)
#define VBIF_XIN_HALT_CTRL0_MASK	GENMASK(3, 0)
#define VBIF_RESET_ACK_MASK		0xF0
#define GPR0_GBIF_HALT_REQUEST		0x1E0

void a6xx_bus_clear_pending_transactions(struct adreno_gpu *adreno_gpu, bool gx_off)
{
	struct msm_gpu *gpu = &adreno_gpu->base;

	if (adreno_is_a619_holi(adreno_gpu)) {
		gpu_write(gpu, REG_A6XX_RBBM_GPR0_CNTL, GPR0_GBIF_HALT_REQUEST);
		spin_until((gpu_read(gpu, REG_A6XX_RBBM_VBIF_GX_RESET_STATUS) &
				(VBIF_RESET_ACK_MASK)) == VBIF_RESET_ACK_MASK);
	} else if (!a6xx_has_gbif(adreno_gpu)) {
		gpu_write(gpu, REG_A6XX_VBIF_XIN_HALT_CTRL0, VBIF_XIN_HALT_CTRL0_MASK);
		spin_until((gpu_read(gpu, REG_A6XX_VBIF_XIN_HALT_CTRL1) &
				(VBIF_XIN_HALT_CTRL0_MASK)) == VBIF_XIN_HALT_CTRL0_MASK);
		gpu_write(gpu, REG_A6XX_VBIF_XIN_HALT_CTRL0, 0);

		return;
	}

	if (gx_off) {
		/* Halt the gx side of GBIF */
		gpu_write(gpu, REG_A6XX_RBBM_GBIF_HALT, 1);
		spin_until(gpu_read(gpu, REG_A6XX_RBBM_GBIF_HALT_ACK) & 1);
	}

	/* Halt new client requests on GBIF */
	gpu_write(gpu, REG_A6XX_GBIF_HALT, GBIF_CLIENT_HALT_MASK);
	spin_until((gpu_read(gpu, REG_A6XX_GBIF_HALT_ACK) &
			(GBIF_CLIENT_HALT_MASK)) == GBIF_CLIENT_HALT_MASK);

	/* Halt all AXI requests on GBIF */
	gpu_write(gpu, REG_A6XX_GBIF_HALT, GBIF_ARB_HALT_MASK);
	spin_until((gpu_read(gpu,  REG_A6XX_GBIF_HALT_ACK) &
			(GBIF_ARB_HALT_MASK)) == GBIF_ARB_HALT_MASK);

	/* The GBIF halt needs to be explicitly cleared */
	gpu_write(gpu, REG_A6XX_GBIF_HALT, 0x0);
}

void a6xx_gpu_sw_reset(struct msm_gpu *gpu, bool assert)
{
	/* 11nm chips (e.g. ones with A610) have hw issues with the reset line! */
	if (adreno_is_a610(to_adreno_gpu(gpu)))
		return;

	gpu_write(gpu, REG_A6XX_RBBM_SW_RESET_CMD, assert);
	/* Perform a bogus read and add a brief delay to ensure ordering. */
	gpu_read(gpu, REG_A6XX_RBBM_SW_RESET_CMD);
	udelay(1);

	/* The reset line needs to be asserted for at least 100 us */
	if (assert)
		udelay(100);
}

static int a6xx_gmu_pm_resume(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	int ret;

	gpu->needs_hw_init = true;

	trace_msm_gpu_resume(0);

	mutex_lock(&a6xx_gpu->gmu.lock);
	ret = a6xx_gmu_resume(a6xx_gpu);
	mutex_unlock(&a6xx_gpu->gmu.lock);
	if (ret)
		return ret;

	msm_devfreq_resume(gpu);

	adreno_is_a7xx(adreno_gpu) ? a7xx_llc_activate(a6xx_gpu) : a6xx_llc_activate(a6xx_gpu);

	return ret;
}

static int a6xx_pm_resume(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	struct a6xx_gmu *gmu = &a6xx_gpu->gmu;
	unsigned long freq = gpu->fast_rate;
	struct dev_pm_opp *opp;
	int ret;

	gpu->needs_hw_init = true;

	trace_msm_gpu_resume(0);

	mutex_lock(&a6xx_gpu->gmu.lock);

	opp = dev_pm_opp_find_freq_ceil(&gpu->pdev->dev, &freq);
	if (IS_ERR(opp)) {
		ret = PTR_ERR(opp);
		goto err_set_opp;
	}
	dev_pm_opp_put(opp);

	/* Set the core clock and bus bw, having VDD scaling in mind */
	dev_pm_opp_set_opp(&gpu->pdev->dev, opp);

	pm_runtime_resume_and_get(gmu->dev);
	pm_runtime_resume_and_get(gmu->gxpd);

	ret = clk_bulk_prepare_enable(gpu->nr_clocks, gpu->grp_clks);
	if (ret)
		goto err_bulk_clk;

	if (adreno_is_a619_holi(adreno_gpu))
		a6xx_sptprac_enable(gmu);

	/* If anything goes south, tear the GPU down piece by piece.. */
	if (ret) {
err_bulk_clk:
		pm_runtime_put(gmu->gxpd);
		pm_runtime_put(gmu->dev);
		dev_pm_opp_set_opp(&gpu->pdev->dev, NULL);
	}
err_set_opp:
	mutex_unlock(&a6xx_gpu->gmu.lock);

	if (!ret)
		msm_devfreq_resume(gpu);

	return ret;
}

static int a6xx_gmu_pm_suspend(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	int i, ret;

	trace_msm_gpu_suspend(0);

	a6xx_llc_deactivate(a6xx_gpu);

	msm_devfreq_suspend(gpu);

	mutex_lock(&a6xx_gpu->gmu.lock);
	ret = a6xx_gmu_stop(a6xx_gpu);
	mutex_unlock(&a6xx_gpu->gmu.lock);
	if (ret)
		return ret;

	if (a6xx_gpu->shadow_bo)
		for (i = 0; i < gpu->nr_rings; i++)
			a6xx_gpu->shadow[i] = 0;

	gpu->suspend_count++;

	return 0;
}

static int a6xx_pm_suspend(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	struct a6xx_gmu *gmu = &a6xx_gpu->gmu;
	int i;

	trace_msm_gpu_suspend(0);

	msm_devfreq_suspend(gpu);

	mutex_lock(&a6xx_gpu->gmu.lock);

	/* Drain the outstanding traffic on memory buses */
	a6xx_bus_clear_pending_transactions(adreno_gpu, true);

	if (adreno_is_a619_holi(adreno_gpu))
		a6xx_sptprac_disable(gmu);

	clk_bulk_disable_unprepare(gpu->nr_clocks, gpu->grp_clks);

	pm_runtime_put_sync(gmu->gxpd);
	dev_pm_opp_set_opp(&gpu->pdev->dev, NULL);
	pm_runtime_put_sync(gmu->dev);

	mutex_unlock(&a6xx_gpu->gmu.lock);

	if (a6xx_gpu->shadow_bo)
		for (i = 0; i < gpu->nr_rings; i++)
			a6xx_gpu->shadow[i] = 0;

	gpu->suspend_count++;

	return 0;
}

static int a6xx_gmu_get_timestamp(struct msm_gpu *gpu, uint64_t *value)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);

	mutex_lock(&a6xx_gpu->gmu.lock);

	/* Force the GPU power on so we can read this register */
	a6xx_gmu_set_oob(&a6xx_gpu->gmu, GMU_OOB_PERFCOUNTER_SET);

	*value = gpu_read64(gpu, REG_A6XX_CP_ALWAYS_ON_COUNTER);

	a6xx_gmu_clear_oob(&a6xx_gpu->gmu, GMU_OOB_PERFCOUNTER_SET);

	mutex_unlock(&a6xx_gpu->gmu.lock);

	return 0;
}

static int a6xx_get_timestamp(struct msm_gpu *gpu, uint64_t *value)
{
	*value = gpu_read64(gpu, REG_A6XX_CP_ALWAYS_ON_COUNTER);
	return 0;
}

static struct msm_ringbuffer *a6xx_active_ring(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);

	return a6xx_gpu->cur_ring;
}

static void a6xx_destroy(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);

	if (a6xx_gpu->sqe_bo) {
		msm_gem_unpin_iova(a6xx_gpu->sqe_bo, gpu->aspace);
		drm_gem_object_put(a6xx_gpu->sqe_bo);
	}

	if (a6xx_gpu->shadow_bo) {
		msm_gem_unpin_iova(a6xx_gpu->shadow_bo, gpu->aspace);
		drm_gem_object_put(a6xx_gpu->shadow_bo);
	}

	a6xx_llc_slices_destroy(a6xx_gpu);

	a6xx_gmu_remove(a6xx_gpu);

	adreno_gpu_cleanup(adreno_gpu);

	kfree(a6xx_gpu);
}

static u64 a6xx_gpu_busy(struct msm_gpu *gpu, unsigned long *out_sample_rate)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	u64 busy_cycles;

	/* 19.2MHz */
	*out_sample_rate = 19200000;

	busy_cycles = gmu_read64(&a6xx_gpu->gmu,
			REG_A6XX_GMU_CX_GMU_POWER_COUNTER_XOCLK_0_L,
			REG_A6XX_GMU_CX_GMU_POWER_COUNTER_XOCLK_0_H);

	return busy_cycles;
}

static void a6xx_gpu_set_freq(struct msm_gpu *gpu, struct dev_pm_opp *opp,
			      bool suspended)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);

	mutex_lock(&a6xx_gpu->gmu.lock);
	a6xx_gmu_set_freq(gpu, opp, suspended);
	mutex_unlock(&a6xx_gpu->gmu.lock);
}

static struct msm_gem_address_space *
a6xx_create_address_space(struct msm_gpu *gpu, struct platform_device *pdev)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	unsigned long quirks = 0;

	/*
	 * This allows GPU to set the bus attributes required to use system
	 * cache on behalf of the iommu page table walker.
	 */
	if (!IS_ERR_OR_NULL(a6xx_gpu->htw_llc_slice) &&
	    !device_iommu_capable(&pdev->dev, IOMMU_CAP_CACHE_COHERENCY))
		quirks |= IO_PGTABLE_QUIRK_ARM_OUTER_WBWA;

	return adreno_iommu_create_address_space(gpu, pdev, quirks);
}

static struct msm_gem_address_space *
a6xx_create_private_address_space(struct msm_gpu *gpu)
{
	struct msm_mmu *mmu;

	mmu = msm_iommu_pagetable_create(gpu->aspace->mmu);

	if (IS_ERR(mmu))
		return ERR_CAST(mmu);

	return msm_gem_address_space_create(mmu,
		"gpu", 0x100000000ULL,
		adreno_private_address_space_size(gpu));
}

static uint32_t a6xx_get_rptr(struct msm_gpu *gpu, struct msm_ringbuffer *ring)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);

	if (adreno_gpu->base.hw_apriv || a6xx_gpu->has_whereami)
		return a6xx_gpu->shadow[ring->id];

	return ring->memptrs->rptr = gpu_read(gpu, REG_A6XX_CP_RB_RPTR);
}

static bool a6xx_progress(struct msm_gpu *gpu, struct msm_ringbuffer *ring)
{
	struct msm_cp_state cp_state = {
		.ib1_base = gpu_read64(gpu, REG_A6XX_CP_IB1_BASE),
		.ib2_base = gpu_read64(gpu, REG_A6XX_CP_IB2_BASE),
		.ib1_rem  = gpu_read(gpu, REG_A6XX_CP_IB1_REM_SIZE),
		.ib2_rem  = gpu_read(gpu, REG_A6XX_CP_IB2_REM_SIZE),
	};
	bool progress;

	/*
	 * Adjust the remaining data to account for what has already been
	 * fetched from memory, but not yet consumed by the SQE.
	 *
	 * This is not *technically* correct, the amount buffered could
	 * exceed the IB size due to hw prefetching ahead, but:
	 *
	 * (1) We aren't trying to find the exact position, just whether
	 *     progress has been made
	 * (2) The CP_REG_TO_MEM at the end of a submit should be enough
	 *     to prevent prefetching into an unrelated submit.  (And
	 *     either way, at some point the ROQ will be full.)
	 */
	cp_state.ib1_rem += gpu_read(gpu, REG_A6XX_CP_ROQ_AVAIL_IB1) >> 16;
	cp_state.ib2_rem += gpu_read(gpu, REG_A6XX_CP_ROQ_AVAIL_IB2) >> 16;

	progress = !!memcmp(&cp_state, &ring->last_cp_state, sizeof(cp_state));

	ring->last_cp_state = cp_state;

	return progress;
}

static u32 fuse_to_supp_hw(const struct adreno_info *info, u32 fuse)
{
	if (!info->speedbins)
		return UINT_MAX;

	for (int i = 0; info->speedbins[i].fuse != SHRT_MAX; i++)
		if (info->speedbins[i].fuse == fuse)
			return BIT(info->speedbins[i].speedbin);

	return UINT_MAX;
}

static int a6xx_set_supported_hw(struct device *dev, const struct adreno_info *info)
{
	u32 supp_hw;
	u32 speedbin;
	int ret;

	ret = adreno_read_speedbin(dev, &speedbin);
	/*
	 * -ENOENT means that the platform doesn't support speedbin which is
	 * fine
	 */
	if (ret == -ENOENT) {
		return 0;
	} else if (ret) {
		dev_err_probe(dev, ret,
			      "failed to read speed-bin. Some OPPs may not be supported by hardware\n");
		return ret;
	}

	supp_hw = fuse_to_supp_hw(info, speedbin);

	if (supp_hw == UINT_MAX) {
		DRM_DEV_ERROR(dev,
			"missing support for speed-bin: %u. Some OPPs may not be supported by hardware\n",
			speedbin);
		supp_hw = BIT(0); /* Default */
	}

	ret = devm_pm_opp_set_supported_hw(dev, &supp_hw, 1);
	if (ret)
		return ret;

	return 0;
}

static const struct adreno_gpu_funcs funcs = {
	.base = {
		.get_param = adreno_get_param,
		.set_param = adreno_set_param,
		.hw_init = a6xx_hw_init,
		.ucode_load = a6xx_ucode_load,
		.pm_suspend = a6xx_gmu_pm_suspend,
		.pm_resume = a6xx_gmu_pm_resume,
		.recover = a6xx_recover,
		.submit = a6xx_submit,
		.active_ring = a6xx_active_ring,
		.irq = a6xx_irq,
		.destroy = a6xx_destroy,
#if defined(CONFIG_DRM_MSM_GPU_STATE)
		.show = a6xx_show,
#endif
		.gpu_busy = a6xx_gpu_busy,
		.gpu_get_freq = a6xx_gmu_get_freq,
		.gpu_set_freq = a6xx_gpu_set_freq,
#if defined(CONFIG_DRM_MSM_GPU_STATE)
		.gpu_state_get = a6xx_gpu_state_get,
		.gpu_state_put = a6xx_gpu_state_put,
#endif
		.create_address_space = a6xx_create_address_space,
		.create_private_address_space = a6xx_create_private_address_space,
		.get_rptr = a6xx_get_rptr,
		.progress = a6xx_progress,
	},
	.get_timestamp = a6xx_gmu_get_timestamp,
};

static const struct adreno_gpu_funcs funcs_gmuwrapper = {
	.base = {
		.get_param = adreno_get_param,
		.set_param = adreno_set_param,
		.hw_init = a6xx_hw_init,
		.ucode_load = a6xx_ucode_load,
		.pm_suspend = a6xx_pm_suspend,
		.pm_resume = a6xx_pm_resume,
		.recover = a6xx_recover,
		.submit = a6xx_submit,
		.active_ring = a6xx_active_ring,
		.irq = a6xx_irq,
		.destroy = a6xx_destroy,
#if defined(CONFIG_DRM_MSM_GPU_STATE)
		.show = a6xx_show,
#endif
		.gpu_busy = a6xx_gpu_busy,
#if defined(CONFIG_DRM_MSM_GPU_STATE)
		.gpu_state_get = a6xx_gpu_state_get,
		.gpu_state_put = a6xx_gpu_state_put,
#endif
		.create_address_space = a6xx_create_address_space,
		.create_private_address_space = a6xx_create_private_address_space,
		.get_rptr = a6xx_get_rptr,
		.progress = a6xx_progress,
	},
	.get_timestamp = a6xx_get_timestamp,
};

static const struct adreno_gpu_funcs funcs_a7xx = {
	.base = {
		.get_param = adreno_get_param,
		.set_param = adreno_set_param,
		.hw_init = a6xx_hw_init,
		.ucode_load = a6xx_ucode_load,
		.pm_suspend = a6xx_gmu_pm_suspend,
		.pm_resume = a6xx_gmu_pm_resume,
		.recover = a6xx_recover,
		.submit = a7xx_submit,
		.active_ring = a6xx_active_ring,
		.irq = a6xx_irq,
		.destroy = a6xx_destroy,
#if defined(CONFIG_DRM_MSM_GPU_STATE)
		.show = a6xx_show,
#endif
		.gpu_busy = a6xx_gpu_busy,
		.gpu_get_freq = a6xx_gmu_get_freq,
		.gpu_set_freq = a6xx_gpu_set_freq,
#if defined(CONFIG_DRM_MSM_GPU_STATE)
		.gpu_state_get = a6xx_gpu_state_get,
		.gpu_state_put = a6xx_gpu_state_put,
#endif
		.create_address_space = a6xx_create_address_space,
		.create_private_address_space = a6xx_create_private_address_space,
		.get_rptr = a6xx_get_rptr,
		.progress = a6xx_progress,
	},
	.get_timestamp = a6xx_gmu_get_timestamp,
};

struct msm_gpu *a6xx_gpu_init(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct platform_device *pdev = priv->gpu_pdev;
	struct adreno_platform_config *config = pdev->dev.platform_data;
	struct device_node *node;
	struct a6xx_gpu *a6xx_gpu;
	struct adreno_gpu *adreno_gpu;
	struct msm_gpu *gpu;
	extern int enable_preemption;
	bool is_a7xx;
	int ret;

	a6xx_gpu = kzalloc(sizeof(*a6xx_gpu), GFP_KERNEL);
	if (!a6xx_gpu)
		return ERR_PTR(-ENOMEM);

	adreno_gpu = &a6xx_gpu->base;
	gpu = &adreno_gpu->base;

	mutex_init(&a6xx_gpu->gmu.lock);

	adreno_gpu->registers = NULL;

	/* Check if there is a GMU phandle and set it up */
	node = of_parse_phandle(pdev->dev.of_node, "qcom,gmu", 0);
	/* FIXME: How do we gracefully handle this? */
	BUG_ON(!node);

	adreno_gpu->gmu_is_wrapper = of_device_is_compatible(node, "qcom,adreno-gmu-wrapper");

	adreno_gpu->base.hw_apriv =
		!!(config->info->quirks & ADRENO_QUIRK_HAS_HW_APRIV);

	/* gpu->info only gets assigned in adreno_gpu_init() */
	is_a7xx = config->info->family == ADRENO_7XX_GEN1 ||
		  config->info->family == ADRENO_7XX_GEN2 ||
		  config->info->family == ADRENO_7XX_GEN3;

	a6xx_llc_slices_init(pdev, a6xx_gpu, is_a7xx);

	ret = a6xx_set_supported_hw(&pdev->dev, config->info);
	if (ret) {
		a6xx_llc_slices_destroy(a6xx_gpu);
		kfree(a6xx_gpu);
		return ERR_PTR(ret);
	}

	if ((enable_preemption == 1) || (enable_preemption == -1 &&
	    (config->info->quirks & ADRENO_QUIRK_PREEMPTION)))
		ret = adreno_gpu_init(dev, pdev, adreno_gpu, &funcs_a7xx, 4);
	else if (is_a7xx)
		ret = adreno_gpu_init(dev, pdev, adreno_gpu, &funcs_a7xx, 1);
	else if (adreno_has_gmu_wrapper(adreno_gpu))
		ret = adreno_gpu_init(dev, pdev, adreno_gpu, &funcs_gmuwrapper, 1);
	else
		ret = adreno_gpu_init(dev, pdev, adreno_gpu, &funcs, 1);
	if (ret) {
		a6xx_destroy(&(a6xx_gpu->base.base));
		return ERR_PTR(ret);
	}

	/*
	 * For now only clamp to idle freq for devices where this is known not
	 * to cause power supply issues:
	 */
	if (adreno_is_a618(adreno_gpu) || adreno_is_7c3(adreno_gpu))
		priv->gpu_clamp_to_idle = true;

	if (adreno_has_gmu_wrapper(adreno_gpu))
		ret = a6xx_gmu_wrapper_init(a6xx_gpu, node);
	else
		ret = a6xx_gmu_init(a6xx_gpu, node);
	of_node_put(node);
	if (ret) {
		a6xx_destroy(&(a6xx_gpu->base.base));
		return ERR_PTR(ret);
	}

	if (adreno_is_a7xx(adreno_gpu)) {
		ret = a7xx_cx_mem_init(a6xx_gpu);
		if (ret) {
			a6xx_destroy(&(a6xx_gpu->base.base));
			return ERR_PTR(ret);
		}
	}

	adreno_gpu->uche_trap_base = 0x1fffffffff000ull;

	if (gpu->aspace)
		msm_mmu_set_fault_handler(gpu->aspace->mmu, gpu,
				a6xx_fault_handler);

	a6xx_calc_ubwc_config(adreno_gpu);
	/* Set up the preemption specific bits and pieces for each ringbuffer */
	a6xx_preempt_init(gpu);

	return gpu;
}
