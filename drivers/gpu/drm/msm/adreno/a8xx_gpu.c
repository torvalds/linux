// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. */


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

static void a8xx_aperture_slice_set(struct msm_gpu *gpu, enum adreno_pipe pipe, u32 slice)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	u32 val;

	val = A8XX_CP_APERTURE_CNTL_HOST_PIPEID(pipe) | A8XX_CP_APERTURE_CNTL_HOST_SLICEID(slice);

	if (a6xx_gpu->cached_aperture == val)
		return;

	gpu_write(gpu, REG_A8XX_CP_APERTURE_CNTL_HOST, val);

	a6xx_gpu->cached_aperture = val;
}

static void a8xx_aperture_acquire(struct msm_gpu *gpu, enum adreno_pipe pipe, unsigned long *flags)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);

	spin_lock_irqsave(&a6xx_gpu->aperture_lock, *flags);

	a8xx_aperture_slice_set(gpu, pipe, 0);
}

static void a8xx_aperture_release(struct msm_gpu *gpu, unsigned long flags)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);

	spin_unlock_irqrestore(&a6xx_gpu->aperture_lock, flags);
}

static void a8xx_aperture_clear(struct msm_gpu *gpu)
{
	unsigned long flags;

	a8xx_aperture_acquire(gpu, PIPE_NONE, &flags);
	a8xx_aperture_release(gpu, flags);
}

static void a8xx_write_pipe(struct msm_gpu *gpu, enum adreno_pipe pipe, u32 offset, u32 data)
{
	unsigned long flags;

	a8xx_aperture_acquire(gpu, pipe, &flags);
	gpu_write(gpu, offset, data);
	a8xx_aperture_release(gpu, flags);
}

static u32 a8xx_read_pipe_slice(struct msm_gpu *gpu, enum adreno_pipe pipe, u32 slice, u32 offset)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&a6xx_gpu->aperture_lock, flags);
	a8xx_aperture_slice_set(gpu, pipe, slice);
	val = gpu_read(gpu, offset);
	spin_unlock_irqrestore(&a6xx_gpu->aperture_lock, flags);

	return val;
}

void a8xx_gpu_get_slice_info(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	const struct a6xx_info *info = adreno_gpu->info->a6xx;
	u32 slice_mask;

	if (adreno_gpu->info->family < ADRENO_8XX_GEN1)
		return;

	if (a6xx_gpu->slice_mask)
		return;

	slice_mask = GENMASK(info->max_slices - 1, 0);

	/* GEN1 doesn't support partial slice configurations */
	if (adreno_gpu->info->family == ADRENO_8XX_GEN1) {
		a6xx_gpu->slice_mask = slice_mask;
		return;
	}

	slice_mask &= a6xx_llc_read(a6xx_gpu,
			REG_A8XX_CX_MISC_SLICE_ENABLE_FINAL);

	a6xx_gpu->slice_mask = slice_mask;

	/* Chip ID depends on the number of slices available. So update it */
	adreno_gpu->chip_id |= FIELD_PREP(GENMASK(7, 4), hweight32(slice_mask));
}

static u32 a8xx_get_first_slice(struct a6xx_gpu *a6xx_gpu)
{
	return ffs(a6xx_gpu->slice_mask) - 1;
}

static inline bool _a8xx_check_idle(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);

	/* Check that the GMU is idle */
	if (!a6xx_gmu_isidle(&a6xx_gpu->gmu))
		return false;

	/* Check that the CX master is idle */
	if (gpu_read(gpu, REG_A8XX_RBBM_STATUS) &
			~A8XX_RBBM_STATUS_CP_AHB_BUSY_CX_MASTER)
		return false;

	return !(gpu_read(gpu, REG_A8XX_RBBM_INT_0_STATUS) &
		 A6XX_RBBM_INT_0_MASK_RBBM_HANG_DETECT);
}

static bool a8xx_idle(struct msm_gpu *gpu, struct msm_ringbuffer *ring)
{
	/* wait for CP to drain ringbuffer: */
	if (!adreno_idle(gpu, ring))
		return false;

	if (spin_until(_a8xx_check_idle(gpu))) {
		DRM_ERROR(
			"%s: %ps: timeout waiting for GPU to idle: status %8.8X irq %8.8X rptr/wptr %d/%d\n",
			gpu->name, __builtin_return_address(0),
			gpu_read(gpu, REG_A8XX_RBBM_STATUS),
			gpu_read(gpu, REG_A8XX_RBBM_INT_0_STATUS),
			gpu_read(gpu, REG_A6XX_CP_RB_RPTR),
			gpu_read(gpu, REG_A6XX_CP_RB_WPTR));
		return false;
	}

	return true;
}

void a8xx_flush(struct msm_gpu *gpu, struct msm_ringbuffer *ring)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	uint32_t wptr;
	unsigned long flags;

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

static void a8xx_set_hwcg(struct msm_gpu *gpu, bool state)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	struct a6xx_gmu *gmu = &a6xx_gpu->gmu;
	u32 val;

	if (adreno_is_x285(adreno_gpu) && state)
		gpu_write(gpu, REG_A8XX_RBBM_CGC_0_PC, 0x00000702);

	gmu_write(gmu, REG_A6XX_GPU_GMU_AO_GMU_CGC_MODE_CNTL,
			state ? adreno_gpu->info->a6xx->gmu_cgc_mode : 0);
	gmu_write(gmu, REG_A6XX_GPU_GMU_AO_GMU_CGC_DELAY_CNTL,
			state ? 0x110111 : 0);
	gmu_write(gmu, REG_A6XX_GPU_GMU_AO_GMU_CGC_HYST_CNTL,
			state ? 0x55555 : 0);

	gpu_write(gpu, REG_A8XX_RBBM_CLOCK_CNTL_GLOBAL, 1);
	gpu_write(gpu, REG_A8XX_RBBM_CGC_GLOBAL_LOAD_CMD, !!state);

	if (state) {
		gpu_write(gpu, REG_A8XX_RBBM_CGC_P2S_TRIG_CMD, 1);

		if (gpu_poll_timeout(gpu, REG_A8XX_RBBM_CGC_P2S_STATUS, val,
				     val & A8XX_RBBM_CGC_P2S_STATUS_TXDONE, 1, 10)) {
			dev_err(&gpu->pdev->dev, "RBBM_CGC_P2S_STATUS TXDONE Poll failed\n");
			return;
		}

		gpu_write(gpu, REG_A8XX_RBBM_CLOCK_CNTL_GLOBAL, 0);
	} else {
		/*
		 * GMU enables clk gating in GBIF during boot up. So,
		 * override that here when hwcg feature is disabled
		 */
		gpu_rmw(gpu, REG_A8XX_GBIF_CX_CONFIG, BIT(0), 0);
	}
}

static void a8xx_set_cp_protect(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	const struct adreno_protect *protect = adreno_gpu->info->a6xx->protect;
	u32 cntl, final_cfg;
	unsigned int i;

	cntl = A8XX_CP_PROTECT_CNTL_PIPE_ACCESS_PROT_EN |
		A8XX_CP_PROTECT_CNTL_PIPE_ACCESS_FAULT_ON_VIOL_EN |
		A8XX_CP_PROTECT_CNTL_PIPE_LAST_SPAN_INF_RANGE |
		A8XX_CP_PROTECT_CNTL_PIPE_HALT_SQE_RANGE__MASK;
	/*
	 * Enable access protection to privileged registers, fault on an access
	 * protect violation and select the last span to protect from the start
	 * address all the way to the end of the register address space
	 */
	a8xx_write_pipe(gpu, PIPE_BR, REG_A8XX_CP_PROTECT_CNTL_PIPE, cntl);
	a8xx_write_pipe(gpu, PIPE_BV, REG_A8XX_CP_PROTECT_CNTL_PIPE, cntl);

	a8xx_aperture_clear(gpu);

	for (i = 0; i < protect->count; i++) {
		/* Intentionally skip writing to some registers */
		if (protect->regs[i]) {
			gpu_write(gpu, REG_A8XX_CP_PROTECT_GLOBAL(i), protect->regs[i]);
			final_cfg = protect->regs[i];
		}
	}

	/*
	 * Last span feature is only supported on PIPE specific register.
	 * So update those here
	 */
	a8xx_write_pipe(gpu, PIPE_BR, REG_A8XX_CP_PROTECT_PIPE(protect->count_max), final_cfg);
	a8xx_write_pipe(gpu, PIPE_BV, REG_A8XX_CP_PROTECT_PIPE(protect->count_max), final_cfg);

	a8xx_aperture_clear(gpu);
}

static void a8xx_set_ubwc_config(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	const struct qcom_ubwc_cfg_data *cfg = adreno_gpu->ubwc_config;
	u32 level2_swizzling_dis = !(cfg->ubwc_swizzle & UBWC_SWIZZLE_ENABLE_LVL2);
	u32 level3_swizzling_dis = !(cfg->ubwc_swizzle & UBWC_SWIZZLE_ENABLE_LVL3);
	bool rgba8888_lossless = false, fp16compoptdis = false;
	bool yuvnotcomptofc = false, min_acc_len_64b = false;
	bool rgb565_predicator = false, amsbc = false;
	bool ubwc_mode = qcom_ubwc_get_ubwc_mode(cfg);
	u32 ubwc_version = cfg->ubwc_enc_version;
	u32 hbb, hbb_hi, hbb_lo, mode = 1;
	u8 uavflagprd_inv = 2;

	switch (ubwc_version) {
	case UBWC_6_0:
		yuvnotcomptofc = true;
		mode = 5;
		break;
	case UBWC_5_0:
		amsbc = true;
		rgb565_predicator = true;
		mode = 4;
		break;
	case UBWC_4_0:
		amsbc = true;
		rgb565_predicator = true;
		fp16compoptdis = true;
		rgba8888_lossless = true;
		mode = 2;
		break;
	case UBWC_3_0:
		amsbc = true;
		mode = 1;
		break;
	default:
		dev_err(&gpu->pdev->dev, "Unknown UBWC version: 0x%x\n", ubwc_version);
		break;
	}

	/*
	 * We subtract 13 from the highest bank bit (13 is the minimum value
	 * allowed by hw) and write the lowest two bits of the remaining value
	 * as hbb_lo and the one above it as hbb_hi to the hardware.
	 */
	WARN_ON(cfg->highest_bank_bit < 13);
	hbb = cfg->highest_bank_bit - 13;
	hbb_hi = hbb >> 2;
	hbb_lo = hbb & 3;
	a8xx_write_pipe(gpu, PIPE_BV, REG_A8XX_GRAS_NC_MODE_CNTL, hbb << 5);
	a8xx_write_pipe(gpu, PIPE_BR, REG_A8XX_GRAS_NC_MODE_CNTL, hbb << 5);

	a8xx_write_pipe(gpu, PIPE_BR, REG_A8XX_RB_CCU_NC_MODE_CNTL,
			yuvnotcomptofc << 6 |
			hbb_hi << 3 |
			hbb_lo << 1);

	a8xx_write_pipe(gpu, PIPE_BR, REG_A8XX_RB_CMP_NC_MODE_CNTL,
			mode << 15 |
			yuvnotcomptofc << 6 |
			rgba8888_lossless << 4 |
			fp16compoptdis << 3 |
			rgb565_predicator << 2 |
			amsbc << 1 |
			min_acc_len_64b);

	a8xx_aperture_clear(gpu);

	gpu_write(gpu, REG_A6XX_SP_NC_MODE_CNTL,
		  level3_swizzling_dis << 13 |
		  level2_swizzling_dis << 12 |
		  hbb_hi << 10 |
		  uavflagprd_inv << 4 |
		  min_acc_len_64b << 3 |
		  hbb_lo << 1 | ubwc_mode);

	gpu_write(gpu, REG_A6XX_TPL1_NC_MODE_CNTL,
		  level3_swizzling_dis << 7 |
		  level2_swizzling_dis << 6 |
		  hbb_hi << 4 |
		  min_acc_len_64b << 3 |
		  hbb_lo << 1 | ubwc_mode);
}

static void a8xx_nonctxt_config(struct msm_gpu *gpu, u32 *gmem_protect)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	const struct a6xx_info *info = adreno_gpu->info->a6xx;
	const struct adreno_reglist_pipe *regs = info->nonctxt_reglist;
	unsigned int pipe_id, i;
	unsigned long flags;

	for (pipe_id = PIPE_NONE; pipe_id <= PIPE_DDE_BV; pipe_id++) {
		/* We don't have support for LPAC yet */
		if (pipe_id == PIPE_LPAC)
			continue;

		a8xx_aperture_acquire(gpu, pipe_id, &flags);

		for (i = 0; regs[i].offset; i++) {
			if (!(BIT(pipe_id) & regs[i].pipe))
				continue;

			if (regs[i].offset == REG_A8XX_RB_GC_GMEM_PROTECT)
				*gmem_protect = regs[i].value;

			gpu_write(gpu, regs[i].offset, regs[i].value);
		}

		a8xx_aperture_release(gpu, flags);
	}

	a8xx_aperture_clear(gpu);
}

static int a8xx_cp_init(struct msm_gpu *gpu)
{
	struct msm_ringbuffer *ring = gpu->rb[0];
	u32 mask;

	/* Disable concurrent binning before sending CP init */
	OUT_PKT7(ring, CP_THREAD_CONTROL, 1);
	OUT_RING(ring, BIT(27));

	OUT_PKT7(ring, CP_ME_INIT, 4);

	/* Use multiple HW contexts */
	mask = BIT(0);

	/* Enable error detection */
	mask |= BIT(1);

	/* Set default reset state */
	mask |= BIT(3);

	/* Disable save/restore of performance counters across preemption */
	mask |= BIT(6);

	OUT_RING(ring, mask);

	/* Enable multiple hardware contexts */
	OUT_RING(ring, 0x00000003);

	/* Enable error detection */
	OUT_RING(ring, 0x20000000);

	/* Operation mode mask */
	OUT_RING(ring, 0x00000002);

	a6xx_flush(gpu, ring);
	return a8xx_idle(gpu, ring) ? 0 : -EINVAL;
}

#define A8XX_INT_MASK \
	(A6XX_RBBM_INT_0_MASK_CP_AHB_ERROR | \
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

#define A8XX_APRIV_MASK \
	(A8XX_CP_APRIV_CNTL_PIPE_ICACHE | \
	 A8XX_CP_APRIV_CNTL_PIPE_RBFETCH | \
	 A8XX_CP_APRIV_CNTL_PIPE_RBPRIVLEVEL | \
	 A8XX_CP_APRIV_CNTL_PIPE_RBRPWB)

#define A8XX_BR_APRIV_MASK \
	(A8XX_APRIV_MASK | \
	 A8XX_CP_APRIV_CNTL_PIPE_CDREAD | \
	 A8XX_CP_APRIV_CNTL_PIPE_CDWRITE)

#define A8XX_CP_GLOBAL_INT_MASK \
	(A8XX_CP_GLOBAL_INT_MASK_HWFAULTBR | \
	 A8XX_CP_GLOBAL_INT_MASK_HWFAULTBV | \
	 A8XX_CP_GLOBAL_INT_MASK_HWFAULTLPAC | \
	 A8XX_CP_GLOBAL_INT_MASK_HWFAULTAQE0 | \
	 A8XX_CP_GLOBAL_INT_MASK_HWFAULTAQE1 | \
	 A8XX_CP_GLOBAL_INT_MASK_HWFAULTDDEBR | \
	 A8XX_CP_GLOBAL_INT_MASK_HWFAULTDDEBV | \
	 A8XX_CP_GLOBAL_INT_MASK_SWFAULTBR | \
	 A8XX_CP_GLOBAL_INT_MASK_SWFAULTBV | \
	 A8XX_CP_GLOBAL_INT_MASK_SWFAULTLPAC | \
	 A8XX_CP_GLOBAL_INT_MASK_SWFAULTAQE0 | \
	 A8XX_CP_GLOBAL_INT_MASK_SWFAULTAQE1 | \
	 A8XX_CP_GLOBAL_INT_MASK_SWFAULTDDEBR | \
	 A8XX_CP_GLOBAL_INT_MASK_SWFAULTDDEBV)

#define A8XX_CP_INTERRUPT_STATUS_MASK_PIPE \
	(A8XX_CP_INTERRUPT_STATUS_MASK_PIPE_CSFRBWRAP | \
	 A8XX_CP_INTERRUPT_STATUS_MASK_PIPE_CSFIB1WRAP | \
	 A8XX_CP_INTERRUPT_STATUS_MASK_PIPE_CSFIB2WRAP | \
	 A8XX_CP_INTERRUPT_STATUS_MASK_PIPE_CSFIB3WRAP | \
	 A8XX_CP_INTERRUPT_STATUS_MASK_PIPE_CSFSDSWRAP | \
	 A8XX_CP_INTERRUPT_STATUS_MASK_PIPE_CSFMRBWRAP | \
	 A8XX_CP_INTERRUPT_STATUS_MASK_PIPE_CSFVSDWRAP | \
	 A8XX_CP_INTERRUPT_STATUS_MASK_PIPE_OPCODEERROR | \
	 A8XX_CP_INTERRUPT_STATUS_MASK_PIPE_VSDPARITYERROR | \
	 A8XX_CP_INTERRUPT_STATUS_MASK_PIPE_REGISTERPROTECTIONERROR | \
	 A8XX_CP_INTERRUPT_STATUS_MASK_PIPE_ILLEGALINSTRUCTION | \
	 A8XX_CP_INTERRUPT_STATUS_MASK_PIPE_SMMUFAULT | \
	 A8XX_CP_INTERRUPT_STATUS_MASK_PIPE_VBIFRESPCLIENT| \
	 A8XX_CP_INTERRUPT_STATUS_MASK_PIPE_VBIFRESPTYPE | \
	 A8XX_CP_INTERRUPT_STATUS_MASK_PIPE_VBIFRESPREAD | \
	 A8XX_CP_INTERRUPT_STATUS_MASK_PIPE_VBIFRESP | \
	 A8XX_CP_INTERRUPT_STATUS_MASK_PIPE_RTWROVF | \
	 A8XX_CP_INTERRUPT_STATUS_MASK_PIPE_LRZRTWROVF | \
	 A8XX_CP_INTERRUPT_STATUS_MASK_PIPE_LRZRTREFCNTOVF | \
	 A8XX_CP_INTERRUPT_STATUS_MASK_PIPE_LRZRTCLRRESMISS)

#define A8XX_CP_HW_FAULT_STATUS_MASK_PIPE \
	(A8XX_CP_HW_FAULT_STATUS_MASK_PIPE_CSFRBFAULT | \
	 A8XX_CP_HW_FAULT_STATUS_MASK_PIPE_CSFIB1FAULT | \
	 A8XX_CP_HW_FAULT_STATUS_MASK_PIPE_CSFIB2FAULT | \
	 A8XX_CP_HW_FAULT_STATUS_MASK_PIPE_CSFIB3FAULT | \
	 A8XX_CP_HW_FAULT_STATUS_MASK_PIPE_CSFSDSFAULT | \
	 A8XX_CP_HW_FAULT_STATUS_MASK_PIPE_CSFMRBFAULT | \
	 A8XX_CP_HW_FAULT_STATUS_MASK_PIPE_CSFVSDFAULT | \
	 A8XX_CP_HW_FAULT_STATUS_MASK_PIPE_SQEREADBURSTOVF | \
	 A8XX_CP_HW_FAULT_STATUS_MASK_PIPE_EVENTENGINEOVF | \
	 A8XX_CP_HW_FAULT_STATUS_MASK_PIPE_UCODEERROR)

static int hw_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	struct a6xx_gmu *gmu = &a6xx_gpu->gmu;
	unsigned int pipe_id, i;
	u32 gmem_protect = 0;
	u64 gmem_range_min;
	int ret;

	ret = a6xx_gmu_set_oob(&a6xx_gpu->gmu, GMU_OOB_GPU_SET);
	if (ret)
		return ret;

	/* Clear the cached value to force aperture configuration next time */
	a6xx_gpu->cached_aperture = UINT_MAX;
	a8xx_aperture_clear(gpu);

	/* Clear GBIF halt in case GX domain was not collapsed */
	gpu_write(gpu, REG_A6XX_GBIF_HALT, 0);
	gpu_read(gpu, REG_A6XX_GBIF_HALT);

	gpu_write(gpu, REG_A8XX_RBBM_GBIF_HALT, 0);
	gpu_read(gpu, REG_A8XX_RBBM_GBIF_HALT);

	gpu_write(gpu, REG_A6XX_RBBM_SECVID_TSB_CNTL, 0);

	/*
	 * Disable the trusted memory range - we don't actually supported secure
	 * memory rendering at this point in time and we don't want to block off
	 * part of the virtual memory space.
	 */
	gpu_write64(gpu, REG_A6XX_RBBM_SECVID_TSB_TRUSTED_BASE, 0x00000000);
	gpu_write(gpu, REG_A6XX_RBBM_SECVID_TSB_TRUSTED_SIZE, 0x00000000);

	/* Make all blocks contribute to the GPU BUSY perf counter */
	gpu_write(gpu, REG_A8XX_RBBM_PERFCTR_GPU_BUSY_MASKED, 0xffffffff);

	/* Setup GMEM Range in UCHE */
	gmem_range_min = SZ_64M;
	/* Set the GMEM VA range [0x100000:0x100000 + gpu->gmem - 1] */
	gpu_write64(gpu, REG_A8XX_UCHE_CCHE_GC_GMEM_RANGE_MIN, gmem_range_min);
	gpu_write64(gpu, REG_A8XX_SP_HLSQ_GC_GMEM_RANGE_MIN, gmem_range_min);

	/* Setup UCHE Trap region */
	gpu_write64(gpu, REG_A8XX_UCHE_TRAP_BASE, adreno_gpu->uche_trap_base);
	gpu_write64(gpu, REG_A8XX_UCHE_WRITE_THRU_BASE, adreno_gpu->uche_trap_base);
	gpu_write64(gpu, REG_A8XX_UCHE_CCHE_TRAP_BASE, adreno_gpu->uche_trap_base);
	gpu_write64(gpu, REG_A8XX_UCHE_CCHE_WRITE_THRU_BASE, adreno_gpu->uche_trap_base);

	/* Turn on performance counters */
	gpu_write(gpu, REG_A8XX_RBBM_PERFCTR_CNTL, 0x1);
	gpu_write(gpu, REG_A8XX_RBBM_SLICE_PERFCTR_CNTL, 0x1);

	/* Turn on the IFPC counter (countable 4 on XOCLK1) */
	gmu_write(&a6xx_gpu->gmu, REG_A8XX_GMU_CX_GMU_POWER_COUNTER_SELECT_XOCLK_1,
		  FIELD_PREP(GENMASK(7, 0), 0x4));

	/* Select CP0 to always count cycles */
	gpu_write(gpu, REG_A8XX_CP_PERFCTR_CP_SEL(0), 1);

	a8xx_set_ubwc_config(gpu);

	/* Set weights for bicubic filtering */
	gpu_write(gpu, REG_A8XX_TPL1_BICUBIC_WEIGHTS_TABLE(0), 0);
	gpu_write(gpu, REG_A8XX_TPL1_BICUBIC_WEIGHTS_TABLE(1), 0x3fe05ff4);
	gpu_write(gpu, REG_A8XX_TPL1_BICUBIC_WEIGHTS_TABLE(2), 0x3fa0ebee);
	gpu_write(gpu, REG_A8XX_TPL1_BICUBIC_WEIGHTS_TABLE(3), 0x3f5193ed);
	gpu_write(gpu, REG_A8XX_TPL1_BICUBIC_WEIGHTS_TABLE(4), 0x3f0243f0);
	gpu_write(gpu, REG_A8XX_TPL1_BICUBIC_WEIGHTS_TABLE(5), 0x00000000);
	gpu_write(gpu, REG_A8XX_TPL1_BICUBIC_WEIGHTS_TABLE(6), 0x3fd093e8);
	gpu_write(gpu, REG_A8XX_TPL1_BICUBIC_WEIGHTS_TABLE(7), 0x3f4133dc);
	gpu_write(gpu, REG_A8XX_TPL1_BICUBIC_WEIGHTS_TABLE(8), 0x3ea1dfdb);
	gpu_write(gpu, REG_A8XX_TPL1_BICUBIC_WEIGHTS_TABLE(9), 0x3e0283e0);
	gpu_write(gpu, REG_A8XX_TPL1_BICUBIC_WEIGHTS_TABLE(10), 0x0000ac2b);
	gpu_write(gpu, REG_A8XX_TPL1_BICUBIC_WEIGHTS_TABLE(11), 0x0000f01d);
	gpu_write(gpu, REG_A8XX_TPL1_BICUBIC_WEIGHTS_TABLE(12), 0x00114412);
	gpu_write(gpu, REG_A8XX_TPL1_BICUBIC_WEIGHTS_TABLE(13), 0x0021980a);
	gpu_write(gpu, REG_A8XX_TPL1_BICUBIC_WEIGHTS_TABLE(14), 0x0051ec05);
	gpu_write(gpu, REG_A8XX_TPL1_BICUBIC_WEIGHTS_TABLE(15), 0x0000380e);
	gpu_write(gpu, REG_A8XX_TPL1_BICUBIC_WEIGHTS_TABLE(16), 0x3ff09001);
	gpu_write(gpu, REG_A8XX_TPL1_BICUBIC_WEIGHTS_TABLE(17), 0x3fc10bfa);
	gpu_write(gpu, REG_A8XX_TPL1_BICUBIC_WEIGHTS_TABLE(18), 0x3f9193f7);
	gpu_write(gpu, REG_A8XX_TPL1_BICUBIC_WEIGHTS_TABLE(19), 0x3f7227f7);

	gpu_write(gpu, REG_A8XX_UCHE_CLIENT_PF, BIT(7) | 0x1);

	a8xx_nonctxt_config(gpu, &gmem_protect);

	/* Enable fault detection */
	gpu_write(gpu, REG_A8XX_RBBM_INTERFACE_HANG_INT_CNTL, BIT(30) | 0xcfffff);
	gpu_write(gpu, REG_A8XX_RBBM_SLICE_INTERFACE_HANG_INT_CNTL, BIT(30));

	/* Set up the CX GMU counter 0 to count busy ticks */
	gmu_write(gmu, REG_A6XX_GPU_GMU_AO_GPU_CX_BUSY_MASK, 0xff000000);

	/* Enable the power counter */
	gmu_rmw(gmu, REG_A8XX_GMU_CX_GMU_POWER_COUNTER_SELECT_XOCLK_0, 0xff, BIT(5));
	gmu_write(gmu, REG_A8XX_GMU_CX_GMU_POWER_COUNTER_ENABLE, 1);

	/* Protect registers from the CP */
	a8xx_set_cp_protect(gpu);

	/* Enable the GMEM save/restore feature for preemption */
	a8xx_write_pipe(gpu, PIPE_BR, REG_A6XX_RB_CONTEXT_SWITCH_GMEM_SAVE_RESTORE_ENABLE, 1);

	for (pipe_id = PIPE_BR; pipe_id <= PIPE_DDE_BV; pipe_id++) {
		u32 apriv_mask = A8XX_APRIV_MASK;
		unsigned long flags;

		if (pipe_id == PIPE_LPAC)
			continue;

		if (pipe_id == PIPE_BR)
			apriv_mask = A8XX_BR_APRIV_MASK;

		a8xx_aperture_acquire(gpu, pipe_id, &flags);
		gpu_write(gpu, REG_A8XX_CP_APRIV_CNTL_PIPE, apriv_mask);
		gpu_write(gpu, REG_A8XX_CP_INTERRUPT_STATUS_MASK_PIPE,
				A8XX_CP_INTERRUPT_STATUS_MASK_PIPE);
		gpu_write(gpu, REG_A8XX_CP_HW_FAULT_STATUS_MASK_PIPE,
				A8XX_CP_HW_FAULT_STATUS_MASK_PIPE);
		a8xx_aperture_release(gpu, flags);
	}

	a8xx_aperture_clear(gpu);

	/* Enable interrupts */
	gpu_write(gpu, REG_A8XX_CP_INTERRUPT_STATUS_MASK_GLOBAL, A8XX_CP_GLOBAL_INT_MASK);
	gpu_write(gpu, REG_A8XX_RBBM_INT_0_MASK, A8XX_INT_MASK);

	ret = adreno_hw_init(gpu);
	if (ret)
		goto out;

	gpu_write64(gpu, REG_A8XX_CP_SQE_INSTR_BASE, a6xx_gpu->sqe_iova);
	if (a6xx_gpu->aqe_iova)
		gpu_write64(gpu, REG_A8XX_CP_AQE_INSTR_BASE_0, a6xx_gpu->aqe_iova);

	/* Set the ringbuffer address */
	gpu_write64(gpu, REG_A6XX_CP_RB_BASE, gpu->rb[0]->iova);
	gpu_write(gpu, REG_A6XX_CP_RB_CNTL, MSM_GPU_RB_CNTL_DEFAULT);

	/* Configure the RPTR shadow if needed: */
	gpu_write64(gpu, REG_A6XX_CP_RB_RPTR_ADDR, shadowptr(a6xx_gpu, gpu->rb[0]));
	gpu_write64(gpu, REG_A8XX_CP_RB_RPTR_ADDR_BV, rbmemptr(gpu->rb[0], bv_rptr));

	for (i = 0; i < gpu->nr_rings; i++)
		a6xx_gpu->shadow[i] = 0;

	/* Always come up on rb 0 */
	a6xx_gpu->cur_ring = gpu->rb[0];

	for (i = 0; i < gpu->nr_rings; i++)
		gpu->rb[i]->cur_ctx_seqno = 0;

	/* Enable the SQE_to start the CP engine */
	gpu_write(gpu, REG_A8XX_CP_SQE_CNTL, 1);

	ret = a8xx_cp_init(gpu);
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
		if (!a8xx_idle(gpu, gpu->rb[0]))
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

	/*
	 * GMEM_PROTECT register should be programmed after GPU is transitioned to
	 * non-secure mode
	 */
	a8xx_write_pipe(gpu, PIPE_BR, REG_A8XX_RB_GC_GMEM_PROTECT, gmem_protect);
	WARN_ON(!gmem_protect);
	a8xx_aperture_clear(gpu);

	/* Enable hardware clockgating */
	a8xx_set_hwcg(gpu, true);
out:
	/*
	 * Tell the GMU that we are done touching the GPU and it can start power
	 * management
	 */
	a6xx_gmu_clear_oob(&a6xx_gpu->gmu, GMU_OOB_GPU_SET);

	return ret;
}

int a8xx_hw_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	int ret;

	mutex_lock(&a6xx_gpu->gmu.lock);
	ret = hw_init(gpu);
	mutex_unlock(&a6xx_gpu->gmu.lock);

	return ret;
}

static void a8xx_dump(struct msm_gpu *gpu)
{
	DRM_DEV_INFO(&gpu->pdev->dev, "status:   %08x\n", gpu_read(gpu, REG_A8XX_RBBM_STATUS));
	adreno_dump(gpu);
}

void a8xx_recover(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	struct a6xx_gmu *gmu = &a6xx_gpu->gmu;
	int active_submits;

	adreno_dump_info(gpu);

	if (hang_debug)
		a8xx_dump(gpu);

	/*
	 * To handle recovery specific sequences during the rpm suspend we are
	 * about to trigger
	 */
	a6xx_gpu->hung = true;

	/* Halt SQE first */
	gpu_write(gpu, REG_A8XX_CP_SQE_CNTL, 3);

	pm_runtime_dont_use_autosuspend(&gpu->pdev->dev);

	/* active_submit won't change until we make a submission */
	mutex_lock(&gpu->active_lock);
	active_submits = gpu->active_submits;

	/*
	 * Temporarily clear active_submits count to silence a WARN() in the
	 * runtime suspend cb
	 */
	gpu->active_submits = 0;

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

static const char *a8xx_uche_fault_block(struct msm_gpu *gpu, u32 mid)
{
	static const char * const uche_clients[] = {
		"BR_VFD", "BR_SP", "BR_VSC", "BR_VPC", "BR_HLSQ", "BR_PC", "BR_LRZ", "BR_TP",
		"BV_VFD", "BV_SP", "BV_VSC", "BV_VPC", "BV_HLSQ", "BV_PC", "BV_LRZ", "BV_TP",
		"STCHE",
	};
	static const char * const uche_clients_lpac[] = {
		"-", "SP_LPAC", "-", "-", "HLSQ_LPAC", "-", "-", "TP_LPAC",
	};
	u32 val;

	/*
	 * The source of the data depends on the mid ID read from FSYNR1.
	 * and the client ID read from the UCHE block
	 */
	val = gpu_read(gpu, REG_A8XX_UCHE_CLIENT_PF);

	val &= GENMASK(6, 0);

	/* mid=3 refers to BR or BV */
	if (mid == 3) {
		if (val < ARRAY_SIZE(uche_clients))
			return uche_clients[val];
		else
			return "UCHE";
	}

	/* mid=8 refers to LPAC */
	if (mid == 8) {
		if (val < ARRAY_SIZE(uche_clients_lpac))
			return uche_clients_lpac[val];
		else
			return "UCHE_LPAC";
	}

	return "Unknown";
}

static const char *a8xx_fault_block(struct msm_gpu *gpu, u32 id)
{
	switch (id) {
	case 0x0:
		return "CP";
	case 0x1:
		return "UCHE: Unknown";
	case 0x2:
		return "UCHE_LPAC: Unknown";
	case 0x3:
	case 0x8:
		return a8xx_uche_fault_block(gpu, id);
	case 0x4:
		return "CCU";
	case 0x5:
		return "Flag cache";
	case 0x6:
		return "PREFETCH";
	case 0x7:
		return "GMU";
	case 0x9:
		return "UCHE_HPAC";
	}

	return "Unknown";
}

int a8xx_fault_handler(void *arg, unsigned long iova, int flags, void *data)
{
	struct msm_gpu *gpu = arg;
	struct adreno_smmu_fault_info *info = data;
	const char *block = "unknown";

	u32 scratch[] = {
			gpu_read(gpu, REG_A8XX_CP_SCRATCH_GLOBAL(0)),
			gpu_read(gpu, REG_A8XX_CP_SCRATCH_GLOBAL(1)),
			gpu_read(gpu, REG_A8XX_CP_SCRATCH_GLOBAL(2)),
			gpu_read(gpu, REG_A8XX_CP_SCRATCH_GLOBAL(3)),
	};

	if (info)
		block = a8xx_fault_block(gpu, info->fsynr1 & 0xff);

	return adreno_fault_handler(gpu, iova, flags, info, block, scratch);
}

static void a8xx_cp_hw_err_irq(struct msm_gpu *gpu)
{
	u32 status = gpu_read(gpu, REG_A8XX_CP_INTERRUPT_STATUS_GLOBAL);
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	u32 slice = a8xx_get_first_slice(a6xx_gpu);
	u32 hw_fault_mask = GENMASK(6, 0);
	u32 sw_fault_mask = GENMASK(22, 16);
	u32 pipe = 0;

	dev_err_ratelimited(&gpu->pdev->dev, "CP Fault Global INT status: 0x%x\n", status);

	if (status & (A8XX_CP_GLOBAL_INT_MASK_HWFAULTBR |
		      A8XX_CP_GLOBAL_INT_MASK_SWFAULTBR))
		pipe |= BIT(PIPE_BR);

	if (status & (A8XX_CP_GLOBAL_INT_MASK_HWFAULTBV |
		      A8XX_CP_GLOBAL_INT_MASK_SWFAULTBV))
		pipe |= BIT(PIPE_BV);

	if (!pipe) {
		dev_err_ratelimited(&gpu->pdev->dev, "CP Fault Unknown pipe\n");
		goto out;
	}

	for (unsigned int pipe_id = PIPE_NONE; pipe_id <= PIPE_DDE_BV; pipe_id++) {
		if (!(BIT(pipe_id) & pipe))
			continue;

		if (hw_fault_mask & status) {
			status = a8xx_read_pipe_slice(gpu, pipe_id, slice,
					REG_A8XX_CP_HW_FAULT_STATUS_PIPE);
			dev_err_ratelimited(&gpu->pdev->dev,
					"CP HW FAULT pipe: %u status: 0x%x\n", pipe_id, status);
		}

		if (sw_fault_mask & status) {
			status = a8xx_read_pipe_slice(gpu, pipe_id, slice,
					REG_A8XX_CP_INTERRUPT_STATUS_PIPE);
			dev_err_ratelimited(&gpu->pdev->dev,
					"CP SW FAULT pipe: %u status: 0x%x\n", pipe_id, status);

			if (status & BIT(8)) {
				a8xx_write_pipe(gpu, pipe_id, REG_A8XX_CP_SQE_STAT_ADDR_PIPE, 1);
				status = a8xx_read_pipe_slice(gpu, pipe_id, slice,
						REG_A8XX_CP_SQE_STAT_DATA_PIPE);
				dev_err_ratelimited(&gpu->pdev->dev,
						"CP Opcode error, opcode=0x%x\n", status);
			}

			if (status & BIT(10)) {
				status = a8xx_read_pipe_slice(gpu, pipe_id, slice,
						REG_A8XX_CP_PROTECT_STATUS_PIPE);
				dev_err_ratelimited(&gpu->pdev->dev,
						"CP REG PROTECT error, status=0x%x\n", status);
			}
		}
	}

out:
	/* Turn off interrupts to avoid triggering recovery again */
	a8xx_aperture_clear(gpu);
	gpu_write(gpu, REG_A8XX_CP_INTERRUPT_STATUS_MASK_GLOBAL, 0);
	gpu_write(gpu, REG_A8XX_RBBM_INT_0_MASK, 0);

	kthread_queue_work(gpu->worker, &gpu->recover_work);
}

static u32 gpu_periph_read(struct msm_gpu *gpu, u32 dbg_offset)
{
	gpu_write(gpu, REG_A8XX_CP_SQE_UCODE_DBG_ADDR_PIPE, dbg_offset);

	return gpu_read(gpu, REG_A8XX_CP_SQE_UCODE_DBG_DATA_PIPE);
}

static u64 gpu_periph_read64(struct msm_gpu *gpu, u32 dbg_offset)
{
	u64 lo, hi;

	lo = gpu_periph_read(gpu, dbg_offset);
	hi = gpu_periph_read(gpu, dbg_offset + 1);

	return (hi << 32) | lo;
}

#define CP_PERIPH_IB1_BASE_LO   0x7005
#define CP_PERIPH_IB1_BASE_HI   0x7006
#define CP_PERIPH_IB1_SIZE      0x7007
#define CP_PERIPH_IB1_OFFSET    0x7008
#define CP_PERIPH_IB2_BASE_LO   0x7009
#define CP_PERIPH_IB2_BASE_HI   0x700a
#define CP_PERIPH_IB2_SIZE      0x700b
#define CP_PERIPH_IB2_OFFSET    0x700c
#define CP_PERIPH_IB3_BASE_LO   0x700d
#define CP_PERIPH_IB3_BASE_HI   0x700e
#define CP_PERIPH_IB3_SIZE      0x700f
#define CP_PERIPH_IB3_OFFSET    0x7010

static void a8xx_fault_detect_irq(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	struct msm_ringbuffer *ring = gpu->funcs->active_ring(gpu);
	unsigned long flags;

	/*
	 * If stalled on SMMU fault, we could trip the GPU's hang detection,
	 * but the fault handler will trigger the devcore dump, and we want
	 * to otherwise resume normally rather than killing the submit, so
	 * just bail.
	 */
	if (gpu_read(gpu, REG_A8XX_RBBM_MISC_STATUS) & A8XX_RBBM_MISC_STATUS_SMMU_STALLED_ON_FAULT)
		return;

	/*
	 * Force the GPU to stay on until after we finish
	 * collecting information
	 */
	if (!adreno_has_gmu_wrapper(adreno_gpu))
		gmu_write(&a6xx_gpu->gmu, REG_A6XX_GMU_GMU_PWR_COL_KEEPALIVE, 1);

	DRM_DEV_ERROR(&gpu->pdev->dev,
		"gpu fault ring %d fence %x status %8.8X gfx_status %8.8X\n",
		ring ? ring->id : -1, ring ? ring->fctx->last_fence : 0,
		gpu_read(gpu, REG_A8XX_RBBM_STATUS), gpu_read(gpu, REG_A8XX_RBBM_GFX_STATUS));

	a8xx_aperture_acquire(gpu, PIPE_BR, &flags);

	DRM_DEV_ERROR(&gpu->pdev->dev,
		"BR: status %8.8X rb %4.4x/%4.4x ib1 %16.16llX/%4.4x ib2 %16.16llX/%4.4x ib3 %16.16llX/%4.4x\n",
		gpu_read(gpu, REG_A8XX_RBBM_GFX_BR_STATUS),
		gpu_read(gpu, REG_A6XX_CP_RB_RPTR),
		gpu_read(gpu, REG_A6XX_CP_RB_WPTR),
		gpu_periph_read64(gpu, CP_PERIPH_IB1_BASE_LO),
		gpu_periph_read(gpu, CP_PERIPH_IB1_OFFSET),
		gpu_periph_read64(gpu, CP_PERIPH_IB2_BASE_LO),
		gpu_periph_read(gpu, CP_PERIPH_IB2_OFFSET),
		gpu_periph_read64(gpu, CP_PERIPH_IB3_BASE_LO),
		gpu_periph_read(gpu, CP_PERIPH_IB3_OFFSET));

	a8xx_aperture_release(gpu, flags);
	a8xx_aperture_acquire(gpu, PIPE_BV, &flags);

	DRM_DEV_ERROR(&gpu->pdev->dev,
		"BV: status %8.8X rb %4.4x/%4.4x ib1 %16.16llX/%4.4x ib2 %16.16llX/%4.4x ib3 %16.16llX/%4.4x\n",
		gpu_read(gpu, REG_A8XX_RBBM_GFX_BV_STATUS),
		gpu_read(gpu, REG_A8XX_CP_RB_RPTR_BV),
		gpu_read(gpu, REG_A6XX_CP_RB_WPTR),
		gpu_periph_read64(gpu, CP_PERIPH_IB1_BASE_LO),
		gpu_periph_read(gpu, CP_PERIPH_IB1_OFFSET),
		gpu_periph_read64(gpu, CP_PERIPH_IB2_BASE_LO),
		gpu_periph_read(gpu, CP_PERIPH_IB2_OFFSET),
		gpu_periph_read64(gpu, CP_PERIPH_IB3_BASE_LO),
		gpu_periph_read(gpu, CP_PERIPH_IB3_OFFSET));

	a8xx_aperture_release(gpu, flags);
	a8xx_aperture_clear(gpu);

	/* Turn off the hangcheck timer to keep it from bothering us */
	timer_delete(&gpu->hangcheck_timer);

	kthread_queue_work(gpu->worker, &gpu->recover_work);
}

static void a8xx_sw_fuse_violation_irq(struct msm_gpu *gpu)
{
	u32 status;

	status = gpu_read(gpu, REG_A8XX_RBBM_SW_FUSE_INT_STATUS);
	gpu_write(gpu, REG_A8XX_RBBM_SW_FUSE_INT_MASK, 0);

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

irqreturn_t a8xx_irq(struct msm_gpu *gpu)
{
	struct msm_drm_private *priv = gpu->dev->dev_private;
	u32 status = gpu_read(gpu, REG_A8XX_RBBM_INT_0_STATUS);

	gpu_write(gpu, REG_A8XX_RBBM_INT_CLEAR_CMD, status);

	if (priv->disable_err_irq)
		status &= A6XX_RBBM_INT_0_MASK_CP_CACHE_FLUSH_TS;

	if (status & A6XX_RBBM_INT_0_MASK_RBBM_HANG_DETECT)
		a8xx_fault_detect_irq(gpu);

	if (status & A6XX_RBBM_INT_0_MASK_CP_AHB_ERROR) {
		u32 rl0, rl1;

		rl0 = gpu_read(gpu, REG_A8XX_CP_RL_ERROR_DETAILS_0);
		rl1 = gpu_read(gpu, REG_A8XX_CP_RL_ERROR_DETAILS_1);
		dev_err_ratelimited(&gpu->pdev->dev,
				"CP | AHB bus error RL_ERROR_0: %x, RL_ERROR_1: %x\n", rl0, rl1);
	}

	if (status & A6XX_RBBM_INT_0_MASK_CP_HW_ERROR)
		a8xx_cp_hw_err_irq(gpu);

	if (status & A6XX_RBBM_INT_0_MASK_RBBM_ATB_ASYNCFIFO_OVERFLOW)
		dev_err_ratelimited(&gpu->pdev->dev, "RBBM | ATB ASYNC overflow\n");

	if (status & A6XX_RBBM_INT_0_MASK_RBBM_ATB_BUS_OVERFLOW)
		dev_err_ratelimited(&gpu->pdev->dev, "RBBM | ATB bus overflow\n");

	if (status & A6XX_RBBM_INT_0_MASK_UCHE_OOB_ACCESS)
		dev_err_ratelimited(&gpu->pdev->dev, "UCHE | Out of bounds access\n");

	if (status & A6XX_RBBM_INT_0_MASK_UCHE_TRAP_INTR)
		dev_err_ratelimited(&gpu->pdev->dev, "UCHE | Trap interrupt\n");

	if (status & A6XX_RBBM_INT_0_MASK_SWFUSEVIOLATION)
		a8xx_sw_fuse_violation_irq(gpu);

	if (status & A6XX_RBBM_INT_0_MASK_CP_CACHE_FLUSH_TS) {
		msm_gpu_retire(gpu);
		a6xx_preempt_trigger(gpu);
	}

	if (status & A6XX_RBBM_INT_0_MASK_CP_SW)
		a6xx_preempt_irq(gpu);

	return IRQ_HANDLED;
}

void a8xx_llc_activate(struct a6xx_gpu *a6xx_gpu)
{
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	struct msm_gpu *gpu = &adreno_gpu->base;

	if (!llcc_slice_activate(a6xx_gpu->llc_slice)) {
		u32 gpu_scid = llcc_get_slice_id(a6xx_gpu->llc_slice);

		gpu_scid &= GENMASK(5, 0);

		gpu_write(gpu, REG_A6XX_GBIF_SCACHE_CNTL1,
			  FIELD_PREP(GENMASK(29, 24), gpu_scid) |
			  FIELD_PREP(GENMASK(23, 18), gpu_scid) |
			  FIELD_PREP(GENMASK(17, 12), gpu_scid) |
			  FIELD_PREP(GENMASK(11, 6), gpu_scid)  |
			  FIELD_PREP(GENMASK(5, 0), gpu_scid));

		gpu_write(gpu, REG_A6XX_GBIF_SCACHE_CNTL0,
			  FIELD_PREP(GENMASK(27, 22), gpu_scid) |
			  FIELD_PREP(GENMASK(21, 16), gpu_scid) |
			  FIELD_PREP(GENMASK(15, 10), gpu_scid) |
			  BIT(8));
	}

	llcc_slice_activate(a6xx_gpu->htw_llc_slice);
}

#define GBIF_CLIENT_HALT_MASK		BIT(0)
#define GBIF_ARB_HALT_MASK		BIT(1)
#define VBIF_XIN_HALT_CTRL0_MASK	GENMASK(3, 0)
#define VBIF_RESET_ACK_MASK		0xF0
#define GPR0_GBIF_HALT_REQUEST		0x1E0

void a8xx_bus_clear_pending_transactions(struct adreno_gpu *adreno_gpu, bool gx_off)
{
	struct msm_gpu *gpu = &adreno_gpu->base;

	if (gx_off) {
		/* Halt the gx side of GBIF */
		gpu_write(gpu, REG_A8XX_RBBM_GBIF_HALT, 1);
		spin_until(gpu_read(gpu, REG_A8XX_RBBM_GBIF_HALT_ACK) & 1);
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

int a8xx_gmu_get_timestamp(struct msm_gpu *gpu, uint64_t *value)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);

	mutex_lock(&a6xx_gpu->gmu.lock);

	/* Force the GPU power on so we can read this register */
	a6xx_gmu_set_oob(&a6xx_gpu->gmu, GMU_OOB_PERFCOUNTER_SET);

	*value = gpu_read64(gpu, REG_A8XX_CP_ALWAYS_ON_COUNTER);

	a6xx_gmu_clear_oob(&a6xx_gpu->gmu, GMU_OOB_PERFCOUNTER_SET);

	mutex_unlock(&a6xx_gpu->gmu.lock);

	return 0;
}

u64 a8xx_gpu_busy(struct msm_gpu *gpu, unsigned long *out_sample_rate)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	u64 busy_cycles;

	/* 19.2MHz */
	*out_sample_rate = 19200000;

	busy_cycles = gmu_read64(&a6xx_gpu->gmu,
			REG_A8XX_GMU_CX_GMU_POWER_COUNTER_XOCLK_0_L,
			REG_A8XX_GMU_CX_GMU_POWER_COUNTER_XOCLK_0_H);

	return busy_cycles;
}

bool a8xx_progress(struct msm_gpu *gpu, struct msm_ringbuffer *ring)
{
	return true;
}
