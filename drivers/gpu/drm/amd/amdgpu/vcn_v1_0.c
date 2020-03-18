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

#include <linux/firmware.h>
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_vcn.h"
#include "soc15.h"
#include "soc15d.h"
#include "soc15_common.h"

#include "vcn/vcn_1_0_offset.h"
#include "vcn/vcn_1_0_sh_mask.h"
#include "hdp/hdp_4_0_offset.h"
#include "mmhub/mmhub_9_1_offset.h"
#include "mmhub/mmhub_9_1_sh_mask.h"

#include "ivsrcid/vcn/irqsrcs_vcn_1_0.h"

static int vcn_v1_0_stop(struct amdgpu_device *adev);
static void vcn_v1_0_set_dec_ring_funcs(struct amdgpu_device *adev);
static void vcn_v1_0_set_enc_ring_funcs(struct amdgpu_device *adev);
static void vcn_v1_0_set_jpeg_ring_funcs(struct amdgpu_device *adev);
static void vcn_v1_0_set_irq_funcs(struct amdgpu_device *adev);
static void vcn_v1_0_jpeg_ring_set_patch_ring(struct amdgpu_ring *ring, uint32_t ptr);
static int vcn_v1_0_set_powergating_state(void *handle, enum amd_powergating_state state);

/**
 * vcn_v1_0_early_init - set function pointers
 *
 * @handle: amdgpu_device pointer
 *
 * Set ring and irq function pointers
 */
static int vcn_v1_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev->vcn.num_enc_rings = 2;

	vcn_v1_0_set_dec_ring_funcs(adev);
	vcn_v1_0_set_enc_ring_funcs(adev);
	vcn_v1_0_set_jpeg_ring_funcs(adev);
	vcn_v1_0_set_irq_funcs(adev);

	return 0;
}

/**
 * vcn_v1_0_sw_init - sw init for VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Load firmware and sw initialization
 */
static int vcn_v1_0_sw_init(void *handle)
{
	struct amdgpu_ring *ring;
	int i, r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* VCN DEC TRAP */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_VCN, VCN_1_0__SRCID__UVD_SYSTEM_MESSAGE_INTERRUPT, &adev->vcn.irq);
	if (r)
		return r;

	/* VCN ENC TRAP */
	for (i = 0; i < adev->vcn.num_enc_rings; ++i) {
		r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_VCN, i + VCN_1_0__SRCID__UVD_ENC_GENERAL_PURPOSE,
					&adev->vcn.irq);
		if (r)
			return r;
	}

	/* VCN JPEG TRAP */
	r = amdgpu_irq_add_id(adev, SOC15_IH_CLIENTID_VCN, 126, &adev->vcn.irq);
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

	return r;
}

/**
 * vcn_v1_0_sw_fini - sw fini for VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * VCN suspend and free up sw allocation
 */
static int vcn_v1_0_sw_fini(void *handle)
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
 * vcn_v1_0_hw_init - start and test VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Initialize the hardware, boot up the VCPU and do some testing
 */
static int vcn_v1_0_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct amdgpu_ring *ring = &adev->vcn.ring_dec;
	int i, r;

	ring->ready = true;
	r = amdgpu_ring_test_ring(ring);
	if (r) {
		ring->ready = false;
		goto done;
	}

	for (i = 0; i < adev->vcn.num_enc_rings; ++i) {
		ring = &adev->vcn.ring_enc[i];
		ring->ready = true;
		r = amdgpu_ring_test_ring(ring);
		if (r) {
			ring->ready = false;
			goto done;
		}
	}

	ring = &adev->vcn.ring_jpeg;
	ring->ready = true;
	r = amdgpu_ring_test_ring(ring);
	if (r) {
		ring->ready = false;
		goto done;
	}

done:
	if (!r)
		DRM_INFO("VCN decode and encode initialized successfully.\n");

	return r;
}

/**
 * vcn_v1_0_hw_fini - stop the hardware block
 *
 * @handle: amdgpu_device pointer
 *
 * Stop the VCN block, mark ring as not ready any more
 */
static int vcn_v1_0_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct amdgpu_ring *ring = &adev->vcn.ring_dec;

	if (RREG32_SOC15(VCN, 0, mmUVD_STATUS))
		vcn_v1_0_set_powergating_state(adev, AMD_PG_STATE_GATE);

	ring->ready = false;

	return 0;
}

/**
 * vcn_v1_0_suspend - suspend VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * HW fini and suspend VCN block
 */
static int vcn_v1_0_suspend(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = vcn_v1_0_hw_fini(adev);
	if (r)
		return r;

	r = amdgpu_vcn_suspend(adev);

	return r;
}

/**
 * vcn_v1_0_resume - resume VCN block
 *
 * @handle: amdgpu_device pointer
 *
 * Resume firmware and hw init VCN block
 */
static int vcn_v1_0_resume(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = amdgpu_vcn_resume(adev);
	if (r)
		return r;

	r = vcn_v1_0_hw_init(adev);

	return r;
}

/**
 * vcn_v1_0_mc_resume - memory controller programming
 *
 * @adev: amdgpu_device pointer
 *
 * Let the VCN memory controller know it's offsets
 */
static void vcn_v1_0_mc_resume(struct amdgpu_device *adev)
{
	uint32_t size = AMDGPU_GPU_PAGE_ALIGN(adev->vcn.fw->size + 4);
	uint32_t offset;

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
		WREG32_SOC15(UVD, 0, mmUVD_VCPU_CACHE_OFFSET0,
			     AMDGPU_UVD_FIRMWARE_OFFSET >> 3);
	}

	WREG32_SOC15(UVD, 0, mmUVD_VCPU_CACHE_SIZE0, size);

	WREG32_SOC15(UVD, 0, mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW,
		     lower_32_bits(adev->vcn.gpu_addr + offset));
	WREG32_SOC15(UVD, 0, mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH,
		     upper_32_bits(adev->vcn.gpu_addr + offset));
	WREG32_SOC15(UVD, 0, mmUVD_VCPU_CACHE_OFFSET1, 0);
	WREG32_SOC15(UVD, 0, mmUVD_VCPU_CACHE_SIZE1, AMDGPU_VCN_HEAP_SIZE);

	WREG32_SOC15(UVD, 0, mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW,
		     lower_32_bits(adev->vcn.gpu_addr + offset + AMDGPU_VCN_HEAP_SIZE));
	WREG32_SOC15(UVD, 0, mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH,
		     upper_32_bits(adev->vcn.gpu_addr + offset + AMDGPU_VCN_HEAP_SIZE));
	WREG32_SOC15(UVD, 0, mmUVD_VCPU_CACHE_OFFSET2, 0);
	WREG32_SOC15(UVD, 0, mmUVD_VCPU_CACHE_SIZE2,
			AMDGPU_VCN_STACK_SIZE + (AMDGPU_VCN_SESSION_SIZE * 40));

	WREG32_SOC15(UVD, 0, mmUVD_UDEC_ADDR_CONFIG,
			adev->gfx.config.gb_addr_config);
	WREG32_SOC15(UVD, 0, mmUVD_UDEC_DB_ADDR_CONFIG,
			adev->gfx.config.gb_addr_config);
	WREG32_SOC15(UVD, 0, mmUVD_UDEC_DBW_ADDR_CONFIG,
			adev->gfx.config.gb_addr_config);
}

/**
 * vcn_v1_0_disable_clock_gating - disable VCN clock gating
 *
 * @adev: amdgpu_device pointer
 * @sw: enable SW clock gating
 *
 * Disable clock gating for VCN block
 */
static void vcn_v1_0_disable_clock_gating(struct amdgpu_device *adev)
{
	uint32_t data;

	/* JPEG disable CGC */
	data = RREG32_SOC15(VCN, 0, mmJPEG_CGC_CTRL);

	if (adev->cg_flags & AMD_CG_SUPPORT_VCN_MGCG)
		data |= 1 << JPEG_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	else
		data &= ~JPEG_CGC_CTRL__DYN_CLOCK_MODE_MASK;

	data |= 1 << JPEG_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT;
	data |= 4 << JPEG_CGC_CTRL__CLK_OFF_DELAY__SHIFT;
	WREG32_SOC15(VCN, 0, mmJPEG_CGC_CTRL, data);

	data = RREG32_SOC15(VCN, 0, mmJPEG_CGC_GATE);
	data &= ~(JPEG_CGC_GATE__JPEG_MASK | JPEG_CGC_GATE__JPEG2_MASK);
	WREG32_SOC15(VCN, 0, mmJPEG_CGC_GATE, data);

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
		| UVD_CGC_GATE__SCPU_MASK);
	WREG32_SOC15(VCN, 0, mmUVD_CGC_GATE, data);

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
		| UVD_CGC_CTRL__SCPU_MODE_MASK);
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
 * vcn_v1_0_enable_clock_gating - enable VCN clock gating
 *
 * @adev: amdgpu_device pointer
 * @sw: enable SW clock gating
 *
 * Enable clock gating for VCN block
 */
static void vcn_v1_0_enable_clock_gating(struct amdgpu_device *adev)
{
	uint32_t data = 0;

	/* enable JPEG CGC */
	data = RREG32_SOC15(VCN, 0, mmJPEG_CGC_CTRL);
	if (adev->cg_flags & AMD_CG_SUPPORT_VCN_MGCG)
		data |= 1 << JPEG_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	else
		data |= 0 << JPEG_CGC_CTRL__DYN_CLOCK_MODE__SHIFT;
	data |= 1 << JPEG_CGC_CTRL__CLK_GATE_DLY_TIMER__SHIFT;
	data |= 4 << JPEG_CGC_CTRL__CLK_OFF_DELAY__SHIFT;
	WREG32_SOC15(VCN, 0, mmJPEG_CGC_CTRL, data);

	data = RREG32_SOC15(VCN, 0, mmJPEG_CGC_GATE);
	data |= (JPEG_CGC_GATE__JPEG_MASK | JPEG_CGC_GATE__JPEG2_MASK);
	WREG32_SOC15(VCN, 0, mmJPEG_CGC_GATE, data);

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
		| UVD_CGC_CTRL__VCPU_MODE_MASK
		| UVD_CGC_CTRL__SCPU_MODE_MASK);
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

static void vcn_1_0_disable_static_power_gating(struct amdgpu_device *adev)
{
	uint32_t data = 0;
	int ret;

	if (adev->pg_flags & AMD_PG_SUPPORT_VCN) {
		data = (1 << UVD_PGFSM_CONFIG__UVDM_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDU_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDF_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDC_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDB_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDIL_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDIR_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTD_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTE_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDE_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDW_PWR_CONFIG__SHIFT);

		WREG32_SOC15(VCN, 0, mmUVD_PGFSM_CONFIG, data);
		SOC15_WAIT_ON_RREG(VCN, 0, mmUVD_PGFSM_STATUS, UVD_PGFSM_STATUS__UVDM_UVDU_PWR_ON, 0xFFFFFF, ret);
	} else {
		data = (1 << UVD_PGFSM_CONFIG__UVDM_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDU_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDF_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDC_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDB_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDIL_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDIR_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDTD_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDTE_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDE_PWR_CONFIG__SHIFT
			| 1 << UVD_PGFSM_CONFIG__UVDW_PWR_CONFIG__SHIFT);
		WREG32_SOC15(VCN, 0, mmUVD_PGFSM_CONFIG, data);
		SOC15_WAIT_ON_RREG(VCN, 0, mmUVD_PGFSM_STATUS, 0,  0xFFFFFFFF, ret);
	}

	/* polling UVD_PGFSM_STATUS to confirm UVDM_PWR_STATUS , UVDU_PWR_STATUS are 0 (power on) */

	data = RREG32_SOC15(VCN, 0, mmUVD_POWER_STATUS);
	data &= ~0x103;
	if (adev->pg_flags & AMD_PG_SUPPORT_VCN)
		data |= UVD_PGFSM_CONFIG__UVDM_UVDU_PWR_ON | UVD_POWER_STATUS__UVD_PG_EN_MASK;

	WREG32_SOC15(VCN, 0, mmUVD_POWER_STATUS, data);
}

static void vcn_1_0_enable_static_power_gating(struct amdgpu_device *adev)
{
	uint32_t data = 0;
	int ret;

	if (adev->pg_flags & AMD_PG_SUPPORT_VCN) {
		/* Before power off, this indicator has to be turned on */
		data = RREG32_SOC15(VCN, 0, mmUVD_POWER_STATUS);
		data &= ~UVD_POWER_STATUS__UVD_POWER_STATUS_MASK;
		data |= UVD_POWER_STATUS__UVD_POWER_STATUS_TILES_OFF;
		WREG32_SOC15(VCN, 0, mmUVD_POWER_STATUS, data);


		data = (2 << UVD_PGFSM_CONFIG__UVDM_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDU_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDF_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDC_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDB_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDIL_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDIR_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTD_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDTE_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDE_PWR_CONFIG__SHIFT
			| 2 << UVD_PGFSM_CONFIG__UVDW_PWR_CONFIG__SHIFT);

		WREG32_SOC15(VCN, 0, mmUVD_PGFSM_CONFIG, data);

		data = (2 << UVD_PGFSM_STATUS__UVDM_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDU_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDF_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDC_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDB_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDIL_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDIR_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDTD_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDTE_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDE_PWR_STATUS__SHIFT
			| 2 << UVD_PGFSM_STATUS__UVDW_PWR_STATUS__SHIFT);
		SOC15_WAIT_ON_RREG(VCN, 0, mmUVD_PGFSM_STATUS, data, 0xFFFFFFFF, ret);
	}
}

/**
 * vcn_v1_0_start - start VCN block
 *
 * @adev: amdgpu_device pointer
 *
 * Setup and start the VCN block
 */
static int vcn_v1_0_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring = &adev->vcn.ring_dec;
	uint32_t rb_bufsz, tmp;
	uint32_t lmi_swap_cntl;
	int i, j, r;

	/* disable byte swapping */
	lmi_swap_cntl = 0;

	vcn_1_0_disable_static_power_gating(adev);
	/* disable clock gating */
	vcn_v1_0_disable_clock_gating(adev);

	vcn_v1_0_mc_resume(adev);

	/* disable interupt */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_MASTINT_EN), 0,
			~UVD_MASTINT_EN__VCPU_EN_MASK);

	/* stall UMC and register bus before resetting VCPU */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_CTRL2),
			UVD_LMI_CTRL2__STALL_ARB_UMC_MASK,
			~UVD_LMI_CTRL2__STALL_ARB_UMC_MASK);
	mdelay(1);

	/* put LMI, VCPU, RBC etc... into reset */
	WREG32_SOC15(UVD, 0, mmUVD_SOFT_RESET,
		UVD_SOFT_RESET__LMI_SOFT_RESET_MASK |
		UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK |
		UVD_SOFT_RESET__LBSI_SOFT_RESET_MASK |
		UVD_SOFT_RESET__RBC_SOFT_RESET_MASK |
		UVD_SOFT_RESET__CSM_SOFT_RESET_MASK |
		UVD_SOFT_RESET__CXW_SOFT_RESET_MASK |
		UVD_SOFT_RESET__TAP_SOFT_RESET_MASK |
		UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK);
	mdelay(5);

	/* initialize VCN memory controller */
	WREG32_SOC15(UVD, 0, mmUVD_LMI_CTRL,
		(0x40 << UVD_LMI_CTRL__WRITE_CLEAN_TIMER__SHIFT) |
		UVD_LMI_CTRL__WRITE_CLEAN_TIMER_EN_MASK |
		UVD_LMI_CTRL__DATA_COHERENCY_EN_MASK |
		UVD_LMI_CTRL__VCPU_DATA_COHERENCY_EN_MASK |
		UVD_LMI_CTRL__REQ_MODE_MASK |
		0x00100000L);

#ifdef __BIG_ENDIAN
	/* swap (8 in 32) RB and IB */
	lmi_swap_cntl = 0xa;
#endif
	WREG32_SOC15(UVD, 0, mmUVD_LMI_SWAP_CNTL, lmi_swap_cntl);

	WREG32_SOC15(UVD, 0, mmUVD_MPC_SET_MUXA0, 0x40c2040);
	WREG32_SOC15(UVD, 0, mmUVD_MPC_SET_MUXA1, 0x0);
	WREG32_SOC15(UVD, 0, mmUVD_MPC_SET_MUXB0, 0x40c2040);
	WREG32_SOC15(UVD, 0, mmUVD_MPC_SET_MUXB1, 0x0);
	WREG32_SOC15(UVD, 0, mmUVD_MPC_SET_ALU, 0);
	WREG32_SOC15(UVD, 0, mmUVD_MPC_SET_MUX, 0x88);

	/* take all subblocks out of reset, except VCPU */
	WREG32_SOC15(UVD, 0, mmUVD_SOFT_RESET,
			UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK);
	mdelay(5);

	/* enable VCPU clock */
	WREG32_SOC15(UVD, 0, mmUVD_VCPU_CNTL,
			UVD_VCPU_CNTL__CLK_EN_MASK);

	/* enable UMC */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_CTRL2), 0,
			~UVD_LMI_CTRL2__STALL_ARB_UMC_MASK);

	/* boot up the VCPU */
	WREG32_SOC15(UVD, 0, mmUVD_SOFT_RESET, 0);
	mdelay(10);

	for (i = 0; i < 10; ++i) {
		uint32_t status;

		for (j = 0; j < 100; ++j) {
			status = RREG32_SOC15(UVD, 0, mmUVD_STATUS);
			if (status & 2)
				break;
			mdelay(10);
		}
		r = 0;
		if (status & 2)
			break;

		DRM_ERROR("VCN decode not responding, trying to reset the VCPU!!!\n");
		WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_SOFT_RESET),
				UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK,
				~UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK);
		mdelay(10);
		WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_SOFT_RESET), 0,
				~UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK);
		mdelay(10);
		r = -1;
	}

	if (r) {
		DRM_ERROR("VCN decode not responding, giving up!!!\n");
		return r;
	}
	/* enable master interrupt */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_MASTINT_EN),
		(UVD_MASTINT_EN__VCPU_EN_MASK|UVD_MASTINT_EN__SYS_EN_MASK),
		~(UVD_MASTINT_EN__VCPU_EN_MASK|UVD_MASTINT_EN__SYS_EN_MASK));

	/* clear the bit 4 of VCN_STATUS */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_STATUS), 0,
			~(2 << UVD_STATUS__VCPU_REPORT__SHIFT));

	/* force RBC into idle state */
	rb_bufsz = order_base_2(ring->ring_size);
	tmp = REG_SET_FIELD(0, UVD_RBC_RB_CNTL, RB_BUFSZ, rb_bufsz);
	tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_BLKSZ, 1);
	tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_NO_FETCH, 1);
	tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_WPTR_POLL_EN, 0);
	tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_NO_UPDATE, 1);
	tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_RPTR_WR_EN, 1);
	WREG32_SOC15(UVD, 0, mmUVD_RBC_RB_CNTL, tmp);

	/* set the write pointer delay */
	WREG32_SOC15(UVD, 0, mmUVD_RBC_RB_WPTR_CNTL, 0);

	/* set the wb address */
	WREG32_SOC15(UVD, 0, mmUVD_RBC_RB_RPTR_ADDR,
			(upper_32_bits(ring->gpu_addr) >> 2));

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

	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_RBC_RB_CNTL), 0,
			~UVD_RBC_RB_CNTL__RB_NO_FETCH_MASK);

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

	ring = &adev->vcn.ring_jpeg;
	WREG32_SOC15(UVD, 0, mmUVD_LMI_JRBC_RB_VMID, 0);
	WREG32_SOC15(UVD, 0, mmUVD_JRBC_RB_CNTL, (0x00000001L | 0x00000002L));
	WREG32_SOC15(UVD, 0, mmUVD_LMI_JRBC_RB_64BIT_BAR_LOW, lower_32_bits(ring->gpu_addr));
	WREG32_SOC15(UVD, 0, mmUVD_LMI_JRBC_RB_64BIT_BAR_HIGH, upper_32_bits(ring->gpu_addr));
	WREG32_SOC15(UVD, 0, mmUVD_JRBC_RB_RPTR, 0);
	WREG32_SOC15(UVD, 0, mmUVD_JRBC_RB_WPTR, 0);
	WREG32_SOC15(UVD, 0, mmUVD_JRBC_RB_CNTL, 0x00000002L);

	/* initialize wptr */
	ring->wptr = RREG32_SOC15(UVD, 0, mmUVD_JRBC_RB_WPTR);

	/* copy patch commands to the jpeg ring */
	vcn_v1_0_jpeg_ring_set_patch_ring(ring,
		(ring->wptr + ring->max_dw * amdgpu_sched_hw_submission));

	return 0;
}

/**
 * vcn_v1_0_stop - stop VCN block
 *
 * @adev: amdgpu_device pointer
 *
 * stop the VCN block
 */
static int vcn_v1_0_stop(struct amdgpu_device *adev)
{
	/* force RBC into idle state */
	WREG32_SOC15(UVD, 0, mmUVD_RBC_RB_CNTL, 0x11010101);

	/* Stall UMC and register bus before resetting VCPU */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_CTRL2),
			UVD_LMI_CTRL2__STALL_ARB_UMC_MASK,
			~UVD_LMI_CTRL2__STALL_ARB_UMC_MASK);
	mdelay(1);

	/* put VCPU into reset */
	WREG32_SOC15(UVD, 0, mmUVD_SOFT_RESET,
			UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK);
	mdelay(5);

	/* disable VCPU clock */
	WREG32_SOC15(UVD, 0, mmUVD_VCPU_CNTL, 0x0);

	/* Unstall UMC and register bus */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_CTRL2), 0,
			~UVD_LMI_CTRL2__STALL_ARB_UMC_MASK);

	WREG32_SOC15(VCN, 0, mmUVD_STATUS, 0);

	vcn_v1_0_enable_clock_gating(adev);
	vcn_1_0_enable_static_power_gating(adev);
	return 0;
}

static bool vcn_v1_0_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return (RREG32_SOC15(VCN, 0, mmUVD_STATUS) == 0x2);
}

static int vcn_v1_0_wait_for_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int ret = 0;

	SOC15_WAIT_ON_RREG(VCN, 0, mmUVD_STATUS, 0x2, 0x2, ret);

	return ret;
}

static int vcn_v1_0_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	bool enable = (state == AMD_CG_STATE_GATE) ? true : false;

	if (enable) {
		/* wait for STATUS to clear */
		if (!vcn_v1_0_is_idle(handle))
			return -EBUSY;
		vcn_v1_0_enable_clock_gating(adev);
	} else {
		/* disable HW gating and enable Sw gating */
		vcn_v1_0_disable_clock_gating(adev);
	}
	return 0;
}

/**
 * vcn_v1_0_dec_ring_get_rptr - get read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware read pointer
 */
static uint64_t vcn_v1_0_dec_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32_SOC15(UVD, 0, mmUVD_RBC_RB_RPTR);
}

/**
 * vcn_v1_0_dec_ring_get_wptr - get write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware write pointer
 */
static uint64_t vcn_v1_0_dec_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32_SOC15(UVD, 0, mmUVD_RBC_RB_WPTR);
}

/**
 * vcn_v1_0_dec_ring_set_wptr - set write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the write pointer to the hardware
 */
static void vcn_v1_0_dec_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	WREG32_SOC15(UVD, 0, mmUVD_RBC_RB_WPTR, lower_32_bits(ring->wptr));
}

/**
 * vcn_v1_0_dec_ring_insert_start - insert a start command
 *
 * @ring: amdgpu_ring pointer
 *
 * Write a start command to the ring.
 */
static void vcn_v1_0_dec_ring_insert_start(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA0), 0));
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_CMD), 0));
	amdgpu_ring_write(ring, VCN_DEC_CMD_PACKET_START << 1);
}

/**
 * vcn_v1_0_dec_ring_insert_end - insert a end command
 *
 * @ring: amdgpu_ring pointer
 *
 * Write a end command to the ring.
 */
static void vcn_v1_0_dec_ring_insert_end(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_CMD), 0));
	amdgpu_ring_write(ring, VCN_DEC_CMD_PACKET_END << 1);
}

/**
 * vcn_v1_0_dec_ring_emit_fence - emit an fence & trap command
 *
 * @ring: amdgpu_ring pointer
 * @fence: fence to emit
 *
 * Write a fence and a trap command to the ring.
 */
static void vcn_v1_0_dec_ring_emit_fence(struct amdgpu_ring *ring, u64 addr, u64 seq,
				     unsigned flags)
{
	struct amdgpu_device *adev = ring->adev;

	WARN_ON(flags & AMDGPU_FENCE_FLAG_64BIT);

	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_CONTEXT_ID), 0));
	amdgpu_ring_write(ring, seq);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA0), 0));
	amdgpu_ring_write(ring, addr & 0xffffffff);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA1), 0));
	amdgpu_ring_write(ring, upper_32_bits(addr) & 0xff);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_CMD), 0));
	amdgpu_ring_write(ring, VCN_DEC_CMD_FENCE << 1);

	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA0), 0));
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA1), 0));
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_CMD), 0));
	amdgpu_ring_write(ring, VCN_DEC_CMD_TRAP << 1);
}

/**
 * vcn_v1_0_dec_ring_emit_ib - execute indirect buffer
 *
 * @ring: amdgpu_ring pointer
 * @ib: indirect buffer to execute
 *
 * Write ring commands to execute the indirect buffer
 */
static void vcn_v1_0_dec_ring_emit_ib(struct amdgpu_ring *ring,
				  struct amdgpu_ib *ib,
				  unsigned vmid, bool ctx_switch)
{
	struct amdgpu_device *adev = ring->adev;

	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_RBC_IB_VMID), 0));
	amdgpu_ring_write(ring, vmid);

	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_RBC_IB_64BIT_BAR_LOW), 0));
	amdgpu_ring_write(ring, lower_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_RBC_IB_64BIT_BAR_HIGH), 0));
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_RBC_IB_SIZE), 0));
	amdgpu_ring_write(ring, ib->length_dw);
}

static void vcn_v1_0_dec_ring_emit_reg_wait(struct amdgpu_ring *ring,
					    uint32_t reg, uint32_t val,
					    uint32_t mask)
{
	struct amdgpu_device *adev = ring->adev;

	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA0), 0));
	amdgpu_ring_write(ring, reg << 2);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA1), 0));
	amdgpu_ring_write(ring, val);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GP_SCRATCH8), 0));
	amdgpu_ring_write(ring, mask);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_CMD), 0));
	amdgpu_ring_write(ring, VCN_DEC_CMD_REG_READ_COND_WAIT << 1);
}

static void vcn_v1_0_dec_ring_emit_vm_flush(struct amdgpu_ring *ring,
					    unsigned vmid, uint64_t pd_addr)
{
	struct amdgpu_vmhub *hub = &ring->adev->vmhub[ring->funcs->vmhub];
	uint32_t data0, data1, mask;

	pd_addr = amdgpu_gmc_emit_flush_gpu_tlb(ring, vmid, pd_addr);

	/* wait for register write */
	data0 = hub->ctx0_ptb_addr_lo32 + vmid * 2;
	data1 = lower_32_bits(pd_addr);
	mask = 0xffffffff;
	vcn_v1_0_dec_ring_emit_reg_wait(ring, data0, data1, mask);
}

static void vcn_v1_0_dec_ring_emit_wreg(struct amdgpu_ring *ring,
					uint32_t reg, uint32_t val)
{
	struct amdgpu_device *adev = ring->adev;

	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA0), 0));
	amdgpu_ring_write(ring, reg << 2);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA1), 0));
	amdgpu_ring_write(ring, val);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_CMD), 0));
	amdgpu_ring_write(ring, VCN_DEC_CMD_WRITE_REG << 1);
}

/**
 * vcn_v1_0_enc_ring_get_rptr - get enc read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware enc read pointer
 */
static uint64_t vcn_v1_0_enc_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring == &adev->vcn.ring_enc[0])
		return RREG32_SOC15(UVD, 0, mmUVD_RB_RPTR);
	else
		return RREG32_SOC15(UVD, 0, mmUVD_RB_RPTR2);
}

 /**
 * vcn_v1_0_enc_ring_get_wptr - get enc write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware enc write pointer
 */
static uint64_t vcn_v1_0_enc_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring == &adev->vcn.ring_enc[0])
		return RREG32_SOC15(UVD, 0, mmUVD_RB_WPTR);
	else
		return RREG32_SOC15(UVD, 0, mmUVD_RB_WPTR2);
}

 /**
 * vcn_v1_0_enc_ring_set_wptr - set enc write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the enc write pointer to the hardware
 */
static void vcn_v1_0_enc_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring == &adev->vcn.ring_enc[0])
		WREG32_SOC15(UVD, 0, mmUVD_RB_WPTR,
			lower_32_bits(ring->wptr));
	else
		WREG32_SOC15(UVD, 0, mmUVD_RB_WPTR2,
			lower_32_bits(ring->wptr));
}

/**
 * vcn_v1_0_enc_ring_emit_fence - emit an enc fence & trap command
 *
 * @ring: amdgpu_ring pointer
 * @fence: fence to emit
 *
 * Write enc a fence and a trap command to the ring.
 */
static void vcn_v1_0_enc_ring_emit_fence(struct amdgpu_ring *ring, u64 addr,
			u64 seq, unsigned flags)
{
	WARN_ON(flags & AMDGPU_FENCE_FLAG_64BIT);

	amdgpu_ring_write(ring, VCN_ENC_CMD_FENCE);
	amdgpu_ring_write(ring, addr);
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, seq);
	amdgpu_ring_write(ring, VCN_ENC_CMD_TRAP);
}

static void vcn_v1_0_enc_ring_insert_end(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring, VCN_ENC_CMD_END);
}

/**
 * vcn_v1_0_enc_ring_emit_ib - enc execute indirect buffer
 *
 * @ring: amdgpu_ring pointer
 * @ib: indirect buffer to execute
 *
 * Write enc ring commands to execute the indirect buffer
 */
static void vcn_v1_0_enc_ring_emit_ib(struct amdgpu_ring *ring,
		struct amdgpu_ib *ib, unsigned int vmid, bool ctx_switch)
{
	amdgpu_ring_write(ring, VCN_ENC_CMD_IB);
	amdgpu_ring_write(ring, vmid);
	amdgpu_ring_write(ring, lower_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, ib->length_dw);
}

static void vcn_v1_0_enc_ring_emit_reg_wait(struct amdgpu_ring *ring,
					    uint32_t reg, uint32_t val,
					    uint32_t mask)
{
	amdgpu_ring_write(ring, VCN_ENC_CMD_REG_WAIT);
	amdgpu_ring_write(ring, reg << 2);
	amdgpu_ring_write(ring, mask);
	amdgpu_ring_write(ring, val);
}

static void vcn_v1_0_enc_ring_emit_vm_flush(struct amdgpu_ring *ring,
					    unsigned int vmid, uint64_t pd_addr)
{
	struct amdgpu_vmhub *hub = &ring->adev->vmhub[ring->funcs->vmhub];

	pd_addr = amdgpu_gmc_emit_flush_gpu_tlb(ring, vmid, pd_addr);

	/* wait for reg writes */
	vcn_v1_0_enc_ring_emit_reg_wait(ring, hub->ctx0_ptb_addr_lo32 + vmid * 2,
					lower_32_bits(pd_addr), 0xffffffff);
}

static void vcn_v1_0_enc_ring_emit_wreg(struct amdgpu_ring *ring,
					uint32_t reg, uint32_t val)
{
	amdgpu_ring_write(ring, VCN_ENC_CMD_REG_WRITE);
	amdgpu_ring_write(ring,	reg << 2);
	amdgpu_ring_write(ring, val);
}


/**
 * vcn_v1_0_jpeg_ring_get_rptr - get read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware read pointer
 */
static uint64_t vcn_v1_0_jpeg_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32_SOC15(UVD, 0, mmUVD_JRBC_RB_RPTR);
}

/**
 * vcn_v1_0_jpeg_ring_get_wptr - get write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware write pointer
 */
static uint64_t vcn_v1_0_jpeg_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32_SOC15(UVD, 0, mmUVD_JRBC_RB_WPTR);
}

/**
 * vcn_v1_0_jpeg_ring_set_wptr - set write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the write pointer to the hardware
 */
static void vcn_v1_0_jpeg_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	WREG32_SOC15(UVD, 0, mmUVD_JRBC_RB_WPTR, lower_32_bits(ring->wptr));
}

/**
 * vcn_v1_0_jpeg_ring_insert_start - insert a start command
 *
 * @ring: amdgpu_ring pointer
 *
 * Write a start command to the ring.
 */
static void vcn_v1_0_jpeg_ring_insert_start(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_JRBC_EXTERNAL_REG_BASE), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x68e04);

	amdgpu_ring_write(ring, PACKETJ(0, 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x80010000);
}

/**
 * vcn_v1_0_jpeg_ring_insert_end - insert a end command
 *
 * @ring: amdgpu_ring pointer
 *
 * Write a end command to the ring.
 */
static void vcn_v1_0_jpeg_ring_insert_end(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_JRBC_EXTERNAL_REG_BASE), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x68e04);

	amdgpu_ring_write(ring, PACKETJ(0, 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x00010000);
}

/**
 * vcn_v1_0_jpeg_ring_emit_fence - emit an fence & trap command
 *
 * @ring: amdgpu_ring pointer
 * @fence: fence to emit
 *
 * Write a fence and a trap command to the ring.
 */
static void vcn_v1_0_jpeg_ring_emit_fence(struct amdgpu_ring *ring, u64 addr, u64 seq,
				     unsigned flags)
{
	struct amdgpu_device *adev = ring->adev;

	WARN_ON(flags & AMDGPU_FENCE_FLAG_64BIT);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_JPEG_GPCOM_DATA0), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, seq);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_JPEG_GPCOM_DATA1), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, seq);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_JRBC_RB_MEM_WR_64BIT_BAR_LOW), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, lower_32_bits(addr));

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_JRBC_RB_MEM_WR_64BIT_BAR_HIGH), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, upper_32_bits(addr));

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_JPEG_GPCOM_CMD), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x8);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_JPEG_GPCOM_CMD), 0, PACKETJ_CONDITION_CHECK0, PACKETJ_TYPE4));
	amdgpu_ring_write(ring, 0);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_JRBC_RB_COND_RD_TIMER), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x01400200);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_JRBC_RB_REF_DATA), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, seq);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_LOW), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, lower_32_bits(addr));

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_HIGH), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, upper_32_bits(addr));

	amdgpu_ring_write(ring,
		PACKETJ(0, 0, PACKETJ_CONDITION_CHECK3, PACKETJ_TYPE2));
	amdgpu_ring_write(ring, 0xffffffff);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_JRBC_EXTERNAL_REG_BASE), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x3fbc);

	amdgpu_ring_write(ring,
		PACKETJ(0, 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x1);
}

/**
 * vcn_v1_0_jpeg_ring_emit_ib - execute indirect buffer
 *
 * @ring: amdgpu_ring pointer
 * @ib: indirect buffer to execute
 *
 * Write ring commands to execute the indirect buffer.
 */
static void vcn_v1_0_jpeg_ring_emit_ib(struct amdgpu_ring *ring,
				  struct amdgpu_ib *ib,
				  unsigned vmid, bool ctx_switch)
{
	struct amdgpu_device *adev = ring->adev;

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_JRBC_IB_VMID), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, (vmid | (vmid << 4)));

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_JPEG_VMID), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, (vmid | (vmid << 4)));

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_JRBC_IB_64BIT_BAR_LOW), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, lower_32_bits(ib->gpu_addr));

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_JRBC_IB_64BIT_BAR_HIGH), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_JRBC_IB_SIZE), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, ib->length_dw);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_LOW), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, lower_32_bits(ring->gpu_addr));

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_HIGH), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, upper_32_bits(ring->gpu_addr));

	amdgpu_ring_write(ring,
		PACKETJ(0, 0, PACKETJ_CONDITION_CHECK0, PACKETJ_TYPE2));
	amdgpu_ring_write(ring, 0);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_JRBC_RB_COND_RD_TIMER), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x01400200);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_JRBC_RB_REF_DATA), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x2);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_JRBC_STATUS), 0, PACKETJ_CONDITION_CHECK3, PACKETJ_TYPE3));
	amdgpu_ring_write(ring, 0x2);
}

static void vcn_v1_0_jpeg_ring_emit_reg_wait(struct amdgpu_ring *ring,
					    uint32_t reg, uint32_t val,
					    uint32_t mask)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t reg_offset = (reg << 2);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_JRBC_RB_COND_RD_TIMER), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, 0x01400200);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_JRBC_RB_REF_DATA), 0, 0, PACKETJ_TYPE0));
	amdgpu_ring_write(ring, val);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_JRBC_EXTERNAL_REG_BASE), 0, 0, PACKETJ_TYPE0));
	if (((reg_offset >= 0x1f800) && (reg_offset <= 0x21fff)) ||
		((reg_offset >= 0x1e000) && (reg_offset <= 0x1e1ff))) {
		amdgpu_ring_write(ring, 0);
		amdgpu_ring_write(ring,
			PACKETJ((reg_offset >> 2), 0, 0, PACKETJ_TYPE3));
	} else {
		amdgpu_ring_write(ring, reg_offset);
		amdgpu_ring_write(ring,
			PACKETJ(0, 0, 0, PACKETJ_TYPE3));
	}
	amdgpu_ring_write(ring, mask);
}

static void vcn_v1_0_jpeg_ring_emit_vm_flush(struct amdgpu_ring *ring,
		unsigned vmid, uint64_t pd_addr)
{
	struct amdgpu_vmhub *hub = &ring->adev->vmhub[ring->funcs->vmhub];
	uint32_t data0, data1, mask;

	pd_addr = amdgpu_gmc_emit_flush_gpu_tlb(ring, vmid, pd_addr);

	/* wait for register write */
	data0 = hub->ctx0_ptb_addr_lo32 + vmid * 2;
	data1 = lower_32_bits(pd_addr);
	mask = 0xffffffff;
	vcn_v1_0_jpeg_ring_emit_reg_wait(ring, data0, data1, mask);
}

static void vcn_v1_0_jpeg_ring_emit_wreg(struct amdgpu_ring *ring,
					uint32_t reg, uint32_t val)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t reg_offset = (reg << 2);

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_JRBC_EXTERNAL_REG_BASE), 0, 0, PACKETJ_TYPE0));
	if (((reg_offset >= 0x1f800) && (reg_offset <= 0x21fff)) ||
			((reg_offset >= 0x1e000) && (reg_offset <= 0x1e1ff))) {
		amdgpu_ring_write(ring, 0);
		amdgpu_ring_write(ring,
			PACKETJ((reg_offset >> 2), 0, 0, PACKETJ_TYPE0));
	} else {
		amdgpu_ring_write(ring, reg_offset);
		amdgpu_ring_write(ring,
			PACKETJ(0, 0, 0, PACKETJ_TYPE0));
	}
	amdgpu_ring_write(ring, val);
}

static void vcn_v1_0_jpeg_ring_nop(struct amdgpu_ring *ring, uint32_t count)
{
	int i;

	WARN_ON(ring->wptr % 2 || count % 2);

	for (i = 0; i < count / 2; i++) {
		amdgpu_ring_write(ring, PACKETJ(0, 0, 0, PACKETJ_TYPE6));
		amdgpu_ring_write(ring, 0);
	}
}

static void vcn_v1_0_jpeg_ring_patch_wreg(struct amdgpu_ring *ring, uint32_t *ptr, uint32_t reg_offset, uint32_t val)
{
	struct amdgpu_device *adev = ring->adev;
	ring->ring[(*ptr)++] = PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_JRBC_EXTERNAL_REG_BASE), 0, 0, PACKETJ_TYPE0);
	if (((reg_offset >= 0x1f800) && (reg_offset <= 0x21fff)) ||
		((reg_offset >= 0x1e000) && (reg_offset <= 0x1e1ff))) {
		ring->ring[(*ptr)++] = 0;
		ring->ring[(*ptr)++] = PACKETJ((reg_offset >> 2), 0, 0, PACKETJ_TYPE0);
	} else {
		ring->ring[(*ptr)++] = reg_offset;
		ring->ring[(*ptr)++] = PACKETJ(0, 0, 0, PACKETJ_TYPE0);
	}
	ring->ring[(*ptr)++] = val;
}

static void vcn_v1_0_jpeg_ring_set_patch_ring(struct amdgpu_ring *ring, uint32_t ptr)
{
	struct amdgpu_device *adev = ring->adev;

	uint32_t reg, reg_offset, val, mask, i;

	// 1st: program mmUVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_LOW
	reg = SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_LOW);
	reg_offset = (reg << 2);
	val = lower_32_bits(ring->gpu_addr);
	vcn_v1_0_jpeg_ring_patch_wreg(ring, &ptr, reg_offset, val);

	// 2nd: program mmUVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_HIGH
	reg = SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_JRBC_RB_MEM_RD_64BIT_BAR_HIGH);
	reg_offset = (reg << 2);
	val = upper_32_bits(ring->gpu_addr);
	vcn_v1_0_jpeg_ring_patch_wreg(ring, &ptr, reg_offset, val);

	// 3rd to 5th: issue MEM_READ commands
	for (i = 0; i <= 2; i++) {
		ring->ring[ptr++] = PACKETJ(0, 0, 0, PACKETJ_TYPE2);
		ring->ring[ptr++] = 0;
	}

	// 6th: program mmUVD_JRBC_RB_CNTL register to enable NO_FETCH and RPTR write ability
	reg = SOC15_REG_OFFSET(UVD, 0, mmUVD_JRBC_RB_CNTL);
	reg_offset = (reg << 2);
	val = 0x13;
	vcn_v1_0_jpeg_ring_patch_wreg(ring, &ptr, reg_offset, val);

	// 7th: program mmUVD_JRBC_RB_REF_DATA
	reg = SOC15_REG_OFFSET(UVD, 0, mmUVD_JRBC_RB_REF_DATA);
	reg_offset = (reg << 2);
	val = 0x1;
	vcn_v1_0_jpeg_ring_patch_wreg(ring, &ptr, reg_offset, val);

	// 8th: issue conditional register read mmUVD_JRBC_RB_CNTL
	reg = SOC15_REG_OFFSET(UVD, 0, mmUVD_JRBC_RB_CNTL);
	reg_offset = (reg << 2);
	val = 0x1;
	mask = 0x1;

	ring->ring[ptr++] = PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_JRBC_RB_COND_RD_TIMER), 0, 0, PACKETJ_TYPE0);
	ring->ring[ptr++] = 0x01400200;
	ring->ring[ptr++] = PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_JRBC_RB_REF_DATA), 0, 0, PACKETJ_TYPE0);
	ring->ring[ptr++] = val;
	ring->ring[ptr++] = PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_JRBC_EXTERNAL_REG_BASE), 0, 0, PACKETJ_TYPE0);
	if (((reg_offset >= 0x1f800) && (reg_offset <= 0x21fff)) ||
		((reg_offset >= 0x1e000) && (reg_offset <= 0x1e1ff))) {
		ring->ring[ptr++] = 0;
		ring->ring[ptr++] = PACKETJ((reg_offset >> 2), 0, 0, PACKETJ_TYPE3);
	} else {
		ring->ring[ptr++] = reg_offset;
		ring->ring[ptr++] = PACKETJ(0, 0, 0, PACKETJ_TYPE3);
	}
	ring->ring[ptr++] = mask;

	//9th to 21st: insert no-op
	for (i = 0; i <= 12; i++) {
		ring->ring[ptr++] = PACKETJ(0, 0, 0, PACKETJ_TYPE6);
		ring->ring[ptr++] = 0;
	}

	//22nd: reset mmUVD_JRBC_RB_RPTR
	reg = SOC15_REG_OFFSET(UVD, 0, mmUVD_JRBC_RB_RPTR);
	reg_offset = (reg << 2);
	val = 0;
	vcn_v1_0_jpeg_ring_patch_wreg(ring, &ptr, reg_offset, val);

	//23rd: program mmUVD_JRBC_RB_CNTL to disable no_fetch
	reg = SOC15_REG_OFFSET(UVD, 0, mmUVD_JRBC_RB_CNTL);
	reg_offset = (reg << 2);
	val = 0x12;
	vcn_v1_0_jpeg_ring_patch_wreg(ring, &ptr, reg_offset, val);
}

static int vcn_v1_0_set_interrupt_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	return 0;
}

static int vcn_v1_0_process_interrupt(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	DRM_DEBUG("IH: VCN TRAP\n");

	switch (entry->src_id) {
	case 124:
		amdgpu_fence_process(&adev->vcn.ring_dec);
		break;
	case 119:
		amdgpu_fence_process(&adev->vcn.ring_enc[0]);
		break;
	case 120:
		amdgpu_fence_process(&adev->vcn.ring_enc[1]);
		break;
	case 126:
		amdgpu_fence_process(&adev->vcn.ring_jpeg);
		break;
	default:
		DRM_ERROR("Unhandled interrupt: %d %d\n",
			  entry->src_id, entry->src_data[0]);
		break;
	}

	return 0;
}

static void vcn_v1_0_dec_ring_insert_nop(struct amdgpu_ring *ring, uint32_t count)
{
	struct amdgpu_device *adev = ring->adev;
	int i;

	WARN_ON(ring->wptr % 2 || count % 2);

	for (i = 0; i < count / 2; i++) {
		amdgpu_ring_write(ring, PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_NO_OP), 0));
		amdgpu_ring_write(ring, 0);
	}
}

static int vcn_v1_0_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	/* This doesn't actually powergate the VCN block.
	 * That's done in the dpm code via the SMC.  This
	 * just re-inits the block as necessary.  The actual
	 * gating still happens in the dpm code.  We should
	 * revisit this when there is a cleaner line between
	 * the smc and the hw blocks
	 */
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (state == AMD_PG_STATE_GATE)
		return vcn_v1_0_stop(adev);
	else
		return vcn_v1_0_start(adev);
}

static const struct amd_ip_funcs vcn_v1_0_ip_funcs = {
	.name = "vcn_v1_0",
	.early_init = vcn_v1_0_early_init,
	.late_init = NULL,
	.sw_init = vcn_v1_0_sw_init,
	.sw_fini = vcn_v1_0_sw_fini,
	.hw_init = vcn_v1_0_hw_init,
	.hw_fini = vcn_v1_0_hw_fini,
	.suspend = vcn_v1_0_suspend,
	.resume = vcn_v1_0_resume,
	.is_idle = vcn_v1_0_is_idle,
	.wait_for_idle = vcn_v1_0_wait_for_idle,
	.check_soft_reset = NULL /* vcn_v1_0_check_soft_reset */,
	.pre_soft_reset = NULL /* vcn_v1_0_pre_soft_reset */,
	.soft_reset = NULL /* vcn_v1_0_soft_reset */,
	.post_soft_reset = NULL /* vcn_v1_0_post_soft_reset */,
	.set_clockgating_state = vcn_v1_0_set_clockgating_state,
	.set_powergating_state = vcn_v1_0_set_powergating_state,
};

static const struct amdgpu_ring_funcs vcn_v1_0_dec_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_DEC,
	.align_mask = 0xf,
	.support_64bit_ptrs = false,
	.vmhub = AMDGPU_MMHUB,
	.get_rptr = vcn_v1_0_dec_ring_get_rptr,
	.get_wptr = vcn_v1_0_dec_ring_get_wptr,
	.set_wptr = vcn_v1_0_dec_ring_set_wptr,
	.emit_frame_size =
		6 + 6 + /* hdp invalidate / flush */
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 6 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 8 +
		8 + /* vcn_v1_0_dec_ring_emit_vm_flush */
		14 + 14 + /* vcn_v1_0_dec_ring_emit_fence x2 vm fence */
		6,
	.emit_ib_size = 8, /* vcn_v1_0_dec_ring_emit_ib */
	.emit_ib = vcn_v1_0_dec_ring_emit_ib,
	.emit_fence = vcn_v1_0_dec_ring_emit_fence,
	.emit_vm_flush = vcn_v1_0_dec_ring_emit_vm_flush,
	.test_ring = amdgpu_vcn_dec_ring_test_ring,
	.test_ib = amdgpu_vcn_dec_ring_test_ib,
	.insert_nop = vcn_v1_0_dec_ring_insert_nop,
	.insert_start = vcn_v1_0_dec_ring_insert_start,
	.insert_end = vcn_v1_0_dec_ring_insert_end,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_vcn_ring_begin_use,
	.end_use = amdgpu_vcn_ring_end_use,
	.emit_wreg = vcn_v1_0_dec_ring_emit_wreg,
	.emit_reg_wait = vcn_v1_0_dec_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
};

static const struct amdgpu_ring_funcs vcn_v1_0_enc_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_ENC,
	.align_mask = 0x3f,
	.nop = VCN_ENC_CMD_NO_OP,
	.support_64bit_ptrs = false,
	.vmhub = AMDGPU_MMHUB,
	.get_rptr = vcn_v1_0_enc_ring_get_rptr,
	.get_wptr = vcn_v1_0_enc_ring_get_wptr,
	.set_wptr = vcn_v1_0_enc_ring_set_wptr,
	.emit_frame_size =
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 3 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 4 +
		4 + /* vcn_v1_0_enc_ring_emit_vm_flush */
		5 + 5 + /* vcn_v1_0_enc_ring_emit_fence x2 vm fence */
		1, /* vcn_v1_0_enc_ring_insert_end */
	.emit_ib_size = 5, /* vcn_v1_0_enc_ring_emit_ib */
	.emit_ib = vcn_v1_0_enc_ring_emit_ib,
	.emit_fence = vcn_v1_0_enc_ring_emit_fence,
	.emit_vm_flush = vcn_v1_0_enc_ring_emit_vm_flush,
	.test_ring = amdgpu_vcn_enc_ring_test_ring,
	.test_ib = amdgpu_vcn_enc_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.insert_end = vcn_v1_0_enc_ring_insert_end,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_vcn_ring_begin_use,
	.end_use = amdgpu_vcn_ring_end_use,
	.emit_wreg = vcn_v1_0_enc_ring_emit_wreg,
	.emit_reg_wait = vcn_v1_0_enc_ring_emit_reg_wait,
	.emit_reg_write_reg_wait = amdgpu_ring_emit_reg_write_reg_wait_helper,
};

static const struct amdgpu_ring_funcs vcn_v1_0_jpeg_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_VCN_JPEG,
	.align_mask = 0xf,
	.nop = PACKET0(0x81ff, 0),
	.support_64bit_ptrs = false,
	.vmhub = AMDGPU_MMHUB,
	.extra_dw = 64,
	.get_rptr = vcn_v1_0_jpeg_ring_get_rptr,
	.get_wptr = vcn_v1_0_jpeg_ring_get_wptr,
	.set_wptr = vcn_v1_0_jpeg_ring_set_wptr,
	.emit_frame_size =
		6 + 6 + /* hdp invalidate / flush */
		SOC15_FLUSH_GPU_TLB_NUM_WREG * 6 +
		SOC15_FLUSH_GPU_TLB_NUM_REG_WAIT * 8 +
		8 + /* vcn_v1_0_dec_ring_emit_vm_flush */
		14 + 14 + /* vcn_v1_0_dec_ring_emit_fence x2 vm fence */
		6,
	.emit_ib_size = 22, /* vcn_v1_0_dec_ring_emit_ib */
	.emit_ib = vcn_v1_0_jpeg_ring_emit_ib,
	.emit_fence = vcn_v1_0_jpeg_ring_emit_fence,
	.emit_vm_flush = vcn_v1_0_jpeg_ring_emit_vm_flush,
	.test_ring = amdgpu_vcn_jpeg_ring_test_ring,
	.test_ib = amdgpu_vcn_jpeg_ring_test_ib,
	.insert_nop = vcn_v1_0_jpeg_ring_nop,
	.insert_start = vcn_v1_0_jpeg_ring_insert_start,
	.insert_end = vcn_v1_0_jpeg_ring_insert_end,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_vcn_ring_begin_use,
	.end_use = amdgpu_vcn_ring_end_use,
	.emit_wreg = vcn_v1_0_jpeg_ring_emit_wreg,
	.emit_reg_wait = vcn_v1_0_jpeg_ring_emit_reg_wait,
};

static void vcn_v1_0_set_dec_ring_funcs(struct amdgpu_device *adev)
{
	adev->vcn.ring_dec.funcs = &vcn_v1_0_dec_ring_vm_funcs;
	DRM_INFO("VCN decode is enabled in VM mode\n");
}

static void vcn_v1_0_set_enc_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->vcn.num_enc_rings; ++i)
		adev->vcn.ring_enc[i].funcs = &vcn_v1_0_enc_ring_vm_funcs;

	DRM_INFO("VCN encode is enabled in VM mode\n");
}

static void vcn_v1_0_set_jpeg_ring_funcs(struct amdgpu_device *adev)
{
	adev->vcn.ring_jpeg.funcs = &vcn_v1_0_jpeg_ring_vm_funcs;
	DRM_INFO("VCN jpeg decode is enabled in VM mode\n");
}

static const struct amdgpu_irq_src_funcs vcn_v1_0_irq_funcs = {
	.set = vcn_v1_0_set_interrupt_state,
	.process = vcn_v1_0_process_interrupt,
};

static void vcn_v1_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->vcn.irq.num_types = adev->vcn.num_enc_rings + 1;
	adev->vcn.irq.funcs = &vcn_v1_0_irq_funcs;
}

const struct amdgpu_ip_block_version vcn_v1_0_ip_block =
{
		.type = AMD_IP_BLOCK_TYPE_VCN,
		.major = 1,
		.minor = 0,
		.rev = 0,
		.funcs = &vcn_v1_0_ip_funcs,
};
