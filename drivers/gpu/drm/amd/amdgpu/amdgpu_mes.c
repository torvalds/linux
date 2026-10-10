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
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <drm/drm_exec.h>

#include "amdgpu_mes.h"
#include "amdgpu.h"
#include "soc15_common.h"
#include "amdgpu_mes_ctx.h"

#define AMDGPU_MES_MAX_NUM_OF_QUEUES_PER_PROCESS 1024
#define AMDGPU_ONE_DOORBELL_SIZE 8

int amdgpu_mes_doorbell_process_slice(struct amdgpu_device *adev)
{
	return roundup(AMDGPU_ONE_DOORBELL_SIZE *
		       AMDGPU_MES_MAX_NUM_OF_QUEUES_PER_PROCESS,
		       PAGE_SIZE);
}

static int amdgpu_mes_doorbell_init(struct amdgpu_device *adev)
{
	int i;
	struct amdgpu_mes *mes = &adev->mes;

	/* Bitmap for dynamic allocation of kernel doorbells */
	mes->doorbell_bitmap = bitmap_zalloc(PAGE_SIZE / sizeof(u32), GFP_KERNEL);
	if (!mes->doorbell_bitmap) {
		dev_err(adev->dev, "Failed to allocate MES doorbell bitmap\n");
		return -ENOMEM;
	}

	mes->num_mes_dbs = PAGE_SIZE / AMDGPU_ONE_DOORBELL_SIZE;
	for (i = 0; i < AMDGPU_MES_PRIORITY_NUM_LEVELS; i++) {
		adev->mes.aggregated_doorbells[i] = mes->db_start_dw_offset + i * 2;
		set_bit(i, mes->doorbell_bitmap);
	}

	return 0;
}

static int amdgpu_mes_event_log_init(struct amdgpu_device *adev)
{
	int r;

	if (!amdgpu_mes_log_enable)
		return 0;

	r = amdgpu_bo_create_kernel(adev, adev->mes.event_log_size, PAGE_SIZE,
				    AMDGPU_GEM_DOMAIN_VRAM,
				    &adev->mes.event_log_gpu_obj,
				    &adev->mes.event_log_gpu_addr,
				    &adev->mes.event_log_cpu_addr);
	if (r) {
		dev_warn(adev->dev, "failed to create MES event log buffer (%d)", r);
		return r;
	}

	memset(adev->mes.event_log_cpu_addr, 0, adev->mes.event_log_size);

	return  0;

}

static void amdgpu_mes_doorbell_free(struct amdgpu_device *adev)
{
	bitmap_free(adev->mes.doorbell_bitmap);
}

static inline u32 amdgpu_mes_get_hqd_mask(u32 num_pipe,
					  u32 num_hqd_per_pipe,
					  u32 num_reserved_hqd)
{
	if (num_pipe == 0)
		return 0;

	u32 total_hqd_mask = (u32)((1ULL << num_hqd_per_pipe) - 1);
	u32 reserved_hqd_mask = (u32)((1ULL << DIV_ROUND_UP(num_reserved_hqd, num_pipe)) - 1);

	return (total_hqd_mask & ~reserved_hqd_mask);
}

static void amdgpu_mes_userq_notify_unmap_work_handler(struct work_struct *work);

int amdgpu_mes_init(struct amdgpu_device *adev)
{
	int i, r, num_pipes, num_queues = 0;
	int num_xcc = adev->gfx.xcc_mask ? NUM_XCC(adev->gfx.xcc_mask) : 1;
	u32 gfx_hqd_mask = amdgpu_mes_get_hqd_mask(adev->gfx.me.num_pipe_per_me,
				adev->gfx.me.num_queue_per_pipe,
				adev->gfx.disable_kq ? 0 : adev->gfx.num_gfx_rings);
	u32 compute_hqd_mask = amdgpu_mes_get_hqd_mask(adev->gfx.mec.num_pipe_per_mec,
				adev->gfx.mec.num_queue_per_pipe,
				adev->gfx.disable_kq ? 0 : adev->gfx.num_compute_rings);

	adev->mes.adev = adev;

	ida_init(&adev->mes.doorbell_ida);
	spin_lock_init(&adev->mes.queue_id_lock);
	mutex_init(&adev->mes.mutex_hidden);
	mutex_init(&adev->mes.dbgext_lock);

	for (i = 0; i < AMDGPU_MAX_MES_PIPES * num_xcc; i++)
		spin_lock_init(&adev->mes.ring_lock[i]);

	adev->mes.total_max_queue = AMDGPU_FENCE_MES_QUEUE_ID_MASK;
	atomic_set(&adev->mes.userq_hw_queue_count, 0);
	INIT_DELAYED_WORK(&adev->mes.userq_notify_unmap_work,
			  amdgpu_mes_userq_notify_unmap_work_handler);
	num_pipes = adev->gfx.me.num_pipe_per_me * adev->gfx.me.num_me;
	if (num_pipes > AMDGPU_MES_MAX_GFX_PIPES)
		dev_warn(adev->dev, "more gfx pipes than supported by MES! (%d vs %d)\n",
			 num_pipes, AMDGPU_MES_MAX_GFX_PIPES);

	for (i = 0; i < AMDGPU_MES_MAX_GFX_PIPES; i++) {
		if (i >= num_pipes)
			break;

		adev->mes.gfx_hqd_mask[i] = gfx_hqd_mask;
	}

	num_pipes = adev->gfx.mec.num_pipe_per_mec * adev->gfx.mec.num_mec;
	if (num_pipes > AMDGPU_MES_MAX_COMPUTE_PIPES)
		dev_warn(adev->dev, "more compute pipes than supported by MES! (%d vs %d)\n",
			 num_pipes, AMDGPU_MES_MAX_COMPUTE_PIPES);

	for (i = 0; i < AMDGPU_MES_MAX_COMPUTE_PIPES; i++) {
		/*
		 * Currently, only MEC1 is used for both kernel and user compute queue.
		 * To enable other MEC, we need to redistribute queues per pipe and
		 * adjust queue resource shared with kfd that needs a separate patch.
		 * Skip other MEC for now to avoid potential issues.
		 */
		if (i >= adev->gfx.mec.num_pipe_per_mec)
			break;

		adev->mes.compute_hqd_mask[i] = compute_hqd_mask;
	}

	num_pipes = adev->sdma.num_inst_per_xcc ?
		adev->sdma.num_inst_per_xcc : adev->sdma.num_instances;
	if (num_pipes > AMDGPU_MES_MAX_SDMA_PIPES)
		dev_warn(adev->dev, "more SDMA pipes than supported by MES! (%d vs %d)\n",
			 num_pipes, AMDGPU_MES_MAX_SDMA_PIPES);

	for (i = 0; i < AMDGPU_MES_MAX_SDMA_PIPES; i++) {
		if (i >= num_pipes)
			break;
		adev->mes.sdma_hqd_mask[i] = 0xfc;
	}

	dev_info(adev->dev,
			 "MES: vmid_uq_mask_mmhub 0x%08x, vmid_uq_mask_gfxhub 0x%08x\n",
			 adev->vm_manager.vmid_uq_mask_mmhub,
			 adev->vm_manager.vmid_uq_mask_gfxhub);

	dev_info(adev->dev,
			 "MES: gfx_hqd_mask 0x%08x, compute_hqd_mask 0x%08x, sdma_hqd_mask 0x%08x\n",
			 adev->mes.gfx_hqd_mask[0],
			 adev->mes.compute_hqd_mask[0],
			 adev->mes.sdma_hqd_mask[0]);

	for (i = 0; i < AMDGPU_MAX_MES_PIPES * num_xcc; i++) {
		r = amdgpu_wb_get(adev, &adev->mes.sch_ctx_offs[i]);
		if (r) {
			dev_err(adev->dev,
				"(%d) ring trail_fence_offs wb alloc failed\n",
				r);
			goto error;
		}
		adev->mes.sch_ctx_gpu_addr[i] =
			adev->wb.gpu_addr + (adev->mes.sch_ctx_offs[i] * 4);
		adev->mes.sch_ctx_ptr[i] =
			(uint64_t *)&adev->wb.wb[adev->mes.sch_ctx_offs[i]];

		r = amdgpu_wb_get(adev,
				 &adev->mes.query_status_fence_offs[i]);
		if (r) {
			dev_err(adev->dev,
			      "(%d) query_status_fence_offs wb alloc failed\n",
			      r);
			goto error;
		}
		adev->mes.query_status_fence_gpu_addr[i] = adev->wb.gpu_addr +
			(adev->mes.query_status_fence_offs[i] * 4);
		adev->mes.query_status_fence_ptr[i] =
			(uint64_t *)&adev->wb.wb[adev->mes.query_status_fence_offs[i]];
	}

	r = amdgpu_mes_doorbell_init(adev);
	if (r)
		goto error;

	r = amdgpu_mes_event_log_init(adev);
	if (r)
		goto error_doorbell;

	if (amdgpu_ip_version(adev, GC_HWIP, 0) >= IP_VERSION(11, 0, 0)) {
		/* When queue/pipe reset is done in MES instead of in the
		 * driver, MES passes hung queues information to the driver in
		 * hung_queue_hqd_info. Calculate required space to store this
		 * information.
		 */
		for (i = 0; i < AMDGPU_MES_MAX_GFX_PIPES; i++)
			num_queues += hweight32(adev->mes.gfx_hqd_mask[i]);

		for (i = 0; i < AMDGPU_MES_MAX_COMPUTE_PIPES; i++)
			num_queues += hweight32(adev->mes.compute_hqd_mask[i]);

		for (i = 0; i < AMDGPU_MES_MAX_SDMA_PIPES; i++)
			num_queues += hweight32(adev->mes.sdma_hqd_mask[i]) * num_xcc;

		adev->mes.hung_queue_hqd_info_offset = num_queues;
		adev->mes.hung_queue_db_array_size = num_queues * 2;
	}

	if (adev->mes.hung_queue_db_array_size) {
		for (i = 0; i < AMDGPU_MAX_MES_PIPES * num_xcc; i++) {
			r = amdgpu_bo_create_kernel(adev,
						    adev->mes.hung_queue_db_array_size * sizeof(u32),
						    PAGE_SIZE,
						    AMDGPU_GEM_DOMAIN_GTT,
						    &adev->mes.hung_queue_db_array_gpu_obj[i],
						    &adev->mes.hung_queue_db_array_gpu_addr[i],
						    &adev->mes.hung_queue_db_array_cpu_addr[i]);
			if (r) {
				dev_warn(adev->dev, "failed to create MES hung db array buffer (%d)", r);
				goto error_doorbell;
			}
		}

		adev->gfx.mec.mes_hung_db_array =
			kzalloc_objs(*adev->gfx.mec.mes_hung_db_array,
				     amdgpu_mes_get_hung_queue_db_array_size(adev));

		if (!adev->gfx.mec.mes_hung_db_array) {
			r = -ENOMEM;
			goto error_doorbell;
		}
	}

	return 0;

error_doorbell:
	amdgpu_mes_doorbell_free(adev);
error:
	for (i = 0; i < AMDGPU_MAX_MES_PIPES * num_xcc; i++) {
		if (adev->mes.sch_ctx_ptr[i])
			amdgpu_wb_free(adev, adev->mes.sch_ctx_offs[i]);
		if (adev->mes.query_status_fence_ptr[i])
			amdgpu_wb_free(adev,
				      adev->mes.query_status_fence_offs[i]);
		if (adev->mes.hung_queue_db_array_gpu_obj[i])
			amdgpu_bo_free_kernel(&adev->mes.hung_queue_db_array_gpu_obj[i],
					      &adev->mes.hung_queue_db_array_gpu_addr[i],
					      &adev->mes.hung_queue_db_array_cpu_addr[i]);
	}

	ida_destroy(&adev->mes.doorbell_ida);
	mutex_destroy(&adev->mes.mutex_hidden);
	return r;
}

void amdgpu_mes_fini(struct amdgpu_device *adev)
{
	int i;
	int num_xcc = adev->gfx.xcc_mask ? NUM_XCC(adev->gfx.xcc_mask) : 1;

	cancel_delayed_work_sync(&adev->mes.userq_notify_unmap_work);

	kfree(adev->gfx.mec.mes_hung_db_array);

	amdgpu_bo_free_kernel(&adev->mes.event_log_gpu_obj,
			      &adev->mes.event_log_gpu_addr,
			      &adev->mes.event_log_cpu_addr);

	for (i = 0; i < AMDGPU_MAX_MES_PIPES * num_xcc; i++) {
		if (adev->mes.hung_queue_db_array_gpu_obj[i])
			 amdgpu_bo_free_kernel(&adev->mes.hung_queue_db_array_gpu_obj[i],
					 &adev->mes.hung_queue_db_array_gpu_addr[i],
					 &adev->mes.hung_queue_db_array_cpu_addr[i]);
		if (adev->mes.sch_ctx_ptr[i])
			amdgpu_wb_free(adev, adev->mes.sch_ctx_offs[i]);
		if (adev->mes.query_status_fence_ptr[i])
			amdgpu_wb_free(adev,
				      adev->mes.query_status_fence_offs[i]);
	}

	amdgpu_mes_doorbell_free(adev);

	if (adev->mes.use_rs64mem)
		amdgpu_mes_rs64mem_fini(&adev->mes);

	ida_destroy(&adev->mes.doorbell_ida);
	mutex_destroy(&adev->mes.mutex_hidden);
	mutex_destroy(&adev->mes.dbgext_lock);
}

int amdgpu_mes_suspend(struct amdgpu_device *adev, u32 xcc_id)
{
	struct mes_suspend_gang_input input;
	int r;

	if (!amdgpu_mes_suspend_resume_all_supported(adev))
		return 0;

	memset(&input, 0x0, sizeof(struct mes_suspend_gang_input));
	input.suspend_all_gangs = 1;
	input.xcc_id = xcc_id;
	if ((amdgpu_ip_version(adev, GC_HWIP, 0) == IP_VERSION(12, 1, 0)) &&
		((adev->mes.sched_version & AMDGPU_MES_VERSION_MASK) >= 0x71))
		input.suspend_all_sdma_gangs = 1;

	/*
	 * Avoid taking any other locks under MES lock to avoid circular
	 * lock dependencies.
	 */
	amdgpu_mes_lock(&adev->mes);
	r = adev->mes.funcs->suspend_gang(&adev->mes, &input);
	amdgpu_mes_unlock(&adev->mes);
	if (r)
		dev_err(adev->dev, "failed to suspend all gangs");

	return r;
}

int amdgpu_mes_resume(struct amdgpu_device *adev, u32 xcc_id)
{
	struct mes_resume_gang_input input;
	int r;

	if (!amdgpu_mes_suspend_resume_all_supported(adev))
		return 0;

	memset(&input, 0x0, sizeof(struct mes_resume_gang_input));
	input.resume_all_gangs = 1;
	input.xcc_id = xcc_id;

	/*
	 * Avoid taking any other locks under MES lock to avoid circular
	 * lock dependencies.
	 */
	amdgpu_mes_lock(&adev->mes);
	r = adev->mes.funcs->resume_gang(&adev->mes, &input);
	amdgpu_mes_unlock(&adev->mes);
	if (r)
		dev_err(adev->dev, "failed to resume all gangs");

	return r;
}

int amdgpu_mes_map_legacy_queue(struct amdgpu_device *adev,
				struct amdgpu_ring *ring, uint32_t xcc_id)
{
	struct mes_map_legacy_queue_input queue_input;
	int r;

	memset(&queue_input, 0, sizeof(queue_input));

	queue_input.xcc_id = xcc_id;
	queue_input.queue_type = ring->funcs->type;
	queue_input.doorbell_offset = ring->doorbell_index;
	queue_input.pipe_id = ring->pipe;
	queue_input.queue_id = ring->queue;
	queue_input.mqd_addr = amdgpu_bo_gpu_offset(ring->mqd_obj);
	queue_input.wptr_addr = ring->wptr_gpu_addr;

	amdgpu_mes_lock(&adev->mes);
	r = adev->mes.funcs->map_legacy_queue(&adev->mes, &queue_input);
	amdgpu_mes_unlock(&adev->mes);
	if (r)
		dev_err(adev->dev, "failed to map legacy queue\n");

	return r;
}

int amdgpu_mes_unmap_legacy_queue(struct amdgpu_device *adev,
				  struct amdgpu_ring *ring,
				  enum amdgpu_unmap_queues_action action,
				  u64 gpu_addr, u64 seq, uint32_t xcc_id)
{
	struct mes_unmap_legacy_queue_input queue_input;
	int r;

	queue_input.xcc_id = xcc_id;
	queue_input.action = action;
	queue_input.queue_type = ring->funcs->type;
	queue_input.doorbell_offset = ring->doorbell_index;
	queue_input.pipe_id = ring->pipe;
	queue_input.queue_id = ring->queue;
	queue_input.trail_fence_addr = gpu_addr;
	queue_input.trail_fence_data = seq;

	amdgpu_mes_lock(&adev->mes);
	r = adev->mes.funcs->unmap_legacy_queue(&adev->mes, &queue_input);
	amdgpu_mes_unlock(&adev->mes);
	if (r)
		dev_err(adev->dev, "failed to unmap legacy queue\n");

	return r;
}

int amdgpu_mes_reset_legacy_queue(struct amdgpu_device *adev,
				  struct amdgpu_ring *ring,
				  unsigned int vmid,
				  bool use_mmio,
				  uint32_t xcc_id)
{
	struct mes_reset_queue_input queue_input;
	int r;

	memset(&queue_input, 0, sizeof(queue_input));

	queue_input.xcc_id = xcc_id;
	queue_input.queue_type = ring->funcs->type;
	queue_input.doorbell_offset = ring->doorbell_index;
	queue_input.me_id = ring->me;
	queue_input.pipe_id = ring->pipe;
	queue_input.queue_id = ring->queue;
	queue_input.mqd_addr = ring->mqd_obj ? amdgpu_bo_gpu_offset(ring->mqd_obj) : 0;
	queue_input.wptr_addr = ring->wptr_gpu_addr;
	queue_input.vmid = vmid;
	queue_input.use_mmio = use_mmio;
	queue_input.is_kq = true;
	if (ring->funcs->type == AMDGPU_RING_TYPE_GFX)
		queue_input.legacy_gfx = true;

	amdgpu_mes_lock(&adev->mes);
	r = adev->mes.funcs->reset_hw_queue(&adev->mes, &queue_input);
	amdgpu_mes_unlock(&adev->mes);
	if (r)
		dev_err(adev->dev, "failed to reset legacy queue\n");

	return r;
}

int amdgpu_mes_reset_queue_mmio(struct amdgpu_device *adev,
				int queue_type,
				unsigned int vmid,
				unsigned int me,
				unsigned int pipe,
				unsigned int queue,
				uint32_t xcc_id)
{
	struct mes_reset_queue_input queue_input;
	int r;

	memset(&queue_input, 0, sizeof(queue_input));

	queue_input.xcc_id = xcc_id;
	queue_input.me_id = me;
	queue_input.pipe_id = pipe;
	queue_input.queue_id = queue;
	queue_input.vmid = vmid;
	queue_input.queue_type = queue_type;
	queue_input.use_mmio = true;

	amdgpu_mes_lock(&adev->mes);
	r = adev->mes.funcs->reset_hw_queue(&adev->mes, &queue_input);
	amdgpu_mes_unlock(&adev->mes);
	if (r)
		dev_err(adev->dev, "failed to reset legacy queue\n");

	return r;
}

int amdgpu_mes_reset_user_queue(struct amdgpu_device *adev,
				int queue_type,
				unsigned int doorbell_index,
				unsigned int xcc_id)
{
	struct mes_reset_queue_input queue_input;
	int r;

	memset(&queue_input, 0, sizeof(queue_input));

	queue_input.xcc_id = xcc_id;
	queue_input.queue_type = queue_type;
	queue_input.doorbell_offset = doorbell_index;

	amdgpu_mes_lock(&adev->mes);
	r = adev->mes.funcs->reset_hw_queue(&adev->mes, &queue_input);
	amdgpu_mes_unlock(&adev->mes);
	if (r)
		dev_err(adev->dev, "failed to reset user queue\n");

	return r;
}

int amdgpu_mes_get_hung_queue_db_array_size(struct amdgpu_device *adev)
{
	return adev->mes.hung_queue_db_array_size;
}

int amdgpu_mes_detect_and_reset_hung_queues(struct amdgpu_device *adev,
					    int queue_type,
					    bool detect_only,
					    unsigned int *hung_db_num,
					    u32 *hung_db_array,
					    uint32_t xcc_id)
{
	struct mes_detect_and_reset_queue_input input;
	u32 *db_array = adev->mes.hung_queue_db_array_cpu_addr[xcc_id];
	int hqd_info_offset = adev->mes.hung_queue_hqd_info_offset, r, i;

	if (!hung_db_num || !hung_db_array)
		return -EINVAL;

	if ((queue_type != AMDGPU_RING_TYPE_GFX) &&
	    (queue_type != AMDGPU_RING_TYPE_COMPUTE) &&
	    (queue_type != AMDGPU_RING_TYPE_SDMA))
		return -EINVAL;

	/* Clear the doorbell array before detection */
	memset(adev->mes.hung_queue_db_array_cpu_addr[xcc_id], AMDGPU_MES_INVALID_DB_OFFSET,
		adev->mes.hung_queue_db_array_size * sizeof(u32));
	input.queue_type = queue_type;
	input.detect_only = detect_only;
	input.xcc_id = xcc_id;

	r = adev->mes.funcs->detect_and_reset_hung_queues(&adev->mes,
							  &input);

	if (r && detect_only) {
		dev_err(adev->dev, "Failed to detect hung queues\n");
		return r;
	}

	*hung_db_num = 0;
	/* MES passes hung queues' doorbell to driver */
	for (i = 0; i < adev->mes.hung_queue_hqd_info_offset; i++) {
		/* Finding hung queues where db_array[i] is a valid doorbell */
		if (db_array[i] != AMDGPU_MES_INVALID_DB_OFFSET) {
			hung_db_array[i] = db_array[i];
			*hung_db_num += 1;
		}
	}

	if (r && !(*hung_db_num)) {
		dev_err(adev->dev, "Failed to detect and reset hung queues\n");
		return r;
	}

	for (i = hqd_info_offset; i < hqd_info_offset + *hung_db_num; i++)
		hung_db_array[i] = db_array[i];

	return r;
}

uint32_t amdgpu_mes_rreg(struct amdgpu_device *adev, uint32_t reg,
			 uint32_t xcc_id)
{
	struct mes_misc_op_input op_input;
	int r, val = 0;
	uint32_t addr_offset = 0;
	uint64_t read_val_gpu_addr;
	uint32_t *read_val_ptr;

	if (amdgpu_wb_get(adev, &addr_offset)) {
		dev_err(adev->dev, "critical bug! too many mes readers\n");
		goto error;
	}
	read_val_gpu_addr = adev->wb.gpu_addr + (addr_offset * 4);
	read_val_ptr = (uint32_t *)&adev->wb.wb[addr_offset];
	op_input.xcc_id = xcc_id;
	op_input.op = MES_MISC_OP_READ_REG;
	op_input.read_reg.reg_offset = reg;
	op_input.read_reg.buffer_addr = read_val_gpu_addr;

	if (!adev->mes.funcs->misc_op) {
		dev_err(adev->dev, "mes rreg is not supported!\n");
		goto error;
	}

	amdgpu_mes_lock(&adev->mes);
	r = adev->mes.funcs->misc_op(&adev->mes, &op_input);
	amdgpu_mes_unlock(&adev->mes);
	if (r)
		dev_err(adev->dev, "failed to read reg (0x%x)\n", reg);
	else
		val = *(read_val_ptr);

error:
	if (addr_offset)
		amdgpu_wb_free(adev, addr_offset);
	return val;
}

int amdgpu_mes_wreg(struct amdgpu_device *adev, uint32_t reg,
		    uint32_t val, uint32_t xcc_id)
{
	struct mes_misc_op_input op_input;
	int r;

	op_input.xcc_id = xcc_id;
	op_input.op = MES_MISC_OP_WRITE_REG;
	op_input.write_reg.reg_offset = reg;
	op_input.write_reg.reg_value = val;

	if (!adev->mes.funcs->misc_op) {
		dev_err(adev->dev, "mes wreg is not supported!\n");
		r = -EINVAL;
		goto error;
	}

	amdgpu_mes_lock(&adev->mes);
	r = adev->mes.funcs->misc_op(&adev->mes, &op_input);
	amdgpu_mes_unlock(&adev->mes);
	if (r)
		dev_err(adev->dev, "failed to write reg (0x%x)\n", reg);

error:
	return r;
}

int amdgpu_mes_reg_write_reg_wait(struct amdgpu_device *adev,
				  uint32_t reg0, uint32_t reg1,
				  uint32_t ref, uint32_t mask,
				  uint32_t xcc_id)
{
	struct mes_misc_op_input op_input;
	int r;

	op_input.xcc_id = xcc_id;
	op_input.op = MES_MISC_OP_WRM_REG_WR_WAIT;
	op_input.wrm_reg.reg0 = reg0;
	op_input.wrm_reg.reg1 = reg1;
	op_input.wrm_reg.ref = ref;
	op_input.wrm_reg.mask = mask;

	if (!adev->mes.funcs->misc_op) {
		dev_err(adev->dev, "mes reg_write_reg_wait is not supported!\n");
		r = -EINVAL;
		goto error;
	}

	amdgpu_mes_lock(&adev->mes);
	r = adev->mes.funcs->misc_op(&adev->mes, &op_input);
	amdgpu_mes_unlock(&adev->mes);
	if (r)
		dev_err(adev->dev, "failed to reg_write_reg_wait\n");

error:
	return r;
}

int amdgpu_mes_hdp_flush(struct amdgpu_device *adev)
{
	uint32_t hdp_flush_req_offset, hdp_flush_done_offset;
	struct amdgpu_ring *mes_ring;
	uint32_t ref_and_mask = 0, reg_mem_engine = 0;

	if (!adev->gfx.funcs->get_hdp_flush_mask) {
		dev_err(adev->dev, "mes hdp flush is not supported.\n");
		return -EINVAL;
	}

	mes_ring = &adev->mes.ring[0];
	hdp_flush_req_offset = adev->nbio.funcs->get_hdp_flush_req_offset(adev);
	hdp_flush_done_offset = adev->nbio.funcs->get_hdp_flush_done_offset(adev);

	adev->gfx.funcs->get_hdp_flush_mask(mes_ring, &ref_and_mask, &reg_mem_engine);

	return amdgpu_mes_reg_write_reg_wait(adev, hdp_flush_req_offset, hdp_flush_done_offset,
					     ref_and_mask, ref_and_mask, 0);
}

int amdgpu_mes_set_shader_debugger(struct amdgpu_device *adev,
				uint64_t process_context_addr,
				uint32_t spi_gdbg_per_vmid_cntl,
				const uint32_t *tcp_watch_cntl,
				uint32_t flags,
				bool trap_en,
				uint32_t xcc_id)
{
	struct mes_misc_op_input op_input = {0};
	int r;

	if (!adev->mes.funcs->misc_op) {
		dev_err(adev->dev,
			"mes set shader debugger is not supported!\n");
		return -EINVAL;
	}

	op_input.xcc_id = xcc_id;
	op_input.op = MES_MISC_OP_SET_SHADER_DEBUGGER;
	op_input.set_shader_debugger.process_context_addr = process_context_addr;
	op_input.set_shader_debugger.flags.u32all = flags;

	/* use amdgpu mes_flush_shader_debugger instead */
	if (op_input.set_shader_debugger.flags.process_ctx_flush)
		return -EINVAL;

	op_input.set_shader_debugger.spi_gdbg_per_vmid_cntl = spi_gdbg_per_vmid_cntl;
	memcpy(op_input.set_shader_debugger.tcp_watch_cntl, tcp_watch_cntl,
			sizeof(op_input.set_shader_debugger.tcp_watch_cntl));

	if (((adev->mes.sched_version & AMDGPU_MES_API_VERSION_MASK) >>
			AMDGPU_MES_API_VERSION_SHIFT) >= 14)
		op_input.set_shader_debugger.trap_en = trap_en;

	amdgpu_mes_lock(&adev->mes);

	r = adev->mes.funcs->misc_op(&adev->mes, &op_input);
	if (r)
		dev_err(adev->dev, "failed to set_shader_debugger\n");

	amdgpu_mes_unlock(&adev->mes);

	return r;
}

int amdgpu_mes_flush_shader_debugger(struct amdgpu_device *adev,
				     uint64_t process_context_addr,
				     uint32_t xcc_id)
{
	struct mes_misc_op_input op_input = {0};
	int r;

	if (!adev->mes.funcs->misc_op) {
		dev_err(adev->dev,
			"mes flush shader debugger is not supported!\n");
		return -EINVAL;
	}

	op_input.xcc_id = xcc_id;
	op_input.op = MES_MISC_OP_SET_SHADER_DEBUGGER;
	op_input.set_shader_debugger.process_context_addr = process_context_addr;
	op_input.set_shader_debugger.flags.process_ctx_flush = true;

	amdgpu_mes_lock(&adev->mes);

	r = adev->mes.funcs->misc_op(&adev->mes, &op_input);
	if (r)
		dev_err(adev->dev, "failed to set_shader_debugger\n");

	amdgpu_mes_unlock(&adev->mes);

	return r;
}

uint32_t amdgpu_mes_get_aggregated_doorbell_index(struct amdgpu_device *adev,
						   enum amdgpu_mes_priority_level prio)
{
	return adev->mes.aggregated_doorbells[prio];
}

int amdgpu_mes_init_microcode(struct amdgpu_device *adev, int pipe)
{
	const struct mes_firmware_header_v1_0 *mes_hdr;
	struct amdgpu_firmware_info *info;
	char ucode_prefix[30];
	char fw_name[50];
	bool need_retry = false;
	u32 *ucode_ptr;
	int r;

	amdgpu_ucode_ip_version_decode(adev, GC_HWIP, ucode_prefix,
				       sizeof(ucode_prefix));
	if (adev->enable_uni_mes) {
		snprintf(fw_name, sizeof(fw_name),
			 "amdgpu/%s_uni_mes.bin", ucode_prefix);
	} else if (amdgpu_ip_version(adev, GC_HWIP, 0) >= IP_VERSION(11, 0, 0) &&
	    amdgpu_ip_version(adev, GC_HWIP, 0) < IP_VERSION(12, 0, 0)) {
		snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_mes%s.bin",
			 ucode_prefix,
			 pipe == AMDGPU_MES_SCHED_PIPE ? "_2" : "1");
		need_retry = true;
	} else {
		snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_mes%s.bin",
			 ucode_prefix,
			 pipe == AMDGPU_MES_SCHED_PIPE ? "" : "1");
	}

	r = amdgpu_ucode_request(adev, &adev->mes.fw[pipe], AMDGPU_UCODE_REQUIRED,
				 "%s", fw_name);
	if (r && need_retry && pipe == AMDGPU_MES_SCHED_PIPE) {
		dev_info(adev->dev, "try to fall back to %s_mes.bin\n", ucode_prefix);
		r = amdgpu_ucode_request(adev, &adev->mes.fw[pipe],
					 AMDGPU_UCODE_REQUIRED,
					 "amdgpu/%s_mes.bin", ucode_prefix);
	}

	if (r)
		goto out;

	mes_hdr = (const struct mes_firmware_header_v1_0 *)
		adev->mes.fw[pipe]->data;
	adev->mes.uc_start_addr[pipe] =
		le32_to_cpu(mes_hdr->mes_uc_start_addr_lo) |
		((uint64_t)(le32_to_cpu(mes_hdr->mes_uc_start_addr_hi)) << 32);
	adev->mes.data_start_addr[pipe] =
		le32_to_cpu(mes_hdr->mes_data_start_addr_lo) |
		((uint64_t)(le32_to_cpu(mes_hdr->mes_data_start_addr_hi)) << 32);
	ucode_ptr = (u32 *)(adev->mes.fw[pipe]->data +
			  sizeof(union amdgpu_firmware_header));
	adev->mes.fw_version[pipe] =
		le32_to_cpu(ucode_ptr[24]) & AMDGPU_MES_VERSION_MASK;

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		int ucode, ucode_data;

		if (pipe == AMDGPU_MES_SCHED_PIPE) {
			ucode = AMDGPU_UCODE_ID_CP_MES;
			ucode_data = AMDGPU_UCODE_ID_CP_MES_DATA;
		} else {
			ucode = AMDGPU_UCODE_ID_CP_MES1;
			ucode_data = AMDGPU_UCODE_ID_CP_MES1_DATA;
		}

		info = &adev->firmware.ucode[ucode];
		info->ucode_id = ucode;
		info->fw = adev->mes.fw[pipe];
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(mes_hdr->mes_ucode_size_bytes),
			      PAGE_SIZE);

		info = &adev->firmware.ucode[ucode_data];
		info->ucode_id = ucode_data;
		info->fw = adev->mes.fw[pipe];
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(mes_hdr->mes_ucode_data_size_bytes),
			      PAGE_SIZE);
	}

	return 0;
out:
	amdgpu_ucode_release(&adev->mes.fw[pipe]);
	return r;
}

void amdgpu_mes_validate_fw_version(struct amdgpu_device *adev)
{
	u32 fw_from_ucode = adev->mes.fw_version[AMDGPU_MES_SCHED_PIPE];
	u32 fw_from_reg = adev->mes.sched_version & AMDGPU_MES_VERSION_MASK;

	if (fw_from_ucode != fw_from_reg)
		dev_info(adev->dev,
			 "MES firmware reports incorrect version in ucode binary (0x%x vs 0x%x)\n",
			 fw_from_ucode, fw_from_reg);
}


bool amdgpu_mes_suspend_resume_all_supported(struct amdgpu_device *adev)
{
	uint32_t mes_rev = adev->mes.sched_version & AMDGPU_MES_VERSION_MASK;

	return ((amdgpu_ip_version(adev, GC_HWIP, 0) >= IP_VERSION(11, 0, 0) &&
		 amdgpu_ip_version(adev, GC_HWIP, 0) < IP_VERSION(12, 0, 0) &&
		 mes_rev >= 0x63) ||
		amdgpu_ip_version(adev, GC_HWIP, 0) >= IP_VERSION(12, 0, 0));
}

bool amdgpu_mes_queue_reset_by_mes_supported(struct amdgpu_device *adev)
{
	u32 ip_maj = IP_VERSION_MAJ(amdgpu_ip_version(adev, GC_HWIP, 0));
	u32 ip_min = IP_VERSION_MIN(amdgpu_ip_version(adev, GC_HWIP, 0));
	u32 mes_sched = adev->mes.sched_version & AMDGPU_MES_VERSION_MASK;

	return (ip_maj == 11 && mes_sched >= 0x8c) ||
		((ip_maj == 12 && ip_min == 0) && mes_sched >= 0x8d) ||
		((ip_maj == 12 && ip_min == 1) && mes_sched >= 0x7b);
}

/* Fix me -- node_id is used to identify the correct MES instances in the future */
static int amdgpu_mes_set_enforce_isolation(struct amdgpu_device *adev,
					    uint32_t node_id, bool enable)
{
	struct mes_misc_op_input op_input = {0};
	int r;

	op_input.op = MES_MISC_OP_CHANGE_CONFIG;
	op_input.change_config.option.limit_single_process = enable ? 1 : 0;

	if (!adev->mes.funcs->misc_op) {
		dev_err(adev->dev, "mes change config is not supported!\n");
		r = -EINVAL;
		goto error;
	}

	amdgpu_mes_lock(&adev->mes);
	r = adev->mes.funcs->misc_op(&adev->mes, &op_input);
	amdgpu_mes_unlock(&adev->mes);
	if (r)
		dev_err(adev->dev, "failed to change_config.\n");

error:
	return r;
}

int amdgpu_mes_update_enforce_isolation(struct amdgpu_device *adev)
{
	int i, r = 0;

	if (adev->enable_mes && adev->gfx.enable_cleaner_shader) {
		mutex_lock(&adev->enforce_isolation_mutex);
		for (i = 0; i < (adev->xcp_mgr ? adev->xcp_mgr->num_xcps : 1); i++) {
			if (adev->enforce_isolation[i] == AMDGPU_ENFORCE_ISOLATION_ENABLE)
				r |= amdgpu_mes_set_enforce_isolation(adev, i, true);
			else
				r |= amdgpu_mes_set_enforce_isolation(adev, i, false);
		}
		mutex_unlock(&adev->enforce_isolation_mutex);
	}
	return r;
}

/**
 * amdgpu_mes_rs64mem_init - initialize RS64 local memory context arrays
 *
 * @mes: MES instance
 *
 * Returns 0 on success, negative errno on failure.
 */
int amdgpu_mes_rs64mem_init(struct amdgpu_mes *mes)
{
	struct amdgpu_device *adev = container_of(mes, struct amdgpu_device, mes);
	int r;

	if (!mes->use_rs64mem)
		return 0;

	r = amdgpu_bo_create_kernel(adev, PAGE_SIZE, PAGE_SIZE,
				    AMDGPU_GEM_DOMAIN_GTT,
				    &mes->ctx_array_size_bo,
				    &mes->ctx_array_size_gpu_addr,
				    (void **)&mes->ctx_array_size_cpu_ptr);
	if (r) {
		dev_err(adev->dev,
			"Failed to allocate ctx array size BO, r=%d\n", r);
		return r;
	}

	memset(mes->ctx_array_size_cpu_ptr, 0, PAGE_SIZE);

	return 0;
}

 /**
  * amdgpu_mes_rs64mem_fini - tear down RS64 local memory management
  *
  * @mes: MES instance
  */
void amdgpu_mes_rs64mem_fini(struct amdgpu_mes *mes)
{
	if (mes->ctx_array_size_bo) {
		amdgpu_bo_free_kernel(&mes->ctx_array_size_bo,
				      &mes->ctx_array_size_gpu_addr,
				      (void **)&mes->ctx_array_size_cpu_ptr);
	}
	bitmap_free(mes->proc_ctx_bitmap);
	bitmap_free(mes->gang_ctx_bitmap);
	mes->use_rs64mem = false;
}

/**
 * amdgpu_mes_rs64mem_setup_bitmaps - allocate bitmaps after querying MES
 *
 * Called after QUERY_SCHEDULER_STATUS returns and MES has written
 * the array sizes to the GPU buffer. Reads the sizes and allocates
 * the tracking bitmaps.
 *
 * @mes: MES instance
 *
 * Returns 0 on success, negative errno on failure.
 */
int amdgpu_mes_rs64mem_setup_bitmaps(struct amdgpu_mes *mes)
{
	struct amdgpu_device *adev = container_of(mes, struct amdgpu_device, mes);

	if (!mes->use_rs64mem || !mes->ctx_array_size_cpu_ptr)
		return 0;

	/*
	 * MES FW wrote the sizes to the GPU buffer:
	 *   ctx_array_size_cpu_ptr[0] = proc_ctx_array_size (N)
	 *   ctx_array_size_cpu_ptr[1] = gang_ctx_array_size (M)
	 */
	mes->proc_ctx_array_size = mes->ctx_array_size_cpu_ptr[0];
	mes->gang_ctx_array_size = mes->ctx_array_size_cpu_ptr[1];

	/* Sanity check - MES FW typically returns N=50, M=300 */
	if (mes->proc_ctx_array_size == 0 || mes->gang_ctx_array_size == 0) {
		dev_warn(adev->dev,
			 "MES returned zero ctx array sizes (proc=%u, gang=%u), "
			 "disabling RS64 local memory optimization\n",
			 mes->proc_ctx_array_size, mes->gang_ctx_array_size);
		mes->use_rs64mem = false;
		return 0;
	}

	/* Cap to safety limits */
	if (mes->proc_ctx_array_size > AMDGPU_MES_PROC_CTX_ARRAY_MAX)
		mes->proc_ctx_array_size = AMDGPU_MES_PROC_CTX_ARRAY_MAX;
	if (mes->gang_ctx_array_size > AMDGPU_MES_GANG_CTX_ARRAY_MAX)
		mes->gang_ctx_array_size = AMDGPU_MES_GANG_CTX_ARRAY_MAX;

	dev_info(adev->dev,
		 "MES RS64 local memory: proc_ctx_array_size:%u, "
		 "gang_ctx_array_size:%u\n",
		 mes->proc_ctx_array_size, mes->gang_ctx_array_size);

	/* Allocate bitmaps */
	mes->proc_ctx_bitmap = bitmap_zalloc(mes->proc_ctx_array_size,
					     GFP_KERNEL);
	if (!mes->proc_ctx_bitmap) {
		mes->use_rs64mem = false;
		return -ENOMEM;
	}

	mes->gang_ctx_bitmap = bitmap_zalloc(mes->gang_ctx_array_size,
					     GFP_KERNEL);
	if (!mes->gang_ctx_bitmap) {
		bitmap_free(mes->proc_ctx_bitmap);
		mes->proc_ctx_bitmap = NULL;
		mes->use_rs64mem = false;
		return -ENOMEM;
	}

	return 0;
}

/**
 * amdgpu_mes_alloc_proc_ctx_index - allocate a process context slot
 *
 * @mes: MES instance
 * @index: the allocated process context index
 *
 * Returns 0 on success, -ENOSPC if all slots are used, or
 * -EOPNOTSUPP if RS64 local memory is unavailable.
 */
int amdgpu_mes_alloc_proc_ctx_index(struct amdgpu_mes *mes,
				    uint32_t *index)
{
	unsigned long bit;

	if (!mes->use_rs64mem || !mes->proc_ctx_bitmap)
		return -EOPNOTSUPP;

	amdgpu_mes_lock(mes);
	bit = find_first_zero_bit(mes->proc_ctx_bitmap,
				  mes->proc_ctx_array_size);
	if (bit >= mes->proc_ctx_array_size) {
		amdgpu_mes_unlock(mes);
		return -ENOSPC;
	}
	set_bit(bit, mes->proc_ctx_bitmap);
	*index = (uint32_t)bit;
	amdgpu_mes_unlock(mes);

	return 0;
}

 /**
  * amdgpu_mes_free_proc_ctx_index - free a process context slot
  *
  * @mes: MES instance
  * @index: process context index is released
  */
void amdgpu_mes_free_proc_ctx_index(struct amdgpu_mes *mes,
				    uint32_t index)
{
	if (!mes->use_rs64mem || !mes->proc_ctx_bitmap)
		return;
	if (index >= mes->proc_ctx_array_size)
		return;

	amdgpu_mes_lock(mes);
	clear_bit(index, mes->proc_ctx_bitmap);
	amdgpu_mes_unlock(mes);
}

 /**
  * amdgpu_mes_alloc_gang_ctx_index - allocate a gang context slot
  *
  * @mes: MES instance
  * @index: the allocated gang context index
  *
  * Returns 0 on success, -ENOSPC if all slots are used, or
  * -EOPNOTSUPP if RS64 local memory is unavailable.
  */
int amdgpu_mes_alloc_gang_ctx_index(struct amdgpu_mes *mes,
				    uint32_t *index)
{
	unsigned long bit;

	if (!mes->use_rs64mem || !mes->gang_ctx_bitmap)
		return -EOPNOTSUPP;

	amdgpu_mes_lock(mes);
	bit = find_first_zero_bit(mes->gang_ctx_bitmap,
				  mes->gang_ctx_array_size);
	if (bit >= mes->gang_ctx_array_size) {
		amdgpu_mes_unlock(mes);
		return -ENOSPC;
	}
	set_bit(bit, mes->gang_ctx_bitmap);
	*index = bit;
	amdgpu_mes_unlock(mes);

	return 0;
}

 /**
  * amdgpu_mes_free_gang_ctx_index - free a gang context slot
  *
  * @mes: MES instance
  * @index: gang context index is released
  */
void amdgpu_mes_free_gang_ctx_index(struct amdgpu_mes *mes,
				    uint32_t index)
{
	if (!mes->use_rs64mem || !mes->gang_ctx_bitmap)
		return;
	if (index >= mes->gang_ctx_array_size)
		return;

	amdgpu_mes_lock(mes);
	clear_bit(index, mes->gang_ctx_bitmap);
	amdgpu_mes_unlock(mes);
}

int amdgpu_mes_notify_unmap_queue(struct amdgpu_device *adev)
{
	struct mes_misc_op_input op_input = {0};
	int r;

	op_input.op = MES_MISC_OP_NOTIFY_WORK_ON_UNMAPPED_QUEUE;

	if (!adev->mes.funcs->misc_op) {
		dev_err(adev->dev, "mes notify unmap queue is not supported!\n");
		r = -EINVAL;
		goto error;
	}

	amdgpu_mes_lock(&adev->mes);
	r = adev->mes.funcs->misc_op(&adev->mes, &op_input);
	amdgpu_mes_unlock(&adev->mes);
	if (r)
		dev_err(adev->dev, "failed to notify unmap queue.\n");

error:
	return r;
}

/* Interval for notifying MES of work on unmapped queues during oversubscription */
#define AMDGPU_USERQ_UNMAP_NOTIFY_DELAY_US 50

static unsigned int amdgpu_mes_userq_hw_queue_num(struct amdgpu_device *adev)
{
	int num_xcc = adev->gfx.xcc_mask ? NUM_XCC(adev->gfx.xcc_mask) : 1;
	unsigned int n = bitmap_weight(adev->gfx.me.queue_bitmap, AMDGPU_MAX_GFX_QUEUES);
	int i;

	for (i = 0; i < num_xcc; i++)
		n += bitmap_weight(adev->gfx.mec_bitmap[i].queue_bitmap,
				    AMDGPU_MAX_COMPUTE_QUEUES);

	return n;
}

static void amdgpu_mes_userq_notify_unmap_work_handler(struct work_struct *work)
{
	struct amdgpu_mes *mes = container_of(work, struct amdgpu_mes,
					       userq_notify_unmap_work.work);
	struct amdgpu_device *adev = mes->adev;

	amdgpu_mes_notify_unmap_queue(adev);

	/* Re-arm if still oversubscribed */
	if (atomic_read(&mes->userq_hw_queue_count) >
	    amdgpu_mes_userq_hw_queue_num(adev))
		queue_delayed_work(system_wq, &mes->userq_notify_unmap_work,
				   usecs_to_jiffies(AMDGPU_USERQ_UNMAP_NOTIFY_DELAY_US));
}

/*
 * Called after a GFX11 usermode queue is successfully mapped to MES.
 * Starts the periodic unmap-notify timer if this pushed the device into
 * HW queue oversubscription.
 */
void amdgpu_mes_userq_queue_mapped(struct amdgpu_device *adev)
{
	if (amdgpu_sriov_vf(adev))
		return;

	if (!(amdgpu_ip_version(adev, GC_HWIP, 0) >= IP_VERSION(11, 0, 0) &&
	      amdgpu_ip_version(adev, GC_HWIP, 0) < IP_VERSION(12, 0, 0)))
		return;

	if (atomic_inc_return(&adev->mes.userq_hw_queue_count) >
	    amdgpu_mes_userq_hw_queue_num(adev))
		queue_delayed_work(system_wq, &adev->mes.userq_notify_unmap_work,
				   usecs_to_jiffies(AMDGPU_USERQ_UNMAP_NOTIFY_DELAY_US));
}

/*
 * Called after a GFX11 usermode queue is unmapped from MES. Stops the
 * periodic unmap-notify timer once oversubscription clears.
 */
void amdgpu_mes_userq_queue_unmapped(struct amdgpu_device *adev)
{
	if (amdgpu_sriov_vf(adev))
		return;

	if (!(amdgpu_ip_version(adev, GC_HWIP, 0) >= IP_VERSION(11, 0, 0) &&
	      amdgpu_ip_version(adev, GC_HWIP, 0) < IP_VERSION(12, 0, 0)))
		return;

	if (atomic_dec_return(&adev->mes.userq_hw_queue_count) <=
	    amdgpu_mes_userq_hw_queue_num(adev))
		cancel_delayed_work(&adev->mes.userq_notify_unmap_work);
}

/*
 * MES firmware debug extension ("mes_dbgext")
 *
 * The driver hands the MES a log buffer; the MES firmware writes text log items
 * into it and the driver drains and prints them.  The buffer is a single
 * circular byte stream with a 16-byte header at offset 0:
 *
 *   dword0 : rptr        - driver reads/advances (with wrap)
 *   dword1 : wptr        - firmware writes/advances (with wrap)
 *   dword2 : buffer_size - total buffer size in bytes (set by the driver)
 *   dword3 : header_size - size of this header / wrap-back offset (16)
 *
 * Item data starts at header_size and wraps from buffer_size back to
 * header_size.  Each log item begins with a 4-byte header (type, xor-signature,
 * 16-bit length including the header) followed by NUL-free text.
 *
 * The driver owns rptr; the firmware owns wptr.  The collection method is
 * selectable via the mes_dbgext_options bit0: interrupt-driven (default - the
 * FW raises its host interrupt per message, handled via the CP EOP path) or a
 * polling kthread.  Polling is also used automatically as a fallback when the
 * ASIC has no IRQ-enable hook.
 *
 * NOTE: this requires an MES firmware image built with debug-extension support.
 */

#define MES_DBGEXT_MAX_ITEM_SIZE	2048
#define MES_DBGEXT_POLL_INTERVAL_MS	200
/* Default log-buffer size (KB) used when enabled at runtime with no size set. */
#define MES_DBGEXT_DEFAULT_KB		8

/* Log item text record types (must match mes_aux LOG__* in mes_dbgext.cpp). */
#define MES_DBGEXT_MSG			0x80
#define MES_DBGEXT_MSG_ASSERT		0x81
#define MES_DBGEXT_MSG_HALT		0x82

/*
 * Log buffer option bits, must match the MES firmware's MES_DBGEXT_INIT_DATA.
 * bit0 = trigger_interrupt_per_new_msg: when SET, the firmware raises the
 * debug-message host interrupt after each message
 * (mes_dbgext.cpp: "if (trigger_interrupt_per_new_msg) SendIntToHost()").
 * When CLEAR, the firmware only writes the buffer and the driver must poll.
 */
#define MES_DBGEXT_OPT_TRIGGER_INT_PER_MSG	(1ULL << 0)

/*
 * MES firmware routes mes_dbgext through the shared "mes_aux" component, which
 * uses a zone-partitioned buffer layout:
 *
 *   offset 0: struct { u32 zone_count; struct {u32 offset, length}[zone_count]; }
 *
 * Each zone starts (at its byte offset from the buffer base) with a 16-byte
 * LOG_ZONE_HEADER {rptr, wptr, buffer_size, header_size} whose rptr/wptr are
 * relative to the zone start and wrap from buffer_size back to header_size.
 * Every log item begins with an 8-byte header: type, xor-signature, a 24-bit
 * big-endian length (total item size including the header, in bytes[2..4]),
 * then level, seq and a reserved byte.  Zone 0 carries human-readable text
 * (printed to dmesg); other zones carry binary event/interrupt/api records
 * (types 0x93..0x95) that are not text and are skipped.
 *
 * The driver seeds only the total buffer size in the first dword; the firmware
 * (mes_aux InitializeLogBuffer) reads it and writes the zone header in place.
 */
#define MES_DBGEXT_ITEM_HDR_SIZE	8
#define MES_DBGEXT_MAX_ZONES		8

/*
 * mes_aux option word (struct MesExtConfig) layout, which differs from the
 * gfx11 MES_DBGEXT_INIT_DATA: bit0 is host_poll_msg (INVERTED sense - when set,
 * the firmware does not raise the per-message interrupt), bit1 enables logging,
 * and bit3 enables an internal write-back cache (left off for prompt delivery).
 */
#define MES_DBGEXT_AUX_OPT_HOST_POLL		(1ULL << 0)
#define MES_DBGEXT_AUX_OPT_ENABLE_MES_LOG	(1ULL << 1)
#define MES_DBGEXT_AUX_OPT_ENABLE_LOG_CACHE	(1ULL << 3)

struct mes_dbgext_zone_info {
	u32 offset;
	u32 length;
};

struct mes_dbgext_zone_header {
	u32 rptr;
	u32 wptr;
	u32 buffer_size;
	u32 header_size;
};

/*
 * Copy @n bytes out of the circular data region starting at byte offset @off,
 * wrapping back to @hdr_size when @buffer_size is reached.
 */
static void mes_dbgext_buf_read(const u8 *buf, u32 buffer_size, u32 hdr_size,
				u32 off, u8 *dst, u32 n)
{
	while (n--) {
		*dst++ = buf[off++];
		if (off >= buffer_size)
			off = hdr_size;
	}
}

static void mes_dbgext_print_item(struct amdgpu_device *adev, int xcc,
				  u32 type, char *text)
{
	size_t n = strlen(text);
	char pfx[12] = "";

	/* Normalize to exactly one trailing newline: the gfx11 firmware appends
	 * one to the text, the gfx12/mes_aux firmware does not.
	 */
	while (n && (text[n - 1] == '\n' || text[n - 1] == '\r'))
		text[--n] = '\0';

	/* Tag with the source XCC only when more than one is being logged, so
	 * single-XCC (gfx11/gfx12) output is unchanged.
	 */
	if (adev->mes.dbgext_num_xcc > 1)
		snprintf(pfx, sizeof(pfx), " xcc%d", xcc);

	switch (type) {
	case MES_DBGEXT_MSG_ASSERT:
		dev_err(adev->dev, "[mes_dbgext%s] ASSERT %s\n", pfx, text);
		break;
	case MES_DBGEXT_MSG:
		dev_info(adev->dev, "[mes_dbgext%s] %s\n", pfx, text);
		break;
	default:
		dev_warn(adev->dev, "[mes_dbgext%s] %s\n", pfx, text);
		break;
	}
}

/*
 * Drain a single zone of the mes_aux buffer.  @zbase points at the
 * zone start (its LOG_ZONE_HEADER); rptr/wptr are relative to @zbase.  @item is
 * caller-provided scratch of at least MES_DBGEXT_MAX_ITEM_SIZE + 1 bytes.
 */
static void mes_dbgext_process_zone(struct amdgpu_device *adev, u8 *zbase,
				    u8 *item, int xcc)
{
	struct mes_dbgext_zone_header *zh =
		(struct mes_dbgext_zone_header *)zbase;
	u32 rptr, wptr, buffer_size, hdr_size;

	buffer_size = READ_ONCE(zh->buffer_size);
	hdr_size = READ_ONCE(zh->header_size);
	rptr = READ_ONCE(zh->rptr);
	wptr = READ_ONCE(zh->wptr);

	/* Nothing to do until the firmware has written a new message. */
	if (rptr == wptr)
		return;

	/*
	 * Order the wptr load ahead of the item-body loads below.  The firmware
	 * publishes an item by writing its body first and advancing wptr last;
	 * this barrier ensures we observe the body that wptr claims is present.
	 */
	dma_rmb();

	if (hdr_size < sizeof(*zh) || buffer_size <= hdr_size ||
	    rptr < hdr_size || rptr >= buffer_size ||
	    wptr < hdr_size || wptr >= buffer_size)
		return;

	while (rptr != wptr) {
		u8 hb[MES_DBGEXT_ITEM_HDR_SIZE];
		u32 type, len;

		mes_dbgext_buf_read(zbase, buffer_size, hdr_size, rptr,
				    hb, sizeof(hb));
		type = hb[0];
		len = ((u32)hb[2] << 16) | ((u32)hb[3] << 8) | hb[4];

		/* sign = xor of all header bytes except the sign byte itself. */
		if ((u8)(hb[0] ^ hb[2] ^ hb[3] ^ hb[4] ^ hb[5] ^ hb[6]) != hb[1] ||
		    len <= MES_DBGEXT_ITEM_HDR_SIZE ||
		    len > MES_DBGEXT_MAX_ITEM_SIZE ||
		    len > buffer_size - hdr_size) {
			dev_dbg(adev->dev,
				"mes_dbgext: bad item @%u (type 0x%x len %u), skipping to %u\n",
				rptr, type, len, wptr);
			rptr = wptr;
			break;
		}

		/*
		 * Zones also carry binary event/interrupt/api records (types
		 * 0x93..0x95) whose payload is a NUL-terminated file name
		 * followed by raw struct bytes - not human-readable text.  Only
		 * print the text record types (MSG/ASSERT/HALT); consume and
		 * skip everything else so the binary records' file-name prefix
		 * is not dumped to dmesg.
		 */
		if (type == MES_DBGEXT_MSG ||
		    type == MES_DBGEXT_MSG_ASSERT ||
		    type == MES_DBGEXT_MSG_HALT) {
			mes_dbgext_buf_read(zbase, buffer_size, hdr_size, rptr,
					    item, len);
			item[len] = '\0';
			mes_dbgext_print_item(adev, xcc, type,
				(char *)item + MES_DBGEXT_ITEM_HDR_SIZE);
		}

		rptr += len;
		if (rptr >= buffer_size)
			rptr -= (buffer_size - hdr_size);
		if (rptr < hdr_size || rptr >= buffer_size) {
			rptr = wptr;
			break;
		}
	}

	/*
	 * Ensure all item-body reads complete before we publish the new rptr;
	 * otherwise the firmware may observe the advanced rptr and reuse buffer
	 * space we have not finished reading.
	 */
	dma_wmb();
	WRITE_ONCE(zh->rptr, rptr);
}

/*
 * Drain one per-XCC region: parse its zone table (a zone_count dword followed
 * by per-zone {offset,length} descriptors) and drain each zone.  @base points
 * at the region start; @xcc tags the output.
 */
static void mes_dbgext_process_region(struct amdgpu_device *adev, u8 *base, int xcc)
{
	struct amdgpu_mes *mes = &adev->mes;
	u32 zone_count, hdr_len, z;
	u8 *item;

	if (!base)
		return;

	zone_count = READ_ONCE(*(u32 *)base);
	if (zone_count == 0 || zone_count > MES_DBGEXT_MAX_ZONES)
		return;

	hdr_len = sizeof(u32) + zone_count * sizeof(struct mes_dbgext_zone_info);
	if (hdr_len >= mes->dbgext_log_size)
		return;

	/* Scratch to linearize a (possibly wrapped) item; +1 for NUL. */
	item = kmalloc(MES_DBGEXT_MAX_ITEM_SIZE + 1, GFP_KERNEL);
	if (!item)
		return;

	for (z = 0; z < zone_count; z++) {
		struct mes_dbgext_zone_info *zi =
			(struct mes_dbgext_zone_info *)(base + sizeof(u32)) + z;
		u32 zoff = READ_ONCE(zi->offset);
		u32 zlen = READ_ONCE(zi->length);

		/* Skip a bogus zone descriptor rather than the whole buffer. */
		if (zoff < hdr_len || zoff > mes->dbgext_log_size ||
		    zlen < sizeof(struct mes_dbgext_zone_header) ||
		    zlen > mes->dbgext_log_size - zoff)
			continue;

		mes_dbgext_process_zone(adev, base + zoff, item, xcc);
	}

	kfree(item);
}

static void mes_dbgext_process_all(struct amdgpu_device *adev)
{
	struct amdgpu_mes *mes = &adev->mes;
	u32 num_xcc = mes->dbgext_num_xcc ? mes->dbgext_num_xcc : 1;
	u8 *buf = mes->dbgext_log_cpu_addr;
	u32 xcc;

	if (!buf)
		return;

	/*
	 * The buffer holds num_xcc back-to-back per-XCC regions, each
	 * dbgext_log_size bytes.  Drain each region and tag its output with the
	 * source XCC (single-XCC ASICs have exactly one region).
	 */
	for (xcc = 0; xcc < num_xcc; xcc++) {
		u8 *base = buf + xcc * mes->dbgext_log_size;

		mes_dbgext_process_region(adev, base, xcc);
	}
}

static int amdgpu_mes_dbgext_reader(void *param)
{
	struct amdgpu_device *adev = param;

	while (!kthread_should_stop()) {
		mes_dbgext_process_all(adev);
		msleep_interruptible(MES_DBGEXT_POLL_INTERVAL_MS);
	}
	return 0;
}

/* Runs in process context; does the (sleepable) buffer drain. */
static void amdgpu_mes_dbgext_work_fn(struct work_struct *work)
{
	struct amdgpu_mes *mes = container_of(work, struct amdgpu_mes,
					      dbgext_work);
	struct amdgpu_device *adev =
		container_of(mes, struct amdgpu_device, mes);

	mes_dbgext_process_all(adev);
}

/*
 * Called from the MES interrupt handler (hard/soft IRQ context).  Decodes the
 * MES->host interrupt type and, for a debug-message notification, schedules the
 * drain on a workqueue (the parser sleeps / allocates, so it cannot run here).
 */
void amdgpu_mes_dbgext_notify(struct amdgpu_device *adev, u32 context_data)
{
	struct amdgpu_mes *mes = &adev->mes;

	/*
	 * Caller has already matched MES_DBGMSG (type 7 in bits 31:26 of the
	 * IH context dword).  Kick the drain; it is a no-op when the log has
	 * no new data.
	 */

	/*
	 * Only the interrupt path may schedule the drain work.  In polling mode
	 * the kthread owns rptr; honoring a stale/spurious interrupt here would
	 * let the work item and the kthread drain concurrently and race on rptr.
	 */
	if (!READ_ONCE(mes->dbgext_use_irq))
		return;

	if (READ_ONCE(mes->dbgext_log_cpu_addr))
		schedule_work(&mes->dbgext_work);
}

static int amdgpu_mes_dbgext_setup_fw(struct amdgpu_device *adev, u32 xcc_id,
				      u64 log_buffer_mc_addr, u64 log_options)
{
	struct amdgpu_mes *mes = &adev->mes;
	struct mes_misc_op_input op_input = {0};
	int r;

	if (!mes->funcs || !mes->funcs->misc_op)
		return -EINVAL;

	op_input.xcc_id = xcc_id;
	op_input.op = MES_MISC_OP_SETUP_MES_DBGEXT;
	op_input.setup_mes_dbgext.log_buffer_mc_addr = log_buffer_mc_addr;
	op_input.setup_mes_dbgext.log_options = log_options;

	amdgpu_mes_lock(mes);
	r = mes->funcs->misc_op(mes, &op_input);
	amdgpu_mes_unlock(mes);

	return r;
}

static int amdgpu_mes_dbgext_start_locked(struct amdgpu_device *adev)
{
	struct amdgpu_mes *mes = &adev->mes;
	struct task_struct *reader;
	bool use_irq;
	u64 fw_options;
	u32 size;
	u32 req_kb;
	u32 num_xcc, xcc;
	int r;

	/*
	 * Effective buffer size (KB): the module parameter wins (so a boot-time
	 * request is honored), otherwise use the runtime-requested size set by
	 * the debugfs on/off switch.  Zero means the feature is off.
	 */
	req_kb = amdgpu_mes_dbgext_buffer_size ?
		 amdgpu_mes_dbgext_buffer_size : mes->dbgext_runtime_kb;
	if (!req_kb)
		return 0;

	/*
	 * Already armed for this hw bring-up.  hw_init() can run start() more
	 * than once (e.g. kiq_hw_init() -> hw_init(), then the MES IP block's
	 * own hw_init()); only the first should arm.  A post-suspend resume
	 * comes back here with the buffer kept but dbgext_active cleared by
	 * stop(), so re-arm runs below.
	 */

	if (mes->dbgext_active)
		return 0;

	if (!mes->funcs || !mes->funcs->misc_op) {
		dev_warn(adev->dev, "mes_dbgext not supported by this MES\n");
		return 0;
	}

	/*
	 * Log every XCC's MES firmware.  Each XCC runs its own MES and gets its
	 * own per-XCC region within one buffer (see dbgext_num_xcc); single-XCC
	 * ASICs (gfx11/gfx12) collapse to a single region.
	 */
	num_xcc = adev->gfx.xcc_mask ? NUM_XCC(adev->gfx.xcc_mask) : 1;
	mes->dbgext_num_xcc = num_xcc;

	if (!mes->dbgext_log_gpu_obj) {
		/* Clamp to a sane range (4 KB .. 1 MB), per XCC. */
		size = clamp(req_kb, 4U, 1024U);
		size = ALIGN((u32)size * SZ_1K, PAGE_SIZE);
		/*
		 * The mes_aux firmware splits the buffer into per-thread zones
		 * and rejects buffers that are not larger than its 4 KB minimum,
		 * so give it at least 8 KB.
		 */
		if (size < SZ_8K)
			size = SZ_8K;

		/* One buffer holds num_xcc back-to-back per-XCC regions. */
		r = amdgpu_bo_create_kernel(adev, size * num_xcc, PAGE_SIZE,
					    AMDGPU_GEM_DOMAIN_GTT,
					    &mes->dbgext_log_gpu_obj,
					    &mes->dbgext_log_gpu_addr,
					    &mes->dbgext_log_cpu_addr);
		if (r) {
			dev_warn(adev->dev,
				 "failed to create mes_dbgext log buffer (%d)\n", r);
			return r;
		}
		mes->dbgext_log_size = size;
		INIT_WORK(&mes->dbgext_work, amdgpu_mes_dbgext_work_fn);
	} else {
		/*
		 * Resume: the log buffer is kept allocated across a suspend/
		 * resume cycle (freeing a kernel BO while suspended is not
		 * allowed), so reuse it and just re-arm the firmware below.
		 */
		size = mes->dbgext_log_size;
	}

	/*
	 * (Re)initialize each per-XCC region's header.  On resume the MES was
	 * reset and re-lays its header, and the driver's rptr/wptr must restart
	 * clean.  Clear the whole buffer once, then seed every region.
	 */
	memset(mes->dbgext_log_cpu_addr, 0, size * num_xcc);
	for (xcc = 0; xcc < num_xcc; xcc++) {
		u8 *base = (u8 *)mes->dbgext_log_cpu_addr + xcc * size;

		/*
		 * mes_aux: seed only the total region size in the first dword.
		 * The firmware (InitializeLogBuffer) reads it during setup and
		 * lays out its own zone-partitioned header in place.
		 */
		*(u32 *)base = size;
	}

	/*
	 * The options word is passed verbatim to the firmware.  bit0
	 * (trigger_interrupt_per_new_msg) selects the collection method:
	 *   set   -> firmware raises an interrupt per message; the driver
	 *            collects them via the CP EOP path (gfx_v11_0_eop_irq).
	 *   clear -> firmware only writes the buffer; the driver polls with a
	 *            kthread.
	 * If interrupts are requested but the ASIC has no CP enable hook, fall
	 * back to polling and clear the bit so the firmware does not raise an
	 * interrupt nobody will service.
	 */
	mes->dbgext_log_options = (u32)amdgpu_mes_dbgext_options;
	use_irq = (mes->dbgext_log_options & MES_DBGEXT_OPT_TRIGGER_INT_PER_MSG) &&
		  mes->funcs->enable_dbgext_irq;
	if (!use_irq)
		mes->dbgext_log_options &= ~MES_DBGEXT_OPT_TRIGGER_INT_PER_MSG;
	mes->dbgext_use_irq = use_irq;

	/*
	 * Translate the options to the mes_aux firmware layout: enable logging
	 * (bit1), select interrupt vs polling via host_poll_msg (bit0, inverted
	 * sense), and leave the write-back cache off for prompt delivery.
	 */
	fw_options = MES_DBGEXT_AUX_OPT_ENABLE_MES_LOG;
	if (!use_irq)
		fw_options |= MES_DBGEXT_AUX_OPT_HOST_POLL;

	/*
	 * Mark the feature active *before* arming the firmware.  setup_fw()
	 * below makes the firmware emit its first ("enabled") message and, in
	 * interrupt mode, immediately raise the host interrupt for it.  The EOP
	 * handler drops the drain unless dbgext_active is already set, so if it
	 * were set only after setup_fw() the enable interrupt would race the
	 * handler and be lost - and on emulation no further message/interrupt
	 * follows to recover it.  Cleared again on the error paths below.
	 */
	mes->dbgext_active = true;

	/* Enable delivery of the MES host interrupt at the CP (process ctx). */
	if (use_irq)
		mes->funcs->enable_dbgext_irq(mes, true);

	/* Point each XCC's MES firmware at its own per-XCC region. */
	for (xcc = 0; xcc < num_xcc; xcc++) {
		r = amdgpu_mes_dbgext_setup_fw(adev, xcc,
					       mes->dbgext_log_gpu_addr + xcc * size,
					       fw_options);
		if (r) {
			dev_err(adev->dev,
				"failed to setup mes_dbgext in FW on xcc%u (%d)\n",
				xcc, r);
			/* Detach the XCCs already armed before bailing. */
			while (xcc--)
				amdgpu_mes_dbgext_setup_fw(adev, xcc, 0, 0);
			goto err_disable;
		}
	}

	if (!use_irq) {
		/*
		 * Never leak a reader: if one is somehow still around (a missed
		 * stop()), reap it before creating a new one so it cannot outlive
		 * the module.
		 */
		if (WARN_ON(mes->dbgext_reader)) {
			kthread_stop(mes->dbgext_reader);
			mes->dbgext_reader = NULL;
		}

		reader = kthread_run(amdgpu_mes_dbgext_reader, adev,
				     "amdgpu_mes_dbgext");
		if (IS_ERR(reader)) {
			r = PTR_ERR(reader);
			dev_err(adev->dev,
				"failed to start mes_dbgext reader (%d)\n", r);
			goto err_fw_detach;
		}
		mes->dbgext_reader = reader;
	}

	dev_info(adev->dev,
		 "mes_dbgext enabled: %u KB x %u xcc @ 0x%llx (%s, fw options 0x%llx)\n",
		 size / SZ_1K, num_xcc, mes->dbgext_log_gpu_addr,
		 use_irq ? "interrupt" : "polling", fw_options);

	/*
	 * In interrupt mode the firmware has already written its "enabled"
	 * message (and raised the one-shot enable interrupt) during setup_fw()
	 * above.  Kick an explicit drain now so that message is collected even
	 * if that interrupt was missed, and so the log is not left waiting for
	 * the next firmware message - which, on emulation, may never come.
	 */
	if (use_irq)
		amdgpu_mes_dbgext_notify(adev, 0);

	return 0;

err_fw_detach:
	for (xcc = 0; xcc < num_xcc; xcc++)
		amdgpu_mes_dbgext_setup_fw(adev, xcc, 0, 0);
err_disable:
	/* Undo the early arming done before setup_fw(). */
	mes->dbgext_active = false;
	if (use_irq)
		mes->funcs->enable_dbgext_irq(mes, false);
	mes->dbgext_use_irq = false;
	/* Never free a kernel BO while suspended; keep it for the next start. */
	if (!adev->in_suspend) {
		amdgpu_bo_free_kernel(&mes->dbgext_log_gpu_obj,
				      &mes->dbgext_log_gpu_addr,
				      &mes->dbgext_log_cpu_addr);
		mes->dbgext_log_size = 0;
	}
	return r;
}

int amdgpu_mes_dbgext_start(struct amdgpu_device *adev)
{
	int r;

	mutex_lock(&adev->mes.dbgext_lock);
	r = amdgpu_mes_dbgext_start_locked(adev);
	mutex_unlock(&adev->mes.dbgext_lock);

	return r;
}

static void amdgpu_mes_dbgext_stop_locked(struct amdgpu_device *adev)
{
	struct amdgpu_mes *mes = &adev->mes;

	mes->dbgext_active = false;

	/*
	 * Tear down the reader kthread and the host interrupt first, and do so
	 * independently of the buffer state.  The kthread runs code that lives
	 * in this module, so it must never be left alive past module unload -
	 * otherwise it faults on an instruction fetch once the module text is
	 * freed.  A second stop() in a teardown/reset sequence, or one reached
	 * after the buffer was already freed on an error path, must still be
	 * able to reap a stale reader; hence this runs before the buffer guard.
	 */
	if (mes->dbgext_reader) {
		kthread_stop(mes->dbgext_reader);
		mes->dbgext_reader = NULL;
	}

	if (mes->dbgext_use_irq) {
		mes->funcs->enable_dbgext_irq(mes, false);
		mes->dbgext_use_irq = false;
	}

	if (!mes->dbgext_log_gpu_obj)
		return;

	/*
	 * Detach the firmware from the buffer before we free it, on every XCC we
	 * armed.  Skip this across a suspend: the MES is being torn down anyway,
	 * the buffer is kept for resume, and submitting a packet on the suspend
	 * path is unnecessary.
	 */
	if (!adev->in_suspend) {
		u32 num_xcc = mes->dbgext_num_xcc ? mes->dbgext_num_xcc : 1;
		u32 xcc;

		for (xcc = 0; xcc < num_xcc; xcc++)
			amdgpu_mes_dbgext_setup_fw(adev, xcc, 0, 0);
	}

	/* Make sure no drain work is still touching the buffer. */
	cancel_work_sync(&mes->dbgext_work);

	/* Drain anything the firmware wrote before we stopped. */
	mes_dbgext_process_all(adev);

	/*
	 * Keep the buffer allocated across a suspend/resume cycle (freeing a
	 * kernel BO while suspended is not allowed - it trips a WARN in
	 * amdgpu_bo_free_kernel()); only free it on real teardown.
	 */
	if (adev->in_suspend)
		return;

	amdgpu_bo_free_kernel(&mes->dbgext_log_gpu_obj,
			      &mes->dbgext_log_gpu_addr,
			      &mes->dbgext_log_cpu_addr);
	mes->dbgext_log_size = 0;
}

void amdgpu_mes_dbgext_stop(struct amdgpu_device *adev)
{
	mutex_lock(&adev->mes.dbgext_lock);
	amdgpu_mes_dbgext_stop_locked(adev);
	mutex_unlock(&adev->mes.dbgext_lock);
}

#if defined(CONFIG_DEBUG_FS)

static int amdgpu_debugfs_mes_event_log_show(struct seq_file *m, void *unused)
{
	struct amdgpu_device *adev = m->private;
	uint32_t *mem = (uint32_t *)(adev->mes.event_log_cpu_addr);

	seq_hex_dump(m, "", DUMP_PREFIX_OFFSET, 32, 4,
		     mem, adev->mes.event_log_size, false);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(amdgpu_debugfs_mes_event_log);

/*
 * Runtime on/off switch for the MES firmware debug extension.
 *
 *   cat  <debugfs>/amdgpu_mes_dbgext   -> current state / size / mode
 *   echo 1 > <debugfs>/amdgpu_mes_dbgext   -> enable
 *   echo 0 > <debugfs>/amdgpu_mes_dbgext   -> disable
 *
 * When enabled with no boot-time size (mes_dbgext_buffer_size=0) a default
 * buffer size is used; the collection method still follows mes_dbgext_options.
 */
static int amdgpu_debugfs_mes_dbgext_show(struct seq_file *m, void *unused)
{
	struct amdgpu_device *adev = m->private;
	struct amdgpu_mes *mes = &adev->mes;

	mutex_lock(&mes->dbgext_lock);
	seq_printf(m, "state:   %s\n", mes->dbgext_active ? "on" : "off");
	seq_printf(m, "size:    %u KB x %u xcc\n", mes->dbgext_log_size / SZ_1K,
		   mes->dbgext_num_xcc ? mes->dbgext_num_xcc : 1);
	seq_printf(m, "mode:    %s\n",
		   mes->dbgext_use_irq ? "interrupt" : "polling");
	seq_printf(m, "options: 0x%llx\n", mes->dbgext_log_options);
	mutex_unlock(&mes->dbgext_lock);

	return 0;
}

static int amdgpu_debugfs_mes_dbgext_open(struct inode *inode, struct file *file)
{
	return single_open(file, amdgpu_debugfs_mes_dbgext_show,
			   inode->i_private);
}

static ssize_t amdgpu_debugfs_mes_dbgext_write(struct file *file,
					       const char __user *buf,
					       size_t count, loff_t *ppos)
{
	struct amdgpu_device *adev =
		((struct seq_file *)file->private_data)->private;
	struct amdgpu_mes *mes = &adev->mes;
	bool enable;
	int r;

	r = kstrtobool_from_user(buf, count, &enable);
	if (r)
		return r;

	if (!mes->funcs || !mes->funcs->misc_op)
		return -EOPNOTSUPP;

	mutex_lock(&mes->dbgext_lock);
	if (enable) {
		mes->dbgext_runtime_kb = amdgpu_mes_dbgext_buffer_size ?
			amdgpu_mes_dbgext_buffer_size : MES_DBGEXT_DEFAULT_KB;
		r = amdgpu_mes_dbgext_start_locked(adev);
	} else {
		amdgpu_mes_dbgext_stop_locked(adev);
		mes->dbgext_runtime_kb = 0;
		r = 0;
	}
	mutex_unlock(&mes->dbgext_lock);

	return r ? r : count;
}

static const struct file_operations amdgpu_debugfs_mes_dbgext_fops = {
	.owner = THIS_MODULE,
	.open = amdgpu_debugfs_mes_dbgext_open,
	.read = seq_read,
	.write = amdgpu_debugfs_mes_dbgext_write,
	.llseek = seq_lseek,
	.release = single_release,
};

#endif

void amdgpu_debugfs_mes_init(struct amdgpu_device *adev)
{
#if defined(CONFIG_DEBUG_FS)
	struct drm_minor *minor = adev_to_drm(adev)->primary;
	struct dentry *root = minor->debugfs_root;

	if (!adev->enable_mes)
		return;

	if (amdgpu_mes_log_enable)
		debugfs_create_file("amdgpu_mes_event_log", 0444, root,
				    adev, &amdgpu_debugfs_mes_event_log_fops);

	debugfs_create_file("amdgpu_mes_dbgext", 0644, root,
			    adev, &amdgpu_debugfs_mes_dbgext_fops);
#endif
}
