/* Copyright (c) 2016 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/pm_opp.h>
#include "a5xx_gpu.h"

/*
 * The GPMU data block is a block of shared registers that can be used to
 * communicate back and forth. These "registers" are by convention with the GPMU
 * firwmare and not bound to any specific hardware design
 */

#define AGC_INIT_BASE REG_A5XX_GPMU_DATA_RAM_BASE
#define AGC_INIT_MSG_MAGIC (AGC_INIT_BASE + 5)
#define AGC_MSG_BASE (AGC_INIT_BASE + 7)

#define AGC_MSG_STATE (AGC_MSG_BASE + 0)
#define AGC_MSG_COMMAND (AGC_MSG_BASE + 1)
#define AGC_MSG_PAYLOAD_SIZE (AGC_MSG_BASE + 3)
#define AGC_MSG_PAYLOAD(_o) ((AGC_MSG_BASE + 5) + (_o))

#define AGC_POWER_CONFIG_PRODUCTION_ID 1
#define AGC_INIT_MSG_VALUE 0xBABEFACE

static struct {
	uint32_t reg;
	uint32_t value;
} a5xx_sequence_regs[] = {
	{ 0xB9A1, 0x00010303 },
	{ 0xB9A2, 0x13000000 },
	{ 0xB9A3, 0x00460020 },
	{ 0xB9A4, 0x10000000 },
	{ 0xB9A5, 0x040A1707 },
	{ 0xB9A6, 0x00010000 },
	{ 0xB9A7, 0x0E000904 },
	{ 0xB9A8, 0x10000000 },
	{ 0xB9A9, 0x01165000 },
	{ 0xB9AA, 0x000E0002 },
	{ 0xB9AB, 0x03884141 },
	{ 0xB9AC, 0x10000840 },
	{ 0xB9AD, 0x572A5000 },
	{ 0xB9AE, 0x00000003 },
	{ 0xB9AF, 0x00000000 },
	{ 0xB9B0, 0x10000000 },
	{ 0xB828, 0x6C204010 },
	{ 0xB829, 0x6C204011 },
	{ 0xB82A, 0x6C204012 },
	{ 0xB82B, 0x6C204013 },
	{ 0xB82C, 0x6C204014 },
	{ 0xB90F, 0x00000004 },
	{ 0xB910, 0x00000002 },
	{ 0xB911, 0x00000002 },
	{ 0xB912, 0x00000002 },
	{ 0xB913, 0x00000002 },
	{ 0xB92F, 0x00000004 },
	{ 0xB930, 0x00000005 },
	{ 0xB931, 0x00000005 },
	{ 0xB932, 0x00000005 },
	{ 0xB933, 0x00000005 },
	{ 0xB96F, 0x00000001 },
	{ 0xB970, 0x00000003 },
	{ 0xB94F, 0x00000004 },
	{ 0xB950, 0x0000000B },
	{ 0xB951, 0x0000000B },
	{ 0xB952, 0x0000000B },
	{ 0xB953, 0x0000000B },
	{ 0xB907, 0x00000019 },
	{ 0xB927, 0x00000019 },
	{ 0xB947, 0x00000019 },
	{ 0xB967, 0x00000019 },
	{ 0xB987, 0x00000019 },
	{ 0xB906, 0x00220001 },
	{ 0xB926, 0x00220001 },
	{ 0xB946, 0x00220001 },
	{ 0xB966, 0x00220001 },
	{ 0xB986, 0x00300000 },
	{ 0xAC40, 0x0340FF41 },
	{ 0xAC41, 0x03BEFED0 },
	{ 0xAC42, 0x00331FED },
	{ 0xAC43, 0x021FFDD3 },
	{ 0xAC44, 0x5555AAAA },
	{ 0xAC45, 0x5555AAAA },
	{ 0xB9BA, 0x00000008 },
};

/*
 * Get the actual voltage value for the operating point at the specified
 * frequency
 */
static inline uint32_t _get_mvolts(struct msm_gpu *gpu, uint32_t freq)
{
	struct drm_device *dev = gpu->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct platform_device *pdev = priv->gpu_pdev;
	struct dev_pm_opp *opp;
	u32 ret = 0;

	opp = dev_pm_opp_find_freq_exact(&pdev->dev, freq, true);

	if (!IS_ERR(opp)) {
		ret = dev_pm_opp_get_voltage(opp) / 1000;
		dev_pm_opp_put(opp);
	}

	return ret;
}

/* Setup thermal limit management */
static void a5xx_lm_setup(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a5xx_gpu *a5xx_gpu = to_a5xx_gpu(adreno_gpu);
	unsigned int i;

	/* Write the block of sequence registers */
	for (i = 0; i < ARRAY_SIZE(a5xx_sequence_regs); i++)
		gpu_write(gpu, a5xx_sequence_regs[i].reg,
			a5xx_sequence_regs[i].value);

	/* Hard code the A530 GPU thermal sensor ID for the GPMU */
	gpu_write(gpu, REG_A5XX_GPMU_TEMP_SENSOR_ID, 0x60007);
	gpu_write(gpu, REG_A5XX_GPMU_DELTA_TEMP_THRESHOLD, 0x01);
	gpu_write(gpu, REG_A5XX_GPMU_TEMP_SENSOR_CONFIG, 0x01);

	/* Until we get clock scaling 0 is always the active power level */
	gpu_write(gpu, REG_A5XX_GPMU_GPMU_VOLTAGE, 0x80000000 | 0);

	gpu_write(gpu, REG_A5XX_GPMU_BASE_LEAKAGE, a5xx_gpu->lm_leakage);

	/* The threshold is fixed at 6000 for A530 */
	gpu_write(gpu, REG_A5XX_GPMU_GPMU_PWR_THRESHOLD, 0x80000000 | 6000);

	gpu_write(gpu, REG_A5XX_GPMU_BEC_ENABLE, 0x10001FFF);
	gpu_write(gpu, REG_A5XX_GDPM_CONFIG1, 0x00201FF1);

	/* Write the voltage table */
	gpu_write(gpu, REG_A5XX_GPMU_BEC_ENABLE, 0x10001FFF);
	gpu_write(gpu, REG_A5XX_GDPM_CONFIG1, 0x201FF1);

	gpu_write(gpu, AGC_MSG_STATE, 1);
	gpu_write(gpu, AGC_MSG_COMMAND, AGC_POWER_CONFIG_PRODUCTION_ID);

	/* Write the max power - hard coded to 5448 for A530 */
	gpu_write(gpu, AGC_MSG_PAYLOAD(0), 5448);
	gpu_write(gpu, AGC_MSG_PAYLOAD(1), 1);

	/*
	 * For now just write the one voltage level - we will do more when we
	 * can do scaling
	 */
	gpu_write(gpu, AGC_MSG_PAYLOAD(2), _get_mvolts(gpu, gpu->fast_rate));
	gpu_write(gpu, AGC_MSG_PAYLOAD(3), gpu->fast_rate / 1000000);

	gpu_write(gpu, AGC_MSG_PAYLOAD_SIZE, 4 * sizeof(uint32_t));
	gpu_write(gpu, AGC_INIT_MSG_MAGIC, AGC_INIT_MSG_VALUE);
}

/* Enable SP/TP cpower collapse */
static void a5xx_pc_init(struct msm_gpu *gpu)
{
	gpu_write(gpu, REG_A5XX_GPMU_PWR_COL_INTER_FRAME_CTRL, 0x7F);
	gpu_write(gpu, REG_A5XX_GPMU_PWR_COL_BINNING_CTRL, 0);
	gpu_write(gpu, REG_A5XX_GPMU_PWR_COL_INTER_FRAME_HYST, 0xA0080);
	gpu_write(gpu, REG_A5XX_GPMU_PWR_COL_STAGGER_DELAY, 0x600040);
}

/* Enable the GPMU microcontroller */
static int a5xx_gpmu_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a5xx_gpu *a5xx_gpu = to_a5xx_gpu(adreno_gpu);
	struct msm_ringbuffer *ring = gpu->rb[0];

	if (!a5xx_gpu->gpmu_dwords)
		return 0;

	/* Turn off protected mode for this operation */
	OUT_PKT7(ring, CP_SET_PROTECTED_MODE, 1);
	OUT_RING(ring, 0);

	/* Kick off the IB to load the GPMU microcode */
	OUT_PKT7(ring, CP_INDIRECT_BUFFER_PFE, 3);
	OUT_RING(ring, lower_32_bits(a5xx_gpu->gpmu_iova));
	OUT_RING(ring, upper_32_bits(a5xx_gpu->gpmu_iova));
	OUT_RING(ring, a5xx_gpu->gpmu_dwords);

	/* Turn back on protected mode */
	OUT_PKT7(ring, CP_SET_PROTECTED_MODE, 1);
	OUT_RING(ring, 1);

	gpu->funcs->flush(gpu, ring);

	if (!a5xx_idle(gpu, ring)) {
		DRM_ERROR("%s: Unable to load GPMU firmware. GPMU will not be active\n",
			gpu->name);
		return -EINVAL;
	}

	gpu_write(gpu, REG_A5XX_GPMU_WFI_CONFIG, 0x4014);

	/* Kick off the GPMU */
	gpu_write(gpu, REG_A5XX_GPMU_CM3_SYSRESET, 0x0);

	/*
	 * Wait for the GPMU to respond. It isn't fatal if it doesn't, we just
	 * won't have advanced power collapse.
	 */
	if (spin_usecs(gpu, 25, REG_A5XX_GPMU_GENERAL_0, 0xFFFFFFFF,
		0xBABEFACE))
		DRM_ERROR("%s: GPMU firmware initialization timed out\n",
			gpu->name);

	return 0;
}

/* Enable limits management */
static void a5xx_lm_enable(struct msm_gpu *gpu)
{
	gpu_write(gpu, REG_A5XX_GDPM_INT_MASK, 0x0);
	gpu_write(gpu, REG_A5XX_GDPM_INT_EN, 0x0A);
	gpu_write(gpu, REG_A5XX_GPMU_GPMU_VOLTAGE_INTR_EN_MASK, 0x01);
	gpu_write(gpu, REG_A5XX_GPMU_TEMP_THRESHOLD_INTR_EN_MASK, 0x50000);
	gpu_write(gpu, REG_A5XX_GPMU_THROTTLE_UNMASK_FORCE_CTRL, 0x30000);

	gpu_write(gpu, REG_A5XX_GPMU_CLOCK_THROTTLE_CTRL, 0x011);
}

int a5xx_power_init(struct msm_gpu *gpu)
{
	int ret;

	/* Set up the limits management */
	a5xx_lm_setup(gpu);

	/* Set up SP/TP power collpase */
	a5xx_pc_init(gpu);

	/* Start the GPMU */
	ret = a5xx_gpmu_init(gpu);
	if (ret)
		return ret;

	/* Start the limits management */
	a5xx_lm_enable(gpu);

	return 0;
}

void a5xx_gpmu_ucode_init(struct msm_gpu *gpu)
{
	struct adreno_gpu *adreno_gpu = to_adreno_gpu(gpu);
	struct a5xx_gpu *a5xx_gpu = to_a5xx_gpu(adreno_gpu);
	struct drm_device *drm = gpu->dev;
	const struct firmware *fw;
	uint32_t dwords = 0, offset = 0, bosize;
	unsigned int *data, *ptr, *cmds;
	unsigned int cmds_size;

	if (a5xx_gpu->gpmu_bo)
		return;

	/* Get the firmware */
	fw = adreno_request_fw(adreno_gpu, adreno_gpu->info->gpmufw);
	if (IS_ERR(fw)) {
		DRM_ERROR("%s: Could not get GPMU firmware. GPMU will not be active\n",
			gpu->name);
		return;
	}

	data = (unsigned int *) fw->data;

	/*
	 * The first dword is the size of the remaining data in dwords. Use it
	 * as a checksum of sorts and make sure it matches the actual size of
	 * the firmware that we read
	 */

	if (fw->size < 8 || (data[0] < 2) || (data[0] >= (fw->size >> 2)))
		goto out;

	/* The second dword is an ID - look for 2 (GPMU_FIRMWARE_ID) */
	if (data[1] != 2)
		goto out;

	cmds = data + data[2] + 3;
	cmds_size = data[0] - data[2] - 2;

	/*
	 * A single type4 opcode can only have so many values attached so
	 * add enough opcodes to load the all the commands
	 */
	bosize = (cmds_size + (cmds_size / TYPE4_MAX_PAYLOAD) + 1) << 2;

	ptr = msm_gem_kernel_new_locked(drm, bosize,
		MSM_BO_UNCACHED | MSM_BO_GPU_READONLY, gpu->aspace,
		&a5xx_gpu->gpmu_bo, &a5xx_gpu->gpmu_iova);
	if (IS_ERR(ptr))
		goto err;

	while (cmds_size > 0) {
		int i;
		uint32_t _size = cmds_size > TYPE4_MAX_PAYLOAD ?
			TYPE4_MAX_PAYLOAD : cmds_size;

		ptr[dwords++] = PKT4(REG_A5XX_GPMU_INST_RAM_BASE + offset,
			_size);

		for (i = 0; i < _size; i++)
			ptr[dwords++] = *cmds++;

		offset += _size;
		cmds_size -= _size;
	}

	msm_gem_put_vaddr(a5xx_gpu->gpmu_bo);
	a5xx_gpu->gpmu_dwords = dwords;

	goto out;

err:
	if (a5xx_gpu->gpmu_iova)
		msm_gem_put_iova(a5xx_gpu->gpmu_bo, gpu->aspace);
	if (a5xx_gpu->gpmu_bo)
		drm_gem_object_unreference(a5xx_gpu->gpmu_bo);

	a5xx_gpu->gpmu_bo = NULL;
	a5xx_gpu->gpmu_iova = 0;
	a5xx_gpu->gpmu_dwords = 0;

out:
	/* No need to keep that firmware laying around anymore */
	release_firmware(fw);
}
