/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 *
 *
 */
#include <linux/list.h>
#include <linux/slab.h>
#include <drm/drmP.h>
#include <linux/firmware.h>
#include <drm/amdgpu_drm.h>
#include "amdgpu.h"
#include "atom.h"
#include "amdgpu_ucode.h"

struct amdgpu_cgs_device {
	struct cgs_device base;
	struct amdgpu_device *adev;
};

#define CGS_FUNC_ADEV							\
	struct amdgpu_device *adev =					\
		((struct amdgpu_cgs_device *)cgs_device)->adev


static uint32_t amdgpu_cgs_read_register(struct cgs_device *cgs_device, unsigned offset)
{
	CGS_FUNC_ADEV;
	return RREG32(offset);
}

static void amdgpu_cgs_write_register(struct cgs_device *cgs_device, unsigned offset,
				      uint32_t value)
{
	CGS_FUNC_ADEV;
	WREG32(offset, value);
}

static uint32_t amdgpu_cgs_read_ind_register(struct cgs_device *cgs_device,
					     enum cgs_ind_reg space,
					     unsigned index)
{
	CGS_FUNC_ADEV;
	switch (space) {
	case CGS_IND_REG__MMIO:
		return RREG32_IDX(index);
	case CGS_IND_REG__PCIE:
		return RREG32_PCIE(index);
	case CGS_IND_REG__SMC:
		return RREG32_SMC(index);
	case CGS_IND_REG__UVD_CTX:
		return RREG32_UVD_CTX(index);
	case CGS_IND_REG__DIDT:
		return RREG32_DIDT(index);
	case CGS_IND_REG_GC_CAC:
		return RREG32_GC_CAC(index);
	case CGS_IND_REG_SE_CAC:
		return RREG32_SE_CAC(index);
	case CGS_IND_REG__AUDIO_ENDPT:
		DRM_ERROR("audio endpt register access not implemented.\n");
		return 0;
	}
	WARN(1, "Invalid indirect register space");
	return 0;
}

static void amdgpu_cgs_write_ind_register(struct cgs_device *cgs_device,
					  enum cgs_ind_reg space,
					  unsigned index, uint32_t value)
{
	CGS_FUNC_ADEV;
	switch (space) {
	case CGS_IND_REG__MMIO:
		return WREG32_IDX(index, value);
	case CGS_IND_REG__PCIE:
		return WREG32_PCIE(index, value);
	case CGS_IND_REG__SMC:
		return WREG32_SMC(index, value);
	case CGS_IND_REG__UVD_CTX:
		return WREG32_UVD_CTX(index, value);
	case CGS_IND_REG__DIDT:
		return WREG32_DIDT(index, value);
	case CGS_IND_REG_GC_CAC:
		return WREG32_GC_CAC(index, value);
	case CGS_IND_REG_SE_CAC:
		return WREG32_SE_CAC(index, value);
	case CGS_IND_REG__AUDIO_ENDPT:
		DRM_ERROR("audio endpt register access not implemented.\n");
		return;
	}
	WARN(1, "Invalid indirect register space");
}

static uint32_t fw_type_convert(struct cgs_device *cgs_device, uint32_t fw_type)
{
	CGS_FUNC_ADEV;
	enum AMDGPU_UCODE_ID result = AMDGPU_UCODE_ID_MAXIMUM;

	switch (fw_type) {
	case CGS_UCODE_ID_SDMA0:
		result = AMDGPU_UCODE_ID_SDMA0;
		break;
	case CGS_UCODE_ID_SDMA1:
		result = AMDGPU_UCODE_ID_SDMA1;
		break;
	case CGS_UCODE_ID_CP_CE:
		result = AMDGPU_UCODE_ID_CP_CE;
		break;
	case CGS_UCODE_ID_CP_PFP:
		result = AMDGPU_UCODE_ID_CP_PFP;
		break;
	case CGS_UCODE_ID_CP_ME:
		result = AMDGPU_UCODE_ID_CP_ME;
		break;
	case CGS_UCODE_ID_CP_MEC:
	case CGS_UCODE_ID_CP_MEC_JT1:
		result = AMDGPU_UCODE_ID_CP_MEC1;
		break;
	case CGS_UCODE_ID_CP_MEC_JT2:
		/* for VI. JT2 should be the same as JT1, because:
			1, MEC2 and MEC1 use exactly same FW.
			2, JT2 is not pached but JT1 is.
		*/
		if (adev->asic_type >= CHIP_TOPAZ)
			result = AMDGPU_UCODE_ID_CP_MEC1;
		else
			result = AMDGPU_UCODE_ID_CP_MEC2;
		break;
	case CGS_UCODE_ID_RLC_G:
		result = AMDGPU_UCODE_ID_RLC_G;
		break;
	case CGS_UCODE_ID_STORAGE:
		result = AMDGPU_UCODE_ID_STORAGE;
		break;
	default:
		DRM_ERROR("Firmware type not supported\n");
	}
	return result;
}

static uint16_t amdgpu_get_firmware_version(struct cgs_device *cgs_device,
					enum cgs_ucode_id type)
{
	CGS_FUNC_ADEV;
	uint16_t fw_version = 0;

	switch (type) {
		case CGS_UCODE_ID_SDMA0:
			fw_version = adev->sdma.instance[0].fw_version;
			break;
		case CGS_UCODE_ID_SDMA1:
			fw_version = adev->sdma.instance[1].fw_version;
			break;
		case CGS_UCODE_ID_CP_CE:
			fw_version = adev->gfx.ce_fw_version;
			break;
		case CGS_UCODE_ID_CP_PFP:
			fw_version = adev->gfx.pfp_fw_version;
			break;
		case CGS_UCODE_ID_CP_ME:
			fw_version = adev->gfx.me_fw_version;
			break;
		case CGS_UCODE_ID_CP_MEC:
			fw_version = adev->gfx.mec_fw_version;
			break;
		case CGS_UCODE_ID_CP_MEC_JT1:
			fw_version = adev->gfx.mec_fw_version;
			break;
		case CGS_UCODE_ID_CP_MEC_JT2:
			fw_version = adev->gfx.mec_fw_version;
			break;
		case CGS_UCODE_ID_RLC_G:
			fw_version = adev->gfx.rlc_fw_version;
			break;
		case CGS_UCODE_ID_STORAGE:
			break;
		default:
			DRM_ERROR("firmware type %d do not have version\n", type);
			break;
	}
	return fw_version;
}

static int amdgpu_cgs_get_firmware_info(struct cgs_device *cgs_device,
					enum cgs_ucode_id type,
					struct cgs_firmware_info *info)
{
	CGS_FUNC_ADEV;

	if ((CGS_UCODE_ID_SMU != type) && (CGS_UCODE_ID_SMU_SK != type)) {
		uint64_t gpu_addr;
		uint32_t data_size;
		const struct gfx_firmware_header_v1_0 *header;
		enum AMDGPU_UCODE_ID id;
		struct amdgpu_firmware_info *ucode;

		id = fw_type_convert(cgs_device, type);
		ucode = &adev->firmware.ucode[id];
		if (ucode->fw == NULL)
			return -EINVAL;

		gpu_addr  = ucode->mc_addr;
		header = (const struct gfx_firmware_header_v1_0 *)ucode->fw->data;
		data_size = le32_to_cpu(header->header.ucode_size_bytes);

		if ((type == CGS_UCODE_ID_CP_MEC_JT1) ||
		    (type == CGS_UCODE_ID_CP_MEC_JT2)) {
			gpu_addr += ALIGN(le32_to_cpu(header->header.ucode_size_bytes), PAGE_SIZE);
			data_size = le32_to_cpu(header->jt_size) << 2;
		}

		info->kptr = ucode->kaddr;
		info->image_size = data_size;
		info->mc_addr = gpu_addr;
		info->version = (uint16_t)le32_to_cpu(header->header.ucode_version);

		if (CGS_UCODE_ID_CP_MEC == type)
			info->image_size = le32_to_cpu(header->jt_offset) << 2;

		info->fw_version = amdgpu_get_firmware_version(cgs_device, type);
		info->feature_version = (uint16_t)le32_to_cpu(header->ucode_feature_version);
	} else {
		char fw_name[30] = {0};
		int err = 0;
		uint32_t ucode_size;
		uint32_t ucode_start_address;
		const uint8_t *src;
		const struct smc_firmware_header_v1_0 *hdr;
		const struct common_firmware_header *header;
		struct amdgpu_firmware_info *ucode = NULL;

		if (!adev->pm.fw) {
			switch (adev->asic_type) {
			case CHIP_TAHITI:
				strcpy(fw_name, "radeon/tahiti_smc.bin");
				break;
			case CHIP_PITCAIRN:
				if ((adev->pdev->revision == 0x81) &&
				    ((adev->pdev->device == 0x6810) ||
				    (adev->pdev->device == 0x6811))) {
					info->is_kicker = true;
					strcpy(fw_name, "radeon/pitcairn_k_smc.bin");
				} else {
					strcpy(fw_name, "radeon/pitcairn_smc.bin");
				}
				break;
			case CHIP_VERDE:
				if (((adev->pdev->device == 0x6820) &&
					((adev->pdev->revision == 0x81) ||
					(adev->pdev->revision == 0x83))) ||
				    ((adev->pdev->device == 0x6821) &&
					((adev->pdev->revision == 0x83) ||
					(adev->pdev->revision == 0x87))) ||
				    ((adev->pdev->revision == 0x87) &&
					((adev->pdev->device == 0x6823) ||
					(adev->pdev->device == 0x682b)))) {
					info->is_kicker = true;
					strcpy(fw_name, "radeon/verde_k_smc.bin");
				} else {
					strcpy(fw_name, "radeon/verde_smc.bin");
				}
				break;
			case CHIP_OLAND:
				if (((adev->pdev->revision == 0x81) &&
					((adev->pdev->device == 0x6600) ||
					(adev->pdev->device == 0x6604) ||
					(adev->pdev->device == 0x6605) ||
					(adev->pdev->device == 0x6610))) ||
				    ((adev->pdev->revision == 0x83) &&
					(adev->pdev->device == 0x6610))) {
					info->is_kicker = true;
					strcpy(fw_name, "radeon/oland_k_smc.bin");
				} else {
					strcpy(fw_name, "radeon/oland_smc.bin");
				}
				break;
			case CHIP_HAINAN:
				if (((adev->pdev->revision == 0x81) &&
					(adev->pdev->device == 0x6660)) ||
				    ((adev->pdev->revision == 0x83) &&
					((adev->pdev->device == 0x6660) ||
					(adev->pdev->device == 0x6663) ||
					(adev->pdev->device == 0x6665) ||
					 (adev->pdev->device == 0x6667)))) {
					info->is_kicker = true;
					strcpy(fw_name, "radeon/hainan_k_smc.bin");
				} else if ((adev->pdev->revision == 0xc3) &&
					 (adev->pdev->device == 0x6665)) {
					info->is_kicker = true;
					strcpy(fw_name, "radeon/banks_k_2_smc.bin");
				} else {
					strcpy(fw_name, "radeon/hainan_smc.bin");
				}
				break;
			case CHIP_BONAIRE:
				if ((adev->pdev->revision == 0x80) ||
					(adev->pdev->revision == 0x81) ||
					(adev->pdev->device == 0x665f)) {
					info->is_kicker = true;
					strcpy(fw_name, "amdgpu/bonaire_k_smc.bin");
				} else {
					strcpy(fw_name, "amdgpu/bonaire_smc.bin");
				}
				break;
			case CHIP_HAWAII:
				if (adev->pdev->revision == 0x80) {
					info->is_kicker = true;
					strcpy(fw_name, "amdgpu/hawaii_k_smc.bin");
				} else {
					strcpy(fw_name, "amdgpu/hawaii_smc.bin");
				}
				break;
			case CHIP_TOPAZ:
				if (((adev->pdev->device == 0x6900) && (adev->pdev->revision == 0x81)) ||
				    ((adev->pdev->device == 0x6900) && (adev->pdev->revision == 0x83)) ||
				    ((adev->pdev->device == 0x6907) && (adev->pdev->revision == 0x87)) ||
				    ((adev->pdev->device == 0x6900) && (adev->pdev->revision == 0xD1)) ||
				    ((adev->pdev->device == 0x6900) && (adev->pdev->revision == 0xD3))) {
					info->is_kicker = true;
					strcpy(fw_name, "amdgpu/topaz_k_smc.bin");
				} else
					strcpy(fw_name, "amdgpu/topaz_smc.bin");
				break;
			case CHIP_TONGA:
				if (((adev->pdev->device == 0x6939) && (adev->pdev->revision == 0xf1)) ||
				    ((adev->pdev->device == 0x6938) && (adev->pdev->revision == 0xf1))) {
					info->is_kicker = true;
					strcpy(fw_name, "amdgpu/tonga_k_smc.bin");
				} else
					strcpy(fw_name, "amdgpu/tonga_smc.bin");
				break;
			case CHIP_FIJI:
				strcpy(fw_name, "amdgpu/fiji_smc.bin");
				break;
			case CHIP_POLARIS11:
				if (type == CGS_UCODE_ID_SMU) {
					if (((adev->pdev->device == 0x67ef) &&
					     ((adev->pdev->revision == 0xe0) ||
					      (adev->pdev->revision == 0xe5))) ||
					    ((adev->pdev->device == 0x67ff) &&
					     ((adev->pdev->revision == 0xcf) ||
					      (adev->pdev->revision == 0xef) ||
					      (adev->pdev->revision == 0xff)))) {
						info->is_kicker = true;
						strcpy(fw_name, "amdgpu/polaris11_k_smc.bin");
					} else if ((adev->pdev->device == 0x67ef) &&
						   (adev->pdev->revision == 0xe2)) {
						info->is_kicker = true;
						strcpy(fw_name, "amdgpu/polaris11_k2_smc.bin");
					} else {
						strcpy(fw_name, "amdgpu/polaris11_smc.bin");
					}
				} else if (type == CGS_UCODE_ID_SMU_SK) {
					strcpy(fw_name, "amdgpu/polaris11_smc_sk.bin");
				}
				break;
			case CHIP_POLARIS10:
				if (type == CGS_UCODE_ID_SMU) {
					if (((adev->pdev->device == 0x67df) &&
					     ((adev->pdev->revision == 0xe0) ||
					      (adev->pdev->revision == 0xe3) ||
					      (adev->pdev->revision == 0xe4) ||
					      (adev->pdev->revision == 0xe5) ||
					      (adev->pdev->revision == 0xe7) ||
					      (adev->pdev->revision == 0xef))) ||
					    ((adev->pdev->device == 0x6fdf) &&
					     (adev->pdev->revision == 0xef))) {
						info->is_kicker = true;
						strcpy(fw_name, "amdgpu/polaris10_k_smc.bin");
					} else if ((adev->pdev->device == 0x67df) &&
						   ((adev->pdev->revision == 0xe1) ||
						    (adev->pdev->revision == 0xf7))) {
						info->is_kicker = true;
						strcpy(fw_name, "amdgpu/polaris10_k2_smc.bin");
					} else {
						strcpy(fw_name, "amdgpu/polaris10_smc.bin");
					}
				} else if (type == CGS_UCODE_ID_SMU_SK) {
					strcpy(fw_name, "amdgpu/polaris10_smc_sk.bin");
				}
				break;
			case CHIP_POLARIS12:
				if (((adev->pdev->device == 0x6987) &&
				     ((adev->pdev->revision == 0xc0) ||
				      (adev->pdev->revision == 0xc3))) ||
				    ((adev->pdev->device == 0x6981) &&
				     ((adev->pdev->revision == 0x00) ||
				      (adev->pdev->revision == 0x01) ||
				      (adev->pdev->revision == 0x10)))) {
					info->is_kicker = true;
					strcpy(fw_name, "amdgpu/polaris12_k_smc.bin");
				} else {
					strcpy(fw_name, "amdgpu/polaris12_smc.bin");
				}
				break;
			case CHIP_VEGAM:
				strcpy(fw_name, "amdgpu/vegam_smc.bin");
				break;
			case CHIP_VEGA10:
				if ((adev->pdev->device == 0x687f) &&
					((adev->pdev->revision == 0xc0) ||
					(adev->pdev->revision == 0xc1) ||
					(adev->pdev->revision == 0xc3)))
					strcpy(fw_name, "amdgpu/vega10_acg_smc.bin");
				else
					strcpy(fw_name, "amdgpu/vega10_smc.bin");
				break;
			case CHIP_VEGA12:
				strcpy(fw_name, "amdgpu/vega12_smc.bin");
				break;
			case CHIP_VEGA20:
				strcpy(fw_name, "amdgpu/vega20_smc.bin");
				break;
			default:
				DRM_ERROR("SMC firmware not supported\n");
				return -EINVAL;
			}

			err = request_firmware(&adev->pm.fw, fw_name, adev->dev);
			if (err) {
				DRM_ERROR("Failed to request firmware\n");
				return err;
			}

			err = amdgpu_ucode_validate(adev->pm.fw);
			if (err) {
				DRM_ERROR("Failed to load firmware \"%s\"", fw_name);
				release_firmware(adev->pm.fw);
				adev->pm.fw = NULL;
				return err;
			}

			if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
				ucode = &adev->firmware.ucode[AMDGPU_UCODE_ID_SMC];
				ucode->ucode_id = AMDGPU_UCODE_ID_SMC;
				ucode->fw = adev->pm.fw;
				header = (const struct common_firmware_header *)ucode->fw->data;
				adev->firmware.fw_size +=
					ALIGN(le32_to_cpu(header->ucode_size_bytes), PAGE_SIZE);
			}
		}

		hdr = (const struct smc_firmware_header_v1_0 *)	adev->pm.fw->data;
		amdgpu_ucode_print_smc_hdr(&hdr->header);
		adev->pm.fw_version = le32_to_cpu(hdr->header.ucode_version);
		ucode_size = le32_to_cpu(hdr->header.ucode_size_bytes);
		ucode_start_address = le32_to_cpu(hdr->ucode_start_addr);
		src = (const uint8_t *)(adev->pm.fw->data +
		       le32_to_cpu(hdr->header.ucode_array_offset_bytes));

		info->version = adev->pm.fw_version;
		info->image_size = ucode_size;
		info->ucode_start_address = ucode_start_address;
		info->kptr = (void *)src;
	}
	return 0;
}

static const struct cgs_ops amdgpu_cgs_ops = {
	.read_register = amdgpu_cgs_read_register,
	.write_register = amdgpu_cgs_write_register,
	.read_ind_register = amdgpu_cgs_read_ind_register,
	.write_ind_register = amdgpu_cgs_write_ind_register,
	.get_firmware_info = amdgpu_cgs_get_firmware_info,
};

struct cgs_device *amdgpu_cgs_create_device(struct amdgpu_device *adev)
{
	struct amdgpu_cgs_device *cgs_device =
		kmalloc(sizeof(*cgs_device), GFP_KERNEL);

	if (!cgs_device) {
		DRM_ERROR("Couldn't allocate CGS device structure\n");
		return NULL;
	}

	cgs_device->base.ops = &amdgpu_cgs_ops;
	cgs_device->adev = adev;

	return (struct cgs_device *)cgs_device;
}

void amdgpu_cgs_destroy_device(struct cgs_device *cgs_device)
{
	kfree(cgs_device);
}
