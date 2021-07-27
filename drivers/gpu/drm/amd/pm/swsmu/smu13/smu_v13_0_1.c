/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

//#include <linux/reboot.h>

#define SWSMU_CODE_LAYER_L3

#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "smu_v13_0_1.h"
#include "soc15_common.h"
#include "smu_cmn.h"
#include "atomfirmware.h"
#include "amdgpu_atomfirmware.h"
#include "amdgpu_atombios.h"
#include "atom.h"

#include "asic_reg/mp/mp_13_0_1_offset.h"
#include "asic_reg/mp/mp_13_0_1_sh_mask.h"

/*
 * DO NOT use these for err/warn/info/debug messages.
 * Use dev_err, dev_warn, dev_info and dev_dbg instead.
 * They are more MGPU friendly.
 */
#undef pr_err
#undef pr_warn
#undef pr_info
#undef pr_debug

int smu_v13_0_1_check_fw_status(struct smu_context *smu)
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

int smu_v13_0_1_check_fw_version(struct smu_context *smu)
{
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

	switch (smu->adev->asic_type) {
	case CHIP_YELLOW_CARP:
		smu->smc_driver_if_version = SMU13_0_1_DRIVER_IF_VERSION_YELLOW_CARP;
		break;

	default:
		dev_err(smu->adev->dev, "smu unsupported asic type:%d.\n", smu->adev->asic_type);
		smu->smc_driver_if_version = SMU13_0_1_DRIVER_IF_VERSION_INV;
		break;
	}

	dev_info(smu->adev->dev, "smu fw reported version = 0x%08x (%d.%d.%d)\n",
			 smu_version, smu_major, smu_minor, smu_debug);

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

int smu_v13_0_1_fini_smc_tables(struct smu_context *smu)
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

static int smu_v13_0_1_atom_get_smu_clockinfo(struct amdgpu_device *adev,
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

int smu_v13_0_1_get_vbios_bootup_values(struct smu_context *smu)
{
	int ret, index;
	uint16_t size;
	uint8_t frev, crev;
	struct atom_common_table_header *header;
	struct atom_firmware_info_v3_4 *v_3_4;
	struct atom_firmware_info_v3_3 *v_3_3;
	struct atom_firmware_info_v3_1 *v_3_1;

	index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
					    firmwareinfo);

	ret = amdgpu_atombios_get_data_table(smu->adev, index, &size, &frev, &crev,
					     (uint8_t **)&header);
	if (ret)
		return ret;

	if (header->format_revision != 3) {
		dev_err(smu->adev->dev, "unknown atom_firmware_info version! for smu13\n");
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
		break;
	case 3:
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
		break;
	case 4:
	default:
		v_3_4 = (struct atom_firmware_info_v3_4 *)header;
		smu->smu_table.boot_values.revision = v_3_4->firmware_revision;
		smu->smu_table.boot_values.gfxclk = v_3_4->bootup_sclk_in10khz;
		smu->smu_table.boot_values.uclk = v_3_4->bootup_mclk_in10khz;
		smu->smu_table.boot_values.socclk = 0;
		smu->smu_table.boot_values.dcefclk = 0;
		smu->smu_table.boot_values.vddc = v_3_4->bootup_vddc_mv;
		smu->smu_table.boot_values.vddci = v_3_4->bootup_vddci_mv;
		smu->smu_table.boot_values.mvddc = v_3_4->bootup_mvddc_mv;
		smu->smu_table.boot_values.vdd_gfx = v_3_4->bootup_vddgfx_mv;
		smu->smu_table.boot_values.cooling_id = v_3_4->coolingsolution_id;
		break;
	}

	smu->smu_table.boot_values.format_revision = header->format_revision;
	smu->smu_table.boot_values.content_revision = header->content_revision;

	smu_v13_0_1_atom_get_smu_clockinfo(smu->adev,
					(uint8_t)SMU11_SYSPLL0_SOCCLK_ID,
					(uint8_t)0,
					&smu->smu_table.boot_values.socclk);

	smu_v13_0_1_atom_get_smu_clockinfo(smu->adev,
					(uint8_t)SMU11_SYSPLL0_DCEFCLK_ID,
					(uint8_t)0,
					&smu->smu_table.boot_values.dcefclk);

	smu_v13_0_1_atom_get_smu_clockinfo(smu->adev,
					(uint8_t)SMU11_SYSPLL0_ECLK_ID,
					(uint8_t)0,
					&smu->smu_table.boot_values.eclk);

	smu_v13_0_1_atom_get_smu_clockinfo(smu->adev,
					(uint8_t)SMU11_SYSPLL0_VCLK_ID,
					(uint8_t)0,
					&smu->smu_table.boot_values.vclk);

	smu_v13_0_1_atom_get_smu_clockinfo(smu->adev,
					(uint8_t)SMU11_SYSPLL0_DCLK_ID,
					(uint8_t)0,
					&smu->smu_table.boot_values.dclk);

	if ((smu->smu_table.boot_values.format_revision == 3) &&
	    (smu->smu_table.boot_values.content_revision >= 2))
		smu_v13_0_1_atom_get_smu_clockinfo(smu->adev,
						(uint8_t)SMU11_SYSPLL1_0_FCLK_ID,
						(uint8_t)SMU11_SYSPLL1_2_ID,
						&smu->smu_table.boot_values.fclk);

	return 0;
}

int smu_v13_0_1_set_default_dpm_tables(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;

	return smu_cmn_update_table(smu, SMU_TABLE_DPMCLOCKS, 0, smu_table->clocks_table, false);
}

int smu_v13_0_1_set_driver_table_location(struct smu_context *smu)
{
	struct smu_table *driver_table = &smu->smu_table.driver_table;
	int ret = 0;

	if (!driver_table->mc_address)
		return 0;

	ret = smu_cmn_send_smc_msg_with_param(smu,
			SMU_MSG_SetDriverDramAddrHigh,
			upper_32_bits(driver_table->mc_address),
			NULL);

	if (ret)
		return ret;

	ret = smu_cmn_send_smc_msg_with_param(smu,
			SMU_MSG_SetDriverDramAddrLow,
			lower_32_bits(driver_table->mc_address),
			NULL);

	return ret;
}

int smu_v13_0_1_gfx_off_control(struct smu_context *smu, bool enable)
{
	int ret = 0;
	struct amdgpu_device *adev = smu->adev;

	switch (adev->asic_type) {
	case CHIP_YELLOW_CARP:
		if (!(adev->pm.pp_feature & PP_GFXOFF_MASK))
			return 0;
		if (enable)
			ret = smu_cmn_send_smc_msg(smu, SMU_MSG_AllowGfxOff, NULL);
		else
			ret = smu_cmn_send_smc_msg(smu, SMU_MSG_DisallowGfxOff, NULL);
		break;
	default:
		break;
	}

	return ret;
}
