/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

struct kfd_mem_obj *allocate_hiq_mqd(struct kfd_dev *dev)
{
	struct kfd_mem_obj *mqd_mem_obj = NULL;

	mqd_mem_obj = kzalloc(sizeof(struct kfd_mem_obj), GFP_KERNEL);
	if (!mqd_mem_obj)
		return NULL;

	mqd_mem_obj->gtt_mem = dev->dqm->hiq_sdma_mqd.gtt_mem;
	mqd_mem_obj->gpu_addr = dev->dqm->hiq_sdma_mqd.gpu_addr;
	mqd_mem_obj->cpu_ptr = dev->dqm->hiq_sdma_mqd.cpu_ptr;

	return mqd_mem_obj;
}

struct kfd_mem_obj *allocate_sdma_mqd(struct kfd_dev *dev,
					struct queue_properties *q)
{
	struct kfd_mem_obj *mqd_mem_obj = NULL;
	uint64_t offset;

	mqd_mem_obj = kzalloc(sizeof(struct kfd_mem_obj), GFP_KERNEL);
	if (!mqd_mem_obj)
		return NULL;

	offset = (q->sdma_engine_id *
		dev->device_info->num_sdma_queues_per_engine +
		q->sdma_queue_id) *
		dev->dqm->mqd_mgrs[KFD_MQD_TYPE_SDMA]->mqd_size;

	offset += dev->dqm->mqd_mgrs[KFD_MQD_TYPE_HIQ]->mqd_size;

	mqd_mem_obj->gtt_mem = (void *)((uint64_t)dev->dqm->hiq_sdma_mqd.gtt_mem
				+ offset);
	mqd_mem_obj->gpu_addr = dev->dqm->hiq_sdma_mqd.gpu_addr + offset;
	mqd_mem_obj->cpu_ptr = (uint32_t *)((uint64_t)
				dev->dqm->hiq_sdma_mqd.cpu_ptr + offset);

	return mqd_mem_obj;
}

void uninit_mqd_hiq_sdma(struct mqd_manager *mm, void *mqd,
			struct kfd_mem_obj *mqd_mem_obj)
{
	WARN_ON(!mqd_mem_obj->gtt_mem);
	kfree(mqd_mem_obj);
}

void mqd_symmetrically_map_cu_mask(struct mqd_manager *mm,
		const uint32_t *cu_mask, uint32_t cu_mask_count,
		uint32_t *se_mask)
{
	struct kfd_cu_info cu_info;
	uint32_t cu_per_sh[4] = {0};
	int i, se, cu = 0;

	amdgpu_amdkfd_get_cu_info(mm->dev->kgd, &cu_info);

	if (cu_mask_count > cu_info.cu_active_number)
		cu_mask_count = cu_info.cu_active_number;

	for (se = 0; se < cu_info.num_shader_engines; se++)
		for (i = 0; i < 4; i++)
			cu_per_sh[se] += hweight32(cu_info.cu_bitmap[se][i]);

	/* Symmetrically map cu_mask to all SEs:
	 * cu_mask[0] bit0 -> se_mask[0] bit0;
	 * cu_mask[0] bit1 -> se_mask[1] bit0;
	 * ... (if # SE is 4)
	 * cu_mask[0] bit4 -> se_mask[0] bit1;
	 * ...
	 */
	se = 0;
	for (i = 0; i < cu_mask_count; i++) {
		if (cu_mask[i / 32] & (1 << (i % 32)))
			se_mask[se] |= 1 << cu;

		do {
			se++;
			if (se == cu_info.num_shader_engines) {
				se = 0;
				cu++;
			}
		} while (cu >= cu_per_sh[se] && cu < 32);
	}
}
