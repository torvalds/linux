// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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

#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "kfd_priv.h"
#include "kfd_mqd_manager.h"
#include "v12_structs.h"
#include "gc/gc_12_1_0_sh_mask.h"
#include "amdgpu_amdkfd.h"
#include "kfd_device_queue_manager.h"

static inline struct v12_1_compute_mqd *get_mqd(void *mqd)
{
	return (struct v12_1_compute_mqd *)mqd;
}

static inline struct v12_sdma_mqd *get_sdma_mqd(void *mqd)
{
	return (struct v12_sdma_mqd *)mqd;
}

static void mqd_symmetrically_map_cu_mask_v12_1(struct mqd_manager *mm,
		const uint32_t *cu_mask, uint32_t cu_mask_count,
		uint32_t *se_mask, uint32_t inst)
{
	struct amdgpu_cu_info *cu_info = &mm->dev->adev->gfx.cu_info;
	struct amdgpu_gfx_config *gfx_info = &mm->dev->adev->gfx.config;
	uint32_t cu_per_sh[2][2] = {0};
	uint32_t en_mask = 0x3;
	int i, se, sh, cu, cu_inc = 0;
	uint32_t cu_active_per_node;
	int inc = NUM_XCC(mm->dev->xcc_mask);
	int xcc_inst = inst + ffs(mm->dev->xcc_mask) - 1;

	cu_active_per_node = cu_info->number / mm->dev->kfd->num_nodes;
	if (cu_mask_count > cu_active_per_node)
		cu_mask_count = cu_active_per_node;

	/*
	 * Count active CUs per SE/SH.
	 */
	for (se = 0; se < gfx_info->max_shader_engines; se++)
		for (sh = 0; sh < gfx_info->max_sh_per_se; sh++)
			cu_per_sh[se][sh] = hweight32(
				cu_info->bitmap[xcc_inst][se][sh]);

	/* Symmetrically map cu_mask to all SEs & SHs:
	 * For GFX 12.1.0, the following code only looks at a
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
	for (cu = 0; cu < 16; cu++) {
		for (sh = 0; sh < gfx_info->max_sh_per_se; sh++) {
			for (se = 0; se < gfx_info->max_shader_engines; se++) {
				if (cu_per_sh[se][sh] > cu) {
					if (cu_mask[i / 32] & (1U << (i % 32))) {
						if (cu == 8 && sh == 0)
							se_mask[se] |= en_mask << 30;
						else
							se_mask[se] |= en_mask << (cu_inc + sh * 16);
					}
					i += inc;
					if (i >= cu_mask_count)
						return;
				}
			}
		}
		cu_inc += 2;
	}
}

static void update_cu_mask(struct mqd_manager *mm, void *mqd,
			   struct mqd_update_info *minfo, uint32_t inst)
{
	struct v12_1_compute_mqd *m;
	uint32_t se_mask[2] = {0};

	if (!minfo || !minfo->cu_mask.ptr)
		return;

	mqd_symmetrically_map_cu_mask_v12_1(mm,
		minfo->cu_mask.ptr, minfo->cu_mask.count, se_mask, inst);

	m = get_mqd(mqd);
	m->compute_static_thread_mgmt_se0 = se_mask[0];
	m->compute_static_thread_mgmt_se1 = se_mask[1];

	pr_debug("update cu mask to %#x %#x\n",
		m->compute_static_thread_mgmt_se0,
		m->compute_static_thread_mgmt_se1);
}

static void set_priority(struct v12_1_compute_mqd *m, struct queue_properties *q)
{
	m->cp_hqd_pipe_priority = pipe_priority_map[q->priority];
	m->cp_hqd_queue_priority = q->priority;
}

static struct kfd_mem_obj *allocate_mqd(struct mqd_manager *mm,
		struct queue_properties *q)
{
	u32 mqd_size = AMDGPU_MQD_SIZE_ALIGN(mm->mqd_size);
	struct kfd_node *node = mm->dev;
	struct kfd_mem_obj *mqd_mem_obj;

	if (q->type == KFD_QUEUE_TYPE_COMPUTE)
		mqd_size *= NUM_XCC(node->xcc_mask);

	if (kfd_gtt_sa_allocate(node, mqd_size, &mqd_mem_obj))
		return NULL;

	return mqd_mem_obj;
}

static void init_mqd(struct mqd_manager *mm, void **mqd,
			struct kfd_mem_obj *mqd_mem_obj, uint64_t *gart_addr,
			struct queue_properties *q)
{
	uint64_t addr;
	struct v12_1_compute_mqd *m;
	u32 mqd_size = AMDGPU_MQD_SIZE_ALIGN(mm->mqd_size);

	m = (struct v12_1_compute_mqd *) mqd_mem_obj->cpu_ptr;
	addr = mqd_mem_obj->gpu_addr;

	memset(m, 0, mqd_size);

	m->header = 0xC0310800;
	m->compute_pipelinestat_enable = 1;
	m->compute_static_thread_mgmt_se0 = 0xFFFFFFFF;
	m->compute_static_thread_mgmt_se1 = 0xFFFFFFFF;
	m->compute_static_thread_mgmt_se2 = 0xFFFFFFFF;
	m->compute_static_thread_mgmt_se3 = 0xFFFFFFFF;
	m->compute_static_thread_mgmt_se4 = 0xFFFFFFFF;
	m->compute_static_thread_mgmt_se5 = 0xFFFFFFFF;
	m->compute_static_thread_mgmt_se6 = 0xFFFFFFFF;
	m->compute_static_thread_mgmt_se7 = 0xFFFFFFFF;
	m->compute_static_thread_mgmt_se8 = 0xFFFFFFFF;

	m->cp_hqd_persistent_state = CP_HQD_PERSISTENT_STATE__PRELOAD_REQ_MASK |
			0x63 << CP_HQD_PERSISTENT_STATE__PRELOAD_SIZE__SHIFT;

	m->cp_mqd_control = 1 << CP_MQD_CONTROL__PRIV_STATE__SHIFT;

	m->cp_mqd_base_addr_lo	= lower_32_bits(addr);
	m->cp_mqd_base_addr_hi	= upper_32_bits(addr);

	m->cp_hqd_quantum = 1 << CP_HQD_QUANTUM__QUANTUM_EN__SHIFT |
			1 << CP_HQD_QUANTUM__QUANTUM_SCALE__SHIFT |
			1 << CP_HQD_QUANTUM__QUANTUM_DURATION__SHIFT;

	/* Set cp_hqd_hq_status0.c_queue_debug_en to 1 to have the CP set up the
	 * DISPATCH_PTR.  This is required for the kfd debugger
	 */
	m->cp_hqd_hq_status0 = 1 << 14;

	if (amdgpu_amdkfd_have_atomics_support(mm->dev->adev))
		m->cp_hqd_hq_status0 |= 1 << 29;

	if (q->format == KFD_QUEUE_FORMAT_AQL) {
		m->cp_hqd_aql_control =
			1 << CP_HQD_AQL_CONTROL__CONTROL0__SHIFT;
	}

	if (mm->dev->kfd->cwsr_enabled) {
		m->cp_hqd_persistent_state |=
			(1 << CP_HQD_PERSISTENT_STATE__QSWITCH_MODE__SHIFT);
		m->cp_hqd_ctx_save_base_addr_lo =
			lower_32_bits(q->ctx_save_restore_area_address);
		m->cp_hqd_ctx_save_base_addr_hi =
			upper_32_bits(q->ctx_save_restore_area_address);
		m->cp_hqd_ctx_save_size = q->ctx_save_restore_area_size;
		m->cp_hqd_cntl_stack_size = q->ctl_stack_size;
		m->cp_hqd_cntl_stack_offset = q->ctl_stack_size;
		m->cp_hqd_wg_state_offset = q->ctl_stack_size;
	}

	*mqd = m;
	if (gart_addr)
		*gart_addr = addr;
	mm->update_mqd(mm, m, q, NULL);
}

static int load_mqd(struct mqd_manager *mm, void *mqd,
			uint32_t pipe_id, uint32_t queue_id,
			struct queue_properties *p, struct mm_struct *mms)
{
	int r = 0;
	/* AQL write pointer counts in 64B packets, PM4/CP counts in dwords. */
	uint32_t wptr_shift = (p->format == KFD_QUEUE_FORMAT_AQL ? 4 : 0);

	r = mm->dev->kfd2kgd->hqd_load(mm->dev->adev, mqd, pipe_id, queue_id,
					  (uint32_t __user *)p->write_ptr,
					  wptr_shift, 0, mms, 0);
	return r;
}

static void update_mqd(struct mqd_manager *mm, void *mqd,
		       struct queue_properties *q,
		       struct mqd_update_info *minfo)
{
	struct v12_1_compute_mqd *m;

	m = get_mqd(mqd);

	m->cp_hqd_pq_control = 5 << CP_HQD_PQ_CONTROL__RPTR_BLOCK_SIZE__SHIFT;
	m->cp_hqd_pq_control |=
			ffs(q->queue_size / sizeof(unsigned int)) - 1 - 1;
	m->cp_hqd_pq_control |= CP_HQD_PQ_CONTROL__UNORD_DISPATCH_MASK;
	pr_debug("cp_hqd_pq_control 0x%x\n", m->cp_hqd_pq_control);

	m->cp_hqd_pq_base_lo = lower_32_bits((uint64_t)q->queue_address >> 8);
	m->cp_hqd_pq_base_hi = upper_32_bits((uint64_t)q->queue_address >> 8);

	if (q->metadata_queue_size) {
		/* On GC 12.1 is 64 DWs which is 4 times size of AQL packet */
		if (q->metadata_queue_size == q->queue_size * 4) {
			/*
			 * User application allocates main queue ring and metadata queue ring
			 * with a single allocation. metadata queue ring starts after main
			 * queue ring.
			 */
			m->cp_hqd_kd_base =
				lower_32_bits((q->queue_address + q->queue_size) >> 8);
			m->cp_hqd_kd_base_hi =
				upper_32_bits((q->queue_address + q->queue_size) >> 8);

			m->cp_hqd_kd_cntl |= CP_HQD_KD_CNTL__KD_FETCHER_ENABLE_MASK;
			/* KD_SIZE = 2 for metadata packet = 64 DWs */
			m->cp_hqd_kd_cntl |= 2 << CP_HQD_KD_CNTL__KD_SIZE__SHIFT;
		} else {
			pr_warn("Invalid metadata ring size, metadata queue will be ignored\n");
		}
	}

	m->cp_hqd_pq_rptr_report_addr_lo = lower_32_bits((uint64_t)q->read_ptr);
	m->cp_hqd_pq_rptr_report_addr_hi = upper_32_bits((uint64_t)q->read_ptr);
	m->cp_hqd_pq_wptr_poll_addr_lo = lower_32_bits((uint64_t)q->write_ptr);
	m->cp_hqd_pq_wptr_poll_addr_hi = upper_32_bits((uint64_t)q->write_ptr);

	m->cp_hqd_pq_doorbell_control =
		q->doorbell_off <<
			CP_HQD_PQ_DOORBELL_CONTROL__DOORBELL_OFFSET__SHIFT;
	pr_debug("cp_hqd_pq_doorbell_control 0x%x\n",
			m->cp_hqd_pq_doorbell_control);

	m->cp_hqd_ib_control = 1 << CP_HQD_IB_CONTROL__MIN_IB_AVAIL_SIZE__SHIFT;

	/*
	 * HW does not clamp this field correctly. Maximum EOP queue size
	 * is constrained by per-SE EOP done signal count, which is 8-bit.
	 * Limit is 0xFF EOP entries (= 0x7F8 dwords). CP will not submit
	 * more than (EOP entry count - 1) so a queue size of 0x800 dwords
	 * is safe, giving a maximum field value of 0xA.
	 */
	m->cp_hqd_eop_control = min(0xA,
		ffs(q->eop_ring_buffer_size / sizeof(unsigned int)) - 1 - 1);
	m->cp_hqd_eop_base_addr_lo =
			lower_32_bits(q->eop_ring_buffer_address >> 8);
	m->cp_hqd_eop_base_addr_hi =
			upper_32_bits(q->eop_ring_buffer_address >> 8);

	m->cp_hqd_iq_timer = 0;

	m->cp_hqd_vmid = q->vmid;

	if (q->format == KFD_QUEUE_FORMAT_AQL) {
		/* GC 10 removed WPP_CLAMP from PQ Control */
		m->cp_hqd_pq_control |= CP_HQD_PQ_CONTROL__NO_UPDATE_RPTR_MASK |
				2 << CP_HQD_PQ_CONTROL__SLOT_BASED_WPTR__SHIFT |
				1 << CP_HQD_PQ_CONTROL__QUEUE_FULL_EN__SHIFT;
		m->cp_hqd_pq_doorbell_control |=
			1 << CP_HQD_PQ_DOORBELL_CONTROL__DOORBELL_BIF_DROP__SHIFT;
	}
	if (mm->dev->kfd->cwsr_enabled)
		m->cp_hqd_ctx_save_control = 0;

	set_priority(m, q);

	q->is_active = QUEUE_IS_ACTIVE(*q);
}

static bool check_preemption_failed(struct mqd_manager *mm, void *mqd)
{
	return false;
}

static int get_wave_state(struct mqd_manager *mm, void *mqd,
			  struct queue_properties *q,
			  void __user *ctl_stack,
			  u32 *ctl_stack_used_size,
			  u32 *save_area_used_size)
{
	struct v12_1_compute_mqd *m;
	struct mqd_user_context_save_area_header header;

	m = get_mqd(mqd);

	/* Control stack is written backwards, while workgroup context data
	 * is written forwards. Both starts from m->cp_hqd_cntl_stack_size.
	 * Current position is at m->cp_hqd_cntl_stack_offset and
	 * m->cp_hqd_wg_state_offset, respectively.
	 */
	*ctl_stack_used_size = m->cp_hqd_cntl_stack_size -
		m->cp_hqd_cntl_stack_offset;
	*save_area_used_size = m->cp_hqd_wg_state_offset -
		m->cp_hqd_cntl_stack_size;

	/* Control stack is not copied to user mode for GFXv12 because
	 * it's part of the context save area that is already
	 * accessible to user mode
	 */
	header.control_stack_size = *ctl_stack_used_size;
	header.wave_state_size = *save_area_used_size;

	header.wave_state_offset = m->cp_hqd_wg_state_offset;
	header.control_stack_offset = m->cp_hqd_cntl_stack_offset;

	if (copy_to_user(ctl_stack, &header, sizeof(header)))
		return -EFAULT;

	return 0;
}

static void init_mqd_hiq(struct mqd_manager *mm, void **mqd,
			struct kfd_mem_obj *mqd_mem_obj, uint64_t *gart_addr,
			struct queue_properties *q)
{
	struct v12_1_compute_mqd *m;

	init_mqd(mm, mqd, mqd_mem_obj, gart_addr, q);

	m = get_mqd(*mqd);

	m->cp_hqd_pq_control |= 1 << CP_HQD_PQ_CONTROL__PRIV_STATE__SHIFT |
			1 << CP_HQD_PQ_CONTROL__KMD_QUEUE__SHIFT;
}

static void init_mqd_sdma(struct mqd_manager *mm, void **mqd,
		struct kfd_mem_obj *mqd_mem_obj, uint64_t *gart_addr,
		struct queue_properties *q)
{
	struct v12_sdma_mqd *m;

	m = (struct v12_sdma_mqd *) mqd_mem_obj->cpu_ptr;

	memset(m, 0, PAGE_SIZE);

	*mqd = m;
	if (gart_addr)
		*gart_addr = mqd_mem_obj->gpu_addr;

	mm->update_mqd(mm, m, q, NULL);
}

#define SDMA_RLC_DUMMY_DEFAULT 0xf

static void update_mqd_sdma(struct mqd_manager *mm, void *mqd,
		struct queue_properties *q,
		struct mqd_update_info *minfo)
{
	struct v12_sdma_mqd *m;

	m = get_sdma_mqd(mqd);
	m->sdmax_rlcx_rb_cntl = (ffs(q->queue_size / sizeof(unsigned int)) - 1)
		<< SDMA0_SDMA_QUEUE0_RB_CNTL__RB_SIZE__SHIFT |
		q->vmid << SDMA0_SDMA_QUEUE0_RB_CNTL__RB_VMID__SHIFT |
		1 << SDMA0_SDMA_QUEUE0_RB_CNTL__RPTR_WRITEBACK_ENABLE__SHIFT |
		6 << SDMA0_SDMA_QUEUE0_RB_CNTL__RPTR_WRITEBACK_TIMER__SHIFT |
		1 << SDMA0_SDMA_QUEUE0_RB_CNTL__MCU_WPTR_POLL_ENABLE__SHIFT;

	m->sdmax_rlcx_rb_base = lower_32_bits(q->queue_address >> 8);
	m->sdmax_rlcx_rb_base_hi = upper_32_bits(q->queue_address >> 8);
	m->sdmax_rlcx_rb_rptr_addr_lo = lower_32_bits((uint64_t)q->read_ptr);
	m->sdmax_rlcx_rb_rptr_addr_hi = upper_32_bits((uint64_t)q->read_ptr);
	m->sdmax_rlcx_rb_wptr_poll_addr_lo = lower_32_bits((uint64_t)q->write_ptr);
	m->sdmax_rlcx_rb_wptr_poll_addr_hi = upper_32_bits((uint64_t)q->write_ptr);
	m->sdmax_rlcx_doorbell_offset =
		q->doorbell_off << SDMA0_SDMA_QUEUE0_DOORBELL_OFFSET__OFFSET__SHIFT;

	m->sdmax_rlcx_sched_cntl = (amdgpu_sdma_phase_quantum
		<< SDMA0_SDMA_QUEUE0_SCHEDULE_CNTL__CONTEXT_QUANTUM__SHIFT)
		 & SDMA0_SDMA_QUEUE0_SCHEDULE_CNTL__CONTEXT_QUANTUM_MASK;

	m->sdma_engine_id = q->sdma_engine_id;
	m->sdma_queue_id = q->sdma_queue_id;

	m->sdmax_rlcx_dummy_reg = SDMA_RLC_DUMMY_DEFAULT;

	/* Allow context switch so we don't cross-process starve with a massive
	* command buffer of long-running SDMA commands
	* sdmax_rlcx_ib_cntl represent SDMA_QUEUE0_IB_CNTL register
	*/
	m->sdmax_rlcx_ib_cntl |= SDMA0_SDMA_QUEUE0_IB_CNTL__SWITCH_INSIDE_IB_MASK;

	q->is_active = QUEUE_IS_ACTIVE(*q);
}

static void get_xcc_mqd(struct kfd_mem_obj *mqd_mem_obj,
			       struct kfd_mem_obj *xcc_mqd_mem_obj,
			       uint64_t offset)
{
	xcc_mqd_mem_obj->mem = (offset == 0) ?
					mqd_mem_obj->mem : NULL;
	xcc_mqd_mem_obj->gpu_addr = mqd_mem_obj->gpu_addr + offset;
	xcc_mqd_mem_obj->cpu_ptr = (uint32_t *)((uintptr_t)mqd_mem_obj->cpu_ptr
						+ offset);
}

static void init_mqd_v12_1(struct mqd_manager *mm, void **mqd,
			struct kfd_mem_obj *mqd_mem_obj, uint64_t *gart_addr,
			struct queue_properties *q)
{
	struct v12_1_compute_mqd *m;
	int xcc = 0;
	struct kfd_mem_obj xcc_mqd_mem_obj;
	uint64_t xcc_gart_addr = 0;
	uint64_t xcc_ctx_save_restore_area_address;
	uint64_t offset = mm->mqd_stride(mm, q);
	uint32_t local_xcc_start = mm->dev->dqm->current_logical_xcc_start++;

	memset(&xcc_mqd_mem_obj, 0x0, sizeof(struct kfd_mem_obj));
	for (xcc = 0; xcc < NUM_XCC(mm->dev->xcc_mask); xcc++) {
		get_xcc_mqd(mqd_mem_obj, &xcc_mqd_mem_obj, offset*xcc);

		init_mqd(mm, (void **)&m, &xcc_mqd_mem_obj, &xcc_gart_addr, q);

		m->cp_mqd_stride_size = offset;

		/*
		 * Update the CWSR address for each XCC if CWSR is enabled
		 * and CWSR area is allocated in thunk
		 */
		if (mm->dev->kfd->cwsr_enabled &&
		    q->ctx_save_restore_area_address) {
			xcc_ctx_save_restore_area_address =
				q->ctx_save_restore_area_address +
				(xcc * q->ctx_save_restore_area_size);

			m->cp_hqd_ctx_save_base_addr_lo =
				lower_32_bits(xcc_ctx_save_restore_area_address);
			m->cp_hqd_ctx_save_base_addr_hi =
				upper_32_bits(xcc_ctx_save_restore_area_address);
		}

		if (q->format == KFD_QUEUE_FORMAT_AQL) {
			m->compute_tg_chunk_size = 1;
			m->compute_current_logical_xcc_id =
					(local_xcc_start + xcc) %
					NUM_XCC(mm->dev->xcc_mask);
		} else {
			/* PM4 Queue */
			m->compute_current_logical_xcc_id = 0;
			m->compute_tg_chunk_size = 0;
			m->pm4_target_xcc_in_xcp = q->pm4_target_xcc;
		}

		if (xcc == 0) {
			/* Set the MQD pointer and gart address to XCC0 MQD */
			*mqd = m;
			*gart_addr = xcc_gart_addr;
		}
	}
}

static void update_mqd_v12_1(struct mqd_manager *mm, void *mqd,
		      struct queue_properties *q, struct mqd_update_info *minfo)
{
	struct v12_1_compute_mqd *m;
	int xcc = 0;
	uint64_t size = mm->mqd_stride(mm, q);

	for (xcc = 0; xcc < NUM_XCC(mm->dev->xcc_mask); xcc++) {
		m = get_mqd(mqd + size * xcc);
		update_mqd(mm, m, q, minfo);

		update_cu_mask(mm, m, minfo, xcc);

		if (q->format == KFD_QUEUE_FORMAT_AQL) {
			m->compute_tg_chunk_size = 1;
		} else {
			/* PM4 Queue */
			m->compute_current_logical_xcc_id = 0;
			m->compute_tg_chunk_size = 0;
			m->pm4_target_xcc_in_xcp = q->pm4_target_xcc;
		}
	}
}

static int destroy_mqd_v12_1(struct mqd_manager *mm, void *mqd,
		   enum kfd_preempt_type type, unsigned int timeout,
		   uint32_t pipe_id, uint32_t queue_id)
{
	uint32_t xcc_mask = mm->dev->xcc_mask;
	int xcc_id, err, inst = 0;
	void *xcc_mqd;
	struct v12_1_compute_mqd *m;
	uint64_t mqd_offset;

	m = get_mqd(mqd);
	mqd_offset = m->cp_mqd_stride_size;

	for_each_inst(xcc_id, xcc_mask) {
		xcc_mqd = mqd + mqd_offset * inst;
		err = mm->dev->kfd2kgd->hqd_destroy(mm->dev->adev, xcc_mqd,
						    type, timeout, pipe_id,
						    queue_id, xcc_id);
		if (err) {
			pr_debug("Destroy MQD failed for xcc: %d\n", inst);
			break;
		}
		++inst;
	}

	return err;
}

static int load_mqd_v12_1(struct mqd_manager *mm, void *mqd,
			uint32_t pipe_id, uint32_t queue_id,
			struct queue_properties *p, struct mm_struct *mms)
{
	/* AQL write pointer counts in 64B packets, PM4/CP counts in dwords. */
	uint32_t wptr_shift = (p->format == KFD_QUEUE_FORMAT_AQL ? 4 : 0);
	uint32_t xcc_mask = mm->dev->xcc_mask;
	int xcc_id, err, inst = 0;
	void *xcc_mqd;
	uint64_t mqd_stride_size = mm->mqd_stride(mm, p);

	for_each_inst(xcc_id, xcc_mask) {
		xcc_mqd = mqd + mqd_stride_size * inst;
		err = mm->dev->kfd2kgd->hqd_load(
			mm->dev->adev, xcc_mqd, pipe_id, queue_id,
			(uint32_t __user *)p->write_ptr, wptr_shift, 0, mms,
			xcc_id);
		if (err) {
			pr_debug("Load MQD failed for xcc: %d\n", inst);
			break;
		}
		++inst;
	}

	return err;
}

static int get_wave_state_v12_1(struct mqd_manager *mm, void *mqd,
				 struct queue_properties *q,
				 void __user *ctl_stack,
				 u32 *ctl_stack_used_size,
				 u32 *save_area_used_size)
{
	int xcc, err = 0;
	void *xcc_mqd;
	void __user *xcc_ctl_stack;
	uint64_t mqd_stride_size = mm->mqd_stride(mm, q);
	u32 tmp_ctl_stack_used_size = 0, tmp_save_area_used_size = 0;

	for (xcc = 0; xcc < NUM_XCC(mm->dev->xcc_mask); xcc++) {
		xcc_mqd = mqd + mqd_stride_size * xcc;
		xcc_ctl_stack = (void __user *)((uintptr_t)ctl_stack +
					q->ctx_save_restore_area_size * xcc);

		err = get_wave_state(mm, xcc_mqd, q, xcc_ctl_stack,
				     &tmp_ctl_stack_used_size,
				     &tmp_save_area_used_size);
		if (err)
			break;

		/*
		 * Set the ctl_stack_used_size and save_area_used_size to
		 * ctl_stack_used_size and save_area_used_size of XCC 0 when
		 * passing the info to user-space.
		 * For multi XCC, user-space would have to look at the header
		 * info of each Control stack area to determine the control
		 * stack size and save area used.
		 */
		if (xcc == 0) {
			*ctl_stack_used_size = tmp_ctl_stack_used_size;
			*save_area_used_size = tmp_save_area_used_size;
		}
	}

	return err;
}

#if defined(CONFIG_DEBUG_FS)

static int debugfs_show_mqd(struct seq_file *m, void *data)
{
	seq_hex_dump(m, "    ", DUMP_PREFIX_OFFSET, 32, 4,
		     data, sizeof(struct v12_1_compute_mqd), false);
	return 0;
}

static int debugfs_show_mqd_sdma(struct seq_file *m, void *data)
{
	seq_hex_dump(m, "    ", DUMP_PREFIX_OFFSET, 32, 4,
		     data, sizeof(struct v12_sdma_mqd), false);
	return 0;
}

#endif

struct mqd_manager *mqd_manager_init_v12_1(enum KFD_MQD_TYPE type,
		struct kfd_node *dev)
{
	struct mqd_manager *mqd;

	if (WARN_ON(type >= KFD_MQD_TYPE_MAX))
		return NULL;

	mqd = kzalloc(sizeof(*mqd), GFP_KERNEL);
	if (!mqd)
		return NULL;

	mqd->dev = dev;

	switch (type) {
	case KFD_MQD_TYPE_CP:
		pr_debug("%s@%i\n", __func__, __LINE__);
		mqd->allocate_mqd = allocate_mqd;
		mqd->init_mqd = init_mqd_v12_1;
		mqd->free_mqd = kfd_free_mqd_cp;
		mqd->load_mqd = load_mqd_v12_1;
		mqd->update_mqd = update_mqd_v12_1;
		mqd->destroy_mqd = destroy_mqd_v12_1;
		mqd->is_occupied = kfd_is_occupied_cp;
		mqd->mqd_size = sizeof(struct v12_1_compute_mqd);
		mqd->get_wave_state = get_wave_state_v12_1;
		mqd->mqd_stride = kfd_mqd_stride;
#if defined(CONFIG_DEBUG_FS)
		mqd->debugfs_show_mqd = debugfs_show_mqd;
#endif
		pr_debug("%s@%i\n", __func__, __LINE__);
		break;
	case KFD_MQD_TYPE_HIQ:
		pr_debug("%s@%i\n", __func__, __LINE__);
		mqd->allocate_mqd = allocate_hiq_mqd;
		mqd->init_mqd = init_mqd_hiq;
		mqd->free_mqd = free_mqd_hiq_sdma;
		mqd->load_mqd = kfd_hiq_load_mqd_kiq;
		mqd->update_mqd = update_mqd;
		mqd->destroy_mqd = kfd_destroy_mqd_cp;
		mqd->is_occupied = kfd_is_occupied_cp;
		mqd->mqd_size = sizeof(struct v12_1_compute_mqd);
		mqd->mqd_stride = kfd_mqd_stride;
#if defined(CONFIG_DEBUG_FS)
		mqd->debugfs_show_mqd = debugfs_show_mqd;
#endif
		mqd->check_preemption_failed = check_preemption_failed;
		pr_debug("%s@%i\n", __func__, __LINE__);
		break;
	case KFD_MQD_TYPE_DIQ:
		mqd->allocate_mqd = allocate_mqd;
		mqd->init_mqd = init_mqd_hiq;
		mqd->free_mqd = kfd_free_mqd_cp;
		mqd->load_mqd = load_mqd;
		mqd->update_mqd = update_mqd;
		mqd->destroy_mqd = kfd_destroy_mqd_cp;
		mqd->is_occupied = kfd_is_occupied_cp;
		mqd->mqd_size = sizeof(struct v12_1_compute_mqd);
#if defined(CONFIG_DEBUG_FS)
		mqd->debugfs_show_mqd = debugfs_show_mqd;
#endif
		break;
	case KFD_MQD_TYPE_SDMA:
		pr_debug("%s@%i\n", __func__, __LINE__);
		mqd->allocate_mqd = allocate_mqd;
		mqd->init_mqd = init_mqd_sdma;
		mqd->free_mqd = kfd_free_mqd_cp;
		mqd->load_mqd = kfd_load_mqd_sdma;
		mqd->update_mqd = update_mqd_sdma;
		mqd->destroy_mqd = kfd_destroy_mqd_sdma;
		mqd->is_occupied = kfd_is_occupied_sdma;
		mqd->mqd_size = sizeof(struct v12_sdma_mqd);
		mqd->mqd_stride = kfd_mqd_stride;
#if defined(CONFIG_DEBUG_FS)
		mqd->debugfs_show_mqd = debugfs_show_mqd_sdma;
#endif
		pr_debug("%s@%i\n", __func__, __LINE__);
		break;
	default:
		kfree(mqd);
		return NULL;
	}

	return mqd;
}
