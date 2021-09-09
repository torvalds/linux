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
	const struct smc_firmware_header_v1_0 *v1_0_hdr;
	const struct smc_firmware_header_v2_0 *v2_0_hdr;
	const struct smc_firmware_header_v2_1 *v2_1_hdr;

	DRM_DEBUG("SMC\n");
	amdgpu_ucode_print_common_hdr(hdr);

	if (version_major == 1) {
		v1_0_hdr = container_of(hdr, struct smc_firmware_header_v1_0, header);
		DRM_DEBUG("ucode_start_addr: %u\n", le32_to_cpu(v1_0_hdr->ucode_start_addr));
	} else if (version_major == 2) {
		switch (version_minor) {
		case 0:
			v2_0_hdr = container_of(hdr, struct smc_firmware_header_v2_0, v1_0.header);
			DRM_DEBUG("ppt_offset_bytes: %u\n", le32_to_cpu(v2_0_hdr->ppt_offset_bytes));
			DRM_DEBUG("ppt_size_bytes: %u\n", le32_to_cpu(v2_0_hdr->ppt_size_bytes));
			break;
		case 1:
			v2_1_hdr = container_of(hdr, struct smc_firmware_header_v2_1, v1_0.header);
			DRM_DEBUG("pptable_count: %u\n", le32_to_cpu(v2_1_hdr->pptable_count));
			DRM_DEBUG("pptable_entry_offset: %u\n", le32_to_cpu(v2_1_hdr->pptable_entry_offset));
			break;
		default:
			break;
		}

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
		DRM_DEBUG("reg_list_separate_array_offset_bytes: %u\n",
			  le32_to_cpu(rlc_hdr->reg_list_separate_array_offset_bytes));
		if (version_minor == 1) {
			const struct rlc_firmware_header_v2_1 *v2_1 =
				container_of(rlc_hdr, struct rlc_firmware_header_v2_1, v2_0);
			DRM_DEBUG("reg_list_format_direct_reg_list_length: %u\n",
				  le32_to_cpu(v2_1->reg_list_format_direct_reg_list_length));
			DRM_DEBUG("save_restore_list_cntl_ucode_ver: %u\n",
				  le32_to_cpu(v2_1->save_restore_list_cntl_ucode_ver));
			DRM_DEBUG("save_restore_list_cntl_feature_ver: %u\n",
				  le32_to_cpu(v2_1->save_restore_list_cntl_feature_ver));
			DRM_DEBUG("save_restore_list_cntl_size_bytes %u\n",
				  le32_to_cpu(v2_1->save_restore_list_cntl_size_bytes));
			DRM_DEBUG("save_restore_list_cntl_offset_bytes: %u\n",
				  le32_to_cpu(v2_1->save_restore_list_cntl_offset_bytes));
			DRM_DEBUG("save_restore_list_gpm_ucode_ver: %u\n",
				  le32_to_cpu(v2_1->save_restore_list_gpm_ucode_ver));
			DRM_DEBUG("save_restore_list_gpm_feature_ver: %u\n",
				  le32_to_cpu(v2_1->save_restore_list_gpm_feature_ver));
			DRM_DEBUG("save_restore_list_gpm_size_bytes %u\n",
				  le32_to_cpu(v2_1->save_restore_list_gpm_size_bytes));
			DRM_DEBUG("save_restore_list_gpm_offset_bytes: %u\n",
				  le32_to_cpu(v2_1->save_restore_list_gpm_offset_bytes));
			DRM_DEBUG("save_restore_list_srm_ucode_ver: %u\n",
				  le32_to_cpu(v2_1->save_restore_list_srm_ucode_ver));
			DRM_DEBUG("save_restore_list_srm_feature_ver: %u\n",
				  le32_to_cpu(v2_1->save_restore_list_srm_feature_ver));
			DRM_DEBUG("save_restore_list_srm_size_bytes %u\n",
				  le32_to_cpu(v2_1->save_restore_list_srm_size_bytes));
			DRM_DEBUG("save_restore_list_srm_offset_bytes: %u\n",
				  le32_to_cpu(v2_1->save_restore_list_srm_offset_bytes));
		}
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

void amdgpu_ucode_print_psp_hdr(const struct common_firmware_header *hdr)
{
	uint16_t version_major = le16_to_cpu(hdr->header_version_major);
	uint16_t version_minor = le16_to_cpu(hdr->header_version_minor);

	DRM_DEBUG("PSP\n");
	amdgpu_ucode_print_common_hdr(hdr);

	if (version_major == 1) {
		const struct psp_firmware_header_v1_0 *psp_hdr =
			container_of(hdr, struct psp_firmware_header_v1_0, header);

		DRM_DEBUG("ucode_feature_version: %u\n",
			  le32_to_cpu(psp_hdr->sos.fw_version));
		DRM_DEBUG("sos_offset_bytes: %u\n",
			  le32_to_cpu(psp_hdr->sos.offset_bytes));
		DRM_DEBUG("sos_size_bytes: %u\n",
			  le32_to_cpu(psp_hdr->sos.size_bytes));
		if (version_minor == 1) {
			const struct psp_firmware_header_v1_1 *psp_hdr_v1_1 =
				container_of(psp_hdr, struct psp_firmware_header_v1_1, v1_0);
			DRM_DEBUG("toc_header_version: %u\n",
				  le32_to_cpu(psp_hdr_v1_1->toc.fw_version));
			DRM_DEBUG("toc_offset_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_1->toc.offset_bytes));
			DRM_DEBUG("toc_size_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_1->toc.size_bytes));
			DRM_DEBUG("kdb_header_version: %u\n",
				  le32_to_cpu(psp_hdr_v1_1->kdb.fw_version));
			DRM_DEBUG("kdb_offset_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_1->kdb.offset_bytes));
			DRM_DEBUG("kdb_size_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_1->kdb.size_bytes));
		}
		if (version_minor == 2) {
			const struct psp_firmware_header_v1_2 *psp_hdr_v1_2 =
				container_of(psp_hdr, struct psp_firmware_header_v1_2, v1_0);
			DRM_DEBUG("kdb_header_version: %u\n",
				  le32_to_cpu(psp_hdr_v1_2->kdb.fw_version));
			DRM_DEBUG("kdb_offset_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_2->kdb.offset_bytes));
			DRM_DEBUG("kdb_size_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_2->kdb.size_bytes));
		}
		if (version_minor == 3) {
			const struct psp_firmware_header_v1_1 *psp_hdr_v1_1 =
				container_of(psp_hdr, struct psp_firmware_header_v1_1, v1_0);
			const struct psp_firmware_header_v1_3 *psp_hdr_v1_3 =
				container_of(psp_hdr_v1_1, struct psp_firmware_header_v1_3, v1_1);
			DRM_DEBUG("toc_header_version: %u\n",
				  le32_to_cpu(psp_hdr_v1_3->v1_1.toc.fw_version));
			DRM_DEBUG("toc_offset_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_3->v1_1.toc.offset_bytes));
			DRM_DEBUG("toc_size_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_3->v1_1.toc.size_bytes));
			DRM_DEBUG("kdb_header_version: %u\n",
				  le32_to_cpu(psp_hdr_v1_3->v1_1.kdb.fw_version));
			DRM_DEBUG("kdb_offset_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_3->v1_1.kdb.offset_bytes));
			DRM_DEBUG("kdb_size_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_3->v1_1.kdb.size_bytes));
			DRM_DEBUG("spl_header_version: %u\n",
				  le32_to_cpu(psp_hdr_v1_3->spl.fw_version));
			DRM_DEBUG("spl_offset_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_3->spl.offset_bytes));
			DRM_DEBUG("spl_size_bytes: %u\n",
				  le32_to_cpu(psp_hdr_v1_3->spl.size_bytes));
		}
	} else {
		DRM_ERROR("Unknown PSP ucode version: %u.%u\n",
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
	case CHIP_HAINAN:
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
	case CHIP_VEGAM:
		return AMDGPU_FW_LOAD_SMU;
	case CHIP_VEGA10:
	case CHIP_RAVEN:
	case CHIP_VEGA12:
	case CHIP_VEGA20:
	case CHIP_ARCTURUS:
	case CHIP_RENOIR:
	case CHIP_NAVI10:
	case CHIP_NAVI14:
	case CHIP_NAVI12:
	case CHIP_SIENNA_CICHLID:
	case CHIP_NAVY_FLOUNDER:
	case CHIP_VANGOGH:
	case CHIP_DIMGREY_CAVEFISH:
	case CHIP_ALDEBARAN:
	case CHIP_BEIGE_GOBY:
	case CHIP_YELLOW_CARP:
		if (!load_type)
			return AMDGPU_FW_LOAD_DIRECT;
		else
			return AMDGPU_FW_LOAD_PSP;
	case CHIP_CYAN_SKILLFISH:
		if (!(load_type &&
		      adev->apu_flags & AMD_APU_IS_CYAN_SKILLFISH2))
			return AMDGPU_FW_LOAD_DIRECT;
		else
			return AMDGPU_FW_LOAD_PSP;
	default:
		DRM_ERROR("Unknown firmware load type\n");
	}

	return AMDGPU_FW_LOAD_DIRECT;
}

const char *amdgpu_ucode_name(enum AMDGPU_UCODE_ID ucode_id)
{
	switch (ucode_id) {
	case AMDGPU_UCODE_ID_SDMA0:
		return "SDMA0";
	case AMDGPU_UCODE_ID_SDMA1:
		return "SDMA1";
	case AMDGPU_UCODE_ID_SDMA2:
		return "SDMA2";
	case AMDGPU_UCODE_ID_SDMA3:
		return "SDMA3";
	case AMDGPU_UCODE_ID_SDMA4:
		return "SDMA4";
	case AMDGPU_UCODE_ID_SDMA5:
		return "SDMA5";
	case AMDGPU_UCODE_ID_SDMA6:
		return "SDMA6";
	case AMDGPU_UCODE_ID_SDMA7:
		return "SDMA7";
	case AMDGPU_UCODE_ID_CP_CE:
		return "CP_CE";
	case AMDGPU_UCODE_ID_CP_PFP:
		return "CP_PFP";
	case AMDGPU_UCODE_ID_CP_ME:
		return "CP_ME";
	case AMDGPU_UCODE_ID_CP_MEC1:
		return "CP_MEC1";
	case AMDGPU_UCODE_ID_CP_MEC1_JT:
		return "CP_MEC1_JT";
	case AMDGPU_UCODE_ID_CP_MEC2:
		return "CP_MEC2";
	case AMDGPU_UCODE_ID_CP_MEC2_JT:
		return "CP_MEC2_JT";
	case AMDGPU_UCODE_ID_CP_MES:
		return "CP_MES";
	case AMDGPU_UCODE_ID_CP_MES_DATA:
		return "CP_MES_DATA";
	case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_CNTL:
		return "RLC_RESTORE_LIST_CNTL";
	case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_GPM_MEM:
		return "RLC_RESTORE_LIST_GPM_MEM";
	case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_SRM_MEM:
		return "RLC_RESTORE_LIST_SRM_MEM";
	case AMDGPU_UCODE_ID_RLC_IRAM:
		return "RLC_IRAM";
	case AMDGPU_UCODE_ID_RLC_DRAM:
		return "RLC_DRAM";
	case AMDGPU_UCODE_ID_RLC_G:
		return "RLC_G";
	case AMDGPU_UCODE_ID_STORAGE:
		return "STORAGE";
	case AMDGPU_UCODE_ID_SMC:
		return "SMC";
	case AMDGPU_UCODE_ID_UVD:
		return "UVD";
	case AMDGPU_UCODE_ID_UVD1:
		return "UVD1";
	case AMDGPU_UCODE_ID_VCE:
		return "VCE";
	case AMDGPU_UCODE_ID_VCN:
		return "VCN";
	case AMDGPU_UCODE_ID_VCN1:
		return "VCN1";
	case AMDGPU_UCODE_ID_DMCU_ERAM:
		return "DMCU_ERAM";
	case AMDGPU_UCODE_ID_DMCU_INTV:
		return "DMCU_INTV";
	case AMDGPU_UCODE_ID_VCN0_RAM:
		return "VCN0_RAM";
	case AMDGPU_UCODE_ID_VCN1_RAM:
		return "VCN1_RAM";
	case AMDGPU_UCODE_ID_DMCUB:
		return "DMCUB";
	default:
		return "UNKNOWN UCODE";
	}
}

#define FW_VERSION_ATTR(name, mode, field)				\
static ssize_t show_##name(struct device *dev,				\
			  struct device_attribute *attr,		\
			  char *buf)					\
{									\
	struct drm_device *ddev = dev_get_drvdata(dev);			\
	struct amdgpu_device *adev = drm_to_adev(ddev);			\
									\
	return snprintf(buf, PAGE_SIZE, "0x%08x\n", adev->field);	\
}									\
static DEVICE_ATTR(name, mode, show_##name, NULL)

FW_VERSION_ATTR(vce_fw_version, 0444, vce.fw_version);
FW_VERSION_ATTR(uvd_fw_version, 0444, uvd.fw_version);
FW_VERSION_ATTR(mc_fw_version, 0444, gmc.fw_version);
FW_VERSION_ATTR(me_fw_version, 0444, gfx.me_fw_version);
FW_VERSION_ATTR(pfp_fw_version, 0444, gfx.pfp_fw_version);
FW_VERSION_ATTR(ce_fw_version, 0444, gfx.ce_fw_version);
FW_VERSION_ATTR(rlc_fw_version, 0444, gfx.rlc_fw_version);
FW_VERSION_ATTR(rlc_srlc_fw_version, 0444, gfx.rlc_srlc_fw_version);
FW_VERSION_ATTR(rlc_srlg_fw_version, 0444, gfx.rlc_srlg_fw_version);
FW_VERSION_ATTR(rlc_srls_fw_version, 0444, gfx.rlc_srls_fw_version);
FW_VERSION_ATTR(mec_fw_version, 0444, gfx.mec_fw_version);
FW_VERSION_ATTR(mec2_fw_version, 0444, gfx.mec2_fw_version);
FW_VERSION_ATTR(sos_fw_version, 0444, psp.sos.fw_version);
FW_VERSION_ATTR(asd_fw_version, 0444, psp.asd.fw_version);
FW_VERSION_ATTR(ta_ras_fw_version, 0444, psp.ras.feature_version);
FW_VERSION_ATTR(ta_xgmi_fw_version, 0444, psp.xgmi.feature_version);
FW_VERSION_ATTR(smc_fw_version, 0444, pm.fw_version);
FW_VERSION_ATTR(sdma_fw_version, 0444, sdma.instance[0].fw_version);
FW_VERSION_ATTR(sdma2_fw_version, 0444, sdma.instance[1].fw_version);
FW_VERSION_ATTR(vcn_fw_version, 0444, vcn.fw_version);
FW_VERSION_ATTR(dmcu_fw_version, 0444, dm.dmcu_fw_version);

static struct attribute *fw_attrs[] = {
	&dev_attr_vce_fw_version.attr, &dev_attr_uvd_fw_version.attr,
	&dev_attr_mc_fw_version.attr, &dev_attr_me_fw_version.attr,
	&dev_attr_pfp_fw_version.attr, &dev_attr_ce_fw_version.attr,
	&dev_attr_rlc_fw_version.attr, &dev_attr_rlc_srlc_fw_version.attr,
	&dev_attr_rlc_srlg_fw_version.attr, &dev_attr_rlc_srls_fw_version.attr,
	&dev_attr_mec_fw_version.attr, &dev_attr_mec2_fw_version.attr,
	&dev_attr_sos_fw_version.attr, &dev_attr_asd_fw_version.attr,
	&dev_attr_ta_ras_fw_version.attr, &dev_attr_ta_xgmi_fw_version.attr,
	&dev_attr_smc_fw_version.attr, &dev_attr_sdma_fw_version.attr,
	&dev_attr_sdma2_fw_version.attr, &dev_attr_vcn_fw_version.attr,
	&dev_attr_dmcu_fw_version.attr, NULL
};

static const struct attribute_group fw_attr_group = {
	.name = "fw_version",
	.attrs = fw_attrs
};

int amdgpu_ucode_sysfs_init(struct amdgpu_device *adev)
{
	return sysfs_create_group(&adev->dev->kobj, &fw_attr_group);
}

void amdgpu_ucode_sysfs_fini(struct amdgpu_device *adev)
{
	sysfs_remove_group(&adev->dev->kobj, &fw_attr_group);
}

static int amdgpu_ucode_init_single_fw(struct amdgpu_device *adev,
				       struct amdgpu_firmware_info *ucode,
				       uint64_t mc_addr, void *kptr)
{
	const struct common_firmware_header *header = NULL;
	const struct gfx_firmware_header_v1_0 *cp_hdr = NULL;
	const struct dmcu_firmware_header_v1_0 *dmcu_hdr = NULL;
	const struct dmcub_firmware_header_v1_0 *dmcub_hdr = NULL;
	const struct mes_firmware_header_v1_0 *mes_hdr = NULL;
	u8 *ucode_addr;

	if (NULL == ucode->fw)
		return 0;

	ucode->mc_addr = mc_addr;
	ucode->kaddr = kptr;

	if (ucode->ucode_id == AMDGPU_UCODE_ID_STORAGE)
		return 0;

	header = (const struct common_firmware_header *)ucode->fw->data;
	cp_hdr = (const struct gfx_firmware_header_v1_0 *)ucode->fw->data;
	dmcu_hdr = (const struct dmcu_firmware_header_v1_0 *)ucode->fw->data;
	dmcub_hdr = (const struct dmcub_firmware_header_v1_0 *)ucode->fw->data;
	mes_hdr = (const struct mes_firmware_header_v1_0 *)ucode->fw->data;

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		switch (ucode->ucode_id) {
		case AMDGPU_UCODE_ID_CP_MEC1:
		case AMDGPU_UCODE_ID_CP_MEC2:
			ucode->ucode_size = le32_to_cpu(header->ucode_size_bytes) -
				le32_to_cpu(cp_hdr->jt_size) * 4;
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(header->ucode_array_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_CP_MEC1_JT:
		case AMDGPU_UCODE_ID_CP_MEC2_JT:
			ucode->ucode_size = le32_to_cpu(cp_hdr->jt_size) * 4;
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(header->ucode_array_offset_bytes) +
				le32_to_cpu(cp_hdr->jt_offset) * 4;
			break;
		case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_CNTL:
			ucode->ucode_size = adev->gfx.rlc.save_restore_list_cntl_size_bytes;
			ucode_addr = adev->gfx.rlc.save_restore_list_cntl;
			break;
		case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_GPM_MEM:
			ucode->ucode_size = adev->gfx.rlc.save_restore_list_gpm_size_bytes;
			ucode_addr = adev->gfx.rlc.save_restore_list_gpm;
			break;
		case AMDGPU_UCODE_ID_RLC_RESTORE_LIST_SRM_MEM:
			ucode->ucode_size = adev->gfx.rlc.save_restore_list_srm_size_bytes;
			ucode_addr = adev->gfx.rlc.save_restore_list_srm;
			break;
		case AMDGPU_UCODE_ID_RLC_IRAM:
			ucode->ucode_size = adev->gfx.rlc.rlc_iram_ucode_size_bytes;
			ucode_addr = adev->gfx.rlc.rlc_iram_ucode;
			break;
		case AMDGPU_UCODE_ID_RLC_DRAM:
			ucode->ucode_size = adev->gfx.rlc.rlc_dram_ucode_size_bytes;
			ucode_addr = adev->gfx.rlc.rlc_dram_ucode;
			break;
		case AMDGPU_UCODE_ID_CP_MES:
			ucode->ucode_size = le32_to_cpu(mes_hdr->mes_ucode_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(mes_hdr->mes_ucode_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_CP_MES_DATA:
			ucode->ucode_size = le32_to_cpu(mes_hdr->mes_ucode_data_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(mes_hdr->mes_ucode_data_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_DMCU_ERAM:
			ucode->ucode_size = le32_to_cpu(header->ucode_size_bytes) -
				le32_to_cpu(dmcu_hdr->intv_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(header->ucode_array_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_DMCU_INTV:
			ucode->ucode_size = le32_to_cpu(dmcu_hdr->intv_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(header->ucode_array_offset_bytes) +
				le32_to_cpu(dmcu_hdr->intv_offset_bytes);
			break;
		case AMDGPU_UCODE_ID_DMCUB:
			ucode->ucode_size = le32_to_cpu(dmcub_hdr->inst_const_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(header->ucode_array_offset_bytes);
			break;
		default:
			ucode->ucode_size = le32_to_cpu(header->ucode_size_bytes);
			ucode_addr = (u8 *)ucode->fw->data +
				le32_to_cpu(header->ucode_array_offset_bytes);
			break;
		}
	} else {
		ucode->ucode_size = le32_to_cpu(header->ucode_size_bytes);
		ucode_addr = (u8 *)ucode->fw->data +
			le32_to_cpu(header->ucode_array_offset_bytes);
	}

	memcpy(ucode->kaddr, ucode_addr, ucode->ucode_size);

	return 0;
}

static int amdgpu_ucode_patch_jt(struct amdgpu_firmware_info *ucode,
				uint64_t mc_addr, void *kptr)
{
	const struct gfx_firmware_header_v1_0 *header = NULL;
	const struct common_firmware_header *comm_hdr = NULL;
	uint8_t *src_addr = NULL;
	uint8_t *dst_addr = NULL;

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

int amdgpu_ucode_create_bo(struct amdgpu_device *adev)
{
	if (adev->firmware.load_type != AMDGPU_FW_LOAD_DIRECT) {
		amdgpu_bo_create_kernel(adev, adev->firmware.fw_size, PAGE_SIZE,
			amdgpu_sriov_vf(adev) ? AMDGPU_GEM_DOMAIN_VRAM : AMDGPU_GEM_DOMAIN_GTT,
			&adev->firmware.fw_buf,
			&adev->firmware.fw_buf_mc,
			&adev->firmware.fw_buf_ptr);
		if (!adev->firmware.fw_buf) {
			dev_err(adev->dev, "failed to create kernel buffer for firmware.fw_buf\n");
			return -ENOMEM;
		} else if (amdgpu_sriov_vf(adev)) {
			memset(adev->firmware.fw_buf_ptr, 0, adev->firmware.fw_size);
		}
	}
	return 0;
}

void amdgpu_ucode_free_bo(struct amdgpu_device *adev)
{
	if (adev->firmware.load_type != AMDGPU_FW_LOAD_DIRECT)
		amdgpu_bo_free_kernel(&adev->firmware.fw_buf,
		&adev->firmware.fw_buf_mc,
		&adev->firmware.fw_buf_ptr);
}

int amdgpu_ucode_init_bo(struct amdgpu_device *adev)
{
	uint64_t fw_offset = 0;
	int i;
	struct amdgpu_firmware_info *ucode = NULL;

 /* for baremetal, the ucode is allocated in gtt, so don't need to fill the bo when reset/suspend */
	if (!amdgpu_sriov_vf(adev) && (amdgpu_in_reset(adev) || adev->in_suspend))
		return 0;
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
}
