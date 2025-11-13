/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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

#include <linux/module.h>

#ifdef CONFIG_X86
#include <asm/hypervisor.h>
#endif

#include <drm/drm_drv.h>
#include <xen/xen.h>

#include "amdgpu.h"
#include "amdgpu_ras.h"
#include "amdgpu_reset.h"
#include "amdgpu_dpm.h"
#include "vi.h"
#include "soc15.h"
#include "nv.h"

#define POPULATE_UCODE_INFO(vf2pf_info, ucode, ver) \
	do { \
		vf2pf_info->ucode_info[ucode].id = ucode; \
		vf2pf_info->ucode_info[ucode].version = ver; \
	} while (0)

bool amdgpu_virt_mmio_blocked(struct amdgpu_device *adev)
{
	/* By now all MMIO pages except mailbox are blocked */
	/* if blocking is enabled in hypervisor. Choose the */
	/* SCRATCH_REG0 to test. */
	return RREG32_NO_KIQ(0xc040) == 0xffffffff;
}

void amdgpu_virt_init_setting(struct amdgpu_device *adev)
{
	struct drm_device *ddev = adev_to_drm(adev);

	/* enable virtual display */
	if (adev->asic_type != CHIP_ALDEBARAN &&
	    adev->asic_type != CHIP_ARCTURUS &&
	    ((adev->pdev->class >> 8) != PCI_CLASS_ACCELERATOR_PROCESSING)) {
		if (adev->mode_info.num_crtc == 0)
			adev->mode_info.num_crtc = 1;
		adev->enable_virtual_display = true;
	}
	ddev->driver_features &= ~DRIVER_ATOMIC;
	adev->cg_flags = 0;
	adev->pg_flags = 0;

	/* Reduce kcq number to 2 to reduce latency */
	if (amdgpu_num_kcq == -1)
		amdgpu_num_kcq = 2;
}

/**
 * amdgpu_virt_request_full_gpu() - request full gpu access
 * @adev:	amdgpu device.
 * @init:	is driver init time.
 * When start to init/fini driver, first need to request full gpu access.
 * Return: Zero if request success, otherwise will return error.
 */
int amdgpu_virt_request_full_gpu(struct amdgpu_device *adev, bool init)
{
	struct amdgpu_virt *virt = &adev->virt;
	int r;

	if (virt->ops && virt->ops->req_full_gpu) {
		r = virt->ops->req_full_gpu(adev, init);
		if (r) {
			adev->no_hw_access = true;
			return r;
		}

		adev->virt.caps &= ~AMDGPU_SRIOV_CAPS_RUNTIME;
	}

	return 0;
}

/**
 * amdgpu_virt_release_full_gpu() - release full gpu access
 * @adev:	amdgpu device.
 * @init:	is driver init time.
 * When finishing driver init/fini, need to release full gpu access.
 * Return: Zero if release success, otherwise will returen error.
 */
int amdgpu_virt_release_full_gpu(struct amdgpu_device *adev, bool init)
{
	struct amdgpu_virt *virt = &adev->virt;
	int r;

	if (virt->ops && virt->ops->rel_full_gpu) {
		r = virt->ops->rel_full_gpu(adev, init);
		if (r)
			return r;

		adev->virt.caps |= AMDGPU_SRIOV_CAPS_RUNTIME;
	}
	return 0;
}

/**
 * amdgpu_virt_reset_gpu() - reset gpu
 * @adev:	amdgpu device.
 * Send reset command to GPU hypervisor to reset GPU that VM is using
 * Return: Zero if reset success, otherwise will return error.
 */
int amdgpu_virt_reset_gpu(struct amdgpu_device *adev)
{
	struct amdgpu_virt *virt = &adev->virt;
	int r;

	if (virt->ops && virt->ops->reset_gpu) {
		r = virt->ops->reset_gpu(adev);
		if (r)
			return r;

		adev->virt.caps &= ~AMDGPU_SRIOV_CAPS_RUNTIME;
	}

	return 0;
}

void amdgpu_virt_request_init_data(struct amdgpu_device *adev)
{
	struct amdgpu_virt *virt = &adev->virt;

	if (virt->ops && virt->ops->req_init_data)
		virt->ops->req_init_data(adev);

	if (adev->virt.req_init_data_ver > 0)
		DRM_INFO("host supports REQ_INIT_DATA handshake\n");
	else
		DRM_WARN("host doesn't support REQ_INIT_DATA handshake\n");
}

/**
 * amdgpu_virt_ready_to_reset() - send ready to reset to host
 * @adev:	amdgpu device.
 * Send ready to reset message to GPU hypervisor to signal we have stopped GPU
 * activity and is ready for host FLR
 */
void amdgpu_virt_ready_to_reset(struct amdgpu_device *adev)
{
	struct amdgpu_virt *virt = &adev->virt;

	if (virt->ops && virt->ops->reset_gpu)
		virt->ops->ready_to_reset(adev);
}

/**
 * amdgpu_virt_wait_reset() - wait for reset gpu completed
 * @adev:	amdgpu device.
 * Wait for GPU reset completed.
 * Return: Zero if reset success, otherwise will return error.
 */
int amdgpu_virt_wait_reset(struct amdgpu_device *adev)
{
	struct amdgpu_virt *virt = &adev->virt;

	if (!virt->ops || !virt->ops->wait_reset)
		return -EINVAL;

	return virt->ops->wait_reset(adev);
}

/**
 * amdgpu_virt_alloc_mm_table() - alloc memory for mm table
 * @adev:	amdgpu device.
 * MM table is used by UVD and VCE for its initialization
 * Return: Zero if allocate success.
 */
int amdgpu_virt_alloc_mm_table(struct amdgpu_device *adev)
{
	int r;

	if (!amdgpu_sriov_vf(adev) || adev->virt.mm_table.gpu_addr)
		return 0;

	r = amdgpu_bo_create_kernel(adev, PAGE_SIZE, PAGE_SIZE,
				    AMDGPU_GEM_DOMAIN_VRAM |
				    AMDGPU_GEM_DOMAIN_GTT,
				    &adev->virt.mm_table.bo,
				    &adev->virt.mm_table.gpu_addr,
				    (void *)&adev->virt.mm_table.cpu_addr);
	if (r) {
		DRM_ERROR("failed to alloc mm table and error = %d.\n", r);
		return r;
	}

	memset((void *)adev->virt.mm_table.cpu_addr, 0, PAGE_SIZE);
	DRM_INFO("MM table gpu addr = 0x%llx, cpu addr = %p.\n",
		 adev->virt.mm_table.gpu_addr,
		 adev->virt.mm_table.cpu_addr);
	return 0;
}

/**
 * amdgpu_virt_free_mm_table() - free mm table memory
 * @adev:	amdgpu device.
 * Free MM table memory
 */
void amdgpu_virt_free_mm_table(struct amdgpu_device *adev)
{
	if (!amdgpu_sriov_vf(adev) || !adev->virt.mm_table.gpu_addr)
		return;

	amdgpu_bo_free_kernel(&adev->virt.mm_table.bo,
			      &adev->virt.mm_table.gpu_addr,
			      (void *)&adev->virt.mm_table.cpu_addr);
	adev->virt.mm_table.gpu_addr = 0;
}

/**
 * amdgpu_virt_rcvd_ras_interrupt() - receive ras interrupt
 * @adev:	amdgpu device.
 * Check whether host sent RAS error message
 * Return: true if found, otherwise false
 */
bool amdgpu_virt_rcvd_ras_interrupt(struct amdgpu_device *adev)
{
	struct amdgpu_virt *virt = &adev->virt;

	if (!virt->ops || !virt->ops->rcvd_ras_intr)
		return false;

	return virt->ops->rcvd_ras_intr(adev);
}


unsigned int amd_sriov_msg_checksum(void *obj,
				unsigned long obj_size,
				unsigned int key,
				unsigned int checksum)
{
	unsigned int ret = key;
	unsigned long i = 0;
	unsigned char *pos;

	pos = (char *)obj;
	/* calculate checksum */
	for (i = 0; i < obj_size; ++i)
		ret += *(pos + i);
	/* minus the checksum itself */
	pos = (char *)&checksum;
	for (i = 0; i < sizeof(checksum); ++i)
		ret -= *(pos + i);
	return ret;
}

static int amdgpu_virt_init_ras_err_handler_data(struct amdgpu_device *adev)
{
	struct amdgpu_virt *virt = &adev->virt;
	struct amdgpu_virt_ras_err_handler_data **data = &virt->virt_eh_data;
	/* GPU will be marked bad on host if bp count more then 10,
	 * so alloc 512 is enough.
	 */
	unsigned int align_space = 512;
	void *bps = NULL;
	struct amdgpu_bo **bps_bo = NULL;

	*data = kmalloc(sizeof(struct amdgpu_virt_ras_err_handler_data), GFP_KERNEL);
	if (!*data)
		goto data_failure;

	bps = kmalloc_array(align_space, sizeof(*(*data)->bps), GFP_KERNEL);
	if (!bps)
		goto bps_failure;

	bps_bo = kmalloc_array(align_space, sizeof(*(*data)->bps_bo), GFP_KERNEL);
	if (!bps_bo)
		goto bps_bo_failure;

	(*data)->bps = bps;
	(*data)->bps_bo = bps_bo;
	(*data)->count = 0;
	(*data)->last_reserved = 0;

	virt->ras_init_done = true;

	return 0;

bps_bo_failure:
	kfree(bps);
bps_failure:
	kfree(*data);
data_failure:
	return -ENOMEM;
}

static void amdgpu_virt_ras_release_bp(struct amdgpu_device *adev)
{
	struct amdgpu_virt *virt = &adev->virt;
	struct amdgpu_virt_ras_err_handler_data *data = virt->virt_eh_data;
	struct amdgpu_bo *bo;
	int i;

	if (!data)
		return;

	for (i = data->last_reserved - 1; i >= 0; i--) {
		bo = data->bps_bo[i];
		if (bo) {
			amdgpu_bo_free_kernel(&bo, NULL, NULL);
			data->bps_bo[i] = bo;
		}
		data->last_reserved = i;
	}
}

void amdgpu_virt_release_ras_err_handler_data(struct amdgpu_device *adev)
{
	struct amdgpu_virt *virt = &adev->virt;
	struct amdgpu_virt_ras_err_handler_data *data = virt->virt_eh_data;

	virt->ras_init_done = false;

	if (!data)
		return;

	amdgpu_virt_ras_release_bp(adev);

	kfree(data->bps);
	kfree(data->bps_bo);
	kfree(data);
	virt->virt_eh_data = NULL;
}

static void amdgpu_virt_ras_add_bps(struct amdgpu_device *adev,
		struct eeprom_table_record *bps, int pages)
{
	struct amdgpu_virt *virt = &adev->virt;
	struct amdgpu_virt_ras_err_handler_data *data = virt->virt_eh_data;

	if (!data)
		return;

	memcpy(&data->bps[data->count], bps, pages * sizeof(*data->bps));
	data->count += pages;
}

static void amdgpu_virt_ras_reserve_bps(struct amdgpu_device *adev)
{
	struct amdgpu_virt *virt = &adev->virt;
	struct amdgpu_virt_ras_err_handler_data *data = virt->virt_eh_data;
	struct amdgpu_vram_mgr *mgr = &adev->mman.vram_mgr;
	struct ttm_resource_manager *man = &mgr->manager;
	struct amdgpu_bo *bo = NULL;
	uint64_t bp;
	int i;

	if (!data)
		return;

	for (i = data->last_reserved; i < data->count; i++) {
		bp = data->bps[i].retired_page;

		/* There are two cases of reserve error should be ignored:
		 * 1) a ras bad page has been allocated (used by someone);
		 * 2) a ras bad page has been reserved (duplicate error injection
		 *    for one page);
		 */
		if  (ttm_resource_manager_used(man)) {
			amdgpu_vram_mgr_reserve_range(&adev->mman.vram_mgr,
				bp << AMDGPU_GPU_PAGE_SHIFT,
				AMDGPU_GPU_PAGE_SIZE);
			data->bps_bo[i] = NULL;
		} else {
			if (amdgpu_bo_create_kernel_at(adev, bp << AMDGPU_GPU_PAGE_SHIFT,
							AMDGPU_GPU_PAGE_SIZE,
							&bo, NULL))
				DRM_DEBUG("RAS WARN: reserve vram for retired page %llx fail\n", bp);
			data->bps_bo[i] = bo;
		}
		data->last_reserved = i + 1;
		bo = NULL;
	}
}

static bool amdgpu_virt_ras_check_bad_page(struct amdgpu_device *adev,
		uint64_t retired_page)
{
	struct amdgpu_virt *virt = &adev->virt;
	struct amdgpu_virt_ras_err_handler_data *data = virt->virt_eh_data;
	int i;

	if (!data)
		return true;

	for (i = 0; i < data->count; i++)
		if (retired_page == data->bps[i].retired_page)
			return true;

	return false;
}

static void amdgpu_virt_add_bad_page(struct amdgpu_device *adev,
		uint64_t bp_block_offset, uint32_t bp_block_size)
{
	struct eeprom_table_record bp;
	uint64_t retired_page;
	uint32_t bp_idx, bp_cnt;
	void *vram_usage_va = NULL;

	if (adev->mman.fw_vram_usage_va)
		vram_usage_va = adev->mman.fw_vram_usage_va;
	else
		vram_usage_va = adev->mman.drv_vram_usage_va;

	memset(&bp, 0, sizeof(bp));

	if (bp_block_size) {
		bp_cnt = bp_block_size / sizeof(uint64_t);
		for (bp_idx = 0; bp_idx < bp_cnt; bp_idx++) {
			retired_page = *(uint64_t *)(vram_usage_va +
					bp_block_offset + bp_idx * sizeof(uint64_t));
			bp.retired_page = retired_page;

			if (amdgpu_virt_ras_check_bad_page(adev, retired_page))
				continue;

			amdgpu_virt_ras_add_bps(adev, &bp, 1);

			amdgpu_virt_ras_reserve_bps(adev);
		}
	}
}

static int amdgpu_virt_read_pf2vf_data(struct amdgpu_device *adev)
{
	struct amd_sriov_msg_pf2vf_info_header *pf2vf_info = adev->virt.fw_reserve.p_pf2vf;
	uint32_t checksum;
	uint32_t checkval;

	uint32_t i;
	uint32_t tmp;

	if (adev->virt.fw_reserve.p_pf2vf == NULL)
		return -EINVAL;

	if (pf2vf_info->size > 1024) {
		dev_err(adev->dev, "invalid pf2vf message size: 0x%x\n", pf2vf_info->size);
		return -EINVAL;
	}

	switch (pf2vf_info->version) {
	case 1:
		checksum = ((struct amdgim_pf2vf_info_v1 *)pf2vf_info)->checksum;
		checkval = amd_sriov_msg_checksum(
			adev->virt.fw_reserve.p_pf2vf, pf2vf_info->size,
			adev->virt.fw_reserve.checksum_key, checksum);
		if (checksum != checkval) {
			dev_err(adev->dev,
				"invalid pf2vf message: header checksum=0x%x calculated checksum=0x%x\n",
				checksum, checkval);
			return -EINVAL;
		}

		adev->virt.gim_feature =
			((struct amdgim_pf2vf_info_v1 *)pf2vf_info)->feature_flags;
		break;
	case 2:
		/* TODO: missing key, need to add it later */
		checksum = ((struct amd_sriov_msg_pf2vf_info *)pf2vf_info)->checksum;
		checkval = amd_sriov_msg_checksum(
			adev->virt.fw_reserve.p_pf2vf, pf2vf_info->size,
			0, checksum);
		if (checksum != checkval) {
			dev_err(adev->dev,
				"invalid pf2vf message: header checksum=0x%x calculated checksum=0x%x\n",
				checksum, checkval);
			return -EINVAL;
		}

		adev->virt.vf2pf_update_interval_ms =
			((struct amd_sriov_msg_pf2vf_info *)pf2vf_info)->vf2pf_update_interval_ms;
		adev->virt.gim_feature =
			((struct amd_sriov_msg_pf2vf_info *)pf2vf_info)->feature_flags.all;
		adev->virt.reg_access =
			((struct amd_sriov_msg_pf2vf_info *)pf2vf_info)->reg_access_flags.all;

		adev->virt.decode_max_dimension_pixels = 0;
		adev->virt.decode_max_frame_pixels = 0;
		adev->virt.encode_max_dimension_pixels = 0;
		adev->virt.encode_max_frame_pixels = 0;
		adev->virt.is_mm_bw_enabled = false;
		for (i = 0; i < AMD_SRIOV_MSG_RESERVE_VCN_INST; i++) {
			tmp = ((struct amd_sriov_msg_pf2vf_info *)pf2vf_info)->mm_bw_management[i].decode_max_dimension_pixels;
			adev->virt.decode_max_dimension_pixels = max(tmp, adev->virt.decode_max_dimension_pixels);

			tmp = ((struct amd_sriov_msg_pf2vf_info *)pf2vf_info)->mm_bw_management[i].decode_max_frame_pixels;
			adev->virt.decode_max_frame_pixels = max(tmp, adev->virt.decode_max_frame_pixels);

			tmp = ((struct amd_sriov_msg_pf2vf_info *)pf2vf_info)->mm_bw_management[i].encode_max_dimension_pixels;
			adev->virt.encode_max_dimension_pixels = max(tmp, adev->virt.encode_max_dimension_pixels);

			tmp = ((struct amd_sriov_msg_pf2vf_info *)pf2vf_info)->mm_bw_management[i].encode_max_frame_pixels;
			adev->virt.encode_max_frame_pixels = max(tmp, adev->virt.encode_max_frame_pixels);
		}
		if ((adev->virt.decode_max_dimension_pixels > 0) || (adev->virt.encode_max_dimension_pixels > 0))
			adev->virt.is_mm_bw_enabled = true;

		adev->unique_id =
			((struct amd_sriov_msg_pf2vf_info *)pf2vf_info)->uuid;
		adev->virt.ras_en_caps.all = ((struct amd_sriov_msg_pf2vf_info *)pf2vf_info)->ras_en_caps.all;
		adev->virt.ras_telemetry_en_caps.all =
			((struct amd_sriov_msg_pf2vf_info *)pf2vf_info)->ras_telemetry_en_caps.all;
		break;
	default:
		dev_err(adev->dev, "invalid pf2vf version: 0x%x\n", pf2vf_info->version);
		return -EINVAL;
	}

	/* correct too large or too little interval value */
	if (adev->virt.vf2pf_update_interval_ms < 200 || adev->virt.vf2pf_update_interval_ms > 10000)
		adev->virt.vf2pf_update_interval_ms = 2000;

	return 0;
}

static void amdgpu_virt_populate_vf2pf_ucode_info(struct amdgpu_device *adev)
{
	struct amd_sriov_msg_vf2pf_info *vf2pf_info;
	vf2pf_info = (struct amd_sriov_msg_vf2pf_info *) adev->virt.fw_reserve.p_vf2pf;

	if (adev->virt.fw_reserve.p_vf2pf == NULL)
		return;

	POPULATE_UCODE_INFO(vf2pf_info, AMD_SRIOV_UCODE_ID_VCE,      adev->vce.fw_version);
	POPULATE_UCODE_INFO(vf2pf_info, AMD_SRIOV_UCODE_ID_UVD,      adev->uvd.fw_version);
	POPULATE_UCODE_INFO(vf2pf_info, AMD_SRIOV_UCODE_ID_MC,       adev->gmc.fw_version);
	POPULATE_UCODE_INFO(vf2pf_info, AMD_SRIOV_UCODE_ID_ME,       adev->gfx.me_fw_version);
	POPULATE_UCODE_INFO(vf2pf_info, AMD_SRIOV_UCODE_ID_PFP,      adev->gfx.pfp_fw_version);
	POPULATE_UCODE_INFO(vf2pf_info, AMD_SRIOV_UCODE_ID_CE,       adev->gfx.ce_fw_version);
	POPULATE_UCODE_INFO(vf2pf_info, AMD_SRIOV_UCODE_ID_RLC,      adev->gfx.rlc_fw_version);
	POPULATE_UCODE_INFO(vf2pf_info, AMD_SRIOV_UCODE_ID_RLC_SRLC, adev->gfx.rlc_srlc_fw_version);
	POPULATE_UCODE_INFO(vf2pf_info, AMD_SRIOV_UCODE_ID_RLC_SRLG, adev->gfx.rlc_srlg_fw_version);
	POPULATE_UCODE_INFO(vf2pf_info, AMD_SRIOV_UCODE_ID_RLC_SRLS, adev->gfx.rlc_srls_fw_version);
	POPULATE_UCODE_INFO(vf2pf_info, AMD_SRIOV_UCODE_ID_MEC,      adev->gfx.mec_fw_version);
	POPULATE_UCODE_INFO(vf2pf_info, AMD_SRIOV_UCODE_ID_MEC2,     adev->gfx.mec2_fw_version);
	POPULATE_UCODE_INFO(vf2pf_info, AMD_SRIOV_UCODE_ID_SOS,      adev->psp.sos.fw_version);
	POPULATE_UCODE_INFO(vf2pf_info, AMD_SRIOV_UCODE_ID_ASD,
			    adev->psp.asd_context.bin_desc.fw_version);
	POPULATE_UCODE_INFO(vf2pf_info, AMD_SRIOV_UCODE_ID_TA_RAS,
			    adev->psp.ras_context.context.bin_desc.fw_version);
	POPULATE_UCODE_INFO(vf2pf_info, AMD_SRIOV_UCODE_ID_TA_XGMI,
			    adev->psp.xgmi_context.context.bin_desc.fw_version);
	POPULATE_UCODE_INFO(vf2pf_info, AMD_SRIOV_UCODE_ID_SMC,      adev->pm.fw_version);
	POPULATE_UCODE_INFO(vf2pf_info, AMD_SRIOV_UCODE_ID_SDMA,     adev->sdma.instance[0].fw_version);
	POPULATE_UCODE_INFO(vf2pf_info, AMD_SRIOV_UCODE_ID_SDMA2,    adev->sdma.instance[1].fw_version);
	POPULATE_UCODE_INFO(vf2pf_info, AMD_SRIOV_UCODE_ID_VCN,      adev->vcn.fw_version);
	POPULATE_UCODE_INFO(vf2pf_info, AMD_SRIOV_UCODE_ID_DMCU,     adev->dm.dmcu_fw_version);
}

static int amdgpu_virt_write_vf2pf_data(struct amdgpu_device *adev)
{
	struct amd_sriov_msg_vf2pf_info *vf2pf_info;

	vf2pf_info = (struct amd_sriov_msg_vf2pf_info *) adev->virt.fw_reserve.p_vf2pf;

	if (adev->virt.fw_reserve.p_vf2pf == NULL)
		return -EINVAL;

	memset(vf2pf_info, 0, sizeof(struct amd_sriov_msg_vf2pf_info));

	vf2pf_info->header.size = sizeof(struct amd_sriov_msg_vf2pf_info);
	vf2pf_info->header.version = AMD_SRIOV_MSG_FW_VRAM_VF2PF_VER;

#ifdef MODULE
	if (THIS_MODULE->version != NULL)
		strcpy(vf2pf_info->driver_version, THIS_MODULE->version);
	else
#endif
		strcpy(vf2pf_info->driver_version, "N/A");

	vf2pf_info->pf2vf_version_required = 0; // no requirement, guest understands all
	vf2pf_info->driver_cert = 0;
	vf2pf_info->os_info.all = 0;

	vf2pf_info->fb_usage = ttm_resource_manager_used(&adev->mman.vram_mgr.manager) ?
		 ttm_resource_manager_usage(&adev->mman.vram_mgr.manager) >> 20 : 0;
	vf2pf_info->fb_vis_usage =
		amdgpu_vram_mgr_vis_usage(&adev->mman.vram_mgr) >> 20;
	vf2pf_info->fb_size = adev->gmc.real_vram_size >> 20;
	vf2pf_info->fb_vis_size = adev->gmc.visible_vram_size >> 20;

	amdgpu_virt_populate_vf2pf_ucode_info(adev);

	/* TODO: read dynamic info */
	vf2pf_info->gfx_usage = 0;
	vf2pf_info->compute_usage = 0;
	vf2pf_info->encode_usage = 0;
	vf2pf_info->decode_usage = 0;

	vf2pf_info->dummy_page_addr = (uint64_t)adev->dummy_page_addr;
	if (amdgpu_sriov_is_mes_info_enable(adev)) {
		vf2pf_info->mes_info_addr =
			(uint64_t)(adev->mes.resource_1_gpu_addr[0] + AMDGPU_GPU_PAGE_SIZE);
		vf2pf_info->mes_info_size =
			adev->mes.resource_1[0]->tbo.base.size - AMDGPU_GPU_PAGE_SIZE;
	}
	vf2pf_info->checksum =
		amd_sriov_msg_checksum(
		vf2pf_info, sizeof(*vf2pf_info), 0, 0);

	return 0;
}

static void amdgpu_virt_update_vf2pf_work_item(struct work_struct *work)
{
	struct amdgpu_device *adev = container_of(work, struct amdgpu_device, virt.vf2pf_work.work);
	int ret;

	ret = amdgpu_virt_read_pf2vf_data(adev);
	if (ret) {
		adev->virt.vf2pf_update_retry_cnt++;

		if ((amdgpu_virt_rcvd_ras_interrupt(adev) ||
			adev->virt.vf2pf_update_retry_cnt >= AMDGPU_VF2PF_UPDATE_MAX_RETRY_LIMIT) &&
			amdgpu_sriov_runtime(adev)) {

			amdgpu_ras_set_fed(adev, true);
			if (amdgpu_reset_domain_schedule(adev->reset_domain,
							&adev->kfd.reset_work))
				return;
			else
				dev_err(adev->dev, "Failed to queue work! at %s", __func__);
		}

		goto out;
	}

	adev->virt.vf2pf_update_retry_cnt = 0;
	amdgpu_virt_write_vf2pf_data(adev);

out:
	schedule_delayed_work(&(adev->virt.vf2pf_work), adev->virt.vf2pf_update_interval_ms);
}

void amdgpu_virt_fini_data_exchange(struct amdgpu_device *adev)
{
	if (adev->virt.vf2pf_update_interval_ms != 0) {
		DRM_INFO("clean up the vf2pf work item\n");
		cancel_delayed_work_sync(&adev->virt.vf2pf_work);
		adev->virt.vf2pf_update_interval_ms = 0;
	}
}

void amdgpu_virt_init_data_exchange(struct amdgpu_device *adev)
{
	adev->virt.fw_reserve.p_pf2vf = NULL;
	adev->virt.fw_reserve.p_vf2pf = NULL;
	adev->virt.vf2pf_update_interval_ms = 0;
	adev->virt.vf2pf_update_retry_cnt = 0;

	if (adev->mman.fw_vram_usage_va && adev->mman.drv_vram_usage_va) {
		DRM_WARN("Currently fw_vram and drv_vram should not have values at the same time!");
	} else if (adev->mman.fw_vram_usage_va || adev->mman.drv_vram_usage_va) {
		/* go through this logic in ip_init and reset to init workqueue*/
		amdgpu_virt_exchange_data(adev);

		INIT_DELAYED_WORK(&adev->virt.vf2pf_work, amdgpu_virt_update_vf2pf_work_item);
		schedule_delayed_work(&(adev->virt.vf2pf_work), msecs_to_jiffies(adev->virt.vf2pf_update_interval_ms));
	} else if (adev->bios != NULL) {
		/* got through this logic in early init stage to get necessary flags, e.g. rlcg_acc related*/
		adev->virt.fw_reserve.p_pf2vf =
			(struct amd_sriov_msg_pf2vf_info_header *)
			(adev->bios + (AMD_SRIOV_MSG_PF2VF_OFFSET_KB << 10));

		amdgpu_virt_read_pf2vf_data(adev);
	}
}


void amdgpu_virt_exchange_data(struct amdgpu_device *adev)
{
	uint64_t bp_block_offset = 0;
	uint32_t bp_block_size = 0;
	struct amd_sriov_msg_pf2vf_info *pf2vf_v2 = NULL;

	if (adev->mman.fw_vram_usage_va || adev->mman.drv_vram_usage_va) {
		if (adev->mman.fw_vram_usage_va) {
			adev->virt.fw_reserve.p_pf2vf =
				(struct amd_sriov_msg_pf2vf_info_header *)
				(adev->mman.fw_vram_usage_va + (AMD_SRIOV_MSG_PF2VF_OFFSET_KB << 10));
			adev->virt.fw_reserve.p_vf2pf =
				(struct amd_sriov_msg_vf2pf_info_header *)
				(adev->mman.fw_vram_usage_va + (AMD_SRIOV_MSG_VF2PF_OFFSET_KB << 10));
			adev->virt.fw_reserve.ras_telemetry =
				(adev->mman.fw_vram_usage_va + (AMD_SRIOV_MSG_RAS_TELEMETRY_OFFSET_KB << 10));
		} else if (adev->mman.drv_vram_usage_va) {
			adev->virt.fw_reserve.p_pf2vf =
				(struct amd_sriov_msg_pf2vf_info_header *)
				(adev->mman.drv_vram_usage_va + (AMD_SRIOV_MSG_PF2VF_OFFSET_KB << 10));
			adev->virt.fw_reserve.p_vf2pf =
				(struct amd_sriov_msg_vf2pf_info_header *)
				(adev->mman.drv_vram_usage_va + (AMD_SRIOV_MSG_VF2PF_OFFSET_KB << 10));
			adev->virt.fw_reserve.ras_telemetry =
				(adev->mman.drv_vram_usage_va + (AMD_SRIOV_MSG_RAS_TELEMETRY_OFFSET_KB << 10));
		}

		amdgpu_virt_read_pf2vf_data(adev);
		amdgpu_virt_write_vf2pf_data(adev);

		/* bad page handling for version 2 */
		if (adev->virt.fw_reserve.p_pf2vf->version == 2) {
			pf2vf_v2 = (struct amd_sriov_msg_pf2vf_info *)adev->virt.fw_reserve.p_pf2vf;

			bp_block_offset = ((uint64_t)pf2vf_v2->bp_block_offset_low & 0xFFFFFFFF) |
				((((uint64_t)pf2vf_v2->bp_block_offset_high) << 32) & 0xFFFFFFFF00000000);
			bp_block_size = pf2vf_v2->bp_block_size;

			if (bp_block_size && !adev->virt.ras_init_done)
				amdgpu_virt_init_ras_err_handler_data(adev);

			if (adev->virt.ras_init_done)
				amdgpu_virt_add_bad_page(adev, bp_block_offset, bp_block_size);
		}
	}
}

static u32 amdgpu_virt_init_detect_asic(struct amdgpu_device *adev)
{
	uint32_t reg;

	switch (adev->asic_type) {
	case CHIP_TONGA:
	case CHIP_FIJI:
		reg = RREG32(mmBIF_IOV_FUNC_IDENTIFIER);
		break;
	case CHIP_VEGA10:
	case CHIP_VEGA20:
	case CHIP_NAVI10:
	case CHIP_NAVI12:
	case CHIP_SIENNA_CICHLID:
	case CHIP_ARCTURUS:
	case CHIP_ALDEBARAN:
	case CHIP_IP_DISCOVERY:
		reg = RREG32(mmRCC_IOV_FUNC_IDENTIFIER);
		break;
	default: /* other chip doesn't support SRIOV */
		reg = 0;
		break;
	}

	if (reg & 1)
		adev->virt.caps |= AMDGPU_SRIOV_CAPS_IS_VF;

	if (reg & 0x80000000)
		adev->virt.caps |= AMDGPU_SRIOV_CAPS_ENABLE_IOV;

	if (!reg) {
		/* passthrough mode exclus sriov mod */
		if (is_virtual_machine() && !xen_initial_domain())
			adev->virt.caps |= AMDGPU_PASSTHROUGH_MODE;
	}

	return reg;
}

static bool amdgpu_virt_init_req_data(struct amdgpu_device *adev, u32 reg)
{
	bool is_sriov = false;

	/* we have the ability to check now */
	if (amdgpu_sriov_vf(adev)) {
		is_sriov = true;

		switch (adev->asic_type) {
		case CHIP_TONGA:
		case CHIP_FIJI:
			vi_set_virt_ops(adev);
			break;
		case CHIP_VEGA10:
			soc15_set_virt_ops(adev);
#ifdef CONFIG_X86
			/* not send GPU_INIT_DATA with MS_HYPERV*/
			if (!hypervisor_is_type(X86_HYPER_MS_HYPERV))
#endif
				/* send a dummy GPU_INIT_DATA request to host on vega10 */
				amdgpu_virt_request_init_data(adev);
			break;
		case CHIP_VEGA20:
		case CHIP_ARCTURUS:
		case CHIP_ALDEBARAN:
			soc15_set_virt_ops(adev);
			break;
		case CHIP_NAVI10:
		case CHIP_NAVI12:
		case CHIP_SIENNA_CICHLID:
		case CHIP_IP_DISCOVERY:
			nv_set_virt_ops(adev);
			/* try send GPU_INIT_DATA request to host */
			amdgpu_virt_request_init_data(adev);
			break;
		default: /* other chip doesn't support SRIOV */
			is_sriov = false;
			DRM_ERROR("Unknown asic type: %d!\n", adev->asic_type);
			break;
		}
	}

	return is_sriov;
}

static void amdgpu_virt_init_ras(struct amdgpu_device *adev)
{
	ratelimit_state_init(&adev->virt.ras.ras_error_cnt_rs, 5 * HZ, 1);
	ratelimit_state_init(&adev->virt.ras.ras_cper_dump_rs, 5 * HZ, 1);
	ratelimit_state_init(&adev->virt.ras.ras_chk_criti_rs, 5 * HZ, 1);

	ratelimit_set_flags(&adev->virt.ras.ras_error_cnt_rs,
			    RATELIMIT_MSG_ON_RELEASE);
	ratelimit_set_flags(&adev->virt.ras.ras_cper_dump_rs,
			    RATELIMIT_MSG_ON_RELEASE);
	ratelimit_set_flags(&adev->virt.ras.ras_chk_criti_rs,
			    RATELIMIT_MSG_ON_RELEASE);

	mutex_init(&adev->virt.ras.ras_telemetry_mutex);

	adev->virt.ras.cper_rptr = 0;
}

void amdgpu_virt_init(struct amdgpu_device *adev)
{
	bool is_sriov = false;
	uint32_t reg = amdgpu_virt_init_detect_asic(adev);

	is_sriov = amdgpu_virt_init_req_data(adev, reg);

	if (is_sriov)
		amdgpu_virt_init_ras(adev);
}

static bool amdgpu_virt_access_debugfs_is_mmio(struct amdgpu_device *adev)
{
	return amdgpu_sriov_is_debug(adev) ? true : false;
}

static bool amdgpu_virt_access_debugfs_is_kiq(struct amdgpu_device *adev)
{
	return amdgpu_sriov_is_normal(adev) ? true : false;
}

int amdgpu_virt_enable_access_debugfs(struct amdgpu_device *adev)
{
	if (!amdgpu_sriov_vf(adev) ||
	    amdgpu_virt_access_debugfs_is_kiq(adev))
		return 0;

	if (amdgpu_virt_access_debugfs_is_mmio(adev))
		adev->virt.caps &= ~AMDGPU_SRIOV_CAPS_RUNTIME;
	else
		return -EPERM;

	return 0;
}

void amdgpu_virt_disable_access_debugfs(struct amdgpu_device *adev)
{
	if (amdgpu_sriov_vf(adev))
		adev->virt.caps |= AMDGPU_SRIOV_CAPS_RUNTIME;
}

enum amdgpu_sriov_vf_mode amdgpu_virt_get_sriov_vf_mode(struct amdgpu_device *adev)
{
	enum amdgpu_sriov_vf_mode mode;

	if (amdgpu_sriov_vf(adev)) {
		if (amdgpu_sriov_is_pp_one_vf(adev))
			mode = SRIOV_VF_MODE_ONE_VF;
		else
			mode = SRIOV_VF_MODE_MULTI_VF;
	} else {
		mode = SRIOV_VF_MODE_BARE_METAL;
	}

	return mode;
}

void amdgpu_virt_pre_reset(struct amdgpu_device *adev)
{
	/* stop the data exchange thread */
	amdgpu_virt_fini_data_exchange(adev);
	amdgpu_dpm_set_mp1_state(adev, PP_MP1_STATE_FLR);
}

void amdgpu_virt_post_reset(struct amdgpu_device *adev)
{
	if (amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(11, 0, 3)) {
		/* force set to GFXOFF state after reset,
		 * to avoid some invalid operation before GC enable
		 */
		adev->gfx.is_poweron = false;
	}

	adev->mes.ring[0].sched.ready = false;
}

bool amdgpu_virt_fw_load_skip_check(struct amdgpu_device *adev, uint32_t ucode_id)
{
	switch (amdgpu_ip_version(adev, MP0_HWIP, 0)) {
	case IP_VERSION(13, 0, 0):
		/* no vf autoload, white list */
		if (ucode_id == AMDGPU_UCODE_ID_VCN1 ||
		    ucode_id == AMDGPU_UCODE_ID_VCN)
			return false;
		else
			return true;
	case IP_VERSION(11, 0, 9):
	case IP_VERSION(11, 0, 7):
		/* black list for CHIP_NAVI12 and CHIP_SIENNA_CICHLID */
		if (ucode_id == AMDGPU_UCODE_ID_RLC_G
		    || ucode_id == AMDGPU_UCODE_ID_RLC_RESTORE_LIST_CNTL
		    || ucode_id == AMDGPU_UCODE_ID_RLC_RESTORE_LIST_GPM_MEM
		    || ucode_id == AMDGPU_UCODE_ID_RLC_RESTORE_LIST_SRM_MEM
		    || ucode_id == AMDGPU_UCODE_ID_SMC)
			return true;
		else
			return false;
	case IP_VERSION(13, 0, 10):
		/* white list */
		if (ucode_id == AMDGPU_UCODE_ID_CAP
		|| ucode_id == AMDGPU_UCODE_ID_CP_RS64_PFP
		|| ucode_id == AMDGPU_UCODE_ID_CP_RS64_ME
		|| ucode_id == AMDGPU_UCODE_ID_CP_RS64_MEC
		|| ucode_id == AMDGPU_UCODE_ID_CP_RS64_PFP_P0_STACK
		|| ucode_id == AMDGPU_UCODE_ID_CP_RS64_PFP_P1_STACK
		|| ucode_id == AMDGPU_UCODE_ID_CP_RS64_ME_P0_STACK
		|| ucode_id == AMDGPU_UCODE_ID_CP_RS64_ME_P1_STACK
		|| ucode_id == AMDGPU_UCODE_ID_CP_RS64_MEC_P0_STACK
		|| ucode_id == AMDGPU_UCODE_ID_CP_RS64_MEC_P1_STACK
		|| ucode_id == AMDGPU_UCODE_ID_CP_RS64_MEC_P2_STACK
		|| ucode_id == AMDGPU_UCODE_ID_CP_RS64_MEC_P3_STACK
		|| ucode_id == AMDGPU_UCODE_ID_CP_MES
		|| ucode_id == AMDGPU_UCODE_ID_CP_MES_DATA
		|| ucode_id == AMDGPU_UCODE_ID_CP_MES1
		|| ucode_id == AMDGPU_UCODE_ID_CP_MES1_DATA
		|| ucode_id == AMDGPU_UCODE_ID_VCN1
		|| ucode_id == AMDGPU_UCODE_ID_VCN)
			return false;
		else
			return true;
	default:
		/* lagacy black list */
		if (ucode_id == AMDGPU_UCODE_ID_SDMA0
		    || ucode_id == AMDGPU_UCODE_ID_SDMA1
		    || ucode_id == AMDGPU_UCODE_ID_SDMA2
		    || ucode_id == AMDGPU_UCODE_ID_SDMA3
		    || ucode_id == AMDGPU_UCODE_ID_SDMA4
		    || ucode_id == AMDGPU_UCODE_ID_SDMA5
		    || ucode_id == AMDGPU_UCODE_ID_SDMA6
		    || ucode_id == AMDGPU_UCODE_ID_SDMA7
		    || ucode_id == AMDGPU_UCODE_ID_RLC_G
		    || ucode_id == AMDGPU_UCODE_ID_RLC_RESTORE_LIST_CNTL
		    || ucode_id == AMDGPU_UCODE_ID_RLC_RESTORE_LIST_GPM_MEM
		    || ucode_id == AMDGPU_UCODE_ID_RLC_RESTORE_LIST_SRM_MEM
		    || ucode_id == AMDGPU_UCODE_ID_SMC)
			return true;
		else
			return false;
	}
}

void amdgpu_virt_update_sriov_video_codec(struct amdgpu_device *adev,
			struct amdgpu_video_codec_info *encode, uint32_t encode_array_size,
			struct amdgpu_video_codec_info *decode, uint32_t decode_array_size)
{
	uint32_t i;

	if (!adev->virt.is_mm_bw_enabled)
		return;

	if (encode) {
		for (i = 0; i < encode_array_size; i++) {
			encode[i].max_width = adev->virt.encode_max_dimension_pixels;
			encode[i].max_pixels_per_frame = adev->virt.encode_max_frame_pixels;
			if (encode[i].max_width > 0)
				encode[i].max_height = encode[i].max_pixels_per_frame / encode[i].max_width;
			else
				encode[i].max_height = 0;
		}
	}

	if (decode) {
		for (i = 0; i < decode_array_size; i++) {
			decode[i].max_width = adev->virt.decode_max_dimension_pixels;
			decode[i].max_pixels_per_frame = adev->virt.decode_max_frame_pixels;
			if (decode[i].max_width > 0)
				decode[i].max_height = decode[i].max_pixels_per_frame / decode[i].max_width;
			else
				decode[i].max_height = 0;
		}
	}
}

bool amdgpu_virt_get_rlcg_reg_access_flag(struct amdgpu_device *adev,
						 u32 acc_flags, u32 hwip,
						 bool write, u32 *rlcg_flag)
{
	bool ret = false;

	switch (hwip) {
	case GC_HWIP:
		if (amdgpu_sriov_reg_indirect_gc(adev)) {
			*rlcg_flag =
				write ? AMDGPU_RLCG_GC_WRITE : AMDGPU_RLCG_GC_READ;
			ret = true;
		/* only in new version, AMDGPU_REGS_NO_KIQ and
		 * AMDGPU_REGS_RLC are enabled simultaneously */
		} else if ((acc_flags & AMDGPU_REGS_RLC) &&
				!(acc_flags & AMDGPU_REGS_NO_KIQ) && write) {
			*rlcg_flag = AMDGPU_RLCG_GC_WRITE_LEGACY;
			ret = true;
		}
		break;
	case MMHUB_HWIP:
		if (amdgpu_sriov_reg_indirect_mmhub(adev) &&
		    (acc_flags & AMDGPU_REGS_RLC) && write) {
			*rlcg_flag = AMDGPU_RLCG_MMHUB_WRITE;
			ret = true;
		}
		break;
	default:
		break;
	}
	return ret;
}

u32 amdgpu_virt_rlcg_reg_rw(struct amdgpu_device *adev, u32 offset, u32 v, u32 flag, u32 xcc_id)
{
	struct amdgpu_rlcg_reg_access_ctrl *reg_access_ctrl;
	uint32_t timeout = 50000;
	uint32_t i, tmp;
	uint32_t ret = 0;
	void *scratch_reg0;
	void *scratch_reg1;
	void *scratch_reg2;
	void *scratch_reg3;
	void *spare_int;
	unsigned long flags;

	if (!adev->gfx.rlc.rlcg_reg_access_supported) {
		dev_err(adev->dev,
			"indirect registers access through rlcg is not available\n");
		return 0;
	}

	if (adev->gfx.xcc_mask && (((1 << xcc_id) & adev->gfx.xcc_mask) == 0)) {
		dev_err(adev->dev, "invalid xcc\n");
		return 0;
	}

	if (amdgpu_device_skip_hw_access(adev))
		return 0;

	reg_access_ctrl = &adev->gfx.rlc.reg_access_ctrl[xcc_id];
	scratch_reg0 = (void __iomem *)adev->rmmio + 4 * reg_access_ctrl->scratch_reg0;
	scratch_reg1 = (void __iomem *)adev->rmmio + 4 * reg_access_ctrl->scratch_reg1;
	scratch_reg2 = (void __iomem *)adev->rmmio + 4 * reg_access_ctrl->scratch_reg2;
	scratch_reg3 = (void __iomem *)adev->rmmio + 4 * reg_access_ctrl->scratch_reg3;

	spin_lock_irqsave(&adev->virt.rlcg_reg_lock, flags);

	if (reg_access_ctrl->spare_int)
		spare_int = (void __iomem *)adev->rmmio + 4 * reg_access_ctrl->spare_int;

	if (offset == reg_access_ctrl->grbm_cntl) {
		/* if the target reg offset is grbm_cntl, write to scratch_reg2 */
		writel(v, scratch_reg2);
		if (flag == AMDGPU_RLCG_GC_WRITE_LEGACY)
			writel(v, ((void __iomem *)adev->rmmio) + (offset * 4));
	} else if (offset == reg_access_ctrl->grbm_idx) {
		/* if the target reg offset is grbm_idx, write to scratch_reg3 */
		writel(v, scratch_reg3);
		if (flag == AMDGPU_RLCG_GC_WRITE_LEGACY)
			writel(v, ((void __iomem *)adev->rmmio) + (offset * 4));
	} else {
		/*
		 * SCRATCH_REG0 	= read/write value
		 * SCRATCH_REG1[30:28]	= command
		 * SCRATCH_REG1[19:0]	= address in dword
		 * SCRATCH_REG1[27:24]	= Error reporting
		 */
		writel(v, scratch_reg0);
		writel((offset | flag), scratch_reg1);
		if (reg_access_ctrl->spare_int)
			writel(1, spare_int);

		for (i = 0; i < timeout; i++) {
			tmp = readl(scratch_reg1);
			if (!(tmp & AMDGPU_RLCG_SCRATCH1_ADDRESS_MASK))
				break;
			udelay(10);
		}

		tmp = readl(scratch_reg1);
		if (i >= timeout || (tmp & AMDGPU_RLCG_SCRATCH1_ERROR_MASK) != 0) {
			if (amdgpu_sriov_rlcg_error_report_enabled(adev)) {
				if (tmp & AMDGPU_RLCG_VFGATE_DISABLED) {
					dev_err(adev->dev,
						"vfgate is disabled, rlcg failed to program reg: 0x%05x\n", offset);
				} else if (tmp & AMDGPU_RLCG_WRONG_OPERATION_TYPE) {
					dev_err(adev->dev,
						"wrong operation type, rlcg failed to program reg: 0x%05x\n", offset);
				} else if (tmp & AMDGPU_RLCG_REG_NOT_IN_RANGE) {
					dev_err(adev->dev,
						"register is not in range, rlcg failed to program reg: 0x%05x\n", offset);
				} else {
					dev_err(adev->dev,
						"unknown error type, rlcg failed to program reg: 0x%05x\n", offset);
				}
			} else {
				dev_err(adev->dev,
					"timeout: rlcg faled to program reg: 0x%05x\n", offset);
			}
		}
	}

	ret = readl(scratch_reg0);

	spin_unlock_irqrestore(&adev->virt.rlcg_reg_lock, flags);

	return ret;
}

void amdgpu_sriov_wreg(struct amdgpu_device *adev,
		       u32 offset, u32 value,
		       u32 acc_flags, u32 hwip, u32 xcc_id)
{
	u32 rlcg_flag;

	if (amdgpu_device_skip_hw_access(adev))
		return;

	if (!amdgpu_sriov_runtime(adev) &&
		amdgpu_virt_get_rlcg_reg_access_flag(adev, acc_flags, hwip, true, &rlcg_flag)) {
		amdgpu_virt_rlcg_reg_rw(adev, offset, value, rlcg_flag, xcc_id);
		return;
	}

	if (acc_flags & AMDGPU_REGS_NO_KIQ)
		WREG32_NO_KIQ(offset, value);
	else
		WREG32(offset, value);
}

u32 amdgpu_sriov_rreg(struct amdgpu_device *adev,
		      u32 offset, u32 acc_flags, u32 hwip, u32 xcc_id)
{
	u32 rlcg_flag;

	if (amdgpu_device_skip_hw_access(adev))
		return 0;

	if (!amdgpu_sriov_runtime(adev) &&
		amdgpu_virt_get_rlcg_reg_access_flag(adev, acc_flags, hwip, false, &rlcg_flag))
		return amdgpu_virt_rlcg_reg_rw(adev, offset, 0, rlcg_flag, xcc_id);

	if (acc_flags & AMDGPU_REGS_NO_KIQ)
		return RREG32_NO_KIQ(offset);
	else
		return RREG32(offset);
}

bool amdgpu_sriov_xnack_support(struct amdgpu_device *adev)
{
	bool xnack_mode = true;

	if (amdgpu_sriov_vf(adev) &&
	    amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(9, 4, 2))
		xnack_mode = false;

	return xnack_mode;
}

bool amdgpu_virt_get_ras_capability(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);

	if (!amdgpu_sriov_ras_caps_en(adev))
		return false;

	if (adev->virt.ras_en_caps.bits.block_umc)
		adev->ras_hw_enabled |= BIT(AMDGPU_RAS_BLOCK__UMC);
	if (adev->virt.ras_en_caps.bits.block_sdma)
		adev->ras_hw_enabled |= BIT(AMDGPU_RAS_BLOCK__SDMA);
	if (adev->virt.ras_en_caps.bits.block_gfx)
		adev->ras_hw_enabled |= BIT(AMDGPU_RAS_BLOCK__GFX);
	if (adev->virt.ras_en_caps.bits.block_mmhub)
		adev->ras_hw_enabled |= BIT(AMDGPU_RAS_BLOCK__MMHUB);
	if (adev->virt.ras_en_caps.bits.block_athub)
		adev->ras_hw_enabled |= BIT(AMDGPU_RAS_BLOCK__ATHUB);
	if (adev->virt.ras_en_caps.bits.block_pcie_bif)
		adev->ras_hw_enabled |= BIT(AMDGPU_RAS_BLOCK__PCIE_BIF);
	if (adev->virt.ras_en_caps.bits.block_hdp)
		adev->ras_hw_enabled |= BIT(AMDGPU_RAS_BLOCK__HDP);
	if (adev->virt.ras_en_caps.bits.block_xgmi_wafl)
		adev->ras_hw_enabled |= BIT(AMDGPU_RAS_BLOCK__XGMI_WAFL);
	if (adev->virt.ras_en_caps.bits.block_df)
		adev->ras_hw_enabled |= BIT(AMDGPU_RAS_BLOCK__DF);
	if (adev->virt.ras_en_caps.bits.block_smn)
		adev->ras_hw_enabled |= BIT(AMDGPU_RAS_BLOCK__SMN);
	if (adev->virt.ras_en_caps.bits.block_sem)
		adev->ras_hw_enabled |= BIT(AMDGPU_RAS_BLOCK__SEM);
	if (adev->virt.ras_en_caps.bits.block_mp0)
		adev->ras_hw_enabled |= BIT(AMDGPU_RAS_BLOCK__MP0);
	if (adev->virt.ras_en_caps.bits.block_mp1)
		adev->ras_hw_enabled |= BIT(AMDGPU_RAS_BLOCK__MP1);
	if (adev->virt.ras_en_caps.bits.block_fuse)
		adev->ras_hw_enabled |= BIT(AMDGPU_RAS_BLOCK__FUSE);
	if (adev->virt.ras_en_caps.bits.block_mca)
		adev->ras_hw_enabled |= BIT(AMDGPU_RAS_BLOCK__MCA);
	if (adev->virt.ras_en_caps.bits.block_vcn)
		adev->ras_hw_enabled |= BIT(AMDGPU_RAS_BLOCK__VCN);
	if (adev->virt.ras_en_caps.bits.block_jpeg)
		adev->ras_hw_enabled |= BIT(AMDGPU_RAS_BLOCK__JPEG);
	if (adev->virt.ras_en_caps.bits.block_ih)
		adev->ras_hw_enabled |= BIT(AMDGPU_RAS_BLOCK__IH);
	if (adev->virt.ras_en_caps.bits.block_mpio)
		adev->ras_hw_enabled |= BIT(AMDGPU_RAS_BLOCK__MPIO);

	if (adev->virt.ras_en_caps.bits.poison_propogation_mode)
		con->poison_supported = true; /* Poison is handled by host */

	return true;
}

static inline enum amd_sriov_ras_telemetry_gpu_block
amdgpu_ras_block_to_sriov(struct amdgpu_device *adev, enum amdgpu_ras_block block) {
	switch (block) {
	case AMDGPU_RAS_BLOCK__UMC:
		return RAS_TELEMETRY_GPU_BLOCK_UMC;
	case AMDGPU_RAS_BLOCK__SDMA:
		return RAS_TELEMETRY_GPU_BLOCK_SDMA;
	case AMDGPU_RAS_BLOCK__GFX:
		return RAS_TELEMETRY_GPU_BLOCK_GFX;
	case AMDGPU_RAS_BLOCK__MMHUB:
		return RAS_TELEMETRY_GPU_BLOCK_MMHUB;
	case AMDGPU_RAS_BLOCK__ATHUB:
		return RAS_TELEMETRY_GPU_BLOCK_ATHUB;
	case AMDGPU_RAS_BLOCK__PCIE_BIF:
		return RAS_TELEMETRY_GPU_BLOCK_PCIE_BIF;
	case AMDGPU_RAS_BLOCK__HDP:
		return RAS_TELEMETRY_GPU_BLOCK_HDP;
	case AMDGPU_RAS_BLOCK__XGMI_WAFL:
		return RAS_TELEMETRY_GPU_BLOCK_XGMI_WAFL;
	case AMDGPU_RAS_BLOCK__DF:
		return RAS_TELEMETRY_GPU_BLOCK_DF;
	case AMDGPU_RAS_BLOCK__SMN:
		return RAS_TELEMETRY_GPU_BLOCK_SMN;
	case AMDGPU_RAS_BLOCK__SEM:
		return RAS_TELEMETRY_GPU_BLOCK_SEM;
	case AMDGPU_RAS_BLOCK__MP0:
		return RAS_TELEMETRY_GPU_BLOCK_MP0;
	case AMDGPU_RAS_BLOCK__MP1:
		return RAS_TELEMETRY_GPU_BLOCK_MP1;
	case AMDGPU_RAS_BLOCK__FUSE:
		return RAS_TELEMETRY_GPU_BLOCK_FUSE;
	case AMDGPU_RAS_BLOCK__MCA:
		return RAS_TELEMETRY_GPU_BLOCK_MCA;
	case AMDGPU_RAS_BLOCK__VCN:
		return RAS_TELEMETRY_GPU_BLOCK_VCN;
	case AMDGPU_RAS_BLOCK__JPEG:
		return RAS_TELEMETRY_GPU_BLOCK_JPEG;
	case AMDGPU_RAS_BLOCK__IH:
		return RAS_TELEMETRY_GPU_BLOCK_IH;
	case AMDGPU_RAS_BLOCK__MPIO:
		return RAS_TELEMETRY_GPU_BLOCK_MPIO;
	default:
		DRM_WARN_ONCE("Unsupported SRIOV RAS telemetry block 0x%x\n",
			      block);
		return RAS_TELEMETRY_GPU_BLOCK_COUNT;
	}
}

static int amdgpu_virt_cache_host_error_counts(struct amdgpu_device *adev,
					       struct amdsriov_ras_telemetry *host_telemetry)
{
	struct amd_sriov_ras_telemetry_error_count *tmp = NULL;
	uint32_t checksum, used_size;

	checksum = host_telemetry->header.checksum;
	used_size = host_telemetry->header.used_size;

	if (used_size > (AMD_SRIOV_RAS_TELEMETRY_SIZE_KB << 10))
		return 0;

	tmp = kmemdup(&host_telemetry->body.error_count, used_size, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	if (checksum != amd_sriov_msg_checksum(tmp, used_size, 0, 0))
		goto out;

	memcpy(&adev->virt.count_cache, tmp,
	       min(used_size, sizeof(adev->virt.count_cache)));
out:
	kfree(tmp);

	return 0;
}

static int amdgpu_virt_req_ras_err_count_internal(struct amdgpu_device *adev, bool force_update)
{
	struct amdgpu_virt *virt = &adev->virt;

	if (!virt->ops || !virt->ops->req_ras_err_count)
		return -EOPNOTSUPP;

	/* Host allows 15 ras telemetry requests per 60 seconds. Afterwhich, the Host
	 * will ignore incoming guest messages. Ratelimit the guest messages to
	 * prevent guest self DOS.
	 */
	if (__ratelimit(&virt->ras.ras_error_cnt_rs) || force_update) {
		mutex_lock(&virt->ras.ras_telemetry_mutex);
		if (!virt->ops->req_ras_err_count(adev))
			amdgpu_virt_cache_host_error_counts(adev,
				virt->fw_reserve.ras_telemetry);
		mutex_unlock(&virt->ras.ras_telemetry_mutex);
	}

	return 0;
}

/* Bypass ACA interface and query ECC counts directly from host */
int amdgpu_virt_req_ras_err_count(struct amdgpu_device *adev, enum amdgpu_ras_block block,
				  struct ras_err_data *err_data)
{
	enum amd_sriov_ras_telemetry_gpu_block sriov_block;

	sriov_block = amdgpu_ras_block_to_sriov(adev, block);

	if (sriov_block >= RAS_TELEMETRY_GPU_BLOCK_COUNT ||
	    !amdgpu_sriov_ras_telemetry_block_en(adev, sriov_block))
		return -EOPNOTSUPP;

	/* Host Access may be lost during reset, just return last cached data. */
	if (down_read_trylock(&adev->reset_domain->sem)) {
		amdgpu_virt_req_ras_err_count_internal(adev, false);
		up_read(&adev->reset_domain->sem);
	}

	err_data->ue_count = adev->virt.count_cache.block[sriov_block].ue_count;
	err_data->ce_count = adev->virt.count_cache.block[sriov_block].ce_count;
	err_data->de_count = adev->virt.count_cache.block[sriov_block].de_count;

	return 0;
}

static int
amdgpu_virt_write_cpers_to_ring(struct amdgpu_device *adev,
				struct amdsriov_ras_telemetry *host_telemetry,
				u32 *more)
{
	struct amd_sriov_ras_cper_dump *cper_dump = NULL;
	struct cper_hdr *entry = NULL;
	struct amdgpu_ring *ring = &adev->cper.ring_buf;
	uint32_t checksum, used_size, i;
	int ret = 0;

	checksum = host_telemetry->header.checksum;
	used_size = host_telemetry->header.used_size;

	if (used_size > (AMD_SRIOV_RAS_TELEMETRY_SIZE_KB << 10))
		return -EINVAL;

	cper_dump = kmemdup(&host_telemetry->body.cper_dump, used_size, GFP_KERNEL);
	if (!cper_dump)
		return -ENOMEM;

	if (checksum != amd_sriov_msg_checksum(cper_dump, used_size, 0, 0)) {
		ret = -EINVAL;
		goto out;
	}

	*more = cper_dump->more;

	if (cper_dump->wptr < adev->virt.ras.cper_rptr) {
		dev_warn(
			adev->dev,
			"guest specified rptr that was too high! guest rptr: 0x%llx, host rptr: 0x%llx\n",
			adev->virt.ras.cper_rptr, cper_dump->wptr);

		adev->virt.ras.cper_rptr = cper_dump->wptr;
		goto out;
	}

	entry = (struct cper_hdr *)&cper_dump->buf[0];

	for (i = 0; i < cper_dump->count; i++) {
		amdgpu_cper_ring_write(ring, entry, entry->record_length);
		entry = (struct cper_hdr *)((char *)entry +
					    entry->record_length);
	}

	if (cper_dump->overflow_count)
		dev_warn(adev->dev,
			 "host reported CPER overflow of 0x%llx entries!\n",
			 cper_dump->overflow_count);

	adev->virt.ras.cper_rptr = cper_dump->wptr;
out:
	kfree(cper_dump);

	return ret;
}

static int amdgpu_virt_req_ras_cper_dump_internal(struct amdgpu_device *adev)
{
	struct amdgpu_virt *virt = &adev->virt;
	int ret = 0;
	uint32_t more = 0;

	if (!virt->ops || !virt->ops->req_ras_cper_dump)
		return -EOPNOTSUPP;

	do {
		if (!virt->ops->req_ras_cper_dump(adev, virt->ras.cper_rptr))
			ret = amdgpu_virt_write_cpers_to_ring(
				adev, virt->fw_reserve.ras_telemetry, &more);
		else
			ret = 0;
	} while (more && !ret);

	return ret;
}

int amdgpu_virt_req_ras_cper_dump(struct amdgpu_device *adev, bool force_update)
{
	struct amdgpu_virt *virt = &adev->virt;
	int ret = 0;

	if (!amdgpu_sriov_ras_cper_en(adev))
		return -EOPNOTSUPP;

	if ((__ratelimit(&virt->ras.ras_cper_dump_rs) || force_update) &&
	    down_read_trylock(&adev->reset_domain->sem)) {
		mutex_lock(&virt->ras.ras_telemetry_mutex);
		ret = amdgpu_virt_req_ras_cper_dump_internal(adev);
		mutex_unlock(&virt->ras.ras_telemetry_mutex);
		up_read(&adev->reset_domain->sem);
	}

	return ret;
}

int amdgpu_virt_ras_telemetry_post_reset(struct amdgpu_device *adev)
{
	unsigned long ue_count, ce_count;

	if (amdgpu_sriov_ras_telemetry_en(adev)) {
		amdgpu_virt_req_ras_err_count_internal(adev, true);
		amdgpu_ras_query_error_count(adev, &ce_count, &ue_count, NULL);
	}

	return 0;
}

bool amdgpu_virt_ras_telemetry_block_en(struct amdgpu_device *adev,
					enum amdgpu_ras_block block)
{
	enum amd_sriov_ras_telemetry_gpu_block sriov_block;

	sriov_block = amdgpu_ras_block_to_sriov(adev, block);

	if (sriov_block >= RAS_TELEMETRY_GPU_BLOCK_COUNT ||
	    !amdgpu_sriov_ras_telemetry_block_en(adev, sriov_block))
		return false;

	return true;
}

/*
 * amdgpu_virt_request_bad_pages() - request bad pages
 * @adev: amdgpu device.
 * Send command to GPU hypervisor to write new bad pages into the shared PF2VF region
 */
void amdgpu_virt_request_bad_pages(struct amdgpu_device *adev)
{
	struct amdgpu_virt *virt = &adev->virt;

	if (virt->ops && virt->ops->req_bad_pages)
		virt->ops->req_bad_pages(adev);
}

static int amdgpu_virt_cache_chk_criti_hit(struct amdgpu_device *adev,
					   struct amdsriov_ras_telemetry *host_telemetry,
					   bool *hit)
{
	struct amd_sriov_ras_chk_criti *tmp = NULL;
	uint32_t checksum, used_size;

	checksum = host_telemetry->header.checksum;
	used_size = host_telemetry->header.used_size;

	if (used_size > (AMD_SRIOV_RAS_TELEMETRY_SIZE_KB << 10))
		return 0;

	tmp = kmemdup(&host_telemetry->body.chk_criti, used_size, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	if (checksum != amd_sriov_msg_checksum(tmp, used_size, 0, 0))
		goto out;

	if (hit)
		*hit = tmp->hit ? true : false;

out:
	kfree(tmp);

	return 0;
}

int amdgpu_virt_check_vf_critical_region(struct amdgpu_device *adev, u64 addr, bool *hit)
{
	struct amdgpu_virt *virt = &adev->virt;
	int r = -EPERM;

	if (!virt->ops || !virt->ops->req_ras_chk_criti)
		return -EOPNOTSUPP;

	/* Host allows 15 ras telemetry requests per 60 seconds. Afterwhich, the Host
	 * will ignore incoming guest messages. Ratelimit the guest messages to
	 * prevent guest self DOS.
	 */
	if (__ratelimit(&virt->ras.ras_chk_criti_rs)) {
		mutex_lock(&virt->ras.ras_telemetry_mutex);
		if (!virt->ops->req_ras_chk_criti(adev, addr))
			r = amdgpu_virt_cache_chk_criti_hit(
				adev, virt->fw_reserve.ras_telemetry, hit);
		mutex_unlock(&virt->ras.ras_telemetry_mutex);
	}

	return r;
}
