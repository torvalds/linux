/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
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
#include "amdgpu.h"
#include "amdgpu_gfx.h"
#include "amdgpu_rlc.h"

/**
 * amdgpu_gfx_rlc_enter_safe_mode - Set RLC into safe mode
 *
 * @adev: amdgpu_device pointer
 * @xcc_id: xcc accelerated compute core id
 *
 * Set RLC enter into safe mode if RLC is enabled and haven't in safe mode.
 */
void amdgpu_gfx_rlc_enter_safe_mode(struct amdgpu_device *adev, int xcc_id)
{
	if (adev->gfx.rlc.in_safe_mode[xcc_id])
		return;

	/* if RLC is not enabled, do nothing */
	if (!adev->gfx.rlc.funcs->is_rlc_enabled(adev))
		return;

	if (adev->cg_flags &
	    (AMD_CG_SUPPORT_GFX_CGCG | AMD_CG_SUPPORT_GFX_MGCG |
	     AMD_CG_SUPPORT_GFX_3D_CGCG)) {
		adev->gfx.rlc.funcs->set_safe_mode(adev, xcc_id);
		adev->gfx.rlc.in_safe_mode[xcc_id] = true;
	}
}

/**
 * amdgpu_gfx_rlc_exit_safe_mode - Set RLC out of safe mode
 *
 * @adev: amdgpu_device pointer
 * @xcc_id: xcc accelerated compute core id
 *
 * Set RLC exit safe mode if RLC is enabled and have entered into safe mode.
 */
void amdgpu_gfx_rlc_exit_safe_mode(struct amdgpu_device *adev, int xcc_id)
{
	if (!(adev->gfx.rlc.in_safe_mode[xcc_id]))
		return;

	/* if RLC is not enabled, do nothing */
	if (!adev->gfx.rlc.funcs->is_rlc_enabled(adev))
		return;

	if (adev->cg_flags &
	    (AMD_CG_SUPPORT_GFX_CGCG | AMD_CG_SUPPORT_GFX_MGCG |
	     AMD_CG_SUPPORT_GFX_3D_CGCG)) {
		adev->gfx.rlc.funcs->unset_safe_mode(adev, xcc_id);
		adev->gfx.rlc.in_safe_mode[xcc_id] = false;
	}
}

/**
 * amdgpu_gfx_rlc_init_sr - Init save restore block
 *
 * @adev: amdgpu_device pointer
 * @dws: the size of save restore block
 *
 * Allocate and setup value to save restore block of rlc.
 * Returns 0 on succeess or negative error code if allocate failed.
 */
int amdgpu_gfx_rlc_init_sr(struct amdgpu_device *adev, u32 dws)
{
	const u32 *src_ptr;
	volatile u32 *dst_ptr;
	u32 i;
	int r;

	/* allocate save restore block */
	r = amdgpu_bo_create_reserved(adev, dws * 4, PAGE_SIZE,
				      AMDGPU_GEM_DOMAIN_VRAM |
				      AMDGPU_GEM_DOMAIN_GTT,
				      &adev->gfx.rlc.save_restore_obj,
				      &adev->gfx.rlc.save_restore_gpu_addr,
				      (void **)&adev->gfx.rlc.sr_ptr);
	if (r) {
		dev_warn(adev->dev, "(%d) create RLC sr bo failed\n", r);
		amdgpu_gfx_rlc_fini(adev);
		return r;
	}

	/* write the sr buffer */
	src_ptr = adev->gfx.rlc.reg_list;
	dst_ptr = adev->gfx.rlc.sr_ptr;
	for (i = 0; i < adev->gfx.rlc.reg_list_size; i++)
		dst_ptr[i] = cpu_to_le32(src_ptr[i]);
	amdgpu_bo_kunmap(adev->gfx.rlc.save_restore_obj);
	amdgpu_bo_unreserve(adev->gfx.rlc.save_restore_obj);

	return 0;
}

/**
 * amdgpu_gfx_rlc_init_csb - Init clear state block
 *
 * @adev: amdgpu_device pointer
 *
 * Allocate and setup value to clear state block of rlc.
 * Returns 0 on succeess or negative error code if allocate failed.
 */
int amdgpu_gfx_rlc_init_csb(struct amdgpu_device *adev)
{
	u32 dws;
	int r;

	/* allocate clear state block */
	adev->gfx.rlc.clear_state_size = dws = adev->gfx.rlc.funcs->get_csb_size(adev);
	r = amdgpu_bo_create_kernel(adev, dws * 4, PAGE_SIZE,
				      AMDGPU_GEM_DOMAIN_VRAM |
				      AMDGPU_GEM_DOMAIN_GTT,
				      &adev->gfx.rlc.clear_state_obj,
				      &adev->gfx.rlc.clear_state_gpu_addr,
				      (void **)&adev->gfx.rlc.cs_ptr);
	if (r) {
		dev_err(adev->dev, "(%d) failed to create rlc csb bo\n", r);
		amdgpu_gfx_rlc_fini(adev);
		return r;
	}

	return 0;
}

/**
 * amdgpu_gfx_rlc_init_cpt - Init cp table
 *
 * @adev: amdgpu_device pointer
 *
 * Allocate and setup value to cp table of rlc.
 * Returns 0 on succeess or negative error code if allocate failed.
 */
int amdgpu_gfx_rlc_init_cpt(struct amdgpu_device *adev)
{
	int r;

	r = amdgpu_bo_create_reserved(adev, adev->gfx.rlc.cp_table_size,
				      PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM |
				      AMDGPU_GEM_DOMAIN_GTT,
				      &adev->gfx.rlc.cp_table_obj,
				      &adev->gfx.rlc.cp_table_gpu_addr,
				      (void **)&adev->gfx.rlc.cp_table_ptr);
	if (r) {
		dev_err(adev->dev, "(%d) failed to create cp table bo\n", r);
		amdgpu_gfx_rlc_fini(adev);
		return r;
	}

	/* set up the cp table */
	amdgpu_gfx_rlc_setup_cp_table(adev);
	amdgpu_bo_kunmap(adev->gfx.rlc.cp_table_obj);
	amdgpu_bo_unreserve(adev->gfx.rlc.cp_table_obj);

	return 0;
}

/**
 * amdgpu_gfx_rlc_setup_cp_table - setup cp the buffer of cp table
 *
 * @adev: amdgpu_device pointer
 *
 * Write cp firmware data into cp table.
 */
void amdgpu_gfx_rlc_setup_cp_table(struct amdgpu_device *adev)
{
	const __le32 *fw_data;
	volatile u32 *dst_ptr;
	int me, i, max_me;
	u32 bo_offset = 0;
	u32 table_offset, table_size;

	max_me = adev->gfx.rlc.funcs->get_cp_table_num(adev);

	/* write the cp table buffer */
	dst_ptr = adev->gfx.rlc.cp_table_ptr;
	for (me = 0; me < max_me; me++) {
		if (me == 0) {
			const struct gfx_firmware_header_v1_0 *hdr =
				(const struct gfx_firmware_header_v1_0 *)adev->gfx.ce_fw->data;
			fw_data = (const __le32 *)
				(adev->gfx.ce_fw->data +
				 le32_to_cpu(hdr->header.ucode_array_offset_bytes));
			table_offset = le32_to_cpu(hdr->jt_offset);
			table_size = le32_to_cpu(hdr->jt_size);
		} else if (me == 1) {
			const struct gfx_firmware_header_v1_0 *hdr =
				(const struct gfx_firmware_header_v1_0 *)adev->gfx.pfp_fw->data;
			fw_data = (const __le32 *)
				(adev->gfx.pfp_fw->data +
				 le32_to_cpu(hdr->header.ucode_array_offset_bytes));
			table_offset = le32_to_cpu(hdr->jt_offset);
			table_size = le32_to_cpu(hdr->jt_size);
		} else if (me == 2) {
			const struct gfx_firmware_header_v1_0 *hdr =
				(const struct gfx_firmware_header_v1_0 *)adev->gfx.me_fw->data;
			fw_data = (const __le32 *)
				(adev->gfx.me_fw->data +
				 le32_to_cpu(hdr->header.ucode_array_offset_bytes));
			table_offset = le32_to_cpu(hdr->jt_offset);
			table_size = le32_to_cpu(hdr->jt_size);
		} else if (me == 3) {
			const struct gfx_firmware_header_v1_0 *hdr =
				(const struct gfx_firmware_header_v1_0 *)adev->gfx.mec_fw->data;
			fw_data = (const __le32 *)
				(adev->gfx.mec_fw->data +
				 le32_to_cpu(hdr->header.ucode_array_offset_bytes));
			table_offset = le32_to_cpu(hdr->jt_offset);
			table_size = le32_to_cpu(hdr->jt_size);
		} else  if (me == 4) {
			const struct gfx_firmware_header_v1_0 *hdr =
				(const struct gfx_firmware_header_v1_0 *)adev->gfx.mec2_fw->data;
			fw_data = (const __le32 *)
				(adev->gfx.mec2_fw->data +
				 le32_to_cpu(hdr->header.ucode_array_offset_bytes));
			table_offset = le32_to_cpu(hdr->jt_offset);
			table_size = le32_to_cpu(hdr->jt_size);
		}

		for (i = 0; i < table_size; i ++) {
			dst_ptr[bo_offset + i] =
				cpu_to_le32(le32_to_cpu(fw_data[table_offset + i]));
		}

		bo_offset += table_size;
	}
}

/**
 * amdgpu_gfx_rlc_fini - Free BO which used for RLC
 *
 * @adev: amdgpu_device pointer
 *
 * Free three BO which is used for rlc_save_restore_block, rlc_clear_state_block
 * and rlc_jump_table_block.
 */
void amdgpu_gfx_rlc_fini(struct amdgpu_device *adev)
{
	/* save restore block */
	if (adev->gfx.rlc.save_restore_obj) {
		amdgpu_bo_free_kernel(&adev->gfx.rlc.save_restore_obj,
				      &adev->gfx.rlc.save_restore_gpu_addr,
				      (void **)&adev->gfx.rlc.sr_ptr);
	}

	/* clear state block */
	amdgpu_bo_free_kernel(&adev->gfx.rlc.clear_state_obj,
			      &adev->gfx.rlc.clear_state_gpu_addr,
			      (void **)&adev->gfx.rlc.cs_ptr);

	/* jump table block */
	amdgpu_bo_free_kernel(&adev->gfx.rlc.cp_table_obj,
			      &adev->gfx.rlc.cp_table_gpu_addr,
			      (void **)&adev->gfx.rlc.cp_table_ptr);
}

static int amdgpu_gfx_rlc_init_microcode_v2_0(struct amdgpu_device *adev)
{
	const struct common_firmware_header *common_hdr;
	const struct rlc_firmware_header_v2_0 *rlc_hdr;
	struct amdgpu_firmware_info *info;
	unsigned int *tmp;
	unsigned int i;

	rlc_hdr = (const struct rlc_firmware_header_v2_0 *)adev->gfx.rlc_fw->data;

	adev->gfx.rlc_fw_version = le32_to_cpu(rlc_hdr->header.ucode_version);
	adev->gfx.rlc_feature_version = le32_to_cpu(rlc_hdr->ucode_feature_version);
	adev->gfx.rlc.save_and_restore_offset =
		le32_to_cpu(rlc_hdr->save_and_restore_offset);
	adev->gfx.rlc.clear_state_descriptor_offset =
		le32_to_cpu(rlc_hdr->clear_state_descriptor_offset);
	adev->gfx.rlc.avail_scratch_ram_locations =
		le32_to_cpu(rlc_hdr->avail_scratch_ram_locations);
	adev->gfx.rlc.reg_restore_list_size =
		le32_to_cpu(rlc_hdr->reg_restore_list_size);
	adev->gfx.rlc.reg_list_format_start =
		le32_to_cpu(rlc_hdr->reg_list_format_start);
	adev->gfx.rlc.reg_list_format_separate_start =
		le32_to_cpu(rlc_hdr->reg_list_format_separate_start);
	adev->gfx.rlc.starting_offsets_start =
		le32_to_cpu(rlc_hdr->starting_offsets_start);
	adev->gfx.rlc.reg_list_format_size_bytes =
		le32_to_cpu(rlc_hdr->reg_list_format_size_bytes);
	adev->gfx.rlc.reg_list_size_bytes =
		le32_to_cpu(rlc_hdr->reg_list_size_bytes);
	adev->gfx.rlc.register_list_format =
		kmalloc(adev->gfx.rlc.reg_list_format_size_bytes +
			adev->gfx.rlc.reg_list_size_bytes, GFP_KERNEL);
	if (!adev->gfx.rlc.register_list_format) {
		dev_err(adev->dev, "failed to allocate memory for rlc register_list_format\n");
		return -ENOMEM;
	}

	tmp = (unsigned int *)((uintptr_t)rlc_hdr +
			le32_to_cpu(rlc_hdr->reg_list_format_array_offset_bytes));
	for (i = 0 ; i < (rlc_hdr->reg_list_format_size_bytes >> 2); i++)
		adev->gfx.rlc.register_list_format[i] = le32_to_cpu(tmp[i]);

	adev->gfx.rlc.register_restore = adev->gfx.rlc.register_list_format + i;

	tmp = (unsigned int *)((uintptr_t)rlc_hdr +
			le32_to_cpu(rlc_hdr->reg_list_array_offset_bytes));
	for (i = 0 ; i < (rlc_hdr->reg_list_size_bytes >> 2); i++)
		adev->gfx.rlc.register_restore[i] = le32_to_cpu(tmp[i]);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		info = &adev->firmware.ucode[AMDGPU_UCODE_ID_RLC_G];
		info->ucode_id = AMDGPU_UCODE_ID_RLC_G;
		info->fw = adev->gfx.rlc_fw;
		if (info->fw) {
			common_hdr = (const struct common_firmware_header *)info->fw->data;
			adev->firmware.fw_size +=
				ALIGN(le32_to_cpu(common_hdr->ucode_size_bytes), PAGE_SIZE);
		}
	}

	return 0;
}

static void amdgpu_gfx_rlc_init_microcode_v2_1(struct amdgpu_device *adev)
{
	const struct rlc_firmware_header_v2_1 *rlc_hdr;
	struct amdgpu_firmware_info *info;

	rlc_hdr = (const struct rlc_firmware_header_v2_1 *)adev->gfx.rlc_fw->data;
	adev->gfx.rlc_srlc_fw_version = le32_to_cpu(rlc_hdr->save_restore_list_cntl_ucode_ver);
	adev->gfx.rlc_srlc_feature_version = le32_to_cpu(rlc_hdr->save_restore_list_cntl_feature_ver);
	adev->gfx.rlc.save_restore_list_cntl_size_bytes = le32_to_cpu(rlc_hdr->save_restore_list_cntl_size_bytes);
	adev->gfx.rlc.save_restore_list_cntl = (u8 *)rlc_hdr + le32_to_cpu(rlc_hdr->save_restore_list_cntl_offset_bytes);
	adev->gfx.rlc_srlg_fw_version = le32_to_cpu(rlc_hdr->save_restore_list_gpm_ucode_ver);
	adev->gfx.rlc_srlg_feature_version = le32_to_cpu(rlc_hdr->save_restore_list_gpm_feature_ver);
	adev->gfx.rlc.save_restore_list_gpm_size_bytes = le32_to_cpu(rlc_hdr->save_restore_list_gpm_size_bytes);
	adev->gfx.rlc.save_restore_list_gpm = (u8 *)rlc_hdr + le32_to_cpu(rlc_hdr->save_restore_list_gpm_offset_bytes);
	adev->gfx.rlc_srls_fw_version = le32_to_cpu(rlc_hdr->save_restore_list_srm_ucode_ver);
	adev->gfx.rlc_srls_feature_version = le32_to_cpu(rlc_hdr->save_restore_list_srm_feature_ver);
	adev->gfx.rlc.save_restore_list_srm_size_bytes = le32_to_cpu(rlc_hdr->save_restore_list_srm_size_bytes);
	adev->gfx.rlc.save_restore_list_srm = (u8 *)rlc_hdr + le32_to_cpu(rlc_hdr->save_restore_list_srm_offset_bytes);
	adev->gfx.rlc.reg_list_format_direct_reg_list_length =
		le32_to_cpu(rlc_hdr->reg_list_format_direct_reg_list_length);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		if (adev->gfx.rlc.save_restore_list_cntl_size_bytes) {
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_RLC_RESTORE_LIST_CNTL];
			info->ucode_id = AMDGPU_UCODE_ID_RLC_RESTORE_LIST_CNTL;
			info->fw = adev->gfx.rlc_fw;
			adev->firmware.fw_size +=
				ALIGN(adev->gfx.rlc.save_restore_list_cntl_size_bytes, PAGE_SIZE);
		}

		if (adev->gfx.rlc.save_restore_list_gpm_size_bytes) {
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_RLC_RESTORE_LIST_GPM_MEM];
			info->ucode_id = AMDGPU_UCODE_ID_RLC_RESTORE_LIST_GPM_MEM;
			info->fw = adev->gfx.rlc_fw;
			adev->firmware.fw_size +=
				ALIGN(adev->gfx.rlc.save_restore_list_gpm_size_bytes, PAGE_SIZE);
		}

		if (adev->gfx.rlc.save_restore_list_srm_size_bytes) {
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_RLC_RESTORE_LIST_SRM_MEM];
			info->ucode_id = AMDGPU_UCODE_ID_RLC_RESTORE_LIST_SRM_MEM;
			info->fw = adev->gfx.rlc_fw;
			adev->firmware.fw_size +=
				ALIGN(adev->gfx.rlc.save_restore_list_srm_size_bytes, PAGE_SIZE);
		}
	}
}

static void amdgpu_gfx_rlc_init_microcode_v2_2(struct amdgpu_device *adev)
{
	const struct rlc_firmware_header_v2_2 *rlc_hdr;
	struct amdgpu_firmware_info *info;

	rlc_hdr = (const struct rlc_firmware_header_v2_2 *)adev->gfx.rlc_fw->data;
	adev->gfx.rlc.rlc_iram_ucode_size_bytes = le32_to_cpu(rlc_hdr->rlc_iram_ucode_size_bytes);
	adev->gfx.rlc.rlc_iram_ucode = (u8 *)rlc_hdr + le32_to_cpu(rlc_hdr->rlc_iram_ucode_offset_bytes);
	adev->gfx.rlc.rlc_dram_ucode_size_bytes = le32_to_cpu(rlc_hdr->rlc_dram_ucode_size_bytes);
	adev->gfx.rlc.rlc_dram_ucode = (u8 *)rlc_hdr + le32_to_cpu(rlc_hdr->rlc_dram_ucode_offset_bytes);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		if (adev->gfx.rlc.rlc_iram_ucode_size_bytes) {
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_RLC_IRAM];
			info->ucode_id = AMDGPU_UCODE_ID_RLC_IRAM;
			info->fw = adev->gfx.rlc_fw;
			adev->firmware.fw_size +=
				ALIGN(adev->gfx.rlc.rlc_iram_ucode_size_bytes, PAGE_SIZE);
		}

		if (adev->gfx.rlc.rlc_dram_ucode_size_bytes) {
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_RLC_DRAM];
			info->ucode_id = AMDGPU_UCODE_ID_RLC_DRAM;
			info->fw = adev->gfx.rlc_fw;
			adev->firmware.fw_size +=
				ALIGN(adev->gfx.rlc.rlc_dram_ucode_size_bytes, PAGE_SIZE);
		}
	}
}

static void amdgpu_gfx_rlc_init_microcode_v2_3(struct amdgpu_device *adev)
{
	const struct rlc_firmware_header_v2_3 *rlc_hdr;
	struct amdgpu_firmware_info *info;

	rlc_hdr = (const struct rlc_firmware_header_v2_3 *)adev->gfx.rlc_fw->data;
	adev->gfx.rlcp_ucode_version = le32_to_cpu(rlc_hdr->rlcp_ucode_version);
	adev->gfx.rlcp_ucode_feature_version = le32_to_cpu(rlc_hdr->rlcp_ucode_feature_version);
	adev->gfx.rlc.rlcp_ucode_size_bytes = le32_to_cpu(rlc_hdr->rlcp_ucode_size_bytes);
	adev->gfx.rlc.rlcp_ucode = (u8 *)rlc_hdr + le32_to_cpu(rlc_hdr->rlcp_ucode_offset_bytes);

	adev->gfx.rlcv_ucode_version = le32_to_cpu(rlc_hdr->rlcv_ucode_version);
	adev->gfx.rlcv_ucode_feature_version = le32_to_cpu(rlc_hdr->rlcv_ucode_feature_version);
	adev->gfx.rlc.rlcv_ucode_size_bytes = le32_to_cpu(rlc_hdr->rlcv_ucode_size_bytes);
	adev->gfx.rlc.rlcv_ucode = (u8 *)rlc_hdr + le32_to_cpu(rlc_hdr->rlcv_ucode_offset_bytes);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		if (adev->gfx.rlc.rlcp_ucode_size_bytes) {
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_RLC_P];
			info->ucode_id = AMDGPU_UCODE_ID_RLC_P;
			info->fw = adev->gfx.rlc_fw;
			adev->firmware.fw_size +=
				ALIGN(adev->gfx.rlc.rlcp_ucode_size_bytes, PAGE_SIZE);
		}

		if (adev->gfx.rlc.rlcv_ucode_size_bytes) {
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_RLC_V];
			info->ucode_id = AMDGPU_UCODE_ID_RLC_V;
			info->fw = adev->gfx.rlc_fw;
			adev->firmware.fw_size +=
				ALIGN(adev->gfx.rlc.rlcv_ucode_size_bytes, PAGE_SIZE);
		}
	}
}

static void amdgpu_gfx_rlc_init_microcode_v2_4(struct amdgpu_device *adev)
{
	const struct rlc_firmware_header_v2_4 *rlc_hdr;
	struct amdgpu_firmware_info *info;

	rlc_hdr = (const struct rlc_firmware_header_v2_4 *)adev->gfx.rlc_fw->data;
	adev->gfx.rlc.global_tap_delays_ucode_size_bytes = le32_to_cpu(rlc_hdr->global_tap_delays_ucode_size_bytes);
	adev->gfx.rlc.global_tap_delays_ucode = (u8 *)rlc_hdr + le32_to_cpu(rlc_hdr->global_tap_delays_ucode_offset_bytes);
	adev->gfx.rlc.se0_tap_delays_ucode_size_bytes = le32_to_cpu(rlc_hdr->se0_tap_delays_ucode_size_bytes);
	adev->gfx.rlc.se0_tap_delays_ucode = (u8 *)rlc_hdr + le32_to_cpu(rlc_hdr->se0_tap_delays_ucode_offset_bytes);
	adev->gfx.rlc.se1_tap_delays_ucode_size_bytes = le32_to_cpu(rlc_hdr->se1_tap_delays_ucode_size_bytes);
	adev->gfx.rlc.se1_tap_delays_ucode = (u8 *)rlc_hdr + le32_to_cpu(rlc_hdr->se1_tap_delays_ucode_offset_bytes);
	adev->gfx.rlc.se2_tap_delays_ucode_size_bytes = le32_to_cpu(rlc_hdr->se2_tap_delays_ucode_size_bytes);
	adev->gfx.rlc.se2_tap_delays_ucode = (u8 *)rlc_hdr + le32_to_cpu(rlc_hdr->se2_tap_delays_ucode_offset_bytes);
	adev->gfx.rlc.se3_tap_delays_ucode_size_bytes = le32_to_cpu(rlc_hdr->se3_tap_delays_ucode_size_bytes);
	adev->gfx.rlc.se3_tap_delays_ucode = (u8 *)rlc_hdr + le32_to_cpu(rlc_hdr->se3_tap_delays_ucode_offset_bytes);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		if (adev->gfx.rlc.global_tap_delays_ucode_size_bytes) {
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_GLOBAL_TAP_DELAYS];
			info->ucode_id = AMDGPU_UCODE_ID_GLOBAL_TAP_DELAYS;
			info->fw = adev->gfx.rlc_fw;
			adev->firmware.fw_size +=
				ALIGN(adev->gfx.rlc.global_tap_delays_ucode_size_bytes, PAGE_SIZE);
		}

		if (adev->gfx.rlc.se0_tap_delays_ucode_size_bytes) {
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_SE0_TAP_DELAYS];
			info->ucode_id = AMDGPU_UCODE_ID_SE0_TAP_DELAYS;
			info->fw = adev->gfx.rlc_fw;
			adev->firmware.fw_size +=
				ALIGN(adev->gfx.rlc.se0_tap_delays_ucode_size_bytes, PAGE_SIZE);
		}

		if (adev->gfx.rlc.se1_tap_delays_ucode_size_bytes) {
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_SE1_TAP_DELAYS];
			info->ucode_id = AMDGPU_UCODE_ID_SE1_TAP_DELAYS;
			info->fw = adev->gfx.rlc_fw;
			adev->firmware.fw_size +=
				ALIGN(adev->gfx.rlc.se1_tap_delays_ucode_size_bytes, PAGE_SIZE);
		}

		if (adev->gfx.rlc.se2_tap_delays_ucode_size_bytes) {
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_SE2_TAP_DELAYS];
			info->ucode_id = AMDGPU_UCODE_ID_SE2_TAP_DELAYS;
			info->fw = adev->gfx.rlc_fw;
			adev->firmware.fw_size +=
				ALIGN(adev->gfx.rlc.se2_tap_delays_ucode_size_bytes, PAGE_SIZE);
		}

		if (adev->gfx.rlc.se3_tap_delays_ucode_size_bytes) {
			info = &adev->firmware.ucode[AMDGPU_UCODE_ID_SE3_TAP_DELAYS];
			info->ucode_id = AMDGPU_UCODE_ID_SE3_TAP_DELAYS;
			info->fw = adev->gfx.rlc_fw;
			adev->firmware.fw_size +=
				ALIGN(adev->gfx.rlc.se3_tap_delays_ucode_size_bytes, PAGE_SIZE);
		}
	}
}

int amdgpu_gfx_rlc_init_microcode(struct amdgpu_device *adev,
				  uint16_t version_major,
				  uint16_t version_minor)
{
	int err;

	if (version_major < 2) {
		/* only support rlc_hdr v2.x and onwards */
		dev_err(adev->dev, "unsupported rlc fw hdr\n");
		return -EINVAL;
	}

	/* is_rlc_v2_1 is still used in APU code path */
	if (version_major == 2 && version_minor == 1)
		adev->gfx.rlc.is_rlc_v2_1 = true;

	if (version_minor >= 0) {
		err = amdgpu_gfx_rlc_init_microcode_v2_0(adev);
		if (err) {
			dev_err(adev->dev, "fail to init rlc v2_0 microcode\n");
			return err;
		}
	}
	if (version_minor >= 1)
		amdgpu_gfx_rlc_init_microcode_v2_1(adev);
	if (version_minor >= 2)
		amdgpu_gfx_rlc_init_microcode_v2_2(adev);
	if (version_minor == 3)
		amdgpu_gfx_rlc_init_microcode_v2_3(adev);
	if (version_minor == 4)
		amdgpu_gfx_rlc_init_microcode_v2_4(adev);

	return 0;
}
