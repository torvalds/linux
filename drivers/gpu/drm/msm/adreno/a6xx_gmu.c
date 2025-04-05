// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2017-2019 The Linux Foundation. All rights reserved. */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/interconnect.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_opp.h>
#include <soc/qcom/cmd-db.h>
#include <soc/qcom/tcs.h>
#include <drm/drm_gem.h>

#include "a6xx_gpu.h"
#include "a6xx_gmu.xml.h"
#include "msm_gem.h"
#include "msm_gpu_trace.h"
#include "msm_mmu.h"

static void a6xx_gmu_fault(struct a6xx_gmu *gmu)
{
	struct a6xx_gpu *a6xx_gpu = container_of(gmu, struct a6xx_gpu, gmu);
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	struct msm_gpu *gpu = &adreno_gpu->base;

	/* FIXME: add a banner here */
	gmu->hung = true;

	/* Turn off the hangcheck timer while we are resetting */
	timer_delete(&gpu->hangcheck_timer);

	/* Queue the GPU handler because we need to treat this as a recovery */
	kthread_queue_work(gpu->worker, &gpu->recover_work);
}

static irqreturn_t a6xx_gmu_irq(int irq, void *data)
{
	struct a6xx_gmu *gmu = data;
	u32 status;

	status = gmu_read(gmu, REG_A6XX_GMU_AO_HOST_INTERRUPT_STATUS);
	gmu_write(gmu, REG_A6XX_GMU_AO_HOST_INTERRUPT_CLR, status);

	if (status & A6XX_GMU_AO_HOST_INTERRUPT_STATUS_WDOG_BITE) {
		dev_err_ratelimited(gmu->dev, "GMU watchdog expired\n");

		a6xx_gmu_fault(gmu);
	}

	if (status &  A6XX_GMU_AO_HOST_INTERRUPT_STATUS_HOST_AHB_BUS_ERROR)
		dev_err_ratelimited(gmu->dev, "GMU AHB bus error\n");

	if (status & A6XX_GMU_AO_HOST_INTERRUPT_STATUS_FENCE_ERR)
		dev_err_ratelimited(gmu->dev, "GMU fence error: 0x%x\n",
			gmu_read(gmu, REG_A6XX_GMU_AHB_FENCE_STATUS));

	return IRQ_HANDLED;
}

static irqreturn_t a6xx_hfi_irq(int irq, void *data)
{
	struct a6xx_gmu *gmu = data;
	u32 status;

	status = gmu_read(gmu, REG_A6XX_GMU_GMU2HOST_INTR_INFO);
	gmu_write(gmu, REG_A6XX_GMU_GMU2HOST_INTR_CLR, status);

	if (status & A6XX_GMU_GMU2HOST_INTR_INFO_CM3_FAULT) {
		dev_err_ratelimited(gmu->dev, "GMU firmware fault\n");

		a6xx_gmu_fault(gmu);
	}

	return IRQ_HANDLED;
}

bool a6xx_gmu_sptprac_is_on(struct a6xx_gmu *gmu)
{
	u32 val;

	/* This can be called from gpu state code so make sure GMU is valid */
	if (!gmu->initialized)
		return false;

	val = gmu_read(gmu, REG_A6XX_GMU_SPTPRAC_PWR_CLK_STATUS);

	return !(val &
		(A6XX_GMU_SPTPRAC_PWR_CLK_STATUS_SPTPRAC_GDSC_POWER_OFF |
		A6XX_GMU_SPTPRAC_PWR_CLK_STATUS_SP_CLOCK_OFF));
}

/* Check to see if the GX rail is still powered */
bool a6xx_gmu_gx_is_on(struct a6xx_gmu *gmu)
{
	u32 val;

	/* This can be called from gpu state code so make sure GMU is valid */
	if (!gmu->initialized)
		return false;

	val = gmu_read(gmu, REG_A6XX_GMU_SPTPRAC_PWR_CLK_STATUS);

	return !(val &
		(A6XX_GMU_SPTPRAC_PWR_CLK_STATUS_GX_HM_GDSC_POWER_OFF |
		A6XX_GMU_SPTPRAC_PWR_CLK_STATUS_GX_HM_CLK_OFF));
}

void a6xx_gmu_set_freq(struct msm_gpu *gpu, struct dev_pm_opp *opp,
		       bool suspended)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	const struct a6xx_info *info = adreno_gpu->info->a6xx;
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	struct a6xx_gmu *gmu = &a6xx_gpu->gmu;
	u32 perf_index;
	u32 bw_index = 0;
	unsigned long gpu_freq;
	int ret = 0;

	gpu_freq = dev_pm_opp_get_freq(opp);

	if (gpu_freq == gmu->freq)
		return;

	for (perf_index = 0; perf_index < gmu->nr_gpu_freqs - 1; perf_index++)
		if (gpu_freq == gmu->gpu_freqs[perf_index])
			break;

	/* If enabled, find the corresponding DDR bandwidth index */
	if (info->bcms && gmu->nr_gpu_bws > 1) {
		unsigned int bw = dev_pm_opp_get_bw(opp, true, 0);

		for (bw_index = 0; bw_index < gmu->nr_gpu_bws - 1; bw_index++) {
			if (bw == gmu->gpu_bw_table[bw_index])
				break;
		}

		/* Vote AB as a fraction of the max bandwidth, starting from A750 */
		if (bw && adreno_is_a750_family(adreno_gpu)) {
			u64 tmp;

			/* For now, vote for 25% of the bandwidth */
			tmp = bw * 25;
			do_div(tmp, 100);

			/*
			 * The AB vote consists of a 16 bit wide quantized level
			 * against the maximum supported bandwidth.
			 * Quantization can be calculated as below:
			 * vote = (bandwidth * 2^16) / max bandwidth
			 */
			tmp *= MAX_AB_VOTE;
			do_div(tmp, gmu->gpu_bw_table[gmu->nr_gpu_bws - 1]);

			bw_index |= AB_VOTE(clamp(tmp, 1, MAX_AB_VOTE));
			bw_index |= AB_VOTE_ENABLE;
		}
	}

	gmu->current_perf_index = perf_index;
	gmu->freq = gmu->gpu_freqs[perf_index];

	trace_msm_gmu_freq_change(gmu->freq, perf_index);

	/*
	 * This can get called from devfreq while the hardware is idle. Don't
	 * bring up the power if it isn't already active. All we're doing here
	 * is updating the frequency so that when we come back online we're at
	 * the right rate.
	 */
	if (suspended)
		return;

	if (!gmu->legacy) {
		a6xx_hfi_set_freq(gmu, perf_index, bw_index);
		/* With Bandwidth voting, we now vote for all resources, so skip OPP set */
		if (!bw_index)
			dev_pm_opp_set_opp(&gpu->pdev->dev, opp);
		return;
	}

	gmu_write(gmu, REG_A6XX_GMU_DCVS_ACK_OPTION, 0);

	gmu_write(gmu, REG_A6XX_GMU_DCVS_PERF_SETTING,
			((3 & 0xf) << 28) | perf_index);

	/*
	 * Send an invalid index as a vote for the bus bandwidth and let the
	 * firmware decide on the right vote
	 */
	gmu_write(gmu, REG_A6XX_GMU_DCVS_BW_SETTING, 0xff);

	/* Set and clear the OOB for DCVS to trigger the GMU */
	a6xx_gmu_set_oob(gmu, GMU_OOB_DCVS_SET);
	a6xx_gmu_clear_oob(gmu, GMU_OOB_DCVS_SET);

	ret = gmu_read(gmu, REG_A6XX_GMU_DCVS_RETURN);
	if (ret)
		dev_err(gmu->dev, "GMU set GPU frequency error: %d\n", ret);

	dev_pm_opp_set_opp(&gpu->pdev->dev, opp);
}

unsigned long a6xx_gmu_get_freq(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	struct a6xx_gmu *gmu = &a6xx_gpu->gmu;

	return  gmu->freq;
}

static bool a6xx_gmu_check_idle_level(struct a6xx_gmu *gmu)
{
	u32 val;
	int local = gmu->idle_level;

	/* SPTP and IFPC both report as IFPC */
	if (gmu->idle_level == GMU_IDLE_STATE_SPTP)
		local = GMU_IDLE_STATE_IFPC;

	val = gmu_read(gmu, REG_A6XX_GPU_GMU_CX_GMU_RPMH_POWER_STATE);

	if (val == local) {
		if (gmu->idle_level != GMU_IDLE_STATE_IFPC ||
			!a6xx_gmu_gx_is_on(gmu))
			return true;
	}

	return false;
}

/* Wait for the GMU to get to its most idle state */
int a6xx_gmu_wait_for_idle(struct a6xx_gmu *gmu)
{
	return spin_until(a6xx_gmu_check_idle_level(gmu));
}

static int a6xx_gmu_start(struct a6xx_gmu *gmu)
{
	struct a6xx_gpu *a6xx_gpu = container_of(gmu, struct a6xx_gpu, gmu);
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	u32 mask, reset_val, val;
	int ret;

	val = gmu_read(gmu, REG_A6XX_GMU_CM3_DTCM_START + 0xff8);
	if (val <= 0x20010004) {
		mask = 0xffffffff;
		reset_val = 0xbabeface;
	} else {
		mask = 0x1ff;
		reset_val = 0x100;
	}

	gmu_write(gmu, REG_A6XX_GMU_CM3_SYSRESET, 1);

	/* Set the log wptr index
	 * note: downstream saves the value in poweroff and restores it here
	 */
	if (adreno_is_a7xx(adreno_gpu))
		gmu_write(gmu, REG_A7XX_GMU_GENERAL_9, 0);
	else
		gmu_write(gmu, REG_A6XX_GPU_GMU_CX_GMU_PWR_COL_CP_RESP, 0);


	gmu_write(gmu, REG_A6XX_GMU_CM3_SYSRESET, 0);

	ret = gmu_poll_timeout(gmu, REG_A6XX_GMU_CM3_FW_INIT_RESULT, val,
		(val & mask) == reset_val, 100, 10000);

	if (ret)
		DRM_DEV_ERROR(gmu->dev, "GMU firmware initialization timed out\n");

	return ret;
}

static int a6xx_gmu_hfi_start(struct a6xx_gmu *gmu)
{
	u32 val;
	int ret;

	gmu_write(gmu, REG_A6XX_GMU_HFI_CTRL_INIT, 1);

	ret = gmu_poll_timeout(gmu, REG_A6XX_GMU_HFI_CTRL_STATUS, val,
		val & 1, 100, 10000);
	if (ret)
		DRM_DEV_ERROR(gmu->dev, "Unable to start the HFI queues\n");

	return ret;
}

struct a6xx_gmu_oob_bits {
	int set, ack, set_new, ack_new, clear, clear_new;
	const char *name;
};

/* These are the interrupt / ack bits for each OOB request that are set
 * in a6xx_gmu_set_oob and a6xx_clear_oob
 */
static const struct a6xx_gmu_oob_bits a6xx_gmu_oob_bits[] = {
	[GMU_OOB_GPU_SET] = {
		.name = "GPU_SET",
		.set = 16,
		.ack = 24,
		.set_new = 30,
		.ack_new = 31,
		.clear = 24,
		.clear_new = 31,
	},

	[GMU_OOB_PERFCOUNTER_SET] = {
		.name = "PERFCOUNTER",
		.set = 17,
		.ack = 25,
		.set_new = 28,
		.ack_new = 30,
		.clear = 25,
		.clear_new = 29,
	},

	[GMU_OOB_BOOT_SLUMBER] = {
		.name = "BOOT_SLUMBER",
		.set = 22,
		.ack = 30,
		.clear = 30,
	},

	[GMU_OOB_DCVS_SET] = {
		.name = "GPU_DCVS",
		.set = 23,
		.ack = 31,
		.clear = 31,
	},
};

/* Trigger a OOB (out of band) request to the GMU */
int a6xx_gmu_set_oob(struct a6xx_gmu *gmu, enum a6xx_gmu_oob_state state)
{
	int ret;
	u32 val;
	int request, ack;

	WARN_ON_ONCE(!mutex_is_locked(&gmu->lock));

	if (state >= ARRAY_SIZE(a6xx_gmu_oob_bits))
		return -EINVAL;

	if (gmu->legacy) {
		request = a6xx_gmu_oob_bits[state].set;
		ack = a6xx_gmu_oob_bits[state].ack;
	} else {
		request = a6xx_gmu_oob_bits[state].set_new;
		ack = a6xx_gmu_oob_bits[state].ack_new;
		if (!request || !ack) {
			DRM_DEV_ERROR(gmu->dev,
				      "Invalid non-legacy GMU request %s\n",
				      a6xx_gmu_oob_bits[state].name);
			return -EINVAL;
		}
	}

	/* Trigger the equested OOB operation */
	gmu_write(gmu, REG_A6XX_GMU_HOST2GMU_INTR_SET, 1 << request);

	/* Wait for the acknowledge interrupt */
	ret = gmu_poll_timeout(gmu, REG_A6XX_GMU_GMU2HOST_INTR_INFO, val,
		val & (1 << ack), 100, 10000);

	if (ret)
		DRM_DEV_ERROR(gmu->dev,
			"Timeout waiting for GMU OOB set %s: 0x%x\n",
				a6xx_gmu_oob_bits[state].name,
				gmu_read(gmu, REG_A6XX_GMU_GMU2HOST_INTR_INFO));

	/* Clear the acknowledge interrupt */
	gmu_write(gmu, REG_A6XX_GMU_GMU2HOST_INTR_CLR, 1 << ack);

	return ret;
}

/* Clear a pending OOB state in the GMU */
void a6xx_gmu_clear_oob(struct a6xx_gmu *gmu, enum a6xx_gmu_oob_state state)
{
	int bit;

	WARN_ON_ONCE(!mutex_is_locked(&gmu->lock));

	if (state >= ARRAY_SIZE(a6xx_gmu_oob_bits))
		return;

	if (gmu->legacy)
		bit = a6xx_gmu_oob_bits[state].clear;
	else
		bit = a6xx_gmu_oob_bits[state].clear_new;

	gmu_write(gmu, REG_A6XX_GMU_HOST2GMU_INTR_SET, 1 << bit);
}

/* Enable CPU control of SPTP power power collapse */
int a6xx_sptprac_enable(struct a6xx_gmu *gmu)
{
	int ret;
	u32 val;

	if (!gmu->legacy)
		return 0;

	gmu_write(gmu, REG_A6XX_GMU_GX_SPTPRAC_POWER_CONTROL, 0x778000);

	ret = gmu_poll_timeout(gmu, REG_A6XX_GMU_SPTPRAC_PWR_CLK_STATUS, val,
		(val & 0x38) == 0x28, 1, 100);

	if (ret) {
		DRM_DEV_ERROR(gmu->dev, "Unable to power on SPTPRAC: 0x%x\n",
			gmu_read(gmu, REG_A6XX_GMU_SPTPRAC_PWR_CLK_STATUS));
	}

	return 0;
}

/* Disable CPU control of SPTP power power collapse */
void a6xx_sptprac_disable(struct a6xx_gmu *gmu)
{
	u32 val;
	int ret;

	if (!gmu->legacy)
		return;

	/* Make sure retention is on */
	gmu_rmw(gmu, REG_A6XX_GPU_CC_GX_GDSCR, 0, (1 << 11));

	gmu_write(gmu, REG_A6XX_GMU_GX_SPTPRAC_POWER_CONTROL, 0x778001);

	ret = gmu_poll_timeout(gmu, REG_A6XX_GMU_SPTPRAC_PWR_CLK_STATUS, val,
		(val & 0x04), 100, 10000);

	if (ret)
		DRM_DEV_ERROR(gmu->dev, "failed to power off SPTPRAC: 0x%x\n",
			gmu_read(gmu, REG_A6XX_GMU_SPTPRAC_PWR_CLK_STATUS));
}

/* Let the GMU know we are starting a boot sequence */
static int a6xx_gmu_gfx_rail_on(struct a6xx_gmu *gmu)
{
	u32 vote;

	/* Let the GMU know we are getting ready for boot */
	gmu_write(gmu, REG_A6XX_GMU_BOOT_SLUMBER_OPTION, 0);

	/* Choose the "default" power level as the highest available */
	vote = gmu->gx_arc_votes[gmu->nr_gpu_freqs - 1];

	gmu_write(gmu, REG_A6XX_GMU_GX_VOTE_IDX, vote & 0xff);
	gmu_write(gmu, REG_A6XX_GMU_MX_VOTE_IDX, (vote >> 8) & 0xff);

	/* Let the GMU know the boot sequence has started */
	return a6xx_gmu_set_oob(gmu, GMU_OOB_BOOT_SLUMBER);
}

static void a6xx_gemnoc_workaround(struct a6xx_gmu *gmu)
{
	struct a6xx_gpu *a6xx_gpu = container_of(gmu, struct a6xx_gpu, gmu);
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;

	/*
	 * GEMNoC can power collapse whilst the GPU is being powered down, resulting
	 * in the power down sequence not being fully executed. That in turn can
	 * prevent CX_GDSC from collapsing. Assert Qactive to avoid this.
	 */
	if (adreno_is_a621(adreno_gpu) || adreno_is_7c3(adreno_gpu))
		gmu_write(gmu, REG_A6XX_GMU_AO_AHB_FENCE_CTRL, BIT(0));
}

/* Let the GMU know that we are about to go into slumber */
static int a6xx_gmu_notify_slumber(struct a6xx_gmu *gmu)
{
	int ret;

	/* Disable the power counter so the GMU isn't busy */
	gmu_write(gmu, REG_A6XX_GMU_CX_GMU_POWER_COUNTER_ENABLE, 0);

	/* Disable SPTP_PC if the CPU is responsible for it */
	if (gmu->idle_level < GMU_IDLE_STATE_SPTP)
		a6xx_sptprac_disable(gmu);

	if (!gmu->legacy) {
		ret = a6xx_hfi_send_prep_slumber(gmu);
		goto out;
	}

	/* Tell the GMU to get ready to slumber */
	gmu_write(gmu, REG_A6XX_GMU_BOOT_SLUMBER_OPTION, 1);

	ret = a6xx_gmu_set_oob(gmu, GMU_OOB_BOOT_SLUMBER);
	a6xx_gmu_clear_oob(gmu, GMU_OOB_BOOT_SLUMBER);

	if (!ret) {
		/* Check to see if the GMU really did slumber */
		if (gmu_read(gmu, REG_A6XX_GPU_GMU_CX_GMU_RPMH_POWER_STATE)
			!= 0x0f) {
			DRM_DEV_ERROR(gmu->dev, "The GMU did not go into slumber\n");
			ret = -ETIMEDOUT;
		}
	}

out:
	a6xx_gemnoc_workaround(gmu);

	/* Put fence into allow mode */
	gmu_write(gmu, REG_A6XX_GMU_AO_AHB_FENCE_CTRL, 0);
	return ret;
}

static int a6xx_rpmh_start(struct a6xx_gmu *gmu)
{
	int ret;
	u32 val;

	gmu_write(gmu, REG_A6XX_GMU_RSCC_CONTROL_REQ, BIT(1));

	ret = gmu_poll_timeout(gmu, REG_A6XX_GMU_RSCC_CONTROL_ACK, val,
		val & (1 << 1), 100, 10000);
	if (ret) {
		DRM_DEV_ERROR(gmu->dev, "Unable to power on the GPU RSC\n");
		return ret;
	}

	ret = gmu_poll_timeout_rscc(gmu, REG_A6XX_RSCC_SEQ_BUSY_DRV0, val,
		!val, 100, 10000);

	if (ret) {
		DRM_DEV_ERROR(gmu->dev, "GPU RSC sequence stuck while waking up the GPU\n");
		return ret;
	}

	gmu_write(gmu, REG_A6XX_GMU_RSCC_CONTROL_REQ, 0);

	return 0;
}

static void a6xx_rpmh_stop(struct a6xx_gmu *gmu)
{
	int ret;
	u32 val;

	gmu_write(gmu, REG_A6XX_GMU_RSCC_CONTROL_REQ, 1);

	ret = gmu_poll_timeout_rscc(gmu, REG_A6XX_GPU_RSCC_RSC_STATUS0_DRV0,
		val, val & (1 << 16), 100, 10000);
	if (ret)
		DRM_DEV_ERROR(gmu->dev, "Unable to power off the GPU RSC\n");

	gmu_write(gmu, REG_A6XX_GMU_RSCC_CONTROL_REQ, 0);
}

static inline void pdc_write(void __iomem *ptr, u32 offset, u32 value)
{
	writel(value, ptr + (offset << 2));
}

static void __iomem *a6xx_gmu_get_mmio(struct platform_device *pdev,
		const char *name);

static void a6xx_gmu_rpmh_init(struct a6xx_gmu *gmu)
{
	struct a6xx_gpu *a6xx_gpu = container_of(gmu, struct a6xx_gpu, gmu);
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	struct platform_device *pdev = to_platform_device(gmu->dev);
	void __iomem *pdcptr = a6xx_gmu_get_mmio(pdev, "gmu_pdc");
	u32 seqmem0_drv0_reg = REG_A6XX_RSCC_SEQ_MEM_0_DRV0;
	void __iomem *seqptr = NULL;
	uint32_t pdc_address_offset;
	bool pdc_in_aop = false;

	if (IS_ERR(pdcptr))
		goto err;

	if (adreno_is_a650_family(adreno_gpu) ||
	    adreno_is_a7xx(adreno_gpu))
		pdc_in_aop = true;
	else if (adreno_is_a618(adreno_gpu) || adreno_is_a640_family(adreno_gpu))
		pdc_address_offset = 0x30090;
	else if (adreno_is_a619(adreno_gpu))
		pdc_address_offset = 0x300a0;
	else
		pdc_address_offset = 0x30080;

	if (!pdc_in_aop) {
		seqptr = a6xx_gmu_get_mmio(pdev, "gmu_pdc_seq");
		if (IS_ERR(seqptr))
			goto err;
	}

	/* Disable SDE clock gating */
	gmu_write_rscc(gmu, REG_A6XX_GPU_RSCC_RSC_STATUS0_DRV0, BIT(24));

	/* Setup RSC PDC handshake for sleep and wakeup */
	gmu_write_rscc(gmu, REG_A6XX_RSCC_PDC_SLAVE_ID_DRV0, 1);
	gmu_write_rscc(gmu, REG_A6XX_RSCC_HIDDEN_TCS_CMD0_DATA, 0);
	gmu_write_rscc(gmu, REG_A6XX_RSCC_HIDDEN_TCS_CMD0_ADDR, 0);
	gmu_write_rscc(gmu, REG_A6XX_RSCC_HIDDEN_TCS_CMD0_DATA + 2, 0);
	gmu_write_rscc(gmu, REG_A6XX_RSCC_HIDDEN_TCS_CMD0_ADDR + 2, 0);
	gmu_write_rscc(gmu, REG_A6XX_RSCC_HIDDEN_TCS_CMD0_DATA + 4,
		       adreno_is_a740_family(adreno_gpu) ? 0x80000021 : 0x80000000);
	gmu_write_rscc(gmu, REG_A6XX_RSCC_HIDDEN_TCS_CMD0_ADDR + 4, 0);
	gmu_write_rscc(gmu, REG_A6XX_RSCC_OVERRIDE_START_ADDR, 0);
	gmu_write_rscc(gmu, REG_A6XX_RSCC_PDC_SEQ_START_ADDR, 0x4520);
	gmu_write_rscc(gmu, REG_A6XX_RSCC_PDC_MATCH_VALUE_LO, 0x4510);
	gmu_write_rscc(gmu, REG_A6XX_RSCC_PDC_MATCH_VALUE_HI, 0x4514);

	/* The second spin of A7xx GPUs messed with some register offsets.. */
	if (adreno_is_a740_family(adreno_gpu))
		seqmem0_drv0_reg = REG_A7XX_RSCC_SEQ_MEM_0_DRV0_A740;

	/* Load RSC sequencer uCode for sleep and wakeup */
	if (adreno_is_a650_family(adreno_gpu) ||
	    adreno_is_a7xx(adreno_gpu)) {
		gmu_write_rscc(gmu, seqmem0_drv0_reg, 0xeaaae5a0);
		gmu_write_rscc(gmu, seqmem0_drv0_reg + 1, 0xe1a1ebab);
		gmu_write_rscc(gmu, seqmem0_drv0_reg + 2, 0xa2e0a581);
		gmu_write_rscc(gmu, seqmem0_drv0_reg + 3, 0xecac82e2);
		gmu_write_rscc(gmu, seqmem0_drv0_reg + 4, 0x0020edad);
	} else {
		gmu_write_rscc(gmu, REG_A6XX_RSCC_SEQ_MEM_0_DRV0, 0xa7a506a0);
		gmu_write_rscc(gmu, REG_A6XX_RSCC_SEQ_MEM_0_DRV0 + 1, 0xa1e6a6e7);
		gmu_write_rscc(gmu, REG_A6XX_RSCC_SEQ_MEM_0_DRV0 + 2, 0xa2e081e1);
		gmu_write_rscc(gmu, REG_A6XX_RSCC_SEQ_MEM_0_DRV0 + 3, 0xe9a982e2);
		gmu_write_rscc(gmu, REG_A6XX_RSCC_SEQ_MEM_0_DRV0 + 4, 0x0020e8a8);
	}

	if (pdc_in_aop)
		goto setup_pdc;

	/* Load PDC sequencer uCode for power up and power down sequence */
	pdc_write(seqptr, REG_A6XX_PDC_GPU_SEQ_MEM_0, 0xfebea1e1);
	pdc_write(seqptr, REG_A6XX_PDC_GPU_SEQ_MEM_0 + 1, 0xa5a4a3a2);
	pdc_write(seqptr, REG_A6XX_PDC_GPU_SEQ_MEM_0 + 2, 0x8382a6e0);
	pdc_write(seqptr, REG_A6XX_PDC_GPU_SEQ_MEM_0 + 3, 0xbce3e284);
	pdc_write(seqptr, REG_A6XX_PDC_GPU_SEQ_MEM_0 + 4, 0x002081fc);

	/* Set TCS commands used by PDC sequence for low power modes */
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS1_CMD_ENABLE_BANK, 7);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS1_CMD_WAIT_FOR_CMPL_BANK, 0);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS1_CONTROL, 0);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS1_CMD0_MSGID, 0x10108);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS1_CMD0_ADDR, 0x30010);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS1_CMD0_DATA, 1);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS1_CMD0_MSGID + 4, 0x10108);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS1_CMD0_ADDR + 4, 0x30000);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS1_CMD0_DATA + 4, 0x0);

	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS1_CMD0_MSGID + 8, 0x10108);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS1_CMD0_ADDR + 8, pdc_address_offset);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS1_CMD0_DATA + 8, 0x0);

	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD_ENABLE_BANK, 7);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD_WAIT_FOR_CMPL_BANK, 0);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CONTROL, 0);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD0_MSGID, 0x10108);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD0_ADDR, 0x30010);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD0_DATA, 2);

	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD0_MSGID + 4, 0x10108);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD0_ADDR + 4, 0x30000);
	if (adreno_is_a618(adreno_gpu) || adreno_is_a619(adreno_gpu) ||
			adreno_is_a650_family(adreno_gpu))
		pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD0_DATA + 4, 0x2);
	else
		pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD0_DATA + 4, 0x3);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD0_MSGID + 8, 0x10108);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD0_ADDR + 8, pdc_address_offset);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD0_DATA + 8, 0x3);

	/* Setup GPU PDC */
setup_pdc:
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_SEQ_START_ADDR, 0);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_ENABLE_PDC, 0x80000001);

	/* ensure no writes happen before the uCode is fully written */
	wmb();

	a6xx_rpmh_stop(gmu);

err:
	if (!IS_ERR_OR_NULL(pdcptr))
		iounmap(pdcptr);
	if (!IS_ERR_OR_NULL(seqptr))
		iounmap(seqptr);
}

/*
 * The lowest 16 bits of this value are the number of XO clock cycles for main
 * hysteresis which is set at 0x1680 cycles (300 us).  The higher 16 bits are
 * for the shorter hysteresis that happens after main - this is 0xa (.5 us)
 */

#define GMU_PWR_COL_HYST 0x000a1680

/* Set up the idle state for the GMU */
static void a6xx_gmu_power_config(struct a6xx_gmu *gmu)
{
	struct a6xx_gpu *a6xx_gpu = container_of(gmu, struct a6xx_gpu, gmu);
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;

	/* Disable GMU WB/RB buffer */
	gmu_write(gmu, REG_A6XX_GMU_SYS_BUS_CONFIG, 0x1);
	gmu_write(gmu, REG_A6XX_GMU_ICACHE_CONFIG, 0x1);
	gmu_write(gmu, REG_A6XX_GMU_DCACHE_CONFIG, 0x1);

	/* A7xx knows better by default! */
	if (adreno_is_a7xx(adreno_gpu))
		return;

	gmu_write(gmu, REG_A6XX_GMU_PWR_COL_INTER_FRAME_CTRL, 0x9c40400);

	switch (gmu->idle_level) {
	case GMU_IDLE_STATE_IFPC:
		gmu_write(gmu, REG_A6XX_GMU_PWR_COL_INTER_FRAME_HYST,
			GMU_PWR_COL_HYST);
		gmu_rmw(gmu, REG_A6XX_GMU_PWR_COL_INTER_FRAME_CTRL, 0,
			A6XX_GMU_PWR_COL_INTER_FRAME_CTRL_IFPC_ENABLE |
			A6XX_GMU_PWR_COL_INTER_FRAME_CTRL_HM_POWER_COLLAPSE_ENABLE);
		fallthrough;
	case GMU_IDLE_STATE_SPTP:
		gmu_write(gmu, REG_A6XX_GMU_PWR_COL_SPTPRAC_HYST,
			GMU_PWR_COL_HYST);
		gmu_rmw(gmu, REG_A6XX_GMU_PWR_COL_INTER_FRAME_CTRL, 0,
			A6XX_GMU_PWR_COL_INTER_FRAME_CTRL_IFPC_ENABLE |
			A6XX_GMU_PWR_COL_INTER_FRAME_CTRL_SPTPRAC_POWER_CONTROL_ENABLE);
	}

	/* Enable RPMh GPU client */
	gmu_rmw(gmu, REG_A6XX_GMU_RPMH_CTRL, 0,
		A6XX_GMU_RPMH_CTRL_RPMH_INTERFACE_ENABLE |
		A6XX_GMU_RPMH_CTRL_LLC_VOTE_ENABLE |
		A6XX_GMU_RPMH_CTRL_DDR_VOTE_ENABLE |
		A6XX_GMU_RPMH_CTRL_MX_VOTE_ENABLE |
		A6XX_GMU_RPMH_CTRL_CX_VOTE_ENABLE |
		A6XX_GMU_RPMH_CTRL_GFX_VOTE_ENABLE);
}

struct block_header {
	u32 addr;
	u32 size;
	u32 type;
	u32 value;
	u32 data[];
};

static bool fw_block_mem(struct a6xx_gmu_bo *bo, const struct block_header *blk)
{
	if (!in_range(blk->addr, bo->iova, bo->size))
		return false;

	memcpy(bo->virt + blk->addr - bo->iova, blk->data, blk->size);
	return true;
}

static int a6xx_gmu_fw_load(struct a6xx_gmu *gmu)
{
	struct a6xx_gpu *a6xx_gpu = container_of(gmu, struct a6xx_gpu, gmu);
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	const struct firmware *fw_image = adreno_gpu->fw[ADRENO_FW_GMU];
	const struct block_header *blk;
	u32 reg_offset;
	u32 ver;

	u32 itcm_base = 0x00000000;
	u32 dtcm_base = 0x00040000;

	if (adreno_is_a650_family(adreno_gpu) || adreno_is_a7xx(adreno_gpu))
		dtcm_base = 0x10004000;

	if (gmu->legacy) {
		/* Sanity check the size of the firmware that was loaded */
		if (fw_image->size > 0x8000) {
			DRM_DEV_ERROR(gmu->dev,
				"GMU firmware is bigger than the available region\n");
			return -EINVAL;
		}

		gmu_write_bulk(gmu, REG_A6XX_GMU_CM3_ITCM_START,
			       (u32*) fw_image->data, fw_image->size);
		return 0;
	}


	for (blk = (const struct block_header *) fw_image->data;
	     (const u8*) blk < fw_image->data + fw_image->size;
	     blk = (const struct block_header *) &blk->data[blk->size >> 2]) {
		if (blk->size == 0)
			continue;

		if (in_range(blk->addr, itcm_base, SZ_16K)) {
			reg_offset = (blk->addr - itcm_base) >> 2;
			gmu_write_bulk(gmu,
				REG_A6XX_GMU_CM3_ITCM_START + reg_offset,
				blk->data, blk->size);
		} else if (in_range(blk->addr, dtcm_base, SZ_16K)) {
			reg_offset = (blk->addr - dtcm_base) >> 2;
			gmu_write_bulk(gmu,
				REG_A6XX_GMU_CM3_DTCM_START + reg_offset,
				blk->data, blk->size);
		} else if (!fw_block_mem(&gmu->icache, blk) &&
			   !fw_block_mem(&gmu->dcache, blk) &&
			   !fw_block_mem(&gmu->dummy, blk)) {
			DRM_DEV_ERROR(gmu->dev,
				"failed to match fw block (addr=%.8x size=%d data[0]=%.8x)\n",
				blk->addr, blk->size, blk->data[0]);
		}
	}

	ver = gmu_read(gmu, REG_A6XX_GMU_CORE_FW_VERSION);
	DRM_INFO_ONCE("Loaded GMU firmware v%u.%u.%u\n",
		      FIELD_GET(A6XX_GMU_CORE_FW_VERSION_MAJOR__MASK, ver),
		      FIELD_GET(A6XX_GMU_CORE_FW_VERSION_MINOR__MASK, ver),
		      FIELD_GET(A6XX_GMU_CORE_FW_VERSION_STEP__MASK, ver));

	return 0;
}

static int a6xx_gmu_fw_start(struct a6xx_gmu *gmu, unsigned int state)
{
	struct a6xx_gpu *a6xx_gpu = container_of(gmu, struct a6xx_gpu, gmu);
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	const struct a6xx_info *a6xx_info = adreno_gpu->info->a6xx;
	u32 fence_range_lower, fence_range_upper;
	u32 chipid = 0;
	int ret;

	/* Vote veto for FAL10 */
	if (adreno_is_a650_family(adreno_gpu) || adreno_is_a7xx(adreno_gpu)) {
		gmu_write(gmu, REG_A6XX_GPU_GMU_CX_GMU_CX_FALNEXT_INTF, 1);
		gmu_write(gmu, REG_A6XX_GPU_GMU_CX_GMU_CX_FAL_INTF, 1);
	}

	/* Turn on TCM (Tightly Coupled Memory) retention */
	if (adreno_is_a7xx(adreno_gpu))
		a6xx_llc_write(a6xx_gpu, REG_A7XX_CX_MISC_TCM_RET_CNTL, 1);
	else
		gmu_write(gmu, REG_A6XX_GMU_GENERAL_7, 1);

	if (state == GMU_WARM_BOOT) {
		ret = a6xx_rpmh_start(gmu);
		if (ret)
			return ret;
	} else {
		if (WARN(!adreno_gpu->fw[ADRENO_FW_GMU],
			"GMU firmware is not loaded\n"))
			return -ENOENT;

		ret = a6xx_rpmh_start(gmu);
		if (ret)
			return ret;

		ret = a6xx_gmu_fw_load(gmu);
		if (ret)
			return ret;
	}

	/* Clear init result to make sure we are getting a fresh value */
	gmu_write(gmu, REG_A6XX_GMU_CM3_FW_INIT_RESULT, 0);
	gmu_write(gmu, REG_A6XX_GMU_CM3_BOOT_CONFIG, 0x02);

	/* Write the iova of the HFI table */
	gmu_write(gmu, REG_A6XX_GMU_HFI_QTBL_ADDR, gmu->hfi.iova);
	gmu_write(gmu, REG_A6XX_GMU_HFI_QTBL_INFO, 1);

	if (adreno_is_a7xx(adreno_gpu)) {
		fence_range_upper = 0x32;
		fence_range_lower = 0x8a0;
	} else {
		fence_range_upper = 0xa;
		fence_range_lower = 0xa0;
	}

	gmu_write(gmu, REG_A6XX_GMU_AHB_FENCE_RANGE_0,
		  BIT(31) |
		  FIELD_PREP(GENMASK(30, 18), fence_range_upper) |
		  FIELD_PREP(GENMASK(17, 0), fence_range_lower));

	/*
	 * Snapshots toggle the NMI bit which will result in a jump to the NMI
	 * handler instead of __main. Set the M3 config value to avoid that.
	 */
	gmu_write(gmu, REG_A6XX_GMU_CM3_CFG, 0x4052);

	if (a6xx_info->gmu_chipid) {
		chipid = a6xx_info->gmu_chipid;
	} else {
		/*
		 * Note that the GMU has a slightly different layout for
		 * chip_id, for whatever reason, so a bit of massaging
		 * is needed.  The upper 16b are the same, but minor and
		 * patchid are packed in four bits each with the lower
		 * 8b unused:
		 */
		chipid  = adreno_gpu->chip_id & 0xffff0000;
		chipid |= (adreno_gpu->chip_id << 4) & 0xf000; /* minor */
		chipid |= (adreno_gpu->chip_id << 8) & 0x0f00; /* patchid */
	}

	if (adreno_is_a7xx(adreno_gpu)) {
		gmu_write(gmu, REG_A7XX_GMU_GENERAL_10, chipid);
		gmu_write(gmu, REG_A7XX_GMU_GENERAL_8,
			  (gmu->log.iova & GENMASK(31, 12)) |
			  ((gmu->log.size / SZ_4K - 1) & GENMASK(7, 0)));
	} else {
		gmu_write(gmu, REG_A6XX_GMU_HFI_SFR_ADDR, chipid);

		gmu_write(gmu, REG_A6XX_GPU_GMU_CX_GMU_PWR_COL_CP_MSG,
			  gmu->log.iova | (gmu->log.size / SZ_4K - 1));
	}

	/* Set up the lowest idle level on the GMU */
	a6xx_gmu_power_config(gmu);

	ret = a6xx_gmu_start(gmu);
	if (ret)
		return ret;

	if (gmu->legacy) {
		ret = a6xx_gmu_gfx_rail_on(gmu);
		if (ret)
			return ret;
	}

	/* Enable SPTP_PC if the CPU is responsible for it */
	if (gmu->idle_level < GMU_IDLE_STATE_SPTP) {
		ret = a6xx_sptprac_enable(gmu);
		if (ret)
			return ret;
	}

	ret = a6xx_gmu_hfi_start(gmu);
	if (ret)
		return ret;

	/* FIXME: Do we need this wmb() here? */
	wmb();

	return 0;
}

#define A6XX_HFI_IRQ_MASK \
	(A6XX_GMU_GMU2HOST_INTR_INFO_CM3_FAULT)

#define A6XX_GMU_IRQ_MASK \
	(A6XX_GMU_AO_HOST_INTERRUPT_STATUS_WDOG_BITE | \
	 A6XX_GMU_AO_HOST_INTERRUPT_STATUS_HOST_AHB_BUS_ERROR | \
	 A6XX_GMU_AO_HOST_INTERRUPT_STATUS_FENCE_ERR)

static void a6xx_gmu_irq_disable(struct a6xx_gmu *gmu)
{
	disable_irq(gmu->gmu_irq);
	disable_irq(gmu->hfi_irq);

	gmu_write(gmu, REG_A6XX_GMU_AO_HOST_INTERRUPT_MASK, ~0);
	gmu_write(gmu, REG_A6XX_GMU_GMU2HOST_INTR_MASK, ~0);
}

static void a6xx_gmu_rpmh_off(struct a6xx_gmu *gmu)
{
	struct a6xx_gpu *a6xx_gpu = container_of(gmu, struct a6xx_gpu, gmu);
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	u32 val, seqmem_off = 0;

	/* The second spin of A7xx GPUs messed with some register offsets.. */
	if (adreno_is_a740_family(adreno_gpu))
		seqmem_off = 4;

	/* Make sure there are no outstanding RPMh votes */
	gmu_poll_timeout_rscc(gmu, REG_A6XX_RSCC_TCS0_DRV0_STATUS + seqmem_off,
		val, (val & 1), 100, 10000);
	gmu_poll_timeout_rscc(gmu, REG_A6XX_RSCC_TCS1_DRV0_STATUS + seqmem_off,
		val, (val & 1), 100, 10000);
	gmu_poll_timeout_rscc(gmu, REG_A6XX_RSCC_TCS2_DRV0_STATUS + seqmem_off,
		val, (val & 1), 100, 10000);
	gmu_poll_timeout_rscc(gmu, REG_A6XX_RSCC_TCS3_DRV0_STATUS + seqmem_off,
		val, (val & 1), 100, 1000);
}

/* Force the GMU off in case it isn't responsive */
static void a6xx_gmu_force_off(struct a6xx_gmu *gmu)
{
	struct a6xx_gpu *a6xx_gpu = container_of(gmu, struct a6xx_gpu, gmu);
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	struct msm_gpu *gpu = &adreno_gpu->base;

	/*
	 * Turn off keep alive that might have been enabled by the hang
	 * interrupt
	 */
	gmu_write(&a6xx_gpu->gmu, REG_A6XX_GMU_GMU_PWR_COL_KEEPALIVE, 0);

	/* Flush all the queues */
	a6xx_hfi_stop(gmu);

	/* Stop the interrupts */
	a6xx_gmu_irq_disable(gmu);

	/* Force off SPTP in case the GMU is managing it */
	a6xx_sptprac_disable(gmu);

	a6xx_gemnoc_workaround(gmu);

	/* Make sure there are no outstanding RPMh votes */
	a6xx_gmu_rpmh_off(gmu);

	/* Clear the WRITEDROPPED fields and put fence into allow mode */
	gmu_write(gmu, REG_A6XX_GMU_AHB_FENCE_STATUS_CLR, 0x7);
	gmu_write(gmu, REG_A6XX_GMU_AO_AHB_FENCE_CTRL, 0);

	/* Make sure the above writes go through */
	wmb();

	/* Halt the gmu cm3 core */
	gmu_write(gmu, REG_A6XX_GMU_CM3_SYSRESET, 1);

	a6xx_bus_clear_pending_transactions(adreno_gpu, true);

	/* Reset GPU core blocks */
	a6xx_gpu_sw_reset(gpu, true);
}

static void a6xx_gmu_set_initial_freq(struct msm_gpu *gpu, struct a6xx_gmu *gmu)
{
	struct dev_pm_opp *gpu_opp;
	unsigned long gpu_freq = gmu->gpu_freqs[gmu->current_perf_index];

	gpu_opp = dev_pm_opp_find_freq_exact(&gpu->pdev->dev, gpu_freq, true);
	if (IS_ERR(gpu_opp))
		return;

	gmu->freq = 0; /* so a6xx_gmu_set_freq() doesn't exit early */
	a6xx_gmu_set_freq(gpu, gpu_opp, false);
	dev_pm_opp_put(gpu_opp);
}

static void a6xx_gmu_set_initial_bw(struct msm_gpu *gpu, struct a6xx_gmu *gmu)
{
	struct dev_pm_opp *gpu_opp;
	unsigned long gpu_freq = gmu->gpu_freqs[gmu->current_perf_index];

	gpu_opp = dev_pm_opp_find_freq_exact(&gpu->pdev->dev, gpu_freq, true);
	if (IS_ERR(gpu_opp))
		return;

	dev_pm_opp_set_opp(&gpu->pdev->dev, gpu_opp);
	dev_pm_opp_put(gpu_opp);
}

int a6xx_gmu_resume(struct a6xx_gpu *a6xx_gpu)
{
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	struct msm_gpu *gpu = &adreno_gpu->base;
	struct a6xx_gmu *gmu = &a6xx_gpu->gmu;
	int status, ret;

	if (WARN(!gmu->initialized, "The GMU is not set up yet\n"))
		return -EINVAL;

	gmu->hung = false;

	/* Notify AOSS about the ACD state (unimplemented for now => disable it) */
	if (!IS_ERR(gmu->qmp)) {
		ret = qmp_send(gmu->qmp, "{class: gpu, res: acd, val: %d}",
			       0 /* Hardcode ACD to be disabled for now */);
		if (ret)
			dev_err(gmu->dev, "failed to send GPU ACD state\n");
	}

	/* Turn on the resources */
	pm_runtime_get_sync(gmu->dev);

	/*
	 * "enable" the GX power domain which won't actually do anything but it
	 * will make sure that the refcounting is correct in case we need to
	 * bring down the GX after a GMU failure
	 */
	if (!IS_ERR_OR_NULL(gmu->gxpd))
		pm_runtime_get_sync(gmu->gxpd);

	/* Use a known rate to bring up the GMU */
	clk_set_rate(gmu->core_clk, 200000000);
	clk_set_rate(gmu->hub_clk, adreno_is_a740_family(adreno_gpu) ?
		     200000000 : 150000000);
	ret = clk_bulk_prepare_enable(gmu->nr_clocks, gmu->clocks);
	if (ret) {
		pm_runtime_put(gmu->gxpd);
		pm_runtime_put(gmu->dev);
		return ret;
	}

	/* Set the bus quota to a reasonable value for boot */
	a6xx_gmu_set_initial_bw(gpu, gmu);

	/* Enable the GMU interrupt */
	gmu_write(gmu, REG_A6XX_GMU_AO_HOST_INTERRUPT_CLR, ~0);
	gmu_write(gmu, REG_A6XX_GMU_AO_HOST_INTERRUPT_MASK, ~A6XX_GMU_IRQ_MASK);
	enable_irq(gmu->gmu_irq);

	/* Check to see if we are doing a cold or warm boot */
	if (adreno_is_a7xx(adreno_gpu)) {
		status = a6xx_llc_read(a6xx_gpu, REG_A7XX_CX_MISC_TCM_RET_CNTL) == 1 ?
			GMU_WARM_BOOT : GMU_COLD_BOOT;
	} else if (gmu->legacy) {
		status = gmu_read(gmu, REG_A6XX_GMU_GENERAL_7) == 1 ?
			GMU_WARM_BOOT : GMU_COLD_BOOT;
	} else {
		/*
		 * Warm boot path does not work on newer A6xx GPUs
		 * Presumably this is because icache/dcache regions must be restored
		 */
		status = GMU_COLD_BOOT;
	}

	ret = a6xx_gmu_fw_start(gmu, status);
	if (ret)
		goto out;

	ret = a6xx_hfi_start(gmu, status);
	if (ret)
		goto out;

	/*
	 * Turn on the GMU firmware fault interrupt after we know the boot
	 * sequence is successful
	 */
	gmu_write(gmu, REG_A6XX_GMU_GMU2HOST_INTR_CLR, ~0);
	gmu_write(gmu, REG_A6XX_GMU_GMU2HOST_INTR_MASK, ~A6XX_HFI_IRQ_MASK);
	enable_irq(gmu->hfi_irq);

	/* Set the GPU to the current freq */
	a6xx_gmu_set_initial_freq(gpu, gmu);

out:
	/* On failure, shut down the GMU to leave it in a good state */
	if (ret) {
		disable_irq(gmu->gmu_irq);
		a6xx_rpmh_stop(gmu);
		pm_runtime_put(gmu->gxpd);
		pm_runtime_put(gmu->dev);
	}

	return ret;
}

bool a6xx_gmu_isidle(struct a6xx_gmu *gmu)
{
	u32 reg;

	if (!gmu->initialized)
		return true;

	reg = gmu_read(gmu, REG_A6XX_GPU_GMU_AO_GPU_CX_BUSY_STATUS);

	if (reg &  A6XX_GPU_GMU_AO_GPU_CX_BUSY_STATUS_GPUBUSYIGNAHB)
		return false;

	return true;
}

/* Gracefully try to shut down the GMU and by extension the GPU */
static void a6xx_gmu_shutdown(struct a6xx_gmu *gmu)
{
	struct a6xx_gpu *a6xx_gpu = container_of(gmu, struct a6xx_gpu, gmu);
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	u32 val;
	int ret;

	/*
	 * GMU firmware's internal power state gets messed up if we send "prepare_slumber" hfi when
	 * oob_gpu handshake wasn't done after the last wake up. So do a dummy handshake here when
	 * required
	 */
	if (adreno_gpu->base.needs_hw_init) {
		if (a6xx_gmu_set_oob(&a6xx_gpu->gmu, GMU_OOB_GPU_SET))
			goto force_off;

		a6xx_gmu_clear_oob(&a6xx_gpu->gmu, GMU_OOB_GPU_SET);
	}

	ret = a6xx_gmu_wait_for_idle(gmu);

	/* If the GMU isn't responding assume it is hung */
	if (ret)
		goto force_off;

	a6xx_bus_clear_pending_transactions(adreno_gpu, a6xx_gpu->hung);

	/* tell the GMU we want to slumber */
	ret = a6xx_gmu_notify_slumber(gmu);
	if (ret)
		goto force_off;

	ret = gmu_poll_timeout(gmu,
		REG_A6XX_GPU_GMU_AO_GPU_CX_BUSY_STATUS, val,
		!(val & A6XX_GPU_GMU_AO_GPU_CX_BUSY_STATUS_GPUBUSYIGNAHB),
		100, 10000);

	/*
	 * Let the user know we failed to slumber but don't worry too
	 * much because we are powering down anyway
	 */

	if (ret)
		DRM_DEV_ERROR(gmu->dev,
			"Unable to slumber GMU: status = 0%x/0%x\n",
			gmu_read(gmu,
				REG_A6XX_GPU_GMU_AO_GPU_CX_BUSY_STATUS),
			gmu_read(gmu,
				REG_A6XX_GPU_GMU_AO_GPU_CX_BUSY_STATUS2));

	/* Turn off HFI */
	a6xx_hfi_stop(gmu);

	/* Stop the interrupts and mask the hardware */
	a6xx_gmu_irq_disable(gmu);

	/* Tell RPMh to power off the GPU */
	a6xx_rpmh_stop(gmu);

	return;

force_off:
	a6xx_gmu_force_off(gmu);
}


int a6xx_gmu_stop(struct a6xx_gpu *a6xx_gpu)
{
	struct a6xx_gmu *gmu = &a6xx_gpu->gmu;
	struct msm_gpu *gpu = &a6xx_gpu->base.base;

	if (!pm_runtime_active(gmu->dev))
		return 0;

	/*
	 * Force the GMU off if we detected a hang, otherwise try to shut it
	 * down gracefully
	 */
	if (gmu->hung)
		a6xx_gmu_force_off(gmu);
	else
		a6xx_gmu_shutdown(gmu);

	/* Remove the bus vote */
	dev_pm_opp_set_opp(&gpu->pdev->dev, NULL);

	/*
	 * Make sure the GX domain is off before turning off the GMU (CX)
	 * domain. Usually the GMU does this but only if the shutdown sequence
	 * was successful
	 */
	if (!IS_ERR_OR_NULL(gmu->gxpd))
		pm_runtime_put_sync(gmu->gxpd);

	clk_bulk_disable_unprepare(gmu->nr_clocks, gmu->clocks);

	pm_runtime_put_sync(gmu->dev);

	return 0;
}

static void a6xx_gmu_memory_free(struct a6xx_gmu *gmu)
{
	msm_gem_kernel_put(gmu->hfi.obj, gmu->aspace);
	msm_gem_kernel_put(gmu->debug.obj, gmu->aspace);
	msm_gem_kernel_put(gmu->icache.obj, gmu->aspace);
	msm_gem_kernel_put(gmu->dcache.obj, gmu->aspace);
	msm_gem_kernel_put(gmu->dummy.obj, gmu->aspace);
	msm_gem_kernel_put(gmu->log.obj, gmu->aspace);

	gmu->aspace->mmu->funcs->detach(gmu->aspace->mmu);
	msm_gem_address_space_put(gmu->aspace);
}

static int a6xx_gmu_memory_alloc(struct a6xx_gmu *gmu, struct a6xx_gmu_bo *bo,
		size_t size, u64 iova, const char *name)
{
	struct a6xx_gpu *a6xx_gpu = container_of(gmu, struct a6xx_gpu, gmu);
	struct drm_device *dev = a6xx_gpu->base.base.dev;
	uint32_t flags = MSM_BO_WC;
	u64 range_start, range_end;
	int ret;

	size = PAGE_ALIGN(size);
	if (!iova) {
		/* no fixed address - use GMU's uncached range */
		range_start = 0x60000000 + PAGE_SIZE; /* skip dummy page */
		range_end = 0x80000000;
	} else {
		/* range for fixed address */
		range_start = iova;
		range_end = iova + size;
		/* use IOMMU_PRIV for icache/dcache */
		flags |= MSM_BO_MAP_PRIV;
	}

	bo->obj = msm_gem_new(dev, size, flags);
	if (IS_ERR(bo->obj))
		return PTR_ERR(bo->obj);

	ret = msm_gem_get_and_pin_iova_range(bo->obj, gmu->aspace, &bo->iova,
					     range_start, range_end);
	if (ret) {
		drm_gem_object_put(bo->obj);
		return ret;
	}

	bo->virt = msm_gem_get_vaddr(bo->obj);
	bo->size = size;

	msm_gem_object_set_name(bo->obj, "%s", name);

	return 0;
}

static int a6xx_gmu_memory_probe(struct a6xx_gmu *gmu)
{
	struct msm_mmu *mmu;

	mmu = msm_iommu_new(gmu->dev, 0);
	if (!mmu)
		return -ENODEV;
	if (IS_ERR(mmu))
		return PTR_ERR(mmu);

	gmu->aspace = msm_gem_address_space_create(mmu, "gmu", 0x0, 0x80000000);
	if (IS_ERR(gmu->aspace))
		return PTR_ERR(gmu->aspace);

	return 0;
}

/**
 * struct bcm_db - Auxiliary data pertaining to each Bus Clock Manager (BCM)
 * @unit: divisor used to convert bytes/sec bw value to an RPMh msg
 * @width: multiplier used to convert bytes/sec bw value to an RPMh msg
 * @vcd: virtual clock domain that this bcm belongs to
 * @reserved: reserved field
 */
struct bcm_db {
	__le32 unit;
	__le16 width;
	u8 vcd;
	u8 reserved;
};

static int a6xx_gmu_rpmh_bw_votes_init(struct adreno_gpu *adreno_gpu,
				       const struct a6xx_info *info,
				       struct a6xx_gmu *gmu)
{
	const struct bcm_db *bcm_data[GMU_MAX_BCMS] = { 0 };
	unsigned int bcm_index, bw_index, bcm_count = 0;

	/* Retrieve BCM data from cmd-db */
	for (bcm_index = 0; bcm_index < GMU_MAX_BCMS; bcm_index++) {
		const struct a6xx_bcm *bcm = &info->bcms[bcm_index];
		size_t count;

		/* Stop at NULL terminated bcm entry */
		if (!bcm->name)
			break;

		bcm_data[bcm_index] = cmd_db_read_aux_data(bcm->name, &count);
		if (IS_ERR(bcm_data[bcm_index]))
			return PTR_ERR(bcm_data[bcm_index]);

		if (!count) {
			dev_err(gmu->dev, "invalid BCM '%s' aux data size\n",
				bcm->name);
			return -EINVAL;
		}

		bcm_count++;
	}

	/* Generate BCM votes values for each bandwidth & BCM */
	for (bw_index = 0; bw_index < gmu->nr_gpu_bws; bw_index++) {
		u32 *data = gmu->gpu_ib_votes[bw_index];
		u32 bw = gmu->gpu_bw_table[bw_index];

		/* Calculations loosely copied from bcm_aggregate() & tcs_cmd_gen() */
		for (bcm_index = 0; bcm_index < bcm_count; bcm_index++) {
			const struct a6xx_bcm *bcm = &info->bcms[bcm_index];
			bool commit = false;
			u64 peak;
			u32 vote;

			if (bcm_index == bcm_count - 1 ||
			    (bcm_data[bcm_index + 1] &&
			     bcm_data[bcm_index]->vcd != bcm_data[bcm_index + 1]->vcd))
				commit = true;

			if (!bw) {
				data[bcm_index] = BCM_TCS_CMD(commit, false, 0, 0);
				continue;
			}

			if (bcm->fixed) {
				u32 perfmode = 0;

				/* GMU on A6xx votes perfmode on all valid bandwidth */
				if (!adreno_is_a7xx(adreno_gpu) ||
				    (bcm->perfmode_bw && bw >= bcm->perfmode_bw))
					perfmode = bcm->perfmode;

				data[bcm_index] = BCM_TCS_CMD(commit, true, 0, perfmode);
				continue;
			}

			/* Multiply the bandwidth by the width of the connection */
			peak = (u64)bw * le16_to_cpu(bcm_data[bcm_index]->width);
			do_div(peak, bcm->buswidth);

			/* Input bandwidth value is in KBps, scale the value to BCM unit */
			peak *= 1000;
			do_div(peak, le32_to_cpu(bcm_data[bcm_index]->unit));

			vote = clamp(peak, 1, BCM_TCS_CMD_VOTE_MASK);

			/* GMUs on A7xx votes on both x & y */
			if (adreno_is_a7xx(adreno_gpu))
				data[bcm_index] = BCM_TCS_CMD(commit, true, vote, vote);
			else
				data[bcm_index] = BCM_TCS_CMD(commit, true, 0, vote);
		}
	}

	return 0;
}

/* Return the 'arc-level' for the given frequency */
static unsigned int a6xx_gmu_get_arc_level(struct device *dev,
					   unsigned long freq)
{
	struct dev_pm_opp *opp;
	unsigned int val;

	if (!freq)
		return 0;

	opp = dev_pm_opp_find_freq_exact(dev, freq, true);
	if (IS_ERR(opp))
		return 0;

	val = dev_pm_opp_get_level(opp);

	dev_pm_opp_put(opp);

	return val;
}

static int a6xx_gmu_rpmh_arc_votes_init(struct device *dev, u32 *votes,
		unsigned long *freqs, int freqs_count, const char *id)
{
	int i, j;
	const u16 *pri, *sec;
	size_t pri_count, sec_count;

	pri = cmd_db_read_aux_data(id, &pri_count);
	if (IS_ERR(pri))
		return PTR_ERR(pri);
	/*
	 * The data comes back as an array of unsigned shorts so adjust the
	 * count accordingly
	 */
	pri_count >>= 1;
	if (!pri_count)
		return -EINVAL;

	/*
	 * Some targets have a separate gfx mxc rail. So try to read that first and then fall back
	 * to regular mx rail if it is missing
	 */
	sec = cmd_db_read_aux_data("gmxc.lvl", &sec_count);
	if (IS_ERR(sec) && sec != ERR_PTR(-EPROBE_DEFER))
		sec = cmd_db_read_aux_data("mx.lvl", &sec_count);
	if (IS_ERR(sec))
		return PTR_ERR(sec);

	sec_count >>= 1;
	if (!sec_count)
		return -EINVAL;

	/* Construct a vote for each frequency */
	for (i = 0; i < freqs_count; i++) {
		u8 pindex = 0, sindex = 0;
		unsigned int level = a6xx_gmu_get_arc_level(dev, freqs[i]);

		/* Get the primary index that matches the arc level */
		for (j = 0; j < pri_count; j++) {
			if (pri[j] >= level) {
				pindex = j;
				break;
			}
		}

		if (j == pri_count) {
			DRM_DEV_ERROR(dev,
				      "Level %u not found in the RPMh list\n",
				      level);
			DRM_DEV_ERROR(dev, "Available levels:\n");
			for (j = 0; j < pri_count; j++)
				DRM_DEV_ERROR(dev, "  %u\n", pri[j]);

			return -EINVAL;
		}

		/*
		 * Look for a level in in the secondary list that matches. If
		 * nothing fits, use the maximum non zero vote
		 */

		for (j = 0; j < sec_count; j++) {
			if (sec[j] >= level) {
				sindex = j;
				break;
			} else if (sec[j]) {
				sindex = j;
			}
		}

		/* Construct the vote */
		votes[i] = ((pri[pindex] & 0xffff) << 16) |
			(sindex << 8) | pindex;
	}

	return 0;
}

/*
 * The GMU votes with the RPMh for itself and on behalf of the GPU but we need
 * to construct the list of votes on the CPU and send it over. Query the RPMh
 * voltage levels and build the votes
 * The GMU can also vote for DDR interconnects, use the OPP bandwidth entries
 * and BCM parameters to build the votes.
 */

static int a6xx_gmu_rpmh_votes_init(struct a6xx_gmu *gmu)
{
	struct a6xx_gpu *a6xx_gpu = container_of(gmu, struct a6xx_gpu, gmu);
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	const struct a6xx_info *info = adreno_gpu->info->a6xx;
	struct msm_gpu *gpu = &adreno_gpu->base;
	int ret;

	/* Build the GX votes */
	ret = a6xx_gmu_rpmh_arc_votes_init(&gpu->pdev->dev, gmu->gx_arc_votes,
		gmu->gpu_freqs, gmu->nr_gpu_freqs, "gfx.lvl");

	/* Build the CX votes */
	ret |= a6xx_gmu_rpmh_arc_votes_init(gmu->dev, gmu->cx_arc_votes,
		gmu->gmu_freqs, gmu->nr_gmu_freqs, "cx.lvl");

	/* Build the interconnect votes */
	if (info->bcms && gmu->nr_gpu_bws > 1)
		ret |= a6xx_gmu_rpmh_bw_votes_init(adreno_gpu, info, gmu);

	return ret;
}

static int a6xx_gmu_build_freq_table(struct device *dev, unsigned long *freqs,
		u32 size)
{
	int count = dev_pm_opp_get_opp_count(dev);
	struct dev_pm_opp *opp;
	int i, index = 0;
	unsigned long freq = 1;

	/*
	 * The OPP table doesn't contain the "off" frequency level so we need to
	 * add 1 to the table size to account for it
	 */

	if (WARN(count + 1 > size,
		"The GMU frequency table is being truncated\n"))
		count = size - 1;

	/* Set the "off" frequency */
	freqs[index++] = 0;

	for (i = 0; i < count; i++) {
		opp = dev_pm_opp_find_freq_ceil(dev, &freq);
		if (IS_ERR(opp))
			break;

		dev_pm_opp_put(opp);
		freqs[index++] = freq++;
	}

	return index;
}

static int a6xx_gmu_build_bw_table(struct device *dev, unsigned long *bandwidths,
		u32 size)
{
	int count = dev_pm_opp_get_opp_count(dev);
	struct dev_pm_opp *opp;
	int i, index = 0;
	unsigned int bandwidth = 1;

	/*
	 * The OPP table doesn't contain the "off" bandwidth level so we need to
	 * add 1 to the table size to account for it
	 */

	if (WARN(count + 1 > size,
		"The GMU bandwidth table is being truncated\n"))
		count = size - 1;

	/* Set the "off" bandwidth */
	bandwidths[index++] = 0;

	for (i = 0; i < count; i++) {
		opp = dev_pm_opp_find_bw_ceil(dev, &bandwidth, 0);
		if (IS_ERR(opp))
			break;

		dev_pm_opp_put(opp);
		bandwidths[index++] = bandwidth++;
	}

	return index;
}

static int a6xx_gmu_pwrlevels_probe(struct a6xx_gmu *gmu)
{
	struct a6xx_gpu *a6xx_gpu = container_of(gmu, struct a6xx_gpu, gmu);
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	const struct a6xx_info *info = adreno_gpu->info->a6xx;
	struct msm_gpu *gpu = &adreno_gpu->base;

	int ret = 0;

	/*
	 * The GMU handles its own frequency switching so build a list of
	 * available frequencies to send during initialization
	 */
	ret = devm_pm_opp_of_add_table(gmu->dev);
	if (ret) {
		DRM_DEV_ERROR(gmu->dev, "Unable to set the OPP table for the GMU\n");
		return ret;
	}

	gmu->nr_gmu_freqs = a6xx_gmu_build_freq_table(gmu->dev,
		gmu->gmu_freqs, ARRAY_SIZE(gmu->gmu_freqs));

	/*
	 * The GMU also handles GPU frequency switching so build a list
	 * from the GPU OPP table
	 */
	gmu->nr_gpu_freqs = a6xx_gmu_build_freq_table(&gpu->pdev->dev,
		gmu->gpu_freqs, ARRAY_SIZE(gmu->gpu_freqs));

	gmu->current_perf_index = gmu->nr_gpu_freqs - 1;

	/*
	 * The GMU also handles GPU Interconnect Votes so build a list
	 * of DDR bandwidths from the GPU OPP table
	 */
	if (info->bcms)
		gmu->nr_gpu_bws = a6xx_gmu_build_bw_table(&gpu->pdev->dev,
			gmu->gpu_bw_table, ARRAY_SIZE(gmu->gpu_bw_table));

	/* Build the list of RPMh votes that we'll send to the GMU */
	return a6xx_gmu_rpmh_votes_init(gmu);
}

static int a6xx_gmu_clocks_probe(struct a6xx_gmu *gmu)
{
	int ret = devm_clk_bulk_get_all(gmu->dev, &gmu->clocks);

	if (ret < 1)
		return ret;

	gmu->nr_clocks = ret;

	gmu->core_clk = msm_clk_bulk_get_clock(gmu->clocks,
		gmu->nr_clocks, "gmu");

	gmu->hub_clk = msm_clk_bulk_get_clock(gmu->clocks,
		gmu->nr_clocks, "hub");

	return 0;
}

static void __iomem *a6xx_gmu_get_mmio(struct platform_device *pdev,
		const char *name)
{
	void __iomem *ret;
	struct resource *res = platform_get_resource_byname(pdev,
			IORESOURCE_MEM, name);

	if (!res) {
		DRM_DEV_ERROR(&pdev->dev, "Unable to find the %s registers\n", name);
		return ERR_PTR(-EINVAL);
	}

	ret = ioremap(res->start, resource_size(res));
	if (!ret) {
		DRM_DEV_ERROR(&pdev->dev, "Unable to map the %s registers\n", name);
		return ERR_PTR(-EINVAL);
	}

	return ret;
}

static int a6xx_gmu_get_irq(struct a6xx_gmu *gmu, struct platform_device *pdev,
		const char *name, irq_handler_t handler)
{
	int irq, ret;

	irq = platform_get_irq_byname(pdev, name);

	ret = request_irq(irq, handler, IRQF_TRIGGER_HIGH | IRQF_NO_AUTOEN, name, gmu);
	if (ret) {
		DRM_DEV_ERROR(&pdev->dev, "Unable to get interrupt %s %d\n",
			      name, ret);
		return ret;
	}

	return irq;
}

void a6xx_gmu_remove(struct a6xx_gpu *a6xx_gpu)
{
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	struct a6xx_gmu *gmu = &a6xx_gpu->gmu;
	struct platform_device *pdev = to_platform_device(gmu->dev);

	mutex_lock(&gmu->lock);
	if (!gmu->initialized) {
		mutex_unlock(&gmu->lock);
		return;
	}

	gmu->initialized = false;

	mutex_unlock(&gmu->lock);

	pm_runtime_force_suspend(gmu->dev);

	/*
	 * Since cxpd is a virt device, the devlink with gmu-dev will be removed
	 * automatically when we do detach
	 */
	dev_pm_domain_detach(gmu->cxpd, false);

	if (!IS_ERR_OR_NULL(gmu->gxpd)) {
		pm_runtime_disable(gmu->gxpd);
		dev_pm_domain_detach(gmu->gxpd, false);
	}

	if (!IS_ERR_OR_NULL(gmu->qmp))
		qmp_put(gmu->qmp);

	iounmap(gmu->mmio);
	if (platform_get_resource_byname(pdev, IORESOURCE_MEM, "rscc"))
		iounmap(gmu->rscc);
	gmu->mmio = NULL;
	gmu->rscc = NULL;

	if (!adreno_has_gmu_wrapper(adreno_gpu)) {
		a6xx_gmu_memory_free(gmu);

		free_irq(gmu->gmu_irq, gmu);
		free_irq(gmu->hfi_irq, gmu);
	}

	/* Drop reference taken in of_find_device_by_node */
	put_device(gmu->dev);
}

static int cxpd_notifier_cb(struct notifier_block *nb,
			unsigned long action, void *data)
{
	struct a6xx_gmu *gmu = container_of(nb, struct a6xx_gmu, pd_nb);

	if (action == GENPD_NOTIFY_OFF)
		complete_all(&gmu->pd_gate);

	return 0;
}

int a6xx_gmu_wrapper_init(struct a6xx_gpu *a6xx_gpu, struct device_node *node)
{
	struct platform_device *pdev = of_find_device_by_node(node);
	struct a6xx_gmu *gmu = &a6xx_gpu->gmu;
	int ret;

	if (!pdev)
		return -ENODEV;

	gmu->dev = &pdev->dev;

	ret = of_dma_configure(gmu->dev, node, true);
	if (ret)
		return ret;

	pm_runtime_enable(gmu->dev);

	/* Mark legacy for manual SPTPRAC control */
	gmu->legacy = true;

	/* Map the GMU registers */
	gmu->mmio = a6xx_gmu_get_mmio(pdev, "gmu");
	if (IS_ERR(gmu->mmio)) {
		ret = PTR_ERR(gmu->mmio);
		goto err_mmio;
	}

	gmu->cxpd = dev_pm_domain_attach_by_name(gmu->dev, "cx");
	if (IS_ERR(gmu->cxpd)) {
		ret = PTR_ERR(gmu->cxpd);
		goto err_mmio;
	}

	if (!device_link_add(gmu->dev, gmu->cxpd, DL_FLAG_PM_RUNTIME)) {
		ret = -ENODEV;
		goto detach_cxpd;
	}

	init_completion(&gmu->pd_gate);
	complete_all(&gmu->pd_gate);
	gmu->pd_nb.notifier_call = cxpd_notifier_cb;

	/* Get a link to the GX power domain to reset the GPU */
	gmu->gxpd = dev_pm_domain_attach_by_name(gmu->dev, "gx");
	if (IS_ERR(gmu->gxpd)) {
		ret = PTR_ERR(gmu->gxpd);
		goto err_mmio;
	}

	gmu->initialized = true;

	return 0;

detach_cxpd:
	dev_pm_domain_detach(gmu->cxpd, false);

err_mmio:
	iounmap(gmu->mmio);

	/* Drop reference taken in of_find_device_by_node */
	put_device(gmu->dev);

	return ret;
}

int a6xx_gmu_init(struct a6xx_gpu *a6xx_gpu, struct device_node *node)
{
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	struct a6xx_gmu *gmu = &a6xx_gpu->gmu;
	struct platform_device *pdev = of_find_device_by_node(node);
	struct device_link *link;
	int ret;

	if (!pdev)
		return -ENODEV;

	gmu->dev = &pdev->dev;

	ret = of_dma_configure(gmu->dev, node, true);
	if (ret)
		return ret;

	/* Fow now, don't do anything fancy until we get our feet under us */
	gmu->idle_level = GMU_IDLE_STATE_ACTIVE;

	pm_runtime_enable(gmu->dev);

	/* Get the list of clocks */
	ret = a6xx_gmu_clocks_probe(gmu);
	if (ret)
		goto err_put_device;

	ret = a6xx_gmu_memory_probe(gmu);
	if (ret)
		goto err_put_device;


	/* A660 now requires handling "prealloc requests" in GMU firmware
	 * For now just hardcode allocations based on the known firmware.
	 * note: there is no indication that these correspond to "dummy" or
	 * "debug" regions, but this "guess" allows reusing these BOs which
	 * are otherwise unused by a660.
	 */
	gmu->dummy.size = SZ_4K;
	if (adreno_is_a660_family(adreno_gpu) ||
	    adreno_is_a7xx(adreno_gpu)) {
		ret = a6xx_gmu_memory_alloc(gmu, &gmu->debug, SZ_4K * 7,
					    0x60400000, "debug");
		if (ret)
			goto err_memory;

		gmu->dummy.size = SZ_8K;
	}

	/* Allocate memory for the GMU dummy page */
	ret = a6xx_gmu_memory_alloc(gmu, &gmu->dummy, gmu->dummy.size,
				    0x60000000, "dummy");
	if (ret)
		goto err_memory;

	/* Note that a650 family also includes a660 family: */
	if (adreno_is_a650_family(adreno_gpu) ||
	    adreno_is_a7xx(adreno_gpu)) {
		ret = a6xx_gmu_memory_alloc(gmu, &gmu->icache,
			SZ_16M - SZ_16K, 0x04000, "icache");
		if (ret)
			goto err_memory;
	/*
	 * NOTE: when porting legacy ("pre-650-family") GPUs you may be tempted to add a condition
	 * to allocate icache/dcache here, as per downstream code flow, but it may not actually be
	 * necessary. If you omit this step and you don't get random pagefaults, you are likely
	 * good to go without this!
	 */
	} else if (adreno_is_a640_family(adreno_gpu)) {
		ret = a6xx_gmu_memory_alloc(gmu, &gmu->icache,
			SZ_256K - SZ_16K, 0x04000, "icache");
		if (ret)
			goto err_memory;

		ret = a6xx_gmu_memory_alloc(gmu, &gmu->dcache,
			SZ_256K - SZ_16K, 0x44000, "dcache");
		if (ret)
			goto err_memory;
	} else if (adreno_is_a630_family(adreno_gpu)) {
		/* HFI v1, has sptprac */
		gmu->legacy = true;

		/* Allocate memory for the GMU debug region */
		ret = a6xx_gmu_memory_alloc(gmu, &gmu->debug, SZ_16K, 0, "debug");
		if (ret)
			goto err_memory;
	}

	/* Allocate memory for the GMU log region */
	ret = a6xx_gmu_memory_alloc(gmu, &gmu->log, SZ_16K, 0, "log");
	if (ret)
		goto err_memory;

	/* Allocate memory for for the HFI queues */
	ret = a6xx_gmu_memory_alloc(gmu, &gmu->hfi, SZ_16K, 0, "hfi");
	if (ret)
		goto err_memory;

	/* Map the GMU registers */
	gmu->mmio = a6xx_gmu_get_mmio(pdev, "gmu");
	if (IS_ERR(gmu->mmio)) {
		ret = PTR_ERR(gmu->mmio);
		goto err_memory;
	}

	if (adreno_is_a650_family(adreno_gpu) ||
	    adreno_is_a7xx(adreno_gpu)) {
		gmu->rscc = a6xx_gmu_get_mmio(pdev, "rscc");
		if (IS_ERR(gmu->rscc)) {
			ret = -ENODEV;
			goto err_mmio;
		}
	} else {
		gmu->rscc = gmu->mmio + 0x23000;
	}

	/* Get the HFI and GMU interrupts */
	gmu->hfi_irq = a6xx_gmu_get_irq(gmu, pdev, "hfi", a6xx_hfi_irq);
	gmu->gmu_irq = a6xx_gmu_get_irq(gmu, pdev, "gmu", a6xx_gmu_irq);

	if (gmu->hfi_irq < 0 || gmu->gmu_irq < 0) {
		ret = -ENODEV;
		goto err_mmio;
	}

	gmu->cxpd = dev_pm_domain_attach_by_name(gmu->dev, "cx");
	if (IS_ERR(gmu->cxpd)) {
		ret = PTR_ERR(gmu->cxpd);
		goto err_mmio;
	}

	link = device_link_add(gmu->dev, gmu->cxpd, DL_FLAG_PM_RUNTIME);
	if (!link) {
		ret = -ENODEV;
		goto detach_cxpd;
	}

	gmu->qmp = qmp_get(gmu->dev);
	if (IS_ERR(gmu->qmp) && adreno_is_a7xx(adreno_gpu)) {
		ret = PTR_ERR(gmu->qmp);
		goto remove_device_link;
	}

	init_completion(&gmu->pd_gate);
	complete_all(&gmu->pd_gate);
	gmu->pd_nb.notifier_call = cxpd_notifier_cb;

	/*
	 * Get a link to the GX power domain to reset the GPU in case of GMU
	 * crash
	 */
	gmu->gxpd = dev_pm_domain_attach_by_name(gmu->dev, "gx");

	/* Get the power levels for the GMU and GPU */
	a6xx_gmu_pwrlevels_probe(gmu);

	/* Set up the HFI queues */
	a6xx_hfi_init(gmu);

	/* Initialize RPMh */
	a6xx_gmu_rpmh_init(gmu);

	gmu->initialized = true;

	return 0;

remove_device_link:
	device_link_del(link);

detach_cxpd:
	dev_pm_domain_detach(gmu->cxpd, false);

err_mmio:
	iounmap(gmu->mmio);
	if (platform_get_resource_byname(pdev, IORESOURCE_MEM, "rscc"))
		iounmap(gmu->rscc);
	free_irq(gmu->gmu_irq, gmu);
	free_irq(gmu->hfi_irq, gmu);

err_memory:
	a6xx_gmu_memory_free(gmu);
err_put_device:
	/* Drop reference taken in of_find_device_by_node */
	put_device(gmu->dev);

	return ret;
}
