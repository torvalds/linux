/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "pp_debug.h"
#include <linux/firmware.h>
#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "smu_internal.h"
#include "atomfirmware.h"
#include "amdgpu_atomfirmware.h"
#include "smu_v12_0.h"
#include "soc15_common.h"
#include "atom.h"

#include "asic_reg/mp/mp_12_0_0_offset.h"
#include "asic_reg/mp/mp_12_0_0_sh_mask.h"

#define smnMP1_FIRMWARE_FLAGS                                0x3010024

#define mmSMUIO_GFX_MISC_CNTL                                0x00c8
#define mmSMUIO_GFX_MISC_CNTL_BASE_IDX                       0
#define SMUIO_GFX_MISC_CNTL__PWR_GFXOFF_STATUS_MASK          0x00000006L
#define SMUIO_GFX_MISC_CNTL__PWR_GFXOFF_STATUS__SHIFT        0x1

int smu_v12_0_send_msg_without_waiting(struct smu_context *smu,
					      uint16_t msg)
{
	struct amdgpu_device *adev = smu->adev;

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_66, msg);
	return 0;
}

static int smu_v12_0_read_arg(struct smu_context *smu, uint32_t *arg)
{
	struct amdgpu_device *adev = smu->adev;

	*arg = RREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_82);
	return 0;
}

int smu_v12_0_wait_for_response(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t cur_value, i;

	for (i = 0; i < adev->usec_timeout; i++) {
		cur_value = RREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90);
		if ((cur_value & MP1_C2PMSG_90__CONTENT_MASK) != 0)
			return cur_value == 0x1 ? 0 : -EIO;

		udelay(1);
	}

	/* timeout means wrong logic */
	return -ETIME;
}

int
smu_v12_0_send_msg_with_param(struct smu_context *smu,
			      enum smu_message_type msg,
			      uint32_t param,
			      uint32_t *read_arg)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0, index = 0;

	index = smu_msg_get_index(smu, msg);
	if (index < 0)
		return index;

	mutex_lock(&smu->message_lock);
	ret = smu_v12_0_wait_for_response(smu);
	if (ret) {
		pr_err("Msg issuing pre-check failed and "
		       "SMU may be not in the right state!\n");
		goto out;
	}

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90, 0);

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_82, param);

	smu_v12_0_send_msg_without_waiting(smu, (uint16_t)index);

	ret = smu_v12_0_wait_for_response(smu);
	if (ret) {
		pr_err("Failed to send message 0x%x, response 0x%x param 0x%x\n",
		       index, ret, param);
		goto out;
	}
	if (read_arg) {
		ret = smu_v12_0_read_arg(smu, read_arg);
		if (ret) {
			pr_err("Failed to read message arg 0x%x, response 0x%x param 0x%x\n",
			       index, ret, param);
			goto out;
		}
	}
out:
	mutex_unlock(&smu->message_lock);
	return ret;
}

int smu_v12_0_check_fw_status(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t mp1_fw_flags;

	mp1_fw_flags = RREG32_PCIE(MP1_Public |
		(smnMP1_FIRMWARE_FLAGS & 0xffffffff));

	if ((mp1_fw_flags & MP1_FIRMWARE_FLAGS__INTERRUPTS_ENABLED_MASK) >>
		MP1_FIRMWARE_FLAGS__INTERRUPTS_ENABLED__SHIFT)
		return 0;

	return -EIO;
}

int smu_v12_0_check_fw_version(struct smu_context *smu)
{
	uint32_t if_version = 0xff, smu_version = 0xff;
	uint16_t smu_major;
	uint8_t smu_minor, smu_debug;
	int ret = 0;

	ret = smu_get_smc_version(smu, &if_version, &smu_version);
	if (ret)
		return ret;

	smu_major = (smu_version >> 16) & 0xffff;
	smu_minor = (smu_version >> 8) & 0xff;
	smu_debug = (smu_version >> 0) & 0xff;

	/*
	 * 1. if_version mismatch is not critical as our fw is designed
	 * to be backward compatible.
	 * 2. New fw usually brings some optimizations. But that's visible
	 * only on the paired driver.
	 * Considering above, we just leave user a warning message instead
	 * of halt driver loading.
	 */
	if (if_version != smu->smc_if_version) {
		pr_info("smu driver if version = 0x%08x, smu fw if version = 0x%08x, "
			"smu fw version = 0x%08x (%d.%d.%d)\n",
			smu->smc_if_version, if_version,
			smu_version, smu_major, smu_minor, smu_debug);
		pr_warn("SMU driver if version not matched\n");
	}

	return ret;
}

int smu_v12_0_powergate_sdma(struct smu_context *smu, bool gate)
{
	if (!smu->is_apu)
		return 0;

	if (gate)
		return smu_send_smc_msg(smu, SMU_MSG_PowerDownSdma, NULL);
	else
		return smu_send_smc_msg(smu, SMU_MSG_PowerUpSdma, NULL);
}

int smu_v12_0_powergate_vcn(struct smu_context *smu, bool gate)
{
	if (!smu->is_apu)
		return 0;

	if (gate)
		return smu_send_smc_msg(smu, SMU_MSG_PowerDownVcn, NULL);
	else
		return smu_send_smc_msg(smu, SMU_MSG_PowerUpVcn, NULL);
}

int smu_v12_0_powergate_jpeg(struct smu_context *smu, bool gate)
{
	if (!smu->is_apu)
		return 0;

	if (gate)
		return smu_send_smc_msg_with_param(smu, SMU_MSG_PowerDownJpeg, 0, NULL);
	else
		return smu_send_smc_msg_with_param(smu, SMU_MSG_PowerUpJpeg, 0, NULL);
}

int smu_v12_0_set_gfx_cgpg(struct smu_context *smu, bool enable)
{
	if (!(smu->adev->pg_flags & AMD_PG_SUPPORT_GFX_PG))
		return 0;

	return smu_v12_0_send_msg_with_param(smu,
		SMU_MSG_SetGfxCGPG,
		enable ? 1 : 0,
		NULL);
}

int smu_v12_0_read_sensor(struct smu_context *smu,
				 enum amd_pp_sensors sensor,
				 void *data, uint32_t *size)
{
	int ret = 0;

	if(!data || !size)
		return -EINVAL;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_GFX_MCLK:
		ret = smu_get_current_clk_freq(smu, SMU_UCLK, (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GFX_SCLK:
		ret = smu_get_current_clk_freq(smu, SMU_GFXCLK, (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_MIN_FAN_RPM:
		*(uint32_t *)data = 0;
		*size = 4;
		break;
	default:
		ret = smu_common_read_sensor(smu, sensor, data, size);
		break;
	}

	if (ret)
		*size = 0;

	return ret;
}

/**
 * smu_v12_0_get_gfxoff_status - get gfxoff status
 *
 * @smu: amdgpu_device pointer
 *
 * This function will be used to get gfxoff status
 *
 * Returns 0=GFXOFF(default).
 * Returns 1=Transition out of GFX State.
 * Returns 2=Not in GFXOFF.
 * Returns 3=Transition into GFXOFF.
 */
uint32_t smu_v12_0_get_gfxoff_status(struct smu_context *smu)
{
	uint32_t reg;
	uint32_t gfxOff_Status = 0;
	struct amdgpu_device *adev = smu->adev;

	reg = RREG32_SOC15(SMUIO, 0, mmSMUIO_GFX_MISC_CNTL);
	gfxOff_Status = (reg & SMUIO_GFX_MISC_CNTL__PWR_GFXOFF_STATUS_MASK)
		>> SMUIO_GFX_MISC_CNTL__PWR_GFXOFF_STATUS__SHIFT;

	return gfxOff_Status;
}

int smu_v12_0_gfx_off_control(struct smu_context *smu, bool enable)
{
	int ret = 0, timeout = 500;

	if (enable) {
		ret = smu_send_smc_msg(smu, SMU_MSG_AllowGfxOff, NULL);

	} else {
		ret = smu_send_smc_msg(smu, SMU_MSG_DisallowGfxOff, NULL);

		/* confirm gfx is back to "on" state, timeout is 0.5 second */
		while (!(smu_v12_0_get_gfxoff_status(smu) == 2)) {
			msleep(1);
			timeout--;
			if (timeout == 0) {
				DRM_ERROR("disable gfxoff timeout and failed!\n");
				break;
			}
		}
	}

	return ret;
}

int smu_v12_0_init_smc_tables(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = NULL;

	if (smu_table->tables)
		return -EINVAL;

	tables = kcalloc(SMU_TABLE_COUNT, sizeof(struct smu_table),
			 GFP_KERNEL);
	if (!tables)
		return -ENOMEM;

	smu_table->tables = tables;

	return smu_tables_init(smu, tables);
}

int smu_v12_0_fini_smc_tables(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;

	if (!smu_table->tables)
		return -EINVAL;

	kfree(smu_table->clocks_table);
	kfree(smu_table->tables);

	smu_table->clocks_table = NULL;
	smu_table->tables = NULL;

	return 0;
}

int smu_v12_0_populate_smc_tables(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;

	return smu_update_table(smu, SMU_TABLE_DPMCLOCKS, 0, smu_table->clocks_table, false);
}

int smu_v12_0_get_enabled_mask(struct smu_context *smu,
				      uint32_t *feature_mask, uint32_t num)
{
	uint32_t feature_mask_high = 0, feature_mask_low = 0;
	int ret = 0;

	if (!feature_mask || num < 2)
		return -EINVAL;

	ret = smu_send_smc_msg(smu, SMU_MSG_GetEnabledSmuFeaturesHigh, &feature_mask_high);
	if (ret)
		return ret;

	ret = smu_send_smc_msg(smu, SMU_MSG_GetEnabledSmuFeaturesLow, &feature_mask_low);
	if (ret)
		return ret;

	feature_mask[0] = feature_mask_low;
	feature_mask[1] = feature_mask_high;

	return ret;
}

int smu_v12_0_get_current_clk_freq(struct smu_context *smu,
					  enum smu_clk_type clk_id,
					  uint32_t *value)
{
	int ret = 0;
	uint32_t freq = 0;

	if (clk_id >= SMU_CLK_COUNT || !value)
		return -EINVAL;

	ret = smu_get_current_clk_freq_by_table(smu, clk_id, &freq);
	if (ret)
		return ret;

	freq *= 100;
	*value = freq;

	return ret;
}

int smu_v12_0_get_dpm_ultimate_freq(struct smu_context *smu, enum smu_clk_type clk_type,
						 uint32_t *min, uint32_t *max)
{
	int ret = 0;
	uint32_t mclk_mask, soc_mask;

	if (max) {
		ret = smu_get_profiling_clk_mask(smu, AMD_DPM_FORCED_LEVEL_PROFILE_PEAK,
						 NULL,
						 &mclk_mask,
						 &soc_mask);
		if (ret)
			goto failed;

		switch (clk_type) {
		case SMU_GFXCLK:
		case SMU_SCLK:
			ret = smu_send_smc_msg(smu, SMU_MSG_GetMaxGfxclkFrequency, max);
			if (ret) {
				pr_err("Attempt to get max GX frequency from SMC Failed !\n");
				goto failed;
			}
			break;
		case SMU_UCLK:
		case SMU_FCLK:
		case SMU_MCLK:
			ret = smu_get_dpm_clk_limited(smu, clk_type, mclk_mask, max);
			if (ret)
				goto failed;
			break;
		case SMU_SOCCLK:
			ret = smu_get_dpm_clk_limited(smu, clk_type, soc_mask, max);
			if (ret)
				goto failed;
			break;
		default:
			ret = -EINVAL;
			goto failed;
		}
	}

	if (min) {
		switch (clk_type) {
		case SMU_GFXCLK:
		case SMU_SCLK:
			ret = smu_send_smc_msg(smu, SMU_MSG_GetMinGfxclkFrequency, min);
			if (ret) {
				pr_err("Attempt to get min GX frequency from SMC Failed !\n");
				goto failed;
			}
			break;
		case SMU_UCLK:
		case SMU_FCLK:
		case SMU_MCLK:
			ret = smu_get_dpm_clk_limited(smu, clk_type, 0, min);
			if (ret)
				goto failed;
			break;
		case SMU_SOCCLK:
			ret = smu_get_dpm_clk_limited(smu, clk_type, 0, min);
			if (ret)
				goto failed;
			break;
		default:
			ret = -EINVAL;
			goto failed;
		}
	}
failed:
	return ret;
}

int smu_v12_0_mode2_reset(struct smu_context *smu){
	return smu_v12_0_send_msg_with_param(smu, SMU_MSG_GfxDeviceDriverReset, SMU_RESET_MODE_2, NULL);
}

int smu_v12_0_set_soft_freq_limited_range(struct smu_context *smu, enum smu_clk_type clk_type,
			    uint32_t min, uint32_t max)
{
	int ret = 0;

	switch (clk_type) {
	case SMU_GFXCLK:
	case SMU_SCLK:
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetHardMinGfxClk, min, NULL);
		if (ret)
			return ret;

		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMaxGfxClk, max, NULL);
		if (ret)
			return ret;
	break;
	case SMU_FCLK:
	case SMU_MCLK:
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetHardMinFclkByFreq, min, NULL);
		if (ret)
			return ret;

		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMaxFclkByFreq, max, NULL);
		if (ret)
			return ret;
	break;
	case SMU_SOCCLK:
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetHardMinSocclkByFreq, min, NULL);
		if (ret)
			return ret;

		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMaxSocclkByFreq, max, NULL);
		if (ret)
			return ret;
	break;
	case SMU_VCLK:
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetHardMinVcn, min, NULL);
		if (ret)
			return ret;

		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMaxVcn, max, NULL);
		if (ret)
			return ret;
	break;
	default:
		return -EINVAL;
	}

	return ret;
}

int smu_v12_0_set_driver_table_location(struct smu_context *smu)
{
	struct smu_table *driver_table = &smu->smu_table.driver_table;
	int ret = 0;

	if (driver_table->mc_address) {
		ret = smu_send_smc_msg_with_param(smu,
				SMU_MSG_SetDriverDramAddrHigh,
				upper_32_bits(driver_table->mc_address),
				NULL);
		if (!ret)
			ret = smu_send_smc_msg_with_param(smu,
				SMU_MSG_SetDriverDramAddrLow,
				lower_32_bits(driver_table->mc_address),
				NULL);
	}

	return ret;
}
