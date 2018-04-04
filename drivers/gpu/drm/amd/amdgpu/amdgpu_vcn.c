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
	struct amdgpu_ring *ring;
	struct drm_sched_rq *rq;
	unsigned long bo_size;
	const char *fw_name;
	const struct common_firmware_header *hdr;
	unsigned version_major, version_minor, family_id;
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
	family_id = le32_to_cpu(hdr->ucode_version) & 0xff;
	version_major = (le32_to_cpu(hdr->ucode_version) >> 24) & 0xff;
	version_minor = (le32_to_cpu(hdr->ucode_version) >> 8) & 0xff;
	DRM_INFO("Found VCN firmware Version: %hu.%hu Family ID: %hu\n",
		version_major, version_minor, family_id);


	bo_size = AMDGPU_GPU_PAGE_ALIGN(le32_to_cpu(hdr->ucode_size_bytes) + 8)
		  +  AMDGPU_VCN_STACK_SIZE + AMDGPU_VCN_HEAP_SIZE
		  +  AMDGPU_VCN_SESSION_SIZE * 40;
	r = amdgpu_bo_create_kernel(adev, bo_size, PAGE_SIZE,
				    AMDGPU_GEM_DOMAIN_VRAM, &adev->vcn.vcpu_bo,
				    &adev->vcn.gpu_addr, &adev->vcn.cpu_addr);
	if (r) {
		dev_err(adev->dev, "(%d) failed to allocate vcn bo\n", r);
		return r;
	}

	ring = &adev->vcn.ring_dec;
	rq = &ring->sched.sched_rq[DRM_SCHED_PRIORITY_NORMAL];
	r = drm_sched_entity_init(&ring->sched, &adev->vcn.entity_dec,
				  rq, amdgpu_sched_jobs, NULL);
	if (r != 0) {
		DRM_ERROR("Failed setting up VCN dec run queue.\n");
		return r;
	}

	ring = &adev->vcn.ring_enc[0];
	rq = &ring->sched.sched_rq[DRM_SCHED_PRIORITY_NORMAL];
	r = drm_sched_entity_init(&ring->sched, &adev->vcn.entity_enc,
				  rq, amdgpu_sched_jobs, NULL);
	if (r != 0) {
		DRM_ERROR("Failed setting up VCN enc run queue.\n");
		return r;
	}

	return 0;
}

int amdgpu_vcn_sw_fini(struct amdgpu_device *adev)
{
	int i;

	kfree(adev->vcn.saved_bo);

	drm_sched_entity_fini(&adev->vcn.ring_dec.sched, &adev->vcn.entity_dec);

	drm_sched_entity_fini(&adev->vcn.ring_enc[0].sched, &adev->vcn.entity_enc);

	amdgpu_bo_free_kernel(&adev->vcn.vcpu_bo,
			      &adev->vcn.gpu_addr,
			      (void **)&adev->vcn.cpu_addr);

	amdgpu_ring_fini(&adev->vcn.ring_dec);

	for (i = 0; i < adev->vcn.num_enc_rings; ++i)
		amdgpu_ring_fini(&adev->vcn.ring_enc[i]);

	release_firmware(adev->vcn.fw);

	return 0;
}

int amdgpu_vcn_suspend(struct amdgpu_device *adev)
{
	unsigned size;
	void *ptr;

	if (adev->vcn.vcpu_bo == NULL)
		return 0;

	cancel_delayed_work_sync(&adev->vcn.idle_work);

	size = amdgpu_bo_size(adev->vcn.vcpu_bo);
	ptr = adev->vcn.cpu_addr;

	adev->vcn.saved_bo = kmalloc(size, GFP_KERNEL);
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
		kfree(adev->vcn.saved_bo);
		adev->vcn.saved_bo = NULL;
	} else {
		const struct common_firmware_header *hdr;
		unsigned offset;

		hdr = (const struct common_firmware_header *)adev->vcn.fw->data;
		offset = le32_to_cpu(hdr->ucode_array_offset_bytes);
		memcpy_toio(adev->vcn.cpu_addr, adev->vcn.fw->data + offset,
			    le32_to_cpu(hdr->ucode_size_bytes));
		size -= le32_to_cpu(hdr->ucode_size_bytes);
		ptr += le32_to_cpu(hdr->ucode_size_bytes);
		memset_io(ptr, 0, size);
	}

	return 0;
}

static void amdgpu_vcn_idle_work_handler(struct work_struct *work)
{
	struct amdgpu_device *adev =
		container_of(work, struct amdgpu_device, vcn.idle_work.work);
	unsigned fences = amdgpu_fence_count_emitted(&adev->vcn.ring_dec);

	if (fences == 0) {
		if (adev->pm.dpm_enabled) {
			/* might be used when with pg/cg
			amdgpu_dpm_enable_uvd(adev, false);
			*/
		}
	} else {
		schedule_delayed_work(&adev->vcn.idle_work, VCN_IDLE_TIMEOUT);
	}
}

void amdgpu_vcn_ring_begin_use(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	bool set_clocks = !cancel_delayed_work_sync(&adev->vcn.idle_work);

	if (set_clocks && adev->pm.dpm_enabled) {
		/* might be used when with pg/cg
		amdgpu_dpm_enable_uvd(adev, true);
		*/
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

static int amdgpu_vcn_dec_send_msg(struct amdgpu_ring *ring, struct amdgpu_bo *bo,
			       bool direct, struct dma_fence **fence)
{
	struct ttm_operation_ctx ctx = { true, false };
	struct ttm_validate_buffer tv;
	struct ww_acquire_ctx ticket;
	struct list_head head;
	struct amdgpu_job *job;
	struct amdgpu_ib *ib;
	struct dma_fence *f = NULL;
	struct amdgpu_device *adev = ring->adev;
	uint64_t addr;
	int i, r;

	memset(&tv, 0, sizeof(tv));
	tv.bo = &bo->tbo;

	INIT_LIST_HEAD(&head);
	list_add(&tv.head, &head);

	r = ttm_eu_reserve_buffers(&ticket, &head, true, NULL);
	if (r)
		return r;

	r = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
	if (r)
		goto err;

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

	if (direct) {
		r = amdgpu_ib_schedule(ring, 1, ib, NULL, &f);
		job->fence = dma_fence_get(f);
		if (r)
			goto err_free;

		amdgpu_job_free(job);
	} else {
		r = amdgpu_job_submit(job, ring, &adev->vcn.entity_dec,
				      AMDGPU_FENCE_OWNER_UNDEFINED, &f);
		if (r)
			goto err_free;
	}

	ttm_eu_fence_buffer_objects(&ticket, &head, f);

	if (fence)
		*fence = dma_fence_get(f);
	amdgpu_bo_unref(&bo);
	dma_fence_put(f);

	return 0;

err_free:
	amdgpu_job_free(job);

err:
	ttm_eu_backoff_reservation(&ticket, &head);
	return r;
}

static int amdgpu_vcn_dec_get_create_msg(struct amdgpu_ring *ring, uint32_t handle,
			      struct dma_fence **fence)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_bo *bo;
	uint32_t *msg;
	int r, i;

	r = amdgpu_bo_create(adev, 1024, PAGE_SIZE, true,
			     AMDGPU_GEM_DOMAIN_VRAM,
			     AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED |
			     AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS,
			     NULL, NULL, 0, &bo);
	if (r)
		return r;

	r = amdgpu_bo_reserve(bo, false);
	if (r) {
		amdgpu_bo_unref(&bo);
		return r;
	}

	r = amdgpu_bo_kmap(bo, (void **)&msg);
	if (r) {
		amdgpu_bo_unreserve(bo);
		amdgpu_bo_unref(&bo);
		return r;
	}

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

	amdgpu_bo_kunmap(bo);
	amdgpu_bo_unreserve(bo);

	return amdgpu_vcn_dec_send_msg(ring, bo, true, fence);
}

static int amdgpu_vcn_dec_get_destroy_msg(struct amdgpu_ring *ring, uint32_t handle,
			       bool direct, struct dma_fence **fence)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_bo *bo;
	uint32_t *msg;
	int r, i;

	r = amdgpu_bo_create(adev, 1024, PAGE_SIZE, true,
			     AMDGPU_GEM_DOMAIN_VRAM,
			     AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED |
			     AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS,
			     NULL, NULL, 0, &bo);
	if (r)
		return r;

	r = amdgpu_bo_reserve(bo, false);
	if (r) {
		amdgpu_bo_unref(&bo);
		return r;
	}

	r = amdgpu_bo_kmap(bo, (void **)&msg);
	if (r) {
		amdgpu_bo_unreserve(bo);
		amdgpu_bo_unref(&bo);
		return r;
	}

	msg[0] = cpu_to_le32(0x00000028);
	msg[1] = cpu_to_le32(0x00000018);
	msg[2] = cpu_to_le32(0x00000000);
	msg[3] = cpu_to_le32(0x00000002);
	msg[4] = cpu_to_le32(handle);
	msg[5] = cpu_to_le32(0x00000000);
	for (i = 6; i < 1024; ++i)
		msg[i] = cpu_to_le32(0x0);

	amdgpu_bo_kunmap(bo);
	amdgpu_bo_unreserve(bo);

	return amdgpu_vcn_dec_send_msg(ring, bo, direct, fence);
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

	r = amdgpu_vcn_dec_get_destroy_msg(ring, 1, true, &fence);
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
	uint32_t rptr = amdgpu_ring_get_rptr(ring);
	unsigned i;
	int r;

	r = amdgpu_ring_alloc(ring, 16);
	if (r) {
		DRM_ERROR("amdgpu: vcn enc failed to lock ring %d (%d).\n",
			  ring->idx, r);
		return r;
	}
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
