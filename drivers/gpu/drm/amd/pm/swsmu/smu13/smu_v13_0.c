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

#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/reboot.h>

#define SMU_13_0_PARTIAL_PPTABLE
#define SWSMU_CODE_LAYER_L3

#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "atomfirmware.h"
#include "amdgpu_atomfirmware.h"
#include "amdgpu_atombios.h"
#include "smu_v13_0.h"
#include "soc15_common.h"
#include "atom.h"
#include "amdgpu_ras.h"
#include "smu_cmn.h"

#include "asic_reg/thm/thm_13_0_2_offset.h"
#include "asic_reg/thm/thm_13_0_2_sh_mask.h"
#include "asic_reg/mp/mp_13_0_2_offset.h"
#include "asic_reg/mp/mp_13_0_2_sh_mask.h"
#include "asic_reg/smuio/smuio_13_0_2_offset.h"
#include "asic_reg/smuio/smuio_13_0_2_sh_mask.h"

/*
 * DO NOT use these for err/warn/info/debug messages.
 * Use dev_err, dev_warn, dev_info and dev_dbg instead.
 * They are more MGPU friendly.
 */
#undef pr_err
#undef pr_warn
#undef pr_info
#undef pr_debug

MODULE_FIRMWARE("amdgpu/aldebaran_smc.bin");
MODULE_FIRMWARE("amdgpu/smu_13_0_0.bin");
MODULE_FIRMWARE("amdgpu/smu_13_0_7.bin");
MODULE_FIRMWARE("amdgpu/smu_13_0_10.bin");

#define mmMP1_SMN_C2PMSG_66                                                                            0x0282
#define mmMP1_SMN_C2PMSG_66_BASE_IDX                                                                   0

#define mmMP1_SMN_C2PMSG_82                                                                            0x0292
#define mmMP1_SMN_C2PMSG_82_BASE_IDX                                                                   0

#define mmMP1_SMN_C2PMSG_90                                                                            0x029a
#define mmMP1_SMN_C2PMSG_90_BASE_IDX                                                                   0

#define SMU13_VOLTAGE_SCALE 4

#define LINK_WIDTH_MAX				6
#define LINK_SPEED_MAX				3

#define smnPCIE_LC_LINK_WIDTH_CNTL		0x11140288
#define PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD_MASK 0x00000070L
#define PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD__SHIFT 0x4
#define smnPCIE_LC_SPEED_CNTL			0x11140290
#define PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE_MASK 0xC000
#define PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE__SHIFT 0xE

static const int link_width[] = {0, 1, 2, 4, 8, 12, 16};
static const int link_speed[] = {25, 50, 80, 160};

int smu_v13_0_init_microcode(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	const char *chip_name;
	char fw_name[30];
	char ucode_prefix[30];
	int err = 0;
	const struct smc_firmware_header_v1_0 *hdr;
	const struct common_firmware_header *header;
	struct amdgpu_firmware_info *ucode = NULL;

	/* doesn't need to load smu firmware in IOV mode */
	if (amdgpu_sriov_vf(adev))
		return 0;

	switch (adev->ip_versions[MP1_HWIP][0]) {
	case IP_VERSION(13, 0, 2):
		chip_name = "aldebaran_smc";
		break;
	default:
		amdgpu_ucode_ip_version_decode(adev, MP1_HWIP, ucode_prefix, sizeof(ucode_prefix));
		chip_name = ucode_prefix;
	}

	snprintf(fw_name, sizeof(fw_name), "amdgpu/%s.bin", chip_name);

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
		DRM_ERROR("smu_v13_0: Failed to load firmware \"%s\"\n",
			  fw_name);
		release_firmware(adev->pm.fw);
		adev->pm.fw = NULL;
	}
	return err;
}

void smu_v13_0_fini_microcode(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;

	release_firmware(adev->pm.fw);
	adev->pm.fw = NULL;
	adev->pm.fw_version = 0;
}

int smu_v13_0_load_microcode(struct smu_context *smu)
{
#if 0
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
#endif

	return 0;
}

int smu_v13_0_init_pptable_microcode(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	struct amdgpu_firmware_info *ucode = NULL;
	uint32_t size = 0, pptable_id = 0;
	int ret = 0;
	void *table;

	/* doesn't need to load smu firmware in IOV mode */
	if (amdgpu_sriov_vf(adev))
		return 0;

	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP)
		return 0;

	if (!adev->scpm_enabled)
		return 0;

	if ((adev->ip_versions[MP1_HWIP][0] == IP_VERSION(13, 0, 7)) ||
	    (adev->ip_versions[MP1_HWIP][0] == IP_VERSION(13, 0, 0)) ||
	    (adev->ip_versions[MP1_HWIP][0] == IP_VERSION(13, 0, 10)))
		return 0;

	/* override pptable_id from driver parameter */
	if (amdgpu_smu_pptable_id >= 0) {
		pptable_id = amdgpu_smu_pptable_id;
		dev_info(adev->dev, "override pptable id %d\n", pptable_id);
	} else {
		pptable_id = smu->smu_table.boot_values.pp_table_id;
	}

	/* "pptable_id == 0" means vbios carries the pptable. */
	if (!pptable_id)
		return 0;

	ret = smu_v13_0_get_pptable_from_firmware(smu, &table, &size, pptable_id);
	if (ret)
		return ret;

	smu->pptable_firmware.data = table;
	smu->pptable_firmware.size = size;

	ucode = &adev->firmware.ucode[AMDGPU_UCODE_ID_PPTABLE];
	ucode->ucode_id = AMDGPU_UCODE_ID_PPTABLE;
	ucode->fw = &smu->pptable_firmware;
	adev->firmware.fw_size +=
		ALIGN(smu->pptable_firmware.size, PAGE_SIZE);

	return 0;
}

int smu_v13_0_check_fw_status(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t mp1_fw_flags;

	switch (adev->ip_versions[MP1_HWIP][0]) {
	case IP_VERSION(13, 0, 4):
	case IP_VERSION(13, 0, 11):
		mp1_fw_flags = RREG32_PCIE(MP1_Public |
					   (smnMP1_V13_0_4_FIRMWARE_FLAGS & 0xffffffff));
		break;
	default:
		mp1_fw_flags = RREG32_PCIE(MP1_Public |
					   (smnMP1_FIRMWARE_FLAGS & 0xffffffff));
		break;
	}

	if ((mp1_fw_flags & MP1_FIRMWARE_FLAGS__INTERRUPTS_ENABLED_MASK) >>
	    MP1_FIRMWARE_FLAGS__INTERRUPTS_ENABLED__SHIFT)
		return 0;

	return -EIO;
}

int smu_v13_0_check_fw_version(struct smu_context *smu)
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

	switch (adev->ip_versions[MP1_HWIP][0]) {
	case IP_VERSION(13, 0, 2):
		smu->smc_driver_if_version = SMU13_DRIVER_IF_VERSION_ALDE;
		break;
	case IP_VERSION(13, 0, 0):
		smu->smc_driver_if_version = SMU13_DRIVER_IF_VERSION_SMU_V13_0_0_0;
		break;
	case IP_VERSION(13, 0, 10):
		smu->smc_driver_if_version = SMU13_DRIVER_IF_VERSION_SMU_V13_0_0_10;
		break;
	case IP_VERSION(13, 0, 7):
		smu->smc_driver_if_version = SMU13_DRIVER_IF_VERSION_SMU_V13_0_7;
		break;
	case IP_VERSION(13, 0, 1):
	case IP_VERSION(13, 0, 3):
	case IP_VERSION(13, 0, 8):
		smu->smc_driver_if_version = SMU13_DRIVER_IF_VERSION_YELLOW_CARP;
		break;
	case IP_VERSION(13, 0, 4):
	case IP_VERSION(13, 0, 11):
		smu->smc_driver_if_version = SMU13_DRIVER_IF_VERSION_SMU_V13_0_4;
		break;
	case IP_VERSION(13, 0, 5):
		smu->smc_driver_if_version = SMU13_DRIVER_IF_VERSION_SMU_V13_0_5;
		break;
	default:
		dev_err(adev->dev, "smu unsupported IP version: 0x%x.\n",
			adev->ip_versions[MP1_HWIP][0]);
		smu->smc_driver_if_version = SMU13_DRIVER_IF_VERSION_INV;
		break;
	}

	/* only for dGPU w/ SMU13*/
	if (adev->pm.fw)
		dev_dbg(smu->adev->dev, "smu fw reported program %d, version = 0x%08x (%d.%d.%d)\n",
			 smu_program, smu_version, smu_major, smu_minor, smu_debug);

	/*
	 * 1. if_version mismatch is not critical as our fw is designed
	 * to be backward compatible.
	 * 2. New fw usually brings some optimizations. But that's visible
	 * only on the paired driver.
	 * Considering above, we just leave user a warning message instead
	 * of halt driver loading.
	 */
	if (if_version != smu->smc_driver_if_version) {
		dev_info(adev->dev, "smu driver if version = 0x%08x, smu fw if version = 0x%08x, "
			 "smu fw program = %d, smu fw version = 0x%08x (%d.%d.%d)\n",
			 smu->smc_driver_if_version, if_version,
			 smu_program, smu_version, smu_major, smu_minor, smu_debug);
		dev_warn(adev->dev, "SMU driver if version not matched\n");
	}

	return ret;
}

static int smu_v13_0_set_pptable_v2_0(struct smu_context *smu, void **table, uint32_t *size)
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

static int smu_v13_0_set_pptable_v2_1(struct smu_context *smu, void **table,
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

static int smu_v13_0_get_pptable_from_vbios(struct smu_context *smu, void **table, uint32_t *size)
{
	struct amdgpu_device *adev = smu->adev;
	uint16_t atom_table_size;
	uint8_t frev, crev;
	int ret, index;

	dev_info(adev->dev, "use vbios provided pptable\n");
	index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
					    powerplayinfo);

	ret = amdgpu_atombios_get_data_table(adev, index, &atom_table_size, &frev, &crev,
					     (uint8_t **)table);
	if (ret)
		return ret;

	if (size)
		*size = atom_table_size;

	return 0;
}

int smu_v13_0_get_pptable_from_firmware(struct smu_context *smu,
					void **table,
					uint32_t *size,
					uint32_t pptable_id)
{
	const struct smc_firmware_header_v1_0 *hdr;
	struct amdgpu_device *adev = smu->adev;
	uint16_t version_major, version_minor;
	int ret;

	hdr = (const struct smc_firmware_header_v1_0 *) adev->pm.fw->data;
	if (!hdr)
		return -EINVAL;

	dev_info(adev->dev, "use driver provided pptable %d\n", pptable_id);

	version_major = le16_to_cpu(hdr->header.header_version_major);
	version_minor = le16_to_cpu(hdr->header.header_version_minor);
	if (version_major != 2) {
		dev_err(adev->dev, "Unsupported smu firmware version %d.%d\n",
			version_major, version_minor);
		return -EINVAL;
	}

	switch (version_minor) {
	case 0:
		ret = smu_v13_0_set_pptable_v2_0(smu, table, size);
		break;
	case 1:
		ret = smu_v13_0_set_pptable_v2_1(smu, table, size, pptable_id);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

int smu_v13_0_setup_pptable(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t size = 0, pptable_id = 0;
	void *table;
	int ret = 0;

	/* override pptable_id from driver parameter */
	if (amdgpu_smu_pptable_id >= 0) {
		pptable_id = amdgpu_smu_pptable_id;
		dev_info(adev->dev, "override pptable id %d\n", pptable_id);
	} else {
		pptable_id = smu->smu_table.boot_values.pp_table_id;
	}

	/* force using vbios pptable in sriov mode */
	if ((amdgpu_sriov_vf(adev) || !pptable_id) && (amdgpu_emu_mode != 1))
		ret = smu_v13_0_get_pptable_from_vbios(smu, &table, &size);
	else
		ret = smu_v13_0_get_pptable_from_firmware(smu, &table, &size, pptable_id);

	if (ret)
		return ret;

	if (!smu->smu_table.power_play_table)
		smu->smu_table.power_play_table = table;
	if (!smu->smu_table.power_play_table_size)
		smu->smu_table.power_play_table_size = size;

	return 0;
}

int smu_v13_0_init_smc_tables(struct smu_context *smu)
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
		kzalloc(sizeof(struct smu_13_0_max_sustainable_clocks), GFP_KERNEL);
	if (!smu_table->max_sustainable_clocks) {
		ret = -ENOMEM;
		goto err1_out;
	}

	/* Aldebaran does not support OVERDRIVE */
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
	}

	smu_table->combo_pptable =
		kzalloc(tables[SMU_TABLE_COMBO_PPTABLE].size, GFP_KERNEL);
	if (!smu_table->combo_pptable) {
		ret = -ENOMEM;
		goto err4_out;
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

int smu_v13_0_fini_smc_tables(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;

	kfree(smu_table->gpu_metrics_table);
	kfree(smu_table->combo_pptable);
	kfree(smu_table->boot_overdrive_table);
	kfree(smu_table->overdrive_table);
	kfree(smu_table->max_sustainable_clocks);
	kfree(smu_table->driver_pptable);
	smu_table->gpu_metrics_table = NULL;
	smu_table->combo_pptable = NULL;
	smu_table->boot_overdrive_table = NULL;
	smu_table->overdrive_table = NULL;
	smu_table->max_sustainable_clocks = NULL;
	smu_table->driver_pptable = NULL;
	kfree(smu_table->hardcode_pptable);
	smu_table->hardcode_pptable = NULL;

	kfree(smu_table->ecc_table);
	kfree(smu_table->metrics_table);
	kfree(smu_table->watermarks_table);
	smu_table->ecc_table = NULL;
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

int smu_v13_0_init_power(struct smu_context *smu)
{
	struct smu_power_context *smu_power = &smu->smu_power;

	if (smu_power->power_context || smu_power->power_context_size != 0)
		return -EINVAL;

	smu_power->power_context = kzalloc(sizeof(struct smu_13_0_power_context),
					   GFP_KERNEL);
	if (!smu_power->power_context)
		return -ENOMEM;
	smu_power->power_context_size = sizeof(struct smu_13_0_power_context);

	return 0;
}

int smu_v13_0_fini_power(struct smu_context *smu)
{
	struct smu_power_context *smu_power = &smu->smu_power;

	if (!smu_power->power_context || smu_power->power_context_size == 0)
		return -EINVAL;

	kfree(smu_power->power_context);
	smu_power->power_context = NULL;
	smu_power->power_context_size = 0;

	return 0;
}

int smu_v13_0_get_vbios_bootup_values(struct smu_context *smu)
{
	int ret, index;
	uint16_t size;
	uint8_t frev, crev;
	struct atom_common_table_header *header;
	struct atom_firmware_info_v3_4 *v_3_4;
	struct atom_firmware_info_v3_3 *v_3_3;
	struct atom_firmware_info_v3_1 *v_3_1;
	struct atom_smu_info_v3_6 *smu_info_v3_6;
	struct atom_smu_info_v4_0 *smu_info_v4_0;

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
		smu->smu_table.boot_values.pp_table_id = 0;
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
		smu->smu_table.boot_values.pp_table_id = v_3_3->pplib_pptable_id;
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
		smu->smu_table.boot_values.pp_table_id = v_3_4->pplib_pptable_id;
		break;
	}

	smu->smu_table.boot_values.format_revision = header->format_revision;
	smu->smu_table.boot_values.content_revision = header->content_revision;

	index = get_index_into_master_table(atom_master_list_of_data_tables_v2_1,
					    smu_info);
	if (!amdgpu_atombios_get_data_table(smu->adev, index, &size, &frev, &crev,
					    (uint8_t **)&header)) {

		if ((frev == 3) && (crev == 6)) {
			smu_info_v3_6 = (struct atom_smu_info_v3_6 *)header;

			smu->smu_table.boot_values.socclk = smu_info_v3_6->bootup_socclk_10khz;
			smu->smu_table.boot_values.vclk = smu_info_v3_6->bootup_vclk_10khz;
			smu->smu_table.boot_values.dclk = smu_info_v3_6->bootup_dclk_10khz;
			smu->smu_table.boot_values.fclk = smu_info_v3_6->bootup_fclk_10khz;
		} else if ((frev == 3) && (crev == 1)) {
			return 0;
		} else if ((frev == 4) && (crev == 0)) {
			smu_info_v4_0 = (struct atom_smu_info_v4_0 *)header;

			smu->smu_table.boot_values.socclk = smu_info_v4_0->bootup_socclk_10khz;
			smu->smu_table.boot_values.dcefclk = smu_info_v4_0->bootup_dcefclk_10khz;
			smu->smu_table.boot_values.vclk = smu_info_v4_0->bootup_vclk0_10khz;
			smu->smu_table.boot_values.dclk = smu_info_v4_0->bootup_dclk0_10khz;
			smu->smu_table.boot_values.fclk = smu_info_v4_0->bootup_fclk_10khz;
		} else {
			dev_warn(smu->adev->dev, "Unexpected and unhandled version: %d.%d\n",
						(uint32_t)frev, (uint32_t)crev);
		}
	}

	return 0;
}


int smu_v13_0_notify_memory_pool_location(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *memory_pool = &smu_table->memory_pool;
	int ret = 0;
	uint64_t address;
	uint32_t address_low, address_high;

	if (memory_pool->size == 0 || memory_pool->cpu_addr == NULL)
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

int smu_v13_0_set_min_deep_sleep_dcefclk(struct smu_context *smu, uint32_t clk)
{
	int ret;

	ret = smu_cmn_send_smc_msg_with_param(smu,
					      SMU_MSG_SetMinDeepSleepDcefclk, clk, NULL);
	if (ret)
		dev_err(smu->adev->dev, "SMU13 attempt to set divider for DCEFCLK Failed!");

	return ret;
}

int smu_v13_0_set_driver_table_location(struct smu_context *smu)
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

int smu_v13_0_set_tool_table_location(struct smu_context *smu)
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

int smu_v13_0_init_display_count(struct smu_context *smu, uint32_t count)
{
	int ret = 0;

	if (!smu->pm_enabled)
		return ret;

	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_NumOfDisplays, count, NULL);

	return ret;
}

int smu_v13_0_set_allowed_mask(struct smu_context *smu)
{
	struct smu_feature *feature = &smu->smu_feature;
	int ret = 0;
	uint32_t feature_mask[2];

	if (bitmap_empty(feature->allowed, SMU_FEATURE_MAX) ||
	    feature->feature_num < 64)
		return -EINVAL;

	bitmap_to_arr32(feature_mask, feature->allowed, 64);

	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetAllowedFeaturesMaskHigh,
					      feature_mask[1], NULL);
	if (ret)
		return ret;

	return smu_cmn_send_smc_msg_with_param(smu,
					       SMU_MSG_SetAllowedFeaturesMaskLow,
					       feature_mask[0],
					       NULL);
}

int smu_v13_0_gfx_off_control(struct smu_context *smu, bool enable)
{
	int ret = 0;
	struct amdgpu_device *adev = smu->adev;

	switch (adev->ip_versions[MP1_HWIP][0]) {
	case IP_VERSION(13, 0, 0):
	case IP_VERSION(13, 0, 1):
	case IP_VERSION(13, 0, 3):
	case IP_VERSION(13, 0, 4):
	case IP_VERSION(13, 0, 5):
	case IP_VERSION(13, 0, 7):
	case IP_VERSION(13, 0, 8):
	case IP_VERSION(13, 0, 10):
	case IP_VERSION(13, 0, 11):
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

int smu_v13_0_system_features_control(struct smu_context *smu,
				      bool en)
{
	return smu_cmn_send_smc_msg(smu, (en ? SMU_MSG_EnableAllSmuFeatures :
					  SMU_MSG_DisableAllSmuFeatures), NULL);
}

int smu_v13_0_notify_display_change(struct smu_context *smu)
{
	int ret = 0;

	if (!smu->pm_enabled)
		return ret;

	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT) &&
	    smu->adev->gmc.vram_type == AMDGPU_VRAM_TYPE_HBM)
		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetUclkFastSwitch, 1, NULL);

	return ret;
}

	static int
smu_v13_0_get_max_sustainable_clock(struct smu_context *smu, uint32_t *clock,
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

int smu_v13_0_init_max_sustainable_clocks(struct smu_context *smu)
{
	struct smu_13_0_max_sustainable_clocks *max_sustainable_clocks =
		smu->smu_table.max_sustainable_clocks;
	int ret = 0;

	max_sustainable_clocks->uclock = smu->smu_table.boot_values.uclk / 100;
	max_sustainable_clocks->soc_clock = smu->smu_table.boot_values.socclk / 100;
	max_sustainable_clocks->dcef_clock = smu->smu_table.boot_values.dcefclk / 100;
	max_sustainable_clocks->display_clock = 0xFFFFFFFF;
	max_sustainable_clocks->phy_clock = 0xFFFFFFFF;
	max_sustainable_clocks->pixel_clock = 0xFFFFFFFF;

	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT)) {
		ret = smu_v13_0_get_max_sustainable_clock(smu,
							  &(max_sustainable_clocks->uclock),
							  SMU_UCLK);
		if (ret) {
			dev_err(smu->adev->dev, "[%s] failed to get max UCLK from SMC!",
				__func__);
			return ret;
		}
	}

	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_SOCCLK_BIT)) {
		ret = smu_v13_0_get_max_sustainable_clock(smu,
							  &(max_sustainable_clocks->soc_clock),
							  SMU_SOCCLK);
		if (ret) {
			dev_err(smu->adev->dev, "[%s] failed to get max SOCCLK from SMC!",
				__func__);
			return ret;
		}
	}

	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_DCEFCLK_BIT)) {
		ret = smu_v13_0_get_max_sustainable_clock(smu,
							  &(max_sustainable_clocks->dcef_clock),
							  SMU_DCEFCLK);
		if (ret) {
			dev_err(smu->adev->dev, "[%s] failed to get max DCEFCLK from SMC!",
				__func__);
			return ret;
		}

		ret = smu_v13_0_get_max_sustainable_clock(smu,
							  &(max_sustainable_clocks->display_clock),
							  SMU_DISPCLK);
		if (ret) {
			dev_err(smu->adev->dev, "[%s] failed to get max DISPCLK from SMC!",
				__func__);
			return ret;
		}
		ret = smu_v13_0_get_max_sustainable_clock(smu,
							  &(max_sustainable_clocks->phy_clock),
							  SMU_PHYCLK);
		if (ret) {
			dev_err(smu->adev->dev, "[%s] failed to get max PHYCLK from SMC!",
				__func__);
			return ret;
		}
		ret = smu_v13_0_get_max_sustainable_clock(smu,
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

int smu_v13_0_get_current_power_limit(struct smu_context *smu,
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

	ret = smu_cmn_send_smc_msg_with_param(smu,
					      SMU_MSG_GetPptLimit,
					      power_src << 16,
					      power_limit);
	if (ret)
		dev_err(smu->adev->dev, "[%s] get PPT limit failed!", __func__);

	return ret;
}

int smu_v13_0_set_power_limit(struct smu_context *smu,
			      enum smu_ppt_limit_type limit_type,
			      uint32_t limit)
{
	int ret = 0;

	if (limit_type != SMU_DEFAULT_PPT_LIMIT)
		return -EINVAL;

	if (!smu_cmn_feature_is_enabled(smu, SMU_FEATURE_PPT_BIT)) {
		dev_err(smu->adev->dev, "Setting new power limit is not supported!\n");
		return -EOPNOTSUPP;
	}

	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetPptLimit, limit, NULL);
	if (ret) {
		dev_err(smu->adev->dev, "[%s] Set power limit Failed!\n", __func__);
		return ret;
	}

	smu->current_power_limit = limit;

	return 0;
}

static int smu_v13_0_allow_ih_interrupt(struct smu_context *smu)
{
	return smu_cmn_send_smc_msg(smu,
				    SMU_MSG_AllowIHHostInterrupt,
				    NULL);
}

static int smu_v13_0_process_pending_interrupt(struct smu_context *smu)
{
	int ret = 0;

	if (smu->dc_controlled_by_gpio &&
	    smu_cmn_feature_is_enabled(smu, SMU_FEATURE_ACDC_BIT))
		ret = smu_v13_0_allow_ih_interrupt(smu);

	return ret;
}

int smu_v13_0_enable_thermal_alert(struct smu_context *smu)
{
	int ret = 0;

	if (!smu->irq_source.num_types)
		return 0;

	ret = amdgpu_irq_get(smu->adev, &smu->irq_source, 0);
	if (ret)
		return ret;

	return smu_v13_0_process_pending_interrupt(smu);
}

int smu_v13_0_disable_thermal_alert(struct smu_context *smu)
{
	if (!smu->irq_source.num_types)
		return 0;

	return amdgpu_irq_put(smu->adev, &smu->irq_source, 0);
}

static uint16_t convert_to_vddc(uint8_t vid)
{
	return (uint16_t) ((6200 - (vid * 25)) / SMU13_VOLTAGE_SCALE);
}

int smu_v13_0_get_gfx_vdd(struct smu_context *smu, uint32_t *value)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t vdd = 0, val_vid = 0;

	if (!value)
		return -EINVAL;
	val_vid = (RREG32_SOC15(SMUIO, 0, regSMUSVI0_TEL_PLANE0) &
		   SMUSVI0_TEL_PLANE0__SVI0_PLANE0_VDDCOR_MASK) >>
		SMUSVI0_TEL_PLANE0__SVI0_PLANE0_VDDCOR__SHIFT;

	vdd = (uint32_t)convert_to_vddc((uint8_t)val_vid);

	*value = vdd;

	return 0;

}

int
smu_v13_0_display_clock_voltage_request(struct smu_context *smu,
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

		ret = smu_v13_0_set_hard_freq_limited_range(smu, clk_select, clk_freq, 0);

		if(clk_select == SMU_UCLK)
			smu->hard_min_uclk_req_from_dal = clk_freq;
	}

failed:
	return ret;
}

uint32_t smu_v13_0_get_fan_control_mode(struct smu_context *smu)
{
	if (!smu_cmn_feature_is_enabled(smu, SMU_FEATURE_FAN_CONTROL_BIT))
		return AMD_FAN_CTRL_MANUAL;
	else
		return AMD_FAN_CTRL_AUTO;
}

	static int
smu_v13_0_auto_fan_control(struct smu_context *smu, bool auto_fan_control)
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
smu_v13_0_set_fan_static_mode(struct smu_context *smu, uint32_t mode)
{
	struct amdgpu_device *adev = smu->adev;

	WREG32_SOC15(THM, 0, regCG_FDO_CTRL2,
		     REG_SET_FIELD(RREG32_SOC15(THM, 0, regCG_FDO_CTRL2),
				   CG_FDO_CTRL2, TMIN, 0));
	WREG32_SOC15(THM, 0, regCG_FDO_CTRL2,
		     REG_SET_FIELD(RREG32_SOC15(THM, 0, regCG_FDO_CTRL2),
				   CG_FDO_CTRL2, FDO_PWM_MODE, mode));

	return 0;
}

int smu_v13_0_set_fan_speed_pwm(struct smu_context *smu,
				uint32_t speed)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t duty100, duty;
	uint64_t tmp64;

	speed = MIN(speed, 255);

	if (smu_v13_0_auto_fan_control(smu, 0))
		return -EINVAL;

	duty100 = REG_GET_FIELD(RREG32_SOC15(THM, 0, regCG_FDO_CTRL1),
				CG_FDO_CTRL1, FMAX_DUTY100);
	if (!duty100)
		return -EINVAL;

	tmp64 = (uint64_t)speed * duty100;
	do_div(tmp64, 255);
	duty = (uint32_t)tmp64;

	WREG32_SOC15(THM, 0, regCG_FDO_CTRL0,
		     REG_SET_FIELD(RREG32_SOC15(THM, 0, regCG_FDO_CTRL0),
				   CG_FDO_CTRL0, FDO_STATIC_DUTY, duty));

	return smu_v13_0_set_fan_static_mode(smu, FDO_PWM_MODE_STATIC);
}

	int
smu_v13_0_set_fan_control_mode(struct smu_context *smu,
			       uint32_t mode)
{
	int ret = 0;

	switch (mode) {
	case AMD_FAN_CTRL_NONE:
		ret = smu_v13_0_set_fan_speed_pwm(smu, 255);
		break;
	case AMD_FAN_CTRL_MANUAL:
		ret = smu_v13_0_auto_fan_control(smu, 0);
		break;
	case AMD_FAN_CTRL_AUTO:
		ret = smu_v13_0_auto_fan_control(smu, 1);
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

int smu_v13_0_set_fan_speed_rpm(struct smu_context *smu,
				uint32_t speed)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t crystal_clock_freq = 2500;
	uint32_t tach_period;
	int ret;

	if (!speed)
		return -EINVAL;

	ret = smu_v13_0_auto_fan_control(smu, 0);
	if (ret)
		return ret;

	tach_period = 60 * crystal_clock_freq * 10000 / (8 * speed);
	WREG32_SOC15(THM, 0, regCG_TACH_CTRL,
		     REG_SET_FIELD(RREG32_SOC15(THM, 0, regCG_TACH_CTRL),
				   CG_TACH_CTRL, TARGET_PERIOD,
				   tach_period));

	return smu_v13_0_set_fan_static_mode(smu, FDO_PWM_MODE_STATIC_RPM);
}

int smu_v13_0_set_xgmi_pstate(struct smu_context *smu,
			      uint32_t pstate)
{
	int ret = 0;
	ret = smu_cmn_send_smc_msg_with_param(smu,
					      SMU_MSG_SetXgmiMode,
					      pstate ? XGMI_MODE_PSTATE_D0 : XGMI_MODE_PSTATE_D3,
					      NULL);
	return ret;
}

static int smu_v13_0_set_irq_state(struct amdgpu_device *adev,
				   struct amdgpu_irq_src *source,
				   unsigned tyep,
				   enum amdgpu_interrupt_state state)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	uint32_t low, high;
	uint32_t val = 0;

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		/* For THM irqs */
		val = RREG32_SOC15(THM, 0, regTHM_THERMAL_INT_CTRL);
		val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, THERM_INTH_MASK, 1);
		val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, THERM_INTL_MASK, 1);
		WREG32_SOC15(THM, 0, regTHM_THERMAL_INT_CTRL, val);

		WREG32_SOC15(THM, 0, regTHM_THERMAL_INT_ENA, 0);

		/* For MP1 SW irqs */
		val = RREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT_CTRL);
		val = REG_SET_FIELD(val, MP1_SMN_IH_SW_INT_CTRL, INT_MASK, 1);
		WREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT_CTRL, val);

		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		/* For THM irqs */
		low = max(SMU_THERMAL_MINIMUM_ALERT_TEMP,
			  smu->thermal_range.min / SMU_TEMPERATURE_UNITS_PER_CENTIGRADES);
		high = min(SMU_THERMAL_MAXIMUM_ALERT_TEMP,
			   smu->thermal_range.software_shutdown_temp);

		val = RREG32_SOC15(THM, 0, regTHM_THERMAL_INT_CTRL);
		val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, MAX_IH_CREDIT, 5);
		val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, THERM_IH_HW_ENA, 1);
		val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, THERM_INTH_MASK, 0);
		val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, THERM_INTL_MASK, 0);
		val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, DIG_THERM_INTH, (high & 0xff));
		val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, DIG_THERM_INTL, (low & 0xff));
		val = val & (~THM_THERMAL_INT_CTRL__THERM_TRIGGER_MASK_MASK);
		WREG32_SOC15(THM, 0, regTHM_THERMAL_INT_CTRL, val);

		val = (1 << THM_THERMAL_INT_ENA__THERM_INTH_CLR__SHIFT);
		val |= (1 << THM_THERMAL_INT_ENA__THERM_INTL_CLR__SHIFT);
		val |= (1 << THM_THERMAL_INT_ENA__THERM_TRIGGER_CLR__SHIFT);
		WREG32_SOC15(THM, 0, regTHM_THERMAL_INT_ENA, val);

		/* For MP1 SW irqs */
		val = RREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT);
		val = REG_SET_FIELD(val, MP1_SMN_IH_SW_INT, ID, 0xFE);
		val = REG_SET_FIELD(val, MP1_SMN_IH_SW_INT, VALID, 0);
		WREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT, val);

		val = RREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT_CTRL);
		val = REG_SET_FIELD(val, MP1_SMN_IH_SW_INT_CTRL, INT_MASK, 0);
		WREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT_CTRL, val);

		break;
	default:
		break;
	}

	return 0;
}

static int smu_v13_0_ack_ac_dc_interrupt(struct smu_context *smu)
{
	return smu_cmn_send_smc_msg(smu,
				    SMU_MSG_ReenableAcDcInterrupt,
				    NULL);
}

#define THM_11_0__SRCID__THM_DIG_THERM_L2H		0		/* ASIC_TEMP > CG_THERMAL_INT.DIG_THERM_INTH  */
#define THM_11_0__SRCID__THM_DIG_THERM_H2L		1		/* ASIC_TEMP < CG_THERMAL_INT.DIG_THERM_INTL  */
#define SMUIO_11_0__SRCID__SMUIO_GPIO19			83

static int smu_v13_0_irq_process(struct amdgpu_device *adev,
				 struct amdgpu_irq_src *source,
				 struct amdgpu_iv_entry *entry)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	uint32_t client_id = entry->client_id;
	uint32_t src_id = entry->src_id;
	/*
	 * ctxid is used to distinguish different
	 * events for SMCToHost interrupt.
	 */
	uint32_t ctxid = entry->src_data[0];
	uint32_t data;
	uint32_t high;

	if (client_id == SOC15_IH_CLIENTID_THM) {
		switch (src_id) {
		case THM_11_0__SRCID__THM_DIG_THERM_L2H:
			schedule_delayed_work(&smu->swctf_delayed_work,
					      msecs_to_jiffies(AMDGPU_SWCTF_EXTRA_DELAY));
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
			data = RREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT_CTRL);
			data = REG_SET_FIELD(data, MP1_SMN_IH_SW_INT_CTRL, INT_ACK, 1);
			WREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT_CTRL, data);

			switch (ctxid) {
			case 0x3:
				dev_dbg(adev->dev, "Switched to AC mode!\n");
				smu_v13_0_ack_ac_dc_interrupt(smu);
				adev->pm.ac_power = true;
				break;
			case 0x4:
				dev_dbg(adev->dev, "Switched to DC mode!\n");
				smu_v13_0_ack_ac_dc_interrupt(smu);
				adev->pm.ac_power = false;
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
			case 0x8:
				high = smu->thermal_range.software_shutdown_temp +
					smu->thermal_range.software_shutdown_temp_offset;
				high = min_t(typeof(high),
					     SMU_THERMAL_MAXIMUM_ALERT_TEMP,
					     high);
				dev_emerg(adev->dev, "Reduce soft CTF limit to %d (by an offset %d)\n",
							high,
							smu->thermal_range.software_shutdown_temp_offset);

				data = RREG32_SOC15(THM, 0, regTHM_THERMAL_INT_CTRL);
				data = REG_SET_FIELD(data, THM_THERMAL_INT_CTRL,
							DIG_THERM_INTH,
							(high & 0xff));
				data = data & (~THM_THERMAL_INT_CTRL__THERM_TRIGGER_MASK_MASK);
				WREG32_SOC15(THM, 0, regTHM_THERMAL_INT_CTRL, data);
				break;
			case 0x9:
				high = min_t(typeof(high),
					     SMU_THERMAL_MAXIMUM_ALERT_TEMP,
					     smu->thermal_range.software_shutdown_temp);
				dev_emerg(adev->dev, "Recover soft CTF limit to %d\n", high);

				data = RREG32_SOC15(THM, 0, regTHM_THERMAL_INT_CTRL);
				data = REG_SET_FIELD(data, THM_THERMAL_INT_CTRL,
							DIG_THERM_INTH,
							(high & 0xff));
				data = data & (~THM_THERMAL_INT_CTRL__THERM_TRIGGER_MASK_MASK);
				WREG32_SOC15(THM, 0, regTHM_THERMAL_INT_CTRL, data);
				break;
			}
		}
	}

	return 0;
}

static const struct amdgpu_irq_src_funcs smu_v13_0_irq_funcs =
{
	.set = smu_v13_0_set_irq_state,
	.process = smu_v13_0_irq_process,
};

int smu_v13_0_register_irq_handler(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	struct amdgpu_irq_src *irq_src = &smu->irq_source;
	int ret = 0;

	if (amdgpu_sriov_vf(adev))
		return 0;

	irq_src->num_types = 1;
	irq_src->funcs = &smu_v13_0_irq_funcs;

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

int smu_v13_0_get_max_sustainable_clocks_by_dc(struct smu_context *smu,
					       struct pp_smu_nv_clock_table *max_clocks)
{
	struct smu_table_context *table_context = &smu->smu_table;
	struct smu_13_0_max_sustainable_clocks *sustainable_clocks = NULL;

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

int smu_v13_0_set_azalia_d3_pme(struct smu_context *smu)
{
	int ret = 0;

	ret = smu_cmn_send_smc_msg(smu, SMU_MSG_BacoAudioD3PME, NULL);

	return ret;
}

static int smu_v13_0_wait_for_reset_complete(struct smu_context *smu,
					     uint64_t event_arg)
{
	int ret = 0;

	dev_dbg(smu->adev->dev, "waiting for smu reset complete\n");
	ret = smu_cmn_send_smc_msg(smu, SMU_MSG_GfxDriverResetRecovery, NULL);

	return ret;
}

int smu_v13_0_wait_for_event(struct smu_context *smu, enum smu_event_type event,
			     uint64_t event_arg)
{
	int ret = -EINVAL;

	switch (event) {
	case SMU_EVENT_RESET_COMPLETE:
		ret = smu_v13_0_wait_for_reset_complete(smu, event_arg);
		break;
	default:
		break;
	}

	return ret;
}

int smu_v13_0_get_dpm_ultimate_freq(struct smu_context *smu, enum smu_clk_type clk_type,
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
		if (smu->adev->pm.ac_power)
			ret = smu_cmn_send_smc_msg_with_param(smu,
							      SMU_MSG_GetMaxDpmFreq,
							      param,
							      max);
		else
			ret = smu_cmn_send_smc_msg_with_param(smu,
							      SMU_MSG_GetDcModeMaxDpmFreq,
							      param,
							      max);
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

int smu_v13_0_set_soft_freq_limited_range(struct smu_context *smu,
					  enum smu_clk_type clk_type,
					  uint32_t min,
					  uint32_t max)
{
	int ret = 0, clk_id = 0;
	uint32_t param;

	if (!smu_cmn_clk_dpm_is_enabled(smu, clk_type))
		return 0;

	clk_id = smu_cmn_to_asic_specific_index(smu,
						CMN2ASIC_MAPPING_CLK,
						clk_type);
	if (clk_id < 0)
		return clk_id;

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
	return ret;
}

int smu_v13_0_set_hard_freq_limited_range(struct smu_context *smu,
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

int smu_v13_0_set_performance_level(struct smu_context *smu,
				    enum amd_dpm_forced_level level)
{
	struct smu_13_0_dpm_context *dpm_context =
		smu->smu_dpm.dpm_context;
	struct smu_13_0_dpm_table *gfx_table =
		&dpm_context->dpm_tables.gfx_table;
	struct smu_13_0_dpm_table *mem_table =
		&dpm_context->dpm_tables.uclk_table;
	struct smu_13_0_dpm_table *soc_table =
		&dpm_context->dpm_tables.soc_table;
	struct smu_13_0_dpm_table *vclk_table =
		&dpm_context->dpm_tables.vclk_table;
	struct smu_13_0_dpm_table *dclk_table =
		&dpm_context->dpm_tables.dclk_table;
	struct smu_13_0_dpm_table *fclk_table =
		&dpm_context->dpm_tables.fclk_table;
	struct smu_umd_pstate_table *pstate_table =
		&smu->pstate_table;
	struct amdgpu_device *adev = smu->adev;
	uint32_t sclk_min = 0, sclk_max = 0;
	uint32_t mclk_min = 0, mclk_max = 0;
	uint32_t socclk_min = 0, socclk_max = 0;
	uint32_t vclk_min = 0, vclk_max = 0;
	uint32_t dclk_min = 0, dclk_max = 0;
	uint32_t fclk_min = 0, fclk_max = 0;
	int ret = 0, i;

	switch (level) {
	case AMD_DPM_FORCED_LEVEL_HIGH:
		sclk_min = sclk_max = gfx_table->max;
		mclk_min = mclk_max = mem_table->max;
		socclk_min = socclk_max = soc_table->max;
		vclk_min = vclk_max = vclk_table->max;
		dclk_min = dclk_max = dclk_table->max;
		fclk_min = fclk_max = fclk_table->max;
		break;
	case AMD_DPM_FORCED_LEVEL_LOW:
		sclk_min = sclk_max = gfx_table->min;
		mclk_min = mclk_max = mem_table->min;
		socclk_min = socclk_max = soc_table->min;
		vclk_min = vclk_max = vclk_table->min;
		dclk_min = dclk_max = dclk_table->min;
		fclk_min = fclk_max = fclk_table->min;
		break;
	case AMD_DPM_FORCED_LEVEL_AUTO:
		sclk_min = gfx_table->min;
		sclk_max = gfx_table->max;
		mclk_min = mem_table->min;
		mclk_max = mem_table->max;
		socclk_min = soc_table->min;
		socclk_max = soc_table->max;
		vclk_min = vclk_table->min;
		vclk_max = vclk_table->max;
		dclk_min = dclk_table->min;
		dclk_max = dclk_table->max;
		fclk_min = fclk_table->min;
		fclk_max = fclk_table->max;
		break;
	case AMD_DPM_FORCED_LEVEL_PROFILE_STANDARD:
		sclk_min = sclk_max = pstate_table->gfxclk_pstate.standard;
		mclk_min = mclk_max = pstate_table->uclk_pstate.standard;
		socclk_min = socclk_max = pstate_table->socclk_pstate.standard;
		vclk_min = vclk_max = pstate_table->vclk_pstate.standard;
		dclk_min = dclk_max = pstate_table->dclk_pstate.standard;
		fclk_min = fclk_max = pstate_table->fclk_pstate.standard;
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
		vclk_min = vclk_max = pstate_table->vclk_pstate.peak;
		dclk_min = dclk_max = pstate_table->dclk_pstate.peak;
		fclk_min = fclk_max = pstate_table->fclk_pstate.peak;
		break;
	case AMD_DPM_FORCED_LEVEL_MANUAL:
	case AMD_DPM_FORCED_LEVEL_PROFILE_EXIT:
		return 0;
	default:
		dev_err(adev->dev, "Invalid performance level %d\n", level);
		return -EINVAL;
	}

	/*
	 * Unset those settings for SMU 13.0.2. As soft limits settings
	 * for those clock domains are not supported.
	 */
	if (smu->adev->ip_versions[MP1_HWIP][0] == IP_VERSION(13, 0, 2)) {
		mclk_min = mclk_max = 0;
		socclk_min = socclk_max = 0;
		vclk_min = vclk_max = 0;
		dclk_min = dclk_max = 0;
		fclk_min = fclk_max = 0;
	}

	if (sclk_min && sclk_max) {
		ret = smu_v13_0_set_soft_freq_limited_range(smu,
							    SMU_GFXCLK,
							    sclk_min,
							    sclk_max);
		if (ret)
			return ret;

		pstate_table->gfxclk_pstate.curr.min = sclk_min;
		pstate_table->gfxclk_pstate.curr.max = sclk_max;
	}

	if (mclk_min && mclk_max) {
		ret = smu_v13_0_set_soft_freq_limited_range(smu,
							    SMU_MCLK,
							    mclk_min,
							    mclk_max);
		if (ret)
			return ret;

		pstate_table->uclk_pstate.curr.min = mclk_min;
		pstate_table->uclk_pstate.curr.max = mclk_max;
	}

	if (socclk_min && socclk_max) {
		ret = smu_v13_0_set_soft_freq_limited_range(smu,
							    SMU_SOCCLK,
							    socclk_min,
							    socclk_max);
		if (ret)
			return ret;

		pstate_table->socclk_pstate.curr.min = socclk_min;
		pstate_table->socclk_pstate.curr.max = socclk_max;
	}

	if (vclk_min && vclk_max) {
		for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
			if (adev->vcn.harvest_config & (1 << i))
				continue;
			ret = smu_v13_0_set_soft_freq_limited_range(smu,
								    i ? SMU_VCLK1 : SMU_VCLK,
								    vclk_min,
								    vclk_max);
			if (ret)
				return ret;
		}
		pstate_table->vclk_pstate.curr.min = vclk_min;
		pstate_table->vclk_pstate.curr.max = vclk_max;
	}

	if (dclk_min && dclk_max) {
		for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
			if (adev->vcn.harvest_config & (1 << i))
				continue;
			ret = smu_v13_0_set_soft_freq_limited_range(smu,
								    i ? SMU_DCLK1 : SMU_DCLK,
								    dclk_min,
								    dclk_max);
			if (ret)
				return ret;
		}
		pstate_table->dclk_pstate.curr.min = dclk_min;
		pstate_table->dclk_pstate.curr.max = dclk_max;
	}

	if (fclk_min && fclk_max) {
		ret = smu_v13_0_set_soft_freq_limited_range(smu,
							    SMU_FCLK,
							    fclk_min,
							    fclk_max);
		if (ret)
			return ret;

		pstate_table->fclk_pstate.curr.min = fclk_min;
		pstate_table->fclk_pstate.curr.max = fclk_max;
	}

	return ret;
}

int smu_v13_0_set_power_source(struct smu_context *smu,
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

static int smu_v13_0_get_dpm_freq_by_index(struct smu_context *smu,
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

	*value = *value & 0x7fffffff;

	return ret;
}

static int smu_v13_0_get_dpm_level_count(struct smu_context *smu,
					 enum smu_clk_type clk_type,
					 uint32_t *value)
{
	int ret;

	ret = smu_v13_0_get_dpm_freq_by_index(smu, clk_type, 0xff, value);
	/* SMU v13.0.2 FW returns 0 based max level, increment by one for it */
	if((smu->adev->ip_versions[MP1_HWIP][0] == IP_VERSION(13, 0, 2)) && (!ret && value))
		++(*value);

	return ret;
}

static int smu_v13_0_get_fine_grained_status(struct smu_context *smu,
					     enum smu_clk_type clk_type,
					     bool *is_fine_grained_dpm)
{
	int ret = 0, clk_id = 0;
	uint32_t param;
	uint32_t value;

	if (!is_fine_grained_dpm)
		return -EINVAL;

	if (!smu_cmn_clk_dpm_is_enabled(smu, clk_type))
		return 0;

	clk_id = smu_cmn_to_asic_specific_index(smu,
						CMN2ASIC_MAPPING_CLK,
						clk_type);
	if (clk_id < 0)
		return clk_id;

	param = (uint32_t)(((clk_id & 0xffff) << 16) | 0xff);

	ret = smu_cmn_send_smc_msg_with_param(smu,
					      SMU_MSG_GetDpmFreqByIndex,
					      param,
					      &value);
	if (ret)
		return ret;

	/*
	 * BIT31:  1 - Fine grained DPM, 0 - Dicrete DPM
	 * now, we un-support it
	 */
	*is_fine_grained_dpm = value & 0x80000000;

	return 0;
}

int smu_v13_0_set_single_dpm_table(struct smu_context *smu,
				   enum smu_clk_type clk_type,
				   struct smu_13_0_dpm_table *single_dpm_table)
{
	int ret = 0;
	uint32_t clk;
	int i;

	ret = smu_v13_0_get_dpm_level_count(smu,
					    clk_type,
					    &single_dpm_table->count);
	if (ret) {
		dev_err(smu->adev->dev, "[%s] failed to get dpm levels!\n", __func__);
		return ret;
	}

	if (smu->adev->ip_versions[MP1_HWIP][0] != IP_VERSION(13, 0, 2)) {
		ret = smu_v13_0_get_fine_grained_status(smu,
							clk_type,
							&single_dpm_table->is_fine_grained);
		if (ret) {
			dev_err(smu->adev->dev, "[%s] failed to get fine grained status!\n", __func__);
			return ret;
		}
	}

	for (i = 0; i < single_dpm_table->count; i++) {
		ret = smu_v13_0_get_dpm_freq_by_index(smu,
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

int smu_v13_0_get_dpm_level_range(struct smu_context *smu,
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
		ret = smu_v13_0_get_dpm_freq_by_index(smu,
						      clk_type,
						      0,
						      min_value);
		if (ret)
			return ret;
	}

	if (max_value) {
		ret = smu_v13_0_get_dpm_level_count(smu,
						    clk_type,
						    &level_count);
		if (ret)
			return ret;

		ret = smu_v13_0_get_dpm_freq_by_index(smu,
						      clk_type,
						      level_count - 1,
						      max_value);
		if (ret)
			return ret;
	}

	return ret;
}

int smu_v13_0_get_current_pcie_link_width_level(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;

	return (RREG32_PCIE(smnPCIE_LC_LINK_WIDTH_CNTL) &
		PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD_MASK)
		>> PCIE_LC_LINK_WIDTH_CNTL__LC_LINK_WIDTH_RD__SHIFT;
}

int smu_v13_0_get_current_pcie_link_width(struct smu_context *smu)
{
	uint32_t width_level;

	width_level = smu_v13_0_get_current_pcie_link_width_level(smu);
	if (width_level > LINK_WIDTH_MAX)
		width_level = 0;

	return link_width[width_level];
}

int smu_v13_0_get_current_pcie_link_speed_level(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;

	return (RREG32_PCIE(smnPCIE_LC_SPEED_CNTL) &
		PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE_MASK)
		>> PCIE_LC_SPEED_CNTL__LC_CURRENT_DATA_RATE__SHIFT;
}

int smu_v13_0_get_current_pcie_link_speed(struct smu_context *smu)
{
	uint32_t speed_level;

	speed_level = smu_v13_0_get_current_pcie_link_speed_level(smu);
	if (speed_level > LINK_SPEED_MAX)
		speed_level = 0;

	return link_speed[speed_level];
}

int smu_v13_0_set_vcn_enable(struct smu_context *smu,
			     bool enable)
{
	struct amdgpu_device *adev = smu->adev;
	int i, ret = 0;

	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		ret = smu_cmn_send_smc_msg_with_param(smu, enable ?
						      SMU_MSG_PowerUpVcn : SMU_MSG_PowerDownVcn,
						      i << 16U, NULL);
		if (ret)
			return ret;
	}

	return ret;
}

int smu_v13_0_set_jpeg_enable(struct smu_context *smu,
			      bool enable)
{
	return smu_cmn_send_smc_msg_with_param(smu, enable ?
					       SMU_MSG_PowerUpJpeg : SMU_MSG_PowerDownJpeg,
					       0, NULL);
}

int smu_v13_0_run_btc(struct smu_context *smu)
{
	int res;

	res = smu_cmn_send_smc_msg(smu, SMU_MSG_RunDcBtc, NULL);
	if (res)
		dev_err(smu->adev->dev, "RunDcBtc failed!\n");

	return res;
}

int smu_v13_0_gpo_control(struct smu_context *smu,
			  bool enablement)
{
	int res;

	res = smu_cmn_send_smc_msg_with_param(smu,
					      SMU_MSG_AllowGpo,
					      enablement ? 1 : 0,
					      NULL);
	if (res)
		dev_err(smu->adev->dev, "SetGpoAllow %d failed!\n", enablement);

	return res;
}

int smu_v13_0_deep_sleep_control(struct smu_context *smu,
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

	if (smu_cmn_feature_is_supported(smu, SMU_FEATURE_DS_VCN_BIT)) {
		ret = smu_cmn_feature_set_enabled(smu, SMU_FEATURE_DS_VCN_BIT, enablement);
		if (ret) {
			dev_err(adev->dev, "Failed to %s VCN DS!\n", enablement ? "enable" : "disable");
			return ret;
		}
	}

	if (smu_cmn_feature_is_supported(smu, SMU_FEATURE_DS_MP0CLK_BIT)) {
		ret = smu_cmn_feature_set_enabled(smu, SMU_FEATURE_DS_MP0CLK_BIT, enablement);
		if (ret) {
			dev_err(adev->dev, "Failed to %s MP0/MPIOCLK DS!\n", enablement ? "enable" : "disable");
			return ret;
		}
	}

	if (smu_cmn_feature_is_supported(smu, SMU_FEATURE_DS_MP1CLK_BIT)) {
		ret = smu_cmn_feature_set_enabled(smu, SMU_FEATURE_DS_MP1CLK_BIT, enablement);
		if (ret) {
			dev_err(adev->dev, "Failed to %s MP1CLK DS!\n", enablement ? "enable" : "disable");
			return ret;
		}
	}

	return ret;
}

int smu_v13_0_gfx_ulv_control(struct smu_context *smu,
			      bool enablement)
{
	int ret = 0;

	if (smu_cmn_feature_is_supported(smu, SMU_FEATURE_GFX_ULV_BIT))
		ret = smu_cmn_feature_set_enabled(smu, SMU_FEATURE_GFX_ULV_BIT, enablement);

	return ret;
}

int smu_v13_0_baco_set_armd3_sequence(struct smu_context *smu,
				      enum smu_baco_seq baco_seq)
{
	return smu_cmn_send_smc_msg_with_param(smu,
					       SMU_MSG_ArmD3,
					       baco_seq,
					       NULL);
}

bool smu_v13_0_baco_is_support(struct smu_context *smu)
{
	struct smu_baco_context *smu_baco = &smu->smu_baco;

	if (amdgpu_sriov_vf(smu->adev) ||
	    !smu_baco->platform_support)
		return false;

	/* return true if ASIC is in BACO state already */
	if (smu_v13_0_baco_get_state(smu) == SMU_BACO_STATE_ENTER)
		return true;

	if (smu_cmn_feature_is_supported(smu, SMU_FEATURE_BACO_BIT) &&
	    !smu_cmn_feature_is_enabled(smu, SMU_FEATURE_BACO_BIT))
		return false;

	return true;
}

enum smu_baco_state smu_v13_0_baco_get_state(struct smu_context *smu)
{
	struct smu_baco_context *smu_baco = &smu->smu_baco;

	return smu_baco->state;
}

int smu_v13_0_baco_set_state(struct smu_context *smu,
			     enum smu_baco_state state)
{
	struct smu_baco_context *smu_baco = &smu->smu_baco;
	struct amdgpu_device *adev = smu->adev;
	int ret = 0;

	if (smu_v13_0_baco_get_state(smu) == state)
		return 0;

	if (state == SMU_BACO_STATE_ENTER) {
		ret = smu_cmn_send_smc_msg_with_param(smu,
						      SMU_MSG_EnterBaco,
						      smu_baco->maco_support ?
						      BACO_SEQ_BAMACO : BACO_SEQ_BACO,
						      NULL);
	} else {
		ret = smu_cmn_send_smc_msg(smu,
					   SMU_MSG_ExitBaco,
					   NULL);
		if (ret)
			return ret;

		/* clear vbios scratch 6 and 7 for coming asic reinit */
		WREG32(adev->bios_scratch_reg_offset + 6, 0);
		WREG32(adev->bios_scratch_reg_offset + 7, 0);
	}

	if (!ret)
		smu_baco->state = state;

	return ret;
}

int smu_v13_0_baco_enter(struct smu_context *smu)
{
	int ret = 0;

	ret = smu_v13_0_baco_set_state(smu,
				       SMU_BACO_STATE_ENTER);
	if (ret)
		return ret;

	msleep(10);

	return ret;
}

int smu_v13_0_baco_exit(struct smu_context *smu)
{
	return smu_v13_0_baco_set_state(smu,
					SMU_BACO_STATE_EXIT);
}

int smu_v13_0_set_gfx_power_up_by_imu(struct smu_context *smu)
{
	uint16_t index;

	index = smu_cmn_to_asic_specific_index(smu, CMN2ASIC_MAPPING_MSG,
					       SMU_MSG_EnableGfxImu);
	/* Param 1 to tell PMFW to enable GFXOFF feature */
	return smu_cmn_send_msg_without_waiting(smu, index, 1);
}

int smu_v13_0_od_edit_dpm_table(struct smu_context *smu,
				enum PP_OD_DPM_TABLE_COMMAND type,
				long input[], uint32_t size)
{
	struct smu_dpm_context *smu_dpm = &(smu->smu_dpm);
	int ret = 0;

	/* Only allowed in manual mode */
	if (smu_dpm->dpm_level != AMD_DPM_FORCED_LEVEL_MANUAL)
		return -EINVAL;

	switch (type) {
	case PP_OD_EDIT_SCLK_VDDC_TABLE:
		if (size != 2) {
			dev_err(smu->adev->dev, "Input parameter number not correct\n");
			return -EINVAL;
		}

		if (input[0] == 0) {
			if (input[1] < smu->gfx_default_hard_min_freq) {
				dev_warn(smu->adev->dev,
					 "Fine grain setting minimum sclk (%ld) MHz is less than the minimum allowed (%d) MHz\n",
					 input[1], smu->gfx_default_hard_min_freq);
				return -EINVAL;
			}
			smu->gfx_actual_hard_min_freq = input[1];
		} else if (input[0] == 1) {
			if (input[1] > smu->gfx_default_soft_max_freq) {
				dev_warn(smu->adev->dev,
					 "Fine grain setting maximum sclk (%ld) MHz is greater than the maximum allowed (%d) MHz\n",
					 input[1], smu->gfx_default_soft_max_freq);
				return -EINVAL;
			}
			smu->gfx_actual_soft_max_freq = input[1];
		} else {
			return -EINVAL;
		}
		break;
	case PP_OD_RESTORE_DEFAULT_TABLE:
		if (size != 0) {
			dev_err(smu->adev->dev, "Input parameter number not correct\n");
			return -EINVAL;
		}
		smu->gfx_actual_hard_min_freq = smu->gfx_default_hard_min_freq;
		smu->gfx_actual_soft_max_freq = smu->gfx_default_soft_max_freq;
		break;
	case PP_OD_COMMIT_DPM_TABLE:
		if (size != 0) {
			dev_err(smu->adev->dev, "Input parameter number not correct\n");
			return -EINVAL;
		}
		if (smu->gfx_actual_hard_min_freq > smu->gfx_actual_soft_max_freq) {
			dev_err(smu->adev->dev,
				"The setting minimum sclk (%d) MHz is greater than the setting maximum sclk (%d) MHz\n",
				smu->gfx_actual_hard_min_freq,
				smu->gfx_actual_soft_max_freq);
			return -EINVAL;
		}

		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetHardMinGfxClk,
						      smu->gfx_actual_hard_min_freq,
						      NULL);
		if (ret) {
			dev_err(smu->adev->dev, "Set hard min sclk failed!");
			return ret;
		}

		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMaxGfxClk,
						      smu->gfx_actual_soft_max_freq,
						      NULL);
		if (ret) {
			dev_err(smu->adev->dev, "Set soft max sclk failed!");
			return ret;
		}
		break;
	default:
		return -ENOSYS;
	}

	return ret;
}

int smu_v13_0_set_default_dpm_tables(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;

	return smu_cmn_update_table(smu, SMU_TABLE_DPMCLOCKS, 0,
				    smu_table->clocks_table, false);
}

void smu_v13_0_set_smu_mailbox_registers(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;

	smu->param_reg = SOC15_REG_OFFSET(MP1, 0, mmMP1_SMN_C2PMSG_82);
	smu->msg_reg = SOC15_REG_OFFSET(MP1, 0, mmMP1_SMN_C2PMSG_66);
	smu->resp_reg = SOC15_REG_OFFSET(MP1, 0, mmMP1_SMN_C2PMSG_90);
}

int smu_v13_0_mode1_reset(struct smu_context *smu)
{
	int ret = 0;

	ret = smu_cmn_send_smc_msg(smu, SMU_MSG_Mode1Reset, NULL);
	if (!ret)
		msleep(SMU13_MODE1_RESET_WAIT_TIME_IN_MS);

	return ret;
}

int smu_v13_0_update_pcie_parameters(struct smu_context *smu,
				     uint8_t pcie_gen_cap,
				     uint8_t pcie_width_cap)
{
	struct smu_13_0_dpm_context *dpm_context = smu->smu_dpm.dpm_context;
	struct smu_13_0_pcie_table *pcie_table =
				&dpm_context->dpm_tables.pcie_table;
	int num_of_levels = pcie_table->num_of_link_levels;
	uint32_t smu_pcie_arg;
	int ret, i;

	if (!amdgpu_device_pcie_dynamic_switching_supported()) {
		if (pcie_table->pcie_gen[num_of_levels - 1] < pcie_gen_cap)
			pcie_gen_cap = pcie_table->pcie_gen[num_of_levels - 1];

		if (pcie_table->pcie_lane[num_of_levels - 1] < pcie_width_cap)
			pcie_width_cap = pcie_table->pcie_lane[num_of_levels - 1];

		/* Force all levels to use the same settings */
		for (i = 0; i < num_of_levels; i++) {
			pcie_table->pcie_gen[i] = pcie_gen_cap;
			pcie_table->pcie_lane[i] = pcie_width_cap;
		}
	} else {
		for (i = 0; i < num_of_levels; i++) {
			if (pcie_table->pcie_gen[i] > pcie_gen_cap)
				pcie_table->pcie_gen[i] = pcie_gen_cap;
			if (pcie_table->pcie_lane[i] > pcie_width_cap)
				pcie_table->pcie_lane[i] = pcie_width_cap;
		}
	}

	for (i = 0; i < num_of_levels; i++) {
		smu_pcie_arg = i << 16;
		smu_pcie_arg |= pcie_table->pcie_gen[i] << 8;
		smu_pcie_arg |= pcie_table->pcie_lane[i];

		ret = smu_cmn_send_smc_msg_with_param(smu,
						      SMU_MSG_OverridePcieParameters,
						      smu_pcie_arg,
						      NULL);
		if (ret)
			return ret;
	}

	return 0;
}
