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
#include "atomfirmware.h"
#include "amdgpu_atomfirmware.h"
#include "smu_v11_0.h"
#include "smu11_driver_if.h"
#include "soc15_common.h"
#include "atom.h"
#include "vega20_ppt.h"
#include "pp_thermal.h"

#include "asic_reg/thm/thm_11_0_2_offset.h"
#include "asic_reg/thm/thm_11_0_2_sh_mask.h"
#include "asic_reg/mp/mp_9_0_offset.h"
#include "asic_reg/mp/mp_9_0_sh_mask.h"
#include "asic_reg/nbio/nbio_7_4_offset.h"
#include "asic_reg/smuio/smuio_9_0_offset.h"
#include "asic_reg/smuio/smuio_9_0_sh_mask.h"

MODULE_FIRMWARE("amdgpu/vega20_smc.bin");

#define SMU11_TOOL_SIZE		0x19000
#define SMU11_THERMAL_MINIMUM_ALERT_TEMP      0
#define SMU11_THERMAL_MAXIMUM_ALERT_TEMP      255

#define SMU11_TEMPERATURE_UNITS_PER_CENTIGRADES 1000
#define SMU11_VOLTAGE_SCALE 4

#define SMC_DPM_FEATURE (FEATURE_DPM_PREFETCHER_MASK | \
			 FEATURE_DPM_GFXCLK_MASK | \
			 FEATURE_DPM_UCLK_MASK | \
			 FEATURE_DPM_SOCCLK_MASK | \
			 FEATURE_DPM_UVD_MASK | \
			 FEATURE_DPM_VCE_MASK | \
			 FEATURE_DPM_MP0CLK_MASK | \
			 FEATURE_DPM_LINK_MASK | \
			 FEATURE_DPM_DCEFCLK_MASK)

static int smu_v11_0_send_msg_without_waiting(struct smu_context *smu,
					      uint16_t msg)
{
	struct amdgpu_device *adev = smu->adev;
	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_66, msg);
	return 0;
}

static int smu_v11_0_read_arg(struct smu_context *smu, uint32_t *arg)
{
	struct amdgpu_device *adev = smu->adev;

	*arg = RREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_82);
	return 0;
}

static int smu_v11_0_wait_for_response(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t cur_value, i;

	for (i = 0; i < adev->usec_timeout; i++) {
		cur_value = RREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90);
		if ((cur_value & MP1_C2PMSG_90__CONTENT_MASK) != 0)
			break;
		udelay(1);
	}

	/* timeout means wrong logic */
	if (i == adev->usec_timeout)
		return -ETIME;

	return RREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90) == 0x1 ? 0 : -EIO;
}

static int smu_v11_0_send_msg(struct smu_context *smu, uint16_t msg)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0, index = 0;

	index = smu_msg_get_index(smu, msg);
	if (index < 0)
		return index;

	smu_v11_0_wait_for_response(smu);

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90, 0);

	smu_v11_0_send_msg_without_waiting(smu, (uint16_t)index);

	ret = smu_v11_0_wait_for_response(smu);

	if (ret)
		pr_err("Failed to send message 0x%x, response 0x%x\n", index,
		       ret);

	return ret;

}

static int
smu_v11_0_send_msg_with_param(struct smu_context *smu, uint16_t msg,
			      uint32_t param)
{

	struct amdgpu_device *adev = smu->adev;
	int ret = 0, index = 0;

	index = smu_msg_get_index(smu, msg);
	if (index < 0)
		return index;

	ret = smu_v11_0_wait_for_response(smu);
	if (ret)
		pr_err("Failed to send message 0x%x, response 0x%x, param 0x%x\n",
		       index, ret, param);

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90, 0);

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_82, param);

	smu_v11_0_send_msg_without_waiting(smu, (uint16_t)index);

	ret = smu_v11_0_wait_for_response(smu);
	if (ret)
		pr_err("Failed to send message 0x%x, response 0x%x param 0x%x\n",
		       index, ret, param);

	return ret;
}

static int smu_v11_0_init_microcode(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	const char *chip_name;
	char fw_name[30];
	int err = 0;
	const struct smc_firmware_header_v1_0 *hdr;
	const struct common_firmware_header *header;
	struct amdgpu_firmware_info *ucode = NULL;

	switch (adev->asic_type) {
	case CHIP_VEGA20:
		chip_name = "vega20";
		break;
	default:
		BUG();
	}

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_smc.bin", chip_name);

	err = request_firmware(&adev->pm.fw, fw_name, adev->dev);
	if (err)
		goto out;
	err = amdgpu_ucode_validate(adev->pm.fw);
	if (err)
		goto out;

	hdr = (const struct smc_firmware_header_v1_0 *) adev->pm.fw->data;
	amdgpu_ucode_print_smc_hdr(&hdr->header);
	adev->pm.fw_version = le32_to_cpu(hdr->header.ucode_version);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		ucode = &adev->firmware.ucode[AMDGPU_UCODE_ID_SMC];
		ucode->ucode_id = AMDGPU_UCODE_ID_SMC;
		ucode->fw = adev->pm.fw;
		header = (const struct common_firmware_header *)ucode->fw->data;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(header->ucode_size_bytes), PAGE_SIZE);
	}

out:
	if (err) {
		DRM_ERROR("smu_v11_0: Failed to load firmware \"%s\"\n",
			  fw_name);
		release_firmware(adev->pm.fw);
		adev->pm.fw = NULL;
	}
	return err;
}

static int smu_v11_0_load_microcode(struct smu_context *smu)
{
	return 0;
}

static int smu_v11_0_check_fw_status(struct smu_context *smu)
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

static int smu_v11_0_check_fw_version(struct smu_context *smu)
{
	uint32_t smu_version = 0xff;
	int ret = 0;

	ret = smu_send_smc_msg(smu, SMU_MSG_GetDriverIfVersion);
	if (ret)
		goto err;

	ret = smu_read_smc_arg(smu, &smu_version);
	if (ret)
		goto err;

	if (smu_version != smu->smc_if_version)
		ret = -EINVAL;
err:
	return ret;
}

static int smu_v11_0_read_pptable_from_vbios(struct smu_context *smu)
{
	int ret, index;
	uint16_t size;
	uint8_t frev, crev;
	void *table;

	index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
					    powerplayinfo);

	ret = smu_get_atom_data_table(smu, index, &size, &frev, &crev,
				      (uint8_t **)&table);
	if (ret)
		return ret;

	if (!smu->smu_table.power_play_table)
		smu->smu_table.power_play_table = table;
	if (!smu->smu_table.power_play_table_size)
		smu->smu_table.power_play_table_size = size;

	return 0;
}

static int smu_v11_0_init_dpm_context(struct smu_context *smu)
{
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;

	if (smu_dpm->dpm_context || smu_dpm->dpm_context_size != 0)
		return -EINVAL;

	return smu_alloc_dpm_context(smu);
}

static int smu_v11_0_fini_dpm_context(struct smu_context *smu)
{
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;

	if (!smu_dpm->dpm_context || smu_dpm->dpm_context_size == 0)
		return -EINVAL;

	kfree(smu_dpm->dpm_context);
	kfree(smu_dpm->golden_dpm_context);
	kfree(smu_dpm->dpm_current_power_state);
	kfree(smu_dpm->dpm_request_power_state);
	smu_dpm->dpm_context = NULL;
	smu_dpm->golden_dpm_context = NULL;
	smu_dpm->dpm_context_size = 0;
	smu_dpm->dpm_current_power_state = NULL;
	smu_dpm->dpm_request_power_state = NULL;

	return 0;
}

static int smu_v11_0_init_smc_tables(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = NULL;
	int ret = 0;

	if (smu_table->tables || smu_table->table_count != 0)
		return -EINVAL;

	tables = kcalloc(TABLE_COUNT, sizeof(struct smu_table), GFP_KERNEL);
	if (!tables)
		return -ENOMEM;

	smu_table->tables = tables;
	smu_table->table_count = TABLE_COUNT;

	SMU_TABLE_INIT(tables, TABLE_PPTABLE, sizeof(PPTable_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, TABLE_WATERMARKS, sizeof(Watermarks_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, TABLE_SMU_METRICS, sizeof(SmuMetrics_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, TABLE_OVERDRIVE, sizeof(OverDriveTable_t),
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, TABLE_PMSTATUSLOG, SMU11_TOOL_SIZE, PAGE_SIZE,
		       AMDGPU_GEM_DOMAIN_VRAM);
	SMU_TABLE_INIT(tables, TABLE_ACTIVITY_MONITOR_COEFF,
		       sizeof(DpmActivityMonitorCoeffInt_t),
		       PAGE_SIZE,
		       AMDGPU_GEM_DOMAIN_VRAM);

	ret = smu_v11_0_init_dpm_context(smu);
	if (ret)
		return ret;

	return 0;
}

static int smu_v11_0_fini_smc_tables(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	int ret = 0;

	if (!smu_table->tables || smu_table->table_count == 0)
		return -EINVAL;

	kfree(smu_table->tables);
	smu_table->tables = NULL;
	smu_table->table_count = 0;

	ret = smu_v11_0_fini_dpm_context(smu);
	if (ret)
		return ret;
	return 0;
}

static int smu_v11_0_init_power(struct smu_context *smu)
{
	struct smu_power_context *smu_power = &smu->smu_power;

	if (smu_power->power_context || smu_power->power_context_size != 0)
		return -EINVAL;

	smu_power->power_context = kzalloc(sizeof(struct smu_11_0_dpm_context),
					   GFP_KERNEL);
	if (!smu_power->power_context)
		return -ENOMEM;
	smu_power->power_context_size = sizeof(struct smu_11_0_dpm_context);

	return 0;
}

static int smu_v11_0_fini_power(struct smu_context *smu)
{
	struct smu_power_context *smu_power = &smu->smu_power;

	if (!smu_power->power_context || smu_power->power_context_size == 0)
		return -EINVAL;

	kfree(smu_power->power_context);
	smu_power->power_context = NULL;
	smu_power->power_context_size = 0;

	return 0;
}

int smu_v11_0_get_vbios_bootup_values(struct smu_context *smu)
{
	int ret, index;
	uint16_t size;
	uint8_t frev, crev;
	struct atom_common_table_header *header;
	struct atom_firmware_info_v3_3 *v_3_3;
	struct atom_firmware_info_v3_1 *v_3_1;

	index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
					    firmwareinfo);

	ret = smu_get_atom_data_table(smu, index, &size, &frev, &crev,
				      (uint8_t **)&header);
	if (ret)
		return ret;

	if (header->format_revision != 3) {
		pr_err("unknown atom_firmware_info version! for smu11\n");
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
		break;
	case 3:
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
	}

	return 0;
}

static int smu_v11_0_get_clk_info_from_vbios(struct smu_context *smu)
{
	int ret, index;
	struct amdgpu_device *adev = smu->adev;
	struct atom_get_smu_clock_info_parameters_v3_1 input = {0};
	struct atom_get_smu_clock_info_output_parameters_v3_1 *output;

	input.clk_id = SMU11_SYSPLL0_SOCCLK_ID;
	input.command = GET_SMU_CLOCK_INFO_V3_1_GET_CLOCK_FREQ;
	index = get_index_into_master_table(atom_master_list_of_command_functions_v2_1,
					    getsmuclockinfo);

	ret = amdgpu_atom_execute_table(adev->mode_info.atom_context, index,
					(uint32_t *)&input);
	if (ret)
		return -EINVAL;

	output = (struct atom_get_smu_clock_info_output_parameters_v3_1 *)&input;
	smu->smu_table.boot_values.socclk = le32_to_cpu(output->atom_smu_outputclkfreq.smu_clock_freq_hz) / 10000;

	memset(&input, 0, sizeof(input));
	input.clk_id = SMU11_SYSPLL0_DCEFCLK_ID;
	input.command = GET_SMU_CLOCK_INFO_V3_1_GET_CLOCK_FREQ;
	index = get_index_into_master_table(atom_master_list_of_command_functions_v2_1,
					    getsmuclockinfo);

	ret = amdgpu_atom_execute_table(adev->mode_info.atom_context, index,
					(uint32_t *)&input);
	if (ret)
		return -EINVAL;

	output = (struct atom_get_smu_clock_info_output_parameters_v3_1 *)&input;
	smu->smu_table.boot_values.dcefclk = le32_to_cpu(output->atom_smu_outputclkfreq.smu_clock_freq_hz) / 10000;

	memset(&input, 0, sizeof(input));
	input.clk_id = SMU11_SYSPLL0_ECLK_ID;
	input.command = GET_SMU_CLOCK_INFO_V3_1_GET_CLOCK_FREQ;
	index = get_index_into_master_table(atom_master_list_of_command_functions_v2_1,
					    getsmuclockinfo);

	ret = amdgpu_atom_execute_table(adev->mode_info.atom_context, index,
					(uint32_t *)&input);
	if (ret)
		return -EINVAL;

	output = (struct atom_get_smu_clock_info_output_parameters_v3_1 *)&input;
	smu->smu_table.boot_values.eclk = le32_to_cpu(output->atom_smu_outputclkfreq.smu_clock_freq_hz) / 10000;

	memset(&input, 0, sizeof(input));
	input.clk_id = SMU11_SYSPLL0_VCLK_ID;
	input.command = GET_SMU_CLOCK_INFO_V3_1_GET_CLOCK_FREQ;
	index = get_index_into_master_table(atom_master_list_of_command_functions_v2_1,
					    getsmuclockinfo);

	ret = amdgpu_atom_execute_table(adev->mode_info.atom_context, index,
					(uint32_t *)&input);
	if (ret)
		return -EINVAL;

	output = (struct atom_get_smu_clock_info_output_parameters_v3_1 *)&input;
	smu->smu_table.boot_values.vclk = le32_to_cpu(output->atom_smu_outputclkfreq.smu_clock_freq_hz) / 10000;

	memset(&input, 0, sizeof(input));
	input.clk_id = SMU11_SYSPLL0_DCLK_ID;
	input.command = GET_SMU_CLOCK_INFO_V3_1_GET_CLOCK_FREQ;
	index = get_index_into_master_table(atom_master_list_of_command_functions_v2_1,
					    getsmuclockinfo);

	ret = amdgpu_atom_execute_table(adev->mode_info.atom_context, index,
					(uint32_t *)&input);
	if (ret)
		return -EINVAL;

	output = (struct atom_get_smu_clock_info_output_parameters_v3_1 *)&input;
	smu->smu_table.boot_values.dclk = le32_to_cpu(output->atom_smu_outputclkfreq.smu_clock_freq_hz) / 10000;

	return 0;
}

static int smu_v11_0_notify_memory_pool_location(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *memory_pool = &smu_table->memory_pool;
	int ret = 0;
	uint64_t address;
	uint32_t address_low, address_high;

	if (memory_pool->size == 0 || memory_pool->cpu_addr == NULL)
		return ret;

	address = (uintptr_t)memory_pool->cpu_addr;
	address_high = (uint32_t)upper_32_bits(address);
	address_low  = (uint32_t)lower_32_bits(address);

	ret = smu_send_smc_msg_with_param(smu,
					  SMU_MSG_SetSystemVirtualDramAddrHigh,
					  address_high);
	if (ret)
		return ret;
	ret = smu_send_smc_msg_with_param(smu,
					  SMU_MSG_SetSystemVirtualDramAddrLow,
					  address_low);
	if (ret)
		return ret;

	address = memory_pool->mc_address;
	address_high = (uint32_t)upper_32_bits(address);
	address_low  = (uint32_t)lower_32_bits(address);

	ret = smu_send_smc_msg_with_param(smu, SMU_MSG_DramLogSetDramAddrHigh,
					  address_high);
	if (ret)
		return ret;
	ret = smu_send_smc_msg_with_param(smu, SMU_MSG_DramLogSetDramAddrLow,
					  address_low);
	if (ret)
		return ret;
	ret = smu_send_smc_msg_with_param(smu, SMU_MSG_DramLogSetDramSize,
					  (uint32_t)memory_pool->size);
	if (ret)
		return ret;

	return ret;
}

static int smu_v11_0_check_pptable(struct smu_context *smu)
{
	int ret;

	ret = smu_check_powerplay_table(smu);
	return ret;
}

static int smu_v11_0_parse_pptable(struct smu_context *smu)
{
	int ret;

	struct smu_table_context *table_context = &smu->smu_table;

	if (table_context->driver_pptable)
		return -EINVAL;

	table_context->driver_pptable = kzalloc(sizeof(PPTable_t), GFP_KERNEL);

	if (!table_context->driver_pptable)
		return -ENOMEM;

	ret = smu_store_powerplay_table(smu);
	if (ret)
		return -EINVAL;

	ret = smu_append_powerplay_table(smu);

	return ret;
}

static int smu_v11_0_populate_smc_pptable(struct smu_context *smu)
{
	int ret;

	ret = smu_set_default_dpm_table(smu);

	return ret;
}

static int smu_v11_0_write_pptable(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	int ret = 0;

	ret = smu_update_table(smu, TABLE_PPTABLE, table_context->driver_pptable, true);

	return ret;
}

static int smu_v11_0_write_watermarks_table(struct smu_context *smu)
{
	return smu_update_table(smu, TABLE_WATERMARKS,
				smu->smu_table.tables[TABLE_WATERMARKS].cpu_addr, true);
}

static int smu_v11_0_set_deep_sleep_dcefclk(struct smu_context *smu, uint32_t clk)
{
	int ret;

	ret = smu_send_smc_msg_with_param(smu,
					  SMU_MSG_SetMinDeepSleepDcefclk, clk);
	if (ret)
		pr_err("SMU11 attempt to set divider for DCEFCLK Failed!");

	return ret;
}

static int smu_v11_0_set_min_dcef_deep_sleep(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;

	if (!table_context)
		return -EINVAL;

	return smu_set_deep_sleep_dcefclk(smu,
					  table_context->boot_values.dcefclk / 100);
}

static int smu_v11_0_set_tool_table_location(struct smu_context *smu)
{
	int ret = 0;
	struct smu_table *tool_table = &smu->smu_table.tables[TABLE_PMSTATUSLOG];

	if (tool_table->mc_address) {
		ret = smu_send_smc_msg_with_param(smu,
				SMU_MSG_SetToolsDramAddrHigh,
				upper_32_bits(tool_table->mc_address));
		if (!ret)
			ret = smu_send_smc_msg_with_param(smu,
				SMU_MSG_SetToolsDramAddrLow,
				lower_32_bits(tool_table->mc_address));
	}

	return ret;
}

static int smu_v11_0_init_display(struct smu_context *smu)
{
	int ret = 0;
	ret = smu_send_smc_msg_with_param(smu, SMU_MSG_NumOfDisplays, 0);
	return ret;
}

static int smu_v11_0_update_feature_enable_state(struct smu_context *smu, uint32_t feature_id, bool enabled)
{
	uint32_t feature_low = 0, feature_high = 0;
	int ret = 0;

	if (feature_id >= 0 && feature_id < 31)
		feature_low = (1 << feature_id);
	else if (feature_id > 31 && feature_id < 63)
		feature_high = (1 << feature_id);
	else
		return -EINVAL;

	if (enabled) {
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_EnableSmuFeaturesLow,
						  feature_low);
		if (ret)
			return ret;
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_EnableSmuFeaturesHigh,
						  feature_high);
		if (ret)
			return ret;

	} else {
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_DisableSmuFeaturesLow,
						  feature_low);
		if (ret)
			return ret;
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_DisableSmuFeaturesHigh,
						  feature_high);
		if (ret)
			return ret;

	}

	return ret;
}

static int smu_v11_0_set_allowed_mask(struct smu_context *smu)
{
	struct smu_feature *feature = &smu->smu_feature;
	int ret = 0;
	uint32_t feature_mask[2];

	mutex_lock(&feature->mutex);
	if (bitmap_empty(feature->allowed, SMU_FEATURE_MAX) || feature->feature_num < 64)
		goto failed;

	bitmap_copy((unsigned long *)feature_mask, feature->allowed, 64);

	ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetAllowedFeaturesMaskHigh,
					  feature_mask[1]);
	if (ret)
		goto failed;

	ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetAllowedFeaturesMaskLow,
					  feature_mask[0]);
	if (ret)
		goto failed;

failed:
	mutex_unlock(&feature->mutex);
	return ret;
}

static int smu_v11_0_get_enabled_mask(struct smu_context *smu,
				      uint32_t *feature_mask, uint32_t num)
{
	uint32_t feature_mask_high = 0, feature_mask_low = 0;
	int ret = 0;

	if (!feature_mask || num < 2)
		return -EINVAL;

	ret = smu_send_smc_msg(smu, SMU_MSG_GetEnabledSmuFeaturesHigh);
	if (ret)
		return ret;
	ret = smu_read_smc_arg(smu, &feature_mask_high);
	if (ret)
		return ret;

	ret = smu_send_smc_msg(smu, SMU_MSG_GetEnabledSmuFeaturesLow);
	if (ret)
		return ret;
	ret = smu_read_smc_arg(smu, &feature_mask_low);
	if (ret)
		return ret;

	feature_mask[0] = feature_mask_low;
	feature_mask[1] = feature_mask_high;

	return ret;
}

static bool smu_v11_0_is_dpm_running(struct smu_context *smu)
{
	int ret = 0;
	uint32_t feature_mask[2];
	unsigned long feature_enabled;
	ret = smu_v11_0_get_enabled_mask(smu, feature_mask, 2);
	feature_enabled = (unsigned long)((uint64_t)feature_mask[0] |
			   ((uint64_t)feature_mask[1] << 32));
	return !!(feature_enabled & SMC_DPM_FEATURE);
}

static int smu_v11_0_system_features_control(struct smu_context *smu,
					     bool en)
{
	struct smu_feature *feature = &smu->smu_feature;
	uint32_t feature_mask[2];
	int ret = 0;

	ret = smu_send_smc_msg(smu, (en ? SMU_MSG_EnableAllSmuFeatures :
				     SMU_MSG_DisableAllSmuFeatures));
	if (ret)
		return ret;
	ret = smu_feature_get_enabled_mask(smu, feature_mask, 2);
	if (ret)
		return ret;

	bitmap_copy(feature->enabled, (unsigned long *)&feature_mask,
		    feature->feature_num);
	bitmap_copy(feature->supported, (unsigned long *)&feature_mask,
		    feature->feature_num);

	return ret;
}

static int smu_v11_0_notify_display_change(struct smu_context *smu)
{
	int ret = 0;

	if (smu_feature_is_enabled(smu, FEATURE_DPM_UCLK_BIT))
	    ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetUclkFastSwitch, 1);

	return ret;
}

static int
smu_v11_0_get_max_sustainable_clock(struct smu_context *smu, uint32_t *clock,
				    PPCLK_e clock_select)
{
	int ret = 0;

	ret = smu_send_smc_msg_with_param(smu, SMU_MSG_GetDcModeMaxDpmFreq,
					  clock_select << 16);
	if (ret) {
		pr_err("[GetMaxSustainableClock] Failed to get max DC clock from SMC!");
		return ret;
	}

	ret = smu_read_smc_arg(smu, clock);
	if (ret)
		return ret;

	if (*clock != 0)
		return 0;

	/* if DC limit is zero, return AC limit */
	ret = smu_send_smc_msg_with_param(smu, SMU_MSG_GetMaxDpmFreq,
					  clock_select << 16);
	if (ret) {
		pr_err("[GetMaxSustainableClock] failed to get max AC clock from SMC!");
		return ret;
	}

	ret = smu_read_smc_arg(smu, clock);

	return ret;
}

static int smu_v11_0_init_max_sustainable_clocks(struct smu_context *smu)
{
	struct smu_11_0_max_sustainable_clocks *max_sustainable_clocks;
	int ret = 0;

	max_sustainable_clocks = kzalloc(sizeof(struct smu_11_0_max_sustainable_clocks),
					 GFP_KERNEL);
	smu->smu_table.max_sustainable_clocks = (void *)max_sustainable_clocks;

	max_sustainable_clocks->uclock = smu->smu_table.boot_values.uclk / 100;
	max_sustainable_clocks->soc_clock = smu->smu_table.boot_values.socclk / 100;
	max_sustainable_clocks->dcef_clock = smu->smu_table.boot_values.dcefclk / 100;
	max_sustainable_clocks->display_clock = 0xFFFFFFFF;
	max_sustainable_clocks->phy_clock = 0xFFFFFFFF;
	max_sustainable_clocks->pixel_clock = 0xFFFFFFFF;

	if (smu_feature_is_enabled(smu, FEATURE_DPM_UCLK_BIT)) {
		ret = smu_v11_0_get_max_sustainable_clock(smu,
							  &(max_sustainable_clocks->uclock),
							  PPCLK_UCLK);
		if (ret) {
			pr_err("[%s] failed to get max UCLK from SMC!",
			       __func__);
			return ret;
		}
	}

	if (smu_feature_is_enabled(smu, FEATURE_DPM_SOCCLK_BIT)) {
		ret = smu_v11_0_get_max_sustainable_clock(smu,
							  &(max_sustainable_clocks->soc_clock),
							  PPCLK_SOCCLK);
		if (ret) {
			pr_err("[%s] failed to get max SOCCLK from SMC!",
			       __func__);
			return ret;
		}
	}

	if (smu_feature_is_enabled(smu, FEATURE_DPM_DCEFCLK_BIT)) {
		ret = smu_v11_0_get_max_sustainable_clock(smu,
							  &(max_sustainable_clocks->dcef_clock),
							  PPCLK_DCEFCLK);
		if (ret) {
			pr_err("[%s] failed to get max DCEFCLK from SMC!",
			       __func__);
			return ret;
		}

		ret = smu_v11_0_get_max_sustainable_clock(smu,
							  &(max_sustainable_clocks->display_clock),
							  PPCLK_DISPCLK);
		if (ret) {
			pr_err("[%s] failed to get max DISPCLK from SMC!",
			       __func__);
			return ret;
		}
		ret = smu_v11_0_get_max_sustainable_clock(smu,
							  &(max_sustainable_clocks->phy_clock),
							  PPCLK_PHYCLK);
		if (ret) {
			pr_err("[%s] failed to get max PHYCLK from SMC!",
			       __func__);
			return ret;
		}
		ret = smu_v11_0_get_max_sustainable_clock(smu,
							  &(max_sustainable_clocks->pixel_clock),
							  PPCLK_PIXCLK);
		if (ret) {
			pr_err("[%s] failed to get max PIXCLK from SMC!",
			       __func__);
			return ret;
		}
	}

	if (max_sustainable_clocks->soc_clock < max_sustainable_clocks->uclock)
		max_sustainable_clocks->uclock = max_sustainable_clocks->soc_clock;

	return 0;
}

static int smu_v11_0_get_power_limit(struct smu_context *smu,
				     uint32_t *limit,
				     bool get_default)
{
	int ret = 0;

	if (get_default) {
		mutex_lock(&smu->mutex);
		*limit = smu->default_power_limit;
		if (smu->od_enabled) {
			*limit *= (100 + smu->smu_table.TDPODLimit);
			*limit /= 100;
		}
		mutex_unlock(&smu->mutex);
	} else {
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_GetPptLimit,
						  POWER_SOURCE_AC << 16);
		if (ret) {
			pr_err("[%s] get PPT limit failed!", __func__);
			return ret;
		}
		smu_read_smc_arg(smu, limit);
		smu->power_limit = *limit;
	}

	return ret;
}

static int smu_v11_0_set_power_limit(struct smu_context *smu, uint32_t n)
{
	uint32_t max_power_limit;
	int ret = 0;

	if (n == 0)
		n = smu->default_power_limit;

	max_power_limit = smu->default_power_limit;

	if (smu->od_enabled) {
		max_power_limit *= (100 + smu->smu_table.TDPODLimit);
		max_power_limit /= 100;
	}

	if (smu_feature_is_enabled(smu, FEATURE_PPT_BIT))
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetPptLimit, n);
	if (ret) {
		pr_err("[%s] Set power limit Failed!", __func__);
		return ret;
	}

	return ret;
}

static int smu_v11_0_get_current_clk_freq(struct smu_context *smu, uint32_t clk_id, uint32_t *value)
{
	int ret = 0;
	uint32_t freq;

	if (clk_id >= PPCLK_COUNT || !value)
		return -EINVAL;

	ret = smu_send_smc_msg_with_param(smu,
			SMU_MSG_GetDpmClockFreq, (clk_id << 16));
	if (ret)
		return ret;

	ret = smu_read_smc_arg(smu, &freq);
	if (ret)
		return ret;

	freq *= 100;
	*value = freq;

	return ret;
}

static int smu_v11_0_get_thermal_range(struct smu_context *smu,
				struct PP_TemperatureRange *range)
{
	memcpy(range, &SMU7ThermalWithDelayPolicy[0], sizeof(struct PP_TemperatureRange));

	range->max = smu->smu_table.software_shutdown_temp *
		PP_TEMPERATURE_UNITS_PER_CENTIGRADES;

	return 0;
}

static int smu_v11_0_set_thermal_range(struct smu_context *smu,
			struct PP_TemperatureRange *range)
{
	struct amdgpu_device *adev = smu->adev;
	int low = SMU11_THERMAL_MINIMUM_ALERT_TEMP *
		PP_TEMPERATURE_UNITS_PER_CENTIGRADES;
	int high = SMU11_THERMAL_MAXIMUM_ALERT_TEMP *
		PP_TEMPERATURE_UNITS_PER_CENTIGRADES;
	uint32_t val;

	if (low < range->min)
		low = range->min;
	if (high > range->max)
		high = range->max;

	if (low > high)
		return -EINVAL;

	val = RREG32_SOC15(THM, 0, mmTHM_THERMAL_INT_CTRL);
	val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, MAX_IH_CREDIT, 5);
	val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, THERM_IH_HW_ENA, 1);
	val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, DIG_THERM_INTH, (high / PP_TEMPERATURE_UNITS_PER_CENTIGRADES));
	val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, DIG_THERM_INTL, (low / PP_TEMPERATURE_UNITS_PER_CENTIGRADES));
	val = val & (~THM_THERMAL_INT_CTRL__THERM_TRIGGER_MASK_MASK);

	WREG32_SOC15(THM, 0, mmTHM_THERMAL_INT_CTRL, val);

	return 0;
}

static int smu_v11_0_enable_thermal_alert(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t val = 0;

	val |= (1 << THM_THERMAL_INT_ENA__THERM_INTH_CLR__SHIFT);
	val |= (1 << THM_THERMAL_INT_ENA__THERM_INTL_CLR__SHIFT);
	val |= (1 << THM_THERMAL_INT_ENA__THERM_TRIGGER_CLR__SHIFT);

	WREG32_SOC15(THM, 0, mmTHM_THERMAL_INT_ENA, val);

	return 0;
}

static int smu_v11_0_set_thermal_fan_table(struct smu_context *smu)
{
	int ret;
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_t *pptable = table_context->driver_pptable;

	ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetFanTemperatureTarget,
			(uint32_t)pptable->FanTargetTemperature);

	return ret;
}

static int smu_v11_0_start_thermal_control(struct smu_context *smu)
{
	int ret = 0;
	struct PP_TemperatureRange range;
	struct amdgpu_device *adev = smu->adev;

	smu_v11_0_get_thermal_range(smu, &range);

	if (smu->smu_table.thermal_controller_type) {
		ret = smu_v11_0_set_thermal_range(smu, &range);
		if (ret)
			return ret;

		ret = smu_v11_0_enable_thermal_alert(smu);
		if (ret)
			return ret;
		ret = smu_v11_0_set_thermal_fan_table(smu);
		if (ret)
			return ret;
	}

	adev->pm.dpm.thermal.min_temp = range.min;
	adev->pm.dpm.thermal.max_temp = range.max;

	return ret;
}

static int smu_v11_0_get_current_activity_percent(struct smu_context *smu,
						  uint32_t *value)
{
	int ret = 0;
	SmuMetrics_t metrics;

	if (!value)
		return -EINVAL;

	ret = smu_update_table(smu, TABLE_SMU_METRICS, (void *)&metrics, false);
	if (ret)
		return ret;

	*value = metrics.AverageGfxActivity;

	return 0;
}

static int smu_v11_0_thermal_get_temperature(struct smu_context *smu, uint32_t *value)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t temp = 0;

	if (!value)
		return -EINVAL;

	temp = RREG32_SOC15(THM, 0, mmCG_MULT_THERMAL_STATUS);
	temp = (temp & CG_MULT_THERMAL_STATUS__CTF_TEMP_MASK) >>
			CG_MULT_THERMAL_STATUS__CTF_TEMP__SHIFT;

	temp = temp & 0x1ff;
	temp *= SMU11_TEMPERATURE_UNITS_PER_CENTIGRADES;

	*value = temp;

	return 0;
}

static int smu_v11_0_get_gpu_power(struct smu_context *smu, uint32_t *value)
{
	int ret = 0;
	SmuMetrics_t metrics;

	if (!value)
		return -EINVAL;

	ret = smu_update_table(smu, TABLE_SMU_METRICS, (void *)&metrics, false);
	if (ret)
		return ret;

	*value = metrics.CurrSocketPower << 8;

	return 0;
}

static uint16_t convert_to_vddc(uint8_t vid)
{
	return (uint16_t) ((6200 - (vid * 25)) / SMU11_VOLTAGE_SCALE);
}

static int smu_v11_0_get_gfx_vdd(struct smu_context *smu, uint32_t *value)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t vdd = 0, val_vid = 0;

	if (!value)
		return -EINVAL;
	val_vid = (RREG32_SOC15(SMUIO, 0, mmSMUSVI0_TEL_PLANE0) &
		SMUSVI0_TEL_PLANE0__SVI0_PLANE0_VDDCOR_MASK) >>
		SMUSVI0_TEL_PLANE0__SVI0_PLANE0_VDDCOR__SHIFT;

	vdd = (uint32_t)convert_to_vddc((uint8_t)val_vid);

	*value = vdd;

	return 0;

}

static int smu_v11_0_read_sensor(struct smu_context *smu,
				 enum amd_pp_sensors sensor,
				 void *data, uint32_t *size)
{
	struct smu_table_context *table_context = &smu->smu_table;
	PPTable_t *pptable = table_context->driver_pptable;
	int ret = 0;
	switch (sensor) {
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		ret = smu_v11_0_get_current_activity_percent(smu,
							     (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GFX_MCLK:
		ret = smu_get_current_clk_freq(smu, PPCLK_UCLK, (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GFX_SCLK:
		ret = smu_get_current_clk_freq(smu, PPCLK_GFXCLK, (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_TEMP:
		ret = smu_v11_0_thermal_get_temperature(smu, (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_POWER:
		ret = smu_v11_0_get_gpu_power(smu, (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_VDDGFX:
		ret = smu_v11_0_get_gfx_vdd(smu, (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_UVD_POWER:
		*(uint32_t *)data = smu_feature_is_enabled(smu, FEATURE_DPM_UVD_BIT) ? 1 : 0;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_VCE_POWER:
		*(uint32_t *)data = smu_feature_is_enabled(smu, FEATURE_DPM_VCE_BIT) ? 1 : 0;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_MIN_FAN_RPM:
		*(uint32_t *)data = 0;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_MAX_FAN_RPM:
		*(uint32_t *)data = pptable->FanMaximumRpm;
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

static int
smu_v11_0_display_clock_voltage_request(struct smu_context *smu,
					struct pp_display_clock_request
					*clock_req)
{
	enum amd_pp_clock_type clk_type = clock_req->clock_type;
	int ret = 0;
	PPCLK_e clk_select = 0;
	uint32_t clk_freq = clock_req->clock_freq_in_khz / 1000;

	if (smu_feature_is_enabled(smu, FEATURE_DPM_DCEFCLK_BIT)) {
		switch (clk_type) {
		case amd_pp_dcef_clock:
			clk_select = PPCLK_DCEFCLK;
			break;
		case amd_pp_disp_clock:
			clk_select = PPCLK_DISPCLK;
			break;
		case amd_pp_pixel_clock:
			clk_select = PPCLK_PIXCLK;
			break;
		case amd_pp_phy_clock:
			clk_select = PPCLK_PHYCLK;
			break;
		default:
			pr_info("[%s] Invalid Clock Type!", __func__);
			ret = -EINVAL;
			break;
		}

		if (ret)
			goto failed;

		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetHardMinByFreq,
						  (clk_select << 16) | clk_freq);
	}

failed:
	return ret;
}

static int smu_v11_0_set_watermarks_table(struct smu_context *smu,
					  Watermarks_t *table, struct
					  dm_pp_wm_sets_with_clock_ranges_soc15
					  *clock_ranges)
{
	int i;

	if (!table || !clock_ranges)
		return -EINVAL;

	if (clock_ranges->num_wm_dmif_sets > 4 ||
	    clock_ranges->num_wm_mcif_sets > 4)
                return -EINVAL;

        for (i = 0; i < clock_ranges->num_wm_dmif_sets; i++) {
		table->WatermarkRow[1][i].MinClock =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_dmif_clocks_ranges[i].wm_min_dcfclk_clk_in_khz /
			1000));
		table->WatermarkRow[1][i].MaxClock =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_dmif_clocks_ranges[i].wm_max_dcfclk_clk_in_khz /
			1000));
		table->WatermarkRow[1][i].MinUclk =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_dmif_clocks_ranges[i].wm_min_mem_clk_in_khz /
			1000));
		table->WatermarkRow[1][i].MaxUclk =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_dmif_clocks_ranges[i].wm_max_mem_clk_in_khz /
			1000));
		table->WatermarkRow[1][i].WmSetting = (uint8_t)
				clock_ranges->wm_dmif_clocks_ranges[i].wm_set_id;
        }

	for (i = 0; i < clock_ranges->num_wm_mcif_sets; i++) {
		table->WatermarkRow[0][i].MinClock =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_mcif_clocks_ranges[i].wm_min_socclk_clk_in_khz /
			1000));
		table->WatermarkRow[0][i].MaxClock =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_mcif_clocks_ranges[i].wm_max_socclk_clk_in_khz /
			1000));
		table->WatermarkRow[0][i].MinUclk =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_mcif_clocks_ranges[i].wm_min_mem_clk_in_khz /
			1000));
		table->WatermarkRow[0][i].MaxUclk =
			cpu_to_le16((uint16_t)
			(clock_ranges->wm_mcif_clocks_ranges[i].wm_max_mem_clk_in_khz /
			1000));
		table->WatermarkRow[0][i].WmSetting = (uint8_t)
				clock_ranges->wm_mcif_clocks_ranges[i].wm_set_id;
        }

	return 0;
}

static int
smu_v11_0_set_watermarks_for_clock_ranges(struct smu_context *smu, struct
					  dm_pp_wm_sets_with_clock_ranges_soc15
					  *clock_ranges)
{
	int ret = 0;
	struct smu_table *watermarks = &smu->smu_table.tables[TABLE_WATERMARKS];
	Watermarks_t *table = watermarks->cpu_addr;

	if (!smu->disable_watermark &&
	    smu_feature_is_enabled(smu, FEATURE_DPM_DCEFCLK_BIT) &&
	    smu_feature_is_enabled(smu, FEATURE_DPM_SOCCLK_BIT)) {
		smu_v11_0_set_watermarks_table(smu, table, clock_ranges);
		smu->watermarks_bitmap |= WATERMARKS_EXIST;
		smu->watermarks_bitmap &= ~WATERMARKS_LOADED;
	}

	return ret;
}

static int smu_v11_0_get_clock_ranges(struct smu_context *smu,
				      uint32_t *clock,
				      PPCLK_e clock_select,
				      bool max)
{
	int ret;
	*clock = 0;
	if (max) {
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_GetMaxDpmFreq,
					    (clock_select << 16));
		if (ret) {
			pr_err("[GetClockRanges] Failed to get max clock from SMC!\n");
			return ret;
		}
		smu_read_smc_arg(smu, clock);
	} else {
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_GetMinDpmFreq,
					    (clock_select << 16));
		if (ret) {
			pr_err("[GetClockRanges] Failed to get min clock from SMC!\n");
			return ret;
		}
		smu_read_smc_arg(smu, clock);
	}

	return 0;
}

static uint32_t smu_v11_0_dpm_get_sclk(struct smu_context *smu, bool low)
{
	uint32_t gfx_clk;
	int ret;

	if (!smu_feature_is_enabled(smu, FEATURE_DPM_GFXCLK_BIT)) {
		pr_err("[GetSclks]: gfxclk dpm not enabled!\n");
		return -EPERM;
	}

	if (low) {
		ret = smu_v11_0_get_clock_ranges(smu, &gfx_clk, PPCLK_GFXCLK, false);
		if (ret) {
			pr_err("[GetSclks]: fail to get min PPCLK_GFXCLK\n");
			return ret;
		}
	} else {
		ret = smu_v11_0_get_clock_ranges(smu, &gfx_clk, PPCLK_GFXCLK, true);
		if (ret) {
			pr_err("[GetSclks]: fail to get max PPCLK_GFXCLK\n");
			return ret;
		}
	}

	return (gfx_clk * 100);
}

static uint32_t smu_v11_0_dpm_get_mclk(struct smu_context *smu, bool low)
{
	uint32_t mem_clk;
	int ret;

	if (!smu_feature_is_enabled(smu, FEATURE_DPM_UCLK_BIT)) {
		pr_err("[GetMclks]: memclk dpm not enabled!\n");
		return -EPERM;
	}

	if (low) {
		ret = smu_v11_0_get_clock_ranges(smu, &mem_clk, PPCLK_UCLK, false);
		if (ret) {
			pr_err("[GetMclks]: fail to get min PPCLK_UCLK\n");
			return ret;
		}
	} else {
		ret = smu_v11_0_get_clock_ranges(smu, &mem_clk, PPCLK_GFXCLK, true);
		if (ret) {
			pr_err("[GetMclks]: fail to get max PPCLK_UCLK\n");
			return ret;
		}
	}

	return (mem_clk * 100);
}

static int smu_v11_0_set_od8_default_settings(struct smu_context *smu,
					      bool initialize)
{
	struct smu_table_context *table_context = &smu->smu_table;
	int ret;

	if (initialize) {
		if (table_context->overdrive_table)
			return -EINVAL;

		table_context->overdrive_table = kzalloc(sizeof(OverDriveTable_t), GFP_KERNEL);

		if (!table_context->overdrive_table)
			return -ENOMEM;

		ret = smu_update_table(smu, TABLE_OVERDRIVE, table_context->overdrive_table, false);
		if (ret) {
			pr_err("Failed to export over drive table!\n");
			return ret;
		}

		smu_set_default_od8_settings(smu);
	}

	ret = smu_update_table(smu, TABLE_OVERDRIVE, table_context->overdrive_table, true);
	if (ret) {
		pr_err("Failed to import over drive table!\n");
		return ret;
	}

	return 0;
}

static int smu_v11_0_conv_power_profile_to_pplib_workload(int power_profile)
{
	int pplib_workload = 0;

	switch (power_profile) {
	case PP_SMC_POWER_PROFILE_BOOTUP_DEFAULT:
	     pplib_workload = WORKLOAD_DEFAULT_BIT;
	     break;
	case PP_SMC_POWER_PROFILE_FULLSCREEN3D:
	     pplib_workload = WORKLOAD_PPLIB_FULL_SCREEN_3D_BIT;
	     break;
	case PP_SMC_POWER_PROFILE_POWERSAVING:
	     pplib_workload = WORKLOAD_PPLIB_POWER_SAVING_BIT;
	     break;
	case PP_SMC_POWER_PROFILE_VIDEO:
	     pplib_workload = WORKLOAD_PPLIB_VIDEO_BIT;
	     break;
	case PP_SMC_POWER_PROFILE_VR:
	     pplib_workload = WORKLOAD_PPLIB_VR_BIT;
	     break;
	case PP_SMC_POWER_PROFILE_COMPUTE:
	     pplib_workload = WORKLOAD_PPLIB_COMPUTE_BIT;
	     break;
	case PP_SMC_POWER_PROFILE_CUSTOM:
		pplib_workload = WORKLOAD_PPLIB_CUSTOM_BIT;
		break;
	}

	return pplib_workload;
}

static int smu_v11_0_get_power_profile_mode(struct smu_context *smu, char *buf)
{
	DpmActivityMonitorCoeffInt_t activity_monitor;
	uint32_t i, size = 0;
	uint16_t workload_type = 0;
	static const char *profile_name[] = {
					"BOOTUP_DEFAULT",
					"3D_FULL_SCREEN",
					"POWER_SAVING",
					"VIDEO",
					"VR",
					"COMPUTE",
					"CUSTOM"};
	static const char *title[] = {
			"PROFILE_INDEX(NAME)",
			"CLOCK_TYPE(NAME)",
			"FPS",
			"UseRlcBusy",
			"MinActiveFreqType",
			"MinActiveFreq",
			"BoosterFreqType",
			"BoosterFreq",
			"PD_Data_limit_c",
			"PD_Data_error_coeff",
			"PD_Data_error_rate_coeff"};
	int result = 0;

	if (!buf)
		return -EINVAL;

	size += sprintf(buf + size, "%16s %s %s %s %s %s %s %s %s %s %s\n",
			title[0], title[1], title[2], title[3], title[4], title[5],
			title[6], title[7], title[8], title[9], title[10]);

	for (i = 0; i <= PP_SMC_POWER_PROFILE_CUSTOM; i++) {
		/* conv PP_SMC_POWER_PROFILE* to WORKLOAD_PPLIB_*_BIT */
		workload_type = smu_v11_0_conv_power_profile_to_pplib_workload(i);
		result = smu_update_table_with_arg(smu, TABLE_ACTIVITY_MONITOR_COEFF,
						   workload_type, &activity_monitor, false);
		if (result) {
			pr_err("[%s] Failed to get activity monitor!", __func__);
			return result;
		}

		size += sprintf(buf + size, "%2d %14s%s:\n",
			i, profile_name[i], (i == smu->power_profile_mode) ? "*" : " ");

		size += sprintf(buf + size, "%19s %d(%13s) %7d %7d %7d %7d %7d %7d %7d %7d %7d\n",
			" ",
			0,
			"GFXCLK",
			activity_monitor.Gfx_FPS,
			activity_monitor.Gfx_UseRlcBusy,
			activity_monitor.Gfx_MinActiveFreqType,
			activity_monitor.Gfx_MinActiveFreq,
			activity_monitor.Gfx_BoosterFreqType,
			activity_monitor.Gfx_BoosterFreq,
			activity_monitor.Gfx_PD_Data_limit_c,
			activity_monitor.Gfx_PD_Data_error_coeff,
			activity_monitor.Gfx_PD_Data_error_rate_coeff);

		size += sprintf(buf + size, "%19s %d(%13s) %7d %7d %7d %7d %7d %7d %7d %7d %7d\n",
			" ",
			1,
			"SOCCLK",
			activity_monitor.Soc_FPS,
			activity_monitor.Soc_UseRlcBusy,
			activity_monitor.Soc_MinActiveFreqType,
			activity_monitor.Soc_MinActiveFreq,
			activity_monitor.Soc_BoosterFreqType,
			activity_monitor.Soc_BoosterFreq,
			activity_monitor.Soc_PD_Data_limit_c,
			activity_monitor.Soc_PD_Data_error_coeff,
			activity_monitor.Soc_PD_Data_error_rate_coeff);

		size += sprintf(buf + size, "%19s %d(%13s) %7d %7d %7d %7d %7d %7d %7d %7d %7d\n",
			" ",
			2,
			"UCLK",
			activity_monitor.Mem_FPS,
			activity_monitor.Mem_UseRlcBusy,
			activity_monitor.Mem_MinActiveFreqType,
			activity_monitor.Mem_MinActiveFreq,
			activity_monitor.Mem_BoosterFreqType,
			activity_monitor.Mem_BoosterFreq,
			activity_monitor.Mem_PD_Data_limit_c,
			activity_monitor.Mem_PD_Data_error_coeff,
			activity_monitor.Mem_PD_Data_error_rate_coeff);

		size += sprintf(buf + size, "%19s %d(%13s) %7d %7d %7d %7d %7d %7d %7d %7d %7d\n",
			" ",
			3,
			"FCLK",
			activity_monitor.Fclk_FPS,
			activity_monitor.Fclk_UseRlcBusy,
			activity_monitor.Fclk_MinActiveFreqType,
			activity_monitor.Fclk_MinActiveFreq,
			activity_monitor.Fclk_BoosterFreqType,
			activity_monitor.Fclk_BoosterFreq,
			activity_monitor.Fclk_PD_Data_limit_c,
			activity_monitor.Fclk_PD_Data_error_coeff,
			activity_monitor.Fclk_PD_Data_error_rate_coeff);
	}

	return size;
}

static int smu_v11_0_set_power_profile_mode(struct smu_context *smu, long *input, uint32_t size)
{
	DpmActivityMonitorCoeffInt_t activity_monitor;
	int workload_type = 0, ret = 0;

	smu->power_profile_mode = input[size];

	if (smu->power_profile_mode > PP_SMC_POWER_PROFILE_CUSTOM) {
		pr_err("Invalid power profile mode %d\n", smu->power_profile_mode);
		return -EINVAL;
	}

	if (smu->power_profile_mode == PP_SMC_POWER_PROFILE_CUSTOM) {
		ret = smu_update_table_with_arg(smu, TABLE_ACTIVITY_MONITOR_COEFF,
						WORKLOAD_PPLIB_CUSTOM_BIT, &activity_monitor, false);
		if (ret) {
			pr_err("[%s] Failed to get activity monitor!", __func__);
			return ret;
		}

		switch (input[0]) {
		case 0: /* Gfxclk */
			activity_monitor.Gfx_FPS = input[1];
			activity_monitor.Gfx_UseRlcBusy = input[2];
			activity_monitor.Gfx_MinActiveFreqType = input[3];
			activity_monitor.Gfx_MinActiveFreq = input[4];
			activity_monitor.Gfx_BoosterFreqType = input[5];
			activity_monitor.Gfx_BoosterFreq = input[6];
			activity_monitor.Gfx_PD_Data_limit_c = input[7];
			activity_monitor.Gfx_PD_Data_error_coeff = input[8];
			activity_monitor.Gfx_PD_Data_error_rate_coeff = input[9];
			break;
		case 1: /* Socclk */
			activity_monitor.Soc_FPS = input[1];
			activity_monitor.Soc_UseRlcBusy = input[2];
			activity_monitor.Soc_MinActiveFreqType = input[3];
			activity_monitor.Soc_MinActiveFreq = input[4];
			activity_monitor.Soc_BoosterFreqType = input[5];
			activity_monitor.Soc_BoosterFreq = input[6];
			activity_monitor.Soc_PD_Data_limit_c = input[7];
			activity_monitor.Soc_PD_Data_error_coeff = input[8];
			activity_monitor.Soc_PD_Data_error_rate_coeff = input[9];
			break;
		case 2: /* Uclk */
			activity_monitor.Mem_FPS = input[1];
			activity_monitor.Mem_UseRlcBusy = input[2];
			activity_monitor.Mem_MinActiveFreqType = input[3];
			activity_monitor.Mem_MinActiveFreq = input[4];
			activity_monitor.Mem_BoosterFreqType = input[5];
			activity_monitor.Mem_BoosterFreq = input[6];
			activity_monitor.Mem_PD_Data_limit_c = input[7];
			activity_monitor.Mem_PD_Data_error_coeff = input[8];
			activity_monitor.Mem_PD_Data_error_rate_coeff = input[9];
			break;
		case 3: /* Fclk */
			activity_monitor.Fclk_FPS = input[1];
			activity_monitor.Fclk_UseRlcBusy = input[2];
			activity_monitor.Fclk_MinActiveFreqType = input[3];
			activity_monitor.Fclk_MinActiveFreq = input[4];
			activity_monitor.Fclk_BoosterFreqType = input[5];
			activity_monitor.Fclk_BoosterFreq = input[6];
			activity_monitor.Fclk_PD_Data_limit_c = input[7];
			activity_monitor.Fclk_PD_Data_error_coeff = input[8];
			activity_monitor.Fclk_PD_Data_error_rate_coeff = input[9];
			break;
		}

		ret = smu_update_table_with_arg(smu, TABLE_ACTIVITY_MONITOR_COEFF,
						WORKLOAD_PPLIB_COMPUTE_BIT, &activity_monitor, true);
		if (ret) {
			pr_err("[%s] Failed to set activity monitor!", __func__);
			return ret;
		}
	}

	/* conv PP_SMC_POWER_PROFILE* to WORKLOAD_PPLIB_*_BIT */
	workload_type =
		smu_v11_0_conv_power_profile_to_pplib_workload(smu->power_profile_mode);
	smu_send_smc_msg_with_param(smu, SMU_MSG_SetWorkloadMask,
				    1 << workload_type);

	return ret;
}

static int smu_v11_0_update_od8_settings(struct smu_context *smu,
					uint32_t index,
					uint32_t value)
{
	struct smu_table_context *table_context = &smu->smu_table;
	int ret;

	ret = smu_update_table(smu, TABLE_OVERDRIVE,
			       table_context->overdrive_table, false);
	if (ret) {
		pr_err("Failed to export over drive table!\n");
		return ret;
	}

	smu_update_specified_od8_value(smu, index, value);

	ret = smu_update_table(smu, TABLE_OVERDRIVE,
			       table_context->overdrive_table, true);
	if (ret) {
		pr_err("Failed to import over drive table!\n");
		return ret;
	}

	return 0;
}

static int smu_v11_0_dpm_set_uvd_enable(struct smu_context *smu, bool enable)
{
	if (!smu_feature_is_supported(smu, FEATURE_DPM_VCE_BIT))
		return 0;

	if (enable == smu_feature_is_enabled(smu, FEATURE_DPM_VCE_BIT))
		return 0;

	return smu_feature_set_enabled(smu, FEATURE_DPM_VCE_BIT, enable);
}

static int smu_v11_0_dpm_set_vce_enable(struct smu_context *smu, bool enable)
{
	if (!smu_feature_is_supported(smu, FEATURE_DPM_UVD_BIT))
		return 0;

	if (enable == smu_feature_is_enabled(smu, FEATURE_DPM_UVD_BIT))
		return 0;

	return smu_feature_set_enabled(smu, FEATURE_DPM_UVD_BIT, enable);
}

static int smu_v11_0_get_current_rpm(struct smu_context *smu,
				     uint32_t *current_rpm)
{
	int ret;

	ret = smu_send_smc_msg(smu, SMU_MSG_GetCurrentRpm);

	if (ret) {
		pr_err("Attempt to get current RPM from SMC Failed!\n");
		return ret;
	}

	smu_read_smc_arg(smu, current_rpm);

	return 0;
}

static uint32_t
smu_v11_0_get_fan_control_mode(struct smu_context *smu)
{
	if (!smu_feature_is_enabled(smu, FEATURE_FAN_CONTROL_BIT))
		return AMD_FAN_CTRL_MANUAL;
	else
		return AMD_FAN_CTRL_AUTO;
}

static int
smu_v11_0_get_fan_speed_percent(struct smu_context *smu,
					   uint32_t *speed)
{
	int ret = 0;
	uint32_t percent = 0;
	uint32_t current_rpm;
	PPTable_t *pptable = smu->smu_table.driver_pptable;

	ret = smu_v11_0_get_current_rpm(smu, &current_rpm);
	percent = current_rpm * 100 / pptable->FanMaximumRpm;
	*speed = percent > 100 ? 100 : percent;

	return ret;
}

static int
smu_v11_0_smc_fan_control(struct smu_context *smu, bool start)
{
	int ret = 0;

	if (smu_feature_is_supported(smu, FEATURE_FAN_CONTROL_BIT))
		return 0;

	ret = smu_feature_set_enabled(smu, FEATURE_FAN_CONTROL_BIT, start);
	if (ret)
		pr_err("[%s]%s smc FAN CONTROL feature failed!",
		       __func__, (start ? "Start" : "Stop"));

	return ret;
}

static int
smu_v11_0_set_fan_static_mode(struct smu_context *smu, uint32_t mode)
{
	struct amdgpu_device *adev = smu->adev;

	WREG32_SOC15(THM, 0, mmCG_FDO_CTRL2,
		     REG_SET_FIELD(RREG32_SOC15(THM, 0, mmCG_FDO_CTRL2),
				   CG_FDO_CTRL2, TMIN, 0));
	WREG32_SOC15(THM, 0, mmCG_FDO_CTRL2,
		     REG_SET_FIELD(RREG32_SOC15(THM, 0, mmCG_FDO_CTRL2),
				   CG_FDO_CTRL2, FDO_PWM_MODE, mode));

	return 0;
}

static int
smu_v11_0_set_fan_speed_percent(struct smu_context *smu, uint32_t speed)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t duty100;
	uint32_t duty;
	uint64_t tmp64;
	bool stop = 0;

	if (speed > 100)
		speed = 100;

	if (smu_v11_0_smc_fan_control(smu, stop))
		return -EINVAL;
	duty100 = REG_GET_FIELD(RREG32_SOC15(THM, 0, mmCG_FDO_CTRL1),
				CG_FDO_CTRL1, FMAX_DUTY100);
	if (!duty100)
		return -EINVAL;

	tmp64 = (uint64_t)speed * duty100;
	do_div(tmp64, 100);
	duty = (uint32_t)tmp64;

	WREG32_SOC15(THM, 0, mmCG_FDO_CTRL0,
		     REG_SET_FIELD(RREG32_SOC15(THM, 0, mmCG_FDO_CTRL0),
				   CG_FDO_CTRL0, FDO_STATIC_DUTY, duty));

	return smu_v11_0_set_fan_static_mode(smu, FDO_PWM_MODE_STATIC);
}

static int
smu_v11_0_set_fan_control_mode(struct smu_context *smu,
			       uint32_t mode)
{
	int ret = 0;
	bool start = 1;
	bool stop  = 0;

	switch (mode) {
	case AMD_FAN_CTRL_NONE:
		ret = smu_v11_0_set_fan_speed_percent(smu, 100);
		break;
	case AMD_FAN_CTRL_MANUAL:
		ret = smu_v11_0_smc_fan_control(smu, stop);
		break;
	case AMD_FAN_CTRL_AUTO:
		ret = smu_v11_0_smc_fan_control(smu, start);
		break;
	default:
		break;
	}

	if (ret) {
		pr_err("[%s]Set fan control mode failed!", __func__);
		return -EINVAL;
	}

	return ret;
}

static int smu_v11_0_set_fan_speed_rpm(struct smu_context *smu,
				       uint32_t speed)
{
	struct amdgpu_device *adev = smu->adev;
	int ret;
	uint32_t tach_period, crystal_clock_freq;
	bool stop = 0;

	if (!speed)
		return -EINVAL;

	mutex_lock(&(smu->mutex));
	ret = smu_v11_0_smc_fan_control(smu, stop);
	if (ret)
		goto set_fan_speed_rpm_failed;

	crystal_clock_freq = amdgpu_asic_get_xclk(adev);
	tach_period = 60 * crystal_clock_freq * 10000 / (8 * speed);
	WREG32_SOC15(THM, 0, mmCG_TACH_CTRL,
		     REG_SET_FIELD(RREG32_SOC15(THM, 0, mmCG_TACH_CTRL),
				   CG_TACH_CTRL, TARGET_PERIOD,
				   tach_period));

	ret = smu_v11_0_set_fan_static_mode(smu, FDO_PWM_MODE_STATIC_RPM);

set_fan_speed_rpm_failed:
	mutex_unlock(&(smu->mutex));
	return ret;
}

static int smu_v11_0_set_xgmi_pstate(struct smu_context *smu,
				     uint32_t pstate)
{
	/* send msg to SMU to set pstate */
	return 0;
}

static const struct smu_funcs smu_v11_0_funcs = {
	.init_microcode = smu_v11_0_init_microcode,
	.load_microcode = smu_v11_0_load_microcode,
	.check_fw_status = smu_v11_0_check_fw_status,
	.check_fw_version = smu_v11_0_check_fw_version,
	.send_smc_msg = smu_v11_0_send_msg,
	.send_smc_msg_with_param = smu_v11_0_send_msg_with_param,
	.read_smc_arg = smu_v11_0_read_arg,
	.read_pptable_from_vbios = smu_v11_0_read_pptable_from_vbios,
	.init_smc_tables = smu_v11_0_init_smc_tables,
	.fini_smc_tables = smu_v11_0_fini_smc_tables,
	.init_power = smu_v11_0_init_power,
	.fini_power = smu_v11_0_fini_power,
	.get_vbios_bootup_values = smu_v11_0_get_vbios_bootup_values,
	.get_clk_info_from_vbios = smu_v11_0_get_clk_info_from_vbios,
	.notify_memory_pool_location = smu_v11_0_notify_memory_pool_location,
	.check_pptable = smu_v11_0_check_pptable,
	.parse_pptable = smu_v11_0_parse_pptable,
	.populate_smc_pptable = smu_v11_0_populate_smc_pptable,
	.write_pptable = smu_v11_0_write_pptable,
	.write_watermarks_table = smu_v11_0_write_watermarks_table,
	.set_min_dcef_deep_sleep = smu_v11_0_set_min_dcef_deep_sleep,
	.set_tool_table_location = smu_v11_0_set_tool_table_location,
	.init_display = smu_v11_0_init_display,
	.set_allowed_mask = smu_v11_0_set_allowed_mask,
	.get_enabled_mask = smu_v11_0_get_enabled_mask,
	.is_dpm_running = smu_v11_0_is_dpm_running,
	.system_features_control = smu_v11_0_system_features_control,
	.update_feature_enable_state = smu_v11_0_update_feature_enable_state,
	.notify_display_change = smu_v11_0_notify_display_change,
	.get_power_limit = smu_v11_0_get_power_limit,
	.set_power_limit = smu_v11_0_set_power_limit,
	.get_current_clk_freq = smu_v11_0_get_current_clk_freq,
	.init_max_sustainable_clocks = smu_v11_0_init_max_sustainable_clocks,
	.start_thermal_control = smu_v11_0_start_thermal_control,
	.read_sensor = smu_v11_0_read_sensor,
	.set_deep_sleep_dcefclk = smu_v11_0_set_deep_sleep_dcefclk,
	.display_clock_voltage_request = smu_v11_0_display_clock_voltage_request,
	.set_watermarks_for_clock_ranges = smu_v11_0_set_watermarks_for_clock_ranges,
	.get_sclk = smu_v11_0_dpm_get_sclk,
	.get_mclk = smu_v11_0_dpm_get_mclk,
	.set_od8_default_settings = smu_v11_0_set_od8_default_settings,
	.conv_power_profile_to_pplib_workload = smu_v11_0_conv_power_profile_to_pplib_workload,
	.get_power_profile_mode = smu_v11_0_get_power_profile_mode,
	.set_power_profile_mode = smu_v11_0_set_power_profile_mode,
	.update_od8_settings = smu_v11_0_update_od8_settings,
	.dpm_set_uvd_enable = smu_v11_0_dpm_set_uvd_enable,
	.dpm_set_vce_enable = smu_v11_0_dpm_set_vce_enable,
	.get_current_rpm = smu_v11_0_get_current_rpm,
	.get_fan_control_mode = smu_v11_0_get_fan_control_mode,
	.set_fan_control_mode = smu_v11_0_set_fan_control_mode,
	.get_fan_speed_percent = smu_v11_0_get_fan_speed_percent,
	.set_fan_speed_percent = smu_v11_0_set_fan_speed_percent,
	.set_fan_speed_rpm = smu_v11_0_set_fan_speed_rpm,
	.set_xgmi_pstate = smu_v11_0_set_xgmi_pstate,
};

void smu_v11_0_set_smu_funcs(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;

	smu->funcs = &smu_v11_0_funcs;
	switch (adev->asic_type) {
	case CHIP_VEGA20:
		vega20_set_ppt_funcs(smu);
		break;
	default:
		pr_warn("Unknown asic for smu11\n");
	}
}
