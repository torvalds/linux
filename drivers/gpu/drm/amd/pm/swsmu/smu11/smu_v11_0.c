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
#include <linux/reboot.h>

#define SMU_11_0_PARTIAL_PPTABLE
#define SWSMU_CODE_LAYER_L3

#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "atomfirmware.h"
#include "amdgpu_atomfirmware.h"
#include "amdgpu_atombios.h"
#include "smu_v11_0.h"
#include "soc15_common.h"
#include "atom.h"
#include "amdgpu_ras.h"
#include "smu_cmn.h"

#include "asic_reg/thm/thm_11_0_2_offset.h"
#include "asic_reg/thm/thm_11_0_2_sh_mask.h"
#include "asic_reg/mp/mp_11_0_offset.h"
#include "asic_reg/mp/mp_11_0_sh_mask.h"
#include "asic_reg/smuio/smuio_11_0_0_offset.h"
#include "asic_reg/smuio/smuio_11_0_0_sh_mask.h"

/*
 * DO NOT use these for err/warn/info/debug messages.
 * Use dev_err, dev_warn, dev_info and dev_dbg instead.
 * They are more MGPU friendly.
 */
#undef pr_err
#undef pr_warn
#undef pr_info
#undef pr_debug

MODULE_FIRMWARE("amdgpu/arcturus_smc.bin");
MODULE_FIRMWARE("amdgpu/navi10_smc.bin");
MODULE_FIRMWARE("amdgpu/navi14_smc.bin");
MODULE_FIRMWARE("amdgpu/navi12_smc.bin");
MODULE_FIRMWARE("amdgpu/sienna_cichlid_smc.bin");
MODULE_FIRMWARE("amdgpu/navy_flounder_smc.bin");
MODULE_FIRMWARE("amdgpu/dimgrey_cavefish_smc.bin");
MODULE_FIRMWARE("amdgpu/beige_goby_smc.bin");

#define SMU11_VOLTAGE_SCALE 4

#define SMU11_MODE1_RESET_WAIT_TIME_IN_MS 500  //500ms

#define smnPCIE_LC_LINK_WIDTH_CNTL		0x11140288
#define PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD_MASK 0x00000070L
#define PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD__SHIFT 0x4
#define smnPCIE_LC_SPEED_CNTL			0x11140290
#define PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE_MASK 0xC000
#define PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE__SHIFT 0xE

#define mmTHM_BACO_CNTL_ARCT			0xA7
#define mmTHM_BACO_CNTL_ARCT_BASE_IDX		0

int smu_v11_0_init_microcode(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	const char *chip_name;
	char fw_name[SMU_FW_NAME_LEN];
	int err = 0;
	const struct smc_firmware_header_v1_0 *hdr;
	const struct common_firmware_header *header;
	struct amdgpu_firmware_info *ucode = NULL;

	if (amdgpu_sriov_vf(adev) &&
			((adev->asic_type == CHIP_NAVI12) ||
			 (adev->asic_type == CHIP_SIENNA_CICHLID)))
		return 0;

	switch (adev->asic_type) {
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
	case CHIP_SIENNA_CICHLID:
		chip_name = "sienna_cichlid";
		break;
	case CHIP_NAVY_FLOUNDER:
		chip_name = "navy_flounder";
		break;
	case CHIP_DIMGREY_CAVEFISH:
		chip_name = "dimgrey_cavefish";
		break;
	case CHIP_BEIGE_GOBY:
		chip_name = "beige_goby";
		break;
	default:
		dev_err(adev->dev, "Unsupported ASIC type %d\n", adev->asic_type);
		return -EINVAL;
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

void smu_v11_0_fini_microcode(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;

	release_firmware(adev->pm.fw);
	adev->pm.fw = NULL;
	adev->pm.fw_version = 0;
}

int smu_v11_0_load_microcode(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	const uint32_t *src;
	const struct smc_firmware_header_v1_0 *hdr;
	uint32_t addr_start = MP1_SRAM;
	uint32_t i;
	uint32_t smc_fw_size;
	uint32_t mp1_fw_flags;

	hdr = (const struct smc_firmware_header_v1_0 *) adev->pm.fw->data;
	src = (const uint32_t *)(adev->pm.fw->data +
		le32_to_cpu(hdr->header.ucode_array_offset_bytes));
	smc_fw_size = hdr->header.ucode_size_bytes;

	for (i = 1; i < smc_fw_size/4 - 1; i++) {
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

	switch (smu->adev->asic_type) {
	case CHIP_ARCTURUS:
		smu->smc_driver_if_version = SMU11_DRIVER_IF_VERSION_ARCT;
		break;
	case CHIP_NAVI10:
		smu->smc_driver_if_version = SMU11_DRIVER_IF_VERSION_NV10;
		break;
	case CHIP_NAVI12:
		smu->smc_driver_if_version = SMU11_DRIVER_IF_VERSION_NV12;
		break;
	case CHIP_NAVI14:
		smu->smc_driver_if_version = SMU11_DRIVER_IF_VERSION_NV14;
		break;
	case CHIP_SIENNA_CICHLID:
		smu->smc_driver_if_version = SMU11_DRIVER_IF_VERSION_Sienna_Cichlid;
		break;
	case CHIP_NAVY_FLOUNDER:
		smu->smc_driver_if_version = SMU11_DRIVER_IF_VERSION_Navy_Flounder;
		break;
	case CHIP_VANGOGH:
		smu->smc_driver_if_version = SMU11_DRIVER_IF_VERSION_VANGOGH;
		break;
	case CHIP_DIMGREY_CAVEFISH:
		smu->smc_driver_if_version = SMU11_DRIVER_IF_VERSION_Dimgrey_Cavefish;
		break;
	case CHIP_BEIGE_GOBY:
		smu->smc_driver_if_version = SMU11_DRIVER_IF_VERSION_Beige_Goby;
		break;
	case CHIP_CYAN_SKILLFISH:
		smu->smc_driver_if_version = SMU11_DRIVER_IF_VERSION_Cyan_Skillfish;
		break;
	default:
		dev_err(smu->adev->dev, "smu unsupported asic type:%d.\n", smu->adev->asic_type);
		smu->smc_driver_if_version = SMU11_DRIVER_IF_VERSION_INV;
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
	if (if_version != smu->smc_driver_if_version) {
		dev_info(smu->adev->dev, "smu driver if version = 0x%08x, smu fw if version = 0x%08x, "
			"smu fw version = 0x%08x (%d.%d.%d)\n",
			smu->smc_driver_if_version, if_version,
			smu_version, smu_major, smu_minor, smu_debug);
		dev_warn(smu->adev->dev, "SMU driver if version not matched\n");
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

	if (!amdgpu_sriov_vf(adev)) {
		hdr = (const struct smc_firmware_header_v1_0 *) adev->pm.fw->data;
		version_major = le16_to_cpu(hdr->header.header_version_major);
		version_minor = le16_to_cpu(hdr->header.header_version_minor);
		if (version_major == 2 && smu->smu_table.boot_values.pp_table_id > 0) {
			dev_info(adev->dev, "use driver provided pptable %d\n", smu->smu_table.boot_values.pp_table_id);
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
			goto out;
		}
	}

	dev_info(adev->dev, "use vbios provided pptable\n");
	index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
						powerplayinfo);

	ret = amdgpu_atombios_get_data_table(adev, index, &atom_table_size, &frev, &crev,
						(uint8_t **)&table);
	if (ret)
		return ret;
	size = atom_table_size;

out:
	if (!smu->smu_table.power_play_table)
		smu->smu_table.power_play_table = table;
	if (!smu->smu_table.power_play_table_size)
		smu->smu_table.power_play_table_size = size;

	return 0;
}

int smu_v11_0_init_smc_tables(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = smu_table->tables;
	int ret = 0;

	smu_table->driver_pptable =
		kzalloc(tables[SMU_TABLE_PPTABLE].size, GFP_KERNEL);
	if (!smu_table->driver_pptable) {
		ret = -ENOMEM;
		goto err0_out;
	}

	smu_table->max_sustainable_clocks =
		kzalloc(sizeof(struct smu_11_0_max_sustainable_clocks), GFP_KERNEL);
	if (!smu_table->max_sustainable_clocks) {
		ret = -ENOMEM;
		goto err1_out;
	}

	/* Arcturus does not support OVERDRIVE */
	if (tables[SMU_TABLE_OVERDRIVE].size) {
		smu_table->overdrive_table =
			kzalloc(tables[SMU_TABLE_OVERDRIVE].size, GFP_KERNEL);
		if (!smu_table->overdrive_table) {
			ret = -ENOMEM;
			goto err2_out;
		}

		smu_table->boot_overdrive_table =
			kzalloc(tables[SMU_TABLE_OVERDRIVE].size, GFP_KERNEL);
		if (!smu_table->boot_overdrive_table) {
			ret = -ENOMEM;
			goto err3_out;
		}

		smu_table->user_overdrive_table =
			kzalloc(tables[SMU_TABLE_OVERDRIVE].size, GFP_KERNEL);
		if (!smu_table->user_overdrive_table) {
			ret = -ENOMEM;
			goto err4_out;
		}

	}

	return 0;

err4_out:
	kfree(smu_table->boot_overdrive_table);
err3_out:
	kfree(smu_table->overdrive_table);
err2_out:
	kfree(smu_table->max_sustainable_clocks);
err1_out:
	kfree(smu_table->driver_pptable);
err0_out:
	return ret;
}

int smu_v11_0_fini_smc_tables(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;

	kfree(smu_table->gpu_metrics_table);
	kfree(smu_table->user_overdrive_table);
	kfree(smu_table->boot_overdrive_table);
	kfree(smu_table->overdrive_table);
	kfree(smu_table->max_sustainable_clocks);
	kfree(smu_table->driver_pptable);
	kfree(smu_table->clocks_table);
	smu_table->gpu_metrics_table = NULL;
	smu_table->user_overdrive_table = NULL;
	smu_table->boot_overdrive_table = NULL;
	smu_table->overdrive_table = NULL;
	smu_table->max_sustainable_clocks = NULL;
	smu_table->driver_pptable = NULL;
	smu_table->clocks_table = NULL;
	kfree(smu_table->hardcode_pptable);
	smu_table->hardcode_pptable = NULL;

	kfree(smu_table->metrics_table);
	kfree(smu_table->watermarks_table);
	smu_table->metrics_table = NULL;
	smu_table->watermarks_table = NULL;
	smu_table->metrics_time = 0;

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

int smu_v11_0_init_power(struct smu_context *smu)
{
	struct smu_power_context *smu_power = &smu->smu_power;
	size_t size = smu->adev->asic_type == CHIP_VANGOGH ?
			sizeof(struct smu_11_5_power_context) :
			sizeof(struct smu_11_0_power_context);

	smu_power->power_context = kzalloc(size, GFP_KERNEL);
	if (!smu_power->power_context)
		return -ENOMEM;
	smu_power->power_context_size = size;

	return 0;
}

int smu_v11_0_fini_power(struct smu_context *smu)
{
	struct smu_power_context *smu_power = &smu->smu_power;

	kfree(smu_power->power_context);
	smu_power->power_context = NULL;
	smu_power->power_context_size = 0;

	return 0;
}

static int smu_v11_0_atom_get_smu_clockinfo(struct amdgpu_device *adev,
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

	ret = amdgpu_atombios_get_data_table(smu->adev, index, &size, &frev, &crev,
				      (uint8_t **)&header);
	if (ret)
		return ret;

	if (header->format_revision != 3) {
		dev_err(smu->adev->dev, "unknown atom_firmware_info version! for smu11\n");
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

	smu_v11_0_atom_get_smu_clockinfo(smu->adev,
					 (uint8_t)SMU11_SYSPLL0_SOCCLK_ID,
					 (uint8_t)0,
					 &smu->smu_table.boot_values.socclk);

	smu_v11_0_atom_get_smu_clockinfo(smu->adev,
					 (uint8_t)SMU11_SYSPLL0_DCEFCLK_ID,
					 (uint8_t)0,
					 &smu->smu_table.boot_values.dcefclk);

	smu_v11_0_atom_get_smu_clockinfo(smu->adev,
					 (uint8_t)SMU11_SYSPLL0_ECLK_ID,
					 (uint8_t)0,
					 &smu->smu_table.boot_values.eclk);

	smu_v11_0_atom_get_smu_clockinfo(smu->adev,
					 (uint8_t)SMU11_SYSPLL0_VCLK_ID,
					 (uint8_t)0,
					 &smu->smu_table.boot_values.vclk);

	smu_v11_0_atom_get_smu_clockinfo(smu->adev,
					 (uint8_t)SMU11_SYSPLL0_DCLK_ID,
					 (uint8_t)0,
					 &smu->smu_table.boot_values.dclk);

	if ((smu->smu_table.boot_values.format_revision == 3) &&
	    (smu->smu_table.boot_values.content_revision >= 2))
		smu_v11_0_atom_get_smu_clockinfo(smu->adev,
						 (uint8_t)SMU11_SYSPLL1_0_FCLK_ID,
						 (uint8_t)SMU11_SYSPLL1_2_ID,
						 &smu->smu_table.boot_values.fclk);

	smu_v11_0_atom_get_smu_clockinfo(smu->adev,
					 (uint8_t)SMU11_SYSPLL3_1_LCLK_ID,
					 (uint8_t)SMU11_SYSPLL3_1_ID,
					 &smu->smu_table.boot_values.lclk);

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

	ret = smu_cmn_send_smc_msg_with_param(smu,
					  SMU_MSG_SetSystemVirtualDramAddrHigh,
					  address_high,
					  NULL);
	if (ret)
		return ret;
	ret = smu_cmn_send_smc_msg_with_param(smu,
					  SMU_MSG_SetSystemVirtualDramAddrLow,
					  address_low,
					  NULL);
	if (ret)
		return ret;

	address = memory_pool->mc_address;
	address_high = (uint32_t)upper_32_bits(address);
	address_low  = (uint32_t)lower_32_bits(address);

	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_DramLogSetDramAddrHigh,
					  address_high, NULL);
	if (ret)
		return ret;
	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_DramLogSetDramAddrLow,
					  address_low, NULL);
	if (ret)
		return ret;
	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_DramLogSetDramSize,
					  (uint32_t)memory_pool->size, NULL);
	if (ret)
		return ret;

	return ret;
}

int smu_v11_0_set_min_deep_sleep_dcefclk(struct smu_context *smu, uint32_t clk)
{
	int ret;

	ret = smu_cmn_send_smc_msg_with_param(smu,
					  SMU_MSG_SetMinDeepSleepDcefclk, clk, NULL);
	if (ret)
		dev_err(smu->adev->dev, "SMU11 attempt to set divider for DCEFCLK Failed!");

	return ret;
}

int smu_v11_0_set_driver_table_location(struct smu_context *smu)
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

int smu_v11_0_set_tool_table_location(struct smu_context *smu)
{
	int ret = 0;
	struct smu_table *tool_table = &smu->smu_table.tables[SMU_TABLE_PMSTATUSLOG];

	if (tool_table->mc_address) {
		ret = smu_cmn_send_smc_msg_with_param(smu,
				SMU_MSG_SetToolsDramAddrHigh,
				upper_32_bits(tool_table->mc_address),
				NULL);
		if (!ret)
			ret = smu_cmn_send_smc_msg_with_param(smu,
				SMU_MSG_SetToolsDramAddrLow,
				lower_32_bits(tool_table->mc_address),
				NULL);
	}

	return ret;
}

int smu_v11_0_init_display_count(struct smu_context *smu, uint32_t count)
{
	struct amdgpu_device *adev = smu->adev;

	/* Navy_Flounder/Dimgrey_Cavefish do not support to change
	 * display num currently
	 */
	if (adev->asic_type >= CHIP_NAVY_FLOUNDER &&
	    adev->asic_type <= CHIP_BEIGE_GOBY)
		return 0;

	return smu_cmn_send_smc_msg_with_param(smu,
					       SMU_MSG_NumOfDisplays,
					       count,
					       NULL);
}


int smu_v11_0_set_allowed_mask(struct smu_context *smu)
{
	struct smu_feature *feature = &smu->smu_feature;
	int ret = 0;
	uint32_t feature_mask[2];

	if (bitmap_empty(feature->allowed, SMU_FEATURE_MAX) || feature->feature_num < 64) {
		ret = -EINVAL;
		goto failed;
	}

	bitmap_copy((unsigned long *)feature_mask, feature->allowed, 64);

	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetAllowedFeaturesMaskHigh,
					  feature_mask[1], NULL);
	if (ret)
		goto failed;

	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetAllowedFeaturesMaskLow,
					  feature_mask[0], NULL);
	if (ret)
		goto failed;

failed:
	return ret;
}

int smu_v11_0_system_features_control(struct smu_context *smu,
					     bool en)
{
	struct smu_feature *feature = &smu->smu_feature;
	uint32_t feature_mask[2];
	int ret = 0;

	ret = smu_cmn_send_smc_msg(smu, (en ? SMU_MSG_EnableAllSmuFeatures :
				     SMU_MSG_DisableAllSmuFeatures), NULL);
	if (ret)
		return ret;

	bitmap_zero(feature->enabled, feature->feature_num);
	bitmap_zero(feature->supported, feature->feature_num);

	if (en) {
		ret = smu_cmn_get_enabled_mask(smu, feature_mask, 2);
		if (ret)
			return ret;

		bitmap_copy(feature->enabled, (unsigned long *)&feature_mask,
			    feature->feature_num);
		bitmap_copy(feature->supported, (unsigned long *)&feature_mask,
			    feature->feature_num);
	}

	return ret;
}

int smu_v11_0_notify_display_change(struct smu_context *smu)
{
	int ret = 0;

	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT) &&
	    smu->adev->gmc.vram_type == AMDGPU_VRAM_TYPE_HBM)
		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetUclkFastSwitch, 1, NULL);

	return ret;
}

static int
smu_v11_0_get_max_sustainable_clock(struct smu_context *smu, uint32_t *clock,
				    enum smu_clk_type clock_select)
{
	int ret = 0;
	int clk_id;

	if ((smu_cmn_to_asic_specific_index(smu, CMN2ASIC_MAPPING_MSG, SMU_MSG_GetDcModeMaxDpmFreq) < 0) ||
	    (smu_cmn_to_asic_specific_index(smu, CMN2ASIC_MAPPING_MSG, SMU_MSG_GetMaxDpmFreq) < 0))
		return 0;

	clk_id = smu_cmn_to_asic_specific_index(smu,
						CMN2ASIC_MAPPING_CLK,
						clock_select);
	if (clk_id < 0)
		return -EINVAL;

	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_GetDcModeMaxDpmFreq,
					  clk_id << 16, clock);
	if (ret) {
		dev_err(smu->adev->dev, "[GetMaxSustainableClock] Failed to get max DC clock from SMC!");
		return ret;
	}

	if (*clock != 0)
		return 0;

	/* if DC limit is zero, return AC limit */
	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_GetMaxDpmFreq,
					  clk_id << 16, clock);
	if (ret) {
		dev_err(smu->adev->dev, "[GetMaxSustainableClock] failed to get max AC clock from SMC!");
		return ret;
	}

	return 0;
}

int smu_v11_0_init_max_sustainable_clocks(struct smu_context *smu)
{
	struct smu_11_0_max_sustainable_clocks *max_sustainable_clocks =
			smu->smu_table.max_sustainable_clocks;
	int ret = 0;

	max_sustainable_clocks->uclock = smu->smu_table.boot_values.uclk / 100;
	max_sustainable_clocks->soc_clock = smu->smu_table.boot_values.socclk / 100;
	max_sustainable_clocks->dcef_clock = smu->smu_table.boot_values.dcefclk / 100;
	max_sustainable_clocks->display_clock = 0xFFFFFFFF;
	max_sustainable_clocks->phy_clock = 0xFFFFFFFF;
	max_sustainable_clocks->pixel_clock = 0xFFFFFFFF;

	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT)) {
		ret = smu_v11_0_get_max_sustainable_clock(smu,
							  &(max_sustainable_clocks->uclock),
							  SMU_UCLK);
		if (ret) {
			dev_err(smu->adev->dev, "[%s] failed to get max UCLK from SMC!",
			       __func__);
			return ret;
		}
	}

	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_SOCCLK_BIT)) {
		ret = smu_v11_0_get_max_sustainable_clock(smu,
							  &(max_sustainable_clocks->soc_clock),
							  SMU_SOCCLK);
		if (ret) {
			dev_err(smu->adev->dev, "[%s] failed to get max SOCCLK from SMC!",
			       __func__);
			return ret;
		}
	}

	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_DCEFCLK_BIT)) {
		ret = smu_v11_0_get_max_sustainable_clock(smu,
							  &(max_sustainable_clocks->dcef_clock),
							  SMU_DCEFCLK);
		if (ret) {
			dev_err(smu->adev->dev, "[%s] failed to get max DCEFCLK from SMC!",
			       __func__);
			return ret;
		}

		ret = smu_v11_0_get_max_sustainable_clock(smu,
							  &(max_sustainable_clocks->display_clock),
							  SMU_DISPCLK);
		if (ret) {
			dev_err(smu->adev->dev, "[%s] failed to get max DISPCLK from SMC!",
			       __func__);
			return ret;
		}
		ret = smu_v11_0_get_max_sustainable_clock(smu,
							  &(max_sustainable_clocks->phy_clock),
							  SMU_PHYCLK);
		if (ret) {
			dev_err(smu->adev->dev, "[%s] failed to get max PHYCLK from SMC!",
			       __func__);
			return ret;
		}
		ret = smu_v11_0_get_max_sustainable_clock(smu,
							  &(max_sustainable_clocks->pixel_clock),
							  SMU_PIXCLK);
		if (ret) {
			dev_err(smu->adev->dev, "[%s] failed to get max PIXCLK from SMC!",
			       __func__);
			return ret;
		}
	}

	if (max_sustainable_clocks->soc_clock < max_sustainable_clocks->uclock)
		max_sustainable_clocks->uclock = max_sustainable_clocks->soc_clock;

	return 0;
}

int smu_v11_0_get_current_power_limit(struct smu_context *smu,
				      uint32_t *power_limit)
{
	int power_src;
	int ret = 0;

	if (!smu_cmn_feature_is_enabled(smu, SMU_FEATURE_PPT_BIT))
		return -EINVAL;

	power_src = smu_cmn_to_asic_specific_index(smu,
					CMN2ASIC_MAPPING_PWR,
					smu->adev->pm.ac_power ?
					SMU_POWER_SOURCE_AC :
					SMU_POWER_SOURCE_DC);
	if (power_src < 0)
		return -EINVAL;

	/*
	 * BIT 24-31: ControllerId (only PPT0 is supported for now)
	 * BIT 16-23: PowerSource
	 */
	ret = smu_cmn_send_smc_msg_with_param(smu,
					  SMU_MSG_GetPptLimit,
					  (0 << 24) | (power_src << 16),
					  power_limit);
	if (ret)
		dev_err(smu->adev->dev, "[%s] get PPT limit failed!", __func__);

	return ret;
}

int smu_v11_0_set_power_limit(struct smu_context *smu, uint32_t n)
{
	int power_src;
	int ret = 0;

	if (!smu_cmn_feature_is_enabled(smu, SMU_FEATURE_PPT_BIT)) {
		dev_err(smu->adev->dev, "Setting new power limit is not supported!\n");
		return -EOPNOTSUPP;
	}

	power_src = smu_cmn_to_asic_specific_index(smu,
					CMN2ASIC_MAPPING_PWR,
					smu->adev->pm.ac_power ?
					SMU_POWER_SOURCE_AC :
					SMU_POWER_SOURCE_DC);
	if (power_src < 0)
		return -EINVAL;

	/*
	 * BIT 24-31: ControllerId (only PPT0 is supported for now)
	 * BIT 16-23: PowerSource
	 * BIT 0-15: PowerLimit
	 */
	n &= 0xFFFF;
	n |= 0 << 24;
	n |= (power_src) << 16;
	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetPptLimit, n, NULL);
	if (ret) {
		dev_err(smu->adev->dev, "[%s] Set power limit Failed!\n", __func__);
		return ret;
	}

	smu->current_power_limit = n;

	return 0;
}

static int smu_v11_0_ack_ac_dc_interrupt(struct smu_context *smu)
{
	return smu_cmn_send_smc_msg(smu,
				SMU_MSG_ReenableAcDcInterrupt,
				NULL);
}

static int smu_v11_0_process_pending_interrupt(struct smu_context *smu)
{
	int ret = 0;

	if (smu->dc_controlled_by_gpio &&
	    smu_cmn_feature_is_enabled(smu, SMU_FEATURE_ACDC_BIT))
		ret = smu_v11_0_ack_ac_dc_interrupt(smu);

	return ret;
}

void smu_v11_0_interrupt_work(struct smu_context *smu)
{
	if (smu_v11_0_ack_ac_dc_interrupt(smu))
		dev_err(smu->adev->dev, "Ack AC/DC interrupt Failed!\n");
}

int smu_v11_0_enable_thermal_alert(struct smu_context *smu)
{
	int ret = 0;

	if (smu->smu_table.thermal_controller_type) {
		ret = amdgpu_irq_get(smu->adev, &smu->irq_source, 0);
		if (ret)
			return ret;
	}

	/*
	 * After init there might have been missed interrupts triggered
	 * before driver registers for interrupt (Ex. AC/DC).
	 */
	return smu_v11_0_process_pending_interrupt(smu);
}

int smu_v11_0_disable_thermal_alert(struct smu_context *smu)
{
	return amdgpu_irq_put(smu->adev, &smu->irq_source, 0);
}

static uint16_t convert_to_vddc(uint8_t vid)
{
	return (uint16_t) ((6200 - (vid * 25)) / SMU11_VOLTAGE_SCALE);
}

int smu_v11_0_get_gfx_vdd(struct smu_context *smu, uint32_t *value)
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

int
smu_v11_0_display_clock_voltage_request(struct smu_context *smu,
					struct pp_display_clock_request
					*clock_req)
{
	enum amd_pp_clock_type clk_type = clock_req->clock_type;
	int ret = 0;
	enum smu_clk_type clk_select = 0;
	uint32_t clk_freq = clock_req->clock_freq_in_khz / 1000;

	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_DCEFCLK_BIT) ||
		smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT)) {
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
			dev_info(smu->adev->dev, "[%s] Invalid Clock Type!", __func__);
			ret = -EINVAL;
			break;
		}

		if (ret)
			goto failed;

		if (clk_select == SMU_UCLK && smu->disable_uclk_switch)
			return 0;

		ret = smu_v11_0_set_hard_freq_limited_range(smu, clk_select, clk_freq, 0);

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
	case CHIP_NAVI10:
	case CHIP_NAVI14:
	case CHIP_NAVI12:
	case CHIP_SIENNA_CICHLID:
	case CHIP_NAVY_FLOUNDER:
	case CHIP_DIMGREY_CAVEFISH:
	case CHIP_BEIGE_GOBY:
	case CHIP_VANGOGH:
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

uint32_t
smu_v11_0_get_fan_control_mode(struct smu_context *smu)
{
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_FAN_CONTROL_BIT))
		return AMD_FAN_CTRL_AUTO;
	else
		return smu->user_dpm_profile.fan_mode;
}

static int
smu_v11_0_auto_fan_control(struct smu_context *smu, bool auto_fan_control)
{
	int ret = 0;

	if (!smu_cmn_feature_is_supported(smu, SMU_FEATURE_FAN_CONTROL_BIT))
		return 0;

	ret = smu_cmn_feature_set_enabled(smu, SMU_FEATURE_FAN_CONTROL_BIT, auto_fan_control);
	if (ret)
		dev_err(smu->adev->dev, "[%s]%s smc FAN CONTROL feature failed!",
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

int smu_v11_0_set_fan_speed_rpm(struct smu_context *smu,
				uint32_t speed)
{
	struct amdgpu_device *adev = smu->adev;
	/*
	 * crystal_clock_freq used for fan speed rpm calculation is
	 * always 25Mhz. So, hardcode it as 2500(in 10K unit).
	 */
	uint32_t crystal_clock_freq = 2500;
	uint32_t tach_period;
	int ret;

	ret = smu_v11_0_auto_fan_control(smu, 0);
	if (ret)
		return ret;

	/*
	 * To prevent from possible overheat, some ASICs may have requirement
	 * for minimum fan speed:
	 * - For some NV10 SKU, the fan speed cannot be set lower than
	 *   700 RPM.
	 * - For some Sienna Cichlid SKU, the fan speed cannot be set
	 *   lower than 500 RPM.
	 */
	tach_period = 60 * crystal_clock_freq * 10000 / (8 * speed);
	WREG32_SOC15(THM, 0, mmCG_TACH_CTRL,
		     REG_SET_FIELD(RREG32_SOC15(THM, 0, mmCG_TACH_CTRL),
				   CG_TACH_CTRL, TARGET_PERIOD,
				   tach_period));

	ret = smu_v11_0_set_fan_static_mode(smu, FDO_PWM_MODE_STATIC_RPM);

	return ret;
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
		dev_err(smu->adev->dev, "[%s]Set fan control mode failed!", __func__);
		return -EINVAL;
	}

	return ret;
}

int smu_v11_0_set_xgmi_pstate(struct smu_context *smu,
				     uint32_t pstate)
{
	return smu_cmn_send_smc_msg_with_param(smu,
					       SMU_MSG_SetXgmiMode,
					       pstate ? XGMI_MODE_PSTATE_D0 : XGMI_MODE_PSTATE_D3,
					  NULL);
}

static int smu_v11_0_set_irq_state(struct amdgpu_device *adev,
				   struct amdgpu_irq_src *source,
				   unsigned tyep,
				   enum amdgpu_interrupt_state state)
{
	struct smu_context *smu = &adev->smu;
	uint32_t low, high;
	uint32_t val = 0;

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		/* For THM irqs */
		val = RREG32_SOC15(THM, 0, mmTHM_THERMAL_INT_CTRL);
		val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, THERM_INTH_MASK, 1);
		val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, THERM_INTL_MASK, 1);
		WREG32_SOC15(THM, 0, mmTHM_THERMAL_INT_CTRL, val);

		WREG32_SOC15(THM, 0, mmTHM_THERMAL_INT_ENA, 0);

		/* For MP1 SW irqs */
		val = RREG32_SOC15(MP1, 0, mmMP1_SMN_IH_SW_INT_CTRL);
		val = REG_SET_FIELD(val, MP1_SMN_IH_SW_INT_CTRL, INT_MASK, 1);
		WREG32_SOC15(MP1, 0, mmMP1_SMN_IH_SW_INT_CTRL, val);

		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		/* For THM irqs */
		low = max(SMU_THERMAL_MINIMUM_ALERT_TEMP,
				smu->thermal_range.min / SMU_TEMPERATURE_UNITS_PER_CENTIGRADES);
		high = min(SMU_THERMAL_MAXIMUM_ALERT_TEMP,
				smu->thermal_range.software_shutdown_temp);

		val = RREG32_SOC15(THM, 0, mmTHM_THERMAL_INT_CTRL);
		val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, MAX_IH_CREDIT, 5);
		val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, THERM_IH_HW_ENA, 1);
		val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, THERM_INTH_MASK, 0);
		val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, THERM_INTL_MASK, 0);
		val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, DIG_THERM_INTH, (high & 0xff));
		val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, DIG_THERM_INTL, (low & 0xff));
		val = val & (~THM_THERMAL_INT_CTRL__THERM_TRIGGER_MASK_MASK);
		WREG32_SOC15(THM, 0, mmTHM_THERMAL_INT_CTRL, val);

		val = (1 << THM_THERMAL_INT_ENA__THERM_INTH_CLR__SHIFT);
		val |= (1 << THM_THERMAL_INT_ENA__THERM_INTL_CLR__SHIFT);
		val |= (1 << THM_THERMAL_INT_ENA__THERM_TRIGGER_CLR__SHIFT);
		WREG32_SOC15(THM, 0, mmTHM_THERMAL_INT_ENA, val);

		/* For MP1 SW irqs */
		val = RREG32_SOC15(MP1, 0, mmMP1_SMN_IH_SW_INT);
		val = REG_SET_FIELD(val, MP1_SMN_IH_SW_INT, ID, 0xFE);
		val = REG_SET_FIELD(val, MP1_SMN_IH_SW_INT, VALID, 0);
		WREG32_SOC15(MP1, 0, mmMP1_SMN_IH_SW_INT, val);

		val = RREG32_SOC15(MP1, 0, mmMP1_SMN_IH_SW_INT_CTRL);
		val = REG_SET_FIELD(val, MP1_SMN_IH_SW_INT_CTRL, INT_MASK, 0);
		WREG32_SOC15(MP1, 0, mmMP1_SMN_IH_SW_INT_CTRL, val);

		break;
	default:
		break;
	}

	return 0;
}

#define THM_11_0__SRCID__THM_DIG_THERM_L2H		0		/* ASIC_TEMP > CG_THERMAL_INT.DIG_THERM_INTH  */
#define THM_11_0__SRCID__THM_DIG_THERM_H2L		1		/* ASIC_TEMP < CG_THERMAL_INT.DIG_THERM_INTL  */

#define SMUIO_11_0__SRCID__SMUIO_GPIO19			83

static int smu_v11_0_irq_process(struct amdgpu_device *adev,
				 struct amdgpu_irq_src *source,
				 struct amdgpu_iv_entry *entry)
{
	struct smu_context *smu = &adev->smu;
	uint32_t client_id = entry->client_id;
	uint32_t src_id = entry->src_id;
	/*
	 * ctxid is used to distinguish different
	 * events for SMCToHost interrupt.
	 */
	uint32_t ctxid = entry->src_data[0];
	uint32_t data;

	if (client_id == SOC15_IH_CLIENTID_THM) {
		switch (src_id) {
		case THM_11_0__SRCID__THM_DIG_THERM_L2H:
			dev_emerg(adev->dev, "ERROR: GPU over temperature range(SW CTF) detected!\n");
			/*
			 * SW CTF just occurred.
			 * Try to do a graceful shutdown to prevent further damage.
			 */
			dev_emerg(adev->dev, "ERROR: System is going to shutdown due to GPU SW CTF!\n");
			orderly_poweroff(true);
		break;
		case THM_11_0__SRCID__THM_DIG_THERM_H2L:
			dev_emerg(adev->dev, "ERROR: GPU under temperature range detected\n");
		break;
		default:
			dev_emerg(adev->dev, "ERROR: GPU under temperature range unknown src id (%d)\n",
				src_id);
		break;
		}
	} else if (client_id == SOC15_IH_CLIENTID_ROM_SMUIO) {
		dev_emerg(adev->dev, "ERROR: GPU HW Critical Temperature Fault(aka CTF) detected!\n");
		/*
		 * HW CTF just occurred. Shutdown to prevent further damage.
		 */
		dev_emerg(adev->dev, "ERROR: System is going to shutdown due to GPU HW CTF!\n");
		orderly_poweroff(true);
	} else if (client_id == SOC15_IH_CLIENTID_MP1) {
		if (src_id == 0xfe) {
			/* ACK SMUToHost interrupt */
			data = RREG32_SOC15(MP1, 0, mmMP1_SMN_IH_SW_INT_CTRL);
			data = REG_SET_FIELD(data, MP1_SMN_IH_SW_INT_CTRL, INT_ACK, 1);
			WREG32_SOC15(MP1, 0, mmMP1_SMN_IH_SW_INT_CTRL, data);

			switch (ctxid) {
			case 0x3:
				dev_dbg(adev->dev, "Switched to AC mode!\n");
				schedule_work(&smu->interrupt_work);
				break;
			case 0x4:
				dev_dbg(adev->dev, "Switched to DC mode!\n");
				schedule_work(&smu->interrupt_work);
				break;
			case 0x7:
				/*
				 * Increment the throttle interrupt counter
				 */
				atomic64_inc(&smu->throttle_int_counter);

				if (!atomic_read(&adev->throttling_logging_enabled))
					return 0;

				if (__ratelimit(&adev->throttling_logging_rs))
					schedule_work(&smu->throttling_logging_work);

				break;
			}
		}
	}

	return 0;
}

static const struct amdgpu_irq_src_funcs smu_v11_0_irq_funcs =
{
	.set = smu_v11_0_set_irq_state,
	.process = smu_v11_0_irq_process,
};

int smu_v11_0_register_irq_handler(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	struct amdgpu_irq_src *irq_src = &smu->irq_source;
	int ret = 0;

	irq_src->num_types = 1;
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

	/* Register CTF(GPIO_19) interrupt */
	ret = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_ROM_SMUIO,
				SMUIO_11_0__SRCID__SMUIO_GPIO19,
				irq_src);
	if (ret)
		return ret;

	ret = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_MP1,
				0xfe,
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
	return smu_cmn_send_smc_msg(smu, SMU_MSG_BacoAudioD3PME, NULL);
}

int smu_v11_0_baco_set_armd3_sequence(struct smu_context *smu,
				      enum smu_v11_0_baco_seq baco_seq)
{
	return smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_ArmD3, baco_seq, NULL);
}

bool smu_v11_0_baco_is_support(struct smu_context *smu)
{
	struct smu_baco_context *smu_baco = &smu->smu_baco;

	if (amdgpu_sriov_vf(smu->adev) || !smu_baco->platform_support)
		return false;

	/* Arcturus does not support this bit mask */
	if (smu_cmn_feature_is_supported(smu, SMU_FEATURE_BACO_BIT) &&
	   !smu_cmn_feature_is_enabled(smu, SMU_FEATURE_BACO_BIT))
		return false;

	return true;
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

#define D3HOT_BACO_SEQUENCE 0
#define D3HOT_BAMACO_SEQUENCE 2

int smu_v11_0_baco_set_state(struct smu_context *smu, enum smu_baco_state state)
{
	struct smu_baco_context *smu_baco = &smu->smu_baco;
	struct amdgpu_device *adev = smu->adev;
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);
	uint32_t data;
	int ret = 0;

	if (smu_v11_0_baco_get_state(smu) == state)
		return 0;

	mutex_lock(&smu_baco->mutex);

	if (state == SMU_BACO_STATE_ENTER) {
		switch (adev->asic_type) {
		case CHIP_SIENNA_CICHLID:
		case CHIP_NAVY_FLOUNDER:
		case CHIP_DIMGREY_CAVEFISH:
		case CHIP_BEIGE_GOBY:
			if (amdgpu_runtime_pm == 2)
				ret = smu_cmn_send_smc_msg_with_param(smu,
								      SMU_MSG_EnterBaco,
								      D3HOT_BAMACO_SEQUENCE,
								      NULL);
			else
				ret = smu_cmn_send_smc_msg_with_param(smu,
								      SMU_MSG_EnterBaco,
								      D3HOT_BACO_SEQUENCE,
								      NULL);
			break;
		default:
			if (!ras || !adev->ras_enabled ||
			    adev->gmc.xgmi.pending_reset) {
				if (adev->asic_type == CHIP_ARCTURUS) {
					data = RREG32_SOC15(THM, 0, mmTHM_BACO_CNTL_ARCT);
					data |= 0x80000000;
					WREG32_SOC15(THM, 0, mmTHM_BACO_CNTL_ARCT, data);
				} else {
					data = RREG32_SOC15(THM, 0, mmTHM_BACO_CNTL);
					data |= 0x80000000;
					WREG32_SOC15(THM, 0, mmTHM_BACO_CNTL, data);
				}

				ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_EnterBaco, 0, NULL);
			} else {
				ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_EnterBaco, 1, NULL);
			}
			break;
		}

	} else {
		ret = smu_cmn_send_smc_msg(smu, SMU_MSG_ExitBaco, NULL);
		if (ret)
			goto out;

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
	int ret = 0;

	ret = smu_v11_0_baco_set_state(smu, SMU_BACO_STATE_ENTER);
	if (ret)
		return ret;

	msleep(10);

	return ret;
}

int smu_v11_0_baco_exit(struct smu_context *smu)
{
	return smu_v11_0_baco_set_state(smu, SMU_BACO_STATE_EXIT);
}

int smu_v11_0_mode1_reset(struct smu_context *smu)
{
	int ret = 0;

	ret = smu_cmn_send_smc_msg(smu, SMU_MSG_Mode1Reset, NULL);
	if (!ret)
		msleep(SMU11_MODE1_RESET_WAIT_TIME_IN_MS);

	return ret;
}

int smu_v11_0_set_light_sbr(struct smu_context *smu, bool enable)
{
	int ret = 0;

	ret =  smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_LightSBR, enable ? 1 : 0, NULL);

	return ret;
}


int smu_v11_0_get_dpm_ultimate_freq(struct smu_context *smu, enum smu_clk_type clk_type,
						 uint32_t *min, uint32_t *max)
{
	int ret = 0, clk_id = 0;
	uint32_t param = 0;
	uint32_t clock_limit;

	if (!smu_cmn_clk_dpm_is_enabled(smu, clk_type)) {
		switch (clk_type) {
		case SMU_MCLK:
		case SMU_UCLK:
			clock_limit = smu->smu_table.boot_values.uclk;
			break;
		case SMU_GFXCLK:
		case SMU_SCLK:
			clock_limit = smu->smu_table.boot_values.gfxclk;
			break;
		case SMU_SOCCLK:
			clock_limit = smu->smu_table.boot_values.socclk;
			break;
		default:
			clock_limit = 0;
			break;
		}

		/* clock in Mhz unit */
		if (min)
			*min = clock_limit / 100;
		if (max)
			*max = clock_limit / 100;

		return 0;
	}

	clk_id = smu_cmn_to_asic_specific_index(smu,
						CMN2ASIC_MAPPING_CLK,
						clk_type);
	if (clk_id < 0) {
		ret = -EINVAL;
		goto failed;
	}
	param = (clk_id & 0xffff) << 16;

	if (max) {
		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_GetMaxDpmFreq, param, max);
		if (ret)
			goto failed;
	}

	if (min) {
		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_GetMinDpmFreq, param, min);
		if (ret)
			goto failed;
	}

failed:
	return ret;
}

int smu_v11_0_set_soft_freq_limited_range(struct smu_context *smu,
					  enum smu_clk_type clk_type,
					  uint32_t min,
					  uint32_t max)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0, clk_id = 0;
	uint32_t param;

	if (!smu_cmn_clk_dpm_is_enabled(smu, clk_type))
		return 0;

	clk_id = smu_cmn_to_asic_specific_index(smu,
						CMN2ASIC_MAPPING_CLK,
						clk_type);
	if (clk_id < 0)
		return clk_id;

	if (clk_type == SMU_GFXCLK)
		amdgpu_gfx_off_ctrl(adev, false);

	if (max > 0) {
		param = (uint32_t)((clk_id << 16) | (max & 0xffff));
		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMaxByFreq,
						  param, NULL);
		if (ret)
			goto out;
	}

	if (min > 0) {
		param = (uint32_t)((clk_id << 16) | (min & 0xffff));
		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMinByFreq,
						  param, NULL);
		if (ret)
			goto out;
	}

out:
	if (clk_type == SMU_GFXCLK)
		amdgpu_gfx_off_ctrl(adev, true);

	return ret;
}

int smu_v11_0_set_hard_freq_limited_range(struct smu_context *smu,
					  enum smu_clk_type clk_type,
					  uint32_t min,
					  uint32_t max)
{
	int ret = 0, clk_id = 0;
	uint32_t param;

	if (min <= 0 && max <= 0)
		return -EINVAL;

	if (!smu_cmn_clk_dpm_is_enabled(smu, clk_type))
		return 0;

	clk_id = smu_cmn_to_asic_specific_index(smu,
						CMN2ASIC_MAPPING_CLK,
						clk_type);
	if (clk_id < 0)
		return clk_id;

	if (max > 0) {
		param = (uint32_t)((clk_id << 16) | (max & 0xffff));
		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetHardMaxByFreq,
						  param, NULL);
		if (ret)
			return ret;
	}

	if (min > 0) {
		param = (uint32_t)((clk_id << 16) | (min & 0xffff));
		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetHardMinByFreq,
						  param, NULL);
		if (ret)
			return ret;
	}

	return ret;
}

int smu_v11_0_set_performance_level(struct smu_context *smu,
				    enum amd_dpm_forced_level level)
{
	struct smu_11_0_dpm_context *dpm_context =
				smu->smu_dpm.dpm_context;
	struct smu_11_0_dpm_table *gfx_table =
				&dpm_context->dpm_tables.gfx_table;
	struct smu_11_0_dpm_table *mem_table =
				&dpm_context->dpm_tables.uclk_table;
	struct smu_11_0_dpm_table *soc_table =
				&dpm_context->dpm_tables.soc_table;
	struct smu_umd_pstate_table *pstate_table =
				&smu->pstate_table;
	struct amdgpu_device *adev = smu->adev;
	uint32_t sclk_min = 0, sclk_max = 0;
	uint32_t mclk_min = 0, mclk_max = 0;
	uint32_t socclk_min = 0, socclk_max = 0;
	int ret = 0;

	switch (level) {
	case AMD_DPM_FORCED_LEVEL_HIGH:
		sclk_min = sclk_max = gfx_table->max;
		mclk_min = mclk_max = mem_table->max;
		socclk_min = socclk_max = soc_table->max;
		break;
	case AMD_DPM_FORCED_LEVEL_LOW:
		sclk_min = sclk_max = gfx_table->min;
		mclk_min = mclk_max = mem_table->min;
		socclk_min = socclk_max = soc_table->min;
		break;
	case AMD_DPM_FORCED_LEVEL_AUTO:
		sclk_min = gfx_table->min;
		sclk_max = gfx_table->max;
		mclk_min = mem_table->min;
		mclk_max = mem_table->max;
		socclk_min = soc_table->min;
		socclk_max = soc_table->max;
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD:
		sclk_min = sclk_max = pstate_table->gfxclk_pstate.standard;
		mclk_min = mclk_max = pstate_table->uclk_pstate.standard;
		socclk_min = socclk_max = pstate_table->socclk_pstate.standard;
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_MIN_SCLK:
		sclk_min = sclk_max = pstate_table->gfxclk_pstate.min;
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_MIN_MCLK:
		mclk_min = mclk_max = pstate_table->uclk_pstate.min;
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_PEAK:
		sclk_min = sclk_max = pstate_table->gfxclk_pstate.peak;
		mclk_min = mclk_max = pstate_table->uclk_pstate.peak;
		socclk_min = socclk_max = pstate_table->socclk_pstate.peak;
		break;
	case AMD_DPM_FORCED_LEVEL_MANUAL:
	case AMD_DPM_FORCED_LEVEL_PROFILE_EXIT:
		return 0;
	default:
		dev_err(adev->dev, "Invalid performance level %d\n", level);
		return -EINVAL;
	}

	/*
	 * Separate MCLK and SOCCLK soft min/max settings are not allowed
	 * on Arcturus.
	 */
	if (adev->asic_type == CHIP_ARCTURUS) {
		mclk_min = mclk_max = 0;
		socclk_min = socclk_max = 0;
	}

	if (sclk_min && sclk_max) {
		ret = smu_v11_0_set_soft_freq_limited_range(smu,
							    SMU_GFXCLK,
							    sclk_min,
							    sclk_max);
		if (ret)
			return ret;
	}

	if (mclk_min && mclk_max) {
		ret = smu_v11_0_set_soft_freq_limited_range(smu,
							    SMU_MCLK,
							    mclk_min,
							    mclk_max);
		if (ret)
			return ret;
	}

	if (socclk_min && socclk_max) {
		ret = smu_v11_0_set_soft_freq_limited_range(smu,
							    SMU_SOCCLK,
							    socclk_min,
							    socclk_max);
		if (ret)
			return ret;
	}

	return ret;
}

int smu_v11_0_set_power_source(struct smu_context *smu,
			       enum smu_power_src_type power_src)
{
	int pwr_source;

	pwr_source = smu_cmn_to_asic_specific_index(smu,
						    CMN2ASIC_MAPPING_PWR,
						    (uint32_t)power_src);
	if (pwr_source < 0)
		return -EINVAL;

	return smu_cmn_send_smc_msg_with_param(smu,
					SMU_MSG_NotifyPowerSource,
					pwr_source,
					NULL);
}

int smu_v11_0_get_dpm_freq_by_index(struct smu_context *smu,
				    enum smu_clk_type clk_type,
				    uint16_t level,
				    uint32_t *value)
{
	int ret = 0, clk_id = 0;
	uint32_t param;

	if (!value)
		return -EINVAL;

	if (!smu_cmn_clk_dpm_is_enabled(smu, clk_type))
		return 0;

	clk_id = smu_cmn_to_asic_specific_index(smu,
						CMN2ASIC_MAPPING_CLK,
						clk_type);
	if (clk_id < 0)
		return clk_id;

	param = (uint32_t)(((clk_id & 0xffff) << 16) | (level & 0xffff));

	ret = smu_cmn_send_smc_msg_with_param(smu,
					  SMU_MSG_GetDpmFreqByIndex,
					  param,
					  value);
	if (ret)
		return ret;

	/*
	 * BIT31:  0 - Fine grained DPM, 1 - Dicrete DPM
	 * now, we un-support it
	 */
	*value = *value & 0x7fffffff;

	return ret;
}

int smu_v11_0_get_dpm_level_count(struct smu_context *smu,
				  enum smu_clk_type clk_type,
				  uint32_t *value)
{
	return smu_v11_0_get_dpm_freq_by_index(smu,
					       clk_type,
					       0xff,
					       value);
}

int smu_v11_0_set_single_dpm_table(struct smu_context *smu,
				   enum smu_clk_type clk_type,
				   struct smu_11_0_dpm_table *single_dpm_table)
{
	int ret = 0;
	uint32_t clk;
	int i;

	ret = smu_v11_0_get_dpm_level_count(smu,
					    clk_type,
					    &single_dpm_table->count);
	if (ret) {
		dev_err(smu->adev->dev, "[%s] failed to get dpm levels!\n", __func__);
		return ret;
	}

	for (i = 0; i < single_dpm_table->count; i++) {
		ret = smu_v11_0_get_dpm_freq_by_index(smu,
						      clk_type,
						      i,
						      &clk);
		if (ret) {
			dev_err(smu->adev->dev, "[%s] failed to get dpm freq by index!\n", __func__);
			return ret;
		}

		single_dpm_table->dpm_levels[i].value = clk;
		single_dpm_table->dpm_levels[i].enabled = true;

		if (i == 0)
			single_dpm_table->min = clk;
		else if (i == single_dpm_table->count - 1)
			single_dpm_table->max = clk;
	}

	return 0;
}

int smu_v11_0_get_dpm_level_range(struct smu_context *smu,
				  enum smu_clk_type clk_type,
				  uint32_t *min_value,
				  uint32_t *max_value)
{
	uint32_t level_count = 0;
	int ret = 0;

	if (!min_value && !max_value)
		return -EINVAL;

	if (min_value) {
		/* by default, level 0 clock value as min value */
		ret = smu_v11_0_get_dpm_freq_by_index(smu,
						      clk_type,
						      0,
						      min_value);
		if (ret)
			return ret;
	}

	if (max_value) {
		ret = smu_v11_0_get_dpm_level_count(smu,
						    clk_type,
						    &level_count);
		if (ret)
			return ret;

		ret = smu_v11_0_get_dpm_freq_by_index(smu,
						      clk_type,
						      level_count - 1,
						      max_value);
		if (ret)
			return ret;
	}

	return ret;
}

int smu_v11_0_get_current_pcie_link_width_level(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;

	return (RREG32_PCIE(smnPCIE_LC_LINK_WIDTH_CNTL) &
		PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD_MASK)
		>> PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD__SHIFT;
}

uint16_t smu_v11_0_get_current_pcie_link_width(struct smu_context *smu)
{
	uint32_t width_level;

	width_level = smu_v11_0_get_current_pcie_link_width_level(smu);
	if (width_level > LINK_WIDTH_MAX)
		width_level = 0;

	return link_width[width_level];
}

int smu_v11_0_get_current_pcie_link_speed_level(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;

	return (RREG32_PCIE(smnPCIE_LC_SPEED_CNTL) &
		PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE_MASK)
		>> PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE__SHIFT;
}

uint16_t smu_v11_0_get_current_pcie_link_speed(struct smu_context *smu)
{
	uint32_t speed_level;

	speed_level = smu_v11_0_get_current_pcie_link_speed_level(smu);
	if (speed_level > LINK_SPEED_MAX)
		speed_level = 0;

	return link_speed[speed_level];
}

int smu_v11_0_gfx_ulv_control(struct smu_context *smu,
			      bool enablement)
{
	int ret = 0;

	if (smu_cmn_feature_is_supported(smu, SMU_FEATURE_GFX_ULV_BIT))
		ret = smu_cmn_feature_set_enabled(smu, SMU_FEATURE_GFX_ULV_BIT, enablement);

	return ret;
}

int smu_v11_0_deep_sleep_control(struct smu_context *smu,
				 bool enablement)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0;

	if (smu_cmn_feature_is_supported(smu, SMU_FEATURE_DS_GFXCLK_BIT)) {
		ret = smu_cmn_feature_set_enabled(smu, SMU_FEATURE_DS_GFXCLK_BIT, enablement);
		if (ret) {
			dev_err(adev->dev, "Failed to %s GFXCLK DS!\n", enablement ? "enable" : "disable");
			return ret;
		}
	}

	if (smu_cmn_feature_is_supported(smu, SMU_FEATURE_DS_UCLK_BIT)) {
		ret = smu_cmn_feature_set_enabled(smu, SMU_FEATURE_DS_UCLK_BIT, enablement);
		if (ret) {
			dev_err(adev->dev, "Failed to %s UCLK DS!\n", enablement ? "enable" : "disable");
			return ret;
		}
	}

	if (smu_cmn_feature_is_supported(smu, SMU_FEATURE_DS_FCLK_BIT)) {
		ret = smu_cmn_feature_set_enabled(smu, SMU_FEATURE_DS_FCLK_BIT, enablement);
		if (ret) {
			dev_err(adev->dev, "Failed to %s FCLK DS!\n", enablement ? "enable" : "disable");
			return ret;
		}
	}

	if (smu_cmn_feature_is_supported(smu, SMU_FEATURE_DS_SOCCLK_BIT)) {
		ret = smu_cmn_feature_set_enabled(smu, SMU_FEATURE_DS_SOCCLK_BIT, enablement);
		if (ret) {
			dev_err(adev->dev, "Failed to %s SOCCLK DS!\n", enablement ? "enable" : "disable");
			return ret;
		}
	}

	if (smu_cmn_feature_is_supported(smu, SMU_FEATURE_DS_LCLK_BIT)) {
		ret = smu_cmn_feature_set_enabled(smu, SMU_FEATURE_DS_LCLK_BIT, enablement);
		if (ret) {
			dev_err(adev->dev, "Failed to %s LCLK DS!\n", enablement ? "enable" : "disable");
			return ret;
		}
	}

	return ret;
}

int smu_v11_0_restore_user_od_settings(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;
	void *user_od_table = table_context->user_overdrive_table;
	int ret = 0;

	ret = smu_cmn_update_table(smu, SMU_TABLE_OVERDRIVE, 0, (void *)user_od_table, true);
	if (ret)
		dev_err(smu->adev->dev, "Failed to import overdrive table!\n");

	return ret;
}
