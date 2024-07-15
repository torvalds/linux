// SPDX-License-Identifier: MIT
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
#include <drm/drm_exec.h>

#include "amdgpu.h"
#include "amdgpu_umsch_mm.h"
#include "umsch_mm_v4_0.h"

struct umsch_mm_test_ctx_data {
	uint8_t process_csa[PAGE_SIZE];
	uint8_t vpe_ctx_csa[PAGE_SIZE];
	uint8_t vcn_ctx_csa[PAGE_SIZE];
};

struct umsch_mm_test_mqd_data {
	uint8_t vpe_mqd[PAGE_SIZE];
	uint8_t vcn_mqd[PAGE_SIZE];
};

struct umsch_mm_test_ring_data {
	uint8_t vpe_ring[PAGE_SIZE];
	uint8_t vpe_ib[PAGE_SIZE];
	uint8_t vcn_ring[PAGE_SIZE];
	uint8_t vcn_ib[PAGE_SIZE];
};

struct umsch_mm_test_queue_info {
	uint64_t mqd_addr;
	uint64_t csa_addr;
	uint32_t doorbell_offset_0;
	uint32_t doorbell_offset_1;
	enum UMSCH_SWIP_ENGINE_TYPE engine;
};

struct umsch_mm_test {
	struct amdgpu_bo	*ctx_data_obj;
	uint64_t		ctx_data_gpu_addr;
	uint32_t		*ctx_data_cpu_addr;

	struct amdgpu_bo	*mqd_data_obj;
	uint64_t		mqd_data_gpu_addr;
	uint32_t		*mqd_data_cpu_addr;

	struct amdgpu_bo	*ring_data_obj;
	uint64_t		ring_data_gpu_addr;
	uint32_t		*ring_data_cpu_addr;


	struct amdgpu_vm	*vm;
	struct amdgpu_bo_va	*bo_va;
	uint32_t		pasid;
	uint32_t		vm_cntx_cntl;
	uint32_t		num_queues;
};

static int map_ring_data(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			  struct amdgpu_bo *bo, struct amdgpu_bo_va **bo_va,
			  uint64_t addr, uint32_t size)
{
	struct amdgpu_sync sync;
	struct drm_exec exec;
	int r;

	amdgpu_sync_create(&sync);

	drm_exec_init(&exec, 0, 0);
	drm_exec_until_all_locked(&exec) {
		r = drm_exec_lock_obj(&exec, &bo->tbo.base);
		drm_exec_retry_on_contention(&exec);
		if (unlikely(r))
			goto error_fini_exec;

		r = amdgpu_vm_lock_pd(vm, &exec, 0);
		drm_exec_retry_on_contention(&exec);
		if (unlikely(r))
			goto error_fini_exec;
	}

	*bo_va = amdgpu_vm_bo_add(adev, vm, bo);
	if (!*bo_va) {
		r = -ENOMEM;
		goto error_fini_exec;
	}

	r = amdgpu_vm_bo_map(adev, *bo_va, addr, 0, size,
			     AMDGPU_PTE_READABLE | AMDGPU_PTE_WRITEABLE |
			     AMDGPU_PTE_EXECUTABLE);

	if (r)
		goto error_del_bo_va;


	r = amdgpu_vm_bo_update(adev, *bo_va, false);
	if (r)
		goto error_del_bo_va;

	amdgpu_sync_fence(&sync, (*bo_va)->last_pt_update);

	r = amdgpu_vm_update_pdes(adev, vm, false);
	if (r)
		goto error_del_bo_va;

	amdgpu_sync_fence(&sync, vm->last_update);

	amdgpu_sync_wait(&sync, false);
	drm_exec_fini(&exec);

	amdgpu_sync_free(&sync);

	return 0;

error_del_bo_va:
	amdgpu_vm_bo_del(adev, *bo_va);
	amdgpu_sync_free(&sync);

error_fini_exec:
	drm_exec_fini(&exec);
	amdgpu_sync_free(&sync);
	return r;
}

static int unmap_ring_data(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			    struct amdgpu_bo *bo, struct amdgpu_bo_va *bo_va,
			    uint64_t addr)
{
	struct drm_exec exec;
	long r;

	drm_exec_init(&exec, 0, 0);
	drm_exec_until_all_locked(&exec) {
		r = drm_exec_lock_obj(&exec, &bo->tbo.base);
		drm_exec_retry_on_contention(&exec);
		if (unlikely(r))
			goto out_unlock;

		r = amdgpu_vm_lock_pd(vm, &exec, 0);
		drm_exec_retry_on_contention(&exec);
		if (unlikely(r))
			goto out_unlock;
	}


	r = amdgpu_vm_bo_unmap(adev, bo_va, addr);
	if (r)
		goto out_unlock;

	amdgpu_vm_bo_del(adev, bo_va);

out_unlock:
	drm_exec_fini(&exec);

	return r;
}

static void setup_vpe_queue(struct amdgpu_device *adev,
			    struct umsch_mm_test *test,
			    struct umsch_mm_test_queue_info *qinfo)
{
	struct MQD_INFO *mqd = (struct MQD_INFO *)test->mqd_data_cpu_addr;
	uint64_t ring_gpu_addr = test->ring_data_gpu_addr;

	mqd->rb_base_lo = (ring_gpu_addr >> 8);
	mqd->rb_base_hi = (ring_gpu_addr >> 40);
	mqd->rb_size = PAGE_SIZE / 4;
	mqd->wptr_val = 0;
	mqd->rptr_val = 0;
	mqd->unmapped = 1;

	if (adev->vpe.collaborate_mode)
		memcpy(++mqd, test->mqd_data_cpu_addr, sizeof(struct MQD_INFO));

	qinfo->mqd_addr = test->mqd_data_gpu_addr;
	qinfo->csa_addr = test->ctx_data_gpu_addr +
		offsetof(struct umsch_mm_test_ctx_data, vpe_ctx_csa);
	qinfo->doorbell_offset_0 = 0;
	qinfo->doorbell_offset_1 = 0;
}

static void setup_vcn_queue(struct amdgpu_device *adev,
			    struct umsch_mm_test *test,
			    struct umsch_mm_test_queue_info *qinfo)
{
}

static int add_test_queue(struct amdgpu_device *adev,
			  struct umsch_mm_test *test,
			  struct umsch_mm_test_queue_info *qinfo)
{
	struct umsch_mm_add_queue_input queue_input = {};
	int r;

	queue_input.process_id = test->pasid;
	queue_input.page_table_base_addr = amdgpu_gmc_pd_addr(test->vm->root.bo);

	queue_input.process_va_start = 0;
	queue_input.process_va_end = (adev->vm_manager.max_pfn - 1) << AMDGPU_GPU_PAGE_SHIFT;

	queue_input.process_quantum = 100000; /* 10ms */
	queue_input.process_csa_addr = test->ctx_data_gpu_addr +
				       offsetof(struct umsch_mm_test_ctx_data, process_csa);

	queue_input.context_quantum = 10000; /* 1ms */
	queue_input.context_csa_addr = qinfo->csa_addr;

	queue_input.inprocess_context_priority = CONTEXT_PRIORITY_LEVEL_NORMAL;
	queue_input.context_global_priority_level = CONTEXT_PRIORITY_LEVEL_NORMAL;
	queue_input.doorbell_offset_0 = qinfo->doorbell_offset_0;
	queue_input.doorbell_offset_1 = qinfo->doorbell_offset_1;

	queue_input.engine_type = qinfo->engine;
	queue_input.mqd_addr = qinfo->mqd_addr;
	queue_input.vm_context_cntl = test->vm_cntx_cntl;

	amdgpu_umsch_mm_lock(&adev->umsch_mm);
	r = adev->umsch_mm.funcs->add_queue(&adev->umsch_mm, &queue_input);
	amdgpu_umsch_mm_unlock(&adev->umsch_mm);
	if (r)
		return r;

	return 0;
}

static int remove_test_queue(struct amdgpu_device *adev,
			     struct umsch_mm_test *test,
			     struct umsch_mm_test_queue_info *qinfo)
{
	struct umsch_mm_remove_queue_input queue_input = {};
	int r;

	queue_input.doorbell_offset_0 = qinfo->doorbell_offset_0;
	queue_input.doorbell_offset_1 = qinfo->doorbell_offset_1;
	queue_input.context_csa_addr = qinfo->csa_addr;

	amdgpu_umsch_mm_lock(&adev->umsch_mm);
	r = adev->umsch_mm.funcs->remove_queue(&adev->umsch_mm, &queue_input);
	amdgpu_umsch_mm_unlock(&adev->umsch_mm);
	if (r)
		return r;

	return 0;
}

static int submit_vpe_queue(struct amdgpu_device *adev, struct umsch_mm_test *test)
{
	struct MQD_INFO *mqd = (struct MQD_INFO *)test->mqd_data_cpu_addr;
	uint32_t *ring = test->ring_data_cpu_addr +
		offsetof(struct umsch_mm_test_ring_data, vpe_ring) / 4;
	uint32_t *ib = test->ring_data_cpu_addr +
		offsetof(struct umsch_mm_test_ring_data, vpe_ib) / 4;
	uint64_t ib_gpu_addr = test->ring_data_gpu_addr +
		offsetof(struct umsch_mm_test_ring_data, vpe_ib);
	uint32_t *fence = ib + 2048 / 4;
	uint64_t fence_gpu_addr = ib_gpu_addr + 2048;
	const uint32_t test_pattern = 0xdeadbeef;
	int i;

	ib[0] = VPE_CMD_HEADER(VPE_CMD_OPCODE_FENCE, 0);
	ib[1] = lower_32_bits(fence_gpu_addr);
	ib[2] = upper_32_bits(fence_gpu_addr);
	ib[3] = test_pattern;

	ring[0] = VPE_CMD_HEADER(VPE_CMD_OPCODE_INDIRECT, 0);
	ring[1] = (ib_gpu_addr & 0xffffffe0);
	ring[2] = upper_32_bits(ib_gpu_addr);
	ring[3] = 4;
	ring[4] = 0;
	ring[5] = 0;

	mqd->wptr_val = (6 << 2);
	if (adev->vpe.collaborate_mode)
		(++mqd)->wptr_val = (6 << 2);

	WDOORBELL32(adev->umsch_mm.agdb_index[CONTEXT_PRIORITY_LEVEL_NORMAL], mqd->wptr_val);

	for (i = 0; i < adev->usec_timeout; i++) {
		if (*fence == test_pattern)
			return 0;
		udelay(1);
	}

	dev_err(adev->dev, "vpe queue submission timeout\n");

	return -ETIMEDOUT;
}

static int submit_vcn_queue(struct amdgpu_device *adev, struct umsch_mm_test *test)
{
	return 0;
}

static int setup_umsch_mm_test(struct amdgpu_device *adev,
			  struct umsch_mm_test *test)
{
	struct amdgpu_vmhub *hub = &adev->vmhub[AMDGPU_MMHUB0(0)];
	int r;

	test->vm_cntx_cntl = hub->vm_cntx_cntl;

	test->vm = kzalloc(sizeof(*test->vm), GFP_KERNEL);
	if (!test->vm) {
		r = -ENOMEM;
		return r;
	}

	r = amdgpu_vm_init(adev, test->vm, -1);
	if (r)
		goto error_free_vm;

	r = amdgpu_pasid_alloc(16);
	if (r < 0)
		goto error_fini_vm;
	test->pasid = r;

	r = amdgpu_bo_create_kernel(adev, sizeof(struct umsch_mm_test_ctx_data),
				    PAGE_SIZE, AMDGPU_GEM_DOMAIN_GTT,
				    &test->ctx_data_obj,
				    &test->ctx_data_gpu_addr,
				    (void **)&test->ctx_data_cpu_addr);
	if (r)
		goto error_free_pasid;

	memset(test->ctx_data_cpu_addr, 0, sizeof(struct umsch_mm_test_ctx_data));

	r = amdgpu_bo_create_kernel(adev, PAGE_SIZE,
				    PAGE_SIZE, AMDGPU_GEM_DOMAIN_GTT,
				    &test->mqd_data_obj,
				    &test->mqd_data_gpu_addr,
				    (void **)&test->mqd_data_cpu_addr);
	if (r)
		goto error_free_ctx_data_obj;

	memset(test->mqd_data_cpu_addr, 0, PAGE_SIZE);

	r = amdgpu_bo_create_kernel(adev, sizeof(struct umsch_mm_test_ring_data),
				    PAGE_SIZE, AMDGPU_GEM_DOMAIN_GTT,
				    &test->ring_data_obj,
				    NULL,
				    (void **)&test->ring_data_cpu_addr);
	if (r)
		goto error_free_mqd_data_obj;

	memset(test->ring_data_cpu_addr, 0, sizeof(struct umsch_mm_test_ring_data));

	test->ring_data_gpu_addr = AMDGPU_VA_RESERVED_BOTTOM;
	r = map_ring_data(adev, test->vm, test->ring_data_obj, &test->bo_va,
			  test->ring_data_gpu_addr, sizeof(struct umsch_mm_test_ring_data));
	if (r)
		goto error_free_ring_data_obj;

	return 0;

error_free_ring_data_obj:
	amdgpu_bo_free_kernel(&test->ring_data_obj, NULL,
			      (void **)&test->ring_data_cpu_addr);
error_free_mqd_data_obj:
	amdgpu_bo_free_kernel(&test->mqd_data_obj, &test->mqd_data_gpu_addr,
			      (void **)&test->mqd_data_cpu_addr);
error_free_ctx_data_obj:
	amdgpu_bo_free_kernel(&test->ctx_data_obj, &test->ctx_data_gpu_addr,
			      (void **)&test->ctx_data_cpu_addr);
error_free_pasid:
	amdgpu_pasid_free(test->pasid);
error_fini_vm:
	amdgpu_vm_fini(adev, test->vm);
error_free_vm:
	kfree(test->vm);

	return r;
}

static void cleanup_umsch_mm_test(struct amdgpu_device *adev,
				  struct umsch_mm_test *test)
{
	unmap_ring_data(adev, test->vm, test->ring_data_obj,
			test->bo_va, test->ring_data_gpu_addr);
	amdgpu_bo_free_kernel(&test->mqd_data_obj, &test->mqd_data_gpu_addr,
			      (void **)&test->mqd_data_cpu_addr);
	amdgpu_bo_free_kernel(&test->ring_data_obj, NULL,
			      (void **)&test->ring_data_cpu_addr);
	amdgpu_bo_free_kernel(&test->ctx_data_obj, &test->ctx_data_gpu_addr,
			       (void **)&test->ctx_data_cpu_addr);
	amdgpu_pasid_free(test->pasid);
	amdgpu_vm_fini(adev, test->vm);
	kfree(test->vm);
}

static int setup_test_queues(struct amdgpu_device *adev,
			     struct umsch_mm_test *test,
			     struct umsch_mm_test_queue_info *qinfo)
{
	int i, r;

	for (i = 0; i < test->num_queues; i++) {
		if (qinfo[i].engine == UMSCH_SWIP_ENGINE_TYPE_VPE)
			setup_vpe_queue(adev, test, &qinfo[i]);
		else
			setup_vcn_queue(adev, test, &qinfo[i]);

		r = add_test_queue(adev, test, &qinfo[i]);
		if (r)
			return r;
	}

	return 0;
}

static int submit_test_queues(struct amdgpu_device *adev,
			      struct umsch_mm_test *test,
			      struct umsch_mm_test_queue_info *qinfo)
{
	int i, r;

	for (i = 0; i < test->num_queues; i++) {
		if (qinfo[i].engine == UMSCH_SWIP_ENGINE_TYPE_VPE)
			r = submit_vpe_queue(adev, test);
		else
			r = submit_vcn_queue(adev, test);
		if (r)
			return r;
	}

	return 0;
}

static void cleanup_test_queues(struct amdgpu_device *adev,
			      struct umsch_mm_test *test,
			      struct umsch_mm_test_queue_info *qinfo)
{
	int i;

	for (i = 0; i < test->num_queues; i++)
		remove_test_queue(adev, test, &qinfo[i]);
}

static int umsch_mm_test(struct amdgpu_device *adev)
{
	struct umsch_mm_test_queue_info qinfo[] = {
		{ .engine = UMSCH_SWIP_ENGINE_TYPE_VPE },
	};
	struct umsch_mm_test test = { .num_queues = ARRAY_SIZE(qinfo) };
	int r;

	r = setup_umsch_mm_test(adev, &test);
	if (r)
		return r;

	r = setup_test_queues(adev, &test, qinfo);
	if (r)
		goto cleanup;

	r = submit_test_queues(adev, &test, qinfo);
	if (r)
		goto cleanup;

	cleanup_test_queues(adev, &test, qinfo);
	cleanup_umsch_mm_test(adev, &test);

	return 0;

cleanup:
	cleanup_test_queues(adev, &test, qinfo);
	cleanup_umsch_mm_test(adev, &test);
	return r;
}

int amdgpu_umsch_mm_submit_pkt(struct amdgpu_umsch_mm *umsch, void *pkt, int ndws)
{
	struct amdgpu_ring *ring = &umsch->ring;

	if (amdgpu_ring_alloc(ring, ndws))
		return -ENOMEM;

	amdgpu_ring_write_multiple(ring, pkt, ndws);
	amdgpu_ring_commit(ring);

	return 0;
}

int amdgpu_umsch_mm_query_fence(struct amdgpu_umsch_mm *umsch)
{
	struct amdgpu_ring *ring = &umsch->ring;
	struct amdgpu_device *adev = ring->adev;
	int r;

	r = amdgpu_fence_wait_polling(ring, ring->fence_drv.sync_seq, adev->usec_timeout);
	if (r < 1) {
		dev_err(adev->dev, "ring umsch timeout, emitted fence %u\n",
			ring->fence_drv.sync_seq);
		return -ETIMEDOUT;
	}

	return 0;
}

static void umsch_mm_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_umsch_mm *umsch = (struct amdgpu_umsch_mm *)ring;
	struct amdgpu_device *adev = ring->adev;

	if (ring->use_doorbell)
		WDOORBELL32(ring->doorbell_index, ring->wptr << 2);
	else
		WREG32(umsch->rb_wptr, ring->wptr << 2);
}

static u64 umsch_mm_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_umsch_mm *umsch = (struct amdgpu_umsch_mm *)ring;
	struct amdgpu_device *adev = ring->adev;

	return RREG32(umsch->rb_rptr);
}

static u64 umsch_mm_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_umsch_mm *umsch = (struct amdgpu_umsch_mm *)ring;
	struct amdgpu_device *adev = ring->adev;

	return RREG32(umsch->rb_wptr);
}

static const struct amdgpu_ring_funcs umsch_v4_0_ring_funcs = {
	.type = AMDGPU_RING_TYPE_UMSCH_MM,
	.align_mask = 0,
	.nop = 0,
	.support_64bit_ptrs = false,
	.get_rptr = umsch_mm_ring_get_rptr,
	.get_wptr = umsch_mm_ring_get_wptr,
	.set_wptr = umsch_mm_ring_set_wptr,
	.insert_nop = amdgpu_ring_insert_nop,
};

int amdgpu_umsch_mm_ring_init(struct amdgpu_umsch_mm *umsch)
{
	struct amdgpu_device *adev = container_of(umsch, struct amdgpu_device, umsch_mm);
	struct amdgpu_ring *ring = &umsch->ring;

	ring->vm_hub = AMDGPU_MMHUB0(0);
	ring->use_doorbell = true;
	ring->no_scheduler = true;
	ring->doorbell_index = (AMDGPU_NAVI10_DOORBELL64_VCN0_1 << 1) + 6;

	snprintf(ring->name, sizeof(ring->name), "umsch");

	return amdgpu_ring_init(adev, ring, 1024, NULL, 0, AMDGPU_RING_PRIO_DEFAULT, NULL);
}

int amdgpu_umsch_mm_init_microcode(struct amdgpu_umsch_mm *umsch)
{
	const struct umsch_mm_firmware_header_v1_0 *umsch_mm_hdr;
	struct amdgpu_device *adev = umsch->ring.adev;
	const char *fw_name = NULL;
	int r;

	switch (amdgpu_ip_version(adev, VCN_HWIP, 0)) {
	case IP_VERSION(4, 0, 5):
	case IP_VERSION(4, 0, 6):
		fw_name = "amdgpu/umsch_mm_4_0_0.bin";
		break;
	default:
		break;
	}

	r = amdgpu_ucode_request(adev, &adev->umsch_mm.fw, fw_name);
	if (r) {
		release_firmware(adev->umsch_mm.fw);
		adev->umsch_mm.fw = NULL;
		return r;
	}

	umsch_mm_hdr = (const struct umsch_mm_firmware_header_v1_0 *)adev->umsch_mm.fw->data;

	adev->umsch_mm.ucode_size = le32_to_cpu(umsch_mm_hdr->umsch_mm_ucode_size_bytes);
	adev->umsch_mm.data_size = le32_to_cpu(umsch_mm_hdr->umsch_mm_ucode_data_size_bytes);

	adev->umsch_mm.irq_start_addr =
		le32_to_cpu(umsch_mm_hdr->umsch_mm_irq_start_addr_lo) |
		((uint64_t)(le32_to_cpu(umsch_mm_hdr->umsch_mm_irq_start_addr_hi)) << 32);
	adev->umsch_mm.uc_start_addr =
		le32_to_cpu(umsch_mm_hdr->umsch_mm_uc_start_addr_lo) |
		((uint64_t)(le32_to_cpu(umsch_mm_hdr->umsch_mm_uc_start_addr_hi)) << 32);
	adev->umsch_mm.data_start_addr =
		le32_to_cpu(umsch_mm_hdr->umsch_mm_data_start_addr_lo) |
		((uint64_t)(le32_to_cpu(umsch_mm_hdr->umsch_mm_data_start_addr_hi)) << 32);

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		struct amdgpu_firmware_info *info;

		info = &adev->firmware.ucode[AMDGPU_UCODE_ID_UMSCH_MM_UCODE];
		info->ucode_id = AMDGPU_UCODE_ID_UMSCH_MM_UCODE;
		info->fw = adev->umsch_mm.fw;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(umsch_mm_hdr->umsch_mm_ucode_size_bytes), PAGE_SIZE);

		info = &adev->firmware.ucode[AMDGPU_UCODE_ID_UMSCH_MM_DATA];
		info->ucode_id = AMDGPU_UCODE_ID_UMSCH_MM_DATA;
		info->fw = adev->umsch_mm.fw;
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(umsch_mm_hdr->umsch_mm_ucode_data_size_bytes), PAGE_SIZE);
	}

	return 0;
}

int amdgpu_umsch_mm_allocate_ucode_buffer(struct amdgpu_umsch_mm *umsch)
{
	const struct umsch_mm_firmware_header_v1_0 *umsch_mm_hdr;
	struct amdgpu_device *adev = umsch->ring.adev;
	const __le32 *fw_data;
	uint32_t fw_size;
	int r;

	umsch_mm_hdr = (const struct umsch_mm_firmware_header_v1_0 *)
		       adev->umsch_mm.fw->data;

	fw_data = (const __le32 *)(adev->umsch_mm.fw->data +
		  le32_to_cpu(umsch_mm_hdr->umsch_mm_ucode_offset_bytes));
	fw_size = le32_to_cpu(umsch_mm_hdr->umsch_mm_ucode_size_bytes);

	r = amdgpu_bo_create_reserved(adev, fw_size,
				      4 * 1024, AMDGPU_GEM_DOMAIN_VRAM,
				      &adev->umsch_mm.ucode_fw_obj,
				      &adev->umsch_mm.ucode_fw_gpu_addr,
				      (void **)&adev->umsch_mm.ucode_fw_ptr);
	if (r) {
		dev_err(adev->dev, "(%d) failed to create umsch_mm fw ucode bo\n", r);
		return r;
	}

	memcpy(adev->umsch_mm.ucode_fw_ptr, fw_data, fw_size);

	amdgpu_bo_kunmap(adev->umsch_mm.ucode_fw_obj);
	amdgpu_bo_unreserve(adev->umsch_mm.ucode_fw_obj);
	return 0;
}

int amdgpu_umsch_mm_allocate_ucode_data_buffer(struct amdgpu_umsch_mm *umsch)
{
	const struct umsch_mm_firmware_header_v1_0 *umsch_mm_hdr;
	struct amdgpu_device *adev = umsch->ring.adev;
	const __le32 *fw_data;
	uint32_t fw_size;
	int r;

	umsch_mm_hdr = (const struct umsch_mm_firmware_header_v1_0 *)
		       adev->umsch_mm.fw->data;

	fw_data = (const __le32 *)(adev->umsch_mm.fw->data +
		  le32_to_cpu(umsch_mm_hdr->umsch_mm_ucode_data_offset_bytes));
	fw_size = le32_to_cpu(umsch_mm_hdr->umsch_mm_ucode_data_size_bytes);

	r = amdgpu_bo_create_reserved(adev, fw_size,
				      64 * 1024, AMDGPU_GEM_DOMAIN_VRAM,
				      &adev->umsch_mm.data_fw_obj,
				      &adev->umsch_mm.data_fw_gpu_addr,
				      (void **)&adev->umsch_mm.data_fw_ptr);
	if (r) {
		dev_err(adev->dev, "(%d) failed to create umsch_mm fw data bo\n", r);
		return r;
	}

	memcpy(adev->umsch_mm.data_fw_ptr, fw_data, fw_size);

	amdgpu_bo_kunmap(adev->umsch_mm.data_fw_obj);
	amdgpu_bo_unreserve(adev->umsch_mm.data_fw_obj);
	return 0;
}

int amdgpu_umsch_mm_psp_execute_cmd_buf(struct amdgpu_umsch_mm *umsch)
{
	struct amdgpu_device *adev = umsch->ring.adev;
	struct amdgpu_firmware_info ucode = {
		.ucode_id = AMDGPU_UCODE_ID_UMSCH_MM_CMD_BUFFER,
		.mc_addr = adev->umsch_mm.cmd_buf_gpu_addr,
		.ucode_size = ((uintptr_t)adev->umsch_mm.cmd_buf_curr_ptr -
			      (uintptr_t)adev->umsch_mm.cmd_buf_ptr),
	};

	return psp_execute_ip_fw_load(&adev->psp, &ucode);
}

static void umsch_mm_agdb_index_init(struct amdgpu_device *adev)
{
	uint32_t umsch_mm_agdb_start;
	int i;

	umsch_mm_agdb_start = adev->doorbell_index.max_assignment + 1;
	umsch_mm_agdb_start = roundup(umsch_mm_agdb_start, 1024);
	umsch_mm_agdb_start += (AMDGPU_NAVI10_DOORBELL64_VCN0_1 << 1);

	for (i = 0; i < CONTEXT_PRIORITY_NUM_LEVELS; i++)
		adev->umsch_mm.agdb_index[i] = umsch_mm_agdb_start + i;
}

static int umsch_mm_init(struct amdgpu_device *adev)
{
	int r;

	adev->umsch_mm.vmid_mask_mm_vpe = 0xf00;
	adev->umsch_mm.engine_mask = (1 << UMSCH_SWIP_ENGINE_TYPE_VPE);
	adev->umsch_mm.vpe_hqd_mask = 0xfe;

	r = amdgpu_device_wb_get(adev, &adev->umsch_mm.wb_index);
	if (r) {
		dev_err(adev->dev, "failed to alloc wb for umsch: %d\n", r);
		return r;
	}

	adev->umsch_mm.sch_ctx_gpu_addr = adev->wb.gpu_addr +
					  (adev->umsch_mm.wb_index * 4);

	r = amdgpu_bo_create_kernel(adev, PAGE_SIZE, PAGE_SIZE,
				    AMDGPU_GEM_DOMAIN_GTT,
				    &adev->umsch_mm.cmd_buf_obj,
				    &adev->umsch_mm.cmd_buf_gpu_addr,
				    (void **)&adev->umsch_mm.cmd_buf_ptr);
	if (r) {
		dev_err(adev->dev, "failed to allocate cmdbuf bo %d\n", r);
		amdgpu_device_wb_free(adev, adev->umsch_mm.wb_index);
		return r;
	}

	mutex_init(&adev->umsch_mm.mutex_hidden);

	umsch_mm_agdb_index_init(adev);

	return 0;
}


static int umsch_mm_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	switch (amdgpu_ip_version(adev, VCN_HWIP, 0)) {
	case IP_VERSION(4, 0, 5):
	case IP_VERSION(4, 0, 6):
		umsch_mm_v4_0_set_funcs(&adev->umsch_mm);
		break;
	default:
		return -EINVAL;
	}

	adev->umsch_mm.ring.funcs = &umsch_v4_0_ring_funcs;
	umsch_mm_set_regs(&adev->umsch_mm);

	return 0;
}

static int umsch_mm_late_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (amdgpu_in_reset(adev) || adev->in_s0ix || adev->in_suspend)
		return 0;

	return umsch_mm_test(adev);
}

static int umsch_mm_sw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	r = umsch_mm_init(adev);
	if (r)
		return r;

	r = umsch_mm_ring_init(&adev->umsch_mm);
	if (r)
		return r;

	r = umsch_mm_init_microcode(&adev->umsch_mm);
	if (r)
		return r;

	return 0;
}

static int umsch_mm_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	release_firmware(adev->umsch_mm.fw);
	adev->umsch_mm.fw = NULL;

	amdgpu_ring_fini(&adev->umsch_mm.ring);

	mutex_destroy(&adev->umsch_mm.mutex_hidden);

	amdgpu_bo_free_kernel(&adev->umsch_mm.cmd_buf_obj,
			      &adev->umsch_mm.cmd_buf_gpu_addr,
			      (void **)&adev->umsch_mm.cmd_buf_ptr);

	amdgpu_device_wb_free(adev, adev->umsch_mm.wb_index);

	return 0;
}

static int umsch_mm_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	r = umsch_mm_load_microcode(&adev->umsch_mm);
	if (r)
		return r;

	umsch_mm_ring_start(&adev->umsch_mm);

	r = umsch_mm_set_hw_resources(&adev->umsch_mm);
	if (r)
		return r;

	return 0;
}

static int umsch_mm_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	umsch_mm_ring_stop(&adev->umsch_mm);

	amdgpu_bo_free_kernel(&adev->umsch_mm.data_fw_obj,
			      &adev->umsch_mm.data_fw_gpu_addr,
			      (void **)&adev->umsch_mm.data_fw_ptr);

	amdgpu_bo_free_kernel(&adev->umsch_mm.ucode_fw_obj,
			      &adev->umsch_mm.ucode_fw_gpu_addr,
			      (void **)&adev->umsch_mm.ucode_fw_ptr);
	return 0;
}

static int umsch_mm_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return umsch_mm_hw_fini(adev);
}

static int umsch_mm_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return umsch_mm_hw_init(adev);
}

static const struct amd_ip_funcs umsch_mm_v4_0_ip_funcs = {
	.name = "umsch_mm_v4_0",
	.early_init = umsch_mm_early_init,
	.late_init = umsch_mm_late_init,
	.sw_init = umsch_mm_sw_init,
	.sw_fini = umsch_mm_sw_fini,
	.hw_init = umsch_mm_hw_init,
	.hw_fini = umsch_mm_hw_fini,
	.suspend = umsch_mm_suspend,
	.resume = umsch_mm_resume,
};

const struct amdgpu_ip_block_version umsch_mm_v4_0_ip_block = {
	.type = AMD_IP_BLOCK_TYPE_UMSCH_MM,
	.major = 4,
	.minor = 0,
	.rev = 0,
	.funcs = &umsch_mm_v4_0_ip_funcs,
};
