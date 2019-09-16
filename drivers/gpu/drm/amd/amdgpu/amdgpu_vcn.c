/*
 * Copyright 2016 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */

#include <linux/firmware.h>
#include <linux/module.h>
#include <drm/drmP.h>
#include <drm/drm.h>

#include "amdgpu.h"
#include "amdgpu_pm.h"
#include "amdgpu_vcn.h"
#include "soc15d.h"
#include "soc15_common.h"

#include "vcn/vcn_1_0_offset.h"

/* 1 second timeout */
#define VCN_IDLE_TIMEOUT	msecs_to_jiffies(1000)

/* Firmware Names */
#define FIRMWARE_RAVEN		"amdgpu/raven_vcn.bin"

MODULE_FIRMWARE(FIRMWARE_RAVEN);

static void amdgpu_vcn_idle_work_handler(struct work_struct *work);

int amdgpu_vcn_sw_init(struct amdgpu_device *adev)
{
	unsigned long bo_size;
	const char *fw_name;
	const struct common_firmware_header *hdr;
	unsigned char fw_check;
	int r;

	INIT_DELAYED_WORK(&adev->vcn.idle_work, amdgpu_vcn_idle_work_handler);

	switch (adev->asic_type) {
	case CHIP_RAVEN:
		fw_name = FIRMWARE_RAVEN;
		break;
	default:
		return -EINVAL;
	}

	r = request_firmware(&adev->vcn.fw, fw_name, adev->dev);
	if (r) {
		dev_err(adev->dev, "amdgpu_vcn: Can't load firmware \"%s\"\n",
			fw_name);
		return r;
	}

	r = amdgpu_ucode_validate(adev->vcn.fw);
	if (r) {
		dev_err(adev->dev, "amdgpu_vcn: Can't validate firmware \"%s\"\n",
			fw_name);
		release_firmware(adev->vcn.fw);
		adev->vcn.fw = NULL;
		return r;
	}

	hdr = (const struct common_firmware_header *)adev->vcn.fw->data;
	adev->vcn.fw_version = le32_to_cpu(hdr->ucode_version);

	/* Bit 20-23, it is encode major and non-zero for new naming convention.
	 * This field is part of version minor and DRM_DISABLED_FLAG in old naming
	 * convention. Since the l:wq!atest version minor is 0x5B and DRM_DISABLED_FLAG
	 * is zero in old naming convention, this field is always zero so far.
	 * These four bits are used to tell which naming convention is present.
	 */
	fw_check = (le32_to_cpu(hdr->ucode_version) >> 20) & 0xf;
	if (fw_check) {
		unsigned int dec_ver, enc_major, enc_minor, vep, fw_rev;

		fw_rev = le32_to_cpu(hdr->ucode_version) & 0xfff;
		enc_minor = (le32_to_cpu(hdr->ucode_version) >> 12) & 0xff;
		enc_major = fw_check;
		dec_ver = (le32_to_cpu(hdr->ucode_version) >> 24) & 0xf;
		vep = (le32_to_cpu(hdr->ucode_version) >> 28) & 0xf;
		DRM_INFO("Found VCN firmware Version ENC: %hu.%hu DEC: %hu VEP: %hu Revision: %hu\n",
			enc_major, enc_minor, dec_ver, vep, fw_rev);
	} else {
		unsigned int version_major, version_minor, family_id;

		family_id = le32_to_cpu(hdr->ucode_version) & 0xff;
		version_major = (le32_to_cpu(hdr->ucode_version) >> 24) & 0xff;
		version_minor = (le32_to_cpu(hdr->ucode_version) >> 8) & 0xff;
		DRM_INFO("Found VCN firmware Version: %hu.%hu Family ID: %hu\n",
			version_major, version_minor, family_id);
	}

	bo_size = AMDGPU_VCN_STACK_SIZE + AMDGPU_VCN_HEAP_SIZE
		  +  AMDGPU_VCN_SESSION_SIZE * 40;
	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP)
		bo_size += AMDGPU_GPU_PAGE_ALIGN(le32_to_cpu(hdr->ucode_size_bytes) + 8);
	r = amdgpu_bo_create_kernel(adev, bo_size, PAGE_SIZE,
				    AMDGPU_GEM_DOMAIN_VRAM, &adev->vcn.vcpu_bo,
				    &adev->vcn.gpu_addr, &adev->vcn.cpu_addr);
	if (r) {
		dev_err(adev->dev, "(%d) failed to allocate vcn bo\n", r);
		return r;
	}

	return 0;
}

int amdgpu_vcn_sw_fini(struct amdgpu_device *adev)
{
	int i;

	kvfree(adev->vcn.saved_bo);

	amdgpu_bo_free_kernel(&adev->vcn.vcpu_bo,
			      &adev->vcn.gpu_addr,
			      (void **)&adev->vcn.cpu_addr);

	amdgpu_ring_fini(&adev->vcn.ring_dec);

	for (i = 0; i < adev->vcn.num_enc_rings; ++i)
		amdgpu_ring_fini(&adev->vcn.ring_enc[i]);

	amdgpu_ring_fini(&adev->vcn.ring_jpeg);

	release_firmware(adev->vcn.fw);

	return 0;
}

int amdgpu_vcn_suspend(struct amdgpu_device *adev)
{
	unsigned size;
	void *ptr;

	cancel_delayed_work_sync(&adev->vcn.idle_work);

	if (adev->vcn.vcpu_bo == NULL)
		return 0;

	size = amdgpu_bo_size(adev->vcn.vcpu_bo);
	ptr = adev->vcn.cpu_addr;

	adev->vcn.saved_bo = kvmalloc(size, GFP_KERNEL);
	if (!adev->vcn.saved_bo)
		return -ENOMEM;

	memcpy_fromio(adev->vcn.saved_bo, ptr, size);

	return 0;
}

int amdgpu_vcn_resume(struct amdgpu_device *adev)
{
	unsigned size;
	void *ptr;

	if (adev->vcn.vcpu_bo == NULL)
		return -EINVAL;

	size = amdgpu_bo_size(adev->vcn.vcpu_bo);
	ptr = adev->vcn.cpu_addr;

	if (adev->vcn.saved_bo != NULL) {
		memcpy_toio(ptr, adev->vcn.saved_bo, size);
		kvfree(adev->vcn.saved_bo);
		adev->vcn.saved_bo = NULL;
	} else {
		const struct common_firmware_header *hdr;
		unsigned offset;

		hdr = (const struct common_firmware_header *)adev->vcn.fw->data;
		if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
			offset = le32_to_cpu(hdr->ucode_array_offset_bytes);
			memcpy_toio(adev->vcn.cpu_addr, adev->vcn.fw->data + offset,
				    le32_to_cpu(hdr->ucode_size_bytes));
			size -= le32_to_cpu(hdr->ucode_size_bytes);
			ptr += le32_to_cpu(hdr->ucode_size_bytes);
		}
		memset_io(ptr, 0, size);
	}

	return 0;
}

static void amdgpu_vcn_idle_work_handler(struct work_struct *work)
{
	struct amdgpu_device *adev =
		container_of(work, struct amdgpu_device, vcn.idle_work.work);
	unsigned fences = amdgpu_fence_count_emitted(&adev->vcn.ring_dec);
	unsigned i;

	for (i = 0; i < adev->vcn.num_enc_rings; ++i) {
		fences += amdgpu_fence_count_emitted(&adev->vcn.ring_enc[i]);
	}

	fences += amdgpu_fence_count_emitted(&adev->vcn.ring_jpeg);

	if (fences == 0) {
		if (adev->pm.dpm_enabled)
			amdgpu_dpm_enable_uvd(adev, false);
		else
			amdgpu_device_ip_set_powergating_state(adev, AMD_IP_BLOCK_TYPE_VCN,
							       AMD_PG_STATE_GATE);
	} else {
		schedule_delayed_work(&adev->vcn.idle_work, VCN_IDLE_TIMEOUT);
	}
}

void amdgpu_vcn_ring_begin_use(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	bool set_clocks = !cancel_delayed_work_sync(&adev->vcn.idle_work);

	if (set_clocks) {
		if (adev->pm.dpm_enabled)
			amdgpu_dpm_enable_uvd(adev, true);
		else
			amdgpu_device_ip_set_powergating_state(adev, AMD_IP_BLOCK_TYPE_VCN,
							       AMD_PG_STATE_UNGATE);
	}
}

void amdgpu_vcn_ring_end_use(struct amdgpu_ring *ring)
{
	schedule_delayed_work(&ring->adev->vcn.idle_work, VCN_IDLE_TIMEOUT);
}

int amdgpu_vcn_dec_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t tmp = 0;
	unsigned i;
	int r;

	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_CONTEXT_ID), 0xCAFEDEAD);
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
		tmp = RREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_CONTEXT_ID));
		if (tmp == 0xDEADBEEF)
			break;
		DRM_UDELAY(1);
	}

	if (i < adev->usec_timeout) {
		DRM_DEBUG("ring test on %d succeeded in %d usecs\n",
			 ring->idx, i);
	} else {
		DRM_ERROR("amdgpu: ring %d test failed (0x%08X)\n",
			  ring->idx, tmp);
		r = -EINVAL;
	}
	return r;
}

static int amdgpu_vcn_dec_send_msg(struct amdgpu_ring *ring,
				   struct amdgpu_bo *bo,
				   struct dma_fence **fence)
{
	struct amdgpu_device *adev = ring->adev;
	struct dma_fence *f = NULL;
	struct amdgpu_job *job;
	struct amdgpu_ib *ib;
	uint64_t addr;
	int i, r;

	r = amdgpu_job_alloc_with_ib(adev, 64, &job);
	if (r)
		goto err;

	ib = &job->ibs[0];
	addr = amdgpu_bo_gpu_offset(bo);
	ib->ptr[0] = PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA0), 0);
	ib->ptr[1] = addr;
	ib->ptr[2] = PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_DATA1), 0);
	ib->ptr[3] = addr >> 32;
	ib->ptr[4] = PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_GPCOM_VCPU_CMD), 0);
	ib->ptr[5] = 0;
	for (i = 6; i < 16; i += 2) {
		ib->ptr[i] = PACKET0(SOC15_REG_OFFSET(UVD, 0, mmUVD_NO_OP), 0);
		ib->ptr[i+1] = 0;
	}
	ib->length_dw = 16;

	r = amdgpu_job_submit_direct(job, ring, &f);
	if (r)
		goto err_free;

	amdgpu_bo_fence(bo, f, false);
	amdgpu_bo_unreserve(bo);
	amdgpu_bo_unref(&bo);

	if (fence)
		*fence = dma_fence_get(f);
	dma_fence_put(f);

	return 0;

err_free:
	amdgpu_job_free(job);

err:
	amdgpu_bo_unreserve(bo);
	amdgpu_bo_unref(&bo);
	return r;
}

static int amdgpu_vcn_dec_get_create_msg(struct amdgpu_ring *ring, uint32_t handle,
			      struct dma_fence **fence)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_bo *bo = NULL;
	uint32_t *msg;
	int r, i;

	r = amdgpu_bo_create_reserved(adev, 1024, PAGE_SIZE,
				      AMDGPU_GEM_DOMAIN_VRAM,
				      &bo, NULL, (void **)&msg);
	if (r)
		return r;

	msg[0] = cpu_to_le32(0x00000028);
	msg[1] = cpu_to_le32(0x00000038);
	msg[2] = cpu_to_le32(0x00000001);
	msg[3] = cpu_to_le32(0x00000000);
	msg[4] = cpu_to_le32(handle);
	msg[5] = cpu_to_le32(0x00000000);
	msg[6] = cpu_to_le32(0x00000001);
	msg[7] = cpu_to_le32(0x00000028);
	msg[8] = cpu_to_le32(0x00000010);
	msg[9] = cpu_to_le32(0x00000000);
	msg[10] = cpu_to_le32(0x00000007);
	msg[11] = cpu_to_le32(0x00000000);
	msg[12] = cpu_to_le32(0x00000780);
	msg[13] = cpu_to_le32(0x00000440);
	for (i = 14; i < 1024; ++i)
		msg[i] = cpu_to_le32(0x0);

	return amdgpu_vcn_dec_send_msg(ring, bo, fence);
}

static int amdgpu_vcn_dec_get_destroy_msg(struct amdgpu_ring *ring, uint32_t handle,
			       struct dma_fence **fence)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_bo *bo = NULL;
	uint32_t *msg;
	int r, i;

	r = amdgpu_bo_create_reserved(adev, 1024, PAGE_SIZE,
				      AMDGPU_GEM_DOMAIN_VRAM,
				      &bo, NULL, (void **)&msg);
	if (r)
		return r;

	msg[0] = cpu_to_le32(0x00000028);
	msg[1] = cpu_to_le32(0x00000018);
	msg[2] = cpu_to_le32(0x00000000);
	msg[3] = cpu_to_le32(0x00000002);
	msg[4] = cpu_to_le32(handle);
	msg[5] = cpu_to_le32(0x00000000);
	for (i = 6; i < 1024; ++i)
		msg[i] = cpu_to_le32(0x0);

	return amdgpu_vcn_dec_send_msg(ring, bo, fence);
}

int amdgpu_vcn_dec_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct dma_fence *fence;
	long r;

	r = amdgpu_vcn_dec_get_create_msg(ring, 1, NULL);
	if (r) {
		DRM_ERROR("amdgpu: failed to get create msg (%ld).\n", r);
		goto error;
	}

	r = amdgpu_vcn_dec_get_destroy_msg(ring, 1, &fence);
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
		DRM_DEBUG("ib test on ring %d succeeded\n",  ring->idx);
		r = 0;
	}

	dma_fence_put(fence);

error:
	return r;
}

int amdgpu_vcn_enc_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t rptr;
	unsigned i;
	int r;

	r = amdgpu_ring_alloc(ring, 16);
	if (r) {
		DRM_ERROR("amdgpu: vcn enc failed to lock ring %d (%d).\n",
			  ring->idx, r);
		return r;
	}

	rptr = amdgpu_ring_get_rptr(ring);

	amdgpu_ring_write(ring, VCN_ENC_CMD_END);
	amdgpu_ring_commit(ring);

	for (i = 0; i < adev->usec_timeout; i++) {
		if (amdgpu_ring_get_rptr(ring) != rptr)
			break;
		DRM_UDELAY(1);
	}

	if (i < adev->usec_timeout) {
		DRM_DEBUG("ring test on %d succeeded in %d usecs\n",
			 ring->idx, i);
	} else {
		DRM_ERROR("amdgpu: ring %d test failed\n",
			  ring->idx);
		r = -ETIMEDOUT;
	}

	return r;
}

static int amdgpu_vcn_enc_get_create_msg(struct amdgpu_ring *ring, uint32_t handle,
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
	ib->ptr[ib->length_dw++] = upper_32_bits(dummy);
	ib->ptr[ib->length_dw++] = dummy;
	ib->ptr[ib->length_dw++] = 0x0000000b;

	ib->ptr[ib->length_dw++] = 0x00000014;
	ib->ptr[ib->length_dw++] = 0x00000002; /* task info */
	ib->ptr[ib->length_dw++] = 0x0000001c;
	ib->ptr[ib->length_dw++] = 0x00000000;
	ib->ptr[ib->length_dw++] = 0x00000000;

	ib->ptr[ib->length_dw++] = 0x00000008;
	ib->ptr[ib->length_dw++] = 0x08000001; /* op initialize */

	for (i = ib->length_dw; i < ib_size_dw; ++i)
		ib->ptr[i] = 0x0;

	r = amdgpu_job_submit_direct(job, ring, &f);
	if (r)
		goto err;

	if (fence)
		*fence = dma_fence_get(f);
	dma_fence_put(f);

	return 0;

err:
	amdgpu_job_free(job);
	return r;
}

static int amdgpu_vcn_enc_get_destroy_msg(struct amdgpu_ring *ring, uint32_t handle,
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
	ib->ptr[ib->length_dw++] = 0x00000001;
	ib->ptr[ib->length_dw++] = handle;
	ib->ptr[ib->length_dw++] = upper_32_bits(dummy);
	ib->ptr[ib->length_dw++] = dummy;
	ib->ptr[ib->length_dw++] = 0x0000000b;

	ib->ptr[ib->length_dw++] = 0x00000014;
	ib->ptr[ib->length_dw++] = 0x00000002;
	ib->ptr[ib->length_dw++] = 0x0000001c;
	ib->ptr[ib->length_dw++] = 0x00000000;
	ib->ptr[ib->length_dw++] = 0x00000000;

	ib->ptr[ib->length_dw++] = 0x00000008;
	ib->ptr[ib->length_dw++] = 0x08000002; /* op close session */

	for (i = ib->length_dw; i < ib_size_dw; ++i)
		ib->ptr[i] = 0x0;

	r = amdgpu_job_submit_direct(job, ring, &f);
	if (r)
		goto err;

	if (fence)
		*fence = dma_fence_get(f);
	dma_fence_put(f);

	return 0;

err:
	amdgpu_job_free(job);
	return r;
}

int amdgpu_vcn_enc_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct dma_fence *fence = NULL;
	long r;

	r = amdgpu_vcn_enc_get_create_msg(ring, 1, NULL);
	if (r) {
		DRM_ERROR("amdgpu: failed to get create msg (%ld).\n", r);
		goto error;
	}

	r = amdgpu_vcn_enc_get_destroy_msg(ring, 1, &fence);
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
		DRM_DEBUG("ib test on ring %d succeeded\n", ring->idx);
		r = 0;
	}
error:
	dma_fence_put(fence);
	return r;
}

int amdgpu_vcn_jpeg_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t tmp = 0;
	unsigned i;
	int r;

	WREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_CONTEXT_ID), 0xCAFEDEAD);
	r = amdgpu_ring_alloc(ring, 3);

	if (r) {
		DRM_ERROR("amdgpu: cp failed to lock ring %d (%d).\n",
				  ring->idx, r);
		return r;
	}

	amdgpu_ring_write(ring,
		PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_CONTEXT_ID), 0, 0, 0));
	amdgpu_ring_write(ring, 0xDEADBEEF);
	amdgpu_ring_commit(ring);

	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = RREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_CONTEXT_ID));
		if (tmp == 0xDEADBEEF)
			break;
		DRM_UDELAY(1);
	}

	if (i < adev->usec_timeout) {
		DRM_DEBUG("ring test on %d succeeded in %d usecs\n",
				  ring->idx, i);
	} else {
		DRM_ERROR("amdgpu: ring %d test failed (0x%08X)\n",
				  ring->idx, tmp);
		r = -EINVAL;
	}

	return r;
}

static int amdgpu_vcn_jpeg_set_reg(struct amdgpu_ring *ring, uint32_t handle,
		struct dma_fence **fence)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_job *job;
	struct amdgpu_ib *ib;
	struct dma_fence *f = NULL;
	const unsigned ib_size_dw = 16;
	int i, r;

	r = amdgpu_job_alloc_with_ib(ring->adev, ib_size_dw * 4, &job);
	if (r)
		return r;

	ib = &job->ibs[0];

	ib->ptr[0] = PACKETJ(SOC15_REG_OFFSET(UVD, 0, mmUVD_JPEG_PITCH), 0, 0, PACKETJ_TYPE0);
	ib->ptr[1] = 0xDEADBEEF;
	for (i = 2; i < 16; i += 2) {
		ib->ptr[i] = PACKETJ(0, 0, 0, PACKETJ_TYPE6);
		ib->ptr[i+1] = 0;
	}
	ib->length_dw = 16;

	r = amdgpu_job_submit_direct(job, ring, &f);
	if (r)
		goto err;

	if (fence)
		*fence = dma_fence_get(f);
	dma_fence_put(f);

	return 0;

err:
	amdgpu_job_free(job);
	return r;
}

int amdgpu_vcn_jpeg_ring_test_ib(struct amdgpu_ring *ring, long timeout)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t tmp = 0;
	unsigned i;
	struct dma_fence *fence = NULL;
	long r = 0;

	r = amdgpu_vcn_jpeg_set_reg(ring, 1, &fence);
	if (r) {
		DRM_ERROR("amdgpu: failed to set jpeg register (%ld).\n", r);
		goto error;
	}

	r = dma_fence_wait_timeout(fence, false, timeout);
	if (r == 0) {
		DRM_ERROR("amdgpu: IB test timed out.\n");
		r = -ETIMEDOUT;
		goto error;
	} else if (r < 0) {
		DRM_ERROR("amdgpu: fence wait failed (%ld).\n", r);
		goto error;
	} else
		r = 0;

	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = RREG32(SOC15_REG_OFFSET(UVD, 0, mmUVD_JPEG_PITCH));
		if (tmp == 0xDEADBEEF)
			break;
		DRM_UDELAY(1);
	}

	if (i < adev->usec_timeout)
		DRM_DEBUG("ib test on ring %d succeeded\n", ring->idx);
	else {
		DRM_ERROR("ib test failed (0x%08X)\n", tmp);
		r = -EINVAL;
	}

	dma_fence_put(fence);

error:
	return r;
}
