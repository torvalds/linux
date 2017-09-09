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
#include "amdgpu_uvd.h"
#include "soc15d.h"
#include "soc15_common.h"
#include "mmsch_v1_0.h"

#include "vega10/soc15ip.h"
#include "vega10/UVD/uvd_7_0_offset.h"
#include "vega10/UVD/uvd_7_0_sh_mask.h"
#include "vega10/VCE/vce_4_0_offset.h"
#include "vega10/VCE/vce_4_0_default.h"
#include "vega10/VCE/vce_4_0_sh_mask.h"
#include "vega10/NBIF/nbif_6_1_offset.h"
#include "vega10/HDP/hdp_4_0_offset.h"
#include "vega10/MMHUB/mmhub_1_0_offset.h"
#include "vega10/MMHUB/mmhub_1_0_sh_mask.h"

static void uvd_v7_0_set_ring_funcs(struct amdgpu_device *adev);
static void uvd_v7_0_set_enc_ring_funcs(struct amdgpu_device *adev);
static void uvd_v7_0_set_irq_funcs(struct amdgpu_device *adev);
static int uvd_v7_0_start(struct amdgpu_device *adev);
static void uvd_v7_0_stop(struct amdgpu_device *adev);
static int uvd_v7_0_sriov_start(struct amdgpu_device *adev);

/**
 * uvd_v7_0_ring_get_rptr - get read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware read pointer
 */
static uint64_t uvd_v7_0_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32_SOC15(UVD, 0, mmUVD_RBC_RB_RPTR);
}

/**
 * uvd_v7_0_enc_ring_get_rptr - get enc read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware enc read pointer
 */
static uint64_t uvd_v7_0_enc_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring == &adev->uvd.ring_enc[0])
		return RREG32_SOC15(UVD, 0, mmUVD_RB_RPTR);
	else
		return RREG32_SOC15(UVD, 0, mmUVD_RB_RPTR2);
}

/**
 * uvd_v7_0_ring_get_wptr - get write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware write pointer
 */
static uint64_t uvd_v7_0_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32_SOC15(UVD, 0, mmUVD_RBC_RB_WPTR);
}

/**
 * uvd_v7_0_enc_ring_get_wptr - get enc write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware enc write pointer
 */
static uint64_t uvd_v7_0_enc_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell)
		return adev->wb.wb[ring->wptr_offs];

	if (ring == &adev->uvd.ring_enc[0])
		return RREG32_SOC15(UVD, 0, mmUVD_RB_WPTR);
	else
		return RREG32_SOC15(UVD, 0, mmUVD_RB_WPTR2);
}

/**
 * uvd_v7_0_ring_set_wptr - set write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the write pointer to the hardware
 */
static void uvd_v7_0_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	WREG32_SOC15(UVD, 0, mmUVD_RBC_RB_WPTR, lower_32_bits(ring->wptr));
}

/**
 * uvd_v7_0_enc_ring_set_wptr - set enc write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the enc write pointer to the hardware
 */
static void uvd_v7_0_enc_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell) {
		/* XXX check if swapping is necessary on BE */
		adev->wb.wb[ring->wptr_offs] = lower_32_bits(ring->wptr);
		WDOORBELL32(ring->doorbell_index, lower_32_bits(ring->wptr));
		return;
	}

	if (ring == &adev->uvd.ring_enc[0])
		WREG32_SOC15(UVD, 0, mmUVD_RB_WPTR,
			lower_32_bits(ring->wptr));
	else
		WREG32_SOC15(UVD, 0, mmUVD_RB_WPTR2,
			lower_32_bits(ring->wptr));
}

/**
 * uvd_v7_0_enc_ring_test_ring - test if UVD ENC ring is working
 *
 * @ring: the engine to test on
 *
 */
static int uvd_v7_0_enc_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t rptr = amdgpu_ring_get_rptr(ring);
	unsigned i;
	int r;

	r = amdgpu_ring_alloc(ring, 16);
	if (r) {
		DRM_ERROR("amdgpu: uvd enc failed to lock ring %d (%d).\n",
			  ring->idx, r);
		return r;
	}
	amdgpu_ring_write(ring, HEVC_ENC_CMD_END);
	amdgpu_ring_commit(ring);

	for (i = 0; i < adev->usec_timeout; i++) {
		if (amdgpu_ring_get_rptr(ring) != rptr)
			break;
		DRM_UDELAY(1);
	}

	if (i < adev->usec_timeout) {
		DRM_INFO("ring test on %d succeeded in %d usecs\n",
			 ring->idx, i);
	} else {
		DRM_ERROR("amdgpu: ring %d test failed\n",
			  ring->idx);
		r = -ETIMEDOUT;
	}

	return r;
}

/**
 * uvd_v7_0_enc_get_create_msg - generate a UVD ENC create msg
 *
 * @adev: amdgpu_device pointer
 * @ring: ring we should submit the msg to
 * @handle: session handle to use
 * @fence: optional fence to return
 *
 * Open up a stream for HW test
 */
static int uvd_v7_0_enc_get_create_msg(struct amdgpu_ring *ring, uint32_t handle,
				       struct dma_fence **fence)
{
	const unsigned ib_size_dw = 16;
	struct amdgpu_job *job;
	struct amdgpu_ib *ib;
	struct dma_fence *f = NULL;
	uint64_t dummy;
	int i, r;

	r = amdgpu_job_alloc_with_ib(ring->adev, ib_size_dw * 4, &job);
	if (r)
		return r;

	ib = &job->ibs[0];
	dummy = ib->gpu_addr + 1024;

	ib->length_dw = 0;
	ib->ptr[ib->length_dw++] = 0x00000018;
	ib->ptr[ib->length_dw++] = 0x00000001; /* session info */
	ib->ptr[ib->length_dw++] = handle;
	ib->ptr[ib->length_dw++] = 0x00000000;
	ib->ptr[ib->length_dw++] = upper_32_bits(dummy);
	ib->ptr[ib->length_dw++] = dummy;

	ib->ptr[ib->length_dw++] = 0x00000014;
	ib->ptr[ib->length_dw++] = 0x00000002; /* task info */
	ib->ptr[ib->length_dw++] = 0x0000001c;
	ib->ptr[ib->length_dw++] = 0x00000000;
	ib->ptr[ib->length_dw++] = 0x00000000;

	ib->ptr[ib->length_dw++] = 0x00000008;
	ib->ptr[ib->length_dw++] = 0x08000001; /* op initialize */

	for (i = ib->length_dw; i < ib_size_dw; ++i)
		ib->ptr[i] = 0x0;

	r = amdgpu_ib_schedule(ring, 1, ib, NULL, &f);
	job->fence = dma_fence_get(f);
	if (r)
		goto err;

	amdgpu_job_free(job);
	if (fence)
		*fence = dma_fence_get(f);
	dma_fence_put(f);
	return 0;

err:
	amdgpu_job_free(job);
	return r;
}

/**
 * uvd_v7_0_enc_get_destroy_msg - generate a UVD ENC destroy msg
 *
 * @adev: amdgpu_device pointer
 * @ring: ring we should submit the msg to
 * @handle: session handle to use
 * @fence: optional fence to return
 *
 * Close up a stream for HW test or if userspace failed to do so
 */
int uvd_v7_0_enc_get_destroy_msg(struct amdgpu_ring *ring, uint32_t handle,
				 bool direct, struct dma_fence **fence)
{
	const unsigned ib_size_dw = 16;
	struct amdgpu_job *job;
	struct amdgpu_ib *ib;
	struct dma_fence *f = NULL;
	uint64_t dummy;
	int i, r;

	r = amdgpu_job_alloc_with_ib(ring->adev, ib_size_dw * 4, &job);
	if (r)
		return r;

	ib = &job->ibs[0];
	dummy = ib->gpu_addr + 1024;

	ib->length_dw = 0;
	ib->ptr[ib->length_dw++] = 0x00000018;
	ib->ptr[ib->length_dw++] = 0x00000001;
	ib->ptr[ib->length_dw++] = handle;
	ib->ptr[ib->length_dw++] = 0x00000000;
	ib->ptr[ib->length_dw++] = upper_32_bits(dummy);
	ib->ptr[ib->length_dw++] = dummy;

	ib->ptr[ib->length_dw++] = 0x00000014;
	ib->ptr[ib->length_dw++] = 0x00000002;
	ib->ptr[ib->length_dw++] = 0x0000001c;
	ib->ptr[ib->length_dw++] = 0x00000000;
	ib->ptr[ib->length_dw++] = 0x00000000;

	ib->ptr[ib->length_dw++] = 0x00000008;
	ib->ptr[ib->length_dw++] = 0x08000002; /* op close session */

	for (i = ib->length_dw; i < ib_size_dw; ++i)
		ib->ptr[i] = 0x0;

	if (direct) {
		r = amdgpu_ib_schedule(ring, 1, ib, NULL, &f);
		job->fence = dma_fence_get(f);
		if (r)
			goto err;

		amdgpu_job_free(job);
	} else {
		r = amdgpu_job_submit(job, ring, &ring->adev->vce.entity,
				      AMDGPU_FENCE_OWNER_UNDEFINED, &f);
		if (r)
			goto err;
	}

	if (fence)
		*fence = dma_fence_get(f);
	dma_fence_put(f);
	return 0;

err:
	amdgpu_job_free(job);
	return r;
}

/**
 * uvd_v7_0_enc_ring_test_ib - test if UVD ENC IBs are working
 *
 * @ring: the engine to test on
 *
 */
static int uvd_v7_0_enc_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct dma_fence *fence = NULL;
	long r;

	r = uvd_v7_0_enc_get_create_msg(ring, 1, NULL);
	if (r) {
		DRM_ERROR("amdgpu: failed to get create msg (%ld).\n", r);
		goto error;
	}

	r = uvd_v7_0_enc_get_destroy_msg(ring, 1, true, &fence);
	if (r) {
		DRM_ERROR("amdgpu: failed to get destroy ib (%ld).\n", r);
		goto error;
	}

	r = dma_fence_wait_timeout(fence, false, timeout);
	if (r == 0) {
		DRM_ERROR("amdgpu: IB test timed out.\n");
		r = -ETIMEDOUT;
	} else if (r < 0) {
		DRM_ERROR("amdgpu: fence wait failed (%ld).\n", r);
	} else {
		DRM_INFO("ib test on ring %d succeeded\n", ring->idx);
		r = 0;
	}
error:
	dma_fence_put(fence);
	return r;
}

static int uvd_v7_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_sriov_vf(adev))
		adev->uvd.num_enc_rings = 1;
	else
		adev->uvd.num_enc_rings = 2;
	uvd_v7_0_set_ring_funcs(adev);
	uvd_v7_0_set_enc_ring_funcs(adev);
	uvd_v7_0_set_irq_funcs(adev);

	return 0;
}

static int uvd_v7_0_sw_init(void *handle)
{
	struct amdgpu_ring *ring;
	struct amd_sched_rq *rq;
	int i, r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* UVD TRAP */
	r = amdgpu_irq_add_id(adev, AMDGPU_IH_CLIENTID_UVD, 124, &adev->uvd.irq);
	if (r)
		return r;

	/* UVD ENC TRAP */
	for (i = 0; i < adev->uvd.num_enc_rings; ++i) {
		r = amdgpu_irq_add_id(adev, AMDGPU_IH_CLIENTID_UVD, i + 119, &adev->uvd.irq);
		if (r)
			return r;
	}

	r = amdgpu_uvd_sw_init(adev);
	if (r)
		return r;

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		const struct common_firmware_header *hdr;
		hdr = (const struct common_firmware_header *)adev->uvd.fw->data;
		adev->firmware.ucode[AMDGPU_UCODE_ID_UVD].ucode_id = AMDGPU_UCODE_ID_UVD;
		adev->firmware.ucode[AMDGPU_UCODE_ID_UVD].fw = adev->uvd.fw;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(hdr->ucode_size_bytes), PAGE_SIZE);
		DRM_INFO("PSP loading UVD firmware\n");
	}

	ring = &adev->uvd.ring_enc[0];
	rq = &ring->sched.sched_rq[AMD_SCHED_PRIORITY_NORMAL];
	r = amd_sched_entity_init(&ring->sched, &adev->uvd.entity_enc,
				  rq, amdgpu_sched_jobs);
	if (r) {
		DRM_ERROR("Failed setting up UVD ENC run queue.\n");
		return r;
	}

	r = amdgpu_uvd_resume(adev);
	if (r)
		return r;
	if (!amdgpu_sriov_vf(adev)) {
		ring = &adev->uvd.ring;
		sprintf(ring->name, "uvd");
		r = amdgpu_ring_init(adev, ring, 512, &adev->uvd.irq, 0);
		if (r)
			return r;
	}


	for (i = 0; i < adev->uvd.num_enc_rings; ++i) {
		ring = &adev->uvd.ring_enc[i];
		sprintf(ring->name, "uvd_enc%d", i);
		if (amdgpu_sriov_vf(adev)) {
			ring->use_doorbell = true;
			ring->doorbell_index = AMDGPU_DOORBELL64_UVD_RING0_1 * 2;
		}
		r = amdgpu_ring_init(adev, ring, 512, &adev->uvd.irq, 0);
		if (r)
			return r;
	}

	r = amdgpu_virt_alloc_mm_table(adev);
	if (r)
		return r;

	return r;
}

static int uvd_v7_0_sw_fini(void *handle)
{
	int i, r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	amdgpu_virt_free_mm_table(adev);

	r = amdgpu_uvd_suspend(adev);
	if (r)
		return r;

	amd_sched_entity_fini(&adev->uvd.ring_enc[0].sched, &adev->uvd.entity_enc);

	for (i = 0; i < adev->uvd.num_enc_rings; ++i)
		amdgpu_ring_fini(&adev->uvd.ring_enc[i]);

	return amdgpu_uvd_sw_fini(adev);
}

/**
 * uvd_v7_0_hw_init - start and test UVD block
 *
 * @adev: amdgpu_device pointer
 *
 * Initialize the hardware, boot up the VCPU and do some testing
 */
static int uvd_v7_0_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct amdgpu_ring *ring = &adev->uvd.ring;
	uint32_t tmp;
	int i, r;

	if (amdgpu_sriov_vf(adev))
		r = uvd_v7_0_sriov_start(adev);
	else
		r = uvd_v7_0_start(adev);
	if (r)
		goto done;

	if (!amdgpu_sriov_vf(adev)) {
		ring->ready = true;
		r = amdgpu_ring_test_ring(ring);
		if (r) {
			ring->ready = false;
			goto done;
		}

		r = amdgpu_ring_alloc(ring, 10);
		if (r) {
			DRM_ERROR("amdgpu: ring failed to lock UVD ring (%d).\n", r);
			goto done;
		}

		tmp = PACKET0(SOC15_REG_OFFSET(UVD, 0,
			mmUVD_SEMA_WAIT_FAULT_TIMEOUT_CNTL), 0);
		amdgpu_ring_write(ring, tmp);
		amdgpu_ring_write(ring, 0xFFFFF);

		tmp = PACKET0(SOC15_REG_OFFSET(UVD, 0,
			mmUVD_SEMA_WAIT_INCOMPLETE_TIMEOUT_CNTL), 0);
		amdgpu_ring_write(ring, tmp);
		amdgpu_ring_write(ring, 0xFFFFF);

		tmp = PACKET0(SOC15_REG_OFFSET(UVD, 0,
			mmUVD_SEMA_SIGNAL_INCOMPLETE_TIMEOUT_CNTL), 0);
		amdgpu_ring_write(ring, tmp);
		amdgpu_ring_write(ring, 0xFFFFF);

		/* Clear timeout status bits */
		amdgpu_ring_write(ring, PACKET0(SOC15_REG_OFFSET(UVD, 0,
			mmUVD_SEMA_TIMEOUT_STATUS), 0));
		amdgpu_ring_write(ring, 0x8);

		amdgpu_ring_write(ring, PACKET0(SOC15_REG_OFFSET(UVD, 0,
			mmUVD_SEMA_CNTL), 0));
		amdgpu_ring_write(ring, 3);

		amdgpu_ring_commit(ring);
	}

	for (i = 0; i < adev->uvd.num_enc_rings; ++i) {
		ring = &adev->uvd.ring_enc[i];
		ring->ready = true;
		r = amdgpu_ring_test_ring(ring);
		if (r) {
			ring->ready = false;
			goto done;
		}
	}

done:
	if (!r)
		DRM_INFO("UVD and UVD ENC initialized successfully.\n");

	return r;
}

/**
 * uvd_v7_0_hw_fini - stop the hardware block
 *
 * @adev: amdgpu_device pointer
 *
 * Stop the UVD block, mark ring as not ready any more
 */
static int uvd_v7_0_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct amdgpu_ring *ring = &adev->uvd.ring;

	if (!amdgpu_sriov_vf(adev))
		uvd_v7_0_stop(adev);
	else {
		/* full access mode, so don't touch any UVD register */
		DRM_DEBUG("For SRIOV client, shouldn't do anything.\n");
	}

	ring->ready = false;

	return 0;
}

static int uvd_v7_0_suspend(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = uvd_v7_0_hw_fini(adev);
	if (r)
		return r;

	/* Skip this for APU for now */
	if (!(adev->flags & AMD_IS_APU))
		r = amdgpu_uvd_suspend(adev);

	return r;
}

static int uvd_v7_0_resume(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* Skip this for APU for now */
	if (!(adev->flags & AMD_IS_APU)) {
		r = amdgpu_uvd_resume(adev);
		if (r)
			return r;
	}
	return uvd_v7_0_hw_init(adev);
}

/**
 * uvd_v7_0_mc_resume - memory controller programming
 *
 * @adev: amdgpu_device pointer
 *
 * Let the UVD memory controller know it's offsets
 */
static void uvd_v7_0_mc_resume(struct amdgpu_device *adev)
{
	uint32_t size = AMDGPU_GPU_PAGE_ALIGN(adev->uvd.fw->size + 4);
	uint32_t offset;

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		WREG32_SOC15(UVD, 0, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW,
			lower_32_bits(adev->firmware.ucode[AMDGPU_UCODE_ID_UVD].mc_addr));
		WREG32_SOC15(UVD, 0, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH,
			upper_32_bits(adev->firmware.ucode[AMDGPU_UCODE_ID_UVD].mc_addr));
		offset = 0;
	} else {
		WREG32_SOC15(UVD, 0, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW,
			lower_32_bits(adev->uvd.gpu_addr));
		WREG32_SOC15(UVD, 0, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH,
			upper_32_bits(adev->uvd.gpu_addr));
		offset = size;
	}

	WREG32_SOC15(UVD, 0, mmUVD_VCPU_CACHE_OFFSET0,
				AMDGPU_UVD_FIRMWARE_OFFSET >> 3);
	WREG32_SOC15(UVD, 0, mmUVD_VCPU_CACHE_SIZE0, size);

	WREG32_SOC15(UVD, 0, mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW,
			lower_32_bits(adev->uvd.gpu_addr + offset));
	WREG32_SOC15(UVD, 0, mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH,
			upper_32_bits(adev->uvd.gpu_addr + offset));
	WREG32_SOC15(UVD, 0, mmUVD_VCPU_CACHE_OFFSET1, (1 << 21));
	WREG32_SOC15(UVD, 0, mmUVD_VCPU_CACHE_SIZE1, AMDGPU_UVD_HEAP_SIZE);

	WREG32_SOC15(UVD, 0, mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW,
			lower_32_bits(adev->uvd.gpu_addr + offset + AMDGPU_UVD_HEAP_SIZE));
	WREG32_SOC15(UVD, 0, mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH,
			upper_32_bits(adev->uvd.gpu_addr + offset + AMDGPU_UVD_HEAP_SIZE));
	WREG32_SOC15(UVD, 0, mmUVD_VCPU_CACHE_OFFSET2, (2 << 21));
	WREG32_SOC15(UVD, 0, mmUVD_VCPU_CACHE_SIZE2,
			AMDGPU_UVD_STACK_SIZE + (AMDGPU_UVD_SESSION_SIZE * 40));

	WREG32_SOC15(UVD, 0, mmUVD_UDEC_ADDR_CONFIG,
			adev->gfx.config.gb_addr_config);
	WREG32_SOC15(UVD, 0, mmUVD_UDEC_DB_ADDR_CONFIG,
			adev->gfx.config.gb_addr_config);
	WREG32_SOC15(UVD, 0, mmUVD_UDEC_DBW_ADDR_CONFIG,
			adev->gfx.config.gb_addr_config);

	WREG32_SOC15(UVD, 0, mmUVD_GP_SCRATCH4, adev->uvd.max_handles);
}

static int uvd_v7_0_mmsch_start(struct amdgpu_device *adev,
				struct amdgpu_mm_table *table)
{
	uint32_t data = 0, loop;
	uint64_t addr = table->gpu_addr;
	struct mmsch_v1_0_init_header *header = (struct mmsch_v1_0_init_header *)table->cpu_addr;
	uint32_t size;

	size = header->header_size + header->vce_table_size + header->uvd_table_size;

	/* 1, write to vce_mmsch_vf_ctx_addr_lo/hi register with GPU mc addr of memory descriptor location */
	WREG32_SOC15(VCE, 0, mmVCE_MMSCH_VF_CTX_ADDR_LO, lower_32_bits(addr));
	WREG32_SOC15(VCE, 0, mmVCE_MMSCH_VF_CTX_ADDR_HI, upper_32_bits(addr));

	/* 2, update vmid of descriptor */
	data = RREG32_SOC15(VCE, 0, mmVCE_MMSCH_VF_VMID);
	data &= ~VCE_MMSCH_VF_VMID__VF_CTX_VMID_MASK;
	data |= (0 << VCE_MMSCH_VF_VMID__VF_CTX_VMID__SHIFT); /* use domain0 for MM scheduler */
	WREG32_SOC15(VCE, 0, mmVCE_MMSCH_VF_VMID, data);

	/* 3, notify mmsch about the size of this descriptor */
	WREG32_SOC15(VCE, 0, mmVCE_MMSCH_VF_CTX_SIZE, size);

	/* 4, set resp to zero */
	WREG32_SOC15(VCE, 0, mmVCE_MMSCH_VF_MAILBOX_RESP, 0);

	/* 5, kick off the initialization and wait until VCE_MMSCH_VF_MAILBOX_RESP becomes non-zero */
	WREG32_SOC15(VCE, 0, mmVCE_MMSCH_VF_MAILBOX_HOST, 0x10000001);

	data = RREG32_SOC15(VCE, 0, mmVCE_MMSCH_VF_MAILBOX_RESP);
	loop = 1000;
	while ((data & 0x10000002) != 0x10000002) {
		udelay(10);
		data = RREG32_SOC15(VCE, 0, mmVCE_MMSCH_VF_MAILBOX_RESP);
		loop--;
		if (!loop)
			break;
	}

	if (!loop) {
		dev_err(adev->dev, "failed to init MMSCH, mmVCE_MMSCH_VF_MAILBOX_RESP = %x\n", data);
		return -EBUSY;
	}
	WDOORBELL32(adev->uvd.ring_enc[0].doorbell_index, 0);

	return 0;
}

static int uvd_v7_0_sriov_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring;
	uint32_t offset, size, tmp;
	uint32_t table_size = 0;
	struct mmsch_v1_0_cmd_direct_write direct_wt = { {0} };
	struct mmsch_v1_0_cmd_direct_read_modify_write direct_rd_mod_wt = { {0} };
	struct mmsch_v1_0_cmd_direct_polling direct_poll = { {0} };
	struct mmsch_v1_0_cmd_end end = { {0} };
	uint32_t *init_table = adev->virt.mm_table.cpu_addr;
	struct mmsch_v1_0_init_header *header = (struct mmsch_v1_0_init_header *)init_table;

	direct_wt.cmd_header.command_type = MMSCH_COMMAND__DIRECT_REG_WRITE;
	direct_rd_mod_wt.cmd_header.command_type = MMSCH_COMMAND__DIRECT_REG_READ_MODIFY_WRITE;
	direct_poll.cmd_header.command_type = MMSCH_COMMAND__DIRECT_REG_POLLING;
	end.cmd_header.command_type = MMSCH_COMMAND__END;

	if (header->uvd_table_offset == 0 && header->uvd_table_size == 0) {
		header->version = MMSCH_VERSION;
		header->header_size = sizeof(struct mmsch_v1_0_init_header) >> 2;

		if (header->vce_table_offset == 0 && header->vce_table_size == 0)
			header->uvd_table_offset = header->header_size;
		else
			header->uvd_table_offset = header->vce_table_size + header->vce_table_offset;

		init_table += header->uvd_table_offset;

		ring = &adev->uvd.ring;
		size = AMDGPU_GPU_PAGE_ALIGN(adev->uvd.fw->size + 4);

		/* disable clock gating */
		MMSCH_V1_0_INSERT_DIRECT_RD_MOD_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_POWER_STATUS),
						   ~UVD_POWER_STATUS__UVD_PG_MODE_MASK, 0);
		MMSCH_V1_0_INSERT_DIRECT_RD_MOD_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_STATUS),
						   0xFFFFFFFF, 0x00000004);
		/* mc resume*/
		if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
			MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW),
						    lower_32_bits(adev->firmware.ucode[AMDGPU_UCODE_ID_UVD].mc_addr));
			MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH),
						    upper_32_bits(adev->firmware.ucode[AMDGPU_UCODE_ID_UVD].mc_addr));
			offset = 0;
		} else {
			MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW),
						    lower_32_bits(adev->uvd.gpu_addr));
			MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH),
						    upper_32_bits(adev->uvd.gpu_addr));
			offset = size;
		}

		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_VCPU_CACHE_OFFSET0),
					    AMDGPU_UVD_FIRMWARE_OFFSET >> 3);
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_VCPU_CACHE_SIZE0), size);

		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_LOW),
					    lower_32_bits(adev->uvd.gpu_addr + offset));
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_VCPU_CACHE1_64BIT_BAR_HIGH),
					    upper_32_bits(adev->uvd.gpu_addr + offset));
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_VCPU_CACHE_OFFSET1), (1 << 21));
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_VCPU_CACHE_SIZE1), AMDGPU_UVD_HEAP_SIZE);

		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_LOW),
					    lower_32_bits(adev->uvd.gpu_addr + offset + AMDGPU_UVD_HEAP_SIZE));
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_VCPU_CACHE2_64BIT_BAR_HIGH),
					    upper_32_bits(adev->uvd.gpu_addr + offset + AMDGPU_UVD_HEAP_SIZE));
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_VCPU_CACHE_OFFSET2), (2 << 21));
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_VCPU_CACHE_SIZE2),
					    AMDGPU_UVD_STACK_SIZE + (AMDGPU_UVD_SESSION_SIZE * 40));

		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_UDEC_ADDR_CONFIG),
					    adev->gfx.config.gb_addr_config);
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_UDEC_DB_ADDR_CONFIG),
					    adev->gfx.config.gb_addr_config);
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_UDEC_DBW_ADDR_CONFIG),
					    adev->gfx.config.gb_addr_config);
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_GP_SCRATCH4), adev->uvd.max_handles);
		/* mc resume end*/

		/* disable clock gating */
		MMSCH_V1_0_INSERT_DIRECT_RD_MOD_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_CGC_CTRL),
						   ~UVD_CGC_CTRL__DYN_CLOCK_MODE_MASK, 0);

		/* disable interupt */
		MMSCH_V1_0_INSERT_DIRECT_RD_MOD_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_MASTINT_EN),
						   ~UVD_MASTINT_EN__VCPU_EN_MASK, 0);

		/* stall UMC and register bus before resetting VCPU */
		MMSCH_V1_0_INSERT_DIRECT_RD_MOD_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_CTRL2),
						   ~UVD_LMI_CTRL2__STALL_ARB_UMC_MASK,
						   UVD_LMI_CTRL2__STALL_ARB_UMC_MASK);

		/* put LMI, VCPU, RBC etc... into reset */
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_SOFT_RESET),
					    (uint32_t)(UVD_SOFT_RESET__LMI_SOFT_RESET_MASK |
						       UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK |
						       UVD_SOFT_RESET__LBSI_SOFT_RESET_MASK |
						       UVD_SOFT_RESET__RBC_SOFT_RESET_MASK |
						       UVD_SOFT_RESET__CSM_SOFT_RESET_MASK |
						       UVD_SOFT_RESET__CXW_SOFT_RESET_MASK |
						       UVD_SOFT_RESET__TAP_SOFT_RESET_MASK |
						       UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK));

		/* initialize UVD memory controller */
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_CTRL),
					    (uint32_t)((0x40 << UVD_LMI_CTRL__WRITE_CLEAN_TIMER__SHIFT) |
						       UVD_LMI_CTRL__WRITE_CLEAN_TIMER_EN_MASK |
						       UVD_LMI_CTRL__DATA_COHERENCY_EN_MASK |
						       UVD_LMI_CTRL__VCPU_DATA_COHERENCY_EN_MASK |
						       UVD_LMI_CTRL__REQ_MODE_MASK |
						       0x00100000L));

		/* disable byte swapping */
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_SWAP_CNTL), 0);
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_MP_SWAP_CNTL), 0);

		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_MPC_SET_MUXA0), 0x40c2040);
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_MPC_SET_MUXA1), 0x0);
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_MPC_SET_MUXB0), 0x40c2040);
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_MPC_SET_MUXB1), 0x0);
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_MPC_SET_ALU), 0);
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_MPC_SET_MUX), 0x88);

		/* take all subblocks out of reset, except VCPU */
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_SOFT_RESET),
					    UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK);

		/* enable VCPU clock */
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_VCPU_CNTL),
					    UVD_VCPU_CNTL__CLK_EN_MASK);

		/* enable UMC */
		MMSCH_V1_0_INSERT_DIRECT_RD_MOD_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_CTRL2),
						   ~UVD_LMI_CTRL2__STALL_ARB_UMC_MASK, 0);

		/* boot up the VCPU */
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_SOFT_RESET), 0);

		MMSCH_V1_0_INSERT_DIRECT_POLL(SOC15_REG_OFFSET(UVD, 0, mmUVD_STATUS), 0x02, 0x02);

		/* enable master interrupt */
		MMSCH_V1_0_INSERT_DIRECT_RD_MOD_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_MASTINT_EN),
						   ~(UVD_MASTINT_EN__VCPU_EN_MASK|UVD_MASTINT_EN__SYS_EN_MASK),
						   (UVD_MASTINT_EN__VCPU_EN_MASK|UVD_MASTINT_EN__SYS_EN_MASK));

		/* clear the bit 4 of UVD_STATUS */
		MMSCH_V1_0_INSERT_DIRECT_RD_MOD_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_STATUS),
						   ~(2 << UVD_STATUS__VCPU_REPORT__SHIFT), 0);

		/* force RBC into idle state */
		size = order_base_2(ring->ring_size);
		tmp = REG_SET_FIELD(0, UVD_RBC_RB_CNTL, RB_BUFSZ, size);
		tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_BLKSZ, 1);
		tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_NO_FETCH, 1);
		tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_WPTR_POLL_EN, 0);
		tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_NO_UPDATE, 1);
		tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_RPTR_WR_EN, 1);
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_RBC_RB_CNTL), tmp);

		/* set the write pointer delay */
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_RBC_RB_WPTR_CNTL), 0);

		/* set the wb address */
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_RBC_RB_RPTR_ADDR),
					    (upper_32_bits(ring->gpu_addr) >> 2));

		/* programm the RB_BASE for ring buffer */
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_RBC_RB_64BIT_BAR_LOW),
					    lower_32_bits(ring->gpu_addr));
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_RBC_RB_64BIT_BAR_HIGH),
					    upper_32_bits(ring->gpu_addr));

		ring->wptr = 0;
		ring = &adev->uvd.ring_enc[0];
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_RB_BASE_LO), ring->gpu_addr);
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_RB_BASE_HI), upper_32_bits(ring->gpu_addr));
		MMSCH_V1_0_INSERT_DIRECT_WT(SOC15_REG_OFFSET(UVD, 0, mmUVD_RB_SIZE), ring->ring_size / 4);

		/* add end packet */
		memcpy((void *)init_table, &end, sizeof(struct mmsch_v1_0_cmd_end));
		table_size += sizeof(struct mmsch_v1_0_cmd_end) / 4;
		header->uvd_table_size = table_size;

		return uvd_v7_0_mmsch_start(adev, &adev->virt.mm_table);
	}
	return -EINVAL; /* already initializaed ? */
}

/**
 * uvd_v7_0_start - start UVD block
 *
 * @adev: amdgpu_device pointer
 *
 * Setup and start the UVD block
 */
static int uvd_v7_0_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring = &adev->uvd.ring;
	uint32_t rb_bufsz, tmp;
	uint32_t lmi_swap_cntl;
	uint32_t mp_swap_cntl;
	int i, j, r;

	/* disable DPG */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_POWER_STATUS), 0,
			~UVD_POWER_STATUS__UVD_PG_MODE_MASK);

	/* disable byte swapping */
	lmi_swap_cntl = 0;
	mp_swap_cntl = 0;

	uvd_v7_0_mc_resume(adev);

	/* disable clock gating */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_CGC_CTRL), 0,
			~UVD_CGC_CTRL__DYN_CLOCK_MODE_MASK);

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

	/* initialize UVD memory controller */
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
	mp_swap_cntl = 0;
#endif
	WREG32_SOC15(UVD, 0, mmUVD_LMI_SWAP_CNTL, lmi_swap_cntl);
	WREG32_SOC15(UVD, 0, mmUVD_MP_SWAP_CNTL, mp_swap_cntl);

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

		DRM_ERROR("UVD not responding, trying to reset the VCPU!!!\n");
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
		DRM_ERROR("UVD not responding, giving up!!!\n");
		return r;
	}
	/* enable master interrupt */
	WREG32_P(SOC15_REG_OFFSET(UVD, 0, mmUVD_MASTINT_EN),
		(UVD_MASTINT_EN__VCPU_EN_MASK|UVD_MASTINT_EN__SYS_EN_MASK),
		~(UVD_MASTINT_EN__VCPU_EN_MASK|UVD_MASTINT_EN__SYS_EN_MASK));

	/* clear the bit 4 of UVD_STATUS */
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

	ring = &adev->uvd.ring_enc[0];
	WREG32_SOC15(UVD, 0, mmUVD_RB_RPTR, lower_32_bits(ring->wptr));
	WREG32_SOC15(UVD, 0, mmUVD_RB_WPTR, lower_32_bits(ring->wptr));
	WREG32_SOC15(UVD, 0, mmUVD_RB_BASE_LO, ring->gpu_addr);
	WREG32_SOC15(UVD, 0, mmUVD_RB_BASE_HI, upper_32_bits(ring->gpu_addr));
	WREG32_SOC15(UVD, 0, mmUVD_RB_SIZE, ring->ring_size / 4);

	ring = &adev->uvd.ring_enc[1];
	WREG32_SOC15(UVD, 0, mmUVD_RB_RPTR2, lower_32_bits(ring->wptr));
	WREG32_SOC15(UVD, 0, mmUVD_RB_WPTR2, lower_32_bits(ring->wptr));
	WREG32_SOC15(UVD, 0, mmUVD_RB_BASE_LO2, ring->gpu_addr);
	WREG32_SOC15(UVD, 0, mmUVD_RB_BASE_HI2, upper_32_bits(ring->gpu_addr));
	WREG32_SOC15(UVD, 0, mmUVD_RB_SIZE2, ring->ring_size / 4);

	return 0;
}

/**
 * uvd_v7_0_stop - stop UVD block
 *
 * @adev: amdgpu_device pointer
 *
 * stop the UVD block
 */
static void uvd_v7_0_stop(struct amdgpu_device *adev)
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
}

/**
 * uvd_v7_0_ring_emit_fence - emit an fence & trap command
 *
 * @ring: amdgpu_ring pointer
 * @fence: fence to emit
 *
 * Write a fence and a trap command to the ring.
 */
static void uvd_v7_0_ring_emit_fence(struct amdgpu_ring *ring, u64 addr, u64 seq,
				     unsigned flags)
{
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
	amdgpu_ring_write(ring, 0);

	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA0), 0));
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA1), 0));
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_CMD), 0));
	amdgpu_ring_write(ring, 2);
}

/**
 * uvd_v7_0_enc_ring_emit_fence - emit an enc fence & trap command
 *
 * @ring: amdgpu_ring pointer
 * @fence: fence to emit
 *
 * Write enc a fence and a trap command to the ring.
 */
static void uvd_v7_0_enc_ring_emit_fence(struct amdgpu_ring *ring, u64 addr,
			u64 seq, unsigned flags)
{
	WARN_ON(flags & AMDGPU_FENCE_FLAG_64BIT);

	amdgpu_ring_write(ring, HEVC_ENC_CMD_FENCE);
	amdgpu_ring_write(ring, addr);
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, seq);
	amdgpu_ring_write(ring, HEVC_ENC_CMD_TRAP);
}

/**
 * uvd_v7_0_ring_emit_hdp_flush - emit an hdp flush
 *
 * @ring: amdgpu_ring pointer
 *
 * Emits an hdp flush.
 */
static void uvd_v7_0_ring_emit_hdp_flush(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring, PACKET0(SOC15_REG_OFFSET(NBIF, 0,
		mmHDP_MEM_COHERENCY_FLUSH_CNTL), 0));
	amdgpu_ring_write(ring, 0);
}

/**
 * uvd_v7_0_ring_hdp_invalidate - emit an hdp invalidate
 *
 * @ring: amdgpu_ring pointer
 *
 * Emits an hdp invalidate.
 */
static void uvd_v7_0_ring_emit_hdp_invalidate(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring, PACKET0(SOC15_REG_OFFSET(HDP, 0, mmHDP_DEBUG0), 0));
	amdgpu_ring_write(ring, 1);
}

/**
 * uvd_v7_0_ring_test_ring - register write test
 *
 * @ring: amdgpu_ring pointer
 *
 * Test if we can successfully write to the context register
 */
static int uvd_v7_0_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t tmp = 0;
	unsigned i;
	int r;

	WREG32_SOC15(UVD, 0, mmUVD_CONTEXT_ID, 0xCAFEDEAD);
	r = amdgpu_ring_alloc(ring, 3);
	if (r) {
		DRM_ERROR("amdgpu: cp failed to lock ring %d (%d).\n",
			  ring->idx, r);
		return r;
	}
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_CONTEXT_ID), 0));
	amdgpu_ring_write(ring, 0xDEADBEEF);
	amdgpu_ring_commit(ring);
	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = RREG32_SOC15(UVD, 0, mmUVD_CONTEXT_ID);
		if (tmp == 0xDEADBEEF)
			break;
		DRM_UDELAY(1);
	}

	if (i < adev->usec_timeout) {
		DRM_INFO("ring test on %d succeeded in %d usecs\n",
			 ring->idx, i);
	} else {
		DRM_ERROR("amdgpu: ring %d test failed (0x%08X)\n",
			  ring->idx, tmp);
		r = -EINVAL;
	}
	return r;
}

/**
 * uvd_v7_0_ring_emit_ib - execute indirect buffer
 *
 * @ring: amdgpu_ring pointer
 * @ib: indirect buffer to execute
 *
 * Write ring commands to execute the indirect buffer
 */
static void uvd_v7_0_ring_emit_ib(struct amdgpu_ring *ring,
				  struct amdgpu_ib *ib,
				  unsigned vm_id, bool ctx_switch)
{
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_LMI_RBC_IB_VMID), 0));
	amdgpu_ring_write(ring, vm_id);

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

/**
 * uvd_v7_0_enc_ring_emit_ib - enc execute indirect buffer
 *
 * @ring: amdgpu_ring pointer
 * @ib: indirect buffer to execute
 *
 * Write enc ring commands to execute the indirect buffer
 */
static void uvd_v7_0_enc_ring_emit_ib(struct amdgpu_ring *ring,
		struct amdgpu_ib *ib, unsigned int vm_id, bool ctx_switch)
{
	amdgpu_ring_write(ring, HEVC_ENC_CMD_IB_VM);
	amdgpu_ring_write(ring, vm_id);
	amdgpu_ring_write(ring, lower_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, ib->length_dw);
}

static void uvd_v7_0_vm_reg_write(struct amdgpu_ring *ring,
				uint32_t data0, uint32_t data1)
{
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA0), 0));
	amdgpu_ring_write(ring, data0);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA1), 0));
	amdgpu_ring_write(ring, data1);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_CMD), 0));
	amdgpu_ring_write(ring, 8);
}

static void uvd_v7_0_vm_reg_wait(struct amdgpu_ring *ring,
				uint32_t data0, uint32_t data1, uint32_t mask)
{
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA0), 0));
	amdgpu_ring_write(ring, data0);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA1), 0));
	amdgpu_ring_write(ring, data1);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GP_SCRATCH8), 0));
	amdgpu_ring_write(ring, mask);
	amdgpu_ring_write(ring,
		PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_CMD), 0));
	amdgpu_ring_write(ring, 12);
}

static void uvd_v7_0_ring_emit_vm_flush(struct amdgpu_ring *ring,
					unsigned vm_id, uint64_t pd_addr)
{
	struct amdgpu_vmhub *hub = &ring->adev->vmhub[ring->funcs->vmhub];
	uint32_t req = ring->adev->gart.gart_funcs->get_invalidate_req(vm_id);
	uint32_t data0, data1, mask;
	unsigned eng = ring->vm_inv_eng;

	pd_addr = amdgpu_gart_get_vm_pde(ring->adev, pd_addr);
	pd_addr |= AMDGPU_PTE_VALID;

	data0 = (hub->ctx0_ptb_addr_hi32 + vm_id * 2) << 2;
	data1 = upper_32_bits(pd_addr);
	uvd_v7_0_vm_reg_write(ring, data0, data1);

	data0 = (hub->ctx0_ptb_addr_lo32 + vm_id * 2) << 2;
	data1 = lower_32_bits(pd_addr);
	uvd_v7_0_vm_reg_write(ring, data0, data1);

	data0 = (hub->ctx0_ptb_addr_lo32 + vm_id * 2) << 2;
	data1 = lower_32_bits(pd_addr);
	mask = 0xffffffff;
	uvd_v7_0_vm_reg_wait(ring, data0, data1, mask);

	/* flush TLB */
	data0 = (hub->vm_inv_eng0_req + eng) << 2;
	data1 = req;
	uvd_v7_0_vm_reg_write(ring, data0, data1);

	/* wait for flush */
	data0 = (hub->vm_inv_eng0_ack + eng) << 2;
	data1 = 1 << vm_id;
	mask =  1 << vm_id;
	uvd_v7_0_vm_reg_wait(ring, data0, data1, mask);
}

static void uvd_v7_0_enc_ring_insert_end(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring, HEVC_ENC_CMD_END);
}

static void uvd_v7_0_enc_ring_emit_vm_flush(struct amdgpu_ring *ring,
			 unsigned int vm_id, uint64_t pd_addr)
{
	struct amdgpu_vmhub *hub = &ring->adev->vmhub[ring->funcs->vmhub];
	uint32_t req = ring->adev->gart.gart_funcs->get_invalidate_req(vm_id);
	unsigned eng = ring->vm_inv_eng;

	pd_addr = amdgpu_gart_get_vm_pde(ring->adev, pd_addr);
	pd_addr |= AMDGPU_PTE_VALID;

	amdgpu_ring_write(ring, HEVC_ENC_CMD_REG_WRITE);
	amdgpu_ring_write(ring,	(hub->ctx0_ptb_addr_hi32 + vm_id * 2) << 2);
	amdgpu_ring_write(ring, upper_32_bits(pd_addr));

	amdgpu_ring_write(ring, HEVC_ENC_CMD_REG_WRITE);
	amdgpu_ring_write(ring,	(hub->ctx0_ptb_addr_lo32 + vm_id * 2) << 2);
	amdgpu_ring_write(ring, lower_32_bits(pd_addr));

	amdgpu_ring_write(ring, HEVC_ENC_CMD_REG_WAIT);
	amdgpu_ring_write(ring,	(hub->ctx0_ptb_addr_lo32 + vm_id * 2) << 2);
	amdgpu_ring_write(ring, 0xffffffff);
	amdgpu_ring_write(ring, lower_32_bits(pd_addr));

	/* flush TLB */
	amdgpu_ring_write(ring, HEVC_ENC_CMD_REG_WRITE);
	amdgpu_ring_write(ring,	(hub->vm_inv_eng0_req + eng) << 2);
	amdgpu_ring_write(ring, req);

	/* wait for flush */
	amdgpu_ring_write(ring, HEVC_ENC_CMD_REG_WAIT);
	amdgpu_ring_write(ring, (hub->vm_inv_eng0_ack + eng) << 2);
	amdgpu_ring_write(ring, 1 << vm_id);
	amdgpu_ring_write(ring, 1 << vm_id);
}

#if 0
static bool uvd_v7_0_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return !(RREG32(mmSRBM_STATUS) & SRBM_STATUS__UVD_BUSY_MASK);
}

static int uvd_v7_0_wait_for_idle(void *handle)
{
	unsigned i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	for (i = 0; i < adev->usec_timeout; i++) {
		if (uvd_v7_0_is_idle(handle))
			return 0;
	}
	return -ETIMEDOUT;
}

#define AMDGPU_UVD_STATUS_BUSY_MASK    0xfd
static bool uvd_v7_0_check_soft_reset(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 srbm_soft_reset = 0;
	u32 tmp = RREG32(mmSRBM_STATUS);

	if (REG_GET_FIELD(tmp, SRBM_STATUS, UVD_RQ_PENDING) ||
	    REG_GET_FIELD(tmp, SRBM_STATUS, UVD_BUSY) ||
	    (RREG32_SOC15(UVD, 0, mmUVD_STATUS) &
		    AMDGPU_UVD_STATUS_BUSY_MASK))
		srbm_soft_reset = REG_SET_FIELD(srbm_soft_reset,
				SRBM_SOFT_RESET, SOFT_RESET_UVD, 1);

	if (srbm_soft_reset) {
		adev->uvd.srbm_soft_reset = srbm_soft_reset;
		return true;
	} else {
		adev->uvd.srbm_soft_reset = 0;
		return false;
	}
}

static int uvd_v7_0_pre_soft_reset(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (!adev->uvd.srbm_soft_reset)
		return 0;

	uvd_v7_0_stop(adev);
	return 0;
}

static int uvd_v7_0_soft_reset(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 srbm_soft_reset;

	if (!adev->uvd.srbm_soft_reset)
		return 0;
	srbm_soft_reset = adev->uvd.srbm_soft_reset;

	if (srbm_soft_reset) {
		u32 tmp;

		tmp = RREG32(mmSRBM_SOFT_RESET);
		tmp |= srbm_soft_reset;
		dev_info(adev->dev, "SRBM_SOFT_RESET=0x%08X\n", tmp);
		WREG32(mmSRBM_SOFT_RESET, tmp);
		tmp = RREG32(mmSRBM_SOFT_RESET);

		udelay(50);

		tmp &= ~srbm_soft_reset;
		WREG32(mmSRBM_SOFT_RESET, tmp);
		tmp = RREG32(mmSRBM_SOFT_RESET);

		/* Wait a little for things to settle down */
		udelay(50);
	}

	return 0;
}

static int uvd_v7_0_post_soft_reset(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (!adev->uvd.srbm_soft_reset)
		return 0;

	mdelay(5);

	return uvd_v7_0_start(adev);
}
#endif

static int uvd_v7_0_set_interrupt_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	// TODO
	return 0;
}

static int uvd_v7_0_process_interrupt(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	DRM_DEBUG("IH: UVD TRAP\n");
	switch (entry->src_id) {
	case 124:
		amdgpu_fence_process(&adev->uvd.ring);
		break;
	case 119:
		amdgpu_fence_process(&adev->uvd.ring_enc[0]);
		break;
	case 120:
		if (!amdgpu_sriov_vf(adev))
			amdgpu_fence_process(&adev->uvd.ring_enc[1]);
		break;
	default:
		DRM_ERROR("Unhandled interrupt: %d %d\n",
			  entry->src_id, entry->src_data[0]);
		break;
	}

	return 0;
}

#if 0
static void uvd_v7_0_set_sw_clock_gating(struct amdgpu_device *adev)
{
	uint32_t data, data1, data2, suvd_flags;

	data = RREG32_SOC15(UVD, 0, mmUVD_CGC_CTRL);
	data1 = RREG32_SOC15(UVD, 0, mmUVD_SUVD_CGC_GATE);
	data2 = RREG32_SOC15(UVD, 0, mmUVD_SUVD_CGC_CTRL);

	data &= ~(UVD_CGC_CTRL__CLK_OFF_DELAY_MASK |
		  UVD_CGC_CTRL__CLK_GATE_DLY_TIMER_MASK);

	suvd_flags = UVD_SUVD_CGC_GATE__SRE_MASK |
		     UVD_SUVD_CGC_GATE__SIT_MASK |
		     UVD_SUVD_CGC_GATE__SMP_MASK |
		     UVD_SUVD_CGC_GATE__SCM_MASK |
		     UVD_SUVD_CGC_GATE__SDB_MASK;

	data |= UVD_CGC_CTRL__DYN_CLOCK_MODE_MASK |
		(1 << REG_FIELD_SHIFT(UVD_CGC_CTRL, CLK_GATE_DLY_TIMER)) |
		(4 << REG_FIELD_SHIFT(UVD_CGC_CTRL, CLK_OFF_DELAY));

	data &= ~(UVD_CGC_CTRL__UDEC_RE_MODE_MASK |
			UVD_CGC_CTRL__UDEC_CM_MODE_MASK |
			UVD_CGC_CTRL__UDEC_IT_MODE_MASK |
			UVD_CGC_CTRL__UDEC_DB_MODE_MASK |
			UVD_CGC_CTRL__UDEC_MP_MODE_MASK |
			UVD_CGC_CTRL__SYS_MODE_MASK |
			UVD_CGC_CTRL__UDEC_MODE_MASK |
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
			UVD_CGC_CTRL__VCPU_MODE_MASK |
			UVD_CGC_CTRL__JPEG_MODE_MASK |
			UVD_CGC_CTRL__JPEG2_MODE_MASK |
			UVD_CGC_CTRL__SCPU_MODE_MASK);
	data2 &= ~(UVD_SUVD_CGC_CTRL__SRE_MODE_MASK |
			UVD_SUVD_CGC_CTRL__SIT_MODE_MASK |
			UVD_SUVD_CGC_CTRL__SMP_MODE_MASK |
			UVD_SUVD_CGC_CTRL__SCM_MODE_MASK |
			UVD_SUVD_CGC_CTRL__SDB_MODE_MASK);
	data1 |= suvd_flags;

	WREG32_SOC15(UVD, 0, mmUVD_CGC_CTRL, data);
	WREG32_SOC15(UVD, 0, mmUVD_CGC_GATE, 0);
	WREG32_SOC15(UVD, 0, mmUVD_SUVD_CGC_GATE, data1);
	WREG32_SOC15(UVD, 0, mmUVD_SUVD_CGC_CTRL, data2);
}

static void uvd_v7_0_set_hw_clock_gating(struct amdgpu_device *adev)
{
	uint32_t data, data1, cgc_flags, suvd_flags;

	data = RREG32_SOC15(UVD, 0, mmUVD_CGC_GATE);
	data1 = RREG32_SOC15(UVD, 0, mmUVD_SUVD_CGC_GATE);

	cgc_flags = UVD_CGC_GATE__SYS_MASK |
		UVD_CGC_GATE__UDEC_MASK |
		UVD_CGC_GATE__MPEG2_MASK |
		UVD_CGC_GATE__RBC_MASK |
		UVD_CGC_GATE__LMI_MC_MASK |
		UVD_CGC_GATE__IDCT_MASK |
		UVD_CGC_GATE__MPRD_MASK |
		UVD_CGC_GATE__MPC_MASK |
		UVD_CGC_GATE__LBSI_MASK |
		UVD_CGC_GATE__LRBBM_MASK |
		UVD_CGC_GATE__UDEC_RE_MASK |
		UVD_CGC_GATE__UDEC_CM_MASK |
		UVD_CGC_GATE__UDEC_IT_MASK |
		UVD_CGC_GATE__UDEC_DB_MASK |
		UVD_CGC_GATE__UDEC_MP_MASK |
		UVD_CGC_GATE__WCB_MASK |
		UVD_CGC_GATE__VCPU_MASK |
		UVD_CGC_GATE__SCPU_MASK |
		UVD_CGC_GATE__JPEG_MASK |
		UVD_CGC_GATE__JPEG2_MASK;

	suvd_flags = UVD_SUVD_CGC_GATE__SRE_MASK |
				UVD_SUVD_CGC_GATE__SIT_MASK |
				UVD_SUVD_CGC_GATE__SMP_MASK |
				UVD_SUVD_CGC_GATE__SCM_MASK |
				UVD_SUVD_CGC_GATE__SDB_MASK;

	data |= cgc_flags;
	data1 |= suvd_flags;

	WREG32_SOC15(UVD, 0, mmUVD_CGC_GATE, data);
	WREG32_SOC15(UVD, 0, mmUVD_SUVD_CGC_GATE, data1);
}

static void uvd_v7_0_set_bypass_mode(struct amdgpu_device *adev, bool enable)
{
	u32 tmp = RREG32_SMC(ixGCK_DFS_BYPASS_CNTL);

	if (enable)
		tmp |= (GCK_DFS_BYPASS_CNTL__BYPASSDCLK_MASK |
			GCK_DFS_BYPASS_CNTL__BYPASSVCLK_MASK);
	else
		tmp &= ~(GCK_DFS_BYPASS_CNTL__BYPASSDCLK_MASK |
			 GCK_DFS_BYPASS_CNTL__BYPASSVCLK_MASK);

	WREG32_SMC(ixGCK_DFS_BYPASS_CNTL, tmp);
}


static int uvd_v7_0_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	bool enable = (state == AMD_CG_STATE_GATE) ? true : false;

	uvd_v7_0_set_bypass_mode(adev, enable);

	if (!(adev->cg_flags & AMD_CG_SUPPORT_UVD_MGCG))
		return 0;

	if (enable) {
		/* disable HW gating and enable Sw gating */
		uvd_v7_0_set_sw_clock_gating(adev);
	} else {
		/* wait for STATUS to clear */
		if (uvd_v7_0_wait_for_idle(handle))
			return -EBUSY;

		/* enable HW gates because UVD is idle */
		/* uvd_v7_0_set_hw_clock_gating(adev); */
	}

	return 0;
}

static int uvd_v7_0_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	/* This doesn't actually powergate the UVD block.
	 * That's done in the dpm code via the SMC.  This
	 * just re-inits the block as necessary.  The actual
	 * gating still happens in the dpm code.  We should
	 * revisit this when there is a cleaner line between
	 * the smc and the hw blocks
	 */
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (!(adev->pg_flags & AMD_PG_SUPPORT_UVD))
		return 0;

	WREG32_SOC15(UVD, 0, mmUVD_POWER_STATUS, UVD_POWER_STATUS__UVD_PG_EN_MASK);

	if (state == AMD_PG_STATE_GATE) {
		uvd_v7_0_stop(adev);
		return 0;
	} else {
		return uvd_v7_0_start(adev);
	}
}
#endif

static int uvd_v7_0_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	/* needed for driver unload*/
	return 0;
}

const struct amd_ip_funcs uvd_v7_0_ip_funcs = {
	.name = "uvd_v7_0",
	.early_init = uvd_v7_0_early_init,
	.late_init = NULL,
	.sw_init = uvd_v7_0_sw_init,
	.sw_fini = uvd_v7_0_sw_fini,
	.hw_init = uvd_v7_0_hw_init,
	.hw_fini = uvd_v7_0_hw_fini,
	.suspend = uvd_v7_0_suspend,
	.resume = uvd_v7_0_resume,
	.is_idle = NULL /* uvd_v7_0_is_idle */,
	.wait_for_idle = NULL /* uvd_v7_0_wait_for_idle */,
	.check_soft_reset = NULL /* uvd_v7_0_check_soft_reset */,
	.pre_soft_reset = NULL /* uvd_v7_0_pre_soft_reset */,
	.soft_reset = NULL /* uvd_v7_0_soft_reset */,
	.post_soft_reset = NULL /* uvd_v7_0_post_soft_reset */,
	.set_clockgating_state = uvd_v7_0_set_clockgating_state,
	.set_powergating_state = NULL /* uvd_v7_0_set_powergating_state */,
};

static const struct amdgpu_ring_funcs uvd_v7_0_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_UVD,
	.align_mask = 0xf,
	.nop = PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_NO_OP), 0),
	.support_64bit_ptrs = false,
	.vmhub = AMDGPU_MMHUB,
	.get_rptr = uvd_v7_0_ring_get_rptr,
	.get_wptr = uvd_v7_0_ring_get_wptr,
	.set_wptr = uvd_v7_0_ring_set_wptr,
	.emit_frame_size =
		2 + /* uvd_v7_0_ring_emit_hdp_flush */
		2 + /* uvd_v7_0_ring_emit_hdp_invalidate */
		34 + /* uvd_v7_0_ring_emit_vm_flush */
		14 + 14, /* uvd_v7_0_ring_emit_fence x2 vm fence */
	.emit_ib_size = 8, /* uvd_v7_0_ring_emit_ib */
	.emit_ib = uvd_v7_0_ring_emit_ib,
	.emit_fence = uvd_v7_0_ring_emit_fence,
	.emit_vm_flush = uvd_v7_0_ring_emit_vm_flush,
	.emit_hdp_flush = uvd_v7_0_ring_emit_hdp_flush,
	.emit_hdp_invalidate = uvd_v7_0_ring_emit_hdp_invalidate,
	.test_ring = uvd_v7_0_ring_test_ring,
	.test_ib = amdgpu_uvd_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_uvd_ring_begin_use,
	.end_use = amdgpu_uvd_ring_end_use,
};

static const struct amdgpu_ring_funcs uvd_v7_0_enc_ring_vm_funcs = {
	.type = AMDGPU_RING_TYPE_UVD_ENC,
	.align_mask = 0x3f,
	.nop = HEVC_ENC_CMD_NO_OP,
	.support_64bit_ptrs = false,
	.vmhub = AMDGPU_MMHUB,
	.get_rptr = uvd_v7_0_enc_ring_get_rptr,
	.get_wptr = uvd_v7_0_enc_ring_get_wptr,
	.set_wptr = uvd_v7_0_enc_ring_set_wptr,
	.emit_frame_size =
		17 + /* uvd_v7_0_enc_ring_emit_vm_flush */
		5 + 5 + /* uvd_v7_0_enc_ring_emit_fence x2 vm fence */
		1, /* uvd_v7_0_enc_ring_insert_end */
	.emit_ib_size = 5, /* uvd_v7_0_enc_ring_emit_ib */
	.emit_ib = uvd_v7_0_enc_ring_emit_ib,
	.emit_fence = uvd_v7_0_enc_ring_emit_fence,
	.emit_vm_flush = uvd_v7_0_enc_ring_emit_vm_flush,
	.test_ring = uvd_v7_0_enc_ring_test_ring,
	.test_ib = uvd_v7_0_enc_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.insert_end = uvd_v7_0_enc_ring_insert_end,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_uvd_ring_begin_use,
	.end_use = amdgpu_uvd_ring_end_use,
};

static void uvd_v7_0_set_ring_funcs(struct amdgpu_device *adev)
{
	adev->uvd.ring.funcs = &uvd_v7_0_ring_vm_funcs;
	DRM_INFO("UVD is enabled in VM mode\n");
}

static void uvd_v7_0_set_enc_ring_funcs(struct amdgpu_device *adev)
{
	int i;

	for (i = 0; i < adev->uvd.num_enc_rings; ++i)
		adev->uvd.ring_enc[i].funcs = &uvd_v7_0_enc_ring_vm_funcs;

	DRM_INFO("UVD ENC is enabled in VM mode\n");
}

static const struct amdgpu_irq_src_funcs uvd_v7_0_irq_funcs = {
	.set = uvd_v7_0_set_interrupt_state,
	.process = uvd_v7_0_process_interrupt,
};

static void uvd_v7_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->uvd.irq.num_types = adev->uvd.num_enc_rings + 1;
	adev->uvd.irq.funcs = &uvd_v7_0_irq_funcs;
}

const struct amdgpu_ip_block_version uvd_v7_0_ip_block =
{
		.type = AMD_IP_BLOCK_TYPE_UVD,
		.major = 7,
		.minor = 0,
		.rev = 0,
		.funcs = &uvd_v7_0_ip_funcs,
};
