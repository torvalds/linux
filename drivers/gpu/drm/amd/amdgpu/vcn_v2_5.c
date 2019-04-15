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
	return 0;
}

static int vcn_v2_5_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	return 0;
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
