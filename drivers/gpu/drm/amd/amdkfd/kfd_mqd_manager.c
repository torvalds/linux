// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2014-2022 Advanced Micro Devices, Inc.
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

#include "kfd_mqd_manager.h"
#include "amdgpu_amdkfd.h"
#include "kfd_device_queue_manager.h"

/* Mapping queue priority to pipe priority, indexed by queue priority */
int pipe_priority_map[] = {
	KFD_PIPE_PRIORITY_CS_LOW,
	KFD_PIPE_PRIORITY_CS_LOW,
	KFD_PIPE_PRIORITY_CS_LOW,
	KFD_PIPE_PRIORITY_CS_LOW,
	KFD_PIPE_PRIORITY_CS_LOW,
	KFD_PIPE_PRIORITY_CS_LOW,
	KFD_PIPE_PRIORITY_CS_LOW,
	KFD_PIPE_PRIORITY_CS_MEDIUM,
	KFD_PIPE_PRIORITY_CS_MEDIUM,
	KFD_PIPE_PRIORITY_CS_MEDIUM,
	KFD_PIPE_PRIORITY_CS_MEDIUM,
	KFD_PIPE_PRIORITY_CS_HIGH,
	KFD_PIPE_PRIORITY_CS_HIGH,
	KFD_PIPE_PRIORITY_CS_HIGH,
	KFD_PIPE_PRIORITY_CS_HIGH,
	KFD_PIPE_PRIORITY_CS_HIGH
};

struct kfd_mem_obj *allocate_hiq_mqd(struct kfd_node *dev, struct queue_properties *q)
{
	struct kfd_mem_obj *mqd_mem_obj;

	mqd_mem_obj = kzalloc(sizeof(struct kfd_mem_obj), GFP_KERNEL);
	if (!mqd_mem_obj)
		return NULL;

	mqd_mem_obj->gtt_mem = dev->dqm->hiq_sdma_mqd.gtt_mem;
	mqd_mem_obj->gpu_addr = dev->dqm->hiq_sdma_mqd.gpu_addr;
	mqd_mem_obj->cpu_ptr = dev->dqm->hiq_sdma_mqd.cpu_ptr;

	return mqd_mem_obj;
}

struct kfd_mem_obj *allocate_sdma_mqd(struct kfd_node *dev,
					struct queue_properties *q)
{
	struct kfd_mem_obj *mqd_mem_obj;
	uint64_t offset;

	mqd_mem_obj = kzalloc(sizeof(struct kfd_mem_obj), GFP_KERNEL);
	if (!mqd_mem_obj)
		return NULL;

	offset = (q->sdma_engine_id *
		dev->kfd->device_info.num_sdma_queues_per_engine +
		q->sdma_queue_id) *
		dev->dqm->mqd_mgrs[KFD_MQD_TYPE_SDMA]->mqd_size;

	offset += dev->dqm->mqd_mgrs[KFD_MQD_TYPE_HIQ]->mqd_size *
		  NUM_XCC(dev->xcc_mask);

	mqd_mem_obj->gtt_mem = (void *)((uint64_t)dev->dqm->hiq_sdma_mqd.gtt_mem
				+ offset);
	mqd_mem_obj->gpu_addr = dev->dqm->hiq_sdma_mqd.gpu_addr + offset;
	mqd_mem_obj->cpu_ptr = (uint32_t *)((uint64_t)
				dev->dqm->hiq_sdma_mqd.cpu_ptr + offset);

	return mqd_mem_obj;
}

void free_mqd_hiq_sdma(struct mqd_manager *mm, void *mqd,
			struct kfd_mem_obj *mqd_mem_obj)
{
	WARN_ON(!mqd_mem_obj->gtt_mem);
	kfree(mqd_mem_obj);
}

void mqd_symmetrically_map_cu_mask(struct mqd_manager *mm,
		const uint32_t *cu_mask, uint32_t cu_mask_count,
		uint32_t *se_mask, uint32_t inst)
{
	struct amdgpu_cu_info *cu_info = &mm->dev->adev->gfx.cu_info;
	struct amdgpu_gfx_config *gfx_info = &mm->dev->adev->gfx.config;
	uint32_t cu_per_sh[KFD_MAX_NUM_SE][KFD_MAX_NUM_SH_PER_SE] = {0};
	bool wgp_mode_req = KFD_GC_VERSION(mm->dev) >= IP_VERSION(10, 0, 0);
	uint32_t en_mask = wgp_mode_req ? 0x3 : 0x1;
	int i, se, sh, cu, cu_bitmap_sh_mul, cu_inc = wgp_mode_req ? 2 : 1;
	uint32_t cu_active_per_node;
	int inc = cu_inc * NUM_XCC(mm->dev->xcc_mask);
	int xcc_inst = inst + ffs(mm->dev->xcc_mask) - 1;

	cu_active_per_node = cu_info->number / mm->dev->kfd->num_nodes;
	if (cu_mask_count > cu_active_per_node)
		cu_mask_count = cu_active_per_node;

	/* Exceeding these bounds corrupts the stack and indicates a coding error.
	 * Returning with no CU's enabled will hang the queue, which should be
	 * attention grabbing.
	 */
	if (gfx_info->max_shader_engines > KFD_MAX_NUM_SE) {
		dev_err(mm->dev->adev->dev,
			"Exceeded KFD_MAX_NUM_SE, chip reports %d\n",
			gfx_info->max_shader_engines);
		return;
	}
	if (gfx_info->max_sh_per_se > KFD_MAX_NUM_SH_PER_SE) {
		dev_err(mm->dev->adev->dev,
			"Exceeded KFD_MAX_NUM_SH, chip reports %d\n",
			gfx_info->max_sh_per_se * gfx_info->max_shader_engines);
		return;
	}

	cu_bitmap_sh_mul = (KFD_GC_VERSION(mm->dev) >= IP_VERSION(11, 0, 0) &&
			    KFD_GC_VERSION(mm->dev) < IP_VERSION(13, 0, 0)) ? 2 : 1;

	/* Count active CUs per SH.
	 *
	 * Some CUs in an SH may be disabled.	HW expects disabled CUs to be
	 * represented in the high bits of each SH's enable mask (the upper and lower
	 * 16 bits of se_mask) and will take care of the actual distribution of
	 * disabled CUs within each SH automatically.
	 * Each half of se_mask must be filled only on bits 0-cu_per_sh[se][sh]-1.
	 *
	 * See note on Arcturus cu_bitmap layout in gfx_v9_0_get_cu_info.
	 * See note on GFX11 cu_bitmap layout in gfx_v11_0_get_cu_info.
	 */
	for (se = 0; se < gfx_info->max_shader_engines; se++)
		for (sh = 0; sh < gfx_info->max_sh_per_se; sh++)
			cu_per_sh[se][sh] = hweight32(
				cu_info->bitmap[xcc_inst][se % 4][sh + (se / 4) *
				cu_bitmap_sh_mul]);

	/* Symmetrically map cu_mask to all SEs & SHs:
	 * se_mask programs up to 2 SH in the upper and lower 16 bits.
	 *
	 * Examples
	 * Assuming 1 SH/SE, 4 SEs:
	 * cu_mask[0] bit0 -> se_mask[0] bit0
	 * cu_mask[0] bit1 -> se_mask[1] bit0
	 * ...
	 * cu_mask[0] bit4 -> se_mask[0] bit1
	 * ...
	 *
	 * Assuming 2 SH/SE, 4 SEs
	 * cu_mask[0] bit0 -> se_mask[0] bit0 (SE0,SH0,CU0)
	 * cu_mask[0] bit1 -> se_mask[1] bit0 (SE1,SH0,CU0)
	 * ...
	 * cu_mask[0] bit4 -> se_mask[0] bit16 (SE0,SH1,CU0)
	 * cu_mask[0] bit5 -> se_mask[1] bit16 (SE1,SH1,CU0)
	 * ...
	 * cu_mask[0] bit8 -> se_mask[0] bit1 (SE0,SH0,CU1)
	 * ...
	 *
	 * For GFX 9.4.3, the following code only looks at a
	 * subset of the cu_mask corresponding to the inst parameter.
	 * If we have n XCCs under one GPU node
	 * cu_mask[0] bit0 -> XCC0 se_mask[0] bit0 (XCC0,SE0,SH0,CU0)
	 * cu_mask[0] bit1 -> XCC1 se_mask[0] bit0 (XCC1,SE0,SH0,CU0)
	 * ..
	 * cu_mask[0] bitn -> XCCn se_mask[0] bit0 (XCCn,SE0,SH0,CU0)
	 * cu_mask[0] bit n+1 -> XCC0 se_mask[1] bit0 (XCC0,SE1,SH0,CU0)
	 *
	 * For example, if there are 6 XCCs under 1 KFD node, this code
	 * running for each inst, will look at the bits as:
	 * inst, inst + 6, inst + 12...
	 *
	 * First ensure all CUs are disabled, then enable user specified CUs.
	 */
	for (i = 0; i < gfx_info->max_shader_engines; i++)
		se_mask[i] = 0;

	i = inst;
	for (cu = 0; cu < 16; cu += cu_inc) {
		for (sh = 0; sh < gfx_info->max_sh_per_se; sh++) {
			for (se = 0; se < gfx_info->max_shader_engines; se++) {
				if (cu_per_sh[se][sh] > cu) {
					if (cu_mask[i / 32] & (en_mask << (i % 32)))
						se_mask[se] |= en_mask << (cu + sh * 16);
					i += inc;
					if (i >= cu_mask_count)
						return;
				}
			}
		}
	}
}

int kfd_hiq_load_mqd_kiq(struct mqd_manager *mm, void *mqd,
		     uint32_t pipe_id, uint32_t queue_id,
		     struct queue_properties *p, struct mm_struct *mms)
{
	return mm->dev->kfd2kgd->hiq_mqd_load(mm->dev->adev, mqd, pipe_id,
					      queue_id, p->doorbell_off, 0);
}

int kfd_destroy_mqd_cp(struct mqd_manager *mm, void *mqd,
		enum kfd_preempt_type type, unsigned int timeout,
		uint32_t pipe_id, uint32_t queue_id)
{
	return mm->dev->kfd2kgd->hqd_destroy(mm->dev->adev, mqd, type, timeout,
						pipe_id, queue_id, 0);
}

void kfd_free_mqd_cp(struct mqd_manager *mm, void *mqd,
	      struct kfd_mem_obj *mqd_mem_obj)
{
	if (mqd_mem_obj->gtt_mem) {
		amdgpu_amdkfd_free_gtt_mem(mm->dev->adev, &mqd_mem_obj->gtt_mem);
		kfree(mqd_mem_obj);
	} else {
		kfd_gtt_sa_free(mm->dev, mqd_mem_obj);
	}
}

bool kfd_is_occupied_cp(struct mqd_manager *mm, void *mqd,
		 uint64_t queue_address, uint32_t pipe_id,
		 uint32_t queue_id)
{
	return mm->dev->kfd2kgd->hqd_is_occupied(mm->dev->adev, queue_address,
						pipe_id, queue_id, 0);
}

int kfd_load_mqd_sdma(struct mqd_manager *mm, void *mqd,
		  uint32_t pipe_id, uint32_t queue_id,
		  struct queue_properties *p, struct mm_struct *mms)
{
	return mm->dev->kfd2kgd->hqd_sdma_load(mm->dev->adev, mqd,
						(uint32_t __user *)p->write_ptr,
						mms);
}

/*
 * preempt type here is ignored because there is only one way
 * to preempt sdma queue
 */
int kfd_destroy_mqd_sdma(struct mqd_manager *mm, void *mqd,
		     enum kfd_preempt_type type,
		     unsigned int timeout, uint32_t pipe_id,
		     uint32_t queue_id)
{
	return mm->dev->kfd2kgd->hqd_sdma_destroy(mm->dev->adev, mqd, timeout);
}

bool kfd_is_occupied_sdma(struct mqd_manager *mm, void *mqd,
		      uint64_t queue_address, uint32_t pipe_id,
		      uint32_t queue_id)
{
	return mm->dev->kfd2kgd->hqd_sdma_is_occupied(mm->dev->adev, mqd);
}

uint64_t kfd_hiq_mqd_stride(struct kfd_node *dev)
{
	return dev->dqm->mqd_mgrs[KFD_MQD_TYPE_HIQ]->mqd_size;
}

void kfd_get_hiq_xcc_mqd(struct kfd_node *dev, struct kfd_mem_obj *mqd_mem_obj,
		     uint32_t virtual_xcc_id)
{
	uint64_t offset;

	offset = kfd_hiq_mqd_stride(dev) * virtual_xcc_id;

	mqd_mem_obj->gtt_mem = (virtual_xcc_id == 0) ?
			dev->dqm->hiq_sdma_mqd.gtt_mem : NULL;
	mqd_mem_obj->gpu_addr = dev->dqm->hiq_sdma_mqd.gpu_addr + offset;
	mqd_mem_obj->cpu_ptr = (uint32_t *)((uintptr_t)
				dev->dqm->hiq_sdma_mqd.cpu_ptr + offset);
}

uint64_t kfd_mqd_stride(struct mqd_manager *mm,
			struct queue_properties *q)
{
	return mm->mqd_size;
}

bool kfd_check_hiq_mqd_doorbell_id(struct kfd_node *node, uint32_t doorbell_id,
				   uint32_t inst)
{
	if (doorbell_id) {
		struct device *dev = node->adev->dev;

		if (node->adev->xcp_mgr && node->adev->xcp_mgr->num_xcps > 0)
			dev_err(dev, "XCC %d: Queue preemption failed for queue with doorbell_id: %x\n",
							inst, doorbell_id);
		else
			dev_err(dev, "Queue preemption failed for queue with doorbell_id: %x\n",
							doorbell_id);
		return true;
	}

	return false;
}
