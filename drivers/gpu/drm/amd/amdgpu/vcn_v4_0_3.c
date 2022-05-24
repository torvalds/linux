/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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
#include <drm/drm_drv.h>

#include "amdgpu.h"
#include "amdgpu_vcn.h"
#include "amdgpu_pm.h"
#include "soc15.h"
#include "soc15d.h"
#include "soc15_hw_ip.h"
#include "vcn_v2_0.h"
#include "vcn_sw_ring.h"

#include "vcn/vcn_4_0_3_offset.h"
#include "vcn/vcn_4_0_3_sh_mask.h"
#include "ivsrcid/vcn/irqsrcs_vcn_2_0.h"

#define mmUVD_DPG_LMA_CTL		regUVD_DPG_LMA_CTL
#define mmUVD_DPG_LMA_CTL_BASE_IDX	regUVD_DPG_LMA_CTL_BASE_IDX
#define mmUVD_DPG_LMA_DATA		regUVD_DPG_LMA_DATA
#define mmUVD_DPG_LMA_DATA_BASE_IDX	regUVD_DPG_LMA_DATA_BASE_IDX

#define VCN_VID_SOC_ADDRESS_2_0		0x1fb00
#define VCN1_VID_SOC_ADDRESS_3_0	0x48300

static void vcn_v4_0_3_set_dec_ring_funcs(struct amdgpu_device *adev);
static void vcn_v4_0_3_set_irq_funcs(struct amdgpu_device *adev);
static int vcn_v4_0_3_set_powergating_state(void *handle,
		enum amd_powergating_state state);
static int vcn_v4_0_3_pause_dpg_mode(struct amdgpu_device *adev,
		int inst_idx, struct dpg_pause_state *new_state);

/**
 * vcn_v4_0_3_early_init - set function pointers
 *
 * @handle: amdgpu_device pointer
 *
 * Set ring and irq function pointers
 */
static int vcn_v4_0_3_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	vcn_v4_0_3_set_dec_ring_funcs(adev);
	vcn_v4_0_3_set_irq_funcs(adev);

	return 0;
}

/**
 * vcn_v4_0_3_sw_init - sw init for VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Load firmware and sw initialization
 */
static int vcn_v4_0_3_sw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	volatile struct amdgpu_fw_shared *fw_shared;
	struct amdgpu_ring *ring;
	int r;

	r = amdgpu_vcn_sw_init(adev);
	if (r)
		return r;

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		const struct common_firmware_header *hdr;
		hdr = (const struct common_firmware_header *)adev->vcn.fw->data;
		adev->firmware.ucode[AMDGPU_UCODE_ID_VCN].ucode_id = AMDGPU_UCODE_ID_VCN;
		adev->firmware.ucode[AMDGPU_UCODE_ID_VCN].fw = adev->vcn.fw;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(hdr->ucode_size_bytes), PAGE_SIZE);

		DRM_DEV_INFO(adev->dev, "Will use PSP to load VCN firmware\n");
	}

	r = amdgpu_vcn_resume(adev);
	if (r)
		return r;

	/* VCN DEC TRAP */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_VCN,
		VCN_2_0__SRCID__UVD_SYSTEM_MESSAGE_INTERRUPT, &adev->vcn.inst->irq);
	if (r)
		return r;

	ring = &adev->vcn.inst->ring_dec;
	ring->use_doorbell = false;
	ring->vm_hub = AMDGPU_MMHUB0(0);
	sprintf(ring->name, "vcn_dec");
	r = amdgpu_ring_init(adev, ring, 512, &adev->vcn.inst->irq, 0,
			     AMDGPU_RING_PRIO_DEFAULT,
			     &adev->vcn.inst->sched_score);
	if (r)
		return r;

	fw_shared = adev->vcn.inst->fw_shared.cpu_addr;
	fw_shared->present_flag_0 |= cpu_to_le32(AMDGPU_VCN_SW_RING_FLAG) |
				     cpu_to_le32(AMDGPU_VCN_MULTI_QUEUE_FLAG) |
				     cpu_to_le32(AMDGPU_VCN_FW_SHARED_FLAG_0_RB);
	fw_shared->sw_ring.is_enabled = cpu_to_le32(true);

	if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG)
		adev->vcn.pause_dpg_mode = vcn_v4_0_3_pause_dpg_mode;

	return 0;
}

/**
 * vcn_v4_0_3_sw_fini - sw fini for VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * VCN suspend and free up sw allocation
 */
static int vcn_v4_0_3_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r, idx;

	if (drm_dev_enter(&adev->ddev, &idx)) {
		volatile struct amdgpu_fw_shared *fw_shared;

		fw_shared = adev->vcn.inst->fw_shared.cpu_addr;
		fw_shared->present_flag_0 = 0;
		fw_shared->sw_ring.is_enabled = cpu_to_le32(false);

		drm_dev_exit(idx);
	}

	r = amdgpu_vcn_suspend(adev);
	if (r)
		return r;

	r = amdgpu_vcn_sw_fini(adev);

	return r;
}

/**
 * vcn_v4_0_3_hw_init - start and test VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Initialize the hardware, boot up the VCPU and do some testing
 */
static int vcn_v4_0_3_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct amdgpu_ring *ring = &adev->vcn.inst->ring_dec;
	int r;

	r = amdgpu_ring_test_helper(ring);

	if (!r)
		DRM_DEV_INFO(adev->dev, "VCN decode initialized successfully(under %s).\n",
			(adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG)?"DPG Mode":"SPG Mode");

	return r;
}

/**
 * vcn_v4_0_3_hw_fini - stop the hardware block
 *
 * @handle: amdgpu_device pointer
 *
 * Stop the VCN block, mark ring as not ready any more
 */
static int vcn_v4_0_3_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	cancel_delayed_work_sync(&adev->vcn.idle_work);

	if (adev->vcn.cur_state != AMD_PG_STATE_GATE)
		vcn_v4_0_3_set_powergating_state(adev, AMD_PG_STATE_GATE);

	return 0;
}

/**
 * vcn_v4_0_3_suspend - suspend VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * HW fini and suspend VCN block
 */
static int vcn_v4_0_3_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	r = vcn_v4_0_3_hw_fini(adev);
	if (r)
		return r;

	r = amdgpu_vcn_suspend(adev);

	return r;
}

/**
 * vcn_v4_0_3_resume - resume VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Resume firmware and hw init VCN block
 */
static int vcn_v4_0_3_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	r = amdgpu_vcn_resume(adev);
	if (r)
		return r;

	r = vcn_v4_0_3_hw_init(adev);

	return r;
}

/**
 * vcn_v4_0_3_mc_resume - memory controller programming
 *
 * @adev: amdgpu_device pointer
 *
 * Let the VCN memory controller know it's offsets
 */
static void vcn_v4_0_3_mc_resume(struct amdgpu_device *adev)
{
	uint32_t offset, size;
	const struct common_firmware_header *hdr;

	hdr = (const struct common_firmware_header *)adev->vcn.fw->data;
	size = AMDGPU_GPU_PAGE_ALIGN(le32_to_cpu(hdr->ucode_size_bytes) + 8);

	/* cache window 0: fw */
	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		WREG32_SOC15(VCN, 0, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW,
			(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN].tmr_mc_addr_lo));
		WREG32_SOC15(VCN, 0, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH,
			(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN].tmr_mc_addr_hi));
		WREG32_SOC15(VCN, 0, regUVD_VCPU_CACHE_OFFSET0, 0);
		offset = 0;
	} else {
		WREG32_SOC15(VCN, 0, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW,
			lower_32_bits(adev->vcn.inst->gpu_addr));
		WREG32_SOC15(VCN, 0, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH,
			upper_32_bits(adev->vcn.inst->gpu_addr));
		offset = size;
		if (amdgpu_emu_mode == 1)
			/* No signed header here */
			WREG32_SOC15(VCN, 0, regUVD_VCPU_CACHE_OFFSET0, 0);
		else
			WREG32_SOC15(VCN, 0, regUVD_VCPU_CACHE_OFFSET0,
				AMDGPU_UVD_FIRMWARE_OFFSET >> 3);
	}
	WREG32_SOC15(VCN, 0, regUVD_VCPU_CACHE_SIZE0, size);

	/* cache window 1: stack */
	WREG32_SOC15(VCN, 0, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW,
		lower_32_bits(adev->vcn.inst->gpu_addr + offset));
	WREG32_SOC15(VCN, 0, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH,
		upper_32_bits(adev->vcn.inst->gpu_addr + offset));
	WREG32_SOC15(VCN, 0, regUVD_VCPU_CACHE_OFFSET1, 0);
	WREG32_SOC15(VCN, 0, regUVD_VCPU_CACHE_SIZE1, AMDGPU_VCN_STACK_SIZE);

	/* cache window 2: context */
	WREG32_SOC15(VCN, 0, regUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW,
		lower_32_bits(adev->vcn.inst->gpu_addr + offset + AMDGPU_VCN_STACK_SIZE));
	WREG32_SOC15(VCN, 0, regUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH,
		upper_32_bits(adev->vcn.inst->gpu_addr + offset + AMDGPU_VCN_STACK_SIZE));
	WREG32_SOC15(VCN, 0, regUVD_VCPU_CACHE_OFFSET2, 0);
	WREG32_SOC15(VCN, 0, regUVD_VCPU_CACHE_SIZE2, AMDGPU_VCN_CONTEXT_SIZE);

	/* non-cache window */
	WREG32_SOC15(VCN, 0, regUVD_LMI_VCPU_NC0_64BIT_BAR_LOW,
		lower_32_bits(adev->vcn.inst->fw_shared.gpu_addr));
	WREG32_SOC15(VCN, 0, regUVD_LMI_VCPU_NC0_64BIT_BAR_HIGH,
		upper_32_bits(adev->vcn.inst->fw_shared.gpu_addr));
	WREG32_SOC15(VCN, 0, regUVD_VCPU_NONCACHE_OFFSET0, 0);
	WREG32_SOC15(VCN, 0, regUVD_VCPU_NONCACHE_SIZE0,
		AMDGPU_GPU_PAGE_ALIGN(sizeof(struct amdgpu_fw_shared)));
}

/**
 * vcn_v4_0_mc_resume_dpg_mode - memory controller programming for dpg mode
 *
 * @adev: amdgpu_device pointer
 * @indirect: indirectly write sram
 *
 * Let the VCN memory controller know it's offsets with dpg mode
 */
static void vcn_v4_0_3_mc_resume_dpg_mode(struct amdgpu_device *adev, bool indirect)
{
	uint32_t offset, size;
	const struct common_firmware_header *hdr;

	hdr = (const struct common_firmware_header *)adev->vcn.fw->data;
	size = AMDGPU_GPU_PAGE_ALIGN(le32_to_cpu(hdr->ucode_size_bytes) + 8);

	/* cache window 0: fw */
	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		if (!indirect) {
			WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
				VCN, 0, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW),
				(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN].tmr_mc_addr_lo), 0, indirect);
			WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
				VCN, 0, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH),
				(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN].tmr_mc_addr_hi), 0, indirect);
			WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
				VCN, 0, regUVD_VCPU_CACHE_OFFSET0), 0, 0, indirect);
		} else {
			WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
				VCN, 0, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW), 0, 0, indirect);
			WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
				VCN, 0, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH), 0, 0, indirect);
			WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
				VCN, 0, regUVD_VCPU_CACHE_OFFSET0), 0, 0, indirect);
		}
		offset = 0;
	} else {
		WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW),
			lower_32_bits(adev->vcn.inst->gpu_addr), 0, indirect);
		WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH),
			upper_32_bits(adev->vcn.inst->gpu_addr), 0, indirect);
		offset = size;
		WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_CACHE_OFFSET0),
			0, 0, indirect);
	}

	if (!indirect)
		WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_CACHE_SIZE0), size, 0, indirect);
	else
		WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_CACHE_SIZE0), 0, 0, indirect);

	/* cache window 1: stack */
	if (!indirect) {
		WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW),
			lower_32_bits(adev->vcn.inst->gpu_addr + offset), 0, indirect);
		WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH),
			upper_32_bits(adev->vcn.inst->gpu_addr + offset), 0, indirect);
		WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_CACHE_OFFSET1), 0, 0, indirect);
	} else {
		WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW), 0, 0, indirect);
		WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH), 0, 0, indirect);
		WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_CACHE_OFFSET1), 0, 0, indirect);
	}
	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_CACHE_SIZE1), AMDGPU_VCN_STACK_SIZE, 0, indirect);

	/* cache window 2: context */
	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW),
			lower_32_bits(adev->vcn.inst->gpu_addr + offset + AMDGPU_VCN_STACK_SIZE), 0, indirect);
	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH),
			upper_32_bits(adev->vcn.inst->gpu_addr + offset + AMDGPU_VCN_STACK_SIZE), 0, indirect);
	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_CACHE_OFFSET2), 0, 0, indirect);
	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_CACHE_SIZE2), AMDGPU_VCN_CONTEXT_SIZE, 0, indirect);

	/* non-cache window */
	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_NC0_64BIT_BAR_LOW),
			lower_32_bits(adev->vcn.inst->fw_shared.gpu_addr), 0, indirect);
	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_NC0_64BIT_BAR_HIGH),
			upper_32_bits(adev->vcn.inst->fw_shared.gpu_addr), 0, indirect);
	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_NONCACHE_OFFSET0), 0, 0, indirect);
	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_NONCACHE_SIZE0),
			AMDGPU_GPU_PAGE_ALIGN(sizeof(struct amdgpu_fw_shared)), 0, indirect);

	/* VCN global tiling registers */
	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_GFX8_ADDR_CONFIG), adev->gfx.config.gb_addr_config, 0, indirect);
	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_GFX10_ADDR_CONFIG), adev->gfx.config.gb_addr_config, 0, indirect);
}

/**
 * vcn_v4_0_disable_static_power_gating - disable VCN static power gating
 *
 * @adev: amdgpu_device pointer
 *
 * Disable static power gating for VCN block
 */
static void vcn_v4_0_3_disable_static_power_gating(struct amdgpu_device *adev)
{
	uint32_t data = 0;

	if (adev->pg_flags & AMD_PG_SUPPORT_VCN) {
		data = (1 << UVD_PGFSM_CONFIG__UVDM_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDS_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDLM_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDF_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTC_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDB_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTA_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTD_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTE_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDE_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDAB_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTB_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDNA_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDNB_PWR_CONFIG__SHIFT);

		WREG32_SOC15(VCN, 0, regUVD_PGFSM_CONFIG, data);
		SOC15_WAIT_ON_RREG(VCN, 0, regUVD_PGFSM_STATUS,
			UVD_PGFSM_STATUS__UVDM_UVDU_UVDLM_PWR_ON_3_0, 0x3F3FFFFF);
	} else {
		data = (1 << UVD_PGFSM_CONFIG__UVDM_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDS_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDLM_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDF_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDTC_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDB_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDTA_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDTD_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDTE_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDE_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDAB_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDTB_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDNA_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDNB_PWR_CONFIG__SHIFT);

		WREG32_SOC15(VCN, 0, regUVD_PGFSM_CONFIG, data);
		SOC15_WAIT_ON_RREG(VCN, 0, regUVD_PGFSM_STATUS, 0,  0x3F3FFFFF);
	}

	data = RREG32_SOC15(VCN, 0, regUVD_POWER_STATUS);
	data &= ~0x103;
	if (adev->pg_flags & AMD_PG_SUPPORT_VCN)
		data |= UVD_PGFSM_CONFIG__UVDM_UVDU_PWR_ON |
			UVD_POWER_STATUS__UVD_PG_EN_MASK;

	WREG32_SOC15(VCN, 0, regUVD_POWER_STATUS, data);
}

/**
 * vcn_v4_0_3_enable_static_power_gating - enable VCN static power gating
 *
 * @adev: amdgpu_device pointer
 *
 * Enable static power gating for VCN block
 */
static void vcn_v4_0_3_enable_static_power_gating(struct amdgpu_device *adev)
{
	uint32_t data;

	if (adev->pg_flags & AMD_PG_SUPPORT_VCN) {
		/* Before power off, this indicator has to be turned on */
		data = RREG32_SOC15(VCN, 0, regUVD_POWER_STATUS);
		data &= ~UVD_POWER_STATUS__UVD_POWER_STATUS_MASK;
		data |= UVD_POWER_STATUS__UVD_POWER_STATUS_TILES_OFF;
		WREG32_SOC15(VCN, 0, regUVD_POWER_STATUS, data);

		data = (2 << UVD_PGFSM_CONFIG__UVDM_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDS_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDF_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTC_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDB_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTA_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDLM_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTD_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTE_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDE_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDAB_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTB_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDNA_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDNB_PWR_CONFIG__SHIFT);
		WREG32_SOC15(VCN, 0, regUVD_PGFSM_CONFIG, data);

		data = (2 << UVD_PGFSM_STATUS__UVDM_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDS_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDF_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDTC_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDB_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDTA_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDLM_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDTD_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDTE_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDE_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDAB_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDTB_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDNA_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDNB_PWR_STATUS__SHIFT);
		SOC15_WAIT_ON_RREG(VCN, 0, regUVD_PGFSM_STATUS, data, 0x3F3FFFFF);
	}
}

/**
 * vcn_v4_0_3_disable_clock_gating - disable VCN clock gating
 *
 * @adev: amdgpu_device pointer
 *
 * Disable clock gating for VCN block
 */
static void vcn_v4_0_3_disable_clock_gating(struct amdgpu_device *adev)
{
	uint32_t data;

	/* VCN disable CGC */
	data = RREG32_SOC15(VCN, 0, regUVD_CGC_CTRL);
	if (adev->cg_flags & AMD_CG_SUPPORT_VCN_MGCG)
		data |= 1 << UVD_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	else
		data &= ~UVD_CGC_CTRL__DYN_CLOCK_MODE_MASK;
	data |= 1 << UVD_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT;
	data |= 4 << UVD_CGC_CTRL__CLK_OFF_DELAY__SHIFT;
	WREG32_SOC15(VCN, 0, regUVD_CGC_CTRL, data);

	data = RREG32_SOC15(VCN, 0, regUVD_CGC_GATE);
	data &= ~(UVD_CGC_GATE__SYS_MASK
		| UVD_CGC_GATE__MPEG2_MASK
		| UVD_CGC_GATE__REGS_MASK
		| UVD_CGC_GATE__RBC_MASK
		| UVD_CGC_GATE__LMI_MC_MASK
		| UVD_CGC_GATE__LMI_UMC_MASK
		| UVD_CGC_GATE__MPC_MASK
		| UVD_CGC_GATE__LBSI_MASK
		| UVD_CGC_GATE__LRBBM_MASK
		| UVD_CGC_GATE__WCB_MASK
		| UVD_CGC_GATE__VCPU_MASK
		| UVD_CGC_GATE__MMSCH_MASK);

	WREG32_SOC15(VCN, 0, regUVD_CGC_GATE, data);
	SOC15_WAIT_ON_RREG(VCN, 0, regUVD_CGC_GATE, 0,  0xFFFFFFFF);

	data = RREG32_SOC15(VCN, 0, regUVD_CGC_CTRL);
	data &= ~(UVD_CGC_CTRL__SYS_MODE_MASK
		| UVD_CGC_CTRL__MPEG2_MODE_MASK
		| UVD_CGC_CTRL__REGS_MODE_MASK
		| UVD_CGC_CTRL__RBC_MODE_MASK
		| UVD_CGC_CTRL__LMI_MC_MODE_MASK
		| UVD_CGC_CTRL__LMI_UMC_MODE_MASK
		| UVD_CGC_CTRL__MPC_MODE_MASK
		| UVD_CGC_CTRL__LBSI_MODE_MASK
		| UVD_CGC_CTRL__LRBBM_MODE_MASK
		| UVD_CGC_CTRL__WCB_MODE_MASK
		| UVD_CGC_CTRL__VCPU_MODE_MASK
		| UVD_CGC_CTRL__MMSCH_MODE_MASK);
	WREG32_SOC15(VCN, 0, regUVD_CGC_CTRL, data);

	data = RREG32_SOC15(VCN, 0, regUVD_SUVD_CGC_GATE);
	data |= (UVD_SUVD_CGC_GATE__SRE_MASK
		| UVD_SUVD_CGC_GATE__SIT_MASK
		| UVD_SUVD_CGC_GATE__SMP_MASK
		| UVD_SUVD_CGC_GATE__SCM_MASK
		| UVD_SUVD_CGC_GATE__SDB_MASK
		| UVD_SUVD_CGC_GATE__SRE_H264_MASK
		| UVD_SUVD_CGC_GATE__SRE_HEVC_MASK
		| UVD_SUVD_CGC_GATE__SIT_H264_MASK
		| UVD_SUVD_CGC_GATE__SIT_HEVC_MASK
		| UVD_SUVD_CGC_GATE__SCM_H264_MASK
		| UVD_SUVD_CGC_GATE__SCM_HEVC_MASK
		| UVD_SUVD_CGC_GATE__SDB_H264_MASK
		| UVD_SUVD_CGC_GATE__SDB_HEVC_MASK
		| UVD_SUVD_CGC_GATE__ENT_MASK
		| UVD_SUVD_CGC_GATE__SIT_HEVC_DEC_MASK
		| UVD_SUVD_CGC_GATE__SITE_MASK
		| UVD_SUVD_CGC_GATE__SRE_VP9_MASK
		| UVD_SUVD_CGC_GATE__SCM_VP9_MASK
		| UVD_SUVD_CGC_GATE__SIT_VP9_DEC_MASK
		| UVD_SUVD_CGC_GATE__SDB_VP9_MASK
		| UVD_SUVD_CGC_GATE__IME_HEVC_MASK);
	WREG32_SOC15(VCN, 0, regUVD_SUVD_CGC_GATE, data);

	data = RREG32_SOC15(VCN, 0, regUVD_SUVD_CGC_CTRL);
	data &= ~(UVD_SUVD_CGC_CTRL__SRE_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SIT_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SMP_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SCM_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SDB_MODE_MASK
		| UVD_SUVD_CGC_CTRL__ENT_MODE_MASK
		| UVD_SUVD_CGC_CTRL__IME_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SITE_MODE_MASK);
	WREG32_SOC15(VCN, 0, regUVD_SUVD_CGC_CTRL, data);
}

/**
 * vcn_v4_0_3_disable_clock_gating_dpg_mode - disable VCN clock gating dpg mode
 *
 * @adev: amdgpu_device pointer
 * @sram_sel: sram select
 * @indirect: indirectly write sram
 *
 * Disable clock gating for VCN block with dpg mode
 */
static void vcn_v4_0_3_disable_clock_gating_dpg_mode(struct amdgpu_device *adev, uint8_t sram_sel,
				uint8_t indirect)
{
	uint32_t reg_data = 0;

	/* enable sw clock gating control */
	if (adev->cg_flags & AMD_CG_SUPPORT_VCN_MGCG)
		reg_data = 1 << UVD_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	else
		reg_data = 0 << UVD_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	reg_data |= 1 << UVD_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT;
	reg_data |= 4 << UVD_CGC_CTRL__CLK_OFF_DELAY__SHIFT;
	reg_data &= ~(UVD_CGC_CTRL__SYS_MODE_MASK |
		 UVD_CGC_CTRL__MPEG2_MODE_MASK |
		 UVD_CGC_CTRL__REGS_MODE_MASK |
		 UVD_CGC_CTRL__RBC_MODE_MASK |
		 UVD_CGC_CTRL__LMI_MC_MODE_MASK |
		 UVD_CGC_CTRL__LMI_UMC_MODE_MASK |
		 UVD_CGC_CTRL__IDCT_MODE_MASK |
		 UVD_CGC_CTRL__MPRD_MODE_MASK |
		 UVD_CGC_CTRL__MPC_MODE_MASK |
		 UVD_CGC_CTRL__LBSI_MODE_MASK |
		 UVD_CGC_CTRL__LRBBM_MODE_MASK |
		 UVD_CGC_CTRL__WCB_MODE_MASK |
		 UVD_CGC_CTRL__VCPU_MODE_MASK);
	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_CGC_CTRL), reg_data, sram_sel, indirect);

	/* turn off clock gating */
	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_CGC_GATE), 0, sram_sel, indirect);

	/* turn on SUVD clock gating */
	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_SUVD_CGC_GATE), 1, sram_sel, indirect);

	/* turn on sw mode in UVD_SUVD_CGC_CTRL */
	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_SUVD_CGC_CTRL), 0, sram_sel, indirect);
}

/**
 * vcn_v4_0_enable_clock_gating - enable VCN clock gating
 *
 * @adev: amdgpu_device pointer
 *
 * Enable clock gating for VCN block
 */
static void vcn_v4_0_3_enable_clock_gating(struct amdgpu_device *adev)
{
	uint32_t data;

	/* enable VCN CGC */
	data = RREG32_SOC15(VCN, 0, regUVD_CGC_CTRL);
	if (adev->cg_flags & AMD_CG_SUPPORT_VCN_MGCG)
		data |= 1 << UVD_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	else
		data |= 0 << UVD_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	data |= 1 << UVD_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT;
	data |= 4 << UVD_CGC_CTRL__CLK_OFF_DELAY__SHIFT;
	WREG32_SOC15(VCN, 0, regUVD_CGC_CTRL, data);

	data = RREG32_SOC15(VCN, 0, regUVD_CGC_CTRL);
	data |= (UVD_CGC_CTRL__SYS_MODE_MASK
		| UVD_CGC_CTRL__MPEG2_MODE_MASK
		| UVD_CGC_CTRL__REGS_MODE_MASK
		| UVD_CGC_CTRL__RBC_MODE_MASK
		| UVD_CGC_CTRL__LMI_MC_MODE_MASK
		| UVD_CGC_CTRL__LMI_UMC_MODE_MASK
		| UVD_CGC_CTRL__MPC_MODE_MASK
		| UVD_CGC_CTRL__LBSI_MODE_MASK
		| UVD_CGC_CTRL__LRBBM_MODE_MASK
		| UVD_CGC_CTRL__WCB_MODE_MASK
		| UVD_CGC_CTRL__VCPU_MODE_MASK);
	WREG32_SOC15(VCN, 0, regUVD_CGC_CTRL, data);

	data = RREG32_SOC15(VCN, 0, regUVD_SUVD_CGC_CTRL);
	data |= (UVD_SUVD_CGC_CTRL__SRE_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SIT_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SMP_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SCM_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SDB_MODE_MASK
		| UVD_SUVD_CGC_CTRL__ENT_MODE_MASK
		| UVD_SUVD_CGC_CTRL__IME_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SITE_MODE_MASK);
	WREG32_SOC15(VCN, 0, regUVD_SUVD_CGC_CTRL, data);
}

/**
 * vcn_v4_0_3_start_dpg_mode - VCN start with dpg mode
 *
 * @adev: amdgpu_device pointer
 * @indirect: indirectly write sram
 *
 * Start VCN block with dpg mode
 */
static int vcn_v4_0_3_start_dpg_mode(struct amdgpu_device *adev, bool indirect)
{
	volatile struct amdgpu_fw_shared *fw_shared = adev->vcn.inst->fw_shared.cpu_addr;
	struct amdgpu_ring *ring;
	uint32_t tmp;

	/* disable register anti-hang mechanism */
	WREG32_P(SOC15_REG_OFFSET(VCN, 0, regUVD_POWER_STATUS), 1,
		~UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);
	/* enable dynamic power gating mode */
	tmp = RREG32_SOC15(VCN, 0, regUVD_POWER_STATUS);
	tmp |= UVD_POWER_STATUS__UVD_PG_MODE_MASK;
	tmp |= UVD_POWER_STATUS__UVD_PG_EN_MASK;
	WREG32_SOC15(VCN, 0, regUVD_POWER_STATUS, tmp);

	if (indirect)
		adev->vcn.inst->dpg_sram_curr_addr = (uint32_t *)adev->vcn.inst->dpg_sram_cpu_addr;

	/* enable clock gating */
	vcn_v4_0_3_disable_clock_gating_dpg_mode(adev, 0, indirect);

	/* enable VCPU clock */
	tmp = (0xFF << UVD_VCPU_CNTL__PRB_TIMEOUT_VAL__SHIFT);
	tmp |= UVD_VCPU_CNTL__CLK_EN_MASK;
	tmp |= UVD_VCPU_CNTL__BLK_RST_MASK;

	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_VCPU_CNTL), tmp, 0, indirect);

	/* disable master interrupt */
	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_MASTINT_EN), 0, 0, indirect);

	/* setup regUVD_LMI_CTRL */
	tmp = (UVD_LMI_CTRL__WRITE_CLEAN_TIMER_EN_MASK |
		UVD_LMI_CTRL__REQ_MODE_MASK |
		UVD_LMI_CTRL__CRC_RESET_MASK |
		UVD_LMI_CTRL__MASK_MC_URGENT_MASK |
		UVD_LMI_CTRL__DATA_COHERENCY_EN_MASK |
		UVD_LMI_CTRL__VCPU_DATA_COHERENCY_EN_MASK |
		(8 << UVD_LMI_CTRL__WRITE_CLEAN_TIMER__SHIFT) |
		0x00100000L);
	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_LMI_CTRL), tmp, 0, indirect);

	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_MPC_CNTL),
		0x2 << UVD_MPC_CNTL__REPLACEMENT_MODE__SHIFT, 0, indirect);

	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_MPC_SET_MUXA0),
		((0x1 << UVD_MPC_SET_MUXA0__VARA_1__SHIFT) |
		 (0x2 << UVD_MPC_SET_MUXA0__VARA_2__SHIFT) |
		 (0x3 << UVD_MPC_SET_MUXA0__VARA_3__SHIFT) |
		 (0x4 << UVD_MPC_SET_MUXA0__VARA_4__SHIFT)), 0, indirect);

	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_MPC_SET_MUXB0),
		 ((0x1 << UVD_MPC_SET_MUXB0__VARB_1__SHIFT) |
		 (0x2 << UVD_MPC_SET_MUXB0__VARB_2__SHIFT) |
		 (0x3 << UVD_MPC_SET_MUXB0__VARB_3__SHIFT) |
		 (0x4 << UVD_MPC_SET_MUXB0__VARB_4__SHIFT)), 0, indirect);

	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_MPC_SET_MUX),
		((0x0 << UVD_MPC_SET_MUX__SET_0__SHIFT) |
		 (0x1 << UVD_MPC_SET_MUX__SET_1__SHIFT) |
		 (0x2 << UVD_MPC_SET_MUX__SET_2__SHIFT)), 0, indirect);

	vcn_v4_0_3_mc_resume_dpg_mode(adev, indirect);

	tmp = (0xFF << UVD_VCPU_CNTL__PRB_TIMEOUT_VAL__SHIFT);
	tmp |= UVD_VCPU_CNTL__CLK_EN_MASK;
	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_VCPU_CNTL), tmp, 0, indirect);

	/* enable LMI MC and UMC channels */
	tmp = 0x1f << UVD_LMI_CTRL2__RE_OFLD_MIF_WR_REQ_NUM__SHIFT;
	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_LMI_CTRL2), tmp, 0, indirect);

	/* enable master interrupt */
	WREG32_SOC15_DPG_MODE(0, SOC15_DPG_MODE_OFFSET(
		VCN, 0, regUVD_MASTINT_EN),
		UVD_MASTINT_EN__VCPU_EN_MASK, 0, indirect);

	if (indirect)
		psp_update_vcn_sram(adev, 0, adev->vcn.inst->dpg_sram_gpu_addr,
			(uint32_t)((uintptr_t)adev->vcn.inst->dpg_sram_curr_addr -
				(uintptr_t)adev->vcn.inst->dpg_sram_cpu_addr));

	ring = &adev->vcn.inst->ring_dec;
	fw_shared->multi_queue.decode_queue_mode |= cpu_to_le32(FW_QUEUE_RING_RESET);

	/* program the RB_BASE for ring buffer */
	WREG32_SOC15(VCN, 0, regUVD_RB_BASE_LO4,
		lower_32_bits(ring->gpu_addr));
	WREG32_SOC15(VCN, 0, regUVD_RB_BASE_HI4,
		upper_32_bits(ring->gpu_addr));

	WREG32_SOC15(VCN, 0, regUVD_RB_SIZE4, ring->ring_size / sizeof(uint32_t));

	/* resetting ring, fw should not check RB ring */
	tmp = RREG32_SOC15(VCN, 0, regVCN_RB_ENABLE);
	tmp &= ~(VCN_RB_ENABLE__RB4_EN_MASK);
	WREG32_SOC15(VCN, 0, regVCN_RB_ENABLE, tmp);

	/* Initialize the ring buffer's read and write pointers */
	WREG32_SOC15(VCN, 0, regUVD_RB_RPTR4, 0);
	WREG32_SOC15(VCN, 0, regUVD_RB_WPTR4, 0);
	ring->wptr = RREG32_SOC15(VCN, 0, regUVD_RB_WPTR4);

	tmp = RREG32_SOC15(VCN, 0, regVCN_RB_ENABLE);
	tmp |= VCN_RB_ENABLE__RB4_EN_MASK;
	WREG32_SOC15(VCN, 0, regVCN_RB_ENABLE, tmp);

	WREG32_SOC15(VCN, 0, regUVD_SCRATCH2, 0);

	/* Reset FW shared memory RBC WPTR/RPTR */
	fw_shared->rb.rptr = 0;
	fw_shared->rb.wptr = lower_32_bits(ring->wptr);

	/*resetting done, fw can check RB ring */
	fw_shared->multi_queue.decode_queue_mode &= cpu_to_le32(~FW_QUEUE_RING_RESET);

	return 0;
}

/**
 * vcn_v4_0_3_start - VCN start
 *
 * @adev: amdgpu_device pointer
 *
 * Start VCN block
 */
static int vcn_v4_0_3_start(struct amdgpu_device *adev)
{
	volatile struct amdgpu_fw_shared *fw_shared;
	struct amdgpu_ring *ring;
	uint32_t tmp;
	int j, k, r;

	if (adev->pm.dpm_enabled)
		amdgpu_dpm_enable_uvd(adev, true);

	if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG)
		return vcn_v4_0_3_start_dpg_mode(adev, adev->vcn.indirect_sram);

	/* disable VCN power gating */
	vcn_v4_0_3_disable_static_power_gating(adev);

	/* set VCN status busy */
	tmp = RREG32_SOC15(VCN, 0, regUVD_STATUS) | UVD_STATUS__UVD_BUSY;
	WREG32_SOC15(VCN, 0, regUVD_STATUS, tmp);

	/*SW clock gating */
	vcn_v4_0_3_disable_clock_gating(adev);

	/* enable VCPU clock */
	WREG32_P(SOC15_REG_OFFSET(VCN, 0, regUVD_VCPU_CNTL),
		UVD_VCPU_CNTL__CLK_EN_MASK, ~UVD_VCPU_CNTL__CLK_EN_MASK);

	/* disable master interrupt */
	WREG32_P(SOC15_REG_OFFSET(VCN, 0, regUVD_MASTINT_EN), 0,
		~UVD_MASTINT_EN__VCPU_EN_MASK);

	/* enable LMI MC and UMC channels */
	WREG32_P(SOC15_REG_OFFSET(VCN, 0, regUVD_LMI_CTRL2), 0,
		~UVD_LMI_CTRL2__STALL_ARB_UMC_MASK);

	tmp = RREG32_SOC15(VCN, 0, regUVD_SOFT_RESET);
	tmp &= ~UVD_SOFT_RESET__LMI_SOFT_RESET_MASK;
	tmp &= ~UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK;
	WREG32_SOC15(VCN, 0, regUVD_SOFT_RESET, tmp);

	/* setup regUVD_LMI_CTRL */
	tmp = RREG32_SOC15(VCN, 0, regUVD_LMI_CTRL);
	WREG32_SOC15(VCN, 0, regUVD_LMI_CTRL, tmp |
		UVD_LMI_CTRL__WRITE_CLEAN_TIMER_EN_MASK |
		UVD_LMI_CTRL__MASK_MC_URGENT_MASK |
		UVD_LMI_CTRL__DATA_COHERENCY_EN_MASK |
		UVD_LMI_CTRL__VCPU_DATA_COHERENCY_EN_MASK);

	/* setup regUVD_MPC_CNTL */
	tmp = RREG32_SOC15(VCN, 0, regUVD_MPC_CNTL);
	tmp &= ~UVD_MPC_CNTL__REPLACEMENT_MODE_MASK;
	tmp |= 0x2 << UVD_MPC_CNTL__REPLACEMENT_MODE__SHIFT;
	WREG32_SOC15(VCN, 0, regUVD_MPC_CNTL, tmp);

	/* setup UVD_MPC_SET_MUXA0 */
	WREG32_SOC15(VCN, 0, regUVD_MPC_SET_MUXA0,
		((0x1 << UVD_MPC_SET_MUXA0__VARA_1__SHIFT) |
		(0x2 << UVD_MPC_SET_MUXA0__VARA_2__SHIFT) |
		(0x3 << UVD_MPC_SET_MUXA0__VARA_3__SHIFT) |
		(0x4 << UVD_MPC_SET_MUXA0__VARA_4__SHIFT)));

	/* setup UVD_MPC_SET_MUXB0 */
	WREG32_SOC15(VCN, 0, regUVD_MPC_SET_MUXB0,
		((0x1 << UVD_MPC_SET_MUXB0__VARB_1__SHIFT) |
		(0x2 << UVD_MPC_SET_MUXB0__VARB_2__SHIFT) |
		(0x3 << UVD_MPC_SET_MUXB0__VARB_3__SHIFT) |
		(0x4 << UVD_MPC_SET_MUXB0__VARB_4__SHIFT)));

	/* setup UVD_MPC_SET_MUX */
	WREG32_SOC15(VCN, 0, regUVD_MPC_SET_MUX,
		((0x0 << UVD_MPC_SET_MUX__SET_0__SHIFT) |
		(0x1 << UVD_MPC_SET_MUX__SET_1__SHIFT) |
		(0x2 << UVD_MPC_SET_MUX__SET_2__SHIFT)));

	vcn_v4_0_3_mc_resume(adev);

	/* VCN global tiling registers */
	WREG32_SOC15(VCN, 0, regUVD_GFX8_ADDR_CONFIG,
		adev->gfx.config.gb_addr_config);
	WREG32_SOC15(VCN, 0, regUVD_GFX10_ADDR_CONFIG,
		adev->gfx.config.gb_addr_config);

	/* unblock VCPU register access */
	WREG32_P(SOC15_REG_OFFSET(VCN, 0, regUVD_RB_ARB_CTRL), 0,
		~UVD_RB_ARB_CTRL__VCPU_DIS_MASK);

	/* release VCPU reset to boot */
	WREG32_P(SOC15_REG_OFFSET(VCN, 0, regUVD_VCPU_CNTL), 0,
		~UVD_VCPU_CNTL__BLK_RST_MASK);

	for (j = 0; j < 10; ++j) {
		uint32_t status;

		for (k = 0; k < 100; ++k) {
			status = RREG32_SOC15(VCN, 0, regUVD_STATUS);
			if (status & 2)
				break;
			if (amdgpu_emu_mode == 1)
				msleep(500);
			else
				mdelay(10);
		}
		r = 0;
		if (status & 2)
			break;

		DRM_DEV_ERROR(adev->dev,
			"VCN decode not responding, trying to reset the VCPU!!!\n");
		WREG32_P(SOC15_REG_OFFSET(VCN, 0, regUVD_VCPU_CNTL),
			UVD_VCPU_CNTL__BLK_RST_MASK,
			~UVD_VCPU_CNTL__BLK_RST_MASK);
		mdelay(10);
		WREG32_P(SOC15_REG_OFFSET(VCN, 0, regUVD_VCPU_CNTL), 0,
			~UVD_VCPU_CNTL__BLK_RST_MASK);

		mdelay(10);
		r = -1;
	}

	if (r) {
		DRM_DEV_ERROR(adev->dev, "VCN decode not responding, giving up!!!\n");
		return r;
	}

	/* enable master interrupt */
	WREG32_P(SOC15_REG_OFFSET(VCN, 0, regUVD_MASTINT_EN),
		UVD_MASTINT_EN__VCPU_EN_MASK,
		~UVD_MASTINT_EN__VCPU_EN_MASK);

	/* clear the busy bit of VCN_STATUS */
	WREG32_P(SOC15_REG_OFFSET(VCN, 0, regUVD_STATUS), 0,
		~(2 << UVD_STATUS__VCPU_REPORT__SHIFT));

	ring = &adev->vcn.inst->ring_dec;

	fw_shared = adev->vcn.inst->fw_shared.cpu_addr;
	fw_shared->multi_queue.decode_queue_mode |= cpu_to_le32(FW_QUEUE_RING_RESET);

	/* program the RB_BASE for ring buffer */
	WREG32_SOC15(VCN, 0, regUVD_RB_BASE_LO4,
		lower_32_bits(ring->gpu_addr));
	WREG32_SOC15(VCN, 0, regUVD_RB_BASE_HI4,
		upper_32_bits(ring->gpu_addr));

	WREG32_SOC15(VCN, 0, regUVD_RB_SIZE4, ring->ring_size / sizeof(uint32_t));

	/* resetting ring, fw should not check RB ring */
	tmp = RREG32_SOC15(VCN, 0, regVCN_RB_ENABLE);
	tmp &= ~(VCN_RB_ENABLE__RB4_EN_MASK);
	WREG32_SOC15(VCN, 0, regVCN_RB_ENABLE, tmp);

	/* Initialize the ring buffer's read and write pointers */
	WREG32_SOC15(VCN, 0, regUVD_RB_RPTR4, 0);
	WREG32_SOC15(VCN, 0, regUVD_RB_WPTR4, 0);

	tmp = RREG32_SOC15(VCN, 0, regVCN_RB_ENABLE);
	tmp |= VCN_RB_ENABLE__RB4_EN_MASK;
	WREG32_SOC15(VCN, 0, regVCN_RB_ENABLE, tmp);

	ring->wptr = RREG32_SOC15(VCN, 0, regUVD_RB_WPTR4);
	fw_shared->rb.wptr = cpu_to_le32(lower_32_bits(ring->wptr));
	fw_shared->multi_queue.decode_queue_mode &= cpu_to_le32(~FW_QUEUE_RING_RESET);

	return 0;
}

/**
 * vcn_v4_0_3_stop_dpg_mode - VCN stop with dpg mode
 *
 * @adev: amdgpu_device pointer
 *
 * Stop VCN block with dpg mode
 */
static int vcn_v4_0_3_stop_dpg_mode(struct amdgpu_device *adev)
{
	uint32_t tmp;

	/* Wait for power status to be 1 */
	SOC15_WAIT_ON_RREG(VCN, 0, regUVD_POWER_STATUS, 1,
		UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);

	/* wait for read ptr to be equal to write ptr */
	tmp = RREG32_SOC15(VCN, 0, regUVD_RB_WPTR);
	SOC15_WAIT_ON_RREG(VCN, 0, regUVD_RB_RPTR, tmp, 0xFFFFFFFF);

	tmp = RREG32_SOC15(VCN, 0, regUVD_RB_WPTR2);
	SOC15_WAIT_ON_RREG(VCN, 0, regUVD_RB_RPTR2, tmp, 0xFFFFFFFF);

	tmp = RREG32_SOC15(VCN, 0, regUVD_RB_WPTR4);
	SOC15_WAIT_ON_RREG(VCN, 0, regUVD_RB_RPTR4, tmp, 0xFFFFFFFF);

	SOC15_WAIT_ON_RREG(VCN, 0, regUVD_POWER_STATUS, 1,
		UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);

	/* disable dynamic power gating mode */
	WREG32_P(SOC15_REG_OFFSET(VCN, 0, regUVD_POWER_STATUS), 0,
		~UVD_POWER_STATUS__UVD_PG_MODE_MASK);
	return 0;
}

/**
 * vcn_v4_0_3_stop - VCN stop
 *
 * @adev: amdgpu_device pointer
 *
 * Stop VCN block
 */
static int vcn_v4_0_3_stop(struct amdgpu_device *adev)
{
	uint32_t tmp;
	int r = 0;

	if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG) {
		r = vcn_v4_0_3_stop_dpg_mode(adev);
		goto Done;
	}

	/* wait for vcn idle */
	r = SOC15_WAIT_ON_RREG(VCN, 0, regUVD_STATUS, UVD_STATUS__IDLE, 0x7);
	if (r)
		return r;

	tmp = UVD_LMI_STATUS__VCPU_LMI_WRITE_CLEAN_MASK |
		UVD_LMI_STATUS__READ_CLEAN_MASK |
		UVD_LMI_STATUS__WRITE_CLEAN_MASK |
		UVD_LMI_STATUS__WRITE_CLEAN_RAW_MASK;
	r = SOC15_WAIT_ON_RREG(VCN, 0, regUVD_LMI_STATUS, tmp, tmp);
	if (r)
		return r;

	/* stall UMC channel */
	tmp = RREG32_SOC15(VCN, 0, regUVD_LMI_CTRL2);
	tmp |= UVD_LMI_CTRL2__STALL_ARB_UMC_MASK;
	WREG32_SOC15(VCN, 0, regUVD_LMI_CTRL2, tmp);
	tmp = UVD_LMI_STATUS__UMC_READ_CLEAN_RAW_MASK|
		UVD_LMI_STATUS__UMC_WRITE_CLEAN_RAW_MASK;
	r = SOC15_WAIT_ON_RREG(VCN, 0, regUVD_LMI_STATUS, tmp, tmp);
	if (r)
		return r;

	/* Unblock VCPU Register access */
	WREG32_P(SOC15_REG_OFFSET(VCN, 0, regUVD_RB_ARB_CTRL),
		UVD_RB_ARB_CTRL__VCPU_DIS_MASK,
		~UVD_RB_ARB_CTRL__VCPU_DIS_MASK);

	/* release VCPU reset to boot */
	WREG32_P(SOC15_REG_OFFSET(VCN, 0, regUVD_VCPU_CNTL),
		UVD_VCPU_CNTL__BLK_RST_MASK,
		~UVD_VCPU_CNTL__BLK_RST_MASK);

	/* disable VCPU clock */
	WREG32_P(SOC15_REG_OFFSET(VCN, 0, regUVD_VCPU_CNTL), 0,
		~(UVD_VCPU_CNTL__CLK_EN_MASK));

	/* reset LMI UMC/LMI/VCPU */
	tmp = RREG32_SOC15(VCN, 0, regUVD_SOFT_RESET);
	tmp |= UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK;
	WREG32_SOC15(VCN, 0, regUVD_SOFT_RESET, tmp);

	tmp = RREG32_SOC15(VCN, 0, regUVD_SOFT_RESET);
	tmp |= UVD_SOFT_RESET__LMI_SOFT_RESET_MASK;
	WREG32_SOC15(VCN, 0, regUVD_SOFT_RESET, tmp);

	tmp = RREG32_SOC15(VCN, 0, regUVD_VCPU_CNTL);
	tmp |= UVD_VCPU_CNTL__BLK_RST_MASK;
	WREG32_SOC15(VCN, 0, regUVD_SOFT_RESET, tmp);

	/* clear VCN status */
	WREG32_SOC15(VCN, 0, regUVD_STATUS, 0);

	/* apply HW clock gating */
	vcn_v4_0_3_enable_clock_gating(adev);

	/* enable VCN power gating */
	vcn_v4_0_3_enable_static_power_gating(adev);

Done:
	if (adev->pm.dpm_enabled)
		amdgpu_dpm_enable_uvd(adev, false);

	return 0;
}

/**
 * vcn_v4_0_3_pause_dpg_mode - VCN pause with dpg mode
 *
 * @adev: amdgpu_device pointer
 * @inst_idx: instance number index
 * @new_state: pause state
 *
 * Pause dpg mode for VCN block
 */
static int vcn_v4_0_3_pause_dpg_mode(struct amdgpu_device *adev, int inst_idx,
				struct dpg_pause_state *new_state)
{

	return 0;
}

/**
 * vcn_v4_0_3_dec_ring_get_rptr - get read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware read pointer
 */
static uint64_t vcn_v4_0_3_dec_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32_SOC15(VCN, ring->me, regUVD_RB_RPTR4);
}

/**
 * vcn_v4_0_3_dec_ring_get_wptr - get write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware write pointer
 */
static uint64_t vcn_v4_0_3_dec_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell)
		return adev->wb.wb[ring->wptr_offs];
	else
		return RREG32_SOC15(VCN, ring->me, regUVD_RB_WPTR4);
}

/**
 * vcn_v4_0_3_dec_ring_set_wptr - set write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the write pointer to the hardware
 */
static void vcn_v4_0_3_dec_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	volatile struct amdgpu_fw_shared *fw_shared;

	if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG) {
		/*whenever update RBC_RB_WPTR, we save the wptr in shared rb.wptr and scratch2 */
		fw_shared = adev->vcn.inst->fw_shared.cpu_addr;
		fw_shared->rb.wptr = lower_32_bits(ring->wptr);
		WREG32_SOC15(VCN, ring->me, regUVD_SCRATCH2,
			lower_32_bits(ring->wptr));
	}

	if (ring->use_doorbell) {
		adev->wb.wb[ring->wptr_offs] = lower_32_bits(ring->wptr);
		WDOORBELL32(ring->doorbell_index, lower_32_bits(ring->wptr));
	} else {
		WREG32_SOC15(VCN, ring->me, regUVD_RB_WPTR4, lower_32_bits(ring->wptr));
	}
}

static const struct amdgpu_ring_funcs vcn_v4_0_3_dec_sw_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_DEC,
	.align_mask = 0x3f,
	.nop = VCN_DEC_SW_CMD_NO_OP,
	.get_rptr = vcn_v4_0_3_dec_ring_get_rptr,
	.get_wptr = vcn_v4_0_3_dec_ring_get_wptr,
	.set_wptr = vcn_v4_0_3_dec_ring_set_wptr,
	.emit_frame_size =
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 3 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 4 +
		VCN_SW_RING_EMIT_FRAME_SIZE,
	.emit_ib_size = 5, /* vcn_dec_sw_ring_emit_ib */
	.emit_ib = vcn_dec_sw_ring_emit_ib,
	.emit_fence = vcn_dec_sw_ring_emit_fence,
	.emit_vm_flush = vcn_dec_sw_ring_emit_vm_flush,
	.test_ring = amdgpu_vcn_dec_sw_ring_test_ring,
	.test_ib = amdgpu_vcn_dec_sw_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.insert_end = vcn_dec_sw_ring_insert_end,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_vcn_ring_begin_use,
	.end_use = amdgpu_vcn_ring_end_use,
	.emit_wreg = vcn_dec_sw_ring_emit_wreg,
	.emit_reg_wait = vcn_dec_sw_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
};

/**
 * vcn_v4_0_3_set_dec_ring_funcs - set dec ring functions
 *
 * @adev: amdgpu_device pointer
 *
 * Set decode ring functions
 */
static void vcn_v4_0_3_set_dec_ring_funcs(struct amdgpu_device *adev)
{
	adev->vcn.inst->ring_dec.funcs = &vcn_v4_0_3_dec_sw_ring_vm_funcs;
	adev->vcn.inst->ring_dec.me = 0;
	DRM_DEV_INFO(adev->dev, "VCN decode(Software Ring) is enabled in VM mode\n");
}

/**
 * vcn_v4_0_3_is_idle - check VCN block is idle
 *
 * @handle: amdgpu_device pointer
 *
 * Check whether VCN block is idle
 */
static bool vcn_v4_0_3_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return (RREG32_SOC15(VCN, 0, regUVD_STATUS) == UVD_STATUS__IDLE);
}

/**
 * vcn_v4_0_3_wait_for_idle - wait for VCN block idle
 *
 * @handle: amdgpu_device pointer
 *
 * Wait for VCN block idle
 */
static int vcn_v4_0_3_wait_for_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return SOC15_WAIT_ON_RREG(VCN, 0, regUVD_STATUS, UVD_STATUS__IDLE,
			UVD_STATUS__IDLE);
}

/**
 * vcn_v4_0_3_set_clockgating_state - set VCN block clockgating state
 *
 * @handle: amdgpu_device pointer
 * @state: clock gating state
 *
 * Set VCN block clockgating state
 */
static int vcn_v4_0_3_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	bool enable = (state == AMD_CG_STATE_GATE) ? true : false;

	if (enable) {
		if (RREG32_SOC15(VCN, 0, regUVD_STATUS) != UVD_STATUS__IDLE)
			return -EBUSY;
		vcn_v4_0_3_enable_clock_gating(adev);
	} else {
		vcn_v4_0_3_disable_clock_gating(adev);
	}

	return 0;
}

/**
 * vcn_v4_0_3_set_powergating_state - set VCN block powergating state
 *
 * @handle: amdgpu_device pointer
 * @state: power gating state
 *
 * Set VCN block powergating state
 */
static int vcn_v4_0_3_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int ret;

	if (state == adev->vcn.cur_state)
		return 0;

	if (state == AMD_PG_STATE_GATE)
		ret = vcn_v4_0_3_stop(adev);
	else
		ret = vcn_v4_0_3_start(adev);

	if (!ret)
		adev->vcn.cur_state = state;

	return ret;
}

/**
 * vcn_v4_0_3_set_interrupt_state - set VCN block interrupt state
 *
 * @adev: amdgpu_device pointer
 * @source: interrupt sources
 * @type: interrupt types
 * @state: interrupt states
 *
 * Set VCN block interrupt state
 */
static int vcn_v4_0_3_set_interrupt_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned int type,
					enum amdgpu_interrupt_state state)
{
	return 0;
}

/**
 * vcn_v4_0_process_interrupt - process VCN block interrupt
 *
 * @adev: amdgpu_device pointer
 * @source: interrupt sources
 * @entry: interrupt entry from clients and sources
 *
 * Process VCN block interrupt
 */
static int vcn_v4_0_3_process_interrupt(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	DRM_DEV_DEBUG(adev->dev, "IH: VCN TRAP\n");

	switch (entry->src_id) {
	case VCN_2_0__SRCID__UVD_SYSTEM_MESSAGE_INTERRUPT:
		amdgpu_fence_process(&adev->vcn.inst->ring_dec);
		break;
	default:
		DRM_DEV_ERROR(adev->dev, "Unhandled interrupt: %d %d\n",
			  entry->src_id, entry->src_data[0]);
		break;
	}

	return 0;
}

static const struct amdgpu_irq_src_funcs vcn_v4_0_3_irq_funcs = {
	.set = vcn_v4_0_3_set_interrupt_state,
	.process = vcn_v4_0_3_process_interrupt,
};

/**
 * vcn_v4_0_3_set_irq_funcs - set VCN block interrupt irq functions
 *
 * @adev: amdgpu_device pointer
 *
 * Set VCN block interrupt irq functions
 */
static void vcn_v4_0_3_set_irq_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (adev->vcn.harvest_config & (1 << i))
			continue;

		adev->vcn.inst->irq.num_types = 1;
		adev->vcn.inst->irq.funcs = &vcn_v4_0_3_irq_funcs;
	}
}

static const struct amd_ip_funcs vcn_v4_0_3_ip_funcs = {
	.name = "vcn_v4_0_3",
	.early_init = vcn_v4_0_3_early_init,
	.late_init = NULL,
	.sw_init = vcn_v4_0_3_sw_init,
	.sw_fini = vcn_v4_0_3_sw_fini,
	.hw_init = vcn_v4_0_3_hw_init,
	.hw_fini = vcn_v4_0_3_hw_fini,
	.suspend = vcn_v4_0_3_suspend,
	.resume = vcn_v4_0_3_resume,
	.is_idle = vcn_v4_0_3_is_idle,
	.wait_for_idle = vcn_v4_0_3_wait_for_idle,
	.check_soft_reset = NULL,
	.pre_soft_reset = NULL,
	.soft_reset = NULL,
	.post_soft_reset = NULL,
	.set_clockgating_state = vcn_v4_0_3_set_clockgating_state,
	.set_powergating_state = vcn_v4_0_3_set_powergating_state,
};

const struct amdgpu_ip_block_version vcn_v4_0_3_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_VCN,
	.major = 4,
	.minor = 0,
	.rev = 3,
	.funcs = &vcn_v4_0_3_ip_funcs,
};
