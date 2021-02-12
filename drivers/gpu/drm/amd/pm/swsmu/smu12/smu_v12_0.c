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

#define SWSMU_CODE_LAYER_L3

#include <linux/firmware.h>
#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "atomfirmware.h"
#include "amdgpu_atomfirmware.h"
#include "smu_v12_0.h"
#include "soc15_common.h"
#include "atom.h"
#include "smu_cmn.h"

#include "asic_reg/mp/mp_12_0_0_offset.h"
#include "asic_reg/mp/mp_12_0_0_sh_mask.h"
#include "asic_reg/smuio/smuio_12_0_0_offset.h"
#include "asic_reg/smuio/smuio_12_0_0_sh_mask.h"

/*
 * DO NOT use these for err/warn/info/debug messages.
 * Use dev_err, dev_warn, dev_info and dev_dbg instead.
 * They are more MGPU friendly.
 */
#undef pr_err
#undef pr_warn
#undef pr_info
#undef pr_debug

// because some SMU12 based ASICs use older ip offset tables
// we should undefine this register from the smuio12 header
// to prevent confusion down the road
#undef mmPWR_MISC_CNTL_STATUS

#define smnMP1_FIRMWARE_FLAGS                                0x3010024

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
	struct amdgpu_device *adev = smu->adev;
	uint32_t if_version = 0xff, smu_version = 0xff;
	uint16_t smu_major;
	uint8_t smu_minor, smu_debug;
	int ret = 0;

	ret = smu_cmn_get_smc_version(smu, &if_version, &smu_version);
	if (ret)
		return ret;

	smu_major = (smu_version >> 16) & 0xffff;
	smu_minor = (smu_version >> 8) & 0xff;
	smu_debug = (smu_version >> 0) & 0xff;
	if (smu->is_apu)
		adev->pm.fw_version = smu_version;

	/*
	 * 1. if_version mismatch is not critical as our fw is designed
	 * to be backward compatible.
	 * 2. New fw usually brings some optimizations. But that's visible
	 * only on the paired driver.
	 * Considering above, we just leave user a warning message instead
	 * of halt driver loading.
	 */
	if (if_version != smu->smc_driver_if_version) {
		dev_info(smu->adev->dev, "smu driver if version = 0x%08x, smu fw if version = 0x%08x, "
			"smu fw version = 0x%08x (%d.%d.%d)\n",
			smu->smc_driver_if_version, if_version,
			smu_version, smu_major, smu_minor, smu_debug);
		dev_warn(smu->adev->dev, "SMU driver if version not matched\n");
	}

	return ret;
}

int smu_v12_0_powergate_sdma(struct smu_context *smu, bool gate)
{
	if (!smu->is_apu)
		return 0;

	if (gate)
		return smu_cmn_send_smc_msg(smu, SMU_MSG_PowerDownSdma, NULL);
	else
		return smu_cmn_send_smc_msg(smu, SMU_MSG_PowerUpSdma, NULL);
}

int smu_v12_0_set_gfx_cgpg(struct smu_context *smu, bool enable)
{
	if (!(smu->adev->pg_flags & AMD_PG_SUPPORT_GFX_PG))
		return 0;

	return smu_cmn_send_smc_msg_with_param(smu,
		SMU_MSG_SetGfxCGPG,
		enable ? 1 : 0,
		NULL);
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
		ret = smu_cmn_send_smc_msg(smu, SMU_MSG_AllowGfxOff, NULL);

	} else {
		ret = smu_cmn_send_smc_msg(smu, SMU_MSG_DisallowGfxOff, NULL);

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

int smu_v12_0_fini_smc_tables(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;

	kfree(smu_table->clocks_table);
	smu_table->clocks_table = NULL;

	kfree(smu_table->metrics_table);
	smu_table->metrics_table = NULL;

	kfree(smu_table->watermarks_table);
	smu_table->watermarks_table = NULL;

	return 0;
}

int smu_v12_0_set_default_dpm_tables(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;

	return smu_cmn_update_table(smu, SMU_TABLE_DPMCLOCKS, 0, smu_table->clocks_table, false);
}

int smu_v12_0_mode2_reset(struct smu_context *smu){
	return smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_GfxDeviceDriverReset, SMU_RESET_MODE_2, NULL);
}

int smu_v12_0_set_soft_freq_limited_range(struct smu_context *smu, enum smu_clk_type clk_type,
			    uint32_t min, uint32_t max)
{
	int ret = 0;

	if (!smu_cmn_clk_dpm_is_enabled(smu, clk_type))
		return 0;

	switch (clk_type) {
	case SMU_GFXCLK:
	case SMU_SCLK:
		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetHardMinGfxClk, min, NULL);
		if (ret)
			return ret;

		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMaxGfxClk, max, NULL);
		if (ret)
			return ret;
	break;
	case SMU_FCLK:
	case SMU_MCLK:
	case SMU_UCLK:
		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetHardMinFclkByFreq, min, NULL);
		if (ret)
			return ret;

		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMaxFclkByFreq, max, NULL);
		if (ret)
			return ret;
	break;
	case SMU_SOCCLK:
		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetHardMinSocclkByFreq, min, NULL);
		if (ret)
			return ret;

		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMaxSocclkByFreq, max, NULL);
		if (ret)
			return ret;
	break;
	case SMU_VCLK:
		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetHardMinVcn, min, NULL);
		if (ret)
			return ret;

		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMaxVcn, max, NULL);
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
		ret = smu_cmn_send_smc_msg_with_param(smu,
				SMU_MSG_SetDriverDramAddrHigh,
				upper_32_bits(driver_table->mc_address),
				NULL);
		if (!ret)
			ret = smu_cmn_send_smc_msg_with_param(smu,
				SMU_MSG_SetDriverDramAddrLow,
				lower_32_bits(driver_table->mc_address),
				NULL);
	}

	return ret;
}

void smu_v12_0_init_gpu_metrics_v2_0(struct gpu_metrics_v2_0 *gpu_metrics)
{
	memset(gpu_metrics, 0xFF, sizeof(struct gpu_metrics_v2_0));

	gpu_metrics->common_header.structure_size =
				sizeof(struct gpu_metrics_v2_0);
	gpu_metrics->common_header.format_revision = 2;
	gpu_metrics->common_header.content_revision = 0;

	gpu_metrics->system_clock_counter = ktime_get_boottime_ns();
}
