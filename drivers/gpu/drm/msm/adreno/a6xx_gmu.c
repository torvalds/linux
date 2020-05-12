// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2017-2019 The Linux Foundation. All rights reserved. */

#include <linux/clk.h>
#include <linux/interconnect.h>
#include <linux/pm_domain.h>
#include <linux/pm_opp.h>
#include <soc/qcom/cmd-db.h>

#include "a6xx_gpu.h"
#include "a6xx_gmu.xml.h"

static void a6xx_gmu_fault(struct a6xx_gmu *gmu)
{
	struct a6xx_gpu *a6xx_gpu = container_of(gmu, struct a6xx_gpu, gmu);
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	struct msm_gpu *gpu = &adreno_gpu->base;
	struct drm_device *dev = gpu->dev;
	struct msm_drm_private *priv = dev->dev_private;

	/* FIXME: add a banner here */
	gmu->hung = true;

	/* Turn off the hangcheck timer while we are resetting */
	del_timer(&gpu->hangcheck_timer);

	/* Queue the GPU handler because we need to treat this as a recovery */
	queue_work(priv->wq, &gpu->recover_work);
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

static void __a6xx_gmu_set_freq(struct a6xx_gmu *gmu, int index)
{
	struct a6xx_gpu *a6xx_gpu = container_of(gmu, struct a6xx_gpu, gmu);
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	struct msm_gpu *gpu = &adreno_gpu->base;
	int ret;

	gmu_write(gmu, REG_A6XX_GMU_DCVS_ACK_OPTION, 0);

	gmu_write(gmu, REG_A6XX_GMU_DCVS_PERF_SETTING,
		((3 & 0xf) << 28) | index);

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

	gmu->freq = gmu->gpu_freqs[index];

	/*
	 * Eventually we will want to scale the path vote with the frequency but
	 * for now leave it at max so that the performance is nominal.
	 */
	icc_set_bw(gpu->icc_path, 0, MBps_to_icc(7216));
}

void a6xx_gmu_set_freq(struct msm_gpu *gpu, unsigned long freq)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a6xx_gpu *a6xx_gpu = to_a6xx_gpu(adreno_gpu);
	struct a6xx_gmu *gmu = &a6xx_gpu->gmu;
	u32 perf_index = 0;

	if (freq == gmu->freq)
		return;

	for (perf_index = 0; perf_index < gmu->nr_gpu_freqs - 1; perf_index++)
		if (freq == gmu->gpu_freqs[perf_index])
			break;

	gmu->current_perf_index = perf_index;

	__a6xx_gmu_set_freq(gmu, perf_index);
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
	int ret;
	u32 val;

	gmu_write(gmu, REG_A6XX_GMU_CM3_SYSRESET, 1);
	gmu_write(gmu, REG_A6XX_GMU_CM3_SYSRESET, 0);

	ret = gmu_poll_timeout(gmu, REG_A6XX_GMU_CM3_FW_INIT_RESULT, val,
		val == 0xbabeface, 100, 10000);

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

/* Trigger a OOB (out of band) request to the GMU */
int a6xx_gmu_set_oob(struct a6xx_gmu *gmu, enum a6xx_gmu_oob_state state)
{
	int ret;
	u32 val;
	int request, ack;
	const char *name;

	switch (state) {
	case GMU_OOB_GPU_SET:
		request = GMU_OOB_GPU_SET_REQUEST;
		ack = GMU_OOB_GPU_SET_ACK;
		name = "GPU_SET";
		break;
	case GMU_OOB_BOOT_SLUMBER:
		request = GMU_OOB_BOOT_SLUMBER_REQUEST;
		ack = GMU_OOB_BOOT_SLUMBER_ACK;
		name = "BOOT_SLUMBER";
		break;
	case GMU_OOB_DCVS_SET:
		request = GMU_OOB_DCVS_REQUEST;
		ack = GMU_OOB_DCVS_ACK;
		name = "GPU_DCVS";
		break;
	default:
		return -EINVAL;
	}

	/* Trigger the equested OOB operation */
	gmu_write(gmu, REG_A6XX_GMU_HOST2GMU_INTR_SET, 1 << request);

	/* Wait for the acknowledge interrupt */
	ret = gmu_poll_timeout(gmu, REG_A6XX_GMU_GMU2HOST_INTR_INFO, val,
		val & (1 << ack), 100, 10000);

	if (ret)
		DRM_DEV_ERROR(gmu->dev,
			"Timeout waiting for GMU OOB set %s: 0x%x\n",
				name,
				gmu_read(gmu, REG_A6XX_GMU_GMU2HOST_INTR_INFO));

	/* Clear the acknowledge interrupt */
	gmu_write(gmu, REG_A6XX_GMU_GMU2HOST_INTR_CLR, 1 << ack);

	return ret;
}

/* Clear a pending OOB state in the GMU */
void a6xx_gmu_clear_oob(struct a6xx_gmu *gmu, enum a6xx_gmu_oob_state state)
{
	switch (state) {
	case GMU_OOB_GPU_SET:
		gmu_write(gmu, REG_A6XX_GMU_HOST2GMU_INTR_SET,
			1 << GMU_OOB_GPU_SET_CLEAR);
		break;
	case GMU_OOB_BOOT_SLUMBER:
		gmu_write(gmu, REG_A6XX_GMU_HOST2GMU_INTR_SET,
			1 << GMU_OOB_BOOT_SLUMBER_CLEAR);
		break;
	case GMU_OOB_DCVS_SET:
		gmu_write(gmu, REG_A6XX_GMU_HOST2GMU_INTR_SET,
			1 << GMU_OOB_DCVS_CLEAR);
		break;
	}
}

/* Enable CPU control of SPTP power power collapse */
static int a6xx_sptprac_enable(struct a6xx_gmu *gmu)
{
	int ret;
	u32 val;

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
static void a6xx_sptprac_disable(struct a6xx_gmu *gmu)
{
	u32 val;
	int ret;

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

/* Let the GMU know that we are about to go into slumber */
static int a6xx_gmu_notify_slumber(struct a6xx_gmu *gmu)
{
	int ret;

	/* Disable the power counter so the GMU isn't busy */
	gmu_write(gmu, REG_A6XX_GMU_CX_GMU_POWER_COUNTER_ENABLE, 0);

	/* Disable SPTP_PC if the CPU is responsible for it */
	if (gmu->idle_level < GMU_IDLE_STATE_SPTP)
		a6xx_sptprac_disable(gmu);

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

	/* Put fence into allow mode */
	gmu_write(gmu, REG_A6XX_GMU_AO_AHB_FENCE_CTRL, 0);
	return ret;
}

static int a6xx_rpmh_start(struct a6xx_gmu *gmu)
{
	int ret;
	u32 val;

	gmu_write(gmu, REG_A6XX_GMU_RSCC_CONTROL_REQ, 1 << 1);
	/* Wait for the register to finish posting */
	wmb();

	ret = gmu_poll_timeout(gmu, REG_A6XX_GMU_RSCC_CONTROL_ACK, val,
		val & (1 << 1), 100, 10000);
	if (ret) {
		DRM_DEV_ERROR(gmu->dev, "Unable to power on the GPU RSC\n");
		return ret;
	}

	ret = gmu_poll_timeout(gmu, REG_A6XX_RSCC_SEQ_BUSY_DRV0, val,
		!val, 100, 10000);

	if (ret) {
		DRM_DEV_ERROR(gmu->dev, "GPU RSC sequence stuck while waking up the GPU\n");
		return ret;
	}

	gmu_write(gmu, REG_A6XX_GMU_RSCC_CONTROL_REQ, 0);

	/* Set up CX GMU counter 0 to count busy ticks */
	gmu_write(gmu, REG_A6XX_GPU_GMU_AO_GPU_CX_BUSY_MASK, 0xff000000);
	gmu_rmw(gmu, REG_A6XX_GMU_CX_GMU_POWER_COUNTER_SELECT_0, 0xff, 0x20);

	/* Enable the power counter */
	gmu_write(gmu, REG_A6XX_GMU_CX_GMU_POWER_COUNTER_ENABLE, 1);
	return 0;
}

static void a6xx_rpmh_stop(struct a6xx_gmu *gmu)
{
	int ret;
	u32 val;

	gmu_write(gmu, REG_A6XX_GMU_RSCC_CONTROL_REQ, 1);

	ret = gmu_poll_timeout(gmu, REG_A6XX_GPU_RSCC_RSC_STATUS0_DRV0,
		val, val & (1 << 16), 100, 10000);
	if (ret)
		DRM_DEV_ERROR(gmu->dev, "Unable to power off the GPU RSC\n");

	gmu_write(gmu, REG_A6XX_GMU_RSCC_CONTROL_REQ, 0);
}

static inline void pdc_write(void __iomem *ptr, u32 offset, u32 value)
{
	return msm_writel(value, ptr + (offset << 2));
}

static void __iomem *a6xx_gmu_get_mmio(struct platform_device *pdev,
		const char *name);

static void a6xx_gmu_rpmh_init(struct a6xx_gmu *gmu)
{
	struct a6xx_gpu *a6xx_gpu = container_of(gmu, struct a6xx_gpu, gmu);
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	struct platform_device *pdev = to_platform_device(gmu->dev);
	void __iomem *pdcptr = a6xx_gmu_get_mmio(pdev, "gmu_pdc");
	void __iomem *seqptr = a6xx_gmu_get_mmio(pdev, "gmu_pdc_seq");

	if (!pdcptr || !seqptr)
		goto err;

	/* Disable SDE clock gating */
	gmu_write(gmu, REG_A6XX_GPU_RSCC_RSC_STATUS0_DRV0, BIT(24));

	/* Setup RSC PDC handshake for sleep and wakeup */
	gmu_write(gmu, REG_A6XX_RSCC_PDC_SLAVE_ID_DRV0, 1);
	gmu_write(gmu, REG_A6XX_RSCC_HIDDEN_TCS_CMD0_DATA, 0);
	gmu_write(gmu, REG_A6XX_RSCC_HIDDEN_TCS_CMD0_ADDR, 0);
	gmu_write(gmu, REG_A6XX_RSCC_HIDDEN_TCS_CMD0_DATA + 2, 0);
	gmu_write(gmu, REG_A6XX_RSCC_HIDDEN_TCS_CMD0_ADDR + 2, 0);
	gmu_write(gmu, REG_A6XX_RSCC_HIDDEN_TCS_CMD0_DATA + 4, 0x80000000);
	gmu_write(gmu, REG_A6XX_RSCC_HIDDEN_TCS_CMD0_ADDR + 4, 0);
	gmu_write(gmu, REG_A6XX_RSCC_OVERRIDE_START_ADDR, 0);
	gmu_write(gmu, REG_A6XX_RSCC_PDC_SEQ_START_ADDR, 0x4520);
	gmu_write(gmu, REG_A6XX_RSCC_PDC_MATCH_VALUE_LO, 0x4510);
	gmu_write(gmu, REG_A6XX_RSCC_PDC_MATCH_VALUE_HI, 0x4514);

	/* Load RSC sequencer uCode for sleep and wakeup */
	gmu_write(gmu, REG_A6XX_RSCC_SEQ_MEM_0_DRV0, 0xa7a506a0);
	gmu_write(gmu, REG_A6XX_RSCC_SEQ_MEM_0_DRV0 + 1, 0xa1e6a6e7);
	gmu_write(gmu, REG_A6XX_RSCC_SEQ_MEM_0_DRV0 + 2, 0xa2e081e1);
	gmu_write(gmu, REG_A6XX_RSCC_SEQ_MEM_0_DRV0 + 3, 0xe9a982e2);
	gmu_write(gmu, REG_A6XX_RSCC_SEQ_MEM_0_DRV0 + 4, 0x0020e8a8);

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
	if (adreno_is_a618(adreno_gpu))
		pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS1_CMD0_ADDR + 8, 0x30090);
	else
		pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS1_CMD0_ADDR + 8, 0x30080);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS1_CMD0_DATA + 8, 0x0);

	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD_ENABLE_BANK, 7);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD_WAIT_FOR_CMPL_BANK, 0);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CONTROL, 0);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD0_MSGID, 0x10108);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD0_ADDR, 0x30010);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD0_DATA, 2);

	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD0_MSGID + 4, 0x10108);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD0_ADDR + 4, 0x30000);
	if (adreno_is_a618(adreno_gpu))
		pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD0_DATA + 4, 0x2);
	else
		pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD0_DATA + 4, 0x3);


	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD0_MSGID + 8, 0x10108);
	if (adreno_is_a618(adreno_gpu))
		pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD0_ADDR + 8, 0x30090);
	else
		pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD0_ADDR + 8, 0x30080);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_TCS3_CMD0_DATA + 8, 0x3);

	/* Setup GPU PDC */
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_SEQ_START_ADDR, 0);
	pdc_write(pdcptr, REG_A6XX_PDC_GPU_ENABLE_PDC, 0x80000001);

	/* ensure no writes happen before the uCode is fully written */
	wmb();

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
	/* Disable GMU WB/RB buffer */
	gmu_write(gmu, REG_A6XX_GMU_SYS_BUS_CONFIG, 0x1);

	gmu_write(gmu, REG_A6XX_GMU_PWR_COL_INTER_FRAME_CTRL, 0x9c40400);

	switch (gmu->idle_level) {
	case GMU_IDLE_STATE_IFPC:
		gmu_write(gmu, REG_A6XX_GMU_PWR_COL_INTER_FRAME_HYST,
			GMU_PWR_COL_HYST);
		gmu_rmw(gmu, REG_A6XX_GMU_PWR_COL_INTER_FRAME_CTRL, 0,
			A6XX_GMU_PWR_COL_INTER_FRAME_CTRL_IFPC_ENABLE |
			A6XX_GMU_PWR_COL_INTER_FRAME_CTRL_HM_POWER_COLLAPSE_ENABLE);
		/* Fall through */
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

static int a6xx_gmu_fw_start(struct a6xx_gmu *gmu, unsigned int state)
{
	static bool rpmh_init;
	struct a6xx_gpu *a6xx_gpu = container_of(gmu, struct a6xx_gpu, gmu);
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	int i, ret;
	u32 chipid;
	u32 *image;

	if (state == GMU_WARM_BOOT) {
		ret = a6xx_rpmh_start(gmu);
		if (ret)
			return ret;
	} else {
		if (WARN(!adreno_gpu->fw[ADRENO_FW_GMU],
			"GMU firmware is not loaded\n"))
			return -ENOENT;

		/* Sanity check the size of the firmware that was loaded */
		if (adreno_gpu->fw[ADRENO_FW_GMU]->size > 0x8000) {
			DRM_DEV_ERROR(gmu->dev,
				"GMU firmware is bigger than the available region\n");
			return -EINVAL;
		}

		/* Turn on register retention */
		gmu_write(gmu, REG_A6XX_GMU_GENERAL_7, 1);

		/* We only need to load the RPMh microcode once */
		if (!rpmh_init) {
			a6xx_gmu_rpmh_init(gmu);
			rpmh_init = true;
		} else {
			ret = a6xx_rpmh_start(gmu);
			if (ret)
				return ret;
		}

		image = (u32 *) adreno_gpu->fw[ADRENO_FW_GMU]->data;

		for (i = 0; i < adreno_gpu->fw[ADRENO_FW_GMU]->size >> 2; i++)
			gmu_write(gmu, REG_A6XX_GMU_CM3_ITCM_START + i,
				image[i]);
	}

	gmu_write(gmu, REG_A6XX_GMU_CM3_FW_INIT_RESULT, 0);
	gmu_write(gmu, REG_A6XX_GMU_CM3_BOOT_CONFIG, 0x02);

	/* Write the iova of the HFI table */
	gmu_write(gmu, REG_A6XX_GMU_HFI_QTBL_ADDR, gmu->hfi->iova);
	gmu_write(gmu, REG_A6XX_GMU_HFI_QTBL_INFO, 1);

	gmu_write(gmu, REG_A6XX_GMU_AHB_FENCE_RANGE_0,
		(1 << 31) | (0xa << 18) | (0xa0));

	chipid = adreno_gpu->rev.core << 24;
	chipid |= adreno_gpu->rev.major << 16;
	chipid |= adreno_gpu->rev.minor << 12;
	chipid |= adreno_gpu->rev.patchid << 8;

	gmu_write(gmu, REG_A6XX_GMU_HFI_SFR_ADDR, chipid);

	/* Set up the lowest idle level on the GMU */
	a6xx_gmu_power_config(gmu);

	ret = a6xx_gmu_start(gmu);
	if (ret)
		return ret;

	ret = a6xx_gmu_gfx_rail_on(gmu);
	if (ret)
		return ret;

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
	u32 val;

	/* Make sure there are no outstanding RPMh votes */
	gmu_poll_timeout(gmu, REG_A6XX_RSCC_TCS0_DRV0_STATUS, val,
		(val & 1), 100, 10000);
	gmu_poll_timeout(gmu, REG_A6XX_RSCC_TCS1_DRV0_STATUS, val,
		(val & 1), 100, 10000);
	gmu_poll_timeout(gmu, REG_A6XX_RSCC_TCS2_DRV0_STATUS, val,
		(val & 1), 100, 10000);
	gmu_poll_timeout(gmu, REG_A6XX_RSCC_TCS3_DRV0_STATUS, val,
		(val & 1), 100, 1000);
}

/* Force the GMU off in case it isn't responsive */
static void a6xx_gmu_force_off(struct a6xx_gmu *gmu)
{
	/* Flush all the queues */
	a6xx_hfi_stop(gmu);

	/* Stop the interrupts */
	a6xx_gmu_irq_disable(gmu);

	/* Force off SPTP in case the GMU is managing it */
	a6xx_sptprac_disable(gmu);

	/* Make sure there are no outstanding RPMh votes */
	a6xx_gmu_rpmh_off(gmu);
}

int a6xx_gmu_resume(struct a6xx_gpu *a6xx_gpu)
{
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	struct msm_gpu *gpu = &adreno_gpu->base;
	struct a6xx_gmu *gmu = &a6xx_gpu->gmu;
	int status, ret;

	if (WARN(!gmu->initialized, "The GMU is not set up yet\n"))
		return 0;

	gmu->hung = false;

	/* Turn on the resources */
	pm_runtime_get_sync(gmu->dev);

	/* Use a known rate to bring up the GMU */
	clk_set_rate(gmu->core_clk, 200000000);
	ret = clk_bulk_prepare_enable(gmu->nr_clocks, gmu->clocks);
	if (ret) {
		pm_runtime_put(gmu->dev);
		return ret;
	}

	/* Set the bus quota to a reasonable value for boot */
	icc_set_bw(gpu->icc_path, 0, MBps_to_icc(3072));

	/* Enable the GMU interrupt */
	gmu_write(gmu, REG_A6XX_GMU_AO_HOST_INTERRUPT_CLR, ~0);
	gmu_write(gmu, REG_A6XX_GMU_AO_HOST_INTERRUPT_MASK, ~A6XX_GMU_IRQ_MASK);
	enable_irq(gmu->gmu_irq);

	/* Check to see if we are doing a cold or warm boot */
	status = gmu_read(gmu, REG_A6XX_GMU_GENERAL_7) == 1 ?
		GMU_WARM_BOOT : GMU_COLD_BOOT;

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
	__a6xx_gmu_set_freq(gmu, gmu->current_perf_index);

	/*
	 * "enable" the GX power domain which won't actually do anything but it
	 * will make sure that the refcounting is correct in case we need to
	 * bring down the GX after a GMU failure
	 */
	if (!IS_ERR_OR_NULL(gmu->gxpd))
		pm_runtime_get(gmu->gxpd);

out:
	/* On failure, shut down the GMU to leave it in a good state */
	if (ret) {
		disable_irq(gmu->gmu_irq);
		a6xx_rpmh_stop(gmu);
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

#define GBIF_CLIENT_HALT_MASK             BIT(0)
#define GBIF_ARB_HALT_MASK                BIT(1)

static void a6xx_bus_clear_pending_transactions(struct adreno_gpu *adreno_gpu)
{
	struct msm_gpu *gpu = &adreno_gpu->base;

	if (!a6xx_has_gbif(adreno_gpu)) {
		gpu_write(gpu, REG_A6XX_VBIF_XIN_HALT_CTRL0, 0xf);
		spin_until((gpu_read(gpu, REG_A6XX_VBIF_XIN_HALT_CTRL1) &
								0xf) == 0xf);
		gpu_write(gpu, REG_A6XX_VBIF_XIN_HALT_CTRL0, 0);

		return;
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

/* Gracefully try to shut down the GMU and by extension the GPU */
static void a6xx_gmu_shutdown(struct a6xx_gmu *gmu)
{
	struct a6xx_gpu *a6xx_gpu = container_of(gmu, struct a6xx_gpu, gmu);
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	u32 val;

	/*
	 * The GMU may still be in slumber unless the GPU started so check and
	 * skip putting it back into slumber if so
	 */
	val = gmu_read(gmu, REG_A6XX_GPU_GMU_CX_GMU_RPMH_POWER_STATE);

	if (val != 0xf) {
		int ret = a6xx_gmu_wait_for_idle(gmu);

		/* If the GMU isn't responding assume it is hung */
		if (ret) {
			a6xx_gmu_force_off(gmu);
			return;
		}

		a6xx_bus_clear_pending_transactions(adreno_gpu);

		/* tell the GMU we want to slumber */
		a6xx_gmu_notify_slumber(gmu);

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
	}

	/* Turn off HFI */
	a6xx_hfi_stop(gmu);

	/* Stop the interrupts and mask the hardware */
	a6xx_gmu_irq_disable(gmu);

	/* Tell RPMh to power off the GPU */
	a6xx_rpmh_stop(gmu);
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
	icc_set_bw(gpu->icc_path, 0, 0);

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

static void a6xx_gmu_memory_free(struct a6xx_gmu *gmu, struct a6xx_gmu_bo *bo)
{
	int count, i;
	u64 iova;

	if (IS_ERR_OR_NULL(bo))
		return;

	count = bo->size >> PAGE_SHIFT;
	iova = bo->iova;

	for (i = 0; i < count; i++, iova += PAGE_SIZE) {
		iommu_unmap(gmu->domain, iova, PAGE_SIZE);
		__free_pages(bo->pages[i], 0);
	}

	kfree(bo->pages);
	kfree(bo);
}

static struct a6xx_gmu_bo *a6xx_gmu_memory_alloc(struct a6xx_gmu *gmu,
		size_t size)
{
	struct a6xx_gmu_bo *bo;
	int ret, count, i;

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return ERR_PTR(-ENOMEM);

	bo->size = PAGE_ALIGN(size);

	count = bo->size >> PAGE_SHIFT;

	bo->pages = kcalloc(count, sizeof(struct page *), GFP_KERNEL);
	if (!bo->pages) {
		kfree(bo);
		return ERR_PTR(-ENOMEM);
	}

	for (i = 0; i < count; i++) {
		bo->pages[i] = alloc_page(GFP_KERNEL);
		if (!bo->pages[i])
			goto err;
	}

	bo->iova = gmu->uncached_iova_base;

	for (i = 0; i < count; i++) {
		ret = iommu_map(gmu->domain,
			bo->iova + (PAGE_SIZE * i),
			page_to_phys(bo->pages[i]), PAGE_SIZE,
			IOMMU_READ | IOMMU_WRITE);

		if (ret) {
			DRM_DEV_ERROR(gmu->dev, "Unable to map GMU buffer object\n");

			for (i = i - 1 ; i >= 0; i--)
				iommu_unmap(gmu->domain,
					bo->iova + (PAGE_SIZE * i),
					PAGE_SIZE);

			goto err;
		}
	}

	bo->virt = vmap(bo->pages, count, VM_IOREMAP,
		pgprot_writecombine(PAGE_KERNEL));
	if (!bo->virt)
		goto err;

	/* Align future IOVA addresses on 1MB boundaries */
	gmu->uncached_iova_base += ALIGN(size, SZ_1M);

	return bo;

err:
	for (i = 0; i < count; i++) {
		if (bo->pages[i])
			__free_pages(bo->pages[i], 0);
	}

	kfree(bo->pages);
	kfree(bo);

	return ERR_PTR(-ENOMEM);
}

static int a6xx_gmu_memory_probe(struct a6xx_gmu *gmu)
{
	int ret;

	/*
	 * The GMU address space is hardcoded to treat the range
	 * 0x60000000 - 0x80000000 as un-cached memory. All buffers shared
	 * between the GMU and the CPU will live in this space
	 */
	gmu->uncached_iova_base = 0x60000000;


	gmu->domain = iommu_domain_alloc(&platform_bus_type);
	if (!gmu->domain)
		return -ENODEV;

	ret = iommu_attach_device(gmu->domain, gmu->dev);

	if (ret) {
		iommu_domain_free(gmu->domain);
		gmu->domain = NULL;
	}

	return ret;
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
				"Level %u not found in in the RPMh list\n",
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
 */

static int a6xx_gmu_rpmh_votes_init(struct a6xx_gmu *gmu)
{
	struct a6xx_gpu *a6xx_gpu = container_of(gmu, struct a6xx_gpu, gmu);
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	struct msm_gpu *gpu = &adreno_gpu->base;
	int ret;

	/* Build the GX votes */
	ret = a6xx_gmu_rpmh_arc_votes_init(&gpu->pdev->dev, gmu->gx_arc_votes,
		gmu->gpu_freqs, gmu->nr_gpu_freqs, "gfx.lvl");

	/* Build the CX votes */
	ret |= a6xx_gmu_rpmh_arc_votes_init(gmu->dev, gmu->cx_arc_votes,
		gmu->gmu_freqs, gmu->nr_gmu_freqs, "cx.lvl");

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

static int a6xx_gmu_pwrlevels_probe(struct a6xx_gmu *gmu)
{
	struct a6xx_gpu *a6xx_gpu = container_of(gmu, struct a6xx_gpu, gmu);
	struct adreno_gpu *adreno_gpu = &a6xx_gpu->base;
	struct msm_gpu *gpu = &adreno_gpu->base;

	int ret = 0;

	/*
	 * The GMU handles its own frequency switching so build a list of
	 * available frequencies to send during initialization
	 */
	ret = dev_pm_opp_of_add_table(gmu->dev);
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

	ret = request_irq(irq, handler, IRQF_TRIGGER_HIGH, name, gmu);
	if (ret) {
		DRM_DEV_ERROR(&pdev->dev, "Unable to get interrupt %s %d\n",
			      name, ret);
		return ret;
	}

	disable_irq(irq);

	return irq;
}

void a6xx_gmu_remove(struct a6xx_gpu *a6xx_gpu)
{
	struct a6xx_gmu *gmu = &a6xx_gpu->gmu;

	if (!gmu->initialized)
		return;

	pm_runtime_force_suspend(gmu->dev);

	if (!IS_ERR_OR_NULL(gmu->gxpd)) {
		pm_runtime_disable(gmu->gxpd);
		dev_pm_domain_detach(gmu->gxpd, false);
	}

	iounmap(gmu->mmio);
	gmu->mmio = NULL;

	a6xx_gmu_memory_free(gmu, gmu->hfi);

	iommu_detach_device(gmu->domain, gmu->dev);

	iommu_domain_free(gmu->domain);

	free_irq(gmu->gmu_irq, gmu);
	free_irq(gmu->hfi_irq, gmu);

	/* Drop reference taken in of_find_device_by_node */
	put_device(gmu->dev);

	gmu->initialized = false;
}

int a6xx_gmu_init(struct a6xx_gpu *a6xx_gpu, struct device_node *node)
{
	struct a6xx_gmu *gmu = &a6xx_gpu->gmu;
	struct platform_device *pdev = of_find_device_by_node(node);
	int ret;

	if (!pdev)
		return -ENODEV;

	gmu->dev = &pdev->dev;

	of_dma_configure(gmu->dev, node, true);

	/* Fow now, don't do anything fancy until we get our feet under us */
	gmu->idle_level = GMU_IDLE_STATE_ACTIVE;

	pm_runtime_enable(gmu->dev);

	/* Get the list of clocks */
	ret = a6xx_gmu_clocks_probe(gmu);
	if (ret)
		goto err_put_device;

	/* Set up the IOMMU context bank */
	ret = a6xx_gmu_memory_probe(gmu);
	if (ret)
		goto err_put_device;

	/* Allocate memory for for the HFI queues */
	gmu->hfi = a6xx_gmu_memory_alloc(gmu, SZ_16K);
	if (IS_ERR(gmu->hfi))
		goto err_memory;

	/* Allocate memory for the GMU debug region */
	gmu->debug = a6xx_gmu_memory_alloc(gmu, SZ_16K);
	if (IS_ERR(gmu->debug))
		goto err_memory;

	/* Map the GMU registers */
	gmu->mmio = a6xx_gmu_get_mmio(pdev, "gmu");
	if (IS_ERR(gmu->mmio))
		goto err_memory;

	/* Get the HFI and GMU interrupts */
	gmu->hfi_irq = a6xx_gmu_get_irq(gmu, pdev, "hfi", a6xx_hfi_irq);
	gmu->gmu_irq = a6xx_gmu_get_irq(gmu, pdev, "gmu", a6xx_gmu_irq);

	if (gmu->hfi_irq < 0 || gmu->gmu_irq < 0)
		goto err_mmio;

	/*
	 * Get a link to the GX power domain to reset the GPU in case of GMU
	 * crash
	 */
	gmu->gxpd = dev_pm_domain_attach_by_name(gmu->dev, "gx");

	/* Get the power levels for the GMU and GPU */
	a6xx_gmu_pwrlevels_probe(gmu);

	/* Set up the HFI queues */
	a6xx_hfi_init(gmu);

	gmu->initialized = true;

	return 0;

err_mmio:
	iounmap(gmu->mmio);
	free_irq(gmu->gmu_irq, gmu);
	free_irq(gmu->hfi_irq, gmu);
err_memory:
	a6xx_gmu_memory_free(gmu, gmu->hfi);

	if (gmu->domain) {
		iommu_detach_device(gmu->domain, gmu->dev);

		iommu_domain_free(gmu->domain);
	}
	ret = -ENODEV;

err_put_device:
	/* Drop reference taken in of_find_device_by_node */
	put_device(gmu->dev);

	return ret;
}
