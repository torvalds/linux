/*
 * Copyright 2016-2018 Advanced Micro Devices, Inc.
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
#include "v9_structs.h"
#include "gc/gc_9_0_offset.h"
#include "gc/gc_9_0_sh_mask.h"
#include "sdma0/sdma0_4_0_sh_mask.h"
#include "amdgpu_amdkfd.h"

static inline struct v9_mqd *get_mqd(void *mqd)
{
	return (struct v9_mqd *)mqd;
}

static inline struct v9_sdma_mqd *get_sdma_mqd(void *mqd)
{
	return (struct v9_sdma_mqd *)mqd;
}

static void update_cu_mask(struct mqd_manager *mm, void *mqd,
			struct queue_properties *q)
{
	struct v9_mqd *m;
	uint32_t se_mask[4] = {0}; /* 4 is the max # of SEs */

	if (q->cu_mask_count == 0)
		return;

	mqd_symmetrically_map_cu_mask(mm,
		q->cu_mask, q->cu_mask_count, se_mask);

	m = get_mqd(mqd);
	m->compute_static_thread_mgmt_se0 = se_mask[0];
	m->compute_static_thread_mgmt_se1 = se_mask[1];
	m->compute_static_thread_mgmt_se2 = se_mask[2];
	m->compute_static_thread_mgmt_se3 = se_mask[3];

	pr_debug("update cu mask to %#x %#x %#x %#x\n",
		m->compute_static_thread_mgmt_se0,
		m->compute_static_thread_mgmt_se1,
		m->compute_static_thread_mgmt_se2,
		m->compute_static_thread_mgmt_se3);
}

static int init_mqd(struct mqd_manager *mm, void **mqd,
			struct kfd_mem_obj **mqd_mem_obj, uint64_t *gart_addr,
			struct queue_properties *q)
{
	int retval;
	uint64_t addr;
	struct v9_mqd *m;
	struct kfd_dev *kfd = mm->dev;

	*mqd_mem_obj = NULL;
	/* From V9,  for CWSR, the control stack is located on the next page
	 * boundary after the mqd, we will use the gtt allocation function
	 * instead of sub-allocation function.
	 */
	if (kfd->cwsr_enabled && (q->type == KFD_QUEUE_TYPE_COMPUTE)) {
		*mqd_mem_obj = kzalloc(sizeof(struct kfd_mem_obj), GFP_KERNEL);
		if (!*mqd_mem_obj)
			return -ENOMEM;
		retval = amdgpu_amdkfd_alloc_gtt_mem(kfd->kgd,
			ALIGN(q->ctl_stack_size, PAGE_SIZE) +
				ALIGN(sizeof(struct v9_mqd), PAGE_SIZE),
			&((*mqd_mem_obj)->gtt_mem),
			&((*mqd_mem_obj)->gpu_addr),
			(void *)&((*mqd_mem_obj)->cpu_ptr), true);
	} else
		retval = kfd_gtt_sa_allocate(mm->dev, sizeof(struct v9_mqd),
				mqd_mem_obj);
	if (retval) {
		kfree(*mqd_mem_obj);
		return -ENOMEM;
	}

	m = (struct v9_mqd *) (*mqd_mem_obj)->cpu_ptr;
	addr = (*mqd_mem_obj)->gpu_addr;

	memset(m, 0, sizeof(struct v9_mqd));

	m->header = 0xC0310800;
	m->compute_pipelinestat_enable = 1;
	m->compute_static_thread_mgmt_se0 = 0xFFFFFFFF;
	m->compute_static_thread_mgmt_se1 = 0xFFFFFFFF;
	m->compute_static_thread_mgmt_se2 = 0xFFFFFFFF;
	m->compute_static_thread_mgmt_se3 = 0xFFFFFFFF;

	m->cp_hqd_persistent_state = CP_HQD_PERSISTENT_STATE__PRELOAD_REQ_MASK |
			0x53 << CP_HQD_PERSISTENT_STATE__PRELOAD_SIZE__SHIFT;

	m->cp_mqd_control = 1 << CP_MQD_CONTROL__PRIV_STATE__SHIFT;

	m->cp_mqd_base_addr_lo        = lower_32_bits(addr);
	m->cp_mqd_base_addr_hi        = upper_32_bits(addr);

	m->cp_hqd_quantum = 1 << CP_HQD_QUANTUM__QUANTUM_EN__SHIFT |
			1 << CP_HQD_QUANTUM__QUANTUM_SCALE__SHIFT |
			10 << CP_HQD_QUANTUM__QUANTUM_DURATION__SHIFT;

	m->cp_hqd_pipe_priority = 1;
	m->cp_hqd_queue_priority = 15;

	if (q->format == KFD_QUEUE_FORMAT_AQL) {
		m->cp_hqd_aql_control =
			1 << CP_HQD_AQL_CONTROL__CONTROL0__SHIFT;
	}

	if (q->tba_addr) {
		m->compute_pgm_rsrc2 |=
			(1 << COMPUTE_PGM_RSRC2__TRAP_PRESENT__SHIFT);
	}

	if (mm->dev->cwsr_enabled && q->ctx_save_restore_area_address) {
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
	retval = mm->update_mqd(mm, m, q);

	return retval;
}

static int load_mqd(struct mqd_manager *mm, void *mqd,
			uint32_t pipe_id, uint32_t queue_id,
			struct queue_properties *p, struct mm_struct *mms)
{
	/* AQL write pointer counts in 64B packets, PM4/CP counts in dwords. */
	uint32_t wptr_shift = (p->format == KFD_QUEUE_FORMAT_AQL ? 4 : 0);

	return mm->dev->kfd2kgd->hqd_load(mm->dev->kgd, mqd, pipe_id, queue_id,
					  (uint32_t __user *)p->write_ptr,
					  wptr_shift, 0, mms);
}

static int update_mqd(struct mqd_manager *mm, void *mqd,
		      struct queue_properties *q)
{
	struct v9_mqd *m;

	m = get_mqd(mqd);

	m->cp_hqd_pq_control = 5 << CP_HQD_PQ_CONTROL__RPTR_BLOCK_SIZE__SHIFT;
	m->cp_hqd_pq_control |= order_base_2(q->queue_size / 4) - 1;
	pr_debug("cp_hqd_pq_control 0x%x\n", m->cp_hqd_pq_control);

	m->cp_hqd_pq_base_lo = lower_32_bits((uint64_t)q->queue_address >> 8);
	m->cp_hqd_pq_base_hi = upper_32_bits((uint64_t)q->queue_address >> 8);

	m->cp_hqd_pq_rptr_report_addr_lo = lower_32_bits((uint64_t)q->read_ptr);
	m->cp_hqd_pq_rptr_report_addr_hi = upper_32_bits((uint64_t)q->read_ptr);
	m->cp_hqd_pq_wptr_poll_addr_lo = lower_32_bits((uint64_t)q->write_ptr);
	m->cp_hqd_pq_wptr_poll_addr_hi = upper_32_bits((uint64_t)q->write_ptr);

	m->cp_hqd_pq_doorbell_control =
		q->doorbell_off <<
			CP_HQD_PQ_DOORBELL_CONTROL__DOORBELL_OFFSET__SHIFT;
	pr_debug("cp_hqd_pq_doorbell_control 0x%x\n",
			m->cp_hqd_pq_doorbell_control);

	m->cp_hqd_ib_control =
		3 << CP_HQD_IB_CONTROL__MIN_IB_AVAIL_SIZE__SHIFT |
		1 << CP_HQD_IB_CONTROL__IB_EXE_DISABLE__SHIFT;

	/*
	 * HW does not clamp this field correctly. Maximum EOP queue size
	 * is constrained by per-SE EOP done signal count, which is 8-bit.
	 * Limit is 0xFF EOP entries (= 0x7F8 dwords). CP will not submit
	 * more than (EOP entry count - 1) so a queue size of 0x800 dwords
	 * is safe, giving a maximum field value of 0xA.
	 */
	m->cp_hqd_eop_control = min(0xA,
		order_base_2(q->eop_ring_buffer_size / 4) - 1);
	m->cp_hqd_eop_base_addr_lo =
			lower_32_bits(q->eop_ring_buffer_address >> 8);
	m->cp_hqd_eop_base_addr_hi =
			upper_32_bits(q->eop_ring_buffer_address >> 8);

	m->cp_hqd_iq_timer = 0;

	m->cp_hqd_vmid = q->vmid;

	if (q->format == KFD_QUEUE_FORMAT_AQL) {
		m->cp_hqd_pq_control |= CP_HQD_PQ_CONTROL__NO_UPDATE_RPTR_MASK |
				2 << CP_HQD_PQ_CONTROL__SLOT_BASED_WPTR__SHIFT |
				1 << CP_HQD_PQ_CONTROL__QUEUE_FULL_EN__SHIFT |
				1 << CP_HQD_PQ_CONTROL__WPP_CLAMP_EN__SHIFT;
		m->cp_hqd_pq_doorbell_control |= 1 <<
			CP_HQD_PQ_DOORBELL_CONTROL__DOORBELL_BIF_DROP__SHIFT;
	}
	if (mm->dev->cwsr_enabled && q->ctx_save_restore_area_address)
		m->cp_hqd_ctx_save_control = 0;

	update_cu_mask(mm, mqd, q);

	q->is_active = (q->queue_size > 0 &&
			q->queue_address != 0 &&
			q->queue_percent > 0 &&
			!q->is_evicted);

	return 0;
}


static int destroy_mqd(struct mqd_manager *mm, void *mqd,
			enum kfd_preempt_type type,
			unsigned int timeout, uint32_t pipe_id,
			uint32_t queue_id)
{
	return mm->dev->kfd2kgd->hqd_destroy
		(mm->dev->kgd, mqd, type, timeout,
		pipe_id, queue_id);
}

static void uninit_mqd(struct mqd_manager *mm, void *mqd,
			struct kfd_mem_obj *mqd_mem_obj)
{
	struct kfd_dev *kfd = mm->dev;

	if (mqd_mem_obj->gtt_mem) {
		amdgpu_amdkfd_free_gtt_mem(kfd->kgd, mqd_mem_obj->gtt_mem);
		kfree(mqd_mem_obj);
	} else {
		kfd_gtt_sa_free(mm->dev, mqd_mem_obj);
	}
}

static bool is_occupied(struct mqd_manager *mm, void *mqd,
			uint64_t queue_address,	uint32_t pipe_id,
			uint32_t queue_id)
{
	return mm->dev->kfd2kgd->hqd_is_occupied(
		mm->dev->kgd, queue_address,
		pipe_id, queue_id);
}

static int get_wave_state(struct mqd_manager *mm, void *mqd,
			  void __user *ctl_stack,
			  u32 *ctl_stack_used_size,
			  u32 *save_area_used_size)
{
	struct v9_mqd *m;

	/* Control stack is located one page after MQD. */
	void *mqd_ctl_stack = (void *)((uintptr_t)mqd + PAGE_SIZE);

	m = get_mqd(mqd);

	*ctl_stack_used_size = m->cp_hqd_cntl_stack_size -
		m->cp_hqd_cntl_stack_offset;
	*save_area_used_size = m->cp_hqd_wg_state_offset;

	if (copy_to_user(ctl_stack, mqd_ctl_stack, m->cp_hqd_cntl_stack_size))
		return -EFAULT;

	return 0;
}

static int init_mqd_hiq(struct mqd_manager *mm, void **mqd,
			struct kfd_mem_obj **mqd_mem_obj, uint64_t *gart_addr,
			struct queue_properties *q)
{
	struct v9_mqd *m;
	int retval = init_mqd(mm, mqd, mqd_mem_obj, gart_addr, q);

	if (retval != 0)
		return retval;

	m = get_mqd(*mqd);

	m->cp_hqd_pq_control |= 1 << CP_HQD_PQ_CONTROL__PRIV_STATE__SHIFT |
			1 << CP_HQD_PQ_CONTROL__KMD_QUEUE__SHIFT;

	return retval;
}

static int update_mqd_hiq(struct mqd_manager *mm, void *mqd,
			struct queue_properties *q)
{
	struct v9_mqd *m;
	int retval = update_mqd(mm, mqd, q);

	if (retval != 0)
		return retval;

	/* TODO: what's the point? update_mqd already does this. */
	m = get_mqd(mqd);
	m->cp_hqd_vmid = q->vmid;
	return retval;
}

static int init_mqd_sdma(struct mqd_manager *mm, void **mqd,
		struct kfd_mem_obj **mqd_mem_obj, uint64_t *gart_addr,
		struct queue_properties *q)
{
	int retval;
	struct v9_sdma_mqd *m;


	retval = kfd_gtt_sa_allocate(mm->dev,
			sizeof(struct v9_sdma_mqd),
			mqd_mem_obj);

	if (retval != 0)
		return -ENOMEM;

	m = (struct v9_sdma_mqd *) (*mqd_mem_obj)->cpu_ptr;

	memset(m, 0, sizeof(struct v9_sdma_mqd));

	*mqd = m;
	if (gart_addr)
		*gart_addr = (*mqd_mem_obj)->gpu_addr;

	retval = mm->update_mqd(mm, m, q);

	return retval;
}

static void uninit_mqd_sdma(struct mqd_manager *mm, void *mqd,
		struct kfd_mem_obj *mqd_mem_obj)
{
	kfd_gtt_sa_free(mm->dev, mqd_mem_obj);
}

static int load_mqd_sdma(struct mqd_manager *mm, void *mqd,
		uint32_t pipe_id, uint32_t queue_id,
		struct queue_properties *p, struct mm_struct *mms)
{
	return mm->dev->kfd2kgd->hqd_sdma_load(mm->dev->kgd, mqd,
					       (uint32_t __user *)p->write_ptr,
					       mms);
}

#define SDMA_RLC_DUMMY_DEFAULT 0xf

static int update_mqd_sdma(struct mqd_manager *mm, void *mqd,
		struct queue_properties *q)
{
	struct v9_sdma_mqd *m;

	m = get_sdma_mqd(mqd);
	m->sdmax_rlcx_rb_cntl = order_base_2(q->queue_size / 4)
		<< SDMA0_RLC0_RB_CNTL__RB_SIZE__SHIFT |
		q->vmid << SDMA0_RLC0_RB_CNTL__RB_VMID__SHIFT |
		1 << SDMA0_RLC0_RB_CNTL__RPTR_WRITEBACK_ENABLE__SHIFT |
		6 << SDMA0_RLC0_RB_CNTL__RPTR_WRITEBACK_TIMER__SHIFT;

	m->sdmax_rlcx_rb_base = lower_32_bits(q->queue_address >> 8);
	m->sdmax_rlcx_rb_base_hi = upper_32_bits(q->queue_address >> 8);
	m->sdmax_rlcx_rb_rptr_addr_lo = lower_32_bits((uint64_t)q->read_ptr);
	m->sdmax_rlcx_rb_rptr_addr_hi = upper_32_bits((uint64_t)q->read_ptr);
	m->sdmax_rlcx_doorbell_offset =
		q->doorbell_off << SDMA0_RLC0_DOORBELL_OFFSET__OFFSET__SHIFT;

	m->sdma_engine_id = q->sdma_engine_id;
	m->sdma_queue_id = q->sdma_queue_id;
	m->sdmax_rlcx_dummy_reg = SDMA_RLC_DUMMY_DEFAULT;

	q->is_active = (q->queue_size > 0 &&
			q->queue_address != 0 &&
			q->queue_percent > 0 &&
			!q->is_evicted);

	return 0;
}

/*
 *  * preempt type here is ignored because there is only one way
 *  * to preempt sdma queue
 */
static int destroy_mqd_sdma(struct mqd_manager *mm, void *mqd,
		enum kfd_preempt_type type,
		unsigned int timeout, uint32_t pipe_id,
		uint32_t queue_id)
{
	return mm->dev->kfd2kgd->hqd_sdma_destroy(mm->dev->kgd, mqd, timeout);
}

static bool is_occupied_sdma(struct mqd_manager *mm, void *mqd,
		uint64_t queue_address, uint32_t pipe_id,
		uint32_t queue_id)
{
	return mm->dev->kfd2kgd->hqd_sdma_is_occupied(mm->dev->kgd, mqd);
}

#if defined(CONFIG_DEBUG_FS)

static int debugfs_show_mqd(struct seq_file *m, void *data)
{
	seq_hex_dump(m, "    ", DUMP_PREFIX_OFFSET, 32, 4,
		     data, sizeof(struct v9_mqd), false);
	return 0;
}

static int debugfs_show_mqd_sdma(struct seq_file *m, void *data)
{
	seq_hex_dump(m, "    ", DUMP_PREFIX_OFFSET, 32, 4,
		     data, sizeof(struct v9_sdma_mqd), false);
	return 0;
}

#endif

struct mqd_manager *mqd_manager_init_v9(enum KFD_MQD_TYPE type,
		struct kfd_dev *dev)
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
	case KFD_MQD_TYPE_COMPUTE:
		mqd->init_mqd = init_mqd;
		mqd->uninit_mqd = uninit_mqd;
		mqd->load_mqd = load_mqd;
		mqd->update_mqd = update_mqd;
		mqd->destroy_mqd = destroy_mqd;
		mqd->is_occupied = is_occupied;
		mqd->get_wave_state = get_wave_state;
#if defined(CONFIG_DEBUG_FS)
		mqd->debugfs_show_mqd = debugfs_show_mqd;
#endif
		break;
	case KFD_MQD_TYPE_HIQ:
		mqd->init_mqd = init_mqd_hiq;
		mqd->uninit_mqd = uninit_mqd;
		mqd->load_mqd = load_mqd;
		mqd->update_mqd = update_mqd_hiq;
		mqd->destroy_mqd = destroy_mqd;
		mqd->is_occupied = is_occupied;
#if defined(CONFIG_DEBUG_FS)
		mqd->debugfs_show_mqd = debugfs_show_mqd;
#endif
		break;
	case KFD_MQD_TYPE_SDMA:
		mqd->init_mqd = init_mqd_sdma;
		mqd->uninit_mqd = uninit_mqd_sdma;
		mqd->load_mqd = load_mqd_sdma;
		mqd->update_mqd = update_mqd_sdma;
		mqd->destroy_mqd = destroy_mqd_sdma;
		mqd->is_occupied = is_occupied_sdma;
#if defined(CONFIG_DEBUG_FS)
		mqd->debugfs_show_mqd = debugfs_show_mqd_sdma;
#endif
		break;
	default:
		kfree(mqd);
		return NULL;
	}

	return mqd;
}
