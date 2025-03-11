/*
 * Copyright 2024 Advanced Micro Devices, Inc. All rights reserved.
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
#include "amdgpu_vcn.h"
#include "amdgpu_pm.h"
#include "soc15.h"
#include "soc15d.h"
#include "soc15_hw_ip.h"
#include "vcn_v2_0.h"

#include "vcn/vcn_5_0_0_offset.h"
#include "vcn/vcn_5_0_0_sh_mask.h"
#include "ivsrcid/vcn/irqsrcs_vcn_5_0.h"
#include "vcn_v5_0_0.h"
#include "vcn_v5_0_1.h"

#include <drm/drm_drv.h>

static void vcn_v5_0_1_set_unified_ring_funcs(struct amdgpu_device *adev);
static void vcn_v5_0_1_set_irq_funcs(struct amdgpu_device *adev);
static int vcn_v5_0_1_set_powergating_state(struct amdgpu_ip_block *ip_block,
		enum amd_powergating_state state);
static void vcn_v5_0_1_unified_ring_set_wptr(struct amdgpu_ring *ring);

/**
 * vcn_v5_0_1_early_init - set function pointers and load microcode
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Set ring and irq function pointers
 * Load microcode from filesystem
 */
static int vcn_v5_0_1_early_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	/* re-use enc ring as unified ring */
	adev->vcn.num_enc_rings = 1;

	vcn_v5_0_1_set_unified_ring_funcs(adev);
	vcn_v5_0_1_set_irq_funcs(adev);

	return amdgpu_vcn_early_init(adev);
}

/**
 * vcn_v5_0_1_sw_init - sw init for VCN block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Load firmware and sw initialization
 */
static int vcn_v5_0_1_sw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_ring *ring;
	int i, r, vcn_inst;

	r = amdgpu_vcn_sw_init(adev);
	if (r)
		return r;

	amdgpu_vcn_setup_ucode(adev);

	r = amdgpu_vcn_resume(adev);
	if (r)
		return r;

	/* VCN UNIFIED TRAP */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_VCN,
		VCN_5_0__SRCID__UVD_ENC_GENERAL_PURPOSE, &adev->vcn.inst->irq);
	if (r)
		return r;

	for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
		volatile struct amdgpu_vcn5_fw_shared *fw_shared;

		vcn_inst = GET_INST(VCN, i);

		ring = &adev->vcn.inst[i].ring_enc[0];
		ring->use_doorbell = true;
		ring->doorbell_index = (adev->doorbell_index.vcn.vcn_ring0_1 << 1) + 9 * vcn_inst;

		ring->vm_hub = AMDGPU_MMHUB0(adev->vcn.inst[i].aid_id);
		sprintf(ring->name, "vcn_unified_%d", adev->vcn.inst[i].aid_id);

		r = amdgpu_ring_init(adev, ring, 512, &adev->vcn.inst[i].irq, 0,
					AMDGPU_RING_PRIO_DEFAULT, &adev->vcn.inst[i].sched_score);
		if (r)
			return r;

		fw_shared = adev->vcn.inst[i].fw_shared.cpu_addr;
		fw_shared->present_flag_0 = cpu_to_le32(AMDGPU_FW_SHARED_FLAG_0_UNIFIED_QUEUE);
		fw_shared->sq.is_enabled = true;

		if (amdgpu_vcnfw_log)
			amdgpu_vcn_fwlog_init(&adev->vcn.inst[i]);
	}

	/* TODO: Add queue reset mask when FW fully supports it */
	adev->vcn.supported_reset =
		amdgpu_get_soft_full_reset_mask(&adev->vcn.inst[0].ring_enc[0]);

	vcn_v5_0_0_alloc_ip_dump(adev);

	return amdgpu_vcn_sysfs_reset_mask_init(adev);
}

/**
 * vcn_v5_0_1_sw_fini - sw fini for VCN block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * VCN suspend and free up sw allocation
 */
static int vcn_v5_0_1_sw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i, r, idx;

	if (drm_dev_enter(adev_to_drm(adev), &idx)) {
		for (i = 0; i < adev->vcn.num_vcn_inst; i++) {
			volatile struct amdgpu_vcn4_fw_shared *fw_shared;

			fw_shared = adev->vcn.inst[i].fw_shared.cpu_addr;
			fw_shared->present_flag_0 = 0;
			fw_shared->sq.is_enabled = 0;
		}

		drm_dev_exit(idx);
	}

	r = amdgpu_vcn_suspend(adev);
	if (r)
		return r;

	r = amdgpu_vcn_sw_fini(adev);

	amdgpu_vcn_sysfs_reset_mask_fini(adev);

	kfree(adev->vcn.ip_dump);

	return r;
}

/**
 * vcn_v5_0_1_hw_init - start and test VCN block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Initialize the hardware, boot up the VCPU and do some testing
 */
static int vcn_v5_0_1_hw_init(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	struct amdgpu_ring *ring;
	int i, r, vcn_inst;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		vcn_inst = GET_INST(VCN, i);
		ring = &adev->vcn.inst[i].ring_enc[0];

		if (ring->use_doorbell)
			adev->nbio.funcs->vcn_doorbell_range(adev, ring->use_doorbell,
				((adev->doorbell_index.vcn.vcn_ring0_1 << 1) +
				 9 * vcn_inst),
				adev->vcn.inst[i].aid_id);

		r = amdgpu_ring_test_helper(ring);
		if (r)
			return r;
	}

	return 0;
}

/**
 * vcn_v5_0_1_hw_fini - stop the hardware block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Stop the VCN block, mark ring as not ready any more
 */
static int vcn_v5_0_1_hw_fini(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;

	cancel_delayed_work_sync(&adev->vcn.idle_work);

	return 0;
}

/**
 * vcn_v5_0_1_suspend - suspend VCN block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * HW fini and suspend VCN block
 */
static int vcn_v5_0_1_suspend(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r;

	r = vcn_v5_0_1_hw_fini(ip_block);
	if (r)
		return r;

	r = amdgpu_vcn_suspend(adev);

	return r;
}

/**
 * vcn_v5_0_1_resume - resume VCN block
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Resume firmware and hw init VCN block
 */
static int vcn_v5_0_1_resume(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int r;

	r = amdgpu_vcn_resume(adev);
	if (r)
		return r;

	r = vcn_v5_0_1_hw_init(ip_block);

	return r;
}

/**
 * vcn_v5_0_1_mc_resume - memory controller programming
 *
 * @adev: amdgpu_device pointer
 * @inst: instance number
 *
 * Let the VCN memory controller know it's offsets
 */
static void vcn_v5_0_1_mc_resume(struct amdgpu_device *adev, int inst)
{
	uint32_t offset, size, vcn_inst;
	const struct common_firmware_header *hdr;

	hdr = (const struct common_firmware_header *)adev->vcn.inst[inst].fw->data;
	size = AMDGPU_GPU_PAGE_ALIGN(le32_to_cpu(hdr->ucode_size_bytes) + 8);

	vcn_inst = GET_INST(VCN, inst);
	/* cache window 0: fw */
	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		WREG32_SOC15(VCN, vcn_inst, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW,
			(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN + inst].tmr_mc_addr_lo));
		WREG32_SOC15(VCN, vcn_inst, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH,
			(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN + inst].tmr_mc_addr_hi));
		WREG32_SOC15(VCN, vcn_inst, regUVD_VCPU_CACHE_OFFSET0, 0);
		offset = 0;
	} else {
		WREG32_SOC15(VCN, vcn_inst, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW,
			lower_32_bits(adev->vcn.inst[inst].gpu_addr));
		WREG32_SOC15(VCN, vcn_inst, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH,
			upper_32_bits(adev->vcn.inst[inst].gpu_addr));
		offset = size;
		WREG32_SOC15(VCN, vcn_inst, regUVD_VCPU_CACHE_OFFSET0,
				AMDGPU_UVD_FIRMWARE_OFFSET >> 3);
	}
	WREG32_SOC15(VCN, vcn_inst, regUVD_VCPU_CACHE_SIZE0, size);

	/* cache window 1: stack */
	WREG32_SOC15(VCN, vcn_inst, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW,
		lower_32_bits(adev->vcn.inst[inst].gpu_addr + offset));
	WREG32_SOC15(VCN, vcn_inst, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH,
		upper_32_bits(adev->vcn.inst[inst].gpu_addr + offset));
	WREG32_SOC15(VCN, vcn_inst, regUVD_VCPU_CACHE_OFFSET1, 0);
	WREG32_SOC15(VCN, vcn_inst, regUVD_VCPU_CACHE_SIZE1, AMDGPU_VCN_STACK_SIZE);

	/* cache window 2: context */
	WREG32_SOC15(VCN, vcn_inst, regUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW,
		lower_32_bits(adev->vcn.inst[inst].gpu_addr + offset + AMDGPU_VCN_STACK_SIZE));
	WREG32_SOC15(VCN, vcn_inst, regUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH,
		upper_32_bits(adev->vcn.inst[inst].gpu_addr + offset + AMDGPU_VCN_STACK_SIZE));
	WREG32_SOC15(VCN, vcn_inst, regUVD_VCPU_CACHE_OFFSET2, 0);
	WREG32_SOC15(VCN, vcn_inst, regUVD_VCPU_CACHE_SIZE2, AMDGPU_VCN_CONTEXT_SIZE);

	/* non-cache window */
	WREG32_SOC15(VCN, vcn_inst, regUVD_LMI_VCPU_NC0_64BIT_BAR_LOW,
		lower_32_bits(adev->vcn.inst[inst].fw_shared.gpu_addr));
	WREG32_SOC15(VCN, vcn_inst, regUVD_LMI_VCPU_NC0_64BIT_BAR_HIGH,
		upper_32_bits(adev->vcn.inst[inst].fw_shared.gpu_addr));
	WREG32_SOC15(VCN, vcn_inst, regUVD_VCPU_NONCACHE_OFFSET0, 0);
	WREG32_SOC15(VCN, vcn_inst, regUVD_VCPU_NONCACHE_SIZE0,
		AMDGPU_GPU_PAGE_ALIGN(sizeof(struct amdgpu_vcn4_fw_shared)));
}

/**
 * vcn_v5_0_1_mc_resume_dpg_mode - memory controller programming for dpg mode
 *
 * @adev: amdgpu_device pointer
 * @inst_idx: instance number index
 * @indirect: indirectly write sram
 *
 * Let the VCN memory controller know it's offsets with dpg mode
 */
static void vcn_v5_0_1_mc_resume_dpg_mode(struct amdgpu_device *adev, int inst_idx, bool indirect)
{
	uint32_t offset, size;
	const struct common_firmware_header *hdr;

	hdr = (const struct common_firmware_header *)adev->vcn.inst[inst_idx].fw->data;
	size = AMDGPU_GPU_PAGE_ALIGN(le32_to_cpu(hdr->ucode_size_bytes) + 8);

	/* cache window 0: fw */
	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		if (!indirect) {
			WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
				VCN, 0, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW),
				(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN +
				 inst_idx].tmr_mc_addr_lo), 0, indirect);
			WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
				VCN, 0, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH),
				(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN +
				 inst_idx].tmr_mc_addr_hi), 0, indirect);
			WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
				VCN, 0, regUVD_VCPU_CACHE_OFFSET0), 0, 0, indirect);
		} else {
			WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
				VCN, 0, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW), 0, 0, indirect);
			WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
				VCN, 0, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH), 0, 0, indirect);
			WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
				VCN, 0, regUVD_VCPU_CACHE_OFFSET0), 0, 0, indirect);
		}
		offset = 0;
	} else {
		WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW),
			lower_32_bits(adev->vcn.inst[inst_idx].gpu_addr), 0, indirect);
		WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH),
			upper_32_bits(adev->vcn.inst[inst_idx].gpu_addr), 0, indirect);
		offset = size;
		WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_CACHE_OFFSET0),
			AMDGPU_UVD_FIRMWARE_OFFSET >> 3, 0, indirect);
	}

	if (!indirect)
		WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_CACHE_SIZE0), size, 0, indirect);
	else
		WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_CACHE_SIZE0), 0, 0, indirect);

	/* cache window 1: stack */
	if (!indirect) {
		WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW),
			lower_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset), 0, indirect);
		WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH),
			upper_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset), 0, indirect);
		WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_CACHE_OFFSET1), 0, 0, indirect);
	} else {
		WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW), 0, 0, indirect);
		WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, 0, regUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH), 0, 0, indirect);
		WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_CACHE_OFFSET1), 0, 0, indirect);
	}
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
			VCN, 0, regUVD_VCPU_CACHE_SIZE1), AMDGPU_VCN_STACK_SIZE, 0, indirect);

	/* cache window 2: context */
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, 0, regUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW),
		lower_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset +
			AMDGPU_VCN_STACK_SIZE), 0, indirect);
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, 0, regUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH),
		upper_32_bits(adev->vcn.inst[inst_idx].gpu_addr + offset +
			AMDGPU_VCN_STACK_SIZE), 0, indirect);
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, 0, regUVD_VCPU_CACHE_OFFSET2), 0, 0, indirect);
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, 0, regUVD_VCPU_CACHE_SIZE2), AMDGPU_VCN_CONTEXT_SIZE, 0, indirect);

	/* non-cache window */
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, 0, regUVD_LMI_VCPU_NC0_64BIT_BAR_LOW),
		lower_32_bits(adev->vcn.inst[inst_idx].fw_shared.gpu_addr), 0, indirect);
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, 0, regUVD_LMI_VCPU_NC0_64BIT_BAR_HIGH),
		upper_32_bits(adev->vcn.inst[inst_idx].fw_shared.gpu_addr), 0, indirect);
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, 0, regUVD_VCPU_NONCACHE_OFFSET0), 0, 0, indirect);
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, 0, regUVD_VCPU_NONCACHE_SIZE0),
		AMDGPU_GPU_PAGE_ALIGN(sizeof(struct amdgpu_vcn4_fw_shared)), 0, indirect);

	/* VCN global tiling registers */
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, 0, regUVD_GFX10_ADDR_CONFIG), adev->gfx.config.gb_addr_config, 0, indirect);
}

/**
 * vcn_v5_0_1_disable_clock_gating - disable VCN clock gating
 *
 * @adev: amdgpu_device pointer
 * @inst: instance number
 *
 * Disable clock gating for VCN block
 */
static void vcn_v5_0_1_disable_clock_gating(struct amdgpu_device *adev, int inst)
{
}

/**
 * vcn_v5_0_1_enable_clock_gating - enable VCN clock gating
 *
 * @adev: amdgpu_device pointer
 * @inst: instance number
 *
 * Enable clock gating for VCN block
 */
static void vcn_v5_0_1_enable_clock_gating(struct amdgpu_device *adev, int inst)
{
}

/**
 * vcn_v5_0_1_start_dpg_mode - VCN start with dpg mode
 *
 * @adev: amdgpu_device pointer
 * @inst_idx: instance number index
 * @indirect: indirectly write sram
 *
 * Start VCN block with dpg mode
 */
static int vcn_v5_0_1_start_dpg_mode(struct amdgpu_device *adev, int inst_idx, bool indirect)
{
	volatile struct amdgpu_vcn4_fw_shared *fw_shared =
		adev->vcn.inst[inst_idx].fw_shared.cpu_addr;
	struct amdgpu_ring *ring;
	int vcn_inst;
	uint32_t tmp;

	vcn_inst = GET_INST(VCN, inst_idx);

	/* disable register anti-hang mechanism */
	WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_POWER_STATUS), 1,
		~UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);

	/* enable dynamic power gating mode */
	tmp = RREG32_SOC15(VCN, vcn_inst, regUVD_POWER_STATUS);
	tmp |= UVD_POWER_STATUS__UVD_PG_MODE_MASK;
	WREG32_SOC15(VCN, vcn_inst, regUVD_POWER_STATUS, tmp);

	if (indirect) {
		adev->vcn.inst[inst_idx].dpg_sram_curr_addr =
			(uint32_t *)adev->vcn.inst[inst_idx].dpg_sram_cpu_addr;
		/* Use dummy register 0xDEADBEEF passing AID selection to PSP FW */
		WREG32_SOC24_DPG_MODE(inst_idx, 0xDEADBEEF,
				adev->vcn.inst[inst_idx].aid_id, 0, true);
	}

	/* enable VCPU clock */
	tmp = (0xFF << UVD_VCPU_CNTL__PRB_TIMEOUT_VAL__SHIFT);
	tmp |= UVD_VCPU_CNTL__CLK_EN_MASK | UVD_VCPU_CNTL__BLK_RST_MASK;
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, 0, regUVD_VCPU_CNTL), tmp, 0, indirect);

	/* disable master interrupt */
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
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
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, 0, regUVD_LMI_CTRL), tmp, 0, indirect);

	vcn_v5_0_1_mc_resume_dpg_mode(adev, inst_idx, indirect);

	tmp = (0xFF << UVD_VCPU_CNTL__PRB_TIMEOUT_VAL__SHIFT);
	tmp |= UVD_VCPU_CNTL__CLK_EN_MASK;
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, 0, regUVD_VCPU_CNTL), tmp, 0, indirect);

	/* enable LMI MC and UMC channels */
	tmp = 0x1f << UVD_LMI_CTRL2__RE_OFLD_MIF_WR_REQ_NUM__SHIFT;
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, 0, regUVD_LMI_CTRL2), tmp, 0, indirect);

	/* enable master interrupt */
	WREG32_SOC24_DPG_MODE(inst_idx, SOC24_DPG_MODE_OFFSET(
		VCN, 0, regUVD_MASTINT_EN),
		UVD_MASTINT_EN__VCPU_EN_MASK, 0, indirect);

	if (indirect)
		amdgpu_vcn_psp_update_sram(adev, inst_idx, AMDGPU_UCODE_ID_VCN0_RAM);

	ring = &adev->vcn.inst[inst_idx].ring_enc[0];

	WREG32_SOC15(VCN, vcn_inst, regUVD_RB_BASE_LO, lower_32_bits(ring->gpu_addr));
	WREG32_SOC15(VCN, vcn_inst, regUVD_RB_BASE_HI, upper_32_bits(ring->gpu_addr));
	WREG32_SOC15(VCN, vcn_inst, regUVD_RB_SIZE, ring->ring_size / sizeof(uint32_t));

	tmp = RREG32_SOC15(VCN, vcn_inst, regVCN_RB_ENABLE);
	tmp &= ~(VCN_RB_ENABLE__RB1_EN_MASK);
	WREG32_SOC15(VCN, vcn_inst, regVCN_RB_ENABLE, tmp);
	fw_shared->sq.queue_mode |= FW_QUEUE_RING_RESET;
	WREG32_SOC15(VCN, vcn_inst, regUVD_RB_RPTR, 0);
	WREG32_SOC15(VCN, vcn_inst, regUVD_RB_WPTR, 0);

	tmp = RREG32_SOC15(VCN, vcn_inst, regUVD_RB_RPTR);
	WREG32_SOC15(VCN, vcn_inst, regUVD_RB_WPTR, tmp);
	ring->wptr = RREG32_SOC15(VCN, vcn_inst, regUVD_RB_WPTR);

	tmp = RREG32_SOC15(VCN, vcn_inst, regVCN_RB_ENABLE);
	tmp |= VCN_RB_ENABLE__RB1_EN_MASK;
	WREG32_SOC15(VCN, vcn_inst, regVCN_RB_ENABLE, tmp);
	fw_shared->sq.queue_mode &= ~(FW_QUEUE_RING_RESET | FW_QUEUE_DPG_HOLD_OFF);

	WREG32_SOC15(VCN, vcn_inst, regVCN_RB1_DB_CTRL,
		ring->doorbell_index << VCN_RB1_DB_CTRL__OFFSET__SHIFT |
		VCN_RB1_DB_CTRL__EN_MASK);
	/* Read DB_CTRL to flush the write DB_CTRL command. */
	RREG32_SOC15(VCN, vcn_inst, regVCN_RB1_DB_CTRL);

	return 0;
}

/**
 * vcn_v5_0_1_start - VCN start
 *
 * @adev: amdgpu_device pointer
 *
 * Start VCN block
 */
static int vcn_v5_0_1_start(struct amdgpu_device *adev)
{
	volatile struct amdgpu_vcn4_fw_shared *fw_shared;
	struct amdgpu_ring *ring;
	uint32_t tmp;
	int i, j, k, r, vcn_inst;

	if (adev->pm.dpm_enabled)
		amdgpu_dpm_enable_uvd(adev, true);

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		fw_shared = adev->vcn.inst[i].fw_shared.cpu_addr;

		if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG) {
			r = vcn_v5_0_1_start_dpg_mode(adev, i, adev->vcn.indirect_sram);
			continue;
		}

		vcn_inst = GET_INST(VCN, i);

		/* set VCN status busy */
		tmp = RREG32_SOC15(VCN, vcn_inst, regUVD_STATUS) | UVD_STATUS__UVD_BUSY;
		WREG32_SOC15(VCN, vcn_inst, regUVD_STATUS, tmp);

		/* enable VCPU clock */
		WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_VCPU_CNTL),
			UVD_VCPU_CNTL__CLK_EN_MASK, ~UVD_VCPU_CNTL__CLK_EN_MASK);

		/* disable master interrupt */
		WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_MASTINT_EN), 0,
			~UVD_MASTINT_EN__VCPU_EN_MASK);

		/* enable LMI MC and UMC channels */
		WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_LMI_CTRL2), 0,
			~UVD_LMI_CTRL2__STALL_ARB_UMC_MASK);

		tmp = RREG32_SOC15(VCN, vcn_inst, regUVD_SOFT_RESET);
		tmp &= ~UVD_SOFT_RESET__LMI_SOFT_RESET_MASK;
		tmp &= ~UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK;
		WREG32_SOC15(VCN, vcn_inst, regUVD_SOFT_RESET, tmp);

		/* setup regUVD_LMI_CTRL */
		tmp = RREG32_SOC15(VCN, vcn_inst, regUVD_LMI_CTRL);
		WREG32_SOC15(VCN, vcn_inst, regUVD_LMI_CTRL, tmp |
			UVD_LMI_CTRL__WRITE_CLEAN_TIMER_EN_MASK |
			UVD_LMI_CTRL__MASK_MC_URGENT_MASK |
			UVD_LMI_CTRL__DATA_COHERENCY_EN_MASK |
			UVD_LMI_CTRL__VCPU_DATA_COHERENCY_EN_MASK);

		vcn_v5_0_1_mc_resume(adev, i);

		/* VCN global tiling registers */
		WREG32_SOC15(VCN, vcn_inst, regUVD_GFX10_ADDR_CONFIG,
			adev->gfx.config.gb_addr_config);

		/* unblock VCPU register access */
		WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_RB_ARB_CTRL), 0,
			~UVD_RB_ARB_CTRL__VCPU_DIS_MASK);

		/* release VCPU reset to boot */
		WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_VCPU_CNTL), 0,
			~UVD_VCPU_CNTL__BLK_RST_MASK);

		for (j = 0; j < 10; ++j) {
			uint32_t status;

			for (k = 0; k < 100; ++k) {
				status = RREG32_SOC15(VCN, vcn_inst, regUVD_STATUS);
				if (status & 2)
					break;
				mdelay(100);
				if (amdgpu_emu_mode == 1)
					msleep(20);
			}

			if (amdgpu_emu_mode == 1) {
				r = -1;
				if (status & 2) {
					r = 0;
					break;
				}
			} else {
				r = 0;
				if (status & 2)
					break;

				dev_err(adev->dev,
				    "VCN[%d] is not responding, trying to reset the VCPU!!!\n", i);
				WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_VCPU_CNTL),
							UVD_VCPU_CNTL__BLK_RST_MASK,
							~UVD_VCPU_CNTL__BLK_RST_MASK);
				mdelay(10);
				WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_VCPU_CNTL), 0,
							~UVD_VCPU_CNTL__BLK_RST_MASK);

				mdelay(10);
				r = -1;
			}
		}

		if (r) {
			dev_err(adev->dev, "VCN[%d] is not responding, giving up!!!\n", i);
			return r;
		}

		/* enable master interrupt */
		WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_MASTINT_EN),
				UVD_MASTINT_EN__VCPU_EN_MASK,
				~UVD_MASTINT_EN__VCPU_EN_MASK);

		/* clear the busy bit of VCN_STATUS */
		WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_STATUS), 0,
			~(2 << UVD_STATUS__VCPU_REPORT__SHIFT));

		ring = &adev->vcn.inst[i].ring_enc[0];

		WREG32_SOC15(VCN, vcn_inst, regVCN_RB1_DB_CTRL,
			ring->doorbell_index << VCN_RB1_DB_CTRL__OFFSET__SHIFT |
			VCN_RB1_DB_CTRL__EN_MASK);

		/* Read DB_CTRL to flush the write DB_CTRL command. */
		RREG32_SOC15(VCN, vcn_inst, regVCN_RB1_DB_CTRL);

		WREG32_SOC15(VCN, vcn_inst, regUVD_RB_BASE_LO, ring->gpu_addr);
		WREG32_SOC15(VCN, vcn_inst, regUVD_RB_BASE_HI, upper_32_bits(ring->gpu_addr));
		WREG32_SOC15(VCN, vcn_inst, regUVD_RB_SIZE, ring->ring_size / 4);

		tmp = RREG32_SOC15(VCN, vcn_inst, regVCN_RB_ENABLE);
		tmp &= ~(VCN_RB_ENABLE__RB1_EN_MASK);
		WREG32_SOC15(VCN, vcn_inst, regVCN_RB_ENABLE, tmp);
		fw_shared->sq.queue_mode |= FW_QUEUE_RING_RESET;
		WREG32_SOC15(VCN, vcn_inst, regUVD_RB_RPTR, 0);
		WREG32_SOC15(VCN, vcn_inst, regUVD_RB_WPTR, 0);

		tmp = RREG32_SOC15(VCN, vcn_inst, regUVD_RB_RPTR);
		WREG32_SOC15(VCN, vcn_inst, regUVD_RB_WPTR, tmp);
		ring->wptr = RREG32_SOC15(VCN, vcn_inst, regUVD_RB_WPTR);

		tmp = RREG32_SOC15(VCN, vcn_inst, regVCN_RB_ENABLE);
		tmp |= VCN_RB_ENABLE__RB1_EN_MASK;
		WREG32_SOC15(VCN, vcn_inst, regVCN_RB_ENABLE, tmp);
		fw_shared->sq.queue_mode &= ~(FW_QUEUE_RING_RESET | FW_QUEUE_DPG_HOLD_OFF);
	}

	return 0;
}

/**
 * vcn_v5_0_1_stop_dpg_mode - VCN stop with dpg mode
 *
 * @adev: amdgpu_device pointer
 * @inst_idx: instance number index
 *
 * Stop VCN block with dpg mode
 */
static void vcn_v5_0_1_stop_dpg_mode(struct amdgpu_device *adev, int inst_idx)
{
	uint32_t tmp;
	int vcn_inst;

	vcn_inst = GET_INST(VCN, inst_idx);

	/* Wait for power status to be 1 */
	SOC15_WAIT_ON_RREG(VCN, vcn_inst, regUVD_POWER_STATUS, 1,
		UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);

	/* wait for read ptr to be equal to write ptr */
	tmp = RREG32_SOC15(VCN, vcn_inst, regUVD_RB_WPTR);
	SOC15_WAIT_ON_RREG(VCN, vcn_inst, regUVD_RB_RPTR, tmp, 0xFFFFFFFF);

	/* disable dynamic power gating mode */
	WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_POWER_STATUS), 0,
		~UVD_POWER_STATUS__UVD_PG_MODE_MASK);
}

/**
 * vcn_v5_0_1_stop - VCN stop
 *
 * @adev: amdgpu_device pointer
 *
 * Stop VCN block
 */
static int vcn_v5_0_1_stop(struct amdgpu_device *adev)
{
	volatile struct amdgpu_vcn4_fw_shared *fw_shared;
	uint32_t tmp;
	int i, r = 0, vcn_inst;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		vcn_inst = GET_INST(VCN, i);

		fw_shared = adev->vcn.inst[i].fw_shared.cpu_addr;
		fw_shared->sq.queue_mode |= FW_QUEUE_DPG_HOLD_OFF;

		if (adev->pg_flags & AMD_PG_SUPPORT_VCN_DPG) {
			vcn_v5_0_1_stop_dpg_mode(adev, i);
			continue;
		}

		/* wait for vcn idle */
		r = SOC15_WAIT_ON_RREG(VCN, vcn_inst, regUVD_STATUS, UVD_STATUS__IDLE, 0x7);
		if (r)
			return r;

		tmp = UVD_LMI_STATUS__VCPU_LMI_WRITE_CLEAN_MASK |
		      UVD_LMI_STATUS__READ_CLEAN_MASK |
		      UVD_LMI_STATUS__WRITE_CLEAN_MASK |
		      UVD_LMI_STATUS__WRITE_CLEAN_RAW_MASK;
		r = SOC15_WAIT_ON_RREG(VCN, vcn_inst, regUVD_LMI_STATUS, tmp, tmp);
		if (r)
			return r;

		/* disable LMI UMC channel */
		tmp = RREG32_SOC15(VCN, vcn_inst, regUVD_LMI_CTRL2);
		tmp |= UVD_LMI_CTRL2__STALL_ARB_UMC_MASK;
		WREG32_SOC15(VCN, vcn_inst, regUVD_LMI_CTRL2, tmp);
		tmp = UVD_LMI_STATUS__UMC_READ_CLEAN_RAW_MASK |
		      UVD_LMI_STATUS__UMC_WRITE_CLEAN_RAW_MASK;
		r = SOC15_WAIT_ON_RREG(VCN, vcn_inst, regUVD_LMI_STATUS, tmp, tmp);
		if (r)
			return r;

		/* block VCPU register access */
		WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_RB_ARB_CTRL),
			UVD_RB_ARB_CTRL__VCPU_DIS_MASK,
			~UVD_RB_ARB_CTRL__VCPU_DIS_MASK);

		/* reset VCPU */
		WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_VCPU_CNTL),
			UVD_VCPU_CNTL__BLK_RST_MASK,
			~UVD_VCPU_CNTL__BLK_RST_MASK);

		/* disable VCPU clock */
		WREG32_P(SOC15_REG_OFFSET(VCN, vcn_inst, regUVD_VCPU_CNTL), 0,
			~(UVD_VCPU_CNTL__CLK_EN_MASK));

		/* apply soft reset */
		tmp = RREG32_SOC15(VCN, vcn_inst, regUVD_SOFT_RESET);
		tmp |= UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK;
		WREG32_SOC15(VCN, vcn_inst, regUVD_SOFT_RESET, tmp);
		tmp = RREG32_SOC15(VCN, vcn_inst, regUVD_SOFT_RESET);
		tmp |= UVD_SOFT_RESET__LMI_SOFT_RESET_MASK;
		WREG32_SOC15(VCN, vcn_inst, regUVD_SOFT_RESET, tmp);

		/* clear status */
		WREG32_SOC15(VCN, vcn_inst, regUVD_STATUS, 0);
	}

	if (adev->pm.dpm_enabled)
		amdgpu_dpm_enable_uvd(adev, false);

	return 0;
}

/**
 * vcn_v5_0_1_unified_ring_get_rptr - get unified read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware unified read pointer
 */
static uint64_t vcn_v5_0_1_unified_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring != &adev->vcn.inst[ring->me].ring_enc[0])
		DRM_ERROR("wrong ring id is identified in %s", __func__);

	return RREG32_SOC15(VCN, GET_INST(VCN, ring->me), regUVD_RB_RPTR);
}

/**
 * vcn_v5_0_1_unified_ring_get_wptr - get unified write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware unified write pointer
 */
static uint64_t vcn_v5_0_1_unified_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring != &adev->vcn.inst[ring->me].ring_enc[0])
		DRM_ERROR("wrong ring id is identified in %s", __func__);

	if (ring->use_doorbell)
		return *ring->wptr_cpu_addr;
	else
		return RREG32_SOC15(VCN, GET_INST(VCN, ring->me), regUVD_RB_WPTR);
}

/**
 * vcn_v5_0_1_unified_ring_set_wptr - set enc write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the enc write pointer to the hardware
 */
static void vcn_v5_0_1_unified_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring != &adev->vcn.inst[ring->me].ring_enc[0])
		DRM_ERROR("wrong ring id is identified in %s", __func__);

	if (ring->use_doorbell) {
		*ring->wptr_cpu_addr = lower_32_bits(ring->wptr);
		WDOORBELL32(ring->doorbell_index, lower_32_bits(ring->wptr));
	} else {
		WREG32_SOC15(VCN, GET_INST(VCN, ring->me), regUVD_RB_WPTR,
				lower_32_bits(ring->wptr));
	}
}

static const struct amdgpu_ring_funcs vcn_v5_0_1_unified_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_ENC,
	.align_mask = 0x3f,
	.nop = VCN_ENC_CMD_NO_OP,
	.get_rptr = vcn_v5_0_1_unified_ring_get_rptr,
	.get_wptr = vcn_v5_0_1_unified_ring_get_wptr,
	.set_wptr = vcn_v5_0_1_unified_ring_set_wptr,
	.emit_frame_size =
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 3 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 4 +
		4 + /* vcn_v2_0_enc_ring_emit_vm_flush */
		5 + 5 + /* vcn_v2_0_enc_ring_emit_fence x2 vm fence */
		1, /* vcn_v2_0_enc_ring_insert_end */
	.emit_ib_size = 5, /* vcn_v2_0_enc_ring_emit_ib */
	.emit_ib = vcn_v2_0_enc_ring_emit_ib,
	.emit_fence = vcn_v2_0_enc_ring_emit_fence,
	.emit_vm_flush = vcn_v2_0_enc_ring_emit_vm_flush,
	.test_ring = amdgpu_vcn_enc_ring_test_ring,
	.test_ib = amdgpu_vcn_unified_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.insert_end = vcn_v2_0_enc_ring_insert_end,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_vcn_ring_begin_use,
	.end_use = amdgpu_vcn_ring_end_use,
	.emit_wreg = vcn_v2_0_enc_ring_emit_wreg,
	.emit_reg_wait = vcn_v2_0_enc_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
};

/**
 * vcn_v5_0_1_set_unified_ring_funcs - set unified ring functions
 *
 * @adev: amdgpu_device pointer
 *
 * Set unified ring functions
 */
static void vcn_v5_0_1_set_unified_ring_funcs(struct amdgpu_device *adev)
{
	int i, vcn_inst;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		adev->vcn.inst[i].ring_enc[0].funcs = &vcn_v5_0_1_unified_ring_vm_funcs;
		adev->vcn.inst[i].ring_enc[0].me = i;
		vcn_inst = GET_INST(VCN, i);
		adev->vcn.inst[i].aid_id = vcn_inst / adev->vcn.num_inst_per_aid;
	}
}

/**
 * vcn_v5_0_1_is_idle - check VCN block is idle
 *
 * @handle: amdgpu_device pointer
 *
 * Check whether VCN block is idle
 */
static bool vcn_v5_0_1_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i, ret = 1;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i)
		ret &= (RREG32_SOC15(VCN, GET_INST(VCN, i), regUVD_STATUS) == UVD_STATUS__IDLE);

	return ret;
}

/**
 * vcn_v5_0_1_wait_for_idle - wait for VCN block idle
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 *
 * Wait for VCN block idle
 */
static int vcn_v5_0_1_wait_for_idle(struct amdgpu_ip_block *ip_block)
{
	struct amdgpu_device *adev = ip_block->adev;
	int i, ret = 0;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		ret = SOC15_WAIT_ON_RREG(VCN, GET_INST(VCN, i), regUVD_STATUS, UVD_STATUS__IDLE,
			UVD_STATUS__IDLE);
		if (ret)
			return ret;
	}

	return ret;
}

/**
 * vcn_v5_0_1_set_clockgating_state - set VCN block clockgating state
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 * @state: clock gating state
 *
 * Set VCN block clockgating state
 */
static int vcn_v5_0_1_set_clockgating_state(struct amdgpu_ip_block *ip_block,
					    enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = ip_block->adev;
	bool enable = state == AMD_CG_STATE_GATE;
	int i;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		if (enable) {
			if (RREG32_SOC15(VCN, GET_INST(VCN, i), regUVD_STATUS) != UVD_STATUS__IDLE)
				return -EBUSY;
			vcn_v5_0_1_enable_clock_gating(adev, i);
		} else {
			vcn_v5_0_1_disable_clock_gating(adev, i);
		}
	}

	return 0;
}

/**
 * vcn_v5_0_1_set_powergating_state - set VCN block powergating state
 *
 * @ip_block: Pointer to the amdgpu_ip_block for this hw instance.
 * @state: power gating state
 *
 * Set VCN block powergating state
 */
static int vcn_v5_0_1_set_powergating_state(struct amdgpu_ip_block *ip_block,
					    enum amd_powergating_state state)
{
	struct amdgpu_device *adev = ip_block->adev;
	int ret;

	if (state == adev->vcn.cur_state)
		return 0;

	if (state == AMD_PG_STATE_GATE)
		ret = vcn_v5_0_1_stop(adev);
	else
		ret = vcn_v5_0_1_start(adev);

	if (!ret)
		adev->vcn.cur_state = state;

	return ret;
}

/**
 * vcn_v5_0_1_process_interrupt - process VCN block interrupt
 *
 * @adev: amdgpu_device pointer
 * @source: interrupt sources
 * @entry: interrupt entry from clients and sources
 *
 * Process VCN block interrupt
 */
static int vcn_v5_0_1_process_interrupt(struct amdgpu_device *adev, struct amdgpu_irq_src *source,
	struct amdgpu_iv_entry *entry)
{
	uint32_t i, inst;

	i = node_id_to_phys_map[entry->node_id];

	DRM_DEV_DEBUG(adev->dev, "IH: VCN TRAP\n");

	for (inst = 0; inst < adev->vcn.num_vcn_inst; ++inst)
		if (adev->vcn.inst[inst].aid_id == i)
			break;
	if (inst >= adev->vcn.num_vcn_inst) {
		dev_WARN_ONCE(adev->dev, 1,
				"Interrupt received for unknown VCN instance %d",
				entry->node_id);
		return 0;
	}

	switch (entry->src_id) {
	case VCN_5_0__SRCID__UVD_ENC_GENERAL_PURPOSE:
		amdgpu_fence_process(&adev->vcn.inst[inst].ring_enc[0]);
		break;
	default:
		DRM_DEV_ERROR(adev->dev, "Unhandled interrupt: %d %d\n",
			  entry->src_id, entry->src_data[0]);
		break;
	}

	return 0;
}

static const struct amdgpu_irq_src_funcs vcn_v5_0_1_irq_funcs = {
	.process = vcn_v5_0_1_process_interrupt,
};

/**
 * vcn_v5_0_1_set_irq_funcs - set VCN block interrupt irq functions
 *
 * @adev: amdgpu_device pointer
 *
 * Set VCN block interrupt irq functions
 */
static void vcn_v5_0_1_set_irq_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->vcn.num_vcn_inst; ++i)
		adev->vcn.inst->irq.num_types++;
	adev->vcn.inst->irq.funcs = &vcn_v5_0_1_irq_funcs;
}

static const struct amd_ip_funcs vcn_v5_0_1_ip_funcs = {
	.name = "vcn_v5_0_1",
	.early_init = vcn_v5_0_1_early_init,
	.late_init = NULL,
	.sw_init = vcn_v5_0_1_sw_init,
	.sw_fini = vcn_v5_0_1_sw_fini,
	.hw_init = vcn_v5_0_1_hw_init,
	.hw_fini = vcn_v5_0_1_hw_fini,
	.suspend = vcn_v5_0_1_suspend,
	.resume = vcn_v5_0_1_resume,
	.is_idle = vcn_v5_0_1_is_idle,
	.wait_for_idle = vcn_v5_0_1_wait_for_idle,
	.check_soft_reset = NULL,
	.pre_soft_reset = NULL,
	.soft_reset = NULL,
	.post_soft_reset = NULL,
	.set_clockgating_state = vcn_v5_0_1_set_clockgating_state,
	.set_powergating_state = vcn_v5_0_1_set_powergating_state,
	.dump_ip_state = vcn_v5_0_0_dump_ip_state,
	.print_ip_state = vcn_v5_0_0_print_ip_state,
};

const struct amdgpu_ip_block_version vcn_v5_0_1_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_VCN,
	.major = 5,
	.minor = 0,
	.rev = 1,
	.funcs = &vcn_v5_0_1_ip_funcs,
};
