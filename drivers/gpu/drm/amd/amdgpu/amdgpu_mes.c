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

int amdgpu_mes_init(struct amdgpu_device *adev)
{
	int i, r, num_pipes;

	adev->mes.adev = adev;

	idr_init(&adev->mes.pasid_idr);
	idr_init(&adev->mes.gang_id_idr);
	idr_init(&adev->mes.queue_id_idr);
	ida_init(&adev->mes.doorbell_ida);
	spin_lock_init(&adev->mes.queue_id_lock);
	mutex_init(&adev->mes.mutex_hidden);

	for (i = 0; i < AMDGPU_MAX_MES_PIPES; i++)
		spin_lock_init(&adev->mes.ring_lock[i]);

	adev->mes.total_max_queue = AMDGPU_FENCE_MES_QUEUE_ID_MASK;
	adev->mes.vmid_mask_mmhub = 0xffffff00;
	adev->mes.vmid_mask_gfxhub = adev->gfx.disable_kq ? 0xfffffffe : 0xffffff00;

	num_pipes = adev->gfx.me.num_pipe_per_me * adev->gfx.me.num_me;
	if (num_pipes > AMDGPU_MES_MAX_GFX_PIPES)
		dev_warn(adev->dev, "more gfx pipes than supported by MES! (%d vs %d)\n",
			 num_pipes, AMDGPU_MES_MAX_GFX_PIPES);

	for (i = 0; i < AMDGPU_MES_MAX_GFX_PIPES; i++) {
		if (i >= num_pipes)
			break;
		if (amdgpu_ip_version(adev, GC_HWIP, 0) >=
		    IP_VERSION(12, 0, 0))
			/*
			 * GFX V12 has only one GFX pipe, but 8 queues in it.
			 * GFX pipe 0 queue 0 is being used by Kernel queue.
			 * Set GFX pipe 0 queue 1-7 for MES scheduling
			 * mask = 1111 1110b
			 */
			adev->mes.gfx_hqd_mask[i] = adev->gfx.disable_kq ? 0xFF : 0xFE;
		else
			/*
			 * GFX pipe 0 queue 0 is being used by Kernel queue.
			 * Set GFX pipe 0 queue 1 for MES scheduling
			 * mask = 10b
			 */
			adev->mes.gfx_hqd_mask[i] = adev->gfx.disable_kq ? 0x3 : 0x2;
	}

	num_pipes = adev->gfx.mec.num_pipe_per_mec * adev->gfx.mec.num_mec;
	if (num_pipes > AMDGPU_MES_MAX_COMPUTE_PIPES)
		dev_warn(adev->dev, "more compute pipes than supported by MES! (%d vs %d)\n",
			 num_pipes, AMDGPU_MES_MAX_COMPUTE_PIPES);

	for (i = 0; i < AMDGPU_MES_MAX_COMPUTE_PIPES; i++) {
		if (i >= num_pipes)
			break;
		adev->mes.compute_hqd_mask[i] = adev->gfx.disable_kq ? 0xF : 0xC;
	}

	num_pipes = adev->sdma.num_instances;
	if (num_pipes > AMDGPU_MES_MAX_SDMA_PIPES)
		dev_warn(adev->dev, "more SDMA pipes than supported by MES! (%d vs %d)\n",
			 num_pipes, AMDGPU_MES_MAX_SDMA_PIPES);

	for (i = 0; i < AMDGPU_MES_MAX_SDMA_PIPES; i++) {
		if (i >= num_pipes)
			break;
		adev->mes.sdma_hqd_mask[i] = 0xfc;
	}

	for (i = 0; i < AMDGPU_MAX_MES_PIPES; i++) {
		r = amdgpu_device_wb_get(adev, &adev->mes.sch_ctx_offs[i]);
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

		r = amdgpu_device_wb_get(adev,
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

	if (adev->mes.hung_queue_db_array_size) {
		r = amdgpu_bo_create_kernel(adev,
					    adev->mes.hung_queue_db_array_size * sizeof(u32),
					    PAGE_SIZE,
					    AMDGPU_GEM_DOMAIN_GTT,
					    &adev->mes.hung_queue_db_array_gpu_obj,
					    &adev->mes.hung_queue_db_array_gpu_addr,
					    &adev->mes.hung_queue_db_array_cpu_addr);
		if (r) {
			dev_warn(adev->dev, "failed to create MES hung db array buffer (%d)", r);
			goto error_doorbell;
		}
	}

	return 0;

error_doorbell:
	amdgpu_mes_doorbell_free(adev);
error:
	for (i = 0; i < AMDGPU_MAX_MES_PIPES; i++) {
		if (adev->mes.sch_ctx_ptr[i])
			amdgpu_device_wb_free(adev, adev->mes.sch_ctx_offs[i]);
		if (adev->mes.query_status_fence_ptr[i])
			amdgpu_device_wb_free(adev,
				      adev->mes.query_status_fence_offs[i]);
	}

	idr_destroy(&adev->mes.pasid_idr);
	idr_destroy(&adev->mes.gang_id_idr);
	idr_destroy(&adev->mes.queue_id_idr);
	ida_destroy(&adev->mes.doorbell_ida);
	mutex_destroy(&adev->mes.mutex_hidden);
	return r;
}

void amdgpu_mes_fini(struct amdgpu_device *adev)
{
	int i;

	amdgpu_bo_free_kernel(&adev->mes.hung_queue_db_array_gpu_obj,
			      &adev->mes.hung_queue_db_array_gpu_addr,
			      &adev->mes.hung_queue_db_array_cpu_addr);

	amdgpu_bo_free_kernel(&adev->mes.event_log_gpu_obj,
			      &adev->mes.event_log_gpu_addr,
			      &adev->mes.event_log_cpu_addr);

	for (i = 0; i < AMDGPU_MAX_MES_PIPES; i++) {
		if (adev->mes.sch_ctx_ptr[i])
			amdgpu_device_wb_free(adev, adev->mes.sch_ctx_offs[i]);
		if (adev->mes.query_status_fence_ptr[i])
			amdgpu_device_wb_free(adev,
				      adev->mes.query_status_fence_offs[i]);
	}

	amdgpu_mes_doorbell_free(adev);

	idr_destroy(&adev->mes.pasid_idr);
	idr_destroy(&adev->mes.gang_id_idr);
	idr_destroy(&adev->mes.queue_id_idr);
	ida_destroy(&adev->mes.doorbell_ida);
	mutex_destroy(&adev->mes.mutex_hidden);
}

int amdgpu_mes_suspend(struct amdgpu_device *adev)
{
	struct mes_suspend_gang_input input;
	int r;

	if (!amdgpu_mes_suspend_resume_all_supported(adev))
		return 0;

	memset(&input, 0x0, sizeof(struct mes_suspend_gang_input));
	input.suspend_all_gangs = 1;

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

int amdgpu_mes_resume(struct amdgpu_device *adev)
{
	struct mes_resume_gang_input input;
	int r;

	if (!amdgpu_mes_suspend_resume_all_supported(adev))
		return 0;

	memset(&input, 0x0, sizeof(struct mes_resume_gang_input));
	input.resume_all_gangs = 1;

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
				struct amdgpu_ring *ring)
{
	struct mes_map_legacy_queue_input queue_input;
	int r;

	memset(&queue_input, 0, sizeof(queue_input));

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
				  u64 gpu_addr, u64 seq)
{
	struct mes_unmap_legacy_queue_input queue_input;
	int r;

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
				  bool use_mmio)
{
	struct mes_reset_queue_input queue_input;
	int r;

	memset(&queue_input, 0, sizeof(queue_input));

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

int amdgpu_mes_get_hung_queue_db_array_size(struct amdgpu_device *adev)
{
	return adev->mes.hung_queue_db_array_size;
}

int amdgpu_mes_detect_and_reset_hung_queues(struct amdgpu_device *adev,
					    int queue_type,
					    bool detect_only,
					    unsigned int *hung_db_num,
					    u32 *hung_db_array)

{
	struct mes_detect_and_reset_queue_input input;
	u32 *db_array = adev->mes.hung_queue_db_array_cpu_addr;
	int r, i;

	if (!hung_db_num || !hung_db_array)
		return -EINVAL;

	if ((queue_type != AMDGPU_RING_TYPE_GFX) &&
	    (queue_type != AMDGPU_RING_TYPE_COMPUTE) &&
	    (queue_type != AMDGPU_RING_TYPE_SDMA))
		return -EINVAL;

	/* Clear the doorbell array before detection */
	memset(adev->mes.hung_queue_db_array_cpu_addr, AMDGPU_MES_INVALID_DB_OFFSET,
		adev->mes.hung_queue_db_array_size * sizeof(u32));
	input.queue_type = queue_type;
	input.detect_only = detect_only;

	r = adev->mes.funcs->detect_and_reset_hung_queues(&adev->mes,
							  &input);
	if (r) {
		dev_err(adev->dev, "failed to detect and reset\n");
	} else {
		*hung_db_num = 0;
		for (i = 0; i < adev->mes.hung_queue_hqd_info_offset; i++) {
			if (db_array[i] != AMDGPU_MES_INVALID_DB_OFFSET) {
				hung_db_array[i] = db_array[i];
				*hung_db_num += 1;
			}
		}

		/*
		 * TODO: return HQD info for MES scheduled user compute queue reset cases
		 * stored in hung_db_array hqd info offset to full array size
		 */
	}

	return r;
}

uint32_t amdgpu_mes_rreg(struct amdgpu_device *adev, uint32_t reg)
{
	struct mes_misc_op_input op_input;
	int r, val = 0;
	uint32_t addr_offset = 0;
	uint64_t read_val_gpu_addr;
	uint32_t *read_val_ptr;

	if (amdgpu_device_wb_get(adev, &addr_offset)) {
		dev_err(adev->dev, "critical bug! too many mes readers\n");
		goto error;
	}
	read_val_gpu_addr = adev->wb.gpu_addr + (addr_offset * 4);
	read_val_ptr = (uint32_t *)&adev->wb.wb[addr_offset];
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
		amdgpu_device_wb_free(adev, addr_offset);
	return val;
}

int amdgpu_mes_wreg(struct amdgpu_device *adev,
		    uint32_t reg, uint32_t val)
{
	struct mes_misc_op_input op_input;
	int r;

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
				  uint32_t ref, uint32_t mask)
{
	struct mes_misc_op_input op_input;
	int r;

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

int amdgpu_mes_set_shader_debugger(struct amdgpu_device *adev,
				uint64_t process_context_addr,
				uint32_t spi_gdbg_per_vmid_cntl,
				const uint32_t *tcp_watch_cntl,
				uint32_t flags,
				bool trap_en)
{
	struct mes_misc_op_input op_input = {0};
	int r;

	if (!adev->mes.funcs->misc_op) {
		dev_err(adev->dev,
			"mes set shader debugger is not supported!\n");
		return -EINVAL;
	}

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
				     uint64_t process_context_addr)
{
	struct mes_misc_op_input op_input = {0};
	int r;

	if (!adev->mes.funcs->misc_op) {
		dev_err(adev->dev,
			"mes flush shader debugger is not supported!\n");
		return -EINVAL;
	}

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

bool amdgpu_mes_suspend_resume_all_supported(struct amdgpu_device *adev)
{
	uint32_t mes_rev = adev->mes.sched_version & AMDGPU_MES_VERSION_MASK;

	return ((amdgpu_ip_version(adev, GC_HWIP, 0) >= IP_VERSION(11, 0, 0) &&
		 amdgpu_ip_version(adev, GC_HWIP, 0) < IP_VERSION(12, 0, 0) &&
		 mes_rev >= 0x63) ||
		amdgpu_ip_version(adev, GC_HWIP, 0) >= IP_VERSION(12, 0, 0));
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

#endif

void amdgpu_debugfs_mes_event_log_init(struct amdgpu_device *adev)
{

#if defined(CONFIG_DEBUG_FS)
	struct drm_minor *minor = adev_to_drm(adev)->primary;
	struct dentry *root = minor->debugfs_root;
	if (adev->enable_mes && amdgpu_mes_log_enable)
		debugfs_create_file("amdgpu_mes_event_log", 0444, root,
				    adev, &amdgpu_debugfs_mes_event_log_fops);

#endif
}
