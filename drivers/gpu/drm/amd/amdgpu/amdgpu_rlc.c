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
 *
 * Set RLC enter into safe mode if RLC is enabled and haven't in safe mode.
 */
void amdgpu_gfx_rlc_enter_safe_mode(struct amdgpu_device *adev)
{
	if (adev->gfx.rlc.in_safe_mode)
		return;

	/* if RLC is not enabled, do nothing */
	if (!adev->gfx.rlc.funcs->is_rlc_enabled(adev))
		return;

	if (adev->cg_flags &
	    (AMD_CG_SUPPORT_GFX_CGCG | AMD_CG_SUPPORT_GFX_MGCG |
	     AMD_CG_SUPPORT_GFX_3D_CGCG)) {
		adev->gfx.rlc.funcs->set_safe_mode(adev);
		adev->gfx.rlc.in_safe_mode = true;
	}
}

/**
 * amdgpu_gfx_rlc_exit_safe_mode - Set RLC out of safe mode
 *
 * @adev: amdgpu_device pointer
 *
 * Set RLC exit safe mode if RLC is enabled and have entered into safe mode.
 */
void amdgpu_gfx_rlc_exit_safe_mode(struct amdgpu_device *adev)
{
	if (!(adev->gfx.rlc.in_safe_mode))
		return;

	/* if RLC is not enabled, do nothing */
	if (!adev->gfx.rlc.funcs->is_rlc_enabled(adev))
		return;

	if (adev->cg_flags &
	    (AMD_CG_SUPPORT_GFX_CGCG | AMD_CG_SUPPORT_GFX_MGCG |
	     AMD_CG_SUPPORT_GFX_3D_CGCG)) {
		adev->gfx.rlc.funcs->unset_safe_mode(adev);
		adev->gfx.rlc.in_safe_mode = false;
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
				      AMDGPU_GEM_DOMAIN_VRAM,
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
				      AMDGPU_GEM_DOMAIN_VRAM,
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
				      PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM,
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
