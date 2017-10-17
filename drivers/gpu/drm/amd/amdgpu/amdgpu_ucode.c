/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
 */

#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_ucode.h"

static void amdgpu_ucode_print_common_hdr(const struct common_firmware_header *hdr)
{
	DRM_DEBUG("size_bytes: %u\n", le32_to_cpu(hdr->size_bytes));
	DRM_DEBUG("header_size_bytes: %u\n", le32_to_cpu(hdr->header_size_bytes));
	DRM_DEBUG("header_version_major: %u\n", le16_to_cpu(hdr->header_version_major));
	DRM_DEBUG("header_version_minor: %u\n", le16_to_cpu(hdr->header_version_minor));
	DRM_DEBUG("ip_version_major: %u\n", le16_to_cpu(hdr->ip_version_major));
	DRM_DEBUG("ip_version_minor: %u\n", le16_to_cpu(hdr->ip_version_minor));
	DRM_DEBUG("ucode_version: 0x%08x\n", le32_to_cpu(hdr->ucode_version));
	DRM_DEBUG("ucode_size_bytes: %u\n", le32_to_cpu(hdr->ucode_size_bytes));
	DRM_DEBUG("ucode_array_offset_bytes: %u\n",
		  le32_to_cpu(hdr->ucode_array_offset_bytes));
	DRM_DEBUG("crc32: 0x%08x\n", le32_to_cpu(hdr->crc32));
}

void amdgpu_ucode_print_mc_hdr(const struct common_firmware_header *hdr)
{
	uint16_t version_major = le16_to_cpu(hdr->header_version_major);
	uint16_t version_minor = le16_to_cpu(hdr->header_version_minor);

	DRM_DEBUG("MC\n");
	amdgpu_ucode_print_common_hdr(hdr);

	if (version_major == 1) {
		const struct mc_firmware_header_v1_0 *mc_hdr =
			container_of(hdr, struct mc_firmware_header_v1_0, header);

		DRM_DEBUG("io_debug_size_bytes: %u\n",
			  le32_to_cpu(mc_hdr->io_debug_size_bytes));
		DRM_DEBUG("io_debug_array_offset_bytes: %u\n",
			  le32_to_cpu(mc_hdr->io_debug_array_offset_bytes));
	} else {
		DRM_ERROR("Unknown MC ucode version: %u.%u\n", version_major, version_minor);
	}
}

void amdgpu_ucode_print_smc_hdr(const struct common_firmware_header *hdr)
{
	uint16_t version_major = le16_to_cpu(hdr->header_version_major);
	uint16_t version_minor = le16_to_cpu(hdr->header_version_minor);

	DRM_DEBUG("SMC\n");
	amdgpu_ucode_print_common_hdr(hdr);

	if (version_major == 1) {
		const struct smc_firmware_header_v1_0 *smc_hdr =
			container_of(hdr, struct smc_firmware_header_v1_0, header);

		DRM_DEBUG("ucode_start_addr: %u\n", le32_to_cpu(smc_hdr->ucode_start_addr));
	} else {
		DRM_ERROR("Unknown SMC ucode version: %u.%u\n", version_major, version_minor);
	}
}

void amdgpu_ucode_print_gfx_hdr(const struct common_firmware_header *hdr)
{
	uint16_t version_major = le16_to_cpu(hdr->header_version_major);
	uint16_t version_minor = le16_to_cpu(hdr->header_version_minor);

	DRM_DEBUG("GFX\n");
	amdgpu_ucode_print_common_hdr(hdr);

	if (version_major == 1) {
		const struct gfx_firmware_header_v1_0 *gfx_hdr =
			container_of(hdr, struct gfx_firmware_header_v1_0, header);

		DRM_DEBUG("ucode_feature_version: %u\n",
			  le32_to_cpu(gfx_hdr->ucode_feature_version));
		DRM_DEBUG("jt_offset: %u\n", le32_to_cpu(gfx_hdr->jt_offset));
		DRM_DEBUG("jt_size: %u\n", le32_to_cpu(gfx_hdr->jt_size));
	} else {
		DRM_ERROR("Unknown GFX ucode version: %u.%u\n", version_major, version_minor);
	}
}

void amdgpu_ucode_print_rlc_hdr(const struct common_firmware_header *hdr)
{
	uint16_t version_major = le16_to_cpu(hdr->header_version_major);
	uint16_t version_minor = le16_to_cpu(hdr->header_version_minor);

	DRM_DEBUG("RLC\n");
	amdgpu_ucode_print_common_hdr(hdr);

	if (version_major == 1) {
		const struct rlc_firmware_header_v1_0 *rlc_hdr =
			container_of(hdr, struct rlc_firmware_header_v1_0, header);

		DRM_DEBUG("ucode_feature_version: %u\n",
			  le32_to_cpu(rlc_hdr->ucode_feature_version));
		DRM_DEBUG("save_and_restore_offset: %u\n",
			  le32_to_cpu(rlc_hdr->save_and_restore_offset));
		DRM_DEBUG("clear_state_descriptor_offset: %u\n",
			  le32_to_cpu(rlc_hdr->clear_state_descriptor_offset));
		DRM_DEBUG("avail_scratch_ram_locations: %u\n",
			  le32_to_cpu(rlc_hdr->avail_scratch_ram_locations));
		DRM_DEBUG("master_pkt_description_offset: %u\n",
			  le32_to_cpu(rlc_hdr->master_pkt_description_offset));
	} else if (version_major == 2) {
		const struct rlc_firmware_header_v2_0 *rlc_hdr =
			container_of(hdr, struct rlc_firmware_header_v2_0, header);

		DRM_DEBUG("ucode_feature_version: %u\n",
			  le32_to_cpu(rlc_hdr->ucode_feature_version));
		DRM_DEBUG("jt_offset: %u\n", le32_to_cpu(rlc_hdr->jt_offset));
		DRM_DEBUG("jt_size: %u\n", le32_to_cpu(rlc_hdr->jt_size));
		DRM_DEBUG("save_and_restore_offset: %u\n",
			  le32_to_cpu(rlc_hdr->save_and_restore_offset));
		DRM_DEBUG("clear_state_descriptor_offset: %u\n",
			  le32_to_cpu(rlc_hdr->clear_state_descriptor_offset));
		DRM_DEBUG("avail_scratch_ram_locations: %u\n",
			  le32_to_cpu(rlc_hdr->avail_scratch_ram_locations));
		DRM_DEBUG("reg_restore_list_size: %u\n",
			  le32_to_cpu(rlc_hdr->reg_restore_list_size));
		DRM_DEBUG("reg_list_format_start: %u\n",
			  le32_to_cpu(rlc_hdr->reg_list_format_start));
		DRM_DEBUG("reg_list_format_separate_start: %u\n",
			  le32_to_cpu(rlc_hdr->reg_list_format_separate_start));
		DRM_DEBUG("starting_offsets_start: %u\n",
			  le32_to_cpu(rlc_hdr->starting_offsets_start));
		DRM_DEBUG("reg_list_format_size_bytes: %u\n",
			  le32_to_cpu(rlc_hdr->reg_list_format_size_bytes));
		DRM_DEBUG("reg_list_format_array_offset_bytes: %u\n",
			  le32_to_cpu(rlc_hdr->reg_list_format_array_offset_bytes));
		DRM_DEBUG("reg_list_size_bytes: %u\n",
			  le32_to_cpu(rlc_hdr->reg_list_size_bytes));
		DRM_DEBUG("reg_list_array_offset_bytes: %u\n",
			  le32_to_cpu(rlc_hdr->reg_list_array_offset_bytes));
		DRM_DEBUG("reg_list_format_separate_size_bytes: %u\n",
			  le32_to_cpu(rlc_hdr->reg_list_format_separate_size_bytes));
		DRM_DEBUG("reg_list_format_separate_array_offset_bytes: %u\n",
			  le32_to_cpu(rlc_hdr->reg_list_format_separate_array_offset_bytes));
		DRM_DEBUG("reg_list_separate_size_bytes: %u\n",
			  le32_to_cpu(rlc_hdr->reg_list_separate_size_bytes));
		DRM_DEBUG("reg_list_separate_size_bytes: %u\n",
			  le32_to_cpu(rlc_hdr->reg_list_separate_size_bytes));
	} else {
		DRM_ERROR("Unknown RLC ucode version: %u.%u\n", version_major, version_minor);
	}
}

void amdgpu_ucode_print_sdma_hdr(const struct common_firmware_header *hdr)
{
	uint16_t version_major = le16_to_cpu(hdr->header_version_major);
	uint16_t version_minor = le16_to_cpu(hdr->header_version_minor);

	DRM_DEBUG("SDMA\n");
	amdgpu_ucode_print_common_hdr(hdr);

	if (version_major == 1) {
		const struct sdma_firmware_header_v1_0 *sdma_hdr =
			container_of(hdr, struct sdma_firmware_header_v1_0, header);

		DRM_DEBUG("ucode_feature_version: %u\n",
			  le32_to_cpu(sdma_hdr->ucode_feature_version));
		DRM_DEBUG("ucode_change_version: %u\n",
			  le32_to_cpu(sdma_hdr->ucode_change_version));
		DRM_DEBUG("jt_offset: %u\n", le32_to_cpu(sdma_hdr->jt_offset));
		DRM_DEBUG("jt_size: %u\n", le32_to_cpu(sdma_hdr->jt_size));
		if (version_minor >= 1) {
			const struct sdma_firmware_header_v1_1 *sdma_v1_1_hdr =
				container_of(sdma_hdr, struct sdma_firmware_header_v1_1, v1_0);
			DRM_DEBUG("digest_size: %u\n", le32_to_cpu(sdma_v1_1_hdr->digest_size));
		}
	} else {
		DRM_ERROR("Unknown SDMA ucode version: %u.%u\n",
			  version_major, version_minor);
	}
}

void amdgpu_ucode_print_gpu_info_hdr(const struct common_firmware_header *hdr)
{
	uint16_t version_major = le16_to_cpu(hdr->header_version_major);
	uint16_t version_minor = le16_to_cpu(hdr->header_version_minor);

	DRM_DEBUG("GPU_INFO\n");
	amdgpu_ucode_print_common_hdr(hdr);

	if (version_major == 1) {
		const struct gpu_info_firmware_header_v1_0 *gpu_info_hdr =
			container_of(hdr, struct gpu_info_firmware_header_v1_0, header);

		DRM_DEBUG("version_major: %u\n",
			  le16_to_cpu(gpu_info_hdr->version_major));
		DRM_DEBUG("version_minor: %u\n",
			  le16_to_cpu(gpu_info_hdr->version_minor));
	} else {
		DRM_ERROR("Unknown gpu_info ucode version: %u.%u\n", version_major, version_minor);
	}
}

int amdgpu_ucode_validate(const struct firmware *fw)
{
	const struct common_firmware_header *hdr =
		(const struct common_firmware_header *)fw->data;

	if (fw->size == le32_to_cpu(hdr->size_bytes))
		return 0;

	return -EINVAL;
}

bool amdgpu_ucode_hdr_version(union amdgpu_firmware_header *hdr,
				uint16_t hdr_major, uint16_t hdr_minor)
{
	if ((hdr->common.header_version_major == hdr_major) &&
		(hdr->common.header_version_minor == hdr_minor))
		return false;
	return true;
}

enum amdgpu_firmware_load_type
amdgpu_ucode_get_load_type(struct amdgpu_device *adev, int load_type)
{
	switch (adev->asic_type) {
#ifdef CONFIG_DRM_AMDGPU_SI
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
	case CHIP_VERDE:
	case CHIP_OLAND:
		return AMDGPU_FW_LOAD_DIRECT;
#endif
#ifdef CONFIG_DRM_AMDGPU_CIK
	case CHIP_BONAIRE:
	case CHIP_KAVERI:
	case CHIP_KABINI:
	case CHIP_HAWAII:
	case CHIP_MULLINS:
		return AMDGPU_FW_LOAD_DIRECT;
#endif
	case CHIP_TOPAZ:
	case CHIP_TONGA:
	case CHIP_FIJI:
	case CHIP_CARRIZO:
	case CHIP_STONEY:
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
		if (!load_type)
			return AMDGPU_FW_LOAD_DIRECT;
		else
			return AMDGPU_FW_LOAD_SMU;
	case CHIP_VEGA10:
	case CHIP_RAVEN:
		if (!load_type)
			return AMDGPU_FW_LOAD_DIRECT;
		else
			return AMDGPU_FW_LOAD_PSP;
	default:
		DRM_ERROR("Unknow firmware load type\n");
	}

	return AMDGPU_FW_LOAD_DIRECT;
}

static int amdgpu_ucode_init_single_fw(struct amdgpu_device *adev,
				       struct amdgpu_firmware_info *ucode,
				       uint64_t mc_addr, void *kptr)
{
	const struct common_firmware_header *header = NULL;
	const struct gfx_firmware_header_v1_0 *cp_hdr = NULL;

	if (NULL == ucode->fw)
		return 0;

	ucode->mc_addr = mc_addr;
	ucode->kaddr = kptr;

	if (ucode->ucode_id == AMDGPU_UCODE_ID_STORAGE)
		return 0;

	header = (const struct common_firmware_header *)ucode->fw->data;

	cp_hdr = (const struct gfx_firmware_header_v1_0 *)ucode->fw->data;

	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP ||
	    (ucode->ucode_id != AMDGPU_UCODE_ID_CP_MEC1 &&
	     ucode->ucode_id != AMDGPU_UCODE_ID_CP_MEC2 &&
	     ucode->ucode_id != AMDGPU_UCODE_ID_CP_MEC1_JT &&
	     ucode->ucode_id != AMDGPU_UCODE_ID_CP_MEC2_JT)) {
		ucode->ucode_size = le32_to_cpu(header->ucode_size_bytes);

		memcpy(ucode->kaddr, (void *)((uint8_t *)ucode->fw->data +
					      le32_to_cpu(header->ucode_array_offset_bytes)),
		       ucode->ucode_size);
	} else if (ucode->ucode_id == AMDGPU_UCODE_ID_CP_MEC1 ||
		   ucode->ucode_id == AMDGPU_UCODE_ID_CP_MEC2) {
		ucode->ucode_size = le32_to_cpu(header->ucode_size_bytes) -
			le32_to_cpu(cp_hdr->jt_size) * 4;

		memcpy(ucode->kaddr, (void *)((uint8_t *)ucode->fw->data +
					      le32_to_cpu(header->ucode_array_offset_bytes)),
		       ucode->ucode_size);
	} else if (ucode->ucode_id == AMDGPU_UCODE_ID_CP_MEC1_JT ||
		   ucode->ucode_id == AMDGPU_UCODE_ID_CP_MEC2_JT) {
		ucode->ucode_size = le32_to_cpu(cp_hdr->jt_size) * 4;

		memcpy(ucode->kaddr, (void *)((uint8_t *)ucode->fw->data +
					      le32_to_cpu(header->ucode_array_offset_bytes) +
					      le32_to_cpu(cp_hdr->jt_offset) * 4),
		       ucode->ucode_size);
	}

	return 0;
}

static int amdgpu_ucode_patch_jt(struct amdgpu_firmware_info *ucode,
				uint64_t mc_addr, void *kptr)
{
	const struct gfx_firmware_header_v1_0 *header = NULL;
	const struct common_firmware_header *comm_hdr = NULL;
	uint8_t* src_addr = NULL;
	uint8_t* dst_addr = NULL;

	if (NULL == ucode->fw)
		return 0;

	comm_hdr = (const struct common_firmware_header *)ucode->fw->data;
	header = (const struct gfx_firmware_header_v1_0 *)ucode->fw->data;
	dst_addr = ucode->kaddr +
			   ALIGN(le32_to_cpu(comm_hdr->ucode_size_bytes),
			   PAGE_SIZE);
	src_addr = (uint8_t *)ucode->fw->data +
			   le32_to_cpu(comm_hdr->ucode_array_offset_bytes) +
			   (le32_to_cpu(header->jt_offset) * 4);
	memcpy(dst_addr, src_addr, le32_to_cpu(header->jt_size) * 4);

	return 0;
}

int amdgpu_ucode_init_bo(struct amdgpu_device *adev)
{
	struct amdgpu_bo **bo = &adev->firmware.fw_buf;
	uint64_t fw_offset = 0;
	int i, err;
	struct amdgpu_firmware_info *ucode = NULL;
	const struct common_firmware_header *header = NULL;

	if (!adev->firmware.fw_size) {
		dev_warn(adev->dev, "No ip firmware need to load\n");
		return 0;
	}

	if (!amdgpu_sriov_vf(adev) || !adev->in_gpu_reset) {
		err = amdgpu_bo_create(adev, adev->firmware.fw_size, PAGE_SIZE, true,
					amdgpu_sriov_vf(adev) ? AMDGPU_GEM_DOMAIN_VRAM : AMDGPU_GEM_DOMAIN_GTT,
					AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS,
					NULL, NULL, 0, bo);
		if (err) {
			dev_err(adev->dev, "(%d) Firmware buffer allocate failed\n", err);
			goto failed;
		}

		err = amdgpu_bo_reserve(*bo, false);
		if (err) {
			dev_err(adev->dev, "(%d) Firmware buffer reserve failed\n", err);
			goto failed_reserve;
		}

		err = amdgpu_bo_pin(*bo, amdgpu_sriov_vf(adev) ? AMDGPU_GEM_DOMAIN_VRAM : AMDGPU_GEM_DOMAIN_GTT,
					&adev->firmware.fw_buf_mc);
		if (err) {
			dev_err(adev->dev, "(%d) Firmware buffer pin failed\n", err);
			goto failed_pin;
		}

		err = amdgpu_bo_kmap(*bo, &adev->firmware.fw_buf_ptr);
		if (err) {
			dev_err(adev->dev, "(%d) Firmware buffer kmap failed\n", err);
			goto failed_kmap;
		}

		amdgpu_bo_unreserve(*bo);
	}

	memset(adev->firmware.fw_buf_ptr, 0, adev->firmware.fw_size);

	/*
	 * if SMU loaded firmware, it needn't add SMC, UVD, and VCE
	 * ucode info here
	 */
	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
		if (amdgpu_sriov_vf(adev))
			adev->firmware.max_ucodes = AMDGPU_UCODE_ID_MAXIMUM - 3;
		else
			adev->firmware.max_ucodes = AMDGPU_UCODE_ID_MAXIMUM - 4;
	} else {
		adev->firmware.max_ucodes = AMDGPU_UCODE_ID_MAXIMUM;
	}

	for (i = 0; i < adev->firmware.max_ucodes; i++) {
		ucode = &adev->firmware.ucode[i];
		if (ucode->fw) {
			header = (const struct common_firmware_header *)ucode->fw->data;
			amdgpu_ucode_init_single_fw(adev, ucode, adev->firmware.fw_buf_mc + fw_offset,
						    adev->firmware.fw_buf_ptr + fw_offset);
			if (i == AMDGPU_UCODE_ID_CP_MEC1 &&
			    adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
				const struct gfx_firmware_header_v1_0 *cp_hdr;
				cp_hdr = (const struct gfx_firmware_header_v1_0 *)ucode->fw->data;
				amdgpu_ucode_patch_jt(ucode,  adev->firmware.fw_buf_mc + fw_offset,
						    adev->firmware.fw_buf_ptr + fw_offset);
				fw_offset += ALIGN(le32_to_cpu(cp_hdr->jt_size) << 2, PAGE_SIZE);
			}
			fw_offset += ALIGN(ucode->ucode_size, PAGE_SIZE);
		}
	}
	return 0;

failed_kmap:
	amdgpu_bo_unpin(*bo);
failed_pin:
	amdgpu_bo_unreserve(*bo);
failed_reserve:
	amdgpu_bo_unref(bo);
failed:
	if (err)
		adev->firmware.load_type = AMDGPU_FW_LOAD_DIRECT;

	return err;
}

int amdgpu_ucode_fini_bo(struct amdgpu_device *adev)
{
	int i;
	struct amdgpu_firmware_info *ucode = NULL;

	if (!adev->firmware.fw_size)
		return 0;

	for (i = 0; i < adev->firmware.max_ucodes; i++) {
		ucode = &adev->firmware.ucode[i];
		if (ucode->fw) {
			ucode->mc_addr = 0;
			ucode->kaddr = NULL;
		}
	}
	amdgpu_bo_unref(&adev->firmware.fw_buf);
	adev->firmware.fw_buf = NULL;

	return 0;
}
