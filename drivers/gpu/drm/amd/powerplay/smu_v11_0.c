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

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/pci.h>

#define SMU_11_0_PARTIAL_PPTABLE

#include "pp_debug.h"
#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "smu_internal.h"
#include "atomfirmware.h"
#include "amdgpu_atomfirmware.h"
#include "smu_v11_0.h"
#include "smu_v11_0_pptable.h"
#include "soc15_common.h"
#include "atom.h"
#include "amd_pcie.h"
#include "amdgpu_ras.h"

#include "asic_reg/thm/thm_11_0_2_offset.h"
#include "asic_reg/thm/thm_11_0_2_sh_mask.h"
#include "asic_reg/mp/mp_11_0_offset.h"
#include "asic_reg/mp/mp_11_0_sh_mask.h"
#include "asic_reg/nbio/nbio_7_4_offset.h"
#include "asic_reg/nbio/nbio_7_4_sh_mask.h"
#include "asic_reg/smuio/smuio_11_0_0_offset.h"
#include "asic_reg/smuio/smuio_11_0_0_sh_mask.h"

MODULE_FIRMWARE("amdgpu/vega20_smc.bin");
MODULE_FIRMWARE("amdgpu/arcturus_smc.bin");
MODULE_FIRMWARE("amdgpu/navi10_smc.bin");
MODULE_FIRMWARE("amdgpu/navi14_smc.bin");
MODULE_FIRMWARE("amdgpu/navi12_smc.bin");

#define SMU11_VOLTAGE_SCALE 4

static int smu_v11_0_send_msg_without_waiting(struct smu_context *smu,
					      uint16_t msg)
{
	struct amdgpu_device *adev = smu->adev;
	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_66, msg);
	return 0;
}

int smu_v11_0_read_arg(struct smu_context *smu, uint32_t *arg)
{
	struct amdgpu_device *adev = smu->adev;

	*arg = RREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_82);
	return 0;
}

static int smu_v11_0_wait_for_response(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t cur_value, i, timeout = adev->usec_timeout * 10;

	for (i = 0; i < timeout; i++) {
		cur_value = RREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90);
		if ((cur_value & MP1_C2PMSG_90__CONTENT_MASK) != 0)
			return cur_value == 0x1 ? 0 : -EIO;

		udelay(1);
	}

	/* timeout means wrong logic */
	return -ETIME;
}

int
smu_v11_0_send_msg_with_param(struct smu_context *smu,
			      enum smu_message_type msg,
			      uint32_t param)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0, index = 0;

	index = smu_msg_get_index(smu, msg);
	if (index < 0)
		return index;

	ret = smu_v11_0_wait_for_response(smu);
	if (ret) {
		pr_err("Msg issuing pre-check failed and "
		       "SMU may be not in the right state!\n");
		return ret;
	}

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90, 0);

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_82, param);

	smu_v11_0_send_msg_without_waiting(smu, (uint16_t)index);

	ret = smu_v11_0_wait_for_response(smu);
	if (ret)
		pr_err("failed send message: %10s (%d) \tparam: 0x%08x response %#x\n",
		       smu_get_message_name(smu, msg), index, param, ret);

	return ret;
}

int smu_v11_0_init_microcode(struct smu_context *smu)
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
	case CHIP_ARCTURUS:
		chip_name = "arcturus";
		break;
	case CHIP_NAVI10:
		chip_name = "navi10";
		break;
	case CHIP_NAVI14:
		chip_name = "navi14";
		break;
	case CHIP_NAVI12:
		chip_name = "navi12";
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

int smu_v11_0_load_microcode(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	const uint32_t *src;
	const struct smc_firmware_header_v1_0 *hdr;
	uint32_t addr_start = MP1_SRAM;
	uint32_t i;
	uint32_t mp1_fw_flags;

	hdr = (const struct smc_firmware_header_v1_0 *) adev->pm.fw->data;
	src = (const uint32_t *)(adev->pm.fw->data +
		le32_to_cpu(hdr->header.ucode_array_offset_bytes));

	for (i = 1; i < MP1_SMC_SIZE/4 - 1; i++) {
		WREG32_PCIE(addr_start, src[i]);
		addr_start += 4;
	}

	WREG32_PCIE(MP1_Public | (smnMP1_PUB_CTRL & 0xffffffff),
		1 & MP1_SMN_PUB_CTRL__RESET_MASK);
	WREG32_PCIE(MP1_Public | (smnMP1_PUB_CTRL & 0xffffffff),
		1 & ~MP1_SMN_PUB_CTRL__RESET_MASK);

	for (i = 0; i < adev->usec_timeout; i++) {
		mp1_fw_flags = RREG32_PCIE(MP1_Public |
			(smnMP1_FIRMWARE_FLAGS & 0xffffffff));
		if ((mp1_fw_flags & MP1_FIRMWARE_FLAGS__INTERRUPTS_ENABLED_MASK) >>
			MP1_FIRMWARE_FLAGS__INTERRUPTS_ENABLED__SHIFT)
			break;
		udelay(1);
	}

	if (i == adev->usec_timeout)
		return -ETIME;

	return 0;
}

int smu_v11_0_check_fw_status(struct smu_context *smu)
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

int smu_v11_0_check_fw_version(struct smu_context *smu)
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

	switch (smu->adev->asic_type) {
	case CHIP_VEGA20:
		smu->smc_if_version = SMU11_DRIVER_IF_VERSION_VG20;
		break;
	case CHIP_ARCTURUS:
		smu->smc_if_version = SMU11_DRIVER_IF_VERSION_ARCT;
		break;
	case CHIP_NAVI10:
		smu->smc_if_version = SMU11_DRIVER_IF_VERSION_NV10;
		break;
	case CHIP_NAVI14:
		smu->smc_if_version = SMU11_DRIVER_IF_VERSION_NV14;
		break;
	default:
		pr_err("smu unsupported asic type:%d.\n", smu->adev->asic_type);
		smu->smc_if_version = SMU11_DRIVER_IF_VERSION_INV;
		break;
	}

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

static int smu_v11_0_set_pptable_v2_0(struct smu_context *smu, void **table, uint32_t *size)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t ppt_offset_bytes;
	const struct smc_firmware_header_v2_0 *v2;

	v2 = (const struct smc_firmware_header_v2_0 *) adev->pm.fw->data;

	ppt_offset_bytes = le32_to_cpu(v2->ppt_offset_bytes);
	*size = le32_to_cpu(v2->ppt_size_bytes);
	*table = (uint8_t *)v2 + ppt_offset_bytes;

	return 0;
}

static int smu_v11_0_set_pptable_v2_1(struct smu_context *smu, void **table,
				      uint32_t *size, uint32_t pptable_id)
{
	struct amdgpu_device *adev = smu->adev;
	const struct smc_firmware_header_v2_1 *v2_1;
	struct smc_soft_pptable_entry *entries;
	uint32_t pptable_count = 0;
	int i = 0;

	v2_1 = (const struct smc_firmware_header_v2_1 *) adev->pm.fw->data;
	entries = (struct smc_soft_pptable_entry *)
		((uint8_t *)v2_1 + le32_to_cpu(v2_1->pptable_entry_offset));
	pptable_count = le32_to_cpu(v2_1->pptable_count);
	for (i = 0; i < pptable_count; i++) {
		if (le32_to_cpu(entries[i].id) == pptable_id) {
			*table = ((uint8_t *)v2_1 + le32_to_cpu(entries[i].ppt_offset_bytes));
			*size = le32_to_cpu(entries[i].ppt_size_bytes);
			break;
		}
	}

	if (i == pptable_count)
		return -EINVAL;

	return 0;
}

int smu_v11_0_setup_pptable(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	const struct smc_firmware_header_v1_0 *hdr;
	int ret, index;
	uint32_t size = 0;
	uint16_t atom_table_size;
	uint8_t frev, crev;
	void *table;
	uint16_t version_major, version_minor;

	hdr = (const struct smc_firmware_header_v1_0 *) adev->pm.fw->data;
	version_major = le16_to_cpu(hdr->header.header_version_major);
	version_minor = le16_to_cpu(hdr->header.header_version_minor);
	if (version_major == 2 && smu->smu_table.boot_values.pp_table_id > 0) {
		pr_info("use driver provided pptable %d\n", smu->smu_table.boot_values.pp_table_id);
		switch (version_minor) {
		case 0:
			ret = smu_v11_0_set_pptable_v2_0(smu, &table, &size);
			break;
		case 1:
			ret = smu_v11_0_set_pptable_v2_1(smu, &table, &size,
							 smu->smu_table.boot_values.pp_table_id);
			break;
		default:
			ret = -EINVAL;
			break;
		}
		if (ret)
			return ret;

	} else {
		pr_info("use vbios provided pptable\n");
		index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
						    powerplayinfo);

		ret = smu_get_atom_data_table(smu, index, &atom_table_size, &frev, &crev,
					      (uint8_t **)&table);
		if (ret)
			return ret;
		size = atom_table_size;
	}

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

int smu_v11_0_init_smc_tables(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = NULL;
	int ret = 0;

	if (smu_table->tables)
		return -EINVAL;

	tables = kcalloc(SMU_TABLE_COUNT, sizeof(struct smu_table),
			 GFP_KERNEL);
	if (!tables)
		return -ENOMEM;

	smu_table->tables = tables;

	ret = smu_tables_init(smu, tables);
	if (ret)
		return ret;

	ret = smu_v11_0_init_dpm_context(smu);
	if (ret)
		return ret;

	return 0;
}

int smu_v11_0_fini_smc_tables(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	int ret = 0;

	if (!smu_table->tables)
		return -EINVAL;

	kfree(smu_table->tables);
	kfree(smu_table->metrics_table);
	kfree(smu_table->watermarks_table);
	smu_table->tables = NULL;
	smu_table->metrics_table = NULL;
	smu_table->watermarks_table = NULL;
	smu_table->metrics_time = 0;

	ret = smu_v11_0_fini_dpm_context(smu);
	if (ret)
		return ret;
	return 0;
}

int smu_v11_0_init_power(struct smu_context *smu)
{
	struct smu_power_context *smu_power = &smu->smu_power;

	if (!smu->pm_enabled)
		return 0;
	if (smu_power->power_context || smu_power->power_context_size != 0)
		return -EINVAL;

	smu_power->power_context = kzalloc(sizeof(struct smu_11_0_dpm_context),
					   GFP_KERNEL);
	if (!smu_power->power_context)
		return -ENOMEM;
	smu_power->power_context_size = sizeof(struct smu_11_0_dpm_context);

	return 0;
}

int smu_v11_0_fini_power(struct smu_context *smu)
{
	struct smu_power_context *smu_power = &smu->smu_power;

	if (!smu->pm_enabled)
		return 0;
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

	smu->smu_table.boot_values.format_revision = header->format_revision;
	smu->smu_table.boot_values.content_revision = header->content_revision;

	return 0;
}

int smu_v11_0_get_clk_info_from_vbios(struct smu_context *smu)
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

	if ((smu->smu_table.boot_values.format_revision == 3) &&
	    (smu->smu_table.boot_values.content_revision >= 2)) {
		memset(&input, 0, sizeof(input));
		input.clk_id = SMU11_SYSPLL1_0_FCLK_ID;
		input.syspll_id = SMU11_SYSPLL1_2_ID;
		input.command = GET_SMU_CLOCK_INFO_V3_1_GET_CLOCK_FREQ;
		index = get_index_into_master_table(atom_master_list_of_command_functions_v2_1,
						    getsmuclockinfo);

		ret = amdgpu_atom_execute_table(adev->mode_info.atom_context, index,
						(uint32_t *)&input);
		if (ret)
			return -EINVAL;

		output = (struct atom_get_smu_clock_info_output_parameters_v3_1 *)&input;
		smu->smu_table.boot_values.fclk = le32_to_cpu(output->atom_smu_outputclkfreq.smu_clock_freq_hz) / 10000;
	}

	return 0;
}

int smu_v11_0_notify_memory_pool_location(struct smu_context *smu)
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

int smu_v11_0_check_pptable(struct smu_context *smu)
{
	int ret;

	ret = smu_check_powerplay_table(smu);
	return ret;
}

int smu_v11_0_parse_pptable(struct smu_context *smu)
{
	int ret;

	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_table *table = &table_context->tables[SMU_TABLE_PPTABLE];

	if (table_context->driver_pptable)
		return -EINVAL;

	table_context->driver_pptable = kzalloc(table->size, GFP_KERNEL);

	if (!table_context->driver_pptable)
		return -ENOMEM;

	ret = smu_store_powerplay_table(smu);
	if (ret)
		return -EINVAL;

	ret = smu_append_powerplay_table(smu);

	return ret;
}

int smu_v11_0_populate_smc_pptable(struct smu_context *smu)
{
	int ret;

	ret = smu_set_default_dpm_table(smu);

	return ret;
}

int smu_v11_0_write_pptable(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	int ret = 0;

	ret = smu_update_table(smu, SMU_TABLE_PPTABLE, 0,
			       table_context->driver_pptable, true);

	return ret;
}

int smu_v11_0_set_deep_sleep_dcefclk(struct smu_context *smu, uint32_t clk)
{
	int ret;

	ret = smu_send_smc_msg_with_param(smu,
					  SMU_MSG_SetMinDeepSleepDcefclk, clk);
	if (ret)
		pr_err("SMU11 attempt to set divider for DCEFCLK Failed!");

	return ret;
}

int smu_v11_0_set_min_dcef_deep_sleep(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;

	if (!smu->pm_enabled)
		return 0;
	if (!table_context)
		return -EINVAL;

	return smu_v11_0_set_deep_sleep_dcefclk(smu, table_context->boot_values.dcefclk / 100);
}

int smu_v11_0_set_driver_table_location(struct smu_context *smu)
{
	struct smu_table *driver_table = &smu->smu_table.driver_table;
	int ret = 0;

	if (driver_table->mc_address) {
		ret = smu_send_smc_msg_with_param(smu,
				SMU_MSG_SetDriverDramAddrHigh,
				upper_32_bits(driver_table->mc_address));
		if (!ret)
			ret = smu_send_smc_msg_with_param(smu,
				SMU_MSG_SetDriverDramAddrLow,
				lower_32_bits(driver_table->mc_address));
	}

	return ret;
}

int smu_v11_0_set_tool_table_location(struct smu_context *smu)
{
	int ret = 0;
	struct smu_table *tool_table = &smu->smu_table.tables[SMU_TABLE_PMSTATUSLOG];

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

int smu_v11_0_init_display_count(struct smu_context *smu, uint32_t count)
{
	int ret = 0;

	if (!smu->pm_enabled)
		return ret;

	ret = smu_send_smc_msg_with_param(smu, SMU_MSG_NumOfDisplays, count);
	return ret;
}


int smu_v11_0_set_allowed_mask(struct smu_context *smu)
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

int smu_v11_0_get_enabled_mask(struct smu_context *smu,
				      uint32_t *feature_mask, uint32_t num)
{
	uint32_t feature_mask_high = 0, feature_mask_low = 0;
	struct smu_feature *feature = &smu->smu_feature;
	int ret = 0;

	if (!feature_mask || num < 2)
		return -EINVAL;

	if (bitmap_empty(feature->enabled, feature->feature_num)) {
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
	} else {
		bitmap_copy((unsigned long *)feature_mask, feature->enabled,
			     feature->feature_num);
	}

	return ret;
}

int smu_v11_0_system_features_control(struct smu_context *smu,
					     bool en)
{
	struct smu_feature *feature = &smu->smu_feature;
	uint32_t feature_mask[2];
	int ret = 0;

	ret = smu_send_smc_msg(smu, (en ? SMU_MSG_EnableAllSmuFeatures :
				     SMU_MSG_DisableAllSmuFeatures));
	if (ret)
		return ret;

	if (en) {
		ret = smu_feature_get_enabled_mask(smu, feature_mask, 2);
		if (ret)
			return ret;

		bitmap_copy(feature->enabled, (unsigned long *)&feature_mask,
			    feature->feature_num);
		bitmap_copy(feature->supported, (unsigned long *)&feature_mask,
			    feature->feature_num);
	} else {
		bitmap_zero(feature->enabled, feature->feature_num);
		bitmap_zero(feature->supported, feature->feature_num);
	}

	return ret;
}

int smu_v11_0_notify_display_change(struct smu_context *smu)
{
	int ret = 0;

	if (!smu->pm_enabled)
		return ret;
	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT) &&
	    smu->adev->gmc.vram_type == AMDGPU_VRAM_TYPE_HBM)
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetUclkFastSwitch, 1);

	return ret;
}

static int
smu_v11_0_get_max_sustainable_clock(struct smu_context *smu, uint32_t *clock,
				    enum smu_clk_type clock_select)
{
	int ret = 0;
	int clk_id;

	if (!smu->pm_enabled)
		return ret;

	if ((smu_msg_get_index(smu, SMU_MSG_GetDcModeMaxDpmFreq) < 0) ||
	    (smu_msg_get_index(smu, SMU_MSG_GetMaxDpmFreq) < 0))
		return 0;

	clk_id = smu_clk_get_index(smu, clock_select);
	if (clk_id < 0)
		return -EINVAL;

	ret = smu_send_smc_msg_with_param(smu, SMU_MSG_GetDcModeMaxDpmFreq,
					  clk_id << 16);
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
					  clk_id << 16);
	if (ret) {
		pr_err("[GetMaxSustainableClock] failed to get max AC clock from SMC!");
		return ret;
	}

	ret = smu_read_smc_arg(smu, clock);

	return ret;
}

int smu_v11_0_init_max_sustainable_clocks(struct smu_context *smu)
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

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT)) {
		ret = smu_v11_0_get_max_sustainable_clock(smu,
							  &(max_sustainable_clocks->uclock),
							  SMU_UCLK);
		if (ret) {
			pr_err("[%s] failed to get max UCLK from SMC!",
			       __func__);
			return ret;
		}
	}

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_SOCCLK_BIT)) {
		ret = smu_v11_0_get_max_sustainable_clock(smu,
							  &(max_sustainable_clocks->soc_clock),
							  SMU_SOCCLK);
		if (ret) {
			pr_err("[%s] failed to get max SOCCLK from SMC!",
			       __func__);
			return ret;
		}
	}

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_DCEFCLK_BIT)) {
		ret = smu_v11_0_get_max_sustainable_clock(smu,
							  &(max_sustainable_clocks->dcef_clock),
							  SMU_DCEFCLK);
		if (ret) {
			pr_err("[%s] failed to get max DCEFCLK from SMC!",
			       __func__);
			return ret;
		}

		ret = smu_v11_0_get_max_sustainable_clock(smu,
							  &(max_sustainable_clocks->display_clock),
							  SMU_DISPCLK);
		if (ret) {
			pr_err("[%s] failed to get max DISPCLK from SMC!",
			       __func__);
			return ret;
		}
		ret = smu_v11_0_get_max_sustainable_clock(smu,
							  &(max_sustainable_clocks->phy_clock),
							  SMU_PHYCLK);
		if (ret) {
			pr_err("[%s] failed to get max PHYCLK from SMC!",
			       __func__);
			return ret;
		}
		ret = smu_v11_0_get_max_sustainable_clock(smu,
							  &(max_sustainable_clocks->pixel_clock),
							  SMU_PIXCLK);
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

uint32_t smu_v11_0_get_max_power_limit(struct smu_context *smu) {
	uint32_t od_limit, max_power_limit;
	struct smu_11_0_powerplay_table *powerplay_table = NULL;
	struct smu_table_context *table_context = &smu->smu_table;
	powerplay_table = table_context->power_play_table;

	max_power_limit = smu_get_pptable_power_limit(smu);

	if (!max_power_limit) {
		// If we couldn't get the table limit, fall back on first-read value
		if (!smu->default_power_limit)
			smu->default_power_limit = smu->power_limit;
		max_power_limit = smu->default_power_limit;
	}

	if (smu->od_enabled) {
		od_limit = le32_to_cpu(powerplay_table->overdrive_table.max[SMU_11_0_ODSETTING_POWERPERCENTAGE]);

		pr_debug("ODSETTING_POWERPERCENTAGE: %d (default: %d)\n", od_limit, smu->default_power_limit);

		max_power_limit *= (100 + od_limit);
		max_power_limit /= 100;
	}

	return max_power_limit;
}

int smu_v11_0_set_power_limit(struct smu_context *smu, uint32_t n)
{
	int ret = 0;
	uint32_t max_power_limit;

	max_power_limit = smu_v11_0_get_max_power_limit(smu);

	if (n > max_power_limit) {
		pr_err("New power limit (%d) is over the max allowed %d\n",
				n,
				max_power_limit);
		return -EINVAL;
	}

	if (n == 0)
		n = smu->default_power_limit;

	if (!smu_feature_is_enabled(smu, SMU_FEATURE_PPT_BIT)) {
		pr_err("Setting new power limit is not supported!\n");
		return -EOPNOTSUPP;
	}

	ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetPptLimit, n);
	if (ret) {
		pr_err("[%s] Set power limit Failed!\n", __func__);
		return ret;
	}
	smu->power_limit = n;

	return 0;
}

int smu_v11_0_get_current_clk_freq(struct smu_context *smu,
					  enum smu_clk_type clk_id,
					  uint32_t *value)
{
	int ret = 0;
	uint32_t freq = 0;
	int asic_clk_id;

	if (clk_id >= SMU_CLK_COUNT || !value)
		return -EINVAL;

	asic_clk_id = smu_clk_get_index(smu, clk_id);
	if (asic_clk_id < 0)
		return -EINVAL;

	/* if don't has GetDpmClockFreq Message, try get current clock by SmuMetrics_t */
	if (smu_msg_get_index(smu, SMU_MSG_GetDpmClockFreq) < 0)
		ret =  smu_get_current_clk_freq_by_table(smu, clk_id, &freq);
	else {
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_GetDpmClockFreq,
						  (asic_clk_id << 16));
		if (ret)
			return ret;

		ret = smu_read_smc_arg(smu, &freq);
		if (ret)
			return ret;
	}

	freq *= 100;
	*value = freq;

	return ret;
}

static int smu_v11_0_set_thermal_range(struct smu_context *smu,
				       struct smu_temperature_range range)
{
	struct amdgpu_device *adev = smu->adev;
	int low = SMU_THERMAL_MINIMUM_ALERT_TEMP;
	int high = SMU_THERMAL_MAXIMUM_ALERT_TEMP;
	uint32_t val;
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_11_0_powerplay_table *powerplay_table = table_context->power_play_table;

	low = max(SMU_THERMAL_MINIMUM_ALERT_TEMP,
			range.min / SMU_TEMPERATURE_UNITS_PER_CENTIGRADES);
	high = min((uint16_t)SMU_THERMAL_MAXIMUM_ALERT_TEMP, powerplay_table->software_shutdown_temp);

	if (low > high)
		return -EINVAL;

	val = RREG32_SOC15(THM, 0, mmTHM_THERMAL_INT_CTRL);
	val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, MAX_IH_CREDIT, 5);
	val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, THERM_IH_HW_ENA, 1);
	val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, THERM_INTH_MASK, 0);
	val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, THERM_INTL_MASK, 0);
	val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, DIG_THERM_INTH, (high & 0xff));
	val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, DIG_THERM_INTL, (low & 0xff));
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

int smu_v11_0_start_thermal_control(struct smu_context *smu)
{
	int ret = 0;
	struct smu_temperature_range range;
	struct amdgpu_device *adev = smu->adev;

	if (!smu->pm_enabled)
		return ret;

	memcpy(&range, &smu11_thermal_policy[0], sizeof(struct smu_temperature_range));

	ret = smu_get_thermal_temperature_range(smu, &range);
	if (ret)
		return ret;

	if (smu->smu_table.thermal_controller_type) {
		ret = smu_v11_0_set_thermal_range(smu, range);
		if (ret)
			return ret;

		ret = smu_v11_0_enable_thermal_alert(smu);
		if (ret)
			return ret;

		ret = smu_set_thermal_fan_table(smu);
		if (ret)
			return ret;
	}

	adev->pm.dpm.thermal.min_temp = range.min;
	adev->pm.dpm.thermal.max_temp = range.max;
	adev->pm.dpm.thermal.max_edge_emergency_temp = range.edge_emergency_max;
	adev->pm.dpm.thermal.min_hotspot_temp = range.hotspot_min;
	adev->pm.dpm.thermal.max_hotspot_crit_temp = range.hotspot_crit_max;
	adev->pm.dpm.thermal.max_hotspot_emergency_temp = range.hotspot_emergency_max;
	adev->pm.dpm.thermal.min_mem_temp = range.mem_min;
	adev->pm.dpm.thermal.max_mem_crit_temp = range.mem_crit_max;
	adev->pm.dpm.thermal.max_mem_emergency_temp = range.mem_emergency_max;

	return ret;
}

int smu_v11_0_stop_thermal_control(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;

	WREG32_SOC15(THM, 0, mmTHM_THERMAL_INT_ENA, 0);

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

int smu_v11_0_read_sensor(struct smu_context *smu,
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
	case AMDGPU_PP_SENSOR_VDDGFX:
		ret = smu_v11_0_get_gfx_vdd(smu, (uint32_t *)data);
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

int
smu_v11_0_display_clock_voltage_request(struct smu_context *smu,
					struct pp_display_clock_request
					*clock_req)
{
	enum amd_pp_clock_type clk_type = clock_req->clock_type;
	int ret = 0;
	enum smu_clk_type clk_select = 0;
	uint32_t clk_freq = clock_req->clock_freq_in_khz / 1000;

	if (!smu->pm_enabled)
		return -EINVAL;

	if (smu_feature_is_enabled(smu, SMU_FEATURE_DPM_DCEFCLK_BIT) ||
		smu_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT)) {
		switch (clk_type) {
		case amd_pp_dcef_clock:
			clk_select = SMU_DCEFCLK;
			break;
		case amd_pp_disp_clock:
			clk_select = SMU_DISPCLK;
			break;
		case amd_pp_pixel_clock:
			clk_select = SMU_PIXCLK;
			break;
		case amd_pp_phy_clock:
			clk_select = SMU_PHYCLK;
			break;
		case amd_pp_mem_clock:
			clk_select = SMU_UCLK;
			break;
		default:
			pr_info("[%s] Invalid Clock Type!", __func__);
			ret = -EINVAL;
			break;
		}

		if (ret)
			goto failed;

		if (clk_select == SMU_UCLK && smu->disable_uclk_switch)
			return 0;

		ret = smu_set_hard_freq_range(smu, clk_select, clk_freq, 0);

		if(clk_select == SMU_UCLK)
			smu->hard_min_uclk_req_from_dal = clk_freq;
	}

failed:
	return ret;
}

int smu_v11_0_gfx_off_control(struct smu_context *smu, bool enable)
{
	int ret = 0;
	struct amdgpu_device *adev = smu->adev;

	switch (adev->asic_type) {
	case CHIP_VEGA20:
		break;
	case CHIP_NAVI10:
	case CHIP_NAVI14:
	case CHIP_NAVI12:
		if (!(adev->pm.pp_feature & PP_GFXOFF_MASK))
			return 0;
		if (enable)
			ret = smu_send_smc_msg(smu, SMU_MSG_AllowGfxOff);
		else
			ret = smu_send_smc_msg(smu, SMU_MSG_DisallowGfxOff);
		break;
	default:
		break;
	}

	return ret;
}

uint32_t
smu_v11_0_get_fan_control_mode(struct smu_context *smu)
{
	if (!smu_feature_is_enabled(smu, SMU_FEATURE_FAN_CONTROL_BIT))
		return AMD_FAN_CTRL_MANUAL;
	else
		return AMD_FAN_CTRL_AUTO;
}

static int
smu_v11_0_auto_fan_control(struct smu_context *smu, bool auto_fan_control)
{
	int ret = 0;

	if (!smu_feature_is_supported(smu, SMU_FEATURE_FAN_CONTROL_BIT))
		return 0;

	ret = smu_feature_set_enabled(smu, SMU_FEATURE_FAN_CONTROL_BIT, auto_fan_control);
	if (ret)
		pr_err("[%s]%s smc FAN CONTROL feature failed!",
		       __func__, (auto_fan_control ? "Start" : "Stop"));

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

int
smu_v11_0_set_fan_speed_percent(struct smu_context *smu, uint32_t speed)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t duty100, duty;
	uint64_t tmp64;

	if (speed > 100)
		speed = 100;

	if (smu_v11_0_auto_fan_control(smu, 0))
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

int
smu_v11_0_set_fan_control_mode(struct smu_context *smu,
			       uint32_t mode)
{
	int ret = 0;

	switch (mode) {
	case AMD_FAN_CTRL_NONE:
		ret = smu_v11_0_set_fan_speed_percent(smu, 100);
		break;
	case AMD_FAN_CTRL_MANUAL:
		ret = smu_v11_0_auto_fan_control(smu, 0);
		break;
	case AMD_FAN_CTRL_AUTO:
		ret = smu_v11_0_auto_fan_control(smu, 1);
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

int smu_v11_0_set_fan_speed_rpm(struct smu_context *smu,
				       uint32_t speed)
{
	struct amdgpu_device *adev = smu->adev;
	int ret;
	uint32_t tach_period, crystal_clock_freq;

	if (!speed)
		return -EINVAL;

	ret = smu_v11_0_auto_fan_control(smu, 0);
	if (ret)
		return ret;

	crystal_clock_freq = amdgpu_asic_get_xclk(adev);
	tach_period = 60 * crystal_clock_freq * 10000 / (8 * speed);
	WREG32_SOC15(THM, 0, mmCG_TACH_CTRL,
		     REG_SET_FIELD(RREG32_SOC15(THM, 0, mmCG_TACH_CTRL),
				   CG_TACH_CTRL, TARGET_PERIOD,
				   tach_period));

	ret = smu_v11_0_set_fan_static_mode(smu, FDO_PWM_MODE_STATIC_RPM);

	return ret;
}

int smu_v11_0_set_xgmi_pstate(struct smu_context *smu,
				     uint32_t pstate)
{
	int ret = 0;
	ret = smu_send_smc_msg_with_param(smu,
					  SMU_MSG_SetXgmiMode,
					  pstate ? XGMI_MODE_PSTATE_D0 : XGMI_MODE_PSTATE_D3);
	return ret;
}

#define THM_11_0__SRCID__THM_DIG_THERM_L2H		0		/* ASIC_TEMP > CG_THERMAL_INT.DIG_THERM_INTH  */
#define THM_11_0__SRCID__THM_DIG_THERM_H2L		1		/* ASIC_TEMP < CG_THERMAL_INT.DIG_THERM_INTL  */

static int smu_v11_0_irq_process(struct amdgpu_device *adev,
				 struct amdgpu_irq_src *source,
				 struct amdgpu_iv_entry *entry)
{
	uint32_t client_id = entry->client_id;
	uint32_t src_id = entry->src_id;

	if (client_id == SOC15_IH_CLIENTID_THM) {
		switch (src_id) {
		case THM_11_0__SRCID__THM_DIG_THERM_L2H:
			pr_warn("GPU over temperature range detected on PCIe %d:%d.%d!\n",
				PCI_BUS_NUM(adev->pdev->devfn),
				PCI_SLOT(adev->pdev->devfn),
				PCI_FUNC(adev->pdev->devfn));
		break;
		case THM_11_0__SRCID__THM_DIG_THERM_H2L:
			pr_warn("GPU under temperature range detected on PCIe %d:%d.%d!\n",
				PCI_BUS_NUM(adev->pdev->devfn),
				PCI_SLOT(adev->pdev->devfn),
				PCI_FUNC(adev->pdev->devfn));
		break;
		default:
			pr_warn("GPU under temperature range unknown src id (%d), detected on PCIe %d:%d.%d!\n",
				src_id,
				PCI_BUS_NUM(adev->pdev->devfn),
				PCI_SLOT(adev->pdev->devfn),
				PCI_FUNC(adev->pdev->devfn));
		break;

		}
	}

	return 0;
}

static const struct amdgpu_irq_src_funcs smu_v11_0_irq_funcs =
{
	.process = smu_v11_0_irq_process,
};

int smu_v11_0_register_irq_handler(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	struct amdgpu_irq_src *irq_src = smu->irq_source;
	int ret = 0;

	/* already register */
	if (irq_src)
		return 0;

	irq_src = kzalloc(sizeof(struct amdgpu_irq_src), GFP_KERNEL);
	if (!irq_src)
		return -ENOMEM;
	smu->irq_source = irq_src;

	irq_src->funcs = &smu_v11_0_irq_funcs;

	ret = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_THM,
				THM_11_0__SRCID__THM_DIG_THERM_L2H,
				irq_src);
	if (ret)
		return ret;

	ret = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_THM,
				THM_11_0__SRCID__THM_DIG_THERM_H2L,
				irq_src);
	if (ret)
		return ret;

	return ret;
}

int smu_v11_0_get_max_sustainable_clocks_by_dc(struct smu_context *smu,
		struct pp_smu_nv_clock_table *max_clocks)
{
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_11_0_max_sustainable_clocks *sustainable_clocks = NULL;

	if (!max_clocks || !table_context->max_sustainable_clocks)
		return -EINVAL;

	sustainable_clocks = table_context->max_sustainable_clocks;

	max_clocks->dcfClockInKhz =
			(unsigned int) sustainable_clocks->dcef_clock * 1000;
	max_clocks->displayClockInKhz =
			(unsigned int) sustainable_clocks->display_clock * 1000;
	max_clocks->phyClockInKhz =
			(unsigned int) sustainable_clocks->phy_clock * 1000;
	max_clocks->pixelClockInKhz =
			(unsigned int) sustainable_clocks->pixel_clock * 1000;
	max_clocks->uClockInKhz =
			(unsigned int) sustainable_clocks->uclock * 1000;
	max_clocks->socClockInKhz =
			(unsigned int) sustainable_clocks->soc_clock * 1000;
	max_clocks->dscClockInKhz = 0;
	max_clocks->dppClockInKhz = 0;
	max_clocks->fabricClockInKhz = 0;

	return 0;
}

int smu_v11_0_set_azalia_d3_pme(struct smu_context *smu)
{
	int ret = 0;

	ret = smu_send_smc_msg(smu, SMU_MSG_BacoAudioD3PME);

	return ret;
}

static int smu_v11_0_baco_set_armd3_sequence(struct smu_context *smu, enum smu_v11_0_baco_seq baco_seq)
{
	return smu_send_smc_msg_with_param(smu, SMU_MSG_ArmD3, baco_seq);
}

bool smu_v11_0_baco_is_support(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	struct smu_baco_context *smu_baco = &smu->smu_baco;
	uint32_t val;
	bool baco_support;

	mutex_lock(&smu_baco->mutex);
	baco_support = smu_baco->platform_support;
	mutex_unlock(&smu_baco->mutex);

	if (!baco_support)
		return false;

	/* Arcturus does not support this bit mask */
	if (smu_feature_is_supported(smu, SMU_FEATURE_BACO_BIT) &&
	   !smu_feature_is_enabled(smu, SMU_FEATURE_BACO_BIT))
		return false;

	val = RREG32_SOC15(NBIO, 0, mmRCC_BIF_STRAP0);
	if (val & RCC_BIF_STRAP0__STRAP_PX_CAPABLE_MASK)
		return true;

	return false;
}

enum smu_baco_state smu_v11_0_baco_get_state(struct smu_context *smu)
{
	struct smu_baco_context *smu_baco = &smu->smu_baco;
	enum smu_baco_state baco_state;

	mutex_lock(&smu_baco->mutex);
	baco_state = smu_baco->state;
	mutex_unlock(&smu_baco->mutex);

	return baco_state;
}

int smu_v11_0_baco_set_state(struct smu_context *smu, enum smu_baco_state state)
{

	struct smu_baco_context *smu_baco = &smu->smu_baco;
	struct amdgpu_device *adev = smu->adev;
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);
	uint32_t bif_doorbell_intr_cntl;
	uint32_t data;
	int ret = 0;

	if (smu_v11_0_baco_get_state(smu) == state)
		return 0;

	mutex_lock(&smu_baco->mutex);

	bif_doorbell_intr_cntl = RREG32_SOC15(NBIO, 0, mmBIF_DOORBELL_INT_CNTL);

	if (state == SMU_BACO_STATE_ENTER) {
		bif_doorbell_intr_cntl = REG_SET_FIELD(bif_doorbell_intr_cntl,
						BIF_DOORBELL_INT_CNTL,
						DOORBELL_INTERRUPT_DISABLE, 1);
		WREG32_SOC15(NBIO, 0, mmBIF_DOORBELL_INT_CNTL, bif_doorbell_intr_cntl);

		if (!ras || !ras->supported) {
			data = RREG32_SOC15(THM, 0, mmTHM_BACO_CNTL);
			data |= 0x80000000;
			WREG32_SOC15(THM, 0, mmTHM_BACO_CNTL, data);

			ret = smu_send_smc_msg_with_param(smu, SMU_MSG_EnterBaco, 0);
		} else {
			ret = smu_send_smc_msg_with_param(smu, SMU_MSG_EnterBaco, 1);
		}
	} else {
		ret = smu_send_smc_msg(smu, SMU_MSG_ExitBaco);
		if (ret)
			goto out;

		bif_doorbell_intr_cntl = REG_SET_FIELD(bif_doorbell_intr_cntl,
						BIF_DOORBELL_INT_CNTL,
						DOORBELL_INTERRUPT_DISABLE, 0);
		WREG32_SOC15(NBIO, 0, mmBIF_DOORBELL_INT_CNTL, bif_doorbell_intr_cntl);

		/* clear vbios scratch 6 and 7 for coming asic reinit */
		WREG32(adev->bios_scratch_reg_offset + 6, 0);
		WREG32(adev->bios_scratch_reg_offset + 7, 0);
	}
	if (ret)
		goto out;

	smu_baco->state = state;
out:
	mutex_unlock(&smu_baco->mutex);
	return ret;
}

int smu_v11_0_baco_enter(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0;

	/* Arcturus does not need this audio workaround */
	if (adev->asic_type != CHIP_ARCTURUS) {
		ret = smu_v11_0_baco_set_armd3_sequence(smu, BACO_SEQ_BACO);
		if (ret)
			return ret;
	}

	ret = smu_v11_0_baco_set_state(smu, SMU_BACO_STATE_ENTER);
	if (ret)
		return ret;

	msleep(10);

	return ret;
}

int smu_v11_0_baco_exit(struct smu_context *smu)
{
	int ret = 0;

	ret = smu_v11_0_baco_set_state(smu, SMU_BACO_STATE_EXIT);
	if (ret)
		return ret;

	return ret;
}

int smu_v11_0_get_dpm_ultimate_freq(struct smu_context *smu, enum smu_clk_type clk_type,
						 uint32_t *min, uint32_t *max)
{
	int ret = 0, clk_id = 0;
	uint32_t param = 0;

	clk_id = smu_clk_get_index(smu, clk_type);
	if (clk_id < 0) {
		ret = -EINVAL;
		goto failed;
	}
	param = (clk_id & 0xffff) << 16;

	if (max) {
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_GetMaxDpmFreq, param);
		if (ret)
			goto failed;
		ret = smu_read_smc_arg(smu, max);
		if (ret)
			goto failed;
	}

	if (min) {
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_GetMinDpmFreq, param);
		if (ret)
			goto failed;
		ret = smu_read_smc_arg(smu, min);
		if (ret)
			goto failed;
	}

failed:
	return ret;
}

int smu_v11_0_set_soft_freq_limited_range(struct smu_context *smu, enum smu_clk_type clk_type,
			    uint32_t min, uint32_t max)
{
	int ret = 0, clk_id = 0;
	uint32_t param;

	clk_id = smu_clk_get_index(smu, clk_type);
	if (clk_id < 0)
		return clk_id;

	if (max > 0) {
		param = (uint32_t)((clk_id << 16) | (max & 0xffff));
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMaxByFreq,
						  param);
		if (ret)
			return ret;
	}

	if (min > 0) {
		param = (uint32_t)((clk_id << 16) | (min & 0xffff));
		ret = smu_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMinByFreq,
						  param);
		if (ret)
			return ret;
	}

	return ret;
}

int smu_v11_0_override_pcie_parameters(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t pcie_gen = 0, pcie_width = 0;
	int ret;

	if (adev->pm.pcie_gen_mask & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN4)
		pcie_gen = 3;
	else if (adev->pm.pcie_gen_mask & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN3)
		pcie_gen = 2;
	else if (adev->pm.pcie_gen_mask & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN2)
		pcie_gen = 1;
	else if (adev->pm.pcie_gen_mask & CAIL_PCIE_LINK_SPEED_SUPPORT_GEN1)
		pcie_gen = 0;

	/* Bit 31:16: LCLK DPM level. 0 is DPM0, and 1 is DPM1
	 * Bit 15:8:  PCIE GEN, 0 to 3 corresponds to GEN1 to GEN4
	 * Bit 7:0:   PCIE lane width, 1 to 7 corresponds is x1 to x32
	 */
	if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X16)
		pcie_width = 6;
	else if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X12)
		pcie_width = 5;
	else if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X8)
		pcie_width = 4;
	else if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X4)
		pcie_width = 3;
	else if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X2)
		pcie_width = 2;
	else if (adev->pm.pcie_mlw_mask & CAIL_PCIE_LINK_WIDTH_SUPPORT_X1)
		pcie_width = 1;

	ret = smu_update_pcie_parameters(smu, pcie_gen, pcie_width);

	if (ret)
		pr_err("[%s] Attempt to override pcie params failed!\n", __func__);

	return ret;

}

int smu_v11_0_set_default_od_settings(struct smu_context *smu, bool initialize, size_t overdrive_table_size)
{
	struct smu_table_context *table_context = &smu->smu_table;
	int ret = 0;

	if (initialize) {
		if (table_context->overdrive_table) {
			return -EINVAL;
		}
		table_context->overdrive_table = kzalloc(overdrive_table_size, GFP_KERNEL);
		if (!table_context->overdrive_table) {
			return -ENOMEM;
		}
		ret = smu_update_table(smu, SMU_TABLE_OVERDRIVE, 0, table_context->overdrive_table, false);
		if (ret) {
			pr_err("Failed to export overdrive table!\n");
			return ret;
		}
	}
	ret = smu_update_table(smu, SMU_TABLE_OVERDRIVE, 0, table_context->overdrive_table, true);
	if (ret) {
		pr_err("Failed to import overdrive table!\n");
		return ret;
	}
	return ret;
}

int smu_v11_0_set_performance_level(struct smu_context *smu,
				    enum amd_dpm_forced_level level)
{
	int ret = 0;
	uint32_t sclk_mask, mclk_mask, soc_mask;

	switch (level) {
	case AMD_DPM_FORCED_LEVEL_HIGH:
		ret = smu_force_dpm_limit_value(smu, true);
		break;
	case AMD_DPM_FORCED_LEVEL_LOW:
		ret = smu_force_dpm_limit_value(smu, false);
		break;
	case AMD_DPM_FORCED_LEVEL_AUTO:
	case AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD:
		ret = smu_unforce_dpm_levels(smu);
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK:
	case AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK:
	case AMD_DPM_FORCED_LEVEL_PROFILE_PEAK:
		ret = smu_get_profiling_clk_mask(smu, level,
						 &sclk_mask,
						 &mclk_mask,
						 &soc_mask);
		if (ret)
			return ret;
		smu_force_clk_levels(smu, SMU_SCLK, 1 << sclk_mask, false);
		smu_force_clk_levels(smu, SMU_MCLK, 1 << mclk_mask, false);
		smu_force_clk_levels(smu, SMU_SOCCLK, 1 << soc_mask, false);
		break;
	case AMD_DPM_FORCED_LEVEL_MANUAL:
	case AMD_DPM_FORCED_LEVEL_PROFILE_EXIT:
	default:
		break;
	}
	return ret;
}

