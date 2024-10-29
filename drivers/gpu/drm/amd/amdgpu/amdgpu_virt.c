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
	    adev->asic_type != CHIP_ARCTURUS) {
		if (adev->mode_info.num_crtc == 0)
			adev->mode_info.num_crtc = 1;
		adev->enable_virtual_display = true;
	}
	ddev->driver_features &= ~DRIVER_ATOMIC;
	adev->cg_flags = 0;
	adev->pg_flags = 0;
}

void amdgpu_virt_kiq_reg_write_reg_wait(struct amdgpu_device *adev,
					uint32_t reg0, uint32_t reg1,
					uint32_t ref, uint32_t mask)
{
	struct amdgpu_kiq *kiq = &adev->gfx.kiq;
	struct amdgpu_ring *ring = &kiq->ring;
	signed long r, cnt = 0;
	unsigned long flags;
	uint32_t seq;

	if (adev->mes.ring.sched.ready) {
		amdgpu_mes_reg_write_reg_wait(adev, reg0, reg1,
					      ref, mask);
		return;
	}

	spin_lock_irqsave(&kiq->ring_lock, flags);
	amdgpu_ring_alloc(ring, 32);
	amdgpu_ring_emit_reg_write_reg_wait(ring, reg0, reg1,
					    ref, mask);
	r = amdgpu_fence_emit_polling(ring, &seq, MAX_KIQ_REG_WAIT);
	if (r)
		goto failed_undo;

	amdgpu_ring_commit(ring);
	spin_unlock_irqrestore(&kiq->ring_lock, flags);

	r = amdgpu_fence_wait_polling(ring, seq, MAX_KIQ_REG_WAIT);

	/* don't wait anymore for IRQ context */
	if (r < 1 && in_interrupt())
		goto failed_kiq;

	might_sleep();
	while (r < 1 && cnt++ < MAX_KIQ_REG_TRY) {

		msleep(MAX_KIQ_REG_BAILOUT_INTERVAL);
		r = amdgpu_fence_wait_polling(ring, seq, MAX_KIQ_REG_WAIT);
	}

	if (cnt > MAX_KIQ_REG_TRY)
		goto failed_kiq;

	return;

failed_undo:
	amdgpu_ring_undo(ring);
	spin_unlock_irqrestore(&kiq->ring_lock, flags);
failed_kiq:
	dev_err(adev->dev, "failed to write reg %x wait reg %x\n", reg0, reg1);
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
				    AMDGPU_GEM_DOMAIN_VRAM,
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

	bps = kmalloc_array(align_space, sizeof((*data)->bps), GFP_KERNEL);
	if (!bps)
		goto bps_failure;

	bps_bo = kmalloc_array(align_space, sizeof((*data)->bps_bo), GFP_KERNEL);
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
		amdgpu_bo_free_kernel(&bo, NULL, NULL);
		data->bps_bo[i] = bo;
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
		if (amdgpu_bo_create_kernel_at(adev, bp << AMDGPU_GPU_PAGE_SHIFT,
					       AMDGPU_GPU_PAGE_SIZE,
					       &bo, NULL))
			DRM_DEBUG("RAS WARN: reserve vram for retired page %llx fail\n", bp);

		data->bps_bo[i] = bo;
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

	if (bp_block_size) {
		bp_cnt = bp_block_size / sizeof(uint64_t);
		for (bp_idx = 0; bp_idx < bp_cnt; bp_idx++) {
			retired_page = *(uint64_t *)(adev->mman.fw_vram_usage_va +
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
		DRM_ERROR("invalid pf2vf message size\n");
		return -EINVAL;
	}

	switch (pf2vf_info->version) {
	case 1:
		checksum = ((struct amdgim_pf2vf_info_v1 *)pf2vf_info)->checksum;
		checkval = amd_sriov_msg_checksum(
			adev->virt.fw_reserve.p_pf2vf, pf2vf_info->size,
			adev->virt.fw_reserve.checksum_key, checksum);
		if (checksum != checkval) {
			DRM_ERROR("invalid pf2vf message\n");
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
			DRM_ERROR("invalid pf2vf message\n");
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
		if((adev->virt.decode_max_dimension_pixels > 0) || (adev->virt.encode_max_dimension_pixels > 0))
			adev->virt.is_mm_bw_enabled = true;

		adev->unique_id =
			((struct amd_sriov_msg_pf2vf_info *)pf2vf_info)->uuid;
		break;
	default:
		DRM_ERROR("invalid pf2vf version\n");
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
	POPULATE_UCODE_INFO(vf2pf_info, AMD_SRIOV_UCODE_ID_IMU,      adev->gfx.imu_fw_version);
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

	vf2pf_info->fb_usage =
		ttm_resource_manager_usage(&adev->mman.vram_mgr.manager) >> 20;
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
	if (ret)
		goto out;
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

	if (adev->mman.fw_vram_usage_va != NULL) {
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

	if (adev->mman.fw_vram_usage_va != NULL) {

		adev->virt.fw_reserve.p_pf2vf =
			(struct amd_sriov_msg_pf2vf_info_header *)
			(adev->mman.fw_vram_usage_va + (AMD_SRIOV_MSG_PF2VF_OFFSET_KB << 10));
		adev->virt.fw_reserve.p_vf2pf =
			(struct amd_sriov_msg_vf2pf_info_header *)
			(adev->mman.fw_vram_usage_va + (AMD_SRIOV_MSG_VF2PF_OFFSET_KB << 10));

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

void amdgpu_detect_virtualization(struct amdgpu_device *adev)
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

	if (amdgpu_sriov_vf(adev) && adev->asic_type == CHIP_SIENNA_CICHLID)
		/* VF MMIO access (except mailbox range) from CPU
		 * will be blocked during sriov runtime
		 */
		adev->virt.caps |= AMDGPU_VF_MMIO_ACCESS_PROTECT;

	/* we have the ability to check now */
	if (amdgpu_sriov_vf(adev)) {
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
			DRM_ERROR("Unknown asic type: %d!\n", adev->asic_type);
			break;
		}
	}
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

bool amdgpu_virt_fw_load_skip_check(struct amdgpu_device *adev, uint32_t ucode_id)
{
	switch (adev->ip_versions[MP0_HWIP][0]) {
	case IP_VERSION(13, 0, 0):
		/* no vf autoload, white list */
		if (ucode_id == AMDGPU_UCODE_ID_VCN1 ||
		    ucode_id == AMDGPU_UCODE_ID_VCN)
			return false;
		else
			return true;
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

static bool amdgpu_virt_get_rlcg_reg_access_flag(struct amdgpu_device *adev,
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

static u32 amdgpu_virt_rlcg_reg_rw(struct amdgpu_device *adev, u32 offset, u32 v, u32 flag)
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

	if (!adev->gfx.rlc.rlcg_reg_access_supported) {
		dev_err(adev->dev,
			"indirect registers access through rlcg is not available\n");
		return 0;
	}

	reg_access_ctrl = &adev->gfx.rlc.reg_access_ctrl;
	scratch_reg0 = (void __iomem *)adev->rmmio + 4 * reg_access_ctrl->scratch_reg0;
	scratch_reg1 = (void __iomem *)adev->rmmio + 4 * reg_access_ctrl->scratch_reg1;
	scratch_reg2 = (void __iomem *)adev->rmmio + 4 * reg_access_ctrl->scratch_reg2;
	scratch_reg3 = (void __iomem *)adev->rmmio + 4 * reg_access_ctrl->scratch_reg3;

	mutex_lock(&adev->virt.rlcg_reg_lock);

	if (reg_access_ctrl->spare_int)
		spare_int = (void __iomem *)adev->rmmio + 4 * reg_access_ctrl->spare_int;

	if (offset == reg_access_ctrl->grbm_cntl) {
		/* if the target reg offset is grbm_cntl, write to scratch_reg2 */
		writel(v, scratch_reg2);
		writel(v, ((void __iomem *)adev->rmmio) + (offset * 4));
	} else if (offset == reg_access_ctrl->grbm_idx) {
		/* if the target reg offset is grbm_idx, write to scratch_reg3 */
		writel(v, scratch_reg3);
		writel(v, ((void __iomem *)adev->rmmio) + (offset * 4));
	} else {
		/*
		 * SCRATCH_REG0 	= read/write value
		 * SCRATCH_REG1[30:28]	= command
		 * SCRATCH_REG1[19:0]	= address in dword
		 * SCRATCH_REG1[26:24]	= Error reporting
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

		if (i >= timeout) {
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

	mutex_unlock(&adev->virt.rlcg_reg_lock);

	return ret;
}

void amdgpu_sriov_wreg(struct amdgpu_device *adev,
		       u32 offset, u32 value,
		       u32 acc_flags, u32 hwip)
{
	u32 rlcg_flag;

	if (!amdgpu_sriov_runtime(adev) &&
		amdgpu_virt_get_rlcg_reg_access_flag(adev, acc_flags, hwip, true, &rlcg_flag)) {
		amdgpu_virt_rlcg_reg_rw(adev, offset, value, rlcg_flag);
		return;
	}

	if (acc_flags & AMDGPU_REGS_NO_KIQ)
		WREG32_NO_KIQ(offset, value);
	else
		WREG32(offset, value);
}

u32 amdgpu_sriov_rreg(struct amdgpu_device *adev,
		      u32 offset, u32 acc_flags, u32 hwip)
{
	u32 rlcg_flag;

	if (!amdgpu_sriov_runtime(adev) &&
		amdgpu_virt_get_rlcg_reg_access_flag(adev, acc_flags, hwip, false, &rlcg_flag))
		return amdgpu_virt_rlcg_reg_rw(adev, offset, 0, rlcg_flag);

	if (acc_flags & AMDGPU_REGS_NO_KIQ)
		return RREG32_NO_KIQ(offset);
	else
		return RREG32(offset);
}
