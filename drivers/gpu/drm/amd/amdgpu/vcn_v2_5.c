/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_vcn.h"
#include "soc15.h"
#include "soc15d.h"
#include "vcn_v2_0.h"

#include "vcn/vcn_2_5_offset.h"
#include "vcn/vcn_2_5_sh_mask.h"
#include "ivsrcid/vcn/irqsrcs_vcn_2_0.h"

#define mmUVD_CONTEXT_ID_INTERNAL_OFFSET			0x27
#define mmUVD_GPCOM_VCPU_CMD_INTERNAL_OFFSET			0x0f
#define mmUVD_GPCOM_VCPU_DATA0_INTERNAL_OFFSET			0x10
#define mmUVD_GPCOM_VCPU_DATA1_INTERNAL_OFFSET			0x11
#define mmUVD_NO_OP_INTERNAL_OFFSET				0x29
#define mmUVD_GP_SCRATCH8_INTERNAL_OFFSET			0x66
#define mmUVD_SCRATCH9_INTERNAL_OFFSET				0xc01d

#define mmUVD_LMI_RBC_IB_VMID_INTERNAL_OFFSET			0x431
#define mmUVD_LMI_RBC_IB_64BIT_BAR_LOW_INTERNAL_OFFSET		0x3b4
#define mmUVD_LMI_RBC_IB_64BIT_BAR_HIGH_INTERNAL_OFFSET 	0x3b5
#define mmUVD_RBC_IB_SIZE_INTERNAL_OFFSET			0x25c

#define mmUVD_JPEG_PITCH_INTERNAL_OFFSET			0x401f

static void vcn_v2_5_set_dec_ring_funcs(struct amdgpu_device *adev);
static void vcn_v2_5_set_enc_ring_funcs(struct amdgpu_device *adev);
static void vcn_v2_5_set_jpeg_ring_funcs(struct amdgpu_device *adev);
static void vcn_v2_5_set_irq_funcs(struct amdgpu_device *adev);
static int vcn_v2_5_set_powergating_state(void *handle,
				enum amd_powergating_state state);

/**
 * vcn_v2_5_early_init - set function pointers
 *
 * @handle: amdgpu_device pointer
 *
 * Set ring and irq function pointers
 */
static int vcn_v2_5_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->vcn.num_enc_rings = 2;

	vcn_v2_5_set_dec_ring_funcs(adev);
	vcn_v2_5_set_enc_ring_funcs(adev);
	vcn_v2_5_set_jpeg_ring_funcs(adev);
	vcn_v2_5_set_irq_funcs(adev);

	return 0;
}

/**
 * vcn_v2_5_sw_init - sw init for VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Load firmware and sw initialization
 */
static int vcn_v2_5_sw_init(void *handle)
{
	struct amdgpu_ring *ring;
	int i, r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* VCN DEC TRAP */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_VCN,
			VCN_2_0__SRCID__UVD_SYSTEM_MESSAGE_INTERRUPT, &adev->vcn.irq);
	if (r)
		return r;

	/* VCN ENC TRAP */
	for (i = 0; i < adev->vcn.num_enc_rings; ++i) {
		r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_VCN,
			i + VCN_2_0__SRCID__UVD_ENC_GENERAL_PURPOSE, &adev->vcn.irq);
		if (r)
			return r;
	}

	/* VCN JPEG TRAP */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_VCN,
			VCN_2_0__SRCID__JPEG_DECODE, &adev->vcn.irq);
	if (r)
		return r;

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
		DRM_INFO("PSP loading VCN firmware\n");
	}

	r = amdgpu_vcn_resume(adev);
	if (r)
		return r;

	ring = &adev->vcn.ring_dec;
	sprintf(ring->name, "vcn_dec");
	r = amdgpu_ring_init(adev, ring, 512, &adev->vcn.irq, 0);
	if (r)
		return r;

	adev->vcn.internal.context_id = mmUVD_CONTEXT_ID_INTERNAL_OFFSET;
	adev->vcn.internal.ib_vmid = mmUVD_LMI_RBC_IB_VMID_INTERNAL_OFFSET;
	adev->vcn.internal.ib_bar_low = mmUVD_LMI_RBC_IB_64BIT_BAR_LOW_INTERNAL_OFFSET;
	adev->vcn.internal.ib_bar_high = mmUVD_LMI_RBC_IB_64BIT_BAR_HIGH_INTERNAL_OFFSET;
	adev->vcn.internal.ib_size = mmUVD_RBC_IB_SIZE_INTERNAL_OFFSET;
	adev->vcn.internal.gp_scratch8 = mmUVD_GP_SCRATCH8_INTERNAL_OFFSET;

	adev->vcn.internal.scratch9 = mmUVD_SCRATCH9_INTERNAL_OFFSET;
	adev->vcn.external.scratch9 = SOC15_REG_OFFSET(UVD, 0, mmUVD_SCRATCH9);
	adev->vcn.internal.data0 = mmUVD_GPCOM_VCPU_DATA0_INTERNAL_OFFSET;
	adev->vcn.external.data0 = SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA0);
	adev->vcn.internal.data1 = mmUVD_GPCOM_VCPU_DATA1_INTERNAL_OFFSET;
	adev->vcn.external.data1 = SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA1);
	adev->vcn.internal.cmd = mmUVD_GPCOM_VCPU_CMD_INTERNAL_OFFSET;
	adev->vcn.external.cmd = SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_CMD);
	adev->vcn.internal.nop = mmUVD_NO_OP_INTERNAL_OFFSET;
	adev->vcn.external.nop = SOC15_REG_OFFSET(UVD, 0, mmUVD_NO_OP);

	for (i = 0; i < adev->vcn.num_enc_rings; ++i) {
		ring = &adev->vcn.ring_enc[i];
		sprintf(ring->name, "vcn_enc%d", i);
		r = amdgpu_ring_init(adev, ring, 512, &adev->vcn.irq, 0);
		if (r)
			return r;
	}

	ring = &adev->vcn.ring_jpeg;
	sprintf(ring->name, "vcn_jpeg");
	r = amdgpu_ring_init(adev, ring, 512, &adev->vcn.irq, 0);
	if (r)
		return r;

	adev->vcn.internal.jpeg_pitch = mmUVD_JPEG_PITCH_INTERNAL_OFFSET;
	adev->vcn.external.jpeg_pitch = SOC15_REG_OFFSET(UVD, 0, mmUVD_JPEG_PITCH);

	return 0;
}

/**
 * vcn_v2_5_sw_fini - sw fini for VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * VCN suspend and free up sw allocation
 */
static int vcn_v2_5_sw_fini(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = amdgpu_vcn_suspend(adev);
	if (r)
		return r;

	r = amdgpu_vcn_sw_fini(adev);

	return r;
}

/**
 * vcn_v2_5_hw_init - start and test VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Initialize the hardware, boot up the VCPU and do some testing
 */
static int vcn_v2_5_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct amdgpu_ring *ring = &adev->vcn.ring_dec;
	int i, r;

	r = amdgpu_ring_test_ring(ring);
	if (r) {
		ring->sched.ready = false;
		goto done;
	}

	for (i = 0; i < adev->vcn.num_enc_rings; ++i) {
		ring = &adev->vcn.ring_enc[i];
		ring->sched.ready = false;
		continue;
		r = amdgpu_ring_test_ring(ring);
		if (r) {
			ring->sched.ready = false;
			goto done;
		}
	}

	ring = &adev->vcn.ring_jpeg;
	r = amdgpu_ring_test_ring(ring);
	if (r) {
		ring->sched.ready = false;
		goto done;
	}

done:
	if (!r)
		DRM_INFO("VCN decode and encode initialized successfully.\n");

	return r;
}

/**
 * vcn_v2_5_hw_fini - stop the hardware block
 *
 * @handle: amdgpu_device pointer
 *
 * Stop the VCN block, mark ring as not ready any more
 */
static int vcn_v2_5_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct amdgpu_ring *ring = &adev->vcn.ring_dec;
	int i;

	if (RREG32_SOC15(VCN, 0, mmUVD_STATUS))
		vcn_v2_5_set_powergating_state(adev, AMD_PG_STATE_GATE);

	ring->sched.ready = false;

	for (i = 0; i < adev->vcn.num_enc_rings; ++i) {
		ring = &adev->vcn.ring_enc[i];
		ring->sched.ready = false;
	}

	ring = &adev->vcn.ring_jpeg;
	ring->sched.ready = false;

	return 0;
}

/**
 * vcn_v2_5_suspend - suspend VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * HW fini and suspend VCN block
 */
static int vcn_v2_5_suspend(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = vcn_v2_5_hw_fini(adev);
	if (r)
		return r;

	r = amdgpu_vcn_suspend(adev);

	return r;
}

/**
 * vcn_v2_5_resume - resume VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Resume firmware and hw init VCN block
 */
static int vcn_v2_5_resume(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = amdgpu_vcn_resume(adev);
	if (r)
		return r;

	r = vcn_v2_5_hw_init(adev);

	return r;
}

/**
 * vcn_v2_5_mc_resume - memory controller programming
 *
 * @adev: amdgpu_device pointer
 *
 * Let the VCN memory controller know it's offsets
 */
static void vcn_v2_5_mc_resume(struct amdgpu_device *adev)
{
	uint32_t size = AMDGPU_GPU_PAGE_ALIGN(adev->vcn.fw->size + 4);
	uint32_t offset;

	/* cache window 0: fw */
	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		WREG32_SOC15(UVD, 0, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW,
			(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN].tmr_mc_addr_lo));
		WREG32_SOC15(UVD, 0, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH,
			(adev->firmware.ucode[AMDGPU_UCODE_ID_VCN].tmr_mc_addr_hi));
		WREG32_SOC15(UVD, 0, mmUVD_VCPU_CACHE_OFFSET0, 0);
		offset = 0;
	} else {
		WREG32_SOC15(UVD, 0, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW,
			lower_32_bits(adev->vcn.gpu_addr));
		WREG32_SOC15(UVD, 0, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH,
			upper_32_bits(adev->vcn.gpu_addr));
		offset = size;
		/* No signed header for now from firmware
		WREG32_SOC15(UVD, 0, mmUVD_VCPU_CACHE_OFFSET0,
			AMDGPU_UVD_FIRMWARE_OFFSET >> 3);
		*/
		WREG32_SOC15(UVD, 0, mmUVD_VCPU_CACHE_OFFSET0, 0);
	}
	WREG32_SOC15(UVD, 0, mmUVD_VCPU_CACHE_SIZE0, size);

	/* cache window 1: stack */
	WREG32_SOC15(UVD, 0, mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW,
		lower_32_bits(adev->vcn.gpu_addr + offset));
	WREG32_SOC15(UVD, 0, mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH,
		upper_32_bits(adev->vcn.gpu_addr + offset));
	WREG32_SOC15(UVD, 0, mmUVD_VCPU_CACHE_OFFSET1, 0);
	WREG32_SOC15(UVD, 0, mmUVD_VCPU_CACHE_SIZE1, AMDGPU_VCN_STACK_SIZE);

	/* cache window 2: context */
	WREG32_SOC15(UVD, 0, mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW,
		lower_32_bits(adev->vcn.gpu_addr + offset + AMDGPU_VCN_STACK_SIZE));
	WREG32_SOC15(UVD, 0, mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH,
		upper_32_bits(adev->vcn.gpu_addr + offset + AMDGPU_VCN_STACK_SIZE));
	WREG32_SOC15(UVD, 0, mmUVD_VCPU_CACHE_OFFSET2, 0);
	WREG32_SOC15(UVD, 0, mmUVD_VCPU_CACHE_SIZE2, AMDGPU_VCN_CONTEXT_SIZE);
}

/**
 * vcn_v2_5_disable_clock_gating - disable VCN clock gating
 *
 * @adev: amdgpu_device pointer
 * @sw: enable SW clock gating
 *
 * Disable clock gating for VCN block
 */
static void vcn_v2_5_disable_clock_gating(struct amdgpu_device *adev)
{
	uint32_t data;
	int ret = 0;

	/* UVD disable CGC */
	data = RREG32_SOC15(VCN, 0, mmUVD_CGC_CTRL);
	if (adev->cg_flags & AMD_CG_SUPPORT_VCN_MGCG)
		data |= 1 << UVD_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	else
		data &= ~ UVD_CGC_CTRL__DYN_CLOCK_MODE_MASK;
	data |= 1 << UVD_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT;
	data |= 4 << UVD_CGC_CTRL__CLK_OFF_DELAY__SHIFT;
	WREG32_SOC15(VCN, 0, mmUVD_CGC_CTRL, data);

	data = RREG32_SOC15(VCN, 0, mmUVD_CGC_GATE);
	data &= ~(UVD_CGC_GATE__SYS_MASK
		| UVD_CGC_GATE__UDEC_MASK
		| UVD_CGC_GATE__MPEG2_MASK
		| UVD_CGC_GATE__REGS_MASK
		| UVD_CGC_GATE__RBC_MASK
		| UVD_CGC_GATE__LMI_MC_MASK
		| UVD_CGC_GATE__LMI_UMC_MASK
		| UVD_CGC_GATE__IDCT_MASK
		| UVD_CGC_GATE__MPRD_MASK
		| UVD_CGC_GATE__MPC_MASK
		| UVD_CGC_GATE__LBSI_MASK
		| UVD_CGC_GATE__LRBBM_MASK
		| UVD_CGC_GATE__UDEC_RE_MASK
		| UVD_CGC_GATE__UDEC_CM_MASK
		| UVD_CGC_GATE__UDEC_IT_MASK
		| UVD_CGC_GATE__UDEC_DB_MASK
		| UVD_CGC_GATE__UDEC_MP_MASK
		| UVD_CGC_GATE__WCB_MASK
		| UVD_CGC_GATE__VCPU_MASK
		| UVD_CGC_GATE__MMSCH_MASK);

	WREG32_SOC15(VCN, 0, mmUVD_CGC_GATE, data);

	SOC15_WAIT_ON_RREG(VCN, 0, mmUVD_CGC_GATE, 0,  0xFFFFFFFF, ret);

	data = RREG32_SOC15(VCN, 0, mmUVD_CGC_CTRL);
	data &= ~(UVD_CGC_CTRL__UDEC_RE_MODE_MASK
		| UVD_CGC_CTRL__UDEC_CM_MODE_MASK
		| UVD_CGC_CTRL__UDEC_IT_MODE_MASK
		| UVD_CGC_CTRL__UDEC_DB_MODE_MASK
		| UVD_CGC_CTRL__UDEC_MP_MODE_MASK
		| UVD_CGC_CTRL__SYS_MODE_MASK
		| UVD_CGC_CTRL__UDEC_MODE_MASK
		| UVD_CGC_CTRL__MPEG2_MODE_MASK
		| UVD_CGC_CTRL__REGS_MODE_MASK
		| UVD_CGC_CTRL__RBC_MODE_MASK
		| UVD_CGC_CTRL__LMI_MC_MODE_MASK
		| UVD_CGC_CTRL__LMI_UMC_MODE_MASK
		| UVD_CGC_CTRL__IDCT_MODE_MASK
		| UVD_CGC_CTRL__MPRD_MODE_MASK
		| UVD_CGC_CTRL__MPC_MODE_MASK
		| UVD_CGC_CTRL__LBSI_MODE_MASK
		| UVD_CGC_CTRL__LRBBM_MODE_MASK
		| UVD_CGC_CTRL__WCB_MODE_MASK
		| UVD_CGC_CTRL__VCPU_MODE_MASK
		| UVD_CGC_CTRL__MMSCH_MODE_MASK);
	WREG32_SOC15(VCN, 0, mmUVD_CGC_CTRL, data);

	/* turn on */
	data = RREG32_SOC15(VCN, 0, mmUVD_SUVD_CGC_GATE);
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
		| UVD_SUVD_CGC_GATE__SCLR_MASK
		| UVD_SUVD_CGC_GATE__UVD_SC_MASK
		| UVD_SUVD_CGC_GATE__ENT_MASK
		| UVD_SUVD_CGC_GATE__SIT_HEVC_DEC_MASK
		| UVD_SUVD_CGC_GATE__SIT_HEVC_ENC_MASK
		| UVD_SUVD_CGC_GATE__SITE_MASK
		| UVD_SUVD_CGC_GATE__SRE_VP9_MASK
		| UVD_SUVD_CGC_GATE__SCM_VP9_MASK
		| UVD_SUVD_CGC_GATE__SIT_VP9_DEC_MASK
		| UVD_SUVD_CGC_GATE__SDB_VP9_MASK
		| UVD_SUVD_CGC_GATE__IME_HEVC_MASK);
	WREG32_SOC15(VCN, 0, mmUVD_SUVD_CGC_GATE, data);

	data = RREG32_SOC15(VCN, 0, mmUVD_SUVD_CGC_CTRL);
	data &= ~(UVD_SUVD_CGC_CTRL__SRE_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SIT_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SMP_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SCM_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SDB_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SCLR_MODE_MASK
		| UVD_SUVD_CGC_CTRL__UVD_SC_MODE_MASK
		| UVD_SUVD_CGC_CTRL__ENT_MODE_MASK
		| UVD_SUVD_CGC_CTRL__IME_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SITE_MODE_MASK);
	WREG32_SOC15(VCN, 0, mmUVD_SUVD_CGC_CTRL, data);
}

/**
 * vcn_v2_5_enable_clock_gating - enable VCN clock gating
 *
 * @adev: amdgpu_device pointer
 * @sw: enable SW clock gating
 *
 * Enable clock gating for VCN block
 */
static void vcn_v2_5_enable_clock_gating(struct amdgpu_device *adev)
{
	uint32_t data = 0;

	/* enable UVD CGC */
	data = RREG32_SOC15(VCN, 0, mmUVD_CGC_CTRL);
	if (adev->cg_flags & AMD_CG_SUPPORT_VCN_MGCG)
		data |= 1 << UVD_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	else
		data |= 0 << UVD_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	data |= 1 << UVD_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT;
	data |= 4 << UVD_CGC_CTRL__CLK_OFF_DELAY__SHIFT;
	WREG32_SOC15(VCN, 0, mmUVD_CGC_CTRL, data);

	data = RREG32_SOC15(VCN, 0, mmUVD_CGC_CTRL);
	data |= (UVD_CGC_CTRL__UDEC_RE_MODE_MASK
		| UVD_CGC_CTRL__UDEC_CM_MODE_MASK
		| UVD_CGC_CTRL__UDEC_IT_MODE_MASK
		| UVD_CGC_CTRL__UDEC_DB_MODE_MASK
		| UVD_CGC_CTRL__UDEC_MP_MODE_MASK
		| UVD_CGC_CTRL__SYS_MODE_MASK
		| UVD_CGC_CTRL__UDEC_MODE_MASK
		| UVD_CGC_CTRL__MPEG2_MODE_MASK
		| UVD_CGC_CTRL__REGS_MODE_MASK
		| UVD_CGC_CTRL__RBC_MODE_MASK
		| UVD_CGC_CTRL__LMI_MC_MODE_MASK
		| UVD_CGC_CTRL__LMI_UMC_MODE_MASK
		| UVD_CGC_CTRL__IDCT_MODE_MASK
		| UVD_CGC_CTRL__MPRD_MODE_MASK
		| UVD_CGC_CTRL__MPC_MODE_MASK
		| UVD_CGC_CTRL__LBSI_MODE_MASK
		| UVD_CGC_CTRL__LRBBM_MODE_MASK
		| UVD_CGC_CTRL__WCB_MODE_MASK
		| UVD_CGC_CTRL__VCPU_MODE_MASK);
	WREG32_SOC15(VCN, 0, mmUVD_CGC_CTRL, data);

	data = RREG32_SOC15(VCN, 0, mmUVD_SUVD_CGC_CTRL);
	data |= (UVD_SUVD_CGC_CTRL__SRE_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SIT_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SMP_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SCM_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SDB_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SCLR_MODE_MASK
		| UVD_SUVD_CGC_CTRL__UVD_SC_MODE_MASK
		| UVD_SUVD_CGC_CTRL__ENT_MODE_MASK
		| UVD_SUVD_CGC_CTRL__IME_MODE_MASK
		| UVD_SUVD_CGC_CTRL__SITE_MODE_MASK);
	WREG32_SOC15(VCN, 0, mmUVD_SUVD_CGC_CTRL, data);
}

static int vcn_v2_5_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring = &adev->vcn.ring_dec;
	uint32_t rb_bufsz, tmp;
	int i, j, r;

	/* disable register anti-hang mechanism */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_POWER_STATUS), 0,
		~UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);

	/* set uvd status busy */
	tmp = RREG32_SOC15(UVD, 0, mmUVD_STATUS) | UVD_STATUS__UVD_BUSY;
	WREG32_SOC15(UVD, 0, mmUVD_STATUS, tmp);

	/*SW clock gating */
	vcn_v2_5_disable_clock_gating(adev);

	/* enable VCPU clock */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_VCPU_CNTL),
		UVD_VCPU_CNTL__CLK_EN_MASK, ~UVD_VCPU_CNTL__CLK_EN_MASK);

	/* disable master interrupt */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_MASTINT_EN), 0,
		~UVD_MASTINT_EN__VCPU_EN_MASK);

	/* setup mmUVD_LMI_CTRL */
	tmp = RREG32_SOC15(UVD, 0, mmUVD_LMI_CTRL);
	tmp &= ~0xff;
	WREG32_SOC15(UVD, 0, mmUVD_LMI_CTRL, tmp | 0x8|
		UVD_LMI_CTRL__WRITE_CLEAN_TIMER_EN_MASK	|
		UVD_LMI_CTRL__MASK_MC_URGENT_MASK |
		UVD_LMI_CTRL__DATA_COHERENCY_EN_MASK |
		UVD_LMI_CTRL__VCPU_DATA_COHERENCY_EN_MASK);

	/* setup mmUVD_MPC_CNTL */
	tmp = RREG32_SOC15(UVD, 0, mmUVD_MPC_CNTL);
	tmp &= ~UVD_MPC_CNTL__REPLACEMENT_MODE_MASK;
	tmp |= 0x2 << UVD_MPC_CNTL__REPLACEMENT_MODE__SHIFT;
	WREG32_SOC15(VCN, 0, mmUVD_MPC_CNTL, tmp);

	/* setup UVD_MPC_SET_MUXA0 */
	WREG32_SOC15(UVD, 0, mmUVD_MPC_SET_MUXA0,
		((0x1 << UVD_MPC_SET_MUXA0__VARA_1__SHIFT) |
		(0x2 << UVD_MPC_SET_MUXA0__VARA_2__SHIFT) |
		(0x3 << UVD_MPC_SET_MUXA0__VARA_3__SHIFT) |
		(0x4 << UVD_MPC_SET_MUXA0__VARA_4__SHIFT)));

	/* setup UVD_MPC_SET_MUXB0 */
	WREG32_SOC15(UVD, 0, mmUVD_MPC_SET_MUXB0,
		((0x1 << UVD_MPC_SET_MUXB0__VARB_1__SHIFT) |
		(0x2 << UVD_MPC_SET_MUXB0__VARB_2__SHIFT) |
		(0x3 << UVD_MPC_SET_MUXB0__VARB_3__SHIFT) |
		(0x4 << UVD_MPC_SET_MUXB0__VARB_4__SHIFT)));

	/* setup mmUVD_MPC_SET_MUX */
	WREG32_SOC15(UVD, 0, mmUVD_MPC_SET_MUX,
		((0x0 << UVD_MPC_SET_MUX__SET_0__SHIFT) |
		(0x1 << UVD_MPC_SET_MUX__SET_1__SHIFT) |
		(0x2 << UVD_MPC_SET_MUX__SET_2__SHIFT)));

	vcn_v2_5_mc_resume(adev);

	/* VCN global tiling registers */
	WREG32_SOC15(UVD, 0, mmUVD_GFX8_ADDR_CONFIG,
		adev->gfx.config.gb_addr_config);
	WREG32_SOC15(UVD, 0, mmUVD_GFX8_ADDR_CONFIG,
		adev->gfx.config.gb_addr_config);

	/* enable LMI MC and UMC channels */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_CTRL2), 0,
		~UVD_LMI_CTRL2__STALL_ARB_UMC_MASK);

	/* unblock VCPU register access */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_RB_ARB_CTRL), 0,
		~UVD_RB_ARB_CTRL__VCPU_DIS_MASK);

	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_VCPU_CNTL), 0,
		~UVD_VCPU_CNTL__BLK_RST_MASK);

	for (i = 0; i < 10; ++i) {
		uint32_t status;

		for (j = 0; j < 100; ++j) {
			status = RREG32_SOC15(UVD, 0, mmUVD_STATUS);
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

		DRM_ERROR("VCN decode not responding, trying to reset the VCPU!!!\n");
		WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_VCPU_CNTL),
			UVD_VCPU_CNTL__BLK_RST_MASK,
			~UVD_VCPU_CNTL__BLK_RST_MASK);
		mdelay(10);
		WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_VCPU_CNTL), 0,
			~UVD_VCPU_CNTL__BLK_RST_MASK);

		mdelay(10);
		r = -1;
	}

	if (r) {
		DRM_ERROR("VCN decode not responding, giving up!!!\n");
		return r;
	}

	/* enable master interrupt */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_MASTINT_EN),
		UVD_MASTINT_EN__VCPU_EN_MASK,
		~UVD_MASTINT_EN__VCPU_EN_MASK);

	/* clear the busy bit of VCN_STATUS */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_STATUS), 0,
		~(2 << UVD_STATUS__VCPU_REPORT__SHIFT));

	WREG32_SOC15(UVD, 0, mmUVD_LMI_RBC_RB_VMID, 0);

	/* force RBC into idle state */
	rb_bufsz = order_base_2(ring->ring_size);
	tmp = REG_SET_FIELD(0, UVD_RBC_RB_CNTL, RB_BUFSZ, rb_bufsz);
	tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_BLKSZ, 1);
	tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_NO_FETCH, 1);
	tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_NO_UPDATE, 1);
	tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_RPTR_WR_EN, 1);
	WREG32_SOC15(UVD, 0, mmUVD_RBC_RB_CNTL, tmp);

	/* programm the RB_BASE for ring buffer */
	WREG32_SOC15(UVD, 0, mmUVD_LMI_RBC_RB_64BIT_BAR_LOW,
		lower_32_bits(ring->gpu_addr));
	WREG32_SOC15(UVD, 0, mmUVD_LMI_RBC_RB_64BIT_BAR_HIGH,
		upper_32_bits(ring->gpu_addr));

	/* Initialize the ring buffer's read and write pointers */
	WREG32_SOC15(UVD, 0, mmUVD_RBC_RB_RPTR, 0);

	ring->wptr = RREG32_SOC15(UVD, 0, mmUVD_RBC_RB_RPTR);
	WREG32_SOC15(UVD, 0, mmUVD_RBC_RB_WPTR,
			lower_32_bits(ring->wptr));
	ring = &adev->vcn.ring_enc[0];
	WREG32_SOC15(UVD, 0, mmUVD_RB_RPTR, lower_32_bits(ring->wptr));
	WREG32_SOC15(UVD, 0, mmUVD_RB_WPTR, lower_32_bits(ring->wptr));
	WREG32_SOC15(UVD, 0, mmUVD_RB_BASE_LO, ring->gpu_addr);
	WREG32_SOC15(UVD, 0, mmUVD_RB_BASE_HI, upper_32_bits(ring->gpu_addr));
	WREG32_SOC15(UVD, 0, mmUVD_RB_SIZE, ring->ring_size / 4);

	ring = &adev->vcn.ring_enc[1];
	WREG32_SOC15(UVD, 0, mmUVD_RB_RPTR2, lower_32_bits(ring->wptr));
	WREG32_SOC15(UVD, 0, mmUVD_RB_WPTR2, lower_32_bits(ring->wptr));
	WREG32_SOC15(UVD, 0, mmUVD_RB_BASE_LO2, ring->gpu_addr);
	WREG32_SOC15(UVD, 0, mmUVD_RB_BASE_HI2, upper_32_bits(ring->gpu_addr));
	WREG32_SOC15(UVD, 0, mmUVD_RB_SIZE2, ring->ring_size / 4);

	return r;
}

static int vcn_v2_5_stop(struct amdgpu_device *adev)
{
	uint32_t tmp;
	int r;

	/* wait for vcn idle */
	SOC15_WAIT_ON_RREG(VCN, 0, mmUVD_STATUS, UVD_STATUS__IDLE, 0x7, r);
	if (r)
		return r;

	tmp = UVD_LMI_STATUS__VCPU_LMI_WRITE_CLEAN_MASK |
		UVD_LMI_STATUS__READ_CLEAN_MASK |
		UVD_LMI_STATUS__WRITE_CLEAN_MASK |
		UVD_LMI_STATUS__WRITE_CLEAN_RAW_MASK;
	SOC15_WAIT_ON_RREG(VCN, 0, mmUVD_LMI_STATUS, tmp, tmp, r);
	if (r)
		return r;

	/* block LMI UMC channel */
	tmp = RREG32_SOC15(VCN, 0, mmUVD_LMI_CTRL2);
	tmp |= UVD_LMI_CTRL2__STALL_ARB_UMC_MASK;
	WREG32_SOC15(VCN, 0, mmUVD_LMI_CTRL2, tmp);

	tmp = UVD_LMI_STATUS__UMC_READ_CLEAN_RAW_MASK|
		UVD_LMI_STATUS__UMC_WRITE_CLEAN_RAW_MASK;
	SOC15_WAIT_ON_RREG(VCN, 0, mmUVD_LMI_STATUS, tmp, tmp, r);
	if (r)
		return r;

	/* block VCPU register access */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_RB_ARB_CTRL),
		UVD_RB_ARB_CTRL__VCPU_DIS_MASK,
		~UVD_RB_ARB_CTRL__VCPU_DIS_MASK);

	/* reset VCPU */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_VCPU_CNTL),
		UVD_VCPU_CNTL__BLK_RST_MASK,
		~UVD_VCPU_CNTL__BLK_RST_MASK);

	/* disable VCPU clock */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_VCPU_CNTL), 0,
		~(UVD_VCPU_CNTL__CLK_EN_MASK));

	/* clear status */
	WREG32_SOC15(VCN, 0, mmUVD_STATUS, 0);

	vcn_v2_5_enable_clock_gating(adev);

	/* enable register anti-hang mechanism */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_POWER_STATUS),
		UVD_POWER_STATUS__UVD_POWER_STATUS_MASK,
		~UVD_POWER_STATUS__UVD_POWER_STATUS_MASK);

	return 0;
}

/**
 * vcn_v2_5_dec_ring_get_rptr - get read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware read pointer
 */
static uint64_t vcn_v2_5_dec_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32_SOC15(UVD, 0, mmUVD_RBC_RB_RPTR);
}

/**
 * vcn_v2_5_dec_ring_get_wptr - get write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware write pointer
 */
static uint64_t vcn_v2_5_dec_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32_SOC15(UVD, 0, mmUVD_RBC_RB_WPTR);
}

/**
 * vcn_v2_5_dec_ring_set_wptr - set write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the write pointer to the hardware
 */
static void vcn_v2_5_dec_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	WREG32_SOC15(UVD, 0, mmUVD_RBC_RB_WPTR, lower_32_bits(ring->wptr));
}

static const struct amdgpu_ring_funcs vcn_v2_5_dec_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_DEC,
	.align_mask = 0xf,
	.vmhub = AMDGPU_MMHUB_1,
	.get_rptr = vcn_v2_5_dec_ring_get_rptr,
	.get_wptr = vcn_v2_5_dec_ring_get_wptr,
	.set_wptr = vcn_v2_5_dec_ring_set_wptr,
	.emit_frame_size =
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 6 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 8 +
		8 + /* vcn_v2_0_dec_ring_emit_vm_flush */
		14 + 14 + /* vcn_v2_0_dec_ring_emit_fence x2 vm fence */
		6,
	.emit_ib_size = 8, /* vcn_v2_0_dec_ring_emit_ib */
	.emit_ib = vcn_v2_0_dec_ring_emit_ib,
	.emit_fence = vcn_v2_0_dec_ring_emit_fence,
	.emit_vm_flush = vcn_v2_0_dec_ring_emit_vm_flush,
	.test_ring = amdgpu_vcn_dec_ring_test_ring,
	.test_ib = amdgpu_vcn_dec_ring_test_ib,
	.insert_nop = vcn_v2_0_dec_ring_insert_nop,
	.insert_start = vcn_v2_0_dec_ring_insert_start,
	.insert_end = vcn_v2_0_dec_ring_insert_end,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_vcn_ring_begin_use,
	.end_use = amdgpu_vcn_ring_end_use,
	.emit_wreg = vcn_v2_0_dec_ring_emit_wreg,
	.emit_reg_wait = vcn_v2_0_dec_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
};

/**
 * vcn_v2_5_enc_ring_get_rptr - get enc read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware enc read pointer
 */
static uint64_t vcn_v2_5_enc_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring == &adev->vcn.ring_enc[0])
		return RREG32_SOC15(UVD, 0, mmUVD_RB_RPTR);
	else
		return RREG32_SOC15(UVD, 0, mmUVD_RB_RPTR2);
}

/**
 * vcn_v2_5_enc_ring_get_wptr - get enc write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware enc write pointer
 */
static uint64_t vcn_v2_5_enc_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring == &adev->vcn.ring_enc[0])
		return RREG32_SOC15(UVD, 0, mmUVD_RB_WPTR);
	else
		return RREG32_SOC15(UVD, 0, mmUVD_RB_WPTR2);
}

/**
 * vcn_v2_5_enc_ring_set_wptr - set enc write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the enc write pointer to the hardware
 */
static void vcn_v2_5_enc_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring == &adev->vcn.ring_enc[0])
		WREG32_SOC15(UVD, 0, mmUVD_RB_WPTR, lower_32_bits(ring->wptr));
	else
		WREG32_SOC15(UVD, 0, mmUVD_RB_WPTR2, lower_32_bits(ring->wptr));
}

static const struct amdgpu_ring_funcs vcn_v2_5_enc_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_ENC,
	.align_mask = 0x3f,
	.nop = VCN_ENC_CMD_NO_OP,
	.vmhub = AMDGPU_MMHUB_1,
	.get_rptr = vcn_v2_5_enc_ring_get_rptr,
	.get_wptr = vcn_v2_5_enc_ring_get_wptr,
	.set_wptr = vcn_v2_5_enc_ring_set_wptr,
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
	.test_ib = amdgpu_vcn_enc_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.insert_end = vcn_v2_0_enc_ring_insert_end,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_vcn_ring_begin_use,
	.end_use = amdgpu_vcn_ring_end_use,
	.emit_wreg = vcn_v2_0_enc_ring_emit_wreg,
	.emit_reg_wait = vcn_v2_0_enc_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
};

static void vcn_v2_5_set_dec_ring_funcs(struct amdgpu_device *adev)
{
	adev->vcn.ring_dec.funcs = &vcn_v2_5_dec_ring_vm_funcs;
	DRM_INFO("VCN decode is enabled in VM mode\n");
}

static void vcn_v2_5_set_enc_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->vcn.num_enc_rings; ++i)
		adev->vcn.ring_enc[i].funcs = &vcn_v2_5_enc_ring_vm_funcs;

	DRM_INFO("VCN encode is enabled in VM mode\n");
}

static bool vcn_v2_5_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return (RREG32_SOC15(VCN, 0, mmUVD_STATUS) == UVD_STATUS__IDLE);
}

static int vcn_v2_5_wait_for_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int ret = 0;

	SOC15_WAIT_ON_RREG(VCN, 0, mmUVD_STATUS, UVD_STATUS__IDLE,
		UVD_STATUS__IDLE, ret);

	return ret;
}

static int vcn_v2_5_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	bool enable = (state == AMD_CG_STATE_GATE) ? true : false;

	if (enable) {
		if (vcn_v2_5_is_idle(handle))
			return -EBUSY;
		vcn_v2_5_enable_clock_gating(adev);
	} else {
		vcn_v2_5_disable_clock_gating(adev);
	}

	return 0;
}

static int vcn_v2_5_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int ret;

	if(state == adev->vcn.cur_state)
		return 0;

	if (state == AMD_PG_STATE_GATE)
		ret = vcn_v2_5_stop(adev);
	else
		ret = vcn_v2_5_start(adev);

	if(!ret)
		adev->vcn.cur_state = state;

	return ret;
}

static int vcn_v2_5_set_interrupt_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	return 0;
}

static int vcn_v2_5_process_interrupt(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	DRM_DEBUG("IH: VCN TRAP\n");

	switch (entry->src_id) {
	case VCN_2_0__SRCID__UVD_SYSTEM_MESSAGE_INTERRUPT:
		amdgpu_fence_process(&adev->vcn.ring_dec);
		break;
	case VCN_2_0__SRCID__UVD_ENC_GENERAL_PURPOSE:
		amdgpu_fence_process(&adev->vcn.ring_enc[0]);
		break;
	case VCN_2_0__SRCID__UVD_ENC_LOW_LATENCY:
		amdgpu_fence_process(&adev->vcn.ring_enc[1]);
		break;
	case VCN_2_0__SRCID__JPEG_DECODE:
		amdgpu_fence_process(&adev->vcn.ring_jpeg);
		break;
	default:
		DRM_ERROR("Unhandled interrupt: %d %d\n",
			  entry->src_id, entry->src_data[0]);
		break;
	}

	return 0;
}

static const struct amdgpu_irq_src_funcs vcn_v2_5_irq_funcs = {
	.set = vcn_v2_5_set_interrupt_state,
	.process = vcn_v2_5_process_interrupt,
};

static void vcn_v2_5_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->vcn.irq.num_types = adev->vcn.num_enc_rings + 2;
	adev->vcn.irq.funcs = &vcn_v2_5_irq_funcs;
}

static const struct amd_ip_funcs vcn_v2_5_ip_funcs = {
	.name = "vcn_v2_5",
	.early_init = vcn_v2_5_early_init,
	.late_init = NULL,
	.sw_init = vcn_v2_5_sw_init,
	.sw_fini = vcn_v2_5_sw_fini,
	.hw_init = vcn_v2_5_hw_init,
	.hw_fini = vcn_v2_5_hw_fini,
	.suspend = vcn_v2_5_suspend,
	.resume = vcn_v2_5_resume,
	.is_idle = vcn_v2_5_is_idle,
	.wait_for_idle = vcn_v2_5_wait_for_idle,
	.check_soft_reset = NULL,
	.pre_soft_reset = NULL,
	.soft_reset = NULL,
	.post_soft_reset = NULL,
	.set_clockgating_state = vcn_v2_5_set_clockgating_state,
	.set_powergating_state = vcn_v2_5_set_powergating_state,
};

const struct amdgpu_ip_block_version vcn_v2_5_ip_block =
{
		.type = AMD_IP_BLOCK_TYPE_VCN,
		.major = 2,
		.minor = 5,
		.rev = 0,
		.funcs = &vcn_v2_5_ip_funcs,
};
