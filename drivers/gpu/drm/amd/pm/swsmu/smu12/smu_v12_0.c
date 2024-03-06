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
#include "amdgpu_atombios.h"
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
	uint8_t smu_program, smu_major, smu_minor, smu_debug;
	int ret = 0;

	ret = smu_cmn_get_smc_version(smu, &if_version, &smu_version);
	if (ret)
		return ret;

	smu_program = (smu_version >> 24) & 0xff;
	smu_major = (smu_version >> 16) & 0xff;
	smu_minor = (smu_version >> 8) & 0xff;
	smu_debug = (smu_version >> 0) & 0xff;
	if (smu->is_apu)
		adev->pm.fw_version = smu_version;

	/*
	 * 1. if_version mismatch is not critical as our fw is designed
	 * to be backward compatible.
	 * 2. New fw usually brings some optimizations. But that's visible
	 * only on the paired driver.
	 * Considering above, we just leave user a verbal message instead
	 * of halt driver loading.
	 */
	if (if_version != smu->smc_driver_if_version) {
		dev_info(smu->adev->dev, "smu driver if version = 0x%08x, smu fw if version = 0x%08x, "
			"smu fw program = %d, smu fw version = 0x%08x (%d.%d.%d)\n",
			smu->smc_driver_if_version, if_version,
			smu_program, smu_version, smu_major, smu_minor, smu_debug);
		dev_info(smu->adev->dev, "SMU driver if version not matched\n");
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
	/* Until now the SMU12 only implemented for Renoir series so here neen't do APU check. */
	if (!(smu->adev->pg_flags & AMD_PG_SUPPORT_GFX_PG) || smu->adev->in_s0ix)
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

	kfree(smu_table->gpu_metrics_table);
	smu_table->gpu_metrics_table = NULL;

	return 0;
}

int smu_v12_0_set_default_dpm_tables(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;

	return smu_cmn_update_table(smu, SMU_TABLE_DPMCLOCKS, 0, smu_table->clocks_table, false);
}

int smu_v12_0_mode2_reset(struct smu_context *smu)
{
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

static int smu_v12_0_atom_get_smu_clockinfo(struct amdgpu_device *adev,
					    uint8_t clk_id,
					    uint8_t syspll_id,
					    uint32_t *clk_freq)
{
	struct atom_get_smu_clock_info_parameters_v3_1 input = {0};
	struct atom_get_smu_clock_info_output_parameters_v3_1 *output;
	int ret, index;

	input.clk_id = clk_id;
	input.syspll_id = syspll_id;
	input.command = GET_SMU_CLOCK_INFO_V3_1_GET_CLOCK_FREQ;
	index = get_index_into_master_table(atom_master_list_of_command_functions_v2_1,
					    getsmuclockinfo);

	ret = amdgpu_atom_execute_table(adev->mode_info.atom_context, index,
					(uint32_t *)&input);
	if (ret)
		return -EINVAL;

	output = (struct atom_get_smu_clock_info_output_parameters_v3_1 *)&input;
	*clk_freq = le32_to_cpu(output->atom_smu_outputclkfreq.smu_clock_freq_hz) / 10000;

	return 0;
}

int smu_v12_0_get_vbios_bootup_values(struct smu_context *smu)
{
	int ret, index;
	uint16_t size;
	uint8_t frev, crev;
	struct atom_common_table_header *header;
	struct atom_firmware_info_v3_1 *v_3_1;
	struct atom_firmware_info_v3_3 *v_3_3;

	index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
					    firmwareinfo);

	ret = amdgpu_atombios_get_data_table(smu->adev, index, &size, &frev, &crev,
				      (uint8_t **)&header);
	if (ret)
		return ret;

	if (header->format_revision != 3) {
		dev_err(smu->adev->dev, "unknown atom_firmware_info version! for smu12\n");
		return -EINVAL;
	}

	switch (header->content_revision) {
	case 0:
	case 1:
	case 2:
		v_3_1 = (struct atom_firmware_info_v3_1 *)header;
		smu->smu_table.boot_values.revision = v_3_1->firmware_revision;
		smu->smu_table.boot_values.gfxclk = v_3_1->bootup_sclk_in10khz;
		smu->smu_table.boot_values.uclk = v_3_1->bootup_mclk_in10khz;
		smu->smu_table.boot_values.socclk = 0;
		smu->smu_table.boot_values.dcefclk = 0;
		smu->smu_table.boot_values.vddc = v_3_1->bootup_vddc_mv;
		smu->smu_table.boot_values.vddci = v_3_1->bootup_vddci_mv;
		smu->smu_table.boot_values.mvddc = v_3_1->bootup_mvddc_mv;
		smu->smu_table.boot_values.vdd_gfx = v_3_1->bootup_vddgfx_mv;
		smu->smu_table.boot_values.cooling_id = v_3_1->coolingsolution_id;
		smu->smu_table.boot_values.pp_table_id = 0;
		smu->smu_table.boot_values.firmware_caps = v_3_1->firmware_capability;
		break;
	case 3:
	case 4:
	default:
		v_3_3 = (struct atom_firmware_info_v3_3 *)header;
		smu->smu_table.boot_values.revision = v_3_3->firmware_revision;
		smu->smu_table.boot_values.gfxclk = v_3_3->bootup_sclk_in10khz;
		smu->smu_table.boot_values.uclk = v_3_3->bootup_mclk_in10khz;
		smu->smu_table.boot_values.socclk = 0;
		smu->smu_table.boot_values.dcefclk = 0;
		smu->smu_table.boot_values.vddc = v_3_3->bootup_vddc_mv;
		smu->smu_table.boot_values.vddci = v_3_3->bootup_vddci_mv;
		smu->smu_table.boot_values.mvddc = v_3_3->bootup_mvddc_mv;
		smu->smu_table.boot_values.vdd_gfx = v_3_3->bootup_vddgfx_mv;
		smu->smu_table.boot_values.cooling_id = v_3_3->coolingsolution_id;
		smu->smu_table.boot_values.pp_table_id = v_3_3->pplib_pptable_id;
		smu->smu_table.boot_values.firmware_caps = v_3_3->firmware_capability;
	}

	smu->smu_table.boot_values.format_revision = header->format_revision;
	smu->smu_table.boot_values.content_revision = header->content_revision;

	smu_v12_0_atom_get_smu_clockinfo(smu->adev,
					 (uint8_t)SMU12_SYSPLL0_SOCCLK_ID,
					 (uint8_t)SMU12_SYSPLL0_ID,
					 &smu->smu_table.boot_values.socclk);

	smu_v12_0_atom_get_smu_clockinfo(smu->adev,
					 (uint8_t)SMU12_SYSPLL1_DCFCLK_ID,
					 (uint8_t)SMU12_SYSPLL1_ID,
					 &smu->smu_table.boot_values.dcefclk);

	smu_v12_0_atom_get_smu_clockinfo(smu->adev,
					 (uint8_t)SMU12_SYSPLL0_VCLK_ID,
					 (uint8_t)SMU12_SYSPLL0_ID,
					 &smu->smu_table.boot_values.vclk);

	smu_v12_0_atom_get_smu_clockinfo(smu->adev,
					 (uint8_t)SMU12_SYSPLL0_DCLK_ID,
					 (uint8_t)SMU12_SYSPLL0_ID,
					 &smu->smu_table.boot_values.dclk);

	if ((smu->smu_table.boot_values.format_revision == 3) &&
	    (smu->smu_table.boot_values.content_revision >= 2))
		smu_v12_0_atom_get_smu_clockinfo(smu->adev,
						 (uint8_t)SMU12_SYSPLL3_0_FCLK_ID,
						 (uint8_t)SMU12_SYSPLL3_0_ID,
						 &smu->smu_table.boot_values.fclk);

	smu_v12_0_atom_get_smu_clockinfo(smu->adev,
					 (uint8_t)SMU12_SYSPLL0_LCLK_ID,
					 (uint8_t)SMU12_SYSPLL0_ID,
					 &smu->smu_table.boot_values.lclk);

	return 0;
}
